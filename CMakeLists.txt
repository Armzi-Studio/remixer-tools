cmake_minimum_required(VERSION 3.20)
project(RemixerDesktopApp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Core Engine & SDK Dependencies
# ---------------------------------------------------------------------------
if(NOT TARGET remixer_core)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../remixer-tools-core-engine/CMakeLists.txt")
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../remixer-tools-core-engine" remixer_core_build EXCLUDE_FROM_ALL)
    else()
        message(FATAL_ERROR "remixer-tools-core-engine not found.")
    endif()
endif()

if(NOT TARGET remixer_downloader_core)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../remixer-tools-downloader/CMakeLists.txt")
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../remixer-tools-downloader" remixer_downloader_build EXCLUDE_FROM_ALL)
    else()
        message(FATAL_ERROR "remixer-tools-downloader not found.")
    endif()
endif()

# ---------------------------------------------------------------------------
# Optional JUCE Framework Integration
# ---------------------------------------------------------------------------
option(REMIXER_USE_JUCE "Integrate JUCE Framework for Desktop GUI" OFF)

if(REMIXER_USE_JUCE)
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        7.0.12
    )
    FetchContent_MakeAvailable(JUCE)

    juce_add_gui_app(remixer_desktop_app
        PRODUCT_NAME "Remixer Tools"
        COMPANY_NAME "Armzi Studio"
        BUNDLE_ID "com.armzistudio.remixertools"
    )

    target_sources(remixer_desktop_app PRIVATE
        src/main.cpp
        src/main_component.cpp
    )

    target_compile_definitions(remixer_desktop_app PRIVATE REMIXER_USE_JUCE=1)

    target_link_libraries(remixer_desktop_app PRIVATE
        remixer_core
        juce::juce_gui_basics
        juce::juce_audio_utils
        juce::juce_audio_devices
    )
else()
    # Native C++ Desktop Application build
    add_executable(remixer_desktop_app WIN32
        src/main.cpp
        src/main_component.cpp
        src/win32_window.cpp
    )

    target_include_directories(remixer_desktop_app PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    )

    target_link_libraries(remixer_desktop_app PRIVATE
        remixer_core
        remixer_downloader_core
    )

    if(WIN32)
        target_link_libraries(remixer_desktop_app PRIVATE
            dwmapi
            comdlg32
            gdi32
            user32
        )
    endif()

    # -----------------------------------------------------------------------
    # Build-Time Obfuscation & Binary Hardening Flags
    # -----------------------------------------------------------------------
    if(MSVC)
        target_compile_options(remixer_desktop_app PRIVATE /guard:cf /utf-8)
        target_link_options(remixer_desktop_app PRIVATE 
            /GUARD:CF
            /DYNAMICBASE
            /HIGHENTROPYVA
            /NXCOMPAT
            /OPT:REF
            /OPT:ICF
            /INCREMENTAL:NO
        )
    endif()
endif()
