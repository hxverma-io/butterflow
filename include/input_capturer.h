#ifndef BUTTERFLOW_INPUT_CAPTURER_H
#define BUTTERFLOW_INPUT_CAPTURER_H

#include <windows.h>
#include <functional>
#include "protocol.h"
#include "edge_physics.h"

class InputCapturer {
public:
    using MouseEventCallback = std::function<void(const MouseInputPayload& payload)>;
    using KeyEventCallback = std::function<void(const KeyboardInputPayload& payload)>;
    using EscapeCallback = std::function<void()>;

    static InputCapturer& Instance();

    bool InstallHooks(EdgePhysics* physics);
    void UninstallHooks();

    void SetCallbacks(MouseEventCallback onMouse, KeyEventCallback onKey, EscapeCallback onEscape);

    void LockCursorAtEdge(ScreenEdge edge, int screenW, int screenH);
    void UnlockCursor();

    // Internal hook and Raw Input callbacks
    LRESULT HandleMouseHook(int nCode, WPARAM wParam, LPARAM lParam);
    LRESULT HandleKeyboardHook(int nCode, WPARAM wParam, LPARAM lParam);
    void HandleRawInput(LPARAM lParam);

private:
    InputCapturer();
    ~InputCapturer();

    HHOOK m_hMouseHook;
    HHOOK m_hKeyHook;
    HWND m_hwndSink;
    EdgePhysics* m_physics;

    POINT m_lastPos;
    bool m_isLocked;
    int m_screenW;
    int m_screenH;
    ScreenEdge m_lockedEdge;

    MouseEventCallback m_onMouse;
    KeyEventCallback m_onKey;
    EscapeCallback m_onEscape;

    static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK RawInputWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

#endif // BUTTERFLOW_INPUT_CAPTURER_H
