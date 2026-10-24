#include "../include/input_injector.h"
#include <algorithm>
#include <cmath>

InputInjector::InputInjector()
    : m_screenWidth(1920),
      m_screenHeight(1080),
      m_entryEdge(EDGE_LEFT),
      m_isActive(false),
      m_accumulatedReturnPush(0.0f) {
}

InputInjector::~InputInjector() {
}

void InputInjector::Initialize(int screenW, int screenH, ScreenEdge entryEdge) {
    m_screenWidth = screenW > 0 ? screenW : 1920;
    m_screenHeight = screenH > 0 ? screenH : 1080;
    m_entryEdge = entryEdge;
    m_accumulatedReturnPush = 0.0f;
}

void InputInjector::SetReturnCallback(ReturnBorderCallback onReturn) {
    m_onReturn = onReturn;
}

void InputInjector::PlaceCursorAtEdge(ScreenEdge edge, float normY) {
    m_entryEdge = edge;
    m_isActive = true;
    m_accumulatedReturnPush = 0.0f;

    int targetX = m_screenWidth / 2;
    int targetY = static_cast<int>(std::clamp(normY, 0.05f, 0.95f) * m_screenHeight);

    // Place cursor with a comfortable 40px margin inside the screen
    if (edge == EDGE_LEFT) {
        targetX = 40;
    } else if (edge == EDGE_RIGHT) {
        targetX = m_screenWidth - 40;
    } else if (edge == EDGE_TOP) {
        targetX = static_cast<int>(std::clamp(normY, 0.05f, 0.95f) * m_screenWidth);
        targetY = 40;
    } else if (edge == EDGE_BOTTOM) {
        targetX = static_cast<int>(std::clamp(normY, 0.05f, 0.95f) * m_screenWidth);
        targetY = m_screenHeight - 40;
    }

    SetCursorPos(targetX, targetY);
}

void InputInjector::InjectMouse(const MouseInputPayload& payload) {
    if (!m_isActive) return;

    // 1. Check for deliberate return push against the entry edge
    bool atReturnBorder = false;
    float returnPush = 0.0f;

    if (m_entryEdge == EDGE_LEFT) {
        if (payload.normX <= 150 && payload.dx < 0) {
            atReturnBorder = true;
            returnPush = static_cast<float>(-payload.dx);
        }
    } else if (m_entryEdge == EDGE_RIGHT) {
        if (payload.normX >= 65385 && payload.dx > 0) {
            atReturnBorder = true;
            returnPush = static_cast<float>(payload.dx);
        }
    } else if (m_entryEdge == EDGE_TOP) {
        if (payload.normY <= 150 && payload.dy < 0) {
            atReturnBorder = true;
            returnPush = static_cast<float>(-payload.dy);
        }
    } else if (m_entryEdge == EDGE_BOTTOM) {
        if (payload.normY >= 65385 && payload.dy > 0) {
            atReturnBorder = true;
            returnPush = static_cast<float>(payload.dy);
        }
    }

    if (atReturnBorder) {
        m_accumulatedReturnPush += std::max(returnPush, 1.0f);
        // Require 20px deliberate push to return back to Host PC
        if (m_accumulatedReturnPush >= 20.0f) {
            m_isActive = false;
            m_accumulatedReturnPush = 0.0f;
            if (m_onReturn) {
                float normCoord = (m_entryEdge == EDGE_TOP || m_entryEdge == EDGE_BOTTOM) 
                                  ? (static_cast<float>(payload.normX) / 65535.0f) 
                                  : (static_cast<float>(payload.normY) / 65535.0f);
                m_onReturn(normCoord);
            }
            return;
        }
    } else {
        m_accumulatedReturnPush = 0.0f;
    }

    // 2. High-precision Absolute Cursor Positioning (Zero Jitter, Zero Drift)
    if (payload.flags & MFLAG_MOVE) {
        INPUT input = {};
        input.type = INPUT_MOUSE;
        input.mi.dx = (static_cast<DWORD>(payload.normX) * 65536) / 65535;
        input.mi.dy = (static_cast<DWORD>(payload.normY) * 65536) / 65535;
        input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
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
