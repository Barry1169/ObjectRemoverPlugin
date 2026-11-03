# SPDX-License-Identifier: MIT
#
# AI Background Remover - OpenFX plugin for DaVinci Resolve (Windows x64 / MSVC).
#
# This project does NOT use Conan, vcpkg or add_subdirectory() on the OpenFX SDK.
# It only needs the SDK's headers (include/). The plugin talks to the host through the
# raw OFX C API (see docs/03-tradeoffs-and-decisions.md, D10) and loads ONNX Runtime
# dynamically at run time (D1), so there is no ONNX Runtime build dependency either.
#
# Quick start (x64 Native Tools Command Prompt for VS 2026):
#   cmake -S . -B build -G "Visual Studio 18 2026" -A x64 ^
#         -DOPENFX_SDK_DIR="D:/DavinciCoding/OpenFX/openfx"
#   cmake --build build --config Release --parallel
#   cmake --install build --config Release --prefix build/bundle

cmake_minimum_required(VERSION 3.20)

project(
  AiBgRemover
  VERSION 1.0.0
  DESCRIPTION "AI-powered background removal OpenFX plugin"
  LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
set(OPENFX_SDK_DIR
    ""
    CACHE PATH "Root of the OpenFX SDK checkout (the folder containing include/)")

set(OFX_CXX_STANDARD
    20
    CACHE STRING "C++ standard to build with (17 or 20). The code is valid under both.")
set_property(CACHE OFX_CXX_STANDARD PROPERTY STRINGS 17 20)

# Plugin identity. OFX unique IDs are PERMANENT: a saved Resolve project references the
# plugin by this string, so changing it later orphans existing projects. Set it once,
# before the first install into Resolve.
set(OFX_PLUGIN_ID_PREFIX
    "com.hirdesh1729"
    CACHE STRING "Vendor prefix for the OFX plugin unique identifier")
set(OFX_PLUGIN_NAME
    "AiBgRemover"
    CACHE STRING "Plugin / bundle / binary base name")
set(OFX_PLUGIN_LABEL
    "AI Background Remover"
    CACHE STRING "Label shown in the Resolve inspector")
set(OFX_PLUGIN_GROUPING
    "AI Matting"
    CACHE STRING "Grouping shown under Effects -> OpenFX in Resolve")

option(BUILD_TESTS "Build the unit-test executable (no GPU, no ONNX Runtime, no Resolve needed)" ON)
option(OFX_SANITIZERS "Build tests with AddressSanitizer + UBSan (GCC/Clang only)" OFF)
option(OFX_TRACE_ACTIONS
       "Default the action-trace log ON in Debug builds (see docs: 'the debug log trick')" ON)

# The log level in effect between kOfxActionLoad and the first CreateInstance, i.e. before any
# inspector control exists to read. Info captures the host capability dump (which answers V7) and
# the per-render summary; Verbose adds every host action and every property that failed to set.
set(OFX_DEFAULT_LOG_LEVEL
    "Info"
    CACHE STRING "Startup log level before the inspector's own Log Level control takes over")
set_property(CACHE OFX_DEFAULT_LOG_LEVEL PROPERTY STRINGS Off Error Info Verbose)
option(OFX_STATIC_CRT
       "MSVC: link the C runtime statically so the .ofx depends only on KERNEL32.dll" ON)

set(OFX_INSTALL_BUNDLE_ROOT
    ""
    CACHE
      PATH
      "Where 'cmake --install' places <Name>.ofx.bundle. Default: <prefix>/. Leave empty for the default."
)

if(NOT OFX_CXX_STANDARD MATCHES "^(17|20)$")
  message(FATAL_ERROR "OFX_CXX_STANDARD must be 17 or 20 (got '${OFX_CXX_STANDARD}')")
endif()

# LogLevel enum order in util/Log.h: Off=0, Error=1, Info=2, Verbose=3.
if(OFX_DEFAULT_LOG_LEVEL STREQUAL "Off")
  set(AIBGR_DEFAULT_LOG_LEVEL_VALUE 0)
elseif(OFX_DEFAULT_LOG_LEVEL STREQUAL "Error")
  set(AIBGR_DEFAULT_LOG_LEVEL_VALUE 1)
elseif(OFX_DEFAULT_LOG_LEVEL STREQUAL "Info")
  set(AIBGR_DEFAULT_LOG_LEVEL_VALUE 2)
elseif(OFX_DEFAULT_LOG_LEVEL STREQUAL "Verbose")
  set(AIBGR_DEFAULT_LOG_LEVEL_VALUE 3)
else()
  message(FATAL_ERROR
    "OFX_DEFAULT_LOG_LEVEL must be one of Off, Error, Info, Verbose (got '${OFX_DEFAULT_LOG_LEVEL}')")
endif()

set(CMAKE_CXX_STANDARD ${OFX_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(OpenFXSDK)

# ---------------------------------------------------------------------------
# Generated plugin identity header
# ---------------------------------------------------------------------------
set(AIBGR_PLUGIN_UNIQUE_ID "${OFX_PLUGIN_ID_PREFIX}.aibgremover")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/PluginIdentity.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/generated/aibgr/PluginIdentity.h" @ONLY)

# ---------------------------------------------------------------------------
# Host-agnostic core: pixel math, matte processing, compositing, pipeline.
# Compiles and unit-tests with no OFX headers, no ONNX Runtime, no GPU, no Resolve.
# ---------------------------------------------------------------------------
add_library(aibgr_core STATIC
  util/Log.cpp
  util/Paths.cpp
  core/Types.cpp
  core/FloatImage.cpp
  core/PixelConvert.cpp
  core/Premultiply.cpp
  core/ColorSpace.cpp
  core/PipelineSettings.cpp
  core/Pipeline.cpp
  matte/Matte.cpp
  matte/Resize.cpp
  matte/PlaceholderMatte.cpp
  composite/Compositor.cpp
)
target_include_directories(aibgr_core
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
         "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_compile_features(aibgr_core PUBLIC cxx_std_${OFX_CXX_STANDARD})
set_target_properties(aibgr_core PROPERTIES CXX_VISIBILITY_PRESET hidden
                                            VISIBILITY_INLINES_HIDDEN ON)

if(MSVC)
  target_compile_options(aibgr_core PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc
                                            /wd4100 /wd4127 /wd4324)
  target_compile_definitions(aibgr_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
                                                _CRT_NONSTDC_NO_WARNINGS)
else()
  target_compile_options(aibgr_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# util/Log.cpp lives in aibgr_core, so the startup level is defined there and inherited by
# everything that links it.
target_compile_definitions(aibgr_core PUBLIC
  AIBGR_DEFAULT_LOG_LEVEL=${AIBGR_DEFAULT_LOG_LEVEL_VALUE})

if(OFX_SANITIZERS AND NOT MSVC)
  target_compile_options(aibgr_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(aibgr_core PUBLIC -fsanitize=address,undefined)
endif()

# ---------------------------------------------------------------------------
# The plugin itself: a MODULE library renamed to .ofx
# ---------------------------------------------------------------------------
add_library(${OFX_PLUGIN_NAME} MODULE
  plugin/OfxEntry.cpp
  plugin/HostBridge.cpp
  plugin/ParamDefs.cpp
  plugin/ParamValues.cpp
  plugin/PluginPaths.cpp
  plugin/OfxImageAdapter.cpp
  plugin/BgRemoverPlugin.cpp
)
target_link_libraries(${OFX_PLUGIN_NAME} PRIVATE aibgr_core)
target_include_directories(${OFX_PLUGIN_NAME} PRIVATE "${OPENFX_INCLUDE_DIR}")
set_target_properties(${OFX_PLUGIN_NAME} PROPERTIES
  PREFIX ""
  SUFFIX ".ofx"
  OUTPUT_NAME "${OFX_PLUGIN_NAME}"
  C_VISIBILITY_PRESET hidden
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
)

if(MSVC)
  target_compile_options(${OFX_PLUGIN_NAME} PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc
                                                    /wd4100 /wd4127)
  target_compile_definitions(${OFX_PLUGIN_NAME} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  # Per the tested route in the supplied guide: static CRT so the .ofx does not depend on
  # VCRUNTIME140*.dll. Requires CMake >= 3.15 policy CMP0091 (we ask for 3.20).
  if(OFX_STATIC_CRT)
    set_property(TARGET ${OFX_PLUGIN_NAME} PROPERTY
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET aibgr_core PROPERTY
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
  # No unresolved-symbol tolerance: a bad export must fail the build, not the plugin load.
  target_link_options(${OFX_PLUGIN_NAME} PRIVATE /DYNAMICBASE /NXCOMPAT /CETCOMPAT)
else()
  target_compile_options(${OFX_PLUGIN_NAME} PRIVATE -Wall -Wextra -Wno-unused-parameter)
  # Hidden visibility is safe here: plugin/OfxEntry.cpp marks the two OFX entry points with its
  # own visibility("default")/__declspec(dllexport) macro rather than relying on the SDK's
  # OfxExport, whose platform guard varies with the age of the SDK checkout. See the comment at
  # the top of that file.
  target_link_options(${OFX_PLUGIN_NAME} PRIVATE -Wl,--no-undefined)
endif()

if(OFX_TRACE_ACTIONS)
  target_compile_definitions(${OFX_PLUGIN_NAME} PRIVATE
    AIBGR_TRACE_ACTIONS_DEFAULT=$<IF:$<CONFIG:Debug>,1,1>)
else()
  target_compile_definitions(${OFX_PLUGIN_NAME} PRIVATE AIBGR_TRACE_ACTIONS_DEFAULT=0)
endif()

# ---------------------------------------------------------------------------
# Install: assemble <Name>.ofx.bundle/Contents/<ArchDir>/<Name>.ofx
# ---------------------------------------------------------------------------
include(BundleLayout)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(BUILD_TESTS)
  enable_testing()
  add_executable(aibgr_tests
    tests/TestMain.cpp
    tests/test_pixel_convert.cpp
    tests/test_premultiply.cpp
    tests/test_colorspace.cpp
    tests/test_float_image.cpp
    tests/test_matte.cpp
    tests/test_resize.cpp
    tests/test_compositor.cpp
    tests/test_placeholder_matte.cpp
    tests/test_pipeline.cpp
    tests/test_pipeline_settings.cpp
  )
  target_link_libraries(aibgr_tests PRIVATE aibgr_core)
  target_include_directories(aibgr_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
  add_test(NAME aibgr_tests COMMAND aibgr_tests)
endif()

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
message(STATUS "------------------------------------------------------------------")
message(STATUS " ${OFX_PLUGIN_NAME} ${PROJECT_VERSION}")
message(STATUS "   OpenFX SDK headers : ${OPENFX_INCLUDE_DIR}")
message(STATUS "   C++ standard       : ${OFX_CXX_STANDARD}")
message(STATUS "   Unique ID          : ${AIBGR_PLUGIN_UNIQUE_ID}")
message(STATUS "   Label / Grouping   : ${OFX_PLUGIN_LABEL} / ${OFX_PLUGIN_GROUPING}")
message(STATUS "   Bundle output      : ${AIBGR_BUNDLE_STAGING}")
message(STATUS "   Startup log level  : ${OFX_DEFAULT_LOG_LEVEL}")
message(STATUS "   Tests              : ${BUILD_TESTS}")
message(STATUS "------------------------------------------------------------------")
