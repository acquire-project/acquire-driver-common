//! Exercises the api for inspecting digital lines (for trigger assignment)
#include "acquire.h"
#include "device/hal/device.manager.h"
#include "logger.h"
#include <cstdio>
#include <stdexcept>
#include <string>

void
reporter(int is_error,
         const char* file,
         int line,
         const char* function,
         const char* msg)
{
    fprintf(is_error ? stderr : stdout,
            "%s%s(%d) - %s: %s\n",
            is_error ? "ERROR " : "",
            file,
            line,
            function,
            msg);
}

/// Helper for passing size static strings as function args.
/// For a function: `f(char*,size_t)` use `f(SIZED("hello"))`.
/// Expands to `f("hello",5)`.
#define SIZED(str) str, sizeof(str)

#define L (aq_logger)
#define LOG(...) L(0, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define ERR(...) L(1, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define EXPECT(e, ...)                                                         \
    do {                                                                       \
        if (!(e)) {                                                            \
            char buf[1 << 8] = { 0 };                                          \
            ERR(__VA_ARGS__);                                                  \
            snprintf(buf, sizeof(buf) - 1, __VA_ARGS__);                       \
            throw std::runtime_error(buf);                                     \
        }                                                                      \
    } while (0)
#define CHECK(e) EXPECT(e, "Expression evaluated as false: %s", #e)
#define DEVOK(e) CHECK(Device_Ok == (e))
#define OK(e) CHECK(AcquireStatus_Ok == (e))

int
main()
{
    AcquireRuntime* runtime = 0;
    try {
        {
            runtime = acquire_init(reporter);
            auto dm = acquire_device_manager(runtime);
            CHECK(runtime);
            CHECK(dm);

            AcquireProperties props = {};
            OK(acquire_get_configuration(runtime, &props));

            DEVOK(device_manager_select(dm,
                                        DeviceKind_Camera,
                                        SIZED("simulated: empty") - 1,
                                        &props.video[0].camera.identifier));
            DEVOK(device_manager_select(dm,
                                        DeviceKind_Storage,
                                        SIZED("trash") - 1,
                                        &props.video[0].storage.identifier));

            OK(acquire_configure(runtime, &props));

            AcquirePropertyMetadata metadata = { 0 };
            OK(acquire_get_configuration_metadata(runtime, &metadata));

            // List digital lines
            int i_line = -1;
            {
                for (int i = 0;
                     i < metadata.video[0].camera.digital_lines.line_count;
                     ++i) {
                    LOG("Line %2d: %s",
                        i,
                        metadata.video[0].camera.digital_lines.names[i]);
                }
            }
        }
        LOG("OK");
        return 0;
    } catch (const std::runtime_error& e) {
        ERR("Runtime error: %s", e.what());

    } catch (...) {
        ERR("Uncaught exception");
    }
    acquire_shutdown(runtime);
    return 1;
}
