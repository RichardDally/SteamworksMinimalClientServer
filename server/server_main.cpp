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


void ReadCin(std::atomic<bool>& run)
{
    std::string buffer;

    while (run.load())
    {
        std::cin >> buffer;
        if (buffer == "quit")
        {
            run.store(false);
        }
    }
}


int main()
{
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

    std::atomic<bool> run(true);
    std::thread cinThread(ReadCin, std::ref(run));
    Server server;

    if (!server.InitializeSteam(GAME_PORT, QUERY_PORT, SERVER_VERSION)) {
        spdlog::error("Server: Failed to initialize Steam Game Server. Exiting.");
        return 1;
    }

    spdlog::info("Server: Successfully initialized. Running. Type 'quit' to exit.");

    // Main loop: run Steam callbacks and check for admin commands
    while (run.load())
    {
        // Process Steam Game Server callbacks
        server.RunCallbacks();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    run.store(false);
    cinThread.join();

    spdlog::info("Server: Shutting down Steam Game Server...");
    server.ShutdownSteam();

    spdlog::info("Server: Exited cleanly.");
    return 0;
}
