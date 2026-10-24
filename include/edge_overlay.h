#ifndef BUTTERFLOW_EDGE_OVERLAY_H
#define BUTTERFLOW_EDGE_OVERLAY_H

#include <windows.h>
#include <gdiplus.h>
#include "protocol.h"

class EdgeOverlay {
public:
    EdgeOverlay();
    ~EdgeOverlay();

    bool Initialize(HINSTANCE hInstance);
    void ShowApproach(ScreenEdge edge, float pushProgress, float normalizedY);
    void PlayTransitionBurst(ScreenEdge edge, float normalizedY);
    void PlayPreview(ScreenEdge edge);
    void SetEdge(ScreenEdge edge) { m_currentEdge = edge; }
    void Hide();

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    void Render(HDC hdcMem, int width, int height);
    void UpdateLayeredWindowContent();

    HWND m_hWnd;
    HINSTANCE m_hInstance;
    ULONG_PTR m_gdiplusToken;

    ScreenEdge m_currentEdge;
    float m_pushProgress;
    float m_normalizedY;
    bool m_isBursting;
    float m_burstRadius;
    int m_screenWidth;
    int m_screenHeight;

    HDC m_hdcScreen;
    HDC m_hdcMem;
    HBITMAP m_hBitmap;
    HBITMAP m_hOldBitmap;
};

#endif // BUTTERFLOW_EDGE_OVERLAY_H
