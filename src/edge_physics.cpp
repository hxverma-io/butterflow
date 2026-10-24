#include "../include/edge_physics.h"
#include "../include/input_capturer.h"
#include <algorithm>
#include <cmath>

EdgePhysics::EdgePhysics()
    : m_remoteEdge(EDGE_RIGHT),
      m_screenWidth(1920),
      m_screenHeight(1080),
      m_state(ControlState::LOCAL_ACTIVE),
      m_accumulatedPush(0.0f),
      m_pushThreshold(35.0f), // 35 pixels of deliberate displacement
      m_lastPushTime(0),
      m_lastTransitionTime(0),
      m_pushThroughEnabled(true) {
}

void EdgePhysics::Configure(ScreenEdge remoteEdge, int screenW, int screenH) {
    m_remoteEdge = remoteEdge;
    m_screenWidth = screenW;
    m_screenHeight = screenH;
    m_accumulatedPush = 0.0f;
    m_state = ControlState::LOCAL_ACTIVE;
}

void EdgePhysics::SetCallbacks(ApproachCallback onApproach, TransitionCallback onTransition, ReturnCallback onReturn) {
    m_onApproach = onApproach;
    m_onTransition = onTransition;
    m_onReturn = onReturn;
}

void EdgePhysics::SetCompanionReceiving(bool receiving) {
    if (receiving) {
        m_state = ControlState::COMPANION_RECEIVING;
        m_accumulatedPush = 0.0f;
    } else if (m_state == ControlState::COMPANION_RECEIVING) {
        m_state = ControlState::LOCAL_ACTIVE;
        m_accumulatedPush = 0.0f;
    }
}

bool EdgePhysics::ProcessLocalMouseMove(int x, int y, int dx, int dy) {
    // If controlling the remote PC, local mouse movement is suppressed
    if (m_state == ControlState::REMOTE_ACTIVE) {
        return true;
    }

    // If this PC is receiving remote cursor from another machine, edge physics is completely disabled!
    if (m_state == ControlState::COMPANION_RECEIVING) {
        return false;
    }

    if (!m_pushThroughEnabled) {
        return false;
    }

    // Prevent immediate bounce-back within 350ms of any transition
    DWORD now = GetTickCount();
    if (now - m_lastTransitionTime < 350) {
        return false;
    }

    bool atBorder = false;
    switch (m_remoteEdge) {
        case EDGE_RIGHT:
            if (x >= m_screenWidth - 1 || (x >= m_screenWidth - 3 && dx > 0)) {
                atBorder = true;
            }
            break;
        case EDGE_LEFT:
            if (x <= 0 || (x <= 2 && dx < 0)) {
                atBorder = true;
            }
            break;
        case EDGE_TOP:
            if (y <= 0 || (y <= 2 && dy < 0)) {
                atBorder = true;
            }
            break;
        case EDGE_BOTTOM:
            if (y >= m_screenHeight - 1 || (y >= m_screenHeight - 3 && dy > 0)) {
                atBorder = true;
            }
            break;
    }

    if (atBorder) {
        // Seamless fluid edge crossing — zero freeze whether moving slowly or fastly!
        m_state = ControlState::REMOTE_ACTIVE;
        m_accumulatedPush = 0.0f;
        m_lastTransitionTime = now;

        float normCoord = (m_remoteEdge == EDGE_TOP || m_remoteEdge == EDGE_BOTTOM)
                          ? (static_cast<float>(x) / static_cast<float>(m_screenWidth))
                          : (static_cast<float>(y) / static_cast<float>(m_screenHeight));

        if (m_onTransition) {
            m_onTransition(m_remoteEdge, normCoord);
        }
        return true;
    }

    return false;
}

void EdgePhysics::DecayTick() {
    // Instant crossing requires no decay
}

void EdgePhysics::OnRemoteCursorReturned(float normY) {
    m_state = ControlState::LOCAL_ACTIVE;
    m_accumulatedPush = 0.0f;
    m_lastTransitionTime = GetTickCount();

    // Place local cursor safely INSIDE the screen (40px margin) so it never bounces back into remote
    int targetX = m_screenWidth / 2;
    int targetY = static_cast<int>(std::clamp(normY, 0.05f, 0.95f) * m_screenHeight);

    if (m_remoteEdge == EDGE_RIGHT) {
        targetX = m_screenWidth - 40;
    } else if (m_remoteEdge == EDGE_LEFT) {
        targetX = 40;
    } else if (m_remoteEdge == EDGE_TOP) {
        targetX = static_cast<int>(std::clamp(normY, 0.05f, 0.95f) * m_screenWidth);
        targetY = 40;
    } else if (m_remoteEdge == EDGE_BOTTOM) {
        targetX = static_cast<int>(std::clamp(normY, 0.05f, 0.95f) * m_screenWidth);
        targetY = m_screenHeight - 40;
    }

    SetCursorPos(targetX, targetY);
    InputCapturer::Instance().SyncCursorPos(targetX, targetY);

    if (m_onReturn) {
        m_onReturn(m_remoteEdge, normY);
    }
}

void EdgePhysics::ResetToLocal() {
    m_state = ControlState::LOCAL_ACTIVE;
    m_accumulatedPush = 0.0f;
    m_lastTransitionTime = GetTickCount();
    ClipCursor(nullptr);
}
