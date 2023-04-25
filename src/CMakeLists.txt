aq_require(acquire-device-kit)

add_subdirectory(simcams)
add_subdirectory(storage)

set(tgt acquire-driver-common)
add_library(${tgt} MODULE
        identifiers.h
        basics.driver.c
)
target_enable_simd(${tgt})
target_link_libraries(${tgt} PRIVATE
        acquire-core-logger
        acquire-device-kit
        simcams
        storage
)
set_target_properties(${tgt} PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

install(TARGETS ${tgt} LIBRARY DESTINATION lib)
