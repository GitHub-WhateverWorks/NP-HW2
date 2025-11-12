#include "db_server.hpp"
#include <iostream>
using namespace std;

int main() {
    constexpr uint16_t DB_PORT = 12000;

    try {
        DbServer server(DB_PORT);
        cout << "[DB] Starting on port " << DB_PORT <<"\n" ;
        server.run(); 
    } catch (const std::exception& e) {
        cerr << "[DB] Fatal error: " << e.what() << "\n" ;
        return 1;
    }
    return 0;
}
