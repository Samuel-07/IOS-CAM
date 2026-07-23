#include <windows.h>
#include "UIWindow.h"

// Entry point for Pure Windows GUI Application (No Console Window)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    MainWindowUI ui(hInstance);

    if (!ui.CreateAndShow(nCmdShow)) {
        return -1;
    }

    ui.RunMessageLoop();
    return 0;
}
