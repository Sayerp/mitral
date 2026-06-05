#include "server.h"
#include <iostream>
#include <stdexcept>

int main() {
    std::cout << "Mitral starting...\n";
    try {
        Server server(8080);
        server.run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    return 0;
}