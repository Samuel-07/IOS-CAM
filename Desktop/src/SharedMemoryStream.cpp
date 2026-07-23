#include "SharedMemoryStream.h"
#include <iostream>

SharedMemoryStreamWriter::SharedMemoryStreamWriter(const std::string& streamName, uint32_t maxFrameBytes)
    : m_mapName(streamName), m_maxSize(maxFrameBytes + sizeof(SharedFrameHeader)), m_hMapFile(NULL), m_pBuffer(NULL), m_hMutex(NULL)
{
    std::string mutexName = m_mapName + "_Mutex";
    m_hMutex = CreateMutexA(NULL, FALSE, mutexName.c_str());

    m_hMapFile = CreateFileMappingA(
        INVALID_HANDLE_VALUE,
        NULL,
        PAGE_READWRITE,
        0,
        m_maxSize,
        m_mapName.c_str()
    );

    if (m_hMapFile != NULL) {
        m_pBuffer = MapViewOfFile(m_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, m_maxSize);
    }
}

SharedMemoryStreamWriter::~SharedMemoryStreamWriter() {
    if (m_pBuffer) UnmapViewOfFile(m_pBuffer);
    if (m_hMapFile) CloseHandle(m_hMapFile);
    if (m_hMutex) CloseHandle(m_hMutex);
}

bool SharedMemoryStreamWriter::WriteFrame(const uint8_t* frameData, uint32_t dataSize, uint32_t width, uint32_t height, uint32_t stride, uint64_t timestampUs) {
    if (!m_pBuffer || !m_hMutex) return false;

    WaitForSingleObject(m_hMutex, INFINITE);

    SharedFrameHeader header{};
    header.magic = 0x4D43414D;
    header.width = width;
    header.height = height;
    header.stride = stride;
    header.format = 1; // RGB32 / NV12
    header.frameIndex = ++m_frameIndex;
    header.timestampUs = timestampUs;
    header.dataSize = dataSize;

    memcpy(m_pBuffer, &header, sizeof(SharedFrameHeader));
    memcpy((uint8_t*)m_pBuffer + sizeof(SharedFrameHeader), frameData, dataSize);

    ReleaseMutex(m_hMutex);
    return true;
}

SharedMemoryStreamReader::SharedMemoryStreamReader(const std::string& streamName)
    : m_mapName(streamName), m_hMapFile(NULL), m_pBuffer(NULL), m_hMutex(NULL)
{
    std::string mutexName = m_mapName + "_Mutex";
    m_hMutex = OpenMutexA(SYNCHRONIZE, FALSE, mutexName.c_str());

    m_hMapFile = OpenFileMappingA(FILE_MAP_READ, FALSE, m_mapName.c_str());
    if (m_hMapFile != NULL) {
        m_pBuffer = MapViewOfFile(m_hMapFile, FILE_MAP_READ, 0, 0, 0);
    }
}

SharedMemoryStreamReader::~SharedMemoryStreamReader() {
    if (m_pBuffer) UnmapViewOfFile(m_pBuffer);
    if (m_hMapFile) CloseHandle(m_hMapFile);
    if (m_hMutex) CloseHandle(m_hMutex);
}

bool SharedMemoryStreamReader::ReadLatestFrame(SharedFrameHeader& outHeader, uint8_t* outBuffer, uint32_t maxBufferBytes) {
    if (!m_pBuffer) {
        // Retry opening if mapping was created after reader initialization
        m_hMapFile = OpenFileMappingA(FILE_MAP_READ, FALSE, m_mapName.c_str());
        if (m_hMapFile != NULL) {
            m_pBuffer = MapViewOfFile(m_hMapFile, FILE_MAP_READ, 0, 0, 0);
        } else {
            return false;
        }
    }

    if (m_hMutex) WaitForSingleObject(m_hMutex, INFINITE);

    SharedFrameHeader* header = (SharedFrameHeader*)m_pBuffer;
    if (header->magic != 0x4D43414D) {
        if (m_hMutex) ReleaseMutex(m_hMutex);
        return false;
    }

    outHeader = *header;
    uint32_t copySize = min(header->dataSize, maxBufferBytes);
    memcpy(outBuffer, (const uint8_t*)m_pBuffer + sizeof(SharedFrameHeader), copySize);

    if (m_hMutex) ReleaseMutex(m_hMutex);
    return true;
}
