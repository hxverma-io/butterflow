#ifndef BUTTERFLOW_INPUT_INJECTOR_H
#define BUTTERFLOW_INPUT_INJECTOR_H

#include <windows.h>
#include <functional>
#include "protocol.h"

class InputInjector {
public:
    using ReturnBorderCallback = std::function<void(float normY)>;

    InputInjector();
    ~InputInjector();

    void Initialize(int screenW, int screenH, ScreenEdge entryEdge);
    void SetReturnCallback(ReturnBorderCallback onReturn);

    void InjectMouse(const MouseInputPayload& payload);
    void InjectKeyboard(const KeyboardInputPayload& payload);
    void PlaceCursorAtEdge(ScreenEdge edge, float normY);

    void SetActive(bool active) { m_isActive = active; }
    bool IsActive() const { return m_isActive; }
    void SetEntryEdge(ScreenEdge entryEdge) { m_entryEdge = entryEdge; }
    ScreenEdge GetEntryEdge() const { return m_entryEdge; }

private:
    int m_screenWidth;
    int m_screenHeight;
    ScreenEdge m_entryEdge;
    bool m_isActive;
    ReturnBorderCallback m_onReturn;
};

#endif // BUTTERFLOW_INPUT_INJECTOR_H
