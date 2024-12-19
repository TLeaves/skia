#ifndef PathKitBase_DEFINED
#define PathKitBase_DEFINED

#include "include/core/SkScalar.h"

#if !defined(PATHKIT_API)
    #if defined(PATHKIT_DLL)
        #if defined(_MSC_VER)
            #if SKIA_IMPLEMENTATION
                #define PATHKIT_API __declspec(dllexport)
            #else
                #define PATHKIT_API __declspec(dllimport)
            #endif
        #else
            #define PATHKIT_API __attribute__((visibility("default")))
        #endif
    #else
        #define PATHKIT_DLL
    #endif
#endif

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