#ifndef BUTTERFLOW_EDGE_PHYSICS_H
#define BUTTERFLOW_EDGE_PHYSICS_H

#include <windows.h>
#include <functional>
#include "protocol.h"

enum class ControlState {
    LOCAL_ACTIVE,         // Normal local control on this PC
    PUSHING_BORDER,       // Interacting with edge barrier (spring resistance)
    REMOTE_ACTIVE,        // Controlling the remote PC
    COMPANION_RECEIVING   // Receiving remote cursor from the other PC (local physics disabled)
};

class EdgePhysics {
public:
    using ApproachCallback = std::function<void(ScreenEdge edge, float progress, float normY)>;
    using TransitionCallback = std::function<void(ScreenEdge edge, float normY)>;
    using ReturnCallback = std::function<void(ScreenEdge edge, float normY)>;

    EdgePhysics();

    void Configure(ScreenEdge remoteEdge, int screenW, int screenH);
    void SetCallbacks(ApproachCallback onApproach, TransitionCallback onTransition, ReturnCallback onReturn);

    // Process mouse movement for edge resistance and breakthrough
    bool ProcessLocalMouseMove(int x, int y, int dx, int dy);
    
    // Remote edge crossing event received from peer
    void OnRemoteCursorReturned(float normY);

    void DecayTick();
    void ResetToLocal();

    // Mode management
    void SetCompanionReceiving(bool receiving);
    bool IsCompanionReceiving() const { return m_state == ControlState::COMPANION_RECEIVING; }

    ControlState GetState() const { return m_state; }
    ScreenEdge GetRemoteEdge() const { return m_remoteEdge; }
    void SetRemoteEdge(ScreenEdge edge) { m_remoteEdge = edge; m_accumulatedPush = 0.0f; }
    void SetPushThreshold(float threshold) { m_pushThreshold = threshold; }
    float GetPushThreshold() const { return m_pushThreshold; }
    void SetPushThroughEnabled(bool enabled) { m_pushThroughEnabled = enabled; }
    bool IsPushThroughEnabled() const { return m_pushThroughEnabled; }
    bool IsRemote() const { return m_state == ControlState::REMOTE_ACTIVE; }
    bool IsPushing() const { return m_state == ControlState::PUSHING_BORDER; }
    float GetAccumulatedPush() const { return m_accumulatedPush; }

private:
    ScreenEdge m_remoteEdge;
    int m_screenWidth;
    int m_screenHeight;

    ControlState m_state;
    float m_accumulatedPush;
    float m_pushThreshold;
    DWORD m_lastPushTime;
    DWORD m_lastTransitionTime;
    bool m_pushThroughEnabled;

    ApproachCallback m_onApproach;
    TransitionCallback m_onTransition;
    ReturnCallback m_onReturn;
};

#endif // BUTTERFLOW_EDGE_PHYSICS_H
