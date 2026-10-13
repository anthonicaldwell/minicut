#include "App.h"
#include <iostream>

int main(int argc, char* argv[]) {
    std::cout << "Starting minicut - Modern C++17 Multi-Track Video Editor..." << std::endl;
    App app;
    if (!app.init(argc, argv)) {
        std::cerr << "Failed to initialize minicut application!" << std::endl;
        return 1;
    }

    app.run();
    std::cout << "minicut shut down cleanly." << std::endl;
    return 0;
}
