#include "UIWindow.h"
#include <shlobj.h>
#include <string>
#include <vector>

using namespace Gdiplus;

namespace {
constexpr int kHeaderHeight = 65;
constexpr int kSidebarWidth = 340;
constexpr int kControlPanelWidth = 300;

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], len);
    return out;
}
} // namespace

MainWindowUI::MainWindowUI(HINSTANCE hInstance)
    : m_hInstance(hInstance)
{
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, NULL);

    m_pLogoBitmap = Bitmap::FromFile(L"assets/micam_logo.jpg");
}

MainWindowUI::~MainWindowUI() {
    if (m_pLogoBitmap) delete m_pLogoBitmap;
    GdiplusShutdown(m_gdiplusToken);
}

bool MainWindowUI::CreateAndShow(int nCmdShow) {
    // Keeps GetClientRect()/mouse coordinates in the same space the UI is drawn in on
    // high-DPI displays, so click hit-testing can't drift from what's on screen.
    SetProcessDPIAware();

    // Resource 101 in resource.rc (assets/micam_icon.ico) was compiled into the exe but never
    // actually loaded/assigned - Windows fell back to a generic blank icon for the taskbar
    // button and Alt-Tab entry.
    HICON hAppIcon = LoadIconW(m_hInstance, MAKEINTRESOURCEW(101));

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWindowUI::WindowProc;
    wc.hInstance = m_hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"MiCamProStudioWindowClass";
    wc.hIcon = hAppIcon;
    wc.hIconSm = hAppIcon;

    RegisterClassExW(&wc);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int winW = 1360;
    int winH = 860;
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

    // Nothing previously invalidated the window when a new decoded video frame arrived - the
    // preview only ever repainted on device-list changes (~every 2s) or clicks. ~15fps is
    // plenty for a preview panel without repainting the whole window unnecessarily often.
    SetTimer(m_hwnd, 1, 66, NULL);

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
    if (m_selectedDeviceId.empty() && !m_devices.empty()) {
        m_selectedDeviceId = m_devices.front().id;
    }
    if (m_hwnd) {
        // Marshal back onto the UI thread - the callback fires from DeviceManager's
        // discovery thread.
        InvalidateRect(m_hwnd, NULL, FALSE);
    }
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
        // Click regions were registered fresh during the last OnPaint - the very structure
        // that was drawn is what gets hit-tested, so drawing and clicking can never disagree.
        for (const auto& region : m_clickRegions) {
            if (x >= region.rect.left && x <= region.rect.right && y >= region.rect.top && y <= region.rect.bottom) {
                if (region.onClick) region.onClick();
                break;
            }
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_TIMER:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        m_deviceManager.StopDiscovery();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void MainWindowUI::AddClickRegion(const RectF& rect, std::function<void()> onClick) {
    RECT r;
    r.left = (LONG)rect.X;
    r.top = (LONG)rect.Y;
    r.right = (LONG)(rect.X + rect.Width);
    r.bottom = (LONG)(rect.Y + rect.Height);
    m_clickRegions.push_back({ r, std::move(onClick) });
}

void MainWindowUI::OnLensClicked(int lensIndex) {
    m_selectedLens = lensIndex;
    if (m_selectedDeviceId.empty()) return;

    MiCamControlCommand cmd{};
    cmd.lens = static_cast<uint8_t>(m_selectedLens);
    cmd.targetWidth = 1920;
    cmd.targetHeight = 1080;
    cmd.targetFps = 60;
    cmd.zoomFactor = m_zoomFactor;
    cmd.torchEnabled = m_torchEnabled ? 1 : 0;
    m_deviceManager.SendControlCommand(m_selectedDeviceId, cmd);
    InvalidateRect(m_hwnd, NULL, FALSE);
}

void MainWindowUI::OnTorchClicked() {
    m_torchEnabled = !m_torchEnabled;
    if (!m_selectedDeviceId.empty()) {
        MiCamControlCommand cmd{};
        cmd.lens = static_cast<uint8_t>(m_selectedLens);
        cmd.torchEnabled = m_torchEnabled ? 1 : 0;
        m_deviceManager.SendControlCommand(m_selectedDeviceId, cmd);
    }
    InvalidateRect(m_hwnd, NULL, FALSE);
}

void MainWindowUI::OnSwitchToWifiClicked(const std::string& deviceId) {
    bool isEs = (m_language == UILanguage::Spanish);
    bool ok = m_deviceManager.SwitchToWifi(deviceId);
    if (!ok) {
        MessageBoxW(m_hwnd,
            isEs ? L"Aún no se detecta este iPhone por WiFi. Asegúrate de que esté conectado a la misma red WiFi que este PC y espera unos segundos a que aparezca." :
                   L"This iPhone hasn't been detected over WiFi yet. Make sure it's on the same WiFi network as this PC and wait a few seconds for it to appear.",
            L"MiCam Studio Pro", MB_OK | MB_ICONINFORMATION);
    }
}

void MainWindowUI::InstallObsPlugin(HWND hwnd) {
    bool isEs = (m_language == UILanguage::Spanish);

    wchar_t exePathBuf[MAX_PATH];
    GetModuleFileNameW(NULL, exePathBuf, MAX_PATH);
    std::wstring exePath(exePathBuf);
    std::wstring exeDir = exePath.substr(0, exePath.find_last_of(L"\\/"));

    std::wstring pluginSrc = exeDir + L"\\micam-obs-plugin.dll";
    std::wstring vcamSrc = exeDir + L"\\MiCamVirtualCamera.dll";

    if (GetFileAttributesW(pluginSrc.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(hwnd,
            isEs ? L"No se encontró micam-obs-plugin.dll junto a MiCamDesktop.exe.\n\nCompila el proyecto (CMake + MSBuild) antes de instalar el plugin." :
                   L"micam-obs-plugin.dll wasn't found next to MiCamDesktop.exe.\n\nBuild the project (CMake + MSBuild) before installing the plugin.",
            L"MiCam OBS Installer", MB_OK | MB_ICONWARNING);
        return;
    }

    const wchar_t* candidates[] = {
        L"C:\\Program Files\\obs-studio\\obs-plugins\\64bit",
        L"C:\\Program Files (x86)\\obs-studio\\obs-plugins\\64bit"
    };
    std::wstring obsPluginsDir;
    for (const wchar_t* c : candidates) {
        if (GetFileAttributesW(c) != INVALID_FILE_ATTRIBUTES) { obsPluginsDir = c; break; }
    }

    if (obsPluginsDir.empty()) {
        MessageBoxW(hwnd,
            isEs ? L"No se encontró OBS Studio instalado en las rutas estándar.\n\nInstala OBS Studio (obsproject.com) e inténtalo de nuevo." :
                   L"OBS Studio wasn't found in the standard install locations.\n\nInstall OBS Studio (obsproject.com) and try again.",
            L"MiCam OBS Installer", MB_OK | MB_ICONWARNING);
        return;
    }

    std::wstring pluginDest = obsPluginsDir + L"\\micam-obs-plugin.dll";
    BOOL copiedPlugin = CopyFileW(pluginSrc.c_str(), pluginDest.c_str(), FALSE);
    bool accessDenied = !copiedPlugin && GetLastError() == ERROR_ACCESS_DENIED;

    bool vcamRegistered = true;
    if (!accessDenied && GetFileAttributesW(vcamSrc.c_str()) != INVALID_FILE_ATTRIBUTES) {
        HMODULE hDll = LoadLibraryW(vcamSrc.c_str());
        if (hDll) {
            typedef HRESULT(STDAPICALLTYPE* pfnDllRegisterServer)();
            auto pRegister = (pfnDllRegisterServer)GetProcAddress(hDll, "DllRegisterServer");
            if (pRegister) {
                HRESULT hr = pRegister();
                vcamRegistered = SUCCEEDED(hr);
                if (hr == E_ACCESSDENIED) accessDenied = true;
            }
            FreeLibrary(hDll);
        }
    }

    if (accessDenied) {
        RequestElevatedObsInstall(hwnd);
        return;
    }

    if (copiedPlugin) {
        MessageBoxW(hwnd,
            isEs ? L"Plugin 'MiCam OBS' instalado correctamente.\n\nReinicia OBS Studio y búscalo en el menú de agregar fuentes (+) como 'MiCam OBS'." :
                   L"'MiCam OBS' plugin installed successfully.\n\nRestart OBS Studio and look for 'MiCam OBS' in the add-source (+) menu.",
            L"MiCam OBS Installer", MB_OK | MB_ICONINFORMATION);
    } else {
        wchar_t msg[512];
        _snwprintf_s(msg, _TRUNCATE,
            isEs ? L"No se pudo copiar el plugin a OBS (error %lu). Cierra OBS Studio si está abierto e inténtalo de nuevo." :
                   L"Couldn't copy the plugin into OBS (error %lu). Close OBS Studio if it's running and try again.",
            GetLastError());
        MessageBoxW(hwnd, msg, L"MiCam OBS Installer", MB_OK | MB_ICONWARNING);
    }
}

void MainWindowUI::RequestElevatedObsInstall(HWND hwnd) {
    bool isEs = (m_language == UILanguage::Spanish);

    wchar_t exePathBuf[MAX_PATH];
    GetModuleFileNameW(NULL, exePathBuf, MAX_PATH);

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = hwnd;
    sei.lpVerb = L"runas";
    sei.lpFile = exePathBuf;
    sei.lpParameters = L"--install-obs-plugin";
    sei.nShow = SW_HIDE;

    if (!ShellExecuteExW(&sei) || !sei.hProcess) {
        MessageBoxW(hwnd,
            isEs ? L"Instalar el plugin requiere permisos de administrador (copia archivos en Program Files y registra la cámara virtual) y no se pudo elevar el proceso." :
                   L"Installing the plugin needs administrator rights (it copies files into Program Files and registers the virtual camera) and elevation was denied.",
            L"MiCam OBS Installer", MB_OK | MB_ICONERROR);
        return;
    }

    WaitForSingleObject(sei.hProcess, 15000);
    DWORD exitCode = 1;
    GetExitCodeProcess(sei.hProcess, &exitCode);
    CloseHandle(sei.hProcess);

    if (exitCode == 0) {
        MessageBoxW(hwnd,
            isEs ? L"Plugin 'MiCam OBS' instalado correctamente con permisos de administrador.\n\nReinicia OBS Studio." :
                   L"'MiCam OBS' plugin installed successfully with administrator rights.\n\nRestart OBS Studio.",
            L"MiCam OBS Installer", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(hwnd,
            isEs ? L"La instalación elevada no se completó correctamente." :
                   L"The elevated installation didn't complete successfully.",
            L"MiCam OBS Installer", MB_OK | MB_ICONWARNING);
    }
}

void MainWindowUI::OnPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT rc;
    GetClientRect(hwnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    m_clickRegions.clear();

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
    g.FillRectangle(&topBarBrush, 0, 0, width, kHeaderHeight);
    Pen borderPen(Color(255, 30, 35, 55), 1);
    g.DrawLine(&borderPen, 0, kHeaderHeight, width, kHeaderHeight);

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
    const wchar_t* subTitleText = isEs ? L"SUITE DE CAMARA MULTI-DISPOSITIVO" : L"MULTI-DEVICE CAMERA SUITE";
    g.DrawString(subTitleText, -1, &versionFont, PointF(75, 36), &cyanBrush);

    // Live device count badge (real, driven by m_devices)
    SolidBrush metricCard(Color(255, 22, 26, 42));
    g.FillRectangle(&metricCard, width - 480, 15, 320, 36);
    g.DrawRectangle(&borderPen, width - 480, 15, 320, 36);

    Font metricFont(L"Segoe UI", 9, FontStyleRegular);
    SolidBrush grayBrush(Color(255, 156, 163, 185));
    int connectedCount = 0;
    for (auto& d : m_devices) if (d.isConnected) connectedCount++;
    wchar_t metricText[128];
    _snwprintf_s(metricText, _TRUNCATE,
        isEs ? L"Dispositivos conectados: %d" : L"Connected devices: %d", connectedCount);
    g.DrawString(metricText, -1, &metricFont, PointF((REAL)width - 465, 24), &grayBrush);

    // Language Selector Button [ ES ] / [ EN ] (Top Right)
    RectF langRect((REAL)width - 110, 15, 95, 36);
    SolidBrush langBg(Color(255, 124, 58, 237));
    g.FillRectangle(&langBg, langRect);
    Pen langBorder(Color(255, 167, 139, 250), 1);
    g.DrawRectangle(&langBorder, langRect);

    Font langFont(L"Segoe UI", 9, FontStyleBold);
    const wchar_t* langLabel = isEs ? L"ESPAÑOL" : L"ENGLISH";
    g.DrawString(langLabel, -1, &langFont, PointF((REAL)width - 98, 24), &whiteBrush);
    AddClickRegion(langRect, [this]() {
        m_language = (m_language == UILanguage::Spanish) ? UILanguage::English : UILanguage::Spanish;
        InvalidateRect(m_hwnd, NULL, FALSE);
    });

    int mainX = kSidebarWidth + 10;
    int mainW = width - kSidebarWidth - kControlPanelWidth - 20;
    int panelY = kHeaderHeight + 10;
    int panelH = height - kHeaderHeight - 20;

    DrawSidebar(g, 0, kSidebarWidth, height);
    DrawMainPreview(g, mainX, panelY, mainW, panelH);
    DrawControlPanel(g, width - kControlPanelWidth, panelY, kControlPanelWidth - 15, panelH);

    Graphics windowGraphics(hdc);
    windowGraphics.DrawImage(&backBuffer, 0, 0);
    EndPaint(hwnd, &ps);
}

void MainWindowUI::DrawSidebar(Graphics& g, int x, int width, int height) {
    bool isEs = (m_language == UILanguage::Spanish);
    int startY = kHeaderHeight + 10;

    SolidBrush cardBrush(Color(255, 16, 19, 31));
    g.FillRectangle(&cardBrush, x + 15, startY, width - 25, height - startY - 15);

    Pen borderPen(Color(255, 30, 35, 55), 1);
    g.DrawRectangle(&borderPen, x + 15, startY, width - 25, height - startY - 15);

    Font sectionFont(L"Segoe UI", 10, FontStyleBold);
    SolidBrush grayBrush(Color(255, 156, 163, 185));

    const wchar_t* devTitle = isEs ? L"DISPOSITIVOS ACTIVOS" : L"ACTIVE DEVICES";
    g.DrawString(devTitle, -1, &sectionFont, PointF((REAL)x + 30, (REAL)startY + 15), &grayBrush);

    int cardY = startY + 45;
    const int cardHeight = 108;
    const int cardSpacing = 12;
    const int maxVisible = (std::max)(1, (height - startY - 60) / (cardHeight + cardSpacing));

    if (m_devices.empty()) {
        Font emptyFont(L"Segoe UI", 9, FontStyleRegular);
        const wchar_t* emptyText = isEs ?
            L"Sin dispositivos.\n\nConecta un iPhone por USB\ncon MiCam Pro abierto,\no en la misma red WiFi." :
            L"No devices yet.\n\nPlug in an iPhone via USB\nwith MiCam Pro open,\nor join the same WiFi network.";
        g.DrawString(emptyText, -1, &emptyFont, PointF((REAL)x + 30, (REAL)cardY + 10), &grayBrush);
    } else {
        int shown = 0;
        for (const auto& dev : m_devices) {
            if (shown >= maxVisible) break;
            bool isSelected = (dev.id == m_selectedDeviceId);
            DrawDeviceCard(g, dev, x + 28, cardY, width - 50, isSelected);
            cardY += cardHeight + cardSpacing;
            shown++;
        }
        if ((int)m_devices.size() > maxVisible) {
            Font moreFont(L"Segoe UI", 8, FontStyleRegular);
            wchar_t moreText[64];
            _snwprintf_s(moreText, _TRUNCATE,
                isEs ? L"+ %d más (agranda la ventana)" : L"+ %d more (resize the window)",
                (int)m_devices.size() - maxVisible);
            g.DrawString(moreText, -1, &moreFont, PointF((REAL)x + 30, (REAL)cardY + 4), &grayBrush);
        }
    }
}

void MainWindowUI::DrawDeviceCard(Graphics& g, const ConnectedDevice& dev, int x, int y, int width, bool isSelected) {
    bool isEs = (m_language == UILanguage::Spanish);
    const int cardHeight = 108;

    SolidBrush devCard(Color(255, 23, 27, 43));
    RectF cardRect((REAL)x, (REAL)y, (REAL)width, (REAL)cardHeight);
    g.FillRectangle(&devCard, cardRect);

    Pen cardBorder(isSelected ? Color(255, 0, 240, 255) : Color(255, 38, 44, 68), isSelected ? 2.0f : 1.0f);
    g.DrawRectangle(&cardBorder, cardRect);

    // Whole card selects this device as the target for lens/torch/zoom commands.
    std::string deviceId = dev.id;
    AddClickRegion(RectF((REAL)x, (REAL)y, (REAL)(width - 130), 40), [this, deviceId]() {
        m_selectedDeviceId = deviceId;
        InvalidateRect(m_hwnd, NULL, FALSE);
    });

    Font devNameFont(L"Segoe UI", 11, FontStyleBold);
    SolidBrush whiteBrush(Color(255, 255, 255, 255));
    std::wstring nameW = Utf8ToWide(dev.name.empty() ? "iPhone" : dev.name);
    g.DrawString(nameW.c_str(), -1, &devNameFont, PointF((REAL)x + 14, (REAL)y + 12), &whiteBrush);

    Color statusColor = dev.isConnected
        ? (dev.connectionType.find("USB") != std::string::npos ? Color(255, 16, 185, 129) : Color(255, 0, 240, 255))
        : Color(255, 100, 100, 110);
    SolidBrush statusDot(statusColor);
    g.FillEllipse(&statusDot, (REAL)(x + 14), (REAL)(y + 38), (REAL)8, (REAL)8);

    Font statusFont(L"Segoe UI", 8, FontStyleBold);
    SolidBrush statusText(statusColor);
    std::wstring connW = Utf8ToWide(dev.connectionType);
    const wchar_t* stateLabel = dev.isConnected ? (isEs ? L"CONECTADO" : L"CONNECTED") : (isEs ? L"DESCONECTADO" : L"DISCONNECTED");
    wchar_t statusLine[128];
    _snwprintf_s(statusLine, _TRUNCATE, L"%s (%s)", stateLabel, connW.c_str());
    g.DrawString(statusLine, -1, &statusFont, PointF((REAL)x + 27, (REAL)y + 35), &statusText);

    Font detailFont(L"Segoe UI", 8, FontStyleRegular);
    SolidBrush grayBrush(Color(255, 156, 163, 185));
    if (dev.handshakeDone) {
        std::wstring modelW = Utf8ToWide(dev.model.empty() ? "iPhone" : dev.model);
        wchar_t batteryLine[96];
        _snwprintf_s(batteryLine, _TRUNCATE, isEs ? L"%s | Bateria: %d%%%s" : L"%s | Battery: %d%%%s",
            modelW.c_str(), (int)dev.batteryLevel, dev.isCharging ? (isEs ? L" (Cargando)" : L" (Charging)") : L"");
        g.DrawString(batteryLine, -1, &detailFont, PointF((REAL)x + 14, (REAL)y + 56), &grayBrush);
    } else {
        g.DrawString(isEs ? L"Identificando dispositivo..." : L"Identifying device...", -1, &detailFont, PointF((REAL)x + 14, (REAL)y + 56), &grayBrush);
    }

    // Switch to WiFi button - only meaningfully enabled once mDNS has actually seen this
    // device on the same network; otherwise clicking it explains why instead of silently failing.
    RectF wifiBtnRect((REAL)x + 14, (REAL)y + 76, (REAL)width - 28, 24);
    bool isUsbNow = dev.connectionType.find("USB") != std::string::npos;
    bool showButton = isUsbNow; // Already-WiFi devices don't need the button
    if (showButton) {
        SolidBrush wifiBtnBg(dev.wifiAvailable ? Color(255, 124, 58, 237) : Color(255, 30, 34, 50));
        g.FillRectangle(&wifiBtnBg, wifiBtnRect);
        Pen wifiBtnBorder(dev.wifiAvailable ? Color(255, 167, 139, 250) : Color(255, 45, 50, 70), 1);
        g.DrawRectangle(&wifiBtnBorder, wifiBtnRect);

        Font wifiBtnFont(L"Segoe UI", 8, FontStyleBold);
        SolidBrush wifiBtnText(dev.wifiAvailable ? Color(255, 255, 255, 255) : Color(255, 120, 125, 145));
        const wchar_t* wifiLabel = dev.wifiAvailable
            ? (isEs ? L"Cambiar a WiFi" : L"Switch to WiFi")
            : (isEs ? L"WiFi no detectado aún" : L"WiFi not detected yet");
        g.DrawString(wifiLabel, -1, &wifiBtnFont, PointF((REAL)x + 24, (REAL)y + 81), &wifiBtnText);

        std::string deviceIdForWifi = dev.id;
        AddClickRegion(wifiBtnRect, [this, deviceIdForWifi]() { OnSwitchToWifiClicked(deviceIdForWifi); });
    }
}

void MainWindowUI::DrawMainPreview(Graphics& g, int x, int y, int width, int height) {
    bool isEs = (m_language == UILanguage::Spanish);

    SolidBrush cardBrush(Color(255, 16, 19, 31));
    g.FillRectangle(&cardBrush, x, y, width, height);

    Pen borderPen(Color(255, 30, 35, 55), 1);
    g.DrawRectangle(&borderPen, x, y, width, height);

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

    const ConnectedDevice* selected = nullptr;
    for (auto& d : m_devices) if (d.id == m_selectedDeviceId) { selected = &d; break; }

    Font streamTitleFont(L"Segoe UI", 13, FontStyleBold);
    SolidBrush cyanBrush(Color(255, 0, 240, 255));
    SolidBrush grayBrush(Color(255, 156, 163, 185));
    int cx = viewX + viewW / 2;
    int cy = viewY + viewH / 2;

    bool drewFrame = false;
    if (selected && selected->isConnected) {
        std::vector<uint8_t> rgba;
        uint32_t frameW = 0, frameH = 0;
        if (m_deviceManager.GetLatestFrame(selected->id, rgba, frameW, frameH) && frameW > 0 && frameH > 0) {
            // Decoded frames are stored RGBA (matches what the OBS plugin declares via
            // VIDEO_FORMAT_RGBA), but GDI+'s 32bppARGB format is byte-order BGRA in memory -
            // swap R/B into a scratch buffer rather than mutating the shared decode buffer.
            std::vector<uint8_t> bgra(rgba.size());
            for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
                bgra[i + 0] = rgba[i + 2];
                bgra[i + 1] = rgba[i + 1];
                bgra[i + 2] = rgba[i + 0];
                bgra[i + 3] = rgba[i + 3];
            }

            Bitmap frameBitmap(frameW, frameH, frameW * 4, PixelFormat32bppARGB, bgra.data());
            if (frameBitmap.GetLastStatus() == Ok) {
                g.DrawImage(&frameBitmap, viewX, viewY, viewW, viewH);
                drewFrame = true;
            }
        }
    }

    if (!selected || !selected->isConnected) {
        const wchar_t* noDeviceText = isEs ? L"NINGUN DISPOSITIVO SELECCIONADO" : L"NO DEVICE SELECTED";
        g.DrawString(noDeviceText, -1, &streamTitleFont, PointF((REAL)cx - 160, (REAL)cy - 20), &grayBrush);
        Font subFont(L"Segoe UI", 9, FontStyleRegular);
        const wchar_t* subText = isEs ? L"Conecta un iPhone y selecciona su tarjeta en el panel izquierdo" : L"Connect an iPhone and select its card in the left panel";
        g.DrawString(subText, -1, &subFont, PointF((REAL)cx - 195, (REAL)cy + 8), &grayBrush);
    } else if (!drewFrame) {
        std::wstring nameW = Utf8ToWide(selected->name);
        wchar_t titleLine[192];
        _snwprintf_s(titleLine, _TRUNCATE, isEs ? L"%s - CONECTADO" : L"%s - CONNECTED", nameW.c_str());
        g.DrawString(titleLine, -1, &streamTitleFont, PointF((REAL)cx - 150, (REAL)cy - 30), &cyanBrush);

        Font subFont(L"Segoe UI", 9, FontStyleRegular);
        const wchar_t* subText = isEs ?
            L"Esperando primer fotograma de video..." :
            L"Waiting for the first video frame...";
        g.DrawString(subText, -1, &subFont, PointF((REAL)cx - 130, (REAL)cy), &grayBrush);
    }

    // Live Badge Pill (Top Left) - reflects real selection state, not a hardcoded "LIVE".
    SolidBrush badgeBg(Color(220, 10, 12, 20));
    g.FillRectangle(&badgeBg, viewX + 15, viewY + 15, 235, 32);
    g.DrawRectangle(&borderPen, viewX + 15, viewY + 15, 235, 32);
    Font badgeFont(L"Segoe UI", 9, FontStyleBold);
    SolidBrush whiteBrush(Color(255, 255, 255, 255));
    const wchar_t* badgeText = drewFrame
        ? (isEs ? L"EN VIVO" : L"LIVE")
        : (selected && selected->isConnected)
            ? (isEs ? L"DISPOSITIVO CONECTADO" : L"DEVICE CONNECTED")
            : (isEs ? L"SIN SEÑAL" : L"NO SIGNAL");
    g.DrawString(badgeText, -1, &badgeFont, PointF((REAL)viewX + 25, (REAL)viewY + 22), &whiteBrush);

    // Quick Action Bar under Preview
    int barY = viewY + viewH + 20;
    Font btnFont(L"Segoe UI", 9, FontStyleBold);
    const wchar_t* actionsEN[] = { L"Torch", L"HDR", L"Autofocus", L"Stabilization" };
    const wchar_t* actionsES[] = { L"Antorcha", L"HDR", L"Autoenfoque", L"Estabilizacion" };

    int btnX = viewX;
    int btnWidth = (viewW - 30) / 4;
    for (int i = 0; i < 4; ++i) {
        RectF btnRect((REAL)btnX, (REAL)barY, (REAL)btnWidth, 40);
        bool isTorchBtn = (i == 0);
        SolidBrush actionBg((isTorchBtn && m_torchEnabled) ? Color(255, 234, 179, 8) : Color(255, 23, 27, 43));
        g.FillRectangle(&actionBg, btnRect);
        g.DrawRectangle(&borderPen, btnRect);

        const wchar_t* label = isEs ? actionsES[i] : actionsEN[i];
        SolidBrush lblBrush((isTorchBtn && m_torchEnabled) ? Color(255, 0, 0, 0) : Color(255, 255, 255, 255));
        g.DrawString(label, -1, &btnFont, PointF((REAL)btnX + 15, (REAL)barY + 11), &lblBrush);

        if (isTorchBtn) {
            AddClickRegion(btnRect, [this]() { OnTorchClicked(); });
        }
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

    if (m_selectedDeviceId.empty()) {
        Font hintFont(L"Segoe UI", 8, FontStyleRegular);
        g.DrawString(isEs ? L"(Selecciona un dispositivo)" : L"(Select a device)", -1, &hintFont, PointF((REAL)x + 20, (REAL)y + 32), &grayBrush);
    }

    const wchar_t* lensesEN[] = { L"1. Main Wide (1x)", L"2. Ultra Wide (0.5x)", L"3. Telephoto (3x)", L"4. Front Selfie" };
    const wchar_t* lensesES[] = { L"1. Principal Angular (1x)", L"2. Ultra Angular (0.5x)", L"3. Teleobjetivo (3x)", L"4. Frontal Selfie" };

    int lensY = y + 46;
    for (int i = 0; i < 4; ++i) {
        bool isSelected = (i == m_selectedLens);
        RectF lensRect((REAL)x + 20, (REAL)lensY, (REAL)width - 40, 40);
        SolidBrush pillBg(isSelected ? Color(255, 124, 58, 237) : Color(255, 23, 27, 43));
        g.FillRectangle(&pillBg, lensRect);

        Pen pillBorder(isSelected ? Color(255, 167, 139, 250) : Color(255, 38, 44, 68), 1);
        g.DrawRectangle(&pillBorder, lensRect);

        Font pillFont(L"Segoe UI", 9, isSelected ? FontStyleBold : FontStyleRegular);
        const wchar_t* lensText = isEs ? lensesES[i] : lensesEN[i];
        g.DrawString(lensText, -1, &pillFont, PointF((REAL)x + 35, (REAL)lensY + 11), &whiteBrush);

        AddClickRegion(lensRect, [this, i]() { OnLensClicked(i); });
        lensY += 48;
    }

    RectF torchRect((REAL)x + 20, (REAL)lensY + 8, (REAL)width - 40, 40);
    SolidBrush torchBg(m_torchEnabled ? Color(255, 234, 179, 8) : Color(255, 23, 27, 43));
    g.FillRectangle(&torchBg, torchRect);
    g.DrawRectangle(&borderPen, torchRect);

    Font torchFont(L"Segoe UI", 9, FontStyleBold);
    SolidBrush torchText(m_torchEnabled ? Color(255, 0, 0, 0) : Color(255, 255, 255, 255));
    const wchar_t* torchLabel = isEs ?
        (m_torchEnabled ? L"Luz Antorcha ENCENDIDA" : L"Luz Antorcha APAGADA") :
        (m_torchEnabled ? L"Torch Light ON" : L"Torch Light OFF");
    g.DrawString(torchLabel, -1, &torchFont, PointF((REAL)x + 35, (REAL)lensY + 20), &torchText);
    AddClickRegion(torchRect, [this]() { OnTorchClicked(); });

    // Virtual Camera / OBS Nodes Card - lists real connected devices, not fake entries.
    int vcamY = lensY + 60;
    const wchar_t* vcamTitle = isEs ? L"NODOS DE CAMARA VIRTUAL" : L"VIRTUAL CAMERA NODES";
    g.DrawString(vcamTitle, -1, &sectionFont, PointF((REAL)x + 20, (REAL)vcamY), &grayBrush);

    int vcamCardHeight = height - (vcamY - y) - 70;
    if (vcamCardHeight < 40) vcamCardHeight = 40;
    SolidBrush vcamCard(Color(255, 20, 23, 37));
    g.FillRectangle(&vcamCard, x + 20, vcamY + 25, width - 40, vcamCardHeight);
    g.DrawRectangle(&borderPen, x + 20, vcamY + 25, width - 40, vcamCardHeight);

    Font vcamFont(L"Segoe UI", 8, FontStyleBold);
    SolidBrush greenText(Color(255, 52, 211, 153));

    if (m_devices.empty()) {
        Font emptyFont(L"Segoe UI", 8, FontStyleRegular);
        g.DrawString(isEs ? L"Sin dispositivos activos." : L"No active devices.", -1, &emptyFont, PointF((REAL)x + 32, (REAL)vcamY + 36), &grayBrush);
    } else {
        int lineY = vcamY + 32;
        int lineIdx = 0;
        for (auto& dev : m_devices) {
            if (lineY > vcamY + 25 + vcamCardHeight - 34) break;
            std::wstring nameW = Utf8ToWide(dev.name);
            wchar_t line1[128];
            _snwprintf_s(line1, _TRUNCATE, L"* MiCam Virtual Camera %d (%s)", lineIdx + 1, nameW.c_str());
            g.DrawString(line1, -1, &vcamFont, PointF((REAL)x + 32, (REAL)lineY), &whiteBrush);

            const wchar_t* stateLabel = dev.isConnected ? (isEs ? L"ACTIVO" : L"ACTIVE") : (isEs ? L"INACTIVO" : L"INACTIVE");
            wchar_t line2[64];
            _snwprintf_s(line2, _TRUNCATE, isEs ? L"   Estado: %s" : L"   State: %s", stateLabel);
            g.DrawString(line2, -1, &vcamFont, PointF((REAL)x + 32, (REAL)lineY + 16), dev.isConnected ? &greenText : &grayBrush);

            lineY += 36;
            lineIdx++;
        }
    }

    // One-Click OBS Plugin Installer Button
    int obsBtnY = y + height - 46;
    RectF obsBtnRect((REAL)x + 20, (REAL)obsBtnY, (REAL)width - 40, 36);
    SolidBrush obsBtnBg(Color(255, 124, 58, 237));
    g.FillRectangle(&obsBtnBg, obsBtnRect);
    Pen obsBtnBorder(Color(255, 167, 139, 250), 1);
    g.DrawRectangle(&obsBtnBorder, obsBtnRect);

    Font obsBtnFont(L"Segoe UI", 9, FontStyleBold);
    const wchar_t* obsBtnText = isEs ? L"⚡ INSTALAR PLUGIN OBS STUDIO" : L"⚡ INSTALL OBS STUDIO PLUGIN";
    g.DrawString(obsBtnText, -1, &obsBtnFont, PointF((REAL)x + 35, (REAL)obsBtnY + 9), &whiteBrush);

    HWND capturedHwnd = m_hwnd;
    AddClickRegion(obsBtnRect, [this, capturedHwnd]() { InstallObsPlugin(capturedHwnd); });
}
