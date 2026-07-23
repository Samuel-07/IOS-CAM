// Built against the real OBS Studio libobs SDK (vendor/obs-studio/libobs, checked out at the
// exact tag matching the installed OBS Studio version) and linked against an import library
// generated from the installed obs.dll's export table. The previous version of this file
// compiled against a hand-rolled local mock of obs-module.h, which meant obs_register_source /
// obs_module_load never spoke OBS's real module ABI - the resulting DLL could never have been
// loaded by actual OBS Studio no matter where it was copied.
#include <obs-module.h>
#include "../../Desktop/src/SharedMemoryStream.h"
#include "../../Shared/DeviceRegistry.h"
#include "../../Shared/protocol.h"
#include <string>
#include <vector>

OBS_DECLARE_MODULE()

struct micam_source_data {
    obs_source_t* source;
    std::string stream_name;
    SharedMemoryStreamReader* reader;
    std::vector<uint8_t> frame_buffer;
};

static const char* micam_get_name(void*) {
    return "MiCam OBS";
}

static void micam_update(void* data, obs_data_t* settings);

static void* micam_create(obs_data_t* settings, obs_source_t* source) {
    micam_source_data* context = new micam_source_data();
    context->source = source;
    context->reader = nullptr;
    context->frame_buffer.resize(3840 * 2160 * 4);
    micam_update(context, settings);
    return context;
}

static void micam_destroy(void* data) {
    micam_source_data* context = static_cast<micam_source_data*>(data);
    if (context) {
        delete context->reader;
        delete context;
    }
}

static void micam_update(void* data, obs_data_t* settings) {
    micam_source_data* context = static_cast<micam_source_data*>(data);
    const char* streamName = obs_data_get_string(settings, "device_id");
    if (!streamName || !*streamName) return;

    if (context->stream_name != streamName) {
        context->stream_name = streamName;
        delete context->reader;
        context->reader = new SharedMemoryStreamReader(context->stream_name);
    }
}

static void micam_video_render(void* data, gs_effect_t*) {
    micam_source_data* context = static_cast<micam_source_data*>(data);
    if (!context || !context->reader) return;

    SharedFrameHeader header{};
    if (context->reader->ReadLatestFrame(header, context->frame_buffer.data(), (uint32_t)context->frame_buffer.size())) {
        obs_source_frame frame{};
        frame.data[0] = context->frame_buffer.data();
        frame.linesize[0] = header.stride;
        frame.width = header.width;
        frame.height = header.height;
        frame.format = VIDEO_FORMAT_RGBA;
        frame.timestamp = header.timestampUs * 1000;

        obs_source_output_video(context->source, &frame);
    }
}

// Populated fresh every time the source's properties dialog is opened, by reading the live
// device registry the Desktop app (MiCamDesktop.exe) publishes to shared memory. This is what
// makes "OBS actually recognizes both connected iPhones" real: the two processes have no other
// shared state, so without this the plugin has no way to know what's plugged in right now.
static obs_properties_t* micam_get_properties(void*) {
    obs_properties_t* props = obs_properties_create();

    obs_property_t* device_list = obs_properties_add_list(
        props,
        "device_id",
        "Select iPhone Device Stream",
        OBS_COMBO_TYPE_LIST,
        OBS_COMBO_FORMAT_STRING
    );

    DeviceRegistryReader registryReader;
    std::vector<DeviceRegistryEntry> devices = registryReader.Read();

    if (devices.empty()) {
        obs_property_list_add_string(device_list, "No devices detected - open MiCam Studio Pro", "");
    } else {
        for (const auto& dev : devices) {
            obs_property_list_add_string(device_list, dev.displayName, dev.streamName);
        }
    }

    return props;
}

static struct obs_source_info micam_source_info = {};

bool obs_module_load(void) {
    micam_source_info.id = "micam_obs";
    micam_source_info.type = OBS_SOURCE_TYPE_INPUT;
    micam_source_info.output_flags = OBS_SOURCE_ASYNC_VIDEO;
    micam_source_info.get_name = micam_get_name;
    micam_source_info.create = micam_create;
    micam_source_info.destroy = micam_destroy;
    micam_source_info.update = micam_update;
    micam_source_info.get_properties = micam_get_properties;
    micam_source_info.video_render = micam_video_render;

    obs_register_source(&micam_source_info);
    blog(LOG_INFO, "[MiCam OBS Plugin] Loaded successfully as 'MiCam OBS'.");
    return true;
}

void obs_module_unload(void) {
    blog(LOG_INFO, "[MiCam OBS Plugin] Unloaded.");
}
