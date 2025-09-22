/*===---- velintrin_approx.h - VEL intrinsics helper for VE ----------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */
#ifndef __VEL_INTRIN_APPROX_H__
#define __VEL_INTRIN_APPROX_H__

static inline __vr _vel_approx_vfdivs_vvvl(__vr v0, __vr v1, int l) {
  float s0;
  __vr v2, v3, v4, v5;
  v5 = _vel_vrcps_vvl(v1, l);
  s0 = 1.0;
  v4 = _vel_vfnmsbs_vsvvl(s0, v1, v5, l);
  v3 = _vel_vfmads_vvvvl(v5, v5, v4, l);
  v2 = _vel_vfmuls_vvvl(v0, v3, l);
  v4 = _vel_vfnmsbs_vvvvl(v0, v2, v1, l);
  v2 = _vel_vfmads_vvvvl(v2, v5, v4, l);
  v0 = _vel_vfnmsbs_vvvvl(v0, v2, v1, l);
  v0 = _vel_vfmads_vvvvl(v2, v3, v0, l);
  return v0;
}

static inline __vr _vel_approx_pvfdiv_vvvl(__vr v0, __vr v1, int l) {
  float s0;
  __vr v2, v3, v4, v5;
  v5 = _vel_pvrcp_vvl(v1, l);
  s0 = 1.0;
  v4 = _vel_pvfnmsb_vsvvl(s0, v1, v5, l);
  v3 = _vel_pvfmad_vvvvl(v5, v5, v4, l);
  v2 = _vel_pvfmul_vvvl(v0, v3, l);
  v4 = _vel_pvfnmsb_vvvvl(v0, v2, v1, l);
  v2 = _vel_pvfmad_vvvvl(v2, v5, v4, l);
  v0 = _vel_pvfnmsb_vvvvl(v0, v2, v1, l);
  v0 = _vel_pvfmad_vvvvl(v2, v3, v0, l);
  return v0;
}

static inline __vr _vel_approx_vfdivs_vsvl(float s0, __vr v0, int l) {
  float s1;
  __vr v1, v2, v3, v4;
  v4 = _vel_vrcps_vvl(v0, l);
  s1 = 1.0;
  v2 = _vel_vfnmsbs_vsvvl(s1, v0, v4, l);
  v2 = _vel_vfmads_vvvvl(v4, v4, v2, l);
  v1 = _vel_vfmuls_vsvl(s0, v2, l);
  v3 = _vel_vfnmsbs_vsvvl(s0, v1, v0, l);
  v1 = _vel_vfmads_vvvvl(v1, v4, v3, l);
  v3 = _vel_vfnmsbs_vsvvl(s0, v1, v0, l);
  v0 = _vel_vfmads_vvvvl(v1, v2, v3, l);
  return v0;
}

static inline __vr _vel_approx_vfdivs_vvsl(__vr v0, float s0, int l) {
  float s1;
  __vr v1, v2;
  s1 = 1.0f / s0;
  v1 = _vel_vfmuls_vsvl(s1, v0, l);
  v2 = _vel_vfnmsbs_vvsvl(v0, s0, v1, l);
  v0 = _vel_vfmads_vvsvl(v1, s1, v2, l);
  return v0;
}

static inline __vr _vel_approx_vfdivd_vsvl(double s0, __vr v0, int l) {
  __vr v1, v2, v3;
  v2 = _vel_vrcpd_vvl(v0, l);
  double s1 = 1.0;
  v3 = _vel_vfnmsbd_vsvvl(s1, v0, v2, l);
  v2 = _vel_vfmadd_vvvvl(v2, v2, v3, l);
  v1 = _vel_vfnmsbd_vsvvl(s1, v0, v2, l);
  v1 = _vel_vfmadd_vvvvl(v2, v2, v1, l);
  v1 = _vel_vaddul_vsvl(1, v1, l);
  v3 = _vel_vfnmsbd_vsvvl(s1, v0, v1, l);
  v3 = _vel_vfmadd_vvvvl(v1, v1, v3, l);
  v1 = _vel_vfmuld_vsvl(s0, v3, l);
  v0 = _vel_vfnmsbd_vsvvl(s0, v1, v0, l);
  v0 = _vel_vfmadd_vvvvl(v1, v3, v0, l);
  return v0;
}

static inline __vr _vel_approx_vfsqrtd_vvl(__vr v0, int l) {
  double s0, s1;
  __vr v1, v2, v3;
  v2 = _vel_vrsqrtdnex_vvl(v0, l);
  v1 = _vel_vfmuld_vvvl(v0, v2, l);
  s0 = 1.0;
  s1 = 0.5;
  v3 = _vel_vfnmsbd_vsvvl(s0, v1, v2, l);
  v3 = _vel_vfmuld_vsvl(s1, v3, l);
  v2 = _vel_vfmadd_vvvvl(v2, v2, v3, l);
  v1 = _vel_vfmuld_vvvl(v0, v2, l);
  v3 = _vel_vfnmsbd_vsvvl(s0, v1, v2, l);
  v3 = _vel_vfmuld_vsvl(s1, v3, l);
  v0 = _vel_vfmadd_vvvvl(v1, v1, v3, l);
  return v0;
}

static inline __vr _vel_approx_vfsqrts_vvl(__vr v0, int l) {
  float s0, s1;
  __vr v1, v2, v3;
  v0 = _vel_vcvtds_vvl(v0, l);
  v2 = _vel_vrsqrtdnex_vvl(v0, l);
  v1 = _vel_vfmuld_vvvl(v0, v2, l);
  s0 = 1.0;
  s1 = 0.5;
  v3 = _vel_vfnmsbd_vsvvl(s0, v1, v2, l);
  v3 = _vel_vfmuld_vsvl(s1, v3, l);
  v2 = _vel_vfmadd_vvvvl(v2, v2, v3, l);
  v1 = _vel_vfmuld_vvvl(v0, v2, l);
  v3 = _vel_vfnmsbd_vsvvl(s0, v1, v2, l);
  v3 = _vel_vfmuld_vsvl(s1, v3, l);
  v0 = _vel_vfmadd_vvvvl(v1, v1, v3, l);
  v0 = _vel_vcvtsd_vvl(v0, l);
  return v0;
}

#endif
