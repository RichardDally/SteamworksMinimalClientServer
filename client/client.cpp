#include "client.h"
#include <spdlog/spdlog.h>
#include <steam/steamnetworkingtypes.h> // For SteamNetworkingIPAddr
#include <chrono> // For std::this_thread::sleep_for

constexpr uint32 MAX_MESSAGES_PER_POLL = 20;

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

    spdlog::info("Client: Attempting to connect to server {}:{}", serverAddress, serverPort);
    // No custom options needed for this minimal example if relying on STEAM_CALLBACK
    m_hConnection = m_pInterface->ConnectByIPAddress(serverAddr, 0, nullptr);

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

    // Example: if server sends "WELCOME", client sends "HELLO_SERVER_AUTH_TICKET"
    if (message.rfind("WELCOME", 0) == 0) {
        spdlog::info("Client: Server welcomed us. Sending auth ticket.");
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

// This is the C-style callback, dispatching to the member function
void Client::SteamNetConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pCallback) {
    // We need to find the Client instance. This is tricky without global state or a map.
    // For this minimal example, if we only have one client instance, we might get away
    // with a static pointer, but that's not ideal.
    // A better way for multi-instance would be a static map of HSteamNetConnection to Client*.
    // For now, assuming a single global-like Client instance handled by main.
    // This part needs a proper way to get the `this` pointer if Client object isn't singleton-like.
    // If client_main creates one Client, it can call this method on it.
    // Or, the callback struct can be a member of Client:
    // CCallback<MyClass, SteamNetConnectionStatusChangedCallback_t, false> m_SteamNetConnectionStatusChangedCallback;
    // m_SteamNetConnectionStatusChangedCallback.Register(this, &MyClass::OnSteamNetConnectionStatusChanged);
    // But this example uses STEAM_CALLBACK.

    // The STEAM_CALLBACK macro handles this dispatch for us if it's a member.
    OnSteamNetConnectionStatusChanged(pCallback);
}

void Client::OnSteamNetConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pCallback) {
    spdlog::info("Client: Connection status changed. Conn: {}, PrevState: {}, State: {}, EndReason: {}",
                 pCallback->m_hConn,
                 pCallback->m_eOldState,
                 pCallback->m_info.m_eState,
                 pCallback->m_info.m_eEndReason);

    // Is this connection our HSteamNetConnection?
    if (pCallback->m_hConn == m_hConnection || m_hConnection == k_HSteamNetConnection_Invalid) {
        switch (pCallback->m_info.m_eState) {
            case k_ESteamNetworkingConnectionState_None:
            case k_ESteamNetworkingConnectionState_ClosedByPeer:
            case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
                spdlog::info("Client: Connection closed or failed. Reason: {}. Info: {}", pCallback->m_info.m_eEndReason, pCallback->m_info.m_szEndDebug);
                if (m_hConnection == pCallback->m_hConn) { // Check if it's our active connection
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
                spdlog::info("Client: Successfully connected to server!");
                m_bConnected = true;
                m_bAttemptingConnection = false;
                m_hConnection = pCallback->m_hConn; // Ensure we store the actual connection handle

                // Start a thread to poll for messages
                // Note: This is a simple approach. A more robust system might integrate polling into the main loop
                // or use a dedicated I/O service.
                if (m_networkThread.joinable()) m_networkThread.join(); // Should not happen if logic is correct
                m_networkThread = std::thread([this]() {
                    while (m_bRunning && m_bConnected) {
                        PollIncomingMessages();
                        std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Poll frequently
                    }
                    spdlog::info("Client: Network polling thread exiting.");
                });
                break;
        }
    }
}