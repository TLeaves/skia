#ifndef InkStrokeUtils_DEFINED
#define InkStrokeUtils_DEFINED

#include "Base.h"

#include "include/core/SkScalar.h"
#include "include/core/SkTypes.h"
#include "include/core/SkPoint.h"

class SkPaint;
class SkPath;
class SkPoint;

namespace utils {

struct StylusPoint {
    SkPoint point;
    SkScalar pressure;

    StylusPoint(float _x, float _y, float _p = 1.0)
        : point(SkPoint::Make(_x, _y)), pressure(_p) {}
    StylusPoint(const SkPoint& _point, float _p = 1.0)
        : point(_point), pressure(_p) {}
    StylusPoint()
        : StylusPoint(0.0f, 0.0f) {}

    friend bool operator==(const StylusPoint& a, const StylusPoint& b) {
        return a.point == b.point;
    }

    friend bool operator!=(const StylusPoint& a, const StylusPoint& b) {
        return a.point != b.point;
    }
};

enum InkEndpointType {
    Circle,
    Square,
};

PATHKIT_API bool StrokeInkWithPaint(
    const StylusPoint* stylus_point_ptr,
    int point_count,
    InkEndpointType endpoint_type,
    const SkPaint &paint,
    SkPath *dst);

PATHKIT_API bool StrokeInkWithOpts(
    const StylusPoint* stylus_point_ptr,
    int point_count,
    InkEndpointType endpoint_type,
    const StrokeOpts &opts,
    SkPath *dst);
}

#endif // InkStrokeUtils_DEFINED
