#include "device/props/storage.h"
#include "device/kit/storage.h"
#include "platform.h"
#include "logger.h"

#include <string.h>
#include <stdlib.h>

#define LOG(...) aq_logger(0, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOGE(...) aq_logger(1, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define CHECK(e)                                                               \
    do {                                                                       \
        if (!(e)) {                                                            \
            LOGE("Expression evaluated as false:\n\t%s", #e);                  \
            goto Error;                                                        \
        }                                                                      \
    } while (0)
#define TODO                                                                   \
    do {                                                                       \
        LOGE("TODO: Unimplemented");                                           \
        goto Error;                                                            \
    } while (0)

#define containerof(ptr, T, V) ((T*)(((char*)(ptr)) - offsetof(T, V)))

struct Raw
{
    struct Storage writer;
    struct StorageProperties properties;
    struct file file;
    size_t offset;
};

static enum DeviceState
raw_set(struct Storage* self_, const struct StorageProperties* properties)
{
    struct Raw* self = containerof(self_, struct Raw, writer);
    const char* filename = properties->filename.str;
    const size_t nbytes = properties->filename.nbytes;

    // Validate
    CHECK(file_is_writable(filename, nbytes));

    // copy in the properties
    CHECK(storage_properties_copy(&self->properties, properties));

    return DeviceState_Armed;
Error:
    return DeviceState_AwaitingConfiguration;
}

static void
raw_get(const struct Storage* self_, struct StorageProperties* settings)
{
    struct Raw* self = containerof(self_, struct Raw, writer);
    *settings = self->properties;
}

static void
raw_get_meta(const struct Storage* self_, struct StoragePropertyMetadata* meta)
{
    CHECK(meta);
    *meta = (struct StoragePropertyMetadata){ 0 };
Error:
    return;
}

static enum DeviceState
raw_start(struct Storage* self_)
{
    struct Raw* self = containerof(self_, struct Raw, writer);
    CHECK(file_create(&self->file,
                      self->properties.filename.str,
                      self->properties.filename.nbytes));
    LOG("RAW: Frame header size %d bytes", (int)sizeof(struct VideoFrame));
    return DeviceState_Running;
Error:
    return DeviceState_AwaitingConfiguration;
}

static enum DeviceState
raw_stop(struct Storage* self_)
{
    struct Raw* self = containerof(self_, struct Raw, writer);
    file_close(&self->file);
    return DeviceState_Armed;
}

static enum DeviceState
raw_append(struct Storage* self_,
           const struct VideoFrame* frames,
           size_t* nbytes)
{
    struct Raw* self = containerof(self_, struct Raw, writer);
    CHECK(file_write(&self->file,
                     self->offset,
                     (uint8_t*)frames,
                     ((uint8_t*)frames) + *nbytes));
    self->offset += *nbytes;

    return DeviceState_Running;
Error:
    *nbytes = 0;
    return raw_stop(self_);
}

static void
raw_destroy(struct Storage* writer_)
{
    struct Raw* self = containerof(writer_, struct Raw, writer);
    raw_stop(writer_);
    storage_properties_destroy(&self->properties);
    free(self);
}

static void
raw_reserve_image_shape(struct Storage* self_, const struct ImageShape* shape)
{ // no-op
}

struct Storage*
raw_init()
{
    struct Raw* self;
    CHECK(self = malloc(sizeof(*self)));
    memset(self, 0, sizeof(*self));
    const struct PixelScale pixel_scale_um = { 1, 1 };

    CHECK(storage_properties_init(&self->properties,
                                  0,
                                  "out.raw",
                                  sizeof("out.raw"),
                                  0,
                                  0,
                                  pixel_scale_um));
    self->writer =
      (struct Storage){ .state = DeviceState_AwaitingConfiguration,
                        .set = raw_set,
                        .get = raw_get,
                        .get_meta = raw_get_meta,
                        .start = raw_start,
                        .append = raw_append,
                        .stop = raw_stop,
                        .destroy = raw_destroy,
                        .reserve_image_shape = raw_reserve_image_shape };
    return &self->writer;
Error:
    return 0;
}
