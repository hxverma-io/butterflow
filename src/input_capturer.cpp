#include "../include/input_capturer.h"
#include <algorithm>

InputCapturer& InputCapturer::Instance() {
    static InputCapturer instance;
    return instance;
}

InputCapturer::InputCapturer()
    : m_hMouseHook(nullptr),
      m_hKeyHook(nullptr),
      m_physics(nullptr),
      m_lastPos({ 0, 0 }),
      m_anchorPos({ 960, 540 }),
      m_isRemoteCapturing(false),
      m_remoteCurX(960.0f),
      m_remoteCurY(540.0f),
      m_remoteWidth(1920),
      m_remoteHeight(1080),
      m_screenWidth(1920),
      m_screenHeight(1080) {
}

InputCapturer::~InputCapturer() {
    UninstallHooks();
}

bool InputCapturer::InstallHooks(EdgePhysics* physics) {
    m_physics = physics;
    m_screenWidth = GetSystemMetrics(SM_CXSCREEN);
    m_screenHeight = GetSystemMetrics(SM_CYSCREEN);
    GetCursorPos(&m_lastPos);
    m_anchorPos = { m_screenWidth / 2, m_screenHeight / 2 };

    m_hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, GetModuleHandle(nullptr), 0);
    m_hKeyHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(nullptr), 0);

    return (m_hMouseHook != nullptr && m_hKeyHook != nullptr);
}

void InputCapturer::UninstallHooks() {
    StopRemoteCapture();
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

void InputCapturer::SetInterventionCallback(InterventionCallback onIntervention) {
    m_onIntervention = onIntervention;
}

void InputCapturer::StartRemoteCapture(ScreenEdge targetEdge, float normY, int remoteW, int remoteH) {
    m_isRemoteCapturing = true;
    m_remoteWidth = remoteW > 0 ? remoteW : 1920;
    m_remoteHeight = remoteH > 0 ? remoteH : 1080;

    // Initialize remote cursor position on entry side
    if (targetEdge == EDGE_LEFT) {
        m_remoteCurX = 40.0f; // Safe 40px entry margin inside remote display
        m_remoteCurY = std::clamp(normY, 0.05f, 0.95f) * m_remoteHeight;
    } else if (targetEdge == EDGE_RIGHT) {
        m_remoteCurX = m_remoteWidth - 40.0f;
        m_remoteCurY = std::clamp(normY, 0.05f, 0.95f) * m_remoteHeight;
    } else if (targetEdge == EDGE_TOP) {
        m_remoteCurX = std::clamp(normY, 0.05f, 0.95f) * m_remoteWidth;
        m_remoteCurY = 40.0f;
    } else { // EDGE_BOTTOM
        m_remoteCurX = std::clamp(normY, 0.05f, 0.95f) * m_remoteWidth;
        m_remoteCurY = m_remoteHeight - 40.0f;
    }

    // Set anchor position to screen center
    m_anchorPos = { m_screenWidth / 2, m_screenHeight / 2 };
    SetCursorPos(m_anchorPos.x, m_anchorPos.y);
    m_lastPos = m_anchorPos;
}

void InputCapturer::StopRemoteCapture() {
    m_isRemoteCapturing = false;
    GetCursorPos(&m_lastPos);
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

    // CRITICAL: Ignore any synthetic inputs injected by Butterflow or software
    if ((pMouse->flags & LLMHF_INJECTED) || (pMouse->dwExtraInfo == BUTTERFLOW_EXTRA_INFO_MAGIC)) {
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    // 1. Companion Receiver Check: If local user touches physical mouse on Companion PC, yield remote control!
    if (m_physics && m_physics->IsCompanionReceiving()) {
        if (wParam == WM_MOUSEMOVE || wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN) {
            if (m_onIntervention) {
                m_onIntervention();
            }
        }
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    // 2. Remote Host Mode: User is controlling the Companion PC
    bool isRemote = m_isRemoteCapturing || (m_physics && m_physics->GetState() == ControlState::REMOTE_ACTIVE);

    if (isRemote) {
        if (wParam == WM_MOUSEMOVE) {
            int dx = pMouse->pt.x - m_anchorPos.x;
            int dy = pMouse->pt.y - m_anchorPos.y;

            if (dx != 0 || dy != 0) {
                m_remoteCurX = std::clamp(m_remoteCurX + dx, 0.0f, static_cast<float>(m_remoteWidth));
                m_remoteCurY = std::clamp(m_remoteCurY + dy, 0.0f, static_cast<float>(m_remoteHeight));

                uint16_t normX = static_cast<uint16_t>((m_remoteCurX / m_remoteWidth) * 65535.0f);
                uint16_t normY = static_cast<uint16_t>((m_remoteCurY / m_remoteHeight) * 65535.0f);

                if (m_onMouse) {
                    MouseInputPayload payload = {};
                    payload.flags = MFLAG_MOVE;
                    payload.dx = dx;
                    payload.dy = dy;
                    payload.normX = normX;
                    payload.normY = normY;
                    m_onMouse(payload);
                }

                // Re-anchor cursor at center with BUTTERFLOW_EXTRA_INFO_MAGIC tag so hook ignores it
                INPUT in = {};
                in.type = INPUT_MOUSE;
                in.mi.dx = (m_anchorPos.x * 65536) / m_screenWidth;
                in.mi.dy = (m_anchorPos.y * 65536) / m_screenHeight;
                in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
                in.mi.dwExtraInfo = BUTTERFLOW_EXTRA_INFO_MAGIC;
                SendInput(1, &in, sizeof(INPUT));
            }
            return 1; // Suppress local cursor
        }

        // Capture button and wheel actions
        MouseInputPayload payload = {};
        uint16_t normX = static_cast<uint16_t>((m_remoteCurX / m_remoteWidth) * 65535.0f);
        uint16_t normY = static_cast<uint16_t>((m_remoteCurY / m_remoteHeight) * 65535.0f);
        payload.normX = normX;
        payload.normY = normY;

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

        return 1; // Suppress clicks locally while remote is active
    }

    // 3. Local Mode: Track mouse coordinates & process boundary physics
    int dx = pMouse->pt.x - m_lastPos.x;
    int dy = pMouse->pt.y - m_lastPos.y;
    m_lastPos = pMouse->pt;

    if (wParam == WM_MOUSEMOVE && m_physics) {
        bool suppress = m_physics->ProcessLocalMouseMove(pMouse->pt.x, pMouse->pt.y, dx, dy);
        if (suppress) {
            return 1;
        }
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT InputCapturer::HandleKeyboardHook(int nCode, WPARAM wParam, LPARAM lParam) {
    KBDLLHOOKSTRUCT* pKey = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

    // CRITICAL: Ignore synthetic keyboard inputs!
    if ((pKey->flags & LLKHF_INJECTED) || (pKey->dwExtraInfo == BUTTERFLOW_EXTRA_INFO_MAGIC)) {
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    // Emergency Breakout Key: Ctrl + Alt + Shift + Esc
    bool isCtrl  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    bool isAlt   = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    bool isShift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

    if (pKey->vkCode == VK_ESCAPE && isCtrl && isAlt && isShift) {
        if (m_onEscape) {
            m_onEscape();
        }
        return 1;
    }

    // If Companion PC receiving: physical keystroke yields remote control to local user
    if (m_physics && m_physics->IsCompanionReceiving()) {
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            if (m_onIntervention) {
                m_onIntervention();
            }
        }
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    bool isRemote = m_isRemoteCapturing || (m_physics && m_physics->GetState() == ControlState::REMOTE_ACTIVE);
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
