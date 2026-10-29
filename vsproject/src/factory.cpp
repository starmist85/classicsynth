#include "controller/Controller.h"
#include "pluginids.h"
#include "processor/Processor.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

BEGIN_FACTORY_DEF(CS_VENDOR, CS_URL, CS_EMAIL)

    DEF_CLASS2(INLINE_UID_FROM_FUID(cs::kProcessorUID),
               PClassInfo::kManyInstances,
               kVstAudioEffectClass,
               CS_PRODUCT,
               Vst::kDistributable,
               PlugType::kInstrumentSynth,
               CS_VERSION_STR,
               kVstVersionString,
               cs::Processor::createInstance)

    DEF_CLASS2(INLINE_UID_FROM_FUID(cs::kControllerUID),
               PClassInfo::kManyInstances,
               kVstComponentControllerClass,
               CS_PRODUCT " Controller",
               0,
               "",
               CS_VERSION_STR,
               kVstVersionString,
               cs::Controller::createInstance)

END_FACTORY
