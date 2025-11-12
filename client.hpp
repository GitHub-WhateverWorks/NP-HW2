#ifndef CLIENT_HPP
#define CLIENT_HPP

#include "protocol.hpp"
#include "json.hpp"
#include <string>

class ClientApp {
public:
    ClientApp(const std::string& lobbyHost, uint16_t lobbyPort);
    void run();

private:
    TcpSocket lobbySock;
    std::string lobbyHost;
    uint16_t lobbyPort;

    bool loggedIn = false;
    int userId = -1;
    std::string userName;
    std::string sessionId;

    bool connect_lobby();
    bool send_and_recv(const nlohmann::json& req, nlohmann::json& resp);


    void main_menu();

    void handle_register();
    void handle_login();
    void handle_logout();        
    void handle_list_rooms();
    void handle_create_room();
    void handle_join_room();       
    void handle_list_invites();    
    void handle_accept_invite();   
    
    void handle_list_users(); 
    void handle_invite(int roomId);
    void handle_game_start(const nlohmann::json& msg);
    void handle_get_game_start(int roomId);

    void room_loop(int roomId, bool isHost);
    void connect_game_server(const std::string& gameHost,
                             uint16_t gamePort,
                             const std::string& roomToken,
                             int roomId);
};

#endif 
