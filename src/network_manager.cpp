#include "../include/network_manager.h"
#include <iostream>
#include <vector>
#include <random>
#include <chrono>

#pragma comment(lib, "ws2_32.lib")

NetworkManager::NetworkManager()
    : m_socket(INVALID_SOCKET),
      m_port(BUTTERFLOW_PORT),
      m_instanceId(0),
      m_hasPeer(false),
      m_sequence(0),
      m_packetsSent(0),
      m_packetsReceived(0),
      m_lastPeerActivityTick(0),
      m_running(false) {
    memset(&m_localAddr, 0, sizeof(m_localAddr));
    memset(&m_peerAddr, 0, sizeof(m_peerAddr));
    memset(&m_broadcastAddr, 0, sizeof(m_broadcastAddr));

    // Generate cryptographically random 64-bit instance ID
    std::random_device rd;
    std::mt19937_64 gen(rd() ^ static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<uint64_t> dis;
    m_instanceId = dis(gen);

    char hostname[256] = {};
    DWORD hLen = sizeof(hostname);
    if (GetComputerNameA(hostname, &hLen)) {
        m_localDeviceName = hostname;
    }
}

NetworkManager::~NetworkManager() {
    Stop();
}

bool NetworkManager::Start(uint16_t port) {
    m_port = port;
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return false;
    }

    m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_socket == INVALID_SOCKET) {
        return false;
    }

    // Enable Broadcast for automatic discovery on Wi-Fi/Ethernet
    BOOL broadcast = TRUE;
    setsockopt(m_socket, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&broadcast), sizeof(broadcast));

    // Enable Address Reuse
    BOOL reuse = TRUE;
    setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    // Non-blocking mode
    u_long nonBlocking = 1;
    ioctlsocket(m_socket, FIONBIO, &nonBlocking);

    // Minimize send/recv buffer latency (128 KB)
    int bufSize = 131072;
    setsockopt(m_socket, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&bufSize), sizeof(bufSize));
    setsockopt(m_socket, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&bufSize), sizeof(bufSize));

    m_localAddr.sin_family = AF_INET;
    m_localAddr.sin_port = htons(port);
    m_localAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(m_socket, reinterpret_cast<sockaddr*>(&m_localAddr), sizeof(m_localAddr)) == SOCKET_ERROR) {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        return false;
    }

    // Configure Broadcast Target
    m_broadcastAddr.sin_family = AF_INET;
    m_broadcastAddr.sin_port = htons(port);
    m_broadcastAddr.sin_addr.s_addr = INADDR_BROADCAST;

    m_localIpStr = GetLocalIP();

    // Start dedicated high-priority receiver thread
    m_running = true;
    m_recvThread = std::thread(&NetworkManager::RecvThreadFunc, this);

    return true;
}

void NetworkManager::Stop() {
    if (!m_running && m_socket == INVALID_SOCKET) return;

    m_running = false;
    if (m_socket != INVALID_SOCKET) {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }

    if (m_recvThread.joinable()) {
        m_recvThread.join();
    }

    WSACleanup();
}

void NetworkManager::SetPeerAddress(const std::string& ip, uint16_t port) {
    std::lock_guard<std::mutex> lock(m_peerMutex);
    m_peerAddr.sin_family = AF_INET;
    m_peerAddr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &m_peerAddr.sin_addr);
    m_peerIpStr = ip;
    m_hasPeer = true;
    m_lastPeerActivityTick = GetTickCount();
}

void NetworkManager::DisconnectPeer() {
    std::lock_guard<std::mutex> lock(m_peerMutex);
    m_hasPeer = false;
    m_peerIpStr = "";
    m_peerDeviceName = "";
}

bool NetworkManager::HasPeer() const {
    std::lock_guard<std::mutex> lock(m_peerMutex);
    return m_hasPeer;
}

std::string NetworkManager::GetPeerIP() const {
    std::lock_guard<std::mutex> lock(m_peerMutex);
    return m_peerIpStr;
}

std::string NetworkManager::GetPeerDeviceName() const {
    std::lock_guard<std::mutex> lock(m_peerMutex);
    return m_peerDeviceName;
}

void NetworkManager::SendRawPacket(PacketType type, const void* payload, uint16_t size) {
    if (m_socket == INVALID_SOCKET) return;

    sockaddr_in targetAddr;
    {
        std::lock_guard<std::mutex> lock(m_peerMutex);
        if (!m_hasPeer) return;
        targetAddr = m_peerAddr;
    }

    SendRawPacketTo(type, payload, size, targetAddr);
}

void NetworkManager::SendRawPacketTo(PacketType type, const void* payload, uint16_t size, const sockaddr_in& targetAddr) {
    if (m_socket == INVALID_SOCKET) return;

    std::vector<uint8_t> buffer(sizeof(PacketHeader) + size);
    PacketHeader* hdr = reinterpret_cast<PacketHeader*>(buffer.data());
    hdr->magic = BUTTERFLOW_MAGIC;
    hdr->version = BUTTERFLOW_VERSION;
    hdr->type = type;
    hdr->payloadSize = size;
    hdr->sequence = ++m_sequence;

    if (size > 0 && payload != nullptr) {
        memcpy(buffer.data() + sizeof(PacketHeader), payload, size);
    }

    int sent = sendto(m_socket, reinterpret_cast<const char*>(buffer.data()), static_cast<int>(buffer.size()), 0,
                      reinterpret_cast<const sockaddr*>(&targetAddr), sizeof(targetAddr));
    if (sent > 0) {
        m_packetsSent++;
    }
}

void NetworkManager::SendDiscoveryBeacon(const DiscoveryPayload& payload) {
    if (m_socket == INVALID_SOCKET) return;

    DiscoveryPayload beaconWithId = payload;
    beaconWithId.instanceId = m_instanceId;

    SendRawPacketTo(PKT_DISCOVERY_BEACON, &beaconWithId, sizeof(beaconWithId), m_broadcastAddr);
}

void NetworkManager::SendHandshakeRequest() {
    DiscoveryPayload info = {};
    info.instanceId = m_instanceId;
    strncpy(info.deviceName, m_localDeviceName.c_str(), sizeof(info.deviceName) - 1);
    SendRawPacket(PKT_HANDSHAKE_REQUEST, &info, sizeof(info));
}

void NetworkManager::SendHandshakeAccept() {
    DiscoveryPayload info = {};
    info.instanceId = m_instanceId;
    strncpy(info.deviceName, m_localDeviceName.c_str(), sizeof(info.deviceName) - 1);
    SendRawPacket(PKT_HANDSHAKE_ACCEPT, &info, sizeof(info));
}

void NetworkManager::SendHeartbeat() {
    SendRawPacket(PKT_HEARTBEAT, nullptr, 0);
}

void NetworkManager::SendDisconnect() {
    SendRawPacket(PKT_DISCONNECT, nullptr, 0);
}

void NetworkManager::SendEdgeApproach(ScreenEdge edge, float progress, float normY) {
    EdgeApproachPayload payload = { progress, normY, static_cast<uint8_t>(edge) };
    SendRawPacket(PKT_EDGE_APPROACH, &payload, sizeof(payload));
}

void NetworkManager::SendCursorEnter(ScreenEdge edge, float normX, float normY) {
    CursorTransitionPayload payload = { normX, normY, static_cast<uint8_t>(edge) };
    SendRawPacket(PKT_CURSOR_ENTER, &payload, sizeof(payload));
}

void NetworkManager::SendCursorLeave(float normY) {
    CursorTransitionPayload payload = { 0.0f, normY, 0 };
    SendRawPacket(PKT_CURSOR_LEAVE, &payload, sizeof(payload));
}

void NetworkManager::SendMouseInput(const MouseInputPayload& payload) {
    SendRawPacket(PKT_INPUT_MOUSE, &payload, sizeof(payload));
}

void NetworkManager::SendKeyboardInput(const KeyboardInputPayload& payload) {
    SendRawPacket(PKT_INPUT_KEYBOARD, &payload, sizeof(payload));
}

void NetworkManager::SendClipboardText(const std::string& text) {
    if (text.empty()) return;
    uint16_t sendLen = static_cast<uint16_t>(std::min<size_t>(text.size() + 1, 65000));
    SendRawPacket(PKT_CLIPBOARD_DATA, text.c_str(), sendLen);
}

void NetworkManager::RecvThreadFunc() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    uint8_t recvBuffer[65536];
    sockaddr_in senderAddr;
    int senderLen = sizeof(senderAddr);

    while (m_running) {
        if (m_socket == INVALID_SOCKET) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(m_socket, &readfds);

        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 20000; // 20ms

        int ret = select(0, &readfds, nullptr, nullptr, &tv);
        if (ret > 0 && FD_ISSET(m_socket, &readfds)) {
            while (m_running) {
                senderLen = sizeof(senderAddr);
                int bytesRead = recvfrom(m_socket, reinterpret_cast<char*>(recvBuffer), sizeof(recvBuffer), 0,
                                         reinterpret_cast<sockaddr*>(&senderAddr), &senderLen);
                if (bytesRead <= 0) {
                    break; // No more data waiting
                }

                ProcessIncomingPacket(recvBuffer, bytesRead, senderAddr);
            }
        }
    }
}

void NetworkManager::ProcessIncomingPacket(const uint8_t* data, int len, const sockaddr_in& senderAddr) {
    if (len < static_cast<int>(sizeof(PacketHeader))) {
        return;
    }

    const PacketHeader* hdr = reinterpret_cast<const PacketHeader*>(data);
    if (hdr->magic != BUTTERFLOW_MAGIC || hdr->version != BUTTERFLOW_VERSION) {
        return;
    }

    if (len < static_cast<int>(sizeof(PacketHeader) + hdr->payloadSize)) {
        return; // Malformed / truncated packet
    }

    m_packetsReceived++;
    const uint8_t* payloadPtr = data + sizeof(PacketHeader);

    char senderIp[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &senderAddr.sin_addr, senderIp, INET_ADDRSTRLEN);
    uint16_t senderPort = ntohs(senderAddr.sin_port);

    switch (hdr->type) {
        case PKT_DISCOVERY_BEACON: {
            if (hdr->payloadSize >= sizeof(DiscoveryPayload)) {
                const DiscoveryPayload* discovery = reinterpret_cast<const DiscoveryPayload*>(payloadPtr);

                // CRITICAL FIX: Discard own loopback broadcast!
                if (discovery->instanceId == m_instanceId) {
                    return;
                }
                if (m_localIpStr == senderIp && strncmp(discovery->deviceName, m_localDeviceName.c_str(), sizeof(discovery->deviceName)) == 0) {
                    return;
                }

                char safeDeviceName[33] = {};
                memcpy(safeDeviceName, discovery->deviceName, std::min<size_t>(sizeof(discovery->deviceName), 32));

                {
                    std::lock_guard<std::mutex> lock(m_peersMutex);
                    bool found = false;
                    for (auto& peer : m_discoveredPeers) {
                        if (peer.ip == senderIp) {
                            peer.lastSeenTick = GetTickCount();
                            peer.screenWidth = discovery->screenWidth;
                            peer.screenHeight = discovery->screenHeight;
                            peer.preferredEdge = discovery->preferredEdge;
                            strncpy(peer.deviceName, safeDeviceName, sizeof(peer.deviceName) - 1);
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        DiscoveredPeerInfo newPeer = {};
                        newPeer.ip = senderIp;
                        newPeer.port = senderPort;
                        strncpy(newPeer.deviceName, safeDeviceName, sizeof(newPeer.deviceName) - 1);
                        newPeer.screenWidth = discovery->screenWidth;
                        newPeer.screenHeight = discovery->screenHeight;
                        newPeer.preferredEdge = discovery->preferredEdge;
                        newPeer.lastSeenTick = GetTickCount();
                        m_discoveredPeers.push_back(newPeer);
                    }
                }

                if (m_onPeerFound) {
                    m_onPeerFound(senderIp, *discovery);
                }

                // Symmetrical Pairing: Auto-reply directly to sender so they discover us!
                DiscoveryPayload myBeacon = {};
                myBeacon.instanceId = m_instanceId;
                strncpy(myBeacon.deviceName, m_localDeviceName.c_str(), sizeof(myBeacon.deviceName) - 1);
                SendRawPacketTo(PKT_DISCOVERY_RESPONSE, &myBeacon, sizeof(myBeacon), senderAddr);

                // Auto-lock peer if not currently paired
                std::lock_guard<std::mutex> lock(m_peerMutex);
                if (!m_hasPeer) {
                    m_peerAddr = senderAddr;
                    m_peerIpStr = senderIp;
                    m_peerDeviceName = safeDeviceName;
                    m_hasPeer = true;
                    m_lastPeerActivityTick = GetTickCount();
                } else if (m_peerIpStr == senderIp) {
                    m_peerDeviceName = safeDeviceName;
                    m_lastPeerActivityTick = GetTickCount();
                }
            }
            break;
        }

        case PKT_DISCOVERY_RESPONSE: {
            if (hdr->payloadSize >= sizeof(DiscoveryPayload)) {
                const DiscoveryPayload* discovery = reinterpret_cast<const DiscoveryPayload*>(payloadPtr);
                if (discovery->instanceId == m_instanceId) return;

                char safeDeviceName[33] = {};
                memcpy(safeDeviceName, discovery->deviceName, std::min<size_t>(sizeof(discovery->deviceName), 32));

                {
                    std::lock_guard<std::mutex> lock(m_peersMutex);
                    bool found = false;
                    for (auto& peer : m_discoveredPeers) {
                        if (peer.ip == senderIp) {
                            peer.lastSeenTick = GetTickCount();
                            peer.screenWidth = discovery->screenWidth;
                            peer.screenHeight = discovery->screenHeight;
                            peer.preferredEdge = discovery->preferredEdge;
                            strncpy(peer.deviceName, safeDeviceName, sizeof(peer.deviceName) - 1);
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        DiscoveredPeerInfo newPeer = {};
                        newPeer.ip = senderIp;
                        newPeer.port = senderPort;
                        strncpy(newPeer.deviceName, safeDeviceName, sizeof(newPeer.deviceName) - 1);
                        newPeer.screenWidth = discovery->screenWidth;
                        newPeer.screenHeight = discovery->screenHeight;
                        newPeer.preferredEdge = discovery->preferredEdge;
                        newPeer.lastSeenTick = GetTickCount();
                        m_discoveredPeers.push_back(newPeer);
                    }
                }

                if (m_onPeerFound) {
                    m_onPeerFound(senderIp, *discovery);
                }

                std::lock_guard<std::mutex> lock(m_peerMutex);
                if (!m_hasPeer) {
                    m_peerAddr = senderAddr;
                    m_peerIpStr = senderIp;
                    m_peerDeviceName = safeDeviceName;
                    m_hasPeer = true;
                    m_lastPeerActivityTick = GetTickCount();
                }
            }
            break;
        }

        case PKT_HANDSHAKE_REQUEST: {
            if (hdr->payloadSize >= sizeof(DiscoveryPayload)) {
                const DiscoveryPayload* discovery = reinterpret_cast<const DiscoveryPayload*>(payloadPtr);
                if (discovery->instanceId == m_instanceId) return;

                char safeDeviceName[33] = {};
                memcpy(safeDeviceName, discovery->deviceName, std::min<size_t>(sizeof(discovery->deviceName), 32));

                {
                    std::lock_guard<std::mutex> lock(m_peerMutex);
                    m_peerAddr = senderAddr;
                    m_peerIpStr = senderIp;
                    m_peerDeviceName = safeDeviceName;
                    m_hasPeer = true;
                    m_lastPeerActivityTick = GetTickCount();
                }

                // Send accept reply
                DiscoveryPayload myInfo = {};
                myInfo.instanceId = m_instanceId;
                strncpy(myInfo.deviceName, m_localDeviceName.c_str(), sizeof(myInfo.deviceName) - 1);
                SendRawPacketTo(PKT_HANDSHAKE_ACCEPT, &myInfo, sizeof(myInfo), senderAddr);
            }
            break;
        }

        case PKT_HANDSHAKE_ACCEPT: {
            std::lock_guard<std::mutex> lock(m_peerMutex);
            if (m_peerIpStr.empty() || m_peerIpStr == senderIp) {
                m_peerAddr = senderAddr;
                m_peerIpStr = senderIp;
                m_hasPeer = true;
                m_lastPeerActivityTick = GetTickCount();
            }
            break;
        }

        case PKT_HEARTBEAT: {
            std::lock_guard<std::mutex> lock(m_peerMutex);
            if (m_hasPeer && senderAddr.sin_addr.s_addr == m_peerAddr.sin_addr.s_addr) {
                m_lastPeerActivityTick = GetTickCount();
            }
            break;
        }

        case PKT_DISCONNECT: {
            std::lock_guard<std::mutex> lock(m_peerMutex);
            if (m_hasPeer && senderAddr.sin_addr.s_addr == m_peerAddr.sin_addr.s_addr) {
                m_hasPeer = false;
                m_peerIpStr = "";
                m_peerDeviceName = "";
            }
            break;
        }

        // =========================================================================
        // REAL-TIME INPUT PACKETS (STRICT SECURITY VALIDATION)
        // =========================================================================
        case PKT_EDGE_APPROACH: {
            {
                std::lock_guard<std::mutex> lock(m_peerMutex);
                if (!m_hasPeer || senderAddr.sin_addr.s_addr != m_peerAddr.sin_addr.s_addr) {
                    return; // Drop untrusted packet!
                }
            }
            m_lastPeerActivityTick = GetTickCount();

            if (hdr->payloadSize >= sizeof(EdgeApproachPayload) && m_onApproach) {
                const EdgeApproachPayload* approach = reinterpret_cast<const EdgeApproachPayload*>(payloadPtr);
                m_onApproach(*approach);
            }
            break;
        }

        case PKT_CURSOR_ENTER: {
            {
                std::lock_guard<std::mutex> lock(m_peerMutex);
                if (!m_hasPeer || senderAddr.sin_addr.s_addr != m_peerAddr.sin_addr.s_addr) {
                    return; // Drop untrusted packet!
                }
            }
            m_lastPeerActivityTick = GetTickCount();

            if (hdr->payloadSize >= sizeof(CursorTransitionPayload) && m_onTransition) {
                const CursorTransitionPayload* transition = reinterpret_cast<const CursorTransitionPayload*>(payloadPtr);
                m_onTransition(*transition);
            }
            break;
        }

        case PKT_CURSOR_LEAVE: {
            {
                std::lock_guard<std::mutex> lock(m_peerMutex);
                if (!m_hasPeer || senderAddr.sin_addr.s_addr != m_peerAddr.sin_addr.s_addr) {
                    return; // Drop untrusted packet!
                }
            }
            m_lastPeerActivityTick = GetTickCount();

            if (hdr->payloadSize >= sizeof(CursorTransitionPayload) && m_onLeave) {
                const CursorTransitionPayload* leave = reinterpret_cast<const CursorTransitionPayload*>(payloadPtr);
                m_onLeave(leave->normalizedY);
            }
            break;
        }

        case PKT_INPUT_MOUSE: {
            {
                std::lock_guard<std::mutex> lock(m_peerMutex);
                if (!m_hasPeer || senderAddr.sin_addr.s_addr != m_peerAddr.sin_addr.s_addr) {
                    return; // Drop untrusted packet!
                }
            }
            m_lastPeerActivityTick = GetTickCount();

            if (hdr->payloadSize >= sizeof(MouseInputPayload) && m_onMouse) {
                const MouseInputPayload* mouse = reinterpret_cast<const MouseInputPayload*>(payloadPtr);
                m_onMouse(*mouse);
            }
            break;
        }

        case PKT_INPUT_KEYBOARD: {
            {
                std::lock_guard<std::mutex> lock(m_peerMutex);
                if (!m_hasPeer || senderAddr.sin_addr.s_addr != m_peerAddr.sin_addr.s_addr) {
                    return; // Drop untrusted packet!
                }
            }
            m_lastPeerActivityTick = GetTickCount();

            if (hdr->payloadSize >= sizeof(KeyboardInputPayload) && m_onKey) {
                const KeyboardInputPayload* key = reinterpret_cast<const KeyboardInputPayload*>(payloadPtr);
                m_onKey(*key);
            }
            break;
        }

        case PKT_CLIPBOARD_DATA: {
            {
                std::lock_guard<std::mutex> lock(m_peerMutex);
                if (!m_hasPeer || senderAddr.sin_addr.s_addr != m_peerAddr.sin_addr.s_addr) {
                    return; // Drop untrusted packet!
                }
            }
            m_lastPeerActivityTick = GetTickCount();

            if (hdr->payloadSize > 0 && hdr->payloadSize <= 1048576) {
                const char* clipText = reinterpret_cast<const char*>(payloadPtr);
                if (OpenClipboard(nullptr)) {
                    EmptyClipboard();
                    HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, hdr->payloadSize);
                    if (hGlob) {
                        void* pMem = GlobalLock(hGlob);
                        if (pMem) {
                            memcpy(pMem, clipText, hdr->payloadSize);
                            GlobalUnlock(hGlob);
                            SetClipboardData(CF_TEXT, hGlob);
                        }
                    }
                    CloseClipboard();
                }
            }
            break;
        }

        default:
            break;
    }
}

void NetworkManager::Poll() {
    // Packets are processed in real-time in the background m_recvThread.
    // If m_recvThread is ever not running, fallback to manual read here.
    if (!m_running && m_socket != INVALID_SOCKET) {
        uint8_t recvBuffer[4096];
        sockaddr_in senderAddr;
        int senderLen = sizeof(senderAddr);
        while (true) {
            int bytesRead = recvfrom(m_socket, reinterpret_cast<char*>(recvBuffer), sizeof(recvBuffer), 0,
                                     reinterpret_cast<sockaddr*>(&senderAddr), &senderLen);
            if (bytesRead <= 0) break;
            ProcessIncomingPacket(recvBuffer, bytesRead, senderAddr);
        }
    }
}

std::vector<DiscoveredPeerInfo> NetworkManager::GetDiscoveredPeers() const {
    std::lock_guard<std::mutex> lock(m_peersMutex);
    return m_discoveredPeers;
}

std::string NetworkManager::GetLocalIP() const {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        addrinfo hints = {}, *info = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        if (getaddrinfo(hostname, nullptr, &hints, &info) == 0 && info != nullptr) {
            char ipStr[INET_ADDRSTRLEN];
            for (addrinfo* p = info; p != nullptr; p = p->ai_next) {
                sockaddr_in* addr = reinterpret_cast<sockaddr_in*>(p->ai_addr);
                inet_ntop(AF_INET, &addr->sin_addr, ipStr, sizeof(ipStr));
                std::string s(ipStr);
                if (s != "127.0.0.1" && s.rfind("169.254.", 0) == std::string::npos) {
                    freeaddrinfo(info);
                    return s;
                }
            }
            freeaddrinfo(info);
        }
    }
    return "127.0.0.1";
}
