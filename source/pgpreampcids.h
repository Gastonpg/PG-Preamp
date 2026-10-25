#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Steinberg {

enum PGPreampParams : Vst::ParamID
{
    kPGPreampBypassId = 100
};

// Unique IDs for PG Preamp processor and controller.
static const FUID kPGPreampProcessorUID (0xA9E7D13C, 0x6B824F01, 0x9C2D51A8, 0x7E430B66);
static const FUID kPGPreampControllerUID (0xD2B64891, 0x3F70A5CE, 0x84E2197B, 0x6C05D3A2);

#define PGPreampVST3Category "Fx"

} // namespace Steinberg
