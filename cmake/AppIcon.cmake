if(APPLE)
    # Keep JUCE's bundle/resource wiring, but replace its generated icon with a
    # full icon family before the build copies and signs the bundle resources.
    get_target_property(ICON_OUTPUT SineStack_Standalone JUCE_ICON_FILE)
    set(ICON_SOURCE "${SINESTACK_ROOT}/assets/SineStackIcon.png")
    include("${CMAKE_CURRENT_LIST_DIR}/GenerateAppIcon.cmake")
endif()
