#include "client.h"
#include <spdlog/spdlog.h>
#include <steam/steamnetworkingtypes.h> // For SteamNetworkingIPAddr
#include <chrono> // For std::this_thread::sleep_for

constexpr uint32 MAX_MESSAGES_PER_POLL = 20;

namespace
{
    static const char* ConnectionStateToString(const ESteamNetworkingConnectionState eState)
    {
        switch (eState)
        {
        case k_ESteamNetworkingConnectionState_None: return "None (0)";
        case k_ESteamNetworkingConnectionState_Connecting: return "Connecting (1)";
        case k_ESteamNetworkingConnectionState_FindingRoute: return "FindingRoute (2)";
        case k_ESteamNetworkingConnectionState_Connected: return "Connected (3)";
        case k_ESteamNetworkingConnectionState_ClosedByPeer: return "ClosedByPeer (4)";
        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally: return "ProblemDetectedLocally (5)";
        case k_ESteamNetworkingConnectionState_FinWait: return "FinWait (6)";       // Usually not seen by app
        case k_ESteamNetworkingConnectionState_Linger: return "Linger (7)";        // Usually not seen by app
        case k_ESteamNetworkingConnectionState_Dead: return "Dead (8)";          // Usually not seen by app
        default:
            // Create a static buffer to hold unknown state string to avoid returning temporary
            // or handle it differently if you expect many unknown values.
            // For simplicity here, we'll just use a generic unknown.
            // A more robust solution might use a thread-local static buffer if this function
            // could be called from multiple threads simultaneously and you wanted to return
            // "Unknown (value)". But for spdlog, this should be fine.
            return "UnknownState";
        }
    }

    const char* EResultToString(const EResult eResult)
    {
        switch (eResult)
        {
        case k_EResultOK: return "OK (1)";
        case k_EResultFail: return "Fail (2)"; // Generic failure
        case k_EResultNoConnection: return "NoConnection (3)"; // Not connected to Steam
        case k_EResultInvalidParam: return "InvalidParam (8)"; // E.g., hConn was invalid
        case k_EResultBusy: return "Busy (10)"; // System is busy, try again
        case k_EResultInvalidState: return "InvalidState (11)"; // Operation not allowed in current state
        case k_EResultAccessDenied: return "AccessDenied (15)"; // Operation denied
        case k_EResultTimeout: return "Timeout (16)";
        case k_EResultServiceUnavailable: return "ServiceUnavailable (20)";
        case k_EResultNotLoggedOn: return "NotLoggedOn (21)";
        case k_EResultPending: return "Pending (22)"; // Asynchronous operation is pending
        case k_EResultLimitExceeded: return "LimitExceeded (25)"; // E.g., too many connections for this poll group / server
            // Add more EResult codes here as you encounter them or deem them relevant
            // from steamclientpublic.h (included via steam_gameserver.h)
        default: return "Unknown/Other EResult";
        }
    }
}

inline void ManualHostToNet32(uint32_t host_value, uint8_t* network_bytes_out) {
    network_bytes_out[0] = (host_value >> 24) & 0xFF;
    network_bytes_out[1] = (host_value >> 16) & 0xFF;
    network_bytes_out[2] = (host_value >> 8) & 0xFF;
    network_bytes_out[3] = (host_value) & 0xFF;
}

Client::Client()
    : m_hConnection(k_HSteamNetConnection_Invalid),
      m_pInterface(nullptr),
      m_hAuthTicket(k_HAuthTicketInvalid),
      m_bConnected(false),
      m_bAttemptingConnection(false),
      m_bAuthenticated(false),
      m_bRunning(false),
      m_unAuthTicketSize(0) {
    m_authTicketBuffer.resize(m_unAuthTicketBufferSize);
}

Client::~Client() {
    if (m_bRunning) {
        ShutdownSteam(); // Ensure Steam is shut down if not already
    }
}

bool Client::InitializeSteam() {
    if (SteamAPI_Init()) {
        spdlog::info("Client: SteamAPI_Init() successful.");
        m_pInterface = SteamNetworkingSockets();
        if (!m_pInterface) {
            spdlog::error("Client: SteamNetworkingSockets() failed to initialize.");
            SteamAPI_Shutdown();
            return false;
        }

        // Get an auth session ticket
        // For this example, we get it once. In a real game, you might refresh it.
        // The ticket is for this client user to be authenticated by the server.
        m_hAuthTicket = SteamUser()->GetAuthSessionTicket(m_authTicketBuffer.data(), m_unAuthTicketBufferSize, &m_unAuthTicketSize, nullptr);
        if (m_hAuthTicket == k_HAuthTicketInvalid || m_unAuthTicketSize == 0) {
            spdlog::error("Client: GetAuthSessionTicket failed. Ticket handle: {}, Size: {}", m_hAuthTicket, m_unAuthTicketSize);
            SteamAPI_Shutdown();
            return false;
        }
        spdlog::info("Client: Auth session ticket obtained. Handle: {}, Size: {}", m_hAuthTicket, m_unAuthTicketSize);

        m_bRunning = true;
        return true;
    }
    spdlog::error("Client: SteamAPI_Init() failed. Is Steam running?");
    return false;
}

void Client::ShutdownSteam() {
    if (!m_bRunning) return;

    m_bRunning = false;
    if (m_networkThread.joinable()) {
        m_networkThread.join();
    }

    Disconnect(); // Ensure connection is closed

    if (m_hAuthTicket != k_HAuthTicketInvalid) {
        SteamUser()->CancelAuthTicket(m_hAuthTicket);
        m_hAuthTicket = k_HAuthTicketInvalid;
        spdlog::info("Client: Auth ticket cancelled.");
    }

    spdlog::info("Client: Shutting down SteamAPI.");
    SteamAPI_Shutdown();
    m_bConnected = false;
}

bool Client::Connect(const char* serverAddress, uint16 serverPort) {
    if (!m_pInterface || m_bConnected || m_bAttemptingConnection) {
        spdlog::warn("Client: Cannot connect. Already connected, attempting connection, or interface not initialized.");
        return false;
    }

    SteamNetworkingIPAddr serverAddr;
    if (!serverAddr.ParseString(serverAddress)) {
        spdlog::error("Client: Invalid server address format: {}", serverAddress);
        return false;
    }
    serverAddr.m_port = serverPort;

    spdlog::info("=== Step 1: Initiating connection to server {}:{} ===", serverAddress, serverPort);
    // No custom options needed for this minimal example if relying on STEAM_CALLBACK

    // WITHOUT AUTHENTICATION
    SteamNetworkingConfigValue_t arrConnectionOptions[1];
    arrConnectionOptions[0].SetInt32(k_ESteamNetworkingConfig_IP_AllowWithoutAuth, 1);
    m_hConnection = m_pInterface->ConnectByIPAddress(serverAddr, 1, arrConnectionOptions);

    // WITH AUTHENTICATION
    //m_hConnection = m_pInterface->ConnectByIPAddress(serverAddr, 0, nullptr);

    if (m_hConnection == k_HSteamNetConnection_Invalid) {
        spdlog::error("Client: Failed to initiate connection.");
        return false;
    }
    m_bAttemptingConnection = true;
    return true;
}

void Client::Disconnect() {
    if (m_hConnection != k_HSteamNetConnection_Invalid) {
        spdlog::info("Client: Closing connection {}...", m_hConnection);
        m_pInterface->CloseConnection(m_hConnection, 0, "Client disconnecting", true);
        m_hConnection = k_HSteamNetConnection_Invalid;
        m_bConnected = false;
        m_bAttemptingConnection = false;
    }
}

void Client::RunCallbacks() {
    if (!m_bRunning) return;
    SteamAPI_RunCallbacks(); // Handles Steam callbacks like connection status
}

void Client::PollIncomingMessages() {
    if (!m_bConnected || !m_pInterface || m_hConnection == k_HSteamNetConnection_Invalid) {
        return;
    }

    ISteamNetworkingMessage* pIncomingMsg[MAX_MESSAGES_PER_POLL];
    int numMsgs = m_pInterface->ReceiveMessagesOnConnection(m_hConnection, pIncomingMsg, MAX_MESSAGES_PER_POLL);
    if (numMsgs < 0) {
        spdlog::error("Client: Error checking for messages.");
        Disconnect(); // Or handle error more gracefully
        return;
    }

    for (int i = 0; i < numMsgs; ++i) {
        if (pIncomingMsg[i]) {
            ProcessMessage(static_cast<const uint8*>(pIncomingMsg[i]->m_pData), pIncomingMsg[i]->m_cbSize);
            pIncomingMsg[i]->Release(); // Important to release the message
        }
    }
}


void Client::ProcessMessage(const uint8* data, uint32 size) {
    std::string message(reinterpret_cast<const char*>(data), size);
    spdlog::info("Client: Received message from server: '{}'", message);

    // Check for authentication success (Step 5 & 6 happen on server side)
    if (message.rfind("AUTH_SUCCESSFUL", 0) == 0) {
        spdlog::info("=== Step 7: Received AUTH_SUCCESSFUL from server ===");
        spdlog::info("=== Authentication complete! Client is now authenticated ===");
        m_bAuthenticated = true;
        return;
    }

    // Example: if server sends "WELCOME", client sends "HELLO_SERVER_AUTH_TICKET"
    if (message.rfind("WELCOME", 0) == 0) {
        spdlog::info("=== Step 3: Received WELCOME from server ===");
        spdlog::info("=== Step 4: Sending auth ticket to server ===");
        std::vector<uint8> ticketMessage;
        ticketMessage.resize(sizeof(uint32) + m_unAuthTicketSize);

        // Use the manual conversion to write the size in network byte order
        ManualHostToNet32(m_unAuthTicketSize, ticketMessage.data());

        // Copy the actual ticket data after the size
        memcpy(ticketMessage.data() + sizeof(uint32), m_authTicketBuffer.data(), m_unAuthTicketSize);

        EResult res = m_pInterface->SendMessageToConnection(m_hConnection, ticketMessage.data(), ticketMessage.size(), k_nSteamNetworkingSend_Reliable, nullptr);
        if (res == k_EResultOK) {
            spdlog::info("Client: Auth ticket sent to server ({} bytes).", ticketMessage.size());
        }
        else {
            spdlog::error("Client: Failed to send auth ticket to server. Error: {}", res);
        }
    }
}

void Client::SendMessageToServer(const std::string& message) {
    if (!m_bConnected || !m_pInterface || m_hConnection == k_HSteamNetConnection_Invalid) {
        spdlog::warn("Client: Not connected, cannot send message.");
        return;
    }
    EResult res = m_pInterface->SendMessageToConnection(m_hConnection, message.c_str(), (uint32)message.length(), k_nSteamNetworkingSend_Reliable, nullptr);
    if (res == k_EResultOK) {
        spdlog::info("Client: Sent message: '{}'", message);
    } else {
        spdlog::error("Client: Failed to send message. Error: {}", res);
    }
}

bool Client::IsConnected() const {
    return m_bConnected;
}

bool Client::IsAttemptingConnection() const
{
    return m_bAttemptingConnection;
}

bool Client::IsAuthenticated() const
{
    return m_bAuthenticated;
}

void Client::OnSteamNetConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pCallback) {
    spdlog::info("Client: Connection status changed. Conn: {}, PrevState: {}, State: {}, EndReason: {}",
                 pCallback->m_hConn,
                 ConnectionStateToString(pCallback->m_eOldState),
                 ConnectionStateToString(pCallback->m_info.m_eState),
                 pCallback->m_info.m_eEndReason);

    // Is this connection our HSteamNetConnection?
    if (pCallback->m_hConn == m_hConnection || m_hConnection == k_HSteamNetConnection_Invalid) {
        switch (pCallback->m_info.m_eState) {
            case k_ESteamNetworkingConnectionState_None:
            case k_ESteamNetworkingConnectionState_ClosedByPeer:
            case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
                spdlog::info("Client: Connection closed or failed. Reason: {}. Info: {}", pCallback->m_info.m_eEndReason, pCallback->m_info.m_szEndDebug);
                if (m_hConnection == pCallback->m_hConn)
                {
                    // Check if it's our active connection
                    m_pInterface->CloseConnection(pCallback->m_hConn, 0, nullptr, false);
                    m_hConnection = k_HSteamNetConnection_Invalid;
                    m_bConnected = false;
                    m_bAttemptingConnection = false;
                }
                break;

            case k_ESteamNetworkingConnectionState_Connecting:
                { // Added a scope for addrStr
                    char addrStr[SteamNetworkingIPAddr::k_cchMaxString];
                    pCallback->m_info.m_addrRemote.ToString(addrStr, sizeof(addrStr), true); // true to include port
                    spdlog::info("Client: Connection attempt ongoing to {}.", addrStr);
                }
                m_bAttemptingConnection = true;
                break;

            case k_ESteamNetworkingConnectionState_Connected:
                spdlog::info("=== Step 2: Connection established with server ===");
                m_bConnected = true;
                m_bAttemptingConnection = false;
                m_hConnection = pCallback->m_hConn; // Ensure we store the actual connection handle

                // Start a thread to poll for messages
                // Note: This is a simple approach. A more robust system might integrate polling into the main loop
                // or use a dedicated I/O service.
                /*
                if (m_networkThread.joinable()) m_networkThread.join(); // Should not happen if logic is correct
                m_networkThread = std::thread([this]() {
                    while (m_bRunning && m_bConnected) {
                        PollIncomingMessages();
                        std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Poll frequently
                    }
                    spdlog::info("Client: Network polling thread exiting.");
                });
                */
                break;
        }
    }
}