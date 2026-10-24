#include "../include/input_capturer.h"
#include <algorithm>
#include <cmath>

InputCapturer& InputCapturer::Instance() {
    static InputCapturer instance;
    return instance;
}

InputCapturer::InputCapturer()
    : m_hRawInputWnd(nullptr),
      m_hMouseHook(nullptr),
      m_hKeyHook(nullptr),
      m_physics(nullptr),
      m_lastPos({ 0, 0 }),
      m_lastRemoteRawPos({ 960, 540 }),
      m_anchorPos({ 960, 540 }),
      m_isRemoteCapturing(false),
      m_remoteCurX(960.0),
      m_remoteCurY(540.0),
      m_remoteWidth(1920),
      m_remoteHeight(1080),
      m_screenWidth(1920),
      m_screenHeight(1080),
      m_lastMoveTime(std::chrono::steady_clock::now()),
      m_companionPhysicalDelta(0),
      m_speedMultiplier(2.2f),
      m_subPixelDx(0.0),
      m_subPixelDy(0.0) {
}

InputCapturer::~InputCapturer() {
    UninstallHooks();
}

void InputCapturer::ApplyMacBallistics(double rawDx, double rawDy, double dtMs, double& outDx, double& outDy) {
    double dist = std::sqrt(rawDx * rawDx + rawDy * rawDy);
    if (dist < 0.0001) {
        outDx = 0.0;
        outDy = 0.0;
        return;
    }

    double dt = std::clamp(dtMs, 0.5, 64.0);
    double v = dist / dt;

    double gain = 1.0;
    if (v <= 0.18) {
        gain = 0.88;
    } else if (v <= 1.25) {
        double t = (v - 0.18) / (1.25 - 0.18);
        gain = 0.88 + 0.92 * std::pow(t, 1.35);
    } else {
        double excess = (v - 1.25) / 1.5;
        gain = 1.80 + 0.85 * std::tanh(excess);
    }

    outDx = rawDx * gain;
    outDy = rawDy * gain;
}

bool InputCapturer::InstallHooks(EdgePhysics* physics) {
    m_physics = physics;
    m_screenWidth = GetSystemMetrics(SM_CXSCREEN);
    m_screenHeight = GetSystemMetrics(SM_CYSCREEN);
    GetCursorPos(&m_lastPos);
    m_anchorPos = { m_screenWidth / 2, m_screenHeight / 2 };
    m_lastRemoteRawPos = m_anchorPos;

    // 1. Register Hidden Window for Raw Input Sink
    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = RawInputWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "ButterflowRawInputSinkClass";
    RegisterClassExA(&wc);

    m_hRawInputWnd = CreateWindowExA(0, "ButterflowRawInputSinkClass", "ButterflowRawInputSink",
                                     WS_POPUP, 0, 0, 0, 0,
                                     nullptr, nullptr, GetModuleHandle(nullptr), nullptr);

    // 2. Register Raw Input Device for Mouse (hardware deltas regardless of cursor position/screen edges)
    RAWINPUTDEVICE rid = {};
    rid.usUsagePage = 0x01; // HID Generic Desktop
    rid.usUsage = 0x02;     // HID Mouse
    rid.dwFlags = RIDEV_INPUTSINK;
    rid.hwndTarget = m_hRawInputWnd;
    RegisterRawInputDevices(&rid, 1, sizeof(rid));

    // 3. Install Low-Level Hooks for button/keyboard capture and suppression
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
    if (m_hRawInputWnd) {
        DestroyWindow(m_hRawInputWnd);
        m_hRawInputWnd = nullptr;
    }
    UnregisterClassA("ButterflowRawInputSinkClass", GetModuleHandle(nullptr));
}

void InputCapturer::SetCallbacks(MouseEventCallback onMouse, KeyEventCallback onKey, EscapeCallback onEscape) {
    m_onMouse = onMouse;
    m_onKey = onKey;
    m_onEscape = onEscape;
}

void InputCapturer::SetInterventionCallback(InterventionCallback onIntervention) {
    m_onIntervention = onIntervention;
}

void InputCapturer::SyncCursorPos(int x, int y) {
    m_lastPos = { x, y };
    m_lastRemoteRawPos = { x, y };
}

void InputCapturer::StartRemoteCapture(ScreenEdge targetEdge, float normY, int remoteW, int remoteH) {
    m_isRemoteCapturing = true;
    m_remoteWidth = remoteW > 0 ? remoteW : 1920;
    m_remoteHeight = remoteH > 0 ? remoteH : 1080;

    GetCursorPos(&m_lastPos);
    m_lastRemoteRawPos = m_lastPos;
    m_lastMoveTime = std::chrono::steady_clock::now();
    m_subPixelDx = 0.0;
    m_subPixelDy = 0.0;
}

void InputCapturer::StopRemoteCapture() {
    m_isRemoteCapturing = false;
    GetCursorPos(&m_lastPos);
    m_lastRemoteRawPos = m_lastPos;
    m_subPixelDx = 0.0;
    m_subPixelDy = 0.0;
}

LRESULT CALLBACK InputCapturer::RawInputWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_INPUT) {
        InputCapturer::Instance().HandleRawInputMessage(lParam);
        return DefWindowProcA(hwnd, uMsg, wParam, lParam);
    }
    return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}

void InputCapturer::HandleRawInputMessage(LPARAM lParam) {
    UINT dwSize = 0;
    GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &dwSize, sizeof(RAWINPUTHEADER));
    if (dwSize == 0) return;

    std::vector<BYTE> rawBuffer(dwSize);
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, rawBuffer.data(), &dwSize, sizeof(RAWINPUTHEADER)) != dwSize) {
        return;
    }

    RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(rawBuffer.data());
    if (raw->header.dwType != RIM_TYPEMOUSE) return;

    // Ignore synthetic events injected by Butterflow
    if (raw->data.mouse.ulExtraInformation == BUTTERFLOW_EXTRA_INFO_MAGIC) {
        return;
    }

    // 1. Companion Receiver Check: Physical movement on companion yields control
    if (m_physics && m_physics->IsCompanionReceiving()) {
        if (raw->header.hDevice != nullptr) {
            int delta = std::abs(raw->data.mouse.lLastX) + std::abs(raw->data.mouse.lLastY);
            m_companionPhysicalDelta += delta;
            if (m_companionPhysicalDelta > 120) { // Require >120 counts of deliberate physical movement
                m_companionPhysicalDelta = 0;
                if (m_onIntervention) {
                    m_onIntervention();
                }
            }
        }
        return;
    }

    // 2. Remote Host Mode: Stream pure physical relative deltas to Companion PC
    bool isRemote = m_isRemoteCapturing || (m_physics && m_physics->GetState() == ControlState::REMOTE_ACTIVE);
    if (!isRemote) return;

    if (!(raw->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
        int rawDx = raw->data.mouse.lLastX;
        int rawDy = raw->data.mouse.lLastY;

        if (rawDx != 0 || rawDy != 0) {
            double dist = std::sqrt(static_cast<double>(rawDx * rawDx + rawDy * rawDy));
            // Authentic macOS Pointer Ballistics scaled by user-configured speed multiplier:
            // Slow precision: 1.0x * m_speedMultiplier (pixel targeting)
            // Medium/Fast flick: smoothly ramps up to 1.8x * m_speedMultiplier
            double gain = 1.0 + 0.8 * std::tanh(dist / 6.0);
            double effectiveMult = static_cast<double>(m_speedMultiplier) * gain;

            double targetDx = (rawDx * effectiveMult) + m_subPixelDx;
            double targetDy = (rawDy * effectiveMult) + m_subPixelDy;

            int appliedDx = static_cast<int>(std::round(targetDx));
            int appliedDy = static_cast<int>(std::round(targetDy));

            m_subPixelDx = targetDx - appliedDx;
            m_subPixelDy = targetDy - appliedDy;

            if (m_onMouse && (appliedDx != 0 || appliedDy != 0)) {
                MouseInputPayload payload = {};
                payload.flags = MFLAG_MOVE;
                payload.dx = static_cast<int16_t>(appliedDx);
                payload.dy = static_cast<int16_t>(appliedDy);
                m_onMouse(payload);
            }
        }
    }
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

    // CRITICAL: Always keep position synchronized on synthetic / injected inputs
    if ((pMouse->flags & LLMHF_INJECTED) || (pMouse->dwExtraInfo == BUTTERFLOW_EXTRA_INFO_MAGIC)) {
        m_lastPos = pMouse->pt;
        m_lastRemoteRawPos = pMouse->pt;
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    // 1. Companion Receiver Check: Physical mouse clicks immediately yield remote control
    if (m_physics && m_physics->IsCompanionReceiving()) {
        if (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN) {
            m_companionPhysicalDelta = 0;
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
            // Local cursor suppressed and locked in place! (Raw Input handles delta transmission)
            return 1;
        }

        // Capture button and wheel actions
        MouseInputPayload payload = {};

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
