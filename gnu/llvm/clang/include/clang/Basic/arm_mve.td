//===- arm_mve.td - ACLE intrinsic functions for MVE architecture ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the set of ACLE-specified source-level intrinsic
// functions wrapping the MVE vector instruction set and scalar shift
// operations.
//
// Refer to comments in arm_mve_defs.td for the infrastructure used in
// here, and to MveEmitter.cpp for how those are used in turn to
// generate code.
//
//===----------------------------------------------------------------------===//

include "arm_mve_defs.td"

let params = T.Usual in
foreach n = [ 2, 4 ] in {
  def "vst"#n#"q": Intrinsic<Void, (args Ptr<Scalar>, MultiVector<n>),
                             (CustomCodegen<"VST24"> n:$NumVectors,
                              "Intrinsic::arm_mve_vst"#n#"q":$IRIntr)>;
  def "vld"#n#"q": Intrinsic<MultiVector<n>, (args CPtr<Scalar>),
                             (CustomCodegen<"VLD24"> n:$NumVectors,
                              "Intrinsic::arm_mve_vld"#n#"q":$IRIntr)>;
}

multiclass bit_op_fp<IRBuilder bitop> {
def "": Intrinsic<Vector, (args Vector:$a, Vector:$b),
   (bitcast (bitop (bitcast $a, UVector), (bitcast $b, UVector)), Vector)>;
}

multiclass bit_op_fp_with_inv<IRBuilder bitop> {
def "": Intrinsic<Vector, (args Vector:$a, Vector:$b),
   (bitcast (bitop (bitcast $a, UVector), (not (bitcast $b, UVector))), Vector)>;
}

let params = T.Signed in {
def vqaddq: Intrinsic<Vector, (args Vector:$a, Vector:$b),
                              (IRIntBase<"sadd_sat", [Vector]> $a, $b)>;
def vqsubq: Intrinsic<Vector, (args Vector:$a, Vector:$b),
                              (IRIntBase<"ssub_sat", [Vector]> $a, $b)>;
let pnt = PNT_NType in {
  def vqaddq_n: Intrinsic<Vector, (args Vector:$a, unpromoted<Scalar>:$b),
                                  (IRIntBase<"sadd_sat", [Vector]> $a, (splat $b))>;
  def vqsubq_n: Intrinsic<Vector, (args Vector:$a, unpromoted<Scalar>:$b),
                                  (IRIntBase<"ssub_sat", [Vector]> $a, (splat $b))>;
}
}
let params = T.Unsigned in {
def vqaddq_u: Intrinsic<Vector, (args Vector:$a, Vector:$b),
                                (IRIntBase<"uadd_sat", [Vector]> $a, $b)>,
              NameOverride<"vqaddq">;
def vqsubq_u: Intrinsic<Vector, (args Vector:$a, Vector:$b),
                                (IRIntBase<"usub_sat", [Vector]> $a, $b)>,
              NameOverride<"vqsubq">;
let pnt = PNT_NType in {
  def vqaddq_u_n: Intrinsic<Vector, (args Vector:$a, unpromoted<Scalar>:$b),
                                    (IRIntBase<"uadd_sat", [Vector]> $a, (splat $b))>,
                  NameOverride<"vqaddq_n">;
  def vqsubq_u_n: Intrinsic<Vector, (args Vector:$a, unpromoted<Scalar>:$b),
                                    (IRIntBase<"usub_sat", [Vector]> $a, (splat $b))>,
                  NameOverride<"vqsubq_n">;
}
}

// Some intrinsics below are implemented not as IR fragments, but as
// special-purpose IR intrinsics. This is because such a general form
// (such as NEON uses) required a variable-width vector size, and we are
// restricted to 128 bit. Although we can possibly get clever with lane
// operations, the consequent IR representation would be very hard to
// write sensibly. In particular, doubling a vector's width would be a
// mess. Other intrinsics just don't translate nicely into IR.
let params = T.Int in {
def vaddq: Intrinsic<Vector, (args Vector:$a, Vector:$b), (add $a, $b)>;
def vhaddq: Intrinsic<Vector, (args Vector:$a, Vector:$b),
                              (IRInt<"vhadd", [Vector]> $a, $b, (unsignedflag Scalar))>;
def vrhaddq: Intrinsic<Vector, (args Vector:$a, Vector:$b),
                               (IRInt<"vrhadd", [Vector]> $a, $b, (unsignedflag Scalar))>;
def vandq: Intrinsic<Vector, (args Vector:$a, Vector:$b), (and $a, $b)>;
def vbicq: Intrinsic<Vector, (args Vector:$a, Vector:$b), (and $a, (not $b))>;
def veorq: Intrinsic<Vector, (args Vector:$a, Vector:$b), (xor $a, $b)>;
def vornq: Intrinsic<Vector, (args Vector:$a, Vector:$b), (or $a, (not $b))>;
def vorrq: Intrinsic<Vector, (args Vector:$a, Vector:$b), (or $a, $b)>;
def vsubq: Intrinsic<Vector, (args Vector:$a, Vector:$b), (sub $a, $b)>;
def vhsubq: Intrinsic<Vector, (args Vector:$a, Vector:$b),
                              (IRInt<"vhsub", [Vector]> $a, $b, (unsignedflag Scalar))>;
def vmulq: Intrinsic<Vector, (args Vector:$a, Vector:$b), (mul $a, $b)>;
def vmulhq: Intrinsic<Vector, (args Vector:$a, Vector:$b),
                              (IRInt<"vmulh", [Vector]> $a, $b, (unsignedflag Scalar))>;
def vrmulhq: Intrinsic<Vector, (args Vector:$a, Vector:$b),
                               (IRInt<"vrmulh", [Vector]> $a, $b, (unsignedflag Scalar))>;
def vmullbq_int: Intrinsic<DblVector, (args Vector:$a, Vector:$b),
                                      (IRInt<"vmull", [DblVector, Vector]>
                                       $a, $b, (unsignedflag Scalar), 0)>;
def vmulltq_int: Intrinsic<DblVector, (args Vector:$a, Vector:$b),
                                      (IRInt<"vmull", [DblVector, Vector]>
                                       $a, $b, (unsignedflag Scalar), 1)>;
let pnt = PNT_NType in {
  def vaddq_n: Intrinsic<Vector, (args Vector:$a, unpromoted<Scalar>:$b),
                                 (add $a, (splat $b))>;
  def vsubq_n: Intrinsic<Vector, (args Vector:$a, unpromoted<Scalar>:$b),
                                 (sub $a, (splat $b))>;
  def vmulq_n: Intrinsic<Vector, (args Vector:$a, unpromoted<Scalar>:$b),
                                 (mul $a, (splat $b))>;
  def vhaddq_n: Intrinsic<Vector, (args Vector:$a, unpromoted<Scalar>:$b),
                                  (IRInt<"vhadd", [Vector]> $a, (splat $b),
                                  (unsignedflag Scalar))>;
  def vhsubq_n: Intrinsic<Vector, (args Vector:$a, unpromoted<Scalar>:$b),
                                  (IRInt<"vhsub", [Vector]> $a, (splat $b),
                                  (unsignedflag Scalar))>;
}
}
let params = T.Signed in {
def vqdmulhq: Intrinsic<Vector, (args Vector:$a, Vector:$b),
                                (IRInt<"vqdmulh", [Vector]> $a, $b)>;
def vqrdmulhq: Intrinsic<Vector, (args Vector:$a, Vector:$b),
                                 (IRInt<"vqrdmulh", [Vector]> $a, $b)>;
let pnt = PNT_NType in {
  def vqdmulhq_n: Intrinsic<Vector, (args Vector:$a, unpromoted<Scalar>:$b),
                                    (IRInt<"vqdmulh", [Vector]> $a, (splat $b))>;
  def vqrdmulhq_n: Intrinsic<Vector, (args Vector:$a, unpromoted<Scalar>:$b),
                                    (IRInt<"vqrdmulh", [Vector]> $a, (splat $b))>;
}
}

let params = T.Poly, overrideKindLetter = "p" in {
def vmullbq_poly: Intrinsic<DblVector, (args Vector:$a, Vector:$b),
                                       (IRInt<"vmull_poly", [DblVector, Vector]>
                                        $a, $b, 0)>;
def vmulltq_poly: Intrinsic<DblVector, (args Vector:$a, Vector:$b),
                                       (IRInt<"vmull_poly", [DblVector, Vector]>
                                        $a, $b, 1)>;
}

let params = T.Float in {
def vaddqf: Intrinsic<Vector, (args Vector:$a, Vector:$b), (fadd $a, $b)>,
            NameOverride<"vaddq">;
defm vandqf: bit_op_fp<and>, NameOverride<"vandq">;
defm vbicqf: bit_op_fp_with_inv<and>, NameOverride<"vbicq">;
defm veorqf: bit_op_fp<xor>, NameOverride<"veorq">;
defm vornqf: bit_op_fp_with_inv<or>, NameOverride<"vornq">;
defm vorrqf: bit_op_fp<or>, NameOverride<"vorrq">;
def vsubqf: Intrinsic<Vector, (args Vector:$a, Vector:$b), (fsub $a, $b)>,
            NameOverride<"vsubq">;
def vmulqf: Intrinsic<Vector, (args Vector:$a, Vector:$b), (fmul $a, $b)>,
            NameOverride<"vmulq">;

let pnt = PNT_NType in {
  def vaddqf_n: Intrinsic<Vector, (args Vector:$a, unpromoted<Scalar>:$b),
                                  (fadd $a, (splat $b))>,
                NameOverride<"vaddq_n">;
  def vsubqf_n: Intrinsic<Vector, (args Vector:$a, unpromoted<Scalar>:$b),
                                  (fsub $a, (splat $b))>,
                NameOverride<"vsubq_n">;
  def vmulqf_n: Intrinsic<Vector, (args Vector:$a, unpromoted<Scalar>:$b),
                                  (fmul $a, (splat $b))>,
                NameOverride<"vmulq_n">;
}
}

multiclass FMA<bit add> {
  // FMS instructions are defined in the ArmARM as if they negate the
  // second multiply input.
  defvar m2_cg = !if(add, (id $m2), (fneg $m2));

  defvar unpred_cg = (IRIntBase<"fma", [Vector]> $m1, m2_cg, $addend);
  defvar pred_cg   = (IRInt<"fma_predicated", [Vector, Predicate]>
                          $m1, m2_cg, $addend, $pred);

  def q: Intrinsic<Vector, (args Vector:$addend, Vector:$m1, Vector:$m2),
                   unpred_cg>;

  def q_m: Intrinsic<Vector, (args Vector:$addend, Vector:$m1, Vector:$m2,
                                   Predicate:$pred), pred_cg>;

  // Only FMA has the vector/scalar variants, not FMS
  if add then let pnt = PNT_NType in {

    def q_n: Intrinsic<Vector, (args Vector:$addend, Vector:$m1,
                                     unpromoted<Scalar>:$m2_s),
                     (seq (splat $m2_s):$m2, unpred_cg)>;
    def sq_n: Intrinsic<Vector, (args Vector:$m1, Vector:$m2,
                                      unpromoted<Scalar>:$addend_s),
                        (seq (splat $addend_s):$addend, unpred_cg)>;
    def q_m_n: Intrinsic<Vector, (args Vector:$addend, Vector:$m1,
                                       unpromoted<Scalar>:$m2_s,
                                       Predicate:$pred),
                         (seq (splat $m2_s):$m2, pred_cg)>;
    def sq_m_n: Intrinsic<Vector, (args Vector:$m1, Vector:$m2,
                                        unpromoted<Scalar>:$addend_s,
                                        Predicate:$pred),
                          (seq (splat $addend_s):$addend, pred_cg)>;
  }
}

let params = T.Float in {
  defm vfma: FMA<1>;
  defm vfms: FMA<0>;
}

let params = T.Int, pnt = PNT_NType in {
  def vmlaq_n: Intrinsic<
    Vector, (args Vector:$addend, Vector:$m1, unpromoted<Scalar>:$m2_s),
    (add (mul $m1, (splat $m2_s)), $addend)>;
  def vmlasq_n: Intrinsic<
    Vector, (args Vector:$m1, Vector:$m2, unpromoted<Scalar>:$addend_s),
    (add (mul $m1, $m2), (splat $addend_s))>;

  def vmlaq_m_n: Intrinsic<
    Vector, (args Vector:$addend, Vector:$m1, Scalar:$m2_s, Predicate:$pred),
    (IRInt<"vmla_n_predicated", [Vector, Predicate]>
        $addend, $m1, $m2_s, $pred)>;
  def vmlasq_m_n: Intrinsic<
    Vector, (args Vector:$m1, Vector:$m2, Scalar:$addend_s, Predicate:$pred),
    (IRInt<"vmlas_n_predicated", [Vector, Predicate]>
        $m1, $m2, $addend_s, $pred)>;
}

multiclass VQDMLA {
  def hq_n: Intrinsic<
    Vector, (args Vector:$addend, Vector:$m1, Scalar:$m2_s),
    (IRInt<NAME # "h", [Vector]> $addend, $m1, $m2_s)>;
  def shq_n: Intrinsic<
    Vector, (args Vector:$m1, Vector:$m2, Scalar:$addend_s),
    (IRInt<NAME # "sh", [Vector]> $m1, $m2, $addend_s)>;

  def hq_m_n: Intrinsic<
    Vector, (args Vector:$addend, Vector:$m1, Scalar:$m2_s, Predicate:$pred),
    (IRInt<NAME # "h_predicated", [Vector, Predicate]>
         $addend, $m1, $m2_s, $pred)>;
  def shq_m_n: Intrinsic<
    Vector, (args Vector:$m1, Vector:$m2, Scalar:$addend_s, Predicate:$pred),
    (IRInt<NAME # "sh_predicated", [Vector, Predicate]>
         $m1, $m2, $addend_s, $pred)>;
}

let params = T.Signed, pnt = PNT_NType in {
  defm vqdmla: VQDMLA;
  defm vqrdmla: VQDMLA;
}

multiclass VQDMLAD<int exchange, int round, int subtract> {
  def "": Intrinsic<Vector, (args Vector:$a, Vector:$b, Vector:$c),
       (IRInt<"vqdmlad", [Vector]> $a, $b, $c,
            (u32 exchange), (u32 round), (u32 subtract))>;
  def _m: Intrinsic<Vector, (args Vector:$a, Vector:$b, Vector:$c,
                                  Predicate:$pred),
       (IRInt<"vqdmlad_predicated", [Vector, Predicate]> $a, $b, $c,
            (u32 exchange), (u32 round), (u32 subtract), $pred)>;
}
let params = T.Signed in {
  defm vqdmladhq:   VQDMLAD<0, 0, 0>;
  defm vqdmladhxq:  VQDMLAD<1, 0, 0>;
  defm vqdmlsdhq:   VQDMLAD<0, 0, 1>;
  defm vqdmlsdhxq:  VQDMLAD<1, 0, 1>;
  defm vqrdmladhq:  VQDMLAD<0, 1, 0>;
  defm vqrdmladhxq: VQDMLAD<1, 1, 0>;
  defm vqrdmlsdhq:  VQDMLAD<0, 1, 1>;
  defm vqrdmlsdhxq: VQDMLAD<1, 1, 1>;
}

let params = !listconcat(T.Int16, T.Int32) in {
  let pnt = PNT_None in {
    def vmvnq_n: Intrinsic<Vector, (args imm_simd_vmvn:$imm),
                           (not (splat (Scalar $imm)))>;
  }
  defm vmvnq: IntrinsicMX<Vector, (args imm_simd_vmvn:$imm, Predicate:$pred),
                     (select $pred, (not (splat (Scalar $imm))), $inactive),
                     1, "_n", PNT_NType, PNT_None>;
  let pnt = PNT_NType in {
    def vbicq_n: Intrinsic<Vector, (args Vector:$v, imm_simd_restrictive:$imm),
                           (and $v, (not (splat (Scalar $imm))))>;
    def vorrq_n: Intrinsic<Vector, (args Vector:$v, imm_simd_restrictive:$imm),
                           (or $v, (splat (Scalar $imm)))>;
  }
  def vbicq_m_n: Intrinsic<
    Vector, (args Vector:$v, imm_simd_restrictive:$imm, Predicate:$pred),
    (select $pred, (and $v, (not (splat (Scalar $imm)))), $v)>;
  def vorrq_m_n: Intrinsic<
    Vector, (args Vector:$v, imm_simd_restrictive:$imm, Predicate:$pred),
    (select $pred, (or $v, (splat (Scalar $imm))), $v)>;
}

let params = T.Usual in {
  let pnt = PNT_None in
    def vdupq_n: Intrinsic<Vector, (args unpromoted<Scalar>:$s), (splat $s)>;

  defm vdupq: IntrinsicMX<
      Vector, (args unpromoted<Scalar>:$s, Predicate:$pred),
      (select $pred, (splat $s), $inactive), 1, "_n", PNT_NType, PNT_None>;
}

multiclass vxdup_mc<dag paramsIn, dag paramsOut> {
  defvar UnpredInt     = IRInt<NAME, [Vector]>;
  defvar PredInt       = IRInt<NAME # "_predicated", [Vector, Predicate]>;
  defvar UnpredIntCall = !con((UnpredInt $base), paramsOut);
  defvar PredIntCall   = !con((PredInt $inactive, $base), paramsOut, (? $pred));

  // Straightforward case with neither writeback nor predication
  let pnt = PNT_N in
    def q_n: Intrinsic<Vector, !con((args u32:$base), paramsIn),
                       (xval UnpredIntCall, 0)>;

  // Predicated form without writeback
  defm q: IntrinsicMX<
      Vector, !con((args u32:$base), paramsIn, (? Predicate:$pred)),
      (xval PredIntCall, 0), 1, "_n", PNT_NType, PNT_N>;

  // Writeback without predication
  let pnt = PNT_WB in
    def q_wb: Intrinsic<
      Vector, !con((args Ptr<u32>:$baseaddr), paramsIn),
      (seq (load $baseaddr):$base,
           UnpredIntCall:$pair,
           (store (xval $pair, 1), $baseaddr),
           (xval $pair, 0))>;

  // Both writeback and predicated
  defm q: IntrinsicMX<
      Vector, !con((args Ptr<u32>:$baseaddr), paramsIn, (? Predicate:$pred)),
      (seq (load $baseaddr):$base,
           PredIntCall:$pair,
           (store (xval $pair, 1), $baseaddr),
           (xval $pair, 0)), 1, "_wb", PNT_WBType, PNT_WB>;
}

let params = T.Unsigned in {
  defm vidup:  vxdup_mc<(?             imm_1248:$step), (?         $step)>;
  defm vddup:  vxdup_mc<(?             imm_1248:$step), (?         $step)>;
  defm viwdup: vxdup_mc<(? u32:$limit, imm_1248:$step), (? $limit, $step)>;
  defm vdwdup: vxdup_mc<(? u32:$limit, imm_1248:$step), (? $limit, $step)>;
}

let params = T.Int in {
  def vmvnq: Intrinsic<Vector, (args Vector:$a),
                       (xor $a, (uint_max Vector))>;
  defm vmvnq: IntrinsicMX<Vector, (args Vector:$a, Predicate:$pred),
    (IRInt<"mvn_predicated", [Vector, Predicate]> $a, $pred, $inactive)>;
  def vclzq: Intrinsic<Vector, (args Vector:$a),
                       (IRIntBase<"ctlz", [Vector]> $a, (i1 0))>;
  defm vclzq: IntrinsicMX<Vector, (args Vector:$a, Predicate:$pred),
    (IRInt<"clz_predicated", [Vector, Predicate]> $a, $pred, $inactive)>;
}
let params = T.Signed in {
  def vclsq: Intrinsic<Vector, (args Vector:$a), (IRInt<"vcls", [Vector]> $a)>;
  defm vclsq: IntrinsicMX<Vector, (args Vector:$a, Predicate:$pred),
    (IRInt<"cls_predicated", [Vector, Predicate]> $a, $pred, $inactive)>;

  def vnegq: Intrinsic<Vector, (args Vector:$a),
                       (sub (zeroinit Vector), $a)>;
  def vabsq: Intrinsic<Vector, (args Vector:$a),
                       (select (icmp_slt $a, (zeroinit Vector)),
                               (sub (zeroinit Vector), $a), $a)>;
  def vqnegq: Intrinsic<Vector, (args Vector:$a),
                        (select (icmp_eq $a, (int_min Vector)),
                                (int_max Vector),
                                (sub (zeroinit Vector), $a))>;
  def vqabsq: Intrinsic<Vector, (args Vector:$a),
                        (select (icmp_sgt $a, (zeroinit Vector)), $a,
                                (select (icmp_eq $a, (int_min Vector)),
                                        (int_max Vector),
                                        (sub (zeroinit Vector), $a)))>;

  foreach name = ["qneg", "qabs"] in {
    defm v#name#q: IntrinsicMX<Vector, (args Vector:$a, Predicate:$pred),
      (IRInt<name#"_predicated", [Vector, Predicate]> $a, $pred, $inactive),
      0 /* no _x variant for saturating intrinsics */>;
  }
}
let params = !listconcat(T.Signed, T.Float) in {
  foreach name = ["neg", "abs"] in {
    defm v#name#q: IntrinsicMX<Vector, (args Vector:$a, Predicate:$pred),
      (IRInt<name#"_predicated", [Vector, Predicate]> $a, $pred, $inactive)>;
  }
}
let params = T.Float in {
  def vnegq_f: Intrinsic<Vector, (args Vector:$a), (fneg $a)>,
               NameOverride<"vnegq">;
  def vabsq_f: Intrinsic<Vector, (args Vector:$a),
               (IRIntBase<"fabs", [Vector]> $a)>, NameOverride<"vabsq">;
}

// The bitcasting below is not overcomplicating the IR because while
// Vector and UVector may be different vector types at the C level i.e.
// vectors of same size signed/unsigned ints. Once they're lowered
// to IR, they are just bit vectors with no sign at all, so the
// bitcasts will be automatically elided by IRBuilder.
multiclass predicated_bit_op_fp<string int_op> {
def "": Intrinsic<Vector, (args Vector:$inactive, Vector:$a, Vector:$b,
                                Predicate:$pred),
    (bitcast (IRInt<int_op, [UVector, Predicate]>
                    (bitcast $a, UVector),
                    (bitcast $b, UVector),
                    $pred,
                    (bitcast $inactive, UVector)), Vector)>;
}

// Plain intrinsics
let params = T.Usual in {
def vabdq: Intrinsic<Vector, (args Vector:$a, Vector:$b),
                     (IRInt<"vabd", [Vector]> $a, $b, (unsignedflag Scalar))>;
}

multiclass VectorVectorArithmetic<string operation, dag extraArgs = (?),
                                  bit wantXVariant = 1> {
  defm "" : IntrinsicMX<
      Vector, (args Vector:$a, Vector:$b, Predicate:$pred),
      !con((IRInt<operation, [Vector, Predicate]> $a, $b),
           extraArgs, (? $pred, $inactive)), wantXVariant>;
}

multiclass VectorScalarArithmetic<string operation, string basename,
                                  dag extraArgs = (?),
                                  bit wantXVariant = 1> {
  defm "" : IntrinsicMXNameOverride<
      Vector, (args Vector:$a, unpromoted<Scalar>:$b, Predicate:$pred),
      !con((IRInt<operation, [Vector, Predicate]> $a, (splat $b)),
           extraArgs, (? $pred, $inactive)), basename, wantXVariant, "_n",
           PNT_NType, PNT_NType>;
}

multiclass VectorVectorArithmeticBitcast<string operation> {
  defm "" : IntrinsicMX<Vector, (args Vector:$a, Vector:$b,
                                 Predicate:$pred),
                                (bitcast (IRInt<operation, [UVector, Predicate]>
                                          (bitcast $a, UVector),
                                          (bitcast $b, UVector),
                                          $pred,
                                          (bitcast $inactive, UVector)), Vector)>;
}

// Predicated intrinsics
let params = T.Usual in {
  defm vabdq : VectorVectorArithmetic<"abd_predicated", (? (unsignedflag Scalar))>;
  defm vaddq : VectorVectorArithmetic<"add_predicated">;
  defm vsubq : VectorVectorArithmetic<"sub_predicated">;
  defm vmulq : VectorVectorArithmetic<"mul_predicated">;
  defm vandq : VectorVectorArithmeticBitcast<"and_predicated">;
  defm vbicq : VectorVectorArithmeticBitcast<"bic_predicated">;
  defm veorq : VectorVectorArithmeticBitcast<"eor_predicated">;
  defm vornq : VectorVectorArithmeticBitcast<"orn_predicated">;
  defm vorrq : VectorVectorArithmeticBitcast<"orr_predicated">;

  defm : VectorScalarArithmetic<"add_predicated", "vaddq">;
  defm : VectorScalarArithmetic<"sub_predicated", "vsubq">;
  defm : VectorScalarArithmetic<"mul_predicated", "vmulq">;
}

multiclass DblVectorVectorArithmetic<string operation, dag extraArgs = (?),
                                     bit wantXVariant = 1> {
  defm "" : IntrinsicMX<
      DblVector, (args Vector:$a, Vector:$b, DblPredicate:$pred),
      !con((IRInt<operation, [DblVector, Vector, DblPredicate]> $a, $b),
           extraArgs, (? $pred, $inactive)), wantXVariant>;
}

multiclass DblVectorScalarArithmetic<string operation, string basename,
                                     dag extraArgs = (?),
                                     bit wantXVariant = 1> {
  defm "" : IntrinsicMXNameOverride<
      DblVector, (args Vector:$a, unpromoted<Scalar>:$b, DblPredicate:$pred),
      !con((IRInt<operation, [DblVector, Vector, DblPredicate]> $a, (splat $b)),
           extraArgs, (? $pred, $inactive)), basename, wantXVariant, "_n",
           PNT_NType, PNT_NType>;
}

// Predicated intrinsics - Int types only
let params = T.Int in {
  defm vminq : VectorVectorArithmetic<"min_predicated", (? (unsignedflag Scalar))>;
  defm vmaxq : VectorVectorArithmetic<"max_predicated", (? (unsignedflag Scalar))>;
  defm vmulhq : VectorVectorArithmetic<"mulh_predicated", (? (unsignedflag Scalar))>;
  defm vrmulhq : VectorVectorArithmetic<"rmulh_predicated", (? (unsignedflag Scalar))>;
  defm vqaddq : VectorVectorArithmetic<"qadd_predicated", (? (unsignedflag Scalar)), 0>;
  defm vhaddq : VectorVectorArithmetic<"hadd_predicated", (? (unsignedflag Scalar))>;
  defm vrhaddq : VectorVectorArithmetic<"rhadd_predicated", (? (unsignedflag Scalar))>;
  defm vqsubq : VectorVectorArithmetic<"qsub_predicated", (? (unsignedflag Scalar)), 0>;
  defm vhsubq : VectorVectorArithmetic<"hsub_predicated", (? (unsignedflag Scalar))>;
  defm vmullbq_int : DblVectorVectorArithmetic<"mull_int_predicated", (? (unsignedflag Scalar), (u32 0))>;
  defm vmulltq_int : DblVectorVectorArithmetic<"mull_int_predicated", (? (unsignedflag Scalar), (u32 1))>;

  defm : VectorScalarArithmetic<"qadd_predicated", "vqaddq", (? (unsignedflag Scalar)), 0>;
  defm : VectorScalarArithmetic<"hadd_predicated", "vhaddq", (? (unsignedflag Scalar))>;
  defm : VectorScalarArithmetic<"qsub_predicated", "vqsubq", (? (unsignedflag Scalar)), 0>;
  defm : VectorScalarArithmetic<"hsub_predicated", "vhsubq", (? (unsignedflag Scalar))>;
}
let params = T.Signed in {
  defm vqdmulhq : VectorVectorArithmetic<"qdmulh_predicated", (?), 0>;
  defm vqrdmulhq : VectorVectorArithmetic<"qrdmulh_predicated", (?), 0>;
  def vminaq_m: Intrinsic<UVector, (args UVector:$a, Vector:$b, Predicate:$pred),
                                   (IRInt<"vmina_predicated", [UVector,Predicate]> $a, $b, $pred)>;
  def vmaxaq_m: Intrinsic<UVector, (args UVector:$a, Vector:$b, Predicate:$pred),
                                   (IRInt<"vmaxa_predicated", [UVector,Predicate]> $a, $b, $pred)>;

  defm : VectorScalarArithmetic<"qdmulh_predicated", "vqdmulhq", (?), 0>;
  defm : VectorScalarArithmetic<"qrdmulh_predicated", "vqrdmulhq", (?), 0>;
}

let params = T.Poly, overrideKindLetter = "p" in {
  defm vmullbq_poly : DblVectorVectorArithmetic<"mull_poly_predicated", (? (u32 0))>;
  defm vmulltq_poly : DblVectorVectorArithmetic<"mull_poly_predicated", (? (u32 1))>;
}

let params = [s16, s32] in {
  def  vqdmullbq:  Intrinsic<DblVector, (args Vector:$a, Vector:$b),
                             (IRInt<"vqdmull", [DblVector, Vector]> $a, $b, 0)>;
  def  vqdmulltq:  Intrinsic<DblVector, (args Vector:$a, Vector:$b),
                             (IRInt<"vqdmull", [DblVector, Vector]> $a, $b, 1)>;
  defm vqdmullbq:  DblVectorVectorArithmetic<"vqdmull_predicated", (? (u32 0)), 0>;
  defm vqdmulltq:  DblVectorVectorArithmetic<"vqdmull_predicated", (? (u32 1)), 0>;

  let pnt = PNT_NType in {
    def vqdmullbq_n: Intrinsic<DblVector, (args Vector:$a, unpromoted<Scalar>:$b),
                               (IRInt<"vqdmull", [DblVector, Vector]>
                                $a, (splat $b), 0)>;
    def vqdmulltq_n: Intrinsic<DblVector, (args Vector:$a, unpromoted<Scalar>:$b),
                               (IRInt<"vqdmull", [DblVector, Vector]>
                                $a, (splat $b), 1)>;
  }
  defm vqdmullbq_n: DblVectorScalarArithmetic<"vqdmull_predicated",
                                              "vqdmullbq", (? (u32 0)), 0>;
  defm vqdmulltq_n: DblVectorScalarArithmetic<"vqdmull_predicated",
                                              "vqdmulltq", (? (u32 1)), 0>;
}

// Predicated intrinsics - Float types only
let params = T.Float in {
  defm vminnmq : VectorVectorArithmetic<"min_predicated", (? (u32 0))>;
  defm vmaxnmq : VectorVectorArithmetic<"max_predicated", (? (u32 0))>;
  def vminnmaq_m: Intrinsic<Vector, (args Vector:$a, Vector:$b, Predicate:$pred),
                                    (IRInt<"vminnma_predicated", [Vector,Predicate]> $a, $b, $pred)>;
  def vmaxnmaq_m: Intrinsic<Vector, (args Vector:$a, Vector:$b, Predicate:$pred),
                                    (IRInt<"vmaxnma_predicated", [Vector,Predicate]> $a, $b, $pred)>;
}

multiclass Reduction<Type Accumulator, string basename, list<Type> basetypes,
                     bit needSign = 0,
                     dag postCG = (seq (id $ret)),
                     dag accArg = (args Accumulator:$prev),
                     dag preCG = (seq)> {
  defvar intArgsBase   = (? $prev, $vec);
  defvar intArgsUnpred = !con(intArgsBase,
                              !if(needSign, (? (unsignedflag Scalar)), (?)));
  defvar intArgsPred   = !con(intArgsUnpred, (? $pred));
  defvar intUnpred     = !setdagop(intArgsUnpred, IRInt<basename, basetypes>);
  defvar intPred       = !setdagop(intArgsPred, IRInt<
            basename#"_predicated", !listconcat(basetypes, [Predicate])>);

  def "": Intrinsic<
    Accumulator, !con(accArg, (args Vector:$vec)),
    !con(preCG, (seq intUnpred:$ret), postCG)>;
  def _p: Intrinsic<
    Accumulator, !con(accArg, (args Vector:$vec, Predicate:$pred)),
    !con(preCG, (seq intPred:$ret), postCG)>;
}

let params = T.Int in {
defm vminvq: Reduction<Scalar, "minv", [Vector], 1, (seq (Scalar $ret))>;
defm vmaxvq: Reduction<Scalar, "maxv", [Vector], 1, (seq (Scalar $ret))>;
}

let params = T.Signed in {
defm vminavq: Reduction<UScalar, "minav", [Vector], 0, (seq (UScalar $ret))>;
defm vmaxavq: Reduction<UScalar, "maxav", [Vector], 0, (seq (UScalar $ret))>;
}

let params = T.Float in {
defm vminnmvq: Reduction<Scalar, "minnmv", [Scalar, Vector]>;
defm vmaxnmvq: Reduction<Scalar, "maxnmv", [Scalar, Vector]>;
defm vminnmavq: Reduction<Scalar, "minnmav", [Scalar, Vector]>;
defm vmaxnmavq: Reduction<Scalar, "maxnmav", [Scalar, Vector]>;
}

foreach half = [ "b", "t" ] in {
  defvar halfconst = !ne(half, "b");

  let params = [f32], pnt = PNT_None in {
    def vcvt#half#q_f16: Intrinsic<
      VecOf<f16>, (args VecOf<f16>:$inactive, Vector:$a),
      (IRInt<"vcvt_narrow"> $inactive, $a, halfconst)>;
    def vcvt#half#q_m_f16: Intrinsic<
      VecOf<f16>, (args VecOf<f16>:$inactive, Vector:$a, PredOf<f32>:$pred),
      (IRInt<"vcvt_narrow_predicated"> $inactive, $a, halfconst, $pred)>;
  } // params = [f32], pnt = PNT_None

  let params = [f16], pnt = PNT_None in {
    def vcvt#half#q_f32: Intrinsic<VecOf<f32>, (args Vector:$a),
      (IRInt<"vcvt_widen"> $a, halfconst)>;
    defm vcvt#half#q: IntrinsicMX<
      VecOf<f32>, (args Vector:$a, PredOf<f32>:$pred),
      (IRInt<"vcvt_widen_predicated"> $inactive, $a, halfconst, $pred),
      1, "_f32">;
  } // params = [f16], pnt = PNT_None
} // loop over half = "b", "t"

multiclass float_int_conversions<Type FScalar, Type IScalar, IRBuilderBase ftoi, IRBuilderBase itof> {
  defvar FVector = VecOf<FScalar>;
  defvar IVector = VecOf<IScalar>;

  let params = [IScalar] in {
    let pnt = PNT_2Type in {
      def : Intrinsic<FVector, (args IVector:$a), (itof $a, FVector)>,
            NameOverride<"vcvtq_" # FScalar>;
    }
    defm vcvtq: IntrinsicMX<FVector, (args IVector:$a, Predicate:$pred),
        (IRInt<"vcvt_fp_int_predicated", [FVector, IVector, Predicate]>
            $a, (unsignedflag IScalar), $pred, $inactive),
        1, "_" # FScalar, PNT_2Type, PNT_2Type>;
  }
  let params = [FScalar] in {
    let pnt = PNT_None in {
      def : Intrinsic<IVector, (args FVector:$a), (ftoi $a, IVector)>,
            NameOverride<"vcvtq_" # IScalar>;

      foreach suffix = ["a","n","p","m"] in
        def : Intrinsic<IVector, (args FVector:$a),
                        (IRInt<"vcvt"#suffix, [IVector, FVector]>
                            (unsignedflag IScalar), $a)>,
              NameOverride<"vcvt"#suffix#"q_" # IScalar>;
    }
    defm vcvtq: IntrinsicMX<IVector, (args FVector:$a, Predicate:$pred),
        (IRInt<"vcvt_fp_int_predicated", [IVector, FVector, Predicate]>
            $a, (unsignedflag IScalar), $pred, $inactive),
        1, "_" # IScalar, PNT_2Type, PNT_None>;

    foreach suffix = ["a","n","p","m"] in {
      defm "vcvt"#suffix#"q" : IntrinsicMX<
          IVector, (args FVector:$a, Predicate:$pred),
          (IRInt<"vcvt"#suffix#"_predicated", [IVector, FVector, Predicate]>
              (unsignedflag IScalar), $inactive, $a, $pred),
          1, "_" # IScalar, PNT_2Type, PNT_None>;
    }
  }
}

defm "" : float_int_conversions<f32, u32, fptoui, uitofp>;
defm "" : float_int_conversions<f16, u16, fptoui, uitofp>;
defm "" : float_int_conversions<f32, s32, fptosi, sitofp>;
defm "" : float_int_conversions<f16, s16, fptosi, sitofp>;

multiclass vmovl<bit top> {
  let params = [s8, u8, s16, u16] in {
    def "": Intrinsic<DblVector, (args Vector:$a),
      (extend (unzip $a, top), DblVector, (unsignedflag Scalar))>;
    defm "": IntrinsicMX<DblVector, (args Vector:$a, DblPredicate:$pred),
        (IRInt<"vmovl_predicated", [DblVector, Vector, DblPredicate]>
         $a, (unsignedflag Scalar), top, $pred, $inactive)>;
  }
}

defm vmovlbq: vmovl<0>;
defm vmovltq: vmovl<1>;

multiclass vmovn<bit top, dag wide_result> {
  let params = [s16, u16, s32, u32] in {
    def "": Intrinsic<HalfVector, (args HalfVector:$inactive, Vector:$a),
      (trunc wide_result, HalfVector)>;
    def _m: Intrinsic<HalfVector, (args HalfVector:$inactive, Vector:$a,
                                        Predicate:$pred),
      (IRInt<"vmovn_predicated", [HalfVector, Vector, Predicate]>
          $inactive, $a, top, $pred)>;
  }
}

defm vmovntq: vmovn<1, (zip (vreinterpret $inactive, Vector), $a)>;
defm vmovnbq: vmovn<0,
   (zip $a, (vreinterpret (vrev $inactive, (bitsize Scalar)), Vector))>;

multiclass vqmovn<bit top, Type RetScalar> {
  defvar RetVector = VecOf<RetScalar>;

  let params = [s16, u16, s32, u32] in {
    def : Intrinsic<
      RetVector, (args RetVector:$inactive, Vector:$a),
      (IRInt<"vqmovn", [RetVector, Vector]>
          $inactive, $a, (unsignedflag RetScalar), (unsignedflag Scalar), top)>,
      NameOverride<NAME>;
    def: Intrinsic<
      RetVector, (args RetVector:$inactive, Vector:$a, Predicate:$pred),
      (IRInt<"vqmovn_predicated", [RetVector, Vector, Predicate]>
          $inactive, $a, (unsignedflag RetScalar), (unsignedflag Scalar),
          top, $pred)>,
      NameOverride<NAME # "_m">;
  }
}

let params = [s16, s32, u16, u32] in {
  defm vqmovntq: vqmovn<1, HalfScalar>;
  defm vqmovnbq: vqmovn<0, HalfScalar>;
}
let params = [s16, s32] in {
  defm vqmovuntq: vqmovn<1, UHalfScalar>;
  defm vqmovunbq: vqmovn<0, UHalfScalar>;
}

multiclass vrnd<IRIntBase ir_int, string suffix> {
  let params = T.Float in {
    def "": Intrinsic<Vector, (args Vector:$a), (ir_int $a)>;
    defm "": IntrinsicMX<Vector, (args Vector:$a, Predicate:$pred),
        (IRInt<"vrint"#suffix#"_predicated", [Vector, Predicate]>
            $a, $pred, $inactive)>;
  }
}

defm vrndq:  vrnd<IRIntBase<"trunc", [Vector]>, "z">;
defm vrndmq: vrnd<IRIntBase<"floor", [Vector]>, "m">;
defm vrndpq: vrnd<IRIntBase<"ceil",  [Vector]>, "p">;
defm vrndaq: vrnd<IRIntBase<"round", [Vector]>, "a">;
defm vrndxq: vrnd<IRIntBase<"rint",  [Vector]>, "x">;
defm vrndnq: vrnd<IRInt<"vrintn",    [Vector]>, "n">;

multiclass compare_with_pred<string condname, dag arguments,
                             dag cmp, string suffix> {
  // Make the predicated and unpredicated versions of a single comparison.
  def: Intrinsic<Predicate, arguments,
                 (u16 (IRInt<"pred_v2i", [Predicate]> cmp))>,
       NameOverride<"vcmp" # condname # "q" # suffix>;
  def: Intrinsic<Predicate, !con(arguments, (args Predicate:$inpred)),
                 (u16 (IRInt<"pred_v2i", [Predicate]> (and $inpred, cmp)))>,
       NameOverride<"vcmp" # condname # "q_m" # suffix>;
}

multiclass compare<string condname, IRBuilder cmpop> {
  // Make all four variants of a comparison: the vector/vector and
  // vector/scalar forms, each using compare_with_pred to make a
  // predicated and unpredicated version.
  defm: compare_with_pred<condname, (args Vector:$va, Vector:$vb),
                          (cmpop $va, $vb), "">;
  let pnt = PNT_NType in {
    defm: compare_with_pred<condname, (args Vector:$va, unpromoted<Scalar>:$sb),
                            (cmpop $va, (splat $sb)), "_n">;
  }
}
let params = T.Int in {
  defm: compare<"eq", icmp_eq>;
  defm: compare<"ne", icmp_ne>;
}
let params = T.Signed in {
  defm: compare<"gt", icmp_sgt>;
  defm: compare<"ge", icmp_sge>;
  defm: compare<"lt", icmp_slt>;
  defm: compare<"le", icmp_sle>;
}
let params = T.Unsigned in {
  defm: compare<"hi", icmp_ugt>;
  defm: compare<"cs", icmp_uge>;
}
let params = T.Float in {
  defm: compare<"eq", fcmp_eq>;
  defm: compare<"ne", fcmp_ne>;
  defm: compare<"gt", fcmp_gt>;
  defm: compare<"ge", fcmp_ge>;
  defm: compare<"lt", fcmp_lt>;
  defm: compare<"le", fcmp_le>;
}

let params = T.Signed in {
  def vminq: Intrinsic<Vector, (args Vector:$a, Vector:$b),
                               (select (icmp_sle $a, $b), $a, $b)>;
  def vmaxq: Intrinsic<Vector, (args Vector:$a, Vector:$b),
                               (select (icmp_sge $a, $b), $a, $b)>;
  def vminaq: Intrinsic<UVector, (args UVector:$a, Vector:$b),
                                 (seq (select (icmp_slt $b, (zeroinit Vector)),
                                              (sub (zeroinit Vector), $b), $b):$absb,
                                      (select (icmp_ule $a, $absb), $a, $absb))>;
  def vmaxaq: Intrinsic<UVector, (args UVector:$a, Vector:$b),
                                 (seq (select (icmp_slt $b, (zeroinit Vector)),
                                              (sub (zeroinit Vector), $b), $b):$absb,
                                      (select (icmp_uge $a, $absb), $a, $absb))>;
}
let params = T.Unsigned in {
  def vminqu: Intrinsic<Vector, (args Vector:$a, Vector:$b),
                                (select (icmp_ule $a, $b), $a, $b)>,
              NameOverride<"vminq">;
  def vmaxqu: Intrinsic<Vector, (args Vector:$a, Vector:$b),
                                (select (icmp_uge $a, $b), $a, $b)>,
              NameOverride<"vmaxq">;
}
let params = T.Float in {
  def vminnmq: Intrinsic<Vector, (args Vector:$a, Vector:$b),
                                 (IRIntBase<"minnum", [Vector]> $a, $b)>;
  def vmaxnmq: Intrinsic<Vector, (args Vector:$a, Vector:$b),
                                 (IRIntBase<"maxnum", [Vector]> $a, $b)>;
  def vminnmaq: Intrinsic<Vector, (args Vector:$a, Vector:$b),
                                  (IRIntBase<"minnum", [Vector]>
                                   (IRIntBase<"fabs", [Vector]> $a),
                                   (IRIntBase<"fabs", [Vector]> $b))>;
  def vmaxnmaq: Intrinsic<Vector, (args Vector:$a, Vector:$b),
                                  (IRIntBase<"maxnum", [Vector]>
                                   (IRIntBase<"fabs", [Vector]> $a),
                                   (IRIntBase<"fabs", [Vector]> $b))>;
}

def vpselq: Intrinsic<Vector, (args Vector:$t, Vector:$f, Predicate:$pred),
                      (select $pred, $t, $f)> { let params = T.Usual; }
def vpselq_64: Intrinsic<
    Vector, (args Vector:$t, Vector:$f, PredOf<u32>:$pred),
            (bitcast (select $pred, (bitcast $t, VecOf<u32>),
                                    (bitcast $f, VecOf<u32>)), Vector)>,
    NameOverride<"vpselq"> { let params = T.All64; }

let params = [Void], pnt = PNT_None in {

  multiclass vctp<Type pred, string intname> {
    def "": Intrinsic<pred, (args u32:$val),
        (u16 (IRInt<"pred_v2i", [pred]> (IRIntBase<intname> $val)))>;
    def _m: Intrinsic<pred, (args u32:$val, pred:$inpred),
        (u16 (IRInt<"pred_v2i", [pred]> (and $inpred,
                                         (IRIntBase<intname> $val))))>;
  }
  defm vctp8q:  vctp<PredOf<u8>,  "arm_mve_vctp8">;
  defm vctp16q: vctp<PredOf<u16>, "arm_mve_vctp16">;
  defm vctp32q: vctp<PredOf<u32>, "arm_mve_vctp32">;
  defm vctp64q: vctp<PredOf<u64>, "arm_mve_vctp64">;

  def vpnot: Intrinsic<PredOf<u8>, (args unpromoted<PredOf<u8>>:$pred),
                       (xor $pred, (u16 65535))>;

}

multiclass contiguous_load<string mnemonic, PrimitiveType memtype,
                           list<Type> same_size, list<Type> wider> {
  // Intrinsics named with explicit memory and element sizes that match:
  // vldrbq_?8, vldrhq_?16, vldrwq_?32.
  let params = same_size, pnt = PNT_None in {
    def: Intrinsic<Vector, (args CPtr<CopyKind<same_size[0], Scalar>>:$addr),
                   (load (address (CPtr<Vector> $addr), !srl(memtype.size,3)))>,
         NameOverride<mnemonic>;
    def: Intrinsic<Vector, (args CPtr<CopyKind<same_size[0], Scalar>>:$addr,
                                 Predicate:$pred),
                   (IRIntBase<"masked_load", [Vector, CPtr<Vector>]>
                        (CPtr<Vector> $addr), !srl(memtype.size,3),
                        $pred, (zeroinit Vector))>,
         NameOverride<mnemonic # "_z">;
  }

  // Synonyms for the above, with the generic name vld1q that just means
  // 'memory and element sizes match', and allows convenient polymorphism with
  // the memory and element types covariant.
  let params = same_size in {
    def: Intrinsic<Vector, (args CPtr<CopyKind<same_size[0], Scalar>>:$addr),
                   (load (address (CPtr<Vector> $addr), !srl(memtype.size,3)))>,
         NameOverride<"vld1q">;
    def: Intrinsic<Vector, (args CPtr<CopyKind<same_size[0], Scalar>>:$addr,
                                 Predicate:$pred),
                   (IRIntBase<"masked_load", [Vector, CPtr<Vector>]>
                        (CPtr<Vector> $addr), !srl(memtype.size,3),
                        $pred, (zeroinit Vector))>,
         NameOverride<"vld1q_z">;
  }

  // Intrinsics with the memory size narrower than the vector element, so that
  // they load less than 128 bits of memory and sign/zero extend each loaded
  // value into a wider vector lane.
  let params = wider, pnt = PNT_None in {
    def: Intrinsic<Vector, (args CPtr<CopyKind<same_size[0], Scalar>>:$addr),
                   (extend (load (address (CPtr<NarrowedVecOf<memtype,Vector>>
                                           $addr), !srl(memtype.size,3))),
                           Vector, (unsignedflag Scalar))>,
         NameOverride<mnemonic>;
    def: Intrinsic<Vector, (args CPtr<CopyKind<same_size[0], Scalar>>:$addr,
                                 Predicate:$pred),
                   (extend (IRIntBase<"masked_load",
                                      [NarrowedVecOf<memtype,Vector>,
                                      CPtr<NarrowedVecOf<memtype,Vector>>]>
                                (CPtr<NarrowedVecOf<memtype,Vector>> $addr),
                                !srl(memtype.size,3), $pred,
                                (zeroinit NarrowedVecOf<memtype,Vector>)),
                           Vector, (unsignedflag Scalar))>,
         NameOverride<mnemonic # "_z">;
  }
}

defm: contiguous_load<"vldrbq", u8, T.All8, !listconcat(T.Int16, T.Int32)>;
defm: contiguous_load<"vldrhq", u16, T.All16, T.Int32>;
defm: contiguous_load<"vldrwq", u32, T.All32, []>;

multiclass contiguous_store<string mnemonic, PrimitiveType memtype,
                           list<Type> same_size, list<Type> wider> {
  // Intrinsics named with explicit memory and element sizes that match:
  // vstrbq_?8, vstrhq_?16, vstrwq_?32.
  let params = same_size in {
    def: Intrinsic<Void, (args Ptr<CopyKind<same_size[0], Scalar>>:$addr,
                               Vector:$value),
                   (store $value,
                          (address (Ptr<Vector> $addr), !srl(memtype.size,3)))>,
         NameOverride<mnemonic>;
    def: Intrinsic<Void, (args Ptr<CopyKind<same_size[0], Scalar>>:$addr,
                               Vector:$value, Predicate:$pred),
                   (IRIntBase<"masked_store", [Vector, Ptr<Vector>]>
                        $value, (Ptr<Vector> $addr),
                        !srl(memtype.size,3), $pred)>,
         NameOverride<mnemonic # "_p">;
  }

  // Synonyms for the above, with the generic name vst1q that just means
  // 'memory and element sizes match', and allows convenient polymorphism with
  // the memory and element types covariant.
  let params = same_size in {
    def: Intrinsic<Void, (args Ptr<CopyKind<same_size[0], Scalar>>:$addr,
                               Vector:$value),
                   (store $value,
                          (address (Ptr<Vector> $addr), !srl(memtype.size,3)))>,
         NameOverride<"vst1q">;
    def: Intrinsic<Void, (args Ptr<CopyKind<same_size[0], Scalar>>:$addr,
                               Vector:$value, Predicate:$pred),
                   (IRIntBase<"masked_store", [Vector, Ptr<Vector>]>
                        $value, (Ptr<Vector> $addr),
                        !srl(memtype.size,3), $pred)>,
         NameOverride<"vst1q_p">;
  }

  // Intrinsics with the memory size narrower than the vector element, so that
  // they store less than 128 bits of memory, truncating each vector lane into
  // a narrower value to store.
  let params = wider in {
    def: Intrinsic<Void, (args Ptr<CopyKind<same_size[0], Scalar>>:$addr,
                               Vector:$value),
                   (store (trunc $value, NarrowedVecOf<memtype,Vector>),
                          (address (Ptr<NarrowedVecOf<memtype,Vector>> $addr),
                                   !srl(memtype.size,3)))>,
         NameOverride<mnemonic>;
    def: Intrinsic<Void, (args Ptr<CopyKind<same_size[0], Scalar>>:$addr,
                               Vector:$value, Predicate:$pred),
                   (IRIntBase<"masked_store",
                              [NarrowedVecOf<memtype,Vector>,
                               Ptr<NarrowedVecOf<memtype,Vector>>]>
                        (trunc $value, NarrowedVecOf<memtype,Vector>),
                        (Ptr<NarrowedVecOf<memtype,Vector>> $addr),
                        !srl(memtype.size,3), $pred)>,
         NameOverride<mnemonic # "_p">;
  }
}

defm: contiguous_store<"vstrbq", u8, T.All8, !listconcat(T.Int16, T.Int32)>;
defm: contiguous_store<"vstrhq", u16, T.All16, T.Int32>;
defm: contiguous_store<"vstrwq", u32, T.All32, []>;

multiclass gather_base<list<Type> types, int size> {
  let params = types, pnt = PNT_None in {
    def _gather_base: Intrinsic<
      Vector, (args UVector:$addr, imm_mem7bit<size>:$offset),
      (IRInt<"vldr_gather_base", [Vector, UVector]> $addr, $offset)>;

    def _gather_base_z: Intrinsic<
      Vector, (args UVector:$addr, imm_mem7bit<size>:$offset, Predicate:$pred),
      (IRInt<"vldr_gather_base_predicated", [Vector, UVector, Predicate]>
          $addr, $offset, $pred)>;

    def _gather_base_wb: Intrinsic<
      Vector, (args Ptr<UVector>:$addr, imm_mem7bit<size>:$offset),
      (seq (IRInt<"vldr_gather_base_wb", [Vector, UVector]>
               (load $addr), $offset):$pair,
           (store (xval $pair, 1), $addr),
           (xval $pair, 0))>;

    def _gather_base_wb_z: Intrinsic<
      Vector, (args Ptr<UVector>:$addr, imm_mem7bit<size>:$offset,
                    Predicate:$pred),
      (seq (IRInt<"vldr_gather_base_wb_predicated",
                  [Vector, UVector, Predicate]>
               (load $addr), $offset, $pred):$pair,
           (store (xval $pair, 1), $addr),
           (xval $pair, 0))>;
  }
}

defm vldrwq: gather_base<T.All32, 4>;
defm vldrdq: gather_base<T.All64, 8>;

multiclass scatter_base<list<Type> types, int size> {
  let params = types in {
    def _scatter_base: Intrinsic<
      Void, (args UVector:$addr, imm_mem7bit<size>:$offset, Vector:$data),
      (IRInt<"vstr_scatter_base", [UVector, Vector]> $addr, $offset, $data)>;

    def _scatter_base_p: Intrinsic<
      Void, (args UVector:$addr, imm_mem7bit<size>:$offset, Vector:$data,
                  Predicate:$pred),
      (IRInt<"vstr_scatter_base_predicated", [UVector, Vector, Predicate]>
          $addr, $offset, $data, $pred)>;

    def _scatter_base_wb: Intrinsic<
      Void, (args Ptr<UVector>:$addr, imm_mem7bit<size>:$offset, Vector:$data),
      (seq (IRInt<"vstr_scatter_base_wb", [UVector, Vector]>
                 (load $addr), $offset, $data):$wbaddr,
           (store $wbaddr, $addr))>;

    def _scatter_base_wb_p: Intrinsic<
      Void, (args Ptr<UVector>:$addr, imm_mem7bit<size>:$offset,
                    Vector:$data, Predicate:$pred),
      (seq (IRInt<"vstr_scatter_base_wb_predicated",
                  [UVector, Vector, Predicate]>
               (load $addr), $offset, $data, $pred):$wbaddr,
           (store $wbaddr, $addr))>;
  }
}

defm vstrwq: scatter_base<T.All32, 4>;
defm vstrdq: scatter_base<T.All64, 8>;

multiclass gather_offset_unshifted<list<Type> types, PrimitiveType memtype> {
  let params = types in {
    def _gather_offset: Intrinsic<
      Vector, (args CPtr<CopyKind<memtype, Scalar>>:$base, UVector:$offsets),
      (IRInt<"vldr_gather_offset",
             [Vector, CPtr<CopyKind<memtype, Scalar>>, UVector]>
          $base, $offsets, memtype.size, 0, (unsignedflag Scalar))>;
    def _gather_offset_z: Intrinsic<
      Vector, (args CPtr<CopyKind<memtype, Scalar>>:$base, UVector:$offsets,
                    Predicate:$pred),
      (IRInt<"vldr_gather_offset_predicated",
             [Vector, CPtr<CopyKind<memtype, Scalar>>, UVector, Predicate]>
          $base, $offsets, memtype.size, 0, (unsignedflag Scalar), $pred)>;
  }
}

multiclass gather_offset_shifted<list<Type> types, PrimitiveType memtype,
                                 int shift> {
  let params = types in {
    def _gather_shifted_offset: Intrinsic<
      Vector, (args CPtr<CopyKind<memtype, Scalar>>:$base, UVector:$offsets),
      (IRInt<"vldr_gather_offset",
             [Vector, CPtr<CopyKind<memtype, Scalar>>, UVector]>
          $base, $offsets, memtype.size, shift, (unsignedflag Scalar))>;
    def _gather_shifted_offset_z: Intrinsic<
      Vector, (args CPtr<CopyKind<memtype, Scalar>>:$base, UVector:$offsets,
                    Predicate:$pred),
      (IRInt<"vldr_gather_offset_predicated",
             [Vector, CPtr<CopyKind<memtype, Scalar>>, UVector, Predicate]>
          $base, $offsets, memtype.size, shift, (unsignedflag Scalar), $pred)>;
  }
}

multiclass gather_offset_both<list<Type> types, PrimitiveType memtype,
                              int shift> {
  defm "": gather_offset_unshifted<types, memtype>;
  defm "": gather_offset_shifted<types, memtype, shift>;
}

defm vldrbq: gather_offset_unshifted<!listconcat(T.All8, T.Int16, T.Int32), u8>;
defm vldrhq: gather_offset_both<!listconcat(T.All16, T.Int32), u16, 1>;
defm vldrwq: gather_offset_both<T.All32, u32, 2>;
defm vldrdq: gather_offset_both<T.Int64, u64, 3>;

multiclass scatter_offset_unshifted<list<Type> types, PrimitiveType memtype> {
  let params = types in {
    def _scatter_offset: Intrinsic<
      Void, (args Ptr<CopyKind<memtype, Scalar>>:$base, UVector:$offsets,
                  Vector:$data),
      (IRInt<"vstr_scatter_offset",
             [Ptr<CopyKind<memtype, Scalar>>, UVector, Vector]>
          $base, $offsets, $data, memtype.size, 0)>;
    def _scatter_offset_p: Intrinsic<
      Void, (args Ptr<CopyKind<memtype, Scalar>>:$base, UVector:$offsets,
                  Vector:$data, Predicate:$pred),
      (IRInt<"vstr_scatter_offset_predicated",
             [Ptr<CopyKind<memtype, Scalar>>, UVector, Vector, Predicate]>
          $base, $offsets, $data, memtype.size, 0, $pred)>;
  }
}

multiclass scatter_offset_shifted<list<Type> types, PrimitiveType memtype,
                                  int shift> {
  let params = types in {
    def _scatter_shifted_offset: Intrinsic<
      Void, (args Ptr<CopyKind<memtype, Scalar>>:$base, UVector:$offsets,
                  Vector:$data),
      (IRInt<"vstr_scatter_offset",
             [Ptr<CopyKind<memtype, Scalar>>, UVector, Vector]>
          $base, $offsets, $data, memtype.size, shift)>;
    def _scatter_shifted_offset_p: Intrinsic<
      Void, (args Ptr<CopyKind<memtype, Scalar>>:$base, UVector:$offsets,
                  Vector:$data, Predicate:$pred),
      (IRInt<"vstr_scatter_offset_predicated",
             [Ptr<CopyKind<memtype, Scalar>>, UVector, Vector, Predicate]>
          $base, $offsets, $data, memtype.size, shift, $pred)>;
  }
}

multiclass scatter_offset_both<list<Type> types, PrimitiveType memtype,
                               int shift> {
  defm "": scatter_offset_unshifted<types, memtype>;
  defm "": scatter_offset_shifted<types, memtype, shift>;
}

defm vstrbq: scatter_offset_unshifted<!listconcat(T.All8,T.Int16,T.Int32), u8>;
defm vstrhq: scatter_offset_both<!listconcat(T.All16, T.Int32), u16, 1>;
defm vstrwq: scatter_offset_both<T.All32, u32, 2>;
defm vstrdq: scatter_offset_both<T.Int64, u64, 3>;

let params = T.Int in {
  def vshlq_n: Intrinsic<Vector, (args Vector:$v, imm_0toNm1:$sh),
                         (shl $v, (splat (Scalar $sh)))>;
  defm vshlq: IntrinsicMX<Vector, (args Vector:$v, imm_0toNm1:$sh,
                                        Predicate:$pred),
      (IRInt<"shl_imm_predicated", [Vector, Predicate]>
           $v, $sh, $pred, $inactive), 1, "_n">;

  let pnt = PNT_NType in {
    def vshrq_n: Intrinsic<Vector, (args Vector:$v, imm_1toN:$sh),
                            (immshr $v, $sh, (unsignedflag Scalar))>;
    defm vshrq: IntrinsicMX<Vector, (args Vector:$v, imm_1toN:$sh,
                                          Predicate:$pred),
        (IRInt<"shr_imm_predicated", [Vector, Predicate]>
             $v, $sh, (unsignedflag Scalar), $pred, $inactive), 1, "_n">;
  }
}

let params = T.Int in {
  def vqshlq_n: Intrinsic<Vector, (args Vector:$v, imm_0toNm1:$sh),
       (IRInt<"vqshl_imm", [Vector]> $v, $sh, (unsignedflag Scalar))>;
  def vqshlq_m_n: Intrinsic<Vector, (args Vector:$inactive, Vector:$v,
                                          imm_0toNm1:$sh, Predicate:$pred),
       (IRInt<"vqshl_imm_predicated", [Vector, Predicate]>
            $v, $sh, (unsignedflag Scalar), $pred, $inactive)>;

  let pnt = PNT_NType in {
    def vrshrq_n: Intrinsic<Vector, (args Vector:$v, imm_1toN:$sh),
         (IRInt<"vrshr_imm", [Vector]> $v, $sh, (unsignedflag Scalar))>;
    defm vrshrq: IntrinsicMX<Vector, (args Vector:$v, imm_1toN:$sh,
                                           Predicate:$pred),
         (IRInt<"vrshr_imm_predicated", [Vector, Predicate]>
              $v, $sh, (unsignedflag Scalar), $pred, $inactive), 1, "_n">;
  }
}

let params = T.Signed, pnt = PNT_NType in {
  def vqshluq_n: Intrinsic<UVector, (args Vector:$v, imm_0toNm1:$sh),
       (IRInt<"vqshlu_imm", [Vector]> $v, $sh)>;
  def vqshluq_m_n: Intrinsic<UVector, (args UVector:$inactive, Vector:$v,
                                            imm_0toNm1:$sh, Predicate:$pred),
       (IRInt<"vqshlu_imm_predicated", [Vector, Predicate]>
            $v, $sh, $pred, $inactive)>;
}

multiclass vshll_imm<int top> {
  let params = !listconcat(T.Int8, T.Int16), pnt = PNT_NType in {
    def _n: Intrinsic<DblVector, (args Vector:$v, imm_1toN:$sh),
        (IRInt<"vshll_imm", [DblVector, Vector]>
            $v, $sh, (unsignedflag Scalar), top)>;
    defm "": IntrinsicMX<DblVector, (args Vector:$v, imm_1toN:$sh,
                                          DblPredicate:$pred),
        (IRInt<"vshll_imm_predicated", [DblVector, Vector, DblPredicate]>
            $v, $sh, (unsignedflag Scalar), top, $pred, $inactive), 1, "_n">;
  }
}
defm vshllbq : vshll_imm<0>;
defm vshlltq : vshll_imm<1>;

multiclass DyadicImmShift<Type outtype, Immediate imm, string intname = NAME,
                          dag extraargs = (?)> {
  defvar intparams = !if(!eq(outtype, Vector), [Vector], [outtype, Vector]);

  def q_n: Intrinsic<
      outtype, (args outtype:$a, Vector:$b, imm:$sh),
      !con((IRInt<intname, intparams> $a, $b, $sh), extraargs)>;

  def q_m_n: Intrinsic<
      outtype, (args outtype:$a, Vector:$b, imm:$sh, Predicate:$pred),
      !con((IRInt<intname # "_predicated", intparams # [Predicate]>
               $a, $b, $sh), extraargs, (? $pred))>;
}

multiclass VSHRN<Type outtype, Immediate imm, dag extraargs> {
  defm b: DyadicImmShift<outtype, imm, "vshrn", !con(extraargs, (? 0))>;
  defm t: DyadicImmShift<outtype, imm, "vshrn", !con(extraargs, (? 1))>;
}

let params = [s16, s32, u16, u32], pnt = PNT_NType in {
  defvar U = (unsignedflag Scalar);
  defm vshrn   : VSHRN<HalfVector, imm_1toHalfN, (? 0,0,U,U)>;
  defm vqshrn  : VSHRN<HalfVector, imm_1toHalfN, (? 1,0,U,U)>;
  defm vrshrn  : VSHRN<HalfVector, imm_1toHalfN, (? 0,1,U,U)>;
  defm vqrshrn : VSHRN<HalfVector, imm_1toHalfN, (? 1,1,U,U)>;
}
let params = [s16, s32], pnt = PNT_NType in {
  defm vqshrun  : VSHRN<UHalfVector, imm_1toHalfN, (? 1,0,1,0)>;
  defm vqrshrun : VSHRN<UHalfVector, imm_1toHalfN, (? 1,1,1,0)>;
}
let params = T.Int, pnt = PNT_NType in {
  defm vsli : DyadicImmShift<Vector, imm_0toNm1>;
  defm vsri : DyadicImmShift<Vector, imm_1toN>;
}

multiclass VSHL_non_imm<string scalarSuffix, int q, int r,
                        PolymorphicNameType pnt_scalar_unpred = PNT_Type> {
  let pnt = pnt_scalar_unpred in {
    def scalarSuffix: Intrinsic<
      Vector, (args Vector:$in, s32:$sh),
      (IRInt<"vshl_scalar", [Vector]> $in, $sh,
           q, r, (unsignedflag Scalar))>;
  }
  def "_m" # scalarSuffix: Intrinsic<
    Vector, (args Vector:$in, s32:$sh, Predicate:$pred),
    (IRInt<"vshl_scalar_predicated", [Vector, Predicate]> $in, $sh,
         q, r, (unsignedflag Scalar), $pred)>;

  def "": Intrinsic<
    Vector, (args Vector:$in, SVector:$sh),
    (IRInt<"vshl_vector", [Vector, SVector]> $in, $sh,
         q, r, (unsignedflag Scalar))>;
  defm "": IntrinsicMX<
    Vector, (args Vector:$in, SVector:$sh, Predicate:$pred),
    (IRInt<"vshl_vector_predicated", [Vector, SVector, Predicate]> $in, $sh,
         q, r, (unsignedflag Scalar), $pred, $inactive),
    // The saturating shift intrinsics don't have an x variant, so we
    // set wantXVariant to 1 iff q == 0
    !eq(q, 0)>;
}

let params = T.Int in {
  defm vshlq   : VSHL_non_imm<"_r", 0, 0>;
  defm vqshlq  : VSHL_non_imm<"_r", 1, 0>;
  defm vrshlq  : VSHL_non_imm<"_n", 0, 1, PNT_NType>;
  defm vqrshlq : VSHL_non_imm<"_n", 1, 1, PNT_NType>;
}

// Base class for the scalar shift intrinsics.
class ScalarShift<Type argtype, dag shiftCountArg, dag shiftCodeGen>:
  Intrinsic<argtype, !con((args argtype:$value), shiftCountArg), shiftCodeGen> {
  let params = [Void];
  let pnt = PNT_None;
}

// Subclass that includes the machinery to take a 64-bit input apart
// into halves, retrieve the two halves of a shifted output as a pair,
// and glue the pieces of the pair back into an i64 for output.
class LongScalarShift<Type argtype, dag shiftCountArg, dag shiftCodeGen>:
   ScalarShift<argtype, shiftCountArg,
               (seq (u32 (lshr $value, (argtype 32))):$hi,
                    (u32 $value):$lo,
                    shiftCodeGen:$pair,
                    (or (shl (u64 (xval $pair, 1)), (u64 32)),
                             (u64 (xval $pair, 0))))>;

// The family of saturating/rounding scalar shifts that take an
// immediate shift count. They come in matched 32- and 64-bit pairs.
multiclass ScalarSaturatingShiftImm<Type arg32, Type arg64> {
  def "": ScalarShift<arg32, (args imm_1to32:$sh),
                      (IRInt<NAME> $value, $sh)>;
  def l:  LongScalarShift<arg64, (args imm_1to32:$sh),
                          (IRInt<NAME # "l"> $lo, $hi, $sh)>;
}
defm uqshl: ScalarSaturatingShiftImm<u32, u64>;
defm urshr: ScalarSaturatingShiftImm<u32, u64>;
defm sqshl: ScalarSaturatingShiftImm<s32, s64>;
defm srshr: ScalarSaturatingShiftImm<s32, s64>;

// The family of saturating/rounding scalar shifts that take a
// register shift count. They also have 32- and 64-bit forms, but the
// 64-bit form also has a version that saturates to 48 bits, so the IR
// intrinsic takes an extra saturation-type operand.
multiclass ScalarSaturatingShiftReg<Type arg32, Type arg64> {
  def "":          ScalarShift<arg32, (args s32:$sh),
                               (IRInt<NAME> $value, $sh)>;
  def l:       LongScalarShift<arg64, (args s32:$sh),
                               (IRInt<NAME # "l"> $lo, $hi, $sh, 64)>;
  def l_sat48: LongScalarShift<arg64, (args s32:$sh),
                               (IRInt<NAME # "l"> $lo, $hi, $sh, 48)>;
}
defm uqrshl: ScalarSaturatingShiftReg<u32, u64>;
defm sqrshr: ScalarSaturatingShiftReg<s32, s64>;

// The intrinsics for LSLL and ASRL come in 64-bit versions only, with
// no saturation count.
def lsll: LongScalarShift<u64, (args s32:$sh), (IRInt<"lsll"> $lo, $hi, $sh)>;
def asrl: LongScalarShift<s64, (args s32:$sh), (IRInt<"asrl"> $lo, $hi, $sh)>;

multiclass vadcsbc {
  def q: Intrinsic<Vector, (args Vector:$a, Vector:$b, Ptr<uint>:$carry),
      (seq (IRInt<NAME, [Vector]> $a, $b, (shl (load $carry), 29)):$pair,
           (store (and 1, (lshr (xval $pair, 1), 29)), $carry),
           (xval $pair, 0))>;
  def iq: Intrinsic<Vector, (args Vector:$a, Vector:$b, Ptr<uint>:$carry),
      (seq (IRInt<NAME, [Vector]> $a, $b, 0):$pair,
           (store (and 1, (lshr (xval $pair, 1), 29)), $carry),
           (xval $pair, 0))>;
  def q_m: Intrinsic<Vector, (args Vector:$inactive, Vector:$a, Vector:$b,
                                   Ptr<uint>:$carry, Predicate:$pred),
      (seq (IRInt<NAME # "_predicated", [Vector, Predicate]> $inactive, $a, $b,
               (shl (load $carry), 29), $pred):$pair,
           (store (and 1, (lshr (xval $pair, 1), 29)), $carry),
           (xval $pair, 0))>;
  def iq_m: Intrinsic<Vector, (args Vector:$inactive, Vector:$a, Vector:$b,
                                    Ptr<uint>:$carry, Predicate:$pred),
      (seq (IRInt<NAME # "_predicated", [Vector, Predicate]> $inactive, $a, $b,
               0, $pred):$pair,
           (store (and 1, (lshr (xval $pair, 1), 29)), $carry),
           (xval $pair, 0))>;
}
let params = T.Int32 in {
  defm vadc: vadcsbc;
  defm vsbc: vadcsbc;
}

let params = T.Int in {
  def vshlcq: Intrinsic<
    Vector, (args Vector:$v, Ptr<u32>:$ps, imm_1to32:$imm),
    (seq (load $ps):$s,
         (IRInt<"vshlc", [Vector]> $v, $s, $imm):$pair,
         (store (xval $pair, 0), $ps),
         (xval $pair, 1))>;
  def vshlcq_m: Intrinsic<
    Vector, (args Vector:$v, Ptr<u32>:$ps, imm_1to32:$imm, Predicate:$pred),
    (seq (load $ps):$s,
         (IRInt<"vshlc_predicated", [Vector, Predicate]>
              $v, $s, $imm, $pred):$pair,
         (store (xval $pair, 0), $ps),
         (xval $pair, 1))>;
}

multiclass VectorComplexAddPred<dag not_halving, dag angle> {
  def "" : Intrinsic<Vector, (args Vector:$a, Vector:$b),
     (IRInt<"vcaddq", [Vector]> not_halving, angle, $a, $b)>;
  defm "" : IntrinsicMX<Vector, (args Vector:$a, Vector:$b, Predicate:$pred),
     (IRInt<"vcaddq_predicated", [Vector, Predicate]>
       not_halving, angle, $inactive, $a, $b, $pred)>;
}

multiclass VectorComplexMulPred<dag angle> {
  def "" : Intrinsic<Vector, (args Vector:$a, Vector:$b),
    (IRInt<"vcmulq", [Vector]> angle, $a, $b)>;
  defm "" : IntrinsicMX<Vector, (args Vector:$a, Vector:$b, Predicate:$pred),
    (IRInt<"vcmulq_predicated", [Vector, Predicate]> angle, $inactive, $a, $b,
      $pred)>;
}

multiclass VectorComplexMLAPred<dag angle> {
  def "" : Intrinsic<Vector, (args Vector:$a, Vector:$b, Vector:$c),
    (IRInt<"vcmlaq", [Vector]> angle, $a, $b, $c)>;
  def _m : Intrinsic<Vector, (args Vector:$a, Vector:$b, Vector:$c,
                                   Predicate:$pred),
    (IRInt<"vcmlaq_predicated", [Vector, Predicate]> angle, $a, $b, $c, $pred)>;
}

multiclass VectorComplexAddAngle<dag not_halving> {
  defm _rot90 : VectorComplexAddPred<not_halving, (u32 0)>;
  defm _rot270 : VectorComplexAddPred<not_halving, (u32 1)>;
}

multiclass VectorComplexMulAngle {
  defm ""      : VectorComplexMulPred<(u32 0)>;
  defm _rot90  : VectorComplexMulPred<(u32 1)>;
  defm _rot180 : VectorComplexMulPred<(u32 2)>;
  defm _rot270 : VectorComplexMulPred<(u32 3)>;
}

multiclass VectorComplexMLAAngle {
  defm ""      : VectorComplexMLAPred<(u32 0)>;
  defm _rot90  : VectorComplexMLAPred<(u32 1)>;
  defm _rot180 : VectorComplexMLAPred<(u32 2)>;
  defm _rot270 : VectorComplexMLAPred<(u32 3)>;
}

let params = T.Usual in
defm vcaddq : VectorComplexAddAngle<(u32 1)>;

let params = T.Signed in
defm vhcaddq : VectorComplexAddAngle<(u32 0)>;

let params = T.Float in {
defm vcmulq : VectorComplexMulAngle;
defm vcmlaq : VectorComplexMLAAngle;
}

multiclass MVEBinaryVectorHoriz32<dag subtract, dag exchange, string xsuffix> {
  def xsuffix#"q"
    : Intrinsic<Scalar32, (args Vector:$a, Vector:$b),
                          (IRInt<"vmldava", [Vector]>
                           (unsignedflag Scalar), subtract, exchange,
                           (zeroinit Scalar32), $a, $b)>;
  def xsuffix#"q_p"
    : Intrinsic<Scalar32, (args Vector:$a, Vector:$b, Predicate:$pred),
                          (IRInt<"vmldava_predicated", [Vector, Predicate]>
                           (unsignedflag Scalar), subtract, exchange,
                           (zeroinit Scalar32), $a, $b, $pred)>;

  def "a"#xsuffix#"q"
    : Intrinsic<Scalar32, (args Scalar32:$a, Vector:$b, Vector:$c),
                          (IRInt<"vmldava", [Vector]>
                           (unsignedflag Scalar), subtract, exchange,
                           $a, $b, $c)>;
  def "a"#xsuffix#"q_p"
    : Intrinsic<Scalar32, (args Scalar32:$a, Vector:$b, Vector:$c,
                                Predicate:$pred),
                          (IRInt<"vmldava_predicated", [Vector, Predicate]>
                           (unsignedflag Scalar), subtract, exchange,
                           $a, $b, $c, $pred)>;
}

class IntrSplit64<Type resty, dag args, dag codegen>
  : Intrinsic<resty, args,
              (seq (u32 (lshr $a, (u64 32))):$hi,
                   (u32 $a):$lo,
                   codegen:$pair,
                   (or (shl (u64 (xval $pair, 1)), (u64 32)),
                            (u64 (xval $pair, 0))))>;

class IntrSplit64ZeroInit<Type resty, dag args, dag codegen>
  : Intrinsic<resty, args,
              (seq (zeroinit u32):$hi,
                   (zeroinit u32):$lo,
                   codegen:$pair,
                   (or (shl (u64 (xval $pair, 1)), (u64 32)),
                            (u64 (xval $pair, 0))))>;

multiclass MVEBinaryVectorHoriz64Base<dag subtract, dag exchange,
                                      string xsuffix, string irname> {
  def xsuffix#"q"
    : IntrSplit64ZeroInit<Scalar64, (args Vector:$a, Vector:$b),
                          (IRInt<irname, [Vector]>
                           (unsignedflag Scalar), subtract, exchange,
                           $lo, $hi, $a, $b)>;
  def xsuffix#"q_p"
    : IntrSplit64ZeroInit<Scalar64, (args Vector:$a, Vector:$b,
                                          Predicate:$pred),
                          (IRInt<irname#"_predicated", [Vector, Predicate]>
                           (unsignedflag Scalar), subtract, exchange,
                           $lo, $hi, $a, $b, $pred)>;

  def "a"#xsuffix#"q"
    : IntrSplit64<Scalar64, (args Scalar64:$a, Vector:$b, Vector:$c),
                          (IRInt<irname, [Vector]>
                           (unsignedflag Scalar), subtract, exchange,
                           $lo, $hi, $b, $c)>;
  def "a"#xsuffix#"q_p"
    : IntrSplit64<Scalar64, (args Scalar64:$a, Vector:$b, Vector:$c,
                                  Predicate:$pred),
                          (IRInt<irname#"_predicated", [Vector, Predicate]>
                           (unsignedflag Scalar), subtract, exchange,
                           $lo, $hi, $b, $c, $pred)>;
}

multiclass MVEBinaryVectorHoriz64<dag subtract, dag exchange, string xsuffix> {
  defm "" : MVEBinaryVectorHoriz64Base<subtract, exchange, xsuffix, "vmlldava">;
}

multiclass MVEBinaryVectorHoriz64R<dag subtract, dag exchange, string xsuffix> {
  defm "" : MVEBinaryVectorHoriz64Base<subtract, exchange, xsuffix,
                                       "vrmlldavha">;
}

multiclass VADDV<bit acc, bit pred, string intbase, Type Scalar> {
  defvar accArg = !if(acc, (args Scalar:$acc), (args));
  defvar predArg = !if(pred, (args Predicate:$pred), (args));
  defvar intrinsic = !if(pred,
      IRInt<intbase # "_predicated", [Vector, Predicate]>,
      IRInt<intbase, [Vector]>);
  defvar intCG = !con((intrinsic $v, (unsignedflag Scalar)),
                      !if(pred, (? $pred), (?)));
  defvar accCG = !if(acc, (add intCG, $acc), intCG);

  def "": Intrinsic<Scalar, !con(accArg, (args Vector:$v), predArg), accCG>;
}

let params = T.Int in {
defm vaddvq    : VADDV<0, 0, "addv", Scalar32>;
defm vaddvaq   : VADDV<1, 0, "addv", Scalar32>;
defm vaddvq_p  : VADDV<0, 1, "addv", Scalar32>;
defm vaddvaq_p : VADDV<1, 1, "addv", Scalar32>;
}

let params = [s32, u32] in {
defm vaddlvq    : VADDV<0, 0, "addlv", Scalar64>;
defm vaddlvaq   : VADDV<1, 0, "addlv", Scalar64>;
defm vaddlvq_p  : VADDV<0, 1, "addlv", Scalar64>;
defm vaddlvaq_p : VADDV<1, 1, "addlv", Scalar64>;
}

let params = T.Int in {
def vabavq : Intrinsic<u32, (args u32:$a, Vector:$b, Vector:$c),
    (IRInt<"vabav", [Vector]> (unsignedflag Scalar), $a, $b, $c)>;
def vabavq_p : Intrinsic<u32, (args u32:$a, Vector:$b, Vector:$c,
                                    Predicate:$pred),
    (IRInt<"vabav_predicated", [Vector, Predicate]>
                               (unsignedflag Scalar), $a, $b, $c, $pred)>;

defm vmladav  : MVEBinaryVectorHoriz32<V.False, V.False, "">;
}

let params = T.Signed in {
defm vmladav : MVEBinaryVectorHoriz32<V.False, V.True, "x">;
defm vmlsdav : MVEBinaryVectorHoriz32<V.True, V.False, "">;
defm vmlsdav : MVEBinaryVectorHoriz32<V.True, V.True, "x">;
}

let params = [u16, s16, u32, s32] in
defm vmlaldav : MVEBinaryVectorHoriz64<V.False, V.False, "">;

let params = [s16, s32] in {
defm vmlaldav : MVEBinaryVectorHoriz64<V.False, V.True, "x">;
defm vmlsldav : MVEBinaryVectorHoriz64<V.True, V.False, "">;
defm vmlsldav : MVEBinaryVectorHoriz64<V.True, V.True, "x">;
}

let params = T.Int32 in
defm vrmlaldavh : MVEBinaryVectorHoriz64R<V.False, V.False, "">;

let params = [s32] in {
defm vrmlaldavh : MVEBinaryVectorHoriz64R<V.False, V.True, "x">;
defm vrmlsldavh : MVEBinaryVectorHoriz64R<V.True, V.False, "">;
defm vrmlsldavh : MVEBinaryVectorHoriz64R<V.True, V.True, "x">;
}

multiclass vrev_predicated<int revsize> {
  defm "" : IntrinsicMX<Vector, (args Vector:$a, Predicate:$pred),
      (IRInt<"vrev_predicated", [Vector, Predicate]>
          $a, revsize, $pred, $inactive)>;
}

let params = T.All8 in {
  def vrev16q : Intrinsic<Vector, (args Vector:$a), (vrev $a, 16)>;
  defm vrev16q: vrev_predicated<16>;
}
let params = !listconcat(T.All8, T.All16) in {
  def vrev32q : Intrinsic<Vector, (args Vector:$a), (vrev $a, 32)>;
  defm vrev32q: vrev_predicated<32>;
}
let params = T.Usual in {
  def vrev64q : Intrinsic<Vector, (args Vector:$a), (vrev $a, 64)>;
  defm vrev64q: vrev_predicated<64>;
}

foreach desttype = T.All in {
  // We want a vreinterpretq between every pair of supported vector types
  // _except_ that there shouldn't be one from a type to itself.
  let params = !filter(srctype, T.All, !ne(srctype, desttype)) in {
    def "vreinterpretq_" # desttype: Intrinsic<
        VecOf<desttype>, (args Vector:$x), (vreinterpret $x, VecOf<desttype>)>;
  }
}

let params = T.All in {
  let pnt = PNT_None in {
    def vcreateq: Intrinsic<Vector, (args u64:$a, u64:$b),
        (vreinterpret (ielt_const (ielt_const (undef VecOf<u64>), $a, 0),
                             $b, 1), Vector)>;
    def vuninitializedq: Intrinsic<Vector, (args), (undef Vector)>;
  }

  // This is the polymorphic form of vuninitializedq, which takes no type
  // suffix, but takes an _unevaluated_ vector parameter and returns an
  // uninitialized vector of the same vector type.
  //
  // This intrinsic has no _non_-polymorphic form exposed to the user. But each
  // separately typed version of it still has to have its own clang builtin id,
  // which can't be called vuninitializedq_u32 or similar because that would
  // collide with the explicit nullary versions above. So I'm calling them
  // vuninitializedq_polymorphic_u32 (and so on) for builtin id purposes; that
  // full name never appears in the header file due to the polymorphicOnly
  // flag, and the _polymorphic suffix is omitted from the shortened name by
  // the custom PolymorphicNameType here.
  let polymorphicOnly = 1, nonEvaluating = 1,
      pnt = PolymorphicNameType<1, "polymorphic"> in {
    def vuninitializedq_polymorphic: Intrinsic<
        Vector, (args Vector), (undef Vector)>;
  }

  def vgetq_lane: Intrinsic<Scalar, (args Vector:$v, imm_lane:$lane),
                            (xelt_var $v, $lane)>;
  def vsetq_lane: Intrinsic<Vector, (args unpromoted<Scalar>:$e, Vector:$v, imm_lane:$lane),
                            (ielt_var $v, $e, $lane)>;
}

foreach desttype = !listconcat(T.Int16, T.Int32, T.Float) in {
  defvar is_dest_float = !eq(desttype.kind, "f");
  defvar is_dest_unsigned = !eq(desttype.kind, "u");
  // First immediate operand of the LLVM intrinsic
  defvar unsigned_flag = !cond(is_dest_float: (unsignedflag Scalar),
                               is_dest_unsigned: V.True,
                               true: V.False);
  // For float->int conversions _n and _x_n intrinsics are not polymorphic
  // because the signedness of the destination type cannot be inferred.
  defvar pnt_nx = !if(is_dest_float, PNT_2Type, PNT_None);

  let params = !if(is_dest_float,
                   !if(!eq(desttype.size, 16), T.Int16, T.Int32),
                   !if(!eq(desttype.size, 16), [f16], [f32])) in {
    let pnt = pnt_nx in
      def "vcvtq_n_"#desttype : Intrinsic<VecOf<desttype>,
        (args Vector:$a, imm_1toN:$b),
        (IRInt<"vcvt_fix", [VecOf<desttype>, Vector]> unsigned_flag, $a, $b)>;

    defm "vcvtq" : IntrinsicMX<VecOf<desttype>,
      (args Vector:$a, imm_1toN:$b, Predicate:$p),
      (IRInt<"vcvt_fix_predicated", [VecOf<desttype>, Vector, Predicate]>
             unsigned_flag, $inactive, $a, $b, $p),
      1, "_n_"#desttype, PNT_2Type, pnt_nx>;
  }
}

let params = T.Usual in {
let pnt = PNT_NType in
def vbrsrq_n: Intrinsic<Vector, (args Vector:$a, s32:$b),
                        (IRInt<"vbrsr", [Vector]> $a, $b)>;
defm vbrsrq : IntrinsicMX<Vector, (args Vector:$a, s32:$b, Predicate:$pred),
                         (IRInt<"vbrsr_predicated", [Vector, Predicate]>
                         $inactive, $a, $b, $pred), 1, "_n",
                         PNT_NType, PNT_NType>;
}
