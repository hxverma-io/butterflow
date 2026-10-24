#include "../include/input_injector.h"
#include "../include/input_capturer.h"
#include <algorithm>
#include <cmath>

InputInjector::InputInjector()
    : m_screenWidth(1920),
      m_screenHeight(1080),
      m_entryEdge(EDGE_LEFT),
      m_isActive(false),
      m_accumulatedReturnPush(0.0f),
      m_entryTime(0),
      m_pendingDx(0),
      m_pendingDy(0),
      m_pumpRunning(false) {
}

InputInjector::~InputInjector() {
    Stop();
}

void InputInjector::Initialize(int screenW, int screenH, ScreenEdge entryEdge) {
    m_screenWidth = screenW > 0 ? screenW : 1920;
    m_screenHeight = screenH > 0 ? screenH : 1080;
    m_entryEdge = entryEdge;
    m_accumulatedReturnPush = 0.0f;
    m_entryTime = 0;
    m_pendingDx.store(0);
    m_pendingDy.store(0);

    if (!m_pumpRunning) {
        m_pumpRunning = true;
        m_pumpThread = std::thread(&InputInjector::PumpThreadFunc, this);
    }
}

void InputInjector::Stop() {
    if (m_pumpRunning) {
        m_pumpRunning = false;
        if (m_pumpThread.joinable()) {
            m_pumpThread.join();
        }
    }
}

void InputInjector::SetActive(bool active) {
    m_isActive = active;
    m_accumulatedReturnPush = 0.0f;
    m_entryTime = GetTickCount();
    if (!active) {
        m_pendingDx.store(0);
        m_pendingDy.store(0);
    }
}

void InputInjector::PumpThreadFunc() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    while (m_pumpRunning) {
        if (!m_isActive) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        int32_t curDx = m_pendingDx.load(std::memory_order_relaxed);
        int32_t curDy = m_pendingDy.load(std::memory_order_relaxed);

        if (curDx != 0 || curDy != 0) {
            // Adaptive Jitter-Free Dispenser:
            // For normal/small deltas (abs <= 8), dispense 100% immediately for zero latency!
            // For Wi-Fi packet clumping bursts (abs > 8), dispense 70% per 1ms tick to absorb network jitter
            int32_t stepDx = 0;
            if (std::abs(curDx) <= 8) {
                stepDx = curDx;
            } else {
                stepDx = static_cast<int32_t>(curDx * 0.70f);
                if (stepDx == 0) stepDx = (curDx > 0) ? 1 : -1;
            }

            int32_t stepDy = 0;
            if (std::abs(curDy) <= 8) {
                stepDy = curDy;
            } else {
                stepDy = static_cast<int32_t>(curDy * 0.70f);
                if (stepDy == 0) stepDy = (curDy > 0) ? 1 : -1;
            }

            m_pendingDx.fetch_sub(stepDx, std::memory_order_relaxed);
            m_pendingDy.fetch_sub(stepDy, std::memory_order_relaxed);

            INPUT input = {};
            input.type = INPUT_MOUSE;
            input.mi.dx = stepDx;
            input.mi.dy = stepDy;
            input.mi.dwFlags = MOUSEEVENTF_MOVE;
            input.mi.dwExtraInfo = BUTTERFLOW_EXTRA_INFO_MAGIC;
            SendInput(1, &input, sizeof(INPUT));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void InputInjector::SetReturnCallback(ReturnBorderCallback onReturn) {
    m_onReturn = onReturn;
}

void InputInjector::PlaceCursorAtEdge(ScreenEdge edge, float normY) {
    m_entryEdge = edge;
    m_isActive = true;
    m_accumulatedReturnPush = 0.0f;
    m_entryTime = GetTickCount();
    m_pendingDx.store(0);
    m_pendingDy.store(0);

    int targetX = m_screenWidth / 2;
    int targetY = static_cast<int>(std::clamp(normY, 0.05f, 0.95f) * m_screenHeight);

    // Place cursor with 25px safe landing margin inside the screen
    if (edge == EDGE_LEFT) {
        targetX = 25;
    } else if (edge == EDGE_RIGHT) {
        targetX = m_screenWidth - 25;
    } else if (edge == EDGE_TOP) {
        targetX = static_cast<int>(std::clamp(normY, 0.05f, 0.95f) * m_screenWidth);
        targetY = 25;
    } else if (edge == EDGE_BOTTOM) {
        targetX = static_cast<int>(std::clamp(normY, 0.05f, 0.95f) * m_screenWidth);
        targetY = m_screenHeight - 25;
    }

    SetCursorPos(targetX, targetY);
    InputCapturer::Instance().SyncCursorPos(targetX, targetY);
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
        m_pendingDx.store(0);
        m_pendingDy.store(0);
        if (m_onReturn) {
            float normCoord = (m_entryEdge == EDGE_TOP || m_entryEdge == EDGE_BOTTOM)
                              ? (static_cast<float>(cur.x) / static_cast<float>(m_screenWidth))
                              : (static_cast<float>(cur.y) / static_cast<float>(m_screenHeight));
            m_onReturn(normCoord);
        }
        return;
    }

    // 2. Feed movement into 0% Jitter Smoothing Pump
    if (payload.flags & MFLAG_MOVE) {
        // Immediate direction change: flush opposite pending deltas so user flicking never cancels out!
        int32_t currentPendingX = m_pendingDx.load(std::memory_order_relaxed);
        int32_t currentPendingY = m_pendingDy.load(std::memory_order_relaxed);

        if ((currentPendingX > 0 && payload.dx < 0) || (currentPendingX < 0 && payload.dx > 0)) {
            m_pendingDx.store(0, std::memory_order_relaxed);
        }
        if ((currentPendingY > 0 && payload.dy < 0) || (currentPendingY < 0 && payload.dy > 0)) {
            m_pendingDy.store(0, std::memory_order_relaxed);
        }

        m_pendingDx.fetch_add(payload.dx, std::memory_order_relaxed);
        m_pendingDy.fetch_add(payload.dy, std::memory_order_relaxed);
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
