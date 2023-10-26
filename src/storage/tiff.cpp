#include "device/props/storage.h"
#include "device/kit/storage.h"
#include "logger.h"
#include "platform.h"

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <string>
#include <algorithm>

using namespace std;

#define LOG(...) aq_logger(0, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOGE(...) aq_logger(1, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define EXPECT(e, ...)                                                         \
    do {                                                                       \
        if (!(e)) {                                                            \
            LOGE(__VA_ARGS__);                                                 \
            goto Error;                                                        \
        }                                                                      \
    } while (0)
#define CHECK(e) EXPECT(e, "Expression evaluated as false:\n\t%s", #e)

#define countof(e) (sizeof(e) / sizeof(*(e)))
#define containerof(ptr, T, V) ((T*)(((char*)(ptr)) - offsetof(T, V)))

namespace {

/// @brief Utility for tracking strings during ifd construction
struct StringSection final
{
    int64_t offset;   ///< next string offset within file
    int64_t capacity; ///< currently allocated capacity in 'data'
    int64_t size;     ///< next string offset within 'data'
    char* data;       ///< root pointer for buffer of strings

    StringSection();
    ~StringSection();

    void reset(int64_t offset) noexcept;
    char* reserve(size_t nbytes) noexcept;
};

struct Tiff final : public Storage
{
    string filename_;
    string external_metadata_;
    struct PixelScale pixel_scale_um_;
    struct file file_;
    uint64_t last_offset_, last_ifd_next_offset_;
    size_t frame_count_; // the number of frames written to the current file

    // Context for constructing string storage during ifd assembly.
    // This acquires memory. Kept in object context to reuse that memory.
    StringSection ifd_strings_;

    Tiff() noexcept;
    ~Tiff() noexcept;

    int set(const struct StorageProperties* settings) noexcept;
    void get(struct StorageProperties* settings) const noexcept;
    static void get_meta(struct StoragePropertyMetadata* meta) noexcept;
    int start() noexcept;
    int stop() noexcept;
    int append(const struct VideoFrame* frames, size_t nbytes) noexcept;
    void write_(uint64_t offset, void* buf, size_t nbytes) noexcept;

  private:
    void terminate_ifd_list() noexcept;
};

#pragma pack(push, 1)
struct header_t
{
    uint16_t fmt;
    uint16_t ver;
    uint16_t sizeof_offset;
    uint16_t zero;
    uint64_t first_ifd;
};
struct tag_t
{
    uint16_t tag, type;
    uint64_t count;
    union
    {
        uint16_t u16;
        uint32_t u32;
        uint64_t u64;
        struct
        {
            uint32_t num;
            uint32_t den;
        } rational;
        int8_t chars[8];
    } value;

    static tag_t as_u64(uint16_t tag, uint64_t value) noexcept;
    static tag_t as_u32(uint16_t tag, uint32_t value) noexcept;
    static tag_t as_u16(uint16_t tag, uint16_t value) noexcept;
    static tag_t as_rational(uint16_t tag, uint32_t num, uint32_t den) noexcept;
    static tag_t as_formatted_string(StringSection& strings,
                                     uint16_t tag,
                                     const char* fmt,
                                     va_list args) noexcept;
};
template<size_t N>
struct ifd_t
{
    uint64_t ntags;
    struct tag_t tags[N];
    uint64_t next;
};
#pragma pack(pop)

tag_t
tag_t::as_u64(uint16_t tag, uint64_t value) noexcept
{
    return tag_t{
        .tag = tag, .type = 16, .count = 1, .value = { .u64 = value }
    };
}

tag_t
tag_t::as_u32(uint16_t tag, uint32_t value) noexcept
{
    return tag_t{
        .tag = tag, .type = 4, .count = 1, .value = { .u32 = value }
    };
}

tag_t
tag_t::as_u16(uint16_t tag, uint16_t value) noexcept
{
    return tag_t{
        .tag = tag, .type = 3, .count = 1, .value = { .u16 = value }
    };
}

tag_t
tag_t::as_rational(uint16_t tag, uint32_t num, uint32_t den) noexcept
{
    return tag_t{ .tag = tag,
                  .type = 5,
                  .count = 1,
                  .value = { .rational = { .num = num, .den = den } } };
}

tag_t
tag_t::as_formatted_string(StringSection& strings,
                           uint16_t tag,
                           const char* fmt,
                           va_list args) noexcept
{
    va_list args_;
    va_copy(args_, args);

    uint64_t n = vsnprintf(0, 0, fmt, args_);
    auto out = tag_t{
        .tag = tag, .type = 2, .count = n + 1, .value = { .chars = { 0 } }
    };
    if (n > 7) {
        const uint64_t offset = strings.offset;
        char* buf = strings.reserve(n + 1);
        if (buf) {
            vsnprintf(buf, n + 1, fmt, args);
            out.value.u64 = offset;
        } else {
            // couldn't allocate buf
            out.count = 8;
            memcpy((char*)out.value.chars, "memfail", 8);
        }
    } else {
        vsnprintf((char*)out.value.chars, n + 1, fmt, args);
    }
    va_end(args_);
    return out;
}

tag_t
bits_per_sample(uint16_t b)
{
    return tag_t::as_u16(258, b);
}

tag_t
image_description(StringSection& strings, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    const auto out = tag_t::as_formatted_string(strings, 270, fmt, args);
    va_end(args);
    return out;
}

tag_t
image_width(uint64_t w)
{
    return tag_t::as_u32(256, (uint32_t)w);
}

tag_t
image_length(uint64_t h)
{
    return tag_t::as_u32(257, (uint32_t)h);
}

tag_t
new_subfile_type_multipage()
{
    return tag_t::as_u32(254, 0x2);
}

tag_t
orientation_top_left()
{
    return tag_t::as_u16(274, 1);
}

tag_t
photometric_interpretation_black_is_zero()
{
    return tag_t::as_u16(262, 1);
}

tag_t
resolution_unit_centimeter()
{
    return tag_t::as_u16(296, 3);
}

tag_t
rows_per_strip(uint64_t v)
{
    return tag_t::as_u32(278, (uint32_t)v);
}

tag_t
sample_format(SampleType type)
{
    switch (type) {
        case SampleType_u8:
        case SampleType_u10:
        case SampleType_u12:
        case SampleType_u14:
        case SampleType_u16:
            return tag_t::as_u16(339, 1); // unsigned
        case SampleType_i8:
        case SampleType_i16:
            return tag_t::as_u16(339, 2); // signed
        case SampleType_f32:
            return tag_t::as_u16(339, 3); // float
        default:
            return tag_t::as_u16(339, 4); // unknown
    }
}

/// @brief Tag indicates 1 sample per pixel
tag_t
samples_per_pixel_grayscale()
{
    return tag_t::as_u16(277, 1); // 1 sample per pixel
}

tag_t
strip_byte_counts(uint64_t v)
{
    return tag_t::as_u64(279, v);
}

tag_t
strip_offsets(uint64_t v)
{
    return tag_t::as_u64(273, v);
}

tag_t
uncompressed()
{
    return tag_t::as_u16(259, 1);
}

/// pixels per resolution unit
tag_t
x_resolution(uint32_t num, uint32_t den)
{
    if (den == 0) {
        return tag_t::as_rational(282, 0, 1);
    }
    return tag_t::as_rational(282, num, den);
}

/// pixels per resolution unit
tag_t
y_resolution(uint32_t num, uint32_t den)
{
    if (den == 0) {
        return tag_t::as_rational(282, 0, 1);
    }
    return tag_t::as_rational(283, num, den);
}

header_t
header()
{
    return header_t{
        .fmt = 0x4949,
        .ver = 0x002B,
        .sizeof_offset = 8,
        .zero = 0,
        .first_ifd = sizeof(header_t),
    };
}

enum DeviceState
tiff_set(struct Storage*, const struct StorageProperties* properties);

void
tiff_get(const struct Storage*, struct StorageProperties* settings);

void
tiff_get_meta(const struct Storage*, struct StoragePropertyMetadata* meta);

enum DeviceState
tiff_start(struct Storage*);

enum DeviceState
tiff_append(struct Storage* self_,
            const struct VideoFrame* frame,
            size_t* nbytes);

enum DeviceState
tiff_stop(struct Storage*);

void
tiff_destroy(struct Storage*);

void
tiff_reserve_image_shape(struct Storage*, const struct ImageShape* shape);

StringSection::StringSection()
  : offset(0)
  , capacity(0)
  , size(0)
  , data(0)
{
}

StringSection::~StringSection()
{
    free(data);
}

void
StringSection::reset(int64_t offset_) noexcept
{
    offset = offset_;
    size = 0;
}

char*
StringSection::reserve(size_t nbytes) noexcept
{
    if (size + nbytes > size_t(capacity)) {
        capacity = size + nbytes;
        void* tmp = data;
        data = (char*)realloc(data, capacity);
        if (!data) {
            free(tmp);
            return 0;
        }
    }
    char* out = data + size;
    memset(out, 0, nbytes);
    size += nbytes;
    offset += nbytes;
    return out;
}

Tiff::Tiff() noexcept
  : Storage{
    .state = DeviceState_AwaitingConfiguration,
    .set = ::tiff_set,
    .get = ::tiff_get,
    .get_meta = ::tiff_get_meta,
    .start = ::tiff_start,
    .append = ::tiff_append,
    .stop = ::tiff_stop,
    .destroy = ::tiff_destroy,
    .reserve_image_shape = ::tiff_reserve_image_shape,
  }
  , pixel_scale_um_{.x=1.0,.y=1.0}
  , file_{}
  , last_offset_(0)
  , last_ifd_next_offset_(0)
  , frame_count_(0)
{
}

Tiff::~Tiff() noexcept
{
    stop();
}

bool
validate_json(const char* str, size_t nbytes)
{
    if (!str || !nbytes)
        return true;
    // Don't do full json validation here, but make sure it at least
    // begins and ends with '{' and '}'
    EXPECT(nbytes >= 3,
           "nbytes (%d) is too small. Expected a null-terminated json string.",
           (int)nbytes);
    EXPECT(str[nbytes - 1] == '\0', "String must be null-terminated");
    EXPECT(str[0] == '{', "json string must start with \'{\'");
    EXPECT(str[nbytes - 2] == '}', "json string must end with \'}\'");
    return true;
Error:
    return false;
}

int
Tiff::set(const struct StorageProperties* settings) noexcept
{
    EXPECT(settings->filename.str, "Filename string is NULL.");
    EXPECT(settings->filename.nbytes, "Filename string is zero size.");
    {
        string filename(settings->filename.str);

        // Validate and copy the filename
        CHECK(file_is_writable(filename.c_str(), filename.length()));
        filename_ = filename;

        // Validate and copy external metadata
        // If the string isn't null, or "" then it needs to be at least "{}"
        if (settings->external_metadata_json.str &&
            settings->external_metadata_json.nbytes > 1) {
            CHECK(validate_json(settings->external_metadata_json.str,
                                settings->external_metadata_json.nbytes));
            external_metadata_ = string(settings->external_metadata_json.str);
        }
    }
    pixel_scale_um_ = settings->pixel_scale_um;
    return 1;
Error:
    return 0;
}

void
Tiff::get(struct StorageProperties* settings) const noexcept
{
    settings->filename.str = (char*)filename_.c_str();
    settings->filename.nbytes = filename_.size();
    settings->pixel_scale_um = pixel_scale_um_;
}

void
Tiff::get_meta(struct StoragePropertyMetadata* meta) noexcept
{
    CHECK(meta);
    *meta = { 0 };
Error:
    return;
}

int
Tiff::start() noexcept
{
    frame_count_ = 0;
    CHECK(file_create(&file_, filename_.c_str(), filename_.length()));
    {
        const auto hdr = header();
        write_(0, (void*)&hdr, sizeof(hdr));
        last_offset_ = sizeof(hdr);
    }
    LOG("TIFF: Streaming to \"%s\"", filename_.c_str());
    return 1;
Error:
    return 0;
}

void
Tiff::terminate_ifd_list() noexcept
{
    // zero out the last next offset.
    uint64_t data(0);
    write_(last_ifd_next_offset_, &data, sizeof(data));
}

int
Tiff::stop() noexcept
{
    if (state == DeviceState_Running) {
        terminate_ifd_list();
        file_close(&file_);
        state = DeviceState_Armed;
        frame_count_ = 0;
        LOG("TIFF: Writer stop");
    }
    return 1;
}

constexpr uint64_t
align8(uint64_t v)
{
    return (v + 7) >> 3 << 3;
}

int
Tiff::append(const struct VideoFrame* frames, size_t nbytes) noexcept
{
    if (!nbytes)
        return 1;

    const struct VideoFrame* cur = 0;
    auto next = [&]() -> const struct VideoFrame*
    {
        uint8_t* p = ((uint8_t*)cur) + cur->bytes_of_frame;
        size_t o = (p - (uint8_t*)frames);
        return (o < nbytes) ? (const struct VideoFrame*)p : nullptr;
    };
    try {
        for (cur = frames; cur; cur = next()) {
            using ifdN_t = ifd_t<16>;
            const auto bytes_of_image = cur->bytes_of_frame - sizeof(*cur);

            // compute offsets
            const auto section_ifd = align8(last_offset_);
            const auto section_data = align8(section_ifd + sizeof(ifdN_t));
            const auto section_strings = align8(section_data + bytes_of_image);

            // assemble ifd
            ifd_strings_.reset(section_strings);
            ifdN_t ifd{
                countof(ifd.tags),
                {
                  // required fields for grayscale images
                  image_width(cur->shape.dims.width),
                  image_length(cur->shape.dims.height),
                  bits_per_sample(
                    (uint16_t)(8 * bytes_of_type(cur->shape.type))),
                  uncompressed(),
                  photometric_interpretation_black_is_zero(),
                  strip_offsets(section_data),
                  rows_per_strip(cur->shape.dims.height),
                  strip_byte_counts(bytes_of_image),
                  x_resolution(10000 * 10000,
                               10000 * (uint32_t)pixel_scale_um_.x),
                  y_resolution(10000 * 10000,
                               10000 * (uint32_t)pixel_scale_um_.y),
                  resolution_unit_centimeter(),
                  orientation_top_left(),
                  sample_format(cur->shape.type),
                  samples_per_pixel_grayscale(),
                  new_subfile_type_multipage(),

                  (frame_count_ == 0) && (external_metadata_.length() > 0)
                    ? image_description(
                        ifd_strings_,
                        "{\"frame_id\":%llu,\"hardware_frame_id\":%llu,"
                        "\"timestamps\":{"
                        "\"runtime\":%llu,\"hardware\":%llu},\"metadata\":%s}",
                        cur->frame_id,
                        cur->hardware_frame_id,
                        cur->timestamps.acq_thread,
                        cur->timestamps.hardware,
                        external_metadata_.c_str())
                    : image_description(ifd_strings_,
                                        "{\"frame_id\":%llu,\"hardware_frame_"
                                        "id\":%llu,\"timestamps\":{"
                                        "\"runtime\":%llu,\"hardware\":%llu}}",
                                        cur->frame_id,
                                        cur->hardware_frame_id,
                                        cur->timestamps.acq_thread,
                                        cur->timestamps.hardware),
                },
                align8(ifd_strings_.offset)
            };

            // write
            write_(section_ifd, &ifd, sizeof(ifd));
            write_(section_data, (void*)cur->data, bytes_of_image);
            write_(section_strings, ifd_strings_.data, ifd_strings_.size);

            // update markers
            last_ifd_next_offset_ = section_ifd + offsetof(ifdN_t, next);
            last_offset_ = ifd.next;
            ++frame_count_;
        }
    } catch (const std::exception& e) {
        LOGE("Exception: %s", e.what());
        return 0;
    } catch (...) {
        LOGE("Unkown Exception");
        return 0;
    }
    return 1;
}

void
Tiff::write_(uint64_t offset, void* buf, size_t nbytes) noexcept
{
    CHECK(file_write(&file_, offset, (uint8_t*)buf, (uint8_t*)buf + nbytes));
    return;
Error:
    stop();
}

enum DeviceState
tiff_set(struct Storage* self_, const struct StorageProperties* settings)
{
    struct Tiff* self = (struct Tiff*)self_;
    if (self->set(settings))
        return DeviceState_Armed;
    else
        return DeviceState_AwaitingConfiguration;
}

void
tiff_get(const struct Storage* self_, struct StorageProperties* settings)
{
    struct Tiff* self = (struct Tiff*)self_;
    self->get(settings);
}

void
tiff_get_meta(const struct Storage* self_, struct StoragePropertyMetadata* meta)
{
    struct Tiff* self = (struct Tiff*)self_;
    self->get_meta(meta);
}

enum DeviceState
tiff_start(struct Storage* self_)
{
    struct Tiff* self = (struct Tiff*)self_;
    CHECK(self->start());
    return DeviceState_Running;
Error:
    return DeviceState_AwaitingConfiguration;
}

enum DeviceState
tiff_stop(struct Storage* self_)
{
    struct Tiff* self = (struct Tiff*)self_;
    CHECK(self->stop());
    return DeviceState_Armed;
Error:
    return DeviceState_AwaitingConfiguration;
}

enum DeviceState
tiff_append(struct Storage* self_,
            const struct VideoFrame* frames,
            size_t* nbytes)
{
    struct Tiff* self = (struct Tiff*)self_;
    CHECK(self->append(frames, *nbytes));
    return DeviceState_Running;
Error:
    return tiff_stop(self_);
}

void
tiff_destroy(struct Storage* self_)
{
    struct Tiff* self = (struct Tiff*)self_;
    if (self_ && self_->stop)
        self_->stop(self_);
    delete self;
}

void
tiff_reserve_image_shape(struct Storage* self_, const struct ImageShape* shape)
{ // no-op
}

} // end namespace ::{anonymous}

extern "C" struct Storage*
tiff_init()
{
    return new Tiff();
}
