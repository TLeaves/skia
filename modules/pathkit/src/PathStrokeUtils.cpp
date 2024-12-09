#include "PathStrokeUtils.h"

#include "include/core/SkPaint.h"
#include "src/core/SkPaintDefaults.h"
#include "src/core/SkStroke.h"

#include <algorithm>

namespace utils {

// must be < 0, since ==0 means hairline, and >0 means normal stroke
#define kStrokeRec_FillStyleWidth     (-SK_Scalar1)

class SkStrokeRecSlim {
public:
    enum InitStyle {
        kHairline_InitStyle,
        kFill_InitStyle
    };
    enum Style {
        kHairline_Style,
        kFill_Style,
        kStroke_Style,
        kStrokeAndFill_Style
    };

    SkStrokeRecSlim(const StrokeOpts&, Style, SkScalar resScale = 1);

    static constexpr int kStyleCount = kStrokeAndFill_Style + 1;

    Style getStyle() const;

    bool isHairlineStyle() const {
        return kHairline_Style == this->getStyle();
    }

    bool isFillStyle() const {
        return kFill_Style == this->getStyle();
    }

    SkScalar getResScale() const {
        return fResScale;
    }

    void setResScale(SkScalar rs) {
        SkASSERT(rs > 0 && SkScalarIsFinite(rs));
        fResScale = rs;
    }

    /**
     *  Returns true if this specifes any thick stroking, i.e. applyToPath()
     *  will return true.
     */
    bool needToApply() const {
        Style style = this->getStyle();
        return (kStroke_Style == style) || (kStrokeAndFill_Style == style);
    }

    /**
     *  Apply these stroke parameters to the src path, returning the result
     *  in dst.
     *
     *  If there was no change (i.e. style == hairline or fill) this returns
     *  false and dst is unchanged. Otherwise returns true and the result is
     *  stored in dst.
     *
     *  src and dst may be the same path.
     */
    bool applyToPath(SkPath* dst, const SkPath& src) const;

private:
    void init(const StrokeOpts&, Style, SkScalar resScale);

    SkScalar        fResScale;
    SkScalar        fWidth;
    SkScalar        fMiterLimit;
    // The following three members are packed together into a single u32.
    // This is to avoid unnecessary padding and ensure binary equality for
    // hashing (because the padded areas might contain garbage values).
    //
    // fCap and fJoin are larger than needed to avoid having to initialize
    // any pad values
    uint32_t        fCap : 16;             // SkPaint::Cap
    uint32_t        fJoin : 15;            // SkPaint::Join
    uint32_t        fStrokeAndFill : 1;    // bool
};

SkStrokeRecSlim::SkStrokeRecSlim(const StrokeOpts& opts, Style style, SkScalar resScale) {
    init(opts, style, resScale);
}

void SkStrokeRecSlim::init(const StrokeOpts& opts, Style style, SkScalar resScale) {
    fResScale = resScale;

    switch (style) {
        case Style::kFill_Style:
            fWidth = kStrokeRec_FillStyleWidth;
            fStrokeAndFill = false;
            break;
        case Style::kStroke_Style:
            fWidth = opts.width;
            fStrokeAndFill = false;
            break;
        case Style::kStrokeAndFill_Style:
            if (0 == opts.width) {
                // hairline+fill == fill
                fWidth = kStrokeRec_FillStyleWidth;
                fStrokeAndFill = false;
            } else {
                fWidth = opts.width;
                fStrokeAndFill = true;
            }
            break;
        default:
            SkDEBUGFAIL("unknown paint style");
            // fall back on just fill
            fWidth = kStrokeRec_FillStyleWidth;
            fStrokeAndFill = false;
            break;
    }

    // copy these from the paint, regardless of our "style"
    fMiterLimit = opts.miter_limit;
    fCap        = opts.cap;
    fJoin       = opts.join;
}

SkStrokeRecSlim::Style SkStrokeRecSlim::getStyle() const {
    if (fWidth < 0) {
        return kFill_Style;
    } else if (0 == fWidth) {
        return kHairline_Style;
    } else {
        return fStrokeAndFill ? kStrokeAndFill_Style : kStroke_Style;
    }
}

bool SkStrokeRecSlim::applyToPath(SkPath* dst, const SkPath& src) const {
    if (fWidth <= 0) {  // hairline or fill
        return false;
    }

    SkStroke stroker;
    stroker.setCap((SkPaint::Cap)fCap);
    stroker.setJoin((SkPaint::Join)fJoin);
    stroker.setMiterLimit(fMiterLimit);
    stroker.setWidth(fWidth);
    stroker.setDoFill(fStrokeAndFill);
#ifdef SK_DEBUG
    stroker.setResScale(gDebugStrokerErrorSet ? gDebugStrokerError : fResScale);
#else
    stroker.setResScale(fResScale);
#endif
    stroker.strokePath(src, dst);
    return true;
}

bool StrokePathWithOpts(
    const SkPath& src,
    const StrokeOpts& opts,
    SkPath* dst,
    const SkRect* cullRect,
    SkScalar resScale) {

    if (!src.isFinite()) {
        dst->reset();
        return false;
    }

    SkMatrix ctm = SkMatrix::Scale(resScale, resScale);
    SkStrokeRecSlim rec(opts, SkStrokeRecSlim::Style::kStroke_Style, resScale);

    const SkPath* srcPtr = &src;

    if (!rec.applyToPath(dst, *srcPtr)) {
        *dst = *srcPtr;
    }

    if (!dst->isFinite()) {
        dst->reset();
        return false;
    }
    return !rec.isHairlineStyle();
}

}