#pragma once

#include <steam/steam_api.h>
#include <steam/isteamnetworkingsockets.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex> // For protecting shared data if any complex state is added

class Client {
public:
    Client();
    ~Client();

    bool InitializeSteam();
    void ShutdownSteam();

    bool Connect(const char* serverAddress, uint16 serverPort);
    void Disconnect();

    void RunCallbacks(); // Should be called regularly
    void SendMessageToServer(const std::string& message);

    bool IsConnected() const;
    bool IsAttemptingConnection() const;
    bool IsAuthenticated() const;
    void PollIncomingMessages();

private:
    STEAM_CALLBACK(Client, OnSteamNetConnectionStatusChanged, SteamNetConnectionStatusChangedCallback_t);

    void ProcessMessage(const uint8* data, uint32 size);

    HSteamNetConnection m_hConnection;
    ISteamNetworkingSockets* m_pInterface;
    HAuthTicket m_hAuthTicket;
    std::atomic<bool> m_bConnected;
    std::atomic<bool> m_bAttemptingConnection;
    std::atomic<bool> m_bAuthenticated;
    std::thread m_networkThread;
    std::atomic<bool> m_bRunning;

    const uint32 m_unAuthTicketBufferSize = 1024;
    std::vector<uint8> m_authTicketBuffer;
    uint32 m_unAuthTicketSize;
};