# Both projects must configure from the same revision and version reservation.
get_filename_component(SINESTACK_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
file(GLOB_RECURSE sinestack_version_inputs CONFIGURE_DEPENDS
    "${SINESTACK_ROOT}/sine stack au/Source/*" "${SINESTACK_ROOT}/sine stack au/cmake/*"
    "${SINESTACK_ROOT}/sine stack vst3/Source/*" "${SINESTACK_ROOT}/sine stack vst3/cmake/*"
    "${SINESTACK_ROOT}/cmake/*.cmake")
file(GLOB sinestack_packaging_inputs CONFIGURE_DEPENDS
    "${SINESTACK_ROOT}/packaging/*.py" "${SINESTACK_ROOT}/packaging/*.sh"
    "${SINESTACK_ROOT}/packaging/*.txt" "${SINESTACK_ROOT}/packaging/resources/*")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${sinestack_version_inputs}
    ${sinestack_packaging_inputs}
    "${SINESTACK_ROOT}/sine stack au/CMakeLists.txt" "${SINESTACK_ROOT}/sine stack au/CMakePresets.json"
    "${SINESTACK_ROOT}/sine stack vst3/CMakeLists.txt" "${SINESTACK_ROOT}/sine stack vst3/CMakePresets.json"
    "${SINESTACK_ROOT}/scripts/versioning.py" "${SINESTACK_ROOT}/scripts/update_local.py"
    "${SINESTACK_ROOT}/version.json")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${SINESTACK_ROOT}/scripts/versioning.py"
    RESULT_VARIABLE version_result OUTPUT_VARIABLE version_json ERROR_VARIABLE version_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT version_result EQUAL 0)
    message(FATAL_ERROR "Could not prepare shared version: ${version_error}")
endif()
string(JSON SINESTACK_VERSION GET "${version_json}" version)
string(JSON SINESTACK_BUILD_UTC GET "${version_json}" built_at_utc)
string(JSON SINESTACK_SOURCE_FINGERPRINT GET "${version_json}" fingerprint)
set(SINESTACK_VERSION_PLIST "<plist><dict><key>SineStackBuildUTC</key><string>${SINESTACK_BUILD_UTC}</string><key>SineStackSourceFingerprint</key><string>${SINESTACK_SOURCE_FINGERPRINT}</string></dict></plist>")
message(STATUS "Sine Stack ${SINESTACK_VERSION} / ${SINESTACK_BUILD_UTC}")
