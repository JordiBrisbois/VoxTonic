cmake_minimum_required(VERSION 3.31)
project(VoxTonic VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)
include(FetchContent)
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.80 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui)
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})

FetchContent_Declare(nexus GIT_REPOSITORY https://github.com/RaidcoreGG/RCGG-lib-nexus-api.git GIT_TAG v6.1 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(nexus)

set(VOXT_SOURCES
    src/addon.cpp src/settings.cpp src/tonic.cpp src/ui.cpp src/mumble_link.cpp)

set(VOXT_LIVE_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../VoxTonic-RE"
    CACHE PATH "Private VoxTonic live-data implementation")
set(VOXT_LIVE_DATA_REF "main"
    CACHE STRING "Reviewed live-data revision")
option(VOXT_ENABLE_LIVE_DATA "Link the private VoxTonic live-data backend" ON)
if(VOXT_ENABLE_LIVE_DATA AND EXISTS "${VOXT_LIVE_DATA_DIR}/CMakeLists.txt")
    set(VOXT_PUBLIC_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src" CACHE PATH "" FORCE)
    add_subdirectory("${VOXT_LIVE_DATA_DIR}" "${CMAKE_BINARY_DIR}/voxtonic-live-data")
elseif(VOXT_ENABLE_LIVE_DATA AND DEFINED ENV{GITHUB_ACTIONS} AND "$ENV{GITHUB_ACTIONS}" STREQUAL "true")
    set(VOXT_PUBLIC_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src" CACHE PATH "" FORCE)
    FetchContent_Declare(voxtonic_live_data_source
        GIT_REPOSITORY "https://github.com/JordiBrisbois/VoxTonic-RE.git"
        GIT_TAG "${VOXT_LIVE_DATA_REF}")
    FetchContent_MakeAvailable(voxtonic_live_data_source)
else()
    message(FATAL_ERROR "VoxTonic-RE not found at ${VOXT_LIVE_DATA_DIR}")
endif()

option(VOXT_BUILD_ADDON "Build the Windows Nexus addon" ON)
if(VOXT_BUILD_ADDON)
    add_library(VoxTonic SHARED ${VOXT_SOURCES})

    # Timestamp-based versioning (same scheme as VoxSake): YYYY.M.D.HHMM in UTC.
    string(TIMESTAMP VOXT_VER_YEAR "%Y" UTC)
    string(TIMESTAMP VOXT_VER_MONTH "%m" UTC)
    string(REGEX REPLACE "^0" "" VOXT_VER_MONTH "${VOXT_VER_MONTH}")
    string(TIMESTAMP VOXT_VER_DAY "%d" UTC)
    string(REGEX REPLACE "^0" "" VOXT_VER_DAY "${VOXT_VER_DAY}")
    string(TIMESTAMP VOXT_VER_HOUR "%H" UTC)
    string(TIMESTAMP VOXT_VER_MINUTE "%M" UTC)
    set(VOXT_VER_BUILD_STRING "${VOXT_VER_HOUR}${VOXT_VER_MINUTE}")
    set(VOXT_VER_BUILD "${VOXT_VER_BUILD_STRING}")
    string(REGEX REPLACE "^0+" "" VOXT_VER_BUILD "${VOXT_VER_BUILD}")
    if(VOXT_VER_BUILD STREQUAL "")
        set(VOXT_VER_BUILD "0")
    endif()
    set(VOXT_VER_STRING "${VOXT_VER_YEAR}.${VOXT_VER_MONTH}.${VOXT_VER_DAY}.${VOXT_VER_BUILD_STRING}")
    file(WRITE "${CMAKE_BINARY_DIR}/version.txt" "${VOXT_VER_STRING}")

    target_compile_definitions(VoxTonic PRIVATE
        VOXT_VERSION_MAJOR=${VOXT_VER_YEAR}
        VOXT_VERSION_MINOR=${VOXT_VER_MONTH}
        VOXT_VERSION_PATCH=${VOXT_VER_DAY}
        VOXT_VERSION_BUILD=${VOXT_VER_BUILD})
    target_sources(VoxTonic PRIVATE resources.rc)
    set_target_properties(VoxTonic PROPERTIES PREFIX "" SUFFIX ".dll")
    target_include_directories(VoxTonic PRIVATE src ${nexus_SOURCE_DIR})
    target_link_libraries(VoxTonic PRIVATE imgui wininet shell32)
    if(TARGET voxtonic_live_data)
        target_link_libraries(VoxTonic PRIVATE voxtonic_live_data)
    endif()
    target_compile_options(VoxTonic PRIVATE -Wall -Wextra -Wpedantic -fno-strict-aliasing)
    target_link_options(VoxTonic PRIVATE -static -static-libgcc -static-libstdc++)

    set(VOXT_INSTALL_DIR "" CACHE PATH "Optional directory receiving VoxTonic.dll after each build")
    if(VOXT_INSTALL_DIR)
        add_custom_command(TARGET VoxTonic POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${VOXT_INSTALL_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:VoxTonic>"
                "${VOXT_INSTALL_DIR}/VoxTonic.dll"
            COMMENT "Copying VoxTonic.dll to ${VOXT_INSTALL_DIR}")
    endif()
endif()

if(BUILD_TESTING)
    add_executable(tonic_logic_tests tests/tonic_logic_tests.cpp)
    target_include_directories(tonic_logic_tests PRIVATE src)
    add_test(NAME tonic_logic COMMAND tonic_logic_tests)
endif()
