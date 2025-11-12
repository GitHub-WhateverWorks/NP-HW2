#ifndef LOBBY_SERVER_HPP
#define LOBBY_SERVER_HPP

#include "protocol.hpp"
#include "db_client.hpp"
#include "json.hpp"
#include <unordered_map>
#include <mutex>
#include <thread>
#include <string>
#include <vector>

struct SessionInfo {
    int userId;
    std::string name;
    int fd; 
};


struct RoomState {
    int roomId;
    std::string name;
    int hostUserId;
    std::string visibility; 
    std::string status;    
    std::vector<int> players; 
};

struct Invite {
    int roomId;
    int fromUserId;
    std::string roomName;
};

class LobbyServer {
public:
    LobbyServer(uint16_t port,
                const std::string& dbHost,
                uint16_t dbPort);

    void run(); 

private:
    uint16_t port;
    DbClient db;
    std::mutex mtx;

    std::unordered_map<std::string, SessionInfo> sessions;   // sessionId -> info
    std::unordered_map<int, RoomState> rooms;                // roomId -> state
    std::unordered_map<int, std::string> userIdToSession;    // userId -> sessionId
    std::unordered_map<int, std::vector<Invite>> invitesByUser; // targetUserId -> invites
    std::unordered_map<int, nlohmann::json> gameLaunchByRoom; // roomId -> GAME_START msg

    int nextGamePort = 20000; 

    void handle_client(TcpSocket client);
    std::string gen_session_id();
    int gen_room_id();
    std::string gen_token(int len = 32);

    bool check_session(const std::string& sessionId, SessionInfo& out);
    void cleanup_session_by_fd(int fd);
    int allocate_game_port();

    nlohmann::json handle_register(const nlohmann::json& msg);
    nlohmann::json handle_login(const nlohmann::json& msg, int clientFd);
    nlohmann::json handle_logout(const nlohmann::json& msg);           
    nlohmann::json handle_list_users(const nlohmann::json& msg);
    nlohmann::json handle_list_rooms(const nlohmann::json& msg);
    nlohmann::json handle_create_room(const nlohmann::json& msg);
    nlohmann::json handle_join_room(const nlohmann::json& msg);
    nlohmann::json handle_leave_room(const nlohmann::json& msg);
    nlohmann::json handle_list_invites(const nlohmann::json& msg);     
    nlohmann::json handle_accept_invite(const nlohmann::json& msg);     

    nlohmann::json handle_invite(const nlohmann::json& msg);           
    nlohmann::json handle_start_game(const nlohmann::json& msg);        
    nlohmann::json handle_get_game_start(const nlohmann::json& msg);
    nlohmann::json handle_game_finished(const nlohmann::json& msg);

    void push_message_to_user(int userId, const nlohmann::json& msg);
};

#endif 
