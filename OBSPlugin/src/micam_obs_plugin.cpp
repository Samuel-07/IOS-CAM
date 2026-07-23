#include <obs-module.h>
#include "../../Desktop/src/SharedMemoryStream.h"
#include "../../Shared/protocol.h"
#include <string>
#include <vector>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("micam-obs-plugin", "en-US")

struct micam_source_data {
    obs_source_t* source;
    std::string stream_name;
    SharedMemoryStreamReader* reader;
    uint32_t width;
    uint32_t height;
    std::vector<uint8_t> frame_buffer;
};

static const char* micam_get_name(void* unused) {
    return "MiCam OBS";
}

static void* micam_create(obs_data_t* settings, obs_source_t* source) {
    micam_source_data* context = new micam_source_data();
    context->source = source;
    context->stream_name = "Global\\MiCam_Stream_1";
    context->reader = new SharedMemoryStreamReader(context->stream_name);
    context->width = 1920;
    context->height = 1080;
    context->frame_buffer.resize(3840 * 2160 * 4);
    return context;
}

static void micam_destroy(void* data) {
    micam_source_data* context = static_cast<micam_source_data*>(data);
    if (context) {
        delete context->reader;
        delete context;
    }
}

static void micam_video_render(void* data, gs_effect_t* effect) {
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

static obs_properties_t* micam_get_properties(void* data) {
    obs_properties_t* props = obs_properties_create();
    
    obs_property_t* device_list = obs_properties_add_list(
        props,
        "device_id",
        "Select iPhone Device Stream",
        OBS_COMBO_TYPE_LIST,
        OBS_COMBO_FORMAT_STRING
    );
    
    obs_property_list_add_string(device_list, "iPhone 1 (USB / Port 50000)", "Global\\MiCam_Stream_1");
    obs_property_list_add_string(device_list, "iPhone 2 (WiFi / Port 50001)", "Global\\MiCam_Stream_2");
    
    obs_property_t* lens_list = obs_properties_add_list(
        props,
        "camera_lens",
        "Camera Optics Lens",
        OBS_COMBO_TYPE_LIST,
        OBS_COMBO_FORMAT_INT
    );
    obs_property_list_add_int(lens_list, "Main Wide Angle (1x)", 0);
    obs_property_list_add_int(lens_list, "Ultra Wide (0.5x)", 1);
    obs_property_list_add_int(lens_list, "Telephoto (3x)", 2);
    obs_property_list_add_int(lens_list, "Front Selfie", 3);

    return props;
}

static struct obs_source_info micam_source_info = {
    .id             = "micam_obs",
    .type           = OBS_SOURCE_TYPE_INPUT,
    .output_flags   = OBS_SOURCE_VIDEO | OBS_SOURCE_ASYNC_VIDEO,
    .get_name       = micam_get_name,
    .create         = micam_create,
    .destroy        = micam_destroy,
    .get_properties = micam_get_properties,
    .video_render   = micam_video_render,
};

bool obs_module_load(void) {
    obs_register_source(&micam_source_info);
    blog(LOG_INFO, "[MiCam OBS Plugin] Loaded successfully as 'MiCam OBS'.");
    return true;
}

void obs_module_unload(void) {
    blog(LOG_INFO, "[MiCam OBS Plugin] Unloaded.");
}
