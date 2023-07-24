#include "device/props/components.h"
#include "platform.h"

#include <cmath>

namespace {
/// This is used for animating the parameter in im_fill_pattern.
/// This timebase gets shared between all "pattern" cameras and as a result they
/// are synchronized.
///
/// Thread safety: Don't really need to worry about since we don't care who
/// wins during initialization. Afterwards it's effectively read only.
static struct
{
    struct clock clk;
    int is_initialized;
} g_animation_clk = { 0 };

float
get_animation_time_sec()
{
    float t;
    {
        struct clock* const clk = &g_animation_clk.clk;
        if (!g_animation_clk.is_initialized) {
            clock_init(clk);
            g_animation_clk.is_initialized = 1;
        }
        t = (float)clock_toc_ms(clk) * 1e-3f;
    }
    return t;
}

template<typename T>
void
im_fill_pattern(const struct ImageShape* const shape,
                float ox,
                float oy,
                T* buf)
{
    float t = get_animation_time_sec();

    const float cx = ox + 0.5f * (float)shape->dims.width;
    const float cy = oy + 0.5f * (float)shape->dims.height;
    for (uint32_t y = 0; y < shape->dims.height; ++y) {
        const float dy = y - cy;
        const float dy2 = dy * dy;
        for (uint32_t x = 0; x < shape->dims.width; ++x) {
            const size_t o = (size_t)shape->strides.width * x +
                             (size_t)shape->strides.height * y;
            const float dx = x - cx;
            const float dx2 = dx * dx;
            buf[o] =
              (T)(127.0f *
                  (sinf(6.28f * (t * 10.0f + (dx2 + dy2) * 1e-2f)) + 1.0f));
        }
    }
}
} // end namespace ::{anonymous}

extern "C"
{
    void im_fill_pattern_u8(const struct ImageShape* shape,
                            float ox,
                            float oy,
                            uint8_t* buf)
    {
        im_fill_pattern<uint8_t>(shape, ox, oy, buf);
    }

    void im_fill_pattern_i8(const struct ImageShape* shape,
                            float ox,
                            float oy,
                            int8_t* buf)
    {
        im_fill_pattern<int8_t>(shape, ox, oy, buf);
    }

    void im_fill_pattern_u16(const struct ImageShape* shape,
                             float ox,
                             float oy,
                             uint16_t* buf)
    {
        im_fill_pattern<uint16_t>(shape, ox, oy, buf);
    }

    void im_fill_pattern_i16(const struct ImageShape* shape,
                             float ox,
                             float oy,
                             int16_t* buf)
    {
        im_fill_pattern<int16_t>(shape, ox, oy, buf);
    }

    void im_fill_pattern_f32(const struct ImageShape* shape,
                             float ox,
                             float oy,
                             float* buf)
    {
        im_fill_pattern<float>(shape, ox, oy, buf);
    }
};
