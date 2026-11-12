# Regenerate only when build inputs change, not when the app is launched or reinstalled.
file(GLOB_RECURSE sinestack_build_inputs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/*.cmake")
set(sinestack_build_info "${CMAKE_CURRENT_BINARY_DIR}/generated/BuildInfo.cpp")
add_custom_command(OUTPUT "${sinestack_build_info}"
    COMMAND "${CMAKE_COMMAND}" "-DOUTPUT=${sinestack_build_info}" "-DVERSION=${PROJECT_VERSION}"
        "-DHEADER=${CMAKE_CURRENT_SOURCE_DIR}/Source/BuildInfo.h" "-DBUILT_AT=${SINESTACK_BUILD_UTC}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/WriteBuildInfo.cmake"
    DEPENDS ${sinestack_build_inputs} "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
        "${CMAKE_CURRENT_BINARY_DIR}/CMakeCache.txt" "${SINESTACK_ROOT}/version.json"
    COMMENT "Recording Sine Stack version and build time"
    VERBATIM)
target_sources(SineStack PRIVATE "${sinestack_build_info}")
