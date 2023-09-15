if(NOT TARGET acquire-device-kit)
        add_subdirectory(acquire-core-libs)
endif()

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
target_add_git_versioning(${tgt})

install(TARGETS ${tgt} LIBRARY DESTINATION lib)
