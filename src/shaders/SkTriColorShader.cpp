/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/shaders/SkTriColorShader.h"

#include "include/core/SkMatrix.h"
#include "include/core/SkPoint.h"
#include "include/core/SkTypes.h"
#include "include/private/SkColorData.h"
#include "src/base/SkVx.h"
#include "src/core/SkEffectPriv.h"
#include "src/core/SkRasterPipeline.h"
#include "src/core/SkRasterPipelineOpList.h"

#if defined(SK_ENABLE_SKVM)
#include "src/core/SkVM.h"
#endif

bool SkTriColorShader::appendStages(const SkStageRec& rec, const SkShaders::MatrixRec&) const {
    rec.fPipeline->append(SkRasterPipelineOp::seed_shader);
    if (fUsePersp) {
        rec.fPipeline->append(SkRasterPipelineOp::matrix_perspective, &fM33);
    }
    rec.fPipeline->append(SkRasterPipelineOp::matrix_4x3, &fM43);
    return true;
}

#if defined(SK_ENABLE_SKVM)
skvm::Color SkTriColorShader::program(skvm::Builder* b,
                                      skvm::Coord device,
                                      skvm::Coord local,
                                      skvm::Color,
                                      const SkShaders::MatrixRec&,
                                      const SkColorInfo&,
                                      skvm::Uniforms* uniforms,
                                      SkArenaAlloc* alloc) const {
    fColorMatrix = uniforms->pushPtr(&fM43);

    skvm::F32 x = local.x, y = local.y;

    if (fUsePersp) {
        fCoordMatrix = uniforms->pushPtr(&fM33);
        auto dot = [&, x, y](int row) {
            return b->mad(x, b->arrayF(fCoordMatrix, row),
                             b->mad(y, b->arrayF(fCoordMatrix, row + 3),
                                       b->arrayF(fCoordMatrix, row + 6)));
        };

        x = dot(0);
        y = dot(1);
        x = x * (1.0f / dot(2));
        y = y * (1.0f / dot(2));
    }

    auto colorDot = [&, x, y](int row) {
        return b->mad(x, b->arrayF(fColorMatrix, row),
                         b->mad(y, b->arrayF(fColorMatrix, row + 4),
                                   b->arrayF(fColorMatrix, row + 8)));
    };

    skvm::Color color;
    color.r = colorDot(0);
    color.g = colorDot(1);
    color.b = colorDot(2);
    color.a = colorDot(3);
    return color;
}
#endif

bool SkTriColorShader::update(const SkMatrix& ctmInv,
                              const SkPoint pts[],
                              const SkPMColor4f colors[],
                              int index0,
                              int index1,
                              int index2) {
    SkMatrix m, im;
    m.reset();
    m.set(0, pts[index1].fX - pts[index0].fX);
    m.set(1, pts[index2].fX - pts[index0].fX);
    m.set(2, pts[index0].fX);
    m.set(3, pts[index1].fY - pts[index0].fY);
    m.set(4, pts[index2].fY - pts[index0].fY);
    m.set(5, pts[index0].fY);
    if (!m.invert(&im)) {
        return false;
    }

    fM33.setConcat(im, ctmInv);

    auto c0 = skvx::float4::Load(colors[index0].vec()),
         c1 = skvx::float4::Load(colors[index1].vec()),
         c2 = skvx::float4::Load(colors[index2].vec());

    (c1 - c0).store(&fM43.fMat[0]);
    (c2 - c0).store(&fM43.fMat[4]);
    c0.store(&fM43.fMat[8]);

    if (!fUsePersp) {
        fM43.setConcat(fM43, fM33);
    }
    return true;
}
