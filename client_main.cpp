#include "client.hpp"
#include <iostream>
#include <cstdlib>

int main(int argc, char** argv) {
    std::string lobbyHost = "127.0.0.1";
    uint16_t lobbyPort = 13000;


    if (argc >= 2) lobbyHost = argv[1];
    if (argc >= 3) lobbyPort = static_cast<uint16_t>(std::stoi(argv[2]));

    std::cout << "[Client Main] Connecting to lobby at "
              << lobbyHost << ":" << lobbyPort << "\n";

    ClientApp app(lobbyHost, lobbyPort);
    app.run();
    return 0;
}
