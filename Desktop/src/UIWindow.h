#ifndef MICAM_DESKTOP_UIWINDOW_H
#define MICAM_DESKTOP_UIWINDOW_H

#include <winsock2.h> // Must precede windows.h - see DeviceRegistry.h comment
#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <functional>
#include "DeviceManager.h"

#pragma comment(lib, "gdiplus.lib")

enum class UILanguage {
    English,
    Spanish
};

// A clickable rectangle registered by whatever Draw* method last painted it, and consumed by
// WM_LBUTTONDOWN. This is the single source of truth for "where is this button" - drawing and
// hit-testing can never drift out of sync with each other, which is what caused every button
// in the previous implementation to silently miss clicks.
struct ClickRegion {
    RECT rect;
    std::function<void()> onClick;
};

class MainWindowUI {
public:
    MainWindowUI(HINSTANCE hInstance);
    ~MainWindowUI();

    bool CreateAndShow(int nCmdShow);
    void RunMessageLoop();

    void UpdateDeviceList(const std::vector<ConnectedDevice>& devices);

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    void OnPaint(HWND hwnd);
    void DrawSidebar(Gdiplus::Graphics& g, int x, int width, int height);
    void DrawDeviceCard(Gdiplus::Graphics& g, const ConnectedDevice& dev, int x, int y, int width, bool isSelected);
    void DrawMainPreview(Gdiplus::Graphics& g, int x, int y, int width, int height);
    void DrawControlPanel(Gdiplus::Graphics& g, int x, int y, int width, int height);

    // Registers `rect` as clickable for this frame, invoking `onClick` on WM_LBUTTONDOWN.
    void AddClickRegion(const Gdiplus::RectF& rect, std::function<void()> onClick);

    void InstallObsPlugin(HWND hwnd);
    void RequestElevatedObsInstall(HWND hwnd);
    void OnSwitchToWifiClicked(const std::string& deviceId);
    void OnLensClicked(int lensIndex);
    void OnTorchClicked();

    HINSTANCE m_hInstance;
    HWND m_hwnd{NULL};
    ULONG_PTR m_gdiplusToken;

    DeviceManager m_deviceManager;
    std::vector<ConnectedDevice> m_devices;
    std::string m_selectedDeviceId;

    Gdiplus::Bitmap* m_pLogoBitmap{nullptr};

    std::vector<ClickRegion> m_clickRegions;

    // Camera State Controls & Language Toggle
    UILanguage m_language{UILanguage::Spanish}; // Default to Spanish for user
    int m_selectedLens{0}; // 0: Wide, 1: UltraWide, 2: Telephoto, 3: Front
    float m_zoomFactor{1.0f};
    bool m_torchEnabled{false};
};

#endif // MICAM_DESKTOP_UIWINDOW_H
