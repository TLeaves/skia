/*
 * Copyright 2018 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkCubicMap.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathEffect.h"
#include "include/core/SkPathUtils.h"
#include "include/core/SkRect.h"
#include "include/core/SkString.h"
#include "include/core/SkStrokeRec.h"
#include "include/effects/SkDashPathEffect.h"
#include "include/effects/SkTrimPathEffect.h"
#include "include/pathops/SkPathOps.h"
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

#include <emscripten.h>
#include <emscripten/bind.h>

using namespace emscripten;

static const int MOVE = 0;
static const int LINE = 1;
static const int QUAD = 2;
static const int CONIC = 3;
static const int CUBIC = 4;
static const int CLOSE = 5;


// Just for self-documenting purposes where the main thing being returned is an
// SkPath, but in an error case, something of type null (which is val) could also be
// returned;
using SkPathOrNull = emscripten::val;
using SkPointOrNull = emscripten::val;
// Self-documenting for when we return a string
using JSString = emscripten::val;
using JSArray = emscripten::val;

using WASMPointerF32 = uintptr_t;

// =================================================================================
// Creating/Exporting Paths with cmd arrays
// =================================================================================

JSArray EMSCRIPTEN_KEEPALIVE ToCmds(const SkPath& path) {
    JSArray cmds = emscripten::val::array();
    for (auto [verb, pts, w] : SkPathPriv::Iterate(path)) {
        JSArray cmd = emscripten::val::array();
        switch (verb) {
        case SkPathVerb::kMove:
            cmd.call<void>("push", MOVE, pts[0].x(), pts[0].y());
            break;
        case SkPathVerb::kLine:
            cmd.call<void>("push", LINE, pts[1].x(), pts[1].y());
            break;
        case SkPathVerb::kQuad:
            cmd.call<void>("push", QUAD, pts[1].x(), pts[1].y(), pts[2].x(), pts[2].y());
            break;
        case SkPathVerb::kConic:
            cmd.call<void>("push", CONIC,
                           pts[1].x(), pts[1].y(),
                           pts[2].x(), pts[2].y(), *w);
            break;
        case SkPathVerb::kCubic:
            cmd.call<void>("push", CUBIC,
                           pts[1].x(), pts[1].y(),
                           pts[2].x(), pts[2].y(),
                           pts[3].x(), pts[3].y());
            break;
        case SkPathVerb::kClose:
            cmd.call<void>("push", CLOSE);
            break;
        }
        cmds.call<void>("push", cmd);
    }
    return cmds;
}

JSArray EMSCRIPTEN_KEEPALIVE ToNonConicCmds(const SkPath& path) {
    JSArray cmds = emscripten::val::array();

    const auto append_command = [&](int verb, const SkPoint pts[], size_t count) {
        JSArray cmd = emscripten::val::array();
        cmd.call<void>("push", verb);

        for (size_t i = 0; i < count; ++i) {
            cmd.call<void>("push", pts[i].x(), pts[i].y());
        }

        cmds.call<void>("push", cmd);
    };

    SkPath::Iter iter(path, false);
    SkPoint pts[4];
    SkPath::Verb verb;
    while ((verb = iter.next(pts)) != SkPath::kDone_Verb) {
        switch (verb) {
            case SkPath::kMove_Verb:
                append_command(MOVE, &pts[0], 1);
                break;
            case SkPath::kLine_Verb:
                append_command(LINE, &pts[1], 1);
                break;
            case SkPath::kQuad_Verb:
                append_command(QUAD, &pts[1], 2);
                break;
            case SkPath::kConic_Verb:
                SkPoint quads[5];
                // approximate with 2^1=2 quads.
                SkPath::ConvertConicToQuads(pts[0], pts[1], pts[2], iter.conicWeight(), quads, 1);
                append_command(QUAD, &quads[1], 2);
                append_command(QUAD, &quads[3], 2);
                break;
            case SkPath::kCubic_Verb:
                append_command(CUBIC, &pts[1], 3);
                break;
            case SkPath::kClose_Verb:
                append_command(CLOSE, nullptr, 0);
                break;
            case SkPath::kDone_Verb:
                break;
        }
    }

    return cmds;
}

// This type signature is a mess, but it's necessary. See, we can't use "bind" (EMSCRIPTEN_BINDINGS)
// and pointers to primitive types (Only bound types like SkPoint). We could if we used
// cwrap (see https://becominghuman.ai/passing-and-returning-webassembly-array-parameters-a0f572c65d97)
// but that requires us to stick to C code and, AFAIK, doesn't allow us to return nice things like
// SkPath or SkOpBuilder.
//
// So, basically, if we are using C++ and EMSCRIPTEN_BINDINGS, we can't have primative pointers
// in our function type signatures. (this gives an error message like "Cannot call foo due to unbound
// types Pi, Pf").  But, we can just pretend they are numbers and cast them to be pointers and
// the compiler is happy.
SkPathOrNull EMSCRIPTEN_KEEPALIVE FromCmds(uintptr_t /* float* */ cptr, int numCmds) {
    const auto* cmds = reinterpret_cast<const float*>(cptr);
    SkPath path;
    float x1, y1, x2, y2, x3, y3;

    // if there are not enough arguments, bail with the path we've constructed so far.
    #define CHECK_NUM_ARGS(n) \
        if ((i + n) > numCmds) { \
            SkDebugf("Not enough args to match the verbs. Saw %d commands\n", numCmds); \
            return emscripten::val::null(); \
        }

    for(int i = 0; i < numCmds;){
         switch (sk_float_floor2int(cmds[i++])) {
            case MOVE:
                CHECK_NUM_ARGS(2)
                x1 = cmds[i++]; y1 = cmds[i++];
                path.moveTo(x1, y1);
                break;
            case LINE:
                CHECK_NUM_ARGS(2)
                x1 = cmds[i++]; y1 = cmds[i++];
                path.lineTo(x1, y1);
                break;
            case QUAD:
                CHECK_NUM_ARGS(4)
                x1 = cmds[i++]; y1 = cmds[i++];
                x2 = cmds[i++]; y2 = cmds[i++];
                path.quadTo(x1, y1, x2, y2);
                break;
            case CONIC:
                CHECK_NUM_ARGS(5)
                x1 = cmds[i++]; y1 = cmds[i++];
                x2 = cmds[i++]; y2 = cmds[i++];
                x3 = cmds[i++]; // weight
                path.conicTo(x1, y1, x2, y2, x3);
                break;
            case CUBIC:
                CHECK_NUM_ARGS(6)
                x1 = cmds[i++]; y1 = cmds[i++];
                x2 = cmds[i++]; y2 = cmds[i++];
                x3 = cmds[i++]; y3 = cmds[i++];
                path.cubicTo(x1, y1, x2, y2, x3, y3);
                break;
            case CLOSE:
                path.close();
                break;
            default:
                SkDebugf("  path: UNKNOWN command %f, aborting dump...\n", cmds[i-1]);
                return emscripten::val::null();
        }
    }

    #undef CHECK_NUM_ARGS

    return emscripten::val(path);
}

SkPath EMSCRIPTEN_KEEPALIVE NewPath() {
    return SkPath();
}

SkPath EMSCRIPTEN_KEEPALIVE CopyPath(const SkPath& a) {
    SkPath copy(a);
    return copy;
}

bool EMSCRIPTEN_KEEPALIVE Equals(const SkPath& a, const SkPath& b) {
    return a == b;
}

SkPathOrNull FromStrokeInk(uintptr_t stylus_point_ptr, int point_count, float line_width, int endpoint_type) {
    using namespace utils;

    const StylusPoint* sps = reinterpret_cast<const StylusPoint*>(stylus_point_ptr);
    InkEndpointType type = static_cast<InkEndpointType>(endpoint_type);

    SkPaint p;
    p.setStyle(SkPaint::kStroke_Style);
    p.setStrokeCap(type != InkEndpointType::Square ? SkPaint::Cap::kRound_Cap : SkPaint::Cap::kSquare_Cap);
    p.setStrokeJoin(type != InkEndpointType::Square ? SkPaint::Join::kRound_Join : SkPaint::Join::kBevel_Join);
    p.setStrokeWidth(line_width);

    SkPath path;
    if (StrokeInkWithPaint(sps, point_count, type, p, &path)) {
        return emscripten::val(path);
    }
    return emscripten::val::null();
}

//========================================================================================
// Path things
//========================================================================================

// All these Apply* methods are simple wrappers to avoid returning an object.
// The default WASM bindings produce code that will leak if a return value
// isn't assigned to a JS variable and has delete() called on it.
// These Apply methods, combined with the smarter binding code allow for chainable
// commands that don't leak if the return value is ignored (i.e. when used intuitively).

void ApplyArcTo(SkPath& p, SkScalar x1, SkScalar y1, SkScalar x2, SkScalar y2,
                SkScalar radius) {
    p.arcTo(x1, y1, x2, y2, radius);
}

void ApplyClose(SkPath& p) {
    p.close();
}

void ApplyConicTo(SkPath& p, SkScalar x1, SkScalar y1, SkScalar x2, SkScalar y2,
                  SkScalar w) {
    p.conicTo(x1, y1, x2, y2, w);
}

void ApplyCubicTo(SkPath& p, SkScalar x1, SkScalar y1, SkScalar x2, SkScalar y2,
                  SkScalar x3, SkScalar y3) {
    p.cubicTo(x1, y1, x2, y2, x3, y3);
}

void ApplyLineTo(SkPath& p, SkScalar x, SkScalar y) {
    p.lineTo(x, y);
}

void ApplyMoveTo(SkPath& p, SkScalar x, SkScalar y) {
    p.moveTo(x, y);
}

void ApplyQuadTo(SkPath& p, SkScalar x1, SkScalar y1, SkScalar x2, SkScalar y2) {
    p.quadTo(x1, y1, x2, y2);
}

void ApplyReset(SkPath& p) {
    p.reset();
}

void ApplyRewind(SkPath& p) {
    p.rewind();
}

SkPointOrNull GetLastPoint(SkPath& p) {
    SkPoint pt;
    if (p.getLastPt(&pt)) {
        return emscripten::val(pt);
    }
    return emscripten::val::null();
}

float getPathLength(const SkPath& p) {
    SkScalar len = 0;
    SkPathMeasure meas(p, false);
    do {
        len += meas.getLength();
    } while (meas.nextContour());
    return len;
}

bool EMSCRIPTEN_KEEPALIVE IsHadCurve(const SkPath& self) {
    return self.getSegmentMasks() > SkPath::kLine_SegmentMask;
}

class SimpleVertexAllocator : public GrEagerVertexAllocator {
public:
    void* lock(size_t stride, int eagerCount) override {
        SkASSERT(stride == sizeof(SkPoint));
        fPoints.realloc(eagerCount);
        return fPoints;
    }
    void unlock(int actualCount) override {}
    SkPoint operator[](int idx) const { return fPoints[idx]; }

    skia_private::AutoTMalloc<SkPoint> fPoints;
};

struct SkPointAA {
    SkPoint point;
    float alpha;
};

class SimpleAAVertexAllocator : public GrEagerVertexAllocator {
public:
    void* lock(size_t stride, int eagerCount) override {
        SkASSERT(stride == sizeof(SkPointAA));
        fPoints.realloc(eagerCount);
        return fPoints;
    }
    void unlock(int actualCount) override {}
    SkPointAA operator[](int idx) const { return fPoints[idx]; }

    skia_private::AutoTMalloc<SkPointAA> fPoints;
};

namespace {
    SimpleVertexAllocator gVertexAlloc;
    SimpleAAVertexAllocator gAAVertexAlloc;
    std::vector<std::vector<SkPoint>> gContours;
}

JSArray EMSCRIPTEN_KEEPALIVE pathToTrianglesBuffer(const SkPath& path, SkScalar scale) {
    JSArray cmds = emscripten::val::array();
    bool isLinear;
    SkRect clipBounds = path.getBounds();
    SkMatrix m = SkMatrix::Scale(scale, scale);
    SkScalar tol = GrPathUtils::scaleToleranceToSrc(
        GrPathUtils::kDefaultTolerance, m, clipBounds);
    int vertexCount = GrTriangulator::PathToTriangles(
        path, tol, clipBounds, &gVertexAlloc, &isLinear);

    // share raw points buffer to JS by Float32Array(PathKit.HEAPF32), and reuse global vertex buffer.
    SkScalar* points = reinterpret_cast<SkScalar*>(gVertexAlloc.fPoints.data());
    cmds.call<void>("push", reinterpret_cast<WASMPointerF32>(points), vertexCount * 2);
    return cmds;
}

JSArray EMSCRIPTEN_KEEPALIVE pathToAATrianglesBuffer(const SkPath& path, SkScalar scale, SkScalar radius) {
    JSArray cmds = emscripten::val::array();
    bool isLinear;
    SkRect clipBounds = path.getBounds();
    SkMatrix m = SkMatrix::Scale(scale, scale);
    SkScalar tol = GrPathUtils::scaleToleranceToSrc(
        GrPathUtils::kDefaultTolerance, m, clipBounds);
    int vertexCount = GrAATriangulator::PathToAATriangles(
        path, tol, clipBounds, &gAAVertexAlloc, nullptr, radius);

    // share raw points buffer to JS by Float32Array(PathKit.HEAPF32), and reuse global vertex buffer.
    SkScalar* points = reinterpret_cast<SkScalar*>(gAAVertexAlloc.fPoints.data());
    cmds.call<void>("push", reinterpret_cast<WASMPointerF32>(points), vertexCount * 3);
    return cmds;
}

JSArray EMSCRIPTEN_KEEPALIVE pathToAABoundaryTrianglesBuffer(const SkPath& path, SkScalar scale, SkScalar radius) {
    JSArray cmds = emscripten::val::array();
    bool isLinear;
    SkRect clipBounds = path.getBounds();
    SkMatrix m = SkMatrix::Scale(scale, scale);
    SkScalar tol = GrPathUtils::scaleToleranceToSrc(
        GrPathUtils::kDefaultTolerance, m, clipBounds);
    int polysCount = 0;
    int vertexCount = GrAATriangulator::PathToAATriangles(
        path, tol, clipBounds, &gAAVertexAlloc, &polysCount, radius);

    constexpr int kPointSize = 3;
    int polysOffset = 0;
    int boundaryLength = vertexCount * kPointSize;
    if (polysCount < vertexCount) {
        // no complex polygons, antialias triangulators is at the tail.
        polysOffset = polysCount * kPointSize;
        boundaryLength = (vertexCount - polysCount) * kPointSize;
    } else {
        // complex mesh, antialias triangulators is mixed on the buffer. collect them.
        int aaCount = 0;
        for (int i = 0; i < vertexCount; i += 3) {
            const SkPointAA& p0 = gAAVertexAlloc.fPoints[i];
            const SkPointAA& p1 = gAAVertexAlloc.fPoints[i + 1];
            const SkPointAA& p2 = gAAVertexAlloc.fPoints[i + 2];
            if (p0.alpha < 1.0 || p1.alpha < 1.0 || p2.alpha < 1.0) {
                if (i > aaCount) {
                    gAAVertexAlloc.fPoints[aaCount] = p0;
                    gAAVertexAlloc.fPoints[aaCount + 1] = p1;
                    gAAVertexAlloc.fPoints[aaCount + 2] = p2;
                }
                aaCount += 3;
            }
        }
        polysOffset = 0;
        boundaryLength = aaCount * kPointSize;
    }

    // share raw points buffer to JS by Float32Array(PathKit.HEAPF32), and reuse global vertex buffer.
    SkScalar* points = reinterpret_cast<SkScalar*>(gAAVertexAlloc.fPoints.data()) + polysOffset;
    cmds.call<void>("push", reinterpret_cast<WASMPointerF32>(points), boundaryLength);
    return cmds;
}

class SimpleTriangulator : public GrTriangulator {
public:
    static int PathToContours(const SkPath& path,
                              float tolerance,
                              const SkRect& clipBounds,
                              const SimpleTriangulator triangulator,
                              std::unique_ptr<VertexList[]>* contours,
                              std::vector<bool>* isCloseList,
                              bool* isLinear) {
        if (!path.isFinite()) {
            return 0;
        }

        int contourCnt = triangulator.getContourInfo(
            path, tolerance, isCloseList);
        if (contourCnt <= 0) {
            *isLinear = true;
            return 0;
        }

        if (SkPathFillType_IsInverse(path.getFillType())) {
            contourCnt++;
        }

        contours->reset(new VertexList[contourCnt]);
        triangulator.pathToContours(tolerance, clipBounds, contours->get(), isLinear);
        return contourCnt;
    }

    static int getContourInfo(const SkPath& path,
                              SkScalar tolerance,
                              std::vector<bool>* isCloseList) {
        // We could theoretically be more aggressive about not counting empty contours, but we need to
        // actually match the exact number of contour linked lists the tessellator will create later on.
        int contourCnt = 1;
        bool hasPoints = false;

        SkPath::Iter iter(path, false);
        SkPath::Verb verb;
        SkPoint pts[4];
        bool first = true;
        bool curIsClose = false;
        while ((verb = iter.next(pts)) != SkPath::kDone_Verb) {
            switch (verb) {
                case SkPath::kMove_Verb:
                    if (!first) {
                        ++contourCnt;

                        isCloseList->push_back(curIsClose);
                        curIsClose = false;
                    }
                    [[fallthrough]];
                case SkPath::kLine_Verb:
                case SkPath::kConic_Verb:
                case SkPath::kQuad_Verb:
                case SkPath::kCubic_Verb:
                    hasPoints = true;
                    break;
                case SkPath::kClose_Verb:
                    curIsClose = true;
                    break;
                default:
                    break;
            }
            first = false;
        }

        isCloseList->push_back(curIsClose);
        assert(isCloseList->size() == contourCnt);

        if (!hasPoints) {
            return 0;
        }
        return contourCnt;
    }

    SimpleTriangulator(const SkPath& path, SkArenaAlloc* alloc): GrTriangulator(path, alloc) {}
    virtual ~SimpleTriangulator() {}
};

JSArray EMSCRIPTEN_KEEPALIVE pathToContoursBuffer(const SkPath& path, SkScalar scale) {
    JSArray cmds = emscripten::val::array();

    SkArenaAlloc alloc(GrTriangulator::kArenaDefaultChunkSize);
    SimpleTriangulator triangulator(path, &alloc);

    bool isLinear;
    SkRect clipBounds = path.getBounds();
    SkMatrix m = SkMatrix::Scale(scale, scale);
    SkScalar tol = GrPathUtils::scaleToleranceToSrc(
        GrPathUtils::kDefaultTolerance, m, clipBounds);
    std::unique_ptr<GrTriangulator::VertexList[]> contours;
    std::vector<bool> isCloseList;

    int count = SimpleTriangulator::PathToContours(
        path, tol, clipBounds, triangulator, &contours, &isCloseList, &isLinear);

    for (int i = 0; i < count; ++i) {
        const GrTriangulator::VertexList& list = contours.get()[i];
        JSArray cmd = emscripten::val::array();
        while (gContours.size() <= i) {
            gContours.push_back({});
        }

        std::vector<SkPoint>& curContour = gContours[i];
        curContour.clear();
        GrTriangulator::Vertex* p = list.fHead;
        while (p) {
            if (p == list.fTail && isCloseList[i]) {
                const SkScalar nan = std::nanf(0);
                curContour.push_back({ nan, nan });
            } else {
                const SkPoint& point = p->fPoint;
                curContour.push_back({ point.fX, point.fY });
            }
            p = p->fNext;
        }

        // share raw points buffer to JS by Float32Array(PathKit.HEAPF32), and reuse global vertex buffer.
        SkScalar* points = reinterpret_cast<SkScalar*>(curContour.data());
        cmd.call<void>("push", reinterpret_cast<WASMPointerF32>(points), curContour.size() * 2);
        cmds.call<void>("push", cmd);
    }

    return cmds;
}

//========================================================================================
// SVG things
//========================================================================================

JSString EMSCRIPTEN_KEEPALIVE ToSVGString(const SkPath& path) {
    // Wrapping it in val automatically turns it into a JS string.
    // Not too sure on performance implications, but is is simpler than
    // returning a raw pointer to const char * and then using
    // UTF8ToString() on the calling side.
    return emscripten::val(SkParsePath::ToSVGString(path).c_str());
}


SkPathOrNull EMSCRIPTEN_KEEPALIVE FromSVGString(std::string str) {
    SkPath path;
    if (SkParsePath::FromSVGString(str.c_str(), &path)) {
        return emscripten::val(path);
    }
    return emscripten::val::null();
}

//========================================================================================
// PATHOP things
//========================================================================================

bool EMSCRIPTEN_KEEPALIVE ApplySimplify(SkPath& path) {
    return Simplify(path, &path);
}

bool EMSCRIPTEN_KEEPALIVE ApplyPathOp(SkPath& pathOne, const SkPath& pathTwo, SkPathOp op) {
    return Op(pathOne, pathTwo, op, &pathOne);
}

SkPathOrNull EMSCRIPTEN_KEEPALIVE MakeFromOp(const SkPath& pathOne, const SkPath& pathTwo, SkPathOp op) {
    SkPath out;
    if (Op(pathOne, pathTwo, op, &out)) {
        return emscripten::val(out);
    }
    return emscripten::val::null();
}

SkPathOrNull EMSCRIPTEN_KEEPALIVE ResolveBuilder(SkOpBuilder& builder) {
    SkPath path;
    if (builder.resolve(&path)) {
        return emscripten::val(path);
    }
    return emscripten::val::null();
}

SkPathOrNull MakeAsWinding(const SkPath& self) {
    SkPath out;
    if (AsWinding(self, &out)) {
        return emscripten::val(out);
    }
    return emscripten::val::null();
}

//========================================================================================
// Canvas things
//========================================================================================

void EMSCRIPTEN_KEEPALIVE ToCanvas(const SkPath& path, emscripten::val /* Path2D or Canvas*/ ctx) {
    SkPath::Iter iter(path, false);
    SkPoint pts[4];
    SkPath::Verb verb;
    while ((verb = iter.next(pts)) != SkPath::kDone_Verb) {
        switch (verb) {
            case SkPath::kMove_Verb:
                ctx.call<void>("moveTo", pts[0].x(), pts[0].y());
                break;
            case SkPath::kLine_Verb:
                ctx.call<void>("lineTo", pts[1].x(), pts[1].y());
                break;
            case SkPath::kQuad_Verb:
                ctx.call<void>("quadraticCurveTo", pts[1].x(), pts[1].y(), pts[2].x(), pts[2].y());
                break;
            case SkPath::kConic_Verb:
                SkPoint quads[5];
                // approximate with 2^1=2 quads.
                SkPath::ConvertConicToQuads(pts[0], pts[1], pts[2], iter.conicWeight(), quads, 1);
                ctx.call<void>("quadraticCurveTo", quads[1].x(), quads[1].y(), quads[2].x(), quads[2].y());
                ctx.call<void>("quadraticCurveTo", quads[3].x(), quads[3].y(), quads[4].x(), quads[4].y());
                break;
            case SkPath::kCubic_Verb:
                ctx.call<void>("bezierCurveTo", pts[1].x(), pts[1].y(), pts[2].x(), pts[2].y(),
                                                   pts[3].x(), pts[3].y());
                break;
            case SkPath::kClose_Verb:
                ctx.call<void>("closePath");
                break;
            case SkPath::kDone_Verb:
                break;
        }
    }
}

emscripten::val JSPath2D = emscripten::val::global("Path2D");

emscripten::val EMSCRIPTEN_KEEPALIVE ToPath2D(const SkPath& path) {
    emscripten::val retVal = JSPath2D.new_();
    ToCanvas(path, retVal);
    return retVal;
}

// ======================================================================================
// Path2D API things
// ======================================================================================
void ApplyAddRect(SkPath& path, SkScalar x, SkScalar y, SkScalar width, SkScalar height) {
    path.addRect(x, y, x+width, y+height);
}

void ApplyAddArc(SkPath& path, SkScalar x, SkScalar y, SkScalar radius,
              SkScalar startAngle, SkScalar endAngle, bool ccw) {
    SkPath temp;
    SkRect bounds = SkRect::MakeLTRB(x-radius, y-radius, x+radius, y+radius);
    const auto sweep = SkRadiansToDegrees(endAngle - startAngle) - 360 * ccw;
    temp.addArc(bounds, SkRadiansToDegrees(startAngle), sweep);
    path.addPath(temp, SkPath::kExtend_AddPathMode);
}

void ApplyEllipse(SkPath& path, SkScalar x, SkScalar y, SkScalar radiusX, SkScalar radiusY,
                     SkScalar rotation, SkScalar startAngle, SkScalar endAngle, bool ccw) {
    // This is easiest to do by making a new path and then extending the current path
    // (this properly catches the cases of if there's a moveTo before this call or not).
    SkRect bounds = SkRect::MakeLTRB(x-radiusX, y-radiusY, x+radiusX, y+radiusY);
    SkPath temp;
    const auto sweep = SkRadiansToDegrees(endAngle - startAngle) - (360 * ccw);
    temp.addArc(bounds, SkRadiansToDegrees(startAngle), sweep);

    SkMatrix m;
    m.setRotate(SkRadiansToDegrees(rotation), x, y);
    path.addPath(temp, m, SkPath::kExtend_AddPathMode);
}

void ApplyRoundRect(SkPath& path, SkScalar x, SkScalar y, SkScalar width, SkScalar height,
                    const SkScalar radii[], bool ccw = false) {
    const SkRect rect = SkRect::MakeXYWH(x, y, width, height);
    path.addRoundRect(rect, radii, ccw ? SkPathDirection::kCCW : SkPathDirection::kCW);
}

void ApplyRoundRect1(SkPath& path, SkScalar x, SkScalar y, SkScalar width, SkScalar height,
                    SkScalar corner) {
    SkScalar radii[] = { corner, corner, corner, corner, corner, corner, corner, corner };
    ApplyRoundRect(path, x, y, width, height, radii, false);
}

void ApplyRoundRect2(SkPath& path, SkScalar x, SkScalar y, SkScalar width, SkScalar height,
                    SkScalar lt_rb, SkScalar rt_lb) {
    SkScalar radii[] = { lt_rb, lt_rb, rt_lb, rt_lb, lt_rb, lt_rb, rt_lb, rt_lb };
    ApplyRoundRect(path, x, y, width, height, radii, false);
}

void ApplyRoundRect3(SkPath& path, SkScalar x, SkScalar y, SkScalar width, SkScalar height,
                    SkScalar lt, SkScalar rt_lb, SkScalar rb) {
    SkScalar radii[] = { lt, lt, rt_lb, rt_lb, rb, rb, rt_lb, rt_lb };
    ApplyRoundRect(path, x, y, width, height, radii, false);
}

void ApplyRoundRect4(SkPath& path, SkScalar x, SkScalar y, SkScalar width, SkScalar height,
                    SkScalar lt, SkScalar rt, SkScalar rb, SkScalar lb) {
    SkScalar radii[] = { lt, lt, rt, rt, rb, rb, lb, lb };
    ApplyRoundRect(path, x, y, width, height, radii, false);
}

// Allows for full matix control.
void ApplyAddPath(SkPath& orig, const SkPath& newPath,
                   SkScalar scaleX, SkScalar skewX,  SkScalar transX,
                   SkScalar skewY,  SkScalar scaleY, SkScalar transY,
                   SkScalar pers0, SkScalar pers1, SkScalar pers2) {
    SkMatrix m = SkMatrix::MakeAll(scaleX, skewX , transX,
                                   skewY , scaleY, transY,
                                   pers0 , pers1 , pers2);
    orig.addPath(newPath, m);
}

JSString GetFillTypeString(const SkPath& path) {
    if (path.getFillType() == SkPathFillType::kWinding) {
        return emscripten::val("nonzero");
    } else if (path.getFillType() == SkPathFillType::kEvenOdd) {
        return emscripten::val("evenodd");
    } else {
        SkDebugf("warning: can't translate inverted filltype to HTML Canvas\n");
        return emscripten::val("nonzero"); //Use default
    }
}

//========================================================================================
// Path Effects
//========================================================================================

bool ApplyDash(SkPath& path, SkScalar on, SkScalar off, SkScalar phase) {
    SkScalar intervals[] = { on, off };
    auto pe = SkDashPathEffect::Make(intervals, 2, phase);
    if (!pe) {
        SkDebugf("Invalid args to dash()\n");
        return false;
    }
    SkStrokeRec rec(SkStrokeRec::InitStyle::kHairline_InitStyle);
    if (pe->filterPath(&path, path, &rec, nullptr)) {
        return true;
    }
    SkDebugf("Could not make dashed path\n");
    return false;
}

bool ApplyTrim(SkPath& path, SkScalar startT, SkScalar stopT, bool isComplement) {
    auto mode = isComplement ? SkTrimPathEffect::Mode::kInverted : SkTrimPathEffect::Mode::kNormal;
    auto pe = SkTrimPathEffect::Make(startT, stopT, mode);
    if (!pe) {
        SkDebugf("Invalid args to trim(): startT and stopT must be in [0,1]\n");
        return false;
    }
    SkStrokeRec rec(SkStrokeRec::InitStyle::kHairline_InitStyle);
    if (pe->filterPath(&path, path, &rec, nullptr)) {
        return true;
    }
    SkDebugf("Could not trim path\n");
    return false;
}

struct StrokeOpts {
    // Default values are set in chaining.js which allows clients
    // to set any number of them. Otherwise, the binding code complains if
    // any are omitted.
    SkScalar width;
    SkScalar miter_limit;
    SkScalar res_scale;
    SkPaint::Join join;
    SkPaint::Cap cap;
};

bool ApplyStroke(SkPath& path, StrokeOpts opts) {
    SkPaint p;
    p.setStyle(SkPaint::kStroke_Style);
    p.setStrokeCap(opts.cap);
    p.setStrokeJoin(opts.join);
    p.setStrokeWidth(opts.width);
    p.setStrokeMiter(opts.miter_limit);
    // Default to 1.0 if 0 (or an invalid negative number)
    if (opts.res_scale <= 0) {
        opts.res_scale = 1.0;
    }
    return skpathutils::FillPathWithPaint(path, p, &path, nullptr, opts.res_scale);
}

//========================================================================================
// Matrix things
//========================================================================================

struct SimpleMatrix {
    SkScalar scaleX, skewX,  transX;
    SkScalar skewY,  scaleY, transY;
    SkScalar pers0,  pers1,  pers2;
};

SkMatrix toSkMatrix(const SimpleMatrix& sm) {
    return SkMatrix::MakeAll(sm.scaleX, sm.skewX , sm.transX,
                             sm.skewY , sm.scaleY, sm.transY,
                             sm.pers0 , sm.pers1 , sm.pers2);
}

void ApplyTransform(SkPath& orig, const SimpleMatrix& sm) {
    orig.transform(toSkMatrix(sm));
}

void ApplyTransform(SkPath& orig,
                    SkScalar scaleX, SkScalar skewX,  SkScalar transX,
                    SkScalar skewY,  SkScalar scaleY, SkScalar transY,
                    SkScalar pers0, SkScalar pers1, SkScalar pers2) {
    SkMatrix m = SkMatrix::MakeAll(scaleX, skewX , transX,
                                   skewY , scaleY, transY,
                                   pers0 , pers1 , pers2);
    orig.transform(m);
}

//========================================================================================
// Testing things
//========================================================================================

// The use case for this is on the JS side is something like:
//     PathKit.SkBits2FloatUnsigned(parseInt("0xc0a00000"))
// to have precise float values for tests. In the C++ tests, we can use SkBits2Float because
// it takes int32_t, but the JS parseInt basically returns an unsigned int. So, we add in
// this helper which casts for us on the way to SkBits2Float.
float SkBits2FloatUnsigned(uint32_t floatAsBits) {
    return SkBits2Float((int32_t) floatAsBits);
}

// Binds the classes to the JS
//
// See https://kripken.github.io/emscripten-site/docs/porting/connecting_cpp_and_javascript/embind.html#non-member-functions-on-the-javascript-prototype
// for more on binding non-member functions to the JS object, allowing us to rewire
// various functions.  That is, we can make the SkPath we expose appear to have methods
// that the original SkPath does not, like rect(x, y, width, height) and toPath2D().
//
// An important detail for binding non-member functions is that the first argument
// must be SkPath& (the reference part is very important).
//
// Note that we can't expose default or optional arguments, but we can have multiple
// declarations of the same function that take different amounts of arguments.
// For example, see _transform
// Additionally, we are perfectly happy to handle default arguments and function
// overloads in the JS glue code (see chaining.js::addPath() for an example).
EMSCRIPTEN_BINDINGS(skia) {
    class_<SkPath>("SkPath")
        .constructor<>()
        .constructor<const SkPath&>()

        // Path2D API
        .function("_addPath", &ApplyAddPath)
        // 3 additional overloads of addPath are handled in JS bindings
        .function("_arc", &ApplyAddArc)
        .function("_arcTo", &ApplyArcTo)
        //"bezierCurveTo" alias handled in JS bindings
        .function("_close", &ApplyClose)
        //"closePath" alias handled in JS bindings
        .function("_conicTo", &ApplyConicTo)
        .function("_cubicTo", &ApplyCubicTo)

        .function("_ellipse", &ApplyEllipse)
        .function("_lineTo", &ApplyLineTo)
        .function("_moveTo", &ApplyMoveTo)
        // "quadraticCurveTo" alias handled in JS bindings
        .function("_quadTo", &ApplyQuadTo)
        .function("_rect", &ApplyAddRect)
        .function("_roundRect1", &ApplyRoundRect1)
        .function("_roundRect2", &ApplyRoundRect2)
        .function("_roundRect3", &ApplyRoundRect3)
        .function("_roundRect4", &ApplyRoundRect4)

        // Extra features
        .function("setFillType", select_overload<void(SkPathFillType)>(&SkPath::setFillType))
        .function("getFillType", &SkPath::getFillType)
        .function("getFillTypeString", &GetFillTypeString)
        .function("getBounds", &SkPath::getBounds)
        .function("computeTightBounds", &SkPath::computeTightBounds)
        .function("equals", &Equals)
        .function("copy", &CopyPath)
        .function("reset", &ApplyReset)
        .function("rewind", &ApplyRewind)
        .function("getLastPoint", &GetLastPoint)
        .function("getLength", &getPathLength)
        .function("getGenerationID", &SkPath::getGenerationID)
        .function("contains", &SkPath::contains)
        .function("isHadCurve", &IsHadCurve)
        .function("isEmpty", &SkPath::isEmpty)

        // PathEffects
        .function("_dash", &ApplyDash)
        .function("_trim", &ApplyTrim)
        .function("_stroke", &ApplyStroke)

        // Matrix
        .function("_transform", select_overload<void(SkPath& orig, const SimpleMatrix& sm)>(&ApplyTransform))
        .function("_transform", select_overload<void(SkPath& orig, SkScalar, SkScalar, SkScalar, SkScalar, SkScalar, SkScalar, SkScalar, SkScalar, SkScalar)>(&ApplyTransform))

        // PathOps
        .function("_simplify", &ApplySimplify)
        .function("_op", &ApplyPathOp)
        .function("makeAsWinding", &MakeAsWinding)

        // Exporting
        .function("toCmds", &ToCmds)
        .function("toNonConicCmds", &ToNonConicCmds)
        .function("toPath2D", &ToPath2D)
        .function("toCanvas", &ToCanvas)
        .function("toSVGString", &ToSVGString)
        .function("_toContoursBuffer", &pathToContoursBuffer)
        .function("_toTrianglesBuffer", &pathToTrianglesBuffer)
        .function("_toAATrianglesBuffer", &pathToAATrianglesBuffer)
        .function("_toAABoundaryTrianglesBuffer", &pathToAABoundaryTrianglesBuffer)

#ifdef PATHKIT_TESTING
        .function("dump", select_overload<void() const>(&SkPath::dump))
        .function("dumpHex", select_overload<void() const>(&SkPath::dumpHex))
#endif
        ;

    class_<SkOpBuilder>("SkOpBuilder")
        .constructor<>()

        .function("add", &SkOpBuilder::add)
        .function("make", &ResolveBuilder)
        .function("resolve", &ResolveBuilder);

    // Without these function() bindings, the function would be exposed but oblivious to
    // our types (e.g. SkPath)

    // Import
    function("FromSVGString", &FromSVGString);
    function("NewPath", &NewPath);
    function("NewPath", &CopyPath);
    // FromCmds is defined in helper.js to make use of TypedArrays transparent.
    function("_FromCmds", &FromCmds);
    // Path2D is opaque, so we can't read in from it.
    function("_FromStrokeInk", &FromStrokeInk);

    // PathOps
    function("MakeFromOp", &MakeFromOp);

    enum_<SkPathOp>("PathOp")
        .value("DIFFERENCE",         SkPathOp::kDifference_SkPathOp)
        .value("INTERSECT",          SkPathOp::kIntersect_SkPathOp)
        .value("UNION",              SkPathOp::kUnion_SkPathOp)
        .value("XOR",                SkPathOp::kXOR_SkPathOp)
        .value("REVERSE_DIFFERENCE", SkPathOp::kReverseDifference_SkPathOp);

    enum_<SkPathFillType>("FillType")
        .value("WINDING",            SkPathFillType::kWinding)
        .value("EVENODD",            SkPathFillType::kEvenOdd)
        .value("INVERSE_WINDING",    SkPathFillType::kInverseWinding)
        .value("INVERSE_EVENODD",    SkPathFillType::kInverseEvenOdd);

    constant("MOVE_VERB",  MOVE);
    constant("LINE_VERB",  LINE);
    constant("QUAD_VERB",  QUAD);
    constant("CONIC_VERB", CONIC);
    constant("CUBIC_VERB", CUBIC);
    constant("CLOSE_VERB", CLOSE);

    // A value object is much simpler than a class - it is returned as a JS
    // object and does not require delete().
    // https://kripken.github.io/emscripten-site/docs/porting/connecting_cpp_and_javascript/embind.html#value-types
    value_object<SkRect>("SkRect")
        .field("fLeft",   &SkRect::fLeft)
        .field("fTop",    &SkRect::fTop)
        .field("fRight",  &SkRect::fRight)
        .field("fBottom", &SkRect::fBottom);

    function("LTRBRect", &SkRect::MakeLTRB);

    // Stroke
    enum_<SkPaint::Join>("StrokeJoin")
        .value("MITER", SkPaint::Join::kMiter_Join)
        .value("ROUND", SkPaint::Join::kRound_Join)
        .value("BEVEL", SkPaint::Join::kBevel_Join);

    enum_<SkPaint::Cap>("StrokeCap")
        .value("BUTT",   SkPaint::Cap::kButt_Cap)
        .value("ROUND",  SkPaint::Cap::kRound_Cap)
        .value("SQUARE", SkPaint::Cap::kSquare_Cap);

    value_object<StrokeOpts>("StrokeOpts")
        .field("width",       &StrokeOpts::width)
        .field("miter_limit", &StrokeOpts::miter_limit)
        .field("res_scale",   &StrokeOpts::res_scale)
        .field("join",        &StrokeOpts::join)
        .field("cap",         &StrokeOpts::cap);

    // Matrix
    // Allows clients to supply a 1D array of 9 elements and the bindings
    // will automatically turn it into a 3x3 2D matrix.
    // e.g. path.transform([0,1,2,3,4,5,6,7,8])
    // This is likely simpler for the client than exposing SkMatrix
    // directly and requiring them to do a lot of .delete().
    value_array<SimpleMatrix>("SkMatrix")
        .element(&SimpleMatrix::scaleX)
        .element(&SimpleMatrix::skewX)
        .element(&SimpleMatrix::transX)

        .element(&SimpleMatrix::skewY)
        .element(&SimpleMatrix::scaleY)
        .element(&SimpleMatrix::transY)

        .element(&SimpleMatrix::pers0)
        .element(&SimpleMatrix::pers1)
        .element(&SimpleMatrix::pers2);

    value_array<SkPoint>("SkPoint")
        .element(&SkPoint::fX)
        .element(&SkPoint::fY);

    // Not intended for external clients to call directly.
    // See helper.js for the client-facing implementation.
    class_<SkCubicMap>("_SkCubicMap")
        .constructor<SkPoint, SkPoint>()

        .function("computeYFromX", &SkCubicMap::computeYFromX)
        .function("computePtFromT", &SkCubicMap::computeFromT);


    // Test Utils
    function("SkBits2FloatUnsigned", &SkBits2FloatUnsigned);
}
