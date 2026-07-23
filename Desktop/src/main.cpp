#include <winsock2.h> // Must precede windows.h - see DeviceRegistry.h comment
#include <windows.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include <cstring>
#include "UIWindow.h"

#pragma comment(lib, "shlwapi.lib")

namespace {

// Byte-for-byte comparison so an already-current destination file can be treated as a
// no-op success instead of attempting (and failing) a redundant overwrite. This matters
// because OBS keeps micam-obs-plugin.dll memory-mapped while it's running - CopyFileW onto a
// loaded DLL fails with a sharing violation regardless of privilege level (admin elevation
// cannot bypass a file lock), so re-running the installer while OBS is open used to always
// report failure even when the deployed file already had the correct content.
bool FilesAreIdentical(const std::wstring& pathA, const std::wstring& pathB) {
    HANDLE ha = CreateFileW(pathA.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (ha == INVALID_HANDLE_VALUE) return false;
    HANDLE hb = CreateFileW(pathB.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hb == INVALID_HANDLE_VALUE) { CloseHandle(ha); return false; }

    LARGE_INTEGER sizeA{}, sizeB{};
    GetFileSizeEx(ha, &sizeA);
    GetFileSizeEx(hb, &sizeB);

    bool identical = false;
    if (sizeA.QuadPart == sizeB.QuadPart) {
        identical = true;
        const DWORD bufSize = 65536;
        std::vector<uint8_t> bufA(bufSize), bufB(bufSize);
        for (;;) {
            DWORD readA = 0, readB = 0;
            BOOL okA = ReadFile(ha, bufA.data(), bufSize, &readA, NULL);
            BOOL okB = ReadFile(hb, bufB.data(), bufSize, &readB, NULL);
            if (!okA || !okB || readA != readB) { identical = (readA == 0 && readB == 0) && okA && okB; break; }
            if (readA == 0) break;
            if (memcmp(bufA.data(), bufB.data(), readA) != 0) { identical = false; break; }
        }
    }

    CloseHandle(ha);
    CloseHandle(hb);
    return identical;
}

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
        bool destExists = GetFileAttributesW(pluginDest.c_str()) != INVALID_FILE_ATTRIBUTES;
        if (destExists && FilesAreIdentical(pluginSrc, pluginDest)) {
            // Already up to date (likely locked by a running OBS instance) - nothing to do.
        } else if (!CopyFileW(pluginSrc.c_str(), pluginDest.c_str(), FALSE)) {
            ok = false;
        }
    } else {
        ok = false;
    }

    // Virtual camera OS registration isn't implemented yet (MiCamVirtualCamera.dll doesn't
    // export DllRegisterServer) - this is a known, separate gap, not a failure of the OBS
    // plugin install this button is actually responsible for. Attempt it opportunistically
    // but never let its absence flip the overall result to failure.
    if (GetFileAttributesW(vcamSrc.c_str()) != INVALID_FILE_ATTRIBUTES) {
        HMODULE hDll = LoadLibraryW(vcamSrc.c_str());
        if (hDll) {
            typedef HRESULT(STDAPICALLTYPE* pfnDllRegisterServer)();
            auto pRegister = (pfnDllRegisterServer)GetProcAddress(hDll, "DllRegisterServer");
            if (pRegister) {
                pRegister();
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
