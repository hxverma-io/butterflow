#include "../include/edge_overlay.h"
#include <algorithm>
#include <cmath>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

EdgeOverlay::EdgeOverlay()
    : m_hWnd(nullptr),
      m_hInstance(nullptr),
      m_gdiplusToken(0),
      m_currentEdge(EDGE_LEFT),
      m_pushProgress(0.0f),
      m_normalizedY(0.5f),
      m_isBursting(false),
      m_burstRadius(0.0f),
      m_screenWidth(0),
      m_screenHeight(0),
      m_hdcScreen(nullptr),
      m_hdcMem(nullptr),
      m_hBitmap(nullptr),
      m_hOldBitmap(nullptr) {
}

EdgeOverlay::~EdgeOverlay() {
    if (m_hBitmap && m_hdcMem) {
        SelectObject(m_hdcMem, m_hOldBitmap);
        DeleteObject(m_hBitmap);
        DeleteDC(m_hdcMem);
        ReleaseDC(nullptr, m_hdcScreen);
    }
    if (m_hWnd) {
        DestroyWindow(m_hWnd);
    }
    if (m_gdiplusToken) {
        GdiplusShutdown(m_gdiplusToken);
    }
}

LRESULT CALLBACK EdgeOverlay::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_DESTROY) {
        return 0;
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}

bool EdgeOverlay::Initialize(HINSTANCE hInstance) {
    m_hInstance = hInstance;

    GdiplusStartupInput gdiplusStartupInput;
    if (GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, nullptr) != Ok) {
        return false;
    }

    m_screenWidth = GetSystemMetrics(SM_CXSCREEN);
    m_screenHeight = GetSystemMetrics(SM_CYSCREEN);

    WNDCLASSEX wc = { sizeof(WNDCLASSEX) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = m_hInstance;
    wc.lpszClassName = "ButterflowEdgeOverlay";
    RegisterClassEx(&wc);

    // Create layered, click-through, topmost overlay window
    m_hWnd = CreateWindowEx(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        "ButterflowEdgeOverlay",
        "UniversalControl_EdgeIndicator",
        WS_POPUP,
        0, 0, m_screenWidth, m_screenHeight,
        nullptr, nullptr, m_hInstance, nullptr
    );

    if (!m_hWnd) return false;

    m_hdcScreen = GetDC(nullptr);
    m_hdcMem = CreateCompatibleDC(m_hdcScreen);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = m_screenWidth;
    bmi.bmiHeader.biHeight = -m_screenHeight; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pvBits = nullptr;
    m_hBitmap = CreateDIBSection(m_hdcMem, &bmi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
    m_hOldBitmap = (HBITMAP)SelectObject(m_hdcMem, m_hBitmap);

    SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, m_screenWidth, m_screenHeight,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);

    return true;
}

void EdgeOverlay::ShowApproach(ScreenEdge edge, float pushProgress, float normalizedY) {
    m_currentEdge = edge;
    m_pushProgress = std::clamp(pushProgress, 0.0f, 1.0f);
    m_normalizedY = std::clamp(normalizedY, 0.05f, 0.95f);
    m_isBursting = false;

    if (m_pushProgress > 0.01f) {
        UpdateLayeredWindowContent();
    } else {
        Hide();
    }
}

void EdgeOverlay::PlayTransitionBurst(ScreenEdge edge, float normalizedY) {
    m_currentEdge = edge;
    m_normalizedY = normalizedY;
    m_isBursting = true;
    m_burstRadius = 10.0f;

    // Quick burst animation
    for (int frame = 0; frame < 6; ++frame) {
        m_burstRadius += 12.0f;
        m_pushProgress = 1.0f - (frame * 0.18f);
        UpdateLayeredWindowContent();
        Sleep(16);
    }
    Hide();
}

void EdgeOverlay::Hide() {
    m_pushProgress = 0.0f;
    m_isBursting = false;
    
    // Clear backbuffer
    RECT rc = { 0, 0, m_screenWidth, m_screenHeight };
    HBRUSH hNullBrush = (HBRUSH)GetStockObject(BLACK_BRUSH);
    FillRect(m_hdcMem, &rc, hNullBrush);

    POINT ptSrc = { 0, 0 };
    SIZE sizeWnd = { m_screenWidth, m_screenHeight };
    POINT ptDst = { 0, 0 };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 0, AC_SRC_ALPHA };

    UpdateLayeredWindow(m_hWnd, m_hdcScreen, &ptDst, &sizeWnd, m_hdcMem, &ptSrc, 0, &bf, ULW_ALPHA);
}

void EdgeOverlay::Render(HDC hdcMem, int width, int height) {
    Graphics g(hdcMem);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.Clear(Color(0, 0, 0, 0));

    if (m_pushProgress <= 0.01f && !m_isBursting) return;

    float centerY = m_normalizedY * height;
    float pillHeight = 120.0f + (m_pushProgress * 40.0f);
    float maxReach = 32.0f * m_pushProgress; // How far the bubble expands inward
    float pillY = centerY - (pillHeight / 2.0f);

    RectF capsuleRect;
    if (m_currentEdge == EDGE_LEFT) {
        capsuleRect = RectF(0.0f, pillY, maxReach, pillHeight);
    } else if (m_currentEdge == EDGE_RIGHT) {
        capsuleRect = RectF(width - maxReach, pillY, maxReach, pillHeight);
    } else {
        capsuleRect = RectF(0.0f, pillY, maxReach, pillHeight);
    }

    // Outer subtle glow halo
    GraphicsPath glowPath;
    float glowExpand = 12.0f * m_pushProgress;
    RectF glowRect = capsuleRect;
    glowRect.Inflate(glowExpand, glowExpand);
    glowPath.AddEllipse(glowRect);

    PathGradientBrush glowBrush(&glowPath);
    Color glowColors[] = { Color(static_cast<BYTE>(70 * m_pushProgress), 255, 255, 255) };
    int count = 1;
    glowBrush.SetCenterColor(Color(static_cast<BYTE>(100 * m_pushProgress), 180, 215, 255));
    glowBrush.SetSurroundColors(glowColors, &count);
    g.FillPath(&glowBrush, &glowPath);

    // Apple Frosted Glass Pill Capsule
    GraphicsPath pillPath;
    float radius = std::min(maxReach, 18.0f);
    if (m_currentEdge == EDGE_LEFT) {
        pillPath.AddArc(capsuleRect.X + capsuleRect.Width - (radius * 2), capsuleRect.Y, radius * 2, radius * 2, 270, 90);
        pillPath.AddArc(capsuleRect.X + capsuleRect.Width - (radius * 2), capsuleRect.Y + capsuleRect.Height - (radius * 2), radius * 2, radius * 2, 0, 90);
        pillPath.AddLine(capsuleRect.X, capsuleRect.Y + capsuleRect.Height, capsuleRect.X, capsuleRect.Y);
    } else {
        pillPath.AddArc(capsuleRect.X, capsuleRect.Y, radius * 2, radius * 2, 180, 90);
        pillPath.AddArc(capsuleRect.X, capsuleRect.Y + capsuleRect.Height - (radius * 2), radius * 2, radius * 2, 90, 90);
        pillPath.AddLine(capsuleRect.X + capsuleRect.Width, capsuleRect.Y + capsuleRect.Height, capsuleRect.X + capsuleRect.Width, capsuleRect.Y);
    }
    pillPath.CloseFigure();

    // Fill with Apple Acrylic gradient (crisp milky white to translucent ice)
    BYTE baseAlpha = static_cast<BYTE>(160 * m_pushProgress);
    BYTE highlightAlpha = static_cast<BYTE>(230 * m_pushProgress);
    LinearGradientBrush pillBrush(
        PointF(capsuleRect.X, capsuleRect.Y),
        PointF(capsuleRect.X + capsuleRect.Width, capsuleRect.Y + capsuleRect.Height),
        Color(highlightAlpha, 255, 255, 255),
        Color(baseAlpha, 210, 225, 245)
    );
    g.FillPath(&pillBrush, &pillPath);

    // Crisp high-contrast boundary rim
    Pen borderPen(Color(static_cast<BYTE>(200 * m_pushProgress), 255, 255, 255), 1.5f);
    g.DrawPath(&borderPen, &pillPath);

    // If bursting, draw dynamic shockwave ripple
    if (m_isBursting) {
        Pen burstPen(Color(static_cast<BYTE>(180 * m_pushProgress), 255, 255, 255), 2.5f);
        float burstX = (m_currentEdge == EDGE_LEFT) ? 0.0f : width;
        g.DrawEllipse(&burstPen, burstX - m_burstRadius, centerY - m_burstRadius, m_burstRadius * 2, m_burstRadius * 2);
    }
}

void EdgeOverlay::UpdateLayeredWindowContent() {
    Render(m_hdcMem, m_screenWidth, m_screenHeight);

    POINT ptSrc = { 0, 0 };
    SIZE sizeWnd = { m_screenWidth, m_screenHeight };
    POINT ptDst = { 0, 0 };
    BLENDFUNCTION bf = {};
    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;

    UpdateLayeredWindow(m_hWnd, m_hdcScreen, &ptDst, &sizeWnd, m_hdcMem, &ptSrc, 0, &bf, ULW_ALPHA);
}
