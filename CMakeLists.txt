cmake_minimum_required(VERSION 3.16)
project(minicut VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find System Dependencies
find_package(PkgConfig REQUIRED)
find_package(OpenGL REQUIRED)

pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(GLEW REQUIRED glew)

# Find FFmpeg components (Checking system standard paths and third_party fallback)
set(FFMPEG_SEARCH_INCLUDE
    /usr/include/x86_64-linux-gnu
    /usr/include
    /usr/local/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/usr/include/x86_64-linux-gnu
)

set(FFMPEG_SEARCH_LIB
    /usr/lib/x86_64-linux-gnu
    /usr/lib
    /usr/local/lib
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/usr/lib/x86_64-linux-gnu
)

find_path(AVCODEC_INCLUDE_DIR libavcodec/avcodec.h PATHS ${FFMPEG_SEARCH_INCLUDE})
find_path(AVFORMAT_INCLUDE_DIR libavformat/avformat.h PATHS ${FFMPEG_SEARCH_INCLUDE})
find_path(SWSCALE_INCLUDE_DIR libswscale/swscale.h PATHS ${FFMPEG_SEARCH_INCLUDE})
find_path(SWRESAMPLE_INCLUDE_DIR libswresample/swresample.h PATHS ${FFMPEG_SEARCH_INCLUDE})
find_path(AVUTIL_INCLUDE_DIR libavutil/avutil.h PATHS ${FFMPEG_SEARCH_INCLUDE})

find_library(AVCODEC_LIB NAMES avcodec PATHS ${FFMPEG_SEARCH_LIB})
find_library(AVFORMAT_LIB NAMES avformat PATHS ${FFMPEG_SEARCH_LIB})
find_library(SWSCALE_LIB NAMES swscale PATHS ${FFMPEG_SEARCH_LIB})
find_library(SWRESAMPLE_LIB NAMES swresample PATHS ${FFMPEG_SEARCH_LIB})
find_library(AVUTIL_LIB NAMES avutil PATHS ${FFMPEG_SEARCH_LIB})

if(NOT (AVCODEC_INCLUDE_DIR AND AVCODEC_LIB AND AVFORMAT_LIB AND SWSCALE_LIB AND SWRESAMPLE_LIB AND AVUTIL_LIB))
    message(FATAL_ERROR "FFmpeg libraries or headers not found! Please ensure libavformat, libavcodec, libswscale, libswresample are installed.")
endif()

message(STATUS "Found FFmpeg include: ${AVCODEC_INCLUDE_DIR}")
message(STATUS "Found libavcodec: ${AVCODEC_LIB}")
message(STATUS "Found libavformat: ${AVFORMAT_LIB}")
message(STATUS "Found libswscale: ${SWSCALE_LIB}")
message(STATUS "Found libswresample: ${SWRESAMPLE_LIB}")
message(STATUS "Found libavutil: ${AVUTIL_LIB}")

# nlohmann/json setup
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/json/include/nlohmann/json.hpp")
    set(NLOHMANN_JSON_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/json/include")
else()
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# Dear ImGui setup
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui.h")
    set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
else()
    include(FetchContent)
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.91.8
    )
    FetchContent_MakeAvailable(imgui)
    set(IMGUI_DIR "${imgui_SOURCE_DIR}")
endif()

# LuaJIT setup
set(LUAJIT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/luajit/include")
set(LUAJIT_LIB "${CMAKE_CURRENT_SOURCE_DIR}/third_party/luajit/lib/libluajit-5.1.a")

# Dear ImGui sources
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

# minicut sources
set(MINICUT_SOURCES
    src/main.cpp
    src/App.cpp
    src/Timeline.cpp
    src/ClipManager.cpp
    src/KeyframeEditor.cpp
    src/ExportManager.cpp
    src/ProjectSerializer.cpp
    src/MediaDecoder.cpp
    src/AudioEngine.cpp
    src/FileBrowser.cpp
    src/NativeFileDialog.cpp
    src/TempManager.cpp
    src/LocaleManager.cpp
    src/ScriptEngine.cpp
    ${IMGUI_SOURCES}
)

add_executable(minicut ${MINICUT_SOURCES})

target_include_directories(minicut PRIVATE
    src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${SDL2_INCLUDE_DIRS}
    ${GLEW_INCLUDE_DIRS}
    ${OPENGL_INCLUDE_DIR}
    ${AVCODEC_INCLUDE_DIR}
    ${LUAJIT_INCLUDE_DIR}
)

if(NLOHMANN_JSON_INCLUDE_DIR)
    target_include_directories(minicut PRIVATE ${NLOHMANN_JSON_INCLUDE_DIR})
else()
    target_link_libraries(minicut PRIVATE nlohmann_json::nlohmann_json)
endif()

target_compile_definitions(minicut PRIVATE
    ${SDL2_CFLAGS_OTHER}
)

target_link_libraries(minicut PRIVATE
    ${SDL2_LIBRARIES}
    ${GLEW_LIBRARIES}
    OpenGL::GL
    ${AVFORMAT_LIB}
    ${AVCODEC_LIB}
    ${SWSCALE_LIB}
    ${SWRESAMPLE_LIB}
    ${AVUTIL_LIB}
    ${LUAJIT_LIB}
    dl
    pthread
    m
)

set_target_properties(minicut PROPERTIES
    BUILD_RPATH "/usr/lib/x86_64-linux-gnu"
    INSTALL_RPATH "/usr/lib/x86_64-linux-gnu"
    INSTALL_RPATH_USE_LINK_PATH TRUE
)

# Test executable
add_executable(test_minicut
    tests/test_minicut.cpp
    src/ProjectSerializer.cpp
    src/MediaDecoder.cpp
    src/ExportManager.cpp
    src/FileBrowser.cpp
    src/NativeFileDialog.cpp
    src/ClipManager.cpp
    src/AudioEngine.cpp
    src/Timeline.cpp
    src/KeyframeEditor.cpp
    src/TempManager.cpp
    src/LocaleManager.cpp
    src/ScriptEngine.cpp
    ${IMGUI_SOURCES}
)

target_include_directories(test_minicut PRIVATE
    src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${SDL2_INCLUDE_DIRS}
    ${GLEW_INCLUDE_DIRS}
    ${OPENGL_INCLUDE_DIR}
    ${AVCODEC_INCLUDE_DIR}
    ${LUAJIT_INCLUDE_DIR}
)

if(NLOHMANN_JSON_INCLUDE_DIR)
    target_include_directories(test_minicut PRIVATE ${NLOHMANN_JSON_INCLUDE_DIR})
else()
    target_link_libraries(test_minicut PRIVATE nlohmann_json::nlohmann_json)
endif()

target_compile_definitions(test_minicut PRIVATE
    ${SDL2_CFLAGS_OTHER}
)

target_link_libraries(test_minicut PRIVATE
    ${SDL2_LIBRARIES}
    ${GLEW_LIBRARIES}
    OpenGL::GL
    ${AVFORMAT_LIB}
    ${AVCODEC_LIB}
    ${SWSCALE_LIB}
    ${SWRESAMPLE_LIB}
    ${AVUTIL_LIB}
    ${LUAJIT_LIB}
    dl
    pthread
    m
)

set_target_properties(test_minicut PROPERTIES
    BUILD_RPATH "/usr/lib/x86_64-linux-gnu"
    INSTALL_RPATH "/usr/lib/x86_64-linux-gnu"
    INSTALL_RPATH_USE_LINK_PATH TRUE
)
