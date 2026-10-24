#ifndef BUTTERFLOW_MAC_UI_H
#define BUTTERFLOW_MAC_UI_H

#include <winsock2.h>
#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <chrono>
#include <mutex>
#include "protocol.h"
#include "edge_physics.h"
#include "edge_overlay.h"
#include "input_injector.h"
#include "network_manager.h"

// Custom window messages
constexpr UINT WM_TRAYICON = WM_USER + 101;

struct LogEntry {
    std::string timestamp;
    std::string tag;
    std::string text;
    Gdiplus::Color color;
};

class MacUI {
public:
    MacUI();
    ~MacUI();

    bool Initialize(HINSTANCE hInstance, 
                    EdgePhysics* physics, 
                    EdgeOverlay* overlay, 
                    InputInjector* injector, 
                    NetworkManager* net,
                    ScreenEdge initialEdge);

    void Show();
    void Hide();
    void MinimizeToTray();
    void RestoreFromTray();
    void RequestExit();

    bool IsRunning() const { return m_isRunning; }
    HWND GetHWND() const { return m_hWnd; }

    void Log(const std::string& tag, const std::string& text, Gdiplus::Color color = Gdiplus::Color(255, 200, 200, 200));
    void OnTimerTick();

    void SetConfiguredEdge(ScreenEdge edge);
    ScreenEdge GetConfiguredEdge() const { return m_configuredEdge; }

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    void Render(HDC hdc, int width, int height);

    // Render Subcomponents
    void RenderTopBar(Gdiplus::Graphics& g, int w, int h);
    void RenderTabBar(Gdiplus::Graphics& g, int w, int h);
    void RenderTabArrangement(Gdiplus::Graphics& g, int w, int h);
    void RenderTabNetwork(Gdiplus::Graphics& g, int w, int h);
    void RenderTabPhysics(Gdiplus::Graphics& g, int w, int h);
    void RenderTabTelemetry(Gdiplus::Graphics& g, int w, int h);

    // Tray management
    void SetupTrayIcon();
    void RemoveTrayIcon();
    void ShowTrayMenu();

    // Mouse Interaction
    void OnMouseMove(int x, int y);
    void OnMouseDown(int x, int y);
    void OnMouseUp(int x, int y);
    void OnCharInput(char ch);
    void OnKeyDown(WPARAM vk);

    // Helpers
    void DrawMacWindowControls(Gdiplus::Graphics& g, int x, int y);
    void DrawRoundedCard(Gdiplus::Graphics& g, const Gdiplus::RectF& rect, float radius, Gdiplus::Color bg, Gdiplus::Color border);
    void DrawCupertinoToggle(Gdiplus::Graphics& g, float x, float y, bool active, const std::string& label, const std::string& sub);
    void DrawCupertinoSlider(Gdiplus::Graphics& g, float x, float y, float w, float value, float minVal, float maxVal, const std::string& label, const std::string& unit);
    void DrawMonitorGraphic(Gdiplus::Graphics& g, float x, float y, float w, float h, const std::string& title, const std::string& sub, const std::string& badge, bool isHost, bool isConnected);

    HWND m_hWnd;
    HINSTANCE m_hInstance;
    ULONG_PTR m_gdiplusToken;
    bool m_isRunning;
    bool m_inTray;

    // Subsystems
    EdgePhysics* m_physics;
    EdgeOverlay* m_overlay;
    InputInjector* m_injector;
    NetworkManager* m_net;

    // State
    ScreenEdge m_configuredEdge;
    int m_activeTab; // 0: Arrangement, 1: Network, 2: Physics, 3: Telemetry
    int m_windowWidth;
    int m_windowHeight;
    int m_screenWidth;
    int m_screenHeight;
    std::string m_localHostName;
    std::string m_localIP;

    // UI Interactive States
    int m_hoveredTrafficLight; // 0: none, 1: red, 2: yellow, 3: green
    int m_hoveredTab;
    int m_hoveredEdgeBtn;
    bool m_pushThroughEnabled;
    bool m_edgeBubbleEnabled;
    float m_springTension; // 15.0 to 65.0
    bool m_isDraggingMonitor;
    bool m_isDraggingSlider;
    Gdiplus::PointF m_dragOffset;

    // Direct IP text box state
    std::string m_directIpInput;
    bool m_directIpFocused;

    // Animations & Telemetry
    float m_radarAngle;
    float m_bridgePulseOffset;
    float m_statusGlowAlpha;
    DWORD m_lastTickTime;
    uint64_t m_lastSentCount;
    uint64_t m_lastRecvCount;
    float m_packetsPerSec;

    // Log terminal
    std::vector<LogEntry> m_logs;
    std::mutex m_logMutex;
    int m_logScrollOffset;
};

#endif // BUTTERFLOW_MAC_UI_H
