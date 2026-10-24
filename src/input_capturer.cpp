#include "../include/input_capturer.h"

InputCapturer& InputCapturer::Instance() {
    static InputCapturer instance;
    return instance;
}

InputCapturer::InputCapturer()
    : m_hMouseHook(nullptr),
      m_hKeyHook(nullptr),
      m_physics(nullptr),
      m_lastPos({ 0, 0 }),
      m_isLocked(false) {
}

InputCapturer::~InputCapturer() {
    UninstallHooks();
}

bool InputCapturer::InstallHooks(EdgePhysics* physics) {
    m_physics = physics;
    GetCursorPos(&m_lastPos);

    m_hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, GetModuleHandle(nullptr), 0);
    m_hKeyHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(nullptr), 0);

    return (m_hMouseHook != nullptr && m_hKeyHook != nullptr);
}

void InputCapturer::UninstallHooks() {
    UnlockCursor();
    if (m_hMouseHook) {
        UnhookWindowsHookEx(m_hMouseHook);
        m_hMouseHook = nullptr;
    }
    if (m_hKeyHook) {
        UnhookWindowsHookEx(m_hKeyHook);
        m_hKeyHook = nullptr;
    }
}

void InputCapturer::SetCallbacks(MouseEventCallback onMouse, KeyEventCallback onKey, EscapeCallback onEscape) {
    m_onMouse = onMouse;
    m_onKey = onKey;
    m_onEscape = onEscape;
}

void InputCapturer::LockCursorAtEdge(ScreenEdge edge, int screenW, int screenH) {
    m_isLocked = true;
    RECT lockRect;
    if (edge == EDGE_RIGHT) {
        lockRect = { screenW - 2, 0, screenW - 1, screenH };
    } else if (edge == EDGE_LEFT) {
        lockRect = { 0, 0, 1, screenH };
    } else {
        lockRect = { 0, 0, screenW, screenH };
    }
    ClipCursor(&lockRect);
}

void InputCapturer::UnlockCursor() {
    m_isLocked = false;
    ClipCursor(nullptr);
}

LRESULT CALLBACK InputCapturer::LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        return InputCapturer::Instance().HandleMouseHook(nCode, wParam, lParam);
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK InputCapturer::LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        return InputCapturer::Instance().HandleKeyboardHook(nCode, wParam, lParam);
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT InputCapturer::HandleMouseHook(int nCode, WPARAM wParam, LPARAM lParam) {
    MSLLHOOKSTRUCT* pMouse = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);

    int dx = pMouse->pt.x - m_lastPos.x;
    int dy = pMouse->pt.y - m_lastPos.y;
    m_lastPos = pMouse->pt;

    bool isRemote = (m_physics && m_physics->GetState() == ControlState::REMOTE_ACTIVE);

    if (wParam == WM_MOUSEMOVE) {
        if (m_physics) {
            bool suppress = m_physics->ProcessLocalMouseMove(pMouse->pt.x, pMouse->pt.y, dx, dy);
            if (suppress && m_onMouse) {
                MouseInputPayload payload = {};
                payload.flags = MFLAG_MOVE;
                payload.dx = dx;
                payload.dy = dy;
                m_onMouse(payload);
                return 1; // Suppress local cursor
            }
        }
    }

    if (isRemote) {
        MouseInputPayload payload = {};
        payload.dx = dx;
        payload.dy = dy;

        switch (wParam) {
            case WM_LBUTTONDOWN: payload.flags = MFLAG_LEFT_DOWN; break;
            case WM_LBUTTONUP:   payload.flags = MFLAG_LEFT_UP;   break;
            case WM_RBUTTONDOWN: payload.flags = MFLAG_RIGHT_DOWN; break;
            case WM_RBUTTONUP:   payload.flags = MFLAG_RIGHT_UP;   break;
            case WM_MBUTTONDOWN: payload.flags = MFLAG_MID_DOWN;   break;
            case WM_MBUTTONUP:   payload.flags = MFLAG_MID_UP;     break;
            case WM_MOUSEWHEEL:
                payload.flags = MFLAG_WHEEL;
                payload.wheelDelta = static_cast<int16_t>(GET_WHEEL_DELTA_WPARAM(pMouse->mouseData));
                break;
            case WM_MOUSEHWHEEL:
                payload.flags = MFLAG_HWHEEL;
                payload.hWheelDelta = static_cast<int16_t>(GET_WHEEL_DELTA_WPARAM(pMouse->mouseData));
                break;
            case WM_XBUTTONDOWN:
                payload.flags = (HIWORD(pMouse->mouseData) == XBUTTON1) ? MFLAG_X1_DOWN : MFLAG_X2_DOWN;
                break;
            case WM_XBUTTONUP:
                payload.flags = (HIWORD(pMouse->mouseData) == XBUTTON1) ? MFLAG_X1_UP : MFLAG_X2_UP;
                break;
            default:
                break;
        }

        if (m_onMouse && payload.flags != 0) {
            m_onMouse(payload);
        }

        return 1; // Suppress click on host PC while controlling remote PC
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT InputCapturer::HandleKeyboardHook(int nCode, WPARAM wParam, LPARAM lParam) {
    KBDLLHOOKSTRUCT* pKey = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

    // Emergency Breakout Key: Ctrl + Alt + Shift + Esc
    bool isCtrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    bool isAlt  = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    bool isShift= (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

    if (pKey->vkCode == VK_ESCAPE && isCtrl && isAlt && isShift) {
        if (m_onEscape) {
            m_onEscape();
        }
        return 1;
    }

    bool isRemote = (m_physics && m_physics->GetState() == ControlState::REMOTE_ACTIVE);
    if (isRemote) {
        if (m_onKey) {
            KeyboardInputPayload payload = {};
            payload.vkCode = static_cast<uint16_t>(pKey->vkCode);
            payload.scanCode = static_cast<uint16_t>(pKey->scanCode);
            payload.flags = 0;

            if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
                payload.flags |= KEYEVENTF_KEYUP;
            }
            if (pKey->flags & LLKHF_EXTENDED) {
                payload.flags |= KEYEVENTF_EXTENDEDKEY;
            }

            m_onKey(payload);
        }
        return 1; // Suppress local keystrokes while typing on remote PC
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
