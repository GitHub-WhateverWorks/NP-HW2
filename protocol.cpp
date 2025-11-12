#include "protocol.hpp"

#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <cstring>
#include <iostream>

using nlohmann::json;

TcpSocket::TcpSocket() : m_fd(-1) {}

TcpSocket::TcpSocket(int fd) : m_fd(fd) {}

TcpSocket::~TcpSocket() {
    if (m_fd >= 0) ::close(m_fd);
}

TcpSocket::TcpSocket(TcpSocket&& other) noexcept : m_fd(other.m_fd) {
    other.m_fd = -1;
}

TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept {
    if (this != &other) {
        if (m_fd >= 0) ::close(m_fd);
        m_fd = other.m_fd;
        other.m_fd = -1;
    }
    return *this;
}

bool TcpSocket::valid() const { return m_fd >= 0; }
int  TcpSocket::fd() const { return m_fd; }

void TcpSocket::close() {
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

void TcpSocket::connect_to(const std::string& host, uint16_t port) {
    if (m_fd >= 0) close();

    m_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_fd < 0) throw std::runtime_error("socket() failed");

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        hostent* he = ::gethostbyname(host.c_str());
        if (!he) {
            ::close(m_fd);
            m_fd = -1;
            throw std::runtime_error("invalid host");
        }
        std::memcpy(&addr.sin_addr, he->h_addr, he->h_length);
    }

    if (::connect(m_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        ::close(m_fd);
        m_fd = -1;
        throw std::runtime_error("connect() failed");
    }
}

void TcpSocket::bind_and_listen(uint16_t port, int backlog) {
    if (m_fd >= 0) close();
    m_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_fd < 0) throw std::runtime_error("socket() failed");

    int yes = 1;
    ::setsockopt(m_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (::bind(m_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        ::close(m_fd);
        m_fd = -1;
        throw std::runtime_error("bind() failed");
    }

    if (::listen(m_fd, backlog) < 0) {
        ::close(m_fd);
        m_fd = -1;
        throw std::runtime_error("listen() failed");
    }
}

TcpSocket TcpSocket::accept_conn() {
    sockaddr_in cli{};
    socklen_t len = sizeof(cli);
    int cfd = ::accept(m_fd, (sockaddr*)&cli, &len);
    if (cfd < 0) throw std::runtime_error("accept() failed");
    return TcpSocket(cfd);
}

ssize_t read_all(int fd, void* buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        ssize_t n = ::recv(fd, (char*)buf + got, len - got, 0);
        if (n == 0) return 0;        
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        got += (size_t)n;
    }
    return (ssize_t)got;
}

ssize_t write_all(int fd, const void* buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = ::send(fd, (const char*)buf + sent, len - sent, 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return -1;
        }
        sent += (size_t)n;
    }
    return (ssize_t)sent;
}

void send_message(int fd, const std::string& body) {
    if (body.empty() || body.size() > MAX_MSG_SIZE)
        throw std::runtime_error("invalid message size");

    uint32_t len = (uint32_t)body.size();
    uint32_t net_len = htonl(len);

    if (write_all(fd, &net_len, 4) != 4)
        throw std::runtime_error("failed to send length");

    if (write_all(fd, body.data(), body.size()) != (ssize_t)body.size())
        throw std::runtime_error("failed to send body");
}

std::string recv_message(int fd) {
    uint32_t net_len = 0;
    ssize_t n = read_all(fd, &net_len, 4);
    if (n == 0) throw std::runtime_error("connection closed");
    if (n != 4) throw std::runtime_error("failed to read length");

    uint32_t len = ntohl(net_len);
    if (len == 0 || len > MAX_MSG_SIZE)
        throw std::runtime_error("invalid length");

    std::string body(len, '\0');
    n = read_all(fd, body.data(), len);
    if (n != (ssize_t)len)
        throw std::runtime_error("failed to read body");

    return body;
}

void send_json(int fd, const json& j) {
    std::string s = j.dump();
    send_message(fd, s);
}

json recv_json(int fd) {
    std::string s = recv_message(fd);
    return json::parse(s);
}
