#ifndef MICAM_SHARED_MEMORY_STREAM_H
#define MICAM_SHARED_MEMORY_STREAM_H

#include <windows.h>
#include <cstdint>
#include <string>

#pragma pack(push, 1)
struct SharedFrameHeader {
    uint32_t magic;         // 'MCAM'
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;        // 0: NV12, 1: RGB32
    uint64_t frameIndex;
    uint64_t timestampUs;
    uint32_t dataSize;
};
#pragma pack(pop)

class SharedMemoryStreamWriter {
public:
    SharedMemoryStreamWriter(const std::string& streamName, uint32_t maxFrameBytes = 3840 * 2160 * 4);
    ~SharedMemoryStreamWriter();

    bool WriteFrame(const uint8_t* frameData, uint32_t dataSize, uint32_t width, uint32_t height, uint32_t stride, uint64_t timestampUs);

private:
    std::string m_mapName;
    HANDLE m_hMapFile;
    LPVOID m_pBuffer;
    HANDLE m_hMutex;
    uint32_t m_maxSize;
    uint64_t m_frameIndex{0};
};

class SharedMemoryStreamReader {
public:
    SharedMemoryStreamReader(const std::string& streamName);
    ~SharedMemoryStreamReader();

    bool ReadLatestFrame(SharedFrameHeader& outHeader, uint8_t* outBuffer, uint32_t maxBufferBytes);

private:
    std::string m_mapName;
    HANDLE m_hMapFile;
    LPVOID m_pBuffer;
    HANDLE m_hMutex;
};

#endif // MICAM_SHARED_MEMORY_STREAM_H
