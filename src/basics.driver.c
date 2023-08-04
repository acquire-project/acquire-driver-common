#include "device/kit/driver.h"
#include "device/kit/camera.h"
#include "device/kit/storage.h"
#include "identifiers.h"
#include "logger.h"

#include "simcams/simulated.camera.h"
#include "storage/basic.storage.h"

#include <stdlib.h>
#include <string.h>

#define containerof(P, T, F) ((T*)(((char*)(P)) - offsetof(T, F)))

#define L aq_logger
#define LOG(...) L(0, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOGE(...) L(1, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define EXPECT(e, ...)                                                         \
    do {                                                                       \
        if (!(e)) {                                                            \
            LOGE(__VA_ARGS__);                                                 \
            goto Error;                                                        \
        }                                                                      \
    } while (0)
#define CHECK(e) EXPECT(e, "Expression evaluated as false:\n\t%s", #e)

struct BasicsDriver
{
    struct Driver driver;
};

const char*
basic_device_kind_to_string(enum BasicDeviceKind kind)
{
    // clang-format off
    switch (kind) {
#define CASE(e) case e: return #e
        CASE(BasicDevice_Camera_Random);
        CASE(BasicDevice_Camera_Sin);
        CASE(BasicDevice_Camera_Empty);
        CASE(BasicDevice_Storage_Raw);
        CASE(BasicDevice_Storage_Tiff);
        CASE(BasicDevice_Storage_Trash);
        CASE(BasicDevice_Storage_SideBySideTiffJson);
        CASE(BasicDeviceKindCount);
#undef CASE
        default:
            return "(unknown)";
    }
    // clang-format on
}

#ifndef NO_UNIT_TESTS
acquire_export int
unit_test_basic_device_kind_to_string_is_complete()
{
    for (int i = 0; i < BasicDeviceKindCount; ++i) {
        if (basic_device_kind_to_string(i)[0] == '(')
            return 0;
    }
    return 1;
}
#endif // NO_UNIT_TESTS

static unsigned
basic_device_count(struct Driver* driver)
{
    return BasicDeviceKindCount;
}

static enum DeviceStatusCode
basic_device_describe(const struct Driver* driver,
                      struct DeviceIdentifier* identifier,
                      uint64_t i)
{

#define XXX(K, N, NAME)                                                        \
    [BasicDevice_##K##_##N] = {                                                \
        .device_id = BasicDevice_##K##_##N,                                    \
        .kind = DeviceKind_##K,                                                \
        .name = NAME,                                                          \
    }

    // clang-format off
    static struct DeviceIdentifier identifiers[] = {
        XXX(Camera,Random,"simulated: uniform random"),
        XXX(Camera,Sin,"simulated: radial sin"),
        XXX(Camera,Empty,"simulated: empty"),
        XXX(Storage,Raw,"raw"),
        XXX(Storage,Tiff,"tiff"),
        XXX(Storage,Trash,"trash"),
        XXX(Storage,SideBySideTiffJson,"tiff-json"),
    };
    // clang-format on
#undef XXX
    CHECK(i < BasicDeviceKindCount);
    memcpy(identifier, identifiers + i, sizeof(*identifier));
    return Device_Ok;
Error:
    return Device_Err;
}

static enum DeviceStatusCode
basic_device_open(struct Driver* driver,
                  uint64_t device_id,
                  struct Device** out)
{
    EXPECT(out, "Invalid parameter. out was NULL.");

    switch (device_id) {
        case BasicDevice_Camera_Random:
        case BasicDevice_Camera_Sin:
        case BasicDevice_Camera_Empty: {
            struct Camera* camera = 0;
            CHECK(camera = simcam_make_camera(device_id));
            *out = &camera->device;
            break;
        }
        case BasicDevice_Storage_Raw:
        case BasicDevice_Storage_Tiff:
        case BasicDevice_Storage_Trash:
        case BasicDevice_Storage_SideBySideTiffJson: {
            struct Storage* storage = 0;
            CHECK(storage = basics_make_storage(device_id));
            *out = &storage->device;
            break;
        }
        default:
            LOGE("Invalid parameter `device_id`. Got: %d", device_id);
            goto Error;
    }
    return Device_Ok;
Error:
    return Device_Err;
}

static enum DeviceStatusCode
basic_device_close(struct Driver* driver, struct Device* in)
{
    EXPECT(in, "Invalid parameter. Received NULL.");
    switch (in->identifier.device_id) {
        case BasicDevice_Camera_Random:
        case BasicDevice_Camera_Sin:
        case BasicDevice_Camera_Empty: {
            struct Camera* camera = containerof(in, struct Camera, device);
            return simcam_close_camera(camera);
        }
        case BasicDevice_Storage_Raw:
        case BasicDevice_Storage_Tiff:
        case BasicDevice_Storage_Trash:
        case BasicDevice_Storage_SideBySideTiffJson: {
            struct Storage* writer = containerof(in, struct Storage, device);
            writer->destroy(writer);
            return Device_Ok;
        }
        default: {
            char buf[128] = { 0 };
            device_identifier_as_debug_string(
              buf, sizeof(buf), &in->identifier);
            LOGE("Invalid device_id. Got: %s", buf);
        }
    }

Error:
    return Device_Err;
}

static enum DeviceStatusCode
basic_device_shutdown_driver(struct Driver* driver)
{
    if (driver) {
        basics_storage_shutdown(driver);
        free(driver);
    }
    return Device_Ok;
}

acquire_export struct Driver*
acquire_driver_init_v0(void (*reporter)(int is_error,
                                        const char* file,
                                        int line,
                                        const char* function,
                                        const char* msg))
{
    struct BasicsDriver* self;
    logger_set_reporter(reporter);
    CHECK(self = (struct BasicsDriver*)malloc(sizeof(*self)));
    *self = (struct BasicsDriver){
        .driver = { .device_count = basic_device_count,
                    .describe = basic_device_describe,
                    .open = basic_device_open,
                    .close = basic_device_close,
                    .shutdown = basic_device_shutdown_driver },
    };

    return &self->driver;
Error:
    return 0;
}
