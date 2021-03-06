/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "GrCCConicShader.h"

#include "glsl/GrGLSLFragmentShaderBuilder.h"
#include "glsl/GrGLSLVertexGeoBuilder.h"

void GrCCConicShader::emitSetupCode(GrGLSLVertexGeoBuilder* s, const char* pts, const char* wind,
                                    const char** outHull4) const {
    // K is distance from the line P2 -> P0. L is distance from the line P0 -> P1, scaled by 2w.
    // M is distance from the line P1 -> P2, scaled by 2w. We do this in a space where P1=0.
    s->declareGlobal(fKLMMatrix);
    s->codeAppendf("float x0 = %s[0].x - %s[1].x, x2 = %s[2].x - %s[1].x;", pts, pts, pts, pts);
    s->codeAppendf("float y0 = %s[0].y - %s[1].y, y2 = %s[2].y - %s[1].y;", pts, pts, pts, pts);
    s->codeAppendf("float w = %s[3].x;", pts);
    s->codeAppendf("%s = float3x3(y2 - y0, x0 - x2, x2*y0 - x0*y2, "
                                 "2*w * float2(+y0, -x0), 0, "
                                 "2*w * float2(-y2, +x2), 0);", fKLMMatrix.c_str());

    s->declareGlobal(fControlPoint);
    s->codeAppendf("%s = %s[1];", fControlPoint.c_str(), pts);

    // Scale KLM by the inverse Manhattan width of K. This allows K to double as the flat opposite
    // edge AA. kwidth will not be 0 because we cull degenerate conics on the CPU.
    s->codeAppendf("float kwidth = 2*bloat * %s * (abs(%s[0].x) + abs(%s[0].y));",
                   wind, fKLMMatrix.c_str(), fKLMMatrix.c_str());
    s->codeAppendf("%s *= 1/kwidth;", fKLMMatrix.c_str());

    if (outHull4) {
        // Clip the conic triangle by the tangent line at maximum height. Conics have the nice
        // property that maximum height always occurs at T=.5. This is a simple application for
        // De Casteljau's algorithm.
        s->codeAppendf("float2 p1w = %s[1]*w;", pts);
        s->codeAppend ("float r = 1 / (1 + w);");
        s->codeAppendf("float2 conic_hull[4] = float2[4](%s[0], "
                                                        "(%s[0] + p1w) * r, "
                                                        "(p1w + %s[2]) * r, "
                                                        "%s[2]);", pts, pts, pts, pts);
        *outHull4 = "conic_hull";
    }
}

void GrCCConicShader::onEmitVaryings(GrGLSLVaryingHandler* varyingHandler,
                                     GrGLSLVarying::Scope scope, SkString* code,
                                     const char* position, const char* coverage,
                                     const char* cornerCoverage) {
    fKLM_fWind.reset(kFloat4_GrSLType, scope);
    varyingHandler->addVarying("klm_and_wind", &fKLM_fWind);
    code->appendf("float3 klm = float3(%s - %s, 1) * %s;",
                  position, fControlPoint.c_str(), fKLMMatrix.c_str());
    code->appendf("%s.xyz = klm;", OutName(fKLM_fWind));
    code->appendf("%s.w = %s;", OutName(fKLM_fWind), coverage); // coverage == wind.

    fGrad_fCorner.reset(cornerCoverage ? kFloat4_GrSLType : kFloat2_GrSLType, scope);
    varyingHandler->addVarying(cornerCoverage ? "grad_and_corner" : "grad", &fGrad_fCorner);
    code->appendf("%s.xy = 2*bloat * (float3x2(%s) * float3(2*klm[0], -klm[2], -klm[1]));",
                  OutName(fGrad_fCorner), fKLMMatrix.c_str());

    if (cornerCoverage) {
        code->appendf("half hull_coverage;");
        this->calcHullCoverage(code, "klm", OutName(fGrad_fCorner), "hull_coverage");
        code->appendf("%s.zw = half2(hull_coverage, 1) * %s;",
                      OutName(fGrad_fCorner), cornerCoverage);
    }
}

void GrCCConicShader::onEmitFragmentCode(GrGLSLFPFragmentBuilder* f,
                                         const char* outputCoverage) const {
    this->calcHullCoverage(&AccessCodeString(f), fKLM_fWind.fsIn(), fGrad_fCorner.fsIn(),
                           outputCoverage);
    f->codeAppendf("%s *= %s.w;", outputCoverage, fKLM_fWind.fsIn()); // Wind.

    if (kFloat4_GrSLType == fGrad_fCorner.type()) {
        f->codeAppendf("%s = %s.z * %s.w + %s;", // Attenuated corner coverage.
                       outputCoverage, fGrad_fCorner.fsIn(), fGrad_fCorner.fsIn(),
                       outputCoverage);
    }
}

void GrCCConicShader::calcHullCoverage(SkString* code, const char* klm, const char* grad,
                                       const char* outputCoverage) const {
    code->appendf("float k = %s.x, l = %s.y, m = %s.z;", klm, klm, klm);
    code->append ("float f = k*k - l*m;");
    code->appendf("float fwidth = abs(%s.x) + abs(%s.y);", grad, grad);
    code->appendf("%s = min(0.5 - f/fwidth, 1);", outputCoverage); // Curve coverage.
    code->append ("half d = min(k - 0.5, 0);"); // K doubles as the flat opposite edge's AA.
    code->appendf("%s = max(%s + d, 0);", outputCoverage, outputCoverage); // Total hull coverage.
}
