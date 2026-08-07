# Chromium Embedded Framework
#
# Fetches the official CEF binary distribution on first configure and builds the
# libcef_dll_wrapper static library from its sources. Nothing has to be
# installed by hand: everything lands in libs/cef (gitignored).

set(CEF_VERSION "151.3.14+g5d67476+chromium-151.0.7922.72")
set(CEF_PLATFORM "windows64")
set(CEF_ARCHIVE_SHA1 "96abc7e46d7dfe31756be682e1c0d423807b498e")
set(CEF_ARCHIVE_URL
        "https://cef-builds.spotifycdn.com/cef_binary_151.3.14%2Bg5d67476%2Bchromium-151.0.7922.72_windows64_minimal.tar.bz2")

set(CEF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/libs/cef" CACHE PATH "CEF binary distribution root")
set(CEF_STAMP "${CEF_ROOT}/.cef_version")

# Download + unpack if the vendored copy is missing or is the wrong version.
set(_cef_have "")
if (EXISTS "${CEF_STAMP}")
    file(READ "${CEF_STAMP}" _cef_have)
    string(STRIP "${_cef_have}" _cef_have)
endif ()

if (NOT _cef_have STREQUAL CEF_VERSION)
    set(_cef_archive "${CMAKE_CURRENT_BINARY_DIR}/cef_${CEF_PLATFORM}.tar.bz2")
    if (NOT EXISTS "${_cef_archive}")
        message(STATUS "Downloading CEF ${CEF_VERSION} (${CEF_PLATFORM}), this takes a while...")
        file(DOWNLOAD "${CEF_ARCHIVE_URL}" "${_cef_archive}"
                EXPECTED_HASH SHA1=${CEF_ARCHIVE_SHA1}
                SHOW_PROGRESS
                STATUS _cef_dl_status)
        list(GET _cef_dl_status 0 _cef_dl_code)
        if (NOT _cef_dl_code EQUAL 0)
            file(REMOVE "${_cef_archive}")
            list(GET _cef_dl_status 1 _cef_dl_msg)
            message(FATAL_ERROR "Failed to download CEF: ${_cef_dl_msg}")
        endif ()
    endif ()

    message(STATUS "Unpacking CEF into ${CEF_ROOT}")
    set(_cef_tmp "${CMAKE_CURRENT_BINARY_DIR}/cef_unpack")
    file(REMOVE_RECURSE "${_cef_tmp}" "${CEF_ROOT}")
    file(MAKE_DIRECTORY "${_cef_tmp}")
    file(ARCHIVE_EXTRACT INPUT "${_cef_archive}" DESTINATION "${_cef_tmp}")

    # The archive has a single top-level cef_binary_* directory; hoist it.
    file(GLOB _cef_unpacked "${_cef_tmp}/*")
    list(LENGTH _cef_unpacked _cef_unpacked_count)
    if (NOT _cef_unpacked_count EQUAL 1)
        message(FATAL_ERROR "Unexpected CEF archive layout (${_cef_unpacked_count} top-level entries)")
    endif ()
    file(RENAME "${_cef_unpacked}" "${CEF_ROOT}")
    file(REMOVE_RECURSE "${_cef_tmp}")
    file(WRITE "${CEF_STAMP}" "${CEF_VERSION}")
endif ()

set(CEF_BINARY_DIR "${CEF_ROOT}/Release")
set(CEF_RESOURCE_DIR "${CEF_ROOT}/Resources")

if (NOT EXISTS "${CEF_BINARY_DIR}/libcef.lib")
    message(FATAL_ERROR "CEF distribution at ${CEF_ROOT} looks incomplete (no Release/libcef.lib)")
endif ()

# libcef_dll_wrapper: the C++ <-> C translation layer every CEF client links.
# Built from the distribution sources with the same CRT/exception settings as
# the rest of this project - the libcef.dll boundary itself is a stable C ABI.
file(GLOB_RECURSE CEF_WRAPPER_SOURCES CONFIGURE_DEPENDS "${CEF_ROOT}/libcef_dll/*.cc")
list(FILTER CEF_WRAPPER_SOURCES EXCLUDE REGEX "_(mac|linux|posix)\\.cc$")

add_library(libcef_dll_wrapper STATIC ${CEF_WRAPPER_SOURCES})
target_include_directories(libcef_dll_wrapper PUBLIC "${CEF_ROOT}")
target_compile_definitions(libcef_dll_wrapper
        PUBLIC
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        PRIVATE
        WRAPPING_CEF_SHARED
        WIN32 _WIN32 _WINDOWS
        UNICODE _UNICODE
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
        NTDDI_VERSION=NTDDI_WIN10_FE
)
target_compile_options(libcef_dll_wrapper PRIVATE /wd4100 /wd4996)
set_target_properties(libcef_dll_wrapper PROPERTIES FOLDER "libs")

# The framework is shipped under private module names. A host process that is
# itself built on CEF (Garry's Mod) has libcef.dll and chrome_elf.dll loaded
# already, and Windows resolves imports by base name - so our libcef would bind
# to the host's chrome_elf and fail on the exports it does not have. See
# tools/cef_rename.cpp. The names are kept here so the build and the loader
# cannot disagree; ELF_NAME must stay exactly as long as "chrome_elf.dll".
set(CEF_MODULE_NAME "lje_cef.dll")
set(CEF_ELF_NAME "ljeimg_elf.dll")

string(LENGTH "${CEF_ELF_NAME}" _cef_elf_name_length)
if (NOT _cef_elf_name_length EQUAL 14)
    message(FATAL_ERROR "CEF_ELF_NAME must be 14 characters to patch libcef.dll in place")
endif ()

# Interface target consumers link against. libcef.dll is delay loaded so that we
# can point the loader at our own private copy at runtime (see loader.cpp) - the
# host process knows nothing about CEF and its directory is not on the search
# path. The import table still says "libcef.dll" (that is what libcef.lib
# records); only the file on disk is renamed.
add_library(cef INTERFACE)
target_link_libraries(cef INTERFACE libcef_dll_wrapper "${CEF_BINARY_DIR}/libcef.lib" delayimp)
target_link_options(cef INTERFACE "/DELAYLOAD:libcef.dll")
target_compile_definitions(cef INTERFACE LJE_CEF_MODULE_NAME=L"${CEF_MODULE_NAME}")

# Copies libcef.dll/chrome_elf.dll under the private names, patching libcef's
# import directory to match.
add_executable(cef-rename "${CMAKE_CURRENT_SOURCE_DIR}/tools/cef_rename.cpp")
set_target_properties(cef-rename PROPERTIES FOLDER "tools")

# Runtime payload that has to sit next to lje-imgui.dll. libcef.dll and
# chrome_elf.dll are absent on purpose: cef-rename emits them.
set(CEF_RUNTIME_BINARIES
        "${CEF_BINARY_DIR}/d3dcompiler_47.dll"
        "${CEF_BINARY_DIR}/dxcompiler.dll"
        "${CEF_BINARY_DIR}/dxil.dll"
        "${CEF_BINARY_DIR}/libEGL.dll"
        "${CEF_BINARY_DIR}/libGLESv2.dll"
        "${CEF_BINARY_DIR}/vk_swiftshader.dll"
        "${CEF_BINARY_DIR}/vulkan-1.dll"
        "${CEF_BINARY_DIR}/vk_swiftshader_icd.json"
        "${CEF_BINARY_DIR}/v8_context_snapshot.bin"
)

# Copies the CEF runtime (binaries + resources) into <dir>.
function(cef_install_runtime target dir)
    add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${dir}"
            COMMAND cef-rename
            "${CEF_BINARY_DIR}" "${dir}" "${CEF_MODULE_NAME}" "${CEF_ELF_NAME}"
            # Left behind by builds from before the rename. Harmless but 285 MB.
            COMMAND ${CMAKE_COMMAND} -E rm -f "${dir}/libcef.dll" "${dir}/chrome_elf.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CEF_RUNTIME_BINARIES} "${dir}"
            COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "${CEF_RESOURCE_DIR}" "${dir}"
            COMMENT "Copying CEF runtime to ${dir}"
            VERBATIM
    )
    add_dependencies(${target} cef-rename)
endfunction()