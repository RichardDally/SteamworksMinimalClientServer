#include "server.h"
#include <spdlog/spdlog.h>
#include <steam/steamnetworkingtypes.h> // For SteamNetworkingIPAddr
#include <steam/isteamgameserver.h>
#include <chrono> // For std::this_thread::sleep_for

constexpr uint32 MAX_MESSAGES_PER_POLL_SERVER = 32;
constexpr uint16 DEFAULT_SERVER_PORT = 1234; // Same as client attempts to connect to

// Helper function (can be in a utility header or static in the .cpp)
inline uint32_t ManualNetToHost32(const uint8_t* network_bytes_in) {
    return (static_cast<uint32_t>(network_bytes_in[0]) << 24) |
        (static_cast<uint32_t>(network_bytes_in[1]) << 16) |
        (static_cast<uint32_t>(network_bytes_in[2]) << 8) |
        (static_cast<uint32_t>(network_bytes_in[3]));
}

Server::Server()
    : m_pInterface(nullptr),
      m_hListenSocket(k_HSteamListenSocket_Invalid),
      m_hPollGroup(k_HSteamNetPollGroup_Invalid),
      m_bRunning(false) {
}

Server::~Server() {
    if (m_bRunning) {
        ShutdownSteam();
    }
}

bool Server::InitializeSteam(uint16_t usGamePort, uint16_t usQueryPort, const char* pchVersionString) {
    // Initialize Steam Game Server
    // Note: usGamePort is the port for game traffic, usQueryPort for server browser master server pings
    // For SteamNetworkingSockets, the IP and port are specified when creating the listen socket.
    // The gamePort in SteamGameServer_Init is more for master server registration.
    // For direct IP connections as in this example, the listen socket's port is key.
    // Let's use DEFAULT_SERVER_PORT for our listen socket.
    // For SteamGameServer_Init, if not using master server, ports can be nominal.
    // Mod name should be your game's directory name.
    static constexpr uint32 INADDR_ANY = 0;
    if (!SteamGameServer_Init(INADDR_ANY, usGamePort, usQueryPort, EServerMode::eServerModeAuthenticationAndSecure, pchVersionString)) {
        spdlog::error("Server: SteamGameServer_Init failed. Is steam_appid.txt present and valid?");
        return false;
    }
    spdlog::info("Server: SteamGameServer_Init successful.");

    m_pInterface = SteamGameServerNetworkingSockets();
    if (!m_pInterface) {
        spdlog::error("Server: SteamGameServerNetworkingSockets() failed to initialize.");
        SteamGameServer_Shutdown();
        return false;
    }

    // Set server name, map, etc. (optional for this example, but good practice for real servers)
    SteamGameServer()->SetModDir("SteamworksMinimalServer");
    SteamGameServer()->SetProduct("MyAwesomeGame");
    SteamGameServer()->SetGameDescription("Minimal Steamworks Server Example");
    SteamGameServer()->SetDedicatedServer(true);
    SteamGameServer()->SetAdvertiseServerActive(true);
    SteamGameServer()->LogOnAnonymous(); // Or SteamGameServer()->LogOn( "YOUR_SERVER_TOKEN_HERE" ); for GSLT

    // Create a listen socket
    SteamNetworkingIPAddr serverLocalAddr;
    serverLocalAddr.Clear();
    serverLocalAddr.m_port = DEFAULT_SERVER_PORT;

    // No custom options needed for this minimal example if relying on STEAM_GAMESERVER_CALLBACK
    m_hListenSocket = m_pInterface->CreateListenSocketIP(serverLocalAddr, 0, nullptr);
    if (m_hListenSocket == k_HSteamListenSocket_Invalid) {
        spdlog::error("Server: Failed to create listen socket on port {}.", DEFAULT_SERVER_PORT);
        SteamGameServer_Shutdown();
        return false;
    }
    spdlog::info("Server: Listening on port {}.", DEFAULT_SERVER_PORT);

    // Create a poll group for managing connections
    m_hPollGroup = m_pInterface->CreatePollGroup();
    if (m_hPollGroup == k_HSteamNetPollGroup_Invalid) {
        spdlog::error("Server: Failed to create poll group.");
        m_pInterface->CloseListenSocket(m_hListenSocket);
        SteamGameServer_Shutdown();
        return false;
    }
    spdlog::info("Server: Poll group created.");

    m_bRunning = true;

    // Start a polling thread (optional, can integrate into main loop)
    m_networkPollThread = std::thread([this]() {
        while (m_bRunning) {
            PollNetwork(); // Poll for new connections and messages
            std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Adjust polling frequency as needed
        }
        spdlog::info("Server: Network polling thread exiting.");
    });


    return true;
}

void Server::ShutdownSteam() {
    if (!m_bRunning) return;
    m_bRunning = false;

    // Notify Steam master server we are going offline
    SteamGameServer()->SetAdvertiseServerActive(false);

    if (m_networkPollThread.joinable())
    {
        m_networkPollThread.join();
    }

    spdlog::info("Server: Shutting down...");

    // Close all client connections
    {
        std::lock_guard<std::mutex> lock(m_mutexClientData);
        for (auto const& [connHandle, clientData] : m_mapClientData) {
            if (clientData.m_eAuthState == ClientConnectionData_t::AUTH_VALIDATED) {
                 SteamGameServer()->EndAuthSession(clientData.m_steamID);
                 spdlog::info("Server: Ended auth session for SteamID {}.", clientData.m_steamID.ConvertToUint64());
            }
            m_pInterface->CloseConnection(connHandle, 0, "Server shutting down", true);
        }
        m_mapClientData.clear();
    }


    if (m_hListenSocket != k_HSteamListenSocket_Invalid) {
        m_pInterface->CloseListenSocket(m_hListenSocket);
        m_hListenSocket = k_HSteamListenSocket_Invalid;
        spdlog::info("Server: Listen socket closed.");
    }
    if (m_hPollGroup != k_HSteamNetPollGroup_Invalid) {
        m_pInterface->DestroyPollGroup(m_hPollGroup);
        m_hPollGroup = k_HSteamNetPollGroup_Invalid;
        spdlog::info("Server: Poll group destroyed.");
    }

    SteamGameServer()->LogOff();
    SteamGameServer_Shutdown();
    spdlog::info("Server: SteamGameServer has been shut down.");
}

void Server::RunCallbacks() {
    if (!m_bRunning) return;
    SteamGameServer_RunCallbacks(); // Process Steam API callbacks
}

void Server::PollNetwork() {
    if (!m_bRunning || !m_pInterface || m_hPollGroup == k_HSteamNetPollGroup_Invalid) {
        return;
    }

    ISteamNetworkingMessage* pIncomingMsgs[MAX_MESSAGES_PER_POLL_SERVER];
    int numMsgs = m_pInterface->ReceiveMessagesOnPollGroup(m_hPollGroup, pIncomingMsgs, MAX_MESSAGES_PER_POLL_SERVER);

    if (numMsgs < 0) {
        spdlog::error("Server: Error polling for messages on poll group.");
        // Potentially handle this more gracefully, e.g., by re-initializing or shutting down
        return;
    }

    for (int i = 0; i < numMsgs; ++i) {
        if (pIncomingMsgs[i]) {
            HSteamNetConnection hConn = pIncomingMsgs[i]->m_conn;
            {
                std::lock_guard<std::mutex> lock(m_mutexClientData);
                if (m_mapClientData.count(hConn)) { // Ensure client is still considered connected
                    ProcessMessageFromClient(hConn, static_cast<const uint8*>(pIncomingMsgs[i]->m_pData), pIncomingMsgs[i]->m_cbSize);
                } else {
                    spdlog::warn("Server: Received message from unknown or disconnected connection {}. Discarding.", hConn);
                }
            }
            pIncomingMsgs[i]->Release(); // Important to release the message
        }
    }
}

void Server::SendMessageToClient(HSteamNetConnection hConn, const std::string& message) {
    if (!m_pInterface) return;
    EResult res = m_pInterface->SendMessageToConnection(hConn, message.c_str(), (uint32)message.length(), k_nSteamNetworkingSend_Reliable, nullptr);
    if (res == k_EResultOK) {
        // spdlog::info("Server: Sent message to {}: '{}'", hConn, message); // Can be spammy
    } else {
        spdlog::error("Server: Failed to send message to {}. Error: {}", hConn, res);
    }
}

void Server::BroadcastMessage(const std::string& message) {
    std::lock_guard<std::mutex> lock(m_mutexClientData);
    for (auto const& [connHandle, clientData] : m_mapClientData) {
        // Only send to fully authenticated clients, or adjust as needed
        if (clientData.m_eAuthState == ClientConnectionData_t::AUTH_VALIDATED) {
            SendMessageToClient(connHandle, message);
        }
    }
}


void Server::OnSteamNetConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pCallback) {
    HSteamNetConnection hConn = pCallback->m_hConn;
    const SteamNetConnectionInfo_t& info = pCallback->m_info;
    ESteamNetworkingConnectionState eOldState = pCallback->m_eOldState;
    ESteamNetworkingConnectionState eNewState = info.m_eState;

    spdlog::info("Server: Connection status changed for {}. Old: {}, New: {}, EndReason: {}, Desc: '{}'",
                 hConn, eOldState, eNewState, info.m_eEndReason, info.m_szEndDebug);

    std::lock_guard<std::mutex> lock(m_mutexClientData);

    if (eOldState == k_ESteamNetworkingConnectionState_None && eNewState == k_ESteamNetworkingConnectionState_Connecting) {
        // A new client is attempting to connect
        if (info.m_hListenSocket == m_hListenSocket) { // Check if it's from our listen socket
            if (m_mapClientData.size() >= 100) { // Example: Max 100 clients

                char addrStr[SteamNetworkingIPAddr::k_cchMaxString];
                pCallback->m_info.m_addrRemote.ToString(addrStr, sizeof(addrStr), true);
                spdlog::warn("Server: Max clients reached. Rejecting new connection from {}.", addrStr);
                m_pInterface->CloseConnection(hConn, 0, "Server full", false);
                return;
            }

            EResult res = m_pInterface->AcceptConnection(hConn);
            if (res == k_EResultOK) {
                if (!m_pInterface->SetConnectionPollGroup(hConn, m_hPollGroup)) {
                     spdlog::warn("Server: Failed to add connection {} to poll group.", hConn);
                     // Potentially close connection if can't be added to poll group
                }
                ClientConnectionData_t newData;
                newData.m_hConnection = hConn;
                // Get identity (SteamID) when connection is established
                // For now, initialize with invalid SteamID
                m_mapClientData[hConn] = newData;
                // TODO : fix below
                //spdlog::info("Server: Accepted connection from {}. Added to map. Total clients: {}", SteamNetworkingIPAddr_ToString(info.m_addrRemote, true), m_mapClientData.size());
            } else {
                spdlog::error("Server: Failed to accept connection {}. Error: {}", hConn, res);
                m_pInterface->CloseConnection(hConn, 0, "Accept failed", false); // Clean up
            }
        } else {
             spdlog::warn("Server: Connection status change for {} not from our listen socket. Ignoring.", hConn);
        }
    } else if (eNewState == k_ESteamNetworkingConnectionState_Connected) {
        // Client successfully connected
        if (m_mapClientData.count(hConn)) {
            // The m_identityRemote is available in info struct on connected state.
            // However, for BeginAuthSession, we need the client to send us their auth ticket first.
            // The SteamID from info.m_identityRemote is what we expect to be authenticated.
            if (!info.m_identityRemote.IsInvalid()) {
                 m_mapClientData[hConn].m_steamID = info.m_identityRemote.GetSteamID();
                 spdlog::info("Server: Connection {} ({}) is now fully connected. Waiting for auth ticket.", hConn, m_mapClientData[hConn].m_steamID.ConvertToUint64());
            } else {
                 spdlog::warn("Server: Connection {} connected but has invalid remote identity.", hConn);
                 // This might happen if connecting not through Steam relay or not a Steam user
                 // For this example, we expect Steam users.
                 m_pInterface->CloseConnection(hConn, 0, "Invalid identity", true);
                 m_mapClientData.erase(hConn);
                 return;
            }
            // Send a welcome message; client should respond with auth ticket
            SendMessageToClient(hConn, "WELCOME_SEND_AUTH_TICKET");
        } else {
            spdlog::warn("Server: Connection {} reported 'Connected' but not in our map. This shouldn't happen if accept logic is correct.", hConn);
        }
    } else if (eNewState == k_ESteamNetworkingConnectionState_ClosedByPeer ||
               eNewState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally) {
        // Client disconnected or connection lost
        if (m_mapClientData.count(hConn)) {
            HandleClientDisconnection(hConn, info);
        } else {
            // spdlog::info("Server: Connection {} closed/problem, but was not in our map (already handled or unknown).", hConn);
        }
    }
}

void Server::HandleClientDisconnection(HSteamNetConnection hConn, const SteamNetConnectionInfo_t& info) {
    // Assumes m_mutexClientData is already locked if called from OnSteamNetConnectionStatusChanged
    // If called from elsewhere, lock it.
    if (m_mapClientData.count(hConn)) {
        ClientConnectionData_t& clientData = m_mapClientData[hConn];
        spdlog::info("Server: Client {} (SteamID: {}) disconnected. Reason: {}. Debug: '{}'",
                     hConn,
                     clientData.m_steamID.IsValid() ? std::to_string(clientData.m_steamID.ConvertToUint64()) : "N/A",
                     info.m_eEndReason,
                     info.m_szEndDebug);

        if (clientData.m_eAuthState == ClientConnectionData_t::AUTH_VALIDATED && clientData.m_steamID.IsValid()) {
            SteamGameServer()->EndAuthSession(clientData.m_steamID);
            spdlog::info("Server: Ended auth session for SteamID {}.", clientData.m_steamID.ConvertToUint64());
        }
        m_pInterface->CloseConnection(hConn, 0, nullptr, false); // Ensure closed, no linger
        m_mapClientData.erase(hConn);
        spdlog::info("Server: Client {} removed from map. Total clients: {}", hConn, m_mapClientData.size());
    }
}


void Server::ProcessMessageFromClient(HSteamNetConnection hConn, const uint8* data, uint32 size) {
    // Assumes m_mutexClientData is locked
    if (!m_mapClientData.count(hConn)) {
        spdlog::warn("Server: Message from unknown connection {}. Ignoring.", hConn);
        return;
    }

    ClientConnectionData_t& clientData = m_mapClientData[hConn];

    // First message from client after "WELCOME" should be the auth ticket
    if (clientData.m_eAuthState == ClientConnectionData_t::AUTH_PENDING || clientData.m_eAuthState == ClientConnectionData_t::AUTH_TICKET_RECEIVED) {
        if (size > sizeof(uint32)) {
            // Use the manual conversion to read the size from network byte order
            // 'data' is const uint8_t* pointing to the start of the size field
            uint32 ticketDataSize = ManualNetToHost32(data);

            if (ticketDataSize > 0 && (size == sizeof(uint32) + ticketDataSize)) {
                clientData.m_authTicketData.resize(ticketDataSize);
                // Copy the actual ticket data, which starts after the 4-byte size field
                memcpy(clientData.m_authTicketData.data(), data + sizeof(uint32), ticketDataSize);
                clientData.m_eAuthState = ClientConnectionData_t::AUTH_TICKET_RECEIVED;
                // ... (rest of the logic) ...
            }
            else {
                spdlog::warn("Server: Received malformed auth ticket message from {}. Size in msg: {}, Total msg size: {}.", hConn, ticketDataSize, size);
                return;
            }
        }
        else {
            spdlog::warn("Server: Received very small message from {} when expecting auth ticket. Size: {}", hConn, size);
            return;
        }
    }

    // Handle other messages if client is authenticated
    if (clientData.m_eAuthState == ClientConnectionData_t::AUTH_VALIDATED) {
        std::string message(reinterpret_cast<const char*>(data), size);
        spdlog::info("Server: Received from client {} (SteamID {}): '{}'", hConn, clientData.m_steamID.ConvertToUint64(), message);

        // Example: Echo back or handle game logic
        // SendMessageToClient(hConn, "Server received: " + message);
        if (message == "HELLO_SERVER") {
            SendMessageToClient(hConn, "SERVER_SAYS_HI_CLIENT");
        }

    } else if (clientData.m_eAuthState == ClientConnectionData_t::AUTH_FAILED) {
        spdlog::warn("Server: Message from client {} whose auth failed. Ignoring.", hConn);
    } else {
         spdlog::info("Server: Message from client {} (SteamID {}) but auth not yet validated. State: {}. Queuing or ignoring.", hConn, clientData.m_steamID.ConvertToUint64(), clientData.m_eAuthState);
         // Potentially queue messages or disconnect if too many before auth.
    }
}


void Server::OnValidateAuthTicketResponse(ValidateAuthTicketResponse_t* pCallback) {
    spdlog::info("Server: ValidateAuthTicketResponse received. SteamID: {}, AuthSessionResponse: {}, OwnerSteamID: {}",
                 pCallback->m_SteamID.ConvertToUint64(),
                 pCallback->m_eAuthSessionResponse,
                 pCallback->m_OwnerSteamID.ConvertToUint64()); // Owner is useful for DLC/game ownership checks

    std::lock_guard<std::mutex> lock(m_mutexClientData);
    // Find the client by SteamID. This assumes SteamID is unique and was correctly set.
    // A reverse map from CSteamID to HSteamNetConnection might be useful if multiple connections
    // could race for auth with the same SteamID (shouldn't happen with proper connection handling).
    HSteamNetConnection hFoundConn = k_HSteamNetConnection_Invalid;
    for (auto const& [connHandle, clientRef] : m_mapClientData) {
        if (clientRef.m_steamID == pCallback->m_SteamID && clientRef.m_eAuthState == ClientConnectionData_t::AUTH_TICKET_RECEIVED) {
            hFoundConn = connHandle;
            break;
        }
    }

    if (hFoundConn != k_HSteamNetConnection_Invalid) {
        ClientConnectionData_t& clientData = m_mapClientData[hFoundConn];
        if (pCallback->m_eAuthSessionResponse == k_EAuthSessionResponseOK) {
            clientData.m_eAuthState = ClientConnectionData_t::AUTH_VALIDATED;
            // Check if the owner SteamID matches the connecting SteamID if necessary.
            // For simple auth, m_SteamID being validated is usually enough.
            if (pCallback->m_SteamID == pCallback->m_OwnerSteamID) {
                 spdlog::info("Server: Auth validated for SteamID {} (Conn {}). Owner matches.", pCallback->m_SteamID.ConvertToUint64(), hFoundConn);
                 SendMessageToClient(hFoundConn, "AUTH_SUCCESSFUL_WELCOME_PLAYER");
            } else {
                 // This case is more about family sharing or other complex scenarios.
                 // For basic game server auth, usually m_SteamID == m_OwnerSteamID.
                 spdlog::warn("Server: Auth validated for SteamID {} but OwnerSteamID is {} (Conn {}). Treating as valid for this example.",
                              pCallback->m_SteamID.ConvertToUint64(), pCallback->m_OwnerSteamID.ConvertToUint64(), hFoundConn);
                 clientData.m_eAuthState = ClientConnectionData_t::AUTH_VALIDATED; // Still mark as validated if you allow this
                 SendMessageToClient(hFoundConn, "AUTH_SUCCESSFUL_WELCOME_PLAYER (owner mismatch noted)");
            }
        } else {
            clientData.m_eAuthState = ClientConnectionData_t::AUTH_FAILED;
            spdlog::error("Server: Auth failed for SteamID {} (Conn {}). Response: {}. Disconnecting.",
                          pCallback->m_SteamID.ConvertToUint64(), hFoundConn, pCallback->m_eAuthSessionResponse);
            SendMessageToClient(hFoundConn, "AUTH_FAILED_VALIDATION");
            // Close connection; status change callback will clean up map entry
            m_pInterface->CloseConnection(hFoundConn, 0, "Auth validation failed", true);
        }
    } else {
        spdlog::warn("Server: Received ValidateAuthTicketResponse for SteamID {} but no matching client in AUTH_TICKET_RECEIVED state found. Possibly late or mismatched.", pCallback->m_SteamID.ConvertToUint64());
        // If the client disconnected before this callback, EndAuthSession might already have been called or will be.
        // If BeginAuthSession was called, we should call EndAuthSession if it's not a success to clean up Steam's state.
        // However, we need the client's original CSteamID used with BeginAuthSession.
        // This callback provides m_SteamID, so we can use that.
        if (pCallback->m_eAuthSessionResponse != k_EAuthSessionResponseOK) {
             // End session if validation failed and we can't find the client record to handle it.
             // This prevents lingering auth sessions on Steam backend if client dropped unexpectedly.
             SteamGameServer()->EndAuthSession(pCallback->m_SteamID);
             spdlog::info("Server: Called EndAuthSession for SteamID {} due to failed validation for an untracked/late response.", pCallback->m_SteamID.ConvertToUint64());
        }
    }
}

// --- Game Server Connection to Steam Backend Callbacks ---
void Server::OnSteamServersConnected(SteamServersConnected_t* pCallback) {
    spdlog::info("Server: Successfully connected to Steam services.");
    // This is a good place to log on if you're using GSLTs (Game Server Login Tokens)
    // SteamGameServer()->LogOn("YOUR_GSLT_TOKEN_HERE");
    // For anonymous login, it's often done during init.
}

void Server::OnSteamServersDisconnected(SteamServersDisconnected_t* pCallback) {
    spdlog::warn("Server: Disconnected from Steam services. Result: {}", pCallback->m_eResult);
    // Server might still function for LAN or direct IP clients if Steam connection is not strictly required
    // for gameplay, but auth and other Steam features will be unavailable.
}

void Server::OnSteamServerConnectFailure(SteamServerConnectFailure_t* pCallback) {
    spdlog::error("Server: Failed to connect to Steam services. Result: {}. Still retrying: {}",
                  pCallback->m_eResult, pCallback->m_bStillRetrying);
    // If m_bStillRetrying is false, then the GameServer has given up.
}