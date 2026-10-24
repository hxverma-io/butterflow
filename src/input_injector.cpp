#include "../include/input_injector.h"
#include <algorithm>

InputInjector::InputInjector()
    : m_screenWidth(1920),
      m_screenHeight(1080),
      m_entryEdge(EDGE_LEFT),
      m_isActive(false) {
}

InputInjector::~InputInjector() {
}

void InputInjector::Initialize(int screenW, int screenH, ScreenEdge entryEdge) {
    m_screenWidth = screenW;
    m_screenHeight = screenH;
    m_entryEdge = entryEdge;
}

void InputInjector::SetReturnCallback(ReturnBorderCallback onReturn) {
    m_onReturn = onReturn;
}

void InputInjector::PlaceCursorAtEdge(ScreenEdge edge, float normY) {
    m_entryEdge = edge;
    m_isActive = true;

    int targetX = 0;
    int targetY = static_cast<int>(std::clamp(normY, 0.05f, 0.95f) * m_screenHeight);

    if (edge == EDGE_LEFT) {
        targetX = 5;
    } else if (edge == EDGE_RIGHT) {
        targetX = m_screenWidth - 5;
    } else if (edge == EDGE_TOP) {
        targetX = m_screenWidth / 2;
        targetY = 5;
    } else if (edge == EDGE_BOTTOM) {
        targetX = m_screenWidth / 2;
        targetY = m_screenHeight - 5;
    }

    SetCursorPos(targetX, targetY);
}

void InputInjector::InjectMouse(const MouseInputPayload& payload) {
    if (!m_isActive) return;

    POINT currentPos;
    GetCursorPos(&currentPos);

    // Check for reverse border crossing back to Host
    if (m_entryEdge == EDGE_LEFT && payload.dx < 0 && currentPos.x <= 1) {
        m_isActive = false;
        if (m_onReturn) {
            float normY = static_cast<float>(currentPos.y) / static_cast<float>(m_screenHeight);
            m_onReturn(normY);
        }
        return;
    } else if (m_entryEdge == EDGE_RIGHT && payload.dx > 0 && currentPos.x >= m_screenWidth - 2) {
        m_isActive = false;
        if (m_onReturn) {
            float normY = static_cast<float>(currentPos.y) / static_cast<float>(m_screenHeight);
            m_onReturn(normY);
        }
        return;
    } else if (m_entryEdge == EDGE_TOP && payload.dy < 0 && currentPos.y <= 1) {
        m_isActive = false;
        if (m_onReturn) {
            float normX = static_cast<float>(currentPos.x) / static_cast<float>(m_screenWidth);
            m_onReturn(normX);
        }
        return;
    } else if (m_entryEdge == EDGE_BOTTOM && payload.dy > 0 && currentPos.y >= m_screenHeight - 2) {
        m_isActive = false;
        if (m_onReturn) {
            float normX = static_cast<float>(currentPos.x) / static_cast<float>(m_screenWidth);
            m_onReturn(normX);
        }
        return;
    }

    INPUT input = {};
    input.type = INPUT_MOUSE;

    // Movement
    if (payload.flags & MFLAG_MOVE) {
        input.mi.dx = payload.dx;
        input.mi.dy = payload.dy;
        input.mi.dwFlags = MOUSEEVENTF_MOVE;
        SendInput(1, &input, sizeof(INPUT));
    }

    // Button Events
    auto SendMouseEvent = [](DWORD dwFlags, DWORD mouseData = 0) {
        INPUT in = {};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = dwFlags;
        in.mi.mouseData = mouseData;
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

    SendInput(1, &input, sizeof(INPUT));
}
