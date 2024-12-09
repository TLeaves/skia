#ifndef PATHKIT_C_BINDINGS_H
#define PATHKIT_C_BINDINGS_H

#if !defined(SK_API)
    #if defined(_MSC_VER)
        #if SKIA_IMPLEMENTATION
            #define SK_API __declspec(dllexport)
        #else
            #define SK_API __declspec(dllimport)
        #endif
    #else
        #define SK_API __attribute__((visibility("default")))
    #endif
#endif

class SkPath;

typedef struct _rect_t {
    float x;
    float y;
    float width;
    float height;
} rect_t;

//@see SkPaint::Join
typedef enum _line_join_t {
    LINE_JOIN_MITER,
    LINE_JOIN_ROUND,
    LINE_JOIN_BEVEL
} line_join_t;

// @see SkPaint::Cap
typedef enum _line_cap_t {
    LINE_CAP_BUTT,
    LINE_CAP_ROUND,
    LINE_CAP_SQUARE
} line_cap_t;

typedef struct _stroke_opts_t {
    float width;
    float miter_limit;
    float res_scale;
    line_join_t join;
    line_cap_t cap;
} stroke_opts_t;

// @see SkPathOp
typedef enum _path_op_t {
    DIFFERENCE,
    INTERSECT,
    UNION,
    XOR,
    REVERSE_DIFFERENCE
} path_op_t;

//========================================================================================
// PathKit
//========================================================================================

typedef struct _stylus_point_t {
    float x;
    float y;
    float pressure;
} stylus_point_t;

typedef enum _ink_endpoint_type_t {
    INK_ENDPOINT_TYPE_CIRCLE,
    INK_ENDPOINT_TYPE_SQUARE
} ink_endpoint_type_t;

extern "C" SK_API SkPath* pathkit_fromSVGString(const char* svg_string);

extern "C" SK_API SkPath* pathkit_fromStrokeInk(
    const stylus_point_t* stylus_point_ptr, int point_count, float line_width, ink_endpoint_type_t endpoint_type);

extern "C" SK_API SkPath* pathkit_makeFromOp(SkPath* pathOne, SkPath* pathTwo, path_op_t op);

//========================================================================================
// SkPath
//========================================================================================

typedef struct _context_t {
    void* instance;

    void (*moveTo)(void* self, float x, float y);
    void (*lineTo)(void* self, float x, float y);
    void (*quadraticCurveTo)(void* self, float x1, float y1, float x2, float y2);
    void (*bezierCurveTo)(void* self, float x1, float y1, float x2, float y2, float x3, float y3);
    void (*closePath)(void* self);
} context_t;

extern "C" SK_API SkPath* skpath_create();

extern "C" SK_API void skpath_destroy(SkPath* p);

extern "C" SK_API SkPath* skpath_copy(SkPath* p);

extern "C" SK_API void skpath_traverse(SkPath* p, context_t ctx);

extern "C" SK_API void skpath_addPath(SkPath* origin, SkPath* newPath,
                                        float scaleX = 1.0f, float skewX = 0.0f,  float transX = 0.0f,
                                        float skewY = 0.0f,  float scaleY = 1.0f, float transY = 0.0f,
                                        float pers0 = 0.0f,  float pers1 = 0.0f,  float pers2 = 1.0f);

extern "C" SK_API void skpath_moveTo(SkPath* p, float x, float y);

extern "C" SK_API void skpath_lineTo(SkPath* p, float x, float y);

extern "C" SK_API void skpath_quadTo(SkPath* p, float x1, float y1, float x2, float y2);

extern "C" SK_API void skpath_cubicTo(SkPath* p, float x1, float y1, float x2, float y2, float x3, float y3);

extern "C" SK_API void skpath_arc(SkPath* p, float x, float y, float radius, float startAngle, float endAngle, bool ccw);

extern "C" SK_API void skpath_arcTo(SkPath* p, float x1, float y1, float x2, float y2, float radius);

extern "C" SK_API void skpath_close(SkPath* p);

extern "C" SK_API void skpath_reset(SkPath* p);

extern "C" SK_API void skpath_rewind(SkPath* p);

extern "C" SK_API bool skpath_contains(SkPath* p, float x, float y);

extern "C" SK_API bool skpath_isHadCurve(SkPath* p);

extern "C" SK_API bool skpath_isEmpty(SkPath* p);

extern "C" SK_API bool skpath_simplify(SkPath* p);

extern "C" SK_API bool skpath_op(SkPath* p, SkPath* pathOther, path_op_t op);

extern "C" SK_API SkPath* skpath_makeAsWinding(SkPath* p);

extern "C" SK_API bool skpath_stroke(SkPath* p, stroke_opts_t opts);

extern "C" SK_API rect_t skpath_getBounds(SkPath* p);

extern "C" SK_API rect_t skpath_computeTightBounds(SkPath* p);

extern "C" SK_API void skpath_transform(SkPath* p,
                                        float scaleX, float skewX,  float transX,
                                        float skewY,  float scaleY, float transY,
                                        float pers0,  float pers1,  float pers2);

extern "C" SK_API bool skpath_toSVGString(SkPath* p, char** o_str, uint32_t* o_strlen);

#endif // !PATHKIT_C_BINDINGS_H
