//! Test: Aborting an acquisition while waiting for a trigger should return
//! the runtime to a stopped state without generating errors.

#include "acquire.h"
#include "device/hal/device.manager.h"
#include "device/props/components.h"
#include "logger.h"
#include "platform.h"
#include <cstring>
#include <stdexcept>

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

/// @returns the index of the software trigger line or -1 if none is found
static uint8_t
select_software_trigger_line(const AcquirePropertyMetadata* metadata)
{
    // get the software trigger line
    int i_line = -1;
    {
        for (int i = 0; i < metadata->video[0].camera.digital_lines.line_count;
             ++i) {
            if (strcmp(metadata->video[0].camera.digital_lines.names[i],
                       "software") == 0)
                i_line = i;
        }
        EXPECT(i_line >= 0, "Did not find software trigger line.");
        LOG("Software trigger line: %d", i_line);
    }
    return (uint8_t)i_line;
}

static void
setup(AcquireRuntime* runtime)
{
    auto dm = acquire_device_manager(runtime);
    AcquireProperties props = {};
    DEVOK(device_manager_select(dm,
                                DeviceKind_Camera,
                                SIZED("simulated: empty") - 1,
                                &props.video[0].camera.identifier));
    DEVOK(device_manager_select(dm,
                                DeviceKind_Storage,
                                SIZED("trash") - 1,
                                &props.video[0].storage.identifier));
    props.video[0].camera.settings.input_triggers.frame_start.enable = 1;
    props.video[0].camera.settings.input_triggers.frame_start.edge =
      TriggerEdge_Rising;
    props.video[0].max_frame_count = 10;
    OK(acquire_configure(runtime, &props));

    AcquirePropertyMetadata metadata = { 0 };
    OK(acquire_get_configuration_metadata(runtime, &metadata));
    props.video[0].camera.settings.input_triggers.frame_start = {
        .enable = 1,
        .line = select_software_trigger_line(&metadata),
        .kind = Signal_Input,
        .edge = TriggerEdge_Rising,
    };

    OK(acquire_configure(runtime, &props));
}

int
main()
{
    auto runtime = acquire_init(reporter);
    try {
        CHECK(runtime);
        setup(runtime);
        OK(acquire_start(runtime));
        clock_sleep_ms(0, 500);
        OK(acquire_abort(runtime));
        OK(acquire_shutdown(runtime));
        return 0;
    } catch (const std::runtime_error& e) {
        ERR("Runtime error: %s", e.what());
    } catch (...) {
        ERR("Uncaught exception");
    }
    acquire_shutdown(runtime);
    return 1;
}
