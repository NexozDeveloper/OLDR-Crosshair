cmake_minimum_required(VERSION 3.20)

project(OLDR_Crosshair VERSION 1.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "OLDR Crosshair is a Windows application (Win32 API).")
endif()

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(OLDR_SOURCES
    src/main.cpp
    src/config.cpp
    src/overlay.cpp
    src/renderer.cpp
    src/settings.cpp
    src/settings_window.cpp
)
set(OLDR_HEADERS
    src/config.h
    src/lucide_icons.h
    src/overlay.h
    src/renderer.h
    src/settings.h
    src/settings_window.h
)

add_executable(OLDR_Crosshair WIN32
    ${OLDR_SOURCES}
    ${OLDR_HEADERS}
    resources/app.rc
)

set_target_properties(OLDR_Crosshair PROPERTIES OUTPUT_NAME "OLDR_Crosshair")

configure_file(resources/oldr_version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/oldr_version.h @ONLY)

target_include_directories(OLDR_Crosshair PRIVATE
    src
    resources
    third_party/nanosvg
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_compile_definitions(OLDR_Crosshair PRIVATE
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    WINVER=0x0A00 _WIN32_WINNT=0x0A00
)

set_source_files_properties(resources/app.rc PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.manifest;${CMAKE_CURRENT_SOURCE_DIR}/resources/app.ico"
)

target_link_libraries(OLDR_Crosshair PRIVATE
    user32 gdi32 comctl32 comdlg32 shell32 advapi32 msimg32
)

if(MSVC)
    target_compile_options(OLDR_Crosshair PRIVATE /W4 /utf-8 /permissive- /MP)
    target_link_options(OLDR_Crosshair PRIVATE /MANIFEST:NO)
else()
    target_compile_options(OLDR_Crosshair PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
    target_link_options(OLDR_Crosshair PRIVATE -mwindows -static -static-libgcc -static-libstdc++)
endif()

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT OLDR_Crosshair)
