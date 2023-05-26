#include "device/props/storage.h"
#include "device/kit/storage.h"
#include "device/props/components.h"
#include "platform.h"
#include "logger.h"

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define LOG(...) aq_logger(0, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOGE(...) aq_logger(1, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define CHECK(e)                                                               \
    do {                                                                       \
        if (!(e)) {                                                            \
            LOGE("Expression evaluated as false:\n\t%s", #e);                  \
            goto Error;                                                        \
        }                                                                      \
    } while (0)

#define containerof(ptr, T, V) ((T*)(((char*)(ptr)) - offsetof(T, V)))

struct Trash
{
    struct Storage writer;
    struct StorageProperties settings;
    uint64_t iframe;
};

static enum DeviceState
trash_set(struct Storage* self_, const struct StorageProperties* settings)
{
    struct Trash* self = containerof(self_, struct Trash, writer);
    CHECK(storage_properties_copy(&self->settings, settings));
    return DeviceState_Armed;
Error:
    return DeviceState_AwaitingConfiguration;
}

static void
trash_get(const struct Storage* self_, struct StorageProperties* settings)
{
    struct Trash* self = containerof(self_, struct Trash, writer);
    *settings = self->settings;
}

static void
trash_get_meta(const struct Storage* self_,
               struct StoragePropertyMetadata* meta)
{
    CHECK(meta);
    *meta = (struct StoragePropertyMetadata){ 0 };
Error:
    return;
}

static enum DeviceState
trash_start(struct Storage* self_)
{
    struct Trash* self = containerof(self_, struct Trash, writer);
    self->iframe = self->settings.first_frame_id;
    return DeviceState_Running;
}

static enum DeviceState
trash_stop(struct Storage* self_)
{
    return DeviceState_Armed;
}

static enum DeviceState
trash_append(struct Storage* self_,
             const struct VideoFrame* frames,
             size_t* nbytes)
{
    struct Trash* self = containerof(self_, struct Trash, writer);

    {
        const uint8_t* const beg = (const uint8_t*)frames;
        const uint8_t* const end = beg + *nbytes;
        const uint8_t* cur = beg;
        while (cur < end) {
            const struct VideoFrame* im = (const struct VideoFrame*)cur;
            const size_t delta = im->bytes_of_frame;
            ++self->iframe;
            cur += delta;
        }
    }

    return DeviceState_Running;
}

static void
trash_destroy(struct Storage* self_)
{
    struct Trash* self = containerof(self_, struct Trash, writer);
    free(self);
}

static void
trash_reserve_image_shape(struct Storage* self_, const struct ImageShape* shape)
{ // no-op
}

struct Storage*
trash_init()
{
    struct Trash* self;
    CHECK(self = malloc(sizeof(*self)));
    memset(self, 0, sizeof(*self));

    self->writer =
      (struct Storage){ .state = DeviceState_AwaitingConfiguration,
                        .set = trash_set,
                        .get = trash_get,
                        .get_meta = trash_get_meta,
                        .start = trash_start,
                        .append = trash_append,
                        .stop = trash_stop,
                        .destroy = trash_destroy,
                        .reserve_image_shape = trash_reserve_image_shape };
    return &self->writer;
Error:
    return 0;
}
