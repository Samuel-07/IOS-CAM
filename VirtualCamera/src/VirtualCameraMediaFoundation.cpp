#include "VirtualCameraMediaFoundation.h"
#include <iostream>
#include <vector>
#include <algorithm>

MiCamVirtualCameraSource::MiCamVirtualCameraSource(int instanceId)
    : m_instanceId(instanceId),
      m_streamName("Global\\MiCam_Stream_" + std::to_string(instanceId)),
      m_streamReader(m_streamName),
      m_pMediaBuffer(nullptr)
{}

MiCamVirtualCameraSource::~MiCamVirtualCameraSource() {
    if (m_pMediaBuffer) {
        m_pMediaBuffer->Release();
        m_pMediaBuffer = nullptr;
    }
}

HRESULT MiCamVirtualCameraSource::Initialize() {
    HRESULT hr = MFCreateMemoryBuffer(m_width * m_height * 4, &m_pMediaBuffer);
    return hr;
}

HRESULT MiCamVirtualCameraSource::GetNextSample(IMFSample** ppSample, uint64_t timestampUs) {
    if (!ppSample) return E_POINTER;
    *ppSample = nullptr;

    SharedFrameHeader header{};
    static std::vector<uint8_t> frameBuffer(3840 * 2160 * 4);

    if (!m_streamReader.ReadLatestFrame(header, frameBuffer.data(), static_cast<uint32_t>(frameBuffer.size()))) {
        return E_FAIL;
    }

    BYTE* pBufferData = nullptr;
    DWORD maxLen = 0, currentLen = 0;
    HRESULT hr = m_pMediaBuffer->Lock(&pBufferData, &maxLen, &currentLen);
    if (FAILED(hr)) return hr;

    memcpy(pBufferData, frameBuffer.data(), header.dataSize);
    m_pMediaBuffer->Unlock();
    m_pMediaBuffer->SetCurrentLength(header.dataSize);

    IMFSample* pSample = nullptr;
    hr = MFCreateSample(&pSample);
    if (FAILED(hr)) return hr;

    pSample->AddBuffer(m_pMediaBuffer);
    pSample->SetSampleTime(timestampUs * 10); // Convert microsec to 100ns units
    pSample->SetSampleDuration(10000000 / 60); // Default 60 FPS duration

    *ppSample = pSample;
    return S_OK;
}
