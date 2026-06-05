#include "server.h"
#include <iostream>
#include <stdexcept>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

static const char* HTTP_200 = "HTTP/1.1 200 OK\r\nContent-Length: 14\r\n\r\nMitral is up!\n";
static const char* HTTP_429 = "HTTP/1.1 429 Too Many Requests\r\nContent-Length: 21\r\n\r\nRate limit exceeded.\n";

Server::Server(int port)
    : port_(port), server_fd_(-1), limiter_("127.0.0.1", 6379, 5)
{
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0)
        throw std::runtime_error("[ERROR] Failed to allocate socket.");

    int opt = 1;
    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        close(server_fd_);
        throw std::runtime_error("[ERROR] Failed to set socket options.");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(server_fd_);
        throw std::runtime_error("[ERROR] Failed to bind to port " + std::to_string(port_));
    }

    if (listen(server_fd_, 10) < 0) {
        close(server_fd_);
        throw std::runtime_error("[ERROR] Failed to listen on socket.");
    }

    std::cout << "[INFO] Mitral is listening on port " << port_ << "...\n";
}

Server::~Server() {
    if (server_fd_ >= 0) close(server_fd_);
}

void Server::run() {
    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            std::cerr << "[WARNING] Failed to accept connection.\n";
            continue;
        }

        char ip_buf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_buf, INET_ADDRSTRLEN);

        handle_client(client_fd, ip_buf);
    }
}

void Server::handle_client(int client_fd, const std::string& client_ip) {
    std::cout << "\n[+] Connection from " << client_ip << "\n";

    char buffer[2048] = {};
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
        close(client_fd);
        return;
    }

    std::cout << "--- INCOMING REQUEST ---\n" << buffer << "------------------------\n";

    const char* response = limiter_.allow(client_ip) ? HTTP_200 : HTTP_429;
    write(client_fd, response, strlen(response));

    close(client_fd);
    std::cout << "[-] Connection closed.\n";
}
