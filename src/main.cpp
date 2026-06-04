#include <iostream>
#include <sys/socket.h> 
#include <netinet/in.h> 
#include <unistd.h>     
#include <cstring>      

const int PORT = 8080;

int main() {
    std::cout << "Mitral starting...\n";

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

        char buffer[2048] = {0};
        ssize_t bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);
        if (bytes_read > 0) {
            std::cout << "--- INCOMING REQUEST ---\n" << buffer << "------------------------\n";
        }

        std::string response = "HTTP/1.1 200 OK\r\nContent-Length: 14\r\n\r\nMitral is up!\n"; // hard coded for now, logic implemented after
        write(client_socket, response.c_str(), response.length());

        close(client_socket);
        std::cout << "[-] Client connection closed.\n";
    }

    close(server_fd);
    return 0;
}