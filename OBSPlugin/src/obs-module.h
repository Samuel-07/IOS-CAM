#ifndef OBS_MODULE_H
#define OBS_MODULE_H

#include <cstdint>
#include <cstddef>

#define OBS_DECLARE_MODULE()
#define OBS_MODULE_USE_DEFAULT_LOCALE(name, locale)
#define LOG_INFO 100

inline void blog(int level, const char* format, ...) {}

enum obs_source_type {
    OBS_SOURCE_TYPE_INPUT
};

enum obs_source_output_flags {
    OBS_SOURCE_VIDEO = (1 << 0),
    OBS_SOURCE_ASYNC_VIDEO = (1 << 1)
};

enum video_format {
    VIDEO_FORMAT_RGBA
};

struct obs_source_t {};
struct obs_data_t {};
struct obs_properties_t {};
struct obs_property_t {};
struct gs_effect_t {};

struct obs_source_frame {
    uint8_t* data[8];
    uint32_t linesize[8];
    uint32_t width;
    uint32_t height;
    enum video_format format;
    uint64_t timestamp;
};

struct obs_source_info {
    const char* id;
    enum obs_source_type type;
    uint32_t output_flags;
    const char* (*get_name)(void* type_data);
    void* (*create)(obs_data_t* settings, obs_source_t* source);
    void (*destroy)(void* data);
    obs_properties_t* (*get_properties)(void* data);
    void (*video_render)(void* data, gs_effect_t* effect);
};

inline void obs_register_source(struct obs_source_info* info) {}
inline void obs_source_output_video(obs_source_t* source, const struct obs_source_frame* frame) {}

enum obs_combo_type { OBS_COMBO_TYPE_LIST };
enum obs_combo_format { OBS_COMBO_FORMAT_STRING, OBS_COMBO_FORMAT_INT };

inline obs_properties_t* obs_properties_create() { return nullptr; }
inline obs_property_t* obs_properties_add_list(obs_properties_t* props, const char* name, const char* description, enum obs_combo_type type, enum obs_combo_format format) { return nullptr; }
inline void obs_property_list_add_string(obs_property_t* p, const char* name, const char* val) {}
inline void obs_property_list_add_int(obs_property_t* p, const char* name, long long val) {}

#endif // OBS_MODULE_H
