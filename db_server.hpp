#ifndef DB_SERVER_HPP
#define DB_SERVER_HPP

#include "protocol.hpp"
#include "json.hpp"
#include <unordered_map>
#include <mutex>
#include <thread>

class InMemoryCollection {
public:
    int nextId = 1;
    std::unordered_map<int, nlohmann::json> docs;
};

class Database {
public:
    nlohmann::json handle_request(const nlohmann::json& req);
    void load_from_file(const std::string& path);
    void save_to_file(const std::string& path);

private:
    std::mutex mtx;
    std::unordered_map<std::string, InMemoryCollection> collections;

    nlohmann::json handle_create(const std::string& coll, const nlohmann::json& data);
    nlohmann::json handle_read(const std::string& coll, const nlohmann::json& filter);
    nlohmann::json handle_query(const std::string& coll, const nlohmann::json& filter);
    nlohmann::json handle_update(const std::string& coll, const nlohmann::json& filter, const nlohmann::json& data);
    nlohmann::json handle_delete(const std::string& coll, const nlohmann::json& filter);

    bool match_filter(const nlohmann::json& doc, const nlohmann::json& filter);
};

class DbServer {
public:
    explicit DbServer(uint16_t port);

    void run(); 

private:
    uint16_t port;
    Database db;

    void handle_client(TcpSocket client);
};

#endif
