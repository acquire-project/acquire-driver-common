#include "platform.h"
#include "logger.h"

#include <cstdio>
#include <stdexcept>
#include <vector>

#define L (aq_logger)
#define LOG(...) L(0, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define ERR(...) L(1, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)

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

typedef struct Driver* (*init_func_t)(void (*reporter)(int is_error,
                                                       const char* file,
                                                       int line,
                                                       const char* function,
                                                       const char* msg));
//
//      TEST DRIVER
//

int
main()
{
    logger_set_reporter(reporter);
    struct lib lib = { 0 };
    if (!lib_open_by_name(&lib, "acquire-driver-common")) {
        ERR("Failed to \"acquire-driver-common\".");
        exit(2);
    }

    struct testcase
    {
        const char* name;
        int (*test)();
    };
    const std::vector<testcase> tests{
#define CASE(e) { .name = #e, .test = (int (*)())lib_load(&lib, #e) }
        CASE(unit_test_basic_device_kind_to_string_is_complete),
#undef CASE
    };

    bool any = false;
    for (const auto& test : tests) {
        LOG("Running %s", test.name);
        if (!(test.test())) {
            ERR("unit test failed: %s", test.name);
            any = true;
        }
    }
    lib_close(&lib);
    return any;
}
