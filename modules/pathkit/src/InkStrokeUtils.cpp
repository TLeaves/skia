#include "InkStrokeUtils.h"

#include "src/core/SkPathPriv.h"
#include "src/core/SkPointPriv.h"
#include "src/core/SkStrokerPriv.h"
#include "src/core/SkPaintDefaults.h"

namespace inkutils {

static bool set_normal_unitnormal(const SkPoint& before, const SkPoint& after, SkScalar scale,
                                  SkScalar radius,
                                  SkVector* normal, SkVector* unitNormal) {
    if (!unitNormal->setNormalize((after.fX - before.fX) * scale,
                                  (after.fY - before.fY) * scale)) {
        return false;
    }
    SkPointPriv::RotateCCW(unitNormal);
    unitNormal->scale(radius, normal);
    return true;
}

static bool set_normal_unitnormal(const SkVector& vec,
                                  SkScalar radius,
                                  SkVector* normal, SkVector* unitNormal) {
    if (!unitNormal->setNormalize(vec.fX, vec.fY)) {
        return false;
    }
    SkPointPriv::RotateCCW(unitNormal);
    unitNormal->scale(radius, normal);
    return true;
}

// copy from SkPath.reversePathTo
void reversePathTo(SkPath* self, const SkPath& path) {
    if (path.isEmpty()) {
        return;
    }

    const uint8_t* verbsBegin = SkPathPriv::VerbData(path);
    const uint8_t* verbs = verbsBegin + path.countVerbs();
    SkASSERT(verbsBegin[0] == SkPath::Verb::kMove_Verb);
    const SkPoint*  pts = SkPathPriv::PointData(path) + path.countPoints() - 1;
    const SkScalar* conicWeights = SkPathPriv::ConicWeightData(path) + SkPathPriv::ConicWeightCnt(path);

    while (verbs > verbsBegin) {
        uint8_t v = *--verbs;
        pts -= SkPathPriv::PtsInVerb(v);
        switch (v) {
            case SkPath::Verb::kMove_Verb:
                // if the path has multiple contours, stop after reversing the last
                return;
            case SkPath::Verb::kLine_Verb:
                self->lineTo(pts[0]);
                break;
            case SkPath::Verb::kQuad_Verb:
                self->quadTo(pts[1], pts[0]);
                break;
            case SkPath::Verb::kConic_Verb:
                self->conicTo(pts[1], pts[0], *--conicWeights);
                break;
            case SkPath::Verb::kCubic_Verb:
                self->cubicTo(pts[2], pts[1], pts[0]);
                break;
            case SkPath::Verb::kClose_Verb:
                break;
            default:
                SkDEBUGFAIL("bad verb");
                break;
        }
    }
}

struct StylusPoints {
    StylusPoint* ptr = nullptr;
    int count = 0;

    StylusPoints(StylusPoint* stylus_point_ptr, int point_count)
        : ptr(stylus_point_ptr), count(point_count) {}
};

class InkStroker {
public:
    InkStroker(const StylusPoints& src,
               SkScalar radius, SkScalar miterLimit, SkPaint::Cap,
               SkPaint::Join, SkScalar resScale,
               bool canIgnoreCenter);

    void moveTo(const StylusPoint&);
    void lineTo(const StylusPoint&);
    void close(bool isLine) { this->finishContour(true, isLine); }

    void done(SkPath* dst, bool isLine) {
        this->finishContour(false, isLine);
        dst->swap(fOuter);
    }

    SkScalar getResScale() const { return fResScale; }

private:
    SkScalar    fRadius;
    SkScalar    fInvMiterLimit;
    SkScalar    fResScale;
    SkScalar    fInvResScale;
    SkScalar    fInvResScaleSquared;

    SkVector    fFirstNormal, fPrevNormal, fFirstUnitNormal, fPrevUnitNormal;
    StylusPoint fFirstPt, fPrevPt;  // on original path
    SkPoint     fFirstOuterPt;
    int         fFirstOuterPtIndexInContour;
    int         fSegmentCount;
    bool        fPrevIsLine;
    bool        fCanIgnoreCenter;

    SkStrokerPriv::CapProc  fCapper;
    SkStrokerPriv::JoinProc fJoiner;

    SkPath  fInner, fOuter, fCusper; // outer is our working answer, inner is temp

    enum StrokeType {
        kOuter_StrokeType = 1,      // use sign-opposite values later to flip perpendicular axis
        kInner_StrokeType = -1
    } fStrokeType;

    enum ResultType {
        kSplit_ResultType,          // the caller should split the quad stroke in two
        kDegenerate_ResultType,     // the caller should add a line
        kQuad_ResultType,           // the caller should (continue to try to) add a quad stroke
    };

    enum ReductionType {
        kPoint_ReductionType,       // all curve points are practically identical
        kLine_ReductionType,        // the control point is on the line between the ends
        kQuad_ReductionType,        // the control point is outside the line between the ends
        kDegenerate_ReductionType,  // the control point is on the line but outside the ends
        kDegenerate2_ReductionType, // two control points are on the line but outside ends (cubic)
        kDegenerate3_ReductionType, // three areas of max curvature found (for cubic)
    };

    enum IntersectRayType {
        kCtrlPt_RayType,
        kResultType_RayType,
    };

    int fRecursionDepth;            // track stack depth to abort if numerics run amok
    bool fFoundTangents;            // do less work until tangents meet (cubic)
    bool fJoinCompleted;            // previous join was not degenerate

    void    finishContour(bool close, bool isLine);
    bool    preJoinTo(const StylusPoint&, SkVector* normal, SkVector* unitNormal,
                      bool isLine);
    void    postJoinTo(const StylusPoint&, const SkVector& normal,
                       const SkVector& unitNormal);

    void    line_to(const StylusPoint& currPt, const SkVector& normal);
};

InkStroker::InkStroker(const StylusPoints& src,
                       SkScalar radius, SkScalar miterLimit,
                       SkPaint::Cap cap, SkPaint::Join join, SkScalar resScale,
                       bool canIgnoreCenter)
        : fRadius(radius)
        , fResScale(resScale)
        , fCanIgnoreCenter(canIgnoreCenter) {

    /*  This is only used when join is miter_join, but we initialize it here
        so that it is always defined, to fis valgrind warnings.
    */
    fInvMiterLimit = 0;

    if (join == SkPaint::kMiter_Join) {
        if (miterLimit <= SK_Scalar1) {
            join = SkPaint::kBevel_Join;
        } else {
            fInvMiterLimit = SkScalarInvert(miterLimit);
        }
    }
    fCapper = SkStrokerPriv::CapFactory(cap);
    fJoiner = SkStrokerPriv::JoinFactory(join);
    fSegmentCount = -1;
    fFirstOuterPtIndexInContour = 0;
    fPrevIsLine = false;

    // Need some estimate of how large our final result (fOuter)
    // and our per-contour temp (fInner) will be, so we don't spend
    // extra time repeatedly growing these arrays.
    //
    // 3x for result == inner + outer + join (swag)
    // 1x for inner == 'wag' (worst contour length would be better guess)
    fOuter.incReserve(src.count * 3);
    fOuter.setIsVolatile(true);
    fInner.incReserve(src.count);
    fInner.setIsVolatile(true);
    // TODO : write a common error function used by stroking and filling
    // The '4' below matches the fill scan converter's error term
    fInvResScale = SkScalarInvert(resScale * 4);
    fInvResScaleSquared = fInvResScale * fInvResScale;
    fRecursionDepth = 0;
}

void InkStroker::moveTo(const StylusPoint& pt) {
    if (fSegmentCount > 0) {
        this->finishContour(false, false);
    }
    fSegmentCount = 0;
    fFirstPt = fPrevPt = pt;
    fJoinCompleted = false;
}

void InkStroker::lineTo(const StylusPoint& currPt) {
    bool teenyLine = SkPointPriv::EqualsWithinTolerance(
        fPrevPt.point, currPt.point, SK_ScalarNearlyZero * fInvResScale);
    if (SkStrokerPriv::CapFactory(SkPaint::kButt_Cap) == fCapper && teenyLine) {
        return;
    }
    if (teenyLine && (fJoinCompleted || (fPrevPt != currPt))) {
        return;
    }
    SkVector    normal, unitNormal;

    if (!this->preJoinTo(currPt, &normal, &unitNormal, true)) {
        return;
    }

    SkVector curNormal;
    unitNormal.scale(fRadius * currPt.pressure, &curNormal);

    this->line_to(currPt, curNormal);
    this->postJoinTo(currPt, curNormal, unitNormal);
}

bool InkStroker::preJoinTo(const StylusPoint& currPt, SkVector* normal,
                              SkVector* unitNormal, bool currIsLine) {
    SkASSERT(fSegmentCount >= 0);

    SkScalar    prevX = fPrevPt.point.fX;
    SkScalar    prevY = fPrevPt.point.fY;

    if (!set_normal_unitnormal(fPrevPt.point, currPt.point, fResScale, fRadius * fPrevPt.pressure, normal, unitNormal)) {
        if (SkStrokerPriv::CapFactory(SkPaint::kButt_Cap) == fCapper) {
            return false;
        }
        /* Square caps and round caps draw even if the segment length is zero.
           Since the zero length segment has no direction, set the orientation
           to upright as the default orientation */
        normal->set(fRadius * fPrevPt.pressure, 0);
        unitNormal->set(1, 0);
    }

    if (fSegmentCount == 0) {
        fFirstNormal = *normal;
        fFirstUnitNormal = *unitNormal;
        fFirstOuterPt.set(prevX + normal->fX, prevY + normal->fY);

        fOuter.moveTo(fFirstOuterPt.fX, fFirstOuterPt.fY);
        fInner.moveTo(prevX - normal->fX, prevY - normal->fY);
    } else {    // we have a previous segment
        fJoiner(&fOuter, &fInner, fPrevUnitNormal, fPrevPt.point, *unitNormal,
                fRadius * fPrevPt.pressure, fInvMiterLimit, fPrevIsLine, currIsLine);
    }
    fPrevIsLine = currIsLine;
    return true;
}

void InkStroker::line_to(const StylusPoint& currPt, const SkVector& normal) {
    fOuter.lineTo(currPt.point.fX + normal.fX, currPt.point.fY + normal.fY);
    fInner.lineTo(currPt.point.fX - normal.fX, currPt.point.fY - normal.fY);
}

void InkStroker::postJoinTo(const StylusPoint& currPt, const SkVector& normal,
                               const SkVector& unitNormal) {
    fJoinCompleted = true;
    fPrevPt = currPt;
    fPrevUnitNormal = unitNormal;
    fPrevNormal = normal;
    fSegmentCount += 1;
}

void InkStroker::finishContour(bool close, bool currIsLine) {
    if (fSegmentCount > 0) {
        SkPoint pt;

        if (close) {
            fJoiner(&fOuter, &fInner, fPrevUnitNormal, fPrevPt.point,
                    fFirstUnitNormal, fRadius, fInvMiterLimit,
                    fPrevIsLine, currIsLine);
            fOuter.close();

            if (fCanIgnoreCenter) {
                // If we can ignore the center just make sure the larger of the two paths
                // is preserved and don't add the smaller one.
                if (fInner.getBounds().contains(fOuter.getBounds())) {
                    fInner.swap(fOuter);
                }
            } else {
                // now add fInner as its own contour
                fInner.getLastPt(&pt);
                fOuter.moveTo(pt.fX, pt.fY);
                // fOuter.reversePathTo(fInner);
                reversePathTo(&fOuter, fInner);
                fOuter.close();
            }
        } else {    // add caps to start and end
            // cap the end
            fInner.getLastPt(&pt);
            fCapper(&fOuter, fPrevPt.point, fPrevNormal, pt,
                    currIsLine ? &fInner : nullptr);
            // fOuter.reversePathTo(fInner);
            reversePathTo(&fOuter, fInner);
            // cap the start
            fCapper(&fOuter, fFirstPt.point, -fFirstNormal, fFirstOuterPt,
                    fPrevIsLine ? &fInner : nullptr);
            fOuter.close();
        }
        if (!fCusper.isEmpty()) {
            fOuter.addPath(fCusper);
            fCusper.rewind();
        }
    }
    // since we may re-use fInner, we rewind instead of reset, to save on
    // reallocating its internal storage.
    fInner.rewind();
    fSegmentCount = -1;
    fFirstOuterPtIndexInContour = fOuter.countPoints();
}

class InkStroke {
public:
    InkStroke();
    InkStroke(const SkPaint&);

    SkPaint::Cap    getCap() const { return (SkPaint::Cap)fCap; }
    SkPaint::Join   getJoin() const { return (SkPaint::Join)fJoin; }

    void strokeInk(const StylusPoint* stylus_point_ptr, int point_count, SkPath* dst) const;

private:
    SkScalar    fWidth, fMiterLimit;
    uint8_t     fCap, fJoin;
    SkScalar    fResScale;
};

InkStroke::InkStroke() {
    fWidth      = SK_Scalar1;
    fMiterLimit = SkPaintDefaults_MiterLimit;
    fResScale   = 1;
    fCap        = SkPaint::kDefault_Cap;
    fJoin       = SkPaint::kDefault_Join;
}

InkStroke::InkStroke(const SkPaint& p) {
    fWidth      = p.getStrokeWidth();
    fMiterLimit = p.getStrokeMiter();
    fResScale   = 1;
    fCap        = (uint8_t)p.getStrokeCap();
    fJoin       = (uint8_t)p.getStrokeJoin();
}

void InkStroke::strokeInk(const StylusPoint* stylus_point_ptr, int point_count, SkPath* dst) const {
    SkASSERT(dst);

    SkScalar radius = SkScalarHalf(fWidth);

    if (radius <= 0) {
        return;
    }

    StylusPoints    src(const_cast<StylusPoint*>(stylus_point_ptr), point_count);
    InkStroker      stroker(src, radius, fMiterLimit, this->getCap(), this->getJoin(),
                            fResScale, false);
    SkPath::Verb    lastSegment = SkPath::kMove_Verb;

    for (int i = 0; i < src.count; ++i) {
        const StylusPoint& sp = src.ptr[i];
        if (i == 0) {
            stroker.moveTo(sp);
        } else {
            stroker.lineTo(sp);
            lastSegment = SkPath::kLine_Verb;
        }
    }

    stroker.done(dst, lastSegment == SkPath::kLine_Verb);
}

bool StrokeInkWithPaint(
    const StylusPoint* stylus_point_ptr,
    int point_count,
    InkEndpointType endpoint_type,
    const SkPaint &paint,
    SkPath *dst) {

    if (point_count <= 0) {
        dst->reset();
        return false;
    }

    InkStroke stroke(paint);
    stroke.strokeInk(stylus_point_ptr, point_count, dst);

    if (!dst->isFinite()) {
        dst->reset();
        return false;
    }

    return true;
}

}