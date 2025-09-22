//===-- X86InstrUtils.td - X86 Instruction Utilities --------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides utilities for simplifying the instruction definitions.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Classes for setting the fields of X86Inst
//===----------------------------------------------------------------------===//

// Prefix byte classes which are used to indicate to the ad-hoc machine code
// emitter that various prefix bytes are required.
class OpSize16 { OperandSize OpSize = OpSize16; }
class OpSize32 { OperandSize OpSize = OpSize32; }
class AdSize16 { AddressSize AdSize = AdSize16; }
class AdSize32 { AddressSize AdSize = AdSize32; }
class AdSize64 { AddressSize AdSize = AdSize64; }
class REX_W  { bit hasREX_W = 1; }
class LOCK   { bit hasLockPrefix = 1; }
class REP    { bit hasREPPrefix = 1; }
class TB     { Map OpMap = TB; }
class T8     { Map OpMap = T8; }
class TA     { Map OpMap = TA; }
class T_MAP4 { Map OpMap = T_MAP4; }
class T_MAP5 { Map OpMap = T_MAP5; }
class T_MAP6 { Map OpMap = T_MAP6; }
class T_MAP7 { Map OpMap = T_MAP7; }
class XOP8   { Map OpMap = XOP8; }
class XOP9   { Map OpMap = XOP9; }
class XOPA   { Map OpMap = XOPA; }
class ThreeDNow { Map OpMap = ThreeDNow; }
class PS { Prefix OpPrefix = PS; }
class PD { Prefix OpPrefix = PD; }
class XD { Prefix OpPrefix = XD; }
class XS { Prefix OpPrefix = XS; }
class XOP { Encoding OpEnc = EncXOP; }
class VEX { Encoding OpEnc = EncVEX; }
class EVEX { Encoding OpEnc = EncEVEX; }
class WIG  { bit IgnoresW = 1; }
class VEX_L  { bit hasVEX_L = 1; }
class VEX_LIG { bit ignoresVEX_L = 1; }
class VVVV { bit hasVEX_4V = 1; }
class EVEX_K { bit hasEVEX_K = 1; }
class EVEX_KZ : EVEX_K { bit hasEVEX_Z = 1; }
class EVEX_B { bit hasEVEX_B = 1; }
class EVEX_NF { bit hasEVEX_NF = 1; }
class EVEX_RC { bit hasEVEX_RC = 1; }
class EVEX_V512 { bit hasEVEX_L2 = 1; bit hasVEX_L = 0; }
class EVEX_V256 { bit hasEVEX_L2 = 0; bit hasVEX_L = 1; }
class EVEX_V128 { bit hasEVEX_L2 = 0; bit hasVEX_L = 0; }
class NOTRACK { bit hasNoTrackPrefix = 1; }
class SIMD_EXC { list<Register> Uses = [MXCSR]; bit mayRaiseFPException = 1; }
// Specify AVX512 8-bit compressed displacement encoding based on the vector
// element size in bits (8, 16, 32, 64) and the CDisp8 form.
class EVEX_CD8<int esize, CD8VForm form> {
  int CD8_EltSize = !srl(esize, 3);
  bits<3> CD8_Form = form.Value;
}
class NoCD8 { bits<7> CD8_Scale = 0; }

class AVX512BIi8Base : TB, PD {
  Domain ExeDomain = SSEPackedInt;
  ImmType ImmT = Imm8;
}
class AVX512XSIi8Base : TB, XS {
  Domain ExeDomain = SSEPackedInt;
  ImmType ImmT = Imm8;
}
class AVX512XDIi8Base : TB, XD {
  Domain ExeDomain = SSEPackedInt;
  ImmType ImmT = Imm8;
}
class AVX512PSIi8Base : TB {
  Domain ExeDomain = SSEPackedSingle;
  ImmType ImmT = Imm8;
}
class AVX512PDIi8Base : TB, PD {
  Domain ExeDomain = SSEPackedDouble;
  ImmType ImmT = Imm8;
}
class ExplicitREX2Prefix { ExplicitOpPrefix explicitOpPrefix = ExplicitREX2; }
class ExplicitVEXPrefix { ExplicitOpPrefix explicitOpPrefix = ExplicitVEX; }
class ExplicitEVEXPrefix { ExplicitOpPrefix explicitOpPrefix = ExplicitEVEX; }
class DefEFLAGS { list<Register> Defs = [EFLAGS]; }
class UseEFLAGS { list<Register> Uses = [EFLAGS]; }
class DisassembleOnly {
  // The disassembler should know about this, but not the asmparser.
  bit isCodeGenOnly = 1;
  bit ForceDisassemble = 1;
}

defvar unaryop_args = "$src1";
defvar unaryop_ndd_args = "{$src1, $dst|$dst, $src1}";
defvar binop_args = "{$src2, $src1|$src1, $src2}";
defvar binop_ndd_args = "{$src2, $src1, $dst|$dst, $src1, $src2}";
defvar binop_cl_args = "{%cl, $src1|$src1, cl}";
defvar binop_cl_ndd_args = "{%cl, $src1, $dst|$dst, $src1, cl}";
defvar triop_args = "{$src3, $src2, $src1|$src1, $src2, $src3}";
defvar triop_ndd_args = "{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}";
defvar triop_cl_args = "{%cl, $src2, $src1|$src1, $src2, cl}";
defvar triop_cl_ndd_args = "{%cl, $src2, $src1, $dst|$dst, $src1, $src2, cl}";
defvar tie_dst_src1 = "$src1 = $dst";

// NDD - Helper for new data destination instructions
class NDD<bit ndd> {
  string Constraints = !if(!eq(ndd, 0), tie_dst_src1, "");
  Encoding OpEnc = !if(!eq(ndd, 0), EncNormal, EncEVEX);
  bit hasEVEX_B = ndd;
  bit hasVEX_4V = ndd;
  Map OpMap = !if(!eq(ndd, 0), OB, T_MAP4);
}
// NF - Helper for NF (no flags update) instructions
class NF: T_MAP4, EVEX, EVEX_NF;
// PL - Helper for promoted legacy instructions
class PL: T_MAP4, EVEX, ExplicitEVEXPrefix;
// ZU - Helper for Zero Upper instructions
class ZU: T_MAP4, EVEX, EVEX_B;

//===----------------------------------------------------------------------===//
// X86 Type infomation definitions
//===----------------------------------------------------------------------===//

/// X86TypeInfo - This is a bunch of information that describes relevant X86
/// information about value types.  For example, it can tell you what the
/// register class and preferred load to use.
class X86TypeInfo<ValueType vt, string instrsuffix, RegisterClass regclass,
                  PatFrag loadnode, X86MemOperand memoperand, ImmType immkind,
                  Operand immoperand, SDPatternOperator immoperator,
                  SDPatternOperator immnosuoperator, Operand imm8operand,
                  SDPatternOperator imm8operator, SDPatternOperator imm8nosuoperator,
                  bit hasEvenOpcode, bit hasREX_W> {
  /// VT - This is the value type itself.
  ValueType VT = vt;

  /// InstrSuffix - This is the suffix used on instructions with this type.  For
  /// example, i8 -> "b", i16 -> "w", i32 -> "l", i64 -> "q".
  string InstrSuffix = instrsuffix;

  /// RegClass - This is the register class associated with this type.  For
  /// example, i8 -> GR8, i16 -> GR16, i32 -> GR32, i64 -> GR64.
  RegisterClass RegClass = regclass;

  /// LoadNode - This is the load node associated with this type.  For
  /// example, i8 -> loadi8, i16 -> loadi16, i32 -> loadi32, i64 -> loadi64.
  PatFrag LoadNode = loadnode;

  /// MemOperand - This is the memory operand associated with this type.  For
  /// example, i8 -> i8mem, i16 -> i16mem, i32 -> i32mem, i64 -> i64mem.
  X86MemOperand MemOperand = memoperand;

  /// ImmEncoding - This is the encoding of an immediate of this type.  For
  /// example, i8 -> Imm8, i16 -> Imm16, i32 -> Imm32.  Note that i64 -> Imm32
  /// since the immediate fields of i64 instructions is a 32-bit sign extended
  /// value.
  ImmType ImmEncoding = immkind;

  /// ImmOperand - This is the operand kind of an immediate of this type.  For
  /// example, i8 -> i8imm, i16 -> i16imm, i32 -> i32imm.  Note that i64 ->
  /// i64i32imm since the immediate fields of i64 instructions is a 32-bit sign
  /// extended value.
  Operand ImmOperand = immoperand;

  /// ImmOperator - This is the operator that should be used to match an
  /// immediate of this kind in a pattern (e.g. imm, or i64immSExt32).
  SDPatternOperator ImmOperator = immoperator;

  SDPatternOperator ImmNoSuOperator = immnosuoperator;

  /// Imm8Operand - This is the operand kind to use for an imm8 of this type.
  /// For example, i8 -> <invalid>, i16 -> i16i8imm, i32 -> i32i8imm.  This is
  /// only used for instructions that have a sign-extended imm8 field form.
  Operand Imm8Operand = imm8operand;

  /// Imm8Operator - This is the operator that should be used to match an 8-bit
  /// sign extended immediate of this kind in a pattern (e.g. imm16immSExt8).
  SDPatternOperator Imm8Operator = imm8operator;

  SDPatternOperator Imm8NoSuOperator = imm8nosuoperator;

  /// HasEvenOpcode - This bit is true if the instruction should have an even (as
  /// opposed to odd) opcode.  Operations on i8 are even, operations on
  /// other datatypes are usually odd.
  bit HasEvenOpcode = hasEvenOpcode;

  /// HasREX_W - This bit is set to true if the instruction should have
  /// the 0x40 REX prefix.  This is set for i64 types.
  bit HasREX_W = hasREX_W;
}

def invalid_node : SDNode<"<<invalid_node>>", SDTIntLeaf,[],"<<invalid_node>>">;

def Xi8  : X86TypeInfo<i8, "b", GR8, loadi8, i8mem, Imm8, i8imm,
                       imm_su, imm, i8imm, invalid_node, invalid_node,
                       1, 0>;
def Xi16 : X86TypeInfo<i16, "w", GR16, loadi16, i16mem, Imm16, i16imm,
                       imm_su, imm, i16i8imm, i16immSExt8_su, i16immSExt8,
                       0, 0>;
def Xi32 : X86TypeInfo<i32, "l", GR32, loadi32, i32mem, Imm32, i32imm,
                       imm_su, imm, i32i8imm, i32immSExt8_su, i32immSExt8,
                       0, 0>;
def Xi64 : X86TypeInfo<i64, "q", GR64, loadi64, i64mem, Imm32S, i64i32imm,
                       i64immSExt32_su, i64immSExt32, i64i8imm, i64immSExt8_su,
                       i64immSExt8, 0, 1>;

// Group template arguments that can be derived from the vector type (EltNum x
// EltVT).  These are things like the register class for the writemask, etc.
// The idea is to pass one of these as the template argument rather than the
// individual arguments.
// The template is also used for scalar types, in this case numelts is 1.
class X86VectorVTInfo<int numelts, ValueType eltvt, RegisterClass rc,
                      string suffix = ""> {
  RegisterClass RC = rc;
  ValueType EltVT = eltvt;
  int NumElts = numelts;

  // Corresponding mask register class.
  RegisterClass KRC = !cast<RegisterClass>("VK" # NumElts);

  // Corresponding mask register pair class.
  RegisterOperand KRPC = !if (!gt(NumElts, 16), ?,
                              !cast<RegisterOperand>("VK" # NumElts # "Pair"));

  // Corresponding write-mask register class.
  RegisterClass KRCWM = !cast<RegisterClass>("VK" # NumElts # "WM");

  // The mask VT.
  ValueType KVT = !cast<ValueType>("v" # NumElts # "i1");

  // Suffix used in the instruction mnemonic.
  string Suffix = suffix;

  // VTName is a string name for vector VT. For vector types it will be
  // v # NumElts # EltVT, so for vector of 8 elements of i32 it will be v8i32
  // It is a little bit complex for scalar types, where NumElts = 1.
  // In this case we build v4f32 or v2f64
  string VTName = "v" # !if (!eq (NumElts, 1),
                        !if (!eq (EltVT.Size, 16), 8,
                        !if (!eq (EltVT.Size, 32), 4,
                        !if (!eq (EltVT.Size, 64), 2, NumElts))), NumElts) # EltVT;

  // The vector VT.
  ValueType VT = !cast<ValueType>(VTName);

  string EltTypeName = !cast<string>(EltVT);
  // Size of the element type in bits, e.g. 32 for v16i32.
  string EltSizeName = !subst("i", "", !subst("f", "", !subst("b", "", EltTypeName)));
  int EltSize = EltVT.Size;

  // "i" for integer types and "f" for floating-point types
  string TypeVariantName = !subst("b", "", !subst(EltSizeName, "", EltTypeName));

  // Size of RC in bits, e.g. 512 for VR512.
  int Size = VT.Size;

  // The corresponding memory operand, e.g. i512mem for VR512.
  X86MemOperand MemOp = !cast<X86MemOperand>(TypeVariantName # Size # "mem");
  X86MemOperand ScalarMemOp = !cast<X86MemOperand>(!subst("b", "", EltTypeName) # "mem");
  // FP scalar memory operand for intrinsics - ssmem/sdmem.
  Operand IntScalarMemOp = !if (!eq (EltTypeName, "f16"), !cast<Operand>("shmem"),
                           !if (!eq (EltTypeName, "bf16"), !cast<Operand>("shmem"),
                           !if (!eq (EltTypeName, "f32"), !cast<Operand>("ssmem"),
                           !if (!eq (EltTypeName, "f64"), !cast<Operand>("sdmem"), ?))));

  // Load patterns
  PatFrag LdFrag = !cast<PatFrag>("load" # VTName);

  PatFrag AlignedLdFrag = !cast<PatFrag>("alignedload" # VTName);

  PatFrag ScalarLdFrag = !cast<PatFrag>("load" # !subst("b", "", EltTypeName));
  PatFrag BroadcastLdFrag = !cast<PatFrag>("X86VBroadcastld" # EltSizeName);

  PatFrags ScalarIntMemFrags = !if (!eq (EltTypeName, "f16"), !cast<PatFrags>("sse_load_f16"),
                               !if (!eq (EltTypeName, "bf16"), !cast<PatFrags>("sse_load_f16"),
                               !if (!eq (EltTypeName, "f32"), !cast<PatFrags>("sse_load_f32"),
                               !if (!eq (EltTypeName, "f64"), !cast<PatFrags>("sse_load_f64"), ?))));

  // The string to specify embedded broadcast in assembly.
  string BroadcastStr = "{1to" # NumElts # "}";

  // 8-bit compressed displacement tuple/subvector format.  This is only
  // defined for NumElts <= 8.
  CD8VForm CD8TupleForm = !if (!eq (!srl(NumElts, 4), 0),
                               !cast<CD8VForm>("CD8VT" # NumElts), ?);

  SubRegIndex SubRegIdx = !if (!eq (Size, 128), sub_xmm,
                          !if (!eq (Size, 256), sub_ymm, ?));

  Domain ExeDomain = !if (!eq (EltTypeName, "f32"), SSEPackedSingle,
                     !if (!eq (EltTypeName, "f64"), SSEPackedDouble,
                     !if (!eq (EltTypeName, "f16"), SSEPackedSingle, // FIXME?
                     !if (!eq (EltTypeName, "bf16"), SSEPackedSingle, // FIXME?
                     SSEPackedInt))));

  RegisterClass FRC = !if (!eq (EltTypeName, "f32"), FR32X,
                      !if (!eq (EltTypeName, "f16"), FR16X,
                      !if (!eq (EltTypeName, "bf16"), FR16X,
                      FR64X)));

  dag ImmAllZerosV = (VT immAllZerosV);

  string ZSuffix = !if (!eq (Size, 128), "Z128",
                   !if (!eq (Size, 256), "Z256", "Z"));
}

def v64i8_info  : X86VectorVTInfo<64,  i8, VR512, "b">;
def v32i16_info : X86VectorVTInfo<32, i16, VR512, "w">;
def v16i32_info : X86VectorVTInfo<16, i32, VR512, "d">;
def v8i64_info  : X86VectorVTInfo<8,  i64, VR512, "q">;
def v32f16_info : X86VectorVTInfo<32, f16, VR512, "ph">;
def v32bf16_info: X86VectorVTInfo<32, bf16, VR512, "pbf">;
def v16f32_info : X86VectorVTInfo<16, f32, VR512, "ps">;
def v8f64_info  : X86VectorVTInfo<8,  f64, VR512, "pd">;

// "x" in v32i8x_info means RC = VR256X
def v32i8x_info  : X86VectorVTInfo<32,  i8, VR256X, "b">;
def v16i16x_info : X86VectorVTInfo<16, i16, VR256X, "w">;
def v8i32x_info  : X86VectorVTInfo<8,  i32, VR256X, "d">;
def v4i64x_info  : X86VectorVTInfo<4,  i64, VR256X, "q">;
def v16f16x_info : X86VectorVTInfo<16, f16, VR256X, "ph">;
def v16bf16x_info: X86VectorVTInfo<16, bf16, VR256X, "pbf">;
def v8f32x_info  : X86VectorVTInfo<8,  f32, VR256X, "ps">;
def v4f64x_info  : X86VectorVTInfo<4,  f64, VR256X, "pd">;

def v16i8x_info  : X86VectorVTInfo<16,  i8, VR128X, "b">;
def v8i16x_info  : X86VectorVTInfo<8,  i16, VR128X, "w">;
def v4i32x_info  : X86VectorVTInfo<4,  i32, VR128X, "d">;
def v2i64x_info  : X86VectorVTInfo<2,  i64, VR128X, "q">;
def v8f16x_info  : X86VectorVTInfo<8,  f16, VR128X, "ph">;
def v8bf16x_info : X86VectorVTInfo<8,  bf16, VR128X, "pbf">;
def v4f32x_info  : X86VectorVTInfo<4,  f32, VR128X, "ps">;
def v2f64x_info  : X86VectorVTInfo<2,  f64, VR128X, "pd">;

// We map scalar types to the smallest (128-bit) vector type
// with the appropriate element type. This allows to use the same masking logic.
def i32x_info    : X86VectorVTInfo<1,  i32, GR32, "si">;
def i64x_info    : X86VectorVTInfo<1,  i64, GR64, "sq">;
def f16x_info    : X86VectorVTInfo<1,  f16, VR128X, "sh">;
def bf16x_info   : X86VectorVTInfo<1,  bf16, VR128X, "sbf">;
def f32x_info    : X86VectorVTInfo<1,  f32, VR128X, "ss">;
def f64x_info    : X86VectorVTInfo<1,  f64, VR128X, "sd">;

class AVX512VLVectorVTInfo<X86VectorVTInfo i512, X86VectorVTInfo i256,
                           X86VectorVTInfo i128> {
  X86VectorVTInfo info512 = i512;
  X86VectorVTInfo info256 = i256;
  X86VectorVTInfo info128 = i128;
}

def avx512vl_i8_info  : AVX512VLVectorVTInfo<v64i8_info, v32i8x_info,
                                             v16i8x_info>;
def avx512vl_i16_info : AVX512VLVectorVTInfo<v32i16_info, v16i16x_info,
                                             v8i16x_info>;
def avx512vl_i32_info : AVX512VLVectorVTInfo<v16i32_info, v8i32x_info,
                                             v4i32x_info>;
def avx512vl_i64_info : AVX512VLVectorVTInfo<v8i64_info, v4i64x_info,
                                             v2i64x_info>;
def avx512vl_f16_info : AVX512VLVectorVTInfo<v32f16_info, v16f16x_info,
                                             v8f16x_info>;
def avx512vl_bf16_info : AVX512VLVectorVTInfo<v32bf16_info, v16bf16x_info,
                                             v8bf16x_info>;
def avx512vl_f32_info : AVX512VLVectorVTInfo<v16f32_info, v8f32x_info,
                                             v4f32x_info>;
def avx512vl_f64_info : AVX512VLVectorVTInfo<v8f64_info, v4f64x_info,
                                             v2f64x_info>;

class X86KVectorVTInfo<RegisterClass _krc, RegisterClass _krcwm,
                       ValueType _vt> {
  RegisterClass KRC = _krc;
  RegisterClass KRCWM = _krcwm;
  ValueType KVT = _vt;
}

def v1i1_info : X86KVectorVTInfo<VK1, VK1WM, v1i1>;
def v2i1_info : X86KVectorVTInfo<VK2, VK2WM, v2i1>;
def v4i1_info : X86KVectorVTInfo<VK4, VK4WM, v4i1>;
def v8i1_info : X86KVectorVTInfo<VK8, VK8WM, v8i1>;
def v16i1_info : X86KVectorVTInfo<VK16, VK16WM, v16i1>;
def v32i1_info : X86KVectorVTInfo<VK32, VK32WM, v32i1>;
def v64i1_info : X86KVectorVTInfo<VK64, VK64WM, v64i1>;

// Subclasses of X86Inst
class PseudoI<dag oops, dag iops, list<dag> pattern>
  : X86Inst<0, Pseudo, NoImm, oops, iops, ""> {
  let Pattern = pattern;
}

class I<bits<8> o, Format f, dag outs, dag ins, string asm,
        list<dag> pattern, Domain d = GenericDomain>
  : X86Inst<o, f, NoImm, outs, ins, asm, d> {
  let Pattern = pattern;
}
class Ii8<bits<8> o, Format f, dag outs, dag ins, string asm,
          list<dag> pattern, Domain d = GenericDomain>
  : X86Inst<o, f, Imm8, outs, ins, asm, d> {
  let Pattern = pattern;
}
class Ii8Reg<bits<8> o, Format f, dag outs, dag ins, string asm,
             list<dag> pattern, Domain d = GenericDomain>
  : X86Inst<o, f, Imm8Reg, outs, ins, asm, d> {
  let Pattern = pattern;
}
class Ii8PCRel<bits<8> o, Format f, dag outs, dag ins, string asm,
               list<dag> pattern>
  : X86Inst<o, f, Imm8PCRel, outs, ins, asm> {
  let Pattern = pattern;
}
class Ii16<bits<8> o, Format f, dag outs, dag ins, string asm,
           list<dag> pattern>
  : X86Inst<o, f, Imm16, outs, ins, asm> {
  let Pattern = pattern;
}
class Ii32<bits<8> o, Format f, dag outs, dag ins, string asm,
           list<dag> pattern>
  : X86Inst<o, f, Imm32, outs, ins, asm> {
  let Pattern = pattern;
}
class Ii32S<bits<8> o, Format f, dag outs, dag ins, string asm,
            list<dag> pattern>
  : X86Inst<o, f, Imm32S, outs, ins, asm> {
  let Pattern = pattern;
}

class Ii64<bits<8> o, Format f, dag outs, dag ins, string asm,
           list<dag> pattern>
  : X86Inst<o, f, Imm64, outs, ins, asm> {
  let Pattern = pattern;
}

class Ii16PCRel<bits<8> o, Format f, dag outs, dag ins, string asm,
           list<dag> pattern>
           : X86Inst<o, f, Imm16PCRel, outs, ins, asm> {
  let Pattern = pattern;
}

class Ii32PCRel<bits<8> o, Format f, dag outs, dag ins, string asm,
           list<dag> pattern>
  : X86Inst<o, f, Imm32PCRel, outs, ins, asm> {
  let Pattern = pattern;
}

// FPStack Instruction Templates:
// FPI - Floating Point Instruction template.
class FPI<bits<8> o, Format F, dag outs, dag ins, string asm>
  : I<o, F, outs, ins, asm, []> {
  let Defs = [FPSW];
  let Predicates = [HasX87];
}

// FpI_ - Floating Point Pseudo Instruction template.
class FpI_<dag outs, dag ins, FPFormat fp, list<dag> pattern>
  : PseudoI<outs, ins, pattern> {
  let FPForm = fp;
  let Defs = [FPSW];
  let Predicates = [HasX87];
}

// Templates for instructions that use a 16- or 32-bit segmented address as
//  their only operand: lcall (FAR CALL) and ljmp (FAR JMP)
//
//   Iseg16 - 16-bit segment selector, 16-bit offset
//   Iseg32 - 16-bit segment selector, 32-bit offset

class Iseg16 <bits<8> o, Format f, dag outs, dag ins, string asm,
              list<dag> pattern>
      : X86Inst<o, f, Imm16, outs, ins, asm> {
  let Pattern = pattern;
}

class Iseg32 <bits<8> o, Format f, dag outs, dag ins, string asm,
              list<dag> pattern>
      : X86Inst<o, f, Imm32, outs, ins, asm> {
  let Pattern = pattern;
}

// SI - SSE 1 & 2 scalar instructions
class SI<bits<8> o, Format F, dag outs, dag ins, string asm,
         list<dag> pattern, Domain d = GenericDomain>
      : I<o, F, outs, ins, asm, pattern, d> {
  let Predicates = !if(!eq(OpEnc.Value, EncEVEX.Value), [HasAVX512],
                   !if(!eq(OpEnc.Value, EncVEX.Value), [UseAVX],
                   !if(!eq(OpPrefix.Value, XS.Value), [UseSSE1],
                   !if(!eq(OpPrefix.Value, XD.Value), [UseSSE2],
                   !if(!eq(OpPrefix.Value, PD.Value), [UseSSE2],
                   [UseSSE1])))));

  // AVX instructions have a 'v' prefix in the mnemonic
  let AsmString = !if(!eq(OpEnc.Value, EncEVEX.Value), !strconcat("v", asm),
                  !if(!eq(OpEnc.Value, EncVEX.Value), !strconcat("v", asm),
                  asm));
}

// SI - SSE 1 & 2 scalar intrinsics - vex form available on AVX512
class SI_Int<bits<8> o, Format F, dag outs, dag ins, string asm,
         list<dag> pattern, Domain d = GenericDomain>
      : I<o, F, outs, ins, asm, pattern, d> {
  let Predicates = !if(!eq(OpEnc.Value, EncEVEX.Value), [HasAVX512],
                   !if(!eq(OpEnc.Value, EncVEX.Value), [UseAVX],
                   !if(!eq(OpPrefix.Value, XS.Value), [UseSSE1],
                   !if(!eq(OpPrefix.Value, XD.Value), [UseSSE2],
                   !if(!eq(OpPrefix.Value, PD.Value), [UseSSE2],
                   [UseSSE1])))));

  // AVX instructions have a 'v' prefix in the mnemonic
  let AsmString = !if(!eq(OpEnc.Value, EncEVEX.Value), !strconcat("v", asm),
                  !if(!eq(OpEnc.Value, EncVEX.Value), !strconcat("v", asm),
                  asm));
}
// SIi8 - SSE 1 & 2 scalar instructions - vex form available on AVX512
class SIi8<bits<8> o, Format F, dag outs, dag ins, string asm,
           list<dag> pattern>
      : Ii8<o, F, outs, ins, asm, pattern> {
  let Predicates = !if(!eq(OpEnc.Value, EncEVEX.Value), [HasAVX512],
                   !if(!eq(OpEnc.Value, EncVEX.Value), [HasAVX],
                   !if(!eq(OpPrefix.Value, XS.Value), [UseSSE1],
                   [UseSSE2])));

  // AVX instructions have a 'v' prefix in the mnemonic
  let AsmString = !if(!eq(OpEnc.Value, EncEVEX.Value), !strconcat("v", asm),
                  !if(!eq(OpEnc.Value, EncVEX.Value), !strconcat("v", asm),
                  asm));
}

// PI - SSE 1 & 2 packed instructions
class PI<bits<8> o, Format F, dag outs, dag ins, string asm, list<dag> pattern,
         Domain d>
      : I<o, F, outs, ins, asm, pattern, d> {
  let Predicates = !if(!eq(OpEnc.Value, EncEVEX.Value), [HasAVX512],
                   !if(!eq(OpEnc.Value, EncVEX.Value), [HasAVX],
                   !if(!eq(OpPrefix.Value, PD.Value), [UseSSE2],
                   [UseSSE1])));

  // AVX instructions have a 'v' prefix in the mnemonic
  let AsmString = !if(!eq(OpEnc.Value, EncEVEX.Value), !strconcat("v", asm),
                  !if(!eq(OpEnc.Value, EncVEX.Value), !strconcat("v", asm),
                  asm));
}

// MMXPI - SSE 1 & 2 packed instructions with MMX operands
class MMXPI<bits<8> o, Format F, dag outs, dag ins, string asm, list<dag> pattern,
            Domain d>
      : I<o, F, outs, ins, asm, pattern, d> {
  let Predicates = !if(!eq(OpPrefix.Value, PD.Value), [HasMMX, HasSSE2],
                       [HasMMX, HasSSE1]);
}

// PIi8 - SSE 1 & 2 packed instructions with immediate
class PIi8<bits<8> o, Format F, dag outs, dag ins, string asm,
           list<dag> pattern, Domain d>
      : Ii8<o, F, outs, ins, asm, pattern, d> {
  let Predicates = !if(!eq(OpEnc.Value, EncEVEX.Value), [HasAVX512],
                   !if(!eq(OpEnc.Value, EncVEX.Value), [HasAVX],
                   !if(!eq(OpPrefix.Value, PD.Value), [UseSSE2],
                   [UseSSE1])));

  // AVX instructions have a 'v' prefix in the mnemonic
  let AsmString = !if(!eq(OpEnc.Value, EncEVEX.Value), !strconcat("v", asm),
                  !if(!eq(OpEnc.Value, EncVEX.Value), !strconcat("v", asm),
                  asm));
}

// SSE1 Instruction Templates:
//
//   SSI   - SSE1 instructions with XS prefix.
//   PSI   - SSE1 instructions with PS prefix.
//   PSIi8 - SSE1 instructions with ImmT == Imm8 and PS prefix.
//   VSSI  - SSE1 instructions with XS prefix in AVX form.
//   VPSI  - SSE1 instructions with PS prefix in AVX form, packed single.

class SSI<bits<8> o, Format F, dag outs, dag ins, string asm,
          list<dag> pattern>
      : I<o, F, outs, ins, asm, pattern>, TB, XS, Requires<[UseSSE1]>;
class SSIi8<bits<8> o, Format F, dag outs, dag ins, string asm,
            list<dag> pattern>
      : Ii8<o, F, outs, ins, asm, pattern>, TB, XS, Requires<[UseSSE1]>;
class PSI<bits<8> o, Format F, dag outs, dag ins, string asm,
          list<dag> pattern>
      : I<o, F, outs, ins, asm, pattern, SSEPackedSingle>, TB,
        Requires<[UseSSE1]>;
class PSIi8<bits<8> o, Format F, dag outs, dag ins, string asm,
            list<dag> pattern>
      : Ii8<o, F, outs, ins, asm, pattern, SSEPackedSingle>, TB,
        Requires<[UseSSE1]>;
class VSSI<bits<8> o, Format F, dag outs, dag ins, string asm,
           list<dag> pattern>
      : I<o, F, outs, ins, !strconcat("v", asm), pattern>, TB, XS,
        Requires<[HasAVX]>;
class VPSI<bits<8> o, Format F, dag outs, dag ins, string asm,
           list<dag> pattern>
      : I<o, F, outs, ins, !strconcat("v", asm), pattern, SSEPackedSingle>,
        TB, Requires<[HasAVX]>;

// SSE2 Instruction Templates:
//
//   SDI    - SSE2 instructions with XD prefix.
//   SDIi8  - SSE2 instructions with ImmT == Imm8 and XD prefix.
//   S2SI   - SSE2 instructions with XS prefix.
//   SSDIi8 - SSE2 instructions with ImmT == Imm8 and XS prefix.
//   PDI    - SSE2 instructions with PD prefix, packed double domain.
//   PDIi8  - SSE2 instructions with ImmT == Imm8 and PD prefix.
//   VSDI   - SSE2 scalar instructions with XD prefix in AVX form.
//   VPDI   - SSE2 vector instructions with PD prefix in AVX form,
//                 packed double domain.
//   VS2I   - SSE2 scalar instructions with PD prefix in AVX form.
//   S2I    - SSE2 scalar instructions with PD prefix.
//   MMXSDIi8  - SSE2 instructions with ImmT == Imm8 and XD prefix as well as
//               MMX operands.
//   MMXSSDIi8 - SSE2 instructions with ImmT == Imm8 and XS prefix as well as
//               MMX operands.

class SDI<bits<8> o, Format F, dag outs, dag ins, string asm,
          list<dag> pattern>
      : I<o, F, outs, ins, asm, pattern>, TB, XD, Requires<[UseSSE2]>;
class SDIi8<bits<8> o, Format F, dag outs, dag ins, string asm,
            list<dag> pattern>
      : Ii8<o, F, outs, ins, asm, pattern>, TB, XD, Requires<[UseSSE2]>;
class S2SI<bits<8> o, Format F, dag outs, dag ins, string asm,
           list<dag> pattern>
      : I<o, F, outs, ins, asm, pattern>, TB, XS, Requires<[UseSSE2]>;
class S2SIi8<bits<8> o, Format F, dag outs, dag ins, string asm,
             list<dag> pattern>
      : Ii8<o, F, outs, ins, asm, pattern>, TB, XS, Requires<[UseSSE2]>;
class PDI<bits<8> o, Format F, dag outs, dag ins, string asm,
          list<dag> pattern>
      : I<o, F, outs, ins, asm, pattern, SSEPackedDouble>, TB, PD,
        Requires<[UseSSE2]>;
class PDIi8<bits<8> o, Format F, dag outs, dag ins, string asm,
            list<dag> pattern>
      : Ii8<o, F, outs, ins, asm, pattern, SSEPackedDouble>, TB, PD,
        Requires<[UseSSE2]>;
class VSDI<bits<8> o, Format F, dag outs, dag ins, string asm,
           list<dag> pattern>
      : I<o, F, outs, ins, !strconcat("v", asm), pattern>, TB, XD,
        Requires<[UseAVX]>;
class VS2SI<bits<8> o, Format F, dag outs, dag ins, string asm,
            list<dag> pattern>
      : I<o, F, outs, ins, !strconcat("v", asm), pattern>, TB, XS,
        Requires<[HasAVX]>;
class VPDI<bits<8> o, Format F, dag outs, dag ins, string asm,
           list<dag> pattern>
      : I<o, F, outs, ins, !strconcat("v", asm), pattern, SSEPackedDouble>,
        TB, PD, Requires<[HasAVX]>;
class VS2I<bits<8> o, Format F, dag outs, dag ins, string asm,
           list<dag> pattern>
      : I<o, F, outs, ins, !strconcat("v", asm), pattern>, TB, PD,
        Requires<[UseAVX]>;
class S2I<bits<8> o, Format F, dag outs, dag ins, string asm,
           list<dag> pattern>
      : I<o, F, outs, ins, asm, pattern>, TB, PD, Requires<[UseSSE2]>;
class MMXSDIi8<bits<8> o, Format F, dag outs, dag ins, string asm,
               list<dag> pattern>
      : Ii8<o, F, outs, ins, asm, pattern>, TB, XD, Requires<[HasMMX, HasSSE2]>;
class MMXS2SIi8<bits<8> o, Format F, dag outs, dag ins, string asm,
                list<dag> pattern>
      : Ii8<o, F, outs, ins, asm, pattern>, TB, XS, Requires<[HasMMX, HasSSE2]>;

// SSE3 Instruction Templates:
//
//   S3I   - SSE3 instructions with PD prefixes.
//   S3SI  - SSE3 instructions with XS prefix.
//   S3DI  - SSE3 instructions with XD prefix.

class S3SI<bits<8> o, Format F, dag outs, dag ins, string asm,
           list<dag> pattern>
      : I<o, F, outs, ins, asm, pattern, SSEPackedSingle>, TB, XS,
        Requires<[UseSSE3]>;
class S3DI<bits<8> o, Format F, dag outs, dag ins, string asm,
           list<dag> pattern>
      : I<o, F, outs, ins, asm, pattern, SSEPackedDouble>, TB, XD,
        Requires<[UseSSE3]>;
class S3I<bits<8> o, Format F, dag outs, dag ins, string asm,
          list<dag> pattern>
      : I<o, F, outs, ins, asm, pattern, SSEPackedDouble>, TB, PD,
        Requires<[UseSSE3]>;


// SSSE3 Instruction Templates:
//
//   SS38I - SSSE3 instructions with T8 prefix.
//   SS3AI - SSSE3 instructions with TA prefix.
//   MMXSS38I - SSSE3 instructions with T8 prefix and MMX operands.
//   MMXSS3AI - SSSE3 instructions with TA prefix and MMX operands.
//
// Note: SSSE3 instructions have 64-bit and 128-bit versions. The 64-bit version
// uses the MMX registers. The 64-bit versions are grouped with the MMX
// classes. They need to be enabled even if AVX is enabled.

class SS38I<bits<8> o, Format F, dag outs, dag ins, string asm,
            list<dag> pattern>
      : I<o, F, outs, ins, asm, pattern, SSEPackedInt>, T8, PD,
        Requires<[UseSSSE3]>;
class SS3AI<bits<8> o, Format F, dag outs, dag ins, string asm,
            list<dag> pattern>
      : Ii8<o, F, outs, ins, asm, pattern, SSEPackedInt>, TA, PD,
        Requires<[UseSSSE3]>;
class MMXSS38I<bits<8> o, Format F, dag outs, dag ins, string asm,
               list<dag> pattern>
      : I<o, F, outs, ins, asm, pattern, SSEPackedInt>, T8,
        Requires<[HasMMX, HasSSSE3]>;
class MMXSS3AI<bits<8> o, Format F, dag outs, dag ins, string asm,
               list<dag> pattern>
      : Ii8<o, F, outs, ins, asm, pattern, SSEPackedInt>, TA,
        Requires<[HasMMX, HasSSSE3]>;

// SSE4.1 Instruction Templates:
//
//   SS48I - SSE 4.1 instructions with T8 prefix.
//   SS41AIi8 - SSE 4.1 instructions with TA prefix and ImmT == Imm8.
//
class SS48I<bits<8> o, Format F, dag outs, dag ins, string asm,
            list<dag> pattern>
      : I<o, F, outs, ins, asm, pattern, SSEPackedInt>, T8, PD,
        Requires<[UseSSE41]>;
class SS4AIi8<bits<8> o, Format F, dag outs, dag ins, string asm,
            list<dag> pattern>
      : Ii8<o, F, outs, ins, asm, pattern, SSEPackedInt>, TA, PD,
        Requires<[UseSSE41]>;

// SSE4.2 Instruction Templates:
//
//   SS428I - SSE 4.2 instructions with T8 prefix.
class SS428I<bits<8> o, Format F, dag outs, dag ins, string asm,
             list<dag> pattern>
      : I<o, F, outs, ins, asm, pattern, SSEPackedInt>, T8, PD,
        Requires<[UseSSE42]>;

//   SS42AI = SSE 4.2 instructions with TA prefix
class SS42AI<bits<8> o, Format F, dag outs, dag ins, string asm,
             list<dag> pattern>
      : Ii8<o, F, outs, ins, asm, pattern, SSEPackedInt>, TA, PD,
        Requires<[UseSSE42]>;

// AVX Instruction Templates:
//   Instructions introduced in AVX (no SSE equivalent forms)
//
//   AVX8I - AVX instructions with T8, PD prefix.
//   AVXAIi8 - AVX instructions with TA, PD prefix and ImmT = Imm8.
class AVX8I<bits<8> o, Format F, dag outs, dag ins, string asm,
            list<dag> pattern>
      : I<o, F, outs, ins, asm, pattern, SSEPackedInt>, T8, PD,
        Requires<[HasAVX]>;
class AVXAIi8<bits<8> o, Format F, dag outs, dag ins, string asm,
              list<dag> pattern>
      : Ii8<o, F, outs, ins, asm, pattern, SSEPackedInt>, TA, PD,
        Requires<[HasAVX]>;

// AVX2 Instruction Templates:
//   Instructions introduced in AVX2 (no SSE equivalent forms)
//
//   AVX28I - AVX2 instructions with T8, PD prefix.
//   AVX2AIi8 - AVX2 instructions with TA, PD prefix and ImmT = Imm8.
class AVX28I<bits<8> o, Format F, dag outs, dag ins, string asm,
            list<dag> pattern>
      : I<o, F, outs, ins, asm, pattern, SSEPackedInt>, T8, PD,
        Requires<[HasAVX2]>;
class AVX2AIi8<bits<8> o, Format F, dag outs, dag ins, string asm,
              list<dag> pattern>
      : Ii8<o, F, outs, ins, asm, pattern, SSEPackedInt>, TA, PD,
        Requires<[HasAVX2]>;


// AVX-512 Instruction Templates:
//   Instructions introduced in AVX-512 (no SSE equivalent forms)
//
//   AVX5128I - AVX-512 instructions with T8, PD prefix.
//   AVX512AIi8 - AVX-512 instructions with TA, PD prefix and ImmT = Imm8.
//   AVX512PDI  - AVX-512 instructions with PD, double packed.
//   AVX512PSI  - AVX-512 instructions with PS, single packed.
//   AVX512XS8I - AVX-512 instructions with T8 and XS prefixes.
//   AVX512XSI  - AVX-512 instructions with XS prefix, generic domain.
//   AVX512BI   - AVX-512 instructions with PD, int packed domain.
//   AVX512SI   - AVX-512 scalar instructions with PD prefix.

class AVX5128I<bits<8> o, Format F, dag outs, dag ins, string asm,
            list<dag> pattern>
      : I<o, F, outs, ins, asm, pattern, SSEPackedInt>, T8, PD,
        Requires<[HasAVX512]>;
class AVX5128IBase : T8, PD {
  Domain ExeDomain = SSEPackedInt;
}
class AVX512XS8I<bits<8> o, Format F, dag outs, dag ins, string asm,
            list<dag> pattern>
      : I<o, F, outs, ins, asm, pattern, SSEPackedInt>, T8, XS,
        Requires<[HasAVX512]>;
class AVX512XSI<bits<8> o, Format F, dag outs, dag ins, string asm,
            list<dag> pattern>
      : I<o, F, outs, ins, asm, pattern>, TB, XS,
        Requires<[HasAVX512]>;
class AVX512XDI<bits<8> o, Format F, dag outs, dag ins, string asm,
            list<dag> pattern>
      : I<o, F, outs, ins, asm, pattern, SSEPackedInt>, TB, XD,
        Requires<[HasAVX512]>;
class AVX512BI<bits<8> o, Format F, dag outs, dag ins, string asm,
            list<dag> pattern>
      : I<o, F, outs, ins, asm, pattern, SSEPackedInt>, TB, PD,
        Requires<[HasAVX512]>;
class AVX512BIBase : TB, PD {
  Domain ExeDomain = SSEPackedInt;
}
class AVX512BIi8<bits<8> o, Format F, dag outs, dag ins, string asm,
              list<dag> pattern>
      : Ii8<o, F, outs, ins, asm, pattern, SSEPackedInt>, TB, PD,
        Requires<[HasAVX512]>;
class AVX512AIi8<bits<8> o, Format F, dag outs, dag ins, string asm,
              list<dag> pattern>
      : Ii8<o, F, outs, ins, asm, pattern, SSEPackedInt>, TA, PD,
        Requires<[HasAVX512]>;
class AVX512AIi8Base : TA, PD {
  ImmType ImmT = Imm8;
}
class AVX512Ii8<bits<8> o, Format F, dag outs, dag ins, string asm,
              list<dag> pattern>
      : Ii8<o, F, outs, ins, asm, pattern, SSEPackedInt>,
        Requires<[HasAVX512]>;
class AVX512PDI<bits<8> o, Format F, dag outs, dag ins, string asm,
           list<dag> pattern>
      : I<o, F, outs, ins, asm, pattern, SSEPackedDouble>, TB, PD,
        Requires<[HasAVX512]>;
class AVX512PSI<bits<8> o, Format F, dag outs, dag ins, string asm,
           list<dag> pattern>
      : I<o, F, outs, ins, asm, pattern, SSEPackedSingle>, TB,
        Requires<[HasAVX512]>;
class AVX512PIi8<bits<8> o, Format F, dag outs, dag ins, string asm,
              list<dag> pattern, Domain d>
      : Ii8<o, F, outs, ins, asm, pattern, d>, Requires<[HasAVX512]>;
class AVX512PI<bits<8> o, Format F, dag outs, dag ins, string asm,
              list<dag> pattern, Domain d>
      : I<o, F, outs, ins, asm, pattern, d>, Requires<[HasAVX512]>;
class AVX512FMA3S<bits<8> o, Format F, dag outs, dag ins, string asm,
           list<dag>pattern>
      : I<o, F, outs, ins, asm, pattern>, T8, PD,
        EVEX, VVVV, Requires<[HasAVX512]>;

class AVX512<bits<8> o, Format F, dag outs, dag ins, string asm,
           list<dag>pattern>
      : I<o, F, outs, ins, asm, pattern>, Requires<[HasAVX512]>;

// AES Instruction Templates:
//
// AES8I
// These use the same encoding as the SSE4.2 T8 and TA encodings.
class AES8I<bits<8> o, Format F, dag outs, dag ins, string asm,
            list<dag>pattern>
      : I<o, F, outs, ins, asm, pattern, SSEPackedInt>, T8, PD,
        Requires<[NoAVX, HasAES]>;

class AESAI<bits<8> o, Format F, dag outs, dag ins, string asm,
            list<dag> pattern>
      : Ii8<o, F, outs, ins, asm, pattern, SSEPackedInt>, TA, PD,
        Requires<[NoAVX, HasAES]>;

// PCLMUL Instruction Templates
class PCLMULIi8<bits<8> o, Format F, dag outs, dag ins, string asm,
               list<dag>pattern>
      : Ii8<o, F, outs, ins, asm, pattern, SSEPackedInt>, TA, PD;

// FMA3 Instruction Templates
class FMA3<bits<8> o, Format F, dag outs, dag ins, string asm,
           list<dag>pattern>
      : I<o, F, outs, ins, asm, pattern>, T8, PD,
        VEX, VVVV, FMASC, Requires<[HasFMA, NoFMA4, NoVLX]>;
class FMA3S<bits<8> o, Format F, dag outs, dag ins, string asm,
            list<dag>pattern>
      : I<o, F, outs, ins, asm, pattern>, T8, PD,
        VEX, VVVV, FMASC, Requires<[HasFMA, NoFMA4, NoAVX512]>;
class FMA3S_Int<bits<8> o, Format F, dag outs, dag ins, string asm,
                list<dag>pattern>
      : I<o, F, outs, ins, asm, pattern>, T8, PD,
        VEX, VVVV, FMASC, Requires<[HasFMA, NoAVX512]>;

// FMA4 Instruction Templates
class FMA4<bits<8> o, Format F, dag outs, dag ins, string asm,
           list<dag>pattern>
      : Ii8Reg<o, F, outs, ins, asm, pattern>, TA, PD,
        VEX, VVVV, FMASC, Requires<[HasFMA4, NoVLX]>;
class FMA4S<bits<8> o, Format F, dag outs, dag ins, string asm,
            list<dag>pattern>
      : Ii8Reg<o, F, outs, ins, asm, pattern>, TA, PD,
        VEX, VVVV, FMASC, Requires<[HasFMA4, NoAVX512]>;
class FMA4S_Int<bits<8> o, Format F, dag outs, dag ins, string asm,
                list<dag>pattern>
      : Ii8Reg<o, F, outs, ins, asm, pattern>, TA, PD,
        VEX, VVVV, FMASC, Requires<[HasFMA4]>;

// XOP 2, 3 and 4 Operand Instruction Template
class IXOP<bits<8> o, Format F, dag outs, dag ins, string asm,
           list<dag> pattern>
      : I<o, F, outs, ins, asm, pattern, SSEPackedDouble>,
         XOP9, Requires<[HasXOP]>;

// XOP 2 and 3 Operand Instruction Templates with imm byte
class IXOPi8<bits<8> o, Format F, dag outs, dag ins, string asm,
           list<dag> pattern>
      : Ii8<o, F, outs, ins, asm, pattern, SSEPackedDouble>,
         XOP8, Requires<[HasXOP]>;
// XOP 4 Operand Instruction Templates with imm byte
class IXOPi8Reg<bits<8> o, Format F, dag outs, dag ins, string asm,
           list<dag> pattern>
      : Ii8Reg<o, F, outs, ins, asm, pattern, SSEPackedDouble>,
         XOP8, Requires<[HasXOP]>;

//  XOP 5 operand instruction (VEX encoding!)
class IXOP5<bits<8> o, Format F, dag outs, dag ins, string asm,
           list<dag>pattern>
      : Ii8Reg<o, F, outs, ins, asm, pattern, SSEPackedInt>, TA, PD,
        VEX, VVVV, Requires<[HasXOP]>;

// X86-64 Instruction templates...
//

class RI<bits<8> o, Format F, dag outs, dag ins, string asm,
         list<dag> pattern>
      : I<o, F, outs, ins, asm, pattern>, REX_W;
class RIi8 <bits<8> o, Format F, dag outs, dag ins, string asm,
            list<dag> pattern>
      : Ii8<o, F, outs, ins, asm, pattern>, REX_W;
class RIi16 <bits<8> o, Format F, dag outs, dag ins, string asm,
            list<dag> pattern>
      : Ii16<o, F, outs, ins, asm, pattern>, REX_W;
class RIi32 <bits<8> o, Format F, dag outs, dag ins, string asm,
             list<dag> pattern>
      : Ii32<o, F, outs, ins, asm, pattern>, REX_W;
class RIi32S <bits<8> o, Format F, dag outs, dag ins, string asm,
              list<dag> pattern>
      : Ii32S<o, F, outs, ins, asm, pattern>, REX_W;
class RIi64<bits<8> o, Format F, dag outs, dag ins, string asm,
            list<dag> pattern>
      : Ii64<o, F, outs, ins, asm, pattern>, REX_W;

class RS2I<bits<8> o, Format F, dag outs, dag ins, string asm,
           list<dag> pattern>
      : S2I<o, F, outs, ins, asm, pattern>, REX_W;
class VRS2I<bits<8> o, Format F, dag outs, dag ins, string asm,
           list<dag> pattern>
      : VS2I<o, F, outs, ins, asm, pattern>, REX_W;

// MMX Instruction templates
//
// MMXI   - MMX instructions with TB prefix.
// MMXRI  - MMX instructions with TB prefix and REX.W.
// MMXIi8 - MMX instructions with ImmT == Imm8 and PS prefix.
class MMXI<bits<8> o, Format F, dag outs, dag ins, string asm,
           list<dag> pattern>
      : I<o, F, outs, ins, asm, pattern>, TB, Requires<[HasMMX]>;
class MMXRI<bits<8> o, Format F, dag outs, dag ins, string asm,
            list<dag> pattern>
      : I<o, F, outs, ins, asm, pattern>, TB, REX_W,
        Requires<[HasMMX,In64BitMode]>;
class MMXIi8<bits<8> o, Format F, dag outs, dag ins, string asm,
             list<dag> pattern>
      : Ii8<o, F, outs, ins, asm, pattern>, TB, Requires<[HasMMX]>;

/// ITy - This instruction base class takes the type info for the instruction.
/// Using this, it:
/// 1. Concatenates together the instruction mnemonic with the appropriate
///    suffix letter, a tab, and the arguments.
/// 2. Infers whether the instruction should have a 0x40 REX_W prefix.
/// 3. Infers whether the low bit of the opcode should be 0 (for i8 operations)
///    or 1 (for i16,i32,i64 operations).
class ITy<bits<8> o, Format f, X86TypeInfo t, dag outs, dag ins, string m,
          string args, list<dag> p>
  : I<{o{7}, o{6}, o{5}, o{4}, o{3}, o{2}, o{1},
       !if(!eq(t.HasEvenOpcode, 1), 0, o{0})}, f, outs, ins,
      !strconcat(m, "{", t.InstrSuffix, "}\t", args), p>, NoCD8 {
  let hasSideEffects = 0;
  let hasREX_W  = t.HasREX_W;
  let IgnoresW = !if(!eq(t.VT, i8), 1, 0);
}

// BinOpRR - Instructions that read "reg, reg".
class BinOpRR<bits<8> o, string m, string args, X86TypeInfo t, dag out, list<dag> p>
  : ITy<o, MRMDestReg, t, out, (ins t.RegClass:$src1, t.RegClass:$src2), m,
        args, p>, Sched<[WriteALU]>;
// BinOpRR_F - Instructions that read "reg, reg" and write EFLAGS only.
class BinOpRR_F<bits<8> o, string m, X86TypeInfo t, SDPatternOperator node>
  : BinOpRR<o, m, binop_args, t, (outs),
            [(set EFLAGS, (node t.RegClass:$src1, t.RegClass:$src2))]>,
    DefEFLAGS;
// BinOpRR_F_Rev - Reversed encoding of BinOpRR_F
class BinOpRR_F_Rev<bits<8> o, string m, X86TypeInfo t>
  : BinOpRR_F<o, m, t, null_frag>, DisassembleOnly {
  let Form = MRMSrcReg;
}
// BinOpRR_R - Instructions that read "reg, reg" and write "reg".
class BinOpRR_R<bits<8> o, string m, X86TypeInfo t, bit ndd = 0>
  : BinOpRR<o, m, !if(!eq(ndd, 0), binop_args, binop_ndd_args), t,
            (outs t.RegClass:$dst), []>, NDD<ndd>;
// BinOpRR_R_Rev - Reversed encoding of BinOpRR_R
class BinOpRR_R_Rev<bits<8> o, string m, X86TypeInfo t, bit ndd = 0>
  : BinOpRR_R<o, m, t, ndd>, DisassembleOnly {
  let Form = MRMSrcReg;
}
// BinOpRR_RF - Instructions that read "reg, reg", and write "reg", EFLAGS.
class BinOpRR_RF<bits<8> o, string m, X86TypeInfo t, SDPatternOperator node, bit ndd = 0>
  : BinOpRR<o, m, !if(!eq(ndd, 0), binop_args, binop_ndd_args), t,
            (outs t.RegClass:$dst),
            [(set t.RegClass:$dst, EFLAGS,
             (node t.RegClass:$src1, t.RegClass:$src2))]>, DefEFLAGS, NDD<ndd>;
// BinOpRR_RF_Rev - Reversed encoding of BinOpRR_RF.
class BinOpRR_RF_Rev<bits<8> o, string m, X86TypeInfo t, bit ndd = 0>
  : BinOpRR_RF<o, m, t, null_frag, ndd>, DisassembleOnly {
  let Form = MRMSrcReg;
}
// BinOpRRF_RF - Instructions that read "reg, reg", write "reg" and read/write
// EFLAGS.
class BinOpRRF_RF<bits<8> o, string m, X86TypeInfo t, SDPatternOperator node = null_frag, bit ndd = 0>
  : BinOpRR<o, m, !if(!eq(ndd, 0), binop_args, binop_ndd_args), t, (outs t.RegClass:$dst),
            [(set t.RegClass:$dst, EFLAGS,
             (node t.RegClass:$src1, t.RegClass:$src2,
             EFLAGS))]>, DefEFLAGS, UseEFLAGS, NDD<ndd> {
  let SchedRW = [WriteADC];
}
// BinOpRRF_RF_Rev - Reversed encoding of BinOpRRF_RF
class BinOpRRF_RF_Rev<bits<8> o, string m, X86TypeInfo t, bit ndd = 0>
  : BinOpRRF_RF<o, m, t, null_frag, ndd>, DisassembleOnly {
  let Form = MRMSrcReg;
}

// BinOpRM - Instructions that read "reg, [mem]".
class BinOpRM<bits<8> o, string m, string args, X86TypeInfo t, dag out, list<dag> p>
  : ITy<o, MRMSrcMem, t, out, (ins t.RegClass:$src1, t.MemOperand:$src2), m,
        args, p>,
    Sched<[WriteALU.Folded, WriteALU.ReadAfterFold]> {
  let mayLoad = 1;
}
// BinOpRM_F - Instructions that read "reg, [mem]" and write EFLAGS only.
class BinOpRM_F<bits<8> o, string m, X86TypeInfo t, SDNode node>
  : BinOpRM<o, m, binop_args, t, (outs),
            [(set EFLAGS, (node t.RegClass:$src1,
             (t.LoadNode addr:$src2)))]>, DefEFLAGS;
// BinOpRM_R - Instructions that read "reg, [mem]", and write "reg".
class BinOpRM_R<bits<8> o, string m, X86TypeInfo t, bit ndd = 0>
  : BinOpRM<o, m, !if(!eq(ndd, 0), binop_args, binop_ndd_args), t, (outs t.RegClass:$dst),
            []>, NDD<ndd>;
// BinOpRM_RF - Instructions that read "reg, [mem]", and write "reg", EFLAGS.
class BinOpRM_RF<bits<8> o, string m, X86TypeInfo t, SDPatternOperator node, bit ndd = 0>
  : BinOpRM<o, m, !if(!eq(ndd, 0), binop_args, binop_ndd_args), t, (outs t.RegClass:$dst),
            [(set t.RegClass:$dst, EFLAGS, (node t.RegClass:$src1,
             (t.LoadNode addr:$src2)))]>, DefEFLAGS, NDD<ndd>;
// BinOpRMF_RF - Instructions that read "reg, [mem]", write "reg" and read/write
// EFLAGS.
class BinOpRMF_RF<bits<8> o, string m, X86TypeInfo t, SDPatternOperator node = null_frag, bit ndd = 0>
  : BinOpRM<o, m, !if(!eq(ndd, 0), binop_args, binop_ndd_args), t, (outs t.RegClass:$dst),
            [(set t.RegClass:$dst, EFLAGS,
             (node t.RegClass:$src1, (t.LoadNode addr:$src2), EFLAGS))]>,
    DefEFLAGS, UseEFLAGS, NDD<ndd> {
  let SchedRW = [WriteADC.Folded, WriteADC.ReadAfterFold,
                 // base, scale, index, offset, segment.
                 ReadDefault, ReadDefault, ReadDefault, ReadDefault, ReadDefault,
                 // implicit register read.
                 WriteADC.ReadAfterFold];
}

// BinOpRI - Instructions that read "reg, imm".
class BinOpRI<bits<8> o, string m, string args, X86TypeInfo t, Format f, dag out, list<dag> p>
  : ITy<o, f, t, out, (ins t.RegClass:$src1, t.ImmOperand:$src2), m,
        args, p>, Sched<[WriteALU]> {
  let ImmT = t.ImmEncoding;
}
// BinOpRI_F - Instructions that read "reg, imm" and write EFLAGS only.
class BinOpRI_F<bits<8> o, string m, X86TypeInfo t, SDPatternOperator node,
                Format f>
  : BinOpRI<o, m, binop_args, t, f, (outs),
            [(set EFLAGS, (node t.RegClass:$src1,
             t.ImmOperator:$src2))]>, DefEFLAGS;
// BinOpRI_R - Instructions that read "reg, imm" and write "reg".
class BinOpRI_R<bits<8> o, string m, X86TypeInfo t, Format f, bit ndd = 0>
  : BinOpRI<o, m, !if(!eq(ndd, 0), binop_args, binop_ndd_args), t, f, (outs t.RegClass:$dst),
            []>, NDD<ndd>;
// BinOpRI8U_R - Instructions that read "reg, u8imm" and write "reg".
class BinOpRI8U_R<string m, Format f, X86TypeInfo t, SDPatternOperator node = null_frag, bit ndd = 0>
  : ITy<0xC1, f, t, (outs t.RegClass:$dst), (ins t.RegClass:$src1, u8imm:$src2), m,
        !if(!eq(ndd, 0), binop_args, binop_ndd_args),
        [(set t.RegClass:$dst, (node t.RegClass:$src1, (i8 imm:$src2)))]>, NDD<ndd> {
  let ImmT = Imm8;
}
// BinOpRI_RF - Instructions that read "reg, imm" and write "reg", EFLAGS.
class BinOpRI_RF<bits<8> o, string m, X86TypeInfo t, SDPatternOperator node, Format f, bit ndd = 0>
  : BinOpRI<o, m, !if(!eq(ndd, 0), binop_args, binop_ndd_args), t, f, (outs t.RegClass:$dst),
            [(set t.RegClass:$dst, EFLAGS,
             (node t.RegClass:$src1, t.ImmOperator:$src2))]>, DefEFLAGS, NDD<ndd>;
// BinOpRIF_RF - Instructions that read "reg, imm", write "reg" and read/write
// EFLAGS.
class BinOpRIF_RF<bits<8> o, string m, X86TypeInfo t, SDNode node, Format f, bit ndd = 0>
  : BinOpRI<o, m, !if(!eq(ndd, 0), binop_args, binop_ndd_args), t, f, (outs t.RegClass:$dst),
            [(set t.RegClass:$dst, EFLAGS,
             (node t.RegClass:$src1, t.ImmOperator:$src2,
             EFLAGS))]>, DefEFLAGS, UseEFLAGS, NDD<ndd> {
  let SchedRW = [WriteADC];
}
// BinOpRI8 - Instructions that read "reg, imm8".
class BinOpRI8<bits<8> o, string m, string args, X86TypeInfo t, Format f, dag out>
  : ITy<o, f, t, out, (ins t.RegClass:$src1, t.Imm8Operand:$src2), m,
        args, []>, Sched<[WriteALU]> {
  let ImmT = Imm8;
}
// BinOpRI8_F - Instructions that read "reg, imm8" and write EFLAGS only.
class BinOpRI8_F<bits<8> o, string m, X86TypeInfo t, Format f>
  : BinOpRI8<o, m, binop_args, t, f, (outs)>, DefEFLAGS;
// BinOpRI8_R - Instructions that read "reg, imm8" and write "reg".
class BinOpRI8_R<bits<8> o, string m, X86TypeInfo t, Format f, bit ndd = 0>
  : BinOpRI8<o, m, !if(!eq(ndd, 0), binop_args, binop_ndd_args), t, f, (outs t.RegClass:$dst)>, NDD<ndd>;
// BinOpRI8_RF - Instructions that read "reg, imm8" and write "reg", EFLAGS.
class BinOpRI8_RF<bits<8> o, string m, X86TypeInfo t, Format f, bit ndd = 0>
  : BinOpRI8<o, m, !if(!eq(ndd, 0), binop_args, binop_ndd_args), t, f, (outs t.RegClass:$dst)>, DefEFLAGS, NDD<ndd>;
// BinOpRI8F_RF - Instructions that read "reg, imm", write "reg" and read/write
// EFLAGS.
class BinOpRI8F_RF<bits<8> o, string m, X86TypeInfo t, Format f, bit ndd = 0>
  : BinOpRI8<o, m, !if(!eq(ndd, 0), binop_args, binop_ndd_args), t, f, (outs t.RegClass:$dst)>, DefEFLAGS, UseEFLAGS, NDD<ndd> {
  let SchedRW = [WriteADC];
}

// BinOpMR - Instructions that read "[mem], reg".
class BinOpMR<bits<8> o, string m, string args, X86TypeInfo t, dag out, list<dag> p>
  : ITy<o, MRMDestMem, t, out, (ins t.MemOperand:$src1, t.RegClass:$src2), m,
        args, p> {
  let mayLoad = 1;
  let SchedRW = [WriteALU.Folded, WriteALU.ReadAfterFold];
}
// BinOpMR_R - Instructions that read "[mem], reg", and write "reg".
class BinOpMR_R<bits<8> o, string m, X86TypeInfo t>
  : BinOpMR<o, m, binop_ndd_args, t, (outs t.RegClass:$dst), []>, NDD<1>;
// BinOpMR_RF - Instructions that read "[mem], reg", and write "reg", EFLAGS.
class BinOpMR_RF<bits<8> o, string m, X86TypeInfo t, SDPatternOperator node>
  : BinOpMR<o, m, binop_ndd_args, t, (outs t.RegClass:$dst),
            [(set t.RegClass:$dst, EFLAGS, (node (t.LoadNode addr:$src1),
             t.RegClass:$src2))]>, DefEFLAGS, NDD<1>;
// BinOpMR_F - Instructions that read "[mem], imm8" and write EFLAGS only.
class BinOpMR_F<bits<8> o, string m, X86TypeInfo t, SDPatternOperator node>
  : BinOpMR<o, m, binop_args, t, (outs),
            [(set EFLAGS, (node (t.LoadNode addr:$src1), t.RegClass:$src2))]>,
    Sched<[WriteALU.Folded, ReadDefault, ReadDefault, ReadDefault,
            ReadDefault, ReadDefault, WriteALU.ReadAfterFold]>, DefEFLAGS;
// BinOpMR_M - Instructions that read "[mem], reg" and write "[mem]".
class BinOpMR_M<bits<8> o, string m, X86TypeInfo t>
  : BinOpMR<o, m, binop_args, t, (outs), []>,
    Sched<[WriteALURMW,
           // base, scale, index, offset, segment
           ReadDefault, ReadDefault, ReadDefault, ReadDefault, ReadDefault]> {
  let mayStore = 1;
}
// BinOpMR_MF - Instructions that read "[mem], reg" and write "[mem]", EFLAGS.
class BinOpMR_MF<bits<8> o, string m, X86TypeInfo t, SDPatternOperator node>
  : BinOpMR<o, m, binop_args, t, (outs),
            [(store (node (load addr:$src1), t.RegClass:$src2), addr:$src1),
             (implicit EFLAGS)]>,
    Sched<[WriteALURMW,
           // base, scale, index, offset, segment
           ReadDefault, ReadDefault, ReadDefault, ReadDefault, ReadDefault,
           WriteALU.ReadAfterFold]>, // reg
    DefEFLAGS {
  let mayStore = 1;
}
// BinOpMRF_RF - Instructions that read "[mem], reg", write "reg" and
// read/write EFLAGS.
class BinOpMRF_RF<bits<8> o, string m, X86TypeInfo t, SDPatternOperator node>
  : BinOpMR<o, m, binop_ndd_args, t, (outs t.RegClass:$dst),
            [(set t.RegClass:$dst, EFLAGS, (node (load addr:$src1),
             t.RegClass:$src2, EFLAGS))]>, DefEFLAGS, UseEFLAGS, NDD<1>,
    Sched<[WriteADC.Folded, WriteADC.ReadAfterFold]>;
// BinOpMRF_MF - Instructions that read "[mem], reg", write "[mem]" and
// read/write EFLAGS.
class BinOpMRF_MF<bits<8> o, string m, X86TypeInfo t, SDPatternOperator node>
  : BinOpMR<o, m, binop_args, t, (outs),
            [(store (node (load addr:$src1), t.RegClass:$src2, EFLAGS),
             addr:$src1), (implicit EFLAGS)]>,
    Sched<[WriteADCRMW,
          // base, scale, index, offset, segment
          ReadDefault, ReadDefault, ReadDefault,
          ReadDefault, ReadDefault,
          WriteALU.ReadAfterFold,    // reg
          WriteALU.ReadAfterFold]>,  // EFLAGS
    DefEFLAGS, UseEFLAGS {
  let mayStore = 1;
}

// BinOpMI - Instructions that read "[mem], imm".
class BinOpMI<bits<8> o, string m, string args, X86TypeInfo t, Format f, dag out, list<dag> p>
  : ITy<o, f, t, out, (ins t.MemOperand:$src1, t.ImmOperand:$src2), m,
        args, p> {
  let ImmT = t.ImmEncoding;
  let mayLoad = 1;
}
// BinOpMI_F - Instructions that read "[mem], imm" and write EFLAGS only.
class BinOpMI_F<bits<8> o, string m, X86TypeInfo t, SDPatternOperator node,
                Format f>
  : BinOpMI<o, m, binop_args, t, f, (outs),
            [(set EFLAGS, (node (t.LoadNode addr:$src1), t.ImmOperator:$src2))]>,
    Sched<[WriteALU.Folded]>, DefEFLAGS;
// BinOpMI_R - Instructions that read "[mem], imm" and write "reg".
class BinOpMI_R<bits<8> o, string m, X86TypeInfo t, Format f>
  : BinOpMI<o, m, binop_ndd_args, t, f, (outs t.RegClass:$dst), []>,
    Sched<[WriteALU.Folded, WriteALU.ReadAfterFold]>, NDD<1>;
// BinOpMI_R - Instructions that read "[mem], imm" and write "reg", EFLAGS.
class BinOpMI_RF<bits<8> o, string m, X86TypeInfo t, SDPatternOperator node,
                Format f>
  : BinOpMI<o, m, binop_ndd_args, t, f, (outs t.RegClass:$dst),
            [(set t.RegClass:$dst, EFLAGS, (node (t.LoadNode addr:$src1), t.ImmOperator:$src2))]>,
    Sched<[WriteALU.Folded, WriteALU.ReadAfterFold]>, DefEFLAGS, NDD<1>;
// BinOpMI_M - Instructions that read "[mem], imm" and write "[mem]".
class BinOpMI_M<bits<8> o, string m, X86TypeInfo t, Format f>
  : BinOpMI<o, m, binop_args, t, f, (outs), []>, Sched<[WriteALURMW]> {
  let mayStore = 1;
}
// BinOpMI_MF - Instructions that read "[mem], imm" and write "[mem]", EFLAGS.
class BinOpMI_MF<bits<8> o, string m, X86TypeInfo t, SDPatternOperator node, Format f>
  : BinOpMI<o, m, binop_args, t, f, (outs),
            [(store (node (t.VT (load addr:$src1)),
             t.ImmOperator:$src2), addr:$src1), (implicit EFLAGS)]>,
    Sched<[WriteALURMW]>, DefEFLAGS {
  let mayStore = 1;
}
// BinOpMIF_RF - Instructions that read "[mem], imm", write "reg" and
// read/write EFLAGS.
class BinOpMIF_RF<bits<8> o, string m, X86TypeInfo t, SDNode node, Format f>
  : BinOpMI<o, m, binop_ndd_args, t, f, (outs t.RegClass:$dst),
            [(set t.RegClass:$dst, EFLAGS, (node (t.VT (load addr:$src1)),
             t.ImmOperator:$src2, EFLAGS))]>,
    Sched<[WriteADC.Folded, WriteADC.ReadAfterFold]>, DefEFLAGS, UseEFLAGS, NDD<1>;
// BinOpMIF_MF - Instructions that read "[mem], imm", write "[mem]" and
// read/write EFLAGS.
class BinOpMIF_MF<bits<8> o, string m, X86TypeInfo t, SDNode node, Format f>
  : BinOpMI<o, m, binop_args, t, f, (outs),
            [(store (node (t.VT (load addr:$src1)),
             t.ImmOperator:$src2, EFLAGS), addr:$src1), (implicit EFLAGS)]>,
    Sched<[WriteADCRMW]>, DefEFLAGS, UseEFLAGS {
  let mayStore = 1;
}

// BinOpMI8 - Instructions that read "[mem], imm8".
class BinOpMI8<string m, string args, X86TypeInfo t, Format f, dag out>
  : ITy<0x83, f, t, out, (ins t.MemOperand:$src1, t.Imm8Operand:$src2), m,
        args, []> {
  let ImmT = Imm8;
  let mayLoad = 1;
}
// BinOpMI8U - Instructions that read "[mem], u8imm".
class BinOpMI8U<string m, string args, X86TypeInfo t, Format f, dag out, list<dag> p>
  : ITy<0xC1, f, t, out, (ins t.MemOperand:$src1, u8imm:$src2), m, args, p> {
  let ImmT = Imm8;
  let mayLoad = 1;
}
// BinOpMI8_F - Instructions that read "[mem], imm8" and write EFLAGS only.
class BinOpMI8_F<string m, X86TypeInfo t, Format f>
  : BinOpMI8<m, binop_args, t, f, (outs)>, Sched<[WriteALU.Folded]>, DefEFLAGS;
// BinOpMI8_R - Instructions that read "[mem], imm8" and write "reg".
class BinOpMI8_R<string m, X86TypeInfo t, Format f>
  : BinOpMI8<m, binop_ndd_args, t, f, (outs t.RegClass:$dst)>, Sched<[WriteALU.Folded, WriteALU.ReadAfterFold]>, NDD<1>;
// BinOpMI8U_R - Instructions that read "[mem], u8imm" and write "reg".
class BinOpMI8U_R<string m, Format f, X86TypeInfo t, SDPatternOperator node = null_frag>
  : BinOpMI8U<m, binop_ndd_args, t, f, (outs t.RegClass:$dst),
              [(set t.RegClass:$dst, (node (t.LoadNode addr:$src1), (i8 imm:$src2)))]>, NDD<1>;
// BinOpMI8_RF - Instructions that read "[mem], imm8" and write "reg"/EFLAGS.
class BinOpMI8_RF<string m, X86TypeInfo t, Format f>
  : BinOpMI8<m, binop_ndd_args, t, f, (outs t.RegClass:$dst)>, Sched<[WriteALU.Folded, WriteALU.ReadAfterFold]>, DefEFLAGS, NDD<1>;
// BinOpMI8_M - Instructions that read "[mem], imm8" and write "[mem]".
class BinOpMI8_M<string m, X86TypeInfo t, Format f>
  : BinOpMI8<m, binop_args, t, f, (outs)>, Sched<[WriteALURMW]> {
  let mayStore = 1;
}
// BinOpMI8U_M - Instructions that read "[mem], u8imm" and write "[mem]".
class BinOpMI8U_M<string m, Format f, X86TypeInfo t, SDPatternOperator node = null_frag>
  : BinOpMI8U<m, binop_args, t, f, (outs),
              [(store (node (t.LoadNode addr:$src1), (i8 imm:$src2)), addr:$src1)]> {
  let mayStore = 1;
}
// BinOpMI8_MF - Instructions that read "[mem], imm8" and write "[mem]", EFLAGS.
class BinOpMI8_MF<string m, X86TypeInfo t, Format f>
  : BinOpMI8<m, binop_args, t, f, (outs)>, Sched<[WriteALURMW]>, DefEFLAGS {
  let mayStore = 1;
}
// BinOpMI8F_RF - Instructions that read "[mem], imm8", write "reg" and
// read/write EFLAGS.
class BinOpMI8F_RF<string m, X86TypeInfo t, Format f>
  : BinOpMI8<m, binop_ndd_args, t, f, (outs t.RegClass:$dst)>,
    Sched<[WriteADC.Folded, WriteADC.ReadAfterFold]>, DefEFLAGS, UseEFLAGS, NDD<1>;
// BinOpMI8F_MF - Instructions that read "[mem], imm8", write "[mem]" and
// read/write EFLAGS.
class BinOpMI8F_MF<string m, X86TypeInfo t, Format f>
  : BinOpMI8<m, binop_args, t, f, (outs)>, Sched<[WriteADCRMW]>, DefEFLAGS, UseEFLAGS {
  let mayStore = 1;
}

// BinOpAI - Instructions that read "a-reg imm" (Accumulator register).
class BinOpAI<bits<8> o, string m, X86TypeInfo t, Register areg, string args>
  : ITy<o, RawFrm, t, (outs), (ins t.ImmOperand:$src), m, args, []>,
    Sched<[WriteALU]> {
  let ImmT = t.ImmEncoding;
  let Uses = [areg];
}
// BinOpAI_F - Instructions that read "a-reg imm" and write EFLAGS only.
class BinOpAI_F<bits<8> o, string m, X86TypeInfo t, Register areg, string args>
  : BinOpAI<o, m, t, areg, args>, DefEFLAGS;

// BinOpAI_AF - Instructions that read "a-reg imm" and write a-reg/EFLAGS.
class BinOpAI_AF<bits<8> o, string m, X86TypeInfo t, Register areg,
                 string args> : BinOpAI<o, m, t, areg, args> {
  let Defs = [areg, EFLAGS];
}
// BinOpAIF_AF - Instructions that read "a-reg imm", write a-reg and read/write
// EFLAGS.
class BinOpAIF_AF<bits<8> o, string m, X86TypeInfo t, Register areg,
                  string args> : BinOpAI<o, m, t, areg, args> {
  let Uses = [areg, EFLAGS];
  let Defs = [areg, EFLAGS];
  let SchedRW = [WriteADC];
}
// BinOpRC_R - Instructions that read "reg, cl" and write reg.
class BinOpRC_R<string m, Format f, X86TypeInfo t, SDPatternOperator node = null_frag, bit ndd = 0>
  : ITy<0xD3, f, t, (outs t.RegClass:$dst), (ins t.RegClass:$src1), m,
        !if(!eq(ndd, 0), binop_cl_args, binop_cl_ndd_args),
        [(set t.RegClass:$dst, (node t.RegClass:$src1, CL))]>, NDD<ndd> {
  let Uses = [CL];
}
// BinOpMC_M - Instructions that read "[mem], cl" and write [mem].
class BinOpMC_M<string m, Format f, X86TypeInfo t, SDPatternOperator node = null_frag>
  : ITy<0xD3, f, t, (outs), (ins t.MemOperand:$src1), m, binop_cl_args,
        [(store (node (t.LoadNode addr:$src1), CL), addr:$src1)]> {
  let Uses = [CL];
  let mayLoad = 1;
  let mayStore = 1;
}
// BinOpMC_R - Instructions that read "[mem], cl" and write reg.
class BinOpMC_R<string m, Format f, X86TypeInfo t, SDPatternOperator node = null_frag>
  : ITy<0xD3, f, t, (outs t.RegClass:$dst), (ins t.MemOperand:$src1), m, binop_cl_ndd_args,
        [(set t.RegClass:$dst, (node (t.LoadNode addr:$src1), CL))]>, NDD<1> {
  let Uses = [CL];
  let mayLoad = 1;
}

// UnaryOpR - Instructions that read "reg".
class UnaryOpR<bits<8> o, Format f, string m, string args, X86TypeInfo t,
               dag out, list<dag> p>
  : ITy<o, f, t, out, (ins t.RegClass:$src1), m, args, p>, Sched<[WriteALU]>;
// UnaryOpR_R - Instructions that read "reg" and write "reg".
class UnaryOpR_R<bits<8> o, Format f, string m, X86TypeInfo t,
                  SDPatternOperator node = null_frag, bit ndd = 0>
  : UnaryOpR<o, f, m, !if(!eq(ndd, 0), unaryop_args, unaryop_ndd_args), t,
             (outs t.RegClass:$dst),
             [(set t.RegClass:$dst, (node t.RegClass:$src1))]>, NDD<ndd>;
// UnaryOpR_RF - Instructions that read "reg" and write "reg"/EFLAGS.
class UnaryOpR_RF<bits<8> o, Format f, string m, X86TypeInfo t,
                  SDPatternOperator node = null_frag, bit ndd = 0>
  : UnaryOpR<o, f, m, !if(!eq(ndd, 0), unaryop_args, unaryop_ndd_args), t,
             (outs t.RegClass:$dst),
             [(set t.RegClass:$dst, (node t.RegClass:$src1)),
              (implicit EFLAGS)]>, DefEFLAGS, NDD<ndd>;

// UnaryOpM - Instructions that read "[mem]".
class UnaryOpM<bits<8> o, Format f, string m, string args, X86TypeInfo t,
               dag out, list<dag> p>
  : ITy<o, f, t, out, (ins t.MemOperand:$src1), m, args, p> {
  let mayLoad = 1;
}
// UnaryOpM_R - Instructions that read "[mem]" and writes "reg".
class UnaryOpM_R<bits<8> o, Format f, string m, X86TypeInfo t,
                  SDPatternOperator node = null_frag>
  : UnaryOpM<o, f, m, unaryop_ndd_args, t, (outs t.RegClass:$dst),
             [(set t.RegClass:$dst, (node (t.LoadNode addr:$src1)))]>,
    Sched<[WriteALU.Folded, WriteALU.ReadAfterFold]>, NDD<1>;
// UnaryOpM_RF - Instructions that read "[mem]" and writes "reg"/EFLAGS.
class UnaryOpM_RF<bits<8> o, Format f, string m, X86TypeInfo t,
                  SDPatternOperator node = null_frag>
  : UnaryOpM<o, f, m, unaryop_ndd_args, t, (outs t.RegClass:$dst),
             [(set t.RegClass:$dst, EFLAGS, (node (t.LoadNode addr:$src1)))]>,
    Sched<[WriteALU.Folded, WriteALU.ReadAfterFold]>, DefEFLAGS, NDD<1>;
// UnaryOpM_M - Instructions that read "[mem]" and writes "[mem]".
class UnaryOpM_M<bits<8> o, Format f, string m, X86TypeInfo t,
                  SDPatternOperator node = null_frag>
  : UnaryOpM<o, f, m, unaryop_args, t, (outs),
             [(store (node (t.LoadNode addr:$src1)), addr:$src1)]>,
    Sched<[WriteALURMW]>{
  let mayStore = 1;
}
// UnaryOpM_MF - Instructions that read "[mem]" and writes "[mem]"/EFLAGS.
class UnaryOpM_MF<bits<8> o, Format f, string m, X86TypeInfo t,
                  SDPatternOperator node = null_frag>
  : UnaryOpM<o, f, m, unaryop_args, t, (outs),
             [(store (node (t.LoadNode addr:$src1)), addr:$src1),
              (implicit EFLAGS)]>, Sched<[WriteALURMW]>, DefEFLAGS {
  let mayStore = 1;
}
