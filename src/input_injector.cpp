#include "../include/input_injector.h"
#include <algorithm>
#include <cmath>

InputInjector::InputInjector()
    : m_screenWidth(1920),
      m_screenHeight(1080),
      m_entryEdge(EDGE_LEFT),
      m_isActive(false),
      m_accumulatedReturnPush(0.0f),
      m_entryTime(0) {
}

InputInjector::~InputInjector() {
}

void InputInjector::Initialize(int screenW, int screenH, ScreenEdge entryEdge) {
    m_screenWidth = screenW > 0 ? screenW : 1920;
    m_screenHeight = screenH > 0 ? screenH : 1080;
    m_entryEdge = entryEdge;
    m_accumulatedReturnPush = 0.0f;
    m_entryTime = 0;
}

void InputInjector::SetReturnCallback(ReturnBorderCallback onReturn) {
    m_onReturn = onReturn;
}

void InputInjector::PlaceCursorAtEdge(ScreenEdge edge, float normY) {
    m_entryEdge = edge;
    m_isActive = true;
    m_accumulatedReturnPush = 0.0f;
    m_entryTime = GetTickCount();

    int targetX = m_screenWidth / 2;
    int targetY = static_cast<int>(std::clamp(normY, 0.05f, 0.95f) * m_screenHeight);

    // Place cursor with 15px margin inside the screen
    if (edge == EDGE_LEFT) {
        targetX = 15;
    } else if (edge == EDGE_RIGHT) {
        targetX = m_screenWidth - 15;
    } else if (edge == EDGE_TOP) {
        targetX = static_cast<int>(std::clamp(normY, 0.05f, 0.95f) * m_screenWidth);
        targetY = 15;
    } else if (edge == EDGE_BOTTOM) {
        targetX = static_cast<int>(std::clamp(normY, 0.05f, 0.95f) * m_screenWidth);
        targetY = m_screenHeight - 15;
    }

    INPUT in = {};
    in.type = INPUT_MOUSE;
    in.mi.dx = (targetX * 65536) / m_screenWidth;
    in.mi.dy = (targetY * 65536) / m_screenHeight;
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    in.mi.dwExtraInfo = BUTTERFLOW_EXTRA_INFO_MAGIC;
    SendInput(1, &in, sizeof(INPUT));
}

void InputInjector::InjectMouse(const MouseInputPayload& payload) {
    if (!m_isActive) return;

    // 1. Check for return to host when cursor pushes back against the entry edge
    POINT cur;
    GetCursorPos(&cur);

    bool returning = false;
    DWORD now = GetTickCount();
    if (now - m_entryTime >= 350) { // 350ms cooldown before return is permitted
        if (m_entryEdge == EDGE_LEFT && cur.x <= 1 && payload.dx < 0) {
            returning = true;
        } else if (m_entryEdge == EDGE_RIGHT && cur.x >= m_screenWidth - 2 && payload.dx > 0) {
            returning = true;
        } else if (m_entryEdge == EDGE_TOP && cur.y <= 1 && payload.dy < 0) {
            returning = true;
        } else if (m_entryEdge == EDGE_BOTTOM && cur.y >= m_screenHeight - 2 && payload.dy > 0) {
            returning = true;
        }
    }

    if (returning) {
        m_isActive = false;
        if (m_onReturn) {
            float normCoord = (m_entryEdge == EDGE_TOP || m_entryEdge == EDGE_BOTTOM)
                              ? (static_cast<float>(cur.x) / static_cast<float>(m_screenWidth))
                              : (static_cast<float>(cur.y) / static_cast<float>(m_screenHeight));
            m_onReturn(normCoord);
        }
        return;
    }

    // 2. Pure Native Relative Hardware Mouse Movement (Zero Jitter, Zero Lag, Native Acceleration)
    if (payload.flags & MFLAG_MOVE) {
        INPUT input = {};
        input.type = INPUT_MOUSE;
        input.mi.dx = payload.dx;
        input.mi.dy = payload.dy;
        input.mi.dwFlags = MOUSEEVENTF_MOVE;
        input.mi.dwExtraInfo = BUTTERFLOW_EXTRA_INFO_MAGIC;
        SendInput(1, &input, sizeof(INPUT));
    }

    // 3. Button Events
    auto SendMouseEvent = [](DWORD dwFlags, DWORD mouseData = 0) {
        INPUT in = {};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = dwFlags;
        in.mi.mouseData = mouseData;
        in.mi.dwExtraInfo = BUTTERFLOW_EXTRA_INFO_MAGIC;
        SendInput(1, &in, sizeof(INPUT));
    };

    if (payload.flags & MFLAG_LEFT_DOWN)   SendMouseEvent(MOUSEEVENTF_LEFTDOWN);
    if (payload.flags & MFLAG_LEFT_UP)     SendMouseEvent(MOUSEEVENTF_LEFTUP);
    if (payload.flags & MFLAG_RIGHT_DOWN)  SendMouseEvent(MOUSEEVENTF_RIGHTDOWN);
    if (payload.flags & MFLAG_RIGHT_UP)    SendMouseEvent(MOUSEEVENTF_RIGHTUP);
    if (payload.flags & MFLAG_MID_DOWN)    SendMouseEvent(MOUSEEVENTF_MIDDLEDOWN);
    if (payload.flags & MFLAG_MID_UP)      SendMouseEvent(MOUSEEVENTF_MIDDLEUP);

    if (payload.flags & MFLAG_WHEEL) {
        SendMouseEvent(MOUSEEVENTF_WHEEL, static_cast<DWORD>(payload.wheelDelta));
    }
    if (payload.flags & MFLAG_HWHEEL) {
        SendMouseEvent(MOUSEEVENTF_HWHEEL, static_cast<DWORD>(payload.hWheelDelta));
    }
    if (payload.flags & MFLAG_X1_DOWN) {
        SendMouseEvent(MOUSEEVENTF_XDOWN, XBUTTON1);
    }
    if (payload.flags & MFLAG_X1_UP) {
        SendMouseEvent(MOUSEEVENTF_XUP, XBUTTON1);
    }
    if (payload.flags & MFLAG_X2_DOWN) {
        SendMouseEvent(MOUSEEVENTF_XDOWN, XBUTTON2);
    }
    if (payload.flags & MFLAG_X2_UP) {
        SendMouseEvent(MOUSEEVENTF_XUP, XBUTTON2);
    }
}

void InputInjector::InjectKeyboard(const KeyboardInputPayload& payload) {
    if (!m_isActive) return;

    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = payload.vkCode;
    input.ki.wScan = payload.scanCode;
    input.ki.dwFlags = payload.flags;
    input.ki.dwExtraInfo = BUTTERFLOW_EXTRA_INFO_MAGIC;

    SendInput(1, &input, sizeof(INPUT));
}
