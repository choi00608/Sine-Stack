# Build the complete macOS icon family, including small and Retina variants.
# JUCE's ICON_BIG-only conversion otherwise emits just 512px and 1024px images.
if(NOT EXISTS "${ICON_SOURCE}" OR NOT DEFINED ICON_OUTPUT)
    message(FATAL_ERROR "ICON_SOURCE and ICON_OUTPUT are required")
endif()
get_filename_component(output_dir "${ICON_OUTPUT}" DIRECTORY)
set(iconset "${output_dir}/SineStack.iconset")
file(MAKE_DIRECTORY "${iconset}")
foreach(size 16 32 128 256 512)
    foreach(scale 1 2)
        math(EXPR pixels "${size} * ${scale}")
        if(scale EQUAL 2)
            set(suffix "@2x")
        else()
            set(suffix "")
        endif()
        execute_process(
            COMMAND /usr/bin/sips -z ${pixels} ${pixels} "${ICON_SOURCE}"
                --out "${iconset}/icon_${size}x${size}${suffix}.png"
            RESULT_VARIABLE result OUTPUT_QUIET ERROR_VARIABLE error)
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "Icon resize failed: ${error}")
        endif()
    endforeach()
endforeach()
execute_process(COMMAND /usr/bin/iconutil --convert icns "${iconset}" --output "${ICON_OUTPUT}"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Icon family generation failed: ${error}")
endif()
