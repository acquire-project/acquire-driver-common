add_subdirectory(3rdParty)

set(tgt simcams)
add_library(${tgt} STATIC
        simulated.camera.h
        simulated.camera.c
        popcount.cpp
        imfill.pattern.cpp
)
target_enable_simd(${tgt})
target_link_libraries(${tgt} PUBLIC
        acquire-core-logger
        acquire-core-platform
        acquire-device-kit
        pcg
)
