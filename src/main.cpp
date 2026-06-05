#include <iostream>
#include <sys/socket.h> 
#include <netinet/in.h> 
#include <unistd.h>     
#include <cstring>
#include <hiredis/hiredis.h>   
#include <arpa/inet.h>   

const int PORT = 8080;

int main() {
    std::cout << "Mitral starting...\n";

    redisContext *redis = redisConnect("127.0.0.1", 6379);
    
    if (redis == nullptr || redis->err) {
        if (redis) {
            std::cerr << "[ERROR] Redis connection failed: " << redis->errstr << "\n";
            redisFree(redis);
        } else {
            std::cerr << "[ERROR] Cannot allocate Redis context.\n";
        }
        return 1;
    }

    std::cout << "[INFO] Successfully connected to Redis!\n";


    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "[ERROR] Failed to allocate socket.\n";
        return 1;
    }

    // avoid "Address already in use" errors during testing
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "[ERROR] Failed to set socket options.\n";
        close(server_fd);
        return 1;
    }

    sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; 
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "[ERROR] Failed to bind to port " << PORT << ".\n";
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        std::cerr << "[ERROR] Failed to transition socket to listen state.\n";
        close(server_fd);
        return 1;
    }

    std::cout << "[INFO] Mitral is listening on port " << PORT << "...\n";

    while (true) {
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        // Thread will block until a client tries to connect
        int client_socket = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            std::cerr << "[WARNING] Failed to accept incoming connection.\n";
            continue; 
        }

        std::cout << "\n[+] Connection established with a client!\n";

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);

        char buffer[2048] = {0};
        ssize_t bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) {
            close(client_socket);
            continue;
        }

        std::cout << "--- INCOMING REQUEST ---\n" << buffer << "------------------------\n"; // for testing, remove later for performance

        int max_tokens = 5;
        bool allow_request = false;

        redisReply *reply = (redisReply*) redisCommand(redis, "EXISTS %s", client_ip);
        bool exists = (reply->integer == 1);
        freeReplyObject(reply);

        if (!exists) {
            redisCommand(redis, "SET %s %d EX 10", client_ip, max_tokens-1); // IP expires in 10 seconds
            allow_request = true;
            std::cout << "[+] New IP " << client_ip << " registered. Tokens remaining: " << (max_tokens - 1) << "\n";
        } else {
            reply = (redisReply*) redisCommand(redis, "GET %s", client_ip);

            if (reply->str == nullptr) {
                allow_request = true; // race condition, IP expires between EXISTS and GET, allow API request through once -> later dynamically create bucket
            } else {
                int current_tokens = std::stoi(reply->str);

                if (current_tokens > 0) {
                    redisCommand(redis, "DECR %s", client_ip);
                    allow_request = true;
                    std::cout << "[~] IP " << client_ip << " allowed. Tokens remaining: " << (current_tokens - 1) << "\n";
                } else {
                    allow_request = false;
                    std::cout << "[X] IP " << client_ip << " BLOCKED. Rate limit exceeded.\n";
                }
            }
            freeReplyObject(reply);
        }

        if (allow_request) {
            std::string ok_resp = "HTTP/1.1 200 OK\r\nContent-Length: 14\r\n\r\nMitral is up!\n";
            write(client_socket, ok_resp.c_str(), ok_resp.length());
        } else {
            std::string rate_limit_resp = "HTTP/1.1 429 Too Many Requests\r\nContent-Length: 21\r\n\r\nRate limit exceeded.\n";
            write(client_socket, rate_limit_resp.c_str(), rate_limit_resp.length());
        }

        close(client_socket);
        std::cout << "[-] Client connection closed.\n";
    }

    close(server_fd);
    return 0;
}