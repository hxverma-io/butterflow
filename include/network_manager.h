#ifndef BUTTERFLOW_NETWORK_MANAGER_H
#define BUTTERFLOW_NETWORK_MANAGER_H

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <functional>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
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
    bool HasPeer() const;
    std::string GetPeerIP() const;
    std::string GetPeerDeviceName() const;
    uint16_t GetPort() const { return m_port; }

    uint64_t GetInstanceId() const { return m_instanceId; }
    DWORD GetLastPeerActivity() const { return m_lastPeerActivityTick.load(); }
    void SetLocalDeviceName(const std::string& name) { m_localDeviceName = name; }

    // Packet Senders
    void SendDiscoveryBeacon(const DiscoveryPayload& payload);
    void SendHandshakeRequest();
    void SendHandshakeAccept();
    void SendHeartbeat();
    void SendDisconnect();
    void SendEdgeApproach(ScreenEdge edge, float progress, float normY);
    void SendCursorEnter(ScreenEdge edge, float normX, float normY);
    void SendCursorLeave(float normY);
    void SendMouseInput(const MouseInputPayload& payload);
    void SendKeyboardInput(const KeyboardInputPayload& payload);
    void SendClipboardText(const std::string& text);

    // Poll / Process Incoming Network Events (non-blocking fallback)
    void Poll();

    // Telemetry & Discovery Info
    uint64_t GetPacketsSent() const { return m_packetsSent.load(); }
    uint64_t GetPacketsReceived() const { return m_packetsReceived.load(); }
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
    uint64_t m_instanceId;
    std::string m_localIpStr;
    std::string m_localDeviceName;

    sockaddr_in m_localAddr;
    sockaddr_in m_peerAddr;
    sockaddr_in m_broadcastAddr;
    bool m_hasPeer;
    std::string m_peerIpStr;
    std::string m_peerDeviceName;
    mutable std::mutex m_peerMutex;

    std::atomic<uint32_t> m_sequence;
    std::atomic<uint64_t> m_packetsSent;
    std::atomic<uint64_t> m_packetsReceived;
    std::atomic<DWORD> m_lastPeerActivityTick;

    std::vector<DiscoveredPeerInfo> m_discoveredPeers;
    mutable std::mutex m_peersMutex;

    std::atomic<bool> m_running;
    std::thread m_recvThread;

    ApproachHandler m_onApproach;
    TransitionHandler m_onTransition;
    MouseHandler m_onMouse;
    KeyHandler m_onKey;
    LeaveHandler m_onLeave;
    PeerFoundHandler m_onPeerFound;

    void RecvThreadFunc();
    void ProcessIncomingPacket(const uint8_t* data, int len, const sockaddr_in& senderAddr);
    void SendRawPacket(PacketType type, const void* payload, uint16_t size);
    void SendRawPacketTo(PacketType type, const void* payload, uint16_t size, const sockaddr_in& targetAddr);
};

#endif // BUTTERFLOW_NETWORK_MANAGER_H
