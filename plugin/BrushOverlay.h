// SPDX-License-Identifier: MIT
#pragma once
#include "ofxCore.h"
#include "ofxParam.h"
namespace aibgr::ofx {
OfxStatus brushOverlayMain(const char* action, const void* handle,
                           OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs) noexcept;
void editSavedBrush(OfxParamSetHandle params, double time, bool clear) noexcept;
}
