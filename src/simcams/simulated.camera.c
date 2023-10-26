#include "simulated.camera.h"

#include "device/kit/camera.h"
#include "device/kit/driver.h"
#include "device/props/camera.h"
#include "device/props/components.h"
#include "platform.h"
#include "logger.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pcg_basic.h"

#ifdef __AVX2__
#include "bin2.avx2.c"
#else
#include "bin2.plain.c"
#endif

#define MAX_IMAGE_WIDTH (1ULL << 13)
#define MAX_IMAGE_HEIGHT (1ULL << 13)
#define MAX_BYTES_PER_PIXEL (4)

#define containerof(ptr, T, V) ((T*)(((char*)(ptr)) - offsetof(T, V)))
#define countof(e) (sizeof(e) / sizeof(*(e)))

#define L aq_logger
#define LOG(...) L(0, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOGE(...) L(1, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)

// #define TRACE(...) L(0, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define TRACE(...)

#define ECHO(e)                                                                \
    TRACE("ECHO %s", #e);                                                      \
    e
// #define ECHO(e) e

#define EXPECT(e, ...)                                                         \
    do {                                                                       \
        if (!(e)) {                                                            \
            LOGE(__VA_ARGS__);                                                 \
            goto Error;                                                        \
        }                                                                      \
    } while (0)
#define CHECK(e) EXPECT(e, "Expression evaluated as false:\n\t%s", #e)

#define max(a, b) (((a) > (b)) ? (a) : (b))

uint8_t
popcount_u8(uint8_t value);

struct SimulatedCamera
{
    struct CameraProperties properties;
    enum BasicDeviceKind kind;

    struct
    {
        struct clock throttle;
        int is_running;
        struct thread thread;
    } streamer;

    struct
    {
        void* data;
        struct ImageShape shape;
        struct lock lock;
        int64_t frame_id;
        int64_t last_emitted_frame_id;
        struct condition_variable frame_ready;
    } im;

    struct
    {
        int triggered;
        struct condition_variable trigger_ready;
    } software_trigger;

    uint64_t hardware_timestamp;
    struct Camera camera;
};

static size_t
bytes_of_image(const struct ImageShape* const shape)
{
    return shape->strides.planes * bytes_of_type(shape->type);
}

static size_t
aligned_bytes_of_image(const struct ImageShape* const shape)
{
    const size_t n = bytes_of_image(shape);
    return ((n + 31) >> 5) << 5;
}

static void
im_fill_rand(const struct ImageShape* const shape, uint8_t* buf)
{
    const size_t nbytes = aligned_bytes_of_image(shape);
    const uint8_t* const end = buf + nbytes;
    for (uint8_t* p = buf; p < end; p += 4)
        *(uint32_t*)p = pcg32_random();
}

void
im_fill_pattern_u8(const struct ImageShape* const shape,
                   float ox,
                   float oy,
                   uint8_t* buf);
void
im_fill_pattern_i8(const struct ImageShape* const shape,
                   float ox,
                   float oy,
                   int8_t* buf);

void
im_fill_pattern_u16(const struct ImageShape* const shape,
                    float ox,
                    float oy,
                    uint16_t* buf);

void
im_fill_pattern_i16(const struct ImageShape* const shape,
                    float ox,
                    float oy,
                    int16_t* buf);

void
im_fill_pattern_f32(const struct ImageShape* const shape,
                    float ox,
                    float oy,
                    float* buf);

static const char*
sample_type_to_string(enum SampleType type)
{
#define XXX(t) [SampleType_##t] = #t
    // clang-format off
    static const char* const table[] = {
        XXX(u8),
        XXX(u16),
        XXX(i8),
        XXX(i16),
        XXX(f32),
        XXX(u10),
        XXX(u12),
        XXX(u14),
    };
    // clang-format on
#undef XXX

    if (type >= countof(table))
        return "(unknown)";

    return table[type];
}

static void
im_fill_pattern(const struct ImageShape* const shape,
                float ox,
                float oy,
                uint8_t* buf)
{
    switch (shape->type) {
        case SampleType_u8:
            im_fill_pattern_u8(shape, ox, oy, buf);
            break;
        case SampleType_i8:
            im_fill_pattern_i8(shape, ox, oy, (int8_t*)buf);
            break;
        case SampleType_u16:
            im_fill_pattern_u16(shape, ox, oy, (uint16_t*)buf);
            break;
        case SampleType_i16:
            im_fill_pattern_i16(shape, ox, oy, (int16_t*)buf);
            break;
        case SampleType_f32:
            im_fill_pattern_f32(shape, ox, oy, (float*)buf);
            break;
        default:
            LOGE("Unsupported pixel type for this simcam: %s",
                 sample_type_to_string(shape->type));
    }
}

static void
compute_strides(struct ImageShape* shape)
{
    uint32_t* dims = (uint32_t*)&shape->dims;
    int64_t* st = (int64_t*)&shape->strides;
    st[0] = 1;
    for (int i = 1; i < 4; ++i)
        st[i] = st[i - 1] * dims[i - 1];
}

static void
compute_full_resolution_shape_and_offset(const struct SimulatedCamera* self,
                                         struct ImageShape* shape,
                                         uint32_t offset[2])
{
    const uint8_t b = self->properties.binning;
    const uint32_t w = b * self->properties.shape.x;
    const uint32_t h = b * self->properties.shape.y;
    offset[0] = b * self->properties.offset.x;
    offset[1] = b * self->properties.offset.y;
    shape->type = self->im.shape.type;
    shape->dims = (struct image_dims_s){
        .channels = 1,
        .width = w,
        .height = h,
        .planes = 1,
    };
    compute_strides(shape);
}

static void
simulated_camera_streamer_thread(struct SimulatedCamera* self)
{
    clock_init(&self->streamer.throttle);

    while (self->streamer.is_running) {
        struct ImageShape full = { 0 };
        uint32_t origin[2] = { 0, 0 };

        ECHO(lock_acquire(&self->im.lock));
        ECHO(compute_full_resolution_shape_and_offset(self, &full, origin));

        switch (self->kind) {
            case BasicDevice_Camera_Random:
                im_fill_rand(&full, self->im.data);
                break;
            case BasicDevice_Camera_Sin:
                ECHO(im_fill_pattern(
                  &full, (float)origin[0], (float)origin[1], self->im.data));
                break;
            case BasicDevice_Camera_Empty:
                break; // do nothing
            default:
                LOGE(
                  "Unexpected index for the kind of simulated camera. Got: %d",
                  self->kind);
        }
        {
            int w = full.dims.width;
            int h = full.dims.height;
            int b = self->properties.binning >> 1;
            while (b) {
                ECHO(bin2(self->im.data, w, h));
                b >>= 1;
                w >>= 1;
                h >>= 1;
            }
        }

        if (self->properties.input_triggers.frame_start.enable) {
            while (!self->software_trigger.triggered) {
                ECHO(condition_variable_wait(
                  &self->software_trigger.trigger_ready, &self->im.lock));
            }
            self->software_trigger.triggered = 0;
        }

        self->hardware_timestamp = clock_tic(0);
        ++self->im.frame_id;

        ECHO(condition_variable_notify_all(&self->im.frame_ready));
        ECHO(lock_release(&self->im.lock));

        if (self->streamer.is_running)
            clock_sleep_ms(&self->streamer.throttle,
                           self->properties.exposure_time_us * 1e-3f);
    }
}

//
//  CAMERA INTERFACE
//

static enum DeviceStatusCode
simcam_get_meta(const struct Camera* camera,
                struct CameraPropertyMetadata* meta)
{
    const struct SimulatedCamera* self =
      containerof(camera, const struct SimulatedCamera, camera);
    const unsigned binning = self->properties.binning;
    // current shape
    const float cw = (float)self->properties.shape.x;
    const float ch = (float)self->properties.shape.y;
    // max shape
    const float w = (float)MAX_IMAGE_WIDTH / (float)binning;
    const float h = (float)MAX_IMAGE_HEIGHT / (float)binning;
    // max offset - min width and height are 1 px.
    const float ox = max(0, w - cw - 1);
    const float oy = max(0, h - ch - 1);

    *meta = (struct CameraPropertyMetadata){
        .line_interval_us = { 0 },
        .exposure_time_us = { .high = 1.0e6f, .writable = 1, },
        .binning = { .low = 1.0f, .high = 8.0f, .writable = 1, },
        .shape = {
            .x = { .low = 1.0f, .high = w, .writable = 1, },
            .y = { .low = 1.0f, .high = h, .writable = 1, },
        },
        .offset = {
            .x = { .high = ox, .writable = 1, },
            .y = { .high = oy, .writable = 1, },
        },
        .supported_pixel_types = (1ULL << SampleType_u8)  |
                                 (1ULL << SampleType_u16) |
                                 (1ULL << SampleType_i8)  |
                                 (1ULL << SampleType_i16) |
                                 (1ULL << SampleType_f32),
        .digital_lines = {
          .line_count=1,
          .names = { [0] = "software" },
        },
        .triggers = {
          .frame_start = {.input=1, .output=0,},
        },
    };
    return Device_Ok;
}

static enum DeviceStatusCode
simcam_execute_trigger(struct Camera* camera);

#define clamp(v, L, H) (((v) < (L)) ? (L) : (((v) > (H)) ? (H) : (v)))

static enum DeviceStatusCode
simcam_set(struct Camera* camera, struct CameraProperties* settings)
{
    struct SimulatedCamera* self =
      containerof(camera, struct SimulatedCamera, camera);
    struct CameraPropertyMetadata meta = { 0 };

    if (!settings->binning)
        settings->binning = 1;

    EXPECT(popcount_u8(settings->binning) == 1,
           "Binning must be a power of two. Got %d.",
           settings->binning);

    if (self->properties.input_triggers.frame_start.enable &&
        !settings->input_triggers.frame_start.enable) {
        // fire if disabling the software trigger while live
        simcam_execute_trigger(camera);
    }

    self->properties = *settings;
    self->properties.pixel_type = settings->pixel_type;
    self->properties.input_triggers = (struct camera_properties_input_triggers_s){
        .frame_start = { .enable = settings->input_triggers.frame_start.enable,
                         .line = 0, // Software
                         .kind = Signal_Input,
                         .edge = TriggerEdge_Rising,
        },
    };

    simcam_get_meta(camera, &meta);
    struct ImageShape* const shape = &self->im.shape;
    shape->dims = (struct image_dims_s){
        .channels = 1,
        .width = clamp(settings->shape.x,
                       (uint32_t)meta.shape.x.low,
                       (uint32_t)meta.shape.x.high),
        .height = clamp(settings->shape.y,
                        (uint32_t)meta.shape.y.low,
                        (uint32_t)meta.shape.y.high),
        .planes = 1,
    };
    shape->type = settings->pixel_type;
    compute_strides(shape);

    self->properties.shape = (struct camera_properties_shape_s){
        .x = shape->dims.width,
        .y = shape->dims.height,
    };

    size_t nbytes = aligned_bytes_of_image(shape);
    self->im.data = malloc(nbytes);
    EXPECT(self->im.data, "Allocation of %llu bytes failed.", nbytes);

    return Device_Ok;
Error:
    return Device_Err;
}

static enum DeviceStatusCode
simcam_get(const struct Camera* camera, struct CameraProperties* settings)
{
    const struct SimulatedCamera* self =
      containerof(camera, const struct SimulatedCamera, camera);
    *settings = self->properties;
    return Device_Ok;
}

static enum DeviceStatusCode
simcam_get_shape(const struct Camera* camera, struct ImageShape* shape)
{
    const struct SimulatedCamera* self =
      containerof(camera, const struct SimulatedCamera, camera);
    *shape = self->im.shape;
    return Device_Ok;
}

static enum DeviceStatusCode
simcam_start(struct Camera* camera)
{
    struct SimulatedCamera* self =
      containerof(camera, struct SimulatedCamera, camera);
    self->streamer.is_running = 1;
    self->im.last_emitted_frame_id = -1;
    self->im.frame_id = -1;
    TRACE("SIMULATED CAMERA: thread launch");
    CHECK(thread_create(&self->streamer.thread,
                        (void (*)(void*))simulated_camera_streamer_thread,
                        self));
    return Device_Ok;
Error:
    return Device_Err;
}

static enum DeviceStatusCode
simcam_execute_trigger(struct Camera* camera)
{
    struct SimulatedCamera* self =
      containerof(camera, struct SimulatedCamera, camera);

    lock_acquire(&self->im.lock);
    self->software_trigger.triggered = 1;
    condition_variable_notify_all(&self->software_trigger.trigger_ready);
    lock_release(&self->im.lock);

    return Device_Ok;
}

static enum DeviceStatusCode
simcam_stop(struct Camera* camera)
{
    struct SimulatedCamera* self =
      containerof(camera, struct SimulatedCamera, camera);
    self->streamer.is_running = 0;
    simcam_execute_trigger(camera);
    condition_variable_notify_all(&self->im.frame_ready);

    TRACE("SIMULATED CAMERA: thread join");
    ECHO(thread_join(&self->streamer.thread));

    TRACE("SIMULATED CAMERA: exiting");
    return Device_Ok;
}

static enum DeviceStatusCode
simcam_get_frame(struct Camera* camera,
                 void* im,
                 size_t* nbytes,
                 struct ImageInfo* info_out)
{
    struct SimulatedCamera* self =
      containerof(camera, struct SimulatedCamera, camera);
    CHECK(*nbytes >= bytes_of_image(&self->im.shape));
    CHECK(self->streamer.is_running);

    TRACE("last: %5d current %5d",
          self->im.last_emitted_frame_id,
          self->im.frame_id);
    ECHO(lock_acquire(&self->im.lock));
    while (self->streamer.is_running &&
           self->im.last_emitted_frame_id >= self->im.frame_id) {
        ECHO(condition_variable_wait(&self->im.frame_ready, &self->im.lock));
    }
    self->im.last_emitted_frame_id = self->im.frame_id;
    if (!self->streamer.is_running) {
        goto Shutdown;
    }

    memcpy(im, self->im.data, bytes_of_image(&self->im.shape)); // NOLINT
    info_out->shape = self->im.shape;
    info_out->hardware_frame_id = self->im.frame_id;
    info_out->hardware_timestamp = self->hardware_timestamp;
Shutdown:
    ECHO(lock_release(&self->im.lock)); // only acquired in non-error path
    return Device_Ok;
Error:
    return Device_Err;
}

enum DeviceStatusCode
simcam_close_camera(struct Camera* camera_)
{
    struct SimulatedCamera* camera =
      containerof(camera_, struct SimulatedCamera, camera);
    EXPECT(camera_, "Invalid NULL parameter");
    simcam_stop(&camera->camera);
    if (camera->im.data)
        free(camera->im.data);
    free(camera);
    return Device_Ok;
Error:
    return Device_Err;
}

struct Camera*
simcam_make_camera(enum BasicDeviceKind kind)
{
    struct SimulatedCamera* self = malloc(sizeof(*self));
    EXPECT(self, "Allocation of %llu bytes failed.", sizeof(*self));
    memset(self, 0, sizeof(*self)); // NOLINT
    struct CameraProperties properties = {
        .exposure_time_us = 10000,
        .line_interval_us = 0,
        .readout_direction = Direction_Forward,
        .binning = 1,
        .pixel_type = SampleType_u8,
        .shape = { .x = 1920, .y = 1080 },
        .input_triggers = { .frame_start = { .enable = 0,
                                       .line = 0, // Software
                                       .kind = Signal_Input,
                                       .edge = TriggerEdge_Rising, }, },
    };
    *self = (struct SimulatedCamera){
        .properties = properties,
        .kind=kind,
        .im={
          .data=0,
          .shape = {
            .dims = {
              .channels = 1,
              .width = properties.shape.x,
              .height = properties.shape.y,
              .planes = 1,
            },
            .strides = {
              .channels=1,
              .width=1,
              .height=properties.shape.x,
              .planes=properties.shape.x*properties.shape.y,
            },
            .type=properties.pixel_type
          },
        },
        .camera={
          .state = DeviceState_AwaitingConfiguration,
          .set=simcam_set,
          .get=simcam_get,
          .get_meta=simcam_get_meta,
          .get_shape=simcam_get_shape,
          .start=simcam_start,
          .stop=simcam_stop,
          .execute_trigger=simcam_execute_trigger,
          .get_frame=simcam_get_frame
        }
    };
    thread_init(&self->streamer.thread);
    lock_init(&self->im.lock);
    condition_variable_init(&self->im.frame_ready);
    condition_variable_init(&self->software_trigger.trigger_ready);

    return &self->camera;
Error:
    if (self)
        free(self);
    return 0;
}
