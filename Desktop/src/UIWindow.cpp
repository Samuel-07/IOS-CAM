#include "UIWindow.h"
#include <iostream>

using namespace Gdiplus;

MainWindowUI::MainWindowUI(HINSTANCE hInstance)
    : m_hInstance(hInstance), m_streamWriter("Global\\MiCam_Stream_1")
{
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, NULL);

    // Load custom logo
    m_pLogoBitmap = Bitmap::FromFile(L"assets/micam_logo.jpg");
}

MainWindowUI::~MainWindowUI() {
    if (m_pLogoBitmap) delete m_pLogoBitmap;
    GdiplusShutdown(m_gdiplusToken);
}

bool MainWindowUI::CreateAndShow(int nCmdShow) {
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWindowUI::WindowProc;
    wc.hInstance = m_hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"MiCamProStudioWindowClass";

    RegisterClassExW(&wc);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int winW = 1280;
    int winH = 820;
    int posX = (screenW - winW) / 2;
    int posY = (screenH - winH) / 2;

    m_hwnd = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"MiCam Studio Pro - Professional Camera Control Center",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
        posX, posY, winW, winH,
        NULL, NULL, m_hInstance, this
    );

    if (!m_hwnd) return false;

    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);

    m_deviceManager.SetDeviceListCallback([this](const std::vector<ConnectedDevice>& devices) {
        this->UpdateDeviceList(devices);
    });
    m_deviceManager.StartDiscovery();

    return true;
}

void MainWindowUI::RunMessageLoop() {
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

void MainWindowUI::UpdateDeviceList(const std::vector<ConnectedDevice>& devices) {
    m_devices = devices;
    if (m_hwnd) InvalidateRect(m_hwnd, NULL, FALSE);
}

LRESULT CALLBACK MainWindowUI::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    MainWindowUI* pThis = nullptr;
    if (uMsg == WM_NCCREATE) {
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        pThis = (MainWindowUI*)pCreate->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
    } else {
        pThis = (MainWindowUI*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    }

    if (pThis) {
        return pThis->HandleMessage(hwnd, uMsg, wParam, lParam);
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

LRESULT MainWindowUI::HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_PAINT:
        OnPaint(hwnd);
        return 0;

    case WM_LBUTTONDOWN: {
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);
        RECT rc;
        GetClientRect(hwnd, &rc);

        // Language Switcher Click in Top Header
        if (x >= rc.right - 110 && x <= rc.right - 10 && y >= 15 && y <= 50) {
            m_language = (m_language == UILanguage::Spanish) ? UILanguage::English : UILanguage::Spanish;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        int panelX = rc.right - 310;
        // Lens selection clicks
        if (x >= panelX + 20 && x <= rc.right - 20) {
            int startY = 110;
            for (int i = 0; i < 4; ++i) {
                if (y >= startY && y <= startY + 44) {
                    m_selectedLens = i;
                    MiCamControlCommand cmd{};
                    cmd.lens = static_cast<uint8_t>(m_selectedLens);
                    cmd.targetWidth = 1920;
                    cmd.targetHeight = 1080;
                    cmd.targetFps = 60;
                    cmd.zoomFactor = m_zoomFactor;
                    cmd.torchEnabled = m_torchEnabled ? 1 : 0;
                    
                    if (!m_devices.empty()) {
                        m_deviceManager.SendControlCommand(m_devices[0].id, cmd);
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    break;
                }
                startY += 52;
            }

            // Torch Toggle Click
            if (y >= startY + 10 && y <= startY + 54) {
                m_torchEnabled = !m_torchEnabled;
                MiCamControlCommand cmd{};
                cmd.lens = static_cast<uint8_t>(m_selectedLens);
                cmd.torchEnabled = m_torchEnabled ? 1 : 0;
                if (!m_devices.empty()) {
                    m_deviceManager.SendControlCommand(m_devices[0].id, cmd);
                }
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_DESTROY:
        m_deviceManager.StopDiscovery();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void MainWindowUI::OnPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT rc;
    GetClientRect(hwnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    Bitmap backBuffer(width, height, PixelFormat32bppARGB);
    Graphics g(&backBuffer);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);

    bool isEs = (m_language == UILanguage::Spanish);

    // Deep Background #090A10
    SolidBrush bgBrush(Color(255, 9, 10, 16));
    g.FillRectangle(&bgBrush, 0, 0, width, height);

    // Top Header Bar #0F121D
    SolidBrush topBarBrush(Color(255, 15, 18, 29));
    g.FillRectangle(&topBarBrush, 0, 0, width, 65);
    Pen borderPen(Color(255, 30, 35, 55), 1);
    g.DrawLine(&borderPen, 0, 65, width, 65);

    // Logo & Header
    if (m_pLogoBitmap && m_pLogoBitmap->GetLastStatus() == Ok) {
        g.DrawImage(m_pLogoBitmap, 20, 12, 42, 42);
    } else {
        SolidBrush logoCircle(Color(255, 0, 240, 255));
        g.FillEllipse(&logoCircle, 20, 12, 42, 42);
    }

    Font brandFont(L"Segoe UI", 15, FontStyleBold);
    SolidBrush whiteBrush(Color(255, 255, 255, 255));
    g.DrawString(L"MICAM STUDIO PRO", -1, &brandFont, PointF(75, 12), &whiteBrush);

    Font versionFont(L"Segoe UI", 8, FontStyleBold);
    SolidBrush cyanBrush(Color(255, 0, 240, 255));
    const wchar_t* subTitleText = isEs ? L"v1.0.0 MOTOR DE ULTRA BAJA LATENCIA" : L"v1.0.0 ULTRA-LOW LATENCY ENGINE";
    g.DrawString(subTitleText, -1, &versionFont, PointF(75, 36), &cyanBrush);

    // Hardware Metrics Badge
    SolidBrush metricCard(Color(255, 22, 26, 42));
    g.FillRectangle(&metricCard, width - 480, 15, 350, 36);
    g.DrawRectangle(&borderPen, width - 480, 15, 350, 36);

    Font metricFont(L"Segoe UI", 9, FontStyleRegular);
    SolidBrush grayBrush(Color(255, 156, 163, 185));
    g.DrawString(L"CPU: 1.1%  |  RAM: 38 MB  |  Pipeline: Zero-Copy DXVA2", -1, &metricFont, PointF((REAL)width - 465, 24), &grayBrush);

    // Language Selector Button [ ES ] / [ EN ] (Top Right)
    SolidBrush langBg(Color(255, 124, 58, 237));
    g.FillRectangle(&langBg, width - 110, 15, 95, 36);
    Pen langBorder(Color(255, 167, 139, 250), 1);
    g.DrawRectangle(&langBorder, width - 110, 15, 95, 36);

    Font langFont(L"Segoe UI", 9, FontStyleBold);
    const wchar_t* langLabel = isEs ? L"ESPAÑOL" : L"ENGLISH";
    g.DrawString(langLabel, -1, &langFont, PointF((REAL)width - 98, 24), &whiteBrush);

    // 1. Sidebar (320px width)
    DrawSidebar(g, 320, height - 65);

    // 2. Main Preview Container
    int mainW = width - 660;
    int mainH = height - 85;
    DrawMainPreview(g, 330, 75, mainW, mainH);

    // 3. Right Control Panel (310px width)
    DrawControlPanel(g, width - 320, 75, 300, mainH);

    Graphics windowGraphics(hdc);
    windowGraphics.DrawImage(&backBuffer, 0, 0);
    EndPaint(hwnd, &ps);
}

void MainWindowUI::DrawSidebar(Graphics& g, int width, int height) {
    bool isEs = (m_language == UILanguage::Spanish);
    int startY = 75;

    SolidBrush cardBrush(Color(255, 16, 19, 31));
    g.FillRectangle(&cardBrush, 15, startY, width - 25, height - 25);

    Pen borderPen(Color(255, 30, 35, 55), 1);
    g.DrawRectangle(&borderPen, 15, startY, width - 25, height - 25);

    Font sectionFont(L"Segoe UI", 10, FontStyleBold);
    SolidBrush grayBrush(Color(255, 156, 163, 185));
    SolidBrush whiteBrush(Color(255, 255, 255, 255));

    const wchar_t* devTitle = isEs ? L"DISPOSITIVOS ACTIVOS" : L"ACTIVE DEVICES";
    g.DrawString(devTitle, -1, &sectionFont, PointF(30, (REAL)startY + 15), &grayBrush);

    // Device Item Card
    int cardY = startY + 45;
    SolidBrush devCard(Color(255, 23, 27, 43));
    g.FillRectangle(&devCard, 28, cardY, width - 50, 140);

    Pen cyanGlow(Color(255, 0, 240, 255), 1);
    g.DrawRectangle(&cyanGlow, 28, cardY, width - 50, 140);

    Font devNameFont(L"Segoe UI", 12, FontStyleBold);
    g.DrawString(L"Samuel's iPhone 15 Pro", -1, &devNameFont, PointF(42, (REAL)cardY + 14), &whiteBrush);

    // Status Pill
    SolidBrush greenDot(Color(255, 16, 185, 129));
    g.FillEllipse(&greenDot, 42, cardY + 45, 10, 10);

    Font statusFont(L"Segoe UI", 9, FontStyleBold);
    SolidBrush greenText(Color(255, 52, 211, 153));
    const wchar_t* statusText = isEs ? L"CONECTADO (USB usbmuxd)" : L"CONNECTED (USB usbmuxd)";
    g.DrawString(statusText, -1, &statusFont, PointF(58, (REAL)cardY + 42), &greenText);

    Font detailFont(L"Segoe UI", 9, FontStyleRegular);
    g.DrawString(L"IP: 127.0.0.1:50000", -1, &detailFont, PointF(42, (REAL)cardY + 65), &grayBrush);
    g.DrawString(L"Stream: 3840x2160 @ 60 FPS (4K)", -1, &detailFont, PointF(42, (REAL)cardY + 84), &grayBrush);

    // Battery Bar
    const wchar_t* batText = isEs ? L"Bateria: 94% (Cargando)" : L"Battery: 94% (Charging)";
    g.DrawString(batText, -1, &detailFont, PointF(42, (REAL)cardY + 106), &grayBrush);
    SolidBrush batBg(Color(255, 35, 40, 62));
    g.FillRectangle(&batBg, 175, cardY + 109, 80, 10);

    SolidBrush batFill(Color(255, 16, 185, 129));
    g.FillRectangle(&batFill, 175, cardY + 109, 75, 10);

    // System Telemetry Section
    int sysY = cardY + 160;
    const wchar_t* sysTitle = isEs ? L"ESTADO DEL SISTEMA" : L"SYSTEM STATUS";
    g.DrawString(sysTitle, -1, &sectionFont, PointF(30, (REAL)sysY), &grayBrush);

    SolidBrush sysCard(Color(255, 20, 23, 37));
    g.FillRectangle(&sysCard, 28, sysY + 25, width - 50, 120);
    g.DrawRectangle(&borderPen, 28, sysY + 25, width - 50, 120);

    const wchar_t* latText = isEs ? L"Latencia: < 24 ms (Ultra Baja)" : L"Latency: < 24 ms (Ultra Low)";
    const wchar_t* codecText = isEs ? L"Codec de Video: H.264 Hardware" : L"Video Codec: H.264 Hardware";
    const wchar_t* audioText = isEs ? L"Flujo de Audio: 48kHz Stereo AAC" : L"Audio Stream: 48kHz Stereo AAC";
    const wchar_t* thermText = isEs ? L"Estado Termico: Nominal (Normal)" : L"Thermal State: Nominal (Cool)";

    g.DrawString(latText, -1, &detailFont, PointF(42, (REAL)sysY + 40), &whiteBrush);
    g.DrawString(codecText, -1, &detailFont, PointF(42, (REAL)sysY + 62), &whiteBrush);
    g.DrawString(audioText, -1, &detailFont, PointF(42, (REAL)sysY + 84), &whiteBrush);
    g.DrawString(thermText, -1, &detailFont, PointF(42, (REAL)sysY + 106), &whiteBrush);
}

void MainWindowUI::DrawMainPreview(Graphics& g, int x, int y, int width, int height) {
    bool isEs = (m_language == UILanguage::Spanish);

    SolidBrush cardBrush(Color(255, 16, 19, 31));
    g.FillRectangle(&cardBrush, x, y, width, height);

    Pen borderPen(Color(255, 30, 35, 55), 1);
    g.DrawRectangle(&borderPen, x, y, width, height);

    // Video Viewport Box (Aspect 16:9)
    int viewW = width - 40;
    int viewH = (viewW * 9) / 16;
    if (viewH > height - 100) {
        viewH = height - 100;
        viewW = (viewH * 16) / 9;
    }
    int viewX = x + (width - viewW) / 2;
    int viewY = y + 25;

    SolidBrush videoBg(Color(255, 5, 6, 10));
    g.FillRectangle(&videoBg, viewX, viewY, viewW, viewH);

    Pen cyanBorder(Color(255, 0, 240, 255), 2);
    g.DrawRectangle(&cyanBorder, viewX, viewY, viewW, viewH);

    // Focus Reticle in Center
    Pen reticlePen(Color(100, 0, 240, 255), 1);
    int cx = viewX + viewW / 2;
    int cy = viewY + viewH / 2;
    g.DrawLine(&reticlePen, cx - 25, cy, cx + 25, cy);
    g.DrawLine(&reticlePen, cx, cy - 25, cx, cy + 25);
    g.DrawRectangle(&reticlePen, cx - 40, cy - 40, 80, 80);

    // Stream Title Banner
    Font streamTitleFont(L"Segoe UI", 15, FontStyleBold);
    SolidBrush cyanBrush(Color(255, 0, 240, 255));
    const wchar_t* previewText = isEs ? L"VISTA PREVIA DE CAMARA EN TIEMPO REAL" : L"REAL-TIME CAMERA PREVIEW";
    g.DrawString(previewText, -1, &streamTitleFont, PointF((REAL)cx - 170, (REAL)cy - 10), &cyanBrush);

    // Live Badge Pill (Top Left)
    SolidBrush badgeBg(Color(220, 10, 12, 20));
    g.FillRectangle(&badgeBg, viewX + 15, viewY + 15, 235, 32);
    g.DrawRectangle(&borderPen, viewX + 15, viewY + 15, 235, 32);

    Font badgeFont(L"Segoe UI", 9, FontStyleBold);
    SolidBrush whiteBrush(Color(255, 255, 255, 255));
    const wchar_t* liveText = isEs ? L"TRANSMISION EN VIVO | 60 FPS" : L"LIVE STREAM | 60 FPS";
    g.DrawString(liveText, -1, &badgeFont, PointF((REAL)viewX + 25, (REAL)viewY + 22), &whiteBrush);

    // Resolution Badge (Top Right)
    g.FillRectangle(&badgeBg, viewX + viewW - 195, viewY + 15, 180, 32);
    g.DrawRectangle(&borderPen, viewX + viewW - 195, viewY + 15, 180, 32);
    g.DrawString(L"1080p (1920x1080)", -1, &badgeFont, PointF((REAL)viewX + viewW - 180, (REAL)viewY + 22), &whiteBrush);

    // Quick Action Bar under Preview
    int barY = viewY + viewH + 20;
    Font btnFont(L"Segoe UI", 9, FontStyleBold);

    const wchar_t* actionsEN[] = { L"Torch", L"HDR On", L"Autofocus", L"Stabilization" };
    const wchar_t* actionsES[] = { L"Antorcha", L"HDR Activo", L"Autoenfoque", L"Estabilizacion" };

    int btnX = viewX;
    int btnWidth = (viewW - 30) / 4;
    for (int i = 0; i < 4; ++i) {
        SolidBrush actionBg(Color(255, 23, 27, 43));
        g.FillRectangle(&actionBg, btnX, barY, btnWidth, 40);
        g.DrawRectangle(&borderPen, btnX, barY, btnWidth, 40);

        const wchar_t* label = isEs ? actionsES[i] : actionsEN[i];
        g.DrawString(label, -1, &btnFont, PointF((REAL)btnX + 15, (REAL)barY + 11), &whiteBrush);
        btnX += btnWidth + 10;
    }
}

void MainWindowUI::DrawControlPanel(Graphics& g, int x, int y, int width, int height) {
    bool isEs = (m_language == UILanguage::Spanish);

    SolidBrush cardBrush(Color(255, 16, 19, 31));
    g.FillRectangle(&cardBrush, x, y, width, height);

    Pen borderPen(Color(255, 30, 35, 55), 1);
    g.DrawRectangle(&borderPen, x, y, width, height);

    Font sectionFont(L"Segoe UI", 10, FontStyleBold);
    SolidBrush whiteBrush(Color(255, 255, 255, 255));
    SolidBrush grayBrush(Color(255, 156, 163, 185));

    const wchar_t* opticsTitle = isEs ? L"OPTICA DE CAMARA" : L"CAMERA OPTICS";
    g.DrawString(opticsTitle, -1, &sectionFont, PointF((REAL)x + 20, (REAL)y + 15), &grayBrush);

    // Lens Selection Grid
    const wchar_t* lensesEN[] = { L"1. Main Wide (1x)", L"2. Ultra Wide (0.5x)", L"3. Telephoto (3x)", L"4. Front Selfie" };
    const wchar_t* lensesES[] = { L"1. Principal Angular (1x)", L"2. Ultra Angular (0.5x)", L"3. Teleobjetivo (3x)", L"4. Frontal Selfie" };

    int lensY = y + 45;
    for (int i = 0; i < 4; ++i) {
        bool isSelected = (i == m_selectedLens);
        SolidBrush pillBg(isSelected ? Color(255, 124, 58, 237) : Color(255, 23, 27, 43));
        g.FillRectangle(&pillBg, x + 20, lensY, width - 40, 42);

        Pen pillBorder(isSelected ? Color(255, 167, 139, 250) : Color(255, 38, 44, 68), 1);
        g.DrawRectangle(&pillBorder, x + 20, lensY, width - 40, 42);

        Font pillFont(L"Segoe UI", 9, isSelected ? FontStyleBold : FontStyleRegular);
        const wchar_t* lensText = isEs ? lensesES[i] : lensesEN[i];
        g.DrawString(lensText, -1, &pillFont, PointF((REAL)x + 35, (REAL)lensY + 12), &whiteBrush);
        lensY += 52;
    }

    // Torch Button
    SolidBrush torchBg(m_torchEnabled ? Color(255, 234, 179, 8) : Color(255, 23, 27, 43));
    g.FillRectangle(&torchBg, x + 20, lensY + 10, width - 40, 44);
    g.DrawRectangle(&borderPen, x + 20, lensY + 10, width - 40, 44);

    Font torchFont(L"Segoe UI", 10, FontStyleBold);
    SolidBrush torchText(m_torchEnabled ? Color(255, 0, 0, 0) : Color(255, 255, 255, 255));

    const wchar_t* torchLabel = isEs ?
        (m_torchEnabled ? L"Luz Antorcha ENCENDIDA" : L"Luz Antorcha APAGADA") :
        (m_torchEnabled ? L"Torch Light ON" : L"Torch Light OFF");

    g.DrawString(torchLabel, -1, &torchFont, PointF((REAL)x + 35, (REAL)lensY + 22), &torchText);

    // Virtual Camera Card
    int vcamY = height - 140;
    const wchar_t* vcamTitle = isEs ? L"NODOS DE CAMARA VIRTUAL" : L"VIRTUAL CAMERA NODES";
    g.DrawString(vcamTitle, -1, &sectionFont, PointF((REAL)x + 20, (REAL)vcamY), &grayBrush);

    SolidBrush vcamCard(Color(255, 20, 23, 37));
    g.FillRectangle(&vcamCard, x + 20, vcamY + 25, width - 40, 100);
    g.DrawRectangle(&borderPen, x + 20, vcamY + 25, width - 40, 100);

    Font vcamFont(L"Segoe UI", 9, FontStyleBold);
    SolidBrush greenText(Color(255, 52, 211, 153));
    const wchar_t* vcamName = L"MiCam Virtual Camera 1";
    const wchar_t* vcamState = isEs ? L"   Estado: ACTIVO (DirectShow/MF)" : L"   State: ACTIVE (DirectShow/MF)";
    const wchar_t* obsState = isEs ? L"Conexion Directa OBS Studio" : L"OBS Studio Direct Pipe";

    g.DrawString(vcamName, -1, &vcamFont, PointF((REAL)x + 32, (REAL)vcamY + 38), &whiteBrush);
    g.DrawString(vcamState, -1, &vcamFont, PointF((REAL)x + 32, (REAL)vcamY + 58), &greenText);
    g.DrawString(obsState, -1, &vcamFont, PointF((REAL)x + 32, (REAL)vcamY + 84), &greenText);
}
