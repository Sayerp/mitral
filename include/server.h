#pragma once
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>

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
    std::vector<std::thread> workers_;
    std::queue<int> task_queue_;
    std::mutex queue_mutex_;
    std::condition_variable condition_;
    bool stop_pool_ = false;

    void handle_client(int client_fd, const std::string& client_ip);
    void worker_thread();
};
