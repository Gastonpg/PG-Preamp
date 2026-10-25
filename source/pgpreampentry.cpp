#include "pgpreampprocessor.h"
#include "pgpreampcontroller.h"
#include "pgpreampcids.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"

#define stringPluginName "PG Preamp"

using namespace Steinberg;
using namespace Steinberg::Vst;

BEGIN_FACTORY_DEF(
    "PG Audio",
    "https://github.com/Gastonpg/PG-Preamp",
    ""
)

DEF_CLASS2(
    INLINE_UID_FROM_FUID(kPGPreampProcessorUID),
    PClassInfo::kManyInstances,
    kVstAudioEffectClass,
    stringPluginName,
    Vst::kDistributable,
    PGPreampVST3Category,
    FULL_VERSION_STR,
    kVstVersionString,
    PGPreampProcessor::createInstance
)

DEF_CLASS2(
    INLINE_UID_FROM_FUID(kPGPreampControllerUID),
    PClassInfo::kManyInstances,
    kVstComponentControllerClass,
    stringPluginName " Controller",
    0,
    "",
    FULL_VERSION_STR,
    kVstVersionString,
    PGPreampController::createInstance
)

END_FACTORY
