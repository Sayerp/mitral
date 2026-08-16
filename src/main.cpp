#include "server.h"
#include <iostream>
#include <stdexcept>
#include <cstdlib>

int main() {
    std::cout << "Mitral starting...\n";
    try {
        const char* port_env = std::getenv("PORT");
        int port = port_env ? std::atoi(port_env) : 8080;

        Server server(port);
        server.run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    return 0;
}