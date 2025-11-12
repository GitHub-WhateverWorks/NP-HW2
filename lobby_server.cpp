#include "lobby_server.hpp"

#include <iostream>
#include <random>
#include <chrono>
#include <algorithm>
#include <unistd.h> 

using nlohmann::json;

LobbyServer::LobbyServer(uint16_t p,
                         const std::string& dbHost,
                         uint16_t dbPort)
    : port(p), db(dbHost, dbPort) {}

// ============ utility ============

std::string LobbyServer::gen_session_id() {
    static std::mt19937_64 rng(std::random_device{}());
    static const char* chars = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::string s(32, 'a');
    for (auto& ch : s) ch = chars[rng() % 36];
    return s;
}

int LobbyServer::gen_room_id() {
    static int nextId = 1;
    return nextId++;
}

std::string LobbyServer::gen_token(int len) {
    static std::mt19937_64 rng(std::random_device{}());
    static const char* chars =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789";
    std::string s(len, 'a');
    for (auto& c : s) c = chars[rng() % 62];
    return s;
}

bool LobbyServer::check_session(const std::string& sessionId, SessionInfo& out) {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = sessions.find(sessionId);
    if (it == sessions.end()) return false;
    out = it->second;
    return true;
}

void LobbyServer::cleanup_session_by_fd(int fd) {
    std::lock_guard<std::mutex> lock(mtx);
    std::string deadSession;
    int deadUserId = -1;

    for (auto& [sid, s] : sessions) {
        if (s.fd == fd) {
            deadSession = sid;
            deadUserId = s.userId;
            break;
        }
    }

    if (!deadSession.empty()) {
        sessions.erase(deadSession);
    }
    if (deadUserId != -1) {
        userIdToSession.erase(deadUserId);

        for (auto it = rooms.begin(); it != rooms.end(); ) {
            RoomState& r = it->second;
            bool changed = false;

            auto pit = std::find(r.players.begin(), r.players.end(), deadUserId);
            if (pit != r.players.end()) {
                r.players.erase(pit);
                changed = true;
            }

            if (r.players.empty() || r.hostUserId == deadUserId) {
                int roomId = r.roomId;
                db.del("Room", {{"id", roomId}});
                it = rooms.erase(it);
                continue;
            } else if (changed) {
                db.update("Room", {{"id", r.roomId}}, {{"players", r.players}});
            }
            ++it;
        }
    }
}

int LobbyServer::allocate_game_port() {

    int port = nextGamePort;
    nextGamePort += 1;
    if (nextGamePort > 30000) nextGamePort = 20000;
    return port;
}

void LobbyServer::push_message_to_user(int userId, const json& msg) {

    auto itS = userIdToSession.find(userId);
    if (itS == userIdToSession.end()) return;
    const std::string& sid = itS->second;
    auto it = sessions.find(sid);
    if (it == sessions.end()) return;
    int fd = it->second.fd;
    try {
        send_json(fd, msg);
    } catch (...) {
    }
}

// ============ handlers: auth ============

json LobbyServer::handle_register(const json& msg) {
    std::string name  = msg.value("name", "");
    std::string email = msg.value("email", "");
    std::string pw    = msg.value("password", "");
    if (name.empty() || pw.empty()) {
        return {{"type","REGISTER_FAIL"},{"reason","missing name/password"}};
    }

    json r = db.read("User", {{"name", name}});
    if (r["status"] == "ok" && !r["data"].is_null()) {
        return {{"type","REGISTER_FAIL"},{"reason","name taken"}};
    }

    json user = {
        {"name", name},
        {"email", email},
        {"passwordHash", pw}, 
        {"createdAt", (long long)std::time(nullptr)},
        {"lastLoginAt", nullptr}
    };
    json cr = db.create("User", user);
    if (cr["status"] != "ok") {
        return {{"type","REGISTER_FAIL"},{"reason","db error"}};
    }
    return {{"type","REGISTER_OK"}};
}

json LobbyServer::handle_login(const json& msg, int clientFd) {
    std::string name = msg.value("name", "");
    std::string pw   = msg.value("password", "");
    if (name.empty() || pw.empty()) {
        return {{"type","LOGIN_FAIL"},{"reason","missing name/password"}};
    }

    json r = db.read("User", {{"name", name}});
    if (r["status"] != "ok" || r["data"].is_null()) {
        return {{"type","LOGIN_FAIL"},{"reason","no such user"}};
    }
    json user = r["data"];
    if (user.value("passwordHash", "") != pw) {
        return {{"type","LOGIN_FAIL"},{"reason","wrong password"}};
    }

    int userId = user["id"].get<int>();
    {
        std::lock_guard<std::mutex> lock(mtx);

        auto itExisting = userIdToSession.find(userId);
        if (itExisting != userIdToSession.end()) {
            return {
                {"type","LOGIN_FAIL"},
                {"reason","user already logged in"}
            };
        }

        db.update("User", {{"id", userId}},
                {{"lastLoginAt", (long long)std::time(nullptr)}});

        std::string sessionId = gen_session_id();
        SessionInfo s{userId, name, clientFd};
        sessions[sessionId] = s;
        userIdToSession[userId] = sessionId;

        return {
            {"type","LOGIN_OK"},
            {"userId", userId},
            {"sessionId", sessionId}
        };
    }
}

json LobbyServer::handle_logout(const json& msg) {
    std::string sessionId = msg.value("sessionId", "");
    std::lock_guard<std::mutex> lock(mtx);

    auto it = sessions.find(sessionId);
    if (it == sessions.end()) {
        return {{"type","LOGOUT_OK"}}; 
    }

    int uid = it->second.userId;
    sessions.erase(it);
    userIdToSession.erase(uid);

    for (auto rIt = rooms.begin(); rIt != rooms.end(); ) {
        RoomState& r = rIt->second;
        bool changed = false;

        auto pit = std::find(r.players.begin(), r.players.end(), uid);
        if (pit != r.players.end()) {
            r.players.erase(pit);
            changed = true;
        }

        if (r.players.empty() || r.hostUserId == uid) {
            int roomId = r.roomId;
            db.del("Room", {{"id", roomId}});
            rIt = rooms.erase(rIt);
            continue;
        } else if (changed) {
            db.update("Room", {{"id", r.roomId}}, {{"players", r.players}});
        }
        ++rIt;
    }

    return {{"type","LOGOUT_OK"}};
}

// ============ handlers: listing ============

json LobbyServer::handle_list_users(const json& msg) {
    std::string sessionId = msg.value("sessionId", "");
    SessionInfo me;
    if (!check_session(sessionId, me)) {
        return {{"type","ERROR"},{"reason","invalid session"}};
    }

    json arr = json::array();
    std::lock_guard<std::mutex> lock(mtx);
    for (auto& [sid, s] : sessions) {
        arr.push_back({{"userId", s.userId}, {"name", s.name}});
    }
    return {{"type","USERS"},{"users",arr}};
}

json LobbyServer::handle_list_rooms(const json& msg) {
    std::string sessionId = msg.value("sessionId", "");
    SessionInfo me;
    if (!check_session(sessionId, me)) {
        return {{"type","ERROR"},{"reason","invalid session"}};
    }

    json arr = json::array();
    std::lock_guard<std::mutex> lock(mtx);
    for (auto& [rid, r] : rooms) {
        arr.push_back({
            {"id", r.roomId},
            {"name", r.name},
            {"hostUserId", r.hostUserId},
            {"visibility", r.visibility},
            {"status", r.status},
            {"players", r.players}
        });
    }
    return {{"type","ROOMS"},{"rooms",arr}};
}

// ============ handlers: rooms ============

json LobbyServer::handle_create_room(const json& msg) {
    std::string sessionId = msg.value("sessionId", "");
    SessionInfo me;
    if (!check_session(sessionId, me)) {
        return {{"type","ERROR"},{"reason","invalid session"}};
    }

    std::string name = msg.value("name", me.name + "'s room");
    std::string visibility = msg.value("visibility", "public");

    int roomId = gen_room_id();

    RoomState rs;
    rs.roomId = roomId;
    rs.name = name;
    rs.hostUserId = me.userId;
    rs.visibility = visibility;
    rs.status = "idle";
    rs.players = { me.userId };

    {
        std::lock_guard<std::mutex> lock(mtx);
        rooms[roomId] = rs;
    }

    json roomDoc = {
        {"id", roomId},
        {"name", name},
        {"hostUserId", me.userId},
        {"visibility", visibility},
        {"status", "idle"},
        {"players", rs.players},
        {"createdAt", (long long)std::time(nullptr)}
    };
    db.create("Room", roomDoc);

    return {{"type","CREATE_ROOM_OK"},{"roomId",roomId}};
}

json LobbyServer::handle_join_room(const json& msg) {
    std::string sessionId = msg.value("sessionId", "");
    int roomId = msg.value("roomId", 0);
    SessionInfo me;
    if (!check_session(sessionId, me)) {
        return {{"type","ERROR"},{"reason","invalid session"}};
    }
    if (roomId == 0) {
        return {{"type","ERROR"},{"reason","missing roomId"}};
    }

    std::lock_guard<std::mutex> lock(mtx);
    auto it = rooms.find(roomId);
    if (it == rooms.end()) {
        return {{"type","ERROR"},{"reason","no such room"}};
    }
    RoomState& r = it->second;

    if (r.visibility == "private") {
        return {{"type","ERROR"},{"reason","room is private"}};
    }
    if (r.players.size() >= 2) {
        return {{"type","ERROR"},{"reason","room full"}};
    }

    if (std::find(r.players.begin(), r.players.end(), me.userId) == r.players.end()) {
        r.players.push_back(me.userId);
    }

    db.update("Room", {{"id", roomId}}, {{"players", r.players}});
    return {
        {"type","JOIN_ROOM_OK"},
        {"roomId",roomId},
        {"players",r.players}
    };
}

json LobbyServer::handle_leave_room(const json& msg) {
    std::string sessionId = msg.value("sessionId", "");
    int roomId = msg.value("roomId", 0);
    SessionInfo me;
    if (!check_session(sessionId, me)) {
        return {{"type","ERROR"},{"reason","invalid session"}};
    }

    std::lock_guard<std::mutex> lock(mtx);
    auto it = rooms.find(roomId);
    if (it == rooms.end()) {
        return {{"type","ERROR"},{"reason","no such room"}};
    }
    RoomState& r = it->second;

    auto pit = std::find(r.players.begin(), r.players.end(), me.userId);
    if (pit != r.players.end()) {
        r.players.erase(pit);
    }

    if (r.players.empty() || me.userId == r.hostUserId) {
        rooms.erase(it);
        db.del("Room", {{"id", roomId}});
        return {{"type","LEAVE_ROOM_OK"},{"roomDeleted",true}};
    } else {
        db.update("Room", {{"id", roomId}}, {{"players", r.players}});
        return {{"type","LEAVE_ROOM_OK"},{"roomDeleted",false}};
    }
}

// ============ handlers: invites ============

json LobbyServer::handle_list_invites(const json& msg) {
    std::string sessionId = msg.value("sessionId", "");
    SessionInfo me;
    if (!check_session(sessionId, me)) {
        return {{"type","ERROR"},{"reason","invalid session"}};
    }

    std::lock_guard<std::mutex> lock(mtx);
    json arr = json::array();
    auto it = invitesByUser.find(me.userId);
    if (it != invitesByUser.end()) {
        for (auto& inv : it->second) {
            arr.push_back({
                {"roomId", inv.roomId},
                {"fromUserId", inv.fromUserId},
                {"roomName", inv.roomName}
            });
        }
    }

    return {{"type","INVITES"},{"invites",arr}};
}

json LobbyServer::handle_invite(const json& msg) {
    std::string sessionId = msg.value("sessionId", "");
    int roomId = msg.value("roomId", 0);
    int targetUserId = msg.value("targetUserId", 0);

    if (roomId == 0 || targetUserId == 0) {
        return {{"type","ERROR"},{"reason","missing roomId/targetUserId"}};
    }

    std::lock_guard<std::mutex> lock(mtx);

    auto itSess = sessions.find(sessionId);
    if (itSess == sessions.end()) {
        return {{"type","ERROR"},{"reason","invalid session"}};
    }
    const SessionInfo& me = itSess->second;
    auto it = rooms.find(roomId);
    if (it == rooms.end()) {
        return {{"type","ERROR"},{"reason","no such room"}};
    }
    RoomState& r = it->second;

    if (r.hostUserId != me.userId) {
        return {{"type","ERROR"},{"reason","only host can invite"}};
    }
    if (r.players.size() >= 2) {
        return {{"type","ERROR"},{"reason","room full"}};
    }
    if (std::find(r.players.begin(), r.players.end(), targetUserId) != r.players.end()) {
        return {{"type","ERROR"},{"reason","user already in room"}};
    }

    Invite inv{roomId, me.userId, r.name};
    invitesByUser[targetUserId].push_back(inv);

    return {{"type","INVITE_OK"}};

}

json LobbyServer::handle_accept_invite(const json& msg) {
    std::string sessionId = msg.value("sessionId", "");
    int roomId = msg.value("roomId", 0);

    SessionInfo me;
    if (!check_session(sessionId, me)) {
        return {{"type","ERROR"},{"reason","invalid session"}};
    }
    if (roomId == 0) {
        return {{"type","ERROR"},{"reason","missing roomId"}};
    }

    std::lock_guard<std::mutex> lock(mtx);

    auto itInv = invitesByUser.find(me.userId);
    if (itInv == invitesByUser.end()) {
        return {{"type","ERROR"},{"reason","no invites"}};
    }

    auto& vec = itInv->second;
    auto it = std::find_if(vec.begin(), vec.end(),
                           [&](const Invite& inv){ return inv.roomId == roomId; });
    if (it == vec.end()) {
        return {{"type","ERROR"},{"reason","no such invite for this room"}};
    }

    auto itRoom = rooms.find(roomId);
    if (itRoom == rooms.end()) {
        vec.erase(it);
        return {{"type","ERROR"},{"reason","room no longer exists"}};
    }
    RoomState& r = itRoom->second;

    if (r.players.size() >= 2) {
        vec.erase(it);
        return {{"type","ERROR"},{"reason","room full"}};
    }

    if (std::find(r.players.begin(), r.players.end(), me.userId) == r.players.end()) {
        r.players.push_back(me.userId);
    }

    vec.erase(it);
    if (vec.empty()) {
        invitesByUser.erase(itInv);
    }

    db.update("Room", {{"id", roomId}}, {{"players", r.players}});

    return {
        {"type","JOIN_ROOM_OK"},
        {"roomId",roomId},
        {"players",r.players}
    };
}

// ============ handler: START_GAME ============

json LobbyServer::handle_start_game(const json& msg) {
    std::string sessionId = msg.value("sessionId", "");
    int roomId = msg.value("roomId", 0);

    if (roomId == 0) {
        return {{"type","ERROR"},{"reason","missing roomId"}};
    }

    SessionInfo me;
    if (!check_session(sessionId, me)) {
        return {{"type","ERROR"},{"reason","invalid session"}};
    }

    int p1 = -1, p2 = -1;

    {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = rooms.find(roomId);
        if (it == rooms.end()) {
            return {{"type","ERROR"},{"reason","no such room"}};
        }
        RoomState& r = it->second;

        if (r.hostUserId != me.userId) {
            return {{"type","ERROR"},{"reason","only host can start"}};
        }
        if (r.status == "playing") {
            return {{"type","ERROR"},{"reason","game already started"}};
        }
        if (r.players.size() != 2) {
            return {{"type","ERROR"},{"reason","need exactly 2 players"}};
        }

        p1 = r.players[0];
        p2 = r.players[1];

        r.status = "playing";
        db.update("Room", {{"id", roomId}}, {{"status","playing"}});
    }

    int gamePort = allocate_game_port();
    std::string roomToken = gen_token(32);
    std::string gameHost = "140.113.17.14";

    pid_t pid = fork();
    if (pid == 0) {
        std::string portStr   = std::to_string(gamePort);
        std::string roomIdStr = std::to_string(roomId);
        std::string p1Str     = std::to_string(p1);
        std::string p2Str     = std::to_string(p2);

        execlp("./game_server", "game_server",
               "--port",  portStr.c_str(),
               "--roomId",roomIdStr.c_str(),
               "--token", roomToken.c_str(),
               "--p1",    p1Str.c_str(),
               "--p2",    p2Str.c_str(),
               (char*)nullptr);

        std::perror("[Lobby] execlp game_server failed");
        _exit(1);
    } else if (pid < 0) {
        std::perror("[Lobby] fork failed");
        std::lock_guard<std::mutex> lock(mtx);
        auto it = rooms.find(roomId);
        if (it != rooms.end()) {
            it->second.status = "idle";
            db.update("Room", {{"id", roomId}}, {{"status","idle"}});
        }
        return {{"type","ERROR"},{"reason","failed to start game server"}};
    }

    nlohmann::json gameStart = {
        {"type","GAME_START"},
        {"roomId",roomId},
        {"gameHost",gameHost},
        {"gamePort",gamePort},
        {"roomToken",roomToken}
    };

    {
        std::lock_guard<std::mutex> lock(mtx);
        gameLaunchByRoom[roomId] = gameStart;
    }

    return gameStart;
}

json LobbyServer::handle_get_game_start(const json& msg) {
    std::string sessionId = msg.value("sessionId", "");
    int roomId = msg.value("roomId", 0);

    if (roomId == 0) {
        return {{"type","ERROR"},{"reason","missing roomId"}};
    }

    std::lock_guard<std::mutex> lock(mtx);

    auto itSess = sessions.find(sessionId);
    if (itSess == sessions.end()) {
        return {{"type","ERROR"},{"reason","invalid session"}};
    }
    int userId = itSess->second.userId;

    auto itRoom = rooms.find(roomId);
    if (itRoom == rooms.end()) {
        return {{"type","ERROR"},{"reason","no such room"}};
    }
    RoomState& r = itRoom->second;

    if (std::find(r.players.begin(), r.players.end(), userId) == r.players.end()) {
        return {{"type","ERROR"},{"reason","not in this room"}};
    }

    auto itLaunch = gameLaunchByRoom.find(roomId);
    if (itLaunch == gameLaunchByRoom.end()) {
        return {{"type","ERROR"},{"reason","game not started"}};
    }

    return itLaunch->second;
}

nlohmann::json LobbyServer::handle_game_finished(const nlohmann::json& msg) {
    std::string sessionId = msg.value("sessionId", "");
    int roomId = msg.value("roomId", 0);

    if (roomId == 0) {
        return {{"type","ERROR"}, {"reason","missing roomId"}};
    }

    SessionInfo me;
    if (!check_session(sessionId, me)) {
        return {{"type","ERROR"}, {"reason","invalid session"}};
    }

    std::lock_guard<std::mutex> lock(mtx);

    auto it = rooms.find(roomId);
    if (it == rooms.end()) {
        return {{"type","ERROR"}, {"reason","no such room"}};
    }

    RoomState& r = it->second;

    if (std::find(r.players.begin(), r.players.end(), me.userId) == r.players.end()) {
        return {{"type","ERROR"}, {"reason","not in room"}};
    }


    r.status = "idle";
    gameLaunchByRoom.erase(roomId);

    try {
        db.update("Room", {{"id", roomId}}, {{"status","idle"}});
    } catch (...) {
    }

    return {{"type","OK"}};
}



// ============ connection handling ============

void LobbyServer::handle_client(TcpSocket client) {
    int fd = client.fd();
    try {
        while (true) {
            json msg = recv_json(fd);
            std::string type = msg.value("type", "");
            json resp;

            if      (type == "REGISTER")       resp = handle_register(msg);
            else if (type == "LOGIN")          resp = handle_login(msg, fd);
            else if (type == "LOGOUT")         resp = handle_logout(msg);
            else if (type == "LIST_USERS")     resp = handle_list_users(msg);
            else if (type == "LIST_ROOMS")     resp = handle_list_rooms(msg);
            else if (type == "CREATE_ROOM")    resp = handle_create_room(msg);
            else if (type == "JOIN_ROOM")      resp = handle_join_room(msg);
            else if (type == "LEAVE_ROOM")     resp = handle_leave_room(msg);
            else if (type == "LIST_INVITES")   resp = handle_list_invites(msg);
            else if (type == "ACCEPT_INVITE")  resp = handle_accept_invite(msg);
            else if (type == "INVITE")         resp = handle_invite(msg);
            else if (type == "START_GAME")     resp = handle_start_game(msg);
            else if (type == "GET_GAME_START") resp = handle_get_game_start(msg);
            else if (type == "GAME_FINISHED")  resp = handle_game_finished(msg);
            else                               resp = {{"type","ERROR"},{"reason","unknown type"}};
            send_json(fd, resp);
        }
    } catch (const std::exception& e) {
        cleanup_session_by_fd(fd);
        // std::cerr << "[Lobby] client disconnected: " << e.what() << "\n";
    }
}

void LobbyServer::run() {
    TcpSocket listener;
    listener.bind_and_listen(port);
    std::cout << "[Lobby] Listening on port " << port << "\n";

    while (true) {
        try {
            TcpSocket client = listener.accept_conn();
            std::thread(&LobbyServer::handle_client, this, std::move(client)).detach();
        } catch (const std::exception& e) {
            std::cerr << "[Lobby] accept error: " << e.what() << "\n";
        }
    }
}
