# set(tgt acquire-simulated-camera)
# target_sources(${tgt} PRIVATE
# pcg_basic.h
# pcg_basic.c
# )
# target_include_directories(${tgt} PRIVATE ${CMAKE_CURRENT_LIST_DIR})

set(tgt pcg)
add_library(${tgt} OBJECT
        pcg_basic.h
        pcg_basic.c
)
target_include_directories(${tgt} PUBLIC ${CMAKE_CURRENT_LIST_DIR})
