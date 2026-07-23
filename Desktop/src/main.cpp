#include <winsock2.h> // Must precede windows.h - see DeviceRegistry.h comment
#include <windows.h>
#include <shlwapi.h>
#include <string>
#include "UIWindow.h"

#pragma comment(lib, "shlwapi.lib")

namespace {

// Runs with no UI, invoked via ShellExecute("runas", ...) by MainWindowUI::RequestElevatedObsInstall
// when the normal (non-admin) install attempt hits ERROR_ACCESS_DENIED copying into
// Program Files / registering the virtual camera COM server. Exit code 0 = success.
int RunElevatedObsInstallHelper() {
    wchar_t exePathBuf[MAX_PATH];
    GetModuleFileNameW(NULL, exePathBuf, MAX_PATH);
    std::wstring exePath(exePathBuf);
    std::wstring exeDir = exePath.substr(0, exePath.find_last_of(L"\\/"));

    std::wstring pluginSrc = exeDir + L"\\micam-obs-plugin.dll";
    std::wstring vcamSrc = exeDir + L"\\MiCamVirtualCamera.dll";

    const wchar_t* candidates[] = {
        L"C:\\Program Files\\obs-studio\\obs-plugins\\64bit",
        L"C:\\Program Files (x86)\\obs-studio\\obs-plugins\\64bit"
    };
    std::wstring obsPluginsDir;
    for (const wchar_t* c : candidates) {
        if (GetFileAttributesW(c) != INVALID_FILE_ATTRIBUTES) { obsPluginsDir = c; break; }
    }
    if (obsPluginsDir.empty()) return 1;

    bool ok = true;

    if (GetFileAttributesW(pluginSrc.c_str()) != INVALID_FILE_ATTRIBUTES) {
        std::wstring pluginDest = obsPluginsDir + L"\\micam-obs-plugin.dll";
        if (!CopyFileW(pluginSrc.c_str(), pluginDest.c_str(), FALSE)) ok = false;
    } else {
        ok = false;
    }

    if (GetFileAttributesW(vcamSrc.c_str()) != INVALID_FILE_ATTRIBUTES) {
        HMODULE hDll = LoadLibraryW(vcamSrc.c_str());
        if (hDll) {
            typedef HRESULT(STDAPICALLTYPE* pfnDllRegisterServer)();
            auto pRegister = (pfnDllRegisterServer)GetProcAddress(hDll, "DllRegisterServer");
            if (pRegister) {
                if (FAILED(pRegister())) ok = false;
            }
            FreeLibrary(hDll);
        }
    }

    return ok ? 0 : 1;
}

} // namespace

// Entry point for Pure Windows GUI Application (No Console Window)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    if (StrStrIW(GetCommandLineW(), L"--install-obs-plugin") != nullptr) {
        return RunElevatedObsInstallHelper();
    }

    MainWindowUI ui(hInstance);

    if (!ui.CreateAndShow(nCmdShow)) {
        return -1;
    }

    ui.RunMessageLoop();
    return 0;
}
