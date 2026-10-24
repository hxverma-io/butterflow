#include "../include/mac_ui.h"
#include "../include/input_capturer.h"
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <ctime>

using namespace Gdiplus;

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

namespace {
    std::wstring ToWStr(const std::string& str) {
        if (str.empty()) return L"";
        int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
        std::wstring wstr(len, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], len);
        if (!wstr.empty() && wstr.back() == 0) wstr.pop_back();
        return wstr;
    }
}

MacUI::MacUI()
    : m_hWnd(nullptr),
      m_hInstance(nullptr),
      m_gdiplusToken(0),
      m_isRunning(false),
      m_inTray(false),
      m_physics(nullptr),
      m_overlay(nullptr),
      m_injector(nullptr),
      m_net(nullptr),
      m_configuredEdge(EDGE_RIGHT),
      m_activeTab(0),
      m_windowWidth(980),
      m_windowHeight(680),
      m_screenWidth(1920),
      m_screenHeight(1080),
      m_localHostName("Windows PC"),
      m_localIP("127.0.0.1"),
      m_hoveredTrafficLight(0),
      m_hoveredTab(-1),
      m_hoveredEdgeBtn(-1),
      m_pushThroughEnabled(true),
      m_edgeBubbleEnabled(true),
      m_springTension(35.0f),
      m_mouseSpeed(1.4f),
      m_isDraggingMonitor(false),
      m_isDraggingSlider(false),
      m_dragOffset(0.0f, 0.0f),
      m_directIpInput(""),
      m_directIpFocused(false),
      m_radarAngle(0.0f),
      m_bridgePulseOffset(0.0f),
      m_statusGlowAlpha(1.0f),
      m_lastTickTime(0),
      m_lastSentCount(0),
      m_lastRecvCount(0),
      m_packetsPerSec(0.0f),
      m_logScrollOffset(0) {
}

MacUI::~MacUI() {
    RemoveTrayIcon();
    if (m_hWnd) {
        DestroyWindow(m_hWnd);
    }
    if (m_gdiplusToken) {
        GdiplusShutdown(m_gdiplusToken);
    }
}

LRESULT CALLBACK MacUI::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    MacUI* pThis = nullptr;
    if (message == WM_NCCREATE) {
        CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        pThis = reinterpret_cast<MacUI*>(pCreate->lpCreateParams);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    } else {
        pThis = reinterpret_cast<MacUI*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    }

    if (pThis) {
        return pThis->HandleMessage(hWnd, message, wParam, lParam);
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}

bool MacUI::Initialize(HINSTANCE hInstance, 
                       EdgePhysics* physics, 
                       EdgeOverlay* overlay, 
                       InputInjector* injector, 
                       NetworkManager* net,
                       ScreenEdge initialEdge) {
    m_hInstance = hInstance;
    m_physics = physics;
    m_overlay = overlay;
    m_injector = injector;
    m_net = net;
    m_configuredEdge = initialEdge;

    GdiplusStartupInput gdiplusStartupInput;
    if (GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, nullptr) != Ok) {
        return false;
    }

    m_screenWidth = GetSystemMetrics(SM_CXSCREEN);
    m_screenHeight = GetSystemMetrics(SM_CYSCREEN);

    char hostname[256] = {};
    DWORD hLen = sizeof(hostname);
    if (GetComputerNameA(hostname, &hLen)) {
        m_localHostName = hostname;
    }
    if (m_net) {
        m_localIP = m_net->GetLocalIP();
    }

    WNDCLASSEX wc = { sizeof(WNDCLASSEX) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = m_hInstance;
    wc.lpszClassName = "ButterflowMacUI";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // Double buffered in WM_PAINT
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
    RegisterClassEx(&wc);

    // Center window on primary monitor
    int posX = (m_screenWidth - m_windowWidth) / 2;
    int posY = (m_screenHeight - m_windowHeight) / 2;

    m_hWnd = CreateWindowEx(
        WS_EX_APPWINDOW,
        "ButterflowMacUI",
        "Universal Control",
        WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU,
        posX, posY, m_windowWidth, m_windowHeight,
        nullptr, nullptr, m_hInstance, this
    );

    if (!m_hWnd) return false;

    // Windows 11 rounded corners and dark mode attributes
    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(m_hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
    int cornerPref = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(m_hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPref, sizeof(cornerPref));

    // Setup 60 FPS animation timer (16ms)
    SetTimer(m_hWnd, 1, 16, nullptr);

    SetupTrayIcon();
    m_isRunning = true;

    Log("SYSTEM", "Universal Control Engine initialized with macOS Ventura interface.", Color(255, 48, 209, 88));
    Log("NETWORK", "Auto-Discovery active: broadcasting beacon on subnet port 54823.", Color(255, 10, 132, 255));
    Log("PHYSICS", "Apple spring tension configured to default 35.0px resistance.", Color(255, 255, 159, 10));

    return true;
}

void MacUI::Show() {
    if (m_hWnd) {
        m_inTray = false;
        ShowWindow(m_hWnd, SW_SHOW);
        SetForegroundWindow(m_hWnd);
    }
}

void MacUI::Hide() {
    if (m_hWnd) {
        ShowWindow(m_hWnd, SW_HIDE);
    }
}

void MacUI::MinimizeToTray() {
    m_inTray = true;
    Hide();
}

void MacUI::RestoreFromTray() {
    m_inTray = false;
    Show();
}

void MacUI::RequestExit() {
    m_isRunning = false;
    PostQuitMessage(0);
}

void MacUI::SetConfiguredEdge(ScreenEdge edge) {
    m_configuredEdge = edge;
    if (m_physics) {
        m_physics->SetRemoteEdge(edge);
    }
    if (m_overlay) {
        m_overlay->SetEdge(edge);
    }
    if (m_injector) {
        ScreenEdge entry = (edge == EDGE_RIGHT) ? EDGE_LEFT : (edge == EDGE_LEFT ? EDGE_RIGHT : (edge == EDGE_TOP ? EDGE_BOTTOM : EDGE_TOP));
        m_injector->SetEntryEdge(entry);
    }

    std::string edgeName = (edge == EDGE_RIGHT ? "RIGHT" : (edge == EDGE_LEFT ? "LEFT" : (edge == EDGE_TOP ? "TOP" : "BOTTOM")));
    Log("ARRANGEMENT", "Boundary re-anchored to " + edgeName + " display edge.", Color(255, 0, 122, 255));
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void MacUI::SetMouseSpeed(float speed) {
    m_mouseSpeed = std::clamp(speed, 0.5f, 3.5f);
    InputCapturer::Instance().SetSpeedMultiplier(m_mouseSpeed);
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1) << m_mouseSpeed;
    Log("SPEED", "Cursor speed set to " + ss.str() + "x", Color(255, 10, 132, 255));
    if (m_hWnd) InvalidateRect(m_hWnd, nullptr, FALSE);
}

void MacUI::Log(const std::string& tag, const std::string& text, Color color) {
    std::lock_guard<std::mutex> lock(m_logMutex);
    time_t now = time(nullptr);
    tm localTime;
    localtime_s(&localTime, &now);

    char timeBuf[16];
    strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &localTime);

    m_logs.push_back({ timeBuf, tag, text, color });
    if (m_logs.size() > 100) {
        m_logs.erase(m_logs.begin());
    }
}

void MacUI::OnTimerTick() {
    DWORD now = GetTickCount();

    // 1. Radar sweep rotation
    m_radarAngle += 2.5f;
    if (m_radarAngle >= 360.0f) m_radarAngle -= 360.0f;

    // 2. Bridging pulse animation
    m_bridgePulseOffset += 1.8f;
    if (m_bridgePulseOffset > 30.0f) m_bridgePulseOffset = 0.0f;

    // 3. Status glow sine wave
    float t = now / 400.0f;
    m_statusGlowAlpha = 0.6f + 0.4f * std::sin(t);

    // 4. Update packet telemetry every 500ms
    if (m_net && (now - m_lastTickTime >= 500)) {
        uint64_t currentSent = m_net->GetPacketsSent();
        uint64_t currentRecv = m_net->GetPacketsReceived();
        uint64_t delta = (currentSent - m_lastSentCount) + (currentRecv - m_lastRecvCount);
        m_packetsPerSec = (delta * 1000.0f) / static_cast<float>(now - m_lastTickTime);

        m_lastSentCount = currentSent;
        m_lastRecvCount = currentRecv;
        m_lastTickTime = now;
    }

    if (!m_inTray && m_hWnd) {
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }
}

void MacUI::SetupTrayIcon() {
    NOTIFYICONDATA nid = { sizeof(NOTIFYICONDATA) };
    nid.hWnd = m_hWnd;
    nid.uID = 1001;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    strcpy_s(nid.szTip, "Butterflow: macOS Universal Control");
    Shell_NotifyIcon(NIM_ADD, &nid);
}

void MacUI::RemoveTrayIcon() {
    NOTIFYICONDATA nid = { sizeof(NOTIFYICONDATA) };
    nid.hWnd = m_hWnd;
    nid.uID = 1001;
    Shell_NotifyIcon(NIM_DELETE, &nid);
}

void MacUI::ShowTrayMenu() {
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    InsertMenu(hMenu, 0, MF_BYPOSITION | MF_STRING, 1, "Open Universal Control");
    InsertMenu(hMenu, 1, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    InsertMenu(hMenu, 2, MF_BYPOSITION | MF_STRING, 2, "Edge: Left Display");
    InsertMenu(hMenu, 3, MF_BYPOSITION | MF_STRING, 3, "Edge: Right Display");
    InsertMenu(hMenu, 4, MF_BYPOSITION | MF_STRING, 4, "Edge: Top Display");
    InsertMenu(hMenu, 5, MF_BYPOSITION | MF_STRING, 5, "Edge: Bottom Display");
    InsertMenu(hMenu, 6, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    InsertMenu(hMenu, 7, MF_BYPOSITION | MF_STRING, 6, "Emergency Breakout Reset");
    InsertMenu(hMenu, 8, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    InsertMenu(hMenu, 9, MF_BYPOSITION | MF_STRING, 7, "Exit Butterflow");

    SetForegroundWindow(m_hWnd);
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, m_hWnd, nullptr);
    DestroyMenu(hMenu);

    if (cmd == 1) {
        RestoreFromTray();
    } else if (cmd == 2) {
        SetConfiguredEdge(EDGE_LEFT);
    } else if (cmd == 3) {
        SetConfiguredEdge(EDGE_RIGHT);
    } else if (cmd == 4) {
        SetConfiguredEdge(EDGE_TOP);
    } else if (cmd == 5) {
        SetConfiguredEdge(EDGE_BOTTOM);
    } else if (cmd == 6) {
        if (m_physics) m_physics->ResetToLocal();
        if (m_overlay) m_overlay->Hide();
        Log("SAFETY", "Emergency breakout triggered via Tray Menu.", Color(255, 255, 69, 58));
    } else if (cmd == 7) {
        RequestExit();
    }
}

LRESULT MacUI::HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_TIMER: {
            OnTimerTick();
            return 0;
        }

        case WM_NCHITTEST: {
            // Enable dragging window by the custom top bar
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hWnd, &pt);

            // Allow clicking traffic lights without triggering window move
            if (pt.y < 54) {
                if (pt.x >= 18 && pt.x <= 84 && pt.y >= 18 && pt.y <= 38) {
                    return HTCLIENT;
                }
                return HTCAPTION;
            }
            return HTCLIENT;
        }

        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            OnMouseMove(x, y);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            OnMouseDown(x, y);
            return 0;
        }

        case WM_LBUTTONUP: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            OnMouseUp(x, y);
            return 0;
        }

        case WM_CHAR: {
            OnCharInput(static_cast<char>(wParam));
            return 0;
        }

        case WM_KEYDOWN: {
            OnKeyDown(wParam);
            return 0;
        }

        case WM_TRAYICON: {
            if (lParam == WM_LBUTTONDBLCLK) {
                RestoreFromTray();
            } else if (lParam == WM_RBUTTONUP) {
                ShowTrayMenu();
            }
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            RECT rc;
            GetClientRect(hWnd, &rc);
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;

            HDC hdcMem = CreateCompatibleDC(hdc);
            HBITMAP hbmMem = CreateCompatibleBitmap(hdc, w, h);
            HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem);

            Render(hdcMem, w, h);

            BitBlt(hdc, 0, 0, w, h, hdcMem, 0, 0, SRCCOPY);
            SelectObject(hdcMem, hbmOld);
            DeleteObject(hbmMem);
            DeleteDC(hdcMem);

            EndPaint(hWnd, &ps);
            return 0;
        }

        case WM_DESTROY: {
            RequestExit();
            return 0;
        }

        default:
            break;
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}

void MacUI::OnMouseMove(int x, int y) {
    // 1. Check traffic lights hover
    int oldLight = m_hoveredTrafficLight;
    m_hoveredTrafficLight = 0;
    if (y >= 18 && y <= 38) {
        if (x >= 20 && x <= 34) m_hoveredTrafficLight = 1;      // Red
        else if (x >= 42 && x <= 56) m_hoveredTrafficLight = 2; // Yellow
        else if (x >= 64 && x <= 78) m_hoveredTrafficLight = 3; // Green
    }

    // 2. Check tab bar hover (y: 56 to 96)
    int oldTab = m_hoveredTab;
    m_hoveredTab = -1;
    if (y >= 56 && y <= 96) {
        int tabStartX = 30;
        int tabW = 160;
        for (int i = 0; i < 4; ++i) {
            int tx = tabStartX + (i * (tabW + 10));
            if (x >= tx && x <= tx + tabW) {
                m_hoveredTab = i;
                break;
            }
        }
    }

    // 3. Check edge buttons hover on Tab 0
    if (m_activeTab == 0) {
        int oldEdge = m_hoveredEdgeBtn;
        m_hoveredEdgeBtn = -1;
        if (y >= 486 && y <= 526) {
            int btnW = 140;
            int gap = 12;
            int startX = 30;
            for (int i = 0; i < 4; ++i) {
                int bx = startX + i * (btnW + gap);
                if (x >= bx && x <= bx + btnW) {
                    m_hoveredEdgeBtn = i;
                    break;
                }
            }
        }
        if (oldEdge != m_hoveredEdgeBtn) InvalidateRect(m_hWnd, nullptr, FALSE);
    }

    // 4. Handle slider dragging (Cursor Speed & Sensitivity)
    if (m_isDraggingSlider && m_activeTab == 0) {
        float sliderX = 665.0f;
        float sliderW = 245.0f;
        float ratio = std::clamp((static_cast<float>(x) - sliderX) / sliderW, 0.0f, 1.0f);
        m_mouseSpeed = 0.5f + ratio * (3.5f - 0.5f);
        InputCapturer::Instance().SetSpeedMultiplier(m_mouseSpeed);
    }

    if (oldLight != m_hoveredTrafficLight || oldTab != m_hoveredTab) {
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }
}

void MacUI::OnMouseDown(int x, int y) {
    // 1. Traffic lights click
    if (y >= 18 && y <= 38) {
        if (x >= 20 && x <= 34) {
            // Close button: Minimize to tray
            MinimizeToTray();
            return;
        } else if (x >= 42 && x <= 56) {
            // Minimize
            ShowWindow(m_hWnd, SW_MINIMIZE);
            return;
        } else if (x >= 64 && x <= 78) {
            // Zoom: Center window
            int posX = (m_screenWidth - m_windowWidth) / 2;
            int posY = (m_screenHeight - m_windowHeight) / 2;
            SetWindowPos(m_hWnd, nullptr, posX, posY, m_windowWidth, m_windowHeight, SWP_NOZORDER);
            return;
        }
    }

    // 2. Tab Bar click
    if (y >= 56 && y <= 96) {
        int tabStartX = 30;
        int tabW = 160;
        for (int i = 0; i < 4; ++i) {
            int tx = tabStartX + (i * (tabW + 10));
            if (x >= tx && x <= tx + tabW) {
                m_activeTab = i;
                InvalidateRect(m_hWnd, nullptr, FALSE);
                return;
            }
        }
    }

    // 3. Tab 0 Interaction
    if (m_activeTab == 0) {
        // Edge selector buttons (y: 486 to 526)
        if (y >= 486 && y <= 526) {
            int btnW = 140;
            int gap = 12;
            int startX = 30;
            for (int i = 0; i < 4; ++i) {
                int bx = startX + i * (btnW + gap);
                if (x >= bx && x <= bx + btnW) {
                    ScreenEdge newEdge = (i == 0 ? EDGE_LEFT : (i == 1 ? EDGE_RIGHT : (i == 2 ? EDGE_TOP : EDGE_BOTTOM)));
                    SetConfiguredEdge(newEdge);
                    return;
                }
            }
        }

        // Push-through toggle click (Card 1: x=30 to 325, y=540 to 645)
        if (x >= 30 && x <= 325 && y >= 540 && y <= 645) {
            m_pushThroughEnabled = !m_pushThroughEnabled;
            if (m_physics) m_physics->SetPushThroughEnabled(m_pushThroughEnabled);
            Log("SETTING", std::string("Push-through barrier ") + (m_pushThroughEnabled ? "ENABLED" : "DISABLED"), Color(255, 0, 122, 255));
            InvalidateRect(m_hWnd, nullptr, FALSE);
            return;
        }

        // Edge bubble toggle & test button (Card 2: x=345 to 640, y=540 to 645)
        if (x >= 345 && x <= 640 && y >= 540 && y <= 645) {
            if (x >= 490 && x <= 625 && y >= 585 && y <= 625) {
                // Clicked "Preview Bubble ✨" button!
                Log("AFFORDANCE", "Simulating Apple Frosted Glass Edge Bubble and Burst.", Color(255, 175, 82, 222));
                if (m_overlay) {
                    m_overlay->PlayPreview(m_configuredEdge);
                }
                return;
            } else {
                m_edgeBubbleEnabled = !m_edgeBubbleEnabled;
                Log("AFFORDANCE", std::string("Visual edge capsule ") + (m_edgeBubbleEnabled ? "ENABLED" : "DISABLED"), Color(255, 0, 122, 255));
                InvalidateRect(m_hWnd, nullptr, FALSE);
                return;
            }
        }

        // Cursor speed slider (Card 3: x=660 to 955, y=540 to 645)
        if (x >= 660 && x <= 955 && y >= 580 && y <= 630) {
            m_isDraggingSlider = true;
            float sliderX = 665.0f;
            float sliderW = 245.0f;
            float ratio = std::clamp((static_cast<float>(x) - sliderX) / sliderW, 0.0f, 1.0f);
            m_mouseSpeed = 0.5f + ratio * (3.5f - 0.5f);
            InputCapturer::Instance().SetSpeedMultiplier(m_mouseSpeed);
            InvalidateRect(m_hWnd, nullptr, FALSE);
            return;
        }
    }

    // 4. Tab 1: Network Interaction
    if (m_activeTab == 1) {
        // Direct IP input box click (x: 490 to 760, y: 390 to 430)
        if (x >= 490 && x <= 760 && y >= 390 && y <= 430) {
            m_directIpFocused = true;
            InvalidateRect(m_hWnd, nullptr, FALSE);
            return;
        } else {
            m_directIpFocused = false;
        }

        // Connect Direct Button (x: 775 to 925, y: 390 to 430)
        if (x >= 775 && x <= 925 && y >= 390 && y <= 430) {
            if (!m_directIpInput.empty() && m_net) {
                m_net->SetPeerAddress(m_directIpInput, m_net->GetPort());
                m_net->SendHandshakeRequest();
                Log("CONNECT", "Directly locked peer to: " + m_directIpInput, Color(255, 48, 209, 88));
                InvalidateRect(m_hWnd, nullptr, FALSE);
            }
            return;
        }

        // Discovered Peer Connect / Disconnect button (card at y: 220 to 280)
        if (m_net) {
            auto peers = m_net->GetDiscoveredPeers();
            int py = 210;
            for (const auto& peer : peers) {
                if (x >= 800 && x <= 920 && y >= py + 12 && y <= py + 48) {
                    if (m_net->HasPeer() && m_net->GetPeerIP() == peer.ip) {
                        m_net->SendDisconnect();
                        m_net->DisconnectPeer();
                        Log("DISCONNECT", "Disconnected peer " + peer.ip, Color(255, 255, 69, 58));
                    } else {
                        m_net->SetPeerAddress(peer.ip, peer.port);
                        m_net->SendHandshakeRequest();
                        Log("CONNECT", "Paired with discovered peer: " + std::string(peer.deviceName) + " (" + peer.ip + ")", Color(255, 48, 209, 88));
                    }
                    InvalidateRect(m_hWnd, nullptr, FALSE);
                    return;
                }
                py += 75;
            }
        }
    }

    // 5. Tab 2: Physics Settings
    if (m_activeTab == 2) {
        // Emergency reset button click (x: 30 to 300, y: 440 to 490)
        if (x >= 30 && x <= 300 && y >= 440 && y <= 490) {
            if (m_physics) m_physics->ResetToLocal();
            if (m_overlay) m_overlay->Hide();
            Log("SAFETY", "Emergency cursor breakout executed.", Color(255, 255, 69, 58));
            return;
        }
        // Test edge affordance (x: 340 to 640, y: 440 to 490)
        if (x >= 340 && x <= 640 && y >= 440 && y <= 490) {
            if (m_overlay) m_overlay->PlayPreview(m_configuredEdge);
            return;
        }
    }
}

void MacUI::OnMouseUp(int x, int y) {
    m_isDraggingMonitor = false;
    m_isDraggingSlider = false;
}

void MacUI::OnCharInput(char ch) {
    if (m_directIpFocused && m_activeTab == 1) {
        if (ch == '\b') { // Backspace
            if (!m_directIpInput.empty()) {
                m_directIpInput.pop_back();
                InvalidateRect(m_hWnd, nullptr, FALSE);
            }
        } else if ((ch >= '0' && ch <= '9') || ch == '.' || ch == ':') {
            if (m_directIpInput.size() < 24) {
                m_directIpInput += ch;
                InvalidateRect(m_hWnd, nullptr, FALSE);
            }
        }
    }
}

void MacUI::OnKeyDown(WPARAM vk) {
    bool isCtrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    if (isCtrl && (vk == VK_OEM_PLUS || vk == VK_ADD)) {
        m_mouseSpeed = std::clamp(m_mouseSpeed + 0.2f, 0.5f, 3.5f);
        InputCapturer::Instance().SetSpeedMultiplier(m_mouseSpeed);
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1) << m_mouseSpeed;
        Log("SPEED", "Cursor speed increased to " + ss.str() + "x", Color(255, 10, 132, 255));
        InvalidateRect(m_hWnd, nullptr, FALSE);
        return;
    } else if (isCtrl && (vk == VK_OEM_MINUS || vk == VK_SUBTRACT)) {
        m_mouseSpeed = std::clamp(m_mouseSpeed - 0.2f, 0.5f, 3.5f);
        InputCapturer::Instance().SetSpeedMultiplier(m_mouseSpeed);
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1) << m_mouseSpeed;
        Log("SPEED", "Cursor speed decreased to " + ss.str() + "x", Color(255, 10, 132, 255));
        InvalidateRect(m_hWnd, nullptr, FALSE);
        return;
    }

    if (vk == VK_RETURN && m_directIpFocused && !m_directIpInput.empty() && m_net) {
        m_net->SetPeerAddress(m_directIpInput, m_net->GetPort());
        m_net->SendHandshakeRequest();
        Log("CONNECT", "Directly locked peer to: " + m_directIpInput, Color(255, 48, 209, 88));
        m_directIpFocused = false;
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }
}

void MacUI::DrawMacWindowControls(Graphics& g, int x, int y) {
    float r = 6.0f;
    float d = r * 2.0f;

    // Red Close
    Color redCol = (m_hoveredTrafficLight == 1) ? Color(255, 255, 95, 86) : Color(240, 255, 95, 86);
    SolidBrush redBrush(redCol);
    g.FillEllipse(&redBrush, static_cast<float>(x), static_cast<float>(y), d, d);
    Pen redBorder(Color(60, 0, 0, 0), 0.8f);
    g.DrawEllipse(&redBorder, static_cast<float>(x), static_cast<float>(y), d, d);

    if (m_hoveredTrafficLight == 1) {
        Pen iconPen(Color(180, 80, 0, 0), 1.2f);
        g.DrawLine(&iconPen, x + 3.5f, y + 3.5f, x + d - 3.5f, y + d - 3.5f);
        g.DrawLine(&iconPen, x + d - 3.5f, y + 3.5f, x + 3.5f, y + d - 3.5f);
    }

    // Yellow Minimize
    int yx = x + 22;
    Color yelCol = (m_hoveredTrafficLight == 2) ? Color(255, 255, 189, 46) : Color(240, 255, 189, 46);
    SolidBrush yelBrush(yelCol);
    g.FillEllipse(&yelBrush, static_cast<float>(yx), static_cast<float>(y), d, d);
    Pen yelBorder(Color(60, 0, 0, 0), 0.8f);
    g.DrawEllipse(&yelBorder, static_cast<float>(yx), static_cast<float>(y), d, d);

    if (m_hoveredTrafficLight == 2) {
        Pen iconPen(Color(180, 100, 60, 0), 1.2f);
        g.DrawLine(&iconPen, yx + 3.0f, y + r, yx + d - 3.0f, y + r);
    }

    // Green Zoom
    int gx = x + 44;
    Color grnCol = (m_hoveredTrafficLight == 3) ? Color(255, 39, 201, 63) : Color(240, 39, 201, 63);
    SolidBrush grnBrush(grnCol);
    g.FillEllipse(&grnBrush, static_cast<float>(gx), static_cast<float>(y), d, d);
    Pen grnBorder(Color(60, 0, 0, 0), 0.8f);
    g.DrawEllipse(&grnBorder, static_cast<float>(gx), static_cast<float>(y), d, d);

    if (m_hoveredTrafficLight == 3) {
        SolidBrush iconBrush(Color(180, 0, 80, 10));
        PointF tri1[] = { PointF(gx + 4.0f, y + 4.0f), PointF(gx + 8.0f, y + 4.0f), PointF(gx + 4.0f, y + 8.0f) };
        g.FillPolygon(&iconBrush, tri1, 3);
        PointF tri2[] = { PointF(gx + d - 4.0f, y + d - 4.0f), PointF(gx + d - 8.0f, y + d - 4.0f), PointF(gx + d - 4.0f, y + d - 8.0f) };
        g.FillPolygon(&iconBrush, tri2, 3);
    }
}

void MacUI::DrawRoundedCard(Graphics& g, const RectF& rect, float radius, Color bg, Color border) {
    GraphicsPath path;
    path.AddArc(rect.X, rect.Y, radius * 2, radius * 2, 180, 90);
    path.AddArc(rect.X + rect.Width - radius * 2, rect.Y, radius * 2, radius * 2, 270, 90);
    path.AddArc(rect.X + rect.Width - radius * 2, rect.Y + rect.Height - radius * 2, radius * 2, radius * 2, 0, 90);
    path.AddArc(rect.X, rect.Y + rect.Height - radius * 2, radius * 2, radius * 2, 90, 90);
    path.CloseFigure();

    SolidBrush brush(bg);
    g.FillPath(&brush, &path);

    if (border.GetAlpha() > 0) {
        Pen pen(border, 1.0f);
        g.DrawPath(&pen, &path);
    }
}

void MacUI::DrawCupertinoToggle(Graphics& g, float x, float y, bool active, const std::string& label, const std::string& sub) {
    float pillW = 44.0f;
    float pillH = 26.0f;
    float r = pillH / 2.0f;

    // Toggle Track
    Color trackCol = active ? Color(255, 48, 209, 88) : Color(255, 60, 60, 65);
    GraphicsPath path;
    path.AddArc(x, y, r * 2, r * 2, 90, 180);
    path.AddArc(x + pillW - r * 2, y, r * 2, r * 2, 270, 180);
    path.CloseFigure();

    SolidBrush trackBrush(trackCol);
    g.FillPath(&trackBrush, &path);

    // Thumb Knob
    float thumbD = 22.0f;
    float thumbX = active ? (x + pillW - thumbD - 2.0f) : (x + 2.0f);
    float thumbY = y + 2.0f;

    SolidBrush thumbBrush(Color(255, 255, 255, 255));
    g.FillEllipse(&thumbBrush, thumbX, thumbY, thumbD, thumbD);

    // Labels
    FontFamily ff(L"Segoe UI");
    Font fontTitle(&ff, 11, FontStyleBold, UnitPixel);
    Font fontSub(&ff, 9, FontStyleRegular, UnitPixel);

    SolidBrush textBrush(Color(255, 255, 255, 255));
    SolidBrush subBrush(Color(255, 140, 140, 150));

    std::wstring wLabel = ToWStr(label);
    std::wstring wSub = ToWStr(sub);

    g.DrawString(wLabel.c_str(), -1, &fontTitle, PointF(x + pillW + 12.0f, y - 1.0f), &textBrush);
    g.DrawString(wSub.c_str(), -1, &fontSub, PointF(x + pillW + 12.0f, y + 14.0f), &subBrush);
}

void MacUI::DrawCupertinoSlider(Graphics& g, float x, float y, float w, float value, float minVal, float maxVal, const std::string& label, const std::string& unit) {
    FontFamily ff(L"Segoe UI");
    Font fontTitle(&ff, 11, FontStyleBold, UnitPixel);
    Font fontVal(&ff, 10, FontStyleRegular, UnitPixel);

    SolidBrush textBrush(Color(255, 255, 255, 255));
    SolidBrush valBrush(Color(255, 10, 132, 255));

    std::wstring wLabel = ToWStr(label);
    g.DrawString(wLabel.c_str(), -1, &fontTitle, PointF(x, y), &textBrush);

    std::wstringstream ss;
    ss << static_cast<int>(value) << L" " << ToWStr(unit);
    g.DrawString(ss.str().c_str(), -1, &fontVal, PointF(x + w - 75.0f, y), &valBrush);

    float trackY = y + 22.0f;
    float trackH = 6.0f;
    float ratio = std::clamp((value - minVal) / (maxVal - minVal), 0.0f, 1.0f);

    // Inactive track
    RectF bgTrack(x, trackY, w, trackH);
    DrawRoundedCard(g, bgTrack, 3.0f, Color(255, 55, 55, 60), Color(0, 0, 0, 0));

    // Active track
    RectF activeTrack(x, trackY, w * ratio, trackH);
    DrawRoundedCard(g, activeTrack, 3.0f, Color(255, 10, 132, 255), Color(0, 0, 0, 0));

    // Thumb knob
    float thumbX = x + (w * ratio) - 9.0f;
    float thumbY = trackY - 6.0f;
    SolidBrush knobBrush(Color(255, 255, 255, 255));
    g.FillEllipse(&knobBrush, thumbX, thumbY, 18.0f, 18.0f);
    Pen knobBorder(Color(60, 0, 0, 0), 1.0f);
    g.DrawEllipse(&knobBorder, thumbX, thumbY, 18.0f, 18.0f);
}

void MacUI::DrawMonitorGraphic(Graphics& g, float x, float y, float w, float h, const std::string& title, const std::string& sub, const std::string& badge, bool isHost, bool isConnected) {
    // Monitor Stand
    float standW = 42.0f;
    float standH = 20.0f;
    float standX = x + (w - standW) / 2.0f;
    float standY = y + h;

    LinearGradientBrush standBrush(
        PointF(standX, standY), PointF(standX, standY + standH),
        Color(255, 120, 120, 130), Color(255, 60, 60, 70)
    );
    g.FillRectangle(&standBrush, standX, standY, standW, standH);

    // Monitor Base
    float baseW = 80.0f;
    float baseH = 6.0f;
    float baseX = x + (w - baseW) / 2.0f;
    float baseY = standY + standH - 2.0f;
    DrawRoundedCard(g, RectF(baseX, baseY, baseW, baseH), 3.0f, Color(255, 130, 130, 140), Color(40, 255, 255, 255));

    // Outer Display Bezel
    RectF bezelRect(x, y, w, h);
    DrawRoundedCard(g, bezelRect, 10.0f, Color(255, 30, 30, 36), Color(255, 65, 65, 75));

    // Inner Screen Canvas
    float margin = 7.0f;
    RectF screenRect(x + margin, y + margin, w - (margin * 2.0f), h - (margin * 2.0f));

    // Wallpaper Gradient
    Color col1 = isHost ? Color(255, 15, 32, 67) : Color(255, 50, 18, 70);
    Color col2 = isHost ? Color(255, 45, 85, 145) : Color(255, 130, 40, 140);
    LinearGradientBrush screenBrush(
        PointF(screenRect.X, screenRect.Y),
        PointF(screenRect.X + screenRect.Width, screenRect.Y + screenRect.Height),
        col1, col2
    );
    DrawRoundedCard(g, screenRect, 6.0f, Color(255, 20, 20, 25), Color(0, 0, 0, 0));
    g.FillRectangle(&screenBrush, screenRect.X, screenRect.Y, screenRect.Width, screenRect.Height);

    // Subtle Mac Menu Bar strip at top of screen
    SolidBrush barBrush(Color(70, 255, 255, 255));
    g.FillRectangle(&barBrush, screenRect.X, screenRect.Y, screenRect.Width, 8.0f);

    // Subtle Mac Dock silhouette at bottom of screen
    float dockW = screenRect.Width * 0.5f;
    float dockH = 5.0f;
    DrawRoundedCard(g, RectF(screenRect.X + (screenRect.Width - dockW) / 2.0f, screenRect.Y + screenRect.Height - dockH - 3.0f, dockW, dockH), 2.5f, Color(80, 255, 255, 255), Color(0, 0, 0, 0));

    // Centered Content & Badges
    FontFamily ff(L"Segoe UI");
    Font fontTitle(&ff, 12, FontStyleBold, UnitPixel);
    Font fontSub(&ff, 9, FontStyleRegular, UnitPixel);
    Font fontBadge(&ff, 8, FontStyleBold, UnitPixel);

    StringFormat sfCenter;
    sfCenter.SetAlignment(StringAlignmentCenter);
    sfCenter.SetLineAlignment(StringAlignmentCenter);

    SolidBrush whiteBrush(Color(255, 255, 255, 255));
    SolidBrush subBrush(Color(220, 210, 220, 240));

    std::wstring wTitle = ToWStr(title);
    std::wstring wSub = ToWStr(sub);
    std::wstring wBadge = ToWStr(badge);

    float centerY = screenRect.Y + (screenRect.Height / 2.0f) - 6.0f;
    g.DrawString(wTitle.c_str(), -1, &fontTitle, PointF(screenRect.X + screenRect.Width / 2.0f, centerY - 8.0f), &sfCenter, &whiteBrush);
    g.DrawString(wSub.c_str(), -1, &fontSub, PointF(screenRect.X + screenRect.Width / 2.0f, centerY + 8.0f), &sfCenter, &subBrush);

    // Status Pill Badge
    float pillW = 86.0f;
    float pillH = 16.0f;
    float pillX = screenRect.X + (screenRect.Width - pillW) / 2.0f;
    float pillY = screenRect.Y + screenRect.Height - 24.0f;

    Color badgeBg = isConnected ? Color(200, 30, 150, 60) : (isHost ? Color(200, 10, 120, 230) : Color(200, 200, 130, 20));
    DrawRoundedCard(g, RectF(pillX, pillY, pillW, pillH), 8.0f, badgeBg, Color(60, 255, 255, 255));
    g.DrawString(wBadge.c_str(), -1, &fontBadge, PointF(screenRect.X + screenRect.Width / 2.0f, pillY + 8.0f), &sfCenter, &whiteBrush);
}

void MacUI::Render(HDC hdc, int w, int h) {
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);

    // 1. Sleek macOS Ventura Space Black Glass Background
    LinearGradientBrush bgBrush(
        PointF(0.0f, 0.0f), PointF(0.0f, static_cast<float>(h)),
        Color(255, 28, 28, 34), Color(255, 18, 18, 22)
    );
    g.FillRectangle(&bgBrush, 0, 0, w, h);

    // Subtle 1px crisp specular rim border
    Pen borderPen(Color(45, 255, 255, 255), 1.0f);
    g.DrawRectangle(&borderPen, 0, 0, w - 1, h - 1);

    // 2. Render Top Header Bar & Traffic Lights
    RenderTopBar(g, w, h);

    // 3. Render Navigation Tab Bar
    RenderTabBar(g, w, h);

    // 4. Render Active Tab View
    switch (m_activeTab) {
        case 0: RenderTabArrangement(g, w, h); break;
        case 1: RenderTabNetwork(g, w, h); break;
        case 2: RenderTabPhysics(g, w, h); break;
        case 3: RenderTabTelemetry(g, w, h); break;
        default: break;
    }
}

void MacUI::RenderTopBar(Graphics& g, int w, int h) {
    // Traffic Light Buttons
    DrawMacWindowControls(g, 22, 21);

    FontFamily ff(L"Segoe UI");
    Font fontTitle(&ff, 13, FontStyleBold, UnitPixel);
    Font fontSub(&ff, 10, FontStyleRegular, UnitPixel);
    Font fontStatus(&ff, 9, FontStyleBold, UnitPixel);

    SolidBrush textBrush(Color(255, 255, 255, 255));
    SolidBrush subBrush(Color(255, 140, 140, 150));

    // Dual display glyph icon (macOS Universal Control icon)
    float iconX = 95.0f;
    float iconY = 18.0f;
    Pen iconPen(Color(255, 10, 132, 255), 1.5f);
    g.DrawRectangle(&iconPen, iconX, iconY, 14.0f, 10.0f);
    g.DrawRectangle(&iconPen, iconX + 10.0f, iconY + 5.0f, 14.0f, 10.0f);

    g.DrawString(L"Universal Control", -1, &fontTitle, PointF(iconX + 32.0f, 13.0f), &textBrush);
    g.DrawString(L"Displays & Input Bridging Engine • Reverse Engineered", -1, &fontSub, PointF(iconX + 32.0f, 29.0f), &subBrush);

    // Right Side: Live Status Capsule
    bool hasPeer = (m_net && m_net->HasPeer());
    std::wstring statusStr = hasPeer ? (L"Connected: " + ToWStr(m_net->GetPeerDeviceName().empty() ? m_net->GetPeerIP() : m_net->GetPeerDeviceName())) 
                                     : L"Scanning Subnet (Port 54823)";
    float pillW = hasPeer ? 240.0f : 200.0f;
    float pillH = 26.0f;
    float pillX = w - pillW - 24.0f;
    float pillY = 15.0f;

    DrawRoundedCard(g, RectF(pillX, pillY, pillW, pillH), 13.0f, Color(255, 36, 36, 44), Color(50, 255, 255, 255));

    // Glowing LED Dot
    float ledX = pillX + 12.0f;
    float ledY = pillY + 9.0f;
    float ledR = 4.0f;
    Color ledCol = hasPeer ? Color(255, 48, 209, 88) : Color(static_cast<BYTE>(255 * m_statusGlowAlpha), 255, 159, 10);
    SolidBrush ledBrush(ledCol);
    g.FillEllipse(&ledBrush, ledX, ledY, ledR * 2, ledR * 2);

    // Outer bloom
    Color bloomCol = hasPeer ? Color(60, 48, 209, 88) : Color(static_cast<BYTE>(80 * m_statusGlowAlpha), 255, 159, 10);
    SolidBrush bloomBrush(bloomCol);
    g.FillEllipse(&bloomBrush, ledX - 2.0f, ledY - 2.0f, (ledR + 2.0f) * 2, (ledR + 2.0f) * 2);

    g.DrawString(statusStr.c_str(), -1, &fontStatus, PointF(ledX + 14.0f, pillY + 6.0f), &textBrush);
}

void MacUI::RenderTabBar(Graphics& g, int w, int h) {
    const wchar_t* tabTitles[] = {
        L"Displays Arrangement",
        L"Network & Radar",
        L"Physics & Controls",
        L"Live Diagnostics"
    };

    int tabStartX = 30;
    int tabW = 160;
    int tabH = 34;
    int tabY = 56;

    FontFamily ff(L"Segoe UI");
    Font fontTab(&ff, 11, FontStyleBold, UnitPixel);
    StringFormat sfCenter;
    sfCenter.SetAlignment(StringAlignmentCenter);
    sfCenter.SetLineAlignment(StringAlignmentCenter);

    for (int i = 0; i < 4; ++i) {
        int tx = tabStartX + (i * (tabW + 10));
        RectF tabRect(static_cast<float>(tx), static_cast<float>(tabY), static_cast<float>(tabW), static_cast<float>(tabH));

        if (m_activeTab == i) {
            // Active Tab Cupertino Pill
            DrawRoundedCard(g, tabRect, 10.0f, Color(255, 10, 132, 255), Color(80, 255, 255, 255));
            SolidBrush textBrush(Color(255, 255, 255, 255));
            g.DrawString(tabTitles[i], -1, &fontTab, PointF(tx + tabW / 2.0f, tabY + tabH / 2.0f), &sfCenter, &textBrush);
        } else {
            // Inactive Tab
            Color bgCol = (m_hoveredTab == i) ? Color(255, 42, 42, 50) : Color(255, 32, 32, 38);
            DrawRoundedCard(g, tabRect, 10.0f, bgCol, Color(30, 255, 255, 255));
            SolidBrush textBrush(Color(255, 160, 160, 175));
            g.DrawString(tabTitles[i], -1, &fontTab, PointF(tx + tabW / 2.0f, tabY + tabH / 2.0f), &sfCenter, &textBrush);
        }
    }
}

void MacUI::RenderTabArrangement(Graphics& g, int w, int h) {
    // 1. Canvas Card (The Displays Arrangement Area)
    RectF canvasRect(30.0f, 104.0f, static_cast<float>(w - 60), 365.0f);
    DrawRoundedCard(g, canvasRect, 14.0f, Color(255, 20, 20, 25), Color(40, 255, 255, 255));

    // Subtle technical grid dots inside canvas
    SolidBrush dotBrush(Color(25, 255, 255, 255));
    for (float dx = canvasRect.X + 24.0f; dx < canvasRect.X + canvasRect.Width - 10.0f; dx += 28.0f) {
        for (float dy = canvasRect.Y + 24.0f; dy < canvasRect.Y + canvasRect.Height - 10.0f; dy += 28.0f) {
            g.FillEllipse(&dotBrush, dx, dy, 2.0f, 2.0f);
        }
    }

    // Canvas Header Text
    FontFamily ff(L"Segoe UI");
    Font fontCanvasTitle(&ff, 12, FontStyleBold, UnitPixel);
    Font fontCanvasSub(&ff, 10, FontStyleRegular, UnitPixel);
    SolidBrush whiteBrush(Color(255, 255, 255, 255));
    SolidBrush subBrush(Color(255, 130, 130, 140));

    g.DrawString(L"Display Arrangement Canvas", -1, &fontCanvasTitle, PointF(canvasRect.X + 20.0f, canvasRect.Y + 16.0f), &whiteBrush);
    g.DrawString(L"Select or click edge buttons below to mirror the physical layout of your displays.", -1, &fontCanvasSub, PointF(canvasRect.X + 20.0f, canvasRect.Y + 33.0f), &subBrush);

    // Local PC Monitor (Host)
    float hostW = 180.0f;
    float hostH = 120.0f;
    float hostX = canvasRect.X + (canvasRect.Width / 2.0f) - (hostW / 2.0f);
    float hostY = canvasRect.Y + 140.0f;

    std::string hostRes = std::to_string(m_screenWidth) + "x" + std::to_string(m_screenHeight);
    DrawMonitorGraphic(g, hostX, hostY, hostW, hostH, m_localHostName, hostRes, "THIS PC (HOST)", true, true);

    // Remote PC Monitor (Peer)
    float peerW = 160.0f;
    float peerH = 110.0f;
    float peerX = hostX;
    float peerY = hostY;

    float gap = 36.0f;
    if (m_configuredEdge == EDGE_RIGHT) {
        peerX = hostX + hostW + gap;
        peerY = hostY + (hostH - peerH) / 2.0f;
    } else if (m_configuredEdge == EDGE_LEFT) {
        peerX = hostX - peerW - gap;
        peerY = hostY + (hostH - peerH) / 2.0f;
    } else if (m_configuredEdge == EDGE_TOP) {
        peerX = hostX + (hostW - peerW) / 2.0f;
        peerY = hostY - peerH - gap + 10.0f;
    } else { // EDGE_BOTTOM
        peerX = hostX + (hostW - peerW) / 2.0f;
        peerY = hostY + hostH + gap;
    }

    bool hasPeer = (m_net && m_net->HasPeer());
    std::string peerName = hasPeer ? (m_net->GetPeerDeviceName().empty() ? m_net->GetPeerIP() : m_net->GetPeerDeviceName()) : "Discovered Peer";
    std::string peerRes = hasPeer ? "Target Display" : "Subnet Radar";
    std::string peerBadge = hasPeer ? "CONNECTED" : "DISCOVERING";

    DrawMonitorGraphic(g, peerX, peerY, peerW, peerH, peerName, peerRes, peerBadge, false, hasPeer);

    // 2. Animated Apple Universal Control Linking Beam along active boundary
    float beamX1 = 0, beamY1 = 0, beamX2 = 0, beamY2 = 0;
    if (m_configuredEdge == EDGE_RIGHT) {
        beamX1 = hostX + hostW + (gap / 2.0f);
        beamY1 = hostY + 15.0f;
        beamX2 = beamX1;
        beamY2 = hostY + hostH - 15.0f;
    } else if (m_configuredEdge == EDGE_LEFT) {
        beamX1 = hostX - (gap / 2.0f);
        beamY1 = hostY + 15.0f;
        beamX2 = beamX1;
        beamY2 = hostY + hostH - 15.0f;
    } else if (m_configuredEdge == EDGE_TOP) {
        beamX1 = hostX + 20.0f;
        beamY1 = hostY - (gap / 2.0f) + 5.0f;
        beamX2 = hostX + hostW - 20.0f;
        beamY2 = beamY1;
    } else { // EDGE_BOTTOM
        beamX1 = hostX + 20.0f;
        beamY1 = hostY + hostH + (gap / 2.0f);
        beamX2 = hostX + hostW - 20.0f;
        beamY2 = beamY1;
    }

    // Outer glowing bloom
    Pen bloomPen(Color(80, 10, 132, 255), 7.0f);
    g.DrawLine(&bloomPen, beamX1, beamY1, beamX2, beamY2);

    // Core crisp cyan-blue beam
    Pen beamPen(Color(255, 0, 180, 255), 2.5f);
    g.DrawLine(&beamPen, beamX1, beamY1, beamX2, beamY2);

    // Animated pulse packet traveling on beam
    float pulseT = m_bridgePulseOffset / 30.0f;
    float pulsePx = beamX1 + (beamX2 - beamX1) * pulseT;
    float pulsePy = beamY1 + (beamY2 - beamY1) * pulseT;
    SolidBrush pulseBrush(Color(255, 255, 255, 255));
    g.FillEllipse(&pulseBrush, pulsePx - 4.0f, pulsePy - 4.0f, 8.0f, 8.0f);

    // 3. Four Quick-Select Edge Buttons (y: 486, h: 40)
    const wchar_t* edgeLabels[] = { L"◀  Left Display", L"Right Display  ▶", L"▲  Top Display", L"▼  Bottom Display" };
    int btnW = 140;
    int btnGap = 12;
    int startBtnX = 30;
    Font fontEdgeBtn(&ff, 11, FontStyleBold, UnitPixel);
    StringFormat sfCenter;
    sfCenter.SetAlignment(StringAlignmentCenter);
    sfCenter.SetLineAlignment(StringAlignmentCenter);

    for (int i = 0; i < 4; ++i) {
        int bx = startBtnX + i * (btnW + btnGap);
        RectF bRect(static_cast<float>(bx), 486.0f, static_cast<float>(btnW), 40.0f);
        bool isActive = (m_configuredEdge == i);

        if (isActive) {
            DrawRoundedCard(g, bRect, 10.0f, Color(255, 10, 132, 255), Color(80, 255, 255, 255));
            SolidBrush bText(Color(255, 255, 255, 255));
            g.DrawString(edgeLabels[i], -1, &fontEdgeBtn, PointF(bx + btnW / 2.0f, 506.0f), &sfCenter, &bText);
        } else {
            Color bg = (m_hoveredEdgeBtn == i) ? Color(255, 48, 48, 56) : Color(255, 34, 34, 40);
            DrawRoundedCard(g, bRect, 10.0f, bg, Color(40, 255, 255, 255));
            SolidBrush bText(Color(255, 170, 170, 185));
            g.DrawString(edgeLabels[i], -1, &fontEdgeBtn, PointF(bx + btnW / 2.0f, 506.0f), &sfCenter, &bText);
        }
    }

    // 4. Quick Controls Row (Cards 1, 2, 3 at y: 538, h: 110)
    float cardW = (w - 60.0f - 24.0f) / 3.0f;
    float cardY = 538.0f;
    float cardH = 110.0f;

    // Card 1: Cursor Push-Through Handoff
    RectF c1(30.0f, cardY, cardW, cardH);
    DrawRoundedCard(g, c1, 12.0f, Color(255, 28, 28, 34), Color(40, 255, 255, 255));
    DrawCupertinoToggle(g, c1.X + 16.0f, c1.Y + 22.0f, m_pushThroughEnabled, "Push-Through Handoff", "Move pointer seamlessly to nearby PC");
    Font fontTip(&ff, 9, FontStyleRegular, UnitPixel);
    SolidBrush tipBrush(Color(255, 120, 120, 130));
    g.DrawString(L"Tap card to toggle boundary crossing.", -1, &fontTip, PointF(c1.X + 16.0f, c1.Y + 76.0f), &tipBrush);

    // Card 2: Edge Bubble Affordance & Preview
    RectF c2(30.0f + cardW + 12.0f, cardY, cardW, cardH);
    DrawRoundedCard(g, c2, 12.0f, Color(255, 28, 28, 34), Color(40, 255, 255, 255));
    DrawCupertinoToggle(g, c2.X + 16.0f, c2.Y + 22.0f, m_edgeBubbleEnabled, "Frosted Edge Bubble", "Translucent Apple pill indicator");

    // "Preview Bubble ✨" button
    RectF testBtn(c2.X + 16.0f, c2.Y + 68.0f, 135.0f, 28.0f);
    DrawRoundedCard(g, testBtn, 7.0f, Color(255, 175, 82, 222), Color(80, 255, 255, 255));
    Font fontBtn(&ff, 10, FontStyleBold, UnitPixel);
    SolidBrush btnText(Color(255, 255, 255, 255));
    g.DrawString(L"Preview Bubble ✨", -1, &fontBtn, PointF(testBtn.X + testBtn.Width / 2.0f, testBtn.Y + testBtn.Height / 2.0f), &sfCenter, &btnText);

    // Card 3: Apple Cursor Speed & Sensitivity Slider
    RectF c3(30.0f + (cardW + 12.0f) * 2.0f, cardY, cardW, cardH);
    DrawRoundedCard(g, c3, 12.0f, Color(255, 28, 28, 34), Color(40, 255, 255, 255));
    DrawCupertinoSlider(g, c3.X + 16.0f, c3.Y + 20.0f, c3.Width - 32.0f, m_mouseSpeed, 0.5f, 3.5f, "Cursor Speed / Sensitivity", "x");
    std::wstring presetStr = (m_mouseSpeed < 1.0f) ? L"Speed: Slow (High Precision)" : (m_mouseSpeed > 2.0f ? L"Speed: Fast (Multi-Monitor)" : L"Speed: macOS Natural (Balanced)");
    g.DrawString(presetStr.c_str(), -1, &fontTip, PointF(c3.X + 16.0f, c3.Y + 76.0f), &tipBrush);
}

void MacUI::RenderTabNetwork(Graphics& g, int w, int h) {
    FontFamily ff(L"Segoe UI");
    Font fontHeader(&ff, 13, FontStyleBold, UnitPixel);
    Font fontSub(&ff, 10, FontStyleRegular, UnitPixel);
    Font fontLabel(&ff, 11, FontStyleBold, UnitPixel);
    Font fontVal(&ff, 10, FontStyleRegular, UnitPixel);
    StringFormat sfCenter;
    sfCenter.SetAlignment(StringAlignmentCenter);
    sfCenter.SetLineAlignment(StringAlignmentCenter);

    SolidBrush whiteBrush(Color(255, 255, 255, 255));
    SolidBrush subBrush(Color(255, 140, 140, 150));
    SolidBrush greenBrush(Color(255, 48, 209, 88));
    SolidBrush blueBrush(Color(255, 10, 132, 255));

    // Left Column: Apple Subnet Radar (x: 30, w: 430, h: 540)
    RectF radarCard(30.0f, 104.0f, 430.0f, 540.0f);
    DrawRoundedCard(g, radarCard, 14.0f, Color(255, 22, 22, 28), Color(40, 255, 255, 255));

    g.DrawString(L"Subnet Auto-Discovery Radar", -1, &fontHeader, PointF(radarCard.X + 24.0f, radarCard.Y + 20.0f), &whiteBrush);
    g.DrawString(L"Scanning local Wi-Fi & Ethernet for nearby Butterflow peers.", -1, &fontSub, PointF(radarCard.X + 24.0f, radarCard.Y + 38.0f), &subBrush);

    // Radar Center
    float rCenterX = radarCard.X + radarCard.Width / 2.0f;
    float rCenterY = radarCard.Y + 230.0f;
    float maxRadarR = 140.0f;

    // Concentric Radar Rings
    Pen ringPen(Color(45, 10, 132, 255), 1.0f);
    g.DrawEllipse(&ringPen, rCenterX - 45.0f, rCenterY - 45.0f, 90.0f, 90.0f);
    g.DrawEllipse(&ringPen, rCenterX - 90.0f, rCenterY - 90.0f, 180.0f, 180.0f);
    g.DrawEllipse(&ringPen, rCenterX - maxRadarR, rCenterY - maxRadarR, maxRadarR * 2.0f, maxRadarR * 2.0f);

    // Crosshairs
    Pen crossPen(Color(30, 255, 255, 255), 1.0f);
    g.DrawLine(&crossPen, rCenterX - maxRadarR, rCenterY, rCenterX + maxRadarR, rCenterY);
    g.DrawLine(&crossPen, rCenterX, rCenterY - maxRadarR, rCenterX, rCenterY + maxRadarR);

    // Rotating Sweep Beam
    float rad = m_radarAngle * 3.14159265f / 180.0f;
    float sweepEndX = rCenterX + maxRadarR * std::cos(rad);
    float sweepEndY = rCenterY + maxRadarR * std::sin(rad);

    Pen sweepPen(Color(200, 10, 132, 255), 2.0f);
    g.DrawLine(&sweepPen, rCenterX, rCenterY, sweepEndX, sweepEndY);

    // Center host blip
    SolidBrush hostBlip(Color(255, 0, 122, 255));
    g.FillEllipse(&hostBlip, rCenterX - 6.0f, rCenterY - 6.0f, 12.0f, 12.0f);

    // If peer discovered, draw blip on radar
    if (m_net && m_net->HasPeer()) {
        float pBlipX = rCenterX + 75.0f;
        float pBlipY = rCenterY - 45.0f;
        SolidBrush peerBlip(Color(255, 48, 209, 88));
        g.FillEllipse(&peerBlip, pBlipX - 6.0f, pBlipY - 6.0f, 12.0f, 12.0f);

        // Halo
        SolidBrush haloBrush(Color(static_cast<BYTE>(100 * m_statusGlowAlpha), 48, 209, 88));
        g.FillEllipse(&haloBrush, pBlipX - 12.0f, pBlipY - 12.0f, 24.0f, 24.0f);
    }

    // Radar Footer Info
    float infoY = radarCard.Y + 410.0f;
    g.DrawString(L"Local Host:", -1, &fontLabel, PointF(radarCard.X + 24.0f, infoY), &whiteBrush);
    g.DrawString(ToWStr(m_localHostName).c_str(), -1, &fontVal, PointF(radarCard.X + 110.0f, infoY), &blueBrush);

    g.DrawString(L"Local IP:", -1, &fontLabel, PointF(radarCard.X + 24.0f, infoY + 24.0f), &whiteBrush);
    g.DrawString(ToWStr(m_localIP).c_str(), -1, &fontVal, PointF(radarCard.X + 110.0f, infoY + 24.0f), &subBrush);

    g.DrawString(L"Port:", -1, &fontLabel, PointF(radarCard.X + 24.0f, infoY + 48.0f), &whiteBrush);
    g.DrawString(L"54823 (Zero-Delay UDP)", -1, &fontVal, PointF(radarCard.X + 110.0f, infoY + 48.0f), &subBrush);

    g.DrawString(L"Subnet Target:", -1, &fontLabel, PointF(radarCard.X + 24.0f, infoY + 72.0f), &whiteBrush);
    g.DrawString(L"255.255.255.255:54823", -1, &fontVal, PointF(radarCard.X + 110.0f, infoY + 72.0f), &subBrush);

    // Right Column: Discovered Peers & Direct Connect (x: 480, w: 470, h: 540)
    RectF peerCard(480.0f, 104.0f, 470.0f, 540.0f);
    DrawRoundedCard(g, peerCard, 14.0f, Color(255, 22, 22, 28), Color(40, 255, 255, 255));

    g.DrawString(L"Discovered LAN Peers", -1, &fontHeader, PointF(peerCard.X + 24.0f, peerCard.Y + 20.0f), &whiteBrush);
    g.DrawString(L"Devices running Butterflow on your Wi-Fi, Ethernet, or Hotspot.", -1, &fontSub, PointF(peerCard.X + 24.0f, peerCard.Y + 38.0f), &subBrush);

    auto peers = m_net ? m_net->GetDiscoveredPeers() : std::vector<DiscoveredPeerInfo>();
    if (peers.empty()) {
        // Empty state placeholder
        RectF emptyRect(peerCard.X + 24.0f, peerCard.Y + 75.0f, peerCard.Width - 48.0f, 140.0f);
        DrawRoundedCard(g, emptyRect, 10.0f, Color(255, 30, 30, 38), Color(30, 255, 255, 255));
        g.DrawString(L"Listening for nearby Universal Control beacons...", -1, &fontVal, PointF(emptyRect.X + emptyRect.Width / 2.0f, emptyRect.Y + 50.0f), &sfCenter, &subBrush);
        g.DrawString(L"Make sure Butterflow is open on the other Windows PC on the same Wi-Fi.", -1, &fontSub, PointF(emptyRect.X + emptyRect.Width / 2.0f, emptyRect.Y + 75.0f), &sfCenter, &subBrush);
    } else {
        int py = static_cast<int>(peerCard.Y + 75.0f);
        for (const auto& peer : peers) {
            RectF pBox(peerCard.X + 24.0f, static_cast<float>(py), peerCard.Width - 48.0f, 65.0f);
            DrawRoundedCard(g, pBox, 10.0f, Color(255, 34, 34, 42), Color(50, 255, 255, 255));

            // Monitor Icon
            Pen mPen(Color(255, 10, 132, 255), 1.5f);
            g.DrawRectangle(&mPen, pBox.X + 16.0f, pBox.Y + 16.0f, 28.0f, 20.0f);
            g.DrawLine(&mPen, pBox.X + 26.0f, pBox.Y + 36.0f, pBox.X + 34.0f, pBox.Y + 36.0f);
            g.DrawLine(&mPen, pBox.X + 22.0f, pBox.Y + 40.0f, pBox.X + 38.0f, pBox.Y + 40.0f);

            // Peer Info Text
            std::wstring pName = ToWStr(peer.deviceName);
            std::wstring pSub = ToWStr(peer.ip) + L" • " + std::to_wstring(peer.screenWidth) + L"x" + std::to_wstring(peer.screenHeight);
            g.DrawString(pName.c_str(), -1, &fontLabel, PointF(pBox.X + 58.0f, pBox.Y + 14.0f), &whiteBrush);
            g.DrawString(pSub.c_str(), -1, &fontSub, PointF(pBox.X + 58.0f, pBox.Y + 34.0f), &subBrush);

            // Connect / Disconnect Button
            bool isCurrentPeer = (m_net && m_net->HasPeer() && m_net->GetPeerIP() == peer.ip);
            RectF connBtn(pBox.X + pBox.Width - 110.0f, pBox.Y + 14.0f, 95.0f, 36.0f);
            Color btnBg = isCurrentPeer ? Color(255, 255, 69, 58) : Color(255, 10, 132, 255);
            DrawRoundedCard(g, connBtn, 8.0f, btnBg, Color(60, 255, 255, 255));
            const wchar_t* btnLbl = isCurrentPeer ? L"Disconnect" : L"Pair & Connect";
            g.DrawString(btnLbl, -1, &fontLabel, PointF(connBtn.X + connBtn.Width / 2.0f, connBtn.Y + connBtn.Height / 2.0f), &sfCenter, &whiteBrush);

            py += 75;
        }
    }

    // Direct IP Connection Card (Bottom right)
    float directY = peerCard.Y + 280.0f;
    g.DrawString(L"Direct IP Connection", -1, &fontHeader, PointF(peerCard.X + 24.0f, directY), &whiteBrush);
    g.DrawString(L"Enter IP address manually if subnet broadcasts are filtered on your router.", -1, &fontSub, PointF(peerCard.X + 24.0f, directY + 18.0f), &subBrush);

    // Text Input Box (x: peerCard.X + 24, w: 270, h: 42)
    RectF inputRect(peerCard.X + 24.0f, directY + 48.0f, 270.0f, 42.0f);
    Color inputBorder = m_directIpFocused ? Color(255, 10, 132, 255) : Color(45, 255, 255, 255);
    DrawRoundedCard(g, inputRect, 8.0f, Color(255, 28, 28, 36), inputBorder);

    Font fontInput(&ff, 12, FontStyleRegular, UnitPixel);
    if (m_directIpInput.empty() && !m_directIpFocused) {
        g.DrawString(L"e.g. 192.168.1.105", -1, &fontInput, PointF(inputRect.X + 12.0f, inputRect.Y + 12.0f), &subBrush);
    } else {
        std::wstring wInput = ToWStr(m_directIpInput);
        if (m_directIpFocused && (GetTickCount() / 500) % 2 == 0) {
            wInput += L"|";
        }
        g.DrawString(wInput.c_str(), -1, &fontInput, PointF(inputRect.X + 12.0f, inputRect.Y + 12.0f), &whiteBrush);
    }

    // Connect Direct Button
    RectF directBtn(inputRect.X + inputRect.Width + 14.0f, directY + 48.0f, 130.0f, 42.0f);
    DrawRoundedCard(g, directBtn, 8.0f, Color(255, 10, 132, 255), Color(60, 255, 255, 255));
    g.DrawString(L"Connect Direct", -1, &fontLabel, PointF(directBtn.X + directBtn.Width / 2.0f, directBtn.Y + directBtn.Height / 2.0f), &sfCenter, &whiteBrush);

    // Safety Callout Box at bottom
    RectF safetyBox(peerCard.X + 24.0f, directY + 120.0f, peerCard.Width - 48.0f, 100.0f);
    DrawRoundedCard(g, safetyBox, 10.0f, Color(255, 34, 30, 25), Color(60, 255, 159, 10));
    SolidBrush amberBrush(Color(255, 255, 159, 10));
    g.DrawString(L"Emergency Breakout Failsafe", -1, &fontLabel, PointF(safetyBox.X + 16.0f, safetyBox.Y + 14.0f), &amberBrush);
    g.DrawString(L"Press  Ctrl + Alt + Shift + Esc  at any time to immediately break", -1, &fontSub, PointF(safetyBox.X + 16.0f, safetyBox.Y + 36.0f), &whiteBrush);
    g.DrawString(L"cursor capture and regain 100% control on your host computer.", -1, &fontSub, PointF(safetyBox.X + 16.0f, safetyBox.Y + 54.0f), &subBrush);
}

void MacUI::RenderTabPhysics(Graphics& g, int w, int h) {
    FontFamily ff(L"Segoe UI");
    Font fontHeader(&ff, 13, FontStyleBold, UnitPixel);
    Font fontSub(&ff, 10, FontStyleRegular, UnitPixel);
    Font fontLabel(&ff, 11, FontStyleBold, UnitPixel);
    Font fontCode(&ff, 11, FontStyleBold, UnitPixel);
    StringFormat sfCenter;
    sfCenter.SetAlignment(StringAlignmentCenter);
    sfCenter.SetLineAlignment(StringAlignmentCenter);

    SolidBrush whiteBrush(Color(255, 255, 255, 255));
    SolidBrush subBrush(Color(255, 140, 140, 150));
    SolidBrush blueBrush(Color(255, 10, 132, 255));
    SolidBrush greenBrush(Color(255, 48, 209, 88));

    // Left Column: Physics Model (x: 30, w: 430, h: 540)
    RectF physicsCard(30.0f, 104.0f, 430.0f, 540.0f);
    DrawRoundedCard(g, physicsCard, 14.0f, Color(255, 22, 22, 28), Color(40, 255, 255, 255));

    g.DrawString(L"Apple Spring Boundary Dynamics", -1, &fontHeader, PointF(physicsCard.X + 24.0f, physicsCard.Y + 20.0f), &whiteBrush);
    g.DrawString(L"Reverse-engineered spring dampening barrier matching macOS.", -1, &fontSub, PointF(physicsCard.X + 24.0f, physicsCard.Y + 38.0f), &subBrush);

    // Formula Box
    RectF formulaBox(physicsCard.X + 24.0f, physicsCard.Y + 70.0f, physicsCard.Width - 48.0f, 85.0f);
    DrawRoundedCard(g, formulaBox, 10.0f, Color(255, 16, 22, 34), Color(60, 10, 132, 255));
    g.DrawString(L"F_spring = -k * Δx - c * v", -1, &fontCode, PointF(formulaBox.X + 20.0f, formulaBox.Y + 18.0f), &blueBrush);
    g.DrawString(L"Requires continuous, intentional pointer push across border", -1, &fontSub, PointF(formulaBox.X + 20.0f, formulaBox.Y + 40.0f), &whiteBrush);
    g.DrawString(L"before cursor breaks through barrier to remote screen.", -1, &fontSub, PointF(formulaBox.X + 20.0f, formulaBox.Y + 58.0f), &subBrush);

    // Spring Tension Slider Card
    float slY = physicsCard.Y + 180.0f;
    DrawCupertinoSlider(g, physicsCard.X + 24.0f, slY, physicsCard.Width - 48.0f, m_springTension, 15.0f, 65.0f, "Push Resistance Threshold", "px displacement");

    // Live accumulated push meter
    float accY = slY + 70.0f;
    g.DrawString(L"Current Edge Push:", -1, &fontLabel, PointF(physicsCard.X + 24.0f, accY), &whiteBrush);
    float pushVal = m_physics ? m_physics->GetAccumulatedPush() : 0.0f;
    float maxThresh = m_physics ? m_physics->GetPushThreshold() : 35.0f;
    float pushRatio = std::clamp(pushVal / maxThresh, 0.0f, 1.0f);

    RectF meterBg(physicsCard.X + 24.0f, accY + 22.0f, physicsCard.Width - 48.0f, 12.0f);
    DrawRoundedCard(g, meterBg, 6.0f, Color(255, 45, 45, 52), Color(0, 0, 0, 0));
    RectF meterFill(physicsCard.X + 24.0f, accY + 22.0f, (physicsCard.Width - 48.0f) * pushRatio, 12.0f);
    DrawRoundedCard(g, meterFill, 6.0f, Color(255, 48, 209, 88), Color(0, 0, 0, 0));

    // Emergency Reset Button (Left column bottom)
    RectF resetBtn(physicsCard.X + 24.0f, physicsCard.Y + 450.0f, 180.0f, 44.0f);
    DrawRoundedCard(g, resetBtn, 10.0f, Color(255, 255, 69, 58), Color(60, 255, 255, 255));
    g.DrawString(L"Reset Local Cursor", -1, &fontLabel, PointF(resetBtn.X + resetBtn.Width / 2.0f, resetBtn.Y + resetBtn.Height / 2.0f), &sfCenter, &whiteBrush);

    // Right Column: Preferences & Options (x: 480, w: 470, h: 540)
    RectF prefCard(480.0f, 104.0f, 470.0f, 540.0f);
    DrawRoundedCard(g, prefCard, 14.0f, Color(255, 22, 22, 28), Color(40, 255, 255, 255));

    g.DrawString(L"Universal Control Preferences", -1, &fontHeader, PointF(prefCard.X + 24.0f, prefCard.Y + 20.0f), &whiteBrush);
    g.DrawString(L"Customize peripheral sharing, clipboard bridging, and tray options.", -1, &fontSub, PointF(prefCard.X + 24.0f, prefCard.Y + 38.0f), &subBrush);

    // Option 1: Clipboard
    DrawCupertinoToggle(g, prefCard.X + 24.0f, prefCard.Y + 80.0f, true, "Universal Clipboard", "Automatically synchronizes copied text between PCs");

    // Option 2: High Definition Wheel
    DrawCupertinoToggle(g, prefCard.X + 24.0f, prefCard.Y + 150.0f, true, "High-Definition Scroll Smoothing", "Sub-pixel vertical and horizontal tilt wheel streaming");

    // Option 3: Low-Level Hook Priority
    DrawCupertinoToggle(g, prefCard.X + 24.0f, prefCard.Y + 220.0f, true, "Hardware Input Synthesis", "Direct Win32 SendInput hardware event dispatch");

    // Option 4: Background Tray Mode
    DrawCupertinoToggle(g, prefCard.X + 24.0f, prefCard.Y + 290.0f, true, "Run in System Tray on Close", "Keeps Universal Control active while running in background");

    // Test Edge Bubble Button
    RectF testBubbleBtn(prefCard.X + 24.0f, prefCard.Y + 380.0f, 220.0f, 44.0f);
    DrawRoundedCard(g, testBubbleBtn, 10.0f, Color(255, 175, 82, 222), Color(80, 255, 255, 255));
    g.DrawString(L"Trigger Edge Bubble Demo ✨", -1, &fontLabel, PointF(testBubbleBtn.X + testBubbleBtn.Width / 2.0f, testBubbleBtn.Y + testBubbleBtn.Height / 2.0f), &sfCenter, &whiteBrush);
}

void MacUI::RenderTabTelemetry(Graphics& g, int w, int h) {
    FontFamily ff(L"Segoe UI");
    Font fontHeader(&ff, 13, FontStyleBold, UnitPixel);
    Font fontSub(&ff, 10, FontStyleRegular, UnitPixel);
    Font fontGaugeVal(&ff, 22, FontStyleBold, UnitPixel);
    Font fontGaugeLbl(&ff, 10, FontStyleRegular, UnitPixel);
    Font fontLog(&ff, 10, FontStyleRegular, UnitPixel);

    StringFormat sfCenter;
    sfCenter.SetAlignment(StringAlignmentCenter);
    sfCenter.SetLineAlignment(StringAlignmentCenter);

    SolidBrush whiteBrush(Color(255, 255, 255, 255));
    SolidBrush subBrush(Color(255, 140, 140, 150));

    // Top Row: 4 Telemetry Gauge Cards (y: 104, h: 90)
    float gaugeW = (w - 60.0f - 36.0f) / 4.0f;
    float gaugeY = 104.0f;
    float gaugeH = 90.0f;

    // Gauge 1: Input Latency
    RectF g1(30.0f, gaugeY, gaugeW, gaugeH);
    DrawRoundedCard(g, g1, 12.0f, Color(255, 22, 22, 28), Color(40, 255, 255, 255));
    SolidBrush greenBrush(Color(255, 48, 209, 88));
    g.DrawString(L"< 0.4 ms", -1, &fontGaugeVal, PointF(g1.X + g1.Width / 2.0f, g1.Y + 34.0f), &sfCenter, &greenBrush);
    g.DrawString(L"INPUT STREAM LATENCY", -1, &fontGaugeLbl, PointF(g1.X + g1.Width / 2.0f, g1.Y + 68.0f), &sfCenter, &subBrush);

    // Gauge 2: Packets / Sec
    RectF g2(30.0f + gaugeW + 12.0f, gaugeY, gaugeW, gaugeH);
    DrawRoundedCard(g, g2, 12.0f, Color(255, 22, 22, 28), Color(40, 255, 255, 255));
    SolidBrush blueBrush(Color(255, 10, 132, 255));
    std::wstring ppsStr = std::to_wstring(static_cast<int>(m_packetsPerSec)) + L" pkt/s";
    g.DrawString(ppsStr.c_str(), -1, &fontGaugeVal, PointF(g2.X + g2.Width / 2.0f, g2.Y + 34.0f), &sfCenter, &blueBrush);
    g.DrawString(L"UDP TRANSMISSION RATE", -1, &fontGaugeLbl, PointF(g2.X + g2.Width / 2.0f, g2.Y + 68.0f), &sfCenter, &subBrush);

    // Gauge 3: Control State
    RectF g3(30.0f + (gaugeW + 12.0f) * 2.0f, gaugeY, gaugeW, gaugeH);
    DrawRoundedCard(g, g3, 12.0f, Color(255, 22, 22, 28), Color(40, 255, 255, 255));
    SolidBrush purpleBrush(Color(255, 175, 82, 222));
    const wchar_t* stateStr = L"LOCAL";
    if (m_physics) {
        if (m_physics->IsRemote()) stateStr = L"REMOTE";
        else if (m_physics->IsPushing()) stateStr = L"PUSHING";
    }
    g.DrawString(stateStr, -1, &fontGaugeVal, PointF(g3.X + g3.Width / 2.0f, g3.Y + 34.0f), &sfCenter, &purpleBrush);
    g.DrawString(L"ACTIVE CONTROL FOCUS", -1, &fontGaugeLbl, PointF(g3.X + g3.Width / 2.0f, g3.Y + 68.0f), &sfCenter, &subBrush);

    // Gauge 4: CPU Usage
    RectF g4(30.0f + (gaugeW + 12.0f) * 3.0f, gaugeY, gaugeW, gaugeH);
    DrawRoundedCard(g, g4, 12.0f, Color(255, 22, 22, 28), Color(40, 255, 255, 255));
    g.DrawString(L"< 0.1 %", -1, &fontGaugeVal, PointF(g4.X + g4.Width / 2.0f, g4.Y + 34.0f), &sfCenter, &whiteBrush);
    g.DrawString(L"SYSTEM OVERHEAD", -1, &fontGaugeLbl, PointF(g4.X + g4.Width / 2.0f, g4.Y + 68.0f), &sfCenter, &subBrush);

    // Bottom Area: Terminal Event Log (y: 206, h: 438)
    RectF termCard(30.0f, 206.0f, static_cast<float>(w - 60), 438.0f);
    DrawRoundedCard(g, termCard, 14.0f, Color(255, 14, 14, 18), Color(40, 255, 255, 255));

    g.DrawString(L"Real-Time Kernel & Protocol Stream", -1, &fontHeader, PointF(termCard.X + 24.0f, termCard.Y + 18.0f), &whiteBrush);
    g.DrawString(L"Timestamped system logs of discovery beacons, edge collisions, and input synthesis.", -1, &fontSub, PointF(termCard.X + 24.0f, termCard.Y + 36.0f), &subBrush);

    // Terminal Line Container
    float lineY = termCard.Y + 65.0f;
    std::lock_guard<std::mutex> lock(m_logMutex);

    int maxLines = 18;
    int startIdx = std::max(0, static_cast<int>(m_logs.size()) - maxLines);

    for (size_t i = startIdx; i < m_logs.size(); ++i) {
        const auto& entry = m_logs[i];

        // Timestamp
        SolidBrush timeBrush(Color(255, 90, 90, 105));
        std::wstring wTime = L"[" + ToWStr(entry.timestamp) + L"] ";
        g.DrawString(wTime.c_str(), -1, &fontLog, PointF(termCard.X + 24.0f, lineY), &timeBrush);

        // Tag
        SolidBrush tagBrush(entry.color);
        std::wstring wTag = L"[" + ToWStr(entry.tag) + L"] ";
        g.DrawString(wTag.c_str(), -1, &fontLog, PointF(termCard.X + 90.0f, lineY), &tagBrush);

        // Text
        SolidBrush textBrush(Color(255, 220, 220, 230));
        std::wstring wText = ToWStr(entry.text);
        g.DrawString(wText.c_str(), -1, &fontLog, PointF(termCard.X + 195.0f, lineY), &textBrush);

        lineY += 19.0f;
    }
}
