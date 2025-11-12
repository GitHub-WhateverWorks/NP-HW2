#include "game_server.hpp"
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 7) {
        std::cerr << "Usage: " << argv[0]
                  << " --port <p> --roomId <id> --token <token> --p1 <userId1> --p2 <userId2>\n";
        return 1;
    }

    uint16_t port = 0;
    int roomId = 0;
    std::string token;
    int p1 = -1, p2 = -1;

    for (int i = 1; i + 1 < argc; i += 2) {
        std::string k = argv[i];
        std::string v = argv[i+1];
        if (k == "--port") port = static_cast<uint16_t>(std::stoi(v));
        else if (k == "--roomId") roomId = std::stoi(v);
        else if (k == "--token") token = v;
        else if (k == "--p1") p1 = std::stoi(v);
        else if (k == "--p2") p2 = std::stoi(v);
    }

    if (!port || !roomId || token.empty() || p1 < 0 || p2 < 0) {
        std::cerr << "Invalid arguments.\n";
        return 1;
    }

    std::vector<int> players = {p1, p2};
    GameServer gs(port, roomId, token, players);
    gs.run();
    return 0;
}
