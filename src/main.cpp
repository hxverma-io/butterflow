#include <winsock2.h>
#include <windows.h>
#include <mmsystem.h>
#include <iostream>
#include <string>
#include <vector>
#include "../include/protocol.h"
#include "../include/edge_overlay.h"
#include "../include/edge_physics.h"
#include "../include/input_capturer.h"
#include "../include/input_injector.h"
#include "../include/network_manager.h"
#include "../include/mac_ui.h"

int main(int argc, char* argv[]) {
    // Enable DPI awareness for multi-monitor pixel accuracy
    SetProcessDPIAware();

    // Enable high-precision 1ms Windows scheduling (eliminates 15.6ms sleep stutters)
    timeBeginPeriod(1);

    ScreenEdge configuredEdge = EDGE_RIGHT; // Default: Peer is to the right
    std::string directIp = "";
    uint16_t port = BUTTERFLOW_PORT;
    bool headless = false;
    bool attachConsole = false;

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
        } else if (arg == "--headless" || arg == "--no-ui") {
            headless = true;
        } else if (arg == "--console") {
            attachConsole = true;
        }
    }

    if (attachConsole || headless) {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            FILE* fp;
            freopen_s(&fp, "CONOUT$", "w", stdout);
            freopen_s(&fp, "CONOUT$", "w", stderr);
        }
    }

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    std::cout << "=======================================================\n";
    std::cout << "  BUTTERFLOW: macOS Universal Control for Windows\n";
    std::cout << "  Reverse-Engineered Clean-Room Native Implementation\n";
    std::cout << "=======================================================\n";
    std::cout << "  Screen Geometry : " << screenW << "x" << screenH << "\n";
    std::cout << "  Remote Edge     : " << (configuredEdge == EDGE_RIGHT ? "RIGHT" : (configuredEdge == EDGE_LEFT ? "LEFT" : (configuredEdge == EDGE_TOP ? "TOP" : "BOTTOM"))) << "\n";
    std::cout << "  Port            : " << port << "\n";
    std::cout << "  Breakout Key    : Ctrl + Alt + Shift + Esc\n";
    std::cout << "=======================================================\n";

    HINSTANCE hInstance = GetModuleHandle(nullptr);

    // 1. Initialize Visual Apple Edge Bubble Overlay
    EdgeOverlay overlay;
    if (!overlay.Initialize(hInstance)) {
        std::cerr << "[-] Failed to initialize Apple Edge Overlay subsystem.\n";
        timeEndPeriod(1);
        return 1;
    }

    // 2. Initialize Edge Physics Engine
    EdgePhysics physics;
    physics.Configure(configuredEdge, screenW, screenH);

    // 3. Initialize Input Injector
    InputInjector injector;
    ScreenEdge entryEdge = (configuredEdge == EDGE_RIGHT) ? EDGE_LEFT : (configuredEdge == EDGE_LEFT ? EDGE_RIGHT : (configuredEdge == EDGE_TOP ? EDGE_BOTTOM : EDGE_TOP));
    injector.Initialize(screenW, screenH, entryEdge);

    // 4. Initialize Networking
    NetworkManager net;
    if (!net.Start(port)) {
        std::cerr << "[-] Failed to start Network subsystem on port " << port << ".\n";
        timeEndPeriod(1);
        return 1;
    }

    // 5. Initialize macOS Universal Control GUI
    MacUI ui;
    if (!headless) {
        if (!ui.Initialize(hInstance, &physics, &overlay, &injector, &net, configuredEdge)) {
            std::cerr << "[-] Failed to initialize macOS Universal Control GUI.\n";
        } else {
            ui.Show();
        }
    }

    if (!directIp.empty()) {
        std::cout << "[+] Connecting directly to target peer: " << directIp << "\n";
        net.SetPeerAddress(directIp, port);
        net.SendHandshakeRequest();
        ui.Log("CONNECT", "Connecting directly to target peer: " + directIp, Gdiplus::Color(255, 10, 132, 255));
    } else {
        std::cout << "[+] Auto-Discovery active: Broadcasting beacon on local subnet...\n";
        ui.Log("DISCOVERY", "Auto-Discovery active: broadcasting beacon on subnet...", Gdiplus::Color(255, 10, 132, 255));
    }

    // Setup Network Callbacks
    net.SetPeerFoundHandler([&](const std::string& peerIp, const DiscoveryPayload& disc) {
        std::cout << "[+] Discovered nearby Windows peer: " << disc.deviceName 
                  << " (" << peerIp << ") Screen: " << disc.screenWidth << "x" << disc.screenHeight << "\n";
        ui.Log("DISCOVERY", "Discovered nearby peer: " + std::string(disc.deviceName) + " (" + peerIp + ")", Gdiplus::Color(255, 48, 209, 88));
    });

    net.SetApproachHandler([&](const EdgeApproachPayload& approach) {
        // Display the Apple-style incoming bubble on our screen edge
        overlay.ShowApproach(static_cast<ScreenEdge>(approach.edge), approach.pushProgress, approach.normalizedY);
    });

    net.SetTransitionHandler([&](const CursorTransitionPayload& trans) {
        overlay.PlayTransitionBurst(static_cast<ScreenEdge>(trans.edge), trans.normalizedY);
        injector.PlaceCursorAtEdge(static_cast<ScreenEdge>(trans.edge), trans.normalizedY);
        ui.Log("STREAM", "Remote cursor entered local screen display.", Gdiplus::Color(255, 175, 82, 222));
    });

    net.SetLeaveHandler([&](float normY) {
        physics.OnRemoteCursorReturned(normY);
        InputCapturer::Instance().UnlockCursor();
        ui.Log("STREAM", "Remote cursor left back to host PC.", Gdiplus::Color(255, 10, 132, 255));
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
                ScreenEdge targetEdge = (edge == EDGE_RIGHT) ? EDGE_LEFT : (edge == EDGE_LEFT ? EDGE_RIGHT : (edge == EDGE_TOP ? EDGE_BOTTOM : EDGE_TOP));
                net.SendEdgeApproach(targetEdge, progress, normY);
            }
        },
        [&](ScreenEdge edge, float normY) {
            // Local cursor broke through barrier: lock cursor and notify peer
            std::cout << "[*] Cursor crossed boundary onto remote PC.\n";
            ui.Log("HANDOFF", "Pointer crossed boundary onto remote PC screen.", Gdiplus::Color(255, 0, 180, 255));
            InputCapturer::Instance().LockCursorAtEdge(edge, screenW, screenH);
            if (net.HasPeer()) {
                ScreenEdge targetEdge = (edge == EDGE_RIGHT) ? EDGE_LEFT : (edge == EDGE_LEFT ? EDGE_RIGHT : (edge == EDGE_TOP ? EDGE_BOTTOM : EDGE_TOP));
                net.SendCursorEnter(targetEdge, 0.0f, normY);
            }
        },
        [&](ScreenEdge edge, float normY) {
            std::cout << "[*] Cursor returned to local PC.\n";
            ui.Log("HANDOFF", "Pointer returned to local PC.", Gdiplus::Color(255, 48, 209, 88));
            InputCapturer::Instance().UnlockCursor();
            overlay.Hide();
        }
    );

    // Setup Return crossing callback from Injector
    injector.SetReturnCallback([&](float normY) {
        std::cout << "[*] Remote cursor leaving back to host.\n";
        net.SendCursorLeave(normY);
    });

    // 6. Install Low-Level Input Hooks & Raw Input Sink
    if (!InputCapturer::Instance().InstallHooks(&physics)) {
        std::cerr << "[-] Failed to install Windows low-level input hooks.\n";
        ui.Log("ERROR", "Failed to install Windows low-level input hooks.", Gdiplus::Color(255, 255, 69, 58));
        timeEndPeriod(1);
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
            ui.Log("SAFETY", "Emergency Breakout triggered: cursor released to local PC.", Gdiplus::Color(255, 255, 69, 58));
            physics.ResetToLocal();
            InputCapturer::Instance().UnlockCursor();
            overlay.Hide();
        }
    );

    std::cout << "[+] Butterflow macOS Universal Control engine active and running.\n";

    // Prepare Discovery Beacon
    DiscoveryPayload beacon = {};
    beacon.instanceId = net.GetInstanceId();
    DWORD compNameLen = sizeof(beacon.deviceName);
    GetComputerNameA(beacon.deviceName, &compNameLen);
    net.SetLocalDeviceName(beacon.deviceName);
    beacon.screenWidth = static_cast<uint16_t>(screenW);
    beacon.screenHeight = static_cast<uint16_t>(screenH);
    beacon.preferredEdge = configuredEdge;

    DWORD lastBeaconTime = 0;
    DWORD lastHeartbeatTime = 0;
    DWORD lastDecayTime = GetTickCount();

    // High-performance real-time event loop
    MSG msg;
    bool running = true;
    while (running && (headless || ui.IsRunning())) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        DWORD now = GetTickCount();

        // 1. Send Discovery Beacon every 1.5 seconds if no peer paired
        if (!net.HasPeer()) {
            if (now - lastBeaconTime > 1500) {
                beacon.preferredEdge = ui.GetConfiguredEdge();
                beacon.instanceId = net.GetInstanceId();
                net.SendDiscoveryBeacon(beacon);
                lastBeaconTime = now;
            }
        } else {
            // Send periodic keepalive heartbeat every 1 second
            if (now - lastHeartbeatTime > 1000) {
                net.SendHeartbeat();
                lastHeartbeatTime = now;
            }

            // Liveness check: If companion PC timed out for >5 seconds, disconnect cleanly
            if (now - net.GetLastPeerActivity() > 5000) {
                if (physics.GetState() == ControlState::REMOTE_ACTIVE) {
                    physics.ResetToLocal();
                    InputCapturer::Instance().UnlockCursor();
                    overlay.Hide();
                }
                net.DisconnectPeer();
                ui.Log("TIMEOUT", "Companion PC connection timed out. Resuming auto-discovery radar...", Gdiplus::Color(255, 255, 69, 58));
            }
        }

        // 2. Poll fallback
        net.Poll();

        // 3. Apple Spring Resistance Decay Tick (every 16ms / 60 FPS)
        if (now - lastDecayTime >= 16) {
            physics.DecayTick();
            lastDecayTime = now;
        }

        // Sleep 1ms to yield CPU while maintaining ultra-low latency with timeBeginPeriod(1)
        Sleep(1);
    }

    InputCapturer::Instance().UninstallHooks();
    net.Stop();
    timeEndPeriod(1);
    return 0;
}
