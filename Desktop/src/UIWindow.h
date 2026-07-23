#ifndef MICAM_DESKTOP_UIWINDOW_H
#define MICAM_DESKTOP_UIWINDOW_H

#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include "DeviceManager.h"
#include "SharedMemoryStream.h"

#pragma comment(lib, "gdiplus.lib")

enum class UILanguage {
    English,
    Spanish
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
    void DrawSidebar(Gdiplus::Graphics& g, int width, int height);
    void DrawMainPreview(Gdiplus::Graphics& g, int x, int y, int width, int height);
    void DrawControlPanel(Gdiplus::Graphics& g, int x, int y, int width, int height);

    HINSTANCE m_hInstance;
    HWND m_hwnd{NULL};
    ULONG_PTR m_gdiplusToken;

    DeviceManager m_deviceManager;
    SharedMemoryStreamWriter m_streamWriter;
    std::vector<ConnectedDevice> m_devices;

    Gdiplus::Bitmap* m_pLogoBitmap{nullptr};

    // Camera State Controls & Language Toggle
    UILanguage m_language{UILanguage::Spanish}; // Default to Spanish for user
    int m_selectedLens{0}; // 0: Wide, 1: UltraWide, 2: Telephoto, 3: Front
    float m_zoomFactor{1.0f};
    bool m_torchEnabled{false};
};

#endif // MICAM_DESKTOP_UIWINDOW_H
