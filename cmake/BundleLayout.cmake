# SPDX-License-Identifier: MIT
#
# Assemble the OFX bundle layout that DaVinci Resolve expects:
#
#   <Name>.ofx.bundle/
#     Contents/
#       Win64/
#         <Name>.ofx           <- the renamed DLL
#
# This mirrors what the OpenFX SDK's own cmake/OpenFX.cmake add_ofx_plugin() does, without
# dragging in the SDK's Conan-coupled build.
#
# Install with:  cmake --install build --config Release --prefix build/bundle
# Then copy the resulting <Name>.ofx.bundle folder into
#   C:\Program Files\Common Files\OFX\Plugins\
# as administrator, with Resolve completely closed.

if(WIN32)
  set(AIBGR_ARCH_DIR "Win64")
elseif(APPLE)
  set(AIBGR_ARCH_DIR "MacOS")
else()
  set(AIBGR_ARCH_DIR "Linux-x86-64")
endif()

set(AIBGR_BUNDLE_NAME "${OFX_PLUGIN_NAME}.ofx.bundle")

if(OFX_INSTALL_BUNDLE_ROOT)
  set(AIBGR_BUNDLE_STAGING "${OFX_INSTALL_BUNDLE_ROOT}/${AIBGR_BUNDLE_NAME}")
else()
  set(AIBGR_BUNDLE_STAGING "${AIBGR_BUNDLE_NAME}")
endif()

install(TARGETS ${OFX_PLUGIN_NAME}
        LIBRARY DESTINATION "${AIBGR_BUNDLE_STAGING}/Contents/${AIBGR_ARCH_DIR}"
        RUNTIME DESTINATION "${AIBGR_BUNDLE_STAGING}/Contents/${AIBGR_ARCH_DIR}")

# Ship the licence notices alongside the binary. OFX is BSD-3-Clause and ONNX Runtime is MIT;
# whichever model weights the user installs carry their own terms (see docs/01 section 6.1).
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/packaging/THIRD_PARTY_LICENSES.txt"
        DESTINATION "${AIBGR_BUNDLE_STAGING}/Contents/${AIBGR_ARCH_DIR}"
        OPTIONAL)

# A post-build reminder, because the two rules that bite hardest are easy to forget:
# exports must be undecorated, and Resolve must be closed before copying.
if(WIN32)
  add_custom_command(TARGET ${OFX_PLUGIN_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo
      "Built $<TARGET_FILE:${OFX_PLUGIN_NAME}>"
    COMMAND ${CMAKE_COMMAND} -E echo
      "  Verify exports:  dumpbin /exports $<TARGET_FILE_NAME:${OFX_PLUGIN_NAME}>"
    COMMAND ${CMAKE_COMMAND} -E echo
      "  Expect plain:    OfxGetNumberOfPlugins  OfxGetPlugin  OfxSetHost"
    COMMAND ${CMAKE_COMMAND} -E echo
      "  Close Resolve before copying into the OFX Plugins folder."
    VERBATIM)
endif()
