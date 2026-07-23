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
    ${CMAKE_CURRENT_SOURCE_DIR}/OBSPlugin/src
)

# Desktop Control Center Executable (Pure Windows GUI - No Terminal Window)
add_executable(MiCamDesktop WIN32
    Desktop/src/main.cpp
    Desktop/src/UIWindow.cpp
    Desktop/src/DeviceManager.cpp
    Desktop/src/SharedMemoryStream.cpp
)
target_link_libraries(MiCamDesktop PRIVATE ws2_32 Ole32 User32 Gdi32)

# Virtual Camera Shared DLL
add_library(MiCamVirtualCamera SHARED
    VirtualCamera/src/VirtualCameraMediaFoundation.cpp
    Desktop/src/SharedMemoryStream.cpp
)
target_link_libraries(MiCamVirtualCamera PRIVATE mfplat mfuuid Ole32)

# OBS Studio Plugin Shared DLL
add_library(micam-obs-plugin SHARED
    OBSPlugin/src/micam_obs_plugin.cpp
    Desktop/src/SharedMemoryStream.cpp
)
target_link_libraries(micam-obs-plugin PRIVATE Ole32)
