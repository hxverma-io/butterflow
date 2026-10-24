#ifndef BUTTERFLOW_EDGE_PHYSICS_H
#define BUTTERFLOW_EDGE_PHYSICS_H

#include <windows.h>
#include <functional>
#include "protocol.h"

enum class ControlState {
    LOCAL_ACTIVE,        // Normal local control
    PUSHING_BORDER,      // Interacting with edge barrier (spring resistance)
    REMOTE_ACTIVE        // Controlling the remote PC
};

class EdgePhysics {
public:
    using ApproachCallback = std::function<void(ScreenEdge edge, float progress, float normY)>;
    using TransitionCallback = std::function<void(ScreenEdge edge, float normY)>;
    using ReturnCallback = std::function<void(ScreenEdge edge, float normY)>;

    EdgePhysics();

    void Configure(ScreenEdge remoteEdge, int screenW, int screenH);
    void SetCallbacks(ApproachCallback onApproach, TransitionCallback onTransition, ReturnCallback onReturn);

    // Feed raw mouse motion before processing
    // Returns true if event should be intercepted/suppressed
    bool ProcessLocalMouseMove(int x, int y, int dx, int dy);
    
    // Remote edge crossing event received from peer
    void OnRemoteCursorReturned(float normY);

    void DecayTick();
    void ResetToLocal();

    ControlState GetState() const { return m_state; }
    ScreenEdge GetRemoteEdge() const { return m_remoteEdge; }

private:
    ScreenEdge m_remoteEdge;
    int m_screenWidth;
    int m_screenHeight;

    ControlState m_state;
    float m_accumulatedPush;
    float m_pushThreshold;
    DWORD m_lastPushTime;

    ApproachCallback m_onApproach;
    TransitionCallback m_onTransition;
    ReturnCallback m_onReturn;
};

#endif // BUTTERFLOW_EDGE_PHYSICS_H
