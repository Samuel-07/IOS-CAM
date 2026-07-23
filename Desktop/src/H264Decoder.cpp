#include "H264Decoder.h"
#include <mfapi.h>
#include <mftransform.h>
#include <mferror.h>
#include <mfidl.h>
#include <algorithm>
#include <cstring>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "ole32.lib")

// The built-in Windows H.264 decoder MFT. Declared manually (rather than pulling in
// wmcodecdsp.h, which drags in a much larger dependency surface) - this GUID is a stable,
// documented part of the Media Foundation platform since Windows 7.
static const CLSID CLSID_CMSH264DecoderMFT_Local = { 0x62CE7E72, 0x4C71, 0x4d20, { 0xB1, 0x5D, 0x45, 0x28, 0x31, 0xA8, 0x7D, 0x9D } };

namespace {

bool g_mfStarted = false;

void EnsureMediaFoundationStarted() {
    if (!g_mfStarted) {
        CoInitializeEx(NULL, COINIT_MULTITHREADED);
        MFStartup(MF_VERSION, MFSTARTUP_LITE);
        g_mfStarted = true;
    }
}

// Manual BT.601 NV12 -> RGBA conversion. Not SIMD-optimized - fine for a preview/virtual-camera
// pipeline at modest frame rates, not tuned for maximum throughput.
void ConvertNV12ToRGBA(const uint8_t* yPlane, uint32_t yStride,
                        const uint8_t* uvPlane, uint32_t uvStride,
                        uint32_t width, uint32_t height,
                        std::vector<uint8_t>& outRgba) {
    outRgba.resize((size_t)width * height * 4);
    uint8_t* dst = outRgba.data();

    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* yRow = yPlane + (size_t)y * yStride;
        const uint8_t* uvRow = uvPlane + (size_t)(y / 2) * uvStride;
        uint8_t* dstRow = dst + (size_t)y * width * 4;

        for (uint32_t x = 0; x < width; ++x) {
            int Y = yRow[x];
            int U = uvRow[(x / 2) * 2 + 0] - 128;
            int V = uvRow[(x / 2) * 2 + 1] - 128;

            int r = Y + ((91881 * V) >> 16);
            int g = Y - ((22554 * U + 46802 * V) >> 16);
            int b = Y + ((116130 * U) >> 16);

            dstRow[x * 4 + 0] = (uint8_t)std::clamp(r, 0, 255);
            dstRow[x * 4 + 1] = (uint8_t)std::clamp(g, 0, 255);
            dstRow[x * 4 + 2] = (uint8_t)std::clamp(b, 0, 255);
            dstRow[x * 4 + 3] = 255;
        }
    }
}

} // namespace

H264Decoder::H264Decoder() {
    EnsureMediaFoundationStarted();
}

H264Decoder::~H264Decoder() {
    Shutdown();
}

void H264Decoder::Shutdown() {
    if (m_pDecoder) {
        m_pDecoder->Release();
        m_pDecoder = nullptr;
    }
    m_initialized = false;
}

bool H264Decoder::Initialize(uint32_t width, uint32_t height) {
    Shutdown();
    if (width == 0 || height == 0) return false;

    HRESULT hr = CoCreateInstance(CLSID_CMSH264DecoderMFT_Local, NULL, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&m_pDecoder));
    if (FAILED(hr) || !m_pDecoder) return false;

    IMFMediaType* inputType = nullptr;
    hr = MFCreateMediaType(&inputType);
    if (SUCCEEDED(hr)) {
        inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        MFSetAttributeSize(inputType, MF_MT_FRAME_SIZE, width, height);
        inputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        hr = m_pDecoder->SetInputType(0, inputType, 0);
        inputType->Release();
    }
    if (FAILED(hr)) { Shutdown(); return false; }

    m_width = width;
    m_height = height;

    if (!NegotiateOutputType()) { Shutdown(); return false; }

    m_pDecoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    m_pDecoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

    m_initialized = true;
    return true;
}

bool H264Decoder::NegotiateOutputType() {
    // Ask the MFT for an NV12 output type - it's the decoder's native output format, avoiding
    // an extra internal color-conversion step before we do our own NV12 -> RGBA pass.
    for (DWORD i = 0; ; ++i) {
        IMFMediaType* outputType = nullptr;
        HRESULT hr = m_pDecoder->GetOutputAvailableType(0, i, &outputType);
        if (hr == MF_E_NO_MORE_TYPES || FAILED(hr)) break;

        GUID subtype{};
        outputType->GetGUID(MF_MT_SUBTYPE, &subtype);
        if (subtype == MFVideoFormat_NV12) {
            HRESULT setHr = m_pDecoder->SetOutputType(0, outputType, 0);
            outputType->Release();
            if (SUCCEEDED(setHr)) {
                MFT_OUTPUT_STREAM_INFO info{};
                m_pDecoder->GetOutputStreamInfo(0, &info);
                m_outputSampleSize = info.cbSize;
                return true;
            }
            return false;
        }
        outputType->Release();
    }
    return false;
}

bool H264Decoder::Decode(const uint8_t* data, uint32_t size, std::vector<uint8_t>& outRgba, uint32_t& outWidth, uint32_t& outHeight) {
    if (!m_initialized || !data || size == 0) return false;

    IMFMediaBuffer* buffer = nullptr;
    HRESULT hr = MFCreateMemoryBuffer(size, &buffer);
    if (FAILED(hr)) return false;

    BYTE* rawBuffer = nullptr;
    buffer->Lock(&rawBuffer, NULL, NULL);
    memcpy(rawBuffer, data, size);
    buffer->Unlock();
    buffer->SetCurrentLength(size);

    IMFSample* sample = nullptr;
    MFCreateSample(&sample);
    sample->AddBuffer(buffer);
    buffer->Release();

    hr = m_pDecoder->ProcessInput(0, sample, 0);
    sample->Release();
    if (FAILED(hr)) return false; // MF_E_NOTACCEPTING - decoder's output queue is full; caller retries next frame

    bool gotFrame = false;

    while (true) {
        MFT_OUTPUT_DATA_BUFFER outputBuffer{};
        IMFSample* outputSample = nullptr;
        MFCreateSample(&outputSample);

        IMFMediaBuffer* outMediaBuffer = nullptr;
        MFCreateMemoryBuffer((std::max)(m_outputSampleSize, (DWORD)(m_width * m_height * 3 / 2)), &outMediaBuffer);
        outputSample->AddBuffer(outMediaBuffer);
        outMediaBuffer->Release();

        outputBuffer.pSample = outputSample;

        DWORD status = 0;
        HRESULT outHr = m_pDecoder->ProcessOutput(0, 1, &outputBuffer, &status);

        if (outHr == MF_E_TRANSFORM_STREAM_CHANGE) {
            outputSample->Release();
            if (!NegotiateOutputType()) return gotFrame;
            continue; // retry with the renegotiated type
        }
        if (outHr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
            outputSample->Release();
            break;
        }
        if (FAILED(outHr)) {
            outputSample->Release();
            break;
        }

        if (ConvertLatestOutput(outputSample, outRgba, outWidth, outHeight)) {
            gotFrame = true;
        }
        outputSample->Release();
        // Keep draining - ProcessOutput may have more than one frame queued.
    }

    return gotFrame;
}

bool H264Decoder::ConvertLatestOutput(IMFSample* sample, std::vector<uint8_t>& outRgba, uint32_t& outWidth, uint32_t& outHeight) {
    IMFMediaBuffer* buffer = nullptr;
    if (FAILED(sample->ConvertToContiguousBuffer(&buffer)) || !buffer) return false;

    BYTE* data = nullptr;
    DWORD maxLen = 0, curLen = 0;
    if (FAILED(buffer->Lock(&data, &maxLen, &curLen))) {
        buffer->Release();
        return false;
    }

    // NV12 layout: Y plane (width x height), then interleaved UV plane (width x height/2),
    // both using the decoder's negotiated stride (== width for this decoder's default output).
    uint32_t yStride = m_width;
    uint32_t uvStride = m_width;
    const uint8_t* yPlane = data;
    const uint8_t* uvPlane = data + (size_t)yStride * m_height;

    size_t expectedSize = (size_t)yStride * m_height + (size_t)uvStride * (m_height / 2);
    if (curLen < expectedSize) {
        buffer->Unlock();
        buffer->Release();
        return false;
    }

    ConvertNV12ToRGBA(yPlane, yStride, uvPlane, uvStride, m_width, m_height, outRgba);
    outWidth = m_width;
    outHeight = m_height;

    buffer->Unlock();
    buffer->Release();
    return true;
}
