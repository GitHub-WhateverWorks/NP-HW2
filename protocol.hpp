#ifndef PROTOCOL_HPP
#define PROTOCOL_HPP

#include <string>
#include <cstdint>
#include <stdexcept>
#include <vector>
#include <netinet/in.h>
#include "json.hpp"

// Max body size: 64 KiB per spec
constexpr uint32_t MAX_MSG_SIZE = 65536;

class TcpSocket {
public:
    TcpSocket();
    explicit TcpSocket(int fd);
    ~TcpSocket();

    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;

    TcpSocket(TcpSocket&& other) noexcept;
    TcpSocket& operator=(TcpSocket&& other) noexcept;

    bool valid() const;
    int  fd() const;

    void close();

    void connect_to(const std::string& host, uint16_t port);

    void bind_and_listen(uint16_t port, int backlog = 16);

    TcpSocket accept_conn();

private:
    int m_fd;
};

ssize_t read_all(int fd, void* buf, size_t len);
ssize_t write_all(int fd, const void* buf, size_t len);

void send_message(int fd, const std::string& body);
std::string recv_message(int fd);

void send_json(int fd, const nlohmann::json& j);
nlohmann::json recv_json(int fd);

#endif 
