#pragma once
#include <string>

class Server {
public:
    explicit Server(int port);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    void run();

private:
    int port_;
    int server_fd_;
    std::string lua_sha_cache_;

    void handle_client(int client_fd, const std::string& client_ip);
};
