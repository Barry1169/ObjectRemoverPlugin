// SPDX-License-Identifier: MIT
#pragma once
#include "ofxCore.h"
#include "ofxParam.h"
namespace aibgr::ofx {
OfxStatus brushOverlayMain(const char* action, const void* handle,
                           OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs) noexcept;
// Computes a viewport-anchored toolbar origin from the host's GL matrices.
bool toolbarViewportTransform(const double model[16], const double projection[16],
                              const int viewport[4], double& x, double& y,
                              double& sx, double& sy) noexcept;
void editSavedBrush(OfxParamSetHandle params, double time, bool clear) noexcept;
}
