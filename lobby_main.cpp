#include "lobby_server.hpp"
#include <iostream>

int main() {
    constexpr uint16_t LOBBY_PORT = 13000;

    constexpr const char* DB_HOST = "127.0.0.1";
    constexpr uint16_t    DB_PORT = 12000;

    try {
        LobbyServer lobby(LOBBY_PORT, DB_HOST, DB_PORT);
        std::cout << "[Lobby] Starting on port " << LOBBY_PORT
                  << " (DB: " << DB_HOST << ":" << DB_PORT << ")"
                  << std::endl;
        lobby.run(); 
    } catch (const std::exception& e) {
        std::cerr << "[Lobby] Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
