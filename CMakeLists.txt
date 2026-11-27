
cmake_minimum_required(VERSION 3.21)

if(APPLE)
    project(EasyMix LANGUAGES C CXX OBJC OBJCXX)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "macOS deployment target")
else()
    project(EasyMix LANGUAGES C CXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(APPLE)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9-docking
    GIT_SHALLOW TRUE
)

FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.22
    GIT_SHALLOW TRUE
)

set(SMTG_ADD_VSTGUI OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)
set(SMTG_USE_STATIC_CRT OFF CACHE BOOL "" FORCE)

FetchContent_Declare(vst3sdk
    GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
    GIT_TAG v3.7.12_build_20
    GIT_SHALLOW TRUE
    GIT_SUBMODULES
        base
        cmake
        pluginterfaces
        public.sdk
    GIT_SUBMODULES_RECURSE FALSE
)

FetchContent_MakeAvailable(glfw imgui miniaudio)

FetchContent_GetProperties(vst3sdk)

if(NOT vst3sdk_POPULATED)
    FetchContent_Populate(vst3sdk)

    add_subdirectory(
        ${vst3sdk_SOURCE_DIR}
        ${vst3sdk_BINARY_DIR}
        EXCLUDE_FROM_ALL
    )
endif()

if(NOT TARGET sdk_hosting)
    message(FATAL_ERROR "VST3 SDK target sdk_hosting was not created.")
endif()

if(NOT TARGET sdk)
    message(FATAL_ERROR "VST3 SDK target sdk was not created.")
endif()

add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(imgui_lib PUBLIC glfw)

if(APPLE)
    target_compile_definitions(imgui_lib PUBLIC
        GL_SILENCE_DEPRECATION
    )
endif()

set(EASYMIX_SOURCES
    src/main.cpp
)

if(APPLE)
    set(VST3_HOSTING_DIR
        "${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting"
    )

    set(VST3_PLUGPROVIDER
        "${VST3_HOSTING_DIR}/plugprovider.cpp"
    )

    set(VST3_MAC_MODULE
        "${VST3_HOSTING_DIR}/module_mac.mm"
    )

    if(NOT EXISTS "${VST3_PLUGPROVIDER}")
        message(FATAL_ERROR
            "Missing VST3 source: ${VST3_PLUGPROVIDER}"
        )
    endif()

    if(NOT EXISTS "${VST3_MAC_MODULE}")
        message(FATAL_ERROR
            "Missing VST3 source: ${VST3_MAC_MODULE}"
        )
    endif()

    list(APPEND EASYMIX_SOURCES
        src/PlatformMac.mm
        "${VST3_PLUGPROVIDER}"
        "${VST3_MAC_MODULE}"
    )

    set_source_files_properties(
        src/PlatformMac.mm
        "${VST3_MAC_MODULE}"
        PROPERTIES
        COMPILE_OPTIONS "-fobjc-arc"
    )
endif()

if(APPLE)
    add_executable(EasyMix MACOSX_BUNDLE
        ${EASYMIX_SOURCES}
    )

    set_target_properties(EasyMix PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST
            "${CMAKE_SOURCE_DIR}/cmake/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "EasyMix Studio"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.easymix.studio"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
        MACOSX_BUNDLE_BUNDLE_VERSION "1"
    )
else()
    add_executable(EasyMix
        ${EASYMIX_SOURCES}
    )
endif()

target_include_directories(EasyMix PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${miniaudio_SOURCE_DIR}"
    "${vst3sdk_SOURCE_DIR}"
)

target_compile_definitions(EasyMix PRIVATE
    EASYMIX_CMAKE
)

target_link_libraries(EasyMix PRIVATE
    imgui_lib
    sdk_hosting
    sdk_common
    sdk
    base
    pluginterfaces
)

if(WIN32)
    target_compile_definitions(EasyMix PRIVATE
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )

    target_compile_options(EasyMix PRIVATE
        /utf-8
        /bigobj
    )

    target_link_libraries(EasyMix PRIVATE
        opengl32
        comdlg32
        ole32
    )

elseif(APPLE)
    target_compile_options(EasyMix PRIVATE
        -Wno-deprecated-declarations
    )

    target_link_libraries(EasyMix PRIVATE
        "-framework Cocoa"
        "-framework OpenGL"
        "-framework IOKit"
        "-framework CoreFoundation"
        "-framework CoreAudio"
        "-framework AudioToolbox"
        "-framework AudioUnit"
        "-framework CoreMIDI"
        "-framework UniformTypeIdentifiers"
    )
endif()
