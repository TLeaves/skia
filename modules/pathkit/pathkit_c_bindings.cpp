
#include "include/core/SkCubicMap.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathEffect.h"
#include "include/core/SkPathUtils.h"
#include "include/core/SkRect.h"
#include "include/core/SkString.h"
#include "include/effects/SkDashPathEffect.h"
#include "include/effects/SkTrimPathEffect.h"
#include "include/pathops/SkPathOps.h"
#include "include/private/base/SkAPI.h"
#include "include/private/base/SkFloatBits.h"
#include "include/private/base/SkFloatingPoint.h"
#include "include/utils/SkParsePath.h"
#include "src/core/SkPaintDefaults.h"
#include "src/core/SkPathPriv.h"
#include "include/core/SkPathMeasure.h"
#include "include/private/base/SkTemplates.h"
#include "src/gpu/ganesh/geometry/GrTriangulator.h"
#include "src/gpu/ganesh/geometry/GrAATriangulator.h"
#include "src/gpu/ganesh/GrEagerVertexAllocator.h"
#include "src/gpu/ganesh/geometry/GrPathUtils.h"

#include "src/InkStrokeUtils.h"
#include "src/PathStrokeUtils.h"

#include "pathkit_c_bindings.h"

//========================================================================================
// PathKit
//========================================================================================

extern "C" PATHKIT_API SkPath* pathkit_fromSVGString(const char* svg_string) {
    SkPath* path = new SkPath();
    if (SkParsePath::FromSVGString(svg_string, path)) {
        return path;
    }
    return nullptr;
}

extern "C" PATHKIT_API SkPath* pathkit_fromStrokeInk(
    const stylus_point_t* stylus_point_ptr, int point_count, float line_width, ink_endpoint_type_t endpoint_type) {
    using namespace utils;

    const StylusPoint* sps = reinterpret_cast<const StylusPoint*>(stylus_point_ptr);
    InkEndpointType type = static_cast<InkEndpointType>(endpoint_type);

    StrokeOpts opts;
    opts.width = line_width;
    opts.miter_limit = 10.0;
    opts.res_scale = 1.0;
    opts.join = (type != InkEndpointType::Square) ? SkPaint::Join::kRound_Join : SkPaint::Join::kBevel_Join;
    opts.cap = (type != InkEndpointType::Square) ? SkPaint::Cap::kRound_Cap : SkPaint::Cap::kSquare_Cap;

    SkPath* path = new SkPath();
    if (StrokeInkWithOpts(sps, point_count, type, opts, path)) {
        return path;
    }
    return nullptr;
}

extern "C" PATHKIT_API SkPath* pathkit_makeFromOp(SkPath* pathOne, SkPath* pathTwo, path_op_t op) {
    if (!pathOne || !pathTwo) {
        return nullptr;
    }

    SkPath* out = new SkPath();
    if (Op(*pathOne, *pathTwo, static_cast<SkPathOp>(op), out)) {
        return out;
    }
    return nullptr;
}

//========================================================================================
// SkPath
//========================================================================================

extern "C" PATHKIT_API SkPath* skpath_create() {
    return new SkPath();
}

extern "C" PATHKIT_API void skpath_destroy(SkPath* p) {
    if (!p) {
        return;
    }

    delete p;
}

extern "C" PATHKIT_API SkPath* skpath_copy(SkPath* p) {
    return new SkPath(*p);
}

extern "C" PATHKIT_API void skpath_traverse(SkPath* p, context_t ctx) {
    if (!p) {
        return;
    }

    const SkPath& path = *p;
    SkPath::Iter iter(path, false);
    SkPoint pts[4];
    SkPath::Verb verb;
    while ((verb = iter.next(pts)) != SkPath::kDone_Verb) {
        switch (verb) {
            case SkPath::kMove_Verb:
                ctx.moveTo(ctx.instance, pts[0].x(), pts[0].y());
                break;
            case SkPath::kLine_Verb:
                ctx.lineTo(ctx.instance, pts[1].x(), pts[1].y());
                break;
            case SkPath::kQuad_Verb:
                ctx.quadraticCurveTo(ctx.instance, pts[1].x(), pts[1].y(), pts[2].x(), pts[2].y());
                break;
            case SkPath::kConic_Verb:
                SkPoint quads[5];
                // approximate with 2^1=2 quads.
                SkPath::ConvertConicToQuads(pts[0], pts[1], pts[2], iter.conicWeight(), quads, 1);
                ctx.quadraticCurveTo(ctx.instance, quads[1].x(), quads[1].y(), quads[2].x(), quads[2].y());
                ctx.quadraticCurveTo(ctx.instance, quads[3].x(), quads[3].y(), quads[4].x(), quads[4].y());
                break;
            case SkPath::kCubic_Verb:
                ctx.bezierCurveTo(ctx.instance, pts[1].x(), pts[1].y(), pts[2].x(), pts[2].y(),
                                                pts[3].x(), pts[3].y());
                break;
            case SkPath::kClose_Verb:
                ctx.closePath(ctx.instance);
                break;
            case SkPath::kDone_Verb:
                break;
        }
    }
}

extern "C" PATHKIT_API void skpath_addPath(SkPath* origin, SkPath* newPath,
                                        float scaleX, float skewX,  float transX,
                                        float skewY,  float scaleY, float transY,
                                        float pers0,  float pers1,  float pers2) {
    if (!origin || !newPath) {
        return;
    }

    SkMatrix m = SkMatrix::MakeAll(scaleX, skewX , transX,
                                   skewY , scaleY, transY,
                                   pers0 , pers1 , pers2);
    origin->addPath(*newPath, m);
}

extern "C" PATHKIT_API void skpath_moveTo(SkPath* p, float x, float y) {
    if (!p) {
        return;
    }

    p->moveTo(x, y);
}

extern "C" PATHKIT_API void skpath_lineTo(SkPath* p, float x, float y) {
    if (!p) {
        return;
    }

    p->lineTo(x, y);
}

extern "C" PATHKIT_API void skpath_quadTo(SkPath* p, float x1, float y1, float x2, float y2) {
    if (!p) {
        return;
    }

    p->quadTo(x1, y1, x2, y2);
}

extern "C" PATHKIT_API void skpath_cubicTo(SkPath* p, float x1, float y1, float x2, float y2, float x3, float y3) {
    if (!p) {
        return;
    }

    p->cubicTo(x1, y1, x2, y2, x3, y3);
}

extern "C" PATHKIT_API void skpath_arc(SkPath* p, float x, float y, float radius, float startAngle, float endAngle, bool ccw) {
    if (!p) {
        return;
    }

    SkPath temp;
    SkRect bounds = SkRect::MakeLTRB(x - radius, y - radius, x + radius, y + radius);
    const auto sweep = SkRadiansToDegrees(endAngle - startAngle) - 360 * ccw;
    temp.addArc(bounds, SkRadiansToDegrees(startAngle), sweep);
    p->addPath(temp, SkPath::kExtend_AddPathMode);
}

extern "C" PATHKIT_API void skpath_arcTo(SkPath* p, float x1, float y1, float x2, float y2, float radius) {
    if (!p) {
        return;
    }

    p->arcTo(x1, y1, x2, y2, radius);
}

extern "C" PATHKIT_API void skpath_close(SkPath* p) {
    if (!p) {
        return;
    }

    p->close();
}

extern "C" PATHKIT_API void skpath_reset(SkPath* p) {
    if (!p) {
        return;
    }

    p->reset();
}

extern "C" PATHKIT_API void skpath_rewind(SkPath* p) {
    if (!p) {
        return;
    }

    p->rewind();
}

extern "C" PATHKIT_API bool skpath_contains(SkPath* p, float x, float y) {
    if (!p) {
        return false;
    }

    return p->contains(x, y);
}

extern "C" PATHKIT_API bool skpath_isHadCurve(SkPath* p) {
    if (!p) {
        return false;
    }

    return p->getSegmentMasks() > SkPath::kLine_SegmentMask;
}

extern "C" PATHKIT_API bool skpath_isEmpty(SkPath* p) {
    if (!p) {
        return false;
    }

    return p->isEmpty();
}

extern "C" PATHKIT_API bool skpath_simplify(SkPath* p) {
    if (!p) {
        return false;
    }

    return Simplify(*p, p);
}

extern "C" PATHKIT_API bool skpath_op(SkPath* p, SkPath* pathOther, path_op_t op) {
    if (!p || !pathOther) {
        return false;
    }

    return Op(*p, *pathOther, static_cast<SkPathOp>(op), p);
}

extern "C" PATHKIT_API SkPath* skpath_makeAsWinding(SkPath* p) {
    if (!p) {
        return nullptr;
    }

    SkPath* out = new SkPath();
    if (AsWinding(*p, out)) {
        return out;
    }
    if (out) {
        delete out;
    }
    return nullptr;
}

extern "C" PATHKIT_API bool skpath_stroke(SkPath* p, stroke_opts_t opts) {
    if (!p) {
        return false;
    }

    utils::StrokeOpts stroke_opts;
    stroke_opts.width = opts.width;
    stroke_opts.miter_limit = opts.miter_limit;
    stroke_opts.res_scale = opts.res_scale;
    stroke_opts.join = opts.join;
    stroke_opts.cap = opts.cap;
    return utils::StrokePathWithOpts(*p, stroke_opts, p, nullptr, opts.res_scale);
}

extern "C" PATHKIT_API rect_t skpath_getBounds(SkPath* p) {
    if (!p) {
        return { 0, 0, 0, 0 };
    }
    
    SkRect sk_r = p->getBounds();
    return { sk_r.fLeft, sk_r.fTop, sk_r.fRight - sk_r.fLeft, sk_r.fBottom - sk_r.fTop };
}

extern "C" PATHKIT_API rect_t skpath_computeTightBounds(SkPath* p) {
    if (!p) {
        return { 0, 0, 0, 0 };
    }
    
    SkRect sk_r = p->computeTightBounds();
    return { sk_r.fLeft, sk_r.fTop, sk_r.fRight - sk_r.fLeft, sk_r.fBottom - sk_r.fTop };
}

extern "C" PATHKIT_API void skpath_transform(SkPath* p,
                                        float scaleX, float skewX,  float transX,
                                        float skewY,  float scaleY, float transY,
                                        float pers0,  float pers1,  float pers2) {
    if (!p) {
        return;
    }

    SkMatrix m = SkMatrix::MakeAll(scaleX, skewX , transX,
                                   skewY , scaleY, transY,
                                   pers0 , pers1 , pers2);
    p->transform(m);
}

extern "C" PATHKIT_API bool skpath_toSVGString(SkPath* p, char** o_str, uint32_t* o_strlen) {
    if (!p) {
        return false;
    }

    const SkString& svg = SkParsePath::ToSVGString(*p);
    uint32_t len = svg.size() + 1;
    *o_str = new char[len];
    *o_strlen = len - 1;
    memcpy(*o_str, svg.c_str(), *o_strlen);
    return true;
}
