#include "server.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h> // For console logging
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

// Define server parameters
const uint16 GAME_PORT = 27015;     // Example game port for master server listing (not directly used by Sockets)
const uint16 QUERY_PORT = 27016;    // Example query port for master server listing
const char* SERVER_VERSION = "1.0.0.0";

int main() {
    // Setup spdlog
    try {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto logger = std::make_shared<spdlog::logger>("ServerLogger", console_sink);
        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::debug); // Set global log level
        spdlog::flush_on(spdlog::level::debug);
        spdlog::info("Server: Logging initialized.");
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Server Log initialization failed: " << ex.what() << std::endl;
        return 1;
    }

    Server server;

    if (!server.InitializeSteam(GAME_PORT, QUERY_PORT, SERVER_VERSION)) {
        spdlog::error("Server: Failed to initialize Steam Game Server. Exiting.");
        return 1;
    }

    spdlog::info("Server: Successfully initialized. Running. Type 'quit' to exit.");

    // Main loop: run Steam callbacks and check for admin commands
    std::string line;
    bool bRunning = true;
    while (bRunning) {
        server.RunCallbacks(); // Process Steam Game Server callbacks

        // Basic command input (non-blocking would be better for a real server)
        // This is a simple blocking check.
        // For a real server, use a separate thread for console input or a non-blocking method.
        // std::this_thread::sleep_for(std::chrono::milliseconds(15)); // Allow other threads to run
        
        // Non-blocking input check (very basic for console)
        // For a robust server, a dedicated command input thread or select/poll on stdin is better.
        // This example relies more on the network thread for actual work and RunCallbacks.
        // The main loop here primarily keeps the server alive and processes callbacks.

        // Let's just check for a quit command to make it interactive for this example.
        // A full command system is out of scope.
        // For non-blocking input, you'd typically use platform-specific APIs or a library.
        // A simpler approach for this example is a timed sleep and occasional check.
        
        // Artificial way to check stdin without blocking for too long, not ideal.
        // This part is tricky to do cross-platform and non-blockingly without libraries.
        // For this example, we'll just let it run and rely on ctrl-c or the network thread for activity.
        // The "quit" command below is more for a manual shutdown signal.

        // If you want to type 'quit':
        // You might need to hit Enter for std::cin to process it if it's buffered.
        // This is a very basic way, not suitable for a high-performance server's main loop.
        if (std::cin.peek() != EOF && std::cin.rdbuf()->in_avail() > 0) {
            std::getline(std::cin, line);
            if (line == "quit") {
                spdlog::info("Server: 'quit' command received. Shutting down.");
                bRunning = false;
            } else if (line.rfind("say ", 0) == 0) {
                std::string msg_to_broadcast = line.substr(4);
                spdlog::info("Server: Broadcasting message: {}", msg_to_broadcast);
                server.BroadcastMessage("ServerBot: " + msg_to_broadcast);
            }
        }


        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Main loop tick
    }

    spdlog::info("Server: Shutting down Steam Game Server...");
    server.ShutdownSteam();

    spdlog::info("Server: Exited cleanly.");
    return 0;
}