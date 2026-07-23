#include "DeviceRegistry.h"
#include <cstring>
#include <algorithm>

DeviceRegistryWriter::DeviceRegistryWriter() {
    m_hMutex = CreateMutexA(NULL, FALSE, "Global\\MiCam_DeviceRegistry_Mutex");
    m_hMapFile = CreateFileMappingA(
        INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(DeviceRegistryBlock), kDeviceRegistryMapName);
    if (m_hMapFile) {
        m_pBuffer = MapViewOfFile(m_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(DeviceRegistryBlock));
    }
}

DeviceRegistryWriter::~DeviceRegistryWriter() {
    if (m_pBuffer) UnmapViewOfFile(m_pBuffer);
    if (m_hMapFile) CloseHandle(m_hMapFile);
    if (m_hMutex) CloseHandle(m_hMutex);
}

void DeviceRegistryWriter::Publish(const std::vector<DeviceRegistryEntry>& devices) {
    if (!m_pBuffer || !m_hMutex) return;

    WaitForSingleObject(m_hMutex, INFINITE);

    DeviceRegistryBlock block{};
    block.magic = kDeviceRegistryMagic;
    block.count = (uint32_t)(std::min<size_t>)(devices.size(), 16);
    for (uint32_t i = 0; i < block.count; ++i) {
        block.entries[i] = devices[i];
    }

    memcpy(m_pBuffer, &block, sizeof(DeviceRegistryBlock));
    ReleaseMutex(m_hMutex);
}

DeviceRegistryReader::DeviceRegistryReader() {
    m_hMutex = OpenMutexA(SYNCHRONIZE, FALSE, "Global\\MiCam_DeviceRegistry_Mutex");
    m_hMapFile = OpenFileMappingA(FILE_MAP_READ, FALSE, kDeviceRegistryMapName);
    if (m_hMapFile) {
        m_pBuffer = MapViewOfFile(m_hMapFile, FILE_MAP_READ, 0, 0, sizeof(DeviceRegistryBlock));
    }
}

DeviceRegistryReader::~DeviceRegistryReader() {
    if (m_pBuffer) UnmapViewOfFile(m_pBuffer);
    if (m_hMapFile) CloseHandle(m_hMapFile);
    if (m_hMutex) CloseHandle(m_hMutex);
}

std::vector<DeviceRegistryEntry> DeviceRegistryReader::Read() {
    std::vector<DeviceRegistryEntry> result;

    if (!m_pBuffer) {
        // The Desktop app may have been started after this reader (or after OBS itself),
        // so retry opening the mapping lazily on every read.
        m_hMapFile = OpenFileMappingA(FILE_MAP_READ, FALSE, kDeviceRegistryMapName);
        if (!m_hMapFile) return result;
        m_pBuffer = MapViewOfFile(m_hMapFile, FILE_MAP_READ, 0, 0, sizeof(DeviceRegistryBlock));
        if (!m_pBuffer) return result;
    }

    if (m_hMutex) WaitForSingleObject(m_hMutex, INFINITE);

    const DeviceRegistryBlock* block = (const DeviceRegistryBlock*)m_pBuffer;
    if (block->magic == kDeviceRegistryMagic) {
        uint32_t count = (std::min)(block->count, 16u);
        for (uint32_t i = 0; i < count; ++i) {
            result.push_back(block->entries[i]);
        }
    }

    if (m_hMutex) ReleaseMutex(m_hMutex);
    return result;
}
