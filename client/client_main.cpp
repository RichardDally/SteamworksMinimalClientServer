#include "client.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h> // For console logging
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

const char* SERVER_ADDRESS = "127.0.0.1"; // Or your server's IP
const uint16 SERVER_PORT = 42000;         // Match server's listening port

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
        auto logger = std::make_shared<spdlog::logger>("ClientLogger", console_sink);
        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::debug); // Set global log level
        spdlog::flush_on(spdlog::level::debug);
        spdlog::info("Client: Logging initialized.");
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Client Log initialization failed: " << ex.what() << std::endl;
        return 1;
    }

    std::atomic<bool> run(true);
    std::thread cinThread(ReadCin, std::ref(run));
    Client client;

    if (!client.InitializeSteam()) {
        spdlog::error("Client: Failed to initialize Steam. Exiting.");
        return 1;
    }

    if (!client.Connect(SERVER_ADDRESS, SERVER_PORT)) {
        spdlog::error("Client: Failed to initiate connection to server. Exiting.");
        client.ShutdownSteam();
        return 1;
    }

    spdlog::info("Client: Main loop started. Type 'quit' to exit, 'msg <message>' to send.");

    // Main loop: run Steam callbacks and check for user input
    while (run.load() && (client.IsConnected() || client.IsAttemptingConnection()) )
    { // Use m_bAttemptingConnection to keep running while trying
        client.RunCallbacks(); // Process Steam API callbacks
        client.PollIncomingMessages(); // Process incoming messages from server

        // Non-blocking input check (simple version)
        // For a real game, use a proper input library or game loop structure
        // This console input is very basic and might block inappropriately in some terminals
        // For a proper non-blocking stdin, platform-specific code is needed.
        // Here, we just let RunCallbacks and the network thread do their work.

        // Example: Periodically send a ping or check status
        // For this example, messages are primarily driven by connection events or direct commands

        std::this_thread::sleep_for(std::chrono::milliseconds(30)); // Keep main thread responsive

        static auto lastPingTime = std::chrono::steady_clock::now();
        static bool firstPing = true;
        auto now = std::chrono::steady_clock::now();
        if (client.IsAuthenticated() &&
            std::chrono::duration_cast<std::chrono::seconds>(now - lastPingTime).count() >= 5)
        {
            if (firstPing) {
                spdlog::info("=== Step 8: Starting periodic pings (every 2 seconds) ===");
                firstPing = false;
            }
            client.SendMessageToServer("PING");
            lastPingTime = now;
        }
    }
    run.store(false);
    cinThread.join();
    
    // If loop exited because connection dropped or never established fully
    if (!client.IsConnected() && !client.IsAttemptingConnection()) {
         spdlog::info("Client: Disconnected or failed to connect.");
    }

    spdlog::info("Client: Shutting down...");
    client.Disconnect(); // Explicitly disconnect
    client.ShutdownSteam();

    spdlog::info("Client: Exited cleanly.");
    return 0;
}