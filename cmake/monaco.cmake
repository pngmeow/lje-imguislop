# Monaco Editor
#
# Fetches the official monaco-editor npm package on first configure. We ship the
# prebuilt AMD bundle (min/vs) verbatim - nothing is ported or rewritten, the
# real editor runs inside CEF.

set(MONACO_VERSION "0.56.0")
set(MONACO_ARCHIVE_SHA1 "e85ac00b7ab7092ca1d077894504919af5073cf0")
set(MONACO_ARCHIVE_URL "https://registry.npmjs.org/monaco-editor/-/monaco-editor-${MONACO_VERSION}.tgz")

set(MONACO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/libs/monaco" CACHE PATH "monaco-editor package root")
set(MONACO_STAMP "${MONACO_ROOT}/.monaco_version")

set(_monaco_have "")
if (EXISTS "${MONACO_STAMP}")
    file(READ "${MONACO_STAMP}" _monaco_have)
    string(STRIP "${_monaco_have}" _monaco_have)
endif ()

if (NOT _monaco_have STREQUAL MONACO_VERSION)
    set(_monaco_archive "${CMAKE_CURRENT_BINARY_DIR}/monaco-editor-${MONACO_VERSION}.tgz")
    if (NOT EXISTS "${_monaco_archive}")
        message(STATUS "Downloading monaco-editor ${MONACO_VERSION}...")
        file(DOWNLOAD "${MONACO_ARCHIVE_URL}" "${_monaco_archive}"
                EXPECTED_HASH SHA1=${MONACO_ARCHIVE_SHA1}
                SHOW_PROGRESS
                STATUS _monaco_dl_status)
        list(GET _monaco_dl_status 0 _monaco_dl_code)
        if (NOT _monaco_dl_code EQUAL 0)
            file(REMOVE "${_monaco_archive}")
            list(GET _monaco_dl_status 1 _monaco_dl_msg)
            message(FATAL_ERROR "Failed to download monaco-editor: ${_monaco_dl_msg}")
        endif ()
    endif ()

    message(STATUS "Unpacking monaco-editor into ${MONACO_ROOT}")
    set(_monaco_tmp "${CMAKE_CURRENT_BINARY_DIR}/monaco_unpack")
    file(REMOVE_RECURSE "${_monaco_tmp}" "${MONACO_ROOT}")
    file(MAKE_DIRECTORY "${_monaco_tmp}")
    file(ARCHIVE_EXTRACT INPUT "${_monaco_archive}" DESTINATION "${_monaco_tmp}")

    # npm tarballs wrap everything in a "package" directory.
    if (NOT EXISTS "${_monaco_tmp}/package/min/vs/loader.js")
        message(FATAL_ERROR "Unexpected monaco-editor archive layout")
    endif ()
    file(RENAME "${_monaco_tmp}/package" "${MONACO_ROOT}")
    file(REMOVE_RECURSE "${_monaco_tmp}")
    file(WRITE "${MONACO_STAMP}" "${MONACO_VERSION}")
endif ()

set(MONACO_VS_DIR "${MONACO_ROOT}/min/vs")

if (NOT EXISTS "${MONACO_VS_DIR}/loader.js")
    message(FATAL_ERROR "monaco-editor at ${MONACO_ROOT} looks incomplete (no min/vs/loader.js)")
endif ()

# Copies the editor bundle plus our host page into <dir>, which is what the
# monaco:// scheme handler serves from at runtime.
function(monaco_install_assets target dir)
    add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${dir}"
            COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "${MONACO_VS_DIR}" "${dir}/vs"
            COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/monaco" "${dir}"
            COMMENT "Copying Monaco Editor assets to ${dir}"
            VERBATIM
    )
endfunction()