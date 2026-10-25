#include "pgpreampcontroller.h"

namespace Steinberg {

tresult PLUGIN_API PGPreampController::initialize(FUnknown* context)
{
    // The first milestone is a transparent audio path. Parameters will be
    // added once processor/controller state synchronization is implemented.
    return EditControllerEx1::initialize(context);
}

} // namespace Steinberg
