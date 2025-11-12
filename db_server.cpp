#include "db_server.hpp"
#include <iostream>
#include <fstream>

using nlohmann::json;

// ---- Database core ----

bool Database::match_filter(const json& doc, const json& filter) {
    if (!filter.is_object()) return true;
    for (auto it = filter.begin(); it != filter.end(); ++it) {
        const std::string& key = it.key();
        if (!doc.contains(key) || doc[key] != it.value()) {
            return false;
        }
    }
    return true;
}

json Database::handle_create(const std::string& coll, const json& data) {
    if (!data.is_object())
        return {{"status","error"},{"message","data must be object"}};

    auto& c = collections[coll];
    json doc = data;

    if (!doc.contains("id") || !doc["id"].is_number_integer()) {
        doc["id"] = c.nextId++;
    } else {
        int id = doc["id"].get<int>();
        if (c.docs.count(id)) {
            return {{"status","error"},{"message","id already exists"}};
        }
        if (id >= c.nextId) c.nextId = id + 1;
    }

    int id = doc["id"].get<int>();
    c.docs[id] = doc;
    return {{"status","ok"},{"data",doc}};
}

json Database::handle_read(const std::string& coll, const json& filter) {
    auto itc = collections.find(coll);
    if (itc == collections.end())
        return {{"status","ok"},{"data",nullptr}};

    for (auto& [id, doc] : itc->second.docs) {
        if (match_filter(doc, filter)) {
            return {{"status","ok"},{"data",doc}};
        }
    }
    return {{"status","ok"},{"data",nullptr}};
}

json Database::handle_query(const std::string& coll, const json& filter) {
    json arr = json::array();
    auto itc = collections.find(coll);
    if (itc != collections.end()) {
        for (auto& [id, doc] : itc->second.docs) {
            if (match_filter(doc, filter)) {
                arr.push_back(doc);
            }
        }
    }
    return {{"status","ok"},{"items",arr}};
}

json Database::handle_update(const std::string& coll, const json& filter, const json& data) {
    if (!data.is_object())
        return {{"status","error"},{"message","data must be object"}};

    auto itc = collections.find(coll);
    if (itc == collections.end())
        return {{"status","ok"},{"updated",0}};

    int count = 0;
    for (auto& [id, doc] : itc->second.docs) {
        if (match_filter(doc, filter)) {
            for (auto it = data.begin(); it != data.end(); ++it) {
                doc[it.key()] = it.value();
            }
            ++count;
        }
    }
    return {{"status","ok"},{"updated",count}};
}

json Database::handle_delete(const std::string& coll, const json& filter) {
    auto itc = collections.find(coll);
    if (itc == collections.end())
        return {{"status","ok"},{"deleted",0}};

    int count = 0;
    for (auto it = itc->second.docs.begin(); it != itc->second.docs.end(); ) {
        if (match_filter(it->second, filter)) {
            it = itc->second.docs.erase(it);
            ++count;
        } else {
            ++it;
        }
    }
    return {{"status","ok"},{"deleted",count}};
}

json Database::handle_request(const json& req) {
    std::lock_guard<std::mutex> lock(mtx);

    if (!req.contains("collection") || !req.contains("action"))
        return {{"status","error"},{"message","missing collection or action"}};

    std::string coll = req["collection"].get<std::string>();
    std::string action = req["action"].get<std::string>();
    json filter = req.value("filter", json::object());
    json data = req.value("data", json::object());

    json result;
    bool mutated = false;
    if (action == "create"){
        result =  handle_create(coll, data);
        mutated = (result["status"] == "ok");
    }else if (action == "read"){
        result = handle_read(coll, filter);
    }else if (action == "query"){  
        result = handle_query(coll, filter);
    }else if (action == "update"){
        result = handle_update(coll, filter, data);
        mutated = (result["status"] == "ok");
    }else if (action == "delete"){
        result = handle_delete(coll, filter);
        mutated = (result["status"] == "ok");
    }else if(action == "reset"){
        collections.clear();
        save_to_file("db.json");
        result = {{"status","ok"},{"message","all cleared"}};
    }else  result =  {{"status","error"},{"message","unknown action"}};

    if(mutated){
        save_to_file("db.json");
    }

    return result;
    
}

void Database::load_from_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) return; 

    nlohmann::json j;
    in >> j;
    collections.clear();
    for (auto it = j.begin(); it != j.end(); ++it) {
        const std::string collName = it.key();
        const auto& arr = it.value();
        InMemoryCollection c;
        c.nextId = 1;
        for (const auto& doc : arr) {
            if (!doc.contains("id") || !doc["id"].is_number_integer()) continue;
            int id = doc["id"].get<int>();
            c.docs[id] = doc;
            if (id >= c.nextId) c.nextId = id + 1;
        }
        collections[collName] = std::move(c);
    }
}

void Database::save_to_file(const std::string& path) {
    nlohmann::json j;
    for (auto& [name, coll] : collections) {
        nlohmann::json arr = nlohmann::json::array();
        for (auto& [id, doc] : coll.docs) {
            arr.push_back(doc);
        }
        j[name] = arr;
    }
    std::ofstream out(path);
    if (out) {
        out << j.dump(2);
    }
}



// ---- DbServer ----

DbServer::DbServer(uint16_t p) : port(p) {}

void DbServer::handle_client(TcpSocket client) {
    int fd = client.fd();
    try {
        while (true) {
            json req = recv_json(fd);
            json resp = db.handle_request(req);
            send_json(fd, resp);
        }
    } catch (const std::exception& e) {
        // std::cerr << "[DB] client handler ended: " << e.what() << "\n";
    }
}

void DbServer::run() {
    db.load_from_file("db.json");
    TcpSocket listener;
    listener.bind_and_listen(port);
    std::cout << "[DB] Listening on port " << port << "\n";

    while (true) {
        try {
            TcpSocket client = listener.accept_conn();
            std::thread(&DbServer::handle_client, this, std::move(client)).detach();
        } catch (const std::exception& e) {
            std::cerr << "[DB] accept error: " << e.what() << "\n";
        }
    }
}
