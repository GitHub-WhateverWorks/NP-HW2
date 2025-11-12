#include "db_client.hpp"
#include <stdexcept>

using nlohmann::json;

DbClient::DbClient(const std::string& host, uint16_t port) {
    sock.connect_to(host, port);
}

json DbClient::create(const std::string& coll, const json& data) {
    json req = {
        {"collection", coll},
        {"action", "create"},
        {"data", data}
    };
    send_json(sock.fd(), req);
    return recv_json(sock.fd());
}

json DbClient::read(const std::string& coll, const json& filter) {
    json req = {
        {"collection", coll},
        {"action", "read"},
        {"filter", filter}
    };
    send_json(sock.fd(), req);
    return recv_json(sock.fd());
}

json DbClient::query(const std::string& coll, const json& filter) {
    json req = {
        {"collection", coll},
        {"action", "query"},
        {"filter", filter}
    };
    send_json(sock.fd(), req);
    return recv_json(sock.fd());
}

json DbClient::update(const std::string& coll, const json& filter, const json& data) {
    json req = {
        {"collection", coll},
        {"action", "update"},
        {"filter", filter},
        {"data", data}
    };
    send_json(sock.fd(), req);
    return recv_json(sock.fd());
}

json DbClient::del(const std::string& coll, const json& filter) {
    json req = {
        {"collection", coll},
        {"action", "delete"},
        {"filter", filter}
    };
    send_json(sock.fd(), req);
    return recv_json(sock.fd());
}

json DbClient::reset(const std::string& coll, const json& filter) {
    json req = {
        {"collection", coll},
        {"action", "reset"},
        {"filter", filter}
    };
    send_json(sock.fd(), req);
    return recv_json(sock.fd());
}
