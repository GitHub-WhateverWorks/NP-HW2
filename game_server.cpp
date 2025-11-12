#include "game_server.hpp"
#include <iostream>
#include <random>
#include <chrono>
#include <algorithm>

using nlohmann::json;
using namespace std::chrono;



static const int BOARD_W = 10;
static const int BOARD_H = 20;

static const char PIECE_CHARS[7] = {'I','O','T','S','Z','J','L'};

struct Piece {
    int type = 0; 
    int x = 3;
    int y = 0;
    int rot = 0; 
};

struct ShapeRot {
    int x[4];
    int y[4];
};

static const ShapeRot SHAPES[7][4] = {
    // I
    {
        {{0,1,2,3}, {1,1,1,1}},
        {{2,2,2,2}, {0,1,2,3}},
        {{0,1,2,3}, {2,2,2,2}},
        {{1,1,1,1}, {0,1,2,3}},
    },
    // O
    {
        {{1,2,1,2}, {0,0,1,1}},
        {{1,2,1,2}, {0,0,1,1}},
        {{1,2,1,2}, {0,0,1,1}},
        {{1,2,1,2}, {0,0,1,1}},
    },
    // T
    {
        {{1,0,1,2}, {0,1,1,1}},
        {{1,1,1,2}, {0,1,2,1}},
        {{0,1,2,1}, {1,1,1,2}},
        {{0,1,1,1}, {1,0,1,2}},
    },
    // S
    {
        {{1,2,0,1}, {1,1,2,2}},
        {{1,1,2,2}, {0,1,1,2}},
        {{1,2,0,1}, {1,1,2,2}},
        {{1,1,2,2}, {0,1,1,2}},
    },
    // Z
    {
        {{0,1,1,2}, {1,1,2,2}},
        {{2,2,1,1}, {0,1,1,2}},
        {{0,1,1,2}, {1,1,2,2}},
        {{2,2,1,1}, {0,1,1,2}},
    },
    // J
    {
        {{0,0,1,2}, {0,1,1,1}},
        {{1,2,1,1}, {0,0,1,2}},
        {{0,1,2,2}, {1,1,1,0}},
        {{1,1,0,1}, {0,1,2,2}},
    },
    // L
    {
        {{2,0,1,2}, {0,1,1,1}},
        {{1,1,1,2}, {0,1,2,2}},
        {{0,1,2,0}, {1,1,1,2}},
        {{0,1,1,1}, {0,0,1,2}},
    }
};

struct PlayerState {
    int userId = -1;
    std::string role; 
    bool connected = true;
    bool alive = true;

    std::vector<int> board; 
    Piece active;
    bool hasActive = false;

    int holdType = -1;
    bool holdLocked = false;

    std::vector<int> nextQueue; 
    int score = 0;
    int lines = 0;
};

struct BagGenerator {
    std::mt19937 rng;
    std::vector<int> bag;

    BagGenerator(uint64_t seed = 0) {
        if (seed == 0) {
            seed = (uint64_t)std::random_device{}();
        }
        rng.seed(seed);
        bag.clear();
    }

    int next() {
        if (bag.empty()) {
            bag = {0,1,2,3,4,5,6};
            std::shuffle(bag.begin(), bag.end(), rng);
        }
        int t = bag.back();
        bag.pop_back();
        return t;
    }
};



GameServer::GameServer(uint16_t p,
                       int roomId_,
                       const std::string& token,
                       const std::vector<int>& playerIds)
    : port(p), roomId(roomId_), roomToken(token), expectedPlayers(playerIds)
{
    if (expectedPlayers.size() != 2) {
        throw std::runtime_error("GameServer expects exactly 2 players");
    }
}

void GameServer::enqueue_input(int userId, const std::string& action) {
    std::lock_guard<std::mutex> lock(inputMtx);
    inputQueues[userId].push(action);
}

bool GameServer::pop_input(int userId, std::string& out) {
    std::lock_guard<std::mutex> lock(inputMtx);
    auto& q = inputQueues[userId];
    if (q.empty()) return false;
    out = q.front();
    q.pop();
    return true;
}

void GameServer::wait_for_players(TcpSocket& listener) {
    std::cout << "[GameServer] Waiting for players on port " << port << "\n";

    int connectedCount = 0;

    while (connectedCount < 2) {
        TcpSocket client = listener.accept_conn();
        std::cout << "[GameServer] Incoming connection.\n";

        try {
            json hello = recv_json(client.fd());
            if (hello.value("type","") != "HELLO") {
                std::cerr << "[GameServer] First message not HELLO, closing.\n";
                continue;
            }

            int u          = hello.value("userId",-1);
            int rid        = hello.value("roomId",-1);
            std::string tk = hello.value("roomToken","");

            if (rid != roomId || tk != roomToken) {
                std::cerr << "[GameServer] HELLO mismatch, closing.\n";
                continue;
            }

            bool ok = false;
            for (int e : expectedPlayers) {
                if (e == u) { ok = true; break; }
            }
            if (!ok) {
                std::cerr << "[GameServer] Unexpected user " << u << ", closing.\n";
                continue;
            }
            if (players.count(u)) {
                std::cerr << "[GameServer] Duplicate user " << u << ", closing.\n";
                continue;
            }

            GamePlayerConn pc;
            pc.userId = u;
            pc.role   = (players.empty() ? "P1" : "P2");
            pc.ready  = true;
            pc.socket = std::move(client);

            players[u] = std::move(pc);
            connectedCount++;

            std::cout << "[GameServer] User " << u << " connected as "
                      << players[u].role << "\n";
        } catch (const std::exception& e) {
            std::cerr << "[GameServer] Error during HELLO: "
                      << e.what() << "\n";
        }
    }
}

void GameServer::send_welcome_messages(uint64_t seed, int dropMs) {
    for (auto& [uid, pc] : players) {
        json w = {
            {"type","WELCOME"},
            {"role", pc.role},
            {"seed", seed},
            {"bagRule","7bag"},
            {"gravityPlan", {{"mode","fixed"}, {"dropMs", dropMs}}},
            {"boardWidth", BOARD_W},
            {"boardHeight", BOARD_H}
        };
        try {
            send_json(pc.socket.fd(), w);
        } catch (const std::exception& e) {
            std::cerr << "[GameServer] Failed to send WELCOME to "
                      << uid << ": " << e.what() << "\n";
        }
    }
}

void GameServer::input_thread(int userId) {
    auto it = players.find(userId);
    if (it == players.end()) return;
    int fd = it->second.socket.fd();

    try {
        while (running.load()) {
            json msg = recv_json(fd);
            if (msg.value("type","") == "INPUT") {
                std::string act = msg.value("action", "");
                if (!act.empty()) {
                    enqueue_input(userId, act);
                }
            }
        }
    } catch (...) {
        enqueue_input(userId, "DISCONNECT");
    }
}

void GameServer::start_input_threads() {
    for (auto& [uid, pc] : players) {
        std::thread(&GameServer::input_thread, this, uid).detach();
    }
}


static bool piece_fits(const PlayerState& ps, const Piece& p) {
    const ShapeRot& sr = SHAPES[p.type][p.rot];
    for (int i = 0; i < 4; ++i) {
        int x = p.x + sr.x[i];
        int y = p.y + sr.y[i];
        if (x < 0 || x >= BOARD_W || y < 0 || y >= BOARD_H) return false;
        if (ps.board[y * BOARD_W + x] != 0) return false;
    }
    return true;
}

static void lock_piece(PlayerState& ps) {
    const ShapeRot& sr = SHAPES[ps.active.type][ps.active.rot];
    int color = ps.active.type + 1;
    for (int i = 0; i < 4; ++i) {
        int x = ps.active.x + sr.x[i];
        int y = ps.active.y + sr.y[i];
        if (y >= 0 && y < BOARD_H && x >= 0 && x < BOARD_W) {
            ps.board[y * BOARD_W + x] = color;
        }
    }
    ps.hasActive = false;
    ps.holdLocked = false;

    int linesCleared = 0;
    for (int y = 0; y < BOARD_H; ++y) {
        bool full = true;
        for (int x = 0; x < BOARD_W; ++x) {
            if (ps.board[y * BOARD_W + x] == 0) {
                full = false;
                break;
            }
        }
        if (full) {
            linesCleared++;
            for (int yy = y; yy > 0; --yy) {
                for (int x = 0; x < BOARD_W; ++x) {
                    ps.board[yy * BOARD_W + x] =
                        ps.board[(yy - 1) * BOARD_W + x];
                }
            }
            for (int x = 0; x < BOARD_W; ++x)
                ps.board[x] = 0;
        }
    }

    if (linesCleared > 0) {
        ps.lines += linesCleared;
        int add = 0;
        if (linesCleared == 1) add = 100;
        else if (linesCleared == 2) add = 300;
        else if (linesCleared == 3) add = 500;
        else if (linesCleared == 4) add = 800;
        ps.score += add;
    }
}

static void spawn_piece(PlayerState& ps, BagGenerator& bagGen) {
    Piece p;
    p.type = bagGen.next();
    p.rot = 0;
    p.x = 3;
    p.y = 0;

    ps.active = p;
    ps.hasActive = true;

    if (!piece_fits(ps, ps.active)) {
        ps.alive = false; 
        ps.hasActive = false;
    }
}

static void ensure_next_queue(PlayerState& ps, BagGenerator& bagGen, int want = 5) {
    while ((int)ps.nextQueue.size() < want) {
        ps.nextQueue.push_back(bagGen.next());
    }
}

static void spawn_from_queue(PlayerState& ps) {
    if (ps.nextQueue.empty()) return;
    int t = ps.nextQueue.front();
    ps.nextQueue.erase(ps.nextQueue.begin());

    Piece p;
    p.type = t;
    p.rot = 0;
    p.x = 3;
    p.y = 0;

    ps.active = p;
    ps.hasActive = true;

    if (!piece_fits(ps, ps.active)) {
        ps.alive = false;
        ps.hasActive = false;
    }
}


static void try_move(PlayerState& ps, int dx, int dy) {
    if (!ps.hasActive || !ps.alive) return;
    Piece np = ps.active;
    np.x += dx;
    np.y += dy;
    if (piece_fits(ps, np)) {
        ps.active = np;
    }
}

static void try_rotate(PlayerState& ps) {
    if (!ps.hasActive || !ps.alive) return;
    Piece np = ps.active;
    np.rot = (np.rot + 1) & 3;
    if (piece_fits(ps, np)) {
        ps.active = np;
    }
}

static void do_soft_drop(PlayerState& ps) {
    if (!ps.hasActive || !ps.alive) return;
    Piece np = ps.active;
    np.y += 1;
    if (piece_fits(ps, np)) {
        ps.active = np;
        ps.score += 1;
    } else {
        lock_piece(ps);
    }
}

static void do_hard_drop(PlayerState& ps) {
    if (!ps.hasActive || !ps.alive) return;
    Piece np = ps.active;
    int dist = 0;
    while (true) {
        Piece tmp = np;
        tmp.y += 1;
        if (piece_fits(ps, tmp)) {
            np = tmp;
            dist++;
        } else {
            break;
        }
    }
    ps.active = np;
    ps.score += dist * 2;
    lock_piece(ps);
}

static void do_hold(PlayerState& ps, BagGenerator& bagGen) {
    if (!ps.alive || !ps.hasActive || ps.holdLocked) return;

    int cur = ps.active.type;
    if (ps.holdType == -1) {
        ps.holdType = cur;
        ps.hasActive = false;
        spawn_piece(ps, bagGen);
    } else {
        std::swap(ps.holdType, cur);
        Piece p;
        p.type = cur;
        p.rot = 0;
        p.x = 3;
        p.y = 0;
        ps.active = p;
        ps.hasActive = true;
        if (!piece_fits(ps, ps.active)) {
            ps.alive = false;
            ps.hasActive = false;
        }
    }
    ps.holdLocked = true;
}



void GameServer::broadcast_snapshot(int tick,
                                    int remainingMs,
                                    const PlayerState& p1,
                                    const PlayerState& p2)
{
    auto make_snap = [&](const PlayerState& ps) {
        json j;
        j["type"] = "SNAPSHOT";
        j["tick"] = tick;
        j["userId"] = ps.userId;
        j["board"] = json::array();
        for (int v : ps.board) j["board"].push_back(v);

        if (ps.hasActive && ps.alive) {
            json act;
            act["shape"] = std::string(1, PIECE_CHARS[ps.active.type]);
            act["x"] = ps.active.x;
            act["y"] = ps.active.y;
            act["rot"] = ps.active.rot;
            j["active"] = act;
        } else {
            j["active"] = nullptr;
        }

        if (ps.holdType != -1)
            j["hold"] = std::string(1, PIECE_CHARS[ps.holdType]);
        else
            j["hold"] = nullptr;

        json nextArr = json::array();
        for (int t : ps.nextQueue) {
            nextArr.push_back(std::string(1, PIECE_CHARS[t]));
        }
        j["next"] = nextArr;

        j["score"] = ps.score;
        j["lines"] = ps.lines;
        j["alive"] = ps.alive;
        j["remainingMs"] = remainingMs;

        return j;
    };

    json s1 = make_snap(p1);
    json s2 = make_snap(p2);

    for (auto& [uid, pc] : players) {
        try {
            send_json(pc.socket.fd(), s1);
            send_json(pc.socket.fd(), s2);
        } catch (...) {
        }
    }
}

void GameServer::send_game_over(const PlayerState& p1,
                                const PlayerState& p2)
{
    // Determine result
    auto result = [&](const PlayerState& p) {
        return json{
            {"userId", p.userId},
            {"score", p.score},
            {"lines", p.lines}
        };
    };

    bool p1win = false, p2win = false;
    if (p1.lines > p2.lines) p1win = true;
    else if (p2.lines > p1.lines) p2win = true;
    else if (p1.score > p2.score) p1win = true;
    else if (p2.score > p1.score) p2win = true;

    json r1 = result(p1);
    json r2 = result(p2);
    r1["win"] = p1win && !p2win;
    r2["win"] = p2win && !p1win;

    json msg = {
        {"type","GAME_OVER"},
        {"results", json::array({r1, r2})}
    };

    for (auto& [uid, pc] : players) {
        try {
            send_json(pc.socket.fd(), msg);
        } catch (...) {}
    }
}


void GameServer::game_loop(uint64_t seed, int dropMs) {
    if (players.size() != 2) {
        std::cerr << "[GameServer] game_loop with !=2 players\n";
        return;
    }

    auto it1 = players.begin();
    auto it2 = it1; ++it2;

    PlayerState p1, p2;
    p1.userId = it1->first;
    p1.role = it1->second.role;
    p1.board.assign(BOARD_W * BOARD_H, 0);

    p2.userId = it2->first;
    p2.role = it2->second.role;
    p2.board.assign(BOARD_W * BOARD_H, 0);

    BagGenerator bag1(seed);
    BagGenerator bag2(seed);

    ensure_next_queue(p1, bag1);
    ensure_next_queue(p2, bag2);
    spawn_from_queue(p1);
    spawn_from_queue(p2);

    const int GAME_DURATION_MS = 90000; // 90s
    const int SNAPSHOT_INTERVAL_MS = 150;

    auto t0 = steady_clock::now();
    auto lastDrop = t0;
    auto lastSnap = t0;

    int tick = 0;
    running = true;

    while (running.load()) {
        auto now = steady_clock::now();
        int elapsed = (int)duration_cast<milliseconds>(now - t0).count();
        int remaining = GAME_DURATION_MS - elapsed;
        if (remaining < 0) remaining = 0;

        auto handle_inputs = [&](PlayerState& ps, BagGenerator& bag) {
            std::string act;
            while (pop_input(ps.userId, act)) {
                if (act == "DISCONNECT") {
                    ps.alive = false;
                    ps.connected = false;
                } else if (!ps.alive) {
                } else if (act == "LEFT") {
                    try_move(ps, -1, 0);
                } else if (act == "RIGHT") {
                    try_move(ps, +1, 0);
                } else if (act == "ROTATE" || act == "CW") {
                    try_rotate(ps);
                } else if (act == "SOFT") {
                    do_soft_drop(ps);
                    if (!ps.hasActive && ps.alive) {
                        ensure_next_queue(ps, bag);
                        spawn_from_queue(ps);
                    }
                } else if (act == "HARD") {
                    do_hard_drop(ps);
                    if (!ps.hasActive && ps.alive) {
                        ensure_next_queue(ps, bag);
                        spawn_from_queue(ps);
                    }
                } else if (act == "HOLD") {
                    do_hold(ps, bag);
                }
            }
        };

        handle_inputs(p1, bag1);
        handle_inputs(p2, bag2);

        // gravity
        if (duration_cast<milliseconds>(now - lastDrop).count() >= dropMs) {
            lastDrop = now;

            auto gravity_step = [&](PlayerState& ps, BagGenerator& bag) {
                if (!ps.alive) return;
                if (!ps.hasActive) {
                    ensure_next_queue(ps, bag);
                    spawn_from_queue(ps);
                    return;
                }
                Piece np = ps.active;
                np.y += 1;
                if (piece_fits(ps, np)) {
                    ps.active = np;
                } else {
                    lock_piece(ps);
                    if (ps.alive) {
                        ensure_next_queue(ps, bag);
                        spawn_from_queue(ps);
                    }
                }
            };

            gravity_step(p1, bag1);
            gravity_step(p2, bag2);
        }

        if (duration_cast<milliseconds>(now - lastSnap).count() >= SNAPSHOT_INTERVAL_MS) {
            lastSnap = now;
            broadcast_snapshot(tick, remaining, p1, p2);
        }

        bool timeUp = elapsed >= GAME_DURATION_MS;
        bool bothDead = (!p1.alive && !p2.alive);

        if (timeUp || bothDead) {
            send_game_over(p1, p2);
            running = false;
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        tick++;
    }
}

void GameServer::run() {
    try {
        TcpSocket listener;
        listener.bind_and_listen(port);
        std::cout << "[GameServer] Listening on port " << port
                  << " for room " << roomId << "\n";

        wait_for_players(listener);

        uint64_t seed = (uint64_t)std::chrono::steady_clock::now()
                            .time_since_epoch().count();
        int dropMs = 500;

        send_welcome_messages(seed, dropMs);
        start_input_threads();
        game_loop(seed, dropMs);

    } catch (const std::exception& e) {
        std::cerr << "[GameServer] Fatal error: " << e.what() << "\n";
    }
}
