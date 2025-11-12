#ifndef GAME_SERVER_HPP
#define GAME_SERVER_HPP

#include "protocol.hpp"
#include "json.hpp"
#include <vector>
#include <string>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <queue>
#include <thread>

struct GamePlayerConn {
    int userId = -1;
    std::string role;   
    bool ready = false;
    TcpSocket socket;   
};

class GameServer {
public:
    GameServer(uint16_t port,
               int roomId,
               const std::string& roomToken,
               const std::vector<int>& playerIds);

    void run(); 

private:
    uint16_t port;
    int roomId;
    std::string roomToken;
    std::vector<int> expectedPlayers; 

    std::mutex inputMtx;
    std::unordered_map<int, std::queue<std::string>> inputQueues; // userId -> actions
    std::unordered_map<int, GamePlayerConn> players; // userId -> conn

    std::atomic<bool> running{true};

    void wait_for_players(TcpSocket& listener);
    void send_welcome_messages(uint64_t seed, int dropMs);
    void start_input_threads();
    void input_thread(int userId);

    void game_loop(uint64_t seed, int dropMs);

    void broadcast_snapshot(int tick,
                            int remainingMs,
                            const struct PlayerState& p1,
                            const struct PlayerState& p2);
    void send_game_over(const struct PlayerState& p1,
                        const struct PlayerState& p2);

    void enqueue_input(int userId, const std::string& action);
    bool pop_input(int userId, std::string& out);
};

#endif 
