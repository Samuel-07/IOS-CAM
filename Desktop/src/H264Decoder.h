#ifndef MICAM_DESKTOP_H264_DECODER_H
#define MICAM_DESKTOP_H264_DECODER_H

#include <winsock2.h>
#include <windows.h>
#include <mfobjects.h>
#include <cstdint>
#include <vector>

struct IMFTransform;

// Wraps Windows' built-in H.264 decoder MFT (CLSID_CMSH264DecoderMFT) to turn the Annex-B NALU
// stream the iOS app sends over the wire back into RGBA pixels. This is the piece that was
// missing end-to-end: iOS genuinely encodes and transmits H.264 (VideoEncoder.swift /
// NetworkStreamer.sendVideoFrame), but until now nothing on the Windows side ever decoded it -
// devices could be detected and controlled, but never actually seen.
class H264Decoder {
public:
    H264Decoder();
    ~H264Decoder();

    // (Re)initializes the decoder for a given coded size. Safe to call again if the incoming
    // stream's resolution changes (e.g. the user picks a different resolution on the phone).
    bool Initialize(uint32_t width, uint32_t height);

    // Feeds one Annex-B access unit (a full frame's worth of NALUs, start-code prefixed - this
    // matches exactly what VideoEncoder.swift emits and NetworkStreamer forwards per frame).
    // On success, fills outRgba/outWidth/outHeight with the most recently decoded frame and
    // returns true. Returns false if the MFT needs more input before it can produce a frame
    // (normal for the first couple of calls) or on a genuine decode error.
    bool Decode(const uint8_t* data, uint32_t size, std::vector<uint8_t>& outRgba, uint32_t& outWidth, uint32_t& outHeight);

private:
    void Shutdown();
    bool NegotiateOutputType();
    bool ConvertLatestOutput(IMFSample* sample, std::vector<uint8_t>& outRgba, uint32_t& outWidth, uint32_t& outHeight);

    IMFTransform* m_pDecoder{ nullptr };
    bool m_initialized{ false };
    uint32_t m_width{ 0 };
    uint32_t m_height{ 0 };
    DWORD m_outputSampleSize{ 0 };
};

#endif // MICAM_DESKTOP_H264_DECODER_H
