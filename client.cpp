#include "client.hpp"
#include <iostream>
#include <stdexcept>
#include <thread>
#include <SFML/Graphics.hpp>
#include <thread>
#include <chrono>
#include <mutex>
#include <atomic>

using nlohmann::json;

static constexpr int T_BOARD_W = 10;
static constexpr int T_BOARD_H = 20;

struct Board {
    int width = T_BOARD_W;
    int height = T_BOARD_H;
    std::vector<int> cells; 
};

struct PlayerView {
    int userId = -1;
    bool valid = false;

    Board board;
    bool hasActive = false; 
    char activeShape = 0;
    int activeX = 0;
    int activeY = 0;
    int activeRot = 0;

    char hold = 0;
    std::vector<char> next;

    int score = 0;
    int lines = 0;
    bool alive = true;
};

struct GameViewState {
    PlayerView self;
    PlayerView opp;
    int remainingMs = 0;
    bool gameOver = false;
    std::string gameOverText;
};

static std::mutex g_game_mtx;
static GameViewState g_game;


struct ShapeRot {
    int x[4];
    int y[4];
};

static const char PIECE_CHARS[7] = {'I','O','T','S','Z','J','L'};

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

ClientApp::ClientApp(const std::string& host, uint16_t port)
    : lobbyHost(host), lobbyPort(port) {}

bool ClientApp::connect_lobby() {
    try {
        if (!lobbySock.valid()) {
            lobbySock.connect_to(lobbyHost, lobbyPort);
            std::cout << "[Client] Connected to lobby "
                      << lobbyHost << ":" << lobbyPort << "\n";
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Client] Failed to connect to lobby: "
                  << e.what() << "\n";
        return false;
    }
}

bool ClientApp::send_and_recv(const json& req, json& resp) {
    try {
        send_json(lobbySock.fd(), req);
        resp = recv_json(lobbySock.fd());
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Client] Lobby send/recv error: "
                  << e.what() << "\n";
        return false;
    }
}

// ======================= AUTH =======================

void ClientApp::handle_register() {
    std::string name, email, pw;
    std::cout << "=== Register ===\n";
    std::cout << "Name: ";
    std::getline(std::cin, name);
    std::cout << "Email (optional): ";
    std::getline(std::cin, email);
    std::cout << "Password: ";
    std::getline(std::cin, pw);

    json req = {
        {"type","REGISTER"},
        {"name",name},
        {"email",email},
        {"password",pw}
    };
    json resp;
    if (!send_and_recv(req, resp)) return;

    if (resp.value("type","") == "REGISTER_OK") {
        std::cout << "[Client] Register success.\n";
    } else {
        std::cout << "[Client] Register failed: "
                  << resp.value("reason","unknown") << "\n";
    }
}

void ClientApp::handle_login() {
    if (loggedIn) {
        std::cout << "[Client] Already logged in as "
                  << userName << ". Logout first.\n";
        return;
    }

    std::string name, pw;
    std::cout << "=== Login ===\n";
    std::cout << "Name: ";
    std::getline(std::cin, name);
    std::cout << "Password: ";
    std::getline(std::cin, pw);

    json req = {
        {"type","LOGIN"},
        {"name",name},
        {"password",pw}
    };
    json resp;
    if (!send_and_recv(req, resp)) return;

    if (resp.value("type","") == "LOGIN_OK") {
        loggedIn = true;
        userId = resp.value("userId",-1);
        sessionId = resp.value("sessionId","");
        userName = name;
        std::cout << "[Client] Login success. userId="
                  << userId << "\n";
    } else {
        std::cout << "[Client] Login failed: "
                  << resp.value("reason","unknown") << "\n";
    }
}

void ClientApp::handle_logout() {
    if (!loggedIn) {
        std::cout << "[Client] Not logged in.\n";
        return;
    }

    json req = {
        {"type","LOGOUT"},
        {"sessionId",sessionId}
    };
    json resp;
    if (!send_and_recv(req, resp)) {
        loggedIn = false;
        userId = -1;
        userName.clear();
        sessionId.clear();
        std::cout << "[Client] Logout (local) due to comms error.\n";
        return;
    }

    if (resp.value("type","") == "LOGOUT_OK") {
        std::cout << "[Client] Logged out.\n";
    } else {
        std::cout << "[Client] Logout server response: "
                  << resp.dump() << "\n";
    }

    loggedIn = false;
    userId = -1;
    userName.clear();
    sessionId.clear();
}

// ======================= ROOMS =======================

void ClientApp::handle_list_rooms() {
    if (!loggedIn) {
        std::cout << "Please login first.\n";
        return;
    }
    json req = {
        {"type","LIST_ROOMS"},
        {"sessionId",sessionId}
    };
    json resp;
    if (!send_and_recv(req, resp)) return;

    if (resp.value("type","") != "ROOMS") {
        std::cout << "[Client] LIST_ROOMS error: "
                  << resp.dump() << "\n";
        return;
    }

    std::cout << "=== Room List ===\n";
    for (auto& r : resp["rooms"]) {
        int id = r.value("id",0);
        std::string name = r.value("name","");
        std::string vis = r.value("visibility","public");
        std::string status = r.value("status","idle");
        std::cout << "Room " << id << " | " << name
                  << " | " << vis
                  << " | " << status << "\n";
    }
    std::cout << "(Note: private rooms require invite+accept.)\n";
}

void ClientApp::handle_create_room() {
    if (!loggedIn) {
        std::cout << "Please login first.\n";
        return;
    }
    std::string name, vis;
    std::cout << "=== Create Room ===\n";
    std::cout << "Room name (empty for default): ";
    std::getline(std::cin, name);
    std::cout << "Visibility (public/private, default public): ";
    std::getline(std::cin, vis);
    if (vis.empty()) vis = "public";

    json req = {
        {"type","CREATE_ROOM"},
        {"sessionId",sessionId},
        {"name",name},
        {"visibility",vis}
    };
    json resp;
    if (!send_and_recv(req, resp)) return;

    if (resp.value("type","") == "CREATE_ROOM_OK") {
        int roomId = resp.value("roomId",0);
        std::cout << "[Client] Room created. ID=" << roomId << "\n";
        room_loop(roomId, /*isHost=*/true);
    } else {
        std::cout << "[Client] Create room failed: "
                  << resp.dump() << "\n";
    }
}

void ClientApp::handle_join_room() {
    if (!loggedIn) {
        std::cout << "Please login first.\n";
        return;
    }
    std::string s;
    std::cout << "Enter roomId to join (public rooms only; "
                 "private rooms via invite+accept): ";
    std::getline(std::cin, s);
    if (s.empty()) return;
    int roomId = std::stoi(s);

    json req = {
        {"type","JOIN_ROOM"},
        {"sessionId",sessionId},
        {"roomId",roomId}
    };
    json resp;
    if (!send_and_recv(req, resp)) return;

    if (resp.value("type","") == "JOIN_ROOM_OK") {
        std::cout << "[Client] Joined room " << roomId << "\n";
        room_loop(roomId, /*isHost=*/false);
    } else {
        std::cout << "[Client] Join failed: "
                  << resp.value("reason", resp.dump()) << "\n";
    }
}

// ======================= INVITES =======================

void ClientApp::handle_list_invites() {
    if (!loggedIn) {
        std::cout << "Please login first.\n";
        return;
    }

    json req = {
        {"type","LIST_INVITES"},
        {"sessionId",sessionId}
    };
    json resp;
    if (!send_and_recv(req, resp)) return;

    if (resp.value("type","") != "INVITES") {
        std::cout << "[Client] LIST_INVITES error: "
                  << resp.dump() << "\n";
        return;
    }

    std::cout << "=== Pending Invites ===\n";
    auto arr = resp["invites"];
    if (!arr.is_array() || arr.empty()) {
        std::cout << "No invites.\n";
        return;
    }

    for (auto& inv : arr) {
        int roomId = inv.value("roomId",0);
        int fromUserId = inv.value("fromUserId",-1);
        std::string roomName = inv.value("roomName","");
        std::cout << "Room " << roomId
                  << " (" << roomName << ")"
                  << " invited by user " << fromUserId << "\n";
    }
}

void ClientApp::handle_accept_invite() {
    if (!loggedIn) {
        std::cout << "Please login first.\n";
        return;
    }

    std::string s;
    std::cout << "Enter roomId from your invites to accept: ";
    std::getline(std::cin, s);
    if (s.empty()) return;
    int roomId = std::stoi(s);

    json req = {
        {"type","ACCEPT_INVITE"},
        {"sessionId",sessionId},
        {"roomId",roomId}
    };
    json resp;
    if (!send_and_recv(req, resp)) return;

    std::string t = resp.value("type","");
    if (t == "JOIN_ROOM_OK") {
        std::cout << "[Client] Invite accepted, joined room "
                  << roomId << "\n";
        room_loop(roomId, /*isHost=*/false);
    } else if (t == "ACCEPT_INVITE_OK") {
        std::cout << "[Client] Invite accepted for room "
                  << roomId << ". Now joining...\n";
        json jreq = {
            {"type","JOIN_ROOM"},
            {"sessionId",sessionId},
            {"roomId",roomId}
        };
        json jresp;
        if (send_and_recv(jreq, jresp)
            && jresp.value("type","") == "JOIN_ROOM_OK") {
            std::cout << "[Client] Joined room " << roomId << "\n";
            room_loop(roomId, /*isHost=*/false);
        } else {
            std::cout << "[Client] Join after accept failed: "
                      << jresp.dump() << "\n";
        }
    } else {
        std::cout << "[Client] Accept invite failed: "
                  << resp.dump() << "\n";
    }
}

// ======================= ROOM LOOP =======================

void ClientApp::room_loop(int roomId, bool isHost) {

    while (true) {

            std::cout << "\n=== In Room " << roomId
                    << (isHost ? " (Host)" : "") << " ===\n";
            std::cout << "Commands:\n";
            std::cout << "  u  - list all online users\n";
            std::cout << "  r  - refresh rooms\n";
            std::cout << "  l  - list my invites\n";
            std::cout << "  ai - accept invite (enter roomId)\n";
            std::cout << "  g   - connect if game started\n";
            std::cout << "  b  - leave room\n";
            if (isHost) {
                std::cout << "  i  - invite userId to this room\n";
                std::cout << "  s  - START_GAME\n";
            }
            std::cout << "Waiting for GAME_START when room is ready...\n";
        std::cout << "> ";
        std::string cmd;
        if (!std::getline(std::cin, cmd)) break;

        if (cmd == "b") {
            json req = {
                {"type","LEAVE_ROOM"},
                {"sessionId",sessionId},
                {"roomId",roomId}
            };
            json resp;
            send_and_recv(req, resp);
            std::cout << "[Client] Leaving room.\n";
            return;
        }
        if (cmd == "u") {
            handle_list_users();
            continue;
        }
        if (cmd == "r") {
            handle_list_rooms();
            continue;
        }
        if (cmd == "l") {
            handle_list_invites();
            continue;
        }
        if (cmd == "i" && isHost) {
            handle_invite(roomId);
            continue;
        }
        if (cmd == "ai") {
            handle_accept_invite();
            continue;
        }
        if (cmd == "g") {
            handle_get_game_start(roomId);
            continue;
        }
        if (isHost && cmd == "s") {
            json req = {
                {"type","START_GAME"},
                {"sessionId",sessionId},
                {"roomId",roomId}
            };
            json resp;
            if (!send_and_recv(req, resp)) continue;

            std::string t = resp.value("type","");
            if (t == "GAME_START") {
                handle_game_start(resp);
                continue; 
            } else {
                std::cout << "[Client] START_GAME failed: "
                          << resp.dump() << "\n";
            }
            
        }
    }
}

// ======================= GAME SERVER ENTRY =======================

void ClientApp::handle_game_start(const json& msg) {
    std::string gameHost = msg.value("gameHost", "");
    uint16_t gamePort = static_cast<uint16_t>(msg.value("gamePort", 0));
    std::string roomToken = msg.value("roomToken", "");
    int roomId = msg.value("roomId", 0);

    if (gameHost.empty() || gamePort == 0 || roomToken.empty()) {
        std::cout << "[Client] Invalid GAME_START: "
                  << msg.dump() << "\n";
        return;
    }

    std::cout << "[Client] GAME_START received. Connecting to game server "
              << gameHost << ":" << gamePort << "\n";

    connect_game_server(gameHost, gamePort, roomToken, roomId);
}

static void send_game_input(int sockfd, int myUserId, const std::string& action) {
    nlohmann::json msg = {
        {"type","INPUT"},
        {"userId", myUserId},
        {"action", action}
    };
    send_json(sockfd, msg);
}

static void game_net_thread(int sockfd, int myUserId, std::atomic<bool>& running) {
    try {
        while (running.load()) {
            nlohmann::json msg = recv_json(sockfd);
            std::string type = msg.value("type","");

            if (type == "SNAPSHOT") {
                int uid = msg.value("userId",-1);

                PlayerView pv;
                pv.userId = uid;
                pv.valid = true;
                pv.board.width = T_BOARD_W;
                pv.board.height = T_BOARD_H;

                if (msg.contains("board") && msg["board"].is_array()) {
                    auto& arr = msg["board"];
                    pv.board.cells.assign(arr.begin(), arr.end());
                } else {
                    pv.board.cells.assign(T_BOARD_W*T_BOARD_H, 0);
                }

                if (msg.contains("active") && !msg["active"].is_null()) {
                    auto a = msg["active"];
                    pv.hasActive = true;
                    std::string s = a.value("shape","");
                    pv.activeShape = s.empty()?0:s[0];
                    pv.activeX = a.value("x",0);
                    pv.activeY = a.value("y",0);
                    pv.activeRot = a.value("rot",0);
                }

                if (msg.contains("hold") && !msg["hold"].is_null()) {
                    std::string hs = msg["hold"].get<std::string>();
                    pv.hold = hs.empty()?0:hs[0];
                }

                pv.next.clear();
                if (msg.contains("next") && msg["next"].is_array()) {
                    for (auto& e : msg["next"]) {
                        std::string ns = e.get<std::string>();
                        if (!ns.empty()) pv.next.push_back(ns[0]);
                    }
                }

                pv.score = msg.value("score",0);
                pv.lines = msg.value("lines",0);
                pv.alive = msg.value("alive",true);
                int rem = msg.value("remainingMs",0);

                std::lock_guard<std::mutex> lock(g_game_mtx);
                g_game.remainingMs = rem;
                if (uid == myUserId) g_game.self = pv;
                else g_game.opp = pv;
            }
            else if (type == "GAME_OVER") {
                bool iWon = false;
                std::string resultsText = "GAME OVER\n";

                if (msg.contains("results")) {
                    for (auto& r : msg["results"]) {
                        int uid  = r.value("userId", -1);
                        int sc   = r.value("score", 0);
                        int ln   = r.value("lines", 0);
                        bool win = r.value("win", false);

                        if (uid == myUserId && win)
                            iWon = true;

                        resultsText +=
                            "User " + std::to_string(uid) +
                            " lines=" + std::to_string(ln) +
                            " score=" + std::to_string(sc) +
                            (win ? " (WIN)\n" : "\n");
                    }
                }

                std::lock_guard<std::mutex> lock(g_game_mtx);
                g_game.gameOver = true;
                g_game.gameOverText =
                    (iWon ? "YOU WON!\n" : "YOU LOST.\n") + resultsText;

                running = false;
            }
        }
    } catch (...) {
        running = false;
    }
}




void ClientApp::connect_game_server(const std::string& gameHost,
                                    uint16_t gamePort,
                                    const std::string& roomToken,
                                    int roomId)
{
    try {
        TcpSocket gs;

        const int maxAttempts = 30;
        bool connected = false;
        for (int i = 0; i < maxAttempts; ++i) {
            try {
                gs.connect_to(gameHost, gamePort);
                connected = true;
                break;
            } catch (...) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        if (!connected) {
            std::cerr << "[Client] GameServer connection failed.\n";
            return;
        }

        std::cout << "[Client] Connected to GameServer.\n";

        nlohmann::json hello = {
            {"type","HELLO"},
            {"version",1},
            {"roomId",roomId},
            {"userId",userId},
            {"roomToken",roomToken}
        };
        send_json(gs.fd(), hello);

        nlohmann::json w = recv_json(gs.fd());
        if (w.value("type","") != "WELCOME") {
            std::cout << "[Client] Unexpected from GameServer: "
                      << w.dump() << "\n";
            return;
        }

        std::string role = w.value("role","?");
        int dropMs = 500;
        if (w.contains("gravityPlan"))
            dropMs = w["gravityPlan"].value("dropMs", 500);

        std::cout << "[Client] Joined game as " << role
                  << ", dropMs=" << dropMs << "\n";

        {
            std::lock_guard<std::mutex> lock(g_game_mtx);
            g_game = GameViewState{};
        }

        std::atomic<bool> running(true);
        std::thread netThread(game_net_thread, gs.fd(), userId, std::ref(running));

        sf::RenderWindow window(sf::VideoMode(800, 600), "Tetris Battle!");
        window.setFramerateLimit(60);

        const float cell = 22.f;

        while (window.isOpen() && running.load()) {
            sf::Event ev;
            while (window.pollEvent(ev)) {
                if (ev.type == sf::Event::Closed) {
                    window.close();
                    running = false;
                }
                if (ev.type == sf::Event::KeyPressed) {
                    auto code = ev.key.code;
                    if ( code == sf::Keyboard::A)
                        send_game_input(gs.fd(), userId, "LEFT");
                    else if ( code == sf::Keyboard::D)
                        send_game_input(gs.fd(), userId, "RIGHT");
                    else if (code == sf::Keyboard::W)
                        send_game_input(gs.fd(), userId, "ROTATE");
                    else if ( code == sf::Keyboard::S)
                        send_game_input(gs.fd(), userId, "SOFT");
                    else if (code == sf::Keyboard::Space)
                        send_game_input(gs.fd(), userId, "HARD");
                    else if (code == sf::Keyboard::LShift ||
                             code == sf::Keyboard::RShift)
                        send_game_input(gs.fd(), userId, "HOLD");
                }
            }

            GameViewState snap;
            {
                std::lock_guard<std::mutex> lock(g_game_mtx);
                snap = g_game;
            }

            window.clear(sf::Color::Black);

            auto draw_board = [&](const PlayerView& pv,
                                  float originX, float originY,
                                  float cellSize,
                                  bool highlightSelf)
            {
                if (!pv.valid) return;

                sf::RectangleShape bg(sf::Vector2f(
                    T_BOARD_W * cellSize + 4.f,
                    T_BOARD_H * cellSize + 4.f));
                bg.setPosition(originX - 2.f, originY - 2.f);
                bg.setFillColor(sf::Color(30,30,30));
                window.draw(bg);

                for (int y = 0; y < T_BOARD_H; ++y) {
                    for (int x = 0; x < T_BOARD_W; ++x) {
                        int v = 0;
                        if ((int)pv.board.cells.size() == T_BOARD_W * T_BOARD_H) {
                            v = pv.board.cells[y * T_BOARD_W + x];
                        }

                        sf::RectangleShape r(sf::Vector2f(
                            cellSize - 1.f,
                            cellSize - 1.f));
                        r.setPosition(originX + x * cellSize,
                                      originY + y * cellSize);

                        if (v == 0) {
                            r.setFillColor(sf::Color(0,0,0));
                        } else {
                            r.setFillColor(highlightSelf
                                ? sf::Color(80 + v*15, 160, 220)
                                : sf::Color(160, 80 + v*15, 120));
                        }
                        window.draw(r);
                    }
                }
                
                if (pv.hasActive && pv.alive && pv.activeShape != 0) {
                    int typeIdx = -1;
                    for (int i = 0; i < 7; ++i) {
                        if (PIECE_CHARS[i] == pv.activeShape) {
                            typeIdx = i;
                            break;
                        }
                    }
                    if (typeIdx >= 0) {
                        const ShapeRot& sr = SHAPES[typeIdx][pv.activeRot & 3];

                        sf::RectangleShape r(sf::Vector2f(cellSize - 1.f,
                                                        cellSize - 1.f));
                        r.setFillColor(highlightSelf
                            ? sf::Color(250, 250, 120)
                            : sf::Color(220, 140, 140));

                        for (int i = 0; i < 4; ++i) {
                            int x = pv.activeX + sr.x[i];
                            int y = pv.activeY + sr.y[i];
                            if (x < 0 || x >= T_BOARD_W || y < 0 || y >= T_BOARD_H)
                                continue;
                            r.setPosition(originX + x * cellSize,
                                        originY + y * cellSize);
                            window.draw(r);
                        }
                    }
                }
            };

            draw_board(snap.self, 60.f, 40.f, cell, true);
            draw_board(snap.opp, 460.f, 40.f, cell * 0.5f, false);

            if (snap.gameOver) {
                std::cout << snap.gameOverText << std::endl;
                running = false;
            }

            window.display();
        }

        running = false;
        if (netThread.joinable()) netThread.join();

        {
            std::lock_guard<std::mutex> lock(g_game_mtx);
            if (g_game.gameOver && !g_game.gameOverText.empty()) {
                std::cout << g_game.gameOverText << std::endl;
            }
        }

        std::cout << "[Client] Game session ended.\n";

        try {
            nlohmann::json done = {
                {"type", "GAME_FINISHED"},
                {"sessionId", sessionId},
                {"roomId", roomId}
            };
            nlohmann::json resp;
            if (!send_and_recv(done, resp)) {
                std::cout << "[Client] Failed to notify lobby about game finish.\n";
            } else if (resp.value("type","") != "OK") {
                std::cout << "[Client] Lobby GAME_FINISHED response: "
                        << resp.dump() << "\n";
            }
        } catch (...) {
            std::cout << "[Client] Exception while notifying lobby about game finish.\n";
        }

    } catch (const std::exception& e) {
        std::cerr << "[Client] GameServer error: " << e.what() << "\n";
    }
}



void ClientApp::handle_list_users() {
    if (!loggedIn) {
        std::cout << "Please login first.\n";
        return;
    }
    nlohmann::json req = {
        {"type","LIST_USERS"},
        {"sessionId",sessionId}
    };
    nlohmann::json resp;
    if (!send_and_recv(req, resp)) return;

    if (resp.value("type","") != "USERS") {
        std::cout << "[Client] LIST_USERS error: "
                  << resp.dump() << "\n";
        return;
    }

    std::cout << "=== Online Users ===\n";
    for (auto& u : resp["users"]) {
        int uid = u.value("userId",-1);
        std::string name = u.value("name","");
        std::cout << "userId=" << uid << " name=" << name << "\n";
    }
}

void ClientApp::handle_invite(int roomId) {
    if (!loggedIn) {
        std::cout << "Please login first.\n";
        return;
    }

    std::cout << "Enter target userId to invite: ";
    std::string s;
    if (!std::getline(std::cin, s) || s.empty()) return;
    int targetId = std::stoi(s);

    nlohmann::json req = {
        {"type","INVITE"},
        {"sessionId",sessionId},
        {"roomId",roomId},
        {"targetUserId",targetId}
    };
    nlohmann::json resp;
    if (!send_and_recv(req, resp)) return;

    std::string t = resp.value("type","");
    if (t == "INVITE_OK") {
        std::cout << "[Client] Invite sent to userId " << targetId << ".\n";
    } else {
        std::cout << "[Client] Invite failed: "
                  << resp.value("reason", resp.dump()) << "\n";
    }
}

void ClientApp::handle_get_game_start(int roomId) {
    if (!loggedIn) {
        std::cout << "Please login first.\n";
        return;
    }

    nlohmann::json req = {
        {"type","GET_GAME_START"},
        {"sessionId",sessionId},
        {"roomId",roomId}
    };
    nlohmann::json resp;
    if (!send_and_recv(req, resp)) return;

    if (resp.value("type","") == "GAME_START") {
        std::cout << "[Client] GAME_START info received via poll.\n";
        handle_game_start(resp); 
    } else {
        std::cout << "[Client] Game not started yet or error: "
                  << resp.dump() << "\n";
    }
}



// ======================= MAIN MENU =======================

void ClientApp::main_menu() {
    while (true) {
        std::cout << "\n=== Lobby Menu ===\n";
        std::cout << "1) Register\n";
        std::cout << "2) Login\n";
        std::cout << "3) Logout\n";
        std::cout << "4) List Rooms\n";
        std::cout << "5) Create Room\n";
        std::cout << "6) Join Public Room\n";
        std::cout << "7) List Invites\n";
        std::cout << "8) Accept Invite\n";
        std::cout << "q) Quit\n";
        std::cout << "> ";

        std::string choice;
        if (!std::getline(std::cin, choice)) break;

        if (choice == "1") handle_register();
        else if (choice == "2") handle_login();
        else if (choice == "3") handle_logout();
        else if (choice == "4") handle_list_rooms();
        else if (choice == "5") handle_create_room();
        else if (choice == "6") handle_join_room();
        else if (choice == "7") handle_list_invites();
        else if (choice == "8") handle_accept_invite();
        else if (choice == "q") break;
    }
}

void ClientApp::run() {
    if (!connect_lobby()) return;
    main_menu();
}
