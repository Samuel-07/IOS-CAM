cmake_minimum_required(VERSION 3.20)
project(MiCamSuite CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/Shared
    ${CMAKE_CURRENT_SOURCE_DIR}/Desktop/src
    ${CMAKE_CURRENT_SOURCE_DIR}/VirtualCamera/src
)

# Desktop Control Center Executable (Pure Windows GUI - No Terminal Window)
add_executable(MiCamDesktop WIN32
    Desktop/src/main.cpp
    Desktop/src/UIWindow.cpp
    Desktop/src/DeviceManager.cpp
    Desktop/src/UsbMuxClient.cpp
    Desktop/src/MdnsBrowser.cpp
    Desktop/src/SharedMemoryStream.cpp
    Desktop/src/H264Decoder.cpp
    Desktop/src/resource.rc
    Shared/DeviceRegistry.cpp
)
target_link_libraries(MiCamDesktop PRIVATE ws2_32 Ole32 User32 Gdi32 Shlwapi Shell32 mfplat mfuuid mf Advapi32)

# Standalone diagnostic console tool (not shipped) for debugging usbmuxd/mDNS detection issues.
add_executable(UsbMuxDebug
    Desktop/src/UsbMuxDebug.cpp
    Desktop/src/UsbMuxClient.cpp
    Desktop/src/MdnsBrowser.cpp
)
target_link_libraries(UsbMuxDebug PRIVATE ws2_32)

# Virtual Camera Shared DLL
add_library(MiCamVirtualCamera SHARED
    VirtualCamera/src/VirtualCameraMediaFoundation.cpp
    Desktop/src/SharedMemoryStream.cpp
)
target_link_libraries(MiCamVirtualCamera PRIVATE mfplat mfuuid Ole32)

# OBS Studio Plugin Shared DLL - linked against the real libobs SDK.
#
# vendor/obs-studio/libobs contains the real headers (sparse-checked-out from the
# obsproject/obs-studio tag matching the installed OBS Studio version), and
# vendor/obs-lib/obs.lib is an import library generated from the installed obs.dll's export
# table (there's no official dev/SDK package shipped by the OBS installer, so this is the
# standard technique for building a plugin against an existing OBS install: `dumpbin /exports`
# + a generated .def + `lib.exe /def:...`).
add_library(micam-obs-plugin SHARED
    OBSPlugin/src/micam_obs_plugin.cpp
    Desktop/src/SharedMemoryStream.cpp
    Shared/DeviceRegistry.cpp
)
target_include_directories(micam-obs-plugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/obs-studio/libobs
)
target_link_libraries(micam-obs-plugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/obs-lib/obs.lib
    Ole32
)
