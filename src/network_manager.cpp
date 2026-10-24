#include "../include/network_manager.h"
#include <iostream>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

NetworkManager::NetworkManager()
    : m_socket(INVALID_SOCKET),
      m_port(BUTTERFLOW_PORT),
      m_hasPeer(false),
      m_sequence(0),
      m_packetsSent(0),
      m_packetsReceived(0) {
    memset(&m_localAddr, 0, sizeof(m_localAddr));
    memset(&m_peerAddr, 0, sizeof(m_peerAddr));
    memset(&m_broadcastAddr, 0, sizeof(m_broadcastAddr));
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

    // Set Non-blocking mode for low latency
    u_long nonBlocking = 1;
    ioctlsocket(m_socket, FIONBIO, &nonBlocking);

    // Minimize send/recv buffer latency
    int bufSize = 65536;
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

    return true;
}

void NetworkManager::Stop() {
    if (m_socket != INVALID_SOCKET) {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }
    WSACleanup();
}

void NetworkManager::SetPeerAddress(const std::string& ip, uint16_t port) {
    m_peerAddr.sin_family = AF_INET;
    m_peerAddr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &m_peerAddr.sin_addr);
    m_peerIpStr = ip;
    m_hasPeer = true;
}

void NetworkManager::DisconnectPeer() {
    m_hasPeer = false;
    m_peerIpStr = "";
    m_peerDeviceName = "";
}

void NetworkManager::SendRawPacket(PacketType type, const void* payload, uint16_t size) {
    if (m_socket == INVALID_SOCKET || !m_hasPeer) return;

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
                      reinterpret_cast<sockaddr*>(&m_peerAddr), sizeof(m_peerAddr));
    if (sent > 0) {
        m_packetsSent++;
    }
}

void NetworkManager::SendDiscoveryBeacon(const DiscoveryPayload& payload) {
    if (m_socket == INVALID_SOCKET) return;

    std::vector<uint8_t> buffer(sizeof(PacketHeader) + sizeof(DiscoveryPayload));
    PacketHeader* hdr = reinterpret_cast<PacketHeader*>(buffer.data());
    hdr->magic = BUTTERFLOW_MAGIC;
    hdr->version = BUTTERFLOW_VERSION;
    hdr->type = PKT_DISCOVERY_BEACON;
    hdr->payloadSize = sizeof(DiscoveryPayload);
    hdr->sequence = ++m_sequence;

    memcpy(buffer.data() + sizeof(PacketHeader), &payload, sizeof(DiscoveryPayload));

    int sent = sendto(m_socket, reinterpret_cast<const char*>(buffer.data()), static_cast<int>(buffer.size()), 0,
                      reinterpret_cast<sockaddr*>(&m_broadcastAddr), sizeof(m_broadcastAddr));
    if (sent > 0) {
        m_packetsSent++;
    }
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
    SendRawPacket(PKT_CLIPBOARD_DATA, text.c_str(), static_cast<uint16_t>(text.size() + 1));
}

void NetworkManager::Poll() {
    if (m_socket == INVALID_SOCKET) return;

    uint8_t recvBuffer[4096];
    sockaddr_in senderAddr;
    int senderLen = sizeof(senderAddr);

    while (true) {
        int bytesRead = recvfrom(m_socket, reinterpret_cast<char*>(recvBuffer), sizeof(recvBuffer), 0,
                                 reinterpret_cast<sockaddr*>(&senderAddr), &senderLen);
        if (bytesRead <= 0) {
            break; // No more data waiting
        }

        if (bytesRead < static_cast<int>(sizeof(PacketHeader))) {
            continue;
        }

        PacketHeader* hdr = reinterpret_cast<PacketHeader*>(recvBuffer);
        if (hdr->magic != BUTTERFLOW_MAGIC || hdr->version != BUTTERFLOW_VERSION) {
            continue;
        }

        m_packetsReceived++;
        const uint8_t* payloadPtr = recvBuffer + sizeof(PacketHeader);

        switch (hdr->type) {
            case PKT_DISCOVERY_BEACON: {
                if (hdr->payloadSize >= sizeof(DiscoveryPayload)) {
                    char senderIp[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &senderAddr.sin_addr, senderIp, INET_ADDRSTRLEN);
                    
                    const DiscoveryPayload* discovery = reinterpret_cast<const DiscoveryPayload*>(payloadPtr);
                    
                    {
                        std::lock_guard<std::mutex> lock(m_peersMutex);
                        bool found = false;
                        for (auto& peer : m_discoveredPeers) {
                            if (peer.ip == senderIp) {
                                peer.lastSeenTick = GetTickCount();
                                peer.screenWidth = discovery->screenWidth;
                                peer.screenHeight = discovery->screenHeight;
                                peer.preferredEdge = discovery->preferredEdge;
                                strncpy(peer.deviceName, discovery->deviceName, sizeof(peer.deviceName) - 1);
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            DiscoveredPeerInfo newPeer = {};
                            newPeer.ip = senderIp;
                            newPeer.port = ntohs(senderAddr.sin_port);
                            strncpy(newPeer.deviceName, discovery->deviceName, sizeof(newPeer.deviceName) - 1);
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

                    // Auto-reply with response if we don't have peer locked yet
                    if (!m_hasPeer) {
                        SetPeerAddress(senderIp, ntohs(senderAddr.sin_port));
                        m_peerDeviceName = discovery->deviceName;
                    } else if (m_peerIpStr == senderIp) {
                        m_peerDeviceName = discovery->deviceName;
                    }
                }
                break;
            }

            case PKT_EDGE_APPROACH: {
                if (hdr->payloadSize >= sizeof(EdgeApproachPayload) && m_onApproach) {
                    const EdgeApproachPayload* approach = reinterpret_cast<const EdgeApproachPayload*>(payloadPtr);
                    m_onApproach(*approach);
                }
                break;
            }

            case PKT_CURSOR_ENTER: {
                if (hdr->payloadSize >= sizeof(CursorTransitionPayload) && m_onTransition) {
                    const CursorTransitionPayload* transition = reinterpret_cast<const CursorTransitionPayload*>(payloadPtr);
                    m_onTransition(*transition);
                }
                break;
            }

            case PKT_CURSOR_LEAVE: {
                if (hdr->payloadSize >= sizeof(CursorTransitionPayload) && m_onLeave) {
                    const CursorTransitionPayload* leave = reinterpret_cast<const CursorTransitionPayload*>(payloadPtr);
                    m_onLeave(leave->normalizedY);
                }
                break;
            }

            case PKT_INPUT_MOUSE: {
                if (hdr->payloadSize >= sizeof(MouseInputPayload) && m_onMouse) {
                    const MouseInputPayload* mouse = reinterpret_cast<const MouseInputPayload*>(payloadPtr);
                    m_onMouse(*mouse);
                }
                break;
            }

            case PKT_INPUT_KEYBOARD: {
                if (hdr->payloadSize >= sizeof(KeyboardInputPayload) && m_onKey) {
                    const KeyboardInputPayload* key = reinterpret_cast<const KeyboardInputPayload*>(payloadPtr);
                    m_onKey(*key);
                }
                break;
            }

            case PKT_CLIPBOARD_DATA: {
                if (hdr->payloadSize > 0) {
                    const char* clipText = reinterpret_cast<const char*>(payloadPtr);
                    if (OpenClipboard(nullptr)) {
                        EmptyClipboard();
                        HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, hdr->payloadSize);
                        if (hGlob) {
                            memcpy(GlobalLock(hGlob), clipText, hdr->payloadSize);
                            GlobalUnlock(hGlob);
                            SetClipboardData(CF_TEXT, hGlob);
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
            sockaddr_in* addr = reinterpret_cast<sockaddr_in*>(info->ai_addr);
            inet_ntop(AF_INET, &addr->sin_addr, ipStr, sizeof(ipStr));
            freeaddrinfo(info);
            return std::string(ipStr);
        }
    }
    return "127.0.0.1";
}
