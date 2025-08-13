cmake_minimum_required(VERSION 3.20)
project(TegritySpy LANGUAGES CXX RC)

# ---- Options ---------------------------------------------------------------
# Set default build type to Release if not provided
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Place binaries in a predictable spot (build/bin/{config})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
foreach(cfg IN LISTS CMAKE_CONFIGURATION_TYPES)
  string(TOUPPER "${cfg}" cfgU)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfgU} ${CMAKE_BINARY_DIR}/bin/${cfg})
endforeach()

# ---- Target ----------------------------------------------------------------
# Sources: add your file names here
set(SPY_SOURCES
  SpyMain.cpp
  SpyMain.rc
  resource.h
  # SpyMain.ico is referenced by SpyMain.rc (no need to list explicitly)
)

add_executable(TegritySpy WIN32 ${SPY_SOURCES})

# Treat the .rc as a resource file explicitly (helps some IDEs)
set_source_files_properties(SpyMain.rc PROPERTIES LANGUAGE RC)

# ---- Compile settings -------------------------------------------------------
target_compile_features(TegritySpy PRIVATE cxx_std_17)

# Strict, modern, UTF-8, and Windows 10+ APIs
target_compile_definitions(TegritySpy
  PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00  # Windows 10
    WINVER=0x0A00
)

if(MSVC)
  target_compile_options(TegritySpy PRIVATE
    /W4           # high warnings
    /permissive-  # strict standard conformance
    /EHsc         # C++ exceptions
    /utf-8        # source is UTF-8
    /Zc:__cplusplus
  )
else()
  target_compile_options(TegritySpy PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---- Link settings ----------------------------------------------------------
# System libs used by SpyMain.cpp
#   Psapi.lib  -> process names
#   Dwmapi.lib -> dark mode attributes
#   UxTheme.lib-> themed controls
target_link_libraries(TegritySpy PRIVATE
  Dwmapi
  UxTheme
  Psapi
)

# Subsystem is WIN32 because we used add_executable(... WIN32 ...)
# No console window; perfect for a UI-only tool.

# ---- Install (optional) -----------------------------------------------------
# "cmake --install build" will drop TegritySpy.exe into ./dist
include(GNUInstallDirs)
install(TARGETS TegritySpy RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# ---- Notes ------------------------------------------------------------------
# Build (single-config generators like Ninja):
#   cmake -S . -B build -G "Ninja"
#   cmake --build build --config Release
#
# Build (Visual Studio):
#   cmake -S . -B build -G "Visual Studio 17 2022"
#   cmake --build build --config Release
#
# Run:
#   build/bin/Release/TegritySpy.exe
