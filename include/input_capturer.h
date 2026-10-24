#ifndef BUTTERFLOW_INPUT_CAPTURER_H
#define BUTTERFLOW_INPUT_CAPTURER_H

#include <windows.h>
#include <functional>
#include <chrono>
#include "protocol.h"
#include "edge_physics.h"

class InputCapturer {
public:
    using MouseEventCallback = std::function<void(const MouseInputPayload& payload)>;
    using KeyEventCallback = std::function<void(const KeyboardInputPayload& payload)>;
    using EscapeCallback = std::function<void()>;
    using InterventionCallback = std::function<void()>;

    static InputCapturer& Instance();

    bool InstallHooks(EdgePhysics* physics);
    void UninstallHooks();

    void SetCallbacks(MouseEventCallback onMouse, KeyEventCallback onKey, EscapeCallback onEscape);
    void SetInterventionCallback(InterventionCallback onIntervention);

    void StartRemoteCapture(ScreenEdge targetEdge, float normY, int remoteW = 1920, int remoteH = 1080);
    void StopRemoteCapture();

    // Internal hook callbacks
    LRESULT HandleMouseHook(int nCode, WPARAM wParam, LPARAM lParam);
    LRESULT HandleKeyboardHook(int nCode, WPARAM wParam, LPARAM lParam);

private:
    InputCapturer();
    ~InputCapturer();

    // Apple macOS signature ballistics model
    void ApplyMacBallistics(double rawDx, double rawDy, double dtMs, double& outDx, double& outDy);

    HHOOK m_hMouseHook;
    HHOOK m_hKeyHook;
    EdgePhysics* m_physics;

    POINT m_lastPos;
    POINT m_lastRemoteRawPos;
    POINT m_anchorPos;
    bool m_isRemoteCapturing;

    double m_remoteCurX;
    double m_remoteCurY;
    int m_remoteWidth;
    int m_remoteHeight;
    int m_screenWidth;
    int m_screenHeight;

    std::chrono::steady_clock::time_point m_lastMoveTime;

    MouseEventCallback m_onMouse;
    KeyEventCallback m_onKey;
    EscapeCallback m_onEscape;
    InterventionCallback m_onIntervention;
    int m_companionPhysicalDelta;

    static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
};

#endif // BUTTERFLOW_INPUT_CAPTURER_H
