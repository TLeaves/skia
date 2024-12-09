#ifndef PathKitBase_DEFINED
#define PathKitBase_DEFINED

#include "include/core/SkScalar.h"

namespace utils {

struct StrokeOpts {
    // Default values are set in chaining.js which allows clients
    // to set any number of them. Otherwise, the binding code complains if
    // any are omitted.
    SkScalar width;
    SkScalar miter_limit;
    SkScalar res_scale;
    uint8_t join;
    uint8_t cap;
};

}

#endif // PathKitBase_DEFINED