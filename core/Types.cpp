// SPDX-License-Identifier: MIT
#include "core/Types.h"

namespace aibgr {

const char* bitDepthName(BitDepth d) noexcept {
    switch (d) {
    case BitDepth::None: return "None";
    case BitDepth::Byte: return "Byte";
    case BitDepth::Short: return "Short";
    case BitDepth::Half: return "Half";
    case BitDepth::Float: return "Float";
    }
    return "?";
}

int bitDepthComponentBytes(BitDepth d) noexcept {
    switch (d) {
    case BitDepth::None: return 0;
    case BitDepth::Byte: return 1;
    case BitDepth::Short: return 2;
    case BitDepth::Half: return 2;
    case BitDepth::Float: return 4;
    }
    return 0;
}

int bitDepthIntegerMax(BitDepth d) noexcept {
    switch (d) {
    case BitDepth::Byte: return 255;
    case BitDepth::Short: return 65535;
    default: return 0;
    }
}

const char* componentSetName(ComponentSet c) noexcept {
    switch (c) {
    case ComponentSet::None: return "None";
    case ComponentSet::RGBA: return "RGBA";
    case ComponentSet::RGB: return "RGB";
    case ComponentSet::Alpha: return "Alpha";
    }
    return "?";
}

int componentCount(ComponentSet c) noexcept {
    switch (c) {
    case ComponentSet::None: return 0;
    case ComponentSet::RGBA: return 4;
    case ComponentSet::RGB: return 3;
    case ComponentSet::Alpha: return 1;
    }
    return 0;
}

} // namespace aibgr
