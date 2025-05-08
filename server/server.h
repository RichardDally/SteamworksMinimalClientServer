#pragma once

#include <steam/isteamuser.h>
#include <steam/steam_gameserver.h>
#include <steam/isteamnetworkingsockets.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>

// Structure to hold data for each connected client
struct ClientConnectionData_t {
    CSteamID m_steamID;
    HSteamNetConnection m_hConnection;
    enum EAuthState {
        AUTH_PENDING,
        AUTH_TICKET_RECEIVED,
        AUTH_VALIDATED,
        AUTH_FAILED
    } m_eAuthState;
    std::vector<uint8> m_authTicketData; // Store received ticket until processed

    ClientConnectionData_t() : m_eAuthState(AUTH_PENDING), m_hConnection(k_HSteamNetConnection_Invalid) {}
};

class Server {
public:
    Server();
    ~Server();

    bool InitializeSteam(uint16_t usGamePort, uint16_t usQueryPort, const char* pchVersionString);
    void ShutdownSteam();

    void RunCallbacks(); // Should be called regularly
    void PollNetwork();  // Poll for incoming connections and messages

    void SendMessageToClient(HSteamNetConnection hConn, const std::string& message);
    void BroadcastMessage(const std::string& message);

private:
    // Steam Callbacks
    //void OnSteamNetConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pCallback);
    STEAM_GAMESERVER_CALLBACK(Server, OnSteamNetConnectionStatusChanged, SteamNetConnectionStatusChangedCallback_t);

    //void OnValidateAuthTicketResponse(ValidateAuthTicketResponse_t* pCallback);
    STEAM_GAMESERVER_CALLBACK(Server, OnValidateAuthTicketResponse, ValidateAuthTicketResponse_t);

    //void OnSteamServersConnected(SteamServersConnected_t* pCallback);
    STEAM_GAMESERVER_CALLBACK(Server, OnSteamServersConnected, SteamServersConnected_t);

    //void OnSteamServersDisconnected(SteamServersDisconnected_t* pCallback);
    STEAM_GAMESERVER_CALLBACK(Server, OnSteamServersDisconnected, SteamServersDisconnected_t);

    //void OnSteamServerConnectFailure(SteamServerConnectFailure_t* pCallback);
    STEAM_GAMESERVER_CALLBACK(Server, OnSteamServerConnectFailure, SteamServerConnectFailure_t);


    void HandleClientConnection(HSteamNetConnection hConn);
    void HandleClientDisconnection(HSteamNetConnection hConn, const SteamNetConnectionInfo_t& info);
    void ProcessMessageFromClient(HSteamNetConnection hConn, const uint8* data, uint32 size);

    ISteamNetworkingSockets* m_pInterface;
    HSteamListenSocket m_hListenSocket;
    HSteamNetPollGroup m_hPollGroup; // For managing connections efficiently

    std::atomic<bool> m_bRunning;
    std::thread m_networkPollThread; // Potentially for dedicated polling

    // Store client data
    std::mutex m_mutexClientData; // Protect access to m_mapClientData
    std::unordered_map<HSteamNetConnection, ClientConnectionData_t> m_mapClientData;
};