#ifndef PathStrokeUtils_DEFINED
#define PathStrokeUtils_DEFINED

#include "Base.h"

#include "include/private/base/SkMacros.h"

class SkPath;
class SkRect;

namespace utils {

PATHKIT_API bool StrokePathWithOpts(
    const SkPath& src,
    const StrokeOpts& opts,
    SkPath* dst,
    const SkRect* cullRect,
    SkScalar resScale);

}

#endif // PathStrokeUtils_DEFINED
