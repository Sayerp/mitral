#include "server.h"
#include "rate_limiter.h"
#include <hiredis/hiredis.h>
#include <iostream>
#include <stdexcept>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <thread>

static const char* HTTP_200 = "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 14\r\n\r\nMitral is up!\n";
static const char* HTTP_429 = "HTTP/1.1 429 Too Many Requests\r\nConnection: close\r\nContent-Length: 21\r\n\r\nRate limit exceeded.\n";

Server::Server(int port)
    : port_(port), server_fd_(-1)
{
    redisContext* boot_redis = redisConnect("127.0.0.1", 6379);
    if (boot_redis == nullptr || boot_redis->err) {
        std::string err = boot_redis ? boot_redis->errstr : "cannot allocate Redis context";
        if (boot_redis) redisFree(boot_redis);
        throw std::runtime_error("[FATAL] Server boot failed. Cannot connect to Redis: " + err);
    }

    const char* lua_script = R"(
        local key = KEYS[1]
        local max_tokens = tonumber(ARGV[1])
        local now = tonumber(ARGV[2])
        local rate = tonumber(ARGV[3])
        
        local bucket = redis.call('HMGET', key, 'tokens', 'last_update')
        local tokens = tonumber(bucket[1])
        local last_update = tonumber(bucket[2])

        if tokens == nil then
            tokens = max_tokens
            last_update = now
        else
            local elapsed = math.max(0, now - last_update)
            local dripped = math.floor(elapsed * rate) 
            
            if dripped >= 1 then
                tokens = math.min(max_tokens, tokens + dripped)
                last_update = now
            end
        end

        if tokens >= 1 then
            redis.call('HMSET', key, 'tokens', tokens - 1, 'last_update', last_update)
            redis.call('EXPIRE', key, 10) 
            return 1 
        else
            redis.call('EXPIRE', key, 10) 
            return 0 
        end
    )";

    redisReply *reply = (redisReply*)redisCommand(boot_redis, "SCRIPT LOAD %s", lua_script);
    if (reply == nullptr || reply->type != REDIS_REPLY_STRING) {
        if (reply) freeReplyObject(reply);
        redisFree(boot_redis);
        throw std::runtime_error("[FATAL] Server boot failed: Could not compile Lua script.");
    }

    lua_sha_cache_ = reply->str;
    freeReplyObject(reply);

    redisFree(boot_redis);

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

        std::thread(&Server::handle_client, this, client_fd, std::string(ip_buf)).detach();
    }
}

void Server::handle_client(int client_fd, const std::string& client_ip) {
    RateLimiter local_limiter("127.0.0.1", 6379, 5, lua_sha_cache_);

    char buffer[2048] = {};
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
        close(client_fd);
        return;
    }

    const char* response = local_limiter.allow(client_ip) ? HTTP_200 : HTTP_429;
    write(client_fd, response, strlen(response));

    close(client_fd);
}

void Server::worker_thread() {
    RateLimiter local_limiter("127.0.0.1", 6379, 5, lua_sha_cache_);

    while (true) {
        int client_fd = -1;

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);

            condition_.wait(lock, [this]() {
                return !task_queue_.empty() || stop_pool_;
            });

            if (stop_pool_ && task_queue_.empty()) {
                return;
            }

            client_fd = task_queue_.front();
            task_queue_.pop();
        }

        char buffer[2048] = {};
        ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read > 0) {
            const char* response = local_limiter.allow("127.0.0.1") ? HTTP_200 : HTTP_429;
            write(client_fd, response, strlen(response));
        }

        close(client_fd);
    }
}