#include "../include/edge_physics.h"
#include <algorithm>
#include <cmath>

EdgePhysics::EdgePhysics()
    : m_remoteEdge(EDGE_RIGHT),
      m_screenWidth(1920),
      m_screenHeight(1080),
      m_state(ControlState::LOCAL_ACTIVE),
      m_accumulatedPush(0.0f),
      m_pushThreshold(35.0f), // 35 pixels of deliberate continuous displacement
      m_lastPushTime(0),
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

bool EdgePhysics::ProcessLocalMouseMove(int x, int y, int dx, int dy) {
    if (m_state == ControlState::REMOTE_ACTIVE) {
        // Entirely suppressed locally; transmitted to remote peer
        return true;
    }

    if (!m_pushThroughEnabled) {
        return false;
    }

    bool atBorder = false;
    float pushDelta = 0.0f;

    switch (m_remoteEdge) {
        case EDGE_RIGHT:
            if (x >= m_screenWidth - 2) {
                atBorder = true;
                pushDelta = static_cast<float>(dx > 0 ? dx : 1);
            }
            break;
        case EDGE_LEFT:
            if (x <= 1) {
                atBorder = true;
                pushDelta = static_cast<float>(dx < 0 ? -dx : 1);
            }
            break;
        case EDGE_TOP:
            if (y <= 1) {
                atBorder = true;
                pushDelta = static_cast<float>(dy < 0 ? -dy : 1);
            }
            break;
        case EDGE_BOTTOM:
            if (y >= m_screenHeight - 2) {
                atBorder = true;
                pushDelta = static_cast<float>(dy > 0 ? dy : 1);
            }
            break;
    }

    if (atBorder) {
        m_state = ControlState::PUSHING_BORDER;
        m_lastPushTime = GetTickCount();
        m_accumulatedPush += std::max(pushDelta, 1.0f);

        float progress = std::clamp(m_accumulatedPush / m_pushThreshold, 0.0f, 1.0f);
        float normY = static_cast<float>(y) / static_cast<float>(m_screenHeight);

        if (m_onApproach) {
            m_onApproach(m_remoteEdge, progress, normY);
        }

        // Check if barrier is broken through
        if (progress >= 1.0f) {
            m_state = ControlState::REMOTE_ACTIVE;
            m_accumulatedPush = 0.0f;
            if (m_onTransition) {
                m_onTransition(m_remoteEdge, normY);
            }
            return true; // Suppress local cursor
        }

        return false;
    } else {
        // If cursor moved away from the edge, decay or reset
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
            m_accumulatedPush = std::max(0.0f, m_accumulatedPush - 3.5f);
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

    // Reposition local cursor at the matching edge
    int targetX = m_screenWidth / 2;
    int targetY = static_cast<int>(normY * m_screenHeight);

    if (m_remoteEdge == EDGE_RIGHT) {
        targetX = m_screenWidth - 5;
    } else if (m_remoteEdge == EDGE_LEFT) {
        targetX = 5;
    } else if (m_remoteEdge == EDGE_TOP) {
        targetX = static_cast<int>(normY * m_screenWidth);
        targetY = 5;
    } else if (m_remoteEdge == EDGE_BOTTOM) {
        targetX = static_cast<int>(normY * m_screenWidth);
        targetY = m_screenHeight - 5;
    }
    SetCursorPos(targetX, targetY);

    if (m_onReturn) {
        m_onReturn(m_remoteEdge, normY);
    }
}

void EdgePhysics::ResetToLocal() {
    m_state = ControlState::LOCAL_ACTIVE;
    m_accumulatedPush = 0.0f;
    ClipCursor(nullptr);
}
