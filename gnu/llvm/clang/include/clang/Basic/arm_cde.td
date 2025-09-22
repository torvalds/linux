//===--- arm_cde.td - ACLE intrinsic functions for CDE --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the set of ACLE-specified source-level intrinsic
// functions wrapping the CDE instructions.
//
//===----------------------------------------------------------------------===//

include "arm_mve_defs.td"

// f64 is not defined in arm_mve_defs.td because MVE instructions only work with
// f16 and f32
def f64: PrimitiveType<"f", 64>;

// Float<t> expects t to be a scalar type, and expands to the floating-point
// type of the same width.
class Float<Type t>: ComplexType<(CTO_CopyKind t, f32)>;
def FScalar: Float<Scalar>;

// ACLE CDE intrinsic
class CDEIntrinsic<Type ret, dag args, dag codegen>
  : Intrinsic<ret, args, codegen> {
  let builtinExtension = "cde";
}

// Immediate (in range [0, 2^numBits - 1])
class IB_ConstBits<int numBits> : IB_ConstRange<0, !add(!shl(1, numBits), -1)>;
// numBits-wide immediate of type u32
class CDEImmediateBits<int numBits> : Immediate<u32, IB_ConstBits<numBits>>;

// LLVM IR CDE intrinsic
class CDEIRInt<string name, list<Type> params = [], bit appendKind = 0>
      : IRIntBase<"arm_cde_" # name, params, appendKind>;

// Class for generating function macros in arm_cde.h:
// "#define <name>(<params>) <definition>"
class FunctionMacro<list<string> params_, string definition_> {
  list<string> params = params_;
  string definition = definition_;
}

// Coprocessor immediate
def imm_coproc : Immediate<sint, IB_ConstRange<0, 7>>;

// Immediate integer parameters
def imm_3b : CDEImmediateBits<3>;
def imm_4b : CDEImmediateBits<4>;
def imm_6b :  CDEImmediateBits<6>;
def imm_7b :  CDEImmediateBits<7>;
def imm_9b :  CDEImmediateBits<9>;
def imm_11b : CDEImmediateBits<11>;
def imm_12b : CDEImmediateBits<12>;
def imm_13b : CDEImmediateBits<13>;

// CX* instructions operating on GPRs
multiclass CDE_CX_m<dag argsImm, dag argsReg, dag cgArgs> {
  defvar cp = (args imm_coproc:$cp);
  let pnt = PNT_None, params = T.None in {
    def "" : CDEIntrinsic<u32, !con(cp, argsReg, argsImm),
                               !con((CDEIRInt<NAME> $cp), cgArgs, (? $imm))>;
    def a  : CDEIntrinsic<u32, !con(cp, (args u32:$acc), argsReg, argsImm),
                               !con((CDEIRInt<NAME # "a"> $cp, $acc),
                                    cgArgs, (? $imm))>;

    def d :
      CDEIntrinsic<u64, !con(cp, argsReg, argsImm),
            (seq !con((CDEIRInt<NAME # "d"> $cp), cgArgs, (? $imm)):$pair,
                 (or (shl (u64 (xval $pair, 1)), (u64 32)),
                          (u64 (xval $pair, 0))))>;
    def da :
      CDEIntrinsic<u64, !con(cp, (args u64:$acc), argsReg, argsImm),
            (seq (u32 (lshr $acc, (u64 32))):$acc_hi,
                 (u32 $acc):$acc_lo,
                 !con((CDEIRInt<NAME # "da"> $cp, $acc_lo, $acc_hi), cgArgs,
                       (? $imm)):$pair,
                 (or (shl (u64 (xval $pair, 1)), (u64 32)),
                          (u64 (xval $pair, 0))))>;
  }
}

defm cx1 : CDE_CX_m<(args imm_13b:$imm), (args), (?)>;
defm cx2 : CDE_CX_m<(args imm_9b:$imm), (args u32:$n), (? $n)>;
defm cx3 : CDE_CX_m<(args imm_6b:$imm), (args u32:$n, u32:$m), (? $n, $m)>;

// VCX* instructions operating on VFP registers
multiclass CDE_VCXFP_m<dag argsImm, dag argsReg32, dag argsReg64, dag cgArgs> {
  defvar cp = (args imm_coproc:$cp);
  let pnt = PNT_None, params = [u32] in {
    def "" : CDEIntrinsic<u32, !con(cp, argsReg32, argsImm),
          (bitcast !con((CDEIRInt<NAME, [f32]> $cp), cgArgs, (? $imm)),
                   Scalar)>;
    def a  : CDEIntrinsic<u32, !con(cp, (args u32:$acc), argsReg32, argsImm),
          (bitcast !con((CDEIRInt<NAME # "a", [f32]> $cp,
                         (bitcast $acc, FScalar)), cgArgs, (? $imm)), Scalar)>;
  }
  let pnt = PNT_None, params = [u64] in {
    def d  : CDEIntrinsic<u64, !con(cp, argsReg64, argsImm),
          (bitcast !con((CDEIRInt<NAME, [f64]> $cp), cgArgs, (? $imm)),
                   Scalar)>;
    def da : CDEIntrinsic<u64, !con(cp, (args u64:$acc), argsReg64, argsImm),
          (bitcast !con((CDEIRInt<NAME # "a", [f64]> $cp,
                         (bitcast $acc, FScalar)), cgArgs, (? $imm)), Scalar)>;
  }
}

defm vcx1: CDE_VCXFP_m<(args imm_11b:$imm), (args), (args), (?)>;
defm vcx2: CDE_VCXFP_m<(args imm_6b:$imm), (args u32:$n), (args u64:$n),
                       (? (bitcast $n, FScalar))>;
defm vcx3: CDE_VCXFP_m<(args imm_3b:$imm),
                       (args u32:$n, u32:$m), (args u64:$n, u64:$m),
                       (? (bitcast $n, FScalar), (bitcast $m, FScalar))>;

// VCX* instructions operating on Q vector registers

def v16u8 : VecOf<u8>;

let pnt = PNT_None, params = [u8] in
def vcx1q : CDEIntrinsic<Vector, (args imm_coproc:$cp, imm_12b:$imm),
                         (CDEIRInt<"vcx1q"> $cp, $imm)>;

let pnt = PNT_Type, params = T.All, polymorphicOnly = 1 in {
  def vcx1qa :
    CDEIntrinsic<Vector, (args imm_coproc:$cp, Vector:$acc, imm_12b:$imm),
            (bitcast (CDEIRInt<"vcx1qa"> $cp, (bitcast $acc, v16u8), $imm),
                     Vector)>;

  def vcx2q :
    CDEIntrinsic<Vector, (args imm_coproc:$cp, Vector:$n, imm_7b:$imm),
            (bitcast (CDEIRInt<"vcx2q"> $cp, (bitcast $n, VecOf<u8>), $imm),
                      Vector)>;
  def vcx2q_u8 :
    CDEIntrinsic<v16u8, (args imm_coproc:$cp, Vector:$n, imm_7b:$imm),
            (CDEIRInt<"vcx2q"> $cp, (bitcast $n, VecOf<u8>), $imm)>;

  def vcx2qa_impl :
    CDEIntrinsic<Vector,
            (args imm_coproc:$cp, Vector:$acc, v16u8:$n, imm_7b:$imm),
            (bitcast (CDEIRInt<"vcx2qa"> $cp, (bitcast $acc, v16u8), $n, $imm),
                     Vector)>;

  def vcx3q_impl :
    CDEIntrinsic<Vector,
            (args imm_coproc:$cp, Vector:$n, v16u8:$m, imm_4b:$imm),
            (bitcast (CDEIRInt<"vcx3q"> $cp, (bitcast $n, v16u8), $m, $imm),
                     Vector)>;
  def vcx3q_u8_impl :
    CDEIntrinsic<v16u8,
            (args imm_coproc:$cp, Vector:$n, v16u8:$m, imm_4b:$imm),
            (CDEIRInt<"vcx3q"> $cp, (bitcast $n, v16u8), $m, $imm)>;
  def vcx3qa_impl :
    CDEIntrinsic<Vector,
            (args imm_coproc:$cp, Vector:$acc, v16u8:$n, v16u8:$m, imm_4b:$imm),
            (bitcast (CDEIRInt<"vcx3qa"> $cp, (bitcast $acc, v16u8), $n, $m,
                                         $imm),
                     Vector)>;
}

// Reinterpret intrinsics required to implement __arm_vcx*q with 2 or 3
// polymorphic paramters.
let params = [/* no u8 */ s8, u16, s16, u32, s32, u64, s64, f16, f32],
    headerOnly = 1, polymorphicOnly = 1 in
def vreinterpretq_u8 :
    Intrinsic<v16u8, (args Vector:$x), (vreinterpret $x, v16u8)>;

// We need vreinterpretq_u8_u8 to avoid doing smart tricks in the macros
let params = [u8], polymorphicOnly = 1 in
def vreinterpretq_u8_cde :
    CDEIntrinsic<v16u8, (args Vector:$x), (id $x)>,
    NameOverride<"vreinterpretq_u8">;


def vcx2qa : FunctionMacro<
  ["cp", "acc", "n", "imm"],
  "__arm_vcx2qa_impl((cp), (acc), __arm_vreinterpretq_u8(n), (imm))">;

def vcx3q : FunctionMacro<
  ["cp", "n", "m", "imm"],
  "__arm_vcx3q_impl((cp), (n), __arm_vreinterpretq_u8(m), (imm))">;
def vcx3q_u8 : FunctionMacro<
  ["cp", "n", "m", "imm"],
  "__arm_vcx3q_u8_impl((cp), (n), __arm_vreinterpretq_u8(m), (imm))">;
def vcx3qa : FunctionMacro<
  ["cp", "acc", "n", "m", "imm"],
  "__arm_vcx3qa_impl((cp), (acc), __arm_vreinterpretq_u8(n), "
                     "__arm_vreinterpretq_u8(m), (imm))">;

class CDEIntrinsicMasked<string irname, dag argsReg, dag imm, dag cgArgs>
  : CDEIntrinsic<Vector,
      !con((args imm_coproc:$cp, Vector:$inactive_or_acc),
           argsReg, imm, (args Predicate:$pred)),
      !con((CDEIRInt<irname # "_predicated", [Vector,Predicate]>
            $cp, $inactive_or_acc), cgArgs, (? $imm, $pred))> {
  let params = T.All;
  let polymorphicOnly = 1;
}

def vcx1q_m : CDEIntrinsicMasked<"vcx1q", (args), (args imm_12b:$imm), (?)>;
def vcx1qa_m : CDEIntrinsicMasked<"vcx1qa", (args), (args imm_12b:$imm), (?)>;

multiclass VCXPredicated<dag argsReg, dag imm, dag cgArgs,
                         list<string> macroArgs, string macro> {
  def _m_impl : CDEIntrinsicMasked<NAME, argsReg, imm, cgArgs>;
  def a_m_impl : CDEIntrinsicMasked<NAME#"a", argsReg, imm, cgArgs>;

  def _m: FunctionMacro<
    !listconcat(["cp", "inactive"], macroArgs, ["imm", "pred"]),
    "__arm_"#NAME#"_m_impl((cp), (inactive), "#macro#" (imm), (pred))">;
  def a_m: FunctionMacro<
    !listconcat(["cp", "acc"], macroArgs, ["imm", "pred"]),
    "__arm_"#NAME#"a_m_impl((cp), (acc), "#macro#" (imm), (pred))">;
}

defm vcx2q :
  VCXPredicated<(args v16u8:$n), (args imm_7b:$imm), (? $n), ["n"],
                "__arm_vreinterpretq_u8(n),">;
defm vcx3q :
  VCXPredicated<(args v16u8:$n, v16u8:$m), (args imm_4b:$imm), (? $n, $m),
                ["n", "m"], "__arm_vreinterpretq_u8(n), "
                            "__arm_vreinterpretq_u8(m),">;

// vreinterpretq intrinsics required by the ACLE CDE specification

foreach desttype = [/* no u8 */ s8, u16, s16, u32, s32, u64, s64, f16, f32] in {
  let params = [u8], headerOnly = 1, pnt = PNT_None in
  def "vreinterpretq_" # desttype : Intrinsic<
    VecOf<desttype>, (args Vector:$x), (vreinterpret $x, VecOf<desttype>)>;
}
