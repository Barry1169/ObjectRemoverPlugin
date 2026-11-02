# SPDX-License-Identifier: MIT
#
# Locate the OpenFX SDK headers.
#
# We deliberately do NOT add_subdirectory() the SDK and do NOT compile its C++ Support
# library. Reasons (docs/03-tradeoffs-and-decisions.md, D10):
#   * the SDK's top-level CMake pulls in Conan packages and, via HostSupport, a public
#     expat dependency we do not need;
#   * the route that is empirically proven to load in DaVinci Resolve 21 on the target
#     machine uses the raw OFX C API with only the SDK headers.
#
# Search order:
#   1. -DOPENFX_SDK_DIR=...
#   2. $ENV{OPENFX_SDK_DIR}
#   3. ../openfx relative to this project   (the layout used by the supplied guide)
#   4. D:/DavinciCoding/OpenFX/openfx       (the documented target machine location)

if(NOT OPENFX_SDK_DIR AND DEFINED ENV{OPENFX_SDK_DIR})
  set(OPENFX_SDK_DIR "$ENV{OPENFX_SDK_DIR}" CACHE PATH "" FORCE)
endif()

set(_aibgr_sdk_candidates
    "${OPENFX_SDK_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../openfx"
    "${CMAKE_CURRENT_SOURCE_DIR}/../OpenFX"
    "D:/DavinciCoding/OpenFX/openfx"
    "C:/OpenFX/openfx"
    "/usr/local/include/openfx")

set(_aibgr_sdk_found "")
foreach(_cand IN LISTS _aibgr_sdk_candidates)
  if(_cand AND EXISTS "${_cand}/include/ofxImageEffect.h")
    set(_aibgr_sdk_found "${_cand}")
    break()
  endif()
endforeach()

if(NOT _aibgr_sdk_found)
  message(FATAL_ERROR
    "Could not find the OpenFX SDK.\n"
    "Searched for <dir>/include/ofxImageEffect.h in:\n"
    "  ${_aibgr_sdk_candidates}\n"
    "Pass -DOPENFX_SDK_DIR=<path to your openfx checkout>.")
endif()

get_filename_component(OPENFX_SDK_DIR "${_aibgr_sdk_found}" ABSOLUTE)
set(OPENFX_INCLUDE_DIR "${OPENFX_SDK_DIR}/include")
set(OPENFX_SUPPORT_INCLUDE_DIR "${OPENFX_SDK_DIR}/Support/include")

if(NOT EXISTS "${OPENFX_INCLUDE_DIR}/ofxCore.h")
  message(FATAL_ERROR "${OPENFX_INCLUDE_DIR} does not look like an OpenFX SDK include dir")
endif()

# Report the SDK's OFX API level so we can see at configure time what we are building against.
file(STRINGS "${OPENFX_INCLUDE_DIR}/ofxCore.h" _aibgr_api_lines
     REGEX "^#define[ \t]+kOfxImageEffectPluginApiVersion[ \t]+")
if(_aibgr_api_lines)
  string(REGEX REPLACE ".*kOfxImageEffectPluginApiVersion[ \t]+([0-9]+).*" "\\1"
         OPENFX_PLUGIN_API_VERSION "${_aibgr_api_lines}")
  message(STATUS "OpenFX SDK: ${OPENFX_SDK_DIR} (image effect plugin API version "
                 "${OPENFX_PLUGIN_API_VERSION})")
else()
  message(STATUS "OpenFX SDK: ${OPENFX_SDK_DIR}")
endif()

# Detect whether this SDK is new enough for the properties we optionally use.
# Everything guarded by these is optional at run time; these flags only keep the build clean.
set(_aibgr_have_thumbnail_render 0)
file(STRINGS "${OPENFX_INCLUDE_DIR}/ofxImageEffect.h" _aibgr_probe
     REGEX "kOfxImageEffectPropThumbnailRender")
if(_aibgr_probe)
  set(_aibgr_have_thumbnail_render 1)
endif()

set(_aibgr_have_sequential_render 0)
file(STRINGS "${OPENFX_INCLUDE_DIR}/ofxImageEffect.h" _aibgr_probe
     REGEX "kOfxImageEffectInstancePropSequentialRender")
if(_aibgr_probe)
  set(_aibgr_have_sequential_render 1)
endif()

set(_aibgr_have_strchoice 0)
file(STRINGS "${OPENFX_INCLUDE_DIR}/ofxParam.h" _aibgr_probe REGEX "kOfxParamTypeStrChoice")
if(_aibgr_probe)
  set(_aibgr_have_strchoice 1)
endif()

message(STATUS "OpenFX SDK features: ThumbnailRender=${_aibgr_have_thumbnail_render} "
               "SequentialRender=${_aibgr_have_sequential_render} "
               "StrChoice=${_aibgr_have_strchoice}")

# Exported for the plugin target.
set(OPENFX_FEATURE_THUMBNAIL_RENDER ${_aibgr_have_thumbnail_render} CACHE INTERNAL "")
set(OPENFX_FEATURE_SEQUENTIAL_RENDER ${_aibgr_have_sequential_render} CACHE INTERNAL "")
set(OPENFX_FEATURE_STRCHOICE ${_aibgr_have_strchoice} CACHE INTERNAL "")
