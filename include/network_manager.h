#ifndef BUTTERFLOW_NETWORK_MANAGER_H
#define BUTTERFLOW_NETWORK_MANAGER_H

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <functional>
#include <string>
#include <vector>
#include <mutex>
#include "protocol.h"

struct DiscoveredPeerInfo {
    std::string ip;
    uint16_t port;
    char deviceName[32];
    uint16_t screenWidth;
    uint16_t screenHeight;
    uint8_t preferredEdge;
    DWORD lastSeenTick;
};

class NetworkManager {
public:
    using ApproachHandler = std::function<void(const EdgeApproachPayload&)>;
    using TransitionHandler = std::function<void(const CursorTransitionPayload&)>;
    using MouseHandler = std::function<void(const MouseInputPayload&)>;
    using KeyHandler = std::function<void(const KeyboardInputPayload&)>;
    using LeaveHandler = std::function<void(float normY)>;
    using PeerFoundHandler = std::function<void(const std::string& peerIp, const DiscoveryPayload&)>;

    NetworkManager();
    ~NetworkManager();

    bool Start(uint16_t port = BUTTERFLOW_PORT);
    void Stop();

    void SetPeerAddress(const std::string& ip, uint16_t port = BUTTERFLOW_PORT);
    void DisconnectPeer();
    bool HasPeer() const { return m_hasPeer; }
    std::string GetPeerIP() const { return m_peerIpStr; }
    std::string GetPeerDeviceName() const { return m_peerDeviceName; }
    uint16_t GetPort() const { return m_port; }

    // Packet Senders
    void SendDiscoveryBeacon(const DiscoveryPayload& payload);
    void SendEdgeApproach(ScreenEdge edge, float progress, float normY);
    void SendCursorEnter(ScreenEdge edge, float normX, float normY);
    void SendCursorLeave(float normY);
    void SendMouseInput(const MouseInputPayload& payload);
    void SendKeyboardInput(const KeyboardInputPayload& payload);
    void SendClipboardText(const std::string& text);

    // Poll / Process Incoming Network Events (non-blocking)
    void Poll();

    // Telemetry & Discovery Info
    uint64_t GetPacketsSent() const { return m_packetsSent; }
    uint64_t GetPacketsReceived() const { return m_packetsReceived; }
    std::vector<DiscoveredPeerInfo> GetDiscoveredPeers() const;
    std::string GetLocalIP() const;

    // Handlers
    void SetApproachHandler(ApproachHandler handler) { m_onApproach = handler; }
    void SetTransitionHandler(TransitionHandler handler) { m_onTransition = handler; }
    void SetMouseHandler(MouseHandler handler) { m_onMouse = handler; }
    void SetKeyHandler(KeyHandler handler) { m_onKey = handler; }
    void SetLeaveHandler(LeaveHandler handler) { m_onLeave = handler; }
    void SetPeerFoundHandler(PeerFoundHandler handler) { m_onPeerFound = handler; }

private:
    SOCKET m_socket;
    uint16_t m_port;
    sockaddr_in m_localAddr;
    sockaddr_in m_peerAddr;
    sockaddr_in m_broadcastAddr;
    bool m_hasPeer;
    std::string m_peerIpStr;
    std::string m_peerDeviceName;
    uint32_t m_sequence;
    uint64_t m_packetsSent;
    uint64_t m_packetsReceived;

    std::vector<DiscoveredPeerInfo> m_discoveredPeers;
    mutable std::mutex m_peersMutex;

    ApproachHandler m_onApproach;
    TransitionHandler m_onTransition;
    MouseHandler m_onMouse;
    KeyHandler m_onKey;
    LeaveHandler m_onLeave;
    PeerFoundHandler m_onPeerFound;

    void SendRawPacket(PacketType type, const void* payload, uint16_t size);
};

#endif // BUTTERFLOW_NETWORK_MANAGER_H
