#ifndef MICAM_VIRTUAL_CAMERA_MF_H
#define MICAM_VIRTUAL_CAMERA_MF_H

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <string>
#include "../../Desktop/src/SharedMemoryStream.h"

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")

class MiCamVirtualCameraSource {
public:
    MiCamVirtualCameraSource(int instanceId);
    ~MiCamVirtualCameraSource();

    HRESULT Initialize();
    HRESULT GetNextSample(IMFSample** ppSample, uint64_t timestampUs);

private:
    int m_instanceId;
    std::string m_streamName;
    SharedMemoryStreamReader m_streamReader;
    IMFMediaBuffer* m_pMediaBuffer;
    uint32_t m_width{1920};
    uint32_t m_height{1080};
};

#endif // MICAM_VIRTUAL_CAMERA_MF_H
