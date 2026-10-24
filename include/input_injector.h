#ifndef BUTTERFLOW_INPUT_INJECTOR_H
#define BUTTERFLOW_INPUT_INJECTOR_H

#include <windows.h>
#include <functional>
#include <thread>
#include <atomic>
#include "protocol.h"

class InputInjector {
public:
    using ReturnBorderCallback = std::function<void(float normY)>;

    InputInjector();
    ~InputInjector();

    void Initialize(int screenW, int screenH, ScreenEdge entryEdge);
    void Stop();
    void SetReturnCallback(ReturnBorderCallback onReturn);

    void InjectMouse(const MouseInputPayload& payload);
    void InjectKeyboard(const KeyboardInputPayload& payload);
    void PlaceCursorAtEdge(ScreenEdge edge, float normY);

    void SetActive(bool active);
    bool IsActive() const { return m_isActive; }
    void SetEntryEdge(ScreenEdge entryEdge) { m_entryEdge = entryEdge; }
    ScreenEdge GetEntryEdge() const { return m_entryEdge; }

private:
    void PumpThreadFunc();

    int m_screenWidth;
    int m_screenHeight;
    ScreenEdge m_entryEdge;
    bool m_isActive;
    float m_accumulatedReturnPush;
    DWORD m_entryTime;
    ReturnBorderCallback m_onReturn;

    // High-precision 0% Jitter Dispatcher Pump
    std::atomic<int32_t> m_pendingDx;
    std::atomic<int32_t> m_pendingDy;
    std::atomic<bool> m_pumpRunning;
    std::thread m_pumpThread;
};

#endif // BUTTERFLOW_INPUT_INJECTOR_H
