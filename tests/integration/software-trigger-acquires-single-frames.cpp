//! Test: For simulated cameras, software trigger events should be usable to
//! control acquisition of single frames.

#include "acquire.h"
#include "device/hal/device.manager.h"
#include "device/props/components.h"
#include "device/props/device.h"
#include "logger.h"
#include "platform.h"
#include <cstdio>
#include <stdexcept>
#include <cstring>

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

/// Check that a==b
/// example: `ASSERT_EQ(int,"%d",42,meaning_of_life())`
#define ASSERT_EQ(T, fmt, a, b)                                                \
    do {                                                                       \
        T a_ = (T)(a);                                                         \
        T b_ = (T)(b);                                                         \
        EXPECT(a_ == b_, "Expected %s==%s but " fmt "!=" fmt, #a, #b, a_, b_); \
    } while (0)

/// @returns the index of the software trigger line or -1 if none is found
static int
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
    return i_line;
}

static void
setup(AcquireRuntime* runtime)
{
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

    props.video[0].camera.settings.binning = 1;
    props.video[0].camera.settings.pixel_type = SampleType_u12;
    props.video[0].camera.settings.shape = { .x = 1024, .y = 1024 };
    props.video[0].max_frame_count = 10;

    // Enable Software Trigger
    {
        int i_line = select_software_trigger_line(&metadata);
        CHECK(props.video[0].camera.settings.input_triggers.frame_start.kind ==
              Signal_Input);
        props.video[0].camera.settings.input_triggers.frame_start.edge =
          TriggerEdge_Rising;
        props.video[0].camera.settings.input_triggers.frame_start.line = i_line;
        props.video[0].camera.settings.input_triggers.frame_start.enable = 1;
    }

    OK(acquire_configure(runtime, &props));
}

static const VideoFrame*
next(const VideoFrame* frame)
{
    const uint8_t* f = (const uint8_t*)frame;
    return (const VideoFrame*)(f + frame->bytes_of_frame);
}

static int
frame_count(const VideoFrame* beg, const VideoFrame* end)
{
    int i = 0;
    for (const VideoFrame* cur = beg; cur < end; cur = next(cur))
        ++i;
    return i;
}

static bool
is_running(AcquireRuntime* runtime)
{
    return acquire_get_state(runtime) == DeviceState_Running;
}

int
main()
{
    float test_timeout_ms = 5000.0;
    AcquireRuntime* runtime = 0;
    try {
        runtime = acquire_init(reporter);
        setup(runtime);
        OK(acquire_start(runtime));

        struct clock t0;
        clock_init(&t0);
        int n = 0;
        while (is_running(runtime)) {
            VideoFrame *beg, *end;

            // Haven't triggered yet, so expect no data
            OK(acquire_map_read(runtime, 0, &beg, &end));
            EXPECT(end == beg, "Expected no available data.");

            OK(acquire_execute_trigger(runtime, 0));

            // wait for data
            while (end == beg && is_running(runtime) &&
                   clock_toc_ms(&t0) < test_timeout_ms) {
                // delay so loop isn't busy polling
                clock_sleep_ms(0, 10);
                OK(acquire_map_read(runtime, 0, &beg, &end));
            }

            if (end == beg && !is_running(runtime))
                break;

            OK(acquire_unmap_read(runtime, 0, (uint8_t*)end - (uint8_t*)beg));

            ASSERT_EQ(int, "%d", frame_count(beg, end), 1);
            LOG("Got a frame");
            ++n;
        }
        ASSERT_EQ(int, "%d", n, 10);

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
