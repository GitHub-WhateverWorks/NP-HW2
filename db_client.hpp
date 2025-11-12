#ifndef DB_CLIENT_HPP
#define DB_CLIENT_HPP

#include "protocol.hpp"
#include "json.hpp"
#include <string>

class DbClient {
public:
    DbClient(const std::string& host, uint16_t port);

    nlohmann::json create(const std::string& coll, const nlohmann::json& data);
    nlohmann::json read(const std::string& coll, const nlohmann::json& filter);
    nlohmann::json query(const std::string& coll, const nlohmann::json& filter);
    nlohmann::json update(const std::string& coll, const nlohmann::json& filter, const nlohmann::json& data);
    nlohmann::json del(const std::string& coll, const nlohmann::json& filter);
    nlohmann::json reset(const std::string& coll, const nlohmann::json& filter);

private:
    TcpSocket sock;
};

#endif 
