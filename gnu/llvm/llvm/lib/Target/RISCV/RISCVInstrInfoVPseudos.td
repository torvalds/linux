//===-- RISCVInstrInfoVPseudos.td - RISC-V 'V' Pseudos -----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// This file contains the required infrastructure to support code generation
/// for the standard 'V' (Vector) extension, version 1.0.
///
/// This file is included from RISCVInstrInfoV.td
///
/// Overview of our vector instruction pseudos.  Many of the instructions
/// have behavior which depends on the value of VTYPE.  Several core aspects of
/// the compiler - e.g. register allocation - depend on fields in this
/// configuration register.  The details of which fields matter differ by the
/// specific instruction, but the common dimensions are:
///
/// LMUL/EMUL - Most instructions can write to differently sized register groups
/// depending on LMUL.
///
/// Masked vs Unmasked - Many instructions which allow a mask disallow register
/// overlap.  As a result, masked vs unmasked require different register
/// allocation constraints.
///
/// Policy - For each of mask and tail policy, there are three options:
/// * "Undisturbed" - As defined in the specification, required to preserve the
/// exact bit pattern of inactive lanes.
/// * "Agnostic" - As defined in the specification, required to either preserve
/// the exact bit pattern of inactive lanes, or produce the bit pattern -1 for
/// those lanes.  Note that each lane can make this choice independently.
/// Instructions which produce masks (and only those instructions) also have the
/// option of producing a result as-if VL had been VLMAX.
/// * "Undefined" - The bit pattern of the inactive lanes is unspecified, and
/// can be changed without impacting the semantics of the program.  Note that
/// this concept does not exist in the specification, and requires source
/// knowledge to be preserved.
///
/// SEW - Some instructions have semantics which depend on SEW.  This is
/// relatively rare, and mostly impacts scheduling and cost estimation.
///
/// We have two techniques we use to represent the impact of these fields:
/// * For fields which don't impact register classes, we largely use
/// dummy operands on the pseudo instructions which convey information
/// about the value of VTYPE.
/// * For fields which do impact register classes (and a few bits of
/// legacy - see policy discussion below), we define a family of pseudo
/// instructions for each actual instruction.  Said differently, we encode
/// each of the preceding fields which are relevant for a given instruction
/// in the opcode space.
///
/// Currently, the policy is represented via the following instrinsic families:
/// * _MASK - Can represent all three policy states for both tail and mask.  If
///   passthrough is IMPLICIT_DEF (or NoReg), then represents "undefined".
///   Otherwise, policy operand and tablegen flags drive the interpretation.
///   (If policy operand is not present - there are a couple, though we're
///   rapidly removing them - a non-undefined policy defaults to "tail
///   agnostic", and "mask undisturbed".  Since this is the only variant with
///   a mask, all other variants are "mask undefined".
/// * Unsuffixed w/ both passthrough and policy operand. Can represent all
///   three policy states.  If passthrough is IMPLICIT_DEF (or NoReg), then
///   represents "undefined".  Otherwise, policy operand and tablegen flags
///   drive the interpretation.
/// * Unsuffixed w/o passthrough or policy operand -- Does not have a
///   passthrough operand, and thus represents the "undefined" state.  Note
///   that terminology in code frequently refers to these as "TA" which is
///   confusing.  We're in the process of migrating away from this
///   representation.
///
//===----------------------------------------------------------------------===//

def riscv_vmv_x_s : SDNode<"RISCVISD::VMV_X_S",
                           SDTypeProfile<1, 1, [SDTCisInt<0>, SDTCisVec<1>,
                                                SDTCisInt<1>]>>;
def riscv_read_vlenb : SDNode<"RISCVISD::READ_VLENB",
                              SDTypeProfile<1, 0, [SDTCisVT<0, XLenVT>]>>;

// Operand that is allowed to be a register other than X0, a 5 bit unsigned
// immediate, or -1. -1 means VLMAX. This allows us to pick between VSETIVLI and
// VSETVLI opcodes using the same pseudo instructions.
def AVL : RegisterOperand<GPRNoX0> {
  let OperandNamespace = "RISCVOp";
  let OperandType = "OPERAND_AVL";
}

// X0 has special meaning for vsetvl/vsetvli.
//  rd | rs1 |   AVL value | Effect on vl
//--------------------------------------------------------------
// !X0 |  X0 |       VLMAX | Set vl to VLMAX
//  X0 |  X0 | Value in vl | Keep current vl, just change vtype.
def VLOp : ComplexPattern<XLenVT, 1, "selectVLOp">;

def DecImm : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(N->getSExtValue() - 1, SDLoc(N),
                                   N->getValueType(0));
}]>;

defvar TAIL_AGNOSTIC = 1;
defvar TU_MU = 0;
defvar TA_MA = 3;

//===----------------------------------------------------------------------===//
// Utilities.
//===----------------------------------------------------------------------===//

class PseudoToVInst<string PseudoInst> {
  defvar AffixSubsts = [["Pseudo", ""],
                        ["_E64", ""],
                        ["_E32", ""],
                        ["_E16", ""],
                        ["_E8", ""],
                        ["FPR64", "F"],
                        ["FPR32", "F"],
                        ["FPR16", "F"],
                        ["_TIED", ""],
                        ["_MASK", ""],
                        ["_B64", ""],
                        ["_B32", ""],
                        ["_B16", ""],
                        ["_B8", ""],
                        ["_B4", ""],
                        ["_B2", ""],
                        ["_B1", ""],
                        ["_MF8", ""],
                        ["_MF4", ""],
                        ["_MF2", ""],
                        ["_M1", ""],
                        ["_M2", ""],
                        ["_M4", ""],
                        ["_M8", ""],
                        ["_SE", ""],
                        ["_RM", ""]
                       ];
  string VInst = !foldl(PseudoInst, AffixSubsts, Acc, AffixSubst,
                        !subst(AffixSubst[0], AffixSubst[1], Acc));
}

// This class describes information associated to the LMUL.
class LMULInfo<int lmul, int oct, VReg regclass, VReg wregclass,
               VReg f2regclass, VReg f4regclass, VReg f8regclass, string mx> {
  bits<3> value = lmul; // This is encoded as the vlmul field of vtype.
  VReg vrclass = regclass;
  VReg wvrclass = wregclass;
  VReg f8vrclass = f8regclass;
  VReg f4vrclass = f4regclass;
  VReg f2vrclass = f2regclass;
  string MX = mx;
  int octuple = oct;
}

// Associate LMUL with tablegen records of register classes.
def V_M1  : LMULInfo<0b000,  8,   VR,        VRM2,   VR,   VR, VR, "M1">;
def V_M2  : LMULInfo<0b001, 16, VRM2,        VRM4,   VR,   VR, VR, "M2">;
def V_M4  : LMULInfo<0b010, 32, VRM4,        VRM8, VRM2,   VR, VR, "M4">;
def V_M8  : LMULInfo<0b011, 64, VRM8,/*NoVReg*/VR, VRM4, VRM2, VR, "M8">;

def V_MF8 : LMULInfo<0b101, 1, VR, VR,/*NoVReg*/VR,/*NoVReg*/VR,/*NoVReg*/VR, "MF8">;
def V_MF4 : LMULInfo<0b110, 2, VR, VR,          VR,/*NoVReg*/VR,/*NoVReg*/VR, "MF4">;
def V_MF2 : LMULInfo<0b111, 4, VR, VR,          VR,          VR,/*NoVReg*/VR, "MF2">;

// Used to iterate over all possible LMULs.
defvar MxList = [V_MF8, V_MF4, V_MF2, V_M1, V_M2, V_M4, V_M8];
// For floating point which don't need MF8.
defvar MxListF = [V_MF4, V_MF2, V_M1, V_M2, V_M4, V_M8];

// Used for widening and narrowing instructions as it doesn't contain M8.
defvar MxListW = [V_MF8, V_MF4, V_MF2, V_M1, V_M2, V_M4];
// Used for widening reductions. It can contain M8 because wider operands are
// scalar operands.
defvar MxListWRed = MxList;
// For floating point which don't need MF8.
defvar MxListFW = [V_MF4, V_MF2, V_M1, V_M2, V_M4];
// For widening floating-point Reduction as it doesn't contain MF8. It can
// contain M8 because wider operands are scalar operands.
defvar MxListFWRed = [V_MF4, V_MF2, V_M1, V_M2, V_M4, V_M8];

// Use for zext/sext.vf2
defvar MxListVF2 = [V_MF4, V_MF2, V_M1, V_M2, V_M4, V_M8];

// Use for zext/sext.vf4 and vector crypto instructions
defvar MxListVF4 = [V_MF2, V_M1, V_M2, V_M4, V_M8];

// Use for zext/sext.vf8
defvar MxListVF8 = [V_M1, V_M2, V_M4, V_M8];

class MxSet<int eew> {
  list<LMULInfo> m = !cond(!eq(eew, 8) : [V_MF8, V_MF4, V_MF2, V_M1, V_M2, V_M4, V_M8],
                           !eq(eew, 16) : [V_MF4, V_MF2, V_M1, V_M2, V_M4, V_M8],
                           !eq(eew, 32) : [V_MF2, V_M1, V_M2, V_M4, V_M8],
                           !eq(eew, 64) : [V_M1, V_M2, V_M4, V_M8]);
}

class FPR_Info<int sew> {
  RegisterClass fprclass = !cast<RegisterClass>("FPR" # sew);
  string FX = "FPR" # sew;
  int SEW = sew;
  list<LMULInfo> MxList = MxSet<sew>.m;
  list<LMULInfo> MxListFW = !if(!eq(sew, 64), [], !listremove(MxList, [V_M8]));
}

def SCALAR_F16 : FPR_Info<16>;
def SCALAR_F32 : FPR_Info<32>;
def SCALAR_F64 : FPR_Info<64>;

// BF16 uses the same register class as F16.
def SCALAR_BF16 : FPR_Info<16>;

defvar FPList = [SCALAR_F16, SCALAR_F32, SCALAR_F64];

// Used for widening instructions. It excludes F64.
defvar FPListW = [SCALAR_F16, SCALAR_F32];

// Used for widening bf16 instructions.
defvar BFPListW = [SCALAR_BF16];

class NFSet<LMULInfo m> {
  defvar lmul = !shl(1, m.value);
  list<int> L = NFList<lmul>.L;
}

class octuple_to_str<int octuple> {
  string ret = !cond(!eq(octuple, 1): "MF8",
                     !eq(octuple, 2): "MF4",
                     !eq(octuple, 4): "MF2",
                     !eq(octuple, 8): "M1",
                     !eq(octuple, 16): "M2",
                     !eq(octuple, 32): "M4",
                     !eq(octuple, 64): "M8");
}

def VLOpFrag : PatFrag<(ops), (XLenVT (VLOp (XLenVT AVL:$vl)))>;

// Output pattern for X0 used to represent VLMAX in the pseudo instructions.
// We can't use X0 register becuase the AVL operands use GPRNoX0.
// This must be kept in sync with RISCV::VLMaxSentinel.
def VLMax : OutPatFrag<(ops), (XLenVT -1)>;

def SelectFPImm : ComplexPattern<fAny, 1, "selectFPImm", [], [], 1>;

// List of EEW.
defvar EEWList = [8, 16, 32, 64];

class SegRegClass<LMULInfo m, int nf> {
  VReg RC = !cast<VReg>("VRN" # nf # !cond(!eq(m.value, V_MF8.value): V_M1.MX,
                                           !eq(m.value, V_MF4.value): V_M1.MX,
                                           !eq(m.value, V_MF2.value): V_M1.MX,
                                           true: m.MX));
}

//===----------------------------------------------------------------------===//
// Vector register and vector group type information.
//===----------------------------------------------------------------------===//

class VTypeInfo<ValueType Vec, ValueType Mas, int Sew, LMULInfo M,
                ValueType Scal = XLenVT, RegisterClass ScalarReg = GPR> {
  ValueType Vector = Vec;
  ValueType Mask = Mas;
  int SEW = Sew;
  int Log2SEW = !logtwo(Sew);
  VReg RegClass = M.vrclass;
  LMULInfo LMul = M;
  ValueType Scalar = Scal;
  RegisterClass ScalarRegClass = ScalarReg;
  // The pattern fragment which produces the AVL operand, representing the
  // "natural" vector length for this type. For scalable vectors this is VLMax.
  OutPatFrag AVL = VLMax;

  string ScalarSuffix = !cond(!eq(Scal, XLenVT) : "X",
                              !eq(Scal, f16) : "FPR16",
                              !eq(Scal, bf16) : "FPR16",
                              !eq(Scal, f32) : "FPR32",
                              !eq(Scal, f64) : "FPR64");
}

class GroupVTypeInfo<ValueType Vec, ValueType VecM1, ValueType Mas, int Sew,
                     LMULInfo M, ValueType Scal = XLenVT,
                     RegisterClass ScalarReg = GPR>
    : VTypeInfo<Vec, Mas, Sew, M, Scal, ScalarReg> {
  ValueType VectorM1 = VecM1;
}

defset list<VTypeInfo> AllVectors = {
  defset list<VTypeInfo> AllIntegerVectors = {
    defset list<VTypeInfo> NoGroupIntegerVectors = {
      defset list<VTypeInfo> FractionalGroupIntegerVectors = {
        def VI8MF8:  VTypeInfo<vint8mf8_t,  vbool64_t, 8,  V_MF8>;
        def VI8MF4:  VTypeInfo<vint8mf4_t,  vbool32_t, 8,  V_MF4>;
        def VI8MF2:  VTypeInfo<vint8mf2_t,  vbool16_t, 8,  V_MF2>;
        def VI16MF4: VTypeInfo<vint16mf4_t, vbool64_t, 16, V_MF4>;
        def VI16MF2: VTypeInfo<vint16mf2_t, vbool32_t, 16, V_MF2>;
        def VI32MF2: VTypeInfo<vint32mf2_t, vbool64_t, 32, V_MF2>;
      }
      def VI8M1:  VTypeInfo<vint8m1_t,  vbool8_t,   8, V_M1>;
      def VI16M1: VTypeInfo<vint16m1_t, vbool16_t, 16, V_M1>;
      def VI32M1: VTypeInfo<vint32m1_t, vbool32_t, 32, V_M1>;
      def VI64M1: VTypeInfo<vint64m1_t, vbool64_t, 64, V_M1>;
    }
    defset list<GroupVTypeInfo> GroupIntegerVectors = {
      def VI8M2: GroupVTypeInfo<vint8m2_t, vint8m1_t, vbool4_t, 8, V_M2>;
      def VI8M4: GroupVTypeInfo<vint8m4_t, vint8m1_t, vbool2_t, 8, V_M4>;
      def VI8M8: GroupVTypeInfo<vint8m8_t, vint8m1_t, vbool1_t, 8, V_M8>;

      def VI16M2: GroupVTypeInfo<vint16m2_t, vint16m1_t, vbool8_t, 16, V_M2>;
      def VI16M4: GroupVTypeInfo<vint16m4_t, vint16m1_t, vbool4_t, 16, V_M4>;
      def VI16M8: GroupVTypeInfo<vint16m8_t, vint16m1_t, vbool2_t, 16, V_M8>;

      def VI32M2: GroupVTypeInfo<vint32m2_t, vint32m1_t, vbool16_t, 32, V_M2>;
      def VI32M4: GroupVTypeInfo<vint32m4_t, vint32m1_t, vbool8_t,  32, V_M4>;
      def VI32M8: GroupVTypeInfo<vint32m8_t, vint32m1_t, vbool4_t,  32, V_M8>;

      def VI64M2: GroupVTypeInfo<vint64m2_t, vint64m1_t, vbool32_t, 64, V_M2>;
      def VI64M4: GroupVTypeInfo<vint64m4_t, vint64m1_t, vbool16_t, 64, V_M4>;
      def VI64M8: GroupVTypeInfo<vint64m8_t, vint64m1_t, vbool8_t,  64, V_M8>;
    }
  }

  defset list<VTypeInfo> AllFloatVectors = {
    defset list<VTypeInfo> NoGroupFloatVectors = {
      defset list<VTypeInfo> FractionalGroupFloatVectors = {
        def VF16MF4: VTypeInfo<vfloat16mf4_t, vbool64_t, 16, V_MF4, f16, FPR16>;
        def VF16MF2: VTypeInfo<vfloat16mf2_t, vbool32_t, 16, V_MF2, f16, FPR16>;
        def VF32MF2: VTypeInfo<vfloat32mf2_t, vbool64_t, 32, V_MF2, f32, FPR32>;
      }
      def VF16M1: VTypeInfo<vfloat16m1_t, vbool16_t, 16, V_M1, f16, FPR16>;
      def VF32M1: VTypeInfo<vfloat32m1_t, vbool32_t, 32, V_M1, f32, FPR32>;
      def VF64M1: VTypeInfo<vfloat64m1_t, vbool64_t, 64, V_M1, f64, FPR64>;
    }

    defset list<GroupVTypeInfo> GroupFloatVectors = {
      def VF16M2: GroupVTypeInfo<vfloat16m2_t, vfloat16m1_t, vbool8_t, 16,
                                 V_M2, f16, FPR16>;
      def VF16M4: GroupVTypeInfo<vfloat16m4_t, vfloat16m1_t, vbool4_t, 16,
                                 V_M4, f16, FPR16>;
      def VF16M8: GroupVTypeInfo<vfloat16m8_t, vfloat16m1_t, vbool2_t, 16,
                                 V_M8, f16, FPR16>;

      def VF32M2: GroupVTypeInfo<vfloat32m2_t, vfloat32m1_t, vbool16_t, 32,
                                 V_M2, f32, FPR32>;
      def VF32M4: GroupVTypeInfo<vfloat32m4_t, vfloat32m1_t, vbool8_t,  32,
                                 V_M4, f32, FPR32>;
      def VF32M8: GroupVTypeInfo<vfloat32m8_t, vfloat32m1_t, vbool4_t,  32,
                                 V_M8, f32, FPR32>;

      def VF64M2: GroupVTypeInfo<vfloat64m2_t, vfloat64m1_t, vbool32_t, 64,
                                 V_M2, f64, FPR64>;
      def VF64M4: GroupVTypeInfo<vfloat64m4_t, vfloat64m1_t, vbool16_t, 64,
                                 V_M4, f64, FPR64>;
      def VF64M8: GroupVTypeInfo<vfloat64m8_t, vfloat64m1_t, vbool8_t,  64,
                                 V_M8, f64, FPR64>;
    }
  }

  defset list<VTypeInfo> AllBFloatVectors = {
    defset list<VTypeInfo> NoGroupBFloatVectors = {
      defset list<VTypeInfo> FractionalGroupBFloatVectors = {
        def VBF16MF4: VTypeInfo<vbfloat16mf4_t, vbool64_t, 16, V_MF4, bf16, FPR16>;
        def VBF16MF2: VTypeInfo<vbfloat16mf2_t, vbool32_t, 16, V_MF2, bf16, FPR16>;
      }
      def VBF16M1:  VTypeInfo<vbfloat16m1_t, vbool16_t, 16, V_M1, bf16, FPR16>;
    }
  
    defset list<GroupVTypeInfo> GroupBFloatVectors = {
      def VBF16M2: GroupVTypeInfo<vbfloat16m2_t, vbfloat16m1_t, vbool8_t, 16,
                                  V_M2, bf16, FPR16>;
      def VBF16M4: GroupVTypeInfo<vbfloat16m4_t, vbfloat16m1_t, vbool4_t, 16,
                                  V_M4, bf16, FPR16>;
      def VBF16M8: GroupVTypeInfo<vbfloat16m8_t, vbfloat16m1_t, vbool2_t, 16,
                                  V_M8, bf16, FPR16>;
    }
  }
}

// This functor is used to obtain the int vector type that has the same SEW and
// multiplier as the input parameter type
class GetIntVTypeInfo<VTypeInfo vti> {
  // Equivalent integer vector type. Eg.
  //   VI8M1 → VI8M1 (identity)
  //   VF64M4 → VI64M4
  VTypeInfo Vti = !cast<VTypeInfo>(!subst("VBF", "VI",
                                          !subst("VF", "VI",
                                                 !cast<string>(vti))));
}

class MTypeInfo<ValueType Mas, LMULInfo M, string Bx> {
  ValueType Mask = Mas;
  // {SEW, VLMul} values set a valid VType to deal with this mask type.
  // we assume SEW=1 and set corresponding LMUL. vsetvli insertion will
  // look for SEW=1 to optimize based on surrounding instructions.
  int SEW = 1;
  int Log2SEW = 0;
  LMULInfo LMul = M;
  string BX = Bx; // Appendix of mask operations.
  // The pattern fragment which produces the AVL operand, representing the
  // "natural" vector length for this mask type. For scalable masks this is
  // VLMax.
  OutPatFrag AVL = VLMax;
}

defset list<MTypeInfo> AllMasks = {
  // vbool<n>_t, <n> = SEW/LMUL, we assume SEW=8 and corresponding LMUL.
  def : MTypeInfo<vbool64_t, V_MF8, "B1">;
  def : MTypeInfo<vbool32_t, V_MF4, "B2">;
  def : MTypeInfo<vbool16_t, V_MF2, "B4">;
  def : MTypeInfo<vbool8_t, V_M1, "B8">;
  def : MTypeInfo<vbool4_t, V_M2, "B16">;
  def : MTypeInfo<vbool2_t, V_M4, "B32">;
  def : MTypeInfo<vbool1_t, V_M8, "B64">;
}

class VTypeInfoToWide<VTypeInfo vti, VTypeInfo wti> {
  VTypeInfo Vti = vti;
  VTypeInfo Wti = wti;
}

class VTypeInfoToFraction<VTypeInfo vti, VTypeInfo fti> {
  VTypeInfo Vti = vti;
  VTypeInfo Fti = fti;
}

defset list<VTypeInfoToWide> AllWidenableIntVectors = {
  def : VTypeInfoToWide<VI8MF8,  VI16MF4>;
  def : VTypeInfoToWide<VI8MF4,  VI16MF2>;
  def : VTypeInfoToWide<VI8MF2,  VI16M1>;
  def : VTypeInfoToWide<VI8M1,   VI16M2>;
  def : VTypeInfoToWide<VI8M2,   VI16M4>;
  def : VTypeInfoToWide<VI8M4,   VI16M8>;

  def : VTypeInfoToWide<VI16MF4, VI32MF2>;
  def : VTypeInfoToWide<VI16MF2, VI32M1>;
  def : VTypeInfoToWide<VI16M1,  VI32M2>;
  def : VTypeInfoToWide<VI16M2,  VI32M4>;
  def : VTypeInfoToWide<VI16M4,  VI32M8>;

  def : VTypeInfoToWide<VI32MF2, VI64M1>;
  def : VTypeInfoToWide<VI32M1,  VI64M2>;
  def : VTypeInfoToWide<VI32M2,  VI64M4>;
  def : VTypeInfoToWide<VI32M4,  VI64M8>;
}

defset list<VTypeInfoToWide> AllWidenableFloatVectors = {
  def : VTypeInfoToWide<VF16MF4, VF32MF2>;
  def : VTypeInfoToWide<VF16MF2, VF32M1>;
  def : VTypeInfoToWide<VF16M1, VF32M2>;
  def : VTypeInfoToWide<VF16M2, VF32M4>;
  def : VTypeInfoToWide<VF16M4, VF32M8>;

  def : VTypeInfoToWide<VF32MF2, VF64M1>;
  def : VTypeInfoToWide<VF32M1, VF64M2>;
  def : VTypeInfoToWide<VF32M2, VF64M4>;
  def : VTypeInfoToWide<VF32M4, VF64M8>;
}

defset list<VTypeInfoToFraction> AllFractionableVF2IntVectors = {
  def : VTypeInfoToFraction<VI16MF4, VI8MF8>;
  def : VTypeInfoToFraction<VI16MF2, VI8MF4>;
  def : VTypeInfoToFraction<VI16M1, VI8MF2>;
  def : VTypeInfoToFraction<VI16M2, VI8M1>;
  def : VTypeInfoToFraction<VI16M4, VI8M2>;
  def : VTypeInfoToFraction<VI16M8, VI8M4>;
  def : VTypeInfoToFraction<VI32MF2, VI16MF4>;
  def : VTypeInfoToFraction<VI32M1, VI16MF2>;
  def : VTypeInfoToFraction<VI32M2, VI16M1>;
  def : VTypeInfoToFraction<VI32M4, VI16M2>;
  def : VTypeInfoToFraction<VI32M8, VI16M4>;
  def : VTypeInfoToFraction<VI64M1, VI32MF2>;
  def : VTypeInfoToFraction<VI64M2, VI32M1>;
  def : VTypeInfoToFraction<VI64M4, VI32M2>;
  def : VTypeInfoToFraction<VI64M8, VI32M4>;
}

defset list<VTypeInfoToFraction> AllFractionableVF4IntVectors = {
  def : VTypeInfoToFraction<VI32MF2, VI8MF8>;
  def : VTypeInfoToFraction<VI32M1, VI8MF4>;
  def : VTypeInfoToFraction<VI32M2, VI8MF2>;
  def : VTypeInfoToFraction<VI32M4, VI8M1>;
  def : VTypeInfoToFraction<VI32M8, VI8M2>;
  def : VTypeInfoToFraction<VI64M1, VI16MF4>;
  def : VTypeInfoToFraction<VI64M2, VI16MF2>;
  def : VTypeInfoToFraction<VI64M4, VI16M1>;
  def : VTypeInfoToFraction<VI64M8, VI16M2>;
}

defset list<VTypeInfoToFraction> AllFractionableVF8IntVectors = {
  def : VTypeInfoToFraction<VI64M1, VI8MF8>;
  def : VTypeInfoToFraction<VI64M2, VI8MF4>;
  def : VTypeInfoToFraction<VI64M4, VI8MF2>;
  def : VTypeInfoToFraction<VI64M8, VI8M1>;
}

defset list<VTypeInfoToWide> AllWidenableIntToFloatVectors = {
  def : VTypeInfoToWide<VI8MF8, VF16MF4>;
  def : VTypeInfoToWide<VI8MF4, VF16MF2>;
  def : VTypeInfoToWide<VI8MF2, VF16M1>;
  def : VTypeInfoToWide<VI8M1, VF16M2>;
  def : VTypeInfoToWide<VI8M2, VF16M4>;
  def : VTypeInfoToWide<VI8M4, VF16M8>;

  def : VTypeInfoToWide<VI16MF4, VF32MF2>;
  def : VTypeInfoToWide<VI16MF2, VF32M1>;
  def : VTypeInfoToWide<VI16M1, VF32M2>;
  def : VTypeInfoToWide<VI16M2, VF32M4>;
  def : VTypeInfoToWide<VI16M4, VF32M8>;

  def : VTypeInfoToWide<VI32MF2, VF64M1>;
  def : VTypeInfoToWide<VI32M1, VF64M2>;
  def : VTypeInfoToWide<VI32M2, VF64M4>;
  def : VTypeInfoToWide<VI32M4, VF64M8>;
}

defset list<VTypeInfoToWide> AllWidenableBFloatToFloatVectors = {
  def : VTypeInfoToWide<VBF16MF4, VF32MF2>;
  def : VTypeInfoToWide<VBF16MF2, VF32M1>;
  def : VTypeInfoToWide<VBF16M1, VF32M2>;
  def : VTypeInfoToWide<VBF16M2, VF32M4>;
  def : VTypeInfoToWide<VBF16M4, VF32M8>;
}

// This class holds the record of the RISCVVPseudoTable below.
// This represents the information we need in codegen for each pseudo.
// The definition should be consistent with `struct PseudoInfo` in
// RISCVInstrInfo.h.
class RISCVVPseudo {
  Pseudo Pseudo = !cast<Pseudo>(NAME); // Used as a key.
  Instruction BaseInstr = !cast<Instruction>(PseudoToVInst<NAME>.VInst);
  // SEW = 0 is used to denote that the Pseudo is not SEW specific (or unknown).
  bits<8> SEW = 0;
  bit NeedBeInPseudoTable = 1;
}

// The actual table.
def RISCVVPseudosTable : GenericTable {
  let FilterClass = "RISCVVPseudo";
  let FilterClassField = "NeedBeInPseudoTable";
  let CppTypeName = "PseudoInfo";
  let Fields = [ "Pseudo", "BaseInstr" ];
  let PrimaryKey = [ "Pseudo" ];
  let PrimaryKeyName = "getPseudoInfo";
  let PrimaryKeyEarlyOut = true;
}

def RISCVVInversePseudosTable : GenericTable {
  let FilterClass = "RISCVVPseudo";
  let CppTypeName = "PseudoInfo";
  let Fields = [ "Pseudo", "BaseInstr", "VLMul", "SEW"];
  let PrimaryKey = [ "BaseInstr", "VLMul", "SEW"];
  let PrimaryKeyName = "getBaseInfo";
  let PrimaryKeyEarlyOut = true;
}

def RISCVVIntrinsicsTable : GenericTable {
  let FilterClass = "RISCVVIntrinsic";
  let CppTypeName = "RISCVVIntrinsicInfo";
  let Fields = ["IntrinsicID", "ScalarOperand", "VLOperand"];
  let PrimaryKey = ["IntrinsicID"];
  let PrimaryKeyName = "getRISCVVIntrinsicInfo";
}

// Describes the relation of a masked pseudo to the unmasked variants.
//    Note that all masked variants (in this table) have exactly one
//    unmasked variant.  For all but compares, both the masked and
//    unmasked variant have a passthru and policy operand.  For compares,
//    neither has a policy op, and only the masked version has a passthru.
class RISCVMaskedPseudo<bits<4> MaskIdx, bit ActiveAffectsRes=false> {
  Pseudo MaskedPseudo = !cast<Pseudo>(NAME);
  Pseudo UnmaskedPseudo = !cast<Pseudo>(!subst("_MASK", "", NAME));
  bits<4> MaskOpIdx = MaskIdx;
  bit ActiveElementsAffectResult = ActiveAffectsRes;
}

def RISCVMaskedPseudosTable : GenericTable {
  let FilterClass = "RISCVMaskedPseudo";
  let CppTypeName = "RISCVMaskedPseudoInfo";
  let Fields = ["MaskedPseudo", "UnmaskedPseudo", "MaskOpIdx", "ActiveElementsAffectResult"];
  let PrimaryKey = ["MaskedPseudo"];
  let PrimaryKeyName = "getMaskedPseudoInfo";
}

class RISCVVLE<bit M, bit Str, bit F, bits<3> S, bits<3> L> {
  bits<1> Masked = M;
  bits<1> Strided = Str;
  bits<1> FF = F;
  bits<3> Log2SEW = S;
  bits<3> LMUL = L;
  Pseudo Pseudo = !cast<Pseudo>(NAME);
}

def lookupMaskedIntrinsicByUnmasked : SearchIndex {
  let Table = RISCVMaskedPseudosTable;
  let Key = ["UnmaskedPseudo"];
}

def RISCVVLETable : GenericTable {
  let FilterClass = "RISCVVLE";
  let CppTypeName = "VLEPseudo";
  let Fields = ["Masked", "Strided", "FF", "Log2SEW", "LMUL", "Pseudo"];
  let PrimaryKey = ["Masked", "Strided", "FF", "Log2SEW", "LMUL"];
  let PrimaryKeyName = "getVLEPseudo";
}

class RISCVVSE<bit M, bit Str, bits<3> S, bits<3> L> {
  bits<1> Masked = M;
  bits<1> Strided = Str;
  bits<3> Log2SEW = S;
  bits<3> LMUL = L;
  Pseudo Pseudo = !cast<Pseudo>(NAME);
}

def RISCVVSETable : GenericTable {
  let FilterClass = "RISCVVSE";
  let CppTypeName = "VSEPseudo";
  let Fields = ["Masked", "Strided", "Log2SEW", "LMUL", "Pseudo"];
  let PrimaryKey = ["Masked", "Strided", "Log2SEW", "LMUL"];
  let PrimaryKeyName = "getVSEPseudo";
}

class RISCVVLX_VSX<bit M, bit O, bits<3> S, bits<3> L, bits<3> IL> {
  bits<1> Masked = M;
  bits<1> Ordered = O;
  bits<3> Log2SEW = S;
  bits<3> LMUL = L;
  bits<3> IndexLMUL = IL;
  Pseudo Pseudo = !cast<Pseudo>(NAME);
}

class RISCVVLX<bit M, bit O, bits<3> S, bits<3> L, bits<3> IL> :
  RISCVVLX_VSX<M, O, S, L, IL>;
class RISCVVSX<bit M, bit O, bits<3> S, bits<3> L, bits<3> IL> :
  RISCVVLX_VSX<M, O, S, L, IL>;

class RISCVVLX_VSXTable : GenericTable {
  let CppTypeName = "VLX_VSXPseudo";
  let Fields = ["Masked", "Ordered", "Log2SEW", "LMUL", "IndexLMUL", "Pseudo"];
  let PrimaryKey = ["Masked", "Ordered", "Log2SEW", "LMUL", "IndexLMUL"];
}

def RISCVVLXTable : RISCVVLX_VSXTable {
  let FilterClass = "RISCVVLX";
  let PrimaryKeyName = "getVLXPseudo";
}

def RISCVVSXTable : RISCVVLX_VSXTable {
  let FilterClass = "RISCVVSX";
  let PrimaryKeyName = "getVSXPseudo";
}

class RISCVVLSEG<bits<4> N, bit M, bit Str, bit F, bits<3> S, bits<3> L> {
  bits<4> NF = N;
  bits<1> Masked = M;
  bits<1> Strided = Str;
  bits<1> FF = F;
  bits<3> Log2SEW = S;
  bits<3> LMUL = L;
  Pseudo Pseudo = !cast<Pseudo>(NAME);
}

def RISCVVLSEGTable : GenericTable {
  let FilterClass = "RISCVVLSEG";
  let CppTypeName = "VLSEGPseudo";
  let Fields = ["NF", "Masked", "Strided", "FF", "Log2SEW", "LMUL", "Pseudo"];
  let PrimaryKey = ["NF", "Masked", "Strided", "FF", "Log2SEW", "LMUL"];
  let PrimaryKeyName = "getVLSEGPseudo";
}

class RISCVVLXSEG<bits<4> N, bit M, bit O, bits<3> S, bits<3> L, bits<3> IL> {
  bits<4> NF = N;
  bits<1> Masked = M;
  bits<1> Ordered = O;
  bits<3> Log2SEW = S;
  bits<3> LMUL = L;
  bits<3> IndexLMUL = IL;
  Pseudo Pseudo = !cast<Pseudo>(NAME);
}

def RISCVVLXSEGTable : GenericTable {
  let FilterClass = "RISCVVLXSEG";
  let CppTypeName = "VLXSEGPseudo";
  let Fields = ["NF", "Masked", "Ordered", "Log2SEW", "LMUL", "IndexLMUL", "Pseudo"];
  let PrimaryKey = ["NF", "Masked", "Ordered", "Log2SEW", "LMUL", "IndexLMUL"];
  let PrimaryKeyName = "getVLXSEGPseudo";
}

class RISCVVSSEG<bits<4> N, bit M, bit Str, bits<3> S, bits<3> L> {
  bits<4> NF = N;
  bits<1> Masked = M;
  bits<1> Strided = Str;
  bits<3> Log2SEW = S;
  bits<3> LMUL = L;
  Pseudo Pseudo = !cast<Pseudo>(NAME);
}

def RISCVVSSEGTable : GenericTable {
  let FilterClass = "RISCVVSSEG";
  let CppTypeName = "VSSEGPseudo";
  let Fields = ["NF", "Masked", "Strided", "Log2SEW", "LMUL", "Pseudo"];
  let PrimaryKey = ["NF", "Masked", "Strided", "Log2SEW", "LMUL"];
  let PrimaryKeyName = "getVSSEGPseudo";
}

class RISCVVSXSEG<bits<4> N, bit M, bit O, bits<3> S, bits<3> L, bits<3> IL> {
  bits<4> NF = N;
  bits<1> Masked = M;
  bits<1> Ordered = O;
  bits<3> Log2SEW = S;
  bits<3> LMUL = L;
  bits<3> IndexLMUL = IL;
  Pseudo Pseudo = !cast<Pseudo>(NAME);
}

def RISCVVSXSEGTable : GenericTable {
  let FilterClass = "RISCVVSXSEG";
  let CppTypeName = "VSXSEGPseudo";
  let Fields = ["NF", "Masked", "Ordered", "Log2SEW", "LMUL", "IndexLMUL", "Pseudo"];
  let PrimaryKey = ["NF", "Masked", "Ordered", "Log2SEW", "LMUL", "IndexLMUL"];
  let PrimaryKeyName = "getVSXSEGPseudo";
}

//===----------------------------------------------------------------------===//
// Helpers to define the different pseudo instructions.
//===----------------------------------------------------------------------===//

// The destination vector register group for a masked vector instruction cannot
// overlap the source mask register (v0), unless the destination vector register
// is being written with a mask value (e.g., comparisons) or the scalar result
// of a reduction.
class GetVRegNoV0<VReg VRegClass> {
  VReg R = !cond(!eq(VRegClass, VR) : VRNoV0,
                 !eq(VRegClass, VRM2) : VRM2NoV0,
                 !eq(VRegClass, VRM4) : VRM4NoV0,
                 !eq(VRegClass, VRM8) : VRM8NoV0,
                 !eq(VRegClass, VRN2M1) : VRN2M1NoV0,
                 !eq(VRegClass, VRN2M2) : VRN2M2NoV0,
                 !eq(VRegClass, VRN2M4) : VRN2M4NoV0,
                 !eq(VRegClass, VRN3M1) : VRN3M1NoV0,
                 !eq(VRegClass, VRN3M2) : VRN3M2NoV0,
                 !eq(VRegClass, VRN4M1) : VRN4M1NoV0,
                 !eq(VRegClass, VRN4M2) : VRN4M2NoV0,
                 !eq(VRegClass, VRN5M1) : VRN5M1NoV0,
                 !eq(VRegClass, VRN6M1) : VRN6M1NoV0,
                 !eq(VRegClass, VRN7M1) : VRN7M1NoV0,
                 !eq(VRegClass, VRN8M1) : VRN8M1NoV0,
                 true : VRegClass);
}

class VPseudo<Instruction instr, LMULInfo m, dag outs, dag ins, int sew = 0> :
      Pseudo<outs, ins, []>, RISCVVPseudo {
  let BaseInstr = instr;
  let VLMul = m.value;
  let SEW = sew;
}

class GetVTypePredicates<VTypeInfo vti> {
  list<Predicate> Predicates = !cond(!eq(vti.Scalar, f16) : [HasVInstructionsF16],
                                     !eq(vti.Scalar, bf16) : [HasVInstructionsBF16],
                                     !eq(vti.Scalar, f32) : [HasVInstructionsAnyF],
                                     !eq(vti.Scalar, f64) : [HasVInstructionsF64],
                                     !eq(vti.SEW, 64) : [HasVInstructionsI64],
                                     true : [HasVInstructions]);
}

class VPseudoUSLoadNoMask<VReg RetClass,
                          int EEW> :
      Pseudo<(outs RetClass:$rd),
             (ins RetClass:$dest, GPRMem:$rs1, AVL:$vl, ixlenimm:$sew,
                  ixlenimm:$policy), []>,
      RISCVVPseudo,
      RISCVVLE</*Masked*/0, /*Strided*/0, /*FF*/0, !logtwo(EEW), VLMul> {
  let mayLoad = 1;
  let mayStore = 0;
  let hasSideEffects = 0;
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
  let Constraints = "$rd = $dest";
}

class VPseudoUSLoadMask<VReg RetClass,
                        int EEW> :
      Pseudo<(outs GetVRegNoV0<RetClass>.R:$rd),
             (ins GetVRegNoV0<RetClass>.R:$merge,
                  GPRMem:$rs1,
                  VMaskOp:$vm, AVL:$vl, ixlenimm:$sew, ixlenimm:$policy), []>,
      RISCVVPseudo,
      RISCVVLE</*Masked*/1, /*Strided*/0, /*FF*/0, !logtwo(EEW), VLMul> {
  let mayLoad = 1;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Constraints = "$rd = $merge";
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
  let UsesMaskPolicy = 1;
}

class VPseudoUSLoadFFNoMask<VReg RetClass,
                            int EEW> :
      Pseudo<(outs RetClass:$rd, GPR:$vl),
             (ins RetClass:$dest, GPRMem:$rs1, AVL:$avl,
                  ixlenimm:$sew, ixlenimm:$policy), []>,
      RISCVVPseudo,
      RISCVVLE</*Masked*/0, /*Strided*/0, /*FF*/1, !logtwo(EEW), VLMul> {
  let mayLoad = 1;
  let mayStore = 0;
  let hasSideEffects = 0;
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
  let Constraints = "$rd = $dest";
}

class VPseudoUSLoadFFMask<VReg RetClass,
                          int EEW> :
      Pseudo<(outs GetVRegNoV0<RetClass>.R:$rd, GPR:$vl),
             (ins GetVRegNoV0<RetClass>.R:$merge,
                  GPRMem:$rs1,
                  VMaskOp:$vm, AVL:$avl, ixlenimm:$sew, ixlenimm:$policy), []>,
      RISCVVPseudo,
      RISCVVLE</*Masked*/1, /*Strided*/0, /*FF*/1, !logtwo(EEW), VLMul> {
  let mayLoad = 1;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Constraints = "$rd = $merge";
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
  let UsesMaskPolicy = 1;
}

class VPseudoSLoadNoMask<VReg RetClass,
                         int EEW> :
      Pseudo<(outs RetClass:$rd),
             (ins RetClass:$dest, GPRMem:$rs1, GPR:$rs2, AVL:$vl,
                  ixlenimm:$sew, ixlenimm:$policy), []>,
      RISCVVPseudo,
      RISCVVLE</*Masked*/0, /*Strided*/1, /*FF*/0, !logtwo(EEW), VLMul> {
  let mayLoad = 1;
  let mayStore = 0;
  let hasSideEffects = 0;
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
  let Constraints = "$rd = $dest";
}

class VPseudoSLoadMask<VReg RetClass,
                       int EEW> :
      Pseudo<(outs GetVRegNoV0<RetClass>.R:$rd),
             (ins GetVRegNoV0<RetClass>.R:$merge,
                  GPRMem:$rs1, GPR:$rs2,
                  VMaskOp:$vm, AVL:$vl, ixlenimm:$sew, ixlenimm:$policy), []>,
      RISCVVPseudo,
      RISCVVLE</*Masked*/1, /*Strided*/1, /*FF*/0, !logtwo(EEW), VLMul> {
  let mayLoad = 1;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Constraints = "$rd = $merge";
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
  let UsesMaskPolicy = 1;
}

class VPseudoILoadNoMask<VReg RetClass,
                         VReg IdxClass,
                         int EEW,
                         bits<3> LMUL,
                         bit Ordered,
                         bit EarlyClobber,
                         int TargetConstraintType = 1> :
      Pseudo<(outs RetClass:$rd),
             (ins RetClass:$dest, GPRMem:$rs1, IdxClass:$rs2, AVL:$vl,
                  ixlenimm:$sew, ixlenimm:$policy), []>,
      RISCVVPseudo,
      RISCVVLX</*Masked*/0, Ordered, !logtwo(EEW), VLMul, LMUL> {
  let mayLoad = 1;
  let mayStore = 0;
  let hasSideEffects = 0;
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
  let Constraints = !if(!eq(EarlyClobber, 1), "@earlyclobber $rd, $rd = $dest", "$rd = $dest");
  let TargetOverlapConstraintType = TargetConstraintType;
}

class VPseudoILoadMask<VReg RetClass,
                       VReg IdxClass,
                       int EEW,
                       bits<3> LMUL,
                       bit Ordered,
                       bit EarlyClobber,
                       int TargetConstraintType = 1> :
      Pseudo<(outs GetVRegNoV0<RetClass>.R:$rd),
             (ins GetVRegNoV0<RetClass>.R:$merge,
                  GPRMem:$rs1, IdxClass:$rs2,
                  VMaskOp:$vm, AVL:$vl, ixlenimm:$sew, ixlenimm:$policy), []>,
      RISCVVPseudo,
      RISCVVLX</*Masked*/1, Ordered, !logtwo(EEW), VLMul, LMUL> {
  let mayLoad = 1;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Constraints = !if(!eq(EarlyClobber, 1), "@earlyclobber $rd, $rd = $merge", "$rd = $merge");
  let TargetOverlapConstraintType = TargetConstraintType;
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
  let UsesMaskPolicy = 1;
}

class VPseudoUSStoreNoMask<VReg StClass,
                           int EEW> :
      Pseudo<(outs),
             (ins StClass:$rd, GPRMem:$rs1, AVL:$vl, ixlenimm:$sew), []>,
      RISCVVPseudo,
      RISCVVSE</*Masked*/0, /*Strided*/0, !logtwo(EEW), VLMul> {
  let mayLoad = 0;
  let mayStore = 1;
  let hasSideEffects = 0;
  let HasVLOp = 1;
  let HasSEWOp = 1;
}

class VPseudoUSStoreMask<VReg StClass,
                         int EEW> :
      Pseudo<(outs),
             (ins StClass:$rd, GPRMem:$rs1,
                  VMaskOp:$vm, AVL:$vl, ixlenimm:$sew), []>,
      RISCVVPseudo,
      RISCVVSE</*Masked*/1, /*Strided*/0, !logtwo(EEW), VLMul> {
  let mayLoad = 0;
  let mayStore = 1;
  let hasSideEffects = 0;
  let HasVLOp = 1;
  let HasSEWOp = 1;
}

class VPseudoSStoreNoMask<VReg StClass,
                          int EEW> :
      Pseudo<(outs),
             (ins StClass:$rd, GPRMem:$rs1, GPR:$rs2,
                  AVL:$vl, ixlenimm:$sew), []>,
      RISCVVPseudo,
      RISCVVSE</*Masked*/0, /*Strided*/1, !logtwo(EEW), VLMul> {
  let mayLoad = 0;
  let mayStore = 1;
  let hasSideEffects = 0;
  let HasVLOp = 1;
  let HasSEWOp = 1;
}

class VPseudoSStoreMask<VReg StClass,
                        int EEW> :
      Pseudo<(outs),
             (ins StClass:$rd, GPRMem:$rs1, GPR:$rs2,
                  VMaskOp:$vm, AVL:$vl, ixlenimm:$sew), []>,
      RISCVVPseudo,
      RISCVVSE</*Masked*/1, /*Strided*/1, !logtwo(EEW), VLMul> {
  let mayLoad = 0;
  let mayStore = 1;
  let hasSideEffects = 0;
  let HasVLOp = 1;
  let HasSEWOp = 1;
}

class VPseudoNullaryNoMask<VReg RegClass> :
      Pseudo<(outs RegClass:$rd),
             (ins RegClass:$merge,
                  AVL:$vl, ixlenimm:$sew, ixlenimm:$policy), []>,
      RISCVVPseudo {
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Constraints = "$rd = $merge";
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
}

class VPseudoNullaryMask<VReg RegClass> :
      Pseudo<(outs GetVRegNoV0<RegClass>.R:$rd),
             (ins GetVRegNoV0<RegClass>.R:$merge,
                  VMaskOp:$vm, AVL:$vl, ixlenimm:$sew, ixlenimm:$policy), []>,
      RISCVVPseudo {
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Constraints ="$rd = $merge";
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let UsesMaskPolicy = 1;
  let HasVecPolicyOp = 1;
}

// Nullary for pseudo instructions. They are expanded in
// RISCVExpandPseudoInsts pass.
class VPseudoNullaryPseudoM<string BaseInst> :
      Pseudo<(outs VR:$rd), (ins AVL:$vl, ixlenimm:$sew), []>,
      RISCVVPseudo {
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let HasVLOp = 1;
  let HasSEWOp = 1;
  // BaseInstr is not used in RISCVExpandPseudoInsts pass.
  // Just fill a corresponding real v-inst to pass tablegen check.
  let BaseInstr = !cast<Instruction>(BaseInst);
  // We exclude them from RISCVVPseudoTable.
  let NeedBeInPseudoTable = 0;
}

class VPseudoUnaryNoMask<DAGOperand RetClass,
                         DAGOperand OpClass,
                         string Constraint = "",
                         int TargetConstraintType = 1> :
      Pseudo<(outs RetClass:$rd),
             (ins RetClass:$merge, OpClass:$rs2,
                  AVL:$vl, ixlenimm:$sew, ixlenimm:$policy), []>,
      RISCVVPseudo {
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Constraints = !interleave([Constraint, "$rd = $merge"], ",");
  let TargetOverlapConstraintType = TargetConstraintType;
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
}

class VPseudoUnaryNoMaskNoPolicy<DAGOperand RetClass,
                                 DAGOperand OpClass,
                                 string Constraint = "",
                                 int TargetConstraintType = 1> :
      Pseudo<(outs RetClass:$rd),
             (ins OpClass:$rs2, AVL:$vl, ixlenimm:$sew), []>,
      RISCVVPseudo {
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Constraints = Constraint;
  let TargetOverlapConstraintType = TargetConstraintType;
  let HasVLOp = 1;
  let HasSEWOp = 1;
}

class VPseudoUnaryNoMaskRoundingMode<DAGOperand RetClass,
                                     DAGOperand OpClass,
                                     string Constraint = "",
                                     int TargetConstraintType = 1> :
      Pseudo<(outs RetClass:$rd),
             (ins RetClass:$merge, OpClass:$rs2, ixlenimm:$rm,
                  AVL:$vl, ixlenimm:$sew, ixlenimm:$policy), []>,
      RISCVVPseudo {
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Constraints = !interleave([Constraint, "$rd = $merge"], ",");
  let TargetOverlapConstraintType = TargetConstraintType;
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
  let HasRoundModeOp = 1;
  let UsesVXRM = 0;
}

class VPseudoUnaryMask<VReg RetClass,
                       VReg OpClass,
                       string Constraint = "",
                       int TargetConstraintType = 1> :
      Pseudo<(outs GetVRegNoV0<RetClass>.R:$rd),
             (ins GetVRegNoV0<RetClass>.R:$merge, OpClass:$rs2,
                  VMaskOp:$vm, AVL:$vl, ixlenimm:$sew, ixlenimm:$policy), []>,
      RISCVVPseudo {
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Constraints = !interleave([Constraint, "$rd = $merge"], ",");
  let TargetOverlapConstraintType = TargetConstraintType;
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
  let UsesMaskPolicy = 1;
}

class VPseudoUnaryMaskRoundingMode<VReg RetClass,
                                   VReg OpClass,
                                   string Constraint = "",
                                   int TargetConstraintType = 1> :
      Pseudo<(outs GetVRegNoV0<RetClass>.R:$rd),
             (ins GetVRegNoV0<RetClass>.R:$merge, OpClass:$rs2,
                  VMaskOp:$vm, ixlenimm:$rm,
                  AVL:$vl, ixlenimm:$sew, ixlenimm:$policy), []>,
      RISCVVPseudo {
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Constraints = !interleave([Constraint, "$rd = $merge"], ",");
  let TargetOverlapConstraintType = TargetConstraintType;
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
  let UsesMaskPolicy = 1;
  let HasRoundModeOp = 1;
  let UsesVXRM = 0;
}

class VPseudoUnaryMask_NoExcept<VReg RetClass,
                                VReg OpClass,
                                string Constraint = ""> :
      Pseudo<(outs GetVRegNoV0<RetClass>.R:$rd),
             (ins GetVRegNoV0<RetClass>.R:$merge, OpClass:$rs2,
                  VMaskOp:$vm, AVL:$vl, ixlenimm:$sew, ixlenimm:$policy), []> {
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Constraints = !interleave([Constraint, "$rd = $merge"], ",");
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
  let UsesMaskPolicy = 1;
  let usesCustomInserter = 1;
}

class VPseudoUnaryNoMask_FRM<VReg RetClass,
                             VReg OpClass,
                             string Constraint = "",
                             int TargetConstraintType = 1> :
      Pseudo<(outs RetClass:$rd),
             (ins RetClass:$merge, OpClass:$rs2, ixlenimm:$frm,
                  AVL:$vl, ixlenimm:$sew, ixlenimm:$policy), []>,
      RISCVVPseudo {
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Constraints = !interleave([Constraint, "$rd = $merge"], ",");
  let TargetOverlapConstraintType = TargetConstraintType;
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
  let HasRoundModeOp = 1;
}

class VPseudoUnaryMask_FRM<VReg RetClass,
                           VReg OpClass,
                           string Constraint = "",
                           int TargetConstraintType = 1> :
      Pseudo<(outs GetVRegNoV0<RetClass>.R:$rd),
             (ins GetVRegNoV0<RetClass>.R:$merge, OpClass:$rs2,
                  VMaskOp:$vm, ixlenimm:$frm,
                  AVL:$vl, ixlenimm:$sew, ixlenimm:$policy), []>,
      RISCVVPseudo {
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Constraints = !interleave([Constraint, "$rd = $merge"], ",");
  let TargetOverlapConstraintType = TargetConstraintType;
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
  let UsesMaskPolicy = 1;
  let HasRoundModeOp = 1;
}

class VPseudoUnaryNoMaskGPROut :
      Pseudo<(outs GPR:$rd),
             (ins VR:$rs2, AVL:$vl, ixlenimm:$sew), []>,
      RISCVVPseudo {
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let HasVLOp = 1;
  let HasSEWOp = 1;
}

class VPseudoUnaryMaskGPROut :
      Pseudo<(outs GPR:$rd),
             (ins VR:$rs1, VMaskOp:$vm, AVL:$vl, ixlenimm:$sew), []>,
      RISCVVPseudo {
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let HasVLOp = 1;
  let HasSEWOp = 1;
}

// Mask can be V0~V31
class VPseudoUnaryAnyMask<VReg RetClass,
                          VReg Op1Class> :
      Pseudo<(outs RetClass:$rd),
             (ins RetClass:$merge, Op1Class:$rs2,
                  VR:$vm, AVL:$vl, ixlenimm:$sew), []>,
      RISCVVPseudo {
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Constraints = "@earlyclobber $rd, $rd = $merge";
  let HasVLOp = 1;
  let HasSEWOp = 1;
}

class VPseudoBinaryNoMask<VReg RetClass,
                          VReg Op1Class,
                          DAGOperand Op2Class,
                          string Constraint,
                          int TargetConstraintType = 1> :
      Pseudo<(outs RetClass:$rd),
             (ins Op1Class:$rs2, Op2Class:$rs1, AVL:$vl, ixlenimm:$sew), []>,
      RISCVVPseudo {
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Constraints = Constraint;
  let TargetOverlapConstraintType = TargetConstraintType;
  let HasVLOp = 1;
  let HasSEWOp = 1;
}

class VPseudoBinaryNoMaskPolicy<VReg RetClass,
                                VReg Op1Class,
                                DAGOperand Op2Class,
                                string Constraint,
                                int TargetConstraintType = 1> :
      Pseudo<(outs RetClass:$rd),
             (ins RetClass:$merge, Op1Class:$rs2, Op2Class:$rs1, AVL:$vl,
                  ixlenimm:$sew, ixlenimm:$policy), []>,
      RISCVVPseudo {
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Constraints = !interleave([Constraint, "$rd = $merge"], ",");
  let TargetOverlapConstraintType = TargetConstraintType;
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
}

class VPseudoBinaryNoMaskRoundingMode<VReg RetClass,
                                      VReg Op1Class,
                                      DAGOperand Op2Class,
                                      string Constraint,
                                      int UsesVXRM_ = 1,
                                      int TargetConstraintType = 1> :
      Pseudo<(outs RetClass:$rd),
             (ins RetClass:$merge, Op1Class:$rs2, Op2Class:$rs1, ixlenimm:$rm,
                  AVL:$vl, ixlenimm:$sew, ixlenimm:$policy), []>,
      RISCVVPseudo {
  let mayLoad = 0;
  let mayStore = 0;
  let Constraints = !interleave([Constraint, "$rd = $merge"], ",");
  let TargetOverlapConstraintType = TargetConstraintType;
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
  let HasRoundModeOp = 1;
  let UsesVXRM = UsesVXRM_;
}

class VPseudoBinaryMaskPolicyRoundingMode<VReg RetClass,
                                          RegisterClass Op1Class,
                                          DAGOperand Op2Class,
                                          string Constraint,
                                          int UsesVXRM_,
                                          int TargetConstraintType = 1> :
      Pseudo<(outs GetVRegNoV0<RetClass>.R:$rd),
             (ins GetVRegNoV0<RetClass>.R:$merge,
                  Op1Class:$rs2, Op2Class:$rs1,
                  VMaskOp:$vm, ixlenimm:$rm, AVL:$vl,
                  ixlenimm:$sew, ixlenimm:$policy), []>,
      RISCVVPseudo {
  let mayLoad = 0;
  let mayStore = 0;
  let Constraints = !interleave([Constraint, "$rd = $merge"], ",");
  let TargetOverlapConstraintType = TargetConstraintType;
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
  let UsesMaskPolicy = 1;
  let HasRoundModeOp = 1;
  let UsesVXRM = UsesVXRM_;
}

// Special version of VPseudoBinaryNoMask where we pretend the first source is
// tied to the destination.
// This allows maskedoff and rs2 to be the same register.
class VPseudoTiedBinaryNoMask<VReg RetClass,
                              DAGOperand Op2Class,
                              string Constraint,
                              int TargetConstraintType = 1> :
      Pseudo<(outs RetClass:$rd),
             (ins RetClass:$rs2, Op2Class:$rs1, AVL:$vl, ixlenimm:$sew,
                  ixlenimm:$policy), []>,
      RISCVVPseudo {
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Constraints = !interleave([Constraint, "$rd = $rs2"], ",");
  let TargetOverlapConstraintType = TargetConstraintType;
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
  let isConvertibleToThreeAddress = 1;
  let IsTiedPseudo = 1;
}

class VPseudoTiedBinaryNoMaskRoundingMode<VReg RetClass,
                                          DAGOperand Op2Class,
                                          string Constraint,
                                          int TargetConstraintType = 1> :
      Pseudo<(outs RetClass:$rd),
             (ins RetClass:$rs2, Op2Class:$rs1,
                  ixlenimm:$rm,
                  AVL:$vl, ixlenimm:$sew,
                  ixlenimm:$policy), []>,
      RISCVVPseudo {
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Constraints = !interleave([Constraint, "$rd = $rs2"], ",");
  let TargetOverlapConstraintType = TargetConstraintType;
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
  let isConvertibleToThreeAddress = 1;
  let IsTiedPseudo = 1;
  let HasRoundModeOp = 1;
  let UsesVXRM = 0;
}

class VPseudoIStoreNoMask<VReg StClass, VReg IdxClass, int EEW, bits<3> LMUL,
                          bit Ordered>:
      Pseudo<(outs),
             (ins StClass:$rd, GPRMem:$rs1, IdxClass:$rs2, AVL:$vl,
                  ixlenimm:$sew),[]>,
      RISCVVPseudo,
      RISCVVSX</*Masked*/0, Ordered, !logtwo(EEW), VLMul, LMUL> {
  let mayLoad = 0;
  let mayStore = 1;
  let hasSideEffects = 0;
  let HasVLOp = 1;
  let HasSEWOp = 1;
}

class VPseudoIStoreMask<VReg StClass, VReg IdxClass, int EEW, bits<3> LMUL,
                        bit Ordered>:
      Pseudo<(outs),
             (ins StClass:$rd, GPRMem:$rs1, IdxClass:$rs2,
                  VMaskOp:$vm, AVL:$vl, ixlenimm:$sew),[]>,
      RISCVVPseudo,
      RISCVVSX</*Masked*/1, Ordered, !logtwo(EEW), VLMul, LMUL> {
  let mayLoad = 0;
  let mayStore = 1;
  let hasSideEffects = 0;
  let HasVLOp = 1;
  let HasSEWOp = 1;
}

class VPseudoBinaryMaskPolicy<VReg RetClass,
                              RegisterClass Op1Class,
                              DAGOperand Op2Class,
                              string Constraint,
                              int TargetConstraintType = 1> :
      Pseudo<(outs GetVRegNoV0<RetClass>.R:$rd),
             (ins GetVRegNoV0<RetClass>.R:$merge,
                  Op1Class:$rs2, Op2Class:$rs1,
                  VMaskOp:$vm, AVL:$vl, ixlenimm:$sew, ixlenimm:$policy), []>,
      RISCVVPseudo {
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Constraints = !interleave([Constraint, "$rd = $merge"], ",");
  let TargetOverlapConstraintType = TargetConstraintType;
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
  let UsesMaskPolicy = 1;
}

class VPseudoTernaryMaskPolicy<VReg RetClass,
                               RegisterClass Op1Class,
                               DAGOperand Op2Class> :
      Pseudo<(outs GetVRegNoV0<RetClass>.R:$rd),
             (ins GetVRegNoV0<RetClass>.R:$merge,
                  Op1Class:$rs2, Op2Class:$rs1,
                  VMaskOp:$vm, AVL:$vl, ixlenimm:$sew, ixlenimm:$policy), []>,
      RISCVVPseudo {
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Constraints = "$rd = $merge";
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
}

class VPseudoTernaryMaskPolicyRoundingMode<VReg RetClass,
                                           RegisterClass Op1Class,
                                           DAGOperand Op2Class> :
      Pseudo<(outs GetVRegNoV0<RetClass>.R:$rd),
             (ins GetVRegNoV0<RetClass>.R:$merge,
                  Op1Class:$rs2, Op2Class:$rs1,
                  VMaskOp:$vm,
                  ixlenimm:$rm,
                  AVL:$vl, ixlenimm:$sew, ixlenimm:$policy), []>,
      RISCVVPseudo {
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Constraints = "$rd = $merge";
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
  let HasRoundModeOp = 1;
  let UsesVXRM = 0;
}

// Like VPseudoBinaryMaskPolicy, but output can be V0 and there is no policy.
class VPseudoBinaryMOutMask<VReg RetClass,
                            RegisterClass Op1Class,
                            DAGOperand Op2Class,
                            string Constraint,
                            int TargetConstraintType = 1> :
      Pseudo<(outs RetClass:$rd),
             (ins RetClass:$merge,
                  Op1Class:$rs2, Op2Class:$rs1,
                  VMaskOp:$vm, AVL:$vl, ixlenimm:$sew), []>,
      RISCVVPseudo {
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Constraints = !interleave([Constraint, "$rd = $merge"], ",");
  let TargetOverlapConstraintType = TargetConstraintType;
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let UsesMaskPolicy = 1;
}

// Special version of VPseudoBinaryMaskPolicy where we pretend the first source
// is tied to the destination so we can workaround the earlyclobber constraint.
// This allows maskedoff and rs2 to be the same register.
class VPseudoTiedBinaryMask<VReg RetClass,
                            DAGOperand Op2Class,
                            string Constraint,
                            int TargetConstraintType = 1> :
      Pseudo<(outs GetVRegNoV0<RetClass>.R:$rd),
             (ins GetVRegNoV0<RetClass>.R:$merge,
                  Op2Class:$rs1,
                  VMaskOp:$vm, AVL:$vl, ixlenimm:$sew, ixlenimm:$policy), []>,
      RISCVVPseudo {
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Constraints = !interleave([Constraint, "$rd = $merge"], ",");
  let TargetOverlapConstraintType = TargetConstraintType;
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
  let UsesMaskPolicy = 1;
  let IsTiedPseudo = 1;
}

class VPseudoTiedBinaryMaskRoundingMode<VReg RetClass,
                                        DAGOperand Op2Class,
                                        string Constraint,
                                        int TargetConstraintType = 1> :
      Pseudo<(outs GetVRegNoV0<RetClass>.R:$rd),
             (ins GetVRegNoV0<RetClass>.R:$merge,
                  Op2Class:$rs1,
                  VMaskOp:$vm,
                  ixlenimm:$rm,
                  AVL:$vl, ixlenimm:$sew, ixlenimm:$policy), []>,
      RISCVVPseudo {
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Constraints = !interleave([Constraint, "$rd = $merge"], ",");
  let TargetOverlapConstraintType = TargetConstraintType;
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
  let UsesMaskPolicy = 1;
  let IsTiedPseudo = 1;
  let HasRoundModeOp = 1;
  let UsesVXRM = 0;
}

class VPseudoBinaryCarry<VReg RetClass,
                         VReg Op1Class,
                         DAGOperand Op2Class,
                         LMULInfo MInfo,
                         bit CarryIn,
                         string Constraint,
                         int TargetConstraintType = 1> :
      Pseudo<(outs RetClass:$rd),
             !if(CarryIn,
                (ins Op1Class:$rs2, Op2Class:$rs1,
                     VMV0:$carry, AVL:$vl, ixlenimm:$sew),
                (ins Op1Class:$rs2, Op2Class:$rs1,
                     AVL:$vl, ixlenimm:$sew)), []>,
      RISCVVPseudo {
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Constraints = Constraint;
  let TargetOverlapConstraintType = TargetConstraintType;
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let VLMul = MInfo.value;
}

class VPseudoTiedBinaryCarryIn<VReg RetClass,
                               VReg Op1Class,
                               DAGOperand Op2Class,
                               LMULInfo MInfo,
                               int TargetConstraintType = 1> :
      Pseudo<(outs RetClass:$rd),
             (ins RetClass:$merge, Op1Class:$rs2, Op2Class:$rs1,
                  VMV0:$carry, AVL:$vl, ixlenimm:$sew), []>,
      RISCVVPseudo {
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Constraints = "$rd = $merge";
  let TargetOverlapConstraintType = TargetConstraintType;
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 0;
  let VLMul = MInfo.value;
}

class VPseudoTernaryNoMask<VReg RetClass,
                           RegisterClass Op1Class,
                           DAGOperand Op2Class,
                           string Constraint> :
      Pseudo<(outs RetClass:$rd),
             (ins RetClass:$rs3, Op1Class:$rs1, Op2Class:$rs2,
                  AVL:$vl, ixlenimm:$sew), []>,
      RISCVVPseudo {
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Constraints = !interleave([Constraint, "$rd = $rs3"], ",");
  let HasVLOp = 1;
  let HasSEWOp = 1;
}

class VPseudoTernaryNoMaskWithPolicy<VReg RetClass,
                                     RegisterClass Op1Class,
                                     DAGOperand Op2Class,
                                     string Constraint = "",
                                     int TargetConstraintType = 1> :
      Pseudo<(outs RetClass:$rd),
             (ins RetClass:$rs3, Op1Class:$rs1, Op2Class:$rs2,
                  AVL:$vl, ixlenimm:$sew, ixlenimm:$policy), []>,
      RISCVVPseudo {
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Constraints = !interleave([Constraint, "$rd = $rs3"], ",");
  let TargetOverlapConstraintType = TargetConstraintType;
  let HasVecPolicyOp = 1;
  let HasVLOp = 1;
  let HasSEWOp = 1;
}

class VPseudoTernaryNoMaskWithPolicyRoundingMode<VReg RetClass,
                                                 RegisterClass Op1Class,
                                                 DAGOperand Op2Class,
                                                 string Constraint = "",
                                                 int TargetConstraintType = 1> :
      Pseudo<(outs RetClass:$rd),
             (ins RetClass:$rs3, Op1Class:$rs1, Op2Class:$rs2,
                  ixlenimm:$rm, AVL:$vl, ixlenimm:$sew, ixlenimm:$policy), []>,
      RISCVVPseudo {
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Constraints = !interleave([Constraint, "$rd = $rs3"], ",");
  let TargetOverlapConstraintType = TargetConstraintType;
  let HasVecPolicyOp = 1;
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasRoundModeOp = 1;
  let UsesVXRM = 0;
}

class VPseudoUSSegLoadNoMask<VReg RetClass,
                             int EEW,
                             bits<4> NF> :
      Pseudo<(outs RetClass:$rd),
             (ins RetClass:$dest, GPRMem:$rs1, AVL:$vl,
                  ixlenimm:$sew, ixlenimm:$policy), []>,
      RISCVVPseudo,
      RISCVVLSEG<NF, /*Masked*/0, /*Strided*/0, /*FF*/0, !logtwo(EEW), VLMul> {
  let mayLoad = 1;
  let mayStore = 0;
  let hasSideEffects = 0;
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
  let Constraints = "$rd = $dest";
}

class VPseudoUSSegLoadMask<VReg RetClass,
                           int EEW,
                           bits<4> NF> :
      Pseudo<(outs GetVRegNoV0<RetClass>.R:$rd),
             (ins GetVRegNoV0<RetClass>.R:$merge, GPRMem:$rs1,
                  VMaskOp:$vm, AVL:$vl, ixlenimm:$sew, ixlenimm:$policy), []>,
      RISCVVPseudo,
      RISCVVLSEG<NF, /*Masked*/1, /*Strided*/0, /*FF*/0, !logtwo(EEW), VLMul> {
  let mayLoad = 1;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Constraints = "$rd = $merge";
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
  let UsesMaskPolicy = 1;
}

class VPseudoUSSegLoadFFNoMask<VReg RetClass,
                               int EEW,
                               bits<4> NF> :
      Pseudo<(outs RetClass:$rd, GPR:$vl),
             (ins RetClass:$dest, GPRMem:$rs1, AVL:$avl,
                  ixlenimm:$sew, ixlenimm:$policy), []>,
      RISCVVPseudo,
      RISCVVLSEG<NF, /*Masked*/0, /*Strided*/0, /*FF*/1, !logtwo(EEW), VLMul> {
  let mayLoad = 1;
  let mayStore = 0;
  let hasSideEffects = 0;
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
  let Constraints = "$rd = $dest";
}

class VPseudoUSSegLoadFFMask<VReg RetClass,
                             int EEW,
                             bits<4> NF> :
      Pseudo<(outs GetVRegNoV0<RetClass>.R:$rd, GPR:$vl),
             (ins GetVRegNoV0<RetClass>.R:$merge, GPRMem:$rs1,
                  VMaskOp:$vm, AVL:$avl, ixlenimm:$sew, ixlenimm:$policy), []>,
      RISCVVPseudo,
      RISCVVLSEG<NF, /*Masked*/1, /*Strided*/0, /*FF*/1, !logtwo(EEW), VLMul> {
  let mayLoad = 1;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Constraints = "$rd = $merge";
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
  let UsesMaskPolicy = 1;
}

class VPseudoSSegLoadNoMask<VReg RetClass,
                            int EEW,
                            bits<4> NF> :
      Pseudo<(outs RetClass:$rd),
             (ins RetClass:$merge, GPRMem:$rs1, GPR:$offset, AVL:$vl,
             ixlenimm:$sew, ixlenimm:$policy), []>,
      RISCVVPseudo,
      RISCVVLSEG<NF, /*Masked*/0, /*Strided*/1, /*FF*/0, !logtwo(EEW), VLMul> {
  let mayLoad = 1;
  let mayStore = 0;
  let hasSideEffects = 0;
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
  let Constraints = "$rd = $merge";
}

class VPseudoSSegLoadMask<VReg RetClass,
                          int EEW,
                          bits<4> NF> :
      Pseudo<(outs GetVRegNoV0<RetClass>.R:$rd),
             (ins GetVRegNoV0<RetClass>.R:$merge, GPRMem:$rs1,
                  GPR:$offset, VMaskOp:$vm, AVL:$vl, ixlenimm:$sew,
                  ixlenimm:$policy), []>,
      RISCVVPseudo,
      RISCVVLSEG<NF, /*Masked*/1, /*Strided*/1, /*FF*/0, !logtwo(EEW), VLMul> {
  let mayLoad = 1;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Constraints = "$rd = $merge";
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
  let UsesMaskPolicy = 1;
}

class VPseudoISegLoadNoMask<VReg RetClass,
                            VReg IdxClass,
                            int EEW,
                            bits<3> LMUL,
                            bits<4> NF,
                            bit Ordered> :
      Pseudo<(outs RetClass:$rd),
             (ins RetClass:$merge, GPRMem:$rs1, IdxClass:$offset, AVL:$vl,
                  ixlenimm:$sew, ixlenimm:$policy), []>,
      RISCVVPseudo,
      RISCVVLXSEG<NF, /*Masked*/0, Ordered, !logtwo(EEW), VLMul, LMUL> {
  let mayLoad = 1;
  let mayStore = 0;
  let hasSideEffects = 0;
  // For vector indexed segment loads, the destination vector register groups
  // cannot overlap the source vector register group
  let Constraints = "@earlyclobber $rd, $rd = $merge";
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
}

class VPseudoISegLoadMask<VReg RetClass,
                          VReg IdxClass,
                          int EEW,
                          bits<3> LMUL,
                          bits<4> NF,
                          bit Ordered> :
      Pseudo<(outs GetVRegNoV0<RetClass>.R:$rd),
             (ins GetVRegNoV0<RetClass>.R:$merge, GPRMem:$rs1,
                  IdxClass:$offset, VMaskOp:$vm, AVL:$vl, ixlenimm:$sew,
                  ixlenimm:$policy), []>,
      RISCVVPseudo,
      RISCVVLXSEG<NF, /*Masked*/1, Ordered, !logtwo(EEW), VLMul, LMUL> {
  let mayLoad = 1;
  let mayStore = 0;
  let hasSideEffects = 0;
  // For vector indexed segment loads, the destination vector register groups
  // cannot overlap the source vector register group
  let Constraints = "@earlyclobber $rd, $rd = $merge";
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
  let UsesMaskPolicy = 1;
}

class VPseudoUSSegStoreNoMask<VReg ValClass,
                              int EEW,
                              bits<4> NF> :
      Pseudo<(outs),
             (ins ValClass:$rd, GPRMem:$rs1, AVL:$vl, ixlenimm:$sew), []>,
      RISCVVPseudo,
      RISCVVSSEG<NF, /*Masked*/0, /*Strided*/0, !logtwo(EEW), VLMul> {
  let mayLoad = 0;
  let mayStore = 1;
  let hasSideEffects = 0;
  let HasVLOp = 1;
  let HasSEWOp = 1;
}

class VPseudoUSSegStoreMask<VReg ValClass,
                            int EEW,
                            bits<4> NF> :
      Pseudo<(outs),
             (ins ValClass:$rd, GPRMem:$rs1,
                  VMaskOp:$vm, AVL:$vl, ixlenimm:$sew), []>,
      RISCVVPseudo,
      RISCVVSSEG<NF, /*Masked*/1, /*Strided*/0, !logtwo(EEW), VLMul> {
  let mayLoad = 0;
  let mayStore = 1;
  let hasSideEffects = 0;
  let HasVLOp = 1;
  let HasSEWOp = 1;
}

class VPseudoSSegStoreNoMask<VReg ValClass,
                             int EEW,
                             bits<4> NF> :
      Pseudo<(outs),
             (ins ValClass:$rd, GPRMem:$rs1, GPR:$offset,
                  AVL:$vl, ixlenimm:$sew), []>,
      RISCVVPseudo,
      RISCVVSSEG<NF, /*Masked*/0, /*Strided*/1, !logtwo(EEW), VLMul> {
  let mayLoad = 0;
  let mayStore = 1;
  let hasSideEffects = 0;
  let HasVLOp = 1;
  let HasSEWOp = 1;
}

class VPseudoSSegStoreMask<VReg ValClass,
                           int EEW,
                           bits<4> NF> :
      Pseudo<(outs),
             (ins ValClass:$rd, GPRMem:$rs1, GPR: $offset,
                  VMaskOp:$vm, AVL:$vl, ixlenimm:$sew), []>,
      RISCVVPseudo,
      RISCVVSSEG<NF, /*Masked*/1, /*Strided*/1, !logtwo(EEW), VLMul> {
  let mayLoad = 0;
  let mayStore = 1;
  let hasSideEffects = 0;
  let HasVLOp = 1;
  let HasSEWOp = 1;
}

class VPseudoISegStoreNoMask<VReg ValClass,
                             VReg IdxClass,
                             int EEW,
                             bits<3> LMUL,
                             bits<4> NF,
                             bit Ordered> :
      Pseudo<(outs),
             (ins ValClass:$rd, GPRMem:$rs1, IdxClass: $index,
                  AVL:$vl, ixlenimm:$sew), []>,
      RISCVVPseudo,
      RISCVVSXSEG<NF, /*Masked*/0, Ordered, !logtwo(EEW), VLMul, LMUL> {
  let mayLoad = 0;
  let mayStore = 1;
  let hasSideEffects = 0;
  let HasVLOp = 1;
  let HasSEWOp = 1;
}

class VPseudoISegStoreMask<VReg ValClass,
                           VReg IdxClass,
                           int EEW,
                           bits<3> LMUL,
                           bits<4> NF,
                           bit Ordered> :
      Pseudo<(outs),
             (ins ValClass:$rd, GPRMem:$rs1, IdxClass: $index,
                  VMaskOp:$vm, AVL:$vl, ixlenimm:$sew), []>,
      RISCVVPseudo,
      RISCVVSXSEG<NF, /*Masked*/1, Ordered, !logtwo(EEW), VLMul, LMUL> {
  let mayLoad = 0;
  let mayStore = 1;
  let hasSideEffects = 0;
  let HasVLOp = 1;
  let HasSEWOp = 1;
}

multiclass VPseudoUSLoad {
  foreach eew = EEWList in {
    foreach lmul = MxSet<eew>.m in {
      defvar LInfo = lmul.MX;
      defvar vreg = lmul.vrclass;
      let VLMul = lmul.value, SEW=eew in {
        def "E" # eew # "_V_" # LInfo :
          VPseudoUSLoadNoMask<vreg, eew>,
          VLESched<LInfo>;
        def "E" # eew # "_V_" # LInfo # "_MASK" :
          VPseudoUSLoadMask<vreg, eew>,
          RISCVMaskedPseudo<MaskIdx=2>,
          VLESched<LInfo>;
      }
    }
  }
}

multiclass VPseudoFFLoad {
  foreach eew = EEWList in {
    foreach lmul = MxSet<eew>.m in {
      defvar LInfo = lmul.MX;
      defvar vreg = lmul.vrclass;
      let VLMul = lmul.value, SEW=eew in {
        def "E" # eew # "FF_V_" # LInfo:
          VPseudoUSLoadFFNoMask<vreg, eew>,
          VLFSched<LInfo>;
        def "E" # eew # "FF_V_" # LInfo # "_MASK":
          VPseudoUSLoadFFMask<vreg, eew>,
          RISCVMaskedPseudo<MaskIdx=2>,
          VLFSched<LInfo>;
      }
    }
  }
}

multiclass VPseudoLoadMask {
  foreach mti = AllMasks in {
    defvar mx = mti.LMul.MX;
    defvar WriteVLDM_MX = !cast<SchedWrite>("WriteVLDM_" # mx);
    let VLMul = mti.LMul.value in {
      def "_V_" # mti.BX : VPseudoUSLoadNoMask<VR, EEW=1>,
        Sched<[WriteVLDM_MX, ReadVLDX]>;
    }
  }
}

multiclass VPseudoSLoad {
  foreach eew = EEWList in {
    foreach lmul = MxSet<eew>.m in {
      defvar LInfo = lmul.MX;
      defvar vreg = lmul.vrclass;
      let VLMul = lmul.value, SEW=eew in {
        def "E" # eew # "_V_" # LInfo : VPseudoSLoadNoMask<vreg, eew>,
                                        VLSSched<eew, LInfo>;
        def "E" # eew # "_V_" # LInfo # "_MASK" :
          VPseudoSLoadMask<vreg, eew>,
          RISCVMaskedPseudo<MaskIdx=3>,
          VLSSched<eew, LInfo>;
      }
    }
  }
}

multiclass VPseudoILoad<bit Ordered> {
  foreach idxEEW = EEWList in {
    foreach dataEEW = EEWList in {
      foreach dataEMUL = MxSet<dataEEW>.m in {
        defvar dataEMULOctuple = dataEMUL.octuple;
        // Calculate emul = eew * lmul / sew
        defvar idxEMULOctuple =
          !srl(!mul(idxEEW, dataEMULOctuple), !logtwo(dataEEW));
        if !and(!ge(idxEMULOctuple, 1), !le(idxEMULOctuple, 64)) then {
          defvar DataLInfo = dataEMUL.MX;
          defvar IdxLInfo = octuple_to_str<idxEMULOctuple>.ret;
          defvar idxEMUL = !cast<LMULInfo>("V_" # IdxLInfo);
          defvar Vreg = dataEMUL.vrclass;
          defvar IdxVreg = idxEMUL.vrclass;
          defvar HasConstraint = !ne(dataEEW, idxEEW);
          defvar TypeConstraints = 
            !if(!eq(dataEEW, idxEEW), 1, !if(!gt(dataEEW, idxEEW), !if(!ge(idxEMULOctuple, 8), 3, 1), 2));
          let VLMul = dataEMUL.value in {
            def "EI" # idxEEW # "_V_" # IdxLInfo # "_" # DataLInfo :
              VPseudoILoadNoMask<Vreg, IdxVreg, idxEEW, idxEMUL.value, Ordered, HasConstraint, TypeConstraints>,
              VLXSched<dataEEW, Ordered, DataLInfo, IdxLInfo>;
            def "EI" # idxEEW # "_V_" # IdxLInfo # "_" # DataLInfo # "_MASK" :
              VPseudoILoadMask<Vreg, IdxVreg, idxEEW, idxEMUL.value, Ordered, HasConstraint, TypeConstraints>,
              RISCVMaskedPseudo<MaskIdx=3>,
              VLXSched<dataEEW, Ordered, DataLInfo, IdxLInfo>;
          }
        }
      }
    }
  }
}

multiclass VPseudoUSStore {
  foreach eew = EEWList in {
    foreach lmul = MxSet<eew>.m in {
      defvar LInfo = lmul.MX;
      defvar vreg = lmul.vrclass;
      let VLMul = lmul.value, SEW=eew in {
        def "E" # eew # "_V_" # LInfo : VPseudoUSStoreNoMask<vreg, eew>,
                                        VSESched<LInfo>;
        def "E" # eew # "_V_" # LInfo # "_MASK" : VPseudoUSStoreMask<vreg, eew>,
                                                  VSESched<LInfo>;
      }
    }
  }
}

multiclass VPseudoStoreMask {
  foreach mti = AllMasks in {
    defvar mx = mti.LMul.MX;
    defvar WriteVSTM_MX = !cast<SchedWrite>("WriteVSTM_" # mx);
    let VLMul = mti.LMul.value in {
      def "_V_" # mti.BX : VPseudoUSStoreNoMask<VR, EEW=1>,
        Sched<[WriteVSTM_MX, ReadVSTX]>;
    }
  }
}

multiclass VPseudoSStore {
  foreach eew = EEWList in {
    foreach lmul = MxSet<eew>.m in {
      defvar LInfo = lmul.MX;
      defvar vreg = lmul.vrclass;
      let VLMul = lmul.value, SEW=eew in {
        def "E" # eew # "_V_" # LInfo : VPseudoSStoreNoMask<vreg, eew>,
                                        VSSSched<eew, LInfo>;
        def "E" # eew # "_V_" # LInfo # "_MASK" : VPseudoSStoreMask<vreg, eew>,
                                                  VSSSched<eew, LInfo>;
      }
    }
  }
}

multiclass VPseudoIStore<bit Ordered> {
  foreach idxEEW = EEWList in {
    foreach dataEEW = EEWList in {
      foreach dataEMUL = MxSet<dataEEW>.m in {
        defvar dataEMULOctuple = dataEMUL.octuple;
        // Calculate emul = eew * lmul / sew
        defvar idxEMULOctuple =
          !srl(!mul(idxEEW, dataEMULOctuple), !logtwo(dataEEW));
        if !and(!ge(idxEMULOctuple, 1), !le(idxEMULOctuple, 64)) then {
          defvar DataLInfo = dataEMUL.MX;
          defvar IdxLInfo = octuple_to_str<idxEMULOctuple>.ret;
          defvar idxEMUL = !cast<LMULInfo>("V_" # IdxLInfo);
          defvar Vreg = dataEMUL.vrclass;
          defvar IdxVreg = idxEMUL.vrclass;
          let VLMul = dataEMUL.value in {
            def "EI" # idxEEW # "_V_" # IdxLInfo # "_" # DataLInfo :
              VPseudoIStoreNoMask<Vreg, IdxVreg, idxEEW, idxEMUL.value, Ordered>,
              VSXSched<dataEEW, Ordered, DataLInfo, IdxLInfo>;
            def "EI" # idxEEW # "_V_" # IdxLInfo # "_" # DataLInfo # "_MASK" :
              VPseudoIStoreMask<Vreg, IdxVreg, idxEEW, idxEMUL.value, Ordered>,
              VSXSched<dataEEW, Ordered, DataLInfo, IdxLInfo>;
          }
        }
      }
    }
  }
}

multiclass VPseudoVPOP_M {
  foreach mti = AllMasks in {
    defvar mx = mti.LMul.MX;
    let VLMul = mti.LMul.value in {
      def "_M_" # mti.BX : VPseudoUnaryNoMaskGPROut,
          SchedBinary<"WriteVMPopV", "ReadVMPopV", "ReadVMPopV", mx>;
      def "_M_" # mti.BX # "_MASK" : VPseudoUnaryMaskGPROut,
          SchedBinary<"WriteVMPopV", "ReadVMPopV", "ReadVMPopV", mx>;
    }
  }
}

multiclass VPseudoV1ST_M {
  foreach mti = AllMasks in {
    defvar mx = mti.LMul.MX;
    let VLMul = mti.LMul.value in {
      def "_M_" #mti.BX : VPseudoUnaryNoMaskGPROut,
          SchedBinary<"WriteVMFFSV", "ReadVMFFSV", "ReadVMFFSV", mx>;
      def "_M_" # mti.BX # "_MASK" : VPseudoUnaryMaskGPROut,
          SchedBinary<"WriteVMFFSV", "ReadVMFFSV", "ReadVMFFSV", mx>;
    }
  }
}

multiclass VPseudoVSFS_M {
  defvar constraint = "@earlyclobber $rd";
  foreach mti = AllMasks in {
    defvar mx = mti.LMul.MX;
    let VLMul = mti.LMul.value in {
      def "_M_" # mti.BX : VPseudoUnaryNoMaskNoPolicy<VR, VR, constraint>,
                           SchedUnary<"WriteVMSFSV", "ReadVMSFSV", mx,
                                      forceMergeOpRead=true>;
      let ForceTailAgnostic = true in
      def "_M_" # mti.BX # "_MASK" : VPseudoUnaryMask<VR, VR, constraint>,
                                     SchedUnary<"WriteVMSFSV", "ReadVMSFSV", mx,
                                                forceMergeOpRead=true>;
    }
  }
}

multiclass VPseudoVID_V {
  foreach m = MxList in {
    defvar mx = m.MX;
    let VLMul = m.value in {
      def "_V_" # mx : VPseudoNullaryNoMask<m.vrclass>,
                         SchedNullary<"WriteVIdxV", mx, forceMergeOpRead=true>;
      def "_V_" # mx # "_MASK" : VPseudoNullaryMask<m.vrclass>,
                                   RISCVMaskedPseudo<MaskIdx=1>,
                                   SchedNullary<"WriteVIdxV", mx,
                                                forceMergeOpRead=true>;
    }
  }
}

multiclass VPseudoNullaryPseudoM <string BaseInst> {
  foreach mti = AllMasks in {
    let VLMul = mti.LMul.value in {
      def "_M_" # mti.BX : VPseudoNullaryPseudoM<BaseInst # "_MM">,
        SchedBinary<"WriteVMALUV", "ReadVMALUV", "ReadVMALUV", mti.LMul.MX>;
    }
  }
}

multiclass VPseudoVIOTA_M {
  defvar constraint = "@earlyclobber $rd";
  foreach m = MxList in {
    defvar mx = m.MX;
    let VLMul = m.value in {
      def "_" # mx : VPseudoUnaryNoMask<m.vrclass, VR, constraint>,
                       SchedUnary<"WriteVIotaV", "ReadVIotaV", mx,
                                  forceMergeOpRead=true>;
      def "_" # mx # "_MASK" : VPseudoUnaryMask<m.vrclass, VR, constraint>,
                                 RISCVMaskedPseudo<MaskIdx=2, ActiveAffectsRes=true>,
                                 SchedUnary<"WriteVIotaV", "ReadVIotaV", mx,
                                            forceMergeOpRead=true>;
    }
  }
}

multiclass VPseudoVCPR_V {
  foreach m = MxList in {
    defvar mx = m.MX;
    defvar sews = SchedSEWSet<mx>.val;
    let VLMul = m.value in
      foreach e = sews in {
        defvar suffix = "_" # m.MX # "_E" # e;
        let SEW = e in
        def _VM # suffix
          : VPseudoUnaryAnyMask<m.vrclass, m.vrclass>,
            SchedBinary<"WriteVCompressV", "ReadVCompressV", "ReadVCompressV",
                        mx, e>;
      }
  }
}

multiclass VPseudoBinary<VReg RetClass,
                         VReg Op1Class,
                         DAGOperand Op2Class,
                         LMULInfo MInfo,
                         string Constraint = "",
                         int sew = 0,
                         int TargetConstraintType = 1,
                         bit Commutable = 0> {
  let VLMul = MInfo.value, SEW=sew, isCommutable = Commutable in {
    defvar suffix = !if(sew, "_" # MInfo.MX # "_E" # sew, "_" # MInfo.MX);
    def suffix : VPseudoBinaryNoMaskPolicy<RetClass, Op1Class, Op2Class,
                                           Constraint, TargetConstraintType>;
    def suffix # "_MASK" : VPseudoBinaryMaskPolicy<RetClass, Op1Class, Op2Class,
                                                   Constraint, TargetConstraintType>,
                           RISCVMaskedPseudo<MaskIdx=3>;
  }
}

multiclass VPseudoBinaryRoundingMode<VReg RetClass,
                                     VReg Op1Class,
                                     DAGOperand Op2Class,
                                     LMULInfo MInfo,
                                     string Constraint = "",
                                     int sew = 0,
                                     int UsesVXRM = 1,
                                     int TargetConstraintType = 1,
                                     bit Commutable = 0> {
  let VLMul = MInfo.value, SEW=sew, isCommutable = Commutable in {
    defvar suffix = !if(sew, "_" # MInfo.MX # "_E" # sew, "_" # MInfo.MX);
    def suffix : VPseudoBinaryNoMaskRoundingMode<RetClass, Op1Class, Op2Class,
                                                 Constraint, UsesVXRM,
                                                 TargetConstraintType>;
    def suffix # "_MASK" : VPseudoBinaryMaskPolicyRoundingMode<RetClass,
                                                               Op1Class,
                                                               Op2Class,
                                                               Constraint,
                                                               UsesVXRM,
                                                               TargetConstraintType>,
                           RISCVMaskedPseudo<MaskIdx=3>;
  }
}


multiclass VPseudoBinaryM<VReg RetClass,
                          VReg Op1Class,
                          DAGOperand Op2Class,
                          LMULInfo MInfo,
                          string Constraint = "",
                          int TargetConstraintType = 1,
                          bit Commutable = 0> {
  let VLMul = MInfo.value, isCommutable = Commutable in {
    def "_" # MInfo.MX : VPseudoBinaryNoMask<RetClass, Op1Class, Op2Class,
                                             Constraint, TargetConstraintType>;
    let ForceTailAgnostic = true in
    def "_" # MInfo.MX # "_MASK" : VPseudoBinaryMOutMask<RetClass, Op1Class,
                                                         Op2Class, Constraint, TargetConstraintType>,
                                   RISCVMaskedPseudo<MaskIdx=3>;
  }
}

multiclass VPseudoBinaryEmul<VReg RetClass,
                             VReg Op1Class,
                             DAGOperand Op2Class,
                             LMULInfo lmul,
                             LMULInfo emul,
                             string Constraint = "",
                             int sew> {
  let VLMul = lmul.value, SEW=sew in {
    defvar suffix = !if(sew, "_" # lmul.MX # "_E" # sew, "_" # lmul.MX);
    def suffix # "_" # emul.MX : VPseudoBinaryNoMaskPolicy<RetClass, Op1Class, Op2Class,
                                                           Constraint>;
    def suffix # "_" # emul.MX # "_MASK" : VPseudoBinaryMaskPolicy<RetClass, Op1Class, Op2Class,
                                                                          Constraint>,
                                                  RISCVMaskedPseudo<MaskIdx=3>;
  }
}

multiclass VPseudoTiedBinary<VReg RetClass,
                             DAGOperand Op2Class,
                             LMULInfo MInfo,
                             string Constraint = "",
                             int TargetConstraintType = 1> {
  let VLMul = MInfo.value in {
    def "_" # MInfo.MX # "_TIED": VPseudoTiedBinaryNoMask<RetClass, Op2Class,
                                                          Constraint, TargetConstraintType>;
    def "_" # MInfo.MX # "_MASK_TIED" : VPseudoTiedBinaryMask<RetClass, Op2Class,
                                                         Constraint, TargetConstraintType>,
                                        RISCVMaskedPseudo<MaskIdx=2>;
  }
}

multiclass VPseudoTiedBinaryRoundingMode<VReg RetClass,
                                         DAGOperand Op2Class,
                                         LMULInfo MInfo,
                                         string Constraint = "",
                                         int sew = 0,
                                         int TargetConstraintType = 1> {
    defvar suffix = !if(sew, "_" # MInfo.MX # "_E" # sew, "_" # MInfo.MX);
    let VLMul = MInfo.value in {
    def suffix # "_TIED":
      VPseudoTiedBinaryNoMaskRoundingMode<RetClass, Op2Class, Constraint, TargetConstraintType>;
    def suffix # "_MASK_TIED" :
      VPseudoTiedBinaryMaskRoundingMode<RetClass, Op2Class, Constraint, TargetConstraintType>,
      RISCVMaskedPseudo<MaskIdx=2>;
  }
}


multiclass VPseudoBinaryV_VV<LMULInfo m, string Constraint = "", int sew = 0, bit Commutable = 0> {
  defm _VV : VPseudoBinary<m.vrclass, m.vrclass, m.vrclass, m, Constraint, sew, Commutable=Commutable>;
}

multiclass VPseudoBinaryV_VV_RM<LMULInfo m, string Constraint = "", bit Commutable = 0> {
  defm _VV : VPseudoBinaryRoundingMode<m.vrclass, m.vrclass, m.vrclass, m, Constraint,
                                       Commutable=Commutable>;
}

multiclass VPseudoBinaryFV_VV_RM<LMULInfo m, int sew> {
  defm _VV : VPseudoBinaryRoundingMode<m.vrclass, m.vrclass, m.vrclass, m,
                                       "", sew, UsesVXRM=0>;
}

multiclass VPseudoVGTR_EI16_VV {
  defvar constraint = "@earlyclobber $rd";
  foreach m = MxList in {
    defvar mx = m.MX;
    foreach sew = EEWList in {
      defvar dataEMULOctuple = m.octuple;
      // emul = lmul * 16 / sew
      defvar idxEMULOctuple = !srl(!mul(dataEMULOctuple, 16), !logtwo(sew));
      if !and(!ge(idxEMULOctuple, 1), !le(idxEMULOctuple, 64)) then {
        defvar emulMX = octuple_to_str<idxEMULOctuple>.ret;
        defvar emul = !cast<LMULInfo>("V_" # emulMX);
        defvar sews = SchedSEWSet<mx>.val;
        foreach e = sews in {
          defm _VV
              : VPseudoBinaryEmul<m.vrclass, m.vrclass, emul.vrclass, m, emul,
                                  constraint, e>,
                SchedBinary<"WriteVRGatherEI16VV", "ReadVRGatherEI16VV_data",
                            "ReadVRGatherEI16VV_index", mx, e, forceMergeOpRead=true>;
        }
      }
    }
  }
}

multiclass VPseudoBinaryV_VX<LMULInfo m, string Constraint = "", int sew = 0> {
  defm "_VX" : VPseudoBinary<m.vrclass, m.vrclass, GPR, m, Constraint, sew>;
}

multiclass VPseudoBinaryV_VX_RM<LMULInfo m, string Constraint = ""> {
  defm "_VX" : VPseudoBinaryRoundingMode<m.vrclass, m.vrclass, GPR, m, Constraint>;
}

multiclass VPseudoVSLD1_VX<string Constraint = ""> {
  foreach m = MxList in {
    defm "_VX" : VPseudoBinary<m.vrclass, m.vrclass, GPR, m, Constraint>,
                 SchedBinary<"WriteVISlide1X", "ReadVISlideV", "ReadVISlideX",
                             m.MX, forceMergeOpRead=true>;
  }
}

multiclass VPseudoBinaryV_VF<LMULInfo m, FPR_Info f, int sew> {
  defm "_V" # f.FX : VPseudoBinary<m.vrclass, m.vrclass,
                                   f.fprclass, m, "", sew>;
}

multiclass VPseudoBinaryV_VF_RM<LMULInfo m, FPR_Info f, int sew> {
  defm "_V" # f.FX : VPseudoBinaryRoundingMode<m.vrclass, m.vrclass,
                                               f.fprclass, m, "", sew,
                                               UsesVXRM=0>;
}

multiclass VPseudoVSLD1_VF<string Constraint = ""> {
  foreach f = FPList in {
    foreach m = f.MxList in {
      defm "_V" #f.FX
          : VPseudoBinary<m.vrclass, m.vrclass, f.fprclass, m, Constraint>,
            SchedBinary<"WriteVFSlide1F", "ReadVFSlideV", "ReadVFSlideF", m.MX,
                      forceMergeOpRead=true>;
    }
  }
}

multiclass VPseudoBinaryV_VI<Operand ImmType, LMULInfo m, string Constraint = ""> {
  defm _VI : VPseudoBinary<m.vrclass, m.vrclass, ImmType, m, Constraint>;
}

multiclass VPseudoBinaryV_VI_RM<Operand ImmType, LMULInfo m, string Constraint = ""> {
  defm _VI : VPseudoBinaryRoundingMode<m.vrclass, m.vrclass, ImmType, m, Constraint>;
}

multiclass VPseudoVALU_MM<bit Commutable = 0> {
  foreach m = MxList in {
    defvar mx = m.MX;
    let VLMul = m.value, isCommutable = Commutable in {
      def "_MM_" # mx : VPseudoBinaryNoMask<VR, VR, VR, "">,
                        SchedBinary<"WriteVMALUV", "ReadVMALUV", "ReadVMALUV", mx>;
    }
  }
}

// We use earlyclobber here due to
// * The destination EEW is smaller than the source EEW and the overlap is
//   in the lowest-numbered part of the source register group is legal.
//   Otherwise, it is illegal.
// * The destination EEW is greater than the source EEW, the source EMUL is
//   at least 1, and the overlap is in the highest-numbered part of the
//   destination register group is legal. Otherwise, it is illegal.
multiclass VPseudoBinaryW_VV<LMULInfo m, bit Commutable = 0> {
  defm _VV : VPseudoBinary<m.wvrclass, m.vrclass, m.vrclass, m,
                           "@earlyclobber $rd", TargetConstraintType=3,
                           Commutable=Commutable>;
}

multiclass VPseudoBinaryW_VV_RM<LMULInfo m, int sew> {
  defm _VV : VPseudoBinaryRoundingMode<m.wvrclass, m.vrclass, m.vrclass, m,
                                      "@earlyclobber $rd", sew, UsesVXRM=0,
                                      TargetConstraintType=3>;
}

multiclass VPseudoBinaryW_VX<LMULInfo m> {
  defm "_VX" : VPseudoBinary<m.wvrclass, m.vrclass, GPR, m,
                             "@earlyclobber $rd", TargetConstraintType=3>;
}

multiclass VPseudoBinaryW_VI<Operand ImmType, LMULInfo m> {
  defm "_VI" : VPseudoBinary<m.wvrclass, m.vrclass, ImmType, m,
                             "@earlyclobber $rd", TargetConstraintType=3>;
}

multiclass VPseudoBinaryW_VF_RM<LMULInfo m, FPR_Info f, int sew> {
  defm "_V" # f.FX : VPseudoBinaryRoundingMode<m.wvrclass, m.vrclass,
                                               f.fprclass, m,
                                               "@earlyclobber $rd", sew,
                                               UsesVXRM=0,
                                               TargetConstraintType=3>;
}

multiclass VPseudoBinaryW_WV<LMULInfo m> {
  defm _WV : VPseudoBinary<m.wvrclass, m.wvrclass, m.vrclass, m,
                           "@earlyclobber $rd", TargetConstraintType=3>;
  defm _WV : VPseudoTiedBinary<m.wvrclass, m.vrclass, m,
                               "@earlyclobber $rd", TargetConstraintType=3>;
}

multiclass VPseudoBinaryW_WV_RM<LMULInfo m, int sew> {
  defm _WV : VPseudoBinaryRoundingMode<m.wvrclass, m.wvrclass, m.vrclass, m,
                                       "@earlyclobber $rd", sew, UsesVXRM = 0,
                                       TargetConstraintType = 3>;
  defm _WV : VPseudoTiedBinaryRoundingMode<m.wvrclass, m.vrclass, m,
                                           "@earlyclobber $rd", sew,
                                           TargetConstraintType = 3>;
}

multiclass VPseudoBinaryW_WX<LMULInfo m> {
  defm "_WX" : VPseudoBinary<m.wvrclass, m.wvrclass, GPR, m, /*Constraint*/ "", TargetConstraintType=3>;
}

multiclass VPseudoBinaryW_WF_RM<LMULInfo m, FPR_Info f, int sew> {
  defm "_W" # f.FX : VPseudoBinaryRoundingMode<m.wvrclass, m.wvrclass,
                                               f.fprclass, m,
                                               Constraint="",
                                               sew=sew,
                                               UsesVXRM=0,
                                               TargetConstraintType=3>;
}

// Narrowing instructions like vnsrl/vnsra/vnclip(u) don't need @earlyclobber
// if the source and destination have an LMUL<=1. This matches this overlap
// exception from the spec.
// "The destination EEW is smaller than the source EEW and the overlap is in the
//  lowest-numbered part of the source register group."
multiclass VPseudoBinaryV_WV<LMULInfo m> {
  defm _WV : VPseudoBinary<m.vrclass, m.wvrclass, m.vrclass, m,
                           !if(!ge(m.octuple, 8), "@earlyclobber $rd", ""),
                           TargetConstraintType=2>;
}

multiclass VPseudoBinaryV_WV_RM<LMULInfo m> {
  defm _WV : VPseudoBinaryRoundingMode<m.vrclass, m.wvrclass, m.vrclass, m,
                                       !if(!ge(m.octuple, 8),
                                       "@earlyclobber $rd", ""),
                                       TargetConstraintType=2>;
}

multiclass VPseudoBinaryV_WX<LMULInfo m> {
  defm _WX : VPseudoBinary<m.vrclass, m.wvrclass, GPR, m,
                           !if(!ge(m.octuple, 8), "@earlyclobber $rd", ""),
                           TargetConstraintType=2>;
}

multiclass VPseudoBinaryV_WX_RM<LMULInfo m> {
  defm _WX : VPseudoBinaryRoundingMode<m.vrclass, m.wvrclass, GPR, m,
                                       !if(!ge(m.octuple, 8),
                                       "@earlyclobber $rd", ""),
                                       TargetConstraintType=2>;
}

multiclass VPseudoBinaryV_WI<LMULInfo m> {
  defm _WI : VPseudoBinary<m.vrclass, m.wvrclass, uimm5, m,
                           !if(!ge(m.octuple, 8), "@earlyclobber $rd", ""),
                           TargetConstraintType=2>;
}

multiclass VPseudoBinaryV_WI_RM<LMULInfo m> {
  defm _WI : VPseudoBinaryRoundingMode<m.vrclass, m.wvrclass, uimm5, m,
                                       !if(!ge(m.octuple, 8),
                                       "@earlyclobber $rd", ""),
                                       TargetConstraintType=2>;
}

// For vadc and vsbc, the instruction encoding is reserved if the destination
// vector register is v0.
// For vadc and vsbc, CarryIn == 1 and CarryOut == 0
multiclass VPseudoBinaryV_VM<LMULInfo m, bit CarryOut = 0, bit CarryIn = 1,
                             string Constraint = "",
                             bit Commutable = 0,
                             int TargetConstraintType = 1> {
  let isCommutable = Commutable in
  def "_VV" # !if(CarryIn, "M", "") # "_" # m.MX :
    VPseudoBinaryCarry<!if(CarryOut, VR,
                       !if(!and(CarryIn, !not(CarryOut)),
                           GetVRegNoV0<m.vrclass>.R, m.vrclass)),
                       m.vrclass, m.vrclass, m, CarryIn, Constraint, TargetConstraintType>;
}

multiclass VPseudoTiedBinaryV_VM<LMULInfo m, bit Commutable = 0> {
  let isCommutable = Commutable in
  def "_VVM" # "_" # m.MX:
    VPseudoTiedBinaryCarryIn<GetVRegNoV0<m.vrclass>.R,
                             m.vrclass, m.vrclass, m>;
}

multiclass VPseudoBinaryV_XM<LMULInfo m, bit CarryOut = 0, bit CarryIn = 1,
                             string Constraint = "", int TargetConstraintType = 1> {
  def "_VX" # !if(CarryIn, "M", "") # "_" # m.MX :
    VPseudoBinaryCarry<!if(CarryOut, VR,
                       !if(!and(CarryIn, !not(CarryOut)),
                           GetVRegNoV0<m.vrclass>.R, m.vrclass)),
                       m.vrclass, GPR, m, CarryIn, Constraint, TargetConstraintType>;
}

multiclass VPseudoTiedBinaryV_XM<LMULInfo m> {
  def "_VXM" # "_" # m.MX:
    VPseudoTiedBinaryCarryIn<GetVRegNoV0<m.vrclass>.R,
                             m.vrclass, GPR, m>;
}

multiclass VPseudoVMRG_FM {
  foreach f = FPList in {
    foreach m = f.MxList in {
      defvar mx = m.MX;
      def "_V" # f.FX # "M_" # mx
          : VPseudoTiedBinaryCarryIn<GetVRegNoV0<m.vrclass>.R, m.vrclass,
                                     f.fprclass, m>,
          SchedBinary<"WriteVFMergeV", "ReadVFMergeV", "ReadVFMergeF", mx,
                      forceMasked=1, forceMergeOpRead=true>;
    }
  }
}

multiclass VPseudoBinaryV_IM<LMULInfo m, bit CarryOut = 0, bit CarryIn = 1,
                             string Constraint = "", int TargetConstraintType = 1> {
  def "_VI" # !if(CarryIn, "M", "") # "_" # m.MX :
    VPseudoBinaryCarry<!if(CarryOut, VR,
                       !if(!and(CarryIn, !not(CarryOut)),
                           GetVRegNoV0<m.vrclass>.R, m.vrclass)),
                       m.vrclass, simm5, m, CarryIn, Constraint, TargetConstraintType>;
}

multiclass VPseudoTiedBinaryV_IM<LMULInfo m> {
  def "_VIM" # "_" # m.MX:
    VPseudoTiedBinaryCarryIn<GetVRegNoV0<m.vrclass>.R,
                             m.vrclass, simm5, m>;
}

multiclass VPseudoUnaryVMV_V_X_I {
  foreach m = MxList in {
    let VLMul = m.value in {
      defvar mx = m.MX;
      let VLMul = m.value in {
        def "_V_" # mx : VPseudoUnaryNoMask<m.vrclass, m.vrclass>,
                         SchedUnary<"WriteVIMovV", "ReadVIMovV", mx,
                                    forceMergeOpRead=true>;
        def "_X_" # mx : VPseudoUnaryNoMask<m.vrclass, GPR>,
                         SchedUnary<"WriteVIMovX", "ReadVIMovX", mx,
                                    forceMergeOpRead=true>;
        def "_I_" # mx : VPseudoUnaryNoMask<m.vrclass, simm5>,
                         SchedNullary<"WriteVIMovI", mx,
                                      forceMergeOpRead=true>;
      }
    }
  }
}

multiclass VPseudoVMV_F {
  foreach f = FPList in {
    foreach m = f.MxList in {
      defvar mx = m.MX;
      let VLMul = m.value in {
        def "_" # f.FX # "_" # mx :
          VPseudoUnaryNoMask<m.vrclass, f.fprclass>,
          SchedUnary<"WriteVFMovV", "ReadVFMovF", mx, forceMergeOpRead=true>;
      }
    }
  }
}

multiclass VPseudoVCLS_V {
  foreach m = MxListF in {
    defvar mx = m.MX;
    let VLMul = m.value in {
      def "_V_" # mx : VPseudoUnaryNoMask<m.vrclass, m.vrclass>,
                       SchedUnary<"WriteVFClassV", "ReadVFClassV", mx,
                                  forceMergeOpRead=true>;
      def "_V_" # mx # "_MASK" : VPseudoUnaryMask<m.vrclass, m.vrclass>,
                                 RISCVMaskedPseudo<MaskIdx=2>,
                                 SchedUnary<"WriteVFClassV", "ReadVFClassV", mx,
                                            forceMergeOpRead=true>;
    }
  }
}

multiclass VPseudoVSQR_V_RM {
  foreach m = MxListF in {
    defvar mx = m.MX;
    defvar sews = SchedSEWSet<m.MX, isF=1>.val;

    let VLMul = m.value in
      foreach e = sews in {
        defvar suffix = "_" # mx # "_E" # e;
        let SEW = e in {
          def "_V" # suffix : VPseudoUnaryNoMaskRoundingMode<m.vrclass, m.vrclass>,
                              SchedUnary<"WriteVFSqrtV", "ReadVFSqrtV", mx, e,
                                         forceMergeOpRead=true>;
          def "_V" #suffix # "_MASK"
              : VPseudoUnaryMaskRoundingMode<m.vrclass, m.vrclass>,
                RISCVMaskedPseudo<MaskIdx = 2>,
                SchedUnary<"WriteVFSqrtV", "ReadVFSqrtV", mx, e,
                           forceMergeOpRead=true>;
        }
      }
  }
}

multiclass VPseudoVRCP_V {
  foreach m = MxListF in {
    defvar mx = m.MX;
    foreach e = SchedSEWSet<mx, isF=1>.val in {
      let VLMul = m.value in {
        def "_V_" # mx # "_E" # e
            : VPseudoUnaryNoMask<m.vrclass, m.vrclass>,
              SchedUnary<"WriteVFRecpV", "ReadVFRecpV", mx, e, forceMergeOpRead=true>;
        def "_V_" # mx # "_E" # e # "_MASK"
            : VPseudoUnaryMask<m.vrclass, m.vrclass>,
              RISCVMaskedPseudo<MaskIdx = 2>,
              SchedUnary<"WriteVFRecpV", "ReadVFRecpV", mx, e, forceMergeOpRead=true>;
      }
    }
  }
}

multiclass VPseudoVRCP_V_RM {
  foreach m = MxListF in {
    defvar mx = m.MX;
    foreach e = SchedSEWSet<mx, isF=1>.val in {
      let VLMul = m.value in {
        def "_V_" # mx # "_E" # e
            : VPseudoUnaryNoMaskRoundingMode<m.vrclass, m.vrclass>,
              SchedUnary<"WriteVFRecpV", "ReadVFRecpV", mx, e, forceMergeOpRead=true>;
        def "_V_" # mx # "_E" # e # "_MASK"
            : VPseudoUnaryMaskRoundingMode<m.vrclass, m.vrclass>,
              RISCVMaskedPseudo<MaskIdx = 2>,
              SchedUnary<"WriteVFRecpV", "ReadVFRecpV", mx, e, forceMergeOpRead=true>;
      }
    }
  }
}

multiclass PseudoVEXT_VF2 {
  defvar constraints = "@earlyclobber $rd";
  foreach m = MxListVF2 in {
    defvar mx = m.MX;
    defvar CurrTypeConstraints = !if(!or(!eq(mx, "MF4"), !eq(mx, "MF2"), !eq(mx, "M1")), 1, 3);
    let VLMul = m.value in {
      def "_" # mx : VPseudoUnaryNoMask<m.vrclass, m.f2vrclass, constraints, CurrTypeConstraints>,
                     SchedUnary<"WriteVExtV", "ReadVExtV", mx, forceMergeOpRead=true>;
      def "_" # mx # "_MASK" :
        VPseudoUnaryMask<m.vrclass, m.f2vrclass, constraints, CurrTypeConstraints>,
        RISCVMaskedPseudo<MaskIdx=2>,
        SchedUnary<"WriteVExtV", "ReadVExtV", mx, forceMergeOpRead=true>;
    }
  }
}

multiclass PseudoVEXT_VF4 {
  defvar constraints = "@earlyclobber $rd";
  foreach m = MxListVF4 in {
    defvar mx = m.MX;
    defvar CurrTypeConstraints = !if(!or(!eq(mx, "MF2"), !eq(mx, "M1"), !eq(mx, "M2")), 1, 3);
    let VLMul = m.value in {
      def "_" # mx : VPseudoUnaryNoMask<m.vrclass, m.f4vrclass, constraints, CurrTypeConstraints>,
                     SchedUnary<"WriteVExtV", "ReadVExtV", mx, forceMergeOpRead=true>;
      def "_" # mx # "_MASK" :
        VPseudoUnaryMask<m.vrclass, m.f4vrclass, constraints, CurrTypeConstraints>,
        RISCVMaskedPseudo<MaskIdx=2>,
        SchedUnary<"WriteVExtV", "ReadVExtV", mx, forceMergeOpRead=true>;
    }
  }
}

multiclass PseudoVEXT_VF8 {
  defvar constraints = "@earlyclobber $rd";
  foreach m = MxListVF8 in {
    defvar mx = m.MX;
    defvar CurrTypeConstraints = !if(!or(!eq(mx, "M1"), !eq(mx, "M2"), !eq(mx, "M4")), 1, 3);
    let VLMul = m.value in {
      def "_" # mx : VPseudoUnaryNoMask<m.vrclass, m.f8vrclass, constraints, CurrTypeConstraints>,
                     SchedUnary<"WriteVExtV", "ReadVExtV", mx, forceMergeOpRead=true>;
      def "_" # mx # "_MASK" :
        VPseudoUnaryMask<m.vrclass, m.f8vrclass, constraints, CurrTypeConstraints>,
        RISCVMaskedPseudo<MaskIdx=2>,
        SchedUnary<"WriteVExtV", "ReadVExtV", mx, forceMergeOpRead=true>;
    }
  }
}

// The destination EEW is 1 since "For the purposes of register group overlap
// constraints, mask elements have EEW=1."
// The source EEW is 8, 16, 32, or 64.
// When the destination EEW is different from source EEW, we need to use
// @earlyclobber to avoid the overlap between destination and source registers.
// We don't need @earlyclobber for LMUL<=1 since that matches this overlap
// exception from the spec
// "The destination EEW is smaller than the source EEW and the overlap is in the
//  lowest-numbered part of the source register group".
// With LMUL<=1 the source and dest occupy a single register so any overlap
// is in the lowest-numbered part.
multiclass VPseudoBinaryM_VV<LMULInfo m, int TargetConstraintType = 1,
                             bit Commutable = 0> {
  defm _VV : VPseudoBinaryM<VR, m.vrclass, m.vrclass, m,
                            !if(!ge(m.octuple, 16), "@earlyclobber $rd", ""),
                            TargetConstraintType, Commutable=Commutable>;
}

multiclass VPseudoBinaryM_VX<LMULInfo m, int TargetConstraintType = 1> {
  defm "_VX" :
    VPseudoBinaryM<VR, m.vrclass, GPR, m,
                   !if(!ge(m.octuple, 16), "@earlyclobber $rd", ""), TargetConstraintType>;
}

multiclass VPseudoBinaryM_VF<LMULInfo m, FPR_Info f, int TargetConstraintType = 1> {
  defm "_V" # f.FX :
    VPseudoBinaryM<VR, m.vrclass, f.fprclass, m,
                   !if(!ge(m.octuple, 16), "@earlyclobber $rd", ""), TargetConstraintType>;
}

multiclass VPseudoBinaryM_VI<LMULInfo m, int TargetConstraintType = 1> {
  defm _VI : VPseudoBinaryM<VR, m.vrclass, simm5, m,
                            !if(!ge(m.octuple, 16), "@earlyclobber $rd", ""), TargetConstraintType>;
}

multiclass VPseudoVGTR_VV_VX_VI {
  defvar constraint = "@earlyclobber $rd";
  foreach m = MxList in {
    defvar mx = m.MX;
    defm "" : VPseudoBinaryV_VX<m, constraint>,
              SchedBinary<"WriteVRGatherVX", "ReadVRGatherVX_data",
                          "ReadVRGatherVX_index", mx, forceMergeOpRead=true>;
    defm "" : VPseudoBinaryV_VI<uimm5, m, constraint>,
              SchedUnary<"WriteVRGatherVI", "ReadVRGatherVI_data", mx,
                         forceMergeOpRead=true>;

    defvar sews = SchedSEWSet<mx>.val;
    foreach e = sews in {
      defm "" : VPseudoBinaryV_VV<m, constraint, e>,
                SchedBinary<"WriteVRGatherVV", "ReadVRGatherVV_data",
                              "ReadVRGatherVV_index", mx, e, forceMergeOpRead=true>;
    }
  }
}

multiclass VPseudoVSALU_VV_VX_VI<bit Commutable = 0> {
  foreach m = MxList in {
    defvar mx = m.MX;
    defm "" : VPseudoBinaryV_VV<m, Commutable=Commutable>,
              SchedBinary<"WriteVSALUV", "ReadVSALUV", "ReadVSALUX", mx,
                          forceMergeOpRead=true>;
    defm "" : VPseudoBinaryV_VX<m>,
              SchedBinary<"WriteVSALUX", "ReadVSALUV", "ReadVSALUX", mx,
                          forceMergeOpRead=true>;
    defm "" : VPseudoBinaryV_VI<simm5, m>,
              SchedUnary<"WriteVSALUI", "ReadVSALUV", mx, forceMergeOpRead=true>;
  }
}


multiclass VPseudoVSHT_VV_VX_VI {
  foreach m = MxList in {
    defvar mx = m.MX;
    defm "" : VPseudoBinaryV_VV<m>,
              SchedBinary<"WriteVShiftV", "ReadVShiftV", "ReadVShiftV", mx,
                          forceMergeOpRead=true>;
    defm "" : VPseudoBinaryV_VX<m>,
              SchedBinary<"WriteVShiftX", "ReadVShiftV", "ReadVShiftX", mx,
                          forceMergeOpRead=true>;
    defm "" : VPseudoBinaryV_VI<uimm5, m>,
              SchedUnary<"WriteVShiftI", "ReadVShiftV", mx, forceMergeOpRead=true>;
  }
}

multiclass VPseudoVSSHT_VV_VX_VI_RM {
  foreach m = MxList in {
    defvar mx = m.MX;
    defm "" : VPseudoBinaryV_VV_RM<m>,
              SchedBinary<"WriteVSShiftV", "ReadVSShiftV", "ReadVSShiftV", mx,
                          forceMergeOpRead=true>;
    defm "" : VPseudoBinaryV_VX_RM<m>,
              SchedBinary<"WriteVSShiftX", "ReadVSShiftV", "ReadVSShiftX", mx,
                          forceMergeOpRead=true>;
    defm "" : VPseudoBinaryV_VI_RM<uimm5, m>,
              SchedUnary<"WriteVSShiftI", "ReadVSShiftV", mx, forceMergeOpRead=true>;
  }
}

multiclass VPseudoVALU_VV_VX_VI<bit Commutable = 0> {
  foreach m = MxList in {
    defvar mx = m.MX;
    defm "" : VPseudoBinaryV_VV<m, Commutable=Commutable>,
            SchedBinary<"WriteVIALUV", "ReadVIALUV", "ReadVIALUV", mx,
                        forceMergeOpRead=true>;
    defm "" : VPseudoBinaryV_VX<m>,
            SchedBinary<"WriteVIALUX", "ReadVIALUV", "ReadVIALUX", mx,
                        forceMergeOpRead=true>;
    defm "" : VPseudoBinaryV_VI<simm5, m>,
            SchedUnary<"WriteVIALUI", "ReadVIALUV", mx, forceMergeOpRead=true>;
  }
}

multiclass VPseudoVSALU_VV_VX {
  foreach m = MxList in {
    defvar mx = m.MX;
    defm "" : VPseudoBinaryV_VV<m>,
              SchedBinary<"WriteVSALUV", "ReadVSALUV", "ReadVSALUV", mx,
                          forceMergeOpRead=true>;
    defm "" : VPseudoBinaryV_VX<m>,
              SchedBinary<"WriteVSALUX", "ReadVSALUV", "ReadVSALUX", mx,
                          forceMergeOpRead=true>;
  }
}

multiclass VPseudoVSMUL_VV_VX_RM {
  foreach m = MxList in {
    defvar mx = m.MX;
    defm "" : VPseudoBinaryV_VV_RM<m, Commutable=1>,
              SchedBinary<"WriteVSMulV", "ReadVSMulV", "ReadVSMulV", mx,
                          forceMergeOpRead=true>;
    defm "" : VPseudoBinaryV_VX_RM<m>,
              SchedBinary<"WriteVSMulX", "ReadVSMulV", "ReadVSMulX", mx,
                          forceMergeOpRead=true>;
  }
}

multiclass VPseudoVAALU_VV_VX_RM<bit Commutable = 0> {
  foreach m = MxList in {
    defvar mx = m.MX;
    defm "" : VPseudoBinaryV_VV_RM<m, Commutable=Commutable>,
              SchedBinary<"WriteVAALUV", "ReadVAALUV", "ReadVAALUV", mx,
                          forceMergeOpRead=true>;
    defm "" : VPseudoBinaryV_VX_RM<m>,
              SchedBinary<"WriteVAALUX", "ReadVAALUV", "ReadVAALUX", mx,
                          forceMergeOpRead=true>;
  }
}

multiclass VPseudoVMINMAX_VV_VX {
  foreach m = MxList in {
    defvar mx = m.MX;
    defm "" : VPseudoBinaryV_VV<m, Commutable=1>,
              SchedBinary<"WriteVIMinMaxV", "ReadVIMinMaxV", "ReadVIMinMaxV", mx>;
    defm "" : VPseudoBinaryV_VX<m>,
              SchedBinary<"WriteVIMinMaxX", "ReadVIMinMaxV", "ReadVIMinMaxX", mx>;
  }
}

multiclass VPseudoVMUL_VV_VX<bit Commutable = 0> {
  foreach m = MxList in {
    defvar mx = m.MX;
    defm "" : VPseudoBinaryV_VV<m, Commutable=Commutable>,
              SchedBinary<"WriteVIMulV", "ReadVIMulV", "ReadVIMulV", mx>;
    defm "" : VPseudoBinaryV_VX<m>,
              SchedBinary<"WriteVIMulX", "ReadVIMulV", "ReadVIMulX", mx>;
  }
}

multiclass VPseudoVDIV_VV_VX {
  foreach m = MxList in {
    defvar mx = m.MX;
    defvar sews = SchedSEWSet<mx>.val;
    foreach e = sews in {
      defm "" : VPseudoBinaryV_VV<m, "", e>,
                SchedBinary<"WriteVIDivV", "ReadVIDivV", "ReadVIDivV", mx, e>;
      defm "" : VPseudoBinaryV_VX<m, "", e>,
                SchedBinary<"WriteVIDivX", "ReadVIDivV", "ReadVIDivX", mx, e>;
    }
  }
}

multiclass VPseudoVFMUL_VV_VF_RM {
  foreach m = MxListF in {
    foreach e = SchedSEWSet<m.MX, isF=1>.val in
      defm "" : VPseudoBinaryFV_VV_RM<m, e>,
                SchedBinary<"WriteVFMulV", "ReadVFMulV", "ReadVFMulV", m.MX, e,
                            forceMergeOpRead=true>;
  }

  foreach f = FPList in {
    foreach m = f.MxList in {
      defm "" : VPseudoBinaryV_VF_RM<m, f, f.SEW>,
                SchedBinary<"WriteVFMulF", "ReadVFMulV", "ReadVFMulF", m.MX,
                            f.SEW, forceMergeOpRead=true>;
    }
  }
}

multiclass VPseudoVFDIV_VV_VF_RM {
  foreach m = MxListF in {
    defvar mx = m.MX;
    defvar sews = SchedSEWSet<mx, isF=1>.val;
    foreach e = sews in {
      defm "" : VPseudoBinaryFV_VV_RM<m, e>,
                SchedBinary<"WriteVFDivV", "ReadVFDivV", "ReadVFDivV", mx, e,
                            forceMergeOpRead=true>;
    }
  }

  foreach f = FPList in {
    foreach m = f.MxList in {
      defm "" : VPseudoBinaryV_VF_RM<m, f, f.SEW>,
                SchedBinary<"WriteVFDivF", "ReadVFDivV", "ReadVFDivF", m.MX, f.SEW,
                            forceMergeOpRead=true>;
    }
  }
}

multiclass VPseudoVFRDIV_VF_RM {
  foreach f = FPList in {
    foreach m = f.MxList in {
      defm "" : VPseudoBinaryV_VF_RM<m, f, f.SEW>,
                SchedBinary<"WriteVFDivF", "ReadVFDivV", "ReadVFDivF", m.MX, f.SEW,
                            forceMergeOpRead=true>;
    }
  }
}

multiclass VPseudoVALU_VV_VX {
 foreach m = MxList in {
    defm "" : VPseudoBinaryV_VV<m>,
            SchedBinary<"WriteVIALUV", "ReadVIALUV", "ReadVIALUV", m.MX,
                        forceMergeOpRead=true>;
    defm "" : VPseudoBinaryV_VX<m>,
            SchedBinary<"WriteVIALUX", "ReadVIALUV", "ReadVIALUX", m.MX,
                        forceMergeOpRead=true>;
  }
}

multiclass VPseudoVSGNJ_VV_VF {
  foreach m = MxListF in {
    foreach e = SchedSEWSet<m.MX, isF=1>.val in
    defm "" : VPseudoBinaryV_VV<m, sew=e>,
              SchedBinary<"WriteVFSgnjV", "ReadVFSgnjV", "ReadVFSgnjV", m.MX,
                          e, forceMergeOpRead=true>;
  }

  foreach f = FPList in {
    foreach m = f.MxList in {
      defm "" : VPseudoBinaryV_VF<m, f, sew=f.SEW>,
                SchedBinary<"WriteVFSgnjF", "ReadVFSgnjV", "ReadVFSgnjF", m.MX,
                            f.SEW, forceMergeOpRead=true>;
    }
  }
}

multiclass VPseudoVMAX_VV_VF {
  foreach m = MxListF in {
    foreach e = SchedSEWSet<m.MX, isF=1>.val in
      defm "" : VPseudoBinaryV_VV<m, sew=e>,
                SchedBinary<"WriteVFMinMaxV", "ReadVFMinMaxV", "ReadVFMinMaxV",
                            m.MX, e, forceMergeOpRead=true>;
  }

  foreach f = FPList in {
    foreach m = f.MxList in {
      defm "" : VPseudoBinaryV_VF<m, f, sew=f.SEW>,
                SchedBinary<"WriteVFMinMaxF", "ReadVFMinMaxV", "ReadVFMinMaxF",
                            m.MX, f.SEW, forceMergeOpRead=true>;
    }
  }
}

multiclass VPseudoVALU_VV_VF_RM {
  foreach m = MxListF in {
    foreach e = SchedSEWSet<m.MX, isF=1>.val in
      defm "" : VPseudoBinaryFV_VV_RM<m, e>,
                SchedBinary<"WriteVFALUV", "ReadVFALUV", "ReadVFALUV", m.MX, e,
                            forceMergeOpRead=true>;
  }

  foreach f = FPList in {
    foreach m = f.MxList in {
      defm "" : VPseudoBinaryV_VF_RM<m, f, f.SEW>,
                SchedBinary<"WriteVFALUF", "ReadVFALUV", "ReadVFALUF", m.MX,
                            f.SEW, forceMergeOpRead=true>;
    }
  }
}

multiclass VPseudoVALU_VF_RM {
  foreach f = FPList in {
    foreach m = f.MxList in {
      defm "" : VPseudoBinaryV_VF_RM<m, f, f.SEW>,
                SchedBinary<"WriteVFALUF", "ReadVFALUV", "ReadVFALUF", m.MX,
                            f.SEW, forceMergeOpRead=true>;
    }
  }
}

multiclass VPseudoVALU_VX_VI {
  foreach m = MxList in {
    defvar mx = m.MX;
    defm "" : VPseudoBinaryV_VX<m>,
              SchedBinary<"WriteVIALUX", "ReadVIALUV", "ReadVIALUX", mx,
                          forceMergeOpRead=true>;
    defm "" : VPseudoBinaryV_VI<simm5, m>,
              SchedUnary<"WriteVIALUI", "ReadVIALUV", mx, forceMergeOpRead=true>;
  }
}

multiclass VPseudoVWALU_VV_VX<bit Commutable = 0> {
  foreach m = MxListW in {
    defvar mx = m.MX;
    defm "" : VPseudoBinaryW_VV<m, Commutable=Commutable>,
              SchedBinary<"WriteVIWALUV", "ReadVIWALUV", "ReadVIWALUV", mx,
                          forceMergeOpRead=true>;
    defm "" : VPseudoBinaryW_VX<m>, 
              SchedBinary<"WriteVIWALUX", "ReadVIWALUV", "ReadVIWALUX", mx,
                          forceMergeOpRead=true>;
  }
}

multiclass VPseudoVWMUL_VV_VX<bit Commutable = 0> {
  foreach m = MxListW in {
    defvar mx = m.MX;
    defm "" : VPseudoBinaryW_VV<m, Commutable=Commutable>,
              SchedBinary<"WriteVIWMulV", "ReadVIWMulV", "ReadVIWMulV", mx,
                          forceMergeOpRead=true>;
    defm "" : VPseudoBinaryW_VX<m>,
              SchedBinary<"WriteVIWMulX", "ReadVIWMulV", "ReadVIWMulX", mx,
                          forceMergeOpRead=true>;
  }
}

multiclass VPseudoVWMUL_VV_VF_RM {
  foreach m = MxListFW in {
    foreach e = SchedSEWSet<m.MX, isF=1, isWidening=1>.val in
    defm "" : VPseudoBinaryW_VV_RM<m, sew=e>,
              SchedBinary<"WriteVFWMulV", "ReadVFWMulV", "ReadVFWMulV", m.MX,
                          e, forceMergeOpRead=true>;
  }

  foreach f = FPListW in {
    foreach m = f.MxListFW in {
      defm "" : VPseudoBinaryW_VF_RM<m, f, sew=f.SEW>,
                SchedBinary<"WriteVFWMulF", "ReadVFWMulV", "ReadVFWMulF", m.MX,
                          f.SEW, forceMergeOpRead=true>;
    }
  }
}

multiclass VPseudoVWALU_WV_WX {
  foreach m = MxListW in {
    defvar mx = m.MX;
    defm "" : VPseudoBinaryW_WV<m>,
              SchedBinary<"WriteVIWALUV", "ReadVIWALUV", "ReadVIWALUV", mx,
                          forceMergeOpRead=true>;
    defm "" : VPseudoBinaryW_WX<m>,
              SchedBinary<"WriteVIWALUX", "ReadVIWALUV", "ReadVIWALUX", mx,
                          forceMergeOpRead=true>;
  }
}

multiclass VPseudoVFWALU_VV_VF_RM {
  foreach m = MxListFW in {
    foreach e = SchedSEWSet<m.MX, isF=1, isWidening=1>.val in
      defm "" : VPseudoBinaryW_VV_RM<m, sew=e>,
                SchedBinary<"WriteVFWALUV", "ReadVFWALUV", "ReadVFWALUV", m.MX,
                            e, forceMergeOpRead=true>;
  }

  foreach f = FPListW in {
    foreach m = f.MxListFW in {
      defm "" : VPseudoBinaryW_VF_RM<m, f, sew=f.SEW>,
                SchedBinary<"WriteVFWALUF", "ReadVFWALUV", "ReadVFWALUF", m.MX,
                          f.SEW, forceMergeOpRead=true>;
    }
  }
}

multiclass VPseudoVFWALU_WV_WF_RM {
  foreach m = MxListFW in {
    foreach e = SchedSEWSet<m.MX, isF=1, isWidening=1>.val in
      defm "" : VPseudoBinaryW_WV_RM<m, sew=e>,
                SchedBinary<"WriteVFWALUV", "ReadVFWALUV", "ReadVFWALUV", m.MX,
                            e, forceMergeOpRead=true>;
  }
  foreach f = FPListW in {
    foreach m = f.MxListFW in {
      defm "" : VPseudoBinaryW_WF_RM<m, f, sew=f.SEW>,
                SchedBinary<"WriteVFWALUF", "ReadVFWALUV", "ReadVFWALUF", m.MX,
                            f.SEW, forceMergeOpRead=true>;
    }
  }
}

multiclass VPseudoVMRG_VM_XM_IM {
  foreach m = MxList in {
    defvar mx = m.MX;
    def "_VVM" # "_" # m.MX:
      VPseudoTiedBinaryCarryIn<GetVRegNoV0<m.vrclass>.R,
                               m.vrclass, m.vrclass, m>,
      SchedBinary<"WriteVIMergeV", "ReadVIMergeV", "ReadVIMergeV", mx,
                          forceMergeOpRead=true>;
    def "_VXM" # "_" # m.MX:
      VPseudoTiedBinaryCarryIn<GetVRegNoV0<m.vrclass>.R,
                               m.vrclass, GPR, m>,
      SchedBinary<"WriteVIMergeX", "ReadVIMergeV", "ReadVIMergeX", mx,
                          forceMergeOpRead=true>;
    def "_VIM" # "_" # m.MX:
      VPseudoTiedBinaryCarryIn<GetVRegNoV0<m.vrclass>.R,
                               m.vrclass, simm5, m>,
      SchedUnary<"WriteVIMergeI", "ReadVIMergeV", mx,
                          forceMergeOpRead=true>;
  }
}

multiclass VPseudoVCALU_VM_XM_IM {
  foreach m = MxList in {
    defvar mx = m.MX;
    defm "" : VPseudoTiedBinaryV_VM<m, Commutable=1>,
              SchedBinary<"WriteVICALUV", "ReadVICALUV", "ReadVICALUV", mx,
                          forceMergeOpRead=true>;
    defm "" : VPseudoTiedBinaryV_XM<m>,
              SchedBinary<"WriteVICALUX", "ReadVICALUV", "ReadVICALUX", mx,
                          forceMergeOpRead=true>;
    defm "" : VPseudoTiedBinaryV_IM<m>,
              SchedUnary<"WriteVICALUI", "ReadVICALUV", mx,
                          forceMergeOpRead=true>;
  }
}

multiclass VPseudoVCALU_VM_XM {
  foreach m = MxList in {
    defvar mx = m.MX;
    defm "" : VPseudoTiedBinaryV_VM<m>,
              SchedBinary<"WriteVICALUV", "ReadVICALUV", "ReadVICALUV", mx,
                          forceMergeOpRead=true>;
    defm "" : VPseudoTiedBinaryV_XM<m>,
              SchedBinary<"WriteVICALUX", "ReadVICALUV", "ReadVICALUX", mx,
                          forceMergeOpRead=true>;
  }
}

multiclass VPseudoVCALUM_VM_XM_IM {
  defvar constraint = "@earlyclobber $rd";
  foreach m = MxList in {
    defvar mx = m.MX;
    defm "" : VPseudoBinaryV_VM<m, CarryOut=1, CarryIn=1, Constraint=constraint,
                                Commutable=1, TargetConstraintType=2>,
              SchedBinary<"WriteVICALUV", "ReadVICALUV", "ReadVICALUV", mx, forceMasked=1,
                          forceMergeOpRead=true>;
    defm "" : VPseudoBinaryV_XM<m, CarryOut=1, CarryIn=1, Constraint=constraint, TargetConstraintType=2>,
              SchedBinary<"WriteVICALUX", "ReadVICALUV", "ReadVICALUX", mx, forceMasked=1,
                          forceMergeOpRead=true>;
    defm "" : VPseudoBinaryV_IM<m, CarryOut=1, CarryIn=1, Constraint=constraint, TargetConstraintType=2>,
              SchedUnary<"WriteVICALUI", "ReadVICALUV", mx, forceMasked=1,
                          forceMergeOpRead=true>;
  }
}

multiclass VPseudoVCALUM_VM_XM {
  defvar constraint = "@earlyclobber $rd";
  foreach m = MxList in {
    defvar mx = m.MX;
    defm "" : VPseudoBinaryV_VM<m, CarryOut=1, CarryIn=1, Constraint=constraint,
                                TargetConstraintType=2>,
              SchedBinary<"WriteVICALUV", "ReadVICALUV", "ReadVICALUV", mx, forceMasked=1,
                          forceMergeOpRead=true>;
    defm "" : VPseudoBinaryV_XM<m, CarryOut=1, CarryIn=1, Constraint=constraint,
                                TargetConstraintType=2>,
              SchedBinary<"WriteVICALUX", "ReadVICALUV", "ReadVICALUX", mx, forceMasked=1,
                          forceMergeOpRead=true>;
  }
}

multiclass VPseudoVCALUM_V_X_I {
  defvar constraint = "@earlyclobber $rd";
  foreach m = MxList in {
    defvar mx = m.MX;
    defm "" : VPseudoBinaryV_VM<m, CarryOut=1, CarryIn=0, Constraint=constraint,
                                Commutable=1, TargetConstraintType=2>,
              SchedBinary<"WriteVICALUV", "ReadVICALUV", "ReadVICALUV", mx,
                          forceMergeOpRead=true>;
    defm "" : VPseudoBinaryV_XM<m, CarryOut=1, CarryIn=0, Constraint=constraint, TargetConstraintType=2>,
              SchedBinary<"WriteVICALUX", "ReadVICALUV", "ReadVICALUX", mx,
                          forceMergeOpRead=true>;
    defm "" : VPseudoBinaryV_IM<m, CarryOut=1, CarryIn=0, Constraint=constraint>,
              SchedUnary<"WriteVICALUI", "ReadVICALUV", mx,
                          forceMergeOpRead=true>;
  }
}

multiclass VPseudoVCALUM_V_X {
  defvar constraint = "@earlyclobber $rd";
  foreach m = MxList in {
    defvar mx = m.MX;
    defm "" : VPseudoBinaryV_VM<m, CarryOut=1, CarryIn=0, Constraint=constraint, TargetConstraintType=2>,
              SchedBinary<"WriteVICALUV", "ReadVICALUV", "ReadVICALUV", mx,
                          forceMergeOpRead=true>;
    defm "" : VPseudoBinaryV_XM<m, CarryOut=1, CarryIn=0, Constraint=constraint, TargetConstraintType=2>,
              SchedBinary<"WriteVICALUX", "ReadVICALUV", "ReadVICALUX", mx,
                          forceMergeOpRead=true>;
  }
}

multiclass VPseudoVNCLP_WV_WX_WI_RM {
  foreach m = MxListW in {
    defvar mx = m.MX;
    defm "" : VPseudoBinaryV_WV_RM<m>,
              SchedBinary<"WriteVNClipV", "ReadVNClipV", "ReadVNClipV", mx,
                          forceMergeOpRead=true>;
    defm "" : VPseudoBinaryV_WX_RM<m>,
              SchedBinary<"WriteVNClipX", "ReadVNClipV", "ReadVNClipX", mx,
                          forceMergeOpRead=true>;
    defm "" : VPseudoBinaryV_WI_RM<m>,
              SchedUnary<"WriteVNClipI", "ReadVNClipV", mx,
                          forceMergeOpRead=true>;
  }
}

multiclass VPseudoVNSHT_WV_WX_WI {
  foreach m = MxListW in {
    defvar mx = m.MX;
    defm "" : VPseudoBinaryV_WV<m>,
              SchedBinary<"WriteVNShiftV", "ReadVNShiftV", "ReadVNShiftV", mx,
                          forceMergeOpRead=true>;
    defm "" : VPseudoBinaryV_WX<m>,
              SchedBinary<"WriteVNShiftX", "ReadVNShiftV", "ReadVNShiftX", mx,
                          forceMergeOpRead=true>;
    defm "" : VPseudoBinaryV_WI<m>,
              SchedUnary<"WriteVNShiftI", "ReadVNShiftV", mx,
                          forceMergeOpRead=true>;
  }
}

multiclass VPseudoTernaryWithTailPolicy<VReg RetClass,
                                          RegisterClass Op1Class,
                                          DAGOperand Op2Class,
                                          LMULInfo MInfo,
                                          int sew> {
  let VLMul = MInfo.value, SEW=sew in {
    defvar mx = MInfo.MX;
    def "_" # mx # "_E" # sew : VPseudoTernaryNoMaskWithPolicy<RetClass, Op1Class, Op2Class>;
    def "_" # mx # "_E" # sew # "_MASK" : VPseudoTernaryMaskPolicy<RetClass, Op1Class, Op2Class>,
                                          RISCVMaskedPseudo<MaskIdx=3, ActiveAffectsRes=true>;
  }
}

multiclass VPseudoTernaryWithTailPolicyRoundingMode<VReg RetClass,
                                                    RegisterClass Op1Class,
                                                    DAGOperand Op2Class,
                                                    LMULInfo MInfo,
                                                    int sew> {
  let VLMul = MInfo.value, SEW=sew in {
    defvar mx = MInfo.MX;
    def "_" # mx # "_E" # sew
        : VPseudoTernaryNoMaskWithPolicyRoundingMode<RetClass, Op1Class,
                                                     Op2Class>;
    def "_" # mx # "_E" # sew # "_MASK"
        : VPseudoTernaryMaskPolicyRoundingMode<RetClass, Op1Class,
                                               Op2Class>,
          RISCVMaskedPseudo<MaskIdx=3, ActiveAffectsRes=true>;
  }
}

multiclass VPseudoTernaryWithPolicy<VReg RetClass,
                                    RegisterClass Op1Class,
                                    DAGOperand Op2Class,
                                    LMULInfo MInfo,
                                    string Constraint = "",
                                    bit Commutable = 0,
                                    int TargetConstraintType = 1> {
  let VLMul = MInfo.value in {
    let isCommutable = Commutable in
    def "_" # MInfo.MX : VPseudoTernaryNoMaskWithPolicy<RetClass, Op1Class, Op2Class, Constraint, TargetConstraintType>;
    def "_" # MInfo.MX # "_MASK" : VPseudoBinaryMaskPolicy<RetClass, Op1Class, Op2Class, Constraint, TargetConstraintType>,
                                   RISCVMaskedPseudo<MaskIdx=3>;
  }
}

multiclass VPseudoTernaryWithPolicyRoundingMode<VReg RetClass,
                                                RegisterClass Op1Class,
                                                DAGOperand Op2Class,
                                                LMULInfo MInfo,
                                                string Constraint = "",
                                                int sew = 0,
                                                bit Commutable = 0,
                                                int TargetConstraintType = 1> {
  let VLMul = MInfo.value in {
    defvar suffix = !if(sew, "_" # MInfo.MX # "_E" # sew, "_" # MInfo.MX);
    let isCommutable = Commutable in
    def suffix :
        VPseudoTernaryNoMaskWithPolicyRoundingMode<RetClass, Op1Class,
                                                   Op2Class, Constraint,
                                                   TargetConstraintType>;
    def suffix # "_MASK" :
        VPseudoBinaryMaskPolicyRoundingMode<RetClass, Op1Class,
                                            Op2Class, Constraint,
                                            UsesVXRM_=0,
                                            TargetConstraintType=TargetConstraintType>,
                                   RISCVMaskedPseudo<MaskIdx=3>;
  }
}

multiclass VPseudoTernaryV_VV_AAXA<LMULInfo m> {
  defm _VV : VPseudoTernaryWithPolicy<m.vrclass, m.vrclass, m.vrclass, m,
                                      Commutable=1>;
}

multiclass VPseudoTernaryV_VV_AAXA_RM<LMULInfo m, int sew> {
  defm _VV : VPseudoTernaryWithPolicyRoundingMode<m.vrclass, m.vrclass, m.vrclass, m,
                                                  sew=sew, Commutable=1>;
}

multiclass VPseudoTernaryV_VX_AAXA<LMULInfo m> {
  defm "_VX" : VPseudoTernaryWithPolicy<m.vrclass, GPR, m.vrclass, m,
                                        Commutable=1>;
}

multiclass VPseudoTernaryV_VF_AAXA_RM<LMULInfo m, FPR_Info f,
                                      int sew> {
  defm "_V" # f.FX : VPseudoTernaryWithPolicyRoundingMode<m.vrclass, f.fprclass,
                                                          m.vrclass, m,
                                                          sew=sew, Commutable=1>;
}

multiclass VPseudoTernaryW_VV<LMULInfo m, bit Commutable = 0> {
  defvar constraint = "@earlyclobber $rd";
  defm _VV : VPseudoTernaryWithPolicy<m.wvrclass, m.vrclass, m.vrclass, m,
                                      constraint, Commutable=Commutable, TargetConstraintType=3>;
}

multiclass VPseudoTernaryW_VV_RM<LMULInfo m, int sew> {
  defvar constraint = "@earlyclobber $rd";
  defm _VV : VPseudoTernaryWithPolicyRoundingMode<m.wvrclass, m.vrclass, m.vrclass, m,
                                                  constraint, sew,
                                                  TargetConstraintType=3>;
}

multiclass VPseudoTernaryW_VX<LMULInfo m> {
  defvar constraint = "@earlyclobber $rd";
  defm "_VX" : VPseudoTernaryWithPolicy<m.wvrclass, GPR, m.vrclass, m,
                                        constraint, TargetConstraintType=3>;
}

multiclass VPseudoTernaryW_VF_RM<LMULInfo m, FPR_Info f, int sew> {
  defvar constraint = "@earlyclobber $rd";
  defm "_V" # f.FX : VPseudoTernaryWithPolicyRoundingMode<m.wvrclass, f.fprclass,
                                                          m.vrclass, m, constraint,
                                                          sew=sew,
                                                          TargetConstraintType=3>;
}

multiclass VPseudoVSLDVWithPolicy<VReg RetClass,
                                  RegisterClass Op1Class,
                                  DAGOperand Op2Class,
                                  LMULInfo MInfo,
                                  string Constraint = ""> {
  let VLMul = MInfo.value in {
    def "_" # MInfo.MX : VPseudoTernaryNoMaskWithPolicy<RetClass, Op1Class, Op2Class, Constraint>;
    def "_" # MInfo.MX # "_MASK" : VPseudoBinaryMaskPolicy<RetClass, Op1Class, Op2Class, Constraint>,
                                   RISCVMaskedPseudo<MaskIdx=3>;
  }
}

multiclass VPseudoVSLDV_VX<LMULInfo m, string Constraint = ""> {
  defm _VX : VPseudoVSLDVWithPolicy<m.vrclass, m.vrclass, GPR, m, Constraint>;
}

multiclass VPseudoVSLDV_VI<LMULInfo m, string Constraint = ""> {
  defm _VI : VPseudoVSLDVWithPolicy<m.vrclass, m.vrclass, uimm5, m, Constraint>;
}

multiclass VPseudoVMAC_VV_VX_AAXA {
  foreach m = MxList in {
    defvar mx = m.MX;
    defm "" : VPseudoTernaryV_VV_AAXA<m>,
              SchedTernary<"WriteVIMulAddV", "ReadVIMulAddV", "ReadVIMulAddV",
                           "ReadVIMulAddV", mx>;
    defm "" : VPseudoTernaryV_VX_AAXA<m>,
              SchedTernary<"WriteVIMulAddX", "ReadVIMulAddV", "ReadVIMulAddX",
                           "ReadVIMulAddV", mx>;
  }
}

multiclass VPseudoVMAC_VV_VF_AAXA_RM {
  foreach m = MxListF in {
    foreach e = SchedSEWSet<m.MX, isF=1>.val in
      defm "" : VPseudoTernaryV_VV_AAXA_RM<m, sew=e>,
                SchedTernary<"WriteVFMulAddV", "ReadVFMulAddV", "ReadVFMulAddV",
                             "ReadVFMulAddV", m.MX, e>;
  }

  foreach f = FPList in {
    foreach m = f.MxList in {
      defm "" : VPseudoTernaryV_VF_AAXA_RM<m, f, sew=f.SEW>,
                SchedTernary<"WriteVFMulAddF", "ReadVFMulAddV", "ReadVFMulAddF",
                             "ReadVFMulAddV", m.MX, f.SEW>;
    }
  }
}

multiclass VPseudoVSLD_VX_VI<bit slidesUp = false, string Constraint = ""> {
  defvar WriteSlideX = !if(slidesUp, "WriteVSlideUpX", "WriteVSlideDownX");
  foreach m = MxList in {
    defvar mx = m.MX;
    defm "" : VPseudoVSLDV_VX<m, Constraint>,
              SchedTernary<WriteSlideX, "ReadVISlideV", "ReadVISlideV",
                           "ReadVISlideX", mx>;
    defm "" : VPseudoVSLDV_VI<m, Constraint>,
              SchedBinary<"WriteVSlideI", "ReadVISlideV", "ReadVISlideV", mx>;
  }
}

multiclass VPseudoVWMAC_VV_VX<bit Commutable = 0> {
  foreach m = MxListW in {
    defvar mx = m.MX;
    defm "" : VPseudoTernaryW_VV<m, Commutable=Commutable>,
              SchedTernary<"WriteVIWMulAddV", "ReadVIWMulAddV", "ReadVIWMulAddV",
                           "ReadVIWMulAddV", mx>;
    defm "" : VPseudoTernaryW_VX<m>,
              SchedTernary<"WriteVIWMulAddX", "ReadVIWMulAddV", "ReadVIWMulAddX",
                           "ReadVIWMulAddV", mx>;
  }
}

multiclass VPseudoVWMAC_VX {
  foreach m = MxListW in {
    defm "" : VPseudoTernaryW_VX<m>,
              SchedTernary<"WriteVIWMulAddX", "ReadVIWMulAddV", "ReadVIWMulAddX",
                           "ReadVIWMulAddV", m.MX>;
  }
}

multiclass VPseudoVWMAC_VV_VF_RM {
  foreach m = MxListFW in {
    foreach e = SchedSEWSet<m.MX, isF=1, isWidening=1>.val in
      defm "" : VPseudoTernaryW_VV_RM<m, sew=e>,
                SchedTernary<"WriteVFWMulAddV", "ReadVFWMulAddV",
                             "ReadVFWMulAddV", "ReadVFWMulAddV", m.MX, e>;
  }

  foreach f = FPListW in {
    foreach m = f.MxListFW in {
      defm "" : VPseudoTernaryW_VF_RM<m, f, sew=f.SEW>,
                SchedTernary<"WriteVFWMulAddF", "ReadVFWMulAddV",
                             "ReadVFWMulAddF", "ReadVFWMulAddV", m.MX, f.SEW>;
    }
  }
}

multiclass VPseudoVWMAC_VV_VF_BF_RM {
  foreach m = MxListFW in {
    defvar mx = m.MX;
    foreach e = SchedSEWSet<mx, isF=1, isWidening=1>.val in
      defm "" : VPseudoTernaryW_VV_RM<m, sew=e>,
                SchedTernary<"WriteVFWMulAddV", "ReadVFWMulAddV",
                             "ReadVFWMulAddV", "ReadVFWMulAddV", mx, e>;
  }

  foreach f = BFPListW in {
    foreach m = f.MxListFW in {
      defvar mx = m.MX;
      defm "" : VPseudoTernaryW_VF_RM<m, f, sew=f.SEW>,
                SchedTernary<"WriteVFWMulAddF", "ReadVFWMulAddV",
                             "ReadVFWMulAddF", "ReadVFWMulAddV", mx, f.SEW>;
    }
  }
}

multiclass VPseudoVCMPM_VV_VX_VI<bit Commutable = 0> {
  foreach m = MxList in {
    defvar mx = m.MX;
    defm "" : VPseudoBinaryM_VV<m, TargetConstraintType=2, Commutable=Commutable>,
              SchedBinary<"WriteVICmpV", "ReadVICmpV", "ReadVICmpV", mx>;
    defm "" : VPseudoBinaryM_VX<m, TargetConstraintType=2>,
              SchedBinary<"WriteVICmpX", "ReadVICmpV", "ReadVICmpX", mx>;
    defm "" : VPseudoBinaryM_VI<m, TargetConstraintType=2>,
              SchedUnary<"WriteVICmpI", "ReadVICmpV", mx>;
  }
}

multiclass VPseudoVCMPM_VV_VX {
  foreach m = MxList in {
    defvar mx = m.MX;
    defm "" : VPseudoBinaryM_VV<m, TargetConstraintType=2>,
              SchedBinary<"WriteVICmpV", "ReadVICmpV", "ReadVICmpV", mx>;
    defm "" : VPseudoBinaryM_VX<m, TargetConstraintType=2>,
              SchedBinary<"WriteVICmpX", "ReadVICmpV", "ReadVICmpX", mx>;
  }
}

multiclass VPseudoVCMPM_VV_VF {
  foreach m = MxListF in {
    defm "" : VPseudoBinaryM_VV<m, TargetConstraintType=2>,
              SchedBinary<"WriteVFCmpV", "ReadVFCmpV", "ReadVFCmpV", m.MX>;
  }

  foreach f = FPList in {
    foreach m = f.MxList in {
      defm "" : VPseudoBinaryM_VF<m, f, TargetConstraintType=2>,
                SchedBinary<"WriteVFCmpF", "ReadVFCmpV", "ReadVFCmpF", m.MX>;
    }
  }
}

multiclass VPseudoVCMPM_VF {
  foreach f = FPList in {
    foreach m = f.MxList in {
      defm "" : VPseudoBinaryM_VF<m, f, TargetConstraintType=2>,
                SchedBinary<"WriteVFCmpF", "ReadVFCmpV", "ReadVFCmpF", m.MX>;
    }
  }
}

multiclass VPseudoVCMPM_VX_VI {
  foreach m = MxList in {
    defvar mx = m.MX;
    defm "" : VPseudoBinaryM_VX<m, TargetConstraintType=2>,
              SchedBinary<"WriteVICmpX", "ReadVICmpV", "ReadVICmpX", mx>;
    defm "" : VPseudoBinaryM_VI<m, TargetConstraintType=2>,
              SchedUnary<"WriteVICmpI", "ReadVICmpV", mx>;
  }
}

multiclass VPseudoVRED_VS {
  foreach m = MxList in {
    defvar mx = m.MX;
    foreach e = SchedSEWSet<mx>.val in {
      defm _VS : VPseudoTernaryWithTailPolicy<V_M1.vrclass, m.vrclass, V_M1.vrclass, m, e>,
                 SchedReduction<"WriteVIRedV_From", "ReadVIRedV", mx, e>;
    }
  }
}

multiclass VPseudoVREDMINMAX_VS {
  foreach m = MxList in {
    defvar mx = m.MX;
    foreach e = SchedSEWSet<mx>.val in {
      defm _VS : VPseudoTernaryWithTailPolicy<V_M1.vrclass, m.vrclass, V_M1.vrclass, m, e>,
                 SchedReduction<"WriteVIRedMinMaxV_From", "ReadVIRedV", mx, e>;
    }
  }
}

multiclass VPseudoVWRED_VS {
  foreach m = MxListWRed in {
    defvar mx = m.MX;
    foreach e = SchedSEWSet<mx, isWidening=1>.val in {
      defm _VS : VPseudoTernaryWithTailPolicy<V_M1.vrclass, m.vrclass, V_M1.vrclass, m, e>,
                 SchedReduction<"WriteVIWRedV_From", "ReadVIWRedV", mx, e>;
    }
  }
}

multiclass VPseudoVFRED_VS_RM {
  foreach m = MxListF in {
    defvar mx = m.MX;
    foreach e = SchedSEWSet<mx, isF=1>.val in {
      defm _VS
          : VPseudoTernaryWithTailPolicyRoundingMode<V_M1.vrclass, m.vrclass,
                                                     V_M1.vrclass, m, e>,
            SchedReduction<"WriteVFRedV_From", "ReadVFRedV", mx, e>;
    }
  }
}

multiclass VPseudoVFREDMINMAX_VS {
  foreach m = MxListF in {
    defvar mx = m.MX;
    foreach e = SchedSEWSet<mx, isF=1>.val in {
      defm _VS : VPseudoTernaryWithTailPolicy<V_M1.vrclass, m.vrclass, V_M1.vrclass, m, e>,
                 SchedReduction<"WriteVFRedMinMaxV_From", "ReadVFRedV", mx, e>;
    }
  }
}

multiclass VPseudoVFREDO_VS_RM {
  foreach m = MxListF in {
    defvar mx = m.MX;
    foreach e = SchedSEWSet<mx, isF=1>.val in {
      defm _VS : VPseudoTernaryWithTailPolicyRoundingMode<V_M1.vrclass, m.vrclass,
                                                          V_M1.vrclass, m, e>,
                 SchedReduction<"WriteVFRedOV_From", "ReadVFRedOV", mx, e>;
    }
  }
}

multiclass VPseudoVFWRED_VS_RM {
  foreach m = MxListFWRed in {
    defvar mx = m.MX;
    foreach e = SchedSEWSet<mx, isF=1, isWidening=1>.val in {
      defm _VS
          : VPseudoTernaryWithTailPolicyRoundingMode<V_M1.vrclass, m.vrclass,
                                                     V_M1.vrclass, m, e>,
            SchedReduction<"WriteVFWRedV_From", "ReadVFWRedV", mx, e>;
    }
  }
}

multiclass VPseudoVFWREDO_VS_RM {
  foreach m = MxListFWRed in {
    defvar mx = m.MX;
    foreach e = SchedSEWSet<mx, isF=1, isWidening=1>.val in {
      defm _VS
          : VPseudoTernaryWithTailPolicyRoundingMode<V_M1.vrclass, m.vrclass,
                                                     V_M1.vrclass, m, e>,
            SchedReduction<"WriteVFWRedOV_From", "ReadVFWRedV", mx, e>;
    }
  }
}

multiclass VPseudoConversion<VReg RetClass,
                             VReg Op1Class,
                             LMULInfo MInfo,
                             string Constraint = "",
                             int sew = 0,
                             int TargetConstraintType = 1> {
  defvar suffix = !if(sew, "_" # MInfo.MX # "_E" # sew, "_" # MInfo.MX);
  let VLMul = MInfo.value, SEW=sew in {
    def suffix : VPseudoUnaryNoMask<RetClass, Op1Class, Constraint, TargetConstraintType>;
    def suffix # "_MASK" : VPseudoUnaryMask<RetClass, Op1Class,
                                            Constraint, TargetConstraintType>,
                           RISCVMaskedPseudo<MaskIdx=2>;
  }
}

multiclass VPseudoConversionRoundingMode<VReg RetClass,
                             VReg Op1Class,
                             LMULInfo MInfo,
                             string Constraint = "",
                             int sew = 0,
                             int TargetConstraintType = 1> {
  let VLMul = MInfo.value, SEW=sew in {
    defvar suffix = !if(sew, "_" # MInfo.MX # "_E" # sew, "_" # MInfo.MX);
    def suffix : VPseudoUnaryNoMaskRoundingMode<RetClass, Op1Class, Constraint, TargetConstraintType>;
    def suffix # "_MASK" : VPseudoUnaryMaskRoundingMode<RetClass, Op1Class,
                                                        Constraint,
                                                        TargetConstraintType>,
                           RISCVMaskedPseudo<MaskIdx=2>;
  }
}


multiclass VPseudoConversionRM<VReg RetClass,
                               VReg Op1Class,
                               LMULInfo MInfo,
                               string Constraint = "",
                               int sew = 0,
                               int TargetConstraintType = 1> {
  let VLMul = MInfo.value, SEW=sew in {
    defvar suffix = !if(sew, "_" # MInfo.MX # "_E" # sew, "_" # MInfo.MX);
    def suffix : VPseudoUnaryNoMask_FRM<RetClass, Op1Class,
                                        Constraint, TargetConstraintType>;
    def suffix # "_MASK" : VPseudoUnaryMask_FRM<RetClass, Op1Class,
                                                Constraint, TargetConstraintType>,
                           RISCVMaskedPseudo<MaskIdx=2>;
  }
}

multiclass VPseudoConversionNoExcept<VReg RetClass,
                                     VReg Op1Class,
                                     LMULInfo MInfo,
                                     string Constraint = ""> {
  let VLMul = MInfo.value in {
    def "_" # MInfo.MX # "_MASK" : VPseudoUnaryMask_NoExcept<RetClass, Op1Class, Constraint>;
  }
}

multiclass VPseudoVCVTI_V {
  foreach m = MxListF in {
    defm _V : VPseudoConversion<m.vrclass, m.vrclass, m>,
              SchedUnary<"WriteVFCvtFToIV", "ReadVFCvtFToIV", m.MX,
                         forceMergeOpRead=true>;
  }
}

multiclass VPseudoVCVTI_V_RM {
  foreach m = MxListF in {
    defm _V : VPseudoConversionRoundingMode<m.vrclass, m.vrclass, m>,
              SchedUnary<"WriteVFCvtFToIV", "ReadVFCvtFToIV", m.MX,
                         forceMergeOpRead=true>;
  }
}

multiclass VPseudoVCVTI_RM_V {
  foreach m = MxListF in {
    defm _V : VPseudoConversionRM<m.vrclass, m.vrclass, m>,
              SchedUnary<"WriteVFCvtFToIV", "ReadVFCvtFToIV", m.MX,
                         forceMergeOpRead=true>;
  }
}

multiclass VPseudoVFROUND_NOEXCEPT_V {
  foreach m = MxListF in {
    defm _V : VPseudoConversionNoExcept<m.vrclass, m.vrclass, m>,
              SchedUnary<"WriteVFCvtFToIV", "ReadVFCvtFToIV", m.MX,
                         forceMergeOpRead=true>;
  }
}

multiclass VPseudoVCVTF_V_RM {
  foreach m = MxListF in {
    foreach e = SchedSEWSet<m.MX, isF=1>.val in
      defm _V : VPseudoConversionRoundingMode<m.vrclass, m.vrclass, m, sew=e>,
                SchedUnary<"WriteVFCvtIToFV", "ReadVFCvtIToFV", m.MX, e,
                           forceMergeOpRead=true>;
  }
}

multiclass VPseudoVCVTF_RM_V {
  foreach m = MxListF in {
    foreach e = SchedSEWSet<m.MX, isF=1>.val in
      defm _V : VPseudoConversionRM<m.vrclass, m.vrclass, m, sew=e>,
                SchedUnary<"WriteVFCvtIToFV", "ReadVFCvtIToFV", m.MX, e,
                           forceMergeOpRead=true>;
  }
}

multiclass VPseudoVWCVTI_V {
  defvar constraint = "@earlyclobber $rd";
  foreach m = MxListFW in {
    defm _V : VPseudoConversion<m.wvrclass, m.vrclass, m, constraint, TargetConstraintType=3>,
              SchedUnary<"WriteVFWCvtFToIV", "ReadVFWCvtFToIV", m.MX,
                         forceMergeOpRead=true>;
  }
}

multiclass VPseudoVWCVTI_V_RM {
  defvar constraint = "@earlyclobber $rd";
  foreach m = MxListFW in {
    defm _V : VPseudoConversionRoundingMode<m.wvrclass, m.vrclass, m, constraint, TargetConstraintType=3>,
              SchedUnary<"WriteVFWCvtFToIV", "ReadVFWCvtFToIV", m.MX,
                         forceMergeOpRead=true>;
  }
}

multiclass VPseudoVWCVTI_RM_V {
  defvar constraint = "@earlyclobber $rd";
  foreach m = MxListFW in {
    defm _V : VPseudoConversionRM<m.wvrclass, m.vrclass, m, constraint>,
              SchedUnary<"WriteVFWCvtFToIV", "ReadVFWCvtFToIV", m.MX,
                         forceMergeOpRead=true>;
  }
}

multiclass VPseudoVWCVTF_V {
  defvar constraint = "@earlyclobber $rd";
  foreach m = MxListW in {
    foreach e = SchedSEWSet<m.MX, isF=0, isWidening=1>.val in
      defm _V : VPseudoConversion<m.wvrclass, m.vrclass, m, constraint, sew=e,
                                  TargetConstraintType=3>,
                SchedUnary<"WriteVFWCvtIToFV", "ReadVFWCvtIToFV", m.MX, e,
                           forceMergeOpRead=true>;
  }
}

multiclass VPseudoVWCVTD_V {
  defvar constraint = "@earlyclobber $rd";
  foreach m = MxListFW in {
    foreach e = SchedSEWSet<m.MX, isF=1, isWidening=1>.val in
      defm _V : VPseudoConversion<m.wvrclass, m.vrclass, m, constraint, sew=e,
                                  TargetConstraintType=3>,
                SchedUnary<"WriteVFWCvtFToFV", "ReadVFWCvtFToFV", m.MX, e,
                           forceMergeOpRead=true>;
  }
}

multiclass VPseudoVNCVTI_W {
  defvar constraint = "@earlyclobber $rd";
  foreach m = MxListW in {
    defm _W : VPseudoConversion<m.vrclass, m.wvrclass, m, constraint, TargetConstraintType=2>,
              SchedUnary<"WriteVFNCvtFToIV", "ReadVFNCvtFToIV", m.MX,
                         forceMergeOpRead=true>;
  }
}

multiclass VPseudoVNCVTI_W_RM {
  defvar constraint = "@earlyclobber $rd";
  foreach m = MxListW in {
    defm _W : VPseudoConversionRoundingMode<m.vrclass, m.wvrclass, m, constraint, TargetConstraintType=2>,
              SchedUnary<"WriteVFNCvtFToIV", "ReadVFNCvtFToIV", m.MX,
                         forceMergeOpRead=true>;
  }
}

multiclass VPseudoVNCVTI_RM_W {
  defvar constraint = "@earlyclobber $rd";
  foreach m = MxListW in {
    defm _W : VPseudoConversionRM<m.vrclass, m.wvrclass, m, constraint, TargetConstraintType=2>,
              SchedUnary<"WriteVFNCvtFToIV", "ReadVFNCvtFToIV", m.MX,
                         forceMergeOpRead=true>;
  }
}

multiclass VPseudoVNCVTF_W_RM {
  defvar constraint = "@earlyclobber $rd";
  foreach m = MxListFW in {
    foreach e = SchedSEWSet<m.MX, isF=1, isWidening=1>.val in
      defm _W : VPseudoConversionRoundingMode<m.vrclass, m.wvrclass, m,
                                              constraint, sew=e,
                                              TargetConstraintType=2>,
                SchedUnary<"WriteVFNCvtIToFV", "ReadVFNCvtIToFV", m.MX, e,
                           forceMergeOpRead=true>;
  }
}

multiclass VPseudoVNCVTF_RM_W {
  defvar constraint = "@earlyclobber $rd";
  foreach m = MxListFW in {
    foreach e = SchedSEWSet<m.MX, isF=1, isWidening=1>.val in
      defm _W : VPseudoConversionRM<m.vrclass, m.wvrclass, m, constraint, sew=e>,
                SchedUnary<"WriteVFNCvtIToFV", "ReadVFNCvtIToFV", m.MX, e,
                           forceMergeOpRead=true>;
  }
}

multiclass VPseudoVNCVTD_W {
  defvar constraint = "@earlyclobber $rd";
  foreach m = MxListFW in {
    foreach e = SchedSEWSet<m.MX, isF=1, isWidening=1>.val in
      defm _W : VPseudoConversion<m.vrclass, m.wvrclass, m, constraint, sew=e,
                                  TargetConstraintType=2>,
                SchedUnary<"WriteVFNCvtFToFV", "ReadVFNCvtFToFV", m.MX, e,
                           forceMergeOpRead=true>;
  }
}

multiclass VPseudoVNCVTD_W_RM {
  defvar constraint = "@earlyclobber $rd";
  foreach m = MxListFW in {
    foreach e = SchedSEWSet<m.MX, isF=1, isWidening=1>.val in
      defm _W : VPseudoConversionRoundingMode<m.vrclass, m.wvrclass, m,
                                              constraint, sew=e,
                                              TargetConstraintType=2>,
                SchedUnary<"WriteVFNCvtFToFV", "ReadVFNCvtFToFV", m.MX, e,
                           forceMergeOpRead=true>;
  }
}

multiclass VPseudoUSSegLoad {
  foreach eew = EEWList in {
    foreach lmul = MxSet<eew>.m in {
      defvar LInfo = lmul.MX;
      let VLMul = lmul.value, SEW=eew in {
        foreach nf = NFSet<lmul>.L in {
          defvar vreg = SegRegClass<lmul, nf>.RC;
          def nf # "E" # eew # "_V_" # LInfo :
            VPseudoUSSegLoadNoMask<vreg, eew, nf>, VLSEGSched<nf, eew, LInfo>;
          def nf # "E" # eew # "_V_" # LInfo # "_MASK" :
            VPseudoUSSegLoadMask<vreg, eew, nf>, VLSEGSched<nf, eew, LInfo>;
        }
      }
    }
  }
}

multiclass VPseudoUSSegLoadFF {
  foreach eew = EEWList in {
    foreach lmul = MxSet<eew>.m in {
      defvar LInfo = lmul.MX;
      let VLMul = lmul.value, SEW=eew in {
        foreach nf = NFSet<lmul>.L in {
          defvar vreg = SegRegClass<lmul, nf>.RC;
          def nf # "E" # eew # "FF_V_" # LInfo :
            VPseudoUSSegLoadFFNoMask<vreg, eew, nf>, VLSEGFFSched<nf, eew, LInfo>;
          def nf # "E" # eew # "FF_V_" # LInfo # "_MASK" :
            VPseudoUSSegLoadFFMask<vreg, eew, nf>, VLSEGFFSched<nf, eew, LInfo>;
        }
      }
    }
  }
}

multiclass VPseudoSSegLoad {
  foreach eew = EEWList in {
    foreach lmul = MxSet<eew>.m in {
      defvar LInfo = lmul.MX;
      let VLMul = lmul.value, SEW=eew in {
        foreach nf = NFSet<lmul>.L in {
          defvar vreg = SegRegClass<lmul, nf>.RC;
          def nf # "E" # eew # "_V_" # LInfo : VPseudoSSegLoadNoMask<vreg, eew, nf>,
                                               VLSSEGSched<nf, eew, LInfo>;
          def nf # "E" # eew # "_V_" # LInfo # "_MASK" : VPseudoSSegLoadMask<vreg, eew, nf>,
                                                         VLSSEGSched<nf, eew, LInfo>;
        }
      }
    }
  }
}

multiclass VPseudoISegLoad<bit Ordered> {
  foreach idxEEW = EEWList in {
    foreach dataEEW = EEWList in {
      foreach dataEMUL = MxSet<dataEEW>.m in {
        defvar dataEMULOctuple = dataEMUL.octuple;
        // Calculate emul = eew * lmul / sew
        defvar idxEMULOctuple = !srl(!mul(idxEEW, dataEMULOctuple), !logtwo(dataEEW));
        if !and(!ge(idxEMULOctuple, 1), !le(idxEMULOctuple, 64)) then {
          defvar DataLInfo = dataEMUL.MX;
          defvar IdxLInfo = octuple_to_str<idxEMULOctuple>.ret;
          defvar idxEMUL = !cast<LMULInfo>("V_" # IdxLInfo);
          defvar DataVreg = dataEMUL.vrclass;
          defvar IdxVreg = idxEMUL.vrclass;
          let VLMul = dataEMUL.value in {
            foreach nf = NFSet<dataEMUL>.L in {
              defvar Vreg = SegRegClass<dataEMUL, nf>.RC;
              def nf # "EI" # idxEEW # "_V_" # IdxLInfo # "_" # DataLInfo :
                VPseudoISegLoadNoMask<Vreg, IdxVreg, idxEEW, idxEMUL.value,
                                      nf, Ordered>,
                VLXSEGSched<nf, dataEEW, Ordered, DataLInfo>;
              def nf # "EI" # idxEEW # "_V_" # IdxLInfo # "_" # DataLInfo # "_MASK" :
                VPseudoISegLoadMask<Vreg, IdxVreg, idxEEW, idxEMUL.value,
                                    nf, Ordered>,
                VLXSEGSched<nf, dataEEW, Ordered, DataLInfo>;
            }
          }
        }
      }
    }
  }
}

multiclass VPseudoUSSegStore {
  foreach eew = EEWList in {
    foreach lmul = MxSet<eew>.m in {
      defvar LInfo = lmul.MX;
      let VLMul = lmul.value, SEW=eew in {
        foreach nf = NFSet<lmul>.L in {
          defvar vreg = SegRegClass<lmul, nf>.RC;
          def nf # "E" # eew # "_V_" # LInfo : VPseudoUSSegStoreNoMask<vreg, eew, nf>,
                                               VSSEGSched<nf, eew, LInfo>;
          def nf # "E" # eew # "_V_" # LInfo # "_MASK" : VPseudoUSSegStoreMask<vreg, eew, nf>,
                                                         VSSEGSched<nf, eew, LInfo>;
        }
      }
    }
  }
}

multiclass VPseudoSSegStore {
  foreach eew = EEWList in {
    foreach lmul = MxSet<eew>.m in {
      defvar LInfo = lmul.MX;
      let VLMul = lmul.value, SEW=eew in {
        foreach nf = NFSet<lmul>.L in {
          defvar vreg = SegRegClass<lmul, nf>.RC;
          def nf # "E" # eew # "_V_" # LInfo : VPseudoSSegStoreNoMask<vreg, eew, nf>,
                                               VSSSEGSched<nf, eew, LInfo>;
          def nf # "E" # eew # "_V_" # LInfo # "_MASK" : VPseudoSSegStoreMask<vreg, eew, nf>,
                                                         VSSSEGSched<nf, eew, LInfo>;
        }
      }
    }
  }
}

multiclass VPseudoISegStore<bit Ordered> {
  foreach idxEEW = EEWList in {
    foreach dataEEW = EEWList in {
      foreach dataEMUL = MxSet<dataEEW>.m in {
        defvar dataEMULOctuple = dataEMUL.octuple;
        // Calculate emul = eew * lmul / sew
        defvar idxEMULOctuple = !srl(!mul(idxEEW, dataEMULOctuple), !logtwo(dataEEW));
        if !and(!ge(idxEMULOctuple, 1), !le(idxEMULOctuple, 64)) then {
          defvar DataLInfo = dataEMUL.MX;
          defvar IdxLInfo = octuple_to_str<idxEMULOctuple>.ret;
          defvar idxEMUL = !cast<LMULInfo>("V_" # IdxLInfo);
          defvar DataVreg = dataEMUL.vrclass;
          defvar IdxVreg = idxEMUL.vrclass;
          let VLMul = dataEMUL.value in {
            foreach nf = NFSet<dataEMUL>.L in {
              defvar Vreg = SegRegClass<dataEMUL, nf>.RC;
              def nf # "EI" # idxEEW # "_V_" # IdxLInfo # "_" # DataLInfo :
                VPseudoISegStoreNoMask<Vreg, IdxVreg, idxEEW, idxEMUL.value,
                                       nf, Ordered>,
                VSXSEGSched<nf, idxEEW, Ordered, DataLInfo>;
              def nf # "EI" # idxEEW # "_V_" # IdxLInfo # "_" # DataLInfo # "_MASK" :
                VPseudoISegStoreMask<Vreg, IdxVreg, idxEEW, idxEMUL.value,
                                     nf, Ordered>,
                VSXSEGSched<nf, idxEEW, Ordered, DataLInfo>;
            }
          }
        }
      }
    }
  }
}

//===----------------------------------------------------------------------===//
// Helpers to define the intrinsic patterns.
//===----------------------------------------------------------------------===//

class VPatUnaryNoMask<string intrinsic_name,
                      string inst,
                      string kind,
                      ValueType result_type,
                      ValueType op2_type,
                      int log2sew,
                      LMULInfo vlmul,
                      VReg result_reg_class,
                      VReg op2_reg_class,
                      bit isSEWAware = 0> :
  Pat<(result_type (!cast<Intrinsic>(intrinsic_name)
                   (result_type result_reg_class:$merge),
                   (op2_type op2_reg_class:$rs2),
                   VLOpFrag)),
                   (!cast<Instruction>(
                     !if(isSEWAware,
                         inst#"_"#kind#"_"#vlmul.MX#"_E"#!shl(1, log2sew),
                         inst#"_"#kind#"_"#vlmul.MX))
                   (result_type result_reg_class:$merge),
                   (op2_type op2_reg_class:$rs2),
                   GPR:$vl, log2sew, TU_MU)>;

class VPatUnaryNoMaskRoundingMode<string intrinsic_name,
                                  string inst,
                                  string kind,
                                  ValueType result_type,
                                  ValueType op2_type,
                                  int log2sew,
                                  LMULInfo vlmul,
                                  VReg result_reg_class,
                                  VReg op2_reg_class,
                                  bit isSEWAware = 0> :
  Pat<(result_type (!cast<Intrinsic>(intrinsic_name)
                   (result_type result_reg_class:$merge),
                   (op2_type op2_reg_class:$rs2),
                   (XLenVT timm:$round),
                   VLOpFrag)),
                   (!cast<Instruction>(
                      !if(isSEWAware,
                          inst#"_"#kind#"_"#vlmul.MX#"_E"#!shl(1, log2sew),
                          inst#"_"#kind#"_"#vlmul.MX))
                   (result_type result_reg_class:$merge),
                   (op2_type op2_reg_class:$rs2),
                   (XLenVT timm:$round),
                   GPR:$vl, log2sew, TU_MU)>;

class VPatUnaryNoMaskRTZ<string intrinsic_name,
                         string inst,
                         string kind,
                         ValueType result_type,
                         ValueType op2_type,
                         int log2sew,
                         LMULInfo vlmul,
                         VReg result_reg_class,
                         VReg op2_reg_class,
                         bit isSEWAware = 0> :
  Pat<(result_type (!cast<Intrinsic>(intrinsic_name)
                   (result_type result_reg_class:$merge),
                   (op2_type op2_reg_class:$rs2),
                   (XLenVT 0b001),
                   VLOpFrag)),
                   (!cast<Instruction>(
                      !if(isSEWAware,
                          inst#"_"#kind#"_"#vlmul.MX#"_E"#!shl(1, log2sew),
                          inst#"_"#kind#"_"#vlmul.MX))
                   (result_type result_reg_class:$merge),
                   (op2_type op2_reg_class:$rs2),
                   GPR:$vl, log2sew, TU_MU)>;

class VPatUnaryMask<string intrinsic_name,
                    string inst,
                    string kind,
                    ValueType result_type,
                    ValueType op2_type,
                    ValueType mask_type,
                    int log2sew,
                    LMULInfo vlmul,
                    VReg result_reg_class,
                    VReg op2_reg_class,
                    bit isSEWAware = 0> :
  Pat<(result_type (!cast<Intrinsic>(intrinsic_name#"_mask")
                   (result_type result_reg_class:$merge),
                   (op2_type op2_reg_class:$rs2),
                   (mask_type V0),
                   VLOpFrag, (XLenVT timm:$policy))),
                   (!cast<Instruction>(
                      !if(isSEWAware,
                          inst#"_"#kind#"_"#vlmul.MX#"_E"#!shl(1, log2sew)#"_MASK",
                          inst#"_"#kind#"_"#vlmul.MX#"_MASK"))
                   (result_type result_reg_class:$merge),
                   (op2_type op2_reg_class:$rs2),
                   (mask_type V0), GPR:$vl, log2sew, (XLenVT timm:$policy))>;

class VPatUnaryMaskRoundingMode<string intrinsic_name,
                                string inst,
                                string kind,
                                ValueType result_type,
                                ValueType op2_type,
                                ValueType mask_type,
                                int log2sew,
                                LMULInfo vlmul,
                                VReg result_reg_class,
                                VReg op2_reg_class,
                                bit isSEWAware = 0> :
  Pat<(result_type (!cast<Intrinsic>(intrinsic_name#"_mask")
                   (result_type result_reg_class:$merge),
                   (op2_type op2_reg_class:$rs2),
                   (mask_type V0),
                   (XLenVT timm:$round),
                   VLOpFrag, (XLenVT timm:$policy))),
                   (!cast<Instruction>(
                      !if(isSEWAware,
                          inst#"_"#kind#"_"#vlmul.MX#"_E"#!shl(1, log2sew)#"_MASK",
                          inst#"_"#kind#"_"#vlmul.MX#"_MASK"))
                   (result_type result_reg_class:$merge),
                   (op2_type op2_reg_class:$rs2),
                   (mask_type V0),
                   (XLenVT timm:$round),
                   GPR:$vl, log2sew, (XLenVT timm:$policy))>;

class VPatUnaryMaskRTZ<string intrinsic_name,
                       string inst,
                       string kind,
                       ValueType result_type,
                       ValueType op2_type,
                       ValueType mask_type,
                       int log2sew,
                       LMULInfo vlmul,
                       VReg result_reg_class,
                       VReg op2_reg_class,
                       bit isSEWAware = 0> :
  Pat<(result_type (!cast<Intrinsic>(intrinsic_name#"_mask")
                   (result_type result_reg_class:$merge),
                   (op2_type op2_reg_class:$rs2),
                   (mask_type V0),
                   (XLenVT 0b001),
                   VLOpFrag, (XLenVT timm:$policy))),
                   (!cast<Instruction>(
                      !if(isSEWAware,
                          inst#"_"#kind#"_"#vlmul.MX#"_E"#!shl(1, log2sew)#"_MASK",
                          inst#"_"#kind#"_"#vlmul.MX#"_MASK"))
                   (result_type result_reg_class:$merge),
                   (op2_type op2_reg_class:$rs2),
                   (mask_type V0),
                   GPR:$vl, log2sew, (XLenVT timm:$policy))>;

class VPatMaskUnaryNoMask<string intrinsic_name,
                          string inst,
                          MTypeInfo mti> :
  Pat<(mti.Mask (!cast<Intrinsic>(intrinsic_name)
                (mti.Mask VR:$rs2),
                VLOpFrag)),
                (!cast<Instruction>(inst#"_M_"#mti.BX)
                (mti.Mask VR:$rs2),
                GPR:$vl, mti.Log2SEW)>;

class VPatMaskUnaryMask<string intrinsic_name,
                        string inst,
                        MTypeInfo mti> :
  Pat<(mti.Mask (!cast<Intrinsic>(intrinsic_name#"_mask")
                (mti.Mask VR:$merge),
                (mti.Mask VR:$rs2),
                (mti.Mask V0),
                VLOpFrag)),
                (!cast<Instruction>(inst#"_M_"#mti.BX#"_MASK")
                (mti.Mask VR:$merge),
                (mti.Mask VR:$rs2),
                (mti.Mask V0), GPR:$vl, mti.Log2SEW, TU_MU)>;

class VPatUnaryAnyMask<string intrinsic,
                       string inst,
                       string kind,
                       ValueType result_type,
                       ValueType op1_type,
                       ValueType mask_type,
                       int log2sew,
                       LMULInfo vlmul,
                       VReg result_reg_class,
                       VReg op1_reg_class> :
  Pat<(result_type (!cast<Intrinsic>(intrinsic)
                   (result_type result_reg_class:$merge),
                   (op1_type op1_reg_class:$rs1),
                   (mask_type VR:$rs2),
                   VLOpFrag)),
                   (!cast<Instruction>(inst#"_"#kind#"_"#vlmul.MX#"_E"#!shl(1, log2sew))
                   (result_type result_reg_class:$merge),
                   (op1_type op1_reg_class:$rs1),
                   (mask_type VR:$rs2),
                   GPR:$vl, log2sew)>;

class VPatBinaryM<string intrinsic_name,
                  string inst,
                  ValueType result_type,
                  ValueType op1_type,
                  ValueType op2_type,
                  int sew,
                  VReg op1_reg_class,
                  DAGOperand op2_kind> :
  Pat<(result_type (!cast<Intrinsic>(intrinsic_name)
                   (op1_type op1_reg_class:$rs1),
                   (op2_type op2_kind:$rs2),
                   VLOpFrag)),
                   (!cast<Instruction>(inst)
                   (op1_type op1_reg_class:$rs1),
                   (op2_type op2_kind:$rs2),
                   GPR:$vl, sew)>;

class VPatBinaryNoMaskTU<string intrinsic_name,
                         string inst,
                         ValueType result_type,
                         ValueType op1_type,
                         ValueType op2_type,
                         int sew,
                         VReg result_reg_class,
                         VReg op1_reg_class,
                         DAGOperand op2_kind> :
  Pat<(result_type (!cast<Intrinsic>(intrinsic_name)
                   (result_type result_reg_class:$merge),
                   (op1_type op1_reg_class:$rs1),
                   (op2_type op2_kind:$rs2),
                   VLOpFrag)),
                   (!cast<Instruction>(inst)
                   (result_type result_reg_class:$merge),
                   (op1_type op1_reg_class:$rs1),
                   (op2_type op2_kind:$rs2),
                   GPR:$vl, sew, TU_MU)>;

class VPatBinaryNoMaskTURoundingMode<string intrinsic_name,
                                     string inst,
                                     ValueType result_type,
                                     ValueType op1_type,
                                     ValueType op2_type,
                                     int sew,
                                     VReg result_reg_class,
                                     VReg op1_reg_class,
                                     DAGOperand op2_kind> :
  Pat<(result_type (!cast<Intrinsic>(intrinsic_name)
                   (result_type result_reg_class:$merge),
                   (op1_type op1_reg_class:$rs1),
                   (op2_type op2_kind:$rs2),
                   (XLenVT timm:$round),
                   VLOpFrag)),
                   (!cast<Instruction>(inst)
                   (result_type result_reg_class:$merge),
                   (op1_type op1_reg_class:$rs1),
                   (op2_type op2_kind:$rs2),
                   (XLenVT timm:$round),
                   GPR:$vl, sew, TU_MU)>;


// Same as VPatBinaryM but source operands are swapped.
class VPatBinaryMSwapped<string intrinsic_name,
                         string inst,
                         ValueType result_type,
                         ValueType op1_type,
                         ValueType op2_type,
                         int sew,
                         VReg op1_reg_class,
                         DAGOperand op2_kind> :
  Pat<(result_type (!cast<Intrinsic>(intrinsic_name)
                   (op2_type op2_kind:$rs2),
                   (op1_type op1_reg_class:$rs1),
                   VLOpFrag)),
                   (!cast<Instruction>(inst)
                   (op1_type op1_reg_class:$rs1),
                   (op2_type op2_kind:$rs2),
                   GPR:$vl, sew)>;

class VPatBinaryMask<string intrinsic_name,
                     string inst,
                     ValueType result_type,
                     ValueType op1_type,
                     ValueType op2_type,
                     ValueType mask_type,
                     int sew,
                     VReg result_reg_class,
                     VReg op1_reg_class,
                     DAGOperand op2_kind> :
  Pat<(result_type (!cast<Intrinsic>(intrinsic_name#"_mask")
                   (result_type result_reg_class:$merge),
                   (op1_type op1_reg_class:$rs1),
                   (op2_type op2_kind:$rs2),
                   (mask_type V0),
                   VLOpFrag)),
                   (!cast<Instruction>(inst#"_MASK")
                   (result_type result_reg_class:$merge),
                   (op1_type op1_reg_class:$rs1),
                   (op2_type op2_kind:$rs2),
                   (mask_type V0), GPR:$vl, sew)>;

class VPatBinaryMaskPolicy<string intrinsic_name,
                           string inst,
                           ValueType result_type,
                           ValueType op1_type,
                           ValueType op2_type,
                           ValueType mask_type,
                           int sew,
                           VReg result_reg_class,
                           VReg op1_reg_class,
                           DAGOperand op2_kind> :
  Pat<(result_type (!cast<Intrinsic>(intrinsic_name#"_mask")
                   (result_type result_reg_class:$merge),
                   (op1_type op1_reg_class:$rs1),
                   (op2_type op2_kind:$rs2),
                   (mask_type V0),
                   VLOpFrag, (XLenVT timm:$policy))),
                   (!cast<Instruction>(inst#"_MASK")
                   (result_type result_reg_class:$merge),
                   (op1_type op1_reg_class:$rs1),
                   (op2_type op2_kind:$rs2),
                   (mask_type V0), GPR:$vl, sew, (XLenVT timm:$policy))>;

class VPatBinaryMaskPolicyRoundingMode<string intrinsic_name,
                                       string inst,
                                       ValueType result_type,
                                       ValueType op1_type,
                                       ValueType op2_type,
                                       ValueType mask_type,
                                       int sew,
                                       VReg result_reg_class,
                                       VReg op1_reg_class,
                                       DAGOperand op2_kind> :
  Pat<(result_type (!cast<Intrinsic>(intrinsic_name#"_mask")
                   (result_type result_reg_class:$merge),
                   (op1_type op1_reg_class:$rs1),
                   (op2_type op2_kind:$rs2),
                   (mask_type V0),
                   (XLenVT timm:$round),
                   VLOpFrag, (XLenVT timm:$policy))),
                   (!cast<Instruction>(inst#"_MASK")
                   (result_type result_reg_class:$merge),
                   (op1_type op1_reg_class:$rs1),
                   (op2_type op2_kind:$rs2),
                   (mask_type V0),
                   (XLenVT timm:$round),
                   GPR:$vl, sew, (XLenVT timm:$policy))>;

// Same as VPatBinaryMask but source operands are swapped.
class VPatBinaryMaskSwapped<string intrinsic_name,
                            string inst,
                            ValueType result_type,
                            ValueType op1_type,
                            ValueType op2_type,
                            ValueType mask_type,
                            int sew,
                            VReg result_reg_class,
                            VReg op1_reg_class,
                            DAGOperand op2_kind> :
  Pat<(result_type (!cast<Intrinsic>(intrinsic_name#"_mask")
                   (result_type result_reg_class:$merge),
                   (op2_type op2_kind:$rs2),
                   (op1_type op1_reg_class:$rs1),
                   (mask_type V0),
                   VLOpFrag)),
                   (!cast<Instruction>(inst#"_MASK")
                   (result_type result_reg_class:$merge),
                   (op1_type op1_reg_class:$rs1),
                   (op2_type op2_kind:$rs2),
                   (mask_type V0), GPR:$vl, sew)>;

class VPatTiedBinaryNoMask<string intrinsic_name,
                           string inst,
                           ValueType result_type,
                           ValueType op2_type,
                           int sew,
                           VReg result_reg_class,
                           DAGOperand op2_kind> :
  Pat<(result_type (!cast<Intrinsic>(intrinsic_name)
                   (result_type (undef)),
                   (result_type result_reg_class:$rs1),
                   (op2_type op2_kind:$rs2),
                   VLOpFrag)),
                   (!cast<Instruction>(inst#"_TIED")
                   (result_type result_reg_class:$rs1),
                   (op2_type op2_kind:$rs2),
                   GPR:$vl, sew, TAIL_AGNOSTIC)>;

class VPatTiedBinaryNoMaskRoundingMode<string intrinsic_name,
                                       string inst,
                                       ValueType result_type,
                                       ValueType op2_type,
                                       int sew,
                                       VReg result_reg_class,
                                       DAGOperand op2_kind> :
  Pat<(result_type (!cast<Intrinsic>(intrinsic_name)
                   (result_type (undef)),
                   (result_type result_reg_class:$rs1),
                   (op2_type op2_kind:$rs2),
                   (XLenVT timm:$round),
                   VLOpFrag)),
                   (!cast<Instruction>(inst#"_TIED")
                   (result_type result_reg_class:$rs1),
                   (op2_type op2_kind:$rs2),
                   (XLenVT timm:$round),
                   GPR:$vl, sew, TAIL_AGNOSTIC)>;

class VPatTiedBinaryNoMaskTU<string intrinsic_name,
                             string inst,
                             ValueType result_type,
                             ValueType op2_type,
                             int sew,
                             VReg result_reg_class,
                             DAGOperand op2_kind> :
  Pat<(result_type (!cast<Intrinsic>(intrinsic_name)
                   (result_type result_reg_class:$merge),
                   (result_type result_reg_class:$merge),
                   (op2_type op2_kind:$rs2),
                   VLOpFrag)),
                   (!cast<Instruction>(inst#"_TIED")
                   (result_type result_reg_class:$merge),
                   (op2_type op2_kind:$rs2),
                   GPR:$vl, sew, TU_MU)>;

class VPatTiedBinaryNoMaskTURoundingMode<string intrinsic_name,
                                         string inst,
                                         ValueType result_type,
                                         ValueType op2_type,
                                         int sew,
                                         VReg result_reg_class,
                                         DAGOperand op2_kind> :
  Pat<(result_type (!cast<Intrinsic>(intrinsic_name)
                   (result_type result_reg_class:$merge),
                   (result_type result_reg_class:$merge),
                   (op2_type op2_kind:$rs2),
                   (XLenVT timm:$round),
                   VLOpFrag)),
                   (!cast<Instruction>(inst#"_TIED")
                   (result_type result_reg_class:$merge),
                   (op2_type op2_kind:$rs2),
                   (XLenVT timm:$round),
                   GPR:$vl, sew, TU_MU)>;

class VPatTiedBinaryMask<string intrinsic_name,
                         string inst,
                         ValueType result_type,
                         ValueType op2_type,
                         ValueType mask_type,
                         int sew,
                         VReg result_reg_class,
                         DAGOperand op2_kind> :
  Pat<(result_type (!cast<Intrinsic>(intrinsic_name#"_mask")
                   (result_type result_reg_class:$merge),
                   (result_type result_reg_class:$merge),
                   (op2_type op2_kind:$rs2),
                   (mask_type V0),
                   VLOpFrag, (XLenVT timm:$policy))),
                   (!cast<Instruction>(inst#"_MASK_TIED")
                   (result_type result_reg_class:$merge),
                   (op2_type op2_kind:$rs2),
                   (mask_type V0), GPR:$vl, sew, (XLenVT timm:$policy))>;

class VPatTiedBinaryMaskRoundingMode<string intrinsic_name,
                                     string inst,
                                     ValueType result_type,
                                     ValueType op2_type,
                                     ValueType mask_type,
                                     int sew,
                                     VReg result_reg_class,
                                     DAGOperand op2_kind> :
  Pat<(result_type (!cast<Intrinsic>(intrinsic_name#"_mask")
                   (result_type result_reg_class:$merge),
                   (result_type result_reg_class:$merge),
                   (op2_type op2_kind:$rs2),
                   (mask_type V0),
                   (XLenVT timm:$round),
                   VLOpFrag, (XLenVT timm:$policy))),
                   (!cast<Instruction>(inst#"_MASK_TIED")
                   (result_type result_reg_class:$merge),
                   (op2_type op2_kind:$rs2),
                   (mask_type V0),
                   (XLenVT timm:$round),
                   GPR:$vl, sew, (XLenVT timm:$policy))>;

class VPatTernaryNoMaskTU<string intrinsic,
                          string inst,
                          string kind,
                          ValueType result_type,
                          ValueType op1_type,
                          ValueType op2_type,
                          int log2sew,
                          LMULInfo vlmul,
                          VReg result_reg_class,
                          RegisterClass op1_reg_class,
                          DAGOperand op2_kind> :
  Pat<(result_type (!cast<Intrinsic>(intrinsic)
                    (result_type result_reg_class:$rs3),
                    (op1_type op1_reg_class:$rs1),
                    (op2_type op2_kind:$rs2),
                    VLOpFrag)),
                   (!cast<Instruction>(inst#"_"#kind#"_"#vlmul.MX#"_E"#!shl(1, log2sew))
                    result_reg_class:$rs3,
                    (op1_type op1_reg_class:$rs1),
                    op2_kind:$rs2,
                    GPR:$vl, log2sew, TU_MU)>;

class VPatTernaryNoMaskTURoundingMode<string intrinsic,
                                      string inst,
                                      string kind,
                                      ValueType result_type,
                                      ValueType op1_type,
                                      ValueType op2_type,
                                      int log2sew,
                                      LMULInfo vlmul,
                                      VReg result_reg_class,
                                      RegisterClass op1_reg_class,
                                      DAGOperand op2_kind> :
  Pat<(result_type (!cast<Intrinsic>(intrinsic)
                    (result_type result_reg_class:$rs3),
                    (op1_type op1_reg_class:$rs1),
                    (op2_type op2_kind:$rs2),
                    (XLenVT timm:$round),
                    VLOpFrag)),
                   (!cast<Instruction>(inst#"_"#kind#"_"#vlmul.MX#"_E"#!shl(1, log2sew))
                    result_reg_class:$rs3,
                    (op1_type op1_reg_class:$rs1),
                    op2_kind:$rs2,
                    (XLenVT timm:$round),
                    GPR:$vl, log2sew, TU_MU)>;

class VPatTernaryNoMaskWithPolicy<string intrinsic,
                                  string inst,
                                  string kind,
                                  ValueType result_type,
                                  ValueType op1_type,
                                  ValueType op2_type,
                                  int sew,
                                  LMULInfo vlmul,
                                  VReg result_reg_class,
                                  RegisterClass op1_reg_class,
                                  DAGOperand op2_kind> :
  Pat<(result_type (!cast<Intrinsic>(intrinsic)
                    (result_type result_reg_class:$rs3),
                    (op1_type op1_reg_class:$rs1),
                    (op2_type op2_kind:$rs2),
                    VLOpFrag, (XLenVT timm:$policy))),
                   (!cast<Instruction>(inst#"_"#kind#"_"#vlmul.MX)
                    result_reg_class:$rs3,
                    (op1_type op1_reg_class:$rs1),
                    op2_kind:$rs2,
                    GPR:$vl, sew, (XLenVT timm:$policy))>;

class VPatTernaryNoMaskWithPolicyRoundingMode<string intrinsic,
                                  string inst,
                                  string kind,
                                  ValueType result_type,
                                  ValueType op1_type,
                                  ValueType op2_type,
                                  int log2sew,
                                  LMULInfo vlmul,
                                  VReg result_reg_class,
                                  RegisterClass op1_reg_class,
                                  DAGOperand op2_kind,
                                  bit isSEWAware = 0> :
  Pat<(result_type (!cast<Intrinsic>(intrinsic)
                    (result_type result_reg_class:$rs3),
                    (op1_type op1_reg_class:$rs1),
                    (op2_type op2_kind:$rs2),
                    (XLenVT timm:$round),
                    VLOpFrag, (XLenVT timm:$policy))),
                   (!cast<Instruction>(!if(isSEWAware,
                          inst#"_"#kind#"_"#vlmul.MX#"_E"#!shl(1, log2sew),
                          inst#"_"#kind#"_"#vlmul.MX))
                    result_reg_class:$rs3,
                    (op1_type op1_reg_class:$rs1),
                    op2_kind:$rs2,
                    (XLenVT timm:$round),
                    GPR:$vl, log2sew, (XLenVT timm:$policy))>;

class VPatTernaryMaskPolicy<string intrinsic,
                            string inst,
                            string kind,
                            ValueType result_type,
                            ValueType op1_type,
                            ValueType op2_type,
                            ValueType mask_type,
                            int sew,
                            LMULInfo vlmul,
                            VReg result_reg_class,
                            RegisterClass op1_reg_class,
                            DAGOperand op2_kind> :
  Pat<(result_type (!cast<Intrinsic>(intrinsic#"_mask")
                    (result_type result_reg_class:$rs3),
                    (op1_type op1_reg_class:$rs1),
                    (op2_type op2_kind:$rs2),
                    (mask_type V0),
                    VLOpFrag, (XLenVT timm:$policy))),
                   (!cast<Instruction>(inst#"_"#kind#"_"#vlmul.MX # "_MASK")
                    result_reg_class:$rs3,
                    (op1_type op1_reg_class:$rs1),
                    op2_kind:$rs2,
                    (mask_type V0),
                    GPR:$vl, sew, (XLenVT timm:$policy))>;

class VPatTernaryMaskPolicyRoundingMode<string intrinsic,
                                        string inst,
                                        string kind,
                                        ValueType result_type,
                                        ValueType op1_type,
                                        ValueType op2_type,
                                        ValueType mask_type,
                                        int log2sew,
                                        LMULInfo vlmul,
                                        VReg result_reg_class,
                                        RegisterClass op1_reg_class,
                                        DAGOperand op2_kind,
                                        bit isSEWAware = 0> :
  Pat<(result_type (!cast<Intrinsic>(intrinsic#"_mask")
                    (result_type result_reg_class:$rs3),
                    (op1_type op1_reg_class:$rs1),
                    (op2_type op2_kind:$rs2),
                    (mask_type V0),
                    (XLenVT timm:$round),
                    VLOpFrag, (XLenVT timm:$policy))),
                   (!cast<Instruction>(!if(isSEWAware,
                          inst#"_"#kind#"_"#vlmul.MX#"_E"#!shl(1, log2sew) # "_MASK",
                          inst#"_"#kind#"_"#vlmul.MX # "_MASK"))
                    result_reg_class:$rs3,
                    (op1_type op1_reg_class:$rs1),
                    op2_kind:$rs2,
                    (mask_type V0),
                    (XLenVT timm:$round),
                    GPR:$vl, log2sew, (XLenVT timm:$policy))>;

class VPatTernaryMaskTU<string intrinsic,
                        string inst,
                        string kind,
                        ValueType result_type,
                        ValueType op1_type,
                        ValueType op2_type,
                        ValueType mask_type,
                        int log2sew,
                        LMULInfo vlmul,
                        VReg result_reg_class,
                        RegisterClass op1_reg_class,
                        DAGOperand op2_kind> :
  Pat<(result_type (!cast<Intrinsic>(intrinsic#"_mask")
                    (result_type result_reg_class:$rs3),
                    (op1_type op1_reg_class:$rs1),
                    (op2_type op2_kind:$rs2),
                    (mask_type V0),
                    VLOpFrag)),
                   (!cast<Instruction>(inst#"_"#kind#"_"#vlmul.MX#"_E"#!shl(1, log2sew)# "_MASK")
                    result_reg_class:$rs3,
                    (op1_type op1_reg_class:$rs1),
                    op2_kind:$rs2,
                    (mask_type V0),
                    GPR:$vl, log2sew, TU_MU)>;

class VPatTernaryMaskTURoundingMode<string intrinsic,
                                    string inst,
                                    string kind,
                                    ValueType result_type,
                                    ValueType op1_type,
                                    ValueType op2_type,
                                    ValueType mask_type,
                                    int log2sew,
                                    LMULInfo vlmul,
                                    VReg result_reg_class,
                                    RegisterClass op1_reg_class,
                                    DAGOperand op2_kind> :
  Pat<(result_type (!cast<Intrinsic>(intrinsic#"_mask")
                    (result_type result_reg_class:$rs3),
                    (op1_type op1_reg_class:$rs1),
                    (op2_type op2_kind:$rs2),
                    (mask_type V0),
                    (XLenVT timm:$round),
                    VLOpFrag)),
                   (!cast<Instruction>(inst#"_"#kind#"_"#vlmul.MX#"_E"#!shl(1, log2sew)# "_MASK")
                    result_reg_class:$rs3,
                    (op1_type op1_reg_class:$rs1),
                    op2_kind:$rs2,
                    (mask_type V0),
                    (XLenVT timm:$round),
                    GPR:$vl, log2sew, TU_MU)>;

multiclass VPatUnaryS_M<string intrinsic_name,
                             string inst> {
  foreach mti = AllMasks in {
    def : Pat<(XLenVT (!cast<Intrinsic>(intrinsic_name)
                      (mti.Mask VR:$rs1), VLOpFrag)),
                      (!cast<Instruction>(inst#"_M_"#mti.BX) $rs1,
                      GPR:$vl, mti.Log2SEW)>;
    def : Pat<(XLenVT (!cast<Intrinsic>(intrinsic_name # "_mask")
                      (mti.Mask VR:$rs1), (mti.Mask V0), VLOpFrag)),
                      (!cast<Instruction>(inst#"_M_"#mti.BX#"_MASK") $rs1,
                      (mti.Mask V0), GPR:$vl, mti.Log2SEW)>;
  }
}

multiclass VPatUnaryV_V_AnyMask<string intrinsic, string instruction,
                                list<VTypeInfo> vtilist> {
  foreach vti = vtilist in {
    let Predicates = GetVTypePredicates<vti>.Predicates in
    def : VPatUnaryAnyMask<intrinsic, instruction, "VM",
                           vti.Vector, vti.Vector, vti.Mask,
                           vti.Log2SEW, vti.LMul, vti.RegClass, vti.RegClass>;
  }
}

multiclass VPatUnaryM_M<string intrinsic,
                         string inst> {
  foreach mti = AllMasks in {
    def : VPatMaskUnaryNoMask<intrinsic, inst, mti>;
    def : VPatMaskUnaryMask<intrinsic, inst, mti>;
  }
}

multiclass VPatUnaryV_M<string intrinsic, string instruction> {
  foreach vti = AllIntegerVectors in {
    let Predicates = GetVTypePredicates<vti>.Predicates in {
      def : VPatUnaryNoMask<intrinsic, instruction, "M", vti.Vector, vti.Mask,
                            vti.Log2SEW, vti.LMul, vti.RegClass, VR>;
      def : VPatUnaryMask<intrinsic, instruction, "M", vti.Vector, vti.Mask,
                          vti.Mask, vti.Log2SEW, vti.LMul, vti.RegClass, VR>;
    }
  }
}

multiclass VPatUnaryV_VF<string intrinsic, string instruction, string suffix,
                         list<VTypeInfoToFraction> fractionList> {
  foreach vtiTofti = fractionList in {
      defvar vti = vtiTofti.Vti;
      defvar fti = vtiTofti.Fti;
      let Predicates = !listconcat(GetVTypePredicates<vti>.Predicates,
                                   GetVTypePredicates<fti>.Predicates) in {
        def : VPatUnaryNoMask<intrinsic, instruction, suffix,
                              vti.Vector, fti.Vector,
                              vti.Log2SEW, vti.LMul, vti.RegClass, fti.RegClass>;
        def : VPatUnaryMask<intrinsic, instruction, suffix,
                            vti.Vector, fti.Vector, vti.Mask,
                            vti.Log2SEW, vti.LMul, vti.RegClass, fti.RegClass>;
      }
  }
}

multiclass VPatUnaryV_V<string intrinsic, string instruction,
                        list<VTypeInfo> vtilist, bit isSEWAware = 0> {
  foreach vti = vtilist in {
    let Predicates = GetVTypePredicates<vti>.Predicates in {
      def : VPatUnaryNoMask<intrinsic, instruction, "V",
                            vti.Vector, vti.Vector, vti.Log2SEW,
                            vti.LMul, vti.RegClass, vti.RegClass, isSEWAware>;
      def : VPatUnaryMask<intrinsic, instruction, "V",
                          vti.Vector, vti.Vector, vti.Mask, vti.Log2SEW,
                          vti.LMul, vti.RegClass, vti.RegClass, isSEWAware>;
    }
  }
}

multiclass VPatUnaryV_V_RM<string intrinsic, string instruction,
                        list<VTypeInfo> vtilist, bit isSEWAware = 0> {
  foreach vti = vtilist in {
    let Predicates = GetVTypePredicates<vti>.Predicates in {
      def : VPatUnaryNoMaskRoundingMode<intrinsic, instruction, "V",
                                        vti.Vector, vti.Vector, vti.Log2SEW,
                                        vti.LMul, vti.RegClass, vti.RegClass, isSEWAware>;
      def : VPatUnaryMaskRoundingMode<intrinsic, instruction, "V",
                                      vti.Vector, vti.Vector, vti.Mask, vti.Log2SEW,
                                      vti.LMul, vti.RegClass, vti.RegClass, isSEWAware>;
    }
  }
}

multiclass VPatNullaryV<string intrinsic, string instruction> {
  foreach vti = AllIntegerVectors in {
    let Predicates = GetVTypePredicates<vti>.Predicates in {
      def : Pat<(vti.Vector (!cast<Intrinsic>(intrinsic)
                            (vti.Vector vti.RegClass:$merge),
                            VLOpFrag)),
                            (!cast<Instruction>(instruction#"_V_" # vti.LMul.MX)
                            vti.RegClass:$merge, GPR:$vl, vti.Log2SEW, TU_MU)>;
      def : Pat<(vti.Vector (!cast<Intrinsic>(intrinsic # "_mask")
                            (vti.Vector vti.RegClass:$merge),
                            (vti.Mask V0), VLOpFrag, (XLenVT timm:$policy))),
                            (!cast<Instruction>(instruction#"_V_" # vti.LMul.MX # "_MASK")
                            vti.RegClass:$merge, (vti.Mask V0),
                            GPR:$vl, vti.Log2SEW, (XLenVT timm:$policy))>;
  }
  }
}

multiclass VPatNullaryM<string intrinsic, string inst> {
  foreach mti = AllMasks in
    def : Pat<(mti.Mask (!cast<Intrinsic>(intrinsic)
                        VLOpFrag)),
                        (!cast<Instruction>(inst#"_M_"#mti.BX)
                        GPR:$vl, mti.Log2SEW)>;
}

multiclass VPatBinaryM<string intrinsic,
                      string inst,
                      ValueType result_type,
                      ValueType op1_type,
                      ValueType op2_type,
                      ValueType mask_type,
                      int sew,
                      VReg result_reg_class,
                      VReg op1_reg_class,
                      DAGOperand op2_kind> {
  def : VPatBinaryM<intrinsic, inst, result_type, op1_type, op2_type,
                    sew, op1_reg_class, op2_kind>;
  def : VPatBinaryMask<intrinsic, inst, result_type, op1_type, op2_type,
                       mask_type, sew, result_reg_class, op1_reg_class,
                       op2_kind>;
}

multiclass VPatBinary<string intrinsic,
                      string inst,
                      ValueType result_type,
                      ValueType op1_type,
                      ValueType op2_type,
                      ValueType mask_type,
                      int sew,
                      VReg result_reg_class,
                      VReg op1_reg_class,
                      DAGOperand op2_kind> {
  def : VPatBinaryNoMaskTU<intrinsic, inst, result_type, op1_type, op2_type,
                           sew, result_reg_class, op1_reg_class, op2_kind>;
  def : VPatBinaryMaskPolicy<intrinsic, inst, result_type, op1_type, op2_type,
                             mask_type, sew, result_reg_class, op1_reg_class,
                             op2_kind>;
}

multiclass VPatBinaryRoundingMode<string intrinsic,
                                  string inst,
                                  ValueType result_type,
                                  ValueType op1_type,
                                  ValueType op2_type,
                                  ValueType mask_type,
                                  int sew,
                                  VReg result_reg_class,
                                  VReg op1_reg_class,
                                  DAGOperand op2_kind> {
  def : VPatBinaryNoMaskTURoundingMode<intrinsic, inst, result_type, op1_type, op2_type,
                                       sew, result_reg_class, op1_reg_class, op2_kind>;
  def : VPatBinaryMaskPolicyRoundingMode<intrinsic, inst, result_type, op1_type, op2_type,
                                         mask_type, sew, result_reg_class, op1_reg_class,
                                         op2_kind>;
}

multiclass VPatBinaryMSwapped<string intrinsic,
                              string inst,
                              ValueType result_type,
                              ValueType op1_type,
                              ValueType op2_type,
                              ValueType mask_type,
                              int sew,
                              VReg result_reg_class,
                              VReg op1_reg_class,
                              DAGOperand op2_kind> {
  def : VPatBinaryMSwapped<intrinsic, inst, result_type, op1_type, op2_type,
                           sew, op1_reg_class, op2_kind>;
  def : VPatBinaryMaskSwapped<intrinsic, inst, result_type, op1_type, op2_type,
                              mask_type, sew, result_reg_class, op1_reg_class,
                              op2_kind>;
}

multiclass VPatBinaryCarryInTAIL<string intrinsic,
                                 string inst,
                                 string kind,
                                 ValueType result_type,
                                 ValueType op1_type,
                                 ValueType op2_type,
                                 ValueType mask_type,
                                 int sew,
                                 LMULInfo vlmul,
                                 VReg result_reg_class,
                                 VReg op1_reg_class,
                                 DAGOperand op2_kind> {
  def : Pat<(result_type (!cast<Intrinsic>(intrinsic)
                         (result_type result_reg_class:$merge),
                         (op1_type op1_reg_class:$rs1),
                         (op2_type op2_kind:$rs2),
                         (mask_type V0),
                         VLOpFrag)),
                         (!cast<Instruction>(inst#"_"#kind#"_"#vlmul.MX)
                         (result_type result_reg_class:$merge),
                         (op1_type op1_reg_class:$rs1),
                         (op2_type op2_kind:$rs2),
                         (mask_type V0), GPR:$vl, sew)>;
}

multiclass VPatBinaryCarryIn<string intrinsic,
                             string inst,
                             string kind,
                             ValueType result_type,
                             ValueType op1_type,
                             ValueType op2_type,
                             ValueType mask_type,
                             int sew,
                             LMULInfo vlmul,
                             VReg op1_reg_class,
                             DAGOperand op2_kind> {
  def : Pat<(result_type (!cast<Intrinsic>(intrinsic)
                         (op1_type op1_reg_class:$rs1),
                         (op2_type op2_kind:$rs2),
                         (mask_type V0),
                         VLOpFrag)),
                         (!cast<Instruction>(inst#"_"#kind#"_"#vlmul.MX)
                         (op1_type op1_reg_class:$rs1),
                         (op2_type op2_kind:$rs2),
                         (mask_type V0), GPR:$vl, sew)>;
}

multiclass VPatBinaryMaskOut<string intrinsic,
                             string inst,
                             string kind,
                             ValueType result_type,
                             ValueType op1_type,
                             ValueType op2_type,
                             int sew,
                             LMULInfo vlmul,
                             VReg op1_reg_class,
                             DAGOperand op2_kind> {
  def : Pat<(result_type (!cast<Intrinsic>(intrinsic)
                         (op1_type op1_reg_class:$rs1),
                         (op2_type op2_kind:$rs2),
                         VLOpFrag)),
                         (!cast<Instruction>(inst#"_"#kind#"_"#vlmul.MX)
                         (op1_type op1_reg_class:$rs1),
                         (op2_type op2_kind:$rs2),
                         GPR:$vl, sew)>;
}

multiclass VPatConversion<string intrinsic,
                          string inst,
                          string kind,
                          ValueType result_type,
                          ValueType op1_type,
                          ValueType mask_type,
                          int log2sew,
                          LMULInfo vlmul,
                          VReg result_reg_class,
                          VReg op1_reg_class,
                          bit isSEWAware = 0> {
  def : VPatUnaryNoMask<intrinsic, inst, kind, result_type, op1_type,
                        log2sew, vlmul, result_reg_class, op1_reg_class,
                        isSEWAware>;
  def : VPatUnaryMask<intrinsic, inst, kind, result_type, op1_type,
                      mask_type, log2sew, vlmul, result_reg_class, op1_reg_class,
                      isSEWAware>;
}

multiclass VPatConversionRoundingMode<string intrinsic,
                                      string inst,
                                      string kind,
                                      ValueType result_type,
                                      ValueType op1_type,
                                      ValueType mask_type,
                                      int log2sew,
                                      LMULInfo vlmul,
                                      VReg result_reg_class,
                                      VReg op1_reg_class,
                                      bit isSEWAware = 0> {
  def : VPatUnaryNoMaskRoundingMode<intrinsic, inst, kind, result_type, op1_type,
                                    log2sew, vlmul, result_reg_class,
                                    op1_reg_class, isSEWAware>;
  def : VPatUnaryMaskRoundingMode<intrinsic, inst, kind, result_type, op1_type,
                                  mask_type, log2sew, vlmul, result_reg_class,
                                  op1_reg_class, isSEWAware>;
}

multiclass VPatConversionRTZ<string intrinsic,
                             string inst,
                             string kind,
                             ValueType result_type,
                             ValueType op1_type,
                             ValueType mask_type,
                             int log2sew,
                             LMULInfo vlmul,
                             VReg result_reg_class,
                             VReg op1_reg_class,
                             bit isSEWAware = 0> {
  def : VPatUnaryNoMaskRTZ<intrinsic, inst, kind, result_type, op1_type,
                                    log2sew, vlmul, result_reg_class,
                                    op1_reg_class, isSEWAware>;
  def : VPatUnaryMaskRTZ<intrinsic, inst, kind, result_type, op1_type,
                                  mask_type, log2sew, vlmul, result_reg_class,
                                  op1_reg_class, isSEWAware>;
}

multiclass VPatBinaryV_VV<string intrinsic, string instruction,
                          list<VTypeInfo> vtilist, bit isSEWAware = 0> {
  foreach vti = vtilist in
    let Predicates = GetVTypePredicates<vti>.Predicates in
    defm : VPatBinary<intrinsic,
                      !if(isSEWAware,
                          instruction # "_VV_" # vti.LMul.MX # "_E" # vti.SEW,
                          instruction # "_VV_" # vti.LMul.MX),
                      vti.Vector, vti.Vector, vti.Vector,vti.Mask,
                      vti.Log2SEW, vti.RegClass,
                      vti.RegClass, vti.RegClass>;
}

multiclass VPatBinaryV_VV_RM<string intrinsic, string instruction,
                             list<VTypeInfo> vtilist, bit isSEWAware = 0> {
  foreach vti = vtilist in
    let Predicates = GetVTypePredicates<vti>.Predicates in
    defm : VPatBinaryRoundingMode<intrinsic,
                                  !if(isSEWAware,
                                      instruction # "_VV_" # vti.LMul.MX # "_E" # vti.SEW,
                                      instruction # "_VV_" # vti.LMul.MX),
                                  vti.Vector, vti.Vector, vti.Vector,vti.Mask,
                                  vti.Log2SEW, vti.RegClass,
                                  vti.RegClass, vti.RegClass>;
}

multiclass VPatBinaryV_VV_INT<string intrinsic, string instruction,
                              list<VTypeInfo> vtilist> {
  foreach vti = vtilist in {
    defvar ivti = GetIntVTypeInfo<vti>.Vti;
    let Predicates = GetVTypePredicates<vti>.Predicates in
    defm : VPatBinary<intrinsic,
                      instruction # "_VV_" # vti.LMul.MX # "_E" # vti.SEW,
                      vti.Vector, vti.Vector, ivti.Vector, vti.Mask,
                      vti.Log2SEW, vti.RegClass,
                      vti.RegClass, vti.RegClass>;
  }
}

multiclass VPatBinaryV_VV_INT_EEW<string intrinsic, string instruction,
                                  int eew, list<VTypeInfo> vtilist> {
  foreach vti = vtilist in {
    // emul = lmul * eew / sew
    defvar vlmul = vti.LMul;
    defvar octuple_lmul = vlmul.octuple;
    defvar octuple_emul = !srl(!mul(octuple_lmul, eew), vti.Log2SEW);
    if !and(!ge(octuple_emul, 1), !le(octuple_emul, 64)) then {
      defvar emul_str = octuple_to_str<octuple_emul>.ret;
      defvar ivti = !cast<VTypeInfo>("VI" # eew # emul_str);
      defvar inst = instruction # "_VV_" # vti.LMul.MX # "_E" # vti.SEW # "_" # emul_str;
      let Predicates = !listconcat(GetVTypePredicates<vti>.Predicates,
                                   GetVTypePredicates<ivti>.Predicates) in
      defm : VPatBinary<intrinsic, inst,
                        vti.Vector, vti.Vector, ivti.Vector, vti.Mask,
                        vti.Log2SEW, vti.RegClass,
                        vti.RegClass, ivti.RegClass>;
    }
  }
}

multiclass VPatBinaryV_VX<string intrinsic, string instruction,
                          list<VTypeInfo> vtilist, bit isSEWAware = 0> {
  foreach vti = vtilist in {
    defvar kind = "V"#vti.ScalarSuffix;
    let Predicates = GetVTypePredicates<vti>.Predicates in
    defm : VPatBinary<intrinsic,
                      !if(isSEWAware,
                          instruction#"_"#kind#"_"#vti.LMul.MX#"_E"#vti.SEW,
                          instruction#"_"#kind#"_"#vti.LMul.MX),
                      vti.Vector, vti.Vector, vti.Scalar, vti.Mask,
                      vti.Log2SEW, vti.RegClass,
                      vti.RegClass, vti.ScalarRegClass>;
  }
}

multiclass VPatBinaryV_VX_RM<string intrinsic, string instruction,
                             list<VTypeInfo> vtilist, bit isSEWAware = 0> {
  foreach vti = vtilist in {
    defvar kind = "V"#vti.ScalarSuffix;
    let Predicates = GetVTypePredicates<vti>.Predicates in
    defm : VPatBinaryRoundingMode<intrinsic,
                                  !if(isSEWAware,
                                      instruction#"_"#kind#"_"#vti.LMul.MX#"_E"#vti.SEW,
                                      instruction#"_"#kind#"_"#vti.LMul.MX),
                                  vti.Vector, vti.Vector, vti.Scalar, vti.Mask,
                                  vti.Log2SEW, vti.RegClass,
                                  vti.RegClass, vti.ScalarRegClass>;
  }
}

multiclass VPatBinaryV_VX_INT<string intrinsic, string instruction,
                          list<VTypeInfo> vtilist> {
  foreach vti = vtilist in
    let Predicates = GetVTypePredicates<vti>.Predicates in
    defm : VPatBinary<intrinsic, instruction # "_VX_" # vti.LMul.MX,
                      vti.Vector, vti.Vector, XLenVT, vti.Mask,
                      vti.Log2SEW, vti.RegClass,
                      vti.RegClass, GPR>;
}

multiclass VPatBinaryV_VI<string intrinsic, string instruction,
                          list<VTypeInfo> vtilist, Operand imm_type> {
  foreach vti = vtilist in
    let Predicates = GetVTypePredicates<vti>.Predicates in
    defm : VPatBinary<intrinsic, instruction # "_VI_" # vti.LMul.MX,
                      vti.Vector, vti.Vector, XLenVT, vti.Mask,
                      vti.Log2SEW, vti.RegClass,
                      vti.RegClass, imm_type>;
}

multiclass VPatBinaryV_VI_RM<string intrinsic, string instruction,
                             list<VTypeInfo> vtilist,
                             Operand imm_type> {
  foreach vti = vtilist in
    let Predicates = GetVTypePredicates<vti>.Predicates in
    defm : VPatBinaryRoundingMode<intrinsic,
                                  instruction # "_VI_" # vti.LMul.MX,
                                  vti.Vector, vti.Vector, XLenVT, vti.Mask,
                                  vti.Log2SEW, vti.RegClass,
                                  vti.RegClass, imm_type>;
}

multiclass VPatBinaryM_MM<string intrinsic, string instruction> {
  foreach mti = AllMasks in
    let Predicates = [HasVInstructions] in
    def : VPatBinaryM<intrinsic, instruction # "_MM_" # mti.LMul.MX,
                      mti.Mask, mti.Mask, mti.Mask,
                      mti.Log2SEW, VR, VR>;
}

multiclass VPatBinaryW_VV<string intrinsic, string instruction,
                          list<VTypeInfoToWide> vtilist> {
  foreach VtiToWti = vtilist in {
    defvar Vti = VtiToWti.Vti;
    defvar Wti = VtiToWti.Wti;
    let Predicates = !listconcat(GetVTypePredicates<Vti>.Predicates,
                                 GetVTypePredicates<Wti>.Predicates) in
    defm : VPatBinary<intrinsic, instruction # "_VV_" # Vti.LMul.MX,
                      Wti.Vector, Vti.Vector, Vti.Vector, Vti.Mask,
                      Vti.Log2SEW, Wti.RegClass,
                      Vti.RegClass, Vti.RegClass>;
  }
}

multiclass VPatBinaryW_VV_RM<string intrinsic, string instruction,
                             list<VTypeInfoToWide> vtilist, bit isSEWAware = 0> {
  foreach VtiToWti = vtilist in {
    defvar Vti = VtiToWti.Vti;
    defvar Wti = VtiToWti.Wti;
    defvar name = !if(isSEWAware,
                      instruction # "_VV_" # Vti.LMul.MX # "_E" # Vti.SEW,
                      instruction # "_VV_" # Vti.LMul.MX);
    let Predicates = !listconcat(GetVTypePredicates<Vti>.Predicates,
                                 GetVTypePredicates<Wti>.Predicates) in
    defm : VPatBinaryRoundingMode<intrinsic, name,
                                  Wti.Vector, Vti.Vector, Vti.Vector, Vti.Mask,
                                  Vti.Log2SEW, Wti.RegClass,
                                  Vti.RegClass, Vti.RegClass>;
  }
}

multiclass VPatBinaryW_VX<string intrinsic, string instruction,
                          list<VTypeInfoToWide> vtilist> {
  foreach VtiToWti = vtilist in {
    defvar Vti = VtiToWti.Vti;
    defvar Wti = VtiToWti.Wti;
    defvar kind = "V"#Vti.ScalarSuffix;
    let Predicates = !listconcat(GetVTypePredicates<Vti>.Predicates,
                                 GetVTypePredicates<Wti>.Predicates) in
    defm : VPatBinary<intrinsic, instruction#"_"#kind#"_"#Vti.LMul.MX,
                      Wti.Vector, Vti.Vector, Vti.Scalar, Vti.Mask,
                      Vti.Log2SEW, Wti.RegClass,
                      Vti.RegClass, Vti.ScalarRegClass>;
  }
}

multiclass VPatBinaryW_VX_RM<string intrinsic, string instruction,
                          list<VTypeInfoToWide> vtilist, bit isSEWAware = 0> {
  foreach VtiToWti = vtilist in {
    defvar Vti = VtiToWti.Vti;
    defvar Wti = VtiToWti.Wti;
    defvar kind = "V"#Vti.ScalarSuffix;
    defvar name = !if(isSEWAware,
                      instruction#"_"#kind#"_"#Vti.LMul.MX # "_E" # Vti.SEW,
                      instruction#"_"#kind#"_"#Vti.LMul.MX);
    let Predicates = !listconcat(GetVTypePredicates<Vti>.Predicates,
                                 GetVTypePredicates<Wti>.Predicates) in
    defm : VPatBinaryRoundingMode<intrinsic, name,
                                  Wti.Vector, Vti.Vector, Vti.Scalar, Vti.Mask,
                                  Vti.Log2SEW, Wti.RegClass,
                                  Vti.RegClass, Vti.ScalarRegClass>;
  }
}

multiclass VPatBinaryW_WV<string intrinsic, string instruction,
                          list<VTypeInfoToWide> vtilist> {
  foreach VtiToWti = vtilist in {
    defvar Vti = VtiToWti.Vti;
    defvar Wti = VtiToWti.Wti;
    let Predicates = !listconcat(GetVTypePredicates<Vti>.Predicates,
                                 GetVTypePredicates<Wti>.Predicates) in {
      def : VPatTiedBinaryNoMask<intrinsic, instruction # "_WV_" # Vti.LMul.MX,
                                 Wti.Vector, Vti.Vector,
                                 Vti.Log2SEW, Wti.RegClass, Vti.RegClass>;
      def : VPatBinaryNoMaskTU<intrinsic, instruction # "_WV_" # Vti.LMul.MX,
                               Wti.Vector, Wti.Vector, Vti.Vector, Vti.Log2SEW,
                               Wti.RegClass, Wti.RegClass, Vti.RegClass>;
      let AddedComplexity = 1 in {
      def : VPatTiedBinaryNoMaskTU<intrinsic, instruction # "_WV_" # Vti.LMul.MX,
                                   Wti.Vector, Vti.Vector,
                                   Vti.Log2SEW, Wti.RegClass, Vti.RegClass>;
      def : VPatTiedBinaryMask<intrinsic, instruction # "_WV_" # Vti.LMul.MX,
                               Wti.Vector, Vti.Vector, Vti.Mask,
                               Vti.Log2SEW, Wti.RegClass, Vti.RegClass>;
      }
      def : VPatBinaryMaskPolicy<intrinsic, instruction # "_WV_" # Vti.LMul.MX,
                                 Wti.Vector, Wti.Vector, Vti.Vector, Vti.Mask,
                                 Vti.Log2SEW, Wti.RegClass,
                                 Wti.RegClass, Vti.RegClass>;
    }
  }
}

multiclass VPatBinaryW_WV_RM<string intrinsic, string instruction,
                             list<VTypeInfoToWide> vtilist, bit isSEWAware = 0> {
  foreach VtiToWti = vtilist in {
    defvar Vti = VtiToWti.Vti;
    defvar Wti = VtiToWti.Wti;
    defvar name = !if(isSEWAware,
                      instruction # "_WV_" # Vti.LMul.MX # "_E" # Vti.SEW,
                      instruction # "_WV_" # Vti.LMul.MX);
    let Predicates = !listconcat(GetVTypePredicates<Vti>.Predicates,
                                 GetVTypePredicates<Wti>.Predicates) in {
      def : VPatTiedBinaryNoMaskRoundingMode<intrinsic, name,
                                             Wti.Vector, Vti.Vector,
                                             Vti.Log2SEW, Wti.RegClass, Vti.RegClass>;
      def : VPatBinaryNoMaskTURoundingMode<intrinsic, name,
                                           Wti.Vector, Wti.Vector, Vti.Vector, Vti.Log2SEW,
                                           Wti.RegClass, Wti.RegClass, Vti.RegClass>;
      let AddedComplexity = 1 in {
      def : VPatTiedBinaryNoMaskTURoundingMode<intrinsic, name,
                                               Wti.Vector, Vti.Vector,
                                               Vti.Log2SEW, Wti.RegClass, Vti.RegClass>;
      def : VPatTiedBinaryMaskRoundingMode<intrinsic, name,
                                           Wti.Vector, Vti.Vector, Vti.Mask,
                                           Vti.Log2SEW, Wti.RegClass, Vti.RegClass>;
      }
      def : VPatBinaryMaskPolicyRoundingMode<intrinsic, name,
                                             Wti.Vector, Wti.Vector, Vti.Vector, Vti.Mask,
                                             Vti.Log2SEW, Wti.RegClass,
                                             Wti.RegClass, Vti.RegClass>;
    }
  }
}

multiclass VPatBinaryW_WX<string intrinsic, string instruction,
                          list<VTypeInfoToWide> vtilist> {
  foreach VtiToWti = vtilist in {
    defvar Vti = VtiToWti.Vti;
    defvar Wti = VtiToWti.Wti;
    defvar kind = "W"#Vti.ScalarSuffix;
    let Predicates = !listconcat(GetVTypePredicates<Vti>.Predicates,
                                 GetVTypePredicates<Wti>.Predicates) in
    defm : VPatBinary<intrinsic, instruction#"_"#kind#"_"#Vti.LMul.MX,
                      Wti.Vector, Wti.Vector, Vti.Scalar, Vti.Mask,
                      Vti.Log2SEW, Wti.RegClass,
                      Wti.RegClass, Vti.ScalarRegClass>;
  }
}

multiclass VPatBinaryW_WX_RM<string intrinsic, string instruction,
                             list<VTypeInfoToWide> vtilist, bit isSEWAware = 0> {
  foreach VtiToWti = vtilist in {
    defvar Vti = VtiToWti.Vti;
    defvar Wti = VtiToWti.Wti;
    defvar kind = "W"#Vti.ScalarSuffix;
    defvar name = !if(isSEWAware,
                      instruction#"_"#kind#"_"#Vti.LMul.MX#"_E"#Vti.SEW,
                      instruction#"_"#kind#"_"#Vti.LMul.MX);
    let Predicates = !listconcat(GetVTypePredicates<Vti>.Predicates,
                                 GetVTypePredicates<Wti>.Predicates) in
    defm : VPatBinaryRoundingMode<intrinsic, name,
                                  Wti.Vector, Wti.Vector, Vti.Scalar, Vti.Mask,
                                  Vti.Log2SEW, Wti.RegClass,
                                  Wti.RegClass, Vti.ScalarRegClass>;
  }
}

multiclass VPatBinaryV_WV<string intrinsic, string instruction,
                          list<VTypeInfoToWide> vtilist> {
  foreach VtiToWti = vtilist in {
    defvar Vti = VtiToWti.Vti;
    defvar Wti = VtiToWti.Wti;
    let Predicates = !listconcat(GetVTypePredicates<Vti>.Predicates,
                                 GetVTypePredicates<Wti>.Predicates) in
    defm : VPatBinary<intrinsic, instruction # "_WV_" # Vti.LMul.MX,
                      Vti.Vector, Wti.Vector, Vti.Vector, Vti.Mask,
                      Vti.Log2SEW, Vti.RegClass,
                      Wti.RegClass, Vti.RegClass>;
  }
}

multiclass VPatBinaryV_WV_RM<string intrinsic, string instruction,
                             list<VTypeInfoToWide> vtilist> {
  foreach VtiToWti = vtilist in {
    defvar Vti = VtiToWti.Vti;
    defvar Wti = VtiToWti.Wti;
    let Predicates = !listconcat(GetVTypePredicates<Vti>.Predicates,
                                 GetVTypePredicates<Wti>.Predicates) in
    defm : VPatBinaryRoundingMode<intrinsic,
                                  instruction # "_WV_" # Vti.LMul.MX,
                                  Vti.Vector, Wti.Vector, Vti.Vector, Vti.Mask,
                                  Vti.Log2SEW, Vti.RegClass,
                                  Wti.RegClass, Vti.RegClass>;
  }
}

multiclass VPatBinaryV_WX<string intrinsic, string instruction,
                          list<VTypeInfoToWide> vtilist> {
  foreach VtiToWti = vtilist in {
    defvar Vti = VtiToWti.Vti;
    defvar Wti = VtiToWti.Wti;
    defvar kind = "W"#Vti.ScalarSuffix;
    let Predicates = !listconcat(GetVTypePredicates<Vti>.Predicates,
                                 GetVTypePredicates<Wti>.Predicates) in
    defm : VPatBinary<intrinsic, instruction#"_"#kind#"_"#Vti.LMul.MX,
                      Vti.Vector, Wti.Vector, Vti.Scalar, Vti.Mask,
                      Vti.Log2SEW, Vti.RegClass,
                      Wti.RegClass, Vti.ScalarRegClass>;
  }
}

multiclass VPatBinaryV_WX_RM<string intrinsic, string instruction,
                             list<VTypeInfoToWide> vtilist> {
  foreach VtiToWti = vtilist in {
    defvar Vti = VtiToWti.Vti;
    defvar Wti = VtiToWti.Wti;
    defvar kind = "W"#Vti.ScalarSuffix;
    let Predicates = !listconcat(GetVTypePredicates<Vti>.Predicates,
                                 GetVTypePredicates<Wti>.Predicates) in
    defm : VPatBinaryRoundingMode<intrinsic,
                                  instruction#"_"#kind#"_"#Vti.LMul.MX,
                                  Vti.Vector, Wti.Vector, Vti.Scalar, Vti.Mask,
                                  Vti.Log2SEW, Vti.RegClass,
                                  Wti.RegClass, Vti.ScalarRegClass>;
  }
}


multiclass VPatBinaryV_WI<string intrinsic, string instruction,
                          list<VTypeInfoToWide> vtilist> {
  foreach VtiToWti = vtilist in {
    defvar Vti = VtiToWti.Vti;
    defvar Wti = VtiToWti.Wti;
    let Predicates = !listconcat(GetVTypePredicates<Vti>.Predicates,
                                 GetVTypePredicates<Wti>.Predicates) in
    defm : VPatBinary<intrinsic, instruction # "_WI_" # Vti.LMul.MX,
                      Vti.Vector, Wti.Vector, XLenVT, Vti.Mask,
                      Vti.Log2SEW, Vti.RegClass,
                      Wti.RegClass, uimm5>;
  }
}

multiclass VPatBinaryV_WI_RM<string intrinsic, string instruction,
                             list<VTypeInfoToWide> vtilist> {
  foreach VtiToWti = vtilist in {
    defvar Vti = VtiToWti.Vti;
    defvar Wti = VtiToWti.Wti;
    let Predicates = !listconcat(GetVTypePredicates<Vti>.Predicates,
                                 GetVTypePredicates<Wti>.Predicates) in
    defm : VPatBinaryRoundingMode<intrinsic,
                                  instruction # "_WI_" # Vti.LMul.MX,
                                  Vti.Vector, Wti.Vector, XLenVT, Vti.Mask,
                                  Vti.Log2SEW, Vti.RegClass,
                                  Wti.RegClass, uimm5>;
  }
}

multiclass VPatBinaryV_VM<string intrinsic, string instruction,
                          bit CarryOut = 0,
                          list<VTypeInfo> vtilist = AllIntegerVectors> {
  foreach vti = vtilist in
    let Predicates = GetVTypePredicates<vti>.Predicates in
    defm : VPatBinaryCarryIn<intrinsic, instruction, "VVM",
                             !if(CarryOut, vti.Mask, vti.Vector),
                             vti.Vector, vti.Vector, vti.Mask,
                             vti.Log2SEW, vti.LMul,
                             vti.RegClass, vti.RegClass>;
}

multiclass VPatBinaryV_XM<string intrinsic, string instruction,
                          bit CarryOut = 0,
                          list<VTypeInfo> vtilist = AllIntegerVectors> {
  foreach vti = vtilist in
    let Predicates = GetVTypePredicates<vti>.Predicates in
    defm : VPatBinaryCarryIn<intrinsic, instruction,
                             "V"#vti.ScalarSuffix#"M",
                             !if(CarryOut, vti.Mask, vti.Vector),
                             vti.Vector, vti.Scalar, vti.Mask,
                             vti.Log2SEW, vti.LMul,
                             vti.RegClass, vti.ScalarRegClass>;
}

multiclass VPatBinaryV_IM<string intrinsic, string instruction,
                          bit CarryOut = 0> {
  foreach vti = AllIntegerVectors in
    let Predicates = GetVTypePredicates<vti>.Predicates in
    defm : VPatBinaryCarryIn<intrinsic, instruction, "VIM",
                             !if(CarryOut, vti.Mask, vti.Vector),
                             vti.Vector, XLenVT, vti.Mask,
                             vti.Log2SEW, vti.LMul,
                             vti.RegClass, simm5>;
}

multiclass VPatBinaryV_VM_TAIL<string intrinsic, string instruction> {
  foreach vti = AllIntegerVectors in
    let Predicates = GetVTypePredicates<vti>.Predicates in
    defm : VPatBinaryCarryInTAIL<intrinsic, instruction, "VVM",
                                 vti.Vector,
                                 vti.Vector, vti.Vector, vti.Mask,
                                 vti.Log2SEW, vti.LMul, vti.RegClass,
                                 vti.RegClass, vti.RegClass>;
}

multiclass VPatBinaryV_XM_TAIL<string intrinsic, string instruction> {
  foreach vti = AllIntegerVectors in
    let Predicates = GetVTypePredicates<vti>.Predicates in
    defm : VPatBinaryCarryInTAIL<intrinsic, instruction,
                                 "V"#vti.ScalarSuffix#"M",
                                 vti.Vector,
                                 vti.Vector, vti.Scalar, vti.Mask,
                                 vti.Log2SEW, vti.LMul, vti.RegClass,
                                 vti.RegClass, vti.ScalarRegClass>;
}

multiclass VPatBinaryV_IM_TAIL<string intrinsic, string instruction> {
  foreach vti = AllIntegerVectors in
    let Predicates = GetVTypePredicates<vti>.Predicates in
    defm : VPatBinaryCarryInTAIL<intrinsic, instruction, "VIM",
                                 vti.Vector,
                                 vti.Vector, XLenVT, vti.Mask,
                                 vti.Log2SEW, vti.LMul,
                                 vti.RegClass, vti.RegClass, simm5>;
}

multiclass VPatBinaryV_V<string intrinsic, string instruction> {
  foreach vti = AllIntegerVectors in
    let Predicates = GetVTypePredicates<vti>.Predicates in
    defm : VPatBinaryMaskOut<intrinsic, instruction, "VV",
                             vti.Mask, vti.Vector, vti.Vector,
                             vti.Log2SEW, vti.LMul,
                             vti.RegClass, vti.RegClass>;
}

multiclass VPatBinaryV_X<string intrinsic, string instruction> {
  foreach vti = AllIntegerVectors in
    let Predicates = GetVTypePredicates<vti>.Predicates in
    defm : VPatBinaryMaskOut<intrinsic, instruction, "VX",
                             vti.Mask, vti.Vector, XLenVT,
                             vti.Log2SEW, vti.LMul,
                             vti.RegClass, GPR>;
}

multiclass VPatBinaryV_I<string intrinsic, string instruction> {
  foreach vti = AllIntegerVectors in
    let Predicates = GetVTypePredicates<vti>.Predicates in
    defm : VPatBinaryMaskOut<intrinsic, instruction, "VI",
                             vti.Mask, vti.Vector, XLenVT,
                             vti.Log2SEW, vti.LMul,
                             vti.RegClass, simm5>;
}

multiclass VPatBinaryM_VV<string intrinsic, string instruction,
                          list<VTypeInfo> vtilist> {
  foreach vti = vtilist in
    let Predicates = GetVTypePredicates<vti>.Predicates in
    defm : VPatBinaryM<intrinsic, instruction # "_VV_" # vti.LMul.MX,
                       vti.Mask, vti.Vector, vti.Vector, vti.Mask,
                       vti.Log2SEW, VR,
                       vti.RegClass, vti.RegClass>;
}

multiclass VPatBinarySwappedM_VV<string intrinsic, string instruction,
                                 list<VTypeInfo> vtilist> {
  foreach vti = vtilist in
    let Predicates = GetVTypePredicates<vti>.Predicates in
    defm : VPatBinaryMSwapped<intrinsic, instruction # "_VV_" # vti.LMul.MX,
                              vti.Mask, vti.Vector, vti.Vector, vti.Mask,
                              vti.Log2SEW, VR,
                              vti.RegClass, vti.RegClass>;
}

multiclass VPatBinaryM_VX<string intrinsic, string instruction,
                          list<VTypeInfo> vtilist> {
  foreach vti = vtilist in {
    defvar kind = "V"#vti.ScalarSuffix;
    let Predicates = GetVTypePredicates<vti>.Predicates in
    defm : VPatBinaryM<intrinsic, instruction#"_"#kind#"_"#vti.LMul.MX,
                       vti.Mask, vti.Vector, vti.Scalar, vti.Mask,
                       vti.Log2SEW, VR,
                       vti.RegClass, vti.ScalarRegClass>;
  }
}

multiclass VPatBinaryM_VI<string intrinsic, string instruction,
                          list<VTypeInfo> vtilist> {
  foreach vti = vtilist in
    let Predicates = GetVTypePredicates<vti>.Predicates in
    defm : VPatBinaryM<intrinsic, instruction # "_VI_" # vti.LMul.MX,
                       vti.Mask, vti.Vector, XLenVT, vti.Mask,
                       vti.Log2SEW, VR,
                       vti.RegClass, simm5>;
}

multiclass VPatBinaryV_VV_VX_VI<string intrinsic, string instruction,
                                list<VTypeInfo> vtilist, Operand ImmType = simm5>
    : VPatBinaryV_VV<intrinsic, instruction, vtilist>,
      VPatBinaryV_VX<intrinsic, instruction, vtilist>,
      VPatBinaryV_VI<intrinsic, instruction, vtilist, ImmType>;

multiclass VPatBinaryV_VV_VX_VI_RM<string intrinsic, string instruction,
                                   list<VTypeInfo> vtilist, Operand ImmType>
    : VPatBinaryV_VV_RM<intrinsic, instruction, vtilist>,
      VPatBinaryV_VX_RM<intrinsic, instruction, vtilist>,
      VPatBinaryV_VI_RM<intrinsic, instruction, vtilist, ImmType>;

multiclass VPatBinaryV_VV_VX<string intrinsic, string instruction,
                             list<VTypeInfo> vtilist, bit isSEWAware = 0>
    : VPatBinaryV_VV<intrinsic, instruction, vtilist, isSEWAware>,
      VPatBinaryV_VX<intrinsic, instruction, vtilist, isSEWAware>;

multiclass VPatBinaryV_VV_VX_RM<string intrinsic, string instruction,
                                list<VTypeInfo> vtilist, bit isSEWAware = 0>
    : VPatBinaryV_VV_RM<intrinsic, instruction, vtilist, isSEWAware>,
      VPatBinaryV_VX_RM<intrinsic, instruction, vtilist, isSEWAware>;

multiclass VPatBinaryV_VX_VI<string intrinsic, string instruction,
                             list<VTypeInfo> vtilist>
    : VPatBinaryV_VX<intrinsic, instruction, vtilist>,
      VPatBinaryV_VI<intrinsic, instruction, vtilist, simm5>;

multiclass VPatBinaryW_VV_VX<string intrinsic, string instruction,
                             list<VTypeInfoToWide> vtilist>
    : VPatBinaryW_VV<intrinsic, instruction, vtilist>,
      VPatBinaryW_VX<intrinsic, instruction, vtilist>;

multiclass
    VPatBinaryW_VV_VX_RM<string intrinsic, string instruction,
                         list<VTypeInfoToWide> vtilist, bit isSEWAware = 0>
    : VPatBinaryW_VV_RM<intrinsic, instruction, vtilist, isSEWAware>,
      VPatBinaryW_VX_RM<intrinsic, instruction, vtilist, isSEWAware>;

multiclass VPatBinaryW_WV_WX<string intrinsic, string instruction,
                             list<VTypeInfoToWide> vtilist>
    : VPatBinaryW_WV<intrinsic, instruction, vtilist>,
      VPatBinaryW_WX<intrinsic, instruction, vtilist>;

multiclass
    VPatBinaryW_WV_WX_RM<string intrinsic, string instruction,
                         list<VTypeInfoToWide> vtilist, bit isSEWAware = 0>
    : VPatBinaryW_WV_RM<intrinsic, instruction, vtilist, isSEWAware>,
      VPatBinaryW_WX_RM<intrinsic, instruction, vtilist, isSEWAware>;

multiclass VPatBinaryV_WV_WX_WI<string intrinsic, string instruction,
                                list<VTypeInfoToWide> vtilist>
    : VPatBinaryV_WV<intrinsic, instruction, vtilist>,
      VPatBinaryV_WX<intrinsic, instruction, vtilist>,
      VPatBinaryV_WI<intrinsic, instruction, vtilist>;

multiclass VPatBinaryV_WV_WX_WI_RM<string intrinsic, string instruction,
                                   list<VTypeInfoToWide> vtilist>
    : VPatBinaryV_WV_RM<intrinsic, instruction, vtilist>,
      VPatBinaryV_WX_RM<intrinsic, instruction, vtilist>,
      VPatBinaryV_WI_RM<intrinsic, instruction, vtilist>;

multiclass VPatBinaryV_VM_XM_IM<string intrinsic, string instruction>
    : VPatBinaryV_VM_TAIL<intrinsic, instruction>,
      VPatBinaryV_XM_TAIL<intrinsic, instruction>,
      VPatBinaryV_IM_TAIL<intrinsic, instruction>;

multiclass VPatBinaryM_VM_XM_IM<string intrinsic, string instruction>
    : VPatBinaryV_VM<intrinsic, instruction, CarryOut=1>,
      VPatBinaryV_XM<intrinsic, instruction, CarryOut=1>,
      VPatBinaryV_IM<intrinsic, instruction, CarryOut=1>;

multiclass VPatBinaryM_V_X_I<string intrinsic, string instruction>
    : VPatBinaryV_V<intrinsic, instruction>,
      VPatBinaryV_X<intrinsic, instruction>,
      VPatBinaryV_I<intrinsic, instruction>;

multiclass VPatBinaryV_VM_XM<string intrinsic, string instruction>
    : VPatBinaryV_VM_TAIL<intrinsic, instruction>,
      VPatBinaryV_XM_TAIL<intrinsic, instruction>;

multiclass VPatBinaryM_VM_XM<string intrinsic, string instruction>
    : VPatBinaryV_VM<intrinsic, instruction, CarryOut=1>,
      VPatBinaryV_XM<intrinsic, instruction, CarryOut=1>;

multiclass VPatBinaryM_V_X<string intrinsic, string instruction>
    : VPatBinaryV_V<intrinsic, instruction>,
      VPatBinaryV_X<intrinsic, instruction>;

multiclass VPatTernaryWithPolicy<string intrinsic,
                                 string inst,
                                 string kind,
                                 ValueType result_type,
                                 ValueType op1_type,
                                 ValueType op2_type,
                                 ValueType mask_type,
                                 int sew,
                                 LMULInfo vlmul,
                                 VReg result_reg_class,
                                 RegisterClass op1_reg_class,
                                 DAGOperand op2_kind> {
  def : VPatTernaryNoMaskWithPolicy<intrinsic, inst, kind, result_type, op1_type,
                                    op2_type, sew, vlmul, result_reg_class,
                                    op1_reg_class, op2_kind>;
  def : VPatTernaryMaskPolicy<intrinsic, inst, kind, result_type, op1_type, op2_type,
                              mask_type, sew, vlmul, result_reg_class, op1_reg_class,
                              op2_kind>;
}

multiclass VPatTernaryWithPolicyRoundingMode<string intrinsic,
                                             string inst,
                                             string kind,
                                             ValueType result_type,
                                             ValueType op1_type,
                                             ValueType op2_type,
                                             ValueType mask_type,
                                             int sew,
                                             LMULInfo vlmul,
                                             VReg result_reg_class,
                                             RegisterClass op1_reg_class,
                                             DAGOperand op2_kind,
                                             bit isSEWAware = 0> {
  def : VPatTernaryNoMaskWithPolicyRoundingMode<intrinsic, inst, kind, result_type,
                                                op1_type, op2_type, sew, vlmul,
                                                result_reg_class, op1_reg_class,
                                                op2_kind, isSEWAware>;
  def : VPatTernaryMaskPolicyRoundingMode<intrinsic, inst, kind, result_type, op1_type,
                                                op2_type, mask_type, sew, vlmul,
                                                result_reg_class, op1_reg_class,
                                                op2_kind, isSEWAware>;
}

multiclass VPatTernaryTU<string intrinsic,
                         string inst,
                         string kind,
                         ValueType result_type,
                         ValueType op1_type,
                         ValueType op2_type,
                         ValueType mask_type,
                         int log2sew,
                         LMULInfo vlmul,
                         VReg result_reg_class,
                         RegisterClass op1_reg_class,
                         DAGOperand op2_kind> {
  def : VPatTernaryNoMaskTU<intrinsic, inst, kind, result_type, op1_type,
                            op2_type, log2sew, vlmul, result_reg_class,
                            op1_reg_class, op2_kind>;
  def : VPatTernaryMaskTU<intrinsic, inst, kind, result_type, op1_type,
                          op2_type, mask_type, log2sew, vlmul,
                          result_reg_class, op1_reg_class, op2_kind>;
}

multiclass VPatTernaryTURoundingMode<string intrinsic,
                                     string inst,
                                     string kind,
                                     ValueType result_type,
                                     ValueType op1_type,
                                     ValueType op2_type,
                                     ValueType mask_type,
                                     int log2sew,
                                     LMULInfo vlmul,
                                     VReg result_reg_class,
                                     RegisterClass op1_reg_class,
                                     DAGOperand op2_kind> {
  def : VPatTernaryNoMaskTURoundingMode<intrinsic, inst, kind, result_type, op1_type,
                            op2_type, log2sew, vlmul, result_reg_class,
                            op1_reg_class, op2_kind>;
  def : VPatTernaryMaskTURoundingMode<intrinsic, inst, kind, result_type, op1_type,
                          op2_type, mask_type, log2sew, vlmul,
                          result_reg_class, op1_reg_class, op2_kind>;
}

multiclass VPatTernaryV_VV_AAXA<string intrinsic, string instruction,
                                list<VTypeInfo> vtilist> {
  foreach vti = vtilist in
    let Predicates = GetVTypePredicates<vti>.Predicates in
    defm : VPatTernaryWithPolicy<intrinsic, instruction, "VV",
                                 vti.Vector, vti.Vector, vti.Vector, vti.Mask,
                                 vti.Log2SEW, vti.LMul, vti.RegClass,
                                 vti.RegClass, vti.RegClass>;
}

multiclass VPatTernaryV_VV_AAXA_RM<string intrinsic, string instruction,
                                list<VTypeInfo> vtilist, bit isSEWAware = 0> {
  foreach vti = vtilist in
    let Predicates = GetVTypePredicates<vti>.Predicates in
    defm : VPatTernaryWithPolicyRoundingMode<intrinsic, instruction, "VV",
                                             vti.Vector, vti.Vector, vti.Vector, vti.Mask,
                                             vti.Log2SEW, vti.LMul, vti.RegClass,
                                             vti.RegClass, vti.RegClass, isSEWAware>;
}

multiclass VPatTernaryV_VX<string intrinsic, string instruction,
                           list<VTypeInfo> vtilist> {
  foreach vti = vtilist in
    let Predicates = GetVTypePredicates<vti>.Predicates in
    defm : VPatTernaryWithPolicy<intrinsic, instruction, "VX",
                                 vti.Vector, vti.Vector, XLenVT, vti.Mask,
                                 vti.Log2SEW, vti.LMul, vti.RegClass,
                                 vti.RegClass, GPR>;
}

multiclass VPatTernaryV_VX_AAXA<string intrinsic, string instruction,
                           list<VTypeInfo> vtilist> {
  foreach vti = vtilist in
    let Predicates = GetVTypePredicates<vti>.Predicates in
    defm : VPatTernaryWithPolicy<intrinsic, instruction,
                                 "V"#vti.ScalarSuffix,
                                 vti.Vector, vti.Scalar, vti.Vector, vti.Mask,
                                 vti.Log2SEW, vti.LMul, vti.RegClass,
                                 vti.ScalarRegClass, vti.RegClass>;
}

multiclass VPatTernaryV_VX_AAXA_RM<string intrinsic, string instruction,
                           list<VTypeInfo> vtilist, bit isSEWAware = 0> {
  foreach vti = vtilist in
    let Predicates = GetVTypePredicates<vti>.Predicates in
    defm : VPatTernaryWithPolicyRoundingMode<intrinsic, instruction,
                                             "V"#vti.ScalarSuffix,
                                             vti.Vector, vti.Scalar, vti.Vector, vti.Mask,
                                             vti.Log2SEW, vti.LMul, vti.RegClass,
                                             vti.ScalarRegClass, vti.RegClass, isSEWAware>;
}

multiclass VPatTernaryV_VI<string intrinsic, string instruction,
                           list<VTypeInfo> vtilist, Operand Imm_type> {
  foreach vti = vtilist in
    let Predicates = GetVTypePredicates<vti>.Predicates in
    defm : VPatTernaryWithPolicy<intrinsic, instruction, "VI",
                                 vti.Vector, vti.Vector, XLenVT, vti.Mask,
                                 vti.Log2SEW, vti.LMul, vti.RegClass,
                                 vti.RegClass, Imm_type>;
}

multiclass VPatTernaryW_VV<string intrinsic, string instruction,
                           list<VTypeInfoToWide> vtilist> {
  foreach vtiToWti = vtilist in {
    defvar vti = vtiToWti.Vti;
    defvar wti = vtiToWti.Wti;
    let Predicates = !listconcat(GetVTypePredicates<vti>.Predicates,
                                 GetVTypePredicates<wti>.Predicates) in
    defm : VPatTernaryWithPolicy<intrinsic, instruction, "VV",
                                 wti.Vector, vti.Vector, vti.Vector,
                                 vti.Mask, vti.Log2SEW, vti.LMul,
                                 wti.RegClass, vti.RegClass, vti.RegClass>;
  }
}

multiclass VPatTernaryW_VV_RM<string intrinsic, string instruction,
                           list<VTypeInfoToWide> vtilist, bit isSEWAware = 0> {
  foreach vtiToWti = vtilist in {
    defvar vti = vtiToWti.Vti;
    defvar wti = vtiToWti.Wti;
    let Predicates = !listconcat(GetVTypePredicates<vti>.Predicates,
                                 GetVTypePredicates<wti>.Predicates) in
    defm : VPatTernaryWithPolicyRoundingMode<intrinsic, instruction, "VV",
                                             wti.Vector, vti.Vector, vti.Vector,
                                             vti.Mask, vti.Log2SEW, vti.LMul,
                                             wti.RegClass, vti.RegClass,
                                             vti.RegClass, isSEWAware>;
  }
}

multiclass VPatTernaryW_VX<string intrinsic, string instruction,
                           list<VTypeInfoToWide> vtilist> {
  foreach vtiToWti = vtilist in {
    defvar vti = vtiToWti.Vti;
    defvar wti = vtiToWti.Wti;
    let Predicates = !listconcat(GetVTypePredicates<vti>.Predicates,
                                 GetVTypePredicates<wti>.Predicates) in
    defm : VPatTernaryWithPolicy<intrinsic, instruction,
                                 "V"#vti.ScalarSuffix,
                                 wti.Vector, vti.Scalar, vti.Vector,
                                 vti.Mask, vti.Log2SEW, vti.LMul,
                                 wti.RegClass, vti.ScalarRegClass, vti.RegClass>;
  }
}

multiclass
    VPatTernaryW_VX_RM<string intrinsic, string instruction,
                       list<VTypeInfoToWide> vtilist, bit isSEWAware = 0> {
  foreach vtiToWti = vtilist in {
    defvar vti = vtiToWti.Vti;
    defvar wti = vtiToWti.Wti;
    let Predicates = !listconcat(GetVTypePredicates<vti>.Predicates,
                                 GetVTypePredicates<wti>.Predicates) in defm
        : VPatTernaryWithPolicyRoundingMode<
              intrinsic, instruction, "V" #vti.ScalarSuffix, wti.Vector,
              vti.Scalar, vti.Vector, vti.Mask, vti.Log2SEW, vti.LMul,
              wti.RegClass, vti.ScalarRegClass, vti.RegClass, isSEWAware>;
  }
}

multiclass VPatTernaryV_VV_VX_AAXA<string intrinsic, string instruction,
                              list<VTypeInfo> vtilist>
    : VPatTernaryV_VV_AAXA<intrinsic, instruction, vtilist>,
      VPatTernaryV_VX_AAXA<intrinsic, instruction, vtilist>;

multiclass VPatTernaryV_VV_VX_AAXA_RM<string intrinsic, string instruction,
                              list<VTypeInfo> vtilist, bit isSEWAware = 0>
    : VPatTernaryV_VV_AAXA_RM<intrinsic, instruction, vtilist, isSEWAware>,
      VPatTernaryV_VX_AAXA_RM<intrinsic, instruction, vtilist, isSEWAware>;

multiclass VPatTernaryV_VX_VI<string intrinsic, string instruction,
                              list<VTypeInfo> vtilist, Operand Imm_type>
    : VPatTernaryV_VX<intrinsic, instruction, vtilist>,
      VPatTernaryV_VI<intrinsic, instruction, vtilist, Imm_type>;


multiclass VPatBinaryM_VV_VX_VI<string intrinsic, string instruction,
                                list<VTypeInfo> vtilist>
    : VPatBinaryM_VV<intrinsic, instruction, vtilist>,
      VPatBinaryM_VX<intrinsic, instruction, vtilist>,
      VPatBinaryM_VI<intrinsic, instruction, vtilist>;

multiclass VPatTernaryW_VV_VX<string intrinsic, string instruction,
                              list<VTypeInfoToWide> vtilist>
    : VPatTernaryW_VV<intrinsic, instruction, vtilist>,
      VPatTernaryW_VX<intrinsic, instruction, vtilist>;

multiclass VPatTernaryW_VV_VX_RM<string intrinsic, string instruction,
                              list<VTypeInfoToWide> vtilist, bit isSEWAware = 1>
    : VPatTernaryW_VV_RM<intrinsic, instruction, vtilist, isSEWAware>,
      VPatTernaryW_VX_RM<intrinsic, instruction, vtilist, isSEWAware>;

multiclass VPatBinaryM_VV_VX<string intrinsic, string instruction,
                             list<VTypeInfo> vtilist>
    : VPatBinaryM_VV<intrinsic, instruction, vtilist>,
      VPatBinaryM_VX<intrinsic, instruction, vtilist>;

multiclass VPatBinaryM_VX_VI<string intrinsic, string instruction,
                             list<VTypeInfo> vtilist>
    : VPatBinaryM_VX<intrinsic, instruction, vtilist>,
      VPatBinaryM_VI<intrinsic, instruction, vtilist>;

multiclass VPatBinaryV_VV_VX_VI_INT<string intrinsic, string instruction,
                                    list<VTypeInfo> vtilist, Operand ImmType>
    : VPatBinaryV_VV_INT<intrinsic#"_vv", instruction, vtilist>,
      VPatBinaryV_VX_INT<intrinsic#"_vx", instruction, vtilist>,
      VPatBinaryV_VI<intrinsic#"_vx", instruction, vtilist, ImmType>;

multiclass VPatReductionV_VS<string intrinsic, string instruction, bit IsFloat = 0> {
  foreach vti = !if(IsFloat, NoGroupFloatVectors, NoGroupIntegerVectors) in {
    defvar vectorM1 = !cast<VTypeInfo>(!if(IsFloat, "VF", "VI") # vti.SEW # "M1");
    let Predicates = GetVTypePredicates<vti>.Predicates in
    defm : VPatTernaryTU<intrinsic, instruction, "VS",
                         vectorM1.Vector, vti.Vector,
                         vectorM1.Vector, vti.Mask,
                         vti.Log2SEW, vti.LMul,
                         VR, vti.RegClass, VR>;
  }
  foreach gvti = !if(IsFloat, GroupFloatVectors, GroupIntegerVectors) in {
    let Predicates = GetVTypePredicates<gvti>.Predicates in
    defm : VPatTernaryTU<intrinsic, instruction, "VS",
                         gvti.VectorM1, gvti.Vector,
                         gvti.VectorM1, gvti.Mask,
                         gvti.Log2SEW, gvti.LMul,
                         VR, gvti.RegClass, VR>;
  }
}

multiclass VPatReductionV_VS_RM<string intrinsic, string instruction, bit IsFloat = 0> {
  foreach vti = !if(IsFloat, NoGroupFloatVectors, NoGroupIntegerVectors) in {
    defvar vectorM1 = !cast<VTypeInfo>(!if(IsFloat, "VF", "VI") # vti.SEW # "M1");
    let Predicates = GetVTypePredicates<vti>.Predicates in
    defm : VPatTernaryTURoundingMode<intrinsic, instruction, "VS",
                                     vectorM1.Vector, vti.Vector,
                                     vectorM1.Vector, vti.Mask,
                                     vti.Log2SEW, vti.LMul,
                                     VR, vti.RegClass, VR>;
  }
  foreach gvti = !if(IsFloat, GroupFloatVectors, GroupIntegerVectors) in {
    let Predicates = GetVTypePredicates<gvti>.Predicates in
    defm : VPatTernaryTURoundingMode<intrinsic, instruction, "VS",
                                     gvti.VectorM1, gvti.Vector,
                                     gvti.VectorM1, gvti.Mask,
                                     gvti.Log2SEW, gvti.LMul,
                                     VR, gvti.RegClass, VR>;
  }
}

multiclass VPatReductionW_VS<string intrinsic, string instruction, bit IsFloat = 0> {
  foreach vti = !if(IsFloat, AllFloatVectors, AllIntegerVectors) in {
    defvar wtiSEW = !mul(vti.SEW, 2);
    if !le(wtiSEW, 64) then {
      defvar wtiM1 = !cast<VTypeInfo>(!if(IsFloat, "VF", "VI") # wtiSEW # "M1");
      let Predicates = GetVTypePredicates<vti>.Predicates in
      defm : VPatTernaryTU<intrinsic, instruction, "VS",
                           wtiM1.Vector, vti.Vector,
                           wtiM1.Vector, vti.Mask,
                           vti.Log2SEW, vti.LMul,
                           wtiM1.RegClass, vti.RegClass,
                           wtiM1.RegClass>;
    }
  }
}

multiclass VPatReductionW_VS_RM<string intrinsic, string instruction, bit IsFloat = 0> {
  foreach vti = !if(IsFloat, AllFloatVectors, AllIntegerVectors) in {
    defvar wtiSEW = !mul(vti.SEW, 2);
    if !le(wtiSEW, 64) then {
      defvar wtiM1 = !cast<VTypeInfo>(!if(IsFloat, "VF", "VI") # wtiSEW # "M1");
      let Predicates = GetVTypePredicates<vti>.Predicates in
      defm : VPatTernaryTURoundingMode<intrinsic, instruction, "VS",
                                       wtiM1.Vector, vti.Vector,
                                       wtiM1.Vector, vti.Mask,
                                       vti.Log2SEW, vti.LMul,
                                       wtiM1.RegClass, vti.RegClass,
                                       wtiM1.RegClass>;
    }
  }
}

multiclass VPatConversionVI_VF<string intrinsic,
                               string instruction> {
  foreach fvti = AllFloatVectors in {
    defvar ivti = GetIntVTypeInfo<fvti>.Vti;
    let Predicates = !listconcat(GetVTypePredicates<fvti>.Predicates,
                                 GetVTypePredicates<ivti>.Predicates) in
    defm : VPatConversion<intrinsic, instruction, "V",
                          ivti.Vector, fvti.Vector, ivti.Mask, fvti.Log2SEW,
                          fvti.LMul, ivti.RegClass, fvti.RegClass>;
  }
}

multiclass VPatConversionVI_VF_RM<string intrinsic,
                                  string instruction> {
  foreach fvti = AllFloatVectors in {
    defvar ivti = GetIntVTypeInfo<fvti>.Vti;
    let Predicates = !listconcat(GetVTypePredicates<fvti>.Predicates,
                                 GetVTypePredicates<ivti>.Predicates) in
    defm : VPatConversionRoundingMode<intrinsic, instruction, "V",
                                      ivti.Vector, fvti.Vector, ivti.Mask, fvti.Log2SEW,
                                      fvti.LMul, ivti.RegClass, fvti.RegClass>;
  }
}

multiclass VPatConversionVI_VF_RTZ<string intrinsic, 
                                           string instruction> {
  foreach fvti = AllFloatVectors in {
    defvar ivti = GetIntVTypeInfo<fvti>.Vti;
    let Predicates = !listconcat(GetVTypePredicates<fvti>.Predicates,
                                 GetVTypePredicates<ivti>.Predicates) in
    defm : VPatConversionRTZ<intrinsic, instruction, "V",
                                              ivti.Vector, fvti.Vector, ivti.Mask, fvti.Log2SEW,
                                              fvti.LMul, ivti.RegClass, fvti.RegClass>;
  }
}

multiclass VPatConversionVF_VI_RM<string intrinsic, string instruction,
                                  bit isSEWAware = 0> {
  foreach fvti = AllFloatVectors in {
    defvar ivti = GetIntVTypeInfo<fvti>.Vti;
    let Predicates = !listconcat(GetVTypePredicates<fvti>.Predicates,
                                 GetVTypePredicates<ivti>.Predicates) in
    defm : VPatConversionRoundingMode<intrinsic, instruction, "V",
                                      fvti.Vector, ivti.Vector, fvti.Mask, ivti.Log2SEW,
                                      ivti.LMul, fvti.RegClass, ivti.RegClass,
                                      isSEWAware>;
  }
}

multiclass VPatConversionWI_VF<string intrinsic, string instruction> {
  foreach fvtiToFWti = AllWidenableFloatVectors in {
    defvar fvti = fvtiToFWti.Vti;
    defvar iwti = GetIntVTypeInfo<fvtiToFWti.Wti>.Vti;
    let Predicates = !listconcat(GetVTypePredicates<fvti>.Predicates,
                                 GetVTypePredicates<iwti>.Predicates) in
    defm : VPatConversion<intrinsic, instruction, "V",
                          iwti.Vector, fvti.Vector, iwti.Mask, fvti.Log2SEW,
                          fvti.LMul, iwti.RegClass, fvti.RegClass>;
  }
}

multiclass VPatConversionWI_VF_RM<string intrinsic, string instruction> {
  foreach fvtiToFWti = AllWidenableFloatVectors in {
    defvar fvti = fvtiToFWti.Vti;
    defvar iwti = GetIntVTypeInfo<fvtiToFWti.Wti>.Vti;
    let Predicates = !listconcat(GetVTypePredicates<fvti>.Predicates,
                                 GetVTypePredicates<iwti>.Predicates) in
    defm : VPatConversionRoundingMode<intrinsic, instruction, "V",
                                      iwti.Vector, fvti.Vector, iwti.Mask, fvti.Log2SEW,
                                      fvti.LMul, iwti.RegClass, fvti.RegClass>;
  }
}

multiclass VPatConversionWI_VF_RTZ<string intrinsic, string instruction> {
  foreach fvtiToFWti = AllWidenableFloatVectors in {
    defvar fvti = fvtiToFWti.Vti;
    defvar iwti = GetIntVTypeInfo<fvtiToFWti.Wti>.Vti;
    let Predicates = !listconcat(GetVTypePredicates<fvti>.Predicates,
                                 GetVTypePredicates<iwti>.Predicates) in
    defm : VPatConversionRTZ<intrinsic, instruction, "V",
                             iwti.Vector, fvti.Vector, iwti.Mask, fvti.Log2SEW,
                             fvti.LMul, iwti.RegClass, fvti.RegClass>;
  }
}

multiclass VPatConversionWF_VI<string intrinsic, string instruction,
                               bit isSEWAware = 0> {
  foreach vtiToWti = AllWidenableIntToFloatVectors in {
    defvar vti = vtiToWti.Vti;
    defvar fwti = vtiToWti.Wti;
    let Predicates = !listconcat(GetVTypePredicates<vti>.Predicates,
                                 GetVTypePredicates<fwti>.Predicates) in
    defm : VPatConversion<intrinsic, instruction, "V",
                          fwti.Vector, vti.Vector, fwti.Mask, vti.Log2SEW,
                          vti.LMul, fwti.RegClass, vti.RegClass, isSEWAware>;
  }
}

multiclass VPatConversionWF_VF<string intrinsic, string instruction,
                               bit isSEWAware = 0> {
  foreach fvtiToFWti = AllWidenableFloatVectors in {
    defvar fvti = fvtiToFWti.Vti;
    defvar fwti = fvtiToFWti.Wti;
    // Define vfwcvt.f.f.v for f16 when Zvfhmin is enable.
    let Predicates = !if(!eq(fvti.Scalar, f16), [HasVInstructionsF16Minimal],
                         !listconcat(GetVTypePredicates<fvti>.Predicates,
                                     GetVTypePredicates<fwti>.Predicates)) in
      defm : VPatConversion<intrinsic, instruction, "V",
                            fwti.Vector, fvti.Vector, fwti.Mask, fvti.Log2SEW,
                            fvti.LMul, fwti.RegClass, fvti.RegClass, isSEWAware>;
  }
}

multiclass VPatConversionWF_VF_BF<string intrinsic, string instruction,
                                  bit isSEWAware = 0> {
  foreach fvtiToFWti = AllWidenableBFloatToFloatVectors in
  {
    defvar fvti = fvtiToFWti.Vti;
    defvar fwti = fvtiToFWti.Wti;
    let Predicates = !listconcat(GetVTypePredicates<fvti>.Predicates,
                                 GetVTypePredicates<fwti>.Predicates) in
    defm : VPatConversion<intrinsic, instruction, "V",
                          fwti.Vector, fvti.Vector, fwti.Mask, fvti.Log2SEW,
                          fvti.LMul, fwti.RegClass, fvti.RegClass, isSEWAware>;
  }
}

multiclass VPatConversionVI_WF<string intrinsic, string instruction> {
  foreach vtiToWti = AllWidenableIntToFloatVectors in {
    defvar vti = vtiToWti.Vti;
    defvar fwti = vtiToWti.Wti;
    let Predicates = !listconcat(GetVTypePredicates<vti>.Predicates,
                                 GetVTypePredicates<fwti>.Predicates) in
    defm : VPatConversion<intrinsic, instruction, "W",
                          vti.Vector, fwti.Vector, vti.Mask, vti.Log2SEW,
                          vti.LMul, vti.RegClass, fwti.RegClass>;
  }
}

multiclass VPatConversionVI_WF_RM <string intrinsic, string instruction> {
  foreach vtiToWti = AllWidenableIntToFloatVectors in {
    defvar vti = vtiToWti.Vti;
    defvar fwti = vtiToWti.Wti;
    let Predicates = !listconcat(GetVTypePredicates<vti>.Predicates,
                                 GetVTypePredicates<fwti>.Predicates) in
    defm : VPatConversionRoundingMode<intrinsic, instruction, "W",
                                      vti.Vector, fwti.Vector, vti.Mask, vti.Log2SEW,
                                      vti.LMul, vti.RegClass, fwti.RegClass>;
  }
}

multiclass VPatConversionVI_WF_RTZ <string intrinsic, string instruction> {
  foreach vtiToWti = AllWidenableIntToFloatVectors in {
    defvar vti = vtiToWti.Vti;
    defvar fwti = vtiToWti.Wti;
    let Predicates = !listconcat(GetVTypePredicates<vti>.Predicates,
                                 GetVTypePredicates<fwti>.Predicates) in
    defm : VPatConversionRTZ<intrinsic, instruction, "W",
                             vti.Vector, fwti.Vector, vti.Mask, vti.Log2SEW,
                             vti.LMul, vti.RegClass, fwti.RegClass>;
  }
}

multiclass VPatConversionVF_WI_RM <string intrinsic, string instruction,
                                   bit isSEWAware = 0> {
  foreach fvtiToFWti = AllWidenableFloatVectors in {
    defvar fvti = fvtiToFWti.Vti;
    defvar iwti = GetIntVTypeInfo<fvtiToFWti.Wti>.Vti;
    let Predicates = !listconcat(GetVTypePredicates<fvti>.Predicates,
                                 GetVTypePredicates<iwti>.Predicates) in
    defm : VPatConversionRoundingMode<intrinsic, instruction, "W",
                                      fvti.Vector, iwti.Vector, fvti.Mask, fvti.Log2SEW,
                                      fvti.LMul, fvti.RegClass, iwti.RegClass,
                                      isSEWAware>;
  }
}

multiclass VPatConversionVF_WF<string intrinsic, string instruction,
                               bit isSEWAware = 0> {
  foreach fvtiToFWti = AllWidenableFloatVectors in {
    defvar fvti = fvtiToFWti.Vti;
    defvar fwti = fvtiToFWti.Wti;
    let Predicates = !listconcat(GetVTypePredicates<fvti>.Predicates,
                                 GetVTypePredicates<fwti>.Predicates) in
    defm : VPatConversion<intrinsic, instruction, "W",
                          fvti.Vector, fwti.Vector, fvti.Mask, fvti.Log2SEW,
                          fvti.LMul, fvti.RegClass, fwti.RegClass, isSEWAware>;
  }
}

multiclass VPatConversionVF_WF_RM<string intrinsic, string instruction,
                                   list<VTypeInfoToWide> wlist = AllWidenableFloatVectors,
                                   bit isSEWAware = 0> {
  foreach fvtiToFWti = wlist in {
    defvar fvti = fvtiToFWti.Vti;
    defvar fwti = fvtiToFWti.Wti;
    let Predicates = !listconcat(GetVTypePredicates<fvti>.Predicates,
                                 GetVTypePredicates<fwti>.Predicates) in
    defm : VPatConversionRoundingMode<intrinsic, instruction, "W",
                                      fvti.Vector, fwti.Vector, fvti.Mask, fvti.Log2SEW,
                                      fvti.LMul, fvti.RegClass, fwti.RegClass,
                                      isSEWAware>;
  }
}

multiclass VPatConversionVF_WF_RTZ<string intrinsic, string instruction,
                                   list<VTypeInfoToWide> wlist = AllWidenableFloatVectors,
                                   bit isSEWAware = 0> {
  foreach fvtiToFWti = wlist in {
    defvar fvti = fvtiToFWti.Vti;
    defvar fwti = fvtiToFWti.Wti;
    let Predicates = !listconcat(GetVTypePredicates<fvti>.Predicates,
                                 GetVTypePredicates<fwti>.Predicates) in
    defm : VPatConversionRTZ<intrinsic, instruction, "W",
                             fvti.Vector, fwti.Vector, fvti.Mask, fvti.Log2SEW,
                             fvti.LMul, fvti.RegClass, fwti.RegClass, isSEWAware>;
  }
}

multiclass VPatConversionVF_WF_BF_RM<string intrinsic, string instruction,
                                     bit isSEWAware = 0> {
  foreach fvtiToFWti = AllWidenableBFloatToFloatVectors in {
    defvar fvti = fvtiToFWti.Vti;
    defvar fwti = fvtiToFWti.Wti;
    let Predicates = !listconcat(GetVTypePredicates<fvti>.Predicates,
                                 GetVTypePredicates<fwti>.Predicates) in
    defm : VPatConversionRoundingMode<intrinsic, instruction, "W",
                                      fvti.Vector, fwti.Vector, fvti.Mask, fvti.Log2SEW,
                                      fvti.LMul, fvti.RegClass, fwti.RegClass,
                                      isSEWAware>;
  }
}

multiclass VPatCompare_VI<string intrinsic, string inst,
                          ImmLeaf ImmType> {
  foreach vti = AllIntegerVectors in {
    defvar Intr = !cast<Intrinsic>(intrinsic);
    defvar Pseudo = !cast<Instruction>(inst#"_VI_"#vti.LMul.MX);
    let Predicates = GetVTypePredicates<vti>.Predicates in
    def : Pat<(vti.Mask (Intr (vti.Vector vti.RegClass:$rs1),
                              (vti.Scalar ImmType:$rs2),
                              VLOpFrag)),
              (Pseudo vti.RegClass:$rs1, (DecImm ImmType:$rs2),
                      GPR:$vl, vti.Log2SEW)>;
    defvar IntrMask = !cast<Intrinsic>(intrinsic # "_mask");
    defvar PseudoMask = !cast<Instruction>(inst#"_VI_"#vti.LMul.MX#"_MASK");
    let Predicates = GetVTypePredicates<vti>.Predicates in
    def : Pat<(vti.Mask (IntrMask (vti.Mask VR:$merge),
                                  (vti.Vector vti.RegClass:$rs1),
                                  (vti.Scalar ImmType:$rs2),
                                  (vti.Mask V0),
                                  VLOpFrag)),
              (PseudoMask VR:$merge, vti.RegClass:$rs1, (DecImm ImmType:$rs2),
                          (vti.Mask V0), GPR:$vl, vti.Log2SEW)>;
  }
}

//===----------------------------------------------------------------------===//
// Pseudo instructions
//===----------------------------------------------------------------------===//

let Predicates = [HasVInstructions] in {

//===----------------------------------------------------------------------===//
// Pseudo Instructions for CodeGen
//===----------------------------------------------------------------------===//

let hasSideEffects = 0, mayLoad = 0, mayStore = 0, isCodeGenOnly = 1 in {
  def PseudoReadVLENB : Pseudo<(outs GPR:$rd), (ins),
                               [(set GPR:$rd, (riscv_read_vlenb))]>,
                        PseudoInstExpansion<(CSRRS GPR:$rd, SysRegVLENB.Encoding, X0)>,
                        Sched<[WriteRdVLENB]>;
}

let hasSideEffects = 0, mayLoad = 0, mayStore = 0, isCodeGenOnly = 1,
    Uses = [VL] in
def PseudoReadVL : Pseudo<(outs GPR:$rd), (ins), []>,
                   PseudoInstExpansion<(CSRRS GPR:$rd, SysRegVL.Encoding, X0)>;

foreach lmul = MxList in {
  foreach nf = NFSet<lmul>.L in {
    defvar vreg = SegRegClass<lmul, nf>.RC;
    let hasSideEffects = 0, mayLoad = 0, mayStore = 1, isCodeGenOnly = 1,
        Size = !mul(4, !sub(!mul(nf, 2), 1)) in {
      def "PseudoVSPILL" # nf # "_" # lmul.MX :
        Pseudo<(outs), (ins vreg:$rs1, GPR:$rs2), []>;
    }
    let hasSideEffects = 0, mayLoad = 1, mayStore = 0, isCodeGenOnly = 1,
        Size = !mul(4, !sub(!mul(nf, 2), 1)) in {
      def "PseudoVRELOAD" # nf # "_" # lmul.MX :
        Pseudo<(outs vreg:$rs1), (ins GPR:$rs2), []>;
    }
  }
}

/// Empty pseudo for RISCVInitUndefPass
let hasSideEffects = 0, mayLoad = 0, mayStore = 0, Size = 0,
    isCodeGenOnly = 1 in {
  def PseudoRVVInitUndefM1 : Pseudo<(outs VR:$vd), (ins), [], "">;
  def PseudoRVVInitUndefM2 : Pseudo<(outs VRM2:$vd), (ins), [], "">;
  def PseudoRVVInitUndefM4 : Pseudo<(outs VRM4:$vd), (ins), [], "">;
  def PseudoRVVInitUndefM8 : Pseudo<(outs VRM8:$vd), (ins), [], "">;
}

//===----------------------------------------------------------------------===//
// 6. Configuration-Setting Instructions
//===----------------------------------------------------------------------===//

// Pseudos.
let hasSideEffects = 0, mayLoad = 0, mayStore = 0, Defs = [VL, VTYPE] in {
// Due to rs1=X0 having special meaning, we need a GPRNoX0 register class for
// the when we aren't using one of the special X0 encodings. Otherwise it could
// be accidentally be made X0 by MachineIR optimizations. To satisfy the
// verifier, we also need a GPRX0 instruction for the special encodings.
def PseudoVSETVLI : Pseudo<(outs GPR:$rd), (ins GPRNoX0:$rs1, VTypeIOp11:$vtypei), []>,
                    Sched<[WriteVSETVLI, ReadVSETVLI]>;
def PseudoVSETVLIX0 : Pseudo<(outs GPR:$rd), (ins GPRX0:$rs1, VTypeIOp11:$vtypei), []>,
                      Sched<[WriteVSETVLI, ReadVSETVLI]>;
def PseudoVSETIVLI : Pseudo<(outs GPR:$rd), (ins uimm5:$rs1, VTypeIOp10:$vtypei), []>,
                     Sched<[WriteVSETIVLI]>;
}

//===----------------------------------------------------------------------===//
// 7. Vector Loads and Stores
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// 7.4 Vector Unit-Stride Instructions
//===----------------------------------------------------------------------===//

// Pseudos Unit-Stride Loads and Stores
defm PseudoVL : VPseudoUSLoad;
defm PseudoVS : VPseudoUSStore;

defm PseudoVLM : VPseudoLoadMask;
defm PseudoVSM : VPseudoStoreMask;

//===----------------------------------------------------------------------===//
// 7.5 Vector Strided Instructions
//===----------------------------------------------------------------------===//

// Vector Strided Loads and Stores
defm PseudoVLS : VPseudoSLoad;
defm PseudoVSS : VPseudoSStore;

//===----------------------------------------------------------------------===//
// 7.6 Vector Indexed Instructions
//===----------------------------------------------------------------------===//

// Vector Indexed Loads and Stores
defm PseudoVLUX : VPseudoILoad<Ordered=false>;
defm PseudoVLOX : VPseudoILoad<Ordered=true>;
defm PseudoVSOX : VPseudoIStore<Ordered=true>;
defm PseudoVSUX : VPseudoIStore<Ordered=false>;

//===----------------------------------------------------------------------===//
// 7.7. Unit-stride Fault-Only-First Loads
//===----------------------------------------------------------------------===//

// vleff may update VL register
let Defs = [VL] in
defm PseudoVL : VPseudoFFLoad;

//===----------------------------------------------------------------------===//
// 7.8. Vector Load/Store Segment Instructions
//===----------------------------------------------------------------------===//
defm PseudoVLSEG : VPseudoUSSegLoad;
defm PseudoVLSSEG : VPseudoSSegLoad;
defm PseudoVLOXSEG : VPseudoISegLoad<Ordered=true>;
defm PseudoVLUXSEG : VPseudoISegLoad<Ordered=false>;
defm PseudoVSSEG : VPseudoUSSegStore;
defm PseudoVSSSEG : VPseudoSSegStore;
defm PseudoVSOXSEG : VPseudoISegStore<Ordered=true>;
defm PseudoVSUXSEG : VPseudoISegStore<Ordered=false>;

// vlseg<nf>e<eew>ff.v may update VL register
let Defs = [VL] in {
defm PseudoVLSEG : VPseudoUSSegLoadFF;
}

//===----------------------------------------------------------------------===//
// 11. Vector Integer Arithmetic Instructions
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// 11.1. Vector Single-Width Integer Add and Subtract
//===----------------------------------------------------------------------===//
defm PseudoVADD   : VPseudoVALU_VV_VX_VI<Commutable=1>;
defm PseudoVSUB   : VPseudoVALU_VV_VX;
defm PseudoVRSUB  : VPseudoVALU_VX_VI;

foreach vti = AllIntegerVectors in {
  // Match vrsub with 2 vector operands to vsub.vv by swapping operands. This
  // Occurs when legalizing vrsub.vx intrinsics for i64 on RV32 since we need
  // to use a more complex splat sequence. Add the pattern for all VTs for
  // consistency.
  let Predicates = GetVTypePredicates<vti>.Predicates in {
    def : Pat<(vti.Vector (int_riscv_vrsub (vti.Vector vti.RegClass:$merge),
                                           (vti.Vector vti.RegClass:$rs2),
                                           (vti.Vector vti.RegClass:$rs1),
                                           VLOpFrag)),
              (!cast<Instruction>("PseudoVSUB_VV_"#vti.LMul.MX)
                                                        vti.RegClass:$merge,
                                                        vti.RegClass:$rs1,
                                                        vti.RegClass:$rs2,
                                                        GPR:$vl,
                                                        vti.Log2SEW, TU_MU)>;
    def : Pat<(vti.Vector (int_riscv_vrsub_mask (vti.Vector vti.RegClass:$merge),
                                                (vti.Vector vti.RegClass:$rs2),
                                                (vti.Vector vti.RegClass:$rs1),
                                                (vti.Mask V0),
                                                VLOpFrag,
                                                (XLenVT timm:$policy))),
              (!cast<Instruction>("PseudoVSUB_VV_"#vti.LMul.MX#"_MASK")
                                                        vti.RegClass:$merge,
                                                        vti.RegClass:$rs1,
                                                        vti.RegClass:$rs2,
                                                        (vti.Mask V0),
                                                        GPR:$vl,
                                                        vti.Log2SEW,
                                                        (XLenVT timm:$policy))>;

    // Match VSUB with a small immediate to vadd.vi by negating the immediate.
    def : Pat<(vti.Vector (int_riscv_vsub (vti.Vector vti.RegClass:$merge),
                                          (vti.Vector vti.RegClass:$rs1),
                                          (vti.Scalar simm5_plus1:$rs2),
                                          VLOpFrag)),
              (!cast<Instruction>("PseudoVADD_VI_"#vti.LMul.MX)
                                                      vti.RegClass:$merge,
                                                      vti.RegClass:$rs1,
                                                      (NegImm simm5_plus1:$rs2),
                                                      GPR:$vl,
                                                      vti.Log2SEW, TU_MU)>;
    def : Pat<(vti.Vector (int_riscv_vsub_mask (vti.Vector vti.RegClass:$merge),
                                               (vti.Vector vti.RegClass:$rs1),
                                               (vti.Scalar simm5_plus1:$rs2),
                                               (vti.Mask V0),
                                               VLOpFrag,
                                               (XLenVT timm:$policy))),
              (!cast<Instruction>("PseudoVADD_VI_"#vti.LMul.MX#"_MASK")
                                                        vti.RegClass:$merge,
                                                        vti.RegClass:$rs1,
                                                        (NegImm simm5_plus1:$rs2),
                                                        (vti.Mask V0),
                                                        GPR:$vl,
                                                        vti.Log2SEW,
                                                        (XLenVT timm:$policy))>;
  }
}

//===----------------------------------------------------------------------===//
// 11.2. Vector Widening Integer Add/Subtract
//===----------------------------------------------------------------------===//
defm PseudoVWADDU : VPseudoVWALU_VV_VX<Commutable=1>;
defm PseudoVWSUBU : VPseudoVWALU_VV_VX;
defm PseudoVWADD  : VPseudoVWALU_VV_VX<Commutable=1>;
defm PseudoVWSUB  : VPseudoVWALU_VV_VX;
defm PseudoVWADDU : VPseudoVWALU_WV_WX;
defm PseudoVWSUBU : VPseudoVWALU_WV_WX;
defm PseudoVWADD  : VPseudoVWALU_WV_WX;
defm PseudoVWSUB  : VPseudoVWALU_WV_WX;

//===----------------------------------------------------------------------===//
// 11.3. Vector Integer Extension
//===----------------------------------------------------------------------===//
defm PseudoVZEXT_VF2 : PseudoVEXT_VF2;
defm PseudoVZEXT_VF4 : PseudoVEXT_VF4;
defm PseudoVZEXT_VF8 : PseudoVEXT_VF8;
defm PseudoVSEXT_VF2 : PseudoVEXT_VF2;
defm PseudoVSEXT_VF4 : PseudoVEXT_VF4;
defm PseudoVSEXT_VF8 : PseudoVEXT_VF8;

//===----------------------------------------------------------------------===//
// 11.4. Vector Integer Add-with-Carry / Subtract-with-Borrow Instructions
//===----------------------------------------------------------------------===//
defm PseudoVADC  : VPseudoVCALU_VM_XM_IM;
defm PseudoVMADC : VPseudoVCALUM_VM_XM_IM;
defm PseudoVMADC : VPseudoVCALUM_V_X_I;

defm PseudoVSBC  : VPseudoVCALU_VM_XM;
defm PseudoVMSBC : VPseudoVCALUM_VM_XM;
defm PseudoVMSBC : VPseudoVCALUM_V_X;

//===----------------------------------------------------------------------===//
// 11.5. Vector Bitwise Logical Instructions
//===----------------------------------------------------------------------===//
defm PseudoVAND : VPseudoVALU_VV_VX_VI<Commutable=1>;
defm PseudoVOR  : VPseudoVALU_VV_VX_VI<Commutable=1>;
defm PseudoVXOR : VPseudoVALU_VV_VX_VI<Commutable=1>;

//===----------------------------------------------------------------------===//
// 11.6. Vector Single-Width Bit Shift Instructions
//===----------------------------------------------------------------------===//
defm PseudoVSLL : VPseudoVSHT_VV_VX_VI;
defm PseudoVSRL : VPseudoVSHT_VV_VX_VI;
defm PseudoVSRA : VPseudoVSHT_VV_VX_VI;

//===----------------------------------------------------------------------===//
// 11.7. Vector Narrowing Integer Right Shift Instructions
//===----------------------------------------------------------------------===//
defm PseudoVNSRL : VPseudoVNSHT_WV_WX_WI;
defm PseudoVNSRA : VPseudoVNSHT_WV_WX_WI;

//===----------------------------------------------------------------------===//
// 11.8. Vector Integer Comparison Instructions
//===----------------------------------------------------------------------===//
defm PseudoVMSEQ  : VPseudoVCMPM_VV_VX_VI<Commutable=1>;
defm PseudoVMSNE  : VPseudoVCMPM_VV_VX_VI<Commutable=1>;
defm PseudoVMSLTU : VPseudoVCMPM_VV_VX;
defm PseudoVMSLT  : VPseudoVCMPM_VV_VX;
defm PseudoVMSLEU : VPseudoVCMPM_VV_VX_VI;
defm PseudoVMSLE  : VPseudoVCMPM_VV_VX_VI;
defm PseudoVMSGTU : VPseudoVCMPM_VX_VI;
defm PseudoVMSGT  : VPseudoVCMPM_VX_VI;

//===----------------------------------------------------------------------===//
// 11.9. Vector Integer Min/Max Instructions
//===----------------------------------------------------------------------===//
defm PseudoVMINU : VPseudoVMINMAX_VV_VX;
defm PseudoVMIN  : VPseudoVMINMAX_VV_VX;
defm PseudoVMAXU : VPseudoVMINMAX_VV_VX;
defm PseudoVMAX  : VPseudoVMINMAX_VV_VX;

//===----------------------------------------------------------------------===//
// 11.10. Vector Single-Width Integer Multiply Instructions
//===----------------------------------------------------------------------===//
defm PseudoVMUL    : VPseudoVMUL_VV_VX<Commutable=1>;
defm PseudoVMULH   : VPseudoVMUL_VV_VX<Commutable=1>;
defm PseudoVMULHU  : VPseudoVMUL_VV_VX<Commutable=1>;
defm PseudoVMULHSU : VPseudoVMUL_VV_VX;

//===----------------------------------------------------------------------===//
// 11.11. Vector Integer Divide Instructions
//===----------------------------------------------------------------------===//
defm PseudoVDIVU : VPseudoVDIV_VV_VX;
defm PseudoVDIV  : VPseudoVDIV_VV_VX;
defm PseudoVREMU : VPseudoVDIV_VV_VX;
defm PseudoVREM  : VPseudoVDIV_VV_VX;

//===----------------------------------------------------------------------===//
// 11.12. Vector Widening Integer Multiply Instructions
//===----------------------------------------------------------------------===//
defm PseudoVWMUL   : VPseudoVWMUL_VV_VX<Commutable=1>;
defm PseudoVWMULU  : VPseudoVWMUL_VV_VX<Commutable=1>;
defm PseudoVWMULSU : VPseudoVWMUL_VV_VX;

//===----------------------------------------------------------------------===//
// 11.13. Vector Single-Width Integer Multiply-Add Instructions
//===----------------------------------------------------------------------===//
defm PseudoVMACC  : VPseudoVMAC_VV_VX_AAXA;
defm PseudoVNMSAC : VPseudoVMAC_VV_VX_AAXA;
defm PseudoVMADD  : VPseudoVMAC_VV_VX_AAXA;
defm PseudoVNMSUB : VPseudoVMAC_VV_VX_AAXA;

//===----------------------------------------------------------------------===//
// 11.14. Vector Widening Integer Multiply-Add Instructions
//===----------------------------------------------------------------------===//
defm PseudoVWMACCU  : VPseudoVWMAC_VV_VX<Commutable=1>;
defm PseudoVWMACC   : VPseudoVWMAC_VV_VX<Commutable=1>;
defm PseudoVWMACCSU : VPseudoVWMAC_VV_VX;
defm PseudoVWMACCUS : VPseudoVWMAC_VX;

//===----------------------------------------------------------------------===//
// 11.15. Vector Integer Merge Instructions
//===----------------------------------------------------------------------===//
defm PseudoVMERGE : VPseudoVMRG_VM_XM_IM;

//===----------------------------------------------------------------------===//
// 11.16. Vector Integer Move Instructions
//===----------------------------------------------------------------------===//
defm PseudoVMV_V : VPseudoUnaryVMV_V_X_I;

//===----------------------------------------------------------------------===//
// 12. Vector Fixed-Point Arithmetic Instructions
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// 12.1. Vector Single-Width Saturating Add and Subtract
//===----------------------------------------------------------------------===//
let Defs = [VXSAT] in {
  defm PseudoVSADDU : VPseudoVSALU_VV_VX_VI<Commutable=1>;
  defm PseudoVSADD  : VPseudoVSALU_VV_VX_VI<Commutable=1>;
  defm PseudoVSSUBU : VPseudoVSALU_VV_VX;
  defm PseudoVSSUB  : VPseudoVSALU_VV_VX;
}

//===----------------------------------------------------------------------===//
// 12.2. Vector Single-Width Averaging Add and Subtract
//===----------------------------------------------------------------------===//
defm PseudoVAADDU : VPseudoVAALU_VV_VX_RM<Commutable=1>;
defm PseudoVAADD  : VPseudoVAALU_VV_VX_RM<Commutable=1>;
defm PseudoVASUBU : VPseudoVAALU_VV_VX_RM;
defm PseudoVASUB  : VPseudoVAALU_VV_VX_RM;

//===----------------------------------------------------------------------===//
// 12.3. Vector Single-Width Fractional Multiply with Rounding and Saturation
//===----------------------------------------------------------------------===//
let Defs = [VXSAT] in {
  defm PseudoVSMUL : VPseudoVSMUL_VV_VX_RM;
}

//===----------------------------------------------------------------------===//
// 12.4. Vector Single-Width Scaling Shift Instructions
//===----------------------------------------------------------------------===//
defm PseudoVSSRL : VPseudoVSSHT_VV_VX_VI_RM;
defm PseudoVSSRA : VPseudoVSSHT_VV_VX_VI_RM;

//===----------------------------------------------------------------------===//
// 12.5. Vector Narrowing Fixed-Point Clip Instructions
//===----------------------------------------------------------------------===//
let Defs = [VXSAT] in {
  defm PseudoVNCLIP  : VPseudoVNCLP_WV_WX_WI_RM;
  defm PseudoVNCLIPU : VPseudoVNCLP_WV_WX_WI_RM;
}

} // Predicates = [HasVInstructions]

//===----------------------------------------------------------------------===//
// 13. Vector Floating-Point Instructions
//===----------------------------------------------------------------------===//

let Predicates = [HasVInstructionsAnyF] in {
//===----------------------------------------------------------------------===//
// 13.2. Vector Single-Width Floating-Point Add/Subtract Instructions
//===----------------------------------------------------------------------===//
let mayRaiseFPException = true, hasPostISelHook = 1 in {
defm PseudoVFADD  : VPseudoVALU_VV_VF_RM;
defm PseudoVFSUB  : VPseudoVALU_VV_VF_RM;
defm PseudoVFRSUB : VPseudoVALU_VF_RM;
}

//===----------------------------------------------------------------------===//
// 13.3. Vector Widening Floating-Point Add/Subtract Instructions
//===----------------------------------------------------------------------===//
let mayRaiseFPException = true, hasSideEffects = 0, hasPostISelHook = 1 in {
defm PseudoVFWADD : VPseudoVFWALU_VV_VF_RM;
defm PseudoVFWSUB : VPseudoVFWALU_VV_VF_RM;
defm PseudoVFWADD : VPseudoVFWALU_WV_WF_RM;
defm PseudoVFWSUB : VPseudoVFWALU_WV_WF_RM;
}

//===----------------------------------------------------------------------===//
// 13.4. Vector Single-Width Floating-Point Multiply/Divide Instructions
//===----------------------------------------------------------------------===//
let mayRaiseFPException = true, hasSideEffects = 0, hasPostISelHook = 1 in {
defm PseudoVFMUL  : VPseudoVFMUL_VV_VF_RM;
defm PseudoVFDIV  : VPseudoVFDIV_VV_VF_RM;
defm PseudoVFRDIV : VPseudoVFRDIV_VF_RM;
}

//===----------------------------------------------------------------------===//
// 13.5. Vector Widening Floating-Point Multiply
//===----------------------------------------------------------------------===//
let mayRaiseFPException = true, hasSideEffects = 0, hasPostISelHook = 1 in {
defm PseudoVFWMUL : VPseudoVWMUL_VV_VF_RM;
}

//===----------------------------------------------------------------------===//
// 13.6. Vector Single-Width Floating-Point Fused Multiply-Add Instructions
//===----------------------------------------------------------------------===//
let mayRaiseFPException = true, hasSideEffects = 0, hasPostISelHook = 1 in {
defm PseudoVFMACC  : VPseudoVMAC_VV_VF_AAXA_RM;
defm PseudoVFNMACC : VPseudoVMAC_VV_VF_AAXA_RM;
defm PseudoVFMSAC  : VPseudoVMAC_VV_VF_AAXA_RM;
defm PseudoVFNMSAC : VPseudoVMAC_VV_VF_AAXA_RM;
defm PseudoVFMADD  : VPseudoVMAC_VV_VF_AAXA_RM;
defm PseudoVFNMADD : VPseudoVMAC_VV_VF_AAXA_RM;
defm PseudoVFMSUB  : VPseudoVMAC_VV_VF_AAXA_RM;
defm PseudoVFNMSUB : VPseudoVMAC_VV_VF_AAXA_RM;
}

//===----------------------------------------------------------------------===//
// 13.7. Vector Widening Floating-Point Fused Multiply-Add Instructions
//===----------------------------------------------------------------------===//
let mayRaiseFPException = true, hasSideEffects = 0, hasPostISelHook = 1 in {
defm PseudoVFWMACC  : VPseudoVWMAC_VV_VF_RM;
defm PseudoVFWNMACC : VPseudoVWMAC_VV_VF_RM;
defm PseudoVFWMSAC  : VPseudoVWMAC_VV_VF_RM;
defm PseudoVFWNMSAC : VPseudoVWMAC_VV_VF_RM;
let Predicates = [HasStdExtZvfbfwma] in
defm PseudoVFWMACCBF16  : VPseudoVWMAC_VV_VF_BF_RM;
}

//===----------------------------------------------------------------------===//
// 13.8. Vector Floating-Point Square-Root Instruction
//===----------------------------------------------------------------------===//
let mayRaiseFPException = true, hasSideEffects = 0, hasPostISelHook = 1 in
defm PseudoVFSQRT : VPseudoVSQR_V_RM;

//===----------------------------------------------------------------------===//
// 13.9. Vector Floating-Point Reciprocal Square-Root Estimate Instruction
//===----------------------------------------------------------------------===//
let mayRaiseFPException = true in
defm PseudoVFRSQRT7 : VPseudoVRCP_V;

//===----------------------------------------------------------------------===//
// 13.10. Vector Floating-Point Reciprocal Estimate Instruction
//===----------------------------------------------------------------------===//
let mayRaiseFPException = true, hasSideEffects = 0, hasPostISelHook = 1 in
defm PseudoVFREC7 : VPseudoVRCP_V_RM;

//===----------------------------------------------------------------------===//
// 13.11. Vector Floating-Point Min/Max Instructions
//===----------------------------------------------------------------------===//
let mayRaiseFPException = true in {
defm PseudoVFMIN : VPseudoVMAX_VV_VF;
defm PseudoVFMAX : VPseudoVMAX_VV_VF;
}

//===----------------------------------------------------------------------===//
// 13.12. Vector Floating-Point Sign-Injection Instructions
//===----------------------------------------------------------------------===//
defm PseudoVFSGNJ  : VPseudoVSGNJ_VV_VF;
defm PseudoVFSGNJN : VPseudoVSGNJ_VV_VF;
defm PseudoVFSGNJX : VPseudoVSGNJ_VV_VF;

//===----------------------------------------------------------------------===//
// 13.13. Vector Floating-Point Compare Instructions
//===----------------------------------------------------------------------===//
let mayRaiseFPException = true in {
defm PseudoVMFEQ : VPseudoVCMPM_VV_VF;
defm PseudoVMFNE : VPseudoVCMPM_VV_VF;
defm PseudoVMFLT : VPseudoVCMPM_VV_VF;
defm PseudoVMFLE : VPseudoVCMPM_VV_VF;
defm PseudoVMFGT : VPseudoVCMPM_VF;
defm PseudoVMFGE : VPseudoVCMPM_VF;
}

//===----------------------------------------------------------------------===//
// 13.14. Vector Floating-Point Classify Instruction
//===----------------------------------------------------------------------===//
defm PseudoVFCLASS : VPseudoVCLS_V;

//===----------------------------------------------------------------------===//
// 13.15. Vector Floating-Point Merge Instruction
//===----------------------------------------------------------------------===//
defm PseudoVFMERGE : VPseudoVMRG_FM;

//===----------------------------------------------------------------------===//
// 13.16. Vector Floating-Point Move Instruction
//===----------------------------------------------------------------------===//
defm PseudoVFMV_V : VPseudoVMV_F;

//===----------------------------------------------------------------------===//
// 13.17. Single-Width Floating-Point/Integer Type-Convert Instructions
//===----------------------------------------------------------------------===//
let mayRaiseFPException = true in {
let hasSideEffects = 0, hasPostISelHook = 1 in {
defm PseudoVFCVT_XU_F : VPseudoVCVTI_V_RM;
defm PseudoVFCVT_X_F : VPseudoVCVTI_V_RM;
}

defm PseudoVFCVT_RM_XU_F : VPseudoVCVTI_RM_V;
defm PseudoVFCVT_RM_X_F : VPseudoVCVTI_RM_V;

defm PseudoVFCVT_RTZ_XU_F : VPseudoVCVTI_V;
defm PseudoVFCVT_RTZ_X_F : VPseudoVCVTI_V;

defm PseudoVFROUND_NOEXCEPT : VPseudoVFROUND_NOEXCEPT_V;
let hasSideEffects = 0, hasPostISelHook = 1 in {
defm PseudoVFCVT_F_XU : VPseudoVCVTF_V_RM;
defm PseudoVFCVT_F_X : VPseudoVCVTF_V_RM;
}
defm PseudoVFCVT_RM_F_XU : VPseudoVCVTF_RM_V;
defm PseudoVFCVT_RM_F_X  : VPseudoVCVTF_RM_V;
} // mayRaiseFPException = true

//===----------------------------------------------------------------------===//
// 13.18. Widening Floating-Point/Integer Type-Convert Instructions
//===----------------------------------------------------------------------===//
let mayRaiseFPException = true in {
let hasSideEffects = 0, hasPostISelHook = 1 in {
defm PseudoVFWCVT_XU_F     : VPseudoVWCVTI_V_RM;
defm PseudoVFWCVT_X_F      : VPseudoVWCVTI_V_RM;
}
defm PseudoVFWCVT_RM_XU_F  : VPseudoVWCVTI_RM_V;
defm PseudoVFWCVT_RM_X_F   : VPseudoVWCVTI_RM_V;

defm PseudoVFWCVT_RTZ_XU_F : VPseudoVWCVTI_V;
defm PseudoVFWCVT_RTZ_X_F  : VPseudoVWCVTI_V;

defm PseudoVFWCVT_F_XU     : VPseudoVWCVTF_V;
defm PseudoVFWCVT_F_X      : VPseudoVWCVTF_V;

defm PseudoVFWCVT_F_F      : VPseudoVWCVTD_V;
defm PseudoVFWCVTBF16_F_F :  VPseudoVWCVTD_V;
} // mayRaiseFPException = true

//===----------------------------------------------------------------------===//
// 13.19. Narrowing Floating-Point/Integer Type-Convert Instructions
//===----------------------------------------------------------------------===//
let mayRaiseFPException = true in {
let hasSideEffects = 0, hasPostISelHook = 1 in {
defm PseudoVFNCVT_XU_F     : VPseudoVNCVTI_W_RM;
defm PseudoVFNCVT_X_F      : VPseudoVNCVTI_W_RM;
}
defm PseudoVFNCVT_RM_XU_F  : VPseudoVNCVTI_RM_W;
defm PseudoVFNCVT_RM_X_F   : VPseudoVNCVTI_RM_W;

defm PseudoVFNCVT_RTZ_XU_F : VPseudoVNCVTI_W;
defm PseudoVFNCVT_RTZ_X_F  : VPseudoVNCVTI_W;

let hasSideEffects = 0, hasPostISelHook = 1 in {
defm PseudoVFNCVT_F_XU     : VPseudoVNCVTF_W_RM;
defm PseudoVFNCVT_F_X      : VPseudoVNCVTF_W_RM;
}
defm PseudoVFNCVT_RM_F_XU  : VPseudoVNCVTF_RM_W;
defm PseudoVFNCVT_RM_F_X   : VPseudoVNCVTF_RM_W;

let hasSideEffects = 0, hasPostISelHook = 1 in {
defm PseudoVFNCVT_F_F      : VPseudoVNCVTD_W_RM;
defm PseudoVFNCVTBF16_F_F :  VPseudoVNCVTD_W_RM;
}

defm PseudoVFNCVT_ROD_F_F  : VPseudoVNCVTD_W;
} // mayRaiseFPException = true
} // Predicates = [HasVInstructionsAnyF]

//===----------------------------------------------------------------------===//
// 14. Vector Reduction Operations
//===----------------------------------------------------------------------===//

let Predicates = [HasVInstructions] in {
//===----------------------------------------------------------------------===//
// 14.1. Vector Single-Width Integer Reduction Instructions
//===----------------------------------------------------------------------===//
defm PseudoVREDSUM  : VPseudoVRED_VS;
defm PseudoVREDAND  : VPseudoVRED_VS;
defm PseudoVREDOR   : VPseudoVRED_VS;
defm PseudoVREDXOR  : VPseudoVRED_VS;
defm PseudoVREDMINU : VPseudoVREDMINMAX_VS;
defm PseudoVREDMIN  : VPseudoVREDMINMAX_VS;
defm PseudoVREDMAXU : VPseudoVREDMINMAX_VS;
defm PseudoVREDMAX  : VPseudoVREDMINMAX_VS;

//===----------------------------------------------------------------------===//
// 14.2. Vector Widening Integer Reduction Instructions
//===----------------------------------------------------------------------===//
let IsRVVWideningReduction = 1 in {
defm PseudoVWREDSUMU   : VPseudoVWRED_VS;
defm PseudoVWREDSUM    : VPseudoVWRED_VS;
}
} // Predicates = [HasVInstructions]

let Predicates = [HasVInstructionsAnyF] in {
//===----------------------------------------------------------------------===//
// 14.3. Vector Single-Width Floating-Point Reduction Instructions
//===----------------------------------------------------------------------===//
let mayRaiseFPException = true, hasSideEffects = 0, hasPostISelHook = 1 in {
defm PseudoVFREDOSUM : VPseudoVFREDO_VS_RM;
defm PseudoVFREDUSUM : VPseudoVFRED_VS_RM;
}
let mayRaiseFPException = true in {
defm PseudoVFREDMIN  : VPseudoVFREDMINMAX_VS;
defm PseudoVFREDMAX  : VPseudoVFREDMINMAX_VS;
}

//===----------------------------------------------------------------------===//
// 14.4. Vector Widening Floating-Point Reduction Instructions
//===----------------------------------------------------------------------===//
let IsRVVWideningReduction = 1, hasSideEffects = 0, mayRaiseFPException = true,
    hasPostISelHook = 1 in {
defm PseudoVFWREDUSUM  : VPseudoVFWRED_VS_RM;
defm PseudoVFWREDOSUM  : VPseudoVFWREDO_VS_RM;
}

} // Predicates = [HasVInstructionsAnyF]

//===----------------------------------------------------------------------===//
// 15. Vector Mask Instructions
//===----------------------------------------------------------------------===//

let Predicates = [HasVInstructions] in {
//===----------------------------------------------------------------------===//
// 15.1 Vector Mask-Register Logical Instructions
//===----------------------------------------------------------------------===//

defm PseudoVMAND: VPseudoVALU_MM<Commutable=1>;
defm PseudoVMNAND: VPseudoVALU_MM<Commutable=1>;
defm PseudoVMANDN: VPseudoVALU_MM;
defm PseudoVMXOR: VPseudoVALU_MM<Commutable=1>;
defm PseudoVMOR: VPseudoVALU_MM<Commutable=1>;
defm PseudoVMNOR: VPseudoVALU_MM<Commutable=1>;
defm PseudoVMORN: VPseudoVALU_MM;
defm PseudoVMXNOR: VPseudoVALU_MM<Commutable=1>;

// Pseudo instructions
defm PseudoVMCLR : VPseudoNullaryPseudoM<"VMXOR">;
defm PseudoVMSET : VPseudoNullaryPseudoM<"VMXNOR">;

//===----------------------------------------------------------------------===//
// 15.2. Vector mask population count vcpop
//===----------------------------------------------------------------------===//
let IsSignExtendingOpW = 1 in
defm PseudoVCPOP: VPseudoVPOP_M;

//===----------------------------------------------------------------------===//
// 15.3. vfirst find-first-set mask bit
//===----------------------------------------------------------------------===//

let IsSignExtendingOpW = 1 in
defm PseudoVFIRST: VPseudoV1ST_M;

//===----------------------------------------------------------------------===//
// 15.4. vmsbf.m set-before-first mask bit
//===----------------------------------------------------------------------===//
defm PseudoVMSBF: VPseudoVSFS_M;

//===----------------------------------------------------------------------===//
// 15.5. vmsif.m set-including-first mask bit
//===----------------------------------------------------------------------===//
defm PseudoVMSIF: VPseudoVSFS_M;

//===----------------------------------------------------------------------===//
// 15.6. vmsof.m set-only-first mask bit
//===----------------------------------------------------------------------===//
defm PseudoVMSOF: VPseudoVSFS_M;

//===----------------------------------------------------------------------===//
// 15.8.  Vector Iota Instruction
//===----------------------------------------------------------------------===//
defm PseudoVIOTA_M: VPseudoVIOTA_M;

//===----------------------------------------------------------------------===//
// 15.9. Vector Element Index Instruction
//===----------------------------------------------------------------------===//
let isReMaterializable = 1 in
defm PseudoVID : VPseudoVID_V;
} // Predicates = [HasVInstructions]

//===----------------------------------------------------------------------===//
// 16. Vector Permutation Instructions
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// 16.1. Integer Scalar Move Instructions
//===----------------------------------------------------------------------===//

let Predicates = [HasVInstructions] in {
let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in {
  let HasSEWOp = 1, BaseInstr = VMV_X_S in
  def PseudoVMV_X_S:
    Pseudo<(outs GPR:$rd), (ins VR:$rs2, ixlenimm:$sew), []>,
    Sched<[WriteVMovXS, ReadVMovXS]>,
    RISCVVPseudo;
  let HasVLOp = 1, HasSEWOp = 1, BaseInstr = VMV_S_X,
      Constraints = "$rd = $rs1" in
  def PseudoVMV_S_X: Pseudo<(outs VR:$rd),
                            (ins VR:$rs1, GPR:$rs2, AVL:$vl, ixlenimm:$sew),
                            []>,
    Sched<[WriteVMovSX, ReadVMovSX_V, ReadVMovSX_X]>,
    RISCVVPseudo;
}
} // Predicates = [HasVInstructions]

//===----------------------------------------------------------------------===//
// 16.2. Floating-Point Scalar Move Instructions
//===----------------------------------------------------------------------===//

let Predicates = [HasVInstructionsAnyF] in {
let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in {
  foreach f = FPList in {
    foreach m = f.MxList in {
      defvar mx = m.MX;
      let VLMul = m.value in {
        let HasSEWOp = 1, BaseInstr = VFMV_F_S in
        def "PseudoVFMV_" # f.FX # "_S_" # mx :
          Pseudo<(outs f.fprclass:$rd),
                 (ins m.vrclass:$rs2, ixlenimm:$sew), []>,
          Sched<[WriteVMovFS, ReadVMovFS]>,
          RISCVVPseudo;
        let HasVLOp = 1, HasSEWOp = 1, BaseInstr = VFMV_S_F,
            Constraints = "$rd = $rs1" in
        def "PseudoVFMV_S_" # f.FX # "_" # mx :
                                          Pseudo<(outs m.vrclass:$rd),
                                                 (ins m.vrclass:$rs1, f.fprclass:$rs2,
                                                      AVL:$vl, ixlenimm:$sew),
                                                 []>,
          Sched<[WriteVMovSF, ReadVMovSF_V, ReadVMovSF_F]>,
          RISCVVPseudo;
      }
    }
  }
}
} // Predicates = [HasVInstructionsAnyF]

//===----------------------------------------------------------------------===//
// 16.3. Vector Slide Instructions
//===----------------------------------------------------------------------===//
let Predicates = [HasVInstructions] in {
  defm PseudoVSLIDEUP    : VPseudoVSLD_VX_VI</*slidesUp=*/true, "@earlyclobber $rd">;
  defm PseudoVSLIDEDOWN  : VPseudoVSLD_VX_VI</*slidesUp=*/false>;
  defm PseudoVSLIDE1UP   : VPseudoVSLD1_VX<"@earlyclobber $rd">;
  defm PseudoVSLIDE1DOWN : VPseudoVSLD1_VX;
} // Predicates = [HasVInstructions]

let Predicates = [HasVInstructionsAnyF] in {
  defm PseudoVFSLIDE1UP  : VPseudoVSLD1_VF<"@earlyclobber $rd">;
  defm PseudoVFSLIDE1DOWN : VPseudoVSLD1_VF;
} // Predicates = [HasVInstructionsAnyF]

//===----------------------------------------------------------------------===//
// 16.4. Vector Register Gather Instructions
//===----------------------------------------------------------------------===//
let Predicates = [HasVInstructions] in {
defm PseudoVRGATHER     : VPseudoVGTR_VV_VX_VI;
defm PseudoVRGATHEREI16 : VPseudoVGTR_EI16_VV;

//===----------------------------------------------------------------------===//
// 16.5. Vector Compress Instruction
//===----------------------------------------------------------------------===//
defm PseudoVCOMPRESS : VPseudoVCPR_V;
} // Predicates = [HasVInstructions]

//===----------------------------------------------------------------------===//
// Patterns.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// 11. Vector Integer Arithmetic Instructions
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// 11.1. Vector Single-Width Integer Add and Subtract
//===----------------------------------------------------------------------===//
defm : VPatBinaryV_VV_VX_VI<"int_riscv_vadd", "PseudoVADD", AllIntegerVectors>;
defm : VPatBinaryV_VV_VX<"int_riscv_vsub", "PseudoVSUB", AllIntegerVectors>;
defm : VPatBinaryV_VX_VI<"int_riscv_vrsub", "PseudoVRSUB", AllIntegerVectors>;

//===----------------------------------------------------------------------===//
// 11.2. Vector Widening Integer Add/Subtract
//===----------------------------------------------------------------------===//
defm : VPatBinaryW_VV_VX<"int_riscv_vwaddu", "PseudoVWADDU", AllWidenableIntVectors>;
defm : VPatBinaryW_VV_VX<"int_riscv_vwsubu", "PseudoVWSUBU", AllWidenableIntVectors>;
defm : VPatBinaryW_VV_VX<"int_riscv_vwadd", "PseudoVWADD", AllWidenableIntVectors>;
defm : VPatBinaryW_VV_VX<"int_riscv_vwsub", "PseudoVWSUB", AllWidenableIntVectors>;
defm : VPatBinaryW_WV_WX<"int_riscv_vwaddu_w", "PseudoVWADDU", AllWidenableIntVectors>;
defm : VPatBinaryW_WV_WX<"int_riscv_vwsubu_w", "PseudoVWSUBU", AllWidenableIntVectors>;
defm : VPatBinaryW_WV_WX<"int_riscv_vwadd_w", "PseudoVWADD", AllWidenableIntVectors>;
defm : VPatBinaryW_WV_WX<"int_riscv_vwsub_w", "PseudoVWSUB", AllWidenableIntVectors>;

//===----------------------------------------------------------------------===//
// 11.3. Vector Integer Extension
//===----------------------------------------------------------------------===//
defm : VPatUnaryV_VF<"int_riscv_vzext", "PseudoVZEXT", "VF2",
                     AllFractionableVF2IntVectors>;
defm : VPatUnaryV_VF<"int_riscv_vzext", "PseudoVZEXT", "VF4",
                     AllFractionableVF4IntVectors>;
defm : VPatUnaryV_VF<"int_riscv_vzext", "PseudoVZEXT", "VF8",
                     AllFractionableVF8IntVectors>;
defm : VPatUnaryV_VF<"int_riscv_vsext", "PseudoVSEXT", "VF2",
                     AllFractionableVF2IntVectors>;
defm : VPatUnaryV_VF<"int_riscv_vsext", "PseudoVSEXT", "VF4",
                     AllFractionableVF4IntVectors>;
defm : VPatUnaryV_VF<"int_riscv_vsext", "PseudoVSEXT", "VF8",
                     AllFractionableVF8IntVectors>;

//===----------------------------------------------------------------------===//
// 11.4. Vector Integer Add-with-Carry / Subtract-with-Borrow Instructions
//===----------------------------------------------------------------------===//
defm : VPatBinaryV_VM_XM_IM<"int_riscv_vadc", "PseudoVADC">;
defm : VPatBinaryM_VM_XM_IM<"int_riscv_vmadc_carry_in", "PseudoVMADC">;
defm : VPatBinaryM_V_X_I<"int_riscv_vmadc", "PseudoVMADC">;

defm : VPatBinaryV_VM_XM<"int_riscv_vsbc", "PseudoVSBC">;
defm : VPatBinaryM_VM_XM<"int_riscv_vmsbc_borrow_in", "PseudoVMSBC">;
defm : VPatBinaryM_V_X<"int_riscv_vmsbc", "PseudoVMSBC">;

//===----------------------------------------------------------------------===//
// 11.5. Vector Bitwise Logical Instructions
//===----------------------------------------------------------------------===//
defm : VPatBinaryV_VV_VX_VI<"int_riscv_vand", "PseudoVAND", AllIntegerVectors>;
defm : VPatBinaryV_VV_VX_VI<"int_riscv_vor", "PseudoVOR", AllIntegerVectors>;
defm : VPatBinaryV_VV_VX_VI<"int_riscv_vxor", "PseudoVXOR", AllIntegerVectors>;

//===----------------------------------------------------------------------===//
// 11.6. Vector Single-Width Bit Shift Instructions
//===----------------------------------------------------------------------===//
defm : VPatBinaryV_VV_VX_VI<"int_riscv_vsll", "PseudoVSLL", AllIntegerVectors,
                            uimm5>;
defm : VPatBinaryV_VV_VX_VI<"int_riscv_vsrl", "PseudoVSRL", AllIntegerVectors,
                            uimm5>;
defm : VPatBinaryV_VV_VX_VI<"int_riscv_vsra", "PseudoVSRA", AllIntegerVectors,
                            uimm5>;

foreach vti = AllIntegerVectors in {
  // Emit shift by 1 as an add since it might be faster.
  let Predicates = GetVTypePredicates<vti>.Predicates in {
    def : Pat<(vti.Vector (int_riscv_vsll (vti.Vector vti.RegClass:$merge),
                                          (vti.Vector vti.RegClass:$rs1),
                                          (XLenVT 1), VLOpFrag)),
              (!cast<Instruction>("PseudoVADD_VV_"#vti.LMul.MX)
                 vti.RegClass:$merge, vti.RegClass:$rs1,
                 vti.RegClass:$rs1, GPR:$vl, vti.Log2SEW, TU_MU)>;
    def : Pat<(vti.Vector (int_riscv_vsll_mask (vti.Vector vti.RegClass:$merge),
                                               (vti.Vector vti.RegClass:$rs1),
                                               (XLenVT 1),
                                               (vti.Mask V0),
                                               VLOpFrag,
                                               (XLenVT timm:$policy))),
              (!cast<Instruction>("PseudoVADD_VV_"#vti.LMul.MX#"_MASK")
                                                          vti.RegClass:$merge,
                                                          vti.RegClass:$rs1,
                                                          vti.RegClass:$rs1,
                                                          (vti.Mask V0),
                                                          GPR:$vl,
                                                          vti.Log2SEW,
                                                          (XLenVT timm:$policy))>;
  }
}

//===----------------------------------------------------------------------===//
// 11.7. Vector Narrowing Integer Right Shift Instructions
//===----------------------------------------------------------------------===//
defm : VPatBinaryV_WV_WX_WI<"int_riscv_vnsrl", "PseudoVNSRL", AllWidenableIntVectors>;
defm : VPatBinaryV_WV_WX_WI<"int_riscv_vnsra", "PseudoVNSRA", AllWidenableIntVectors>;

//===----------------------------------------------------------------------===//
// 11.8. Vector Integer Comparison Instructions
//===----------------------------------------------------------------------===//
defm : VPatBinaryM_VV_VX_VI<"int_riscv_vmseq", "PseudoVMSEQ", AllIntegerVectors>;
defm : VPatBinaryM_VV_VX_VI<"int_riscv_vmsne", "PseudoVMSNE", AllIntegerVectors>;
defm : VPatBinaryM_VV_VX<"int_riscv_vmsltu", "PseudoVMSLTU", AllIntegerVectors>;
defm : VPatBinaryM_VV_VX<"int_riscv_vmslt", "PseudoVMSLT", AllIntegerVectors>;
defm : VPatBinaryM_VV_VX_VI<"int_riscv_vmsleu", "PseudoVMSLEU", AllIntegerVectors>;
defm : VPatBinaryM_VV_VX_VI<"int_riscv_vmsle", "PseudoVMSLE", AllIntegerVectors>;

defm : VPatBinaryM_VX_VI<"int_riscv_vmsgtu", "PseudoVMSGTU", AllIntegerVectors>;
defm : VPatBinaryM_VX_VI<"int_riscv_vmsgt", "PseudoVMSGT", AllIntegerVectors>;

// Match vmsgt with 2 vector operands to vmslt with the operands swapped.
defm : VPatBinarySwappedM_VV<"int_riscv_vmsgtu", "PseudoVMSLTU", AllIntegerVectors>;
defm : VPatBinarySwappedM_VV<"int_riscv_vmsgt", "PseudoVMSLT", AllIntegerVectors>;

defm : VPatBinarySwappedM_VV<"int_riscv_vmsgeu", "PseudoVMSLEU", AllIntegerVectors>;
defm : VPatBinarySwappedM_VV<"int_riscv_vmsge", "PseudoVMSLE", AllIntegerVectors>;

// Match vmslt(u).vx intrinsics to vmsle(u).vi if the scalar is -15 to 16 and
// non-zero. Zero can be .vx with x0. This avoids the user needing to know that
// there is no vmslt(u).vi instruction. Similar for vmsge(u).vx intrinsics
// using vmslt(u).vi.
defm : VPatCompare_VI<"int_riscv_vmslt", "PseudoVMSLE", simm5_plus1_nonzero>;
defm : VPatCompare_VI<"int_riscv_vmsltu", "PseudoVMSLEU", simm5_plus1_nonzero>;

// We need to handle 0 for vmsge.vi using vmslt.vi because there is no vmsge.vx.
defm : VPatCompare_VI<"int_riscv_vmsge", "PseudoVMSGT", simm5_plus1>;
defm : VPatCompare_VI<"int_riscv_vmsgeu", "PseudoVMSGTU", simm5_plus1_nonzero>;

//===----------------------------------------------------------------------===//
// 11.9. Vector Integer Min/Max Instructions
//===----------------------------------------------------------------------===//
defm : VPatBinaryV_VV_VX<"int_riscv_vminu", "PseudoVMINU", AllIntegerVectors>;
defm : VPatBinaryV_VV_VX<"int_riscv_vmin", "PseudoVMIN", AllIntegerVectors>;
defm : VPatBinaryV_VV_VX<"int_riscv_vmaxu", "PseudoVMAXU", AllIntegerVectors>;
defm : VPatBinaryV_VV_VX<"int_riscv_vmax", "PseudoVMAX", AllIntegerVectors>;

//===----------------------------------------------------------------------===//
// 11.10. Vector Single-Width Integer Multiply Instructions
//===----------------------------------------------------------------------===//
defm : VPatBinaryV_VV_VX<"int_riscv_vmul", "PseudoVMUL", AllIntegerVectors>;

defvar IntegerVectorsExceptI64 = !filter(vti, AllIntegerVectors,
                                         !ne(vti.SEW, 64));
defm : VPatBinaryV_VV_VX<"int_riscv_vmulh", "PseudoVMULH",
                         IntegerVectorsExceptI64>;
defm : VPatBinaryV_VV_VX<"int_riscv_vmulhu", "PseudoVMULHU",
                         IntegerVectorsExceptI64>;
defm : VPatBinaryV_VV_VX<"int_riscv_vmulhsu", "PseudoVMULHSU",
                         IntegerVectorsExceptI64>;

// vmulh, vmulhu, vmulhsu are not included for EEW=64 in Zve64*.
defvar I64IntegerVectors = !filter(vti, AllIntegerVectors, !eq(vti.SEW, 64));
let Predicates = [HasVInstructionsFullMultiply] in {
  defm : VPatBinaryV_VV_VX<"int_riscv_vmulh", "PseudoVMULH",
                           I64IntegerVectors>;
  defm : VPatBinaryV_VV_VX<"int_riscv_vmulhu", "PseudoVMULHU",
                           I64IntegerVectors>;
  defm : VPatBinaryV_VV_VX<"int_riscv_vmulhsu", "PseudoVMULHSU",
                           I64IntegerVectors>;
}

//===----------------------------------------------------------------------===//
// 11.11. Vector Integer Divide Instructions
//===----------------------------------------------------------------------===//
defm : VPatBinaryV_VV_VX<"int_riscv_vdivu", "PseudoVDIVU", AllIntegerVectors, isSEWAware=1>;
defm : VPatBinaryV_VV_VX<"int_riscv_vdiv", "PseudoVDIV", AllIntegerVectors, isSEWAware=1>;
defm : VPatBinaryV_VV_VX<"int_riscv_vremu", "PseudoVREMU", AllIntegerVectors, isSEWAware=1>;
defm : VPatBinaryV_VV_VX<"int_riscv_vrem", "PseudoVREM", AllIntegerVectors, isSEWAware=1>;

//===----------------------------------------------------------------------===//
// 11.12. Vector Widening Integer Multiply Instructions
//===----------------------------------------------------------------------===//
defm : VPatBinaryW_VV_VX<"int_riscv_vwmul", "PseudoVWMUL", AllWidenableIntVectors>;
defm : VPatBinaryW_VV_VX<"int_riscv_vwmulu", "PseudoVWMULU", AllWidenableIntVectors>;
defm : VPatBinaryW_VV_VX<"int_riscv_vwmulsu", "PseudoVWMULSU", AllWidenableIntVectors>;

//===----------------------------------------------------------------------===//
// 11.13. Vector Single-Width Integer Multiply-Add Instructions
//===----------------------------------------------------------------------===//
defm : VPatTernaryV_VV_VX_AAXA<"int_riscv_vmadd", "PseudoVMADD", AllIntegerVectors>;
defm : VPatTernaryV_VV_VX_AAXA<"int_riscv_vnmsub", "PseudoVNMSUB", AllIntegerVectors>;
defm : VPatTernaryV_VV_VX_AAXA<"int_riscv_vmacc", "PseudoVMACC", AllIntegerVectors>;
defm : VPatTernaryV_VV_VX_AAXA<"int_riscv_vnmsac", "PseudoVNMSAC", AllIntegerVectors>;

//===----------------------------------------------------------------------===//
// 11.14. Vector Widening Integer Multiply-Add Instructions
//===----------------------------------------------------------------------===//
defm : VPatTernaryW_VV_VX<"int_riscv_vwmaccu", "PseudoVWMACCU", AllWidenableIntVectors>;
defm : VPatTernaryW_VV_VX<"int_riscv_vwmacc", "PseudoVWMACC", AllWidenableIntVectors>;
defm : VPatTernaryW_VV_VX<"int_riscv_vwmaccsu", "PseudoVWMACCSU", AllWidenableIntVectors>;
defm : VPatTernaryW_VX<"int_riscv_vwmaccus", "PseudoVWMACCUS", AllWidenableIntVectors>;

//===----------------------------------------------------------------------===//
// 11.15. Vector Integer Merge Instructions
//===----------------------------------------------------------------------===//
defm : VPatBinaryV_VM_XM_IM<"int_riscv_vmerge", "PseudoVMERGE">;

//===----------------------------------------------------------------------===//
// 11.16. Vector Integer Move Instructions
//===----------------------------------------------------------------------===//
foreach vti = AllVectors in {
  let Predicates = GetVTypePredicates<vti>.Predicates in {
    def : Pat<(vti.Vector (int_riscv_vmv_v_v (vti.Vector vti.RegClass:$passthru),
                                             (vti.Vector vti.RegClass:$rs1),
                                             VLOpFrag)),
              (!cast<Instruction>("PseudoVMV_V_V_"#vti.LMul.MX)
               $passthru, $rs1, GPR:$vl, vti.Log2SEW, TU_MU)>;

    // vmv.v.x/vmv.v.i are handled in RISCInstrVInstrInfoVVLPatterns.td
  }
}

//===----------------------------------------------------------------------===//
// 12. Vector Fixed-Point Arithmetic Instructions
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// 12.1. Vector Single-Width Saturating Add and Subtract
//===----------------------------------------------------------------------===//
defm : VPatBinaryV_VV_VX_VI<"int_riscv_vsaddu", "PseudoVSADDU", AllIntegerVectors>;
defm : VPatBinaryV_VV_VX_VI<"int_riscv_vsadd", "PseudoVSADD", AllIntegerVectors>;
defm : VPatBinaryV_VV_VX<"int_riscv_vssubu", "PseudoVSSUBU", AllIntegerVectors>;
defm : VPatBinaryV_VV_VX<"int_riscv_vssub", "PseudoVSSUB", AllIntegerVectors>;

//===----------------------------------------------------------------------===//
// 12.2. Vector Single-Width Averaging Add and Subtract
//===----------------------------------------------------------------------===//
defm : VPatBinaryV_VV_VX_RM<"int_riscv_vaaddu", "PseudoVAADDU",
                            AllIntegerVectors>;
defm : VPatBinaryV_VV_VX_RM<"int_riscv_vasubu", "PseudoVASUBU",
                            AllIntegerVectors>;
defm : VPatBinaryV_VV_VX_RM<"int_riscv_vasub", "PseudoVASUB",
                            AllIntegerVectors>;
defm : VPatBinaryV_VV_VX_RM<"int_riscv_vaadd", "PseudoVAADD",
                            AllIntegerVectors>;

//===----------------------------------------------------------------------===//
// 12.3. Vector Single-Width Fractional Multiply with Rounding and Saturation
//===----------------------------------------------------------------------===//
defm : VPatBinaryV_VV_VX_RM<"int_riscv_vsmul", "PseudoVSMUL",
                             IntegerVectorsExceptI64>;
// vsmul.vv and vsmul.vx are not included in EEW=64 in Zve64*.
let Predicates = [HasVInstructionsFullMultiply] in
defm : VPatBinaryV_VV_VX_RM<"int_riscv_vsmul", "PseudoVSMUL",
                             I64IntegerVectors>;

//===----------------------------------------------------------------------===//
// 12.4. Vector Single-Width Scaling Shift Instructions
//===----------------------------------------------------------------------===//
defm : VPatBinaryV_VV_VX_VI_RM<"int_riscv_vssrl", "PseudoVSSRL",
                               AllIntegerVectors, uimm5>;
defm : VPatBinaryV_VV_VX_VI_RM<"int_riscv_vssra", "PseudoVSSRA",
                               AllIntegerVectors, uimm5>;

//===----------------------------------------------------------------------===//
// 12.5. Vector Narrowing Fixed-Point Clip Instructions
//===----------------------------------------------------------------------===//
defm : VPatBinaryV_WV_WX_WI_RM<"int_riscv_vnclipu", "PseudoVNCLIPU",
                               AllWidenableIntVectors>;
defm : VPatBinaryV_WV_WX_WI_RM<"int_riscv_vnclip", "PseudoVNCLIP",
                               AllWidenableIntVectors>;

//===----------------------------------------------------------------------===//
// 13. Vector Floating-Point Instructions
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// 13.2. Vector Single-Width Floating-Point Add/Subtract Instructions
//===----------------------------------------------------------------------===//
defm : VPatBinaryV_VV_VX_RM<"int_riscv_vfadd", "PseudoVFADD", AllFloatVectors,
                            isSEWAware = 1>;
defm : VPatBinaryV_VV_VX_RM<"int_riscv_vfsub", "PseudoVFSUB", AllFloatVectors,
                            isSEWAware = 1>;
defm : VPatBinaryV_VX_RM<"int_riscv_vfrsub", "PseudoVFRSUB", AllFloatVectors,
                         isSEWAware = 1>;

//===----------------------------------------------------------------------===//
// 13.3. Vector Widening Floating-Point Add/Subtract Instructions
//===----------------------------------------------------------------------===//
defm : VPatBinaryW_VV_VX_RM<"int_riscv_vfwadd", "PseudoVFWADD",
                            AllWidenableFloatVectors, isSEWAware=1>;
defm : VPatBinaryW_VV_VX_RM<"int_riscv_vfwsub", "PseudoVFWSUB",
                            AllWidenableFloatVectors, isSEWAware=1>;
defm : VPatBinaryW_WV_WX_RM<"int_riscv_vfwadd_w", "PseudoVFWADD",
                            AllWidenableFloatVectors, isSEWAware=1>;
defm : VPatBinaryW_WV_WX_RM<"int_riscv_vfwsub_w", "PseudoVFWSUB",
                            AllWidenableFloatVectors, isSEWAware=1>;

//===----------------------------------------------------------------------===//
// 13.4. Vector Single-Width Floating-Point Multiply/Divide Instructions
//===----------------------------------------------------------------------===//
defm : VPatBinaryV_VV_VX_RM<"int_riscv_vfmul", "PseudoVFMUL",
                            AllFloatVectors, isSEWAware=1>;
defm : VPatBinaryV_VV_VX_RM<"int_riscv_vfdiv", "PseudoVFDIV",
                            AllFloatVectors, isSEWAware=1>;
defm : VPatBinaryV_VX_RM<"int_riscv_vfrdiv", "PseudoVFRDIV",
                         AllFloatVectors, isSEWAware=1>;

//===----------------------------------------------------------------------===//
// 13.5. Vector Widening Floating-Point Multiply
//===----------------------------------------------------------------------===//
defm : VPatBinaryW_VV_VX_RM<"int_riscv_vfwmul", "PseudoVFWMUL",
                            AllWidenableFloatVectors, isSEWAware=1>;

//===----------------------------------------------------------------------===//
// 13.6. Vector Single-Width Floating-Point Fused Multiply-Add Instructions
//===----------------------------------------------------------------------===//
defm : VPatTernaryV_VV_VX_AAXA_RM<"int_riscv_vfmacc", "PseudoVFMACC",
                                  AllFloatVectors, isSEWAware=1>;
defm : VPatTernaryV_VV_VX_AAXA_RM<"int_riscv_vfnmacc", "PseudoVFNMACC",
                                  AllFloatVectors, isSEWAware=1>;
defm : VPatTernaryV_VV_VX_AAXA_RM<"int_riscv_vfmsac", "PseudoVFMSAC",
                                  AllFloatVectors, isSEWAware=1>;
defm : VPatTernaryV_VV_VX_AAXA_RM<"int_riscv_vfnmsac", "PseudoVFNMSAC",
                                  AllFloatVectors, isSEWAware=1>;
defm : VPatTernaryV_VV_VX_AAXA_RM<"int_riscv_vfmadd", "PseudoVFMADD",
                                  AllFloatVectors, isSEWAware=1>;
defm : VPatTernaryV_VV_VX_AAXA_RM<"int_riscv_vfnmadd", "PseudoVFNMADD",
                                  AllFloatVectors, isSEWAware=1>;
defm : VPatTernaryV_VV_VX_AAXA_RM<"int_riscv_vfmsub", "PseudoVFMSUB",
                                  AllFloatVectors, isSEWAware=1>;
defm : VPatTernaryV_VV_VX_AAXA_RM<"int_riscv_vfnmsub", "PseudoVFNMSUB",
                                  AllFloatVectors, isSEWAware=1>;

//===----------------------------------------------------------------------===//
// 13.7. Vector Widening Floating-Point Fused Multiply-Add Instructions
//===----------------------------------------------------------------------===//
defm : VPatTernaryW_VV_VX_RM<"int_riscv_vfwmacc", "PseudoVFWMACC",
                             AllWidenableFloatVectors, isSEWAware=1>;
defm : VPatTernaryW_VV_VX_RM<"int_riscv_vfwnmacc", "PseudoVFWNMACC",
                             AllWidenableFloatVectors, isSEWAware=1>;
defm : VPatTernaryW_VV_VX_RM<"int_riscv_vfwmsac", "PseudoVFWMSAC",
                             AllWidenableFloatVectors, isSEWAware=1>;
defm : VPatTernaryW_VV_VX_RM<"int_riscv_vfwnmsac", "PseudoVFWNMSAC",
                             AllWidenableFloatVectors, isSEWAware=1>;
let Predicates = [HasStdExtZvfbfwma] in
defm : VPatTernaryW_VV_VX_RM<"int_riscv_vfwmaccbf16", "PseudoVFWMACCBF16",
                              AllWidenableBFloatToFloatVectors, isSEWAware=1>;

//===----------------------------------------------------------------------===//
// 13.8. Vector Floating-Point Square-Root Instruction
//===----------------------------------------------------------------------===//
defm : VPatUnaryV_V_RM<"int_riscv_vfsqrt", "PseudoVFSQRT", AllFloatVectors, isSEWAware=1>;

//===----------------------------------------------------------------------===//
// 13.9. Vector Floating-Point Reciprocal Square-Root Estimate Instruction
//===----------------------------------------------------------------------===//
defm : VPatUnaryV_V<"int_riscv_vfrsqrt7", "PseudoVFRSQRT7", AllFloatVectors, isSEWAware=1>;

//===----------------------------------------------------------------------===//
// 13.10. Vector Floating-Point Reciprocal Estimate Instruction
//===----------------------------------------------------------------------===//
defm : VPatUnaryV_V_RM<"int_riscv_vfrec7", "PseudoVFREC7", AllFloatVectors, isSEWAware=1>;

//===----------------------------------------------------------------------===//
// 13.11. Vector Floating-Point Min/Max Instructions
//===----------------------------------------------------------------------===//
defm : VPatBinaryV_VV_VX<"int_riscv_vfmin", "PseudoVFMIN", AllFloatVectors,
                         isSEWAware=1>;
defm : VPatBinaryV_VV_VX<"int_riscv_vfmax", "PseudoVFMAX", AllFloatVectors,
                         isSEWAware=1>;

//===----------------------------------------------------------------------===//
// 13.12. Vector Floating-Point Sign-Injection Instructions
//===----------------------------------------------------------------------===//
defm : VPatBinaryV_VV_VX<"int_riscv_vfsgnj", "PseudoVFSGNJ", AllFloatVectors,
                         isSEWAware=1>;
defm : VPatBinaryV_VV_VX<"int_riscv_vfsgnjn", "PseudoVFSGNJN", AllFloatVectors,
                         isSEWAware=1>;
defm : VPatBinaryV_VV_VX<"int_riscv_vfsgnjx", "PseudoVFSGNJX", AllFloatVectors,
                         isSEWAware=1>;

//===----------------------------------------------------------------------===//
// 13.13. Vector Floating-Point Compare Instructions
//===----------------------------------------------------------------------===//
defm : VPatBinaryM_VV_VX<"int_riscv_vmfeq", "PseudoVMFEQ", AllFloatVectors>;
defm : VPatBinaryM_VV_VX<"int_riscv_vmfle", "PseudoVMFLE", AllFloatVectors>;
defm : VPatBinaryM_VV_VX<"int_riscv_vmflt", "PseudoVMFLT", AllFloatVectors>;
defm : VPatBinaryM_VV_VX<"int_riscv_vmfne", "PseudoVMFNE", AllFloatVectors>;
defm : VPatBinaryM_VX<"int_riscv_vmfgt", "PseudoVMFGT", AllFloatVectors>;
defm : VPatBinaryM_VX<"int_riscv_vmfge", "PseudoVMFGE", AllFloatVectors>;
defm : VPatBinarySwappedM_VV<"int_riscv_vmfgt", "PseudoVMFLT", AllFloatVectors>;
defm : VPatBinarySwappedM_VV<"int_riscv_vmfge", "PseudoVMFLE", AllFloatVectors>;

//===----------------------------------------------------------------------===//
// 13.14. Vector Floating-Point Classify Instruction
//===----------------------------------------------------------------------===//
defm : VPatConversionVI_VF<"int_riscv_vfclass", "PseudoVFCLASS">;

//===----------------------------------------------------------------------===//
// 13.15. Vector Floating-Point Merge Instruction
//===----------------------------------------------------------------------===//
// We can use vmerge.vvm to support vector-vector vfmerge.
// NOTE: Clang previously used int_riscv_vfmerge for vector-vector, but now uses
// int_riscv_vmerge. Support both for compatibility.
foreach vti = AllFloatVectors in {
  let Predicates = GetVTypePredicates<vti>.Predicates in {
    defm : VPatBinaryCarryInTAIL<"int_riscv_vmerge", "PseudoVMERGE", "VVM",
                                 vti.Vector,
                                 vti.Vector, vti.Vector, vti.Mask,
                                 vti.Log2SEW, vti.LMul, vti.RegClass,
                                 vti.RegClass, vti.RegClass>;
    defm : VPatBinaryCarryInTAIL<"int_riscv_vfmerge", "PseudoVMERGE", "VVM",
                                 vti.Vector,
                                 vti.Vector, vti.Vector, vti.Mask,
                                 vti.Log2SEW, vti.LMul, vti.RegClass,
                                 vti.RegClass, vti.RegClass>;
    defm : VPatBinaryCarryInTAIL<"int_riscv_vfmerge", "PseudoVFMERGE",
                                 "V"#vti.ScalarSuffix#"M",
                                 vti.Vector,
                                 vti.Vector, vti.Scalar, vti.Mask,
                                 vti.Log2SEW, vti.LMul, vti.RegClass,
                                 vti.RegClass, vti.ScalarRegClass>;
  }
}

foreach fvti = AllFloatVectors in {
  defvar instr = !cast<Instruction>("PseudoVMERGE_VIM_"#fvti.LMul.MX);
  let Predicates = GetVTypePredicates<fvti>.Predicates in
  def : Pat<(fvti.Vector (int_riscv_vfmerge (fvti.Vector fvti.RegClass:$merge),
                                            (fvti.Vector fvti.RegClass:$rs2),
                                            (fvti.Scalar (fpimm0)),
                                            (fvti.Mask V0), VLOpFrag)),
            (instr fvti.RegClass:$merge, fvti.RegClass:$rs2, 0,
                   (fvti.Mask V0), GPR:$vl, fvti.Log2SEW)>;
}

//===----------------------------------------------------------------------===//
// 13.17. Single-Width Floating-Point/Integer Type-Convert Instructions
//===----------------------------------------------------------------------===//
defm : VPatConversionVI_VF_RTZ<"int_riscv_vfcvt_x_f_v", "PseudoVFCVT_RTZ_X_F">;
defm : VPatConversionVI_VF_RTZ<"int_riscv_vfcvt_xu_f_v", "PseudoVFCVT_RTZ_XU_F">;
defm : VPatConversionVI_VF_RM<"int_riscv_vfcvt_x_f_v", "PseudoVFCVT_X_F">;
defm : VPatConversionVI_VF_RM<"int_riscv_vfcvt_xu_f_v", "PseudoVFCVT_XU_F">;
defm : VPatConversionVI_VF<"int_riscv_vfcvt_rtz_xu_f_v", "PseudoVFCVT_RTZ_XU_F">;
defm : VPatConversionVI_VF<"int_riscv_vfcvt_rtz_x_f_v", "PseudoVFCVT_RTZ_X_F">;
defm : VPatConversionVF_VI_RM<"int_riscv_vfcvt_f_x_v", "PseudoVFCVT_F_X",
                              isSEWAware=1>;
defm : VPatConversionVF_VI_RM<"int_riscv_vfcvt_f_xu_v", "PseudoVFCVT_F_XU",
                              isSEWAware=1>;

//===----------------------------------------------------------------------===//
// 13.18. Widening Floating-Point/Integer Type-Convert Instructions
//===----------------------------------------------------------------------===//
defm : VPatConversionWI_VF_RTZ<"int_riscv_vfwcvt_xu_f_v", "PseudoVFWCVT_RTZ_XU_F">;
defm : VPatConversionWI_VF_RTZ<"int_riscv_vfwcvt_x_f_v", "PseudoVFWCVT_RTZ_X_F">;
defm : VPatConversionWI_VF_RM<"int_riscv_vfwcvt_xu_f_v", "PseudoVFWCVT_XU_F">;
defm : VPatConversionWI_VF_RM<"int_riscv_vfwcvt_x_f_v", "PseudoVFWCVT_X_F">;
defm : VPatConversionWI_VF<"int_riscv_vfwcvt_rtz_xu_f_v", "PseudoVFWCVT_RTZ_XU_F">;
defm : VPatConversionWI_VF<"int_riscv_vfwcvt_rtz_x_f_v", "PseudoVFWCVT_RTZ_X_F">;
defm : VPatConversionWF_VI<"int_riscv_vfwcvt_f_xu_v", "PseudoVFWCVT_F_XU",
                           isSEWAware=1>;
defm : VPatConversionWF_VI<"int_riscv_vfwcvt_f_x_v", "PseudoVFWCVT_F_X",
                           isSEWAware=1>;
defm : VPatConversionWF_VF<"int_riscv_vfwcvt_f_f_v", "PseudoVFWCVT_F_F",
                           isSEWAware=1>;
defm : VPatConversionWF_VF_BF<"int_riscv_vfwcvtbf16_f_f_v",
                              "PseudoVFWCVTBF16_F_F", isSEWAware=1>;

//===----------------------------------------------------------------------===//
// 13.19. Narrowing Floating-Point/Integer Type-Convert Instructions
//===----------------------------------------------------------------------===//
defm : VPatConversionVI_WF_RTZ<"int_riscv_vfncvt_xu_f_w", "PseudoVFNCVT_RTZ_XU_F">;
defm : VPatConversionVI_WF_RTZ<"int_riscv_vfncvt_x_f_w", "PseudoVFNCVT_RTZ_X_F">;
defm : VPatConversionVI_WF_RM<"int_riscv_vfncvt_xu_f_w", "PseudoVFNCVT_XU_F">;
defm : VPatConversionVI_WF_RM<"int_riscv_vfncvt_x_f_w", "PseudoVFNCVT_X_F">;
defm : VPatConversionVI_WF<"int_riscv_vfncvt_rtz_xu_f_w", "PseudoVFNCVT_RTZ_XU_F">;
defm : VPatConversionVI_WF<"int_riscv_vfncvt_rtz_x_f_w", "PseudoVFNCVT_RTZ_X_F">;
defm : VPatConversionVF_WI_RM<"int_riscv_vfncvt_f_xu_w", "PseudoVFNCVT_F_XU",
                              isSEWAware=1>;
defm : VPatConversionVF_WI_RM<"int_riscv_vfncvt_f_x_w", "PseudoVFNCVT_F_X",
                              isSEWAware=1>;
defvar WidenableFloatVectorsExceptF16 = !filter(fvtiToFWti, AllWidenableFloatVectors,
                                                !ne(fvtiToFWti.Vti.Scalar, f16));
defm : VPatConversionVF_WF_RM<"int_riscv_vfncvt_f_f_w", "PseudoVFNCVT_F_F",
                           WidenableFloatVectorsExceptF16, isSEWAware=1>;
// Define vfncvt.f.f.w for f16 when Zvfhmin is enable.
defvar F16WidenableFloatVectors = !filter(fvtiToFWti, AllWidenableFloatVectors,
                                          !eq(fvtiToFWti.Vti.Scalar, f16));
let Predicates = [HasVInstructionsF16Minimal] in
defm : VPatConversionVF_WF_RM<"int_riscv_vfncvt_f_f_w", "PseudoVFNCVT_F_F",
                           F16WidenableFloatVectors, isSEWAware=1>;
defm : VPatConversionVF_WF_BF_RM<"int_riscv_vfncvtbf16_f_f_w", 
                                 "PseudoVFNCVTBF16_F_F", isSEWAware=1>;
defm : VPatConversionVF_WF<"int_riscv_vfncvt_rod_f_f_w", "PseudoVFNCVT_ROD_F_F",
                           isSEWAware=1>;

//===----------------------------------------------------------------------===//
// 14. Vector Reduction Operations
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// 14.1. Vector Single-Width Integer Reduction Instructions
//===----------------------------------------------------------------------===//
defm : VPatReductionV_VS<"int_riscv_vredsum", "PseudoVREDSUM">;
defm : VPatReductionV_VS<"int_riscv_vredand", "PseudoVREDAND">;
defm : VPatReductionV_VS<"int_riscv_vredor", "PseudoVREDOR">;
defm : VPatReductionV_VS<"int_riscv_vredxor", "PseudoVREDXOR">;
defm : VPatReductionV_VS<"int_riscv_vredminu", "PseudoVREDMINU">;
defm : VPatReductionV_VS<"int_riscv_vredmin", "PseudoVREDMIN">;
defm : VPatReductionV_VS<"int_riscv_vredmaxu", "PseudoVREDMAXU">;
defm : VPatReductionV_VS<"int_riscv_vredmax", "PseudoVREDMAX">;

//===----------------------------------------------------------------------===//
// 14.2. Vector Widening Integer Reduction Instructions
//===----------------------------------------------------------------------===//
defm : VPatReductionW_VS<"int_riscv_vwredsumu", "PseudoVWREDSUMU">;
defm : VPatReductionW_VS<"int_riscv_vwredsum", "PseudoVWREDSUM">;

//===----------------------------------------------------------------------===//
// 14.3. Vector Single-Width Floating-Point Reduction Instructions
//===----------------------------------------------------------------------===//
defm : VPatReductionV_VS_RM<"int_riscv_vfredosum", "PseudoVFREDOSUM", IsFloat=1>;
defm : VPatReductionV_VS_RM<"int_riscv_vfredusum", "PseudoVFREDUSUM", IsFloat=1>;
defm : VPatReductionV_VS<"int_riscv_vfredmin", "PseudoVFREDMIN", IsFloat=1>;
defm : VPatReductionV_VS<"int_riscv_vfredmax", "PseudoVFREDMAX", IsFloat=1>;

//===----------------------------------------------------------------------===//
// 14.4. Vector Widening Floating-Point Reduction Instructions
//===----------------------------------------------------------------------===//
defm : VPatReductionW_VS_RM<"int_riscv_vfwredusum", "PseudoVFWREDUSUM", IsFloat=1>;
defm : VPatReductionW_VS_RM<"int_riscv_vfwredosum", "PseudoVFWREDOSUM", IsFloat=1>;

//===----------------------------------------------------------------------===//
// 15. Vector Mask Instructions
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// 15.1 Vector Mask-Register Logical Instructions
//===----------------------------------------------------------------------===//
defm : VPatBinaryM_MM<"int_riscv_vmand", "PseudoVMAND">;
defm : VPatBinaryM_MM<"int_riscv_vmnand", "PseudoVMNAND">;
defm : VPatBinaryM_MM<"int_riscv_vmandn", "PseudoVMANDN">;
defm : VPatBinaryM_MM<"int_riscv_vmxor", "PseudoVMXOR">;
defm : VPatBinaryM_MM<"int_riscv_vmor", "PseudoVMOR">;
defm : VPatBinaryM_MM<"int_riscv_vmnor", "PseudoVMNOR">;
defm : VPatBinaryM_MM<"int_riscv_vmorn", "PseudoVMORN">;
defm : VPatBinaryM_MM<"int_riscv_vmxnor", "PseudoVMXNOR">;

// pseudo instructions
defm : VPatNullaryM<"int_riscv_vmclr", "PseudoVMCLR">;
defm : VPatNullaryM<"int_riscv_vmset", "PseudoVMSET">;

//===----------------------------------------------------------------------===//
// 15.2. Vector count population in mask vcpop.m
//===----------------------------------------------------------------------===//
defm : VPatUnaryS_M<"int_riscv_vcpop", "PseudoVCPOP">;

//===----------------------------------------------------------------------===//
// 15.3. vfirst find-first-set mask bit
//===----------------------------------------------------------------------===//
defm : VPatUnaryS_M<"int_riscv_vfirst", "PseudoVFIRST">;

//===----------------------------------------------------------------------===//
// 15.4. vmsbf.m set-before-first mask bit
//===----------------------------------------------------------------------===//
defm : VPatUnaryM_M<"int_riscv_vmsbf", "PseudoVMSBF">;

//===----------------------------------------------------------------------===//
// 15.5. vmsif.m set-including-first mask bit
//===----------------------------------------------------------------------===//
defm : VPatUnaryM_M<"int_riscv_vmsif", "PseudoVMSIF">;

//===----------------------------------------------------------------------===//
// 15.6. vmsof.m set-only-first mask bit
//===----------------------------------------------------------------------===//
defm : VPatUnaryM_M<"int_riscv_vmsof", "PseudoVMSOF">;

//===----------------------------------------------------------------------===//
// 15.8.  Vector Iota Instruction
//===----------------------------------------------------------------------===//
defm : VPatUnaryV_M<"int_riscv_viota", "PseudoVIOTA">;

//===----------------------------------------------------------------------===//
// 15.9. Vector Element Index Instruction
//===----------------------------------------------------------------------===//
defm : VPatNullaryV<"int_riscv_vid", "PseudoVID">;


//===----------------------------------------------------------------------===//
// 16. Vector Permutation Instructions
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// 16.1. Integer Scalar Move Instructions
//===----------------------------------------------------------------------===//

foreach vti = NoGroupIntegerVectors in {
  let Predicates = GetVTypePredicates<vti>.Predicates in
  def : Pat<(XLenVT (riscv_vmv_x_s (vti.Vector vti.RegClass:$rs2))),
            (PseudoVMV_X_S $rs2, vti.Log2SEW)>;
  // vmv.s.x is handled with a custom node in RISCVInstrInfoVVLPatterns.td
}

//===----------------------------------------------------------------------===//
// 16.3. Vector Slide Instructions
//===----------------------------------------------------------------------===//
defm : VPatTernaryV_VX_VI<"int_riscv_vslideup", "PseudoVSLIDEUP", AllIntegerVectors, uimm5>;
defm : VPatTernaryV_VX_VI<"int_riscv_vslidedown", "PseudoVSLIDEDOWN", AllIntegerVectors, uimm5>;
defm : VPatBinaryV_VX<"int_riscv_vslide1up", "PseudoVSLIDE1UP", AllIntegerVectors>;
defm : VPatBinaryV_VX<"int_riscv_vslide1down", "PseudoVSLIDE1DOWN", AllIntegerVectors>;

defm : VPatTernaryV_VX_VI<"int_riscv_vslideup", "PseudoVSLIDEUP", AllFloatVectors, uimm5>;
defm : VPatTernaryV_VX_VI<"int_riscv_vslidedown", "PseudoVSLIDEDOWN", AllFloatVectors, uimm5>;
defm : VPatBinaryV_VX<"int_riscv_vfslide1up", "PseudoVFSLIDE1UP", AllFloatVectors>;
defm : VPatBinaryV_VX<"int_riscv_vfslide1down", "PseudoVFSLIDE1DOWN", AllFloatVectors>;

//===----------------------------------------------------------------------===//
// 16.4. Vector Register Gather Instructions
//===----------------------------------------------------------------------===//
defm : VPatBinaryV_VV_VX_VI_INT<"int_riscv_vrgather", "PseudoVRGATHER",
                                AllIntegerVectors, uimm5>;
defm : VPatBinaryV_VV_INT_EEW<"int_riscv_vrgatherei16_vv", "PseudoVRGATHEREI16",
                              eew=16, vtilist=AllIntegerVectors>;

defm : VPatBinaryV_VV_VX_VI_INT<"int_riscv_vrgather", "PseudoVRGATHER",
                                AllFloatVectors, uimm5>;
defm : VPatBinaryV_VV_INT_EEW<"int_riscv_vrgatherei16_vv", "PseudoVRGATHEREI16",
                              eew=16, vtilist=AllFloatVectors>;
//===----------------------------------------------------------------------===//
// 16.5. Vector Compress Instruction
//===----------------------------------------------------------------------===//
defm : VPatUnaryV_V_AnyMask<"int_riscv_vcompress", "PseudoVCOMPRESS", AllIntegerVectors>;
defm : VPatUnaryV_V_AnyMask<"int_riscv_vcompress", "PseudoVCOMPRESS", AllFloatVectors>;

// Include the non-intrinsic ISel patterns
include "RISCVInstrInfoVVLPatterns.td"
include "RISCVInstrInfoVSDPatterns.td"
