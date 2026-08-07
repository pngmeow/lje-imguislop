cmake_minimum_required(VERSION 3.31)
project(lje-imgui CXX)

# Parse version from CHANGELOG.md
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/CHANGELOG.md" CHANGELOG_CONTENTS)
string(REGEX MATCH "## \\[([0-9]+)\\.([0-9]+)\\.([0-9]+)\\]" VERSION_MATCH "${CHANGELOG_CONTENTS}")
set(PROJECT_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(PROJECT_VERSION_MINOR "${CMAKE_MATCH_2}")
set(PROJECT_VERSION_PATCH "${CMAKE_MATCH_3}")
set(PROJECT_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")

configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in"
        "${CMAKE_CURRENT_BINARY_DIR}/src/version.h"
)

set(CMAKE_CXX_STANDARD 20)

# Everything the module needs at runtime lives in a single folder next to the
# DLL, mirroring how it is installed into %USERPROFILE%\.lje_modules.
set(LJE_IMGUI_PAYLOAD_DIR "${CMAKE_BINARY_DIR}/lje-imgui")

# imgui
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/imgui)
add_library(imgui STATIC
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_demo.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/backends/imgui_impl_dx9.cpp
        ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PUBLIC d3d9)

# imnodes
set(IMNODES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/imnodes)
add_library(imnodes STATIC ${IMNODES_DIR}/imnodes.cpp)
target_include_directories(imnodes PUBLIC ${IMNODES_DIR})
target_link_libraries(imnodes PUBLIC imgui)

# MinHook
add_subdirectory(libs/MinHook)

# CEF + Monaco Editor (downloaded on first configure)
include(cmake/cef.cmake)
include(cmake/monaco.cmake)

# Code shared between the browser process (this DLL, injected into the host)
# and the CEF sub-processes.
add_library(lje-cef STATIC
        src/cef/app.cpp
        src/cef/app.hpp
        src/cef/scheme.cpp
        src/cef/scheme.hpp
        src/cef/task.hpp
        # The framework ships under a private module name, so every process that
        # touches CEF has to load it explicitly - the sub-process included.
        src/monaco/loader.cpp
        src/monaco/loader.hpp
        src/monaco/paths.cpp
        src/monaco/paths.hpp
)
target_link_libraries(lje-cef PUBLIC cef shell32 ole32)
set_target_properties(lje-cef PROPERTIES FOLDER "libs")

# Sub-process executable. It is deployed next to libcef.dll so that the normal
# loader search order finds the framework without any extra plumbing.
add_executable(lje-imgui-cef WIN32 src/cef/subprocess/main.cpp)
target_link_libraries(lje-imgui-cef PRIVATE lje-cef)
set_target_properties(lje-imgui-cef PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${LJE_IMGUI_PAYLOAD_DIR}/cef"
        # MSVC's default 1 MiB is not enough for Chromium's renderer stacks.
        LINK_FLAGS "/STACK:8388608"
)

# Monaco integration: off-screen browsers rendered as ImGui widgets.
file(GLOB MONACO_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/monaco/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/monaco/*.hpp")
# Built into lje-cef above so the sub-process gets them too.
list(FILTER MONACO_SOURCES EXCLUDE REGEX "/(loader|paths)\\.(cpp|hpp)$")
add_library(lje-monaco STATIC ${MONACO_SOURCES})
target_include_directories(lje-monaco PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(lje-monaco PUBLIC lje-cef imgui PRIVATE d3d9)
set_target_properties(lje-monaco PROPERTIES FOLDER "libs")

# Main library
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")
# src/cef and src/monaco are built into their own targets above.
list(FILTER SOURCES EXCLUDE REGEX "/src/(cef|monaco)/")
list(FILTER HEADERS EXCLUDE REGEX "/src/(cef|monaco)/")

add_library(lje-imgui SHARED ${SOURCES} ${HEADERS})
target_include_directories(lje-imgui PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/lje/sdk/include"
        "${CMAKE_CURRENT_BINARY_DIR}/src"
)
target_link_libraries(lje-imgui PRIVATE imgui imnodes minhook lje-monaco d3d9 dxguid)
add_dependencies(lje-imgui lje-imgui-cef)

cef_install_runtime(lje-imgui "${LJE_IMGUI_PAYLOAD_DIR}/cef")
monaco_install_assets(lje-imgui "${LJE_IMGUI_PAYLOAD_DIR}/monaco")

# Headless exercise of the CEF/Monaco pipeline. The module only ever runs
# inside a host process, so this is the one way to test it without a game.
option(LJE_IMGUI_BUILD_TESTS "Build the Monaco smoke test executable" OFF)
if (LJE_IMGUI_BUILD_TESTS)
    add_executable(monaco-smoketest
            tests/monaco_smoketest.cpp
            # Lets the test drive real ImGui frames without a window or device.
            ${IMGUI_DIR}/backends/imgui_impl_null.cpp)
    target_link_libraries(monaco-smoketest PRIVATE lje-monaco)
    add_dependencies(monaco-smoketest lje-imgui)
endif ()
