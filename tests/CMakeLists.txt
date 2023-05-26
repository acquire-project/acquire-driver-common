if(${NOTEST})
    message(STATUS "Skipping test targets")
else()

    #
    # PARAMETERS
    #
    set(project acquire-driver-common) # CMAKE_PROJECT_NAME gets overridden if this is a subtree of another project
    set(driver acquire-driver-common)

    #
    # Tests
    #
    set(tests
        can-load-driver-interface
        list-devices
        storage-get-meta
        unit-tests
    )

    foreach(name ${tests})
        set(tgt ${project}-${name})
        add_executable(${tgt} ${name}.cpp)
        target_compile_definitions(${tgt} PUBLIC "TEST=\"${tgt}\"")
        set_target_properties(${tgt} PROPERTIES
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
        target_include_directories(${tgt} PRIVATE "../src/simcams")
        target_link_libraries(${tgt}
            acquire-core-platform
            acquire-core-logger
            acquire-device-kit
            acquire-device-hal
            acquire-device-properties
        )
        add_test(NAME test-${tgt} COMMAND ${tgt})
        set_tests_properties(test-${tgt} PROPERTIES LABELS anyplatform)
    endforeach()

    #
    # Copy driver to tests
    #
    list(POP_FRONT tests onename)
    add_custom_target(${project}-copy-${driver}-for-tests
        COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:${driver}>
        $<TARGET_FILE_DIR:${project}-${onename}>
        DEPENDS ${driver}
        COMMENT "Copying ${driver} to $<TARGET_FILE_DIR:${project}-${onename}>"
    )

    foreach(name ${tests})
        add_dependencies(${tgt} ${project}-copy-${driver}-for-tests)
    endforeach()
endif()
