#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <string>
#include "../include/protocol.h"
#include "../include/edge_overlay.h"
#include "../include/edge_physics.h"
#include "../include/input_capturer.h"
#include "../include/input_injector.h"
#include "../include/network_manager.h"

int main(int argc, char* argv[]) {
    // Enable DPI awareness for multi-monitor pixel accuracy
    SetProcessDPIAware();

    ScreenEdge configuredEdge = EDGE_RIGHT; // Default: Peer is to the right
    std::string directIp = "";
    uint16_t port = BUTTERFLOW_PORT;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--edge" && i + 1 < argc) {
            std::string val = argv[++i];
            if (val == "left") configuredEdge = EDGE_LEFT;
            else if (val == "right") configuredEdge = EDGE_RIGHT;
            else if (val == "top") configuredEdge = EDGE_TOP;
            else if (val == "bottom") configuredEdge = EDGE_BOTTOM;
        } else if (arg == "--connect" && i + 1 < argc) {
            directIp = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = static_cast<uint16_t>(std::stoi(argv[++i]));
        }
    }

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    std::cout << "=======================================================\n";
    std::cout << "  BUTTERFLOW: Universal Control Engine for Windows\n";
    std::cout << "  Reverse-Engineered Clean-Room Native Implementation\n";
    std::cout << "=======================================================\n";
    std::cout << "  Screen Geometry : " << screenW << "x" << screenH << "\n";
    std::cout << "  Remote Edge     : " << (configuredEdge == EDGE_RIGHT ? "RIGHT" : (configuredEdge == EDGE_LEFT ? "LEFT" : "OTHER")) << "\n";
    std::cout << "  Port            : " << port << "\n";
    std::cout << "  Breakout Key    : Ctrl + Alt + Shift + Esc\n";
    std::cout << "=======================================================\n";

    HINSTANCE hInstance = GetModuleHandle(nullptr);

    // 1. Initialize Visual Apple Edge Bubble
    EdgeOverlay overlay;
    if (!overlay.Initialize(hInstance)) {
        std::cerr << "[-] Failed to initialize Apple Edge Overlay subsystem.\n";
        return 1;
    }

    // 2. Initialize Edge Physics Engine
    EdgePhysics physics;
    physics.Configure(configuredEdge, screenW, screenH);

    // 3. Initialize Input Injector
    InputInjector injector;
    ScreenEdge entryEdge = (configuredEdge == EDGE_RIGHT) ? EDGE_LEFT : EDGE_RIGHT;
    injector.Initialize(screenW, screenH, entryEdge);

    // 4. Initialize Networking
    NetworkManager net;
    if (!net.Start(port)) {
        std::cerr << "[-] Failed to start Network subsystem on port " << port << ".\n";
        return 1;
    }

    if (!directIp.empty()) {
        std::cout << "[+] Connecting directly to target peer: " << directIp << "\n";
        net.SetPeerAddress(directIp, port);
    } else {
        std::cout << "[+] Auto-Discovery active: Broadcasting beacon on local subnet...\n";
    }

    // Setup Network Callbacks
    net.SetPeerFoundHandler([&](const std::string& peerIp, const DiscoveryPayload& disc) {
        std::cout << "[+] Discovered nearby Windows peer: " << disc.deviceName 
                  << " (" << peerIp << ") Screen: " << disc.screenWidth << "x" << disc.screenHeight << "\n";
    });

    net.SetApproachHandler([&](const EdgeApproachPayload& approach) {
        // Display the Apple-style incoming bubble on our screen edge!
        overlay.ShowApproach(static_cast<ScreenEdge>(approach.edge), approach.pushProgress, approach.normalizedY);
    });

    net.SetTransitionHandler([&](const CursorTransitionPayload& trans) {
        overlay.PlayTransitionBurst(static_cast<ScreenEdge>(trans.edge), trans.normalizedY);
        injector.PlaceCursorAtEdge(static_cast<ScreenEdge>(trans.edge), trans.normalizedY);
    });

    net.SetLeaveHandler([&](float normY) {
        physics.OnRemoteCursorReturned(normY);
        InputCapturer::Instance().UnlockCursor();
    });

    net.SetMouseHandler([&](const MouseInputPayload& mouse) {
        injector.InjectMouse(mouse);
    });

    net.SetKeyHandler([&](const KeyboardInputPayload& key) {
        injector.InjectKeyboard(key);
    });

    // Setup Local Physics Callbacks
    physics.SetCallbacks(
        [&](ScreenEdge edge, float progress, float normY) {
            // Local push against edge: send approach update to peer
            if (net.HasPeer()) {
                ScreenEdge targetEdge = (edge == EDGE_RIGHT) ? EDGE_LEFT : EDGE_RIGHT;
                net.SendEdgeApproach(targetEdge, progress, normY);
            }
        },
        [&](ScreenEdge edge, float normY) {
            // Local cursor broke through barrier: lock cursor and notify peer
            std::cout << "[*] Cursor crossed boundary onto remote PC.\n";
            InputCapturer::Instance().LockCursorAtEdge(edge, screenW, screenH);
            if (net.HasPeer()) {
                ScreenEdge targetEdge = (edge == EDGE_RIGHT) ? EDGE_LEFT : EDGE_RIGHT;
                net.SendCursorEnter(targetEdge, 0.0f, normY);
            }
        },
        [&](ScreenEdge edge, float normY) {
            std::cout << "[*] Cursor returned to local PC.\n";
            InputCapturer::Instance().UnlockCursor();
            overlay.Hide();
        }
    );

    // Setup Return crossing callback from Injector
    injector.SetReturnCallback([&](float normY) {
        std::cout << "[*] Remote cursor leaving back to host.\n";
        net.SendCursorLeave(normY);
    });

    // 5. Install Low-Level Input Hooks
    if (!InputCapturer::Instance().InstallHooks(&physics)) {
        std::cerr << "[-] Failed to install Windows low-level input hooks.\n";
        return 1;
    }

    InputCapturer::Instance().SetCallbacks(
        [&](const MouseInputPayload& mouse) {
            net.SendMouseInput(mouse);
        },
        [&](const KeyboardInputPayload& key) {
            net.SendKeyboardInput(key);
        },
        [&]() {
            std::cout << "[!] Emergency Breakout triggered. Resetting control to local PC.\n";
            physics.ResetToLocal();
            InputCapturer::Instance().UnlockCursor();
            overlay.Hide();
        }
    );

    std::cout << "[+] Butterflow engine active and running. Press Ctrl+C in console to exit.\n";

    // Prepare Discovery Beacon
    DiscoveryPayload beacon = {};
    DWORD compNameLen = sizeof(beacon.deviceName);
    GetComputerNameA(beacon.deviceName, &compNameLen);
    beacon.screenWidth = static_cast<uint16_t>(screenW);
    beacon.screenHeight = static_cast<uint16_t>(screenH);
    beacon.preferredEdge = configuredEdge;

    DWORD lastBeaconTime = 0;
    DWORD lastDecayTime = GetTickCount();

    // High-performance real-time event loop
    MSG msg;
    bool running = true;
    while (running) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        DWORD now = GetTickCount();

        // 1. Send Discovery Beacon every 2 seconds if no peer confirmed
        if (!net.HasPeer() && now - lastBeaconTime > 2000) {
            net.SendDiscoveryBeacon(beacon);
            lastBeaconTime = now;
        }

        // 2. Poll non-blocking UDP packets (<1ms latency)
        net.Poll();

        // 3. Apple Spring Resistance Decay Tick (every 16ms / 60 FPS)
        if (now - lastDecayTime >= 16) {
            physics.DecayTick();
            lastDecayTime = now;
        }

        // Sleep 1ms to yield CPU while maintaining ultra-low latency
        Sleep(1);
    }

    InputCapturer::Instance().UninstallHooks();
    net.Stop();
    return 0;
}
