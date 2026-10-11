# Version of this build, for display: PROJECT_VERSION on the commit tagged v<PROJECT_VERSION>
# (a release), otherwise "<PROJECT_VERSION>-dev+<short commit>". Written to MnKordVersion.h,
# which only the editor includes, so a new commit recompiles one file rather than the plugin.

set(MNKORD_VERSION_STRING "${PROJECT_VERSION}")

find_package(Git QUIET)
set(_git_dir "${CMAKE_CURRENT_SOURCE_DIR}/.git")

if(GIT_FOUND AND IS_DIRECTORY "${_git_dir}")
    execute_process(COMMAND "${GIT_EXECUTABLE}" describe --tags --exact-match --match "v${PROJECT_VERSION}"
                    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                    RESULT_VARIABLE _not_release OUTPUT_QUIET ERROR_QUIET)

    if(_not_release)
        execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
                        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                        OUTPUT_VARIABLE _commit OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(_commit)
            set(MNKORD_VERSION_STRING "${PROJECT_VERSION}-dev+${_commit}")
        else()
            set(MNKORD_VERSION_STRING "${PROJECT_VERSION}-dev")
        endif()
    endif()

    # Re-run this when HEAD moves (checkout, commit, tag), so the string follows development.
    set(_watch "${_git_dir}/HEAD" "${_git_dir}/packed-refs")
    file(READ "${_git_dir}/HEAD" _head)
    if(_head MATCHES "^ref: ([^\n]+)")
        list(APPEND _watch "${_git_dir}/${CMAKE_MATCH_1}")
    endif()
    foreach(_file IN LISTS _watch)
        if(EXISTS "${_file}")
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_file}")
        endif()
    endforeach()
    if(IS_DIRECTORY "${_git_dir}/refs/tags")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_git_dir}/refs/tags")
    endif()
endif()

# The changelog's "## [Unreleased] - x.y.z" heading must name the same version (see AGENTS.md).
set(_changelog "${CMAKE_CURRENT_SOURCE_DIR}/CHANGELOG.md")
if(EXISTS "${_changelog}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_changelog}")
    file(STRINGS "${_changelog}" _unreleased REGEX "^## \\[Unreleased\\]")
    if(_unreleased AND NOT _unreleased MATCHES "- ${PROJECT_VERSION}$")
        message(WARNING "CHANGELOG.md says '${_unreleased}', but CMakeLists.txt has version ${PROJECT_VERSION}. "
                        "Set both to the same version (see AGENTS.md).")
    endif()
endif()

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/MnKordVersion.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/generated/MnKordVersion.h" @ONLY)
message(STATUS "mnKord version: ${MNKORD_VERSION_STRING}")
