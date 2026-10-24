#include "../include/edge_physics.h"
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

    // Prevent immediate bounce-back within 600ms of any transition
    DWORD now = GetTickCount();
    if (now - m_lastTransitionTime < 600) {
        return false;
    }

    bool atBorder = false;
    float pushDelta = 0.0f;

    switch (m_remoteEdge) {
        case EDGE_RIGHT:
            if (x >= m_screenWidth - 3 && dx > 0) {
                atBorder = true;
                pushDelta = static_cast<float>(dx);
            }
            break;
        case EDGE_LEFT:
            if (x <= 2 && dx < 0) {
                atBorder = true;
                pushDelta = static_cast<float>(-dx);
            }
            break;
        case EDGE_TOP:
            if (y <= 2 && dy < 0) {
                atBorder = true;
                pushDelta = static_cast<float>(-dy);
            }
            break;
        case EDGE_BOTTOM:
            if (y >= m_screenHeight - 3 && dy > 0) {
                atBorder = true;
                pushDelta = static_cast<float>(dy);
            }
            break;
    }

    if (atBorder) {
        m_state = ControlState::PUSHING_BORDER;
        m_lastPushTime = now;
        m_accumulatedPush += std::max(pushDelta, 1.0f);

        float progress = std::clamp(m_accumulatedPush / m_pushThreshold, 0.0f, 1.0f);
        float normY = static_cast<float>(y) / static_cast<float>(m_screenHeight);

        if (m_onApproach) {
            m_onApproach(m_remoteEdge, progress, normY);
        }

        // Barrier broken through! Transition to remote companion PC
        if (progress >= 1.0f) {
            m_state = ControlState::REMOTE_ACTIVE;
            m_accumulatedPush = 0.0f;
            m_lastTransitionTime = now;

            if (m_onTransition) {
                m_onTransition(m_remoteEdge, normY);
            }
            return true;
        }

        return false;
    } else {
        // Cursor moved away from the edge: reset push accumulation
        if (m_state == ControlState::PUSHING_BORDER) {
            m_accumulatedPush = 0.0f;
            m_state = ControlState::LOCAL_ACTIVE;
            if (m_onApproach) {
                m_onApproach(m_remoteEdge, 0.0f, static_cast<float>(y) / static_cast<float>(m_screenHeight));
            }
        }
        return false;
    }
}

void EdgePhysics::DecayTick() {
    if (m_state == ControlState::PUSHING_BORDER) {
        DWORD now = GetTickCount();
        if (now - m_lastPushTime > 80) { // Inactive for >80ms
            m_accumulatedPush = std::max(0.0f, m_accumulatedPush - 4.0f);
            float progress = m_accumulatedPush / m_pushThreshold;
            
            if (m_onApproach) {
                m_onApproach(m_remoteEdge, progress, 0.5f);
            }

            if (m_accumulatedPush <= 0.0f) {
                m_state = ControlState::LOCAL_ACTIVE;
            }
        }
    }
}

void EdgePhysics::OnRemoteCursorReturned(float normY) {
    m_state = ControlState::LOCAL_ACTIVE;
    m_accumulatedPush = 0.0f;
    m_lastTransitionTime = GetTickCount();

    // Place local cursor safely INSIDE the screen (50px margin) so it never bounces back into remote
    int targetX = m_screenWidth / 2;
    int targetY = static_cast<int>(std::clamp(normY, 0.08f, 0.92f) * m_screenHeight);

    if (m_remoteEdge == EDGE_RIGHT) {
        targetX = m_screenWidth - 50;
    } else if (m_remoteEdge == EDGE_LEFT) {
        targetX = 50;
    } else if (m_remoteEdge == EDGE_TOP) {
        targetX = static_cast<int>(std::clamp(normY, 0.08f, 0.92f) * m_screenWidth);
        targetY = 50;
    } else if (m_remoteEdge == EDGE_BOTTOM) {
        targetX = static_cast<int>(std::clamp(normY, 0.08f, 0.92f) * m_screenWidth);
        targetY = m_screenHeight - 50;
    }

    SetCursorPos(targetX, targetY);

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
