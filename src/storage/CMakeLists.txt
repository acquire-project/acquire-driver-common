set(tgt storage)
add_library(${tgt} STATIC
        basic.storage.c
        basic.storage.h
        raw.c
        side-by-side-tiff.cpp
        tiff.cpp
        trash.c
        )
target_enable_simd(${tgt})
target_link_libraries(${tgt} PUBLIC pcg)
set_target_properties(${tgt} PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
target_enable_simd(${tgt})
target_link_libraries(${tgt} PRIVATE
        acquire-core-platform
        acquire-core-logger
        acquire-device-kit
        )
set_target_properties(${tgt} PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
