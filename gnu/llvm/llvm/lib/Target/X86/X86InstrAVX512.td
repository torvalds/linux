//===-- X86InstrAVX512.td - AVX512 Instruction Set ---------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the X86 AVX512 instruction set, defining the
// instructions, and properties of the instructions which are needed for code
// generation, machine code emission, and analysis.
//
//===----------------------------------------------------------------------===//

// This multiclass generates the masking variants from the non-masking
// variant.  It only provides the assembly pieces for the masking variants.
// It assumes custom ISel patterns for masking which can be provided as
// template arguments.
multiclass AVX512_maskable_custom<bits<8> O, Format F,
                                  dag Outs,
                                  dag Ins, dag MaskingIns, dag ZeroMaskingIns,
                                  string OpcodeStr,
                                  string AttSrcAsm, string IntelSrcAsm,
                                  list<dag> Pattern,
                                  list<dag> MaskingPattern,
                                  list<dag> ZeroMaskingPattern,
                                  string MaskingConstraint = "",
                                  bit IsCommutable = 0,
                                  bit IsKCommutable = 0,
                                  bit IsKZCommutable = IsCommutable,
                                  string ClobberConstraint = ""> {
  let isCommutable = IsCommutable, Constraints = ClobberConstraint in
    def NAME: AVX512<O, F, Outs, Ins,
                       OpcodeStr#"\t{"#AttSrcAsm#", $dst|"#
                                     "$dst, "#IntelSrcAsm#"}",
                       Pattern>;

  // Prefer over VMOV*rrk Pat<>
  let isCommutable = IsKCommutable in
    def NAME#k: AVX512<O, F, Outs, MaskingIns,
                       OpcodeStr#"\t{"#AttSrcAsm#", $dst {${mask}}|"#
                                     "$dst {${mask}}, "#IntelSrcAsm#"}",
                       MaskingPattern>,
              EVEX_K {
      // In case of the 3src subclass this is overridden with a let.
      string Constraints = !if(!eq(ClobberConstraint, ""), MaskingConstraint,
                               !if(!eq(MaskingConstraint, ""), ClobberConstraint,
                                   !strconcat(ClobberConstraint, ", ", MaskingConstraint)));
    }

  // Zero mask does not add any restrictions to commute operands transformation.
  // So, it is Ok to use IsCommutable instead of IsKCommutable.
  let isCommutable = IsKZCommutable, // Prefer over VMOV*rrkz Pat<>
      Constraints = ClobberConstraint in
    def NAME#kz: AVX512<O, F, Outs, ZeroMaskingIns,
                       OpcodeStr#"\t{"#AttSrcAsm#", $dst {${mask}} {z}|"#
                                     "$dst {${mask}} {z}, "#IntelSrcAsm#"}",
                       ZeroMaskingPattern>,
              EVEX_KZ;
}


// Common base class of AVX512_maskable and AVX512_maskable_3src.
multiclass AVX512_maskable_common<bits<8> O, Format F, X86VectorVTInfo _,
                                  dag Outs,
                                  dag Ins, dag MaskingIns, dag ZeroMaskingIns,
                                  string OpcodeStr,
                                  string AttSrcAsm, string IntelSrcAsm,
                                  dag RHS, dag MaskingRHS,
                                  SDPatternOperator Select = vselect_mask,
                                  string MaskingConstraint = "",
                                  bit IsCommutable = 0,
                                  bit IsKCommutable = 0,
                                  bit IsKZCommutable = IsCommutable,
                                  string ClobberConstraint = ""> :
  AVX512_maskable_custom<O, F, Outs, Ins, MaskingIns, ZeroMaskingIns, OpcodeStr,
                         AttSrcAsm, IntelSrcAsm,
                         [(set _.RC:$dst, RHS)],
                         [(set _.RC:$dst, MaskingRHS)],
                         [(set _.RC:$dst,
                               (Select _.KRCWM:$mask, RHS, _.ImmAllZerosV))],
                         MaskingConstraint, IsCommutable,
                         IsKCommutable, IsKZCommutable, ClobberConstraint>;

// This multiclass generates the unconditional/non-masking, the masking and
// the zero-masking variant of the vector instruction.  In the masking case, the
// preserved vector elements come from a new dummy input operand tied to $dst.
// This version uses a separate dag for non-masking and masking.
multiclass AVX512_maskable_split<bits<8> O, Format F, X86VectorVTInfo _,
                           dag Outs, dag Ins, string OpcodeStr,
                           string AttSrcAsm, string IntelSrcAsm,
                           dag RHS, dag MaskRHS,
                           string ClobberConstraint = "",
                           bit IsCommutable = 0, bit IsKCommutable = 0,
                           bit IsKZCommutable = IsCommutable> :
   AVX512_maskable_custom<O, F, Outs, Ins,
                          !con((ins _.RC:$src0, _.KRCWM:$mask), Ins),
                          !con((ins _.KRCWM:$mask), Ins),
                          OpcodeStr, AttSrcAsm, IntelSrcAsm,
                          [(set _.RC:$dst, RHS)],
                          [(set _.RC:$dst,
                              (vselect_mask _.KRCWM:$mask, MaskRHS, _.RC:$src0))],
                          [(set _.RC:$dst,
                              (vselect_mask _.KRCWM:$mask, MaskRHS, _.ImmAllZerosV))],
                          "$src0 = $dst", IsCommutable, IsKCommutable,
                          IsKZCommutable, ClobberConstraint>;

// This multiclass generates the unconditional/non-masking, the masking and
// the zero-masking variant of the vector instruction.  In the masking case, the
// preserved vector elements come from a new dummy input operand tied to $dst.
multiclass AVX512_maskable<bits<8> O, Format F, X86VectorVTInfo _,
                           dag Outs, dag Ins, string OpcodeStr,
                           string AttSrcAsm, string IntelSrcAsm,
                           dag RHS,
                           bit IsCommutable = 0, bit IsKCommutable = 0,
                           bit IsKZCommutable = IsCommutable,
                           SDPatternOperator Select = vselect_mask,
                           string ClobberConstraint = ""> :
   AVX512_maskable_common<O, F, _, Outs, Ins,
                          !con((ins _.RC:$src0, _.KRCWM:$mask), Ins),
                          !con((ins _.KRCWM:$mask), Ins),
                          OpcodeStr, AttSrcAsm, IntelSrcAsm, RHS,
                          (Select _.KRCWM:$mask, RHS, _.RC:$src0),
                          Select, "$src0 = $dst", IsCommutable, IsKCommutable,
                          IsKZCommutable, ClobberConstraint>;

// This multiclass generates the unconditional/non-masking, the masking and
// the zero-masking variant of the scalar instruction.
multiclass AVX512_maskable_scalar<bits<8> O, Format F, X86VectorVTInfo _,
                           dag Outs, dag Ins, string OpcodeStr,
                           string AttSrcAsm, string IntelSrcAsm,
                           dag RHS> :
   AVX512_maskable<O, F, _, Outs, Ins, OpcodeStr, AttSrcAsm, IntelSrcAsm,
                   RHS, 0, 0, 0, X86selects_mask>;

// Similar to AVX512_maskable but in this case one of the source operands
// ($src1) is already tied to $dst so we just use that for the preserved
// vector elements.  NOTE that the NonTiedIns (the ins dag) should exclude
// $src1.
multiclass AVX512_maskable_3src<bits<8> O, Format F, X86VectorVTInfo _,
                                dag Outs, dag NonTiedIns, string OpcodeStr,
                                string AttSrcAsm, string IntelSrcAsm,
                                dag RHS,
                                bit IsCommutable = 0,
                                bit IsKCommutable = 0,
                                SDPatternOperator Select = vselect_mask,
                                bit MaskOnly = 0> :
   AVX512_maskable_common<O, F, _, Outs,
                          !con((ins _.RC:$src1), NonTiedIns),
                          !con((ins _.RC:$src1, _.KRCWM:$mask), NonTiedIns),
                          !con((ins _.RC:$src1, _.KRCWM:$mask), NonTiedIns),
                          OpcodeStr, AttSrcAsm, IntelSrcAsm,
                          !if(MaskOnly, (null_frag), RHS),
                          (Select _.KRCWM:$mask, RHS, _.RC:$src1),
                          Select, "", IsCommutable, IsKCommutable>;

// Similar to AVX512_maskable_3src but in this case the input VT for the tied
// operand differs from the output VT. This requires a bitconvert on
// the preserved vector going into the vselect.
// NOTE: The unmasked pattern is disabled.
multiclass AVX512_maskable_3src_cast<bits<8> O, Format F, X86VectorVTInfo OutVT,
                                     X86VectorVTInfo InVT,
                                     dag Outs, dag NonTiedIns, string OpcodeStr,
                                     string AttSrcAsm, string IntelSrcAsm,
                                     dag RHS, bit IsCommutable = 0> :
   AVX512_maskable_common<O, F, OutVT, Outs,
                          !con((ins InVT.RC:$src1), NonTiedIns),
                          !con((ins InVT.RC:$src1, InVT.KRCWM:$mask), NonTiedIns),
                          !con((ins InVT.RC:$src1, InVT.KRCWM:$mask), NonTiedIns),
                          OpcodeStr, AttSrcAsm, IntelSrcAsm, (null_frag),
                          (vselect_mask InVT.KRCWM:$mask, RHS,
                           (bitconvert InVT.RC:$src1)),
                           vselect_mask, "", IsCommutable>;

multiclass AVX512_maskable_3src_scalar<bits<8> O, Format F, X86VectorVTInfo _,
                                     dag Outs, dag NonTiedIns, string OpcodeStr,
                                     string AttSrcAsm, string IntelSrcAsm,
                                     dag RHS,
                                     bit IsCommutable = 0,
                                     bit IsKCommutable = 0,
                                     bit MaskOnly = 0> :
   AVX512_maskable_3src<O, F, _, Outs, NonTiedIns, OpcodeStr, AttSrcAsm,
                        IntelSrcAsm, RHS, IsCommutable, IsKCommutable,
                        X86selects_mask, MaskOnly>;

multiclass AVX512_maskable_in_asm<bits<8> O, Format F, X86VectorVTInfo _,
                                  dag Outs, dag Ins,
                                  string OpcodeStr,
                                  string AttSrcAsm, string IntelSrcAsm,
                                  list<dag> Pattern> :
   AVX512_maskable_custom<O, F, Outs, Ins,
                          !con((ins _.RC:$src0, _.KRCWM:$mask), Ins),
                          !con((ins _.KRCWM:$mask), Ins),
                          OpcodeStr, AttSrcAsm, IntelSrcAsm, Pattern, [], [],
                          "$src0 = $dst">;

multiclass AVX512_maskable_3src_in_asm<bits<8> O, Format F, X86VectorVTInfo _,
                                       dag Outs, dag NonTiedIns,
                                       string OpcodeStr,
                                       string AttSrcAsm, string IntelSrcAsm,
                                       list<dag> Pattern> :
   AVX512_maskable_custom<O, F, Outs,
                          !con((ins _.RC:$src1), NonTiedIns),
                          !con((ins _.RC:$src1, _.KRCWM:$mask), NonTiedIns),
                          !con((ins _.RC:$src1, _.KRCWM:$mask), NonTiedIns),
                          OpcodeStr, AttSrcAsm, IntelSrcAsm, Pattern, [], [],
                          "">;

// Instruction with mask that puts result in mask register,
// like "compare" and "vptest"
multiclass AVX512_maskable_custom_cmp<bits<8> O, Format F,
                                  dag Outs,
                                  dag Ins, dag MaskingIns,
                                  string OpcodeStr,
                                  string AttSrcAsm, string IntelSrcAsm,
                                  list<dag> Pattern,
                                  list<dag> MaskingPattern,
                                  bit IsCommutable = 0> {
    let isCommutable = IsCommutable in {
    def NAME: AVX512<O, F, Outs, Ins,
                       OpcodeStr#"\t{"#AttSrcAsm#", $dst|"#
                                     "$dst, "#IntelSrcAsm#"}",
                       Pattern>;

    def NAME#k: AVX512<O, F, Outs, MaskingIns,
                       OpcodeStr#"\t{"#AttSrcAsm#", $dst {${mask}}|"#
                                     "$dst {${mask}}, "#IntelSrcAsm#"}",
                       MaskingPattern>, EVEX_K;
    }
}

multiclass AVX512_maskable_common_cmp<bits<8> O, Format F, X86VectorVTInfo _,
                                  dag Outs,
                                  dag Ins, dag MaskingIns,
                                  string OpcodeStr,
                                  string AttSrcAsm, string IntelSrcAsm,
                                  dag RHS, dag MaskingRHS,
                                  bit IsCommutable = 0> :
  AVX512_maskable_custom_cmp<O, F, Outs, Ins, MaskingIns, OpcodeStr,
                         AttSrcAsm, IntelSrcAsm,
                         [(set _.KRC:$dst, RHS)],
                         [(set _.KRC:$dst, MaskingRHS)], IsCommutable>;

multiclass AVX512_maskable_cmp<bits<8> O, Format F, X86VectorVTInfo _,
                           dag Outs, dag Ins, string OpcodeStr,
                           string AttSrcAsm, string IntelSrcAsm,
                           dag RHS, dag RHS_su, bit IsCommutable = 0> :
   AVX512_maskable_common_cmp<O, F, _, Outs, Ins,
                          !con((ins _.KRCWM:$mask), Ins),
                          OpcodeStr, AttSrcAsm, IntelSrcAsm, RHS,
                          (and _.KRCWM:$mask, RHS_su), IsCommutable>;

// Used by conversion instructions.
multiclass AVX512_maskable_cvt<bits<8> O, Format F, X86VectorVTInfo _,
                                  dag Outs,
                                  dag Ins, dag MaskingIns, dag ZeroMaskingIns,
                                  string OpcodeStr,
                                  string AttSrcAsm, string IntelSrcAsm,
                                  dag RHS, dag MaskingRHS, dag ZeroMaskingRHS> :
  AVX512_maskable_custom<O, F, Outs, Ins, MaskingIns, ZeroMaskingIns, OpcodeStr,
                         AttSrcAsm, IntelSrcAsm,
                         [(set _.RC:$dst, RHS)],
                         [(set _.RC:$dst, MaskingRHS)],
                         [(set _.RC:$dst, ZeroMaskingRHS)],
                         "$src0 = $dst">;

multiclass AVX512_maskable_fma<bits<8> O, Format F, X86VectorVTInfo _,
                               dag Outs, dag NonTiedIns, string OpcodeStr,
                               string AttSrcAsm, string IntelSrcAsm,
                               dag RHS, dag MaskingRHS, bit IsCommutable,
                               bit IsKCommutable> :
   AVX512_maskable_custom<O, F, Outs,
                          !con((ins _.RC:$src1), NonTiedIns),
                          !con((ins _.RC:$src1, _.KRCWM:$mask), NonTiedIns),
                          !con((ins _.RC:$src1, _.KRCWM:$mask), NonTiedIns),
                          OpcodeStr, AttSrcAsm, IntelSrcAsm,
                          [(set _.RC:$dst, RHS)],
                          [(set _.RC:$dst,
                            (vselect_mask _.KRCWM:$mask, MaskingRHS, _.RC:$src1))],
                          [(set _.RC:$dst,
                            (vselect_mask _.KRCWM:$mask, MaskingRHS, _.ImmAllZerosV))],
                          "", IsCommutable, IsKCommutable>;

// Alias instruction that maps zero vector to pxor / xorp* for AVX-512.
// This is expanded by ExpandPostRAPseudos to an xorps / vxorps, and then
// swizzled by ExecutionDomainFix to pxor.
// We set canFoldAsLoad because this can be converted to a constant-pool
// load of an all-zeros value if folding it would be beneficial.
let isReMaterializable = 1, isAsCheapAsAMove = 1, canFoldAsLoad = 1,
    isPseudo = 1, Predicates = [HasAVX512], SchedRW = [WriteZero] in {
def AVX512_512_SET0 : I<0, Pseudo, (outs VR512:$dst), (ins), "",
               [(set VR512:$dst, (v16i32 immAllZerosV))]>;
def AVX512_512_SETALLONES : I<0, Pseudo, (outs VR512:$dst), (ins), "",
               [(set VR512:$dst, (v16i32 immAllOnesV))]>;
}

let Predicates = [HasAVX512] in {
def : Pat<(v64i8 immAllZerosV), (AVX512_512_SET0)>;
def : Pat<(v32i16 immAllZerosV), (AVX512_512_SET0)>;
def : Pat<(v8i64 immAllZerosV), (AVX512_512_SET0)>;
def : Pat<(v32f16 immAllZerosV), (AVX512_512_SET0)>;
def : Pat<(v16f32 immAllZerosV), (AVX512_512_SET0)>;
def : Pat<(v8f64 immAllZerosV), (AVX512_512_SET0)>;
}

// Alias instructions that allow VPTERNLOG to be used with a mask to create
// a mix of all ones and all zeros elements. This is done this way to force
// the same register to be used as input for all three sources.
let isPseudo = 1, Predicates = [HasAVX512], SchedRW = [WriteVecALU] in {
def AVX512_512_SEXT_MASK_32 : I<0, Pseudo, (outs VR512:$dst),
                                (ins VK16WM:$mask), "",
                           [(set VR512:$dst, (vselect (v16i1 VK16WM:$mask),
                                                      (v16i32 immAllOnesV),
                                                      (v16i32 immAllZerosV)))]>;
def AVX512_512_SEXT_MASK_64 : I<0, Pseudo, (outs VR512:$dst),
                                (ins VK8WM:$mask), "",
                [(set VR512:$dst, (vselect (v8i1 VK8WM:$mask),
                                           (v8i64 immAllOnesV),
                                           (v8i64 immAllZerosV)))]>;
}

let isReMaterializable = 1, isAsCheapAsAMove = 1, canFoldAsLoad = 1,
    isPseudo = 1, Predicates = [HasAVX512], SchedRW = [WriteZero] in {
def AVX512_128_SET0 : I<0, Pseudo, (outs VR128X:$dst), (ins), "",
               [(set VR128X:$dst, (v4i32 immAllZerosV))]>;
def AVX512_256_SET0 : I<0, Pseudo, (outs VR256X:$dst), (ins), "",
               [(set VR256X:$dst, (v8i32 immAllZerosV))]>;
}

let Predicates = [HasAVX512] in {
def : Pat<(v8i16 immAllZerosV), (AVX512_128_SET0)>;
def : Pat<(v16i8 immAllZerosV), (AVX512_128_SET0)>;
def : Pat<(v2i64 immAllZerosV), (AVX512_128_SET0)>;
def : Pat<(v8f16 immAllZerosV), (AVX512_128_SET0)>;
def : Pat<(v4f32 immAllZerosV), (AVX512_128_SET0)>;
def : Pat<(v2f64 immAllZerosV), (AVX512_128_SET0)>;
def : Pat<(v32i8 immAllZerosV), (AVX512_256_SET0)>;
def : Pat<(v16i16 immAllZerosV), (AVX512_256_SET0)>;
def : Pat<(v4i64 immAllZerosV), (AVX512_256_SET0)>;
def : Pat<(v16f16 immAllZerosV), (AVX512_256_SET0)>;
def : Pat<(v8f32 immAllZerosV), (AVX512_256_SET0)>;
def : Pat<(v4f64 immAllZerosV), (AVX512_256_SET0)>;
}

// Alias instructions that map fld0 to xorps for sse or vxorps for avx.
// This is expanded by ExpandPostRAPseudos.
let isReMaterializable = 1, isAsCheapAsAMove = 1, canFoldAsLoad = 1,
    isPseudo = 1, SchedRW = [WriteZero], Predicates = [HasAVX512] in {
  def AVX512_FsFLD0SH : I<0, Pseudo, (outs FR16X:$dst), (ins), "",
                          [(set FR16X:$dst, fp16imm0)]>;
  def AVX512_FsFLD0SS : I<0, Pseudo, (outs FR32X:$dst), (ins), "",
                          [(set FR32X:$dst, fp32imm0)]>;
  def AVX512_FsFLD0SD : I<0, Pseudo, (outs FR64X:$dst), (ins), "",
                          [(set FR64X:$dst, fp64imm0)]>;
  def AVX512_FsFLD0F128 : I<0, Pseudo, (outs VR128X:$dst), (ins), "",
                            [(set VR128X:$dst, fp128imm0)]>;
}

//===----------------------------------------------------------------------===//
// AVX-512 - VECTOR INSERT
//

// Supports two different pattern operators for mask and unmasked ops. Allows
// null_frag to be passed for one.
multiclass vinsert_for_size_split<int Opcode, X86VectorVTInfo From,
                                  X86VectorVTInfo To,
                                  SDPatternOperator vinsert_insert,
                                  SDPatternOperator vinsert_for_mask,
                                  X86FoldableSchedWrite sched> {
  let hasSideEffects = 0, ExeDomain = To.ExeDomain in {
    defm rr : AVX512_maskable_split<Opcode, MRMSrcReg, To, (outs To.RC:$dst),
                   (ins To.RC:$src1, From.RC:$src2, u8imm:$src3),
                   "vinsert" # From.EltTypeName # "x" # From.NumElts,
                   "$src3, $src2, $src1", "$src1, $src2, $src3",
                   (vinsert_insert:$src3 (To.VT To.RC:$src1),
                                         (From.VT From.RC:$src2),
                                         (iPTR imm)),
                   (vinsert_for_mask:$src3 (To.VT To.RC:$src1),
                                           (From.VT From.RC:$src2),
                                           (iPTR imm))>,
                   AVX512AIi8Base, EVEX, VVVV, Sched<[sched]>;
    let mayLoad = 1 in
    defm rm : AVX512_maskable_split<Opcode, MRMSrcMem, To, (outs To.RC:$dst),
                   (ins To.RC:$src1, From.MemOp:$src2, u8imm:$src3),
                   "vinsert" # From.EltTypeName # "x" # From.NumElts,
                   "$src3, $src2, $src1", "$src1, $src2, $src3",
                   (vinsert_insert:$src3 (To.VT To.RC:$src1),
                               (From.VT (From.LdFrag addr:$src2)),
                               (iPTR imm)),
                   (vinsert_for_mask:$src3 (To.VT To.RC:$src1),
                               (From.VT (From.LdFrag addr:$src2)),
                               (iPTR imm))>, AVX512AIi8Base, EVEX, VVVV,
                   EVEX_CD8<From.EltSize, From.CD8TupleForm>,
                   Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

// Passes the same pattern operator for masked and unmasked ops.
multiclass vinsert_for_size<int Opcode, X86VectorVTInfo From,
                            X86VectorVTInfo To,
                            SDPatternOperator vinsert_insert,
                            X86FoldableSchedWrite sched> :
  vinsert_for_size_split<Opcode, From, To, vinsert_insert, vinsert_insert, sched>;

multiclass vinsert_for_size_lowering<string InstrStr, X86VectorVTInfo From,
                       X86VectorVTInfo To, PatFrag vinsert_insert,
                       SDNodeXForm INSERT_get_vinsert_imm , list<Predicate> p> {
  let Predicates = p in {
    def : Pat<(vinsert_insert:$ins
                     (To.VT To.RC:$src1), (From.VT From.RC:$src2), (iPTR imm)),
              (To.VT (!cast<Instruction>(InstrStr#"rr")
                     To.RC:$src1, From.RC:$src2,
                     (INSERT_get_vinsert_imm To.RC:$ins)))>;

    def : Pat<(vinsert_insert:$ins
                  (To.VT To.RC:$src1),
                  (From.VT (From.LdFrag addr:$src2)),
                  (iPTR imm)),
              (To.VT (!cast<Instruction>(InstrStr#"rm")
                  To.RC:$src1, addr:$src2,
                  (INSERT_get_vinsert_imm To.RC:$ins)))>;
  }
}

multiclass vinsert_for_type<ValueType EltVT32, int Opcode128,
                            ValueType EltVT64, int Opcode256,
                            X86FoldableSchedWrite sched> {

  let Predicates = [HasVLX] in
    defm NAME # "32x4Z256" : vinsert_for_size<Opcode128,
                                 X86VectorVTInfo< 4, EltVT32, VR128X>,
                                 X86VectorVTInfo< 8, EltVT32, VR256X>,
                                 vinsert128_insert, sched>, EVEX_V256;

  defm NAME # "32x4Z" : vinsert_for_size<Opcode128,
                                 X86VectorVTInfo< 4, EltVT32, VR128X>,
                                 X86VectorVTInfo<16, EltVT32, VR512>,
                                 vinsert128_insert, sched>, EVEX_V512;

  defm NAME # "64x4Z" : vinsert_for_size<Opcode256,
                                 X86VectorVTInfo< 4, EltVT64, VR256X>,
                                 X86VectorVTInfo< 8, EltVT64, VR512>,
                                 vinsert256_insert, sched>, REX_W, EVEX_V512;

  // Even with DQI we'd like to only use these instructions for masking.
  let Predicates = [HasVLX, HasDQI] in
    defm NAME # "64x2Z256" : vinsert_for_size_split<Opcode128,
                                   X86VectorVTInfo< 2, EltVT64, VR128X>,
                                   X86VectorVTInfo< 4, EltVT64, VR256X>,
                                   null_frag, vinsert128_insert, sched>,
                                   EVEX_V256, REX_W;

  // Even with DQI we'd like to only use these instructions for masking.
  let Predicates = [HasDQI] in {
    defm NAME # "64x2Z" : vinsert_for_size_split<Opcode128,
                                 X86VectorVTInfo< 2, EltVT64, VR128X>,
                                 X86VectorVTInfo< 8, EltVT64, VR512>,
                                 null_frag, vinsert128_insert, sched>,
                                 REX_W, EVEX_V512;

    defm NAME # "32x8Z" : vinsert_for_size_split<Opcode256,
                                   X86VectorVTInfo< 8, EltVT32, VR256X>,
                                   X86VectorVTInfo<16, EltVT32, VR512>,
                                   null_frag, vinsert256_insert, sched>,
                                   EVEX_V512;
  }
}

// FIXME: Is there a better scheduler class for VINSERTF/VINSERTI?
defm VINSERTF : vinsert_for_type<f32, 0x18, f64, 0x1a, WriteFShuffle256>;
defm VINSERTI : vinsert_for_type<i32, 0x38, i64, 0x3a, WriteShuffle256>;

// Codegen pattern with the alternative types,
// Even with AVX512DQ we'll still use these for unmasked operations.
defm : vinsert_for_size_lowering<"VINSERTF32x4Z256", v2f64x_info, v4f64x_info,
              vinsert128_insert, INSERT_get_vinsert128_imm, [HasVLX]>;
defm : vinsert_for_size_lowering<"VINSERTI32x4Z256", v2i64x_info, v4i64x_info,
              vinsert128_insert, INSERT_get_vinsert128_imm, [HasVLX]>;

defm : vinsert_for_size_lowering<"VINSERTF32x4Z", v2f64x_info, v8f64_info,
              vinsert128_insert, INSERT_get_vinsert128_imm, [HasAVX512]>;
defm : vinsert_for_size_lowering<"VINSERTI32x4Z", v2i64x_info, v8i64_info,
              vinsert128_insert, INSERT_get_vinsert128_imm, [HasAVX512]>;

defm : vinsert_for_size_lowering<"VINSERTF64x4Z", v8f32x_info, v16f32_info,
              vinsert256_insert, INSERT_get_vinsert256_imm, [HasAVX512]>;
defm : vinsert_for_size_lowering<"VINSERTI64x4Z", v8i32x_info, v16i32_info,
              vinsert256_insert, INSERT_get_vinsert256_imm, [HasAVX512]>;

// Codegen pattern with the alternative types insert VEC128 into VEC256
defm : vinsert_for_size_lowering<"VINSERTI32x4Z256", v8i16x_info, v16i16x_info,
              vinsert128_insert, INSERT_get_vinsert128_imm, [HasVLX]>;
defm : vinsert_for_size_lowering<"VINSERTI32x4Z256", v16i8x_info, v32i8x_info,
              vinsert128_insert, INSERT_get_vinsert128_imm, [HasVLX]>;
defm : vinsert_for_size_lowering<"VINSERTF32x4Z256", v8f16x_info, v16f16x_info,
              vinsert128_insert, INSERT_get_vinsert128_imm, [HasVLX]>;
defm : vinsert_for_size_lowering<"VINSERTF32x4Z256", v8bf16x_info, v16bf16x_info,
              vinsert128_insert, INSERT_get_vinsert128_imm, [HasVLX]>;
// Codegen pattern with the alternative types insert VEC128 into VEC512
defm : vinsert_for_size_lowering<"VINSERTI32x4Z", v8i16x_info, v32i16_info,
              vinsert128_insert, INSERT_get_vinsert128_imm, [HasAVX512]>;
defm : vinsert_for_size_lowering<"VINSERTI32x4Z", v16i8x_info, v64i8_info,
               vinsert128_insert, INSERT_get_vinsert128_imm, [HasAVX512]>;
defm : vinsert_for_size_lowering<"VINSERTF32x4Z", v8f16x_info, v32f16_info,
              vinsert128_insert, INSERT_get_vinsert128_imm, [HasAVX512]>;
defm : vinsert_for_size_lowering<"VINSERTF32x4Z", v8bf16x_info, v32bf16_info,
              vinsert128_insert, INSERT_get_vinsert128_imm, [HasAVX512]>;
// Codegen pattern with the alternative types insert VEC256 into VEC512
defm : vinsert_for_size_lowering<"VINSERTI64x4Z", v16i16x_info, v32i16_info,
              vinsert256_insert, INSERT_get_vinsert256_imm, [HasAVX512]>;
defm : vinsert_for_size_lowering<"VINSERTI64x4Z", v32i8x_info, v64i8_info,
              vinsert256_insert, INSERT_get_vinsert256_imm, [HasAVX512]>;
defm : vinsert_for_size_lowering<"VINSERTF64x4Z", v16f16x_info, v32f16_info,
              vinsert256_insert, INSERT_get_vinsert256_imm, [HasAVX512]>;
defm : vinsert_for_size_lowering<"VINSERTF64x4Z", v16bf16x_info, v32bf16_info,
              vinsert256_insert, INSERT_get_vinsert256_imm, [HasAVX512]>;


multiclass vinsert_for_mask_cast<string InstrStr, X86VectorVTInfo From,
                                 X86VectorVTInfo To, X86VectorVTInfo Cast,
                                 PatFrag vinsert_insert,
                                 SDNodeXForm INSERT_get_vinsert_imm,
                                 list<Predicate> p> {
let Predicates = p in {
  def : Pat<(Cast.VT
             (vselect_mask Cast.KRCWM:$mask,
                           (bitconvert
                            (vinsert_insert:$ins (To.VT To.RC:$src1),
                                                 (From.VT From.RC:$src2),
                                                 (iPTR imm))),
                           Cast.RC:$src0)),
            (!cast<Instruction>(InstrStr#"rrk")
             Cast.RC:$src0, Cast.KRCWM:$mask, To.RC:$src1, From.RC:$src2,
             (INSERT_get_vinsert_imm To.RC:$ins))>;
  def : Pat<(Cast.VT
             (vselect_mask Cast.KRCWM:$mask,
                           (bitconvert
                            (vinsert_insert:$ins (To.VT To.RC:$src1),
                                                 (From.VT
                                                  (bitconvert
                                                   (From.LdFrag addr:$src2))),
                                                 (iPTR imm))),
                           Cast.RC:$src0)),
            (!cast<Instruction>(InstrStr#"rmk")
             Cast.RC:$src0, Cast.KRCWM:$mask, To.RC:$src1, addr:$src2,
             (INSERT_get_vinsert_imm To.RC:$ins))>;

  def : Pat<(Cast.VT
             (vselect_mask Cast.KRCWM:$mask,
                           (bitconvert
                            (vinsert_insert:$ins (To.VT To.RC:$src1),
                                                 (From.VT From.RC:$src2),
                                                 (iPTR imm))),
                           Cast.ImmAllZerosV)),
            (!cast<Instruction>(InstrStr#"rrkz")
             Cast.KRCWM:$mask, To.RC:$src1, From.RC:$src2,
             (INSERT_get_vinsert_imm To.RC:$ins))>;
  def : Pat<(Cast.VT
             (vselect_mask Cast.KRCWM:$mask,
                           (bitconvert
                            (vinsert_insert:$ins (To.VT To.RC:$src1),
                                                 (From.VT (From.LdFrag addr:$src2)),
                                                 (iPTR imm))),
                           Cast.ImmAllZerosV)),
            (!cast<Instruction>(InstrStr#"rmkz")
             Cast.KRCWM:$mask, To.RC:$src1, addr:$src2,
             (INSERT_get_vinsert_imm To.RC:$ins))>;
}
}

defm : vinsert_for_mask_cast<"VINSERTF32x4Z256", v2f64x_info, v4f64x_info,
                             v8f32x_info, vinsert128_insert,
                             INSERT_get_vinsert128_imm, [HasVLX]>;
defm : vinsert_for_mask_cast<"VINSERTF64x2Z256", v4f32x_info, v8f32x_info,
                             v4f64x_info, vinsert128_insert,
                             INSERT_get_vinsert128_imm, [HasDQI, HasVLX]>;

defm : vinsert_for_mask_cast<"VINSERTI32x4Z256", v2i64x_info, v4i64x_info,
                             v8i32x_info, vinsert128_insert,
                             INSERT_get_vinsert128_imm, [HasVLX]>;
defm : vinsert_for_mask_cast<"VINSERTI32x4Z256", v8i16x_info, v16i16x_info,
                             v8i32x_info, vinsert128_insert,
                             INSERT_get_vinsert128_imm, [HasVLX]>;
defm : vinsert_for_mask_cast<"VINSERTI32x4Z256", v16i8x_info, v32i8x_info,
                             v8i32x_info, vinsert128_insert,
                             INSERT_get_vinsert128_imm, [HasVLX]>;
defm : vinsert_for_mask_cast<"VINSERTF64x2Z256", v4i32x_info, v8i32x_info,
                             v4i64x_info, vinsert128_insert,
                             INSERT_get_vinsert128_imm, [HasDQI, HasVLX]>;
defm : vinsert_for_mask_cast<"VINSERTF64x2Z256", v8i16x_info, v16i16x_info,
                             v4i64x_info, vinsert128_insert,
                             INSERT_get_vinsert128_imm, [HasDQI, HasVLX]>;
defm : vinsert_for_mask_cast<"VINSERTF64x2Z256", v16i8x_info, v32i8x_info,
                             v4i64x_info, vinsert128_insert,
                             INSERT_get_vinsert128_imm, [HasDQI, HasVLX]>;

defm : vinsert_for_mask_cast<"VINSERTF32x4Z", v2f64x_info, v8f64_info,
                             v16f32_info, vinsert128_insert,
                             INSERT_get_vinsert128_imm, [HasAVX512]>;
defm : vinsert_for_mask_cast<"VINSERTF64x2Z", v4f32x_info, v16f32_info,
                             v8f64_info, vinsert128_insert,
                             INSERT_get_vinsert128_imm, [HasDQI]>;

defm : vinsert_for_mask_cast<"VINSERTI32x4Z", v2i64x_info, v8i64_info,
                             v16i32_info, vinsert128_insert,
                             INSERT_get_vinsert128_imm, [HasAVX512]>;
defm : vinsert_for_mask_cast<"VINSERTI32x4Z", v8i16x_info, v32i16_info,
                             v16i32_info, vinsert128_insert,
                             INSERT_get_vinsert128_imm, [HasAVX512]>;
defm : vinsert_for_mask_cast<"VINSERTI32x4Z", v16i8x_info, v64i8_info,
                             v16i32_info, vinsert128_insert,
                             INSERT_get_vinsert128_imm, [HasAVX512]>;
defm : vinsert_for_mask_cast<"VINSERTI64x2Z", v4i32x_info, v16i32_info,
                             v8i64_info, vinsert128_insert,
                             INSERT_get_vinsert128_imm, [HasDQI]>;
defm : vinsert_for_mask_cast<"VINSERTI64x2Z", v8i16x_info, v32i16_info,
                             v8i64_info, vinsert128_insert,
                             INSERT_get_vinsert128_imm, [HasDQI]>;
defm : vinsert_for_mask_cast<"VINSERTI64x2Z", v16i8x_info, v64i8_info,
                             v8i64_info, vinsert128_insert,
                             INSERT_get_vinsert128_imm, [HasDQI]>;

defm : vinsert_for_mask_cast<"VINSERTF32x8Z", v4f64x_info, v8f64_info,
                             v16f32_info, vinsert256_insert,
                             INSERT_get_vinsert256_imm, [HasDQI]>;
defm : vinsert_for_mask_cast<"VINSERTF64x4Z", v8f32x_info, v16f32_info,
                             v8f64_info, vinsert256_insert,
                             INSERT_get_vinsert256_imm, [HasAVX512]>;

defm : vinsert_for_mask_cast<"VINSERTI32x8Z", v4i64x_info, v8i64_info,
                             v16i32_info, vinsert256_insert,
                             INSERT_get_vinsert256_imm, [HasDQI]>;
defm : vinsert_for_mask_cast<"VINSERTI32x8Z", v16i16x_info, v32i16_info,
                             v16i32_info, vinsert256_insert,
                             INSERT_get_vinsert256_imm, [HasDQI]>;
defm : vinsert_for_mask_cast<"VINSERTI32x8Z", v32i8x_info, v64i8_info,
                             v16i32_info, vinsert256_insert,
                             INSERT_get_vinsert256_imm, [HasDQI]>;
defm : vinsert_for_mask_cast<"VINSERTI64x4Z", v8i32x_info, v16i32_info,
                             v8i64_info, vinsert256_insert,
                             INSERT_get_vinsert256_imm, [HasAVX512]>;
defm : vinsert_for_mask_cast<"VINSERTI64x4Z", v16i16x_info, v32i16_info,
                             v8i64_info, vinsert256_insert,
                             INSERT_get_vinsert256_imm, [HasAVX512]>;
defm : vinsert_for_mask_cast<"VINSERTI64x4Z", v32i8x_info, v64i8_info,
                             v8i64_info, vinsert256_insert,
                             INSERT_get_vinsert256_imm, [HasAVX512]>;

// vinsertps - insert f32 to XMM
let ExeDomain = SSEPackedSingle in {
let isCommutable = 1 in
def VINSERTPSZrr : AVX512AIi8<0x21, MRMSrcReg, (outs VR128X:$dst),
      (ins VR128X:$src1, VR128X:$src2, u8imm:$src3),
      "vinsertps\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}",
      [(set VR128X:$dst, (X86insertps VR128X:$src1, VR128X:$src2, timm:$src3))]>,
      EVEX, VVVV, Sched<[SchedWriteFShuffle.XMM]>;
def VINSERTPSZrm: AVX512AIi8<0x21, MRMSrcMem, (outs VR128X:$dst),
      (ins VR128X:$src1, f32mem:$src2, u8imm:$src3),
      "vinsertps\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}",
      [(set VR128X:$dst, (X86insertps VR128X:$src1,
                          (v4f32 (scalar_to_vector (loadf32 addr:$src2))),
                          timm:$src3))]>,
      EVEX, VVVV, EVEX_CD8<32, CD8VT1>,
      Sched<[SchedWriteFShuffle.XMM.Folded, SchedWriteFShuffle.XMM.ReadAfterFold]>;
}

//===----------------------------------------------------------------------===//
// AVX-512 VECTOR EXTRACT
//---

// Supports two different pattern operators for mask and unmasked ops. Allows
// null_frag to be passed for one.
multiclass vextract_for_size_split<int Opcode,
                                   X86VectorVTInfo From, X86VectorVTInfo To,
                                   SDPatternOperator vextract_extract,
                                   SDPatternOperator vextract_for_mask,
                                   SchedWrite SchedRR, SchedWrite SchedMR> {

  let hasSideEffects = 0, ExeDomain = To.ExeDomain in {
    defm rr : AVX512_maskable_split<Opcode, MRMDestReg, To, (outs To.RC:$dst),
                (ins From.RC:$src1, u8imm:$idx),
                "vextract" # To.EltTypeName # "x" # To.NumElts,
                "$idx, $src1", "$src1, $idx",
                (vextract_extract:$idx (From.VT From.RC:$src1), (iPTR imm)),
                (vextract_for_mask:$idx (From.VT From.RC:$src1), (iPTR imm))>,
                AVX512AIi8Base, EVEX, Sched<[SchedRR]>;

    def mr  : AVX512AIi8<Opcode, MRMDestMem, (outs),
                    (ins To.MemOp:$dst, From.RC:$src1, u8imm:$idx),
                    "vextract" # To.EltTypeName # "x" # To.NumElts #
                        "\t{$idx, $src1, $dst|$dst, $src1, $idx}",
                    [(store (To.VT (vextract_extract:$idx
                                    (From.VT From.RC:$src1), (iPTR imm))),
                             addr:$dst)]>, EVEX,
                    Sched<[SchedMR]>;

    let mayStore = 1, hasSideEffects = 0 in
    def mrk : AVX512AIi8<Opcode, MRMDestMem, (outs),
                    (ins To.MemOp:$dst, To.KRCWM:$mask,
                                        From.RC:$src1, u8imm:$idx),
                     "vextract" # To.EltTypeName # "x" # To.NumElts #
                          "\t{$idx, $src1, $dst {${mask}}|"
                          "$dst {${mask}}, $src1, $idx}", []>,
                    EVEX_K, EVEX, Sched<[SchedMR]>;
  }
}

// Passes the same pattern operator for masked and unmasked ops.
multiclass vextract_for_size<int Opcode, X86VectorVTInfo From,
                             X86VectorVTInfo To,
                             SDPatternOperator vextract_extract,
                             SchedWrite SchedRR, SchedWrite SchedMR> :
  vextract_for_size_split<Opcode, From, To, vextract_extract, vextract_extract, SchedRR, SchedMR>;

// Codegen pattern for the alternative types
multiclass vextract_for_size_lowering<string InstrStr, X86VectorVTInfo From,
                X86VectorVTInfo To, PatFrag vextract_extract,
                SDNodeXForm EXTRACT_get_vextract_imm, list<Predicate> p> {
  let Predicates = p in {
     def : Pat<(vextract_extract:$ext (From.VT From.RC:$src1), (iPTR imm)),
               (To.VT (!cast<Instruction>(InstrStr#"rr")
                          From.RC:$src1,
                          (EXTRACT_get_vextract_imm To.RC:$ext)))>;
     def : Pat<(store (To.VT (vextract_extract:$ext (From.VT From.RC:$src1),
                              (iPTR imm))), addr:$dst),
               (!cast<Instruction>(InstrStr#"mr") addr:$dst, From.RC:$src1,
                (EXTRACT_get_vextract_imm To.RC:$ext))>;
  }
}

multiclass vextract_for_type<ValueType EltVT32, int Opcode128,
                             ValueType EltVT64, int Opcode256,
                             SchedWrite SchedRR, SchedWrite SchedMR> {
  let Predicates = [HasAVX512] in {
    defm NAME # "32x4Z" : vextract_for_size<Opcode128,
                                   X86VectorVTInfo<16, EltVT32, VR512>,
                                   X86VectorVTInfo< 4, EltVT32, VR128X>,
                                   vextract128_extract, SchedRR, SchedMR>,
                                       EVEX_V512, EVEX_CD8<32, CD8VT4>;
    defm NAME # "64x4Z" : vextract_for_size<Opcode256,
                                   X86VectorVTInfo< 8, EltVT64, VR512>,
                                   X86VectorVTInfo< 4, EltVT64, VR256X>,
                                   vextract256_extract, SchedRR, SchedMR>,
                                       REX_W, EVEX_V512, EVEX_CD8<64, CD8VT4>;
  }
  let Predicates = [HasVLX] in
    defm NAME # "32x4Z256" : vextract_for_size<Opcode128,
                                 X86VectorVTInfo< 8, EltVT32, VR256X>,
                                 X86VectorVTInfo< 4, EltVT32, VR128X>,
                                 vextract128_extract, SchedRR, SchedMR>,
                                     EVEX_V256, EVEX_CD8<32, CD8VT4>;

  // Even with DQI we'd like to only use these instructions for masking.
  let Predicates = [HasVLX, HasDQI] in
    defm NAME # "64x2Z256" : vextract_for_size_split<Opcode128,
                                 X86VectorVTInfo< 4, EltVT64, VR256X>,
                                 X86VectorVTInfo< 2, EltVT64, VR128X>,
                                 null_frag, vextract128_extract, SchedRR, SchedMR>,
                                    EVEX_V256, EVEX_CD8<64, CD8VT2>, REX_W;

  // Even with DQI we'd like to only use these instructions for masking.
  let Predicates = [HasDQI] in {
    defm NAME # "64x2Z" : vextract_for_size_split<Opcode128,
                                 X86VectorVTInfo< 8, EltVT64, VR512>,
                                 X86VectorVTInfo< 2, EltVT64, VR128X>,
                                 null_frag, vextract128_extract, SchedRR, SchedMR>,
                                     REX_W, EVEX_V512, EVEX_CD8<64, CD8VT2>;
    defm NAME # "32x8Z" : vextract_for_size_split<Opcode256,
                                 X86VectorVTInfo<16, EltVT32, VR512>,
                                 X86VectorVTInfo< 8, EltVT32, VR256X>,
                                 null_frag, vextract256_extract, SchedRR, SchedMR>,
                                     EVEX_V512, EVEX_CD8<32, CD8VT8>;
  }
}

// TODO - replace WriteFStore/WriteVecStore with X86SchedWriteMoveLSWidths types.
defm VEXTRACTF : vextract_for_type<f32, 0x19, f64, 0x1b, WriteFShuffle256, WriteFStore>;
defm VEXTRACTI : vextract_for_type<i32, 0x39, i64, 0x3b, WriteShuffle256, WriteVecStore>;

// extract_subvector codegen patterns with the alternative types.
// Even with AVX512DQ we'll still use these for unmasked operations.
defm : vextract_for_size_lowering<"VEXTRACTF32x4Z", v8f64_info, v2f64x_info,
          vextract128_extract, EXTRACT_get_vextract128_imm, [HasAVX512]>;
defm : vextract_for_size_lowering<"VEXTRACTI32x4Z", v8i64_info, v2i64x_info,
          vextract128_extract, EXTRACT_get_vextract128_imm, [HasAVX512]>;

defm : vextract_for_size_lowering<"VEXTRACTF64x4Z", v16f32_info, v8f32x_info,
          vextract256_extract, EXTRACT_get_vextract256_imm, [HasAVX512]>;
defm : vextract_for_size_lowering<"VEXTRACTI64x4Z", v16i32_info, v8i32x_info,
          vextract256_extract, EXTRACT_get_vextract256_imm, [HasAVX512]>;

defm : vextract_for_size_lowering<"VEXTRACTF32x4Z256", v4f64x_info, v2f64x_info,
          vextract128_extract, EXTRACT_get_vextract128_imm, [HasVLX]>;
defm : vextract_for_size_lowering<"VEXTRACTI32x4Z256", v4i64x_info, v2i64x_info,
          vextract128_extract, EXTRACT_get_vextract128_imm, [HasVLX]>;

// Codegen pattern with the alternative types extract VEC128 from VEC256
defm : vextract_for_size_lowering<"VEXTRACTI32x4Z256", v16i16x_info, v8i16x_info,
          vextract128_extract, EXTRACT_get_vextract128_imm, [HasVLX]>;
defm : vextract_for_size_lowering<"VEXTRACTI32x4Z256", v32i8x_info, v16i8x_info,
          vextract128_extract, EXTRACT_get_vextract128_imm, [HasVLX]>;
defm : vextract_for_size_lowering<"VEXTRACTF32x4Z256", v16f16x_info, v8f16x_info,
          vextract128_extract, EXTRACT_get_vextract128_imm, [HasVLX]>;
defm : vextract_for_size_lowering<"VEXTRACTF32x4Z256", v16bf16x_info, v8bf16x_info,
          vextract128_extract, EXTRACT_get_vextract128_imm, [HasVLX]>;

// Codegen pattern with the alternative types extract VEC128 from VEC512
defm : vextract_for_size_lowering<"VEXTRACTI32x4Z", v32i16_info, v8i16x_info,
                 vextract128_extract, EXTRACT_get_vextract128_imm, [HasAVX512]>;
defm : vextract_for_size_lowering<"VEXTRACTI32x4Z", v64i8_info, v16i8x_info,
                 vextract128_extract, EXTRACT_get_vextract128_imm, [HasAVX512]>;
defm : vextract_for_size_lowering<"VEXTRACTF32x4Z", v32f16_info, v8f16x_info,
                 vextract128_extract, EXTRACT_get_vextract128_imm, [HasAVX512]>;
defm : vextract_for_size_lowering<"VEXTRACTF32x4Z", v32bf16_info, v8bf16x_info,
                 vextract128_extract, EXTRACT_get_vextract128_imm, [HasAVX512]>;
// Codegen pattern with the alternative types extract VEC256 from VEC512
defm : vextract_for_size_lowering<"VEXTRACTI64x4Z", v32i16_info, v16i16x_info,
                 vextract256_extract, EXTRACT_get_vextract256_imm, [HasAVX512]>;
defm : vextract_for_size_lowering<"VEXTRACTI64x4Z", v64i8_info, v32i8x_info,
                 vextract256_extract, EXTRACT_get_vextract256_imm, [HasAVX512]>;
defm : vextract_for_size_lowering<"VEXTRACTF64x4Z", v32f16_info, v16f16x_info,
                 vextract256_extract, EXTRACT_get_vextract256_imm, [HasAVX512]>;
defm : vextract_for_size_lowering<"VEXTRACTF64x4Z", v32bf16_info, v16bf16x_info,
                 vextract256_extract, EXTRACT_get_vextract256_imm, [HasAVX512]>;


// A 128-bit extract from bits [255:128] of a 512-bit vector should use a
// smaller extract to enable EVEX->VEX.
let Predicates = [NoVLX, HasEVEX512] in {
def : Pat<(v2i64 (extract_subvector (v8i64 VR512:$src), (iPTR 2))),
          (v2i64 (VEXTRACTI128rr
                  (v4i64 (EXTRACT_SUBREG (v8i64 VR512:$src), sub_ymm)),
                  (iPTR 1)))>;
def : Pat<(v2f64 (extract_subvector (v8f64 VR512:$src), (iPTR 2))),
          (v2f64 (VEXTRACTF128rr
                  (v4f64 (EXTRACT_SUBREG (v8f64 VR512:$src), sub_ymm)),
                  (iPTR 1)))>;
def : Pat<(v4i32 (extract_subvector (v16i32 VR512:$src), (iPTR 4))),
          (v4i32 (VEXTRACTI128rr
                  (v8i32 (EXTRACT_SUBREG (v16i32 VR512:$src), sub_ymm)),
                  (iPTR 1)))>;
def : Pat<(v4f32 (extract_subvector (v16f32 VR512:$src), (iPTR 4))),
          (v4f32 (VEXTRACTF128rr
                  (v8f32 (EXTRACT_SUBREG (v16f32 VR512:$src), sub_ymm)),
                  (iPTR 1)))>;
def : Pat<(v8i16 (extract_subvector (v32i16 VR512:$src), (iPTR 8))),
          (v8i16 (VEXTRACTI128rr
                  (v16i16 (EXTRACT_SUBREG (v32i16 VR512:$src), sub_ymm)),
                  (iPTR 1)))>;
def : Pat<(v8f16 (extract_subvector (v32f16 VR512:$src), (iPTR 8))),
          (v8f16 (VEXTRACTF128rr
                  (v16f16 (EXTRACT_SUBREG (v32f16 VR512:$src), sub_ymm)),
                  (iPTR 1)))>;
def : Pat<(v16i8 (extract_subvector (v64i8 VR512:$src), (iPTR 16))),
          (v16i8 (VEXTRACTI128rr
                  (v32i8 (EXTRACT_SUBREG (v64i8 VR512:$src), sub_ymm)),
                  (iPTR 1)))>;
}

// A 128-bit extract from bits [255:128] of a 512-bit vector should use a
// smaller extract to enable EVEX->VEX.
let Predicates = [HasVLX] in {
def : Pat<(v2i64 (extract_subvector (v8i64 VR512:$src), (iPTR 2))),
          (v2i64 (VEXTRACTI32x4Z256rr
                  (v4i64 (EXTRACT_SUBREG (v8i64 VR512:$src), sub_ymm)),
                  (iPTR 1)))>;
def : Pat<(v2f64 (extract_subvector (v8f64 VR512:$src), (iPTR 2))),
          (v2f64 (VEXTRACTF32x4Z256rr
                  (v4f64 (EXTRACT_SUBREG (v8f64 VR512:$src), sub_ymm)),
                  (iPTR 1)))>;
def : Pat<(v4i32 (extract_subvector (v16i32 VR512:$src), (iPTR 4))),
          (v4i32 (VEXTRACTI32x4Z256rr
                  (v8i32 (EXTRACT_SUBREG (v16i32 VR512:$src), sub_ymm)),
                  (iPTR 1)))>;
def : Pat<(v4f32 (extract_subvector (v16f32 VR512:$src), (iPTR 4))),
          (v4f32 (VEXTRACTF32x4Z256rr
                  (v8f32 (EXTRACT_SUBREG (v16f32 VR512:$src), sub_ymm)),
                  (iPTR 1)))>;
def : Pat<(v8i16 (extract_subvector (v32i16 VR512:$src), (iPTR 8))),
          (v8i16 (VEXTRACTI32x4Z256rr
                  (v16i16 (EXTRACT_SUBREG (v32i16 VR512:$src), sub_ymm)),
                  (iPTR 1)))>;
def : Pat<(v8f16 (extract_subvector (v32f16 VR512:$src), (iPTR 8))),
          (v8f16 (VEXTRACTF32x4Z256rr
                  (v16f16 (EXTRACT_SUBREG (v32f16 VR512:$src), sub_ymm)),
                  (iPTR 1)))>;
def : Pat<(v16i8 (extract_subvector (v64i8 VR512:$src), (iPTR 16))),
          (v16i8 (VEXTRACTI32x4Z256rr
                  (v32i8 (EXTRACT_SUBREG (v64i8 VR512:$src), sub_ymm)),
                  (iPTR 1)))>;
}


// Additional patterns for handling a bitcast between the vselect and the
// extract_subvector.
multiclass vextract_for_mask_cast<string InstrStr, X86VectorVTInfo From,
                                  X86VectorVTInfo To, X86VectorVTInfo Cast,
                                  PatFrag vextract_extract,
                                  SDNodeXForm EXTRACT_get_vextract_imm,
                                  list<Predicate> p> {
let Predicates = p in {
  def : Pat<(Cast.VT (vselect_mask Cast.KRCWM:$mask,
                                   (bitconvert
                                    (To.VT (vextract_extract:$ext
                                            (From.VT From.RC:$src), (iPTR imm)))),
                                   To.RC:$src0)),
            (Cast.VT (!cast<Instruction>(InstrStr#"rrk")
                      Cast.RC:$src0, Cast.KRCWM:$mask, From.RC:$src,
                      (EXTRACT_get_vextract_imm To.RC:$ext)))>;

  def : Pat<(Cast.VT (vselect_mask Cast.KRCWM:$mask,
                                   (bitconvert
                                    (To.VT (vextract_extract:$ext
                                            (From.VT From.RC:$src), (iPTR imm)))),
                                   Cast.ImmAllZerosV)),
            (Cast.VT (!cast<Instruction>(InstrStr#"rrkz")
                      Cast.KRCWM:$mask, From.RC:$src,
                      (EXTRACT_get_vextract_imm To.RC:$ext)))>;
}
}

defm : vextract_for_mask_cast<"VEXTRACTF32x4Z256", v4f64x_info, v2f64x_info,
                              v4f32x_info, vextract128_extract,
                              EXTRACT_get_vextract128_imm, [HasVLX]>;
defm : vextract_for_mask_cast<"VEXTRACTF64x2Z256", v8f32x_info, v4f32x_info,
                              v2f64x_info, vextract128_extract,
                              EXTRACT_get_vextract128_imm, [HasDQI, HasVLX]>;

defm : vextract_for_mask_cast<"VEXTRACTI32x4Z256", v4i64x_info, v2i64x_info,
                              v4i32x_info, vextract128_extract,
                              EXTRACT_get_vextract128_imm, [HasVLX]>;
defm : vextract_for_mask_cast<"VEXTRACTI32x4Z256", v16i16x_info, v8i16x_info,
                              v4i32x_info, vextract128_extract,
                              EXTRACT_get_vextract128_imm, [HasVLX]>;
defm : vextract_for_mask_cast<"VEXTRACTI32x4Z256", v32i8x_info, v16i8x_info,
                              v4i32x_info, vextract128_extract,
                              EXTRACT_get_vextract128_imm, [HasVLX]>;
defm : vextract_for_mask_cast<"VEXTRACTI64x2Z256", v8i32x_info, v4i32x_info,
                              v2i64x_info, vextract128_extract,
                              EXTRACT_get_vextract128_imm, [HasDQI, HasVLX]>;
defm : vextract_for_mask_cast<"VEXTRACTI64x2Z256", v16i16x_info, v8i16x_info,
                              v2i64x_info, vextract128_extract,
                              EXTRACT_get_vextract128_imm, [HasDQI, HasVLX]>;
defm : vextract_for_mask_cast<"VEXTRACTI64x2Z256", v32i8x_info, v16i8x_info,
                              v2i64x_info, vextract128_extract,
                              EXTRACT_get_vextract128_imm, [HasDQI, HasVLX]>;

defm : vextract_for_mask_cast<"VEXTRACTF32x4Z", v8f64_info, v2f64x_info,
                              v4f32x_info, vextract128_extract,
                              EXTRACT_get_vextract128_imm, [HasAVX512]>;
defm : vextract_for_mask_cast<"VEXTRACTF64x2Z", v16f32_info, v4f32x_info,
                              v2f64x_info, vextract128_extract,
                              EXTRACT_get_vextract128_imm, [HasDQI]>;

defm : vextract_for_mask_cast<"VEXTRACTI32x4Z", v8i64_info, v2i64x_info,
                              v4i32x_info, vextract128_extract,
                              EXTRACT_get_vextract128_imm, [HasAVX512]>;
defm : vextract_for_mask_cast<"VEXTRACTI32x4Z", v32i16_info, v8i16x_info,
                              v4i32x_info, vextract128_extract,
                              EXTRACT_get_vextract128_imm, [HasAVX512]>;
defm : vextract_for_mask_cast<"VEXTRACTI32x4Z", v64i8_info, v16i8x_info,
                              v4i32x_info, vextract128_extract,
                              EXTRACT_get_vextract128_imm, [HasAVX512]>;
defm : vextract_for_mask_cast<"VEXTRACTI64x2Z", v16i32_info, v4i32x_info,
                              v2i64x_info, vextract128_extract,
                              EXTRACT_get_vextract128_imm, [HasDQI]>;
defm : vextract_for_mask_cast<"VEXTRACTI64x2Z", v32i16_info, v8i16x_info,
                              v2i64x_info, vextract128_extract,
                              EXTRACT_get_vextract128_imm, [HasDQI]>;
defm : vextract_for_mask_cast<"VEXTRACTI64x2Z", v64i8_info, v16i8x_info,
                              v2i64x_info, vextract128_extract,
                              EXTRACT_get_vextract128_imm, [HasDQI]>;

defm : vextract_for_mask_cast<"VEXTRACTF32x8Z", v8f64_info, v4f64x_info,
                              v8f32x_info, vextract256_extract,
                              EXTRACT_get_vextract256_imm, [HasDQI]>;
defm : vextract_for_mask_cast<"VEXTRACTF64x4Z", v16f32_info, v8f32x_info,
                              v4f64x_info, vextract256_extract,
                              EXTRACT_get_vextract256_imm, [HasAVX512]>;

defm : vextract_for_mask_cast<"VEXTRACTI32x8Z", v8i64_info, v4i64x_info,
                              v8i32x_info, vextract256_extract,
                              EXTRACT_get_vextract256_imm, [HasDQI]>;
defm : vextract_for_mask_cast<"VEXTRACTI32x8Z", v32i16_info, v16i16x_info,
                              v8i32x_info, vextract256_extract,
                              EXTRACT_get_vextract256_imm, [HasDQI]>;
defm : vextract_for_mask_cast<"VEXTRACTI32x8Z", v64i8_info, v32i8x_info,
                              v8i32x_info, vextract256_extract,
                              EXTRACT_get_vextract256_imm, [HasDQI]>;
defm : vextract_for_mask_cast<"VEXTRACTI64x4Z", v16i32_info, v8i32x_info,
                              v4i64x_info, vextract256_extract,
                              EXTRACT_get_vextract256_imm, [HasAVX512]>;
defm : vextract_for_mask_cast<"VEXTRACTI64x4Z", v32i16_info, v16i16x_info,
                              v4i64x_info, vextract256_extract,
                              EXTRACT_get_vextract256_imm, [HasAVX512]>;
defm : vextract_for_mask_cast<"VEXTRACTI64x4Z", v64i8_info, v32i8x_info,
                              v4i64x_info, vextract256_extract,
                              EXTRACT_get_vextract256_imm, [HasAVX512]>;

// vextractps - extract 32 bits from XMM
def VEXTRACTPSZrr : AVX512AIi8<0x17, MRMDestReg, (outs GR32orGR64:$dst),
      (ins VR128X:$src1, u8imm:$src2),
      "vextractps\t{$src2, $src1, $dst|$dst, $src1, $src2}",
      [(set GR32orGR64:$dst, (extractelt (bc_v4i32 (v4f32 VR128X:$src1)), imm:$src2))]>,
      EVEX, WIG, Sched<[WriteVecExtract]>;

def VEXTRACTPSZmr : AVX512AIi8<0x17, MRMDestMem, (outs),
      (ins f32mem:$dst, VR128X:$src1, u8imm:$src2),
      "vextractps\t{$src2, $src1, $dst|$dst, $src1, $src2}",
      [(store (extractelt (bc_v4i32 (v4f32 VR128X:$src1)), imm:$src2),
                          addr:$dst)]>,
      EVEX, WIG, EVEX_CD8<32, CD8VT1>, Sched<[WriteVecExtractSt]>;

//===---------------------------------------------------------------------===//
// AVX-512 BROADCAST
//---
// broadcast with a scalar argument.
multiclass avx512_broadcast_scalar<string Name, X86VectorVTInfo DestInfo,
                                   X86VectorVTInfo SrcInfo> {
  def : Pat<(DestInfo.VT (X86VBroadcast SrcInfo.FRC:$src)),
            (!cast<Instruction>(Name#DestInfo.ZSuffix#rr)
             (SrcInfo.VT (COPY_TO_REGCLASS SrcInfo.FRC:$src, SrcInfo.RC)))>;
  def : Pat<(DestInfo.VT (vselect_mask DestInfo.KRCWM:$mask,
                                       (X86VBroadcast SrcInfo.FRC:$src),
                                       DestInfo.RC:$src0)),
            (!cast<Instruction>(Name#DestInfo.ZSuffix#rrk)
             DestInfo.RC:$src0, DestInfo.KRCWM:$mask,
             (SrcInfo.VT (COPY_TO_REGCLASS SrcInfo.FRC:$src, SrcInfo.RC)))>;
  def : Pat<(DestInfo.VT (vselect_mask DestInfo.KRCWM:$mask,
                                       (X86VBroadcast SrcInfo.FRC:$src),
                                       DestInfo.ImmAllZerosV)),
            (!cast<Instruction>(Name#DestInfo.ZSuffix#rrkz)
             DestInfo.KRCWM:$mask, (SrcInfo.VT (COPY_TO_REGCLASS SrcInfo.FRC:$src, SrcInfo.RC)))>;
}

// Split version to allow mask and broadcast node to be different types. This
// helps support the 32x2 broadcasts.
multiclass avx512_broadcast_rm_split<bits<8> opc, string OpcodeStr,
                                     SchedWrite SchedRR, SchedWrite SchedRM,
                                     X86VectorVTInfo MaskInfo,
                                     X86VectorVTInfo DestInfo,
                                     X86VectorVTInfo SrcInfo,
                                     bit IsConvertibleToThreeAddress,
                                     SDPatternOperator UnmaskedOp = X86VBroadcast,
                                     SDPatternOperator UnmaskedBcastOp = SrcInfo.BroadcastLdFrag> {
  let hasSideEffects = 0 in
  def rr : AVX512PI<opc, MRMSrcReg, (outs MaskInfo.RC:$dst), (ins SrcInfo.RC:$src),
                    !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                    [(set MaskInfo.RC:$dst,
                      (MaskInfo.VT
                       (bitconvert
                        (DestInfo.VT
                         (UnmaskedOp (SrcInfo.VT SrcInfo.RC:$src))))))],
                    DestInfo.ExeDomain>, T8, PD, EVEX, Sched<[SchedRR]>;
  def rrkz : AVX512PI<opc, MRMSrcReg, (outs MaskInfo.RC:$dst),
                      (ins MaskInfo.KRCWM:$mask, SrcInfo.RC:$src),
                      !strconcat(OpcodeStr, "\t{$src, ${dst} {${mask}} {z}|",
                       "${dst} {${mask}} {z}, $src}"),
                       [(set MaskInfo.RC:$dst,
                         (vselect_mask MaskInfo.KRCWM:$mask,
                          (MaskInfo.VT
                           (bitconvert
                            (DestInfo.VT
                             (X86VBroadcast (SrcInfo.VT SrcInfo.RC:$src))))),
                          MaskInfo.ImmAllZerosV))],
                       DestInfo.ExeDomain>, T8, PD, EVEX, EVEX_KZ, Sched<[SchedRR]>;
  let Constraints = "$src0 = $dst" in
  def rrk : AVX512PI<opc, MRMSrcReg, (outs MaskInfo.RC:$dst),
                     (ins MaskInfo.RC:$src0, MaskInfo.KRCWM:$mask,
                          SrcInfo.RC:$src),
                     !strconcat(OpcodeStr, "\t{$src, ${dst} {${mask}}|",
                     "${dst} {${mask}}, $src}"),
                     [(set MaskInfo.RC:$dst,
                       (vselect_mask MaskInfo.KRCWM:$mask,
                        (MaskInfo.VT
                         (bitconvert
                          (DestInfo.VT
                           (X86VBroadcast (SrcInfo.VT SrcInfo.RC:$src))))),
                        MaskInfo.RC:$src0))],
                      DestInfo.ExeDomain>, T8, PD, EVEX, EVEX_K, Sched<[SchedRR]>;

  let hasSideEffects = 0, mayLoad = 1, isReMaterializable = 1, canFoldAsLoad = 1 in
  def rm : AVX512PI<opc, MRMSrcMem, (outs MaskInfo.RC:$dst),
                    (ins SrcInfo.ScalarMemOp:$src),
                    !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                    [(set MaskInfo.RC:$dst,
                      (MaskInfo.VT
                       (bitconvert
                        (DestInfo.VT
                         (UnmaskedBcastOp addr:$src)))))],
                    DestInfo.ExeDomain>, T8, PD, EVEX,
                    EVEX_CD8<SrcInfo.EltSize, CD8VT1>, Sched<[SchedRM]>;

  def rmkz : AVX512PI<opc, MRMSrcMem, (outs MaskInfo.RC:$dst),
                      (ins MaskInfo.KRCWM:$mask, SrcInfo.ScalarMemOp:$src),
                      !strconcat(OpcodeStr, "\t{$src, ${dst} {${mask}} {z}|",
                       "${dst} {${mask}} {z}, $src}"),
                       [(set MaskInfo.RC:$dst,
                         (vselect_mask MaskInfo.KRCWM:$mask,
                          (MaskInfo.VT
                           (bitconvert
                            (DestInfo.VT
                             (SrcInfo.BroadcastLdFrag addr:$src)))),
                          MaskInfo.ImmAllZerosV))],
                       DestInfo.ExeDomain>, T8, PD, EVEX, EVEX_KZ,
                       EVEX_CD8<SrcInfo.EltSize, CD8VT1>, Sched<[SchedRM]>;

  let Constraints = "$src0 = $dst",
      isConvertibleToThreeAddress = IsConvertibleToThreeAddress in
  def rmk : AVX512PI<opc, MRMSrcMem, (outs MaskInfo.RC:$dst),
                     (ins MaskInfo.RC:$src0, MaskInfo.KRCWM:$mask,
                          SrcInfo.ScalarMemOp:$src),
                     !strconcat(OpcodeStr, "\t{$src, ${dst} {${mask}}|",
                     "${dst} {${mask}}, $src}"),
                     [(set MaskInfo.RC:$dst,
                       (vselect_mask MaskInfo.KRCWM:$mask,
                        (MaskInfo.VT
                         (bitconvert
                          (DestInfo.VT
                           (SrcInfo.BroadcastLdFrag addr:$src)))),
                        MaskInfo.RC:$src0))],
                      DestInfo.ExeDomain>, T8, PD, EVEX, EVEX_K,
                      EVEX_CD8<SrcInfo.EltSize, CD8VT1>, Sched<[SchedRM]>;
}

// Helper class to force mask and broadcast result to same type.
multiclass avx512_broadcast_rm<bits<8> opc, string OpcodeStr,
                               SchedWrite SchedRR, SchedWrite SchedRM,
                               X86VectorVTInfo DestInfo,
                               X86VectorVTInfo SrcInfo,
                               bit IsConvertibleToThreeAddress> :
  avx512_broadcast_rm_split<opc, OpcodeStr, SchedRR, SchedRM,
                            DestInfo, DestInfo, SrcInfo,
                            IsConvertibleToThreeAddress>;

multiclass avx512_fp_broadcast_sd<bits<8> opc, string OpcodeStr,
                                  AVX512VLVectorVTInfo _> {
  let Predicates = [HasAVX512] in {
    defm Z  : avx512_broadcast_rm<opc, OpcodeStr, WriteFShuffle256,
                                  WriteFShuffle256Ld, _.info512, _.info128, 1>,
              avx512_broadcast_scalar<NAME, _.info512, _.info128>,
              EVEX_V512;
  }

  let Predicates = [HasVLX] in {
    defm Z256  : avx512_broadcast_rm<opc, OpcodeStr, WriteFShuffle256,
                                     WriteFShuffle256Ld, _.info256, _.info128, 1>,
                 avx512_broadcast_scalar<NAME, _.info256, _.info128>,
                 EVEX_V256;
  }
}

multiclass avx512_fp_broadcast_ss<bits<8> opc, string OpcodeStr,
                                  AVX512VLVectorVTInfo _> {
  let Predicates = [HasAVX512] in {
    defm Z  : avx512_broadcast_rm<opc, OpcodeStr, WriteFShuffle256,
                                  WriteFShuffle256Ld, _.info512, _.info128, 1>,
              avx512_broadcast_scalar<NAME, _.info512, _.info128>,
              EVEX_V512;
  }

  let Predicates = [HasVLX] in {
    defm Z256  : avx512_broadcast_rm<opc, OpcodeStr, WriteFShuffle256,
                                     WriteFShuffle256Ld, _.info256, _.info128, 1>,
                 avx512_broadcast_scalar<NAME, _.info256, _.info128>,
                 EVEX_V256;
    defm Z128  : avx512_broadcast_rm<opc, OpcodeStr, WriteFShuffle256,
                                     WriteFShuffle256Ld, _.info128, _.info128, 1>,
                 avx512_broadcast_scalar<NAME, _.info128, _.info128>,
                 EVEX_V128;
  }
}
defm VBROADCASTSS  : avx512_fp_broadcast_ss<0x18, "vbroadcastss",
                                       avx512vl_f32_info>;
defm VBROADCASTSD  : avx512_fp_broadcast_sd<0x19, "vbroadcastsd",
                                       avx512vl_f64_info>, REX_W;

multiclass avx512_int_broadcast_reg<bits<8> opc, SchedWrite SchedRR,
                                    X86VectorVTInfo _, SDPatternOperator OpNode,
                                    RegisterClass SrcRC> {
  // Fold with a mask even if it has multiple uses since it is cheap.
  let ExeDomain = _.ExeDomain in
  defm rr : AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                          (ins SrcRC:$src),
                          "vpbroadcast"#_.Suffix, "$src", "$src",
                          (_.VT (OpNode SrcRC:$src)), /*IsCommutable*/0,
                          /*IsKCommutable*/0, /*IsKZCommutable*/0, vselect>,
                          T8, PD, EVEX, Sched<[SchedRR]>;
}

multiclass avx512_int_broadcastbw_reg<bits<8> opc, string Name, SchedWrite SchedRR,
                                    X86VectorVTInfo _, SDPatternOperator OpNode,
                                    RegisterClass SrcRC, SubRegIndex Subreg> {
  let hasSideEffects = 0, ExeDomain = _.ExeDomain in
  defm rr : AVX512_maskable_custom<opc, MRMSrcReg,
                         (outs _.RC:$dst), (ins GR32:$src),
                         !con((ins _.RC:$src0, _.KRCWM:$mask), (ins GR32:$src)),
                         !con((ins _.KRCWM:$mask), (ins GR32:$src)),
                         "vpbroadcast"#_.Suffix, "$src", "$src", [], [], [],
                         "$src0 = $dst">, T8, PD, EVEX, Sched<[SchedRR]>;

  def : Pat <(_.VT (OpNode SrcRC:$src)),
             (!cast<Instruction>(Name#rr)
              (i32 (INSERT_SUBREG (i32 (IMPLICIT_DEF)), SrcRC:$src, Subreg)))>;

  // Fold with a mask even if it has multiple uses since it is cheap.
  def : Pat <(vselect _.KRCWM:$mask, (_.VT (OpNode SrcRC:$src)), _.RC:$src0),
             (!cast<Instruction>(Name#rrk) _.RC:$src0, _.KRCWM:$mask,
              (i32 (INSERT_SUBREG (i32 (IMPLICIT_DEF)), SrcRC:$src, Subreg)))>;

  def : Pat <(vselect _.KRCWM:$mask, (_.VT (OpNode SrcRC:$src)), _.ImmAllZerosV),
             (!cast<Instruction>(Name#rrkz) _.KRCWM:$mask,
              (i32 (INSERT_SUBREG (i32 (IMPLICIT_DEF)), SrcRC:$src, Subreg)))>;
}

multiclass avx512_int_broadcastbw_reg_vl<bits<8> opc, string Name,
                      AVX512VLVectorVTInfo _, SDPatternOperator OpNode,
                      RegisterClass SrcRC, SubRegIndex Subreg, Predicate prd> {
  let Predicates = [prd] in
    defm Z : avx512_int_broadcastbw_reg<opc, Name#Z, WriteShuffle256, _.info512,
              OpNode, SrcRC, Subreg>, EVEX_V512;
  let Predicates = [prd, HasVLX] in {
    defm Z256 : avx512_int_broadcastbw_reg<opc, Name#Z256, WriteShuffle256,
              _.info256, OpNode, SrcRC, Subreg>, EVEX_V256;
    defm Z128 : avx512_int_broadcastbw_reg<opc, Name#Z128, WriteShuffle,
              _.info128, OpNode, SrcRC, Subreg>, EVEX_V128;
  }
}

multiclass avx512_int_broadcast_reg_vl<bits<8> opc, AVX512VLVectorVTInfo _,
                                       SDPatternOperator OpNode,
                                       RegisterClass SrcRC, Predicate prd> {
  let Predicates = [prd] in
    defm Z : avx512_int_broadcast_reg<opc, WriteShuffle256, _.info512, OpNode,
                                      SrcRC>, EVEX_V512;
  let Predicates = [prd, HasVLX] in {
    defm Z256 : avx512_int_broadcast_reg<opc, WriteShuffle256, _.info256, OpNode,
                                         SrcRC>, EVEX_V256;
    defm Z128 : avx512_int_broadcast_reg<opc, WriteShuffle, _.info128, OpNode,
                                         SrcRC>, EVEX_V128;
  }
}

defm VPBROADCASTBr : avx512_int_broadcastbw_reg_vl<0x7A, "VPBROADCASTBr",
                       avx512vl_i8_info, X86VBroadcast, GR8, sub_8bit, HasBWI>;
defm VPBROADCASTWr : avx512_int_broadcastbw_reg_vl<0x7B, "VPBROADCASTWr",
                       avx512vl_i16_info, X86VBroadcast, GR16, sub_16bit,
                       HasBWI>;
defm VPBROADCASTDr : avx512_int_broadcast_reg_vl<0x7C, avx512vl_i32_info,
                                                 X86VBroadcast, GR32, HasAVX512>;
defm VPBROADCASTQr : avx512_int_broadcast_reg_vl<0x7C, avx512vl_i64_info,
                                                 X86VBroadcast, GR64, HasAVX512>, REX_W;

multiclass avx512_int_broadcast_rm_vl<bits<8> opc, string OpcodeStr,
                                      AVX512VLVectorVTInfo _, Predicate prd,
                                      bit IsConvertibleToThreeAddress> {
  let Predicates = [prd] in {
    defm Z :   avx512_broadcast_rm<opc, OpcodeStr, WriteShuffle256,
                                   WriteShuffle256Ld, _.info512, _.info128,
                                   IsConvertibleToThreeAddress>,
                                  EVEX_V512;
  }
  let Predicates = [prd, HasVLX] in {
    defm Z256 : avx512_broadcast_rm<opc, OpcodeStr, WriteShuffle256,
                                    WriteShuffle256Ld, _.info256, _.info128,
                                    IsConvertibleToThreeAddress>,
                                 EVEX_V256;
    defm Z128 : avx512_broadcast_rm<opc, OpcodeStr, WriteShuffle,
                                    WriteShuffleXLd, _.info128, _.info128,
                                    IsConvertibleToThreeAddress>,
                                 EVEX_V128;
  }
}

defm VPBROADCASTB  : avx512_int_broadcast_rm_vl<0x78, "vpbroadcastb",
                                           avx512vl_i8_info, HasBWI, 0>;
defm VPBROADCASTW  : avx512_int_broadcast_rm_vl<0x79, "vpbroadcastw",
                                           avx512vl_i16_info, HasBWI, 0>;
defm VPBROADCASTD  : avx512_int_broadcast_rm_vl<0x58, "vpbroadcastd",
                                           avx512vl_i32_info, HasAVX512, 1>;
defm VPBROADCASTQ  : avx512_int_broadcast_rm_vl<0x59, "vpbroadcastq",
                                           avx512vl_i64_info, HasAVX512, 1>, REX_W;

multiclass avx512_subvec_broadcast_rm<bits<8> opc, string OpcodeStr,
                                      SDPatternOperator OpNode,
                                      X86VectorVTInfo _Dst,
                                      X86VectorVTInfo _Src> {
  defm rm : AVX512_maskable<opc, MRMSrcMem, _Dst, (outs _Dst.RC:$dst),
                           (ins _Src.MemOp:$src), OpcodeStr, "$src", "$src",
                           (_Dst.VT (OpNode addr:$src))>,
                           Sched<[SchedWriteShuffle.YMM.Folded]>,
                           AVX5128IBase, EVEX;
}

// This should be used for the AVX512DQ broadcast instructions. It disables
// the unmasked patterns so that we only use the DQ instructions when masking
//  is requested.
multiclass avx512_subvec_broadcast_rm_dq<bits<8> opc, string OpcodeStr,
                                         SDPatternOperator OpNode,
                                         X86VectorVTInfo _Dst,
                                         X86VectorVTInfo _Src> {
  let hasSideEffects = 0, mayLoad = 1 in
  defm rm : AVX512_maskable_split<opc, MRMSrcMem, _Dst, (outs _Dst.RC:$dst),
                           (ins _Src.MemOp:$src), OpcodeStr, "$src", "$src",
                           (null_frag),
                           (_Dst.VT (OpNode addr:$src))>,
                           Sched<[SchedWriteShuffle.YMM.Folded]>,
                           AVX5128IBase, EVEX;
}
let Predicates = [HasBWI] in {
  def : Pat<(v32f16 (X86VBroadcastld16 addr:$src)),
            (VPBROADCASTWZrm addr:$src)>;

  def : Pat<(v32f16 (X86VBroadcast (v8f16 VR128X:$src))),
            (VPBROADCASTWZrr VR128X:$src)>;
  def : Pat<(v32f16 (X86VBroadcast (f16 FR16X:$src))),
            (VPBROADCASTWZrr (COPY_TO_REGCLASS FR16X:$src, VR128X))>;
}
let Predicates = [HasVLX, HasBWI] in {
  def : Pat<(v8f16 (X86VBroadcastld16 addr:$src)),
            (VPBROADCASTWZ128rm addr:$src)>;
  def : Pat<(v16f16 (X86VBroadcastld16 addr:$src)),
            (VPBROADCASTWZ256rm addr:$src)>;

  def : Pat<(v8f16 (X86VBroadcast (v8f16 VR128X:$src))),
            (VPBROADCASTWZ128rr VR128X:$src)>;
  def : Pat<(v16f16 (X86VBroadcast (v8f16 VR128X:$src))),
            (VPBROADCASTWZ256rr VR128X:$src)>;

  def : Pat<(v8f16 (X86VBroadcast (f16 FR16X:$src))),
            (VPBROADCASTWZ128rr (COPY_TO_REGCLASS FR16X:$src, VR128X))>;
  def : Pat<(v16f16 (X86VBroadcast (f16 FR16X:$src))),
            (VPBROADCASTWZ256rr (COPY_TO_REGCLASS FR16X:$src, VR128X))>;
}

//===----------------------------------------------------------------------===//
// AVX-512 BROADCAST SUBVECTORS
//

defm VBROADCASTI32X4 : avx512_subvec_broadcast_rm<0x5a, "vbroadcasti32x4",
                       X86SubVBroadcastld128, v16i32_info, v4i32x_info>,
                       EVEX_V512, EVEX_CD8<32, CD8VT4>;
defm VBROADCASTF32X4 : avx512_subvec_broadcast_rm<0x1a, "vbroadcastf32x4",
                       X86SubVBroadcastld128, v16f32_info, v4f32x_info>,
                       EVEX_V512, EVEX_CD8<32, CD8VT4>;
defm VBROADCASTI64X4 : avx512_subvec_broadcast_rm<0x5b, "vbroadcasti64x4",
                       X86SubVBroadcastld256, v8i64_info, v4i64x_info>, REX_W,
                       EVEX_V512, EVEX_CD8<64, CD8VT4>;
defm VBROADCASTF64X4 : avx512_subvec_broadcast_rm<0x1b, "vbroadcastf64x4",
                       X86SubVBroadcastld256, v8f64_info, v4f64x_info>, REX_W,
                       EVEX_V512, EVEX_CD8<64, CD8VT4>;

let Predicates = [HasAVX512] in {
def : Pat<(v8f64 (X86SubVBroadcastld256 addr:$src)),
          (VBROADCASTF64X4rm addr:$src)>;
def : Pat<(v16f32 (X86SubVBroadcastld256 addr:$src)),
          (VBROADCASTF64X4rm addr:$src)>;
def : Pat<(v32f16 (X86SubVBroadcastld256 addr:$src)),
          (VBROADCASTF64X4rm addr:$src)>;
def : Pat<(v8i64 (X86SubVBroadcastld256 addr:$src)),
          (VBROADCASTI64X4rm addr:$src)>;
def : Pat<(v16i32 (X86SubVBroadcastld256 addr:$src)),
          (VBROADCASTI64X4rm addr:$src)>;
def : Pat<(v32i16 (X86SubVBroadcastld256 addr:$src)),
          (VBROADCASTI64X4rm addr:$src)>;
def : Pat<(v64i8 (X86SubVBroadcastld256 addr:$src)),
          (VBROADCASTI64X4rm addr:$src)>;

def : Pat<(v8f64 (X86SubVBroadcastld128 addr:$src)),
          (VBROADCASTF32X4rm addr:$src)>;
def : Pat<(v16f32 (X86SubVBroadcastld128 addr:$src)),
          (VBROADCASTF32X4rm addr:$src)>;
def : Pat<(v32f16 (X86SubVBroadcastld128 addr:$src)),
          (VBROADCASTF32X4rm addr:$src)>;
def : Pat<(v8i64 (X86SubVBroadcastld128 addr:$src)),
          (VBROADCASTI32X4rm addr:$src)>;
def : Pat<(v16i32 (X86SubVBroadcastld128 addr:$src)),
          (VBROADCASTI32X4rm addr:$src)>;
def : Pat<(v32i16 (X86SubVBroadcastld128 addr:$src)),
          (VBROADCASTI32X4rm addr:$src)>;
def : Pat<(v64i8 (X86SubVBroadcastld128 addr:$src)),
          (VBROADCASTI32X4rm addr:$src)>;

// Patterns for selects of bitcasted operations.
def : Pat<(vselect_mask VK16WM:$mask,
                        (bc_v16f32 (v8f64 (X86SubVBroadcastld128 addr:$src))),
                        (v16f32 immAllZerosV)),
          (VBROADCASTF32X4rmkz VK16WM:$mask, addr:$src)>;
def : Pat<(vselect_mask VK16WM:$mask,
                        (bc_v16f32 (v8f64 (X86SubVBroadcastld128 addr:$src))),
                        VR512:$src0),
          (VBROADCASTF32X4rmk VR512:$src0, VK16WM:$mask, addr:$src)>;
def : Pat<(vselect_mask VK16WM:$mask,
                        (bc_v16i32 (v8i64 (X86SubVBroadcastld128 addr:$src))),
                        (v16i32 immAllZerosV)),
          (VBROADCASTI32X4rmkz VK16WM:$mask, addr:$src)>;
def : Pat<(vselect_mask VK16WM:$mask,
                        (bc_v16i32 (v8i64 (X86SubVBroadcastld128 addr:$src))),
                        VR512:$src0),
          (VBROADCASTI32X4rmk VR512:$src0, VK16WM:$mask, addr:$src)>;

def : Pat<(vselect_mask VK8WM:$mask,
                        (bc_v8f64 (v16f32 (X86SubVBroadcastld256 addr:$src))),
                        (v8f64 immAllZerosV)),
          (VBROADCASTF64X4rmkz VK8WM:$mask, addr:$src)>;
def : Pat<(vselect_mask VK8WM:$mask,
                        (bc_v8f64 (v16f32 (X86SubVBroadcastld256 addr:$src))),
                        VR512:$src0),
          (VBROADCASTF64X4rmk VR512:$src0, VK8WM:$mask, addr:$src)>;
def : Pat<(vselect_mask VK8WM:$mask,
                        (bc_v8i64 (v16i32 (X86SubVBroadcastld256 addr:$src))),
                        (v8i64 immAllZerosV)),
          (VBROADCASTI64X4rmkz VK8WM:$mask, addr:$src)>;
def : Pat<(vselect_mask VK8WM:$mask,
                        (bc_v8i64 (v16i32 (X86SubVBroadcastld256 addr:$src))),
                        VR512:$src0),
          (VBROADCASTI64X4rmk VR512:$src0, VK8WM:$mask, addr:$src)>;
}

let Predicates = [HasVLX] in {
defm VBROADCASTI32X4Z256 : avx512_subvec_broadcast_rm<0x5a, "vbroadcasti32x4",
                           X86SubVBroadcastld128, v8i32x_info, v4i32x_info>,
                           EVEX_V256, EVEX_CD8<32, CD8VT4>;
defm VBROADCASTF32X4Z256 : avx512_subvec_broadcast_rm<0x1a, "vbroadcastf32x4",
                           X86SubVBroadcastld128, v8f32x_info, v4f32x_info>,
                           EVEX_V256, EVEX_CD8<32, CD8VT4>;

def : Pat<(v4f64 (X86SubVBroadcastld128 addr:$src)),
          (VBROADCASTF32X4Z256rm addr:$src)>;
def : Pat<(v8f32 (X86SubVBroadcastld128 addr:$src)),
          (VBROADCASTF32X4Z256rm addr:$src)>;
def : Pat<(v16f16 (X86SubVBroadcastld128 addr:$src)),
          (VBROADCASTF32X4Z256rm addr:$src)>;
def : Pat<(v4i64 (X86SubVBroadcastld128 addr:$src)),
          (VBROADCASTI32X4Z256rm addr:$src)>;
def : Pat<(v8i32 (X86SubVBroadcastld128 addr:$src)),
          (VBROADCASTI32X4Z256rm addr:$src)>;
def : Pat<(v16i16 (X86SubVBroadcastld128 addr:$src)),
          (VBROADCASTI32X4Z256rm addr:$src)>;
def : Pat<(v32i8 (X86SubVBroadcastld128 addr:$src)),
          (VBROADCASTI32X4Z256rm addr:$src)>;

// Patterns for selects of bitcasted operations.
def : Pat<(vselect_mask VK8WM:$mask,
                        (bc_v8f32 (v4f64 (X86SubVBroadcastld128 addr:$src))),
                        (v8f32 immAllZerosV)),
          (VBROADCASTF32X4Z256rmkz VK8WM:$mask, addr:$src)>;
def : Pat<(vselect_mask VK8WM:$mask,
                        (bc_v8f32 (v4f64 (X86SubVBroadcastld128 addr:$src))),
                        VR256X:$src0),
          (VBROADCASTF32X4Z256rmk VR256X:$src0, VK8WM:$mask, addr:$src)>;
def : Pat<(vselect_mask VK8WM:$mask,
                        (bc_v8i32 (v4i64 (X86SubVBroadcastld128 addr:$src))),
                        (v8i32 immAllZerosV)),
          (VBROADCASTI32X4Z256rmkz VK8WM:$mask, addr:$src)>;
def : Pat<(vselect_mask VK8WM:$mask,
                        (bc_v8i32 (v4i64 (X86SubVBroadcastld128 addr:$src))),
                        VR256X:$src0),
          (VBROADCASTI32X4Z256rmk VR256X:$src0, VK8WM:$mask, addr:$src)>;
}

let Predicates = [HasBF16] in {
  def : Pat<(v32bf16 (X86SubVBroadcastld256 addr:$src)),
            (VBROADCASTF64X4rm addr:$src)>;
  def : Pat<(v32bf16 (X86SubVBroadcastld128 addr:$src)),
            (VBROADCASTF32X4rm addr:$src)>;
}

let Predicates = [HasBF16, HasVLX] in
  def : Pat<(v16bf16 (X86SubVBroadcastld128 addr:$src)),
            (VBROADCASTF32X4Z256rm addr:$src)>;

let Predicates = [HasVLX, HasDQI] in {
defm VBROADCASTI64X2Z128 : avx512_subvec_broadcast_rm_dq<0x5a, "vbroadcasti64x2",
                           X86SubVBroadcastld128, v4i64x_info, v2i64x_info>,
                           EVEX_V256, EVEX_CD8<64, CD8VT2>, REX_W;
defm VBROADCASTF64X2Z128 : avx512_subvec_broadcast_rm_dq<0x1a, "vbroadcastf64x2",
                           X86SubVBroadcastld128, v4f64x_info, v2f64x_info>,
                           EVEX_V256, EVEX_CD8<64, CD8VT2>, REX_W;

// Patterns for selects of bitcasted operations.
def : Pat<(vselect_mask VK4WM:$mask,
                        (bc_v4f64 (v8f32 (X86SubVBroadcastld128 addr:$src))),
                        (v4f64 immAllZerosV)),
          (VBROADCASTF64X2Z128rmkz VK4WM:$mask, addr:$src)>;
def : Pat<(vselect_mask VK4WM:$mask,
                        (bc_v4f64 (v8f32 (X86SubVBroadcastld128 addr:$src))),
                        VR256X:$src0),
          (VBROADCASTF64X2Z128rmk VR256X:$src0, VK4WM:$mask, addr:$src)>;
def : Pat<(vselect_mask VK4WM:$mask,
                        (bc_v4i64 (v8i32 (X86SubVBroadcastld128 addr:$src))),
                        (v4i64 immAllZerosV)),
          (VBROADCASTI64X2Z128rmkz VK4WM:$mask, addr:$src)>;
def : Pat<(vselect_mask VK4WM:$mask,
                        (bc_v4i64 (v8i32 (X86SubVBroadcastld128 addr:$src))),
                        VR256X:$src0),
          (VBROADCASTI64X2Z128rmk VR256X:$src0, VK4WM:$mask, addr:$src)>;
}

let Predicates = [HasDQI] in {
defm VBROADCASTI64X2 : avx512_subvec_broadcast_rm_dq<0x5a, "vbroadcasti64x2",
                       X86SubVBroadcastld128, v8i64_info, v2i64x_info>, REX_W,
                       EVEX_V512, EVEX_CD8<64, CD8VT2>;
defm VBROADCASTI32X8 : avx512_subvec_broadcast_rm_dq<0x5b, "vbroadcasti32x8",
                       X86SubVBroadcastld256, v16i32_info, v8i32x_info>,
                       EVEX_V512, EVEX_CD8<32, CD8VT8>;
defm VBROADCASTF64X2 : avx512_subvec_broadcast_rm_dq<0x1a, "vbroadcastf64x2",
                       X86SubVBroadcastld128, v8f64_info, v2f64x_info>, REX_W,
                       EVEX_V512, EVEX_CD8<64, CD8VT2>;
defm VBROADCASTF32X8 : avx512_subvec_broadcast_rm_dq<0x1b, "vbroadcastf32x8",
                       X86SubVBroadcastld256, v16f32_info, v8f32x_info>,
                       EVEX_V512, EVEX_CD8<32, CD8VT8>;

// Patterns for selects of bitcasted operations.
def : Pat<(vselect_mask VK16WM:$mask,
                        (bc_v16f32 (v8f64 (X86SubVBroadcastld256 addr:$src))),
                        (v16f32 immAllZerosV)),
          (VBROADCASTF32X8rmkz VK16WM:$mask, addr:$src)>;
def : Pat<(vselect_mask VK16WM:$mask,
                        (bc_v16f32 (v8f64 (X86SubVBroadcastld256 addr:$src))),
                        VR512:$src0),
          (VBROADCASTF32X8rmk VR512:$src0, VK16WM:$mask, addr:$src)>;
def : Pat<(vselect_mask VK16WM:$mask,
                        (bc_v16i32 (v8i64 (X86SubVBroadcastld256 addr:$src))),
                        (v16i32 immAllZerosV)),
          (VBROADCASTI32X8rmkz VK16WM:$mask, addr:$src)>;
def : Pat<(vselect_mask VK16WM:$mask,
                        (bc_v16i32 (v8i64 (X86SubVBroadcastld256 addr:$src))),
                        VR512:$src0),
          (VBROADCASTI32X8rmk VR512:$src0, VK16WM:$mask, addr:$src)>;

def : Pat<(vselect_mask VK8WM:$mask,
                        (bc_v8f64 (v16f32 (X86SubVBroadcastld128 addr:$src))),
                        (v8f64 immAllZerosV)),
          (VBROADCASTF64X2rmkz VK8WM:$mask, addr:$src)>;
def : Pat<(vselect_mask VK8WM:$mask,
                        (bc_v8f64 (v16f32 (X86SubVBroadcastld128 addr:$src))),
                        VR512:$src0),
          (VBROADCASTF64X2rmk VR512:$src0, VK8WM:$mask, addr:$src)>;
def : Pat<(vselect_mask VK8WM:$mask,
                        (bc_v8i64 (v16i32 (X86SubVBroadcastld128 addr:$src))),
                        (v8i64 immAllZerosV)),
          (VBROADCASTI64X2rmkz VK8WM:$mask, addr:$src)>;
def : Pat<(vselect_mask VK8WM:$mask,
                        (bc_v8i64 (v16i32 (X86SubVBroadcastld128 addr:$src))),
                        VR512:$src0),
          (VBROADCASTI64X2rmk VR512:$src0, VK8WM:$mask, addr:$src)>;
}

multiclass avx512_common_broadcast_32x2<bits<8> opc, string OpcodeStr,
                                        AVX512VLVectorVTInfo _Dst,
                                        AVX512VLVectorVTInfo _Src> {
  let Predicates = [HasDQI] in
    defm Z :    avx512_broadcast_rm_split<opc, OpcodeStr, WriteShuffle256,
                                          WriteShuffle256Ld, _Dst.info512,
                                          _Src.info512, _Src.info128, 0, null_frag, null_frag>,
                                          EVEX_V512;
  let Predicates = [HasDQI, HasVLX] in
    defm Z256 : avx512_broadcast_rm_split<opc, OpcodeStr, WriteShuffle256,
                                          WriteShuffle256Ld, _Dst.info256,
                                          _Src.info256, _Src.info128, 0, null_frag, null_frag>,
                                          EVEX_V256;
}

multiclass avx512_common_broadcast_i32x2<bits<8> opc, string OpcodeStr,
                                         AVX512VLVectorVTInfo _Dst,
                                         AVX512VLVectorVTInfo _Src> :
  avx512_common_broadcast_32x2<opc, OpcodeStr, _Dst, _Src> {

  let Predicates = [HasDQI, HasVLX] in
    defm Z128 : avx512_broadcast_rm_split<opc, OpcodeStr, WriteShuffle,
                                          WriteShuffleXLd, _Dst.info128,
                                          _Src.info128, _Src.info128, 0, null_frag, null_frag>,
                                          EVEX_V128;
}

defm VBROADCASTI32X2  : avx512_common_broadcast_i32x2<0x59, "vbroadcasti32x2",
                                          avx512vl_i32_info, avx512vl_i64_info>;
defm VBROADCASTF32X2  : avx512_common_broadcast_32x2<0x19, "vbroadcastf32x2",
                                          avx512vl_f32_info, avx512vl_f64_info>;

//===----------------------------------------------------------------------===//
// AVX-512 BROADCAST MASK TO VECTOR REGISTER
//---
multiclass avx512_mask_broadcastm<bits<8> opc, string OpcodeStr,
                                  X86VectorVTInfo _, RegisterClass KRC> {
  def rr : AVX512XS8I<opc, MRMSrcReg, (outs _.RC:$dst), (ins KRC:$src),
                  !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                  [(set _.RC:$dst, (_.VT (X86VBroadcastm KRC:$src)))]>,
                  EVEX, Sched<[WriteShuffle]>;
}

multiclass avx512_mask_broadcast<bits<8> opc, string OpcodeStr,
                                 AVX512VLVectorVTInfo VTInfo, RegisterClass KRC> {
  let Predicates = [HasCDI] in
    defm Z : avx512_mask_broadcastm<opc, OpcodeStr, VTInfo.info512, KRC>, EVEX_V512;
  let Predicates = [HasCDI, HasVLX] in {
    defm Z256 : avx512_mask_broadcastm<opc, OpcodeStr, VTInfo.info256, KRC>, EVEX_V256;
    defm Z128 : avx512_mask_broadcastm<opc, OpcodeStr, VTInfo.info128, KRC>, EVEX_V128;
  }
}

defm VPBROADCASTMW2D : avx512_mask_broadcast<0x3A, "vpbroadcastmw2d",
                                               avx512vl_i32_info, VK16>;
defm VPBROADCASTMB2Q : avx512_mask_broadcast<0x2A, "vpbroadcastmb2q",
                                               avx512vl_i64_info, VK8>, REX_W;

//===----------------------------------------------------------------------===//
// -- VPERMI2 - 3 source operands form --
multiclass avx512_perm_i<bits<8> opc, string OpcodeStr,
                         X86FoldableSchedWrite sched,
                         X86VectorVTInfo _, X86VectorVTInfo IdxVT> {
let Constraints = "$src1 = $dst", ExeDomain = _.ExeDomain,
    hasSideEffects = 0 in {
  defm rr: AVX512_maskable_3src_cast<opc, MRMSrcReg, _, IdxVT, (outs _.RC:$dst),
          (ins _.RC:$src2, _.RC:$src3),
          OpcodeStr, "$src3, $src2", "$src2, $src3",
          (_.VT (X86VPermt2 _.RC:$src2, IdxVT.RC:$src1, _.RC:$src3)), 1>,
          EVEX, VVVV, AVX5128IBase, Sched<[sched]>;

  let mayLoad = 1 in
  defm rm: AVX512_maskable_3src_cast<opc, MRMSrcMem, _, IdxVT, (outs _.RC:$dst),
            (ins _.RC:$src2, _.MemOp:$src3),
            OpcodeStr, "$src3, $src2", "$src2, $src3",
            (_.VT (X86VPermt2 _.RC:$src2, IdxVT.RC:$src1,
                   (_.VT (_.LdFrag addr:$src3)))), 1>,
            EVEX, VVVV, AVX5128IBase, Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

multiclass avx512_perm_i_mb<bits<8> opc, string OpcodeStr,
                            X86FoldableSchedWrite sched,
                            X86VectorVTInfo _, X86VectorVTInfo IdxVT> {
  let Constraints = "$src1 = $dst", ExeDomain = _.ExeDomain,
      hasSideEffects = 0, mayLoad = 1 in
  defm rmb: AVX512_maskable_3src_cast<opc, MRMSrcMem, _, IdxVT, (outs _.RC:$dst),
              (ins _.RC:$src2, _.ScalarMemOp:$src3),
              OpcodeStr,   !strconcat("${src3}", _.BroadcastStr,", $src2"),
              !strconcat("$src2, ${src3}", _.BroadcastStr ),
              (_.VT (X86VPermt2 _.RC:$src2,
               IdxVT.RC:$src1,(_.VT (_.BroadcastLdFrag addr:$src3)))), 1>,
              AVX5128IBase, EVEX, VVVV, EVEX_B,
              Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass avx512_perm_i_sizes<bits<8> opc, string OpcodeStr,
                               X86FoldableSchedWrite sched,
                               AVX512VLVectorVTInfo VTInfo,
                               AVX512VLVectorVTInfo ShuffleMask> {
  defm NAME#Z: avx512_perm_i<opc, OpcodeStr, sched, VTInfo.info512,
                             ShuffleMask.info512>,
               avx512_perm_i_mb<opc, OpcodeStr, sched, VTInfo.info512,
                                ShuffleMask.info512>, EVEX_V512;
  let Predicates = [HasVLX] in {
  defm NAME#Z128: avx512_perm_i<opc, OpcodeStr, sched, VTInfo.info128,
                                ShuffleMask.info128>,
                  avx512_perm_i_mb<opc, OpcodeStr, sched, VTInfo.info128,
                                   ShuffleMask.info128>, EVEX_V128;
  defm NAME#Z256: avx512_perm_i<opc, OpcodeStr, sched, VTInfo.info256,
                                ShuffleMask.info256>,
                  avx512_perm_i_mb<opc, OpcodeStr, sched, VTInfo.info256,
                                   ShuffleMask.info256>, EVEX_V256;
  }
}

multiclass avx512_perm_i_sizes_bw<bits<8> opc, string OpcodeStr,
                                  X86FoldableSchedWrite sched,
                                  AVX512VLVectorVTInfo VTInfo,
                                  AVX512VLVectorVTInfo Idx,
                                  Predicate Prd> {
  let Predicates = [Prd] in
  defm NAME#Z: avx512_perm_i<opc, OpcodeStr, sched, VTInfo.info512,
                             Idx.info512>, EVEX_V512;
  let Predicates = [Prd, HasVLX] in {
  defm NAME#Z128: avx512_perm_i<opc, OpcodeStr, sched, VTInfo.info128,
                                Idx.info128>, EVEX_V128;
  defm NAME#Z256: avx512_perm_i<opc, OpcodeStr, sched, VTInfo.info256,
                                Idx.info256>,  EVEX_V256;
  }
}

defm VPERMI2D  : avx512_perm_i_sizes<0x76, "vpermi2d", WriteVarShuffle256,
                  avx512vl_i32_info, avx512vl_i32_info>, EVEX_CD8<32, CD8VF>;
defm VPERMI2Q  : avx512_perm_i_sizes<0x76, "vpermi2q", WriteVarShuffle256,
                  avx512vl_i64_info, avx512vl_i64_info>, REX_W, EVEX_CD8<64, CD8VF>;
defm VPERMI2W  : avx512_perm_i_sizes_bw<0x75, "vpermi2w", WriteVarShuffle256,
                  avx512vl_i16_info, avx512vl_i16_info, HasBWI>,
                  REX_W, EVEX_CD8<16, CD8VF>;
defm VPERMI2B  : avx512_perm_i_sizes_bw<0x75, "vpermi2b", WriteVarShuffle256,
                  avx512vl_i8_info, avx512vl_i8_info, HasVBMI>,
                  EVEX_CD8<8, CD8VF>;
defm VPERMI2PS : avx512_perm_i_sizes<0x77, "vpermi2ps", WriteFVarShuffle256,
                  avx512vl_f32_info, avx512vl_i32_info>, EVEX_CD8<32, CD8VF>;
defm VPERMI2PD : avx512_perm_i_sizes<0x77, "vpermi2pd", WriteFVarShuffle256,
                  avx512vl_f64_info, avx512vl_i64_info>, REX_W, EVEX_CD8<64, CD8VF>;

// Extra patterns to deal with extra bitcasts due to passthru and index being
// different types on the fp versions.
multiclass avx512_perm_i_lowering<string InstrStr, X86VectorVTInfo _,
                                  X86VectorVTInfo IdxVT,
                                  X86VectorVTInfo CastVT> {
  def : Pat<(_.VT (vselect_mask _.KRCWM:$mask,
                                (X86VPermt2 (_.VT _.RC:$src2),
                                            (IdxVT.VT (bitconvert
                                                       (CastVT.VT _.RC:$src1))),
                                            _.RC:$src3),
                                (_.VT (bitconvert (CastVT.VT _.RC:$src1))))),
            (!cast<Instruction>(InstrStr#"rrk") _.RC:$src1, _.KRCWM:$mask,
                                                _.RC:$src2, _.RC:$src3)>;
  def : Pat<(_.VT (vselect_mask _.KRCWM:$mask,
                                (X86VPermt2 _.RC:$src2,
                                            (IdxVT.VT (bitconvert
                                                       (CastVT.VT _.RC:$src1))),
                                            (_.LdFrag addr:$src3)),
                                (_.VT (bitconvert  (CastVT.VT _.RC:$src1))))),
            (!cast<Instruction>(InstrStr#"rmk") _.RC:$src1, _.KRCWM:$mask,
                                                _.RC:$src2, addr:$src3)>;
  def : Pat<(_.VT (vselect_mask _.KRCWM:$mask,
                                 (X86VPermt2 _.RC:$src2,
                                             (IdxVT.VT (bitconvert  (CastVT.VT _.RC:$src1))),
                                             (_.BroadcastLdFrag addr:$src3)),
                                 (_.VT (bitconvert  (CastVT.VT _.RC:$src1))))),
            (!cast<Instruction>(InstrStr#"rmbk") _.RC:$src1, _.KRCWM:$mask,
                                                 _.RC:$src2, addr:$src3)>;
}

// TODO: Should we add more casts? The vXi64 case is common due to ABI.
defm : avx512_perm_i_lowering<"VPERMI2PSZ", v16f32_info, v16i32_info, v8i64_info>;
defm : avx512_perm_i_lowering<"VPERMI2PSZ256", v8f32x_info, v8i32x_info, v4i64x_info>;
defm : avx512_perm_i_lowering<"VPERMI2PSZ128", v4f32x_info, v4i32x_info, v2i64x_info>;

// VPERMT2
multiclass avx512_perm_t<bits<8> opc, string OpcodeStr,
                         X86FoldableSchedWrite sched,
                         X86VectorVTInfo _, X86VectorVTInfo IdxVT> {
let Constraints = "$src1 = $dst", ExeDomain = _.ExeDomain in {
  defm rr: AVX512_maskable_3src<opc, MRMSrcReg, _, (outs _.RC:$dst),
          (ins IdxVT.RC:$src2, _.RC:$src3),
          OpcodeStr, "$src3, $src2", "$src2, $src3",
          (_.VT (X86VPermt2 _.RC:$src1, IdxVT.RC:$src2, _.RC:$src3)), 1>,
          EVEX, VVVV, AVX5128IBase, Sched<[sched]>;

  defm rm: AVX512_maskable_3src<opc, MRMSrcMem, _, (outs _.RC:$dst),
            (ins IdxVT.RC:$src2, _.MemOp:$src3),
            OpcodeStr, "$src3, $src2", "$src2, $src3",
            (_.VT (X86VPermt2 _.RC:$src1, IdxVT.RC:$src2,
                   (_.LdFrag addr:$src3))), 1>,
            EVEX, VVVV, AVX5128IBase, Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}
multiclass avx512_perm_t_mb<bits<8> opc, string OpcodeStr,
                            X86FoldableSchedWrite sched,
                            X86VectorVTInfo _, X86VectorVTInfo IdxVT> {
  let Constraints = "$src1 = $dst", ExeDomain = _.ExeDomain in
  defm rmb: AVX512_maskable_3src<opc, MRMSrcMem, _, (outs _.RC:$dst),
              (ins IdxVT.RC:$src2, _.ScalarMemOp:$src3),
              OpcodeStr,   !strconcat("${src3}", _.BroadcastStr,", $src2"),
              !strconcat("$src2, ${src3}", _.BroadcastStr ),
              (_.VT (X86VPermt2 _.RC:$src1,
               IdxVT.RC:$src2,(_.VT (_.BroadcastLdFrag addr:$src3)))), 1>,
              AVX5128IBase, EVEX, VVVV, EVEX_B,
              Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass avx512_perm_t_sizes<bits<8> opc, string OpcodeStr,
                               X86FoldableSchedWrite sched,
                               AVX512VLVectorVTInfo VTInfo,
                               AVX512VLVectorVTInfo ShuffleMask> {
  defm NAME#Z: avx512_perm_t<opc, OpcodeStr, sched, VTInfo.info512,
                             ShuffleMask.info512>,
               avx512_perm_t_mb<opc, OpcodeStr, sched, VTInfo.info512,
                                ShuffleMask.info512>, EVEX_V512;
  let Predicates = [HasVLX] in {
  defm NAME#Z128: avx512_perm_t<opc, OpcodeStr, sched, VTInfo.info128,
                                ShuffleMask.info128>,
                  avx512_perm_t_mb<opc, OpcodeStr, sched, VTInfo.info128,
                                   ShuffleMask.info128>, EVEX_V128;
  defm NAME#Z256: avx512_perm_t<opc, OpcodeStr, sched, VTInfo.info256,
                                ShuffleMask.info256>,
                   avx512_perm_t_mb<opc, OpcodeStr, sched, VTInfo.info256,
                                    ShuffleMask.info256>, EVEX_V256;
  }
}

multiclass avx512_perm_t_sizes_bw<bits<8> opc, string OpcodeStr,
                                  X86FoldableSchedWrite sched,
                                  AVX512VLVectorVTInfo VTInfo,
                                  AVX512VLVectorVTInfo Idx, Predicate Prd> {
  let Predicates = [Prd] in
  defm NAME#Z: avx512_perm_t<opc, OpcodeStr, sched, VTInfo.info512,
                             Idx.info512>, EVEX_V512;
  let Predicates = [Prd, HasVLX] in {
  defm NAME#Z128: avx512_perm_t<opc, OpcodeStr, sched, VTInfo.info128,
                                Idx.info128>, EVEX_V128;
  defm NAME#Z256: avx512_perm_t<opc, OpcodeStr, sched, VTInfo.info256,
                                Idx.info256>, EVEX_V256;
  }
}

defm VPERMT2D  : avx512_perm_t_sizes<0x7E, "vpermt2d", WriteVarShuffle256,
                  avx512vl_i32_info, avx512vl_i32_info>, EVEX_CD8<32, CD8VF>;
defm VPERMT2Q  : avx512_perm_t_sizes<0x7E, "vpermt2q", WriteVarShuffle256,
                  avx512vl_i64_info, avx512vl_i64_info>, REX_W, EVEX_CD8<64, CD8VF>;
defm VPERMT2W  : avx512_perm_t_sizes_bw<0x7D, "vpermt2w", WriteVarShuffle256,
                  avx512vl_i16_info, avx512vl_i16_info, HasBWI>,
                  REX_W, EVEX_CD8<16, CD8VF>;
defm VPERMT2B  : avx512_perm_t_sizes_bw<0x7D, "vpermt2b", WriteVarShuffle256,
                  avx512vl_i8_info, avx512vl_i8_info, HasVBMI>,
                  EVEX_CD8<8, CD8VF>;
defm VPERMT2PS : avx512_perm_t_sizes<0x7F, "vpermt2ps", WriteFVarShuffle256,
                  avx512vl_f32_info, avx512vl_i32_info>, EVEX_CD8<32, CD8VF>;
defm VPERMT2PD : avx512_perm_t_sizes<0x7F, "vpermt2pd", WriteFVarShuffle256,
                  avx512vl_f64_info, avx512vl_i64_info>, REX_W, EVEX_CD8<64, CD8VF>;

//===----------------------------------------------------------------------===//
// AVX-512 - BLEND using mask
//

multiclass WriteFVarBlendask<bits<8> opc, string OpcodeStr,
                             X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain, hasSideEffects = 0 in {
  def rr : AVX5128I<opc, MRMSrcReg, (outs _.RC:$dst),
             (ins _.RC:$src1, _.RC:$src2),
             !strconcat(OpcodeStr,
             "\t{$src2, $src1, ${dst}|${dst}, $src1, $src2}"), []>,
             EVEX, VVVV, Sched<[sched]>;
  def rrk : AVX5128I<opc, MRMSrcReg, (outs _.RC:$dst),
             (ins _.KRCWM:$mask, _.RC:$src1, _.RC:$src2),
             !strconcat(OpcodeStr,
             "\t{$src2, $src1, ${dst} {${mask}}|${dst} {${mask}}, $src1, $src2}"),
             []>, EVEX, VVVV, EVEX_K, Sched<[sched]>;
  def rrkz : AVX5128I<opc, MRMSrcReg, (outs _.RC:$dst),
             (ins _.KRCWM:$mask, _.RC:$src1, _.RC:$src2),
             !strconcat(OpcodeStr,
             "\t{$src2, $src1, ${dst} {${mask}} {z}|${dst} {${mask}} {z}, $src1, $src2}"),
             []>, EVEX, VVVV, EVEX_KZ, Sched<[sched]>;
  let mayLoad = 1 in {
  def rm  : AVX5128I<opc, MRMSrcMem, (outs _.RC:$dst),
             (ins _.RC:$src1, _.MemOp:$src2),
             !strconcat(OpcodeStr,
             "\t{$src2, $src1, ${dst}|${dst}, $src1, $src2}"),
             []>, EVEX, VVVV, EVEX_CD8<_.EltSize, CD8VF>,
             Sched<[sched.Folded, sched.ReadAfterFold]>;
  def rmk : AVX5128I<opc, MRMSrcMem, (outs _.RC:$dst),
             (ins _.KRCWM:$mask, _.RC:$src1, _.MemOp:$src2),
             !strconcat(OpcodeStr,
             "\t{$src2, $src1, ${dst} {${mask}}|${dst} {${mask}}, $src1, $src2}"),
             []>, EVEX, VVVV, EVEX_K, EVEX_CD8<_.EltSize, CD8VF>,
             Sched<[sched.Folded, sched.ReadAfterFold]>;
  def rmkz : AVX5128I<opc, MRMSrcMem, (outs _.RC:$dst),
             (ins _.KRCWM:$mask, _.RC:$src1, _.MemOp:$src2),
             !strconcat(OpcodeStr,
             "\t{$src2, $src1, ${dst} {${mask}} {z}|${dst} {${mask}} {z}, $src1, $src2}"),
             []>, EVEX, VVVV, EVEX_KZ, EVEX_CD8<_.EltSize, CD8VF>,
             Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
  }
}
multiclass WriteFVarBlendask_rmb<bits<8> opc, string OpcodeStr,
                                 X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain, mayLoad = 1, hasSideEffects = 0 in {
  def rmbk : AVX5128I<opc, MRMSrcMem, (outs _.RC:$dst),
      (ins _.KRCWM:$mask, _.RC:$src1, _.ScalarMemOp:$src2),
       !strconcat(OpcodeStr,
            "\t{${src2}", _.BroadcastStr, ", $src1, $dst {${mask}}|",
            "$dst {${mask}}, $src1, ${src2}", _.BroadcastStr, "}"), []>,
      EVEX, VVVV, EVEX_K, EVEX_B, EVEX_CD8<_.EltSize, CD8VF>,
      Sched<[sched.Folded, sched.ReadAfterFold]>;

  def rmbkz : AVX5128I<opc, MRMSrcMem, (outs _.RC:$dst),
      (ins _.KRCWM:$mask, _.RC:$src1, _.ScalarMemOp:$src2),
       !strconcat(OpcodeStr,
            "\t{${src2}", _.BroadcastStr, ", $src1, $dst {${mask}} {z}|",
            "$dst {${mask}} {z}, $src1, ${src2}", _.BroadcastStr, "}"), []>,
      EVEX, VVVV, EVEX_KZ, EVEX_B, EVEX_CD8<_.EltSize, CD8VF>,
      Sched<[sched.Folded, sched.ReadAfterFold]>;

  def rmb : AVX5128I<opc, MRMSrcMem, (outs _.RC:$dst),
      (ins _.RC:$src1, _.ScalarMemOp:$src2),
       !strconcat(OpcodeStr,
            "\t{${src2}", _.BroadcastStr, ", $src1, $dst|",
            "$dst, $src1, ${src2}", _.BroadcastStr, "}"), []>,
      EVEX, VVVV, EVEX_B, EVEX_CD8<_.EltSize, CD8VF>,
      Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

multiclass blendmask_dq<bits<8> opc, string OpcodeStr, X86SchedWriteWidths sched,
                        AVX512VLVectorVTInfo VTInfo> {
  defm Z : WriteFVarBlendask<opc, OpcodeStr, sched.ZMM, VTInfo.info512>,
           WriteFVarBlendask_rmb<opc, OpcodeStr, sched.ZMM, VTInfo.info512>,
                                 EVEX_V512;

  let Predicates = [HasVLX] in {
    defm Z256 : WriteFVarBlendask<opc, OpcodeStr, sched.YMM, VTInfo.info256>,
                WriteFVarBlendask_rmb<opc, OpcodeStr, sched.YMM, VTInfo.info256>,
                                      EVEX_V256;
    defm Z128 : WriteFVarBlendask<opc, OpcodeStr, sched.XMM, VTInfo.info128>,
                WriteFVarBlendask_rmb<opc, OpcodeStr, sched.XMM, VTInfo.info128>,
                                      EVEX_V128;
  }
}

multiclass blendmask_bw<bits<8> opc, string OpcodeStr, X86SchedWriteWidths sched,
                        AVX512VLVectorVTInfo VTInfo> {
  let Predicates = [HasBWI] in
    defm Z : WriteFVarBlendask<opc, OpcodeStr, sched.ZMM, VTInfo.info512>,
                               EVEX_V512;

  let Predicates = [HasBWI, HasVLX] in {
    defm Z256 : WriteFVarBlendask<opc, OpcodeStr, sched.YMM, VTInfo.info256>,
                                  EVEX_V256;
    defm Z128 : WriteFVarBlendask<opc, OpcodeStr, sched.XMM, VTInfo.info128>,
                                  EVEX_V128;
  }
}

defm VBLENDMPS : blendmask_dq<0x65, "vblendmps", SchedWriteFVarBlend,
                              avx512vl_f32_info>;
defm VBLENDMPD : blendmask_dq<0x65, "vblendmpd", SchedWriteFVarBlend,
                              avx512vl_f64_info>, REX_W;
defm VPBLENDMD : blendmask_dq<0x64, "vpblendmd", SchedWriteVarBlend,
                              avx512vl_i32_info>;
defm VPBLENDMQ : blendmask_dq<0x64, "vpblendmq", SchedWriteVarBlend,
                              avx512vl_i64_info>, REX_W;
defm VPBLENDMB : blendmask_bw<0x66, "vpblendmb", SchedWriteVarBlend,
                              avx512vl_i8_info>;
defm VPBLENDMW : blendmask_bw<0x66, "vpblendmw", SchedWriteVarBlend,
                              avx512vl_i16_info>, REX_W;

//===----------------------------------------------------------------------===//
// Compare Instructions
//===----------------------------------------------------------------------===//

// avx512_cmp_scalar - AVX512 CMPSS and CMPSD

multiclass avx512_cmp_scalar<X86VectorVTInfo _, SDNode OpNode, SDNode OpNodeSAE,
                             PatFrag OpNode_su, PatFrag OpNodeSAE_su,
                             X86FoldableSchedWrite sched> {
  defm  rri_Int  : AVX512_maskable_cmp<0xC2, MRMSrcReg, _,
                                       (outs _.KRC:$dst),
                                       (ins _.RC:$src1, _.RC:$src2, u8imm:$cc),
                                       "vcmp"#_.Suffix,
                                       "$cc, $src2, $src1", "$src1, $src2, $cc",
                                       (OpNode (_.VT _.RC:$src1), (_.VT _.RC:$src2), timm:$cc),
                                       (OpNode_su (_.VT _.RC:$src1), (_.VT _.RC:$src2), timm:$cc)>,
                                       EVEX, VVVV, VEX_LIG, Sched<[sched]>, SIMD_EXC;
  let mayLoad = 1 in
  defm  rmi_Int  : AVX512_maskable_cmp<0xC2, MRMSrcMem, _,
                                       (outs _.KRC:$dst),
                                       (ins _.RC:$src1, _.IntScalarMemOp:$src2, u8imm:$cc),
                                       "vcmp"#_.Suffix,
                                       "$cc, $src2, $src1", "$src1, $src2, $cc",
                                       (OpNode (_.VT _.RC:$src1), (_.ScalarIntMemFrags addr:$src2),
                                           timm:$cc),
                                       (OpNode_su (_.VT _.RC:$src1), (_.ScalarIntMemFrags addr:$src2),
                                           timm:$cc)>, EVEX, VVVV, VEX_LIG, EVEX_CD8<_.EltSize, CD8VT1>,
                                       Sched<[sched.Folded, sched.ReadAfterFold]>, SIMD_EXC;

  let Uses = [MXCSR] in
  defm  rrib_Int  : AVX512_maskable_cmp<0xC2, MRMSrcReg, _,
                                        (outs _.KRC:$dst),
                                        (ins _.RC:$src1, _.RC:$src2, u8imm:$cc),
                                        "vcmp"#_.Suffix,
                                        "$cc, {sae}, $src2, $src1","$src1, $src2, {sae}, $cc",
                                        (OpNodeSAE (_.VT _.RC:$src1), (_.VT _.RC:$src2),
                                                   timm:$cc),
                                        (OpNodeSAE_su (_.VT _.RC:$src1), (_.VT _.RC:$src2),
                                                      timm:$cc)>,
                                        EVEX, VVVV, VEX_LIG, EVEX_B, Sched<[sched]>;

  let isCodeGenOnly = 1 in {
    let isCommutable = 1 in
    def rri : AVX512Ii8<0xC2, MRMSrcReg,
                        (outs _.KRC:$dst), (ins _.FRC:$src1, _.FRC:$src2, u8imm:$cc),
                        !strconcat("vcmp", _.Suffix,
                                   "\t{$cc, $src2, $src1, $dst|$dst, $src1, $src2, $cc}"),
                        [(set _.KRC:$dst, (OpNode _.FRC:$src1,
                                                  _.FRC:$src2,
                                                  timm:$cc))]>,
                        EVEX, VVVV, VEX_LIG, Sched<[sched]>, SIMD_EXC;
    def rmi : AVX512Ii8<0xC2, MRMSrcMem,
                        (outs _.KRC:$dst),
                        (ins _.FRC:$src1, _.ScalarMemOp:$src2, u8imm:$cc),
                        !strconcat("vcmp", _.Suffix,
                                   "\t{$cc, $src2, $src1, $dst|$dst, $src1, $src2, $cc}"),
                        [(set _.KRC:$dst, (OpNode _.FRC:$src1,
                                                  (_.ScalarLdFrag addr:$src2),
                                                  timm:$cc))]>,
                        EVEX, VVVV, VEX_LIG, EVEX_CD8<_.EltSize, CD8VT1>,
                        Sched<[sched.Folded, sched.ReadAfterFold]>, SIMD_EXC;
  }
}

let Predicates = [HasAVX512] in {
  let ExeDomain = SSEPackedSingle in
  defm VCMPSSZ : avx512_cmp_scalar<f32x_info, X86cmpms, X86cmpmsSAE,
                                   X86cmpms_su, X86cmpmsSAE_su,
                                   SchedWriteFCmp.Scl>, AVX512XSIi8Base;
  let ExeDomain = SSEPackedDouble in
  defm VCMPSDZ : avx512_cmp_scalar<f64x_info, X86cmpms, X86cmpmsSAE,
                                   X86cmpms_su, X86cmpmsSAE_su,
                                   SchedWriteFCmp.Scl>, AVX512XDIi8Base, REX_W;
}
let Predicates = [HasFP16], ExeDomain = SSEPackedSingle in
  defm VCMPSHZ : avx512_cmp_scalar<f16x_info, X86cmpms, X86cmpmsSAE,
                                   X86cmpms_su, X86cmpmsSAE_su,
                                   SchedWriteFCmp.Scl>, AVX512XSIi8Base, TA;

multiclass avx512_icmp_packed<bits<8> opc, string OpcodeStr,
                              X86FoldableSchedWrite sched,
                              X86VectorVTInfo _, bit IsCommutable> {
  let isCommutable = IsCommutable, hasSideEffects = 0 in
  def rr : AVX512BI<opc, MRMSrcReg,
             (outs _.KRC:$dst), (ins _.RC:$src1, _.RC:$src2),
             !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             []>, EVEX, VVVV, Sched<[sched]>;
  let mayLoad = 1, hasSideEffects = 0 in
  def rm : AVX512BI<opc, MRMSrcMem,
             (outs _.KRC:$dst), (ins _.RC:$src1, _.MemOp:$src2),
             !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             []>, EVEX, VVVV, Sched<[sched.Folded, sched.ReadAfterFold]>;
  let isCommutable = IsCommutable, hasSideEffects = 0 in
  def rrk : AVX512BI<opc, MRMSrcReg,
              (outs _.KRC:$dst), (ins _.KRCWM:$mask, _.RC:$src1, _.RC:$src2),
              !strconcat(OpcodeStr, "\t{$src2, $src1, $dst {${mask}}|",
                          "$dst {${mask}}, $src1, $src2}"),
              []>, EVEX, VVVV, EVEX_K, Sched<[sched]>;
  let mayLoad = 1, hasSideEffects = 0 in
  def rmk : AVX512BI<opc, MRMSrcMem,
              (outs _.KRC:$dst), (ins _.KRCWM:$mask, _.RC:$src1, _.MemOp:$src2),
              !strconcat(OpcodeStr, "\t{$src2, $src1, $dst {${mask}}|",
                          "$dst {${mask}}, $src1, $src2}"),
              []>, EVEX, VVVV, EVEX_K, Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass avx512_icmp_packed_rmb<bits<8> opc, string OpcodeStr,
                                  X86FoldableSchedWrite sched, X86VectorVTInfo _,
                                  bit IsCommutable> :
           avx512_icmp_packed<opc, OpcodeStr, sched, _, IsCommutable> {
  let mayLoad = 1, hasSideEffects = 0 in {
  def rmb : AVX512BI<opc, MRMSrcMem,
              (outs _.KRC:$dst), (ins _.RC:$src1, _.ScalarMemOp:$src2),
              !strconcat(OpcodeStr, "\t{${src2}", _.BroadcastStr, ", $src1, $dst",
                                    "|$dst, $src1, ${src2}", _.BroadcastStr, "}"),
              []>, EVEX, VVVV, EVEX_B, Sched<[sched.Folded, sched.ReadAfterFold]>;
  def rmbk : AVX512BI<opc, MRMSrcMem,
               (outs _.KRC:$dst), (ins _.KRCWM:$mask, _.RC:$src1,
                                       _.ScalarMemOp:$src2),
               !strconcat(OpcodeStr,
                          "\t{${src2}", _.BroadcastStr, ", $src1, $dst {${mask}}|",
                          "$dst {${mask}}, $src1, ${src2}", _.BroadcastStr, "}"),
               []>, EVEX, VVVV, EVEX_K, EVEX_B,
               Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

multiclass avx512_icmp_packed_vl<bits<8> opc, string OpcodeStr,
                                 X86SchedWriteWidths sched,
                                 AVX512VLVectorVTInfo VTInfo, Predicate prd,
                                 bit IsCommutable = 0> {
  let Predicates = [prd] in
  defm Z : avx512_icmp_packed<opc, OpcodeStr, sched.ZMM,
                              VTInfo.info512, IsCommutable>, EVEX_V512;

  let Predicates = [prd, HasVLX] in {
    defm Z256 : avx512_icmp_packed<opc, OpcodeStr, sched.YMM,
                                   VTInfo.info256, IsCommutable>, EVEX_V256;
    defm Z128 : avx512_icmp_packed<opc, OpcodeStr, sched.XMM,
                                   VTInfo.info128, IsCommutable>, EVEX_V128;
  }
}

multiclass avx512_icmp_packed_rmb_vl<bits<8> opc, string OpcodeStr,
                                     X86SchedWriteWidths sched,
                                     AVX512VLVectorVTInfo VTInfo,
                                     Predicate prd, bit IsCommutable = 0> {
  let Predicates = [prd] in
  defm Z : avx512_icmp_packed_rmb<opc, OpcodeStr, sched.ZMM,
                                  VTInfo.info512, IsCommutable>, EVEX_V512;

  let Predicates = [prd, HasVLX] in {
    defm Z256 : avx512_icmp_packed_rmb<opc, OpcodeStr, sched.YMM,
                                       VTInfo.info256, IsCommutable>, EVEX_V256;
    defm Z128 : avx512_icmp_packed_rmb<opc, OpcodeStr, sched.XMM,
                                       VTInfo.info128, IsCommutable>, EVEX_V128;
  }
}

// AddedComplexity is needed because the explicit SETEQ/SETGT CondCode doesn't
// increase the pattern complexity the way an immediate would.
let AddedComplexity = 2 in {
// FIXME: Is there a better scheduler class for VPCMP?
defm VPCMPEQB : avx512_icmp_packed_vl<0x74, "vpcmpeqb",
                      SchedWriteVecALU, avx512vl_i8_info, HasBWI, 1>,
                EVEX_CD8<8, CD8VF>, WIG;

defm VPCMPEQW : avx512_icmp_packed_vl<0x75, "vpcmpeqw",
                      SchedWriteVecALU, avx512vl_i16_info, HasBWI, 1>,
                EVEX_CD8<16, CD8VF>, WIG;

defm VPCMPEQD : avx512_icmp_packed_rmb_vl<0x76, "vpcmpeqd",
                      SchedWriteVecALU, avx512vl_i32_info, HasAVX512, 1>,
                EVEX_CD8<32, CD8VF>;

defm VPCMPEQQ : avx512_icmp_packed_rmb_vl<0x29, "vpcmpeqq",
                      SchedWriteVecALU, avx512vl_i64_info, HasAVX512, 1>,
                T8, REX_W, EVEX_CD8<64, CD8VF>;

defm VPCMPGTB : avx512_icmp_packed_vl<0x64, "vpcmpgtb",
                      SchedWriteVecALU, avx512vl_i8_info, HasBWI>,
                EVEX_CD8<8, CD8VF>, WIG;

defm VPCMPGTW : avx512_icmp_packed_vl<0x65, "vpcmpgtw",
                      SchedWriteVecALU, avx512vl_i16_info, HasBWI>,
                EVEX_CD8<16, CD8VF>, WIG;

defm VPCMPGTD : avx512_icmp_packed_rmb_vl<0x66, "vpcmpgtd",
                      SchedWriteVecALU, avx512vl_i32_info, HasAVX512>,
                EVEX_CD8<32, CD8VF>;

defm VPCMPGTQ : avx512_icmp_packed_rmb_vl<0x37, "vpcmpgtq",
                      SchedWriteVecALU, avx512vl_i64_info, HasAVX512>,
                T8, REX_W, EVEX_CD8<64, CD8VF>;
}

multiclass avx512_icmp_cc<bits<8> opc, string Suffix, PatFrag Frag,
                          PatFrag Frag_su,
                          X86FoldableSchedWrite sched,
                          X86VectorVTInfo _, string Name> {
  let isCommutable = 1 in
  def rri : AVX512AIi8<opc, MRMSrcReg,
             (outs _.KRC:$dst), (ins _.RC:$src1, _.RC:$src2, u8imm:$cc),
             !strconcat("vpcmp", Suffix,
                        "\t{$cc, $src2, $src1, $dst|$dst, $src1, $src2, $cc}"),
             [(set _.KRC:$dst, (_.KVT (Frag:$cc (_.VT _.RC:$src1),
                                                (_.VT _.RC:$src2),
                                                cond)))]>,
             EVEX, VVVV, Sched<[sched]>;
  def rmi : AVX512AIi8<opc, MRMSrcMem,
             (outs _.KRC:$dst), (ins _.RC:$src1, _.MemOp:$src2, u8imm:$cc),
             !strconcat("vpcmp", Suffix,
                        "\t{$cc, $src2, $src1, $dst|$dst, $src1, $src2, $cc}"),
             [(set _.KRC:$dst, (_.KVT
                                (Frag:$cc
                                 (_.VT _.RC:$src1),
                                 (_.VT (_.LdFrag addr:$src2)),
                                 cond)))]>,
             EVEX, VVVV, Sched<[sched.Folded, sched.ReadAfterFold]>;
  let isCommutable = 1 in
  def rrik : AVX512AIi8<opc, MRMSrcReg,
              (outs _.KRC:$dst), (ins _.KRCWM:$mask, _.RC:$src1, _.RC:$src2,
                                      u8imm:$cc),
              !strconcat("vpcmp", Suffix,
                         "\t{$cc, $src2, $src1, $dst {${mask}}|",
                         "$dst {${mask}}, $src1, $src2, $cc}"),
              [(set _.KRC:$dst, (and _.KRCWM:$mask,
                                     (_.KVT (Frag_su:$cc (_.VT _.RC:$src1),
                                                         (_.VT _.RC:$src2),
                                                         cond))))]>,
              EVEX, VVVV, EVEX_K, Sched<[sched]>;
  def rmik : AVX512AIi8<opc, MRMSrcMem,
              (outs _.KRC:$dst), (ins _.KRCWM:$mask, _.RC:$src1, _.MemOp:$src2,
                                    u8imm:$cc),
              !strconcat("vpcmp", Suffix,
                         "\t{$cc, $src2, $src1, $dst {${mask}}|",
                         "$dst {${mask}}, $src1, $src2, $cc}"),
              [(set _.KRC:$dst, (and _.KRCWM:$mask,
                                     (_.KVT
                                      (Frag_su:$cc
                                       (_.VT _.RC:$src1),
                                       (_.VT (_.LdFrag addr:$src2)),
                                       cond))))]>,
              EVEX, VVVV, EVEX_K, Sched<[sched.Folded, sched.ReadAfterFold]>;

  def : Pat<(_.KVT (Frag:$cc (_.LdFrag addr:$src2),
                             (_.VT _.RC:$src1), cond)),
            (!cast<Instruction>(Name#_.ZSuffix#"rmi")
             _.RC:$src1, addr:$src2, (X86pcmpm_imm_commute $cc))>;

  def : Pat<(and _.KRCWM:$mask,
                 (_.KVT (Frag_su:$cc (_.LdFrag addr:$src2),
                                     (_.VT _.RC:$src1), cond))),
            (!cast<Instruction>(Name#_.ZSuffix#"rmik")
             _.KRCWM:$mask, _.RC:$src1, addr:$src2,
             (X86pcmpm_imm_commute $cc))>;
}

multiclass avx512_icmp_cc_rmb<bits<8> opc, string Suffix, PatFrag Frag,
                              PatFrag Frag_su, X86FoldableSchedWrite sched,
                              X86VectorVTInfo _, string Name> :
           avx512_icmp_cc<opc, Suffix, Frag, Frag_su, sched, _, Name> {
  def rmib : AVX512AIi8<opc, MRMSrcMem,
             (outs _.KRC:$dst), (ins _.RC:$src1, _.ScalarMemOp:$src2,
                                     u8imm:$cc),
             !strconcat("vpcmp", Suffix,
                        "\t{$cc, ${src2}", _.BroadcastStr, ", $src1, $dst|",
                        "$dst, $src1, ${src2}", _.BroadcastStr, ", $cc}"),
             [(set _.KRC:$dst, (_.KVT (Frag:$cc
                                       (_.VT _.RC:$src1),
                                       (_.BroadcastLdFrag addr:$src2),
                                       cond)))]>,
             EVEX, VVVV, EVEX_B, Sched<[sched.Folded, sched.ReadAfterFold]>;
  def rmibk : AVX512AIi8<opc, MRMSrcMem,
              (outs _.KRC:$dst), (ins _.KRCWM:$mask, _.RC:$src1,
                                       _.ScalarMemOp:$src2, u8imm:$cc),
              !strconcat("vpcmp", Suffix,
                  "\t{$cc, ${src2}", _.BroadcastStr, ", $src1, $dst {${mask}}|",
                  "$dst {${mask}}, $src1, ${src2}", _.BroadcastStr, ", $cc}"),
              [(set _.KRC:$dst, (and _.KRCWM:$mask,
                                     (_.KVT (Frag_su:$cc
                                             (_.VT _.RC:$src1),
                                             (_.BroadcastLdFrag addr:$src2),
                                             cond))))]>,
              EVEX, VVVV, EVEX_K, EVEX_B, Sched<[sched.Folded, sched.ReadAfterFold]>;

  def : Pat<(_.KVT (Frag:$cc (_.BroadcastLdFrag addr:$src2),
                    (_.VT _.RC:$src1), cond)),
            (!cast<Instruction>(Name#_.ZSuffix#"rmib")
             _.RC:$src1, addr:$src2, (X86pcmpm_imm_commute $cc))>;

  def : Pat<(and _.KRCWM:$mask,
                 (_.KVT (Frag_su:$cc (_.BroadcastLdFrag addr:$src2),
                                     (_.VT _.RC:$src1), cond))),
            (!cast<Instruction>(Name#_.ZSuffix#"rmibk")
             _.KRCWM:$mask, _.RC:$src1, addr:$src2,
             (X86pcmpm_imm_commute $cc))>;
}

multiclass avx512_icmp_cc_vl<bits<8> opc, string Suffix, PatFrag Frag,
                             PatFrag Frag_su, X86SchedWriteWidths sched,
                             AVX512VLVectorVTInfo VTInfo, Predicate prd> {
  let Predicates = [prd] in
  defm Z : avx512_icmp_cc<opc, Suffix, Frag, Frag_su,
                          sched.ZMM, VTInfo.info512, NAME>, EVEX_V512;

  let Predicates = [prd, HasVLX] in {
    defm Z256 : avx512_icmp_cc<opc, Suffix, Frag, Frag_su,
                               sched.YMM, VTInfo.info256, NAME>, EVEX_V256;
    defm Z128 : avx512_icmp_cc<opc, Suffix, Frag, Frag_su,
                               sched.XMM, VTInfo.info128, NAME>, EVEX_V128;
  }
}

multiclass avx512_icmp_cc_rmb_vl<bits<8> opc, string Suffix, PatFrag Frag,
                                 PatFrag Frag_su, X86SchedWriteWidths sched,
                                 AVX512VLVectorVTInfo VTInfo, Predicate prd> {
  let Predicates = [prd] in
  defm Z : avx512_icmp_cc_rmb<opc, Suffix, Frag, Frag_su,
                              sched.ZMM, VTInfo.info512, NAME>, EVEX_V512;

  let Predicates = [prd, HasVLX] in {
    defm Z256 : avx512_icmp_cc_rmb<opc, Suffix, Frag, Frag_su,
                                   sched.YMM, VTInfo.info256, NAME>, EVEX_V256;
    defm Z128 : avx512_icmp_cc_rmb<opc, Suffix, Frag, Frag_su,
                                   sched.XMM, VTInfo.info128, NAME>, EVEX_V128;
  }
}

// FIXME: Is there a better scheduler class for VPCMP/VPCMPU?
defm VPCMPB : avx512_icmp_cc_vl<0x3F, "b", X86pcmpm, X86pcmpm_su,
                                SchedWriteVecALU, avx512vl_i8_info, HasBWI>,
                                EVEX_CD8<8, CD8VF>;
defm VPCMPUB : avx512_icmp_cc_vl<0x3E, "ub", X86pcmpum, X86pcmpum_su,
                                 SchedWriteVecALU, avx512vl_i8_info, HasBWI>,
                                 EVEX_CD8<8, CD8VF>;

defm VPCMPW : avx512_icmp_cc_vl<0x3F, "w", X86pcmpm, X86pcmpm_su,
                                SchedWriteVecALU, avx512vl_i16_info, HasBWI>,
                                REX_W, EVEX_CD8<16, CD8VF>;
defm VPCMPUW : avx512_icmp_cc_vl<0x3E, "uw", X86pcmpum, X86pcmpum_su,
                                 SchedWriteVecALU, avx512vl_i16_info, HasBWI>,
                                 REX_W, EVEX_CD8<16, CD8VF>;

defm VPCMPD : avx512_icmp_cc_rmb_vl<0x1F, "d", X86pcmpm, X86pcmpm_su,
                                    SchedWriteVecALU, avx512vl_i32_info,
                                    HasAVX512>, EVEX_CD8<32, CD8VF>;
defm VPCMPUD : avx512_icmp_cc_rmb_vl<0x1E, "ud", X86pcmpum, X86pcmpum_su,
                                     SchedWriteVecALU, avx512vl_i32_info,
                                     HasAVX512>, EVEX_CD8<32, CD8VF>;

defm VPCMPQ : avx512_icmp_cc_rmb_vl<0x1F, "q", X86pcmpm, X86pcmpm_su,
                                    SchedWriteVecALU, avx512vl_i64_info,
                                    HasAVX512>, REX_W, EVEX_CD8<64, CD8VF>;
defm VPCMPUQ : avx512_icmp_cc_rmb_vl<0x1E, "uq", X86pcmpum, X86pcmpum_su,
                                     SchedWriteVecALU, avx512vl_i64_info,
                                     HasAVX512>, REX_W, EVEX_CD8<64, CD8VF>;

multiclass avx512_vcmp_common<X86FoldableSchedWrite sched, X86VectorVTInfo _,
                              string Name> {
let Uses = [MXCSR], mayRaiseFPException = 1 in {
  defm  rri  : AVX512_maskable_cmp<0xC2, MRMSrcReg, _,
                   (outs _.KRC:$dst), (ins _.RC:$src1, _.RC:$src2,u8imm:$cc),
                   "vcmp"#_.Suffix,
                   "$cc, $src2, $src1", "$src1, $src2, $cc",
                   (X86any_cmpm (_.VT _.RC:$src1), (_.VT _.RC:$src2), timm:$cc),
                   (X86cmpm_su (_.VT _.RC:$src1), (_.VT _.RC:$src2), timm:$cc),
                   1>, Sched<[sched]>;

  defm  rmi  : AVX512_maskable_cmp<0xC2, MRMSrcMem, _,
                (outs _.KRC:$dst),(ins _.RC:$src1, _.MemOp:$src2, u8imm:$cc),
                "vcmp"#_.Suffix,
                "$cc, $src2, $src1", "$src1, $src2, $cc",
                (X86any_cmpm (_.VT _.RC:$src1), (_.VT (_.LdFrag addr:$src2)),
                             timm:$cc),
                (X86cmpm_su (_.VT _.RC:$src1), (_.VT (_.LdFrag addr:$src2)),
                            timm:$cc)>,
                Sched<[sched.Folded, sched.ReadAfterFold]>;

  defm  rmbi : AVX512_maskable_cmp<0xC2, MRMSrcMem, _,
                (outs _.KRC:$dst),
                (ins _.RC:$src1, _.ScalarMemOp:$src2, u8imm:$cc),
                "vcmp"#_.Suffix,
                "$cc, ${src2}"#_.BroadcastStr#", $src1",
                "$src1, ${src2}"#_.BroadcastStr#", $cc",
                (X86any_cmpm (_.VT _.RC:$src1),
                             (_.VT (_.BroadcastLdFrag addr:$src2)),
                             timm:$cc),
                (X86cmpm_su (_.VT _.RC:$src1),
                            (_.VT (_.BroadcastLdFrag addr:$src2)),
                            timm:$cc)>,
                EVEX_B, Sched<[sched.Folded, sched.ReadAfterFold]>;
  }

  // Patterns for selecting with loads in other operand.
  def : Pat<(X86any_cmpm (_.LdFrag addr:$src2), (_.VT _.RC:$src1),
                         timm:$cc),
            (!cast<Instruction>(Name#_.ZSuffix#"rmi") _.RC:$src1, addr:$src2,
                                                      (X86cmpm_imm_commute timm:$cc))>;

  def : Pat<(and _.KRCWM:$mask, (X86cmpm_su (_.LdFrag addr:$src2),
                                            (_.VT _.RC:$src1),
                                            timm:$cc)),
            (!cast<Instruction>(Name#_.ZSuffix#"rmik") _.KRCWM:$mask,
                                                       _.RC:$src1, addr:$src2,
                                                       (X86cmpm_imm_commute timm:$cc))>;

  def : Pat<(X86any_cmpm (_.BroadcastLdFrag addr:$src2),
                         (_.VT _.RC:$src1), timm:$cc),
            (!cast<Instruction>(Name#_.ZSuffix#"rmbi") _.RC:$src1, addr:$src2,
                                                       (X86cmpm_imm_commute timm:$cc))>;

  def : Pat<(and _.KRCWM:$mask, (X86cmpm_su (_.BroadcastLdFrag addr:$src2),
                                            (_.VT _.RC:$src1),
                                            timm:$cc)),
            (!cast<Instruction>(Name#_.ZSuffix#"rmbik") _.KRCWM:$mask,
                                                        _.RC:$src1, addr:$src2,
                                                        (X86cmpm_imm_commute timm:$cc))>;

  // Patterns for mask intrinsics.
  def : Pat<(X86cmpmm (_.VT _.RC:$src1), (_.VT _.RC:$src2), timm:$cc,
                      (_.KVT immAllOnesV)),
            (!cast<Instruction>(Name#_.ZSuffix#"rri") _.RC:$src1, _.RC:$src2, timm:$cc)>;

  def : Pat<(X86cmpmm (_.VT _.RC:$src1), (_.VT _.RC:$src2), timm:$cc, _.KRCWM:$mask),
            (!cast<Instruction>(Name#_.ZSuffix#"rrik") _.KRCWM:$mask, _.RC:$src1,
                                                       _.RC:$src2, timm:$cc)>;

  def : Pat<(X86cmpmm (_.VT _.RC:$src1), (_.VT (_.LdFrag addr:$src2)), timm:$cc,
                      (_.KVT immAllOnesV)),
            (!cast<Instruction>(Name#_.ZSuffix#"rmi") _.RC:$src1, addr:$src2, timm:$cc)>;

  def : Pat<(X86cmpmm (_.VT _.RC:$src1), (_.VT (_.LdFrag addr:$src2)), timm:$cc,
                      _.KRCWM:$mask),
            (!cast<Instruction>(Name#_.ZSuffix#"rmik") _.KRCWM:$mask, _.RC:$src1,
                                                       addr:$src2, timm:$cc)>;

  def : Pat<(X86cmpmm (_.VT _.RC:$src1), (_.VT (_.BroadcastLdFrag addr:$src2)), timm:$cc,
                      (_.KVT immAllOnesV)),
            (!cast<Instruction>(Name#_.ZSuffix#"rmbi") _.RC:$src1, addr:$src2, timm:$cc)>;

  def : Pat<(X86cmpmm (_.VT _.RC:$src1), (_.VT (_.BroadcastLdFrag addr:$src2)), timm:$cc,
                      _.KRCWM:$mask),
            (!cast<Instruction>(Name#_.ZSuffix#"rmbik") _.KRCWM:$mask, _.RC:$src1,
                                                        addr:$src2, timm:$cc)>;

  // Patterns for mask intrinsics with loads in other operand.
  def : Pat<(X86cmpmm (_.VT (_.LdFrag addr:$src2)), (_.VT _.RC:$src1), timm:$cc,
                      (_.KVT immAllOnesV)),
            (!cast<Instruction>(Name#_.ZSuffix#"rmi") _.RC:$src1, addr:$src2,
                                                      (X86cmpm_imm_commute timm:$cc))>;

  def : Pat<(X86cmpmm (_.VT (_.LdFrag addr:$src2)), (_.VT _.RC:$src1), timm:$cc,
                      _.KRCWM:$mask),
            (!cast<Instruction>(Name#_.ZSuffix#"rmik") _.KRCWM:$mask,
                                                       _.RC:$src1, addr:$src2,
                                                       (X86cmpm_imm_commute timm:$cc))>;

  def : Pat<(X86cmpmm (_.VT (_.BroadcastLdFrag addr:$src2)), (_.VT _.RC:$src1), timm:$cc,
                      (_.KVT immAllOnesV)),
            (!cast<Instruction>(Name#_.ZSuffix#"rmbi") _.RC:$src1, addr:$src2,
                                                       (X86cmpm_imm_commute timm:$cc))>;

  def : Pat<(X86cmpmm (_.VT (_.BroadcastLdFrag addr:$src2)), (_.VT _.RC:$src1), timm:$cc,
                      _.KRCWM:$mask),
            (!cast<Instruction>(Name#_.ZSuffix#"rmbik") _.KRCWM:$mask,
                                                        _.RC:$src1, addr:$src2,
                                                        (X86cmpm_imm_commute  timm:$cc))>;
}

multiclass avx512_vcmp_sae<X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  // comparison code form (VCMP[EQ/LT/LE/...]
  let Uses = [MXCSR] in
  defm  rrib  : AVX512_maskable_custom_cmp<0xC2, MRMSrcReg, (outs _.KRC:$dst),
                     (ins _.RC:$src1, _.RC:$src2, u8imm:$cc),
                     (ins _.KRCWM:$mask, _.RC:$src1, _.RC:$src2, u8imm:$cc),
                     "vcmp"#_.Suffix,
                     "$cc, {sae}, $src2, $src1",
                     "$src1, $src2, {sae}, $cc",
                     [(set _.KRC:$dst, (X86cmpmmSAE (_.VT _.RC:$src1),
                                        (_.VT _.RC:$src2), timm:$cc, (_.KVT immAllOnesV)))],
                     [(set _.KRC:$dst, (X86cmpmmSAE (_.VT _.RC:$src1),
                                        (_.VT _.RC:$src2), timm:$cc, _.KRCWM:$mask))]>,
                     EVEX_B, Sched<[sched]>;
}

multiclass avx512_vcmp<X86SchedWriteWidths sched, AVX512VLVectorVTInfo _,
                       Predicate Pred = HasAVX512> {
  let Predicates = [Pred] in {
    defm Z    : avx512_vcmp_common<sched.ZMM, _.info512, NAME>,
                avx512_vcmp_sae<sched.ZMM, _.info512>, EVEX_V512;

  }
  let Predicates = [Pred,HasVLX] in {
   defm Z128 : avx512_vcmp_common<sched.XMM, _.info128, NAME>, EVEX_V128;
   defm Z256 : avx512_vcmp_common<sched.YMM, _.info256, NAME>, EVEX_V256;
  }
}

defm VCMPPD : avx512_vcmp<SchedWriteFCmp, avx512vl_f64_info>,
                          AVX512PDIi8Base, EVEX, VVVV, EVEX_CD8<64, CD8VF>, REX_W;
defm VCMPPS : avx512_vcmp<SchedWriteFCmp, avx512vl_f32_info>,
                          AVX512PSIi8Base, EVEX, VVVV, EVEX_CD8<32, CD8VF>;
defm VCMPPH : avx512_vcmp<SchedWriteFCmp, avx512vl_f16_info, HasFP16>,
                          AVX512PSIi8Base, EVEX, VVVV, EVEX_CD8<16, CD8VF>, TA;

// Patterns to select fp compares with load as first operand.
let Predicates = [HasAVX512] in {
  def : Pat<(v1i1 (X86cmpms (loadf64 addr:$src2), FR64X:$src1, timm:$cc)),
            (VCMPSDZrmi FR64X:$src1, addr:$src2, (X86cmpm_imm_commute timm:$cc))>;

  def : Pat<(v1i1 (X86cmpms (loadf32 addr:$src2), FR32X:$src1, timm:$cc)),
            (VCMPSSZrmi FR32X:$src1, addr:$src2, (X86cmpm_imm_commute timm:$cc))>;
}

let Predicates = [HasFP16] in {
  def : Pat<(v1i1 (X86cmpms (loadf16 addr:$src2), FR16X:$src1, timm:$cc)),
            (VCMPSHZrmi FR16X:$src1, addr:$src2, (X86cmpm_imm_commute timm:$cc))>;
}

// ----------------------------------------------------------------
// FPClass

//handle fpclass instruction  mask =  op(reg_scalar,imm)
//                                    op(mem_scalar,imm)
multiclass avx512_scalar_fpclass<bits<8> opc, string OpcodeStr,
                                 X86FoldableSchedWrite sched, X86VectorVTInfo _,
                                 Predicate prd> {
  let Predicates = [prd], ExeDomain = _.ExeDomain, Uses = [MXCSR] in {
      def rr : AVX512<opc, MRMSrcReg, (outs _.KRC:$dst),
                      (ins _.RC:$src1, i32u8imm:$src2),
                      OpcodeStr#_.Suffix#"\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                      [(set _.KRC:$dst,(X86Vfpclasss (_.VT _.RC:$src1),
                              (i32 timm:$src2)))]>,
                      Sched<[sched]>;
      def rrk : AVX512<opc, MRMSrcReg, (outs _.KRC:$dst),
                      (ins _.KRCWM:$mask, _.RC:$src1, i32u8imm:$src2),
                      OpcodeStr#_.Suffix#
                      "\t{$src2, $src1, $dst {${mask}}|$dst {${mask}}, $src1, $src2}",
                      [(set _.KRC:$dst,(and _.KRCWM:$mask,
                                      (X86Vfpclasss_su (_.VT _.RC:$src1),
                                      (i32 timm:$src2))))]>,
                      EVEX_K, Sched<[sched]>;
    def rm : AVX512<opc, MRMSrcMem, (outs _.KRC:$dst),
                    (ins _.IntScalarMemOp:$src1, i32u8imm:$src2),
                    OpcodeStr#_.Suffix#
                              "\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                    [(set _.KRC:$dst,
                          (X86Vfpclasss (_.ScalarIntMemFrags addr:$src1),
                                        (i32 timm:$src2)))]>,
                    Sched<[sched.Folded, sched.ReadAfterFold]>;
    def rmk : AVX512<opc, MRMSrcMem, (outs _.KRC:$dst),
                    (ins _.KRCWM:$mask, _.IntScalarMemOp:$src1, i32u8imm:$src2),
                    OpcodeStr#_.Suffix#
                    "\t{$src2, $src1, $dst {${mask}}|$dst {${mask}}, $src1, $src2}",
                    [(set _.KRC:$dst,(and _.KRCWM:$mask,
                        (X86Vfpclasss_su (_.ScalarIntMemFrags addr:$src1),
                            (i32 timm:$src2))))]>,
                    EVEX_K, Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

//handle fpclass instruction mask = fpclass(reg_vec, reg_vec, imm)
//                                  fpclass(reg_vec, mem_vec, imm)
//                                  fpclass(reg_vec, broadcast(eltVt), imm)
multiclass avx512_vector_fpclass<bits<8> opc, string OpcodeStr,
                                 X86FoldableSchedWrite sched, X86VectorVTInfo _,
                                 string mem>{
  let ExeDomain = _.ExeDomain, Uses = [MXCSR] in {
  def rr : AVX512<opc, MRMSrcReg, (outs _.KRC:$dst),
                      (ins _.RC:$src1, i32u8imm:$src2),
                      OpcodeStr#_.Suffix#"\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                      [(set _.KRC:$dst,(X86Vfpclass (_.VT _.RC:$src1),
                                       (i32 timm:$src2)))]>,
                      Sched<[sched]>;
  def rrk : AVX512<opc, MRMSrcReg, (outs _.KRC:$dst),
                      (ins _.KRCWM:$mask, _.RC:$src1, i32u8imm:$src2),
                      OpcodeStr#_.Suffix#
                      "\t{$src2, $src1, $dst {${mask}}|$dst {${mask}}, $src1, $src2}",
                      [(set _.KRC:$dst,(and _.KRCWM:$mask,
                                       (X86Vfpclass_su (_.VT _.RC:$src1),
                                       (i32 timm:$src2))))]>,
                      EVEX_K, Sched<[sched]>;
  def rm : AVX512<opc, MRMSrcMem, (outs _.KRC:$dst),
                    (ins _.MemOp:$src1, i32u8imm:$src2),
                    OpcodeStr#_.Suffix#"{"#mem#"}"#
                    "\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                    [(set _.KRC:$dst,(X86Vfpclass
                                     (_.VT (_.LdFrag addr:$src1)),
                                     (i32 timm:$src2)))]>,
                    Sched<[sched.Folded, sched.ReadAfterFold]>;
  def rmk : AVX512<opc, MRMSrcMem, (outs _.KRC:$dst),
                    (ins _.KRCWM:$mask, _.MemOp:$src1, i32u8imm:$src2),
                    OpcodeStr#_.Suffix#"{"#mem#"}"#
                    "\t{$src2, $src1, $dst {${mask}}|$dst {${mask}}, $src1, $src2}",
                    [(set _.KRC:$dst, (and _.KRCWM:$mask, (X86Vfpclass_su
                                  (_.VT (_.LdFrag addr:$src1)),
                                  (i32 timm:$src2))))]>,
                    EVEX_K, Sched<[sched.Folded, sched.ReadAfterFold]>;
  def rmb : AVX512<opc, MRMSrcMem, (outs _.KRC:$dst),
                    (ins _.ScalarMemOp:$src1, i32u8imm:$src2),
                    OpcodeStr#_.Suffix#"\t{$src2, ${src1}"#
                                      _.BroadcastStr#", $dst|$dst, ${src1}"
                                                  #_.BroadcastStr#", $src2}",
                    [(set _.KRC:$dst,(X86Vfpclass
                                     (_.VT (_.BroadcastLdFrag addr:$src1)),
                                     (i32 timm:$src2)))]>,
                    EVEX_B, Sched<[sched.Folded, sched.ReadAfterFold]>;
  def rmbk : AVX512<opc, MRMSrcMem, (outs _.KRC:$dst),
                    (ins _.KRCWM:$mask, _.ScalarMemOp:$src1, i32u8imm:$src2),
                    OpcodeStr#_.Suffix#"\t{$src2, ${src1}"#
                          _.BroadcastStr#", $dst {${mask}}|$dst {${mask}}, ${src1}"#
                                                   _.BroadcastStr#", $src2}",
                    [(set _.KRC:$dst,(and _.KRCWM:$mask, (X86Vfpclass_su
                                     (_.VT (_.BroadcastLdFrag addr:$src1)),
                                     (i32 timm:$src2))))]>,
                    EVEX_B, EVEX_K,  Sched<[sched.Folded, sched.ReadAfterFold]>;
  }

  // Allow registers or broadcast with the x, y, z suffix we use to disambiguate
  // the memory form.
  def : InstAlias<OpcodeStr#_.Suffix#mem#
                  "\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                  (!cast<Instruction>(NAME#"rr")
                   _.KRC:$dst, _.RC:$src1, i32u8imm:$src2), 0, "att">;
  def : InstAlias<OpcodeStr#_.Suffix#mem#
                  "\t{$src2, $src1, $dst {${mask}}|$dst {${mask}}, $src1, $src2}",
                  (!cast<Instruction>(NAME#"rrk")
                   _.KRC:$dst, _.KRCWM:$mask, _.RC:$src1, i32u8imm:$src2), 0, "att">;
  def : InstAlias<OpcodeStr#_.Suffix#mem#
                  "\t{$src2, ${src1}"#_.BroadcastStr#", $dst|$dst, ${src1}"#
                  _.BroadcastStr#", $src2}",
                  (!cast<Instruction>(NAME#"rmb")
                   _.KRC:$dst, _.ScalarMemOp:$src1, i32u8imm:$src2), 0, "att">;
  def : InstAlias<OpcodeStr#_.Suffix#mem#
                  "\t{$src2, ${src1}"#_.BroadcastStr#", $dst {${mask}}|"
                  "$dst {${mask}}, ${src1}"#_.BroadcastStr#", $src2}",
                  (!cast<Instruction>(NAME#"rmbk")
                   _.KRC:$dst, _.KRCWM:$mask, _.ScalarMemOp:$src1, i32u8imm:$src2), 0, "att">;
}

multiclass avx512_vector_fpclass_all<string OpcodeStr, AVX512VLVectorVTInfo _,
                                     bits<8> opc, X86SchedWriteWidths sched,
                                     Predicate prd>{
  let Predicates = [prd] in {
    defm Z    : avx512_vector_fpclass<opc, OpcodeStr, sched.ZMM,
                                      _.info512, "z">, EVEX_V512;
  }
  let Predicates = [prd, HasVLX] in {
    defm Z128 : avx512_vector_fpclass<opc, OpcodeStr, sched.XMM,
                                      _.info128, "x">, EVEX_V128;
    defm Z256 : avx512_vector_fpclass<opc, OpcodeStr, sched.YMM,
                                      _.info256, "y">, EVEX_V256;
  }
}

multiclass avx512_fp_fpclass_all<string OpcodeStr, bits<8> opcVec,
                                 bits<8> opcScalar, X86SchedWriteWidths sched> {
  defm PH : avx512_vector_fpclass_all<OpcodeStr,  avx512vl_f16_info, opcVec,
                                      sched, HasFP16>,
                                      EVEX_CD8<16, CD8VF>, AVX512PSIi8Base, TA;
  defm SHZ : avx512_scalar_fpclass<opcScalar, OpcodeStr,
                                   sched.Scl, f16x_info, HasFP16>,
                                   EVEX_CD8<16, CD8VT1>, AVX512PSIi8Base, TA;
  defm PS : avx512_vector_fpclass_all<OpcodeStr,  avx512vl_f32_info, opcVec,
                                      sched, HasDQI>,
                                      EVEX_CD8<32, CD8VF>, AVX512AIi8Base;
  defm PD : avx512_vector_fpclass_all<OpcodeStr,  avx512vl_f64_info, opcVec,
                                      sched, HasDQI>,
                                      EVEX_CD8<64, CD8VF>, AVX512AIi8Base, REX_W;
  defm SSZ : avx512_scalar_fpclass<opcScalar, OpcodeStr,
                                   sched.Scl, f32x_info, HasDQI>, VEX_LIG,
                                   EVEX_CD8<32, CD8VT1>, AVX512AIi8Base;
  defm SDZ : avx512_scalar_fpclass<opcScalar, OpcodeStr,
                                   sched.Scl, f64x_info, HasDQI>, VEX_LIG,
                                   EVEX_CD8<64, CD8VT1>, AVX512AIi8Base, REX_W;
}

defm VFPCLASS : avx512_fp_fpclass_all<"vfpclass", 0x66, 0x67, SchedWriteFCmp>, EVEX;

//-----------------------------------------------------------------
// Mask register copy, including
// - copy between mask registers
// - load/store mask registers
// - copy from GPR to mask register and vice versa
//
multiclass avx512_mask_mov<bits<8> opc_kk, bits<8> opc_km, bits<8> opc_mk,
                          string OpcodeStr, RegisterClass KRC, ValueType vvt,
                          X86MemOperand x86memop, string Suffix = ""> {
  let isMoveReg = 1, hasSideEffects = 0, SchedRW = [WriteMove],
      explicitOpPrefix = !if(!eq(Suffix, ""), NoExplicitOpPrefix, ExplicitEVEX) in
  def kk#Suffix : I<opc_kk, MRMSrcReg, (outs KRC:$dst), (ins KRC:$src),
                    !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"), []>,
                  Sched<[WriteMove]>;
  def km#Suffix : I<opc_km, MRMSrcMem, (outs KRC:$dst), (ins x86memop:$src),
                    !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                    [(set KRC:$dst, (vvt (load addr:$src)))]>,
                  Sched<[WriteLoad]>, NoCD8;
  def mk#Suffix : I<opc_mk, MRMDestMem, (outs), (ins x86memop:$dst, KRC:$src),
                    !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                    [(store KRC:$src, addr:$dst)]>,
                  Sched<[WriteStore]>, NoCD8;
}

multiclass avx512_mask_mov_gpr<bits<8> opc_kr, bits<8> opc_rk,
                               string OpcodeStr, RegisterClass KRC,
                               RegisterClass GRC, string Suffix = ""> {
  let hasSideEffects = 0 in {
    def kr#Suffix : I<opc_kr, MRMSrcReg, (outs KRC:$dst), (ins GRC:$src),
                      !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"), []>,
                    Sched<[WriteMove]>;
    def rk#Suffix : I<opc_rk, MRMSrcReg, (outs GRC:$dst), (ins KRC:$src),
                      !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"), []>,
                    Sched<[WriteMove]>;
  }
}

let Predicates = [HasDQI, NoEGPR] in
  defm KMOVB : avx512_mask_mov<0x90, 0x90, 0x91, "kmovb", VK8, v8i1, i8mem>,
               avx512_mask_mov_gpr<0x92, 0x93, "kmovb", VK8, GR32>,
               VEX, TB, PD;
let Predicates = [HasDQI, HasEGPR, In64BitMode] in
  defm KMOVB : avx512_mask_mov<0x90, 0x90, 0x91, "kmovb", VK8, v8i1, i8mem, "_EVEX">,
               avx512_mask_mov_gpr<0x92, 0x93, "kmovb", VK8, GR32, "_EVEX">,
               EVEX, TB, PD;

let Predicates = [HasAVX512, NoEGPR] in
  defm KMOVW : avx512_mask_mov<0x90, 0x90, 0x91, "kmovw", VK16, v16i1, i16mem>,
               avx512_mask_mov_gpr<0x92, 0x93, "kmovw", VK16, GR32>,
               VEX, TB;
let Predicates = [HasAVX512, HasEGPR, In64BitMode] in
  defm KMOVW : avx512_mask_mov<0x90, 0x90, 0x91, "kmovw", VK16, v16i1, i16mem, "_EVEX">,
               avx512_mask_mov_gpr<0x92, 0x93, "kmovw", VK16, GR32, "_EVEX">,
               EVEX, TB;

let Predicates = [HasBWI, NoEGPR] in {
  defm KMOVD : avx512_mask_mov<0x90, 0x90, 0x91, "kmovd", VK32, v32i1,i32mem>,
               VEX, TB, PD, REX_W;
  defm KMOVD : avx512_mask_mov_gpr<0x92, 0x93, "kmovd", VK32, GR32>,
               VEX, TB, XD;
  defm KMOVQ : avx512_mask_mov<0x90, 0x90, 0x91, "kmovq", VK64, v64i1, i64mem>,
               VEX, TB, REX_W;
  defm KMOVQ : avx512_mask_mov_gpr<0x92, 0x93, "kmovq", VK64, GR64>,
               VEX, TB, XD, REX_W;
}
let Predicates = [HasBWI, HasEGPR, In64BitMode] in {
  defm KMOVD : avx512_mask_mov<0x90, 0x90, 0x91, "kmovd", VK32, v32i1,i32mem, "_EVEX">,
               EVEX, TB, PD, REX_W;
  defm KMOVD : avx512_mask_mov_gpr<0x92, 0x93, "kmovd", VK32, GR32, "_EVEX">,
               EVEX, TB, XD;
  defm KMOVQ : avx512_mask_mov<0x90, 0x90, 0x91, "kmovq", VK64, v64i1, i64mem, "_EVEX">,
               EVEX, TB, REX_W;
  defm KMOVQ : avx512_mask_mov_gpr<0x92, 0x93, "kmovq", VK64, GR64, "_EVEX">,
               EVEX, TB, XD, REX_W;
}

// GR from/to mask register
def : Pat<(v16i1 (bitconvert (i16 GR16:$src))),
          (COPY_TO_REGCLASS (i32 (INSERT_SUBREG (IMPLICIT_DEF), GR16:$src, sub_16bit)), VK16)>;
def : Pat<(i16 (bitconvert (v16i1 VK16:$src))),
          (EXTRACT_SUBREG (i32 (COPY_TO_REGCLASS VK16:$src, GR32)), sub_16bit)>;
def : Pat<(i8 (trunc (i16 (bitconvert (v16i1 VK16:$src))))),
          (EXTRACT_SUBREG (i32 (COPY_TO_REGCLASS VK16:$src, GR32)), sub_8bit)>;

def : Pat<(v8i1 (bitconvert (i8 GR8:$src))),
          (COPY_TO_REGCLASS (i32 (INSERT_SUBREG (IMPLICIT_DEF), GR8:$src, sub_8bit)), VK8)>;
def : Pat<(i8 (bitconvert (v8i1 VK8:$src))),
          (EXTRACT_SUBREG (i32 (COPY_TO_REGCLASS VK8:$src, GR32)), sub_8bit)>;

def : Pat<(i32 (zext (i16 (bitconvert (v16i1 VK16:$src))))),
          (KMOVWrk VK16:$src)>;
def : Pat<(i64 (zext (i16 (bitconvert (v16i1 VK16:$src))))),
          (SUBREG_TO_REG (i64 0), (KMOVWrk VK16:$src), sub_32bit)>;
def : Pat<(i32 (anyext (i16 (bitconvert (v16i1 VK16:$src))))),
          (COPY_TO_REGCLASS VK16:$src, GR32)>;
def : Pat<(i64 (anyext (i16 (bitconvert (v16i1 VK16:$src))))),
          (INSERT_SUBREG (IMPLICIT_DEF), (COPY_TO_REGCLASS VK16:$src, GR32), sub_32bit)>;

def : Pat<(i32 (zext (i8 (bitconvert (v8i1 VK8:$src))))),
          (KMOVBrk VK8:$src)>, Requires<[HasDQI]>;
def : Pat<(i64 (zext (i8 (bitconvert (v8i1 VK8:$src))))),
          (SUBREG_TO_REG (i64 0), (KMOVBrk VK8:$src), sub_32bit)>, Requires<[HasDQI]>;
def : Pat<(i32 (anyext (i8 (bitconvert (v8i1 VK8:$src))))),
          (COPY_TO_REGCLASS VK8:$src, GR32)>;
def : Pat<(i64 (anyext (i8 (bitconvert (v8i1 VK8:$src))))),
          (INSERT_SUBREG (IMPLICIT_DEF), (COPY_TO_REGCLASS VK8:$src, GR32), sub_32bit)>;

def : Pat<(v32i1 (bitconvert (i32 GR32:$src))),
          (COPY_TO_REGCLASS GR32:$src, VK32)>;
def : Pat<(i32 (bitconvert (v32i1 VK32:$src))),
          (COPY_TO_REGCLASS VK32:$src, GR32)>;
def : Pat<(v64i1 (bitconvert (i64 GR64:$src))),
          (COPY_TO_REGCLASS GR64:$src, VK64)>;
def : Pat<(i64 (bitconvert (v64i1 VK64:$src))),
          (COPY_TO_REGCLASS VK64:$src, GR64)>;

// Load/store kreg
let Predicates = [HasDQI] in {
  def : Pat<(v1i1 (load addr:$src)),
            (COPY_TO_REGCLASS (KMOVBkm addr:$src), VK1)>;
  def : Pat<(v2i1 (load addr:$src)),
            (COPY_TO_REGCLASS (KMOVBkm addr:$src), VK2)>;
  def : Pat<(v4i1 (load addr:$src)),
            (COPY_TO_REGCLASS (KMOVBkm addr:$src), VK4)>;
}

let Predicates = [HasAVX512] in {
  def : Pat<(v8i1 (bitconvert (i8 (load addr:$src)))),
            (COPY_TO_REGCLASS (MOVZX32rm8 addr:$src), VK8)>;
  def : Pat<(v16i1 (bitconvert (loadi16 addr:$src))),
            (KMOVWkm addr:$src)>;
}

def X86kextract : SDNode<"ISD::EXTRACT_VECTOR_ELT",
                         SDTypeProfile<1, 2, [SDTCisVT<0, i8>,
                                              SDTCVecEltisVT<1, i1>,
                                              SDTCisPtrTy<2>]>>;

let Predicates = [HasAVX512] in {
  multiclass operation_gpr_mask_copy_lowering<RegisterClass maskRC, ValueType maskVT> {
    def : Pat<(maskVT (scalar_to_vector GR32:$src)),
              (COPY_TO_REGCLASS GR32:$src, maskRC)>;

    def : Pat<(maskVT (scalar_to_vector GR8:$src)),
              (COPY_TO_REGCLASS (INSERT_SUBREG (i32 (IMPLICIT_DEF)), GR8:$src, sub_8bit), maskRC)>;

    def : Pat<(i8 (X86kextract maskRC:$src, (iPTR 0))),
              (EXTRACT_SUBREG (i32 (COPY_TO_REGCLASS maskRC:$src, GR32)), sub_8bit)>;

    def : Pat<(i32 (anyext (i8 (X86kextract maskRC:$src, (iPTR 0))))),
              (i32 (COPY_TO_REGCLASS maskRC:$src, GR32))>;
  }

  defm : operation_gpr_mask_copy_lowering<VK1,  v1i1>;
  defm : operation_gpr_mask_copy_lowering<VK2,  v2i1>;
  defm : operation_gpr_mask_copy_lowering<VK4,  v4i1>;
  defm : operation_gpr_mask_copy_lowering<VK8,  v8i1>;
  defm : operation_gpr_mask_copy_lowering<VK16,  v16i1>;
  defm : operation_gpr_mask_copy_lowering<VK32,  v32i1>;
  defm : operation_gpr_mask_copy_lowering<VK64,  v64i1>;

  def : Pat<(insert_subvector (v16i1 immAllZerosV),
                              (v1i1 (scalar_to_vector GR8:$src)), (iPTR 0)),
            (KMOVWkr (AND32ri
                      (INSERT_SUBREG (i32 (IMPLICIT_DEF)), GR8:$src, sub_8bit),
                      (i32 1)))>;
}

// Mask unary operation
// - KNOT
multiclass avx512_mask_unop<bits<8> opc, string OpcodeStr,
                            RegisterClass KRC, SDPatternOperator OpNode,
                            X86FoldableSchedWrite sched, Predicate prd> {
  let Predicates = [prd] in
    def rr : I<opc, MRMSrcReg, (outs KRC:$dst), (ins KRC:$src),
               !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
               [(set KRC:$dst, (OpNode KRC:$src))]>,
               Sched<[sched]>;
}

multiclass avx512_mask_unop_all<bits<8> opc, string OpcodeStr,
                                SDPatternOperator OpNode,
                                X86FoldableSchedWrite sched> {
  defm B : avx512_mask_unop<opc, !strconcat(OpcodeStr, "b"), VK8, OpNode,
                            sched, HasDQI>, VEX, TB, PD;
  defm W : avx512_mask_unop<opc, !strconcat(OpcodeStr, "w"), VK16, OpNode,
                            sched, HasAVX512>, VEX, TB;
  defm D : avx512_mask_unop<opc, !strconcat(OpcodeStr, "d"), VK32, OpNode,
                            sched, HasBWI>, VEX, TB, PD, REX_W;
  defm Q : avx512_mask_unop<opc, !strconcat(OpcodeStr, "q"), VK64, OpNode,
                            sched, HasBWI>, VEX, TB, REX_W;
}

// TODO - do we need a X86SchedWriteWidths::KMASK type?
defm KNOT : avx512_mask_unop_all<0x44, "knot", vnot, SchedWriteVecLogic.XMM>;

// KNL does not support KMOVB, 8-bit mask is promoted to 16-bit
let Predicates = [HasAVX512, NoDQI] in
def : Pat<(vnot VK8:$src),
          (COPY_TO_REGCLASS (KNOTWrr (COPY_TO_REGCLASS VK8:$src, VK16)), VK8)>;

def : Pat<(vnot VK4:$src),
          (COPY_TO_REGCLASS (KNOTWrr (COPY_TO_REGCLASS VK4:$src, VK16)), VK4)>;
def : Pat<(vnot VK2:$src),
          (COPY_TO_REGCLASS (KNOTWrr (COPY_TO_REGCLASS VK2:$src, VK16)), VK2)>;
def : Pat<(vnot VK1:$src),
          (COPY_TO_REGCLASS (KNOTWrr (COPY_TO_REGCLASS VK1:$src, VK16)), VK2)>;

// Mask binary operation
// - KAND, KANDN, KOR, KXNOR, KXOR
multiclass avx512_mask_binop<bits<8> opc, string OpcodeStr,
                           RegisterClass KRC, SDPatternOperator OpNode,
                           X86FoldableSchedWrite sched, Predicate prd,
                           bit IsCommutable> {
  let Predicates = [prd], isCommutable = IsCommutable in
    def rr : I<opc, MRMSrcReg, (outs KRC:$dst), (ins KRC:$src1, KRC:$src2),
               !strconcat(OpcodeStr,
                          "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
               [(set KRC:$dst, (OpNode KRC:$src1, KRC:$src2))]>,
               Sched<[sched]>;
}

multiclass avx512_mask_binop_all<bits<8> opc, string OpcodeStr,
                                 SDPatternOperator OpNode,
                                 X86FoldableSchedWrite sched, bit IsCommutable,
                                 Predicate prdW = HasAVX512> {
  defm B : avx512_mask_binop<opc, !strconcat(OpcodeStr, "b"), VK8, OpNode,
                             sched, HasDQI, IsCommutable>, VEX, VVVV, VEX_L, TB, PD;
  defm W : avx512_mask_binop<opc, !strconcat(OpcodeStr, "w"), VK16, OpNode,
                             sched, prdW, IsCommutable>, VEX, VVVV, VEX_L, TB;
  defm D : avx512_mask_binop<opc, !strconcat(OpcodeStr, "d"), VK32, OpNode,
                             sched, HasBWI, IsCommutable>, VEX, VVVV, VEX_L, REX_W, TB, PD;
  defm Q : avx512_mask_binop<opc, !strconcat(OpcodeStr, "q"), VK64, OpNode,
                             sched, HasBWI, IsCommutable>, VEX, VVVV, VEX_L, REX_W, TB;
}

// TODO - do we need a X86SchedWriteWidths::KMASK type?
defm KAND  : avx512_mask_binop_all<0x41, "kand",  and,     SchedWriteVecLogic.XMM, 1>;
defm KOR   : avx512_mask_binop_all<0x45, "kor",   or,      SchedWriteVecLogic.XMM, 1>;
defm KXNOR : avx512_mask_binop_all<0x46, "kxnor", vxnor,   SchedWriteVecLogic.XMM, 1>;
defm KXOR  : avx512_mask_binop_all<0x47, "kxor",  xor,     SchedWriteVecLogic.XMM, 1>;
defm KANDN : avx512_mask_binop_all<0x42, "kandn", vandn,   SchedWriteVecLogic.XMM, 0>;
defm KADD  : avx512_mask_binop_all<0x4A, "kadd",  X86kadd, SchedWriteVecLogic.XMM, 1, HasDQI>;

multiclass avx512_binop_pat<SDPatternOperator VOpNode,
                            Instruction Inst> {
  // With AVX512F, 8-bit mask is promoted to 16-bit mask,
  // for the DQI set, this type is legal and KxxxB instruction is used
  let Predicates = [NoDQI] in
  def : Pat<(VOpNode VK8:$src1, VK8:$src2),
            (COPY_TO_REGCLASS
              (Inst (COPY_TO_REGCLASS VK8:$src1, VK16),
                    (COPY_TO_REGCLASS VK8:$src2, VK16)), VK8)>;

  // All types smaller than 8 bits require conversion anyway
  def : Pat<(VOpNode VK1:$src1, VK1:$src2),
        (COPY_TO_REGCLASS (Inst
                           (COPY_TO_REGCLASS VK1:$src1, VK16),
                           (COPY_TO_REGCLASS VK1:$src2, VK16)), VK1)>;
  def : Pat<(VOpNode VK2:$src1, VK2:$src2),
        (COPY_TO_REGCLASS (Inst
                           (COPY_TO_REGCLASS VK2:$src1, VK16),
                           (COPY_TO_REGCLASS VK2:$src2, VK16)), VK2)>;
  def : Pat<(VOpNode VK4:$src1, VK4:$src2),
        (COPY_TO_REGCLASS (Inst
                           (COPY_TO_REGCLASS VK4:$src1, VK16),
                           (COPY_TO_REGCLASS VK4:$src2, VK16)), VK4)>;
}

defm : avx512_binop_pat<and,   KANDWrr>;
defm : avx512_binop_pat<vandn, KANDNWrr>;
defm : avx512_binop_pat<or,    KORWrr>;
defm : avx512_binop_pat<vxnor, KXNORWrr>;
defm : avx512_binop_pat<xor,   KXORWrr>;

// Mask unpacking
multiclass avx512_mask_unpck<string Suffix, X86KVectorVTInfo Dst,
                             X86KVectorVTInfo Src, X86FoldableSchedWrite sched,
                             Predicate prd> {
  let Predicates = [prd] in {
    let hasSideEffects = 0 in
    def rr : I<0x4b, MRMSrcReg, (outs Dst.KRC:$dst),
               (ins Src.KRC:$src1, Src.KRC:$src2),
               "kunpck"#Suffix#"\t{$src2, $src1, $dst|$dst, $src1, $src2}", []>,
               VEX, VVVV, VEX_L, Sched<[sched]>;

    def : Pat<(Dst.KVT (concat_vectors Src.KRC:$src1, Src.KRC:$src2)),
              (!cast<Instruction>(NAME#rr) Src.KRC:$src2, Src.KRC:$src1)>;
  }
}

defm KUNPCKBW : avx512_mask_unpck<"bw", v16i1_info, v8i1_info,  WriteShuffle, HasAVX512>, TB, PD;
defm KUNPCKWD : avx512_mask_unpck<"wd", v32i1_info, v16i1_info, WriteShuffle, HasBWI>, TB;
defm KUNPCKDQ : avx512_mask_unpck<"dq", v64i1_info, v32i1_info, WriteShuffle, HasBWI>, TB, REX_W;

// Mask bit testing
multiclass avx512_mask_testop<bits<8> opc, string OpcodeStr, RegisterClass KRC,
                              SDNode OpNode, X86FoldableSchedWrite sched,
                              Predicate prd> {
  let Predicates = [prd], Defs = [EFLAGS] in
    def rr : I<opc, MRMSrcReg, (outs), (ins KRC:$src1, KRC:$src2),
               !strconcat(OpcodeStr, "\t{$src2, $src1|$src1, $src2}"),
               [(set EFLAGS, (OpNode KRC:$src1, KRC:$src2))]>,
               Sched<[sched]>;
}

multiclass avx512_mask_testop_w<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                X86FoldableSchedWrite sched,
                                Predicate prdW = HasAVX512> {
  defm B : avx512_mask_testop<opc, OpcodeStr#"b", VK8, OpNode, sched, HasDQI>,
                                                                VEX, TB, PD;
  defm W : avx512_mask_testop<opc, OpcodeStr#"w", VK16, OpNode, sched, prdW>,
                                                                VEX, TB;
  defm Q : avx512_mask_testop<opc, OpcodeStr#"q", VK64, OpNode, sched, HasBWI>,
                                                                VEX, TB, REX_W;
  defm D : avx512_mask_testop<opc, OpcodeStr#"d", VK32, OpNode, sched, HasBWI>,
                                                                VEX, TB, PD, REX_W;
}

// TODO - do we need a X86SchedWriteWidths::KMASK type?
defm KORTEST : avx512_mask_testop_w<0x98, "kortest", X86kortest, SchedWriteVecLogic.XMM>;
defm KTEST   : avx512_mask_testop_w<0x99, "ktest", X86ktest, SchedWriteVecLogic.XMM, HasDQI>;

// Mask shift
multiclass avx512_mask_shiftop<bits<8> opc, string OpcodeStr, RegisterClass KRC,
                               SDNode OpNode, X86FoldableSchedWrite sched> {
  let Predicates = [HasAVX512] in
    def ri : Ii8<opc, MRMSrcReg, (outs KRC:$dst), (ins KRC:$src, u8imm:$imm),
                 !strconcat(OpcodeStr,
                            "\t{$imm, $src, $dst|$dst, $src, $imm}"),
                            [(set KRC:$dst, (OpNode KRC:$src, (i8 timm:$imm)))]>,
                 Sched<[sched]>;
}

multiclass avx512_mask_shiftop_w<bits<8> opc1, bits<8> opc2, string OpcodeStr,
                                 SDNode OpNode, X86FoldableSchedWrite sched> {
  defm W : avx512_mask_shiftop<opc1, !strconcat(OpcodeStr, "w"), VK16, OpNode,
                               sched>, VEX, TA, PD, REX_W;
  let Predicates = [HasDQI] in
  defm B : avx512_mask_shiftop<opc1, !strconcat(OpcodeStr, "b"), VK8, OpNode,
                               sched>, VEX, TA, PD;
  let Predicates = [HasBWI] in {
  defm Q : avx512_mask_shiftop<opc2, !strconcat(OpcodeStr, "q"), VK64, OpNode,
                               sched>, VEX, TA, PD, REX_W;
  defm D : avx512_mask_shiftop<opc2, !strconcat(OpcodeStr, "d"), VK32, OpNode,
                               sched>, VEX, TA, PD;
  }
}

defm KSHIFTL : avx512_mask_shiftop_w<0x32, 0x33, "kshiftl", X86kshiftl, WriteShuffle>;
defm KSHIFTR : avx512_mask_shiftop_w<0x30, 0x31, "kshiftr", X86kshiftr, WriteShuffle>;

// Patterns for comparing 128/256-bit integer vectors using 512-bit instruction.
multiclass axv512_icmp_packed_cc_no_vlx_lowering<PatFrag Frag, PatFrag Frag_su,
                                                 string InstStr,
                                                 X86VectorVTInfo Narrow,
                                                 X86VectorVTInfo Wide> {
def : Pat<(Narrow.KVT (Frag:$cc (Narrow.VT Narrow.RC:$src1),
                                (Narrow.VT Narrow.RC:$src2), cond)),
          (COPY_TO_REGCLASS
           (!cast<Instruction>(InstStr#"Zrri")
            (Wide.VT (INSERT_SUBREG (IMPLICIT_DEF), Narrow.RC:$src1, Narrow.SubRegIdx)),
            (Wide.VT (INSERT_SUBREG (IMPLICIT_DEF), Narrow.RC:$src2, Narrow.SubRegIdx)),
            (X86pcmpm_imm $cc)), Narrow.KRC)>;

def : Pat<(Narrow.KVT (and Narrow.KRC:$mask,
                           (Narrow.KVT (Frag_su:$cc (Narrow.VT Narrow.RC:$src1),
                                                    (Narrow.VT Narrow.RC:$src2),
                                                    cond)))),
          (COPY_TO_REGCLASS (!cast<Instruction>(InstStr#"Zrrik")
           (COPY_TO_REGCLASS Narrow.KRC:$mask, Wide.KRC),
           (Wide.VT (INSERT_SUBREG (IMPLICIT_DEF), Narrow.RC:$src1, Narrow.SubRegIdx)),
           (Wide.VT (INSERT_SUBREG (IMPLICIT_DEF), Narrow.RC:$src2, Narrow.SubRegIdx)),
           (X86pcmpm_imm $cc)), Narrow.KRC)>;
}

multiclass axv512_icmp_packed_cc_rmb_no_vlx_lowering<PatFrag Frag, PatFrag Frag_su,
                                                     string InstStr,
                                                     X86VectorVTInfo Narrow,
                                                     X86VectorVTInfo Wide> {
// Broadcast load.
def : Pat<(Narrow.KVT (Frag:$cc (Narrow.VT Narrow.RC:$src1),
                                (Narrow.BroadcastLdFrag addr:$src2), cond)),
          (COPY_TO_REGCLASS
           (!cast<Instruction>(InstStr#"Zrmib")
            (Wide.VT (INSERT_SUBREG (IMPLICIT_DEF), Narrow.RC:$src1, Narrow.SubRegIdx)),
            addr:$src2, (X86pcmpm_imm $cc)), Narrow.KRC)>;

def : Pat<(Narrow.KVT (and Narrow.KRC:$mask,
                           (Narrow.KVT
                            (Frag_su:$cc (Narrow.VT Narrow.RC:$src1),
                                         (Narrow.BroadcastLdFrag addr:$src2),
                                         cond)))),
          (COPY_TO_REGCLASS (!cast<Instruction>(InstStr#"Zrmibk")
           (COPY_TO_REGCLASS Narrow.KRC:$mask, Wide.KRC),
           (Wide.VT (INSERT_SUBREG (IMPLICIT_DEF), Narrow.RC:$src1, Narrow.SubRegIdx)),
           addr:$src2, (X86pcmpm_imm $cc)), Narrow.KRC)>;

// Commuted with broadcast load.
def : Pat<(Narrow.KVT (Frag:$cc (Narrow.BroadcastLdFrag addr:$src2),
                                (Narrow.VT Narrow.RC:$src1),
                                cond)),
          (COPY_TO_REGCLASS
           (!cast<Instruction>(InstStr#"Zrmib")
            (Wide.VT (INSERT_SUBREG (IMPLICIT_DEF), Narrow.RC:$src1, Narrow.SubRegIdx)),
            addr:$src2, (X86pcmpm_imm_commute $cc)), Narrow.KRC)>;

def : Pat<(Narrow.KVT (and Narrow.KRC:$mask,
                           (Narrow.KVT
                            (Frag_su:$cc (Narrow.BroadcastLdFrag addr:$src2),
                                         (Narrow.VT Narrow.RC:$src1),
                                         cond)))),
          (COPY_TO_REGCLASS (!cast<Instruction>(InstStr#"Zrmibk")
           (COPY_TO_REGCLASS Narrow.KRC:$mask, Wide.KRC),
           (Wide.VT (INSERT_SUBREG (IMPLICIT_DEF), Narrow.RC:$src1, Narrow.SubRegIdx)),
           addr:$src2, (X86pcmpm_imm_commute $cc)), Narrow.KRC)>;
}

// Same as above, but for fp types which don't use PatFrags.
multiclass axv512_cmp_packed_cc_no_vlx_lowering<string InstStr,
                                                X86VectorVTInfo Narrow,
                                                X86VectorVTInfo Wide> {
def : Pat<(Narrow.KVT (X86cmpm (Narrow.VT Narrow.RC:$src1),
                               (Narrow.VT Narrow.RC:$src2), timm:$cc)),
          (COPY_TO_REGCLASS
           (!cast<Instruction>(InstStr#"Zrri")
            (Wide.VT (INSERT_SUBREG (IMPLICIT_DEF), Narrow.RC:$src1, Narrow.SubRegIdx)),
            (Wide.VT (INSERT_SUBREG (IMPLICIT_DEF), Narrow.RC:$src2, Narrow.SubRegIdx)),
            timm:$cc), Narrow.KRC)>;

def : Pat<(Narrow.KVT (and Narrow.KRC:$mask,
                           (X86cmpm_su (Narrow.VT Narrow.RC:$src1),
                                       (Narrow.VT Narrow.RC:$src2), timm:$cc))),
          (COPY_TO_REGCLASS (!cast<Instruction>(InstStr#"Zrrik")
           (COPY_TO_REGCLASS Narrow.KRC:$mask, Wide.KRC),
           (Wide.VT (INSERT_SUBREG (IMPLICIT_DEF), Narrow.RC:$src1, Narrow.SubRegIdx)),
           (Wide.VT (INSERT_SUBREG (IMPLICIT_DEF), Narrow.RC:$src2, Narrow.SubRegIdx)),
           timm:$cc), Narrow.KRC)>;

// Broadcast load.
def : Pat<(Narrow.KVT (X86cmpm (Narrow.VT Narrow.RC:$src1),
                               (Narrow.VT (Narrow.BroadcastLdFrag addr:$src2)), timm:$cc)),
          (COPY_TO_REGCLASS
           (!cast<Instruction>(InstStr#"Zrmbi")
            (Wide.VT (INSERT_SUBREG (IMPLICIT_DEF), Narrow.RC:$src1, Narrow.SubRegIdx)),
            addr:$src2, timm:$cc), Narrow.KRC)>;

def : Pat<(Narrow.KVT (and Narrow.KRC:$mask,
                           (X86cmpm_su (Narrow.VT Narrow.RC:$src1),
                                       (Narrow.VT (Narrow.BroadcastLdFrag addr:$src2)), timm:$cc))),
          (COPY_TO_REGCLASS (!cast<Instruction>(InstStr#"Zrmbik")
           (COPY_TO_REGCLASS Narrow.KRC:$mask, Wide.KRC),
           (Wide.VT (INSERT_SUBREG (IMPLICIT_DEF), Narrow.RC:$src1, Narrow.SubRegIdx)),
           addr:$src2, timm:$cc), Narrow.KRC)>;

// Commuted with broadcast load.
def : Pat<(Narrow.KVT (X86cmpm (Narrow.VT (Narrow.BroadcastLdFrag addr:$src2)),
                               (Narrow.VT Narrow.RC:$src1), timm:$cc)),
          (COPY_TO_REGCLASS
           (!cast<Instruction>(InstStr#"Zrmbi")
            (Wide.VT (INSERT_SUBREG (IMPLICIT_DEF), Narrow.RC:$src1, Narrow.SubRegIdx)),
            addr:$src2, (X86cmpm_imm_commute timm:$cc)), Narrow.KRC)>;

def : Pat<(Narrow.KVT (and Narrow.KRC:$mask,
                           (X86cmpm_su (Narrow.VT (Narrow.BroadcastLdFrag addr:$src2)),
                                       (Narrow.VT Narrow.RC:$src1), timm:$cc))),
          (COPY_TO_REGCLASS (!cast<Instruction>(InstStr#"Zrmbik")
           (COPY_TO_REGCLASS Narrow.KRC:$mask, Wide.KRC),
           (Wide.VT (INSERT_SUBREG (IMPLICIT_DEF), Narrow.RC:$src1, Narrow.SubRegIdx)),
           addr:$src2, (X86cmpm_imm_commute timm:$cc)), Narrow.KRC)>;
}

let Predicates = [HasAVX512, NoVLX, HasEVEX512] in {
  defm : axv512_icmp_packed_cc_no_vlx_lowering<X86pcmpm, X86pcmpm_su, "VPCMPD", v8i32x_info, v16i32_info>;
  defm : axv512_icmp_packed_cc_no_vlx_lowering<X86pcmpum, X86pcmpum_su, "VPCMPUD", v8i32x_info, v16i32_info>;

  defm : axv512_icmp_packed_cc_no_vlx_lowering<X86pcmpm, X86pcmpm_su, "VPCMPD", v4i32x_info, v16i32_info>;
  defm : axv512_icmp_packed_cc_no_vlx_lowering<X86pcmpum, X86pcmpum_su, "VPCMPUD", v4i32x_info, v16i32_info>;

  defm : axv512_icmp_packed_cc_no_vlx_lowering<X86pcmpm, X86pcmpm_su, "VPCMPQ", v4i64x_info, v8i64_info>;
  defm : axv512_icmp_packed_cc_no_vlx_lowering<X86pcmpum, X86pcmpum_su, "VPCMPUQ", v4i64x_info, v8i64_info>;

  defm : axv512_icmp_packed_cc_no_vlx_lowering<X86pcmpm, X86pcmpm_su, "VPCMPQ", v2i64x_info, v8i64_info>;
  defm : axv512_icmp_packed_cc_no_vlx_lowering<X86pcmpum, X86pcmpum_su, "VPCMPUQ", v2i64x_info, v8i64_info>;

  defm : axv512_icmp_packed_cc_rmb_no_vlx_lowering<X86pcmpm, X86pcmpm_su, "VPCMPD", v8i32x_info, v16i32_info>;
  defm : axv512_icmp_packed_cc_rmb_no_vlx_lowering<X86pcmpum, X86pcmpum_su, "VPCMPUD", v8i32x_info, v16i32_info>;

  defm : axv512_icmp_packed_cc_rmb_no_vlx_lowering<X86pcmpm, X86pcmpm_su, "VPCMPD", v4i32x_info, v16i32_info>;
  defm : axv512_icmp_packed_cc_rmb_no_vlx_lowering<X86pcmpum, X86pcmpum_su, "VPCMPUD", v4i32x_info, v16i32_info>;

  defm : axv512_icmp_packed_cc_rmb_no_vlx_lowering<X86pcmpm, X86pcmpm_su, "VPCMPQ", v4i64x_info, v8i64_info>;
  defm : axv512_icmp_packed_cc_rmb_no_vlx_lowering<X86pcmpum, X86pcmpum_su, "VPCMPUQ", v4i64x_info, v8i64_info>;

  defm : axv512_icmp_packed_cc_rmb_no_vlx_lowering<X86pcmpm, X86pcmpm_su, "VPCMPQ", v2i64x_info, v8i64_info>;
  defm : axv512_icmp_packed_cc_rmb_no_vlx_lowering<X86pcmpum, X86pcmpum_su, "VPCMPUQ", v2i64x_info, v8i64_info>;

  defm : axv512_cmp_packed_cc_no_vlx_lowering<"VCMPPS", v8f32x_info, v16f32_info>;
  defm : axv512_cmp_packed_cc_no_vlx_lowering<"VCMPPS", v4f32x_info, v16f32_info>;
  defm : axv512_cmp_packed_cc_no_vlx_lowering<"VCMPPD", v4f64x_info, v8f64_info>;
  defm : axv512_cmp_packed_cc_no_vlx_lowering<"VCMPPD", v2f64x_info, v8f64_info>;
}

let Predicates = [HasBWI, NoVLX, HasEVEX512] in {
  defm : axv512_icmp_packed_cc_no_vlx_lowering<X86pcmpm, X86pcmpm_su, "VPCMPB", v32i8x_info, v64i8_info>;
  defm : axv512_icmp_packed_cc_no_vlx_lowering<X86pcmpum, X86pcmpum_su, "VPCMPUB", v32i8x_info, v64i8_info>;

  defm : axv512_icmp_packed_cc_no_vlx_lowering<X86pcmpm, X86pcmpm_su, "VPCMPB", v16i8x_info, v64i8_info>;
  defm : axv512_icmp_packed_cc_no_vlx_lowering<X86pcmpum, X86pcmpum_su, "VPCMPUB", v16i8x_info, v64i8_info>;

  defm : axv512_icmp_packed_cc_no_vlx_lowering<X86pcmpm, X86pcmpm_su, "VPCMPW", v16i16x_info, v32i16_info>;
  defm : axv512_icmp_packed_cc_no_vlx_lowering<X86pcmpum, X86pcmpum_su, "VPCMPUW", v16i16x_info, v32i16_info>;

  defm : axv512_icmp_packed_cc_no_vlx_lowering<X86pcmpm, X86pcmpm_su, "VPCMPW", v8i16x_info, v32i16_info>;
  defm : axv512_icmp_packed_cc_no_vlx_lowering<X86pcmpum, X86pcmpum_su, "VPCMPUW", v8i16x_info, v32i16_info>;
}

// Mask setting all 0s or 1s
multiclass avx512_mask_setop<RegisterClass KRC, ValueType VT, SDPatternOperator Val> {
  let Predicates = [HasAVX512] in
    let isReMaterializable = 1, isAsCheapAsAMove = 1, isPseudo = 1,
        SchedRW = [WriteZero] in
      def NAME# : I<0, Pseudo, (outs KRC:$dst), (ins), "",
                     [(set KRC:$dst, (VT Val))]>;
}

multiclass avx512_mask_setop_w<SDPatternOperator Val> {
  defm W : avx512_mask_setop<VK16, v16i1, Val>;
  defm D : avx512_mask_setop<VK32,  v32i1, Val>;
  defm Q : avx512_mask_setop<VK64, v64i1, Val>;
}

defm KSET0 : avx512_mask_setop_w<immAllZerosV>;
defm KSET1 : avx512_mask_setop_w<immAllOnesV>;

// With AVX-512 only, 8-bit mask is promoted to 16-bit mask.
let Predicates = [HasAVX512] in {
  def : Pat<(v8i1 immAllZerosV), (COPY_TO_REGCLASS (KSET0W), VK8)>;
  def : Pat<(v4i1 immAllZerosV), (COPY_TO_REGCLASS (KSET0W), VK4)>;
  def : Pat<(v2i1 immAllZerosV), (COPY_TO_REGCLASS (KSET0W), VK2)>;
  def : Pat<(v1i1 immAllZerosV), (COPY_TO_REGCLASS (KSET0W), VK1)>;
  def : Pat<(v8i1 immAllOnesV),  (COPY_TO_REGCLASS (KSET1W), VK8)>;
  def : Pat<(v4i1 immAllOnesV),  (COPY_TO_REGCLASS (KSET1W), VK4)>;
  def : Pat<(v2i1 immAllOnesV),  (COPY_TO_REGCLASS (KSET1W), VK2)>;
  def : Pat<(v1i1 immAllOnesV),  (COPY_TO_REGCLASS (KSET1W), VK1)>;
}

// Patterns for kmask insert_subvector/extract_subvector to/from index=0
multiclass operation_subvector_mask_lowering<RegisterClass subRC, ValueType subVT,
                                             RegisterClass RC, ValueType VT> {
  def : Pat<(subVT (extract_subvector (VT RC:$src), (iPTR 0))),
            (subVT (COPY_TO_REGCLASS RC:$src, subRC))>;

  def : Pat<(VT (insert_subvector undef, subRC:$src, (iPTR 0))),
            (VT (COPY_TO_REGCLASS subRC:$src, RC))>;
}
defm : operation_subvector_mask_lowering<VK1,  v1i1,  VK2,  v2i1>;
defm : operation_subvector_mask_lowering<VK1,  v1i1,  VK4,  v4i1>;
defm : operation_subvector_mask_lowering<VK1,  v1i1,  VK8,  v8i1>;
defm : operation_subvector_mask_lowering<VK1,  v1i1,  VK16, v16i1>;
defm : operation_subvector_mask_lowering<VK1,  v1i1,  VK32, v32i1>;
defm : operation_subvector_mask_lowering<VK1,  v1i1,  VK64, v64i1>;

defm : operation_subvector_mask_lowering<VK2,  v2i1,  VK4,  v4i1>;
defm : operation_subvector_mask_lowering<VK2,  v2i1,  VK8,  v8i1>;
defm : operation_subvector_mask_lowering<VK2,  v2i1,  VK16, v16i1>;
defm : operation_subvector_mask_lowering<VK2,  v2i1,  VK32, v32i1>;
defm : operation_subvector_mask_lowering<VK2,  v2i1,  VK64, v64i1>;

defm : operation_subvector_mask_lowering<VK4,  v4i1,  VK8,  v8i1>;
defm : operation_subvector_mask_lowering<VK4,  v4i1,  VK16, v16i1>;
defm : operation_subvector_mask_lowering<VK4,  v4i1,  VK32, v32i1>;
defm : operation_subvector_mask_lowering<VK4,  v4i1,  VK64, v64i1>;

defm : operation_subvector_mask_lowering<VK8,  v8i1,  VK16, v16i1>;
defm : operation_subvector_mask_lowering<VK8,  v8i1,  VK32, v32i1>;
defm : operation_subvector_mask_lowering<VK8,  v8i1,  VK64, v64i1>;

defm : operation_subvector_mask_lowering<VK16, v16i1, VK32, v32i1>;
defm : operation_subvector_mask_lowering<VK16, v16i1, VK64, v64i1>;

defm : operation_subvector_mask_lowering<VK32, v32i1, VK64, v64i1>;

//===----------------------------------------------------------------------===//
// AVX-512 - Aligned and unaligned load and store
//

multiclass avx512_load<bits<8> opc, string OpcodeStr, string Name,
                       X86VectorVTInfo _, PatFrag ld_frag, PatFrag mload,
                       X86SchedWriteMoveLS Sched, bit NoRMPattern = 0,
                       SDPatternOperator SelectOprr = vselect> {
  let hasSideEffects = 0 in {
  let isMoveReg = 1 in
  def rr : AVX512PI<opc, MRMSrcReg, (outs _.RC:$dst), (ins _.RC:$src),
                    !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"), [],
                    _.ExeDomain>, EVEX, Sched<[Sched.RR]>;
  def rrkz : AVX512PI<opc, MRMSrcReg, (outs _.RC:$dst),
                      (ins _.KRCWM:$mask,  _.RC:$src),
                      !strconcat(OpcodeStr, "\t{$src, ${dst} {${mask}} {z}|",
                       "${dst} {${mask}} {z}, $src}"),
                       [(set _.RC:$dst, (_.VT (SelectOprr _.KRCWM:$mask,
                                           (_.VT _.RC:$src),
                                           _.ImmAllZerosV)))], _.ExeDomain>,
                       EVEX, EVEX_KZ, Sched<[Sched.RR]>;

  let mayLoad = 1, canFoldAsLoad = 1, isReMaterializable = 1 in
  def rm : AVX512PI<opc, MRMSrcMem, (outs _.RC:$dst), (ins _.MemOp:$src),
                    !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                    !if(NoRMPattern, [],
                        [(set _.RC:$dst,
                          (_.VT (ld_frag addr:$src)))]),
                    _.ExeDomain>, EVEX, Sched<[Sched.RM]>;

  let Constraints = "$src0 = $dst", isConvertibleToThreeAddress = 1 in {
    def rrk : AVX512PI<opc, MRMSrcReg, (outs _.RC:$dst),
                      (ins _.RC:$src0, _.KRCWM:$mask, _.RC:$src1),
                      !strconcat(OpcodeStr, "\t{$src1, ${dst} {${mask}}|",
                      "${dst} {${mask}}, $src1}"),
                      [(set _.RC:$dst, (_.VT (SelectOprr _.KRCWM:$mask,
                                          (_.VT _.RC:$src1),
                                          (_.VT _.RC:$src0))))], _.ExeDomain>,
                       EVEX, EVEX_K, Sched<[Sched.RR]>;
    def rmk : AVX512PI<opc, MRMSrcMem, (outs _.RC:$dst),
                     (ins _.RC:$src0, _.KRCWM:$mask, _.MemOp:$src1),
                     !strconcat(OpcodeStr, "\t{$src1, ${dst} {${mask}}|",
                      "${dst} {${mask}}, $src1}"),
                     [(set _.RC:$dst, (_.VT
                         (vselect_mask _.KRCWM:$mask,
                          (_.VT (ld_frag addr:$src1)),
                           (_.VT _.RC:$src0))))], _.ExeDomain>,
                     EVEX, EVEX_K, Sched<[Sched.RM]>;
  }
  def rmkz : AVX512PI<opc, MRMSrcMem, (outs _.RC:$dst),
                  (ins _.KRCWM:$mask, _.MemOp:$src),
                  OpcodeStr #"\t{$src, ${dst} {${mask}} {z}|"#
                                "${dst} {${mask}} {z}, $src}",
                  [(set _.RC:$dst, (_.VT (vselect_mask _.KRCWM:$mask,
                    (_.VT (ld_frag addr:$src)), _.ImmAllZerosV)))],
                  _.ExeDomain>, EVEX, EVEX_KZ, Sched<[Sched.RM]>;
  }
  def : Pat<(_.VT (mload addr:$ptr, _.KRCWM:$mask, undef)),
            (!cast<Instruction>(Name#_.ZSuffix#rmkz) _.KRCWM:$mask, addr:$ptr)>;

  def : Pat<(_.VT (mload addr:$ptr, _.KRCWM:$mask, _.ImmAllZerosV)),
            (!cast<Instruction>(Name#_.ZSuffix#rmkz) _.KRCWM:$mask, addr:$ptr)>;

  def : Pat<(_.VT (mload addr:$ptr, _.KRCWM:$mask, (_.VT _.RC:$src0))),
            (!cast<Instruction>(Name#_.ZSuffix#rmk) _.RC:$src0,
             _.KRCWM:$mask, addr:$ptr)>;
}

multiclass avx512_alignedload_vl<bits<8> opc, string OpcodeStr,
                                 AVX512VLVectorVTInfo _, Predicate prd,
                                 X86SchedWriteMoveLSWidths Sched,
                                 bit NoRMPattern = 0> {
  let Predicates = [prd] in
  defm Z : avx512_load<opc, OpcodeStr, NAME, _.info512,
                       _.info512.AlignedLdFrag, masked_load_aligned,
                       Sched.ZMM, NoRMPattern>, EVEX_V512;

  let Predicates = [prd, HasVLX] in {
  defm Z256 : avx512_load<opc, OpcodeStr, NAME, _.info256,
                          _.info256.AlignedLdFrag, masked_load_aligned,
                          Sched.YMM, NoRMPattern>, EVEX_V256;
  defm Z128 : avx512_load<opc, OpcodeStr, NAME, _.info128,
                          _.info128.AlignedLdFrag, masked_load_aligned,
                          Sched.XMM, NoRMPattern>, EVEX_V128;
  }
}

multiclass avx512_load_vl<bits<8> opc, string OpcodeStr,
                          AVX512VLVectorVTInfo _, Predicate prd,
                          X86SchedWriteMoveLSWidths Sched,
                          bit NoRMPattern = 0,
                          SDPatternOperator SelectOprr = vselect> {
  let Predicates = [prd] in
  defm Z : avx512_load<opc, OpcodeStr, NAME, _.info512, _.info512.LdFrag,
                       masked_load, Sched.ZMM, NoRMPattern, SelectOprr>, EVEX_V512;

  let Predicates = [prd, HasVLX] in {
  defm Z256 : avx512_load<opc, OpcodeStr, NAME, _.info256, _.info256.LdFrag,
                         masked_load, Sched.YMM, NoRMPattern, SelectOprr>, EVEX_V256;
  defm Z128 : avx512_load<opc, OpcodeStr, NAME, _.info128, _.info128.LdFrag,
                         masked_load, Sched.XMM, NoRMPattern, SelectOprr>, EVEX_V128;
  }
}

multiclass avx512_store<bits<8> opc, string OpcodeStr, string BaseName,
                        X86VectorVTInfo _, PatFrag st_frag, PatFrag mstore,
                        X86SchedWriteMoveLS Sched, bit NoMRPattern = 0> {
  let hasSideEffects = 0, isCodeGenOnly = 1, ForceDisassemble = 1 in {
  let isMoveReg = 1 in
  def rr_REV  : AVX512PI<opc, MRMDestReg, (outs _.RC:$dst), (ins _.RC:$src),
                         OpcodeStr # "\t{$src, $dst|$dst, $src}",
                         [], _.ExeDomain>, EVEX,
                         Sched<[Sched.RR]>;
  def rrk_REV : AVX512PI<opc, MRMDestReg, (outs  _.RC:$dst),
                         (ins _.KRCWM:$mask, _.RC:$src),
                         OpcodeStr # "\t{$src, ${dst} {${mask}}|"#
                         "${dst} {${mask}}, $src}",
                         [], _.ExeDomain>,  EVEX, EVEX_K,
                         Sched<[Sched.RR]>;
  def rrkz_REV : AVX512PI<opc, MRMDestReg, (outs  _.RC:$dst),
                          (ins _.KRCWM:$mask, _.RC:$src),
                          OpcodeStr # "\t{$src, ${dst} {${mask}} {z}|" #
                          "${dst} {${mask}} {z}, $src}",
                          [], _.ExeDomain>, EVEX, EVEX_KZ,
                          Sched<[Sched.RR]>;
  }

  let hasSideEffects = 0, mayStore = 1 in
  def mr : AVX512PI<opc, MRMDestMem, (outs), (ins _.MemOp:$dst, _.RC:$src),
                    !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                    !if(NoMRPattern, [],
                        [(st_frag (_.VT _.RC:$src), addr:$dst)]),
                    _.ExeDomain>, EVEX, Sched<[Sched.MR]>;
  def mrk : AVX512PI<opc, MRMDestMem, (outs),
                     (ins _.MemOp:$dst, _.KRCWM:$mask, _.RC:$src),
              OpcodeStr # "\t{$src, ${dst} {${mask}}|${dst} {${mask}}, $src}",
               [], _.ExeDomain>, EVEX, EVEX_K, Sched<[Sched.MR]>;

  def: Pat<(mstore (_.VT _.RC:$src), addr:$ptr, _.KRCWM:$mask),
           (!cast<Instruction>(BaseName#_.ZSuffix#mrk) addr:$ptr,
                                                        _.KRCWM:$mask, _.RC:$src)>;

  def : InstAlias<OpcodeStr#".s\t{$src, $dst|$dst, $src}",
                  (!cast<Instruction>(BaseName#_.ZSuffix#"rr_REV")
                   _.RC:$dst, _.RC:$src), 0>;
  def : InstAlias<OpcodeStr#".s\t{$src, ${dst} {${mask}}|${dst} {${mask}}, $src}",
                  (!cast<Instruction>(BaseName#_.ZSuffix#"rrk_REV")
                   _.RC:$dst, _.KRCWM:$mask, _.RC:$src), 0>;
  def : InstAlias<OpcodeStr#".s\t{$src, ${dst} {${mask}} {z}|${dst} {${mask}} {z}, $src}",
                  (!cast<Instruction>(BaseName#_.ZSuffix#"rrkz_REV")
                   _.RC:$dst, _.KRCWM:$mask, _.RC:$src), 0>;
}

multiclass avx512_store_vl< bits<8> opc, string OpcodeStr,
                            AVX512VLVectorVTInfo _, Predicate prd,
                            X86SchedWriteMoveLSWidths Sched,
                            bit NoMRPattern = 0> {
  let Predicates = [prd] in
  defm Z : avx512_store<opc, OpcodeStr, NAME, _.info512, store,
                        masked_store, Sched.ZMM, NoMRPattern>, EVEX_V512;
  let Predicates = [prd, HasVLX] in {
    defm Z256 : avx512_store<opc, OpcodeStr, NAME, _.info256, store,
                             masked_store, Sched.YMM, NoMRPattern>, EVEX_V256;
    defm Z128 : avx512_store<opc, OpcodeStr, NAME, _.info128, store,
                             masked_store, Sched.XMM, NoMRPattern>, EVEX_V128;
  }
}

multiclass avx512_alignedstore_vl<bits<8> opc, string OpcodeStr,
                                  AVX512VLVectorVTInfo _, Predicate prd,
                                  X86SchedWriteMoveLSWidths Sched,
                                  bit NoMRPattern = 0> {
  let Predicates = [prd] in
  defm Z : avx512_store<opc, OpcodeStr, NAME, _.info512, alignedstore,
                        masked_store_aligned, Sched.ZMM, NoMRPattern>, EVEX_V512;

  let Predicates = [prd, HasVLX] in {
    defm Z256 : avx512_store<opc, OpcodeStr, NAME, _.info256, alignedstore,
                             masked_store_aligned, Sched.YMM, NoMRPattern>, EVEX_V256;
    defm Z128 : avx512_store<opc, OpcodeStr, NAME, _.info128, alignedstore,
                             masked_store_aligned, Sched.XMM, NoMRPattern>, EVEX_V128;
  }
}

defm VMOVAPS : avx512_alignedload_vl<0x28, "vmovaps", avx512vl_f32_info,
                                     HasAVX512, SchedWriteFMoveLS>,
               avx512_alignedstore_vl<0x29, "vmovaps", avx512vl_f32_info,
                                      HasAVX512, SchedWriteFMoveLS>,
               TB, EVEX_CD8<32, CD8VF>;

defm VMOVAPD : avx512_alignedload_vl<0x28, "vmovapd", avx512vl_f64_info,
                                     HasAVX512, SchedWriteFMoveLS>,
               avx512_alignedstore_vl<0x29, "vmovapd", avx512vl_f64_info,
                                      HasAVX512, SchedWriteFMoveLS>,
               TB, PD, REX_W, EVEX_CD8<64, CD8VF>;

defm VMOVUPS : avx512_load_vl<0x10, "vmovups", avx512vl_f32_info, HasAVX512,
                              SchedWriteFMoveLS, 0, null_frag>,
               avx512_store_vl<0x11, "vmovups", avx512vl_f32_info, HasAVX512,
                               SchedWriteFMoveLS>,
                               TB, EVEX_CD8<32, CD8VF>;

defm VMOVUPD : avx512_load_vl<0x10, "vmovupd", avx512vl_f64_info, HasAVX512,
                              SchedWriteFMoveLS, 0, null_frag>,
               avx512_store_vl<0x11, "vmovupd", avx512vl_f64_info, HasAVX512,
                               SchedWriteFMoveLS>,
               TB, PD, REX_W, EVEX_CD8<64, CD8VF>;

defm VMOVDQA32 : avx512_alignedload_vl<0x6F, "vmovdqa32", avx512vl_i32_info,
                                       HasAVX512, SchedWriteVecMoveLS, 1>,
                 avx512_alignedstore_vl<0x7F, "vmovdqa32", avx512vl_i32_info,
                                        HasAVX512, SchedWriteVecMoveLS, 1>,
                 TB, PD, EVEX_CD8<32, CD8VF>;

defm VMOVDQA64 : avx512_alignedload_vl<0x6F, "vmovdqa64", avx512vl_i64_info,
                                       HasAVX512, SchedWriteVecMoveLS>,
                 avx512_alignedstore_vl<0x7F, "vmovdqa64", avx512vl_i64_info,
                                        HasAVX512, SchedWriteVecMoveLS>,
                 TB, PD, REX_W, EVEX_CD8<64, CD8VF>;

defm VMOVDQU8 : avx512_load_vl<0x6F, "vmovdqu8", avx512vl_i8_info, HasBWI,
                               SchedWriteVecMoveLS, 1>,
                avx512_store_vl<0x7F, "vmovdqu8", avx512vl_i8_info, HasBWI,
                                SchedWriteVecMoveLS, 1>,
                TB, XD, EVEX_CD8<8, CD8VF>;

defm VMOVDQU16 : avx512_load_vl<0x6F, "vmovdqu16", avx512vl_i16_info, HasBWI,
                                SchedWriteVecMoveLS, 1>,
                 avx512_store_vl<0x7F, "vmovdqu16", avx512vl_i16_info, HasBWI,
                                 SchedWriteVecMoveLS, 1>,
                 TB, XD, REX_W, EVEX_CD8<16, CD8VF>;

defm VMOVDQU32 : avx512_load_vl<0x6F, "vmovdqu32", avx512vl_i32_info, HasAVX512,
                                SchedWriteVecMoveLS, 1, null_frag>,
                 avx512_store_vl<0x7F, "vmovdqu32", avx512vl_i32_info, HasAVX512,
                                 SchedWriteVecMoveLS, 1>,
                 TB, XS, EVEX_CD8<32, CD8VF>;

defm VMOVDQU64 : avx512_load_vl<0x6F, "vmovdqu64", avx512vl_i64_info, HasAVX512,
                                SchedWriteVecMoveLS, 0, null_frag>,
                 avx512_store_vl<0x7F, "vmovdqu64", avx512vl_i64_info, HasAVX512,
                                 SchedWriteVecMoveLS>,
                 TB, XS, REX_W, EVEX_CD8<64, CD8VF>;

// Special instructions to help with spilling when we don't have VLX. We need
// to load or store from a ZMM register instead. These are converted in
// expandPostRAPseudos.
let isReMaterializable = 1, canFoldAsLoad = 1,
    isPseudo = 1, mayLoad = 1, hasSideEffects = 0 in {
def VMOVAPSZ128rm_NOVLX : I<0, Pseudo, (outs VR128X:$dst), (ins f128mem:$src),
                            "", []>, Sched<[WriteFLoadX]>;
def VMOVAPSZ256rm_NOVLX : I<0, Pseudo, (outs VR256X:$dst), (ins f256mem:$src),
                            "", []>, Sched<[WriteFLoadY]>;
def VMOVUPSZ128rm_NOVLX : I<0, Pseudo, (outs VR128X:$dst), (ins f128mem:$src),
                            "", []>, Sched<[WriteFLoadX]>;
def VMOVUPSZ256rm_NOVLX : I<0, Pseudo, (outs VR256X:$dst), (ins f256mem:$src),
                            "", []>, Sched<[WriteFLoadY]>;
}

let isPseudo = 1, mayStore = 1, hasSideEffects = 0 in {
def VMOVAPSZ128mr_NOVLX : I<0, Pseudo, (outs), (ins f128mem:$dst, VR128X:$src),
                            "", []>, Sched<[WriteFStoreX]>;
def VMOVAPSZ256mr_NOVLX : I<0, Pseudo, (outs), (ins f256mem:$dst, VR256X:$src),
                            "", []>, Sched<[WriteFStoreY]>;
def VMOVUPSZ128mr_NOVLX : I<0, Pseudo, (outs), (ins f128mem:$dst, VR128X:$src),
                            "", []>, Sched<[WriteFStoreX]>;
def VMOVUPSZ256mr_NOVLX : I<0, Pseudo, (outs), (ins f256mem:$dst, VR256X:$src),
                            "", []>, Sched<[WriteFStoreY]>;
}

def : Pat<(v8i64 (vselect VK8WM:$mask, (v8i64 immAllZerosV),
                          (v8i64 VR512:$src))),
   (VMOVDQA64Zrrkz (COPY_TO_REGCLASS (KNOTWrr (COPY_TO_REGCLASS VK8:$mask, VK16)),
                                              VK8), VR512:$src)>;

def : Pat<(v16i32 (vselect VK16WM:$mask, (v16i32 immAllZerosV),
                           (v16i32 VR512:$src))),
                  (VMOVDQA32Zrrkz (KNOTWrr VK16WM:$mask), VR512:$src)>;

// These patterns exist to prevent the above patterns from introducing a second
// mask inversion when one already exists.
def : Pat<(v8i64 (vselect (v8i1 (vnot VK8:$mask)),
                          (v8i64 immAllZerosV),
                          (v8i64 VR512:$src))),
                 (VMOVDQA64Zrrkz VK8:$mask, VR512:$src)>;
def : Pat<(v16i32 (vselect (v16i1 (vnot VK16:$mask)),
                           (v16i32 immAllZerosV),
                           (v16i32 VR512:$src))),
                  (VMOVDQA32Zrrkz VK16WM:$mask, VR512:$src)>;

multiclass mask_move_lowering<string InstrStr, X86VectorVTInfo Narrow,
                              X86VectorVTInfo Wide> {
 def : Pat<(Narrow.VT (vselect (Narrow.KVT Narrow.KRCWM:$mask),
                               Narrow.RC:$src1, Narrow.RC:$src0)),
           (EXTRACT_SUBREG
            (Wide.VT
             (!cast<Instruction>(InstrStr#"rrk")
              (Wide.VT (INSERT_SUBREG (IMPLICIT_DEF), Narrow.RC:$src0, Narrow.SubRegIdx)),
              (COPY_TO_REGCLASS Narrow.KRCWM:$mask, Wide.KRCWM),
              (Wide.VT (INSERT_SUBREG (IMPLICIT_DEF), Narrow.RC:$src1, Narrow.SubRegIdx)))),
            Narrow.SubRegIdx)>;

 def : Pat<(Narrow.VT (vselect (Narrow.KVT Narrow.KRCWM:$mask),
                               Narrow.RC:$src1, Narrow.ImmAllZerosV)),
           (EXTRACT_SUBREG
            (Wide.VT
             (!cast<Instruction>(InstrStr#"rrkz")
              (COPY_TO_REGCLASS Narrow.KRCWM:$mask, Wide.KRCWM),
              (Wide.VT (INSERT_SUBREG (IMPLICIT_DEF), Narrow.RC:$src1, Narrow.SubRegIdx)))),
            Narrow.SubRegIdx)>;
}

// Patterns for handling v8i1 selects of 256-bit vectors when VLX isn't
// available. Use a 512-bit operation and extract.
let Predicates = [HasAVX512, NoVLX, HasEVEX512] in {
  defm : mask_move_lowering<"VMOVAPSZ", v4f32x_info, v16f32_info>;
  defm : mask_move_lowering<"VMOVDQA32Z", v4i32x_info, v16i32_info>;
  defm : mask_move_lowering<"VMOVAPSZ", v8f32x_info, v16f32_info>;
  defm : mask_move_lowering<"VMOVDQA32Z", v8i32x_info, v16i32_info>;

  defm : mask_move_lowering<"VMOVAPDZ", v2f64x_info, v8f64_info>;
  defm : mask_move_lowering<"VMOVDQA64Z", v2i64x_info, v8i64_info>;
  defm : mask_move_lowering<"VMOVAPDZ", v4f64x_info, v8f64_info>;
  defm : mask_move_lowering<"VMOVDQA64Z", v4i64x_info, v8i64_info>;
}

let Predicates = [HasBWI, NoVLX, HasEVEX512] in {
  defm : mask_move_lowering<"VMOVDQU8Z", v16i8x_info, v64i8_info>;
  defm : mask_move_lowering<"VMOVDQU8Z", v32i8x_info, v64i8_info>;

  defm : mask_move_lowering<"VMOVDQU16Z", v8i16x_info, v32i16_info>;
  defm : mask_move_lowering<"VMOVDQU16Z", v16i16x_info, v32i16_info>;

  defm : mask_move_lowering<"VMOVDQU16Z", v8f16x_info, v32f16_info>;
  defm : mask_move_lowering<"VMOVDQU16Z", v16f16x_info, v32f16_info>;

  defm : mask_move_lowering<"VMOVDQU16Z", v8bf16x_info, v32bf16_info>;
  defm : mask_move_lowering<"VMOVDQU16Z", v16bf16x_info, v32bf16_info>;
}

let Predicates = [HasAVX512] in {
  // 512-bit load.
  def : Pat<(alignedloadv16i32 addr:$src),
            (VMOVDQA64Zrm addr:$src)>;
  def : Pat<(alignedloadv32i16 addr:$src),
            (VMOVDQA64Zrm addr:$src)>;
  def : Pat<(alignedloadv32f16 addr:$src),
            (VMOVAPSZrm addr:$src)>;
  def : Pat<(alignedloadv32bf16 addr:$src),
            (VMOVAPSZrm addr:$src)>;
  def : Pat<(alignedloadv64i8 addr:$src),
            (VMOVDQA64Zrm addr:$src)>;
  def : Pat<(loadv16i32 addr:$src),
            (VMOVDQU64Zrm addr:$src)>;
  def : Pat<(loadv32i16 addr:$src),
            (VMOVDQU64Zrm addr:$src)>;
  def : Pat<(loadv32f16 addr:$src),
            (VMOVUPSZrm addr:$src)>;
  def : Pat<(loadv32bf16 addr:$src),
            (VMOVUPSZrm addr:$src)>;
  def : Pat<(loadv64i8 addr:$src),
            (VMOVDQU64Zrm addr:$src)>;

  // 512-bit store.
  def : Pat<(alignedstore (v16i32 VR512:$src), addr:$dst),
            (VMOVDQA64Zmr addr:$dst, VR512:$src)>;
  def : Pat<(alignedstore (v32i16 VR512:$src), addr:$dst),
            (VMOVDQA64Zmr addr:$dst, VR512:$src)>;
  def : Pat<(alignedstore (v32f16 VR512:$src), addr:$dst),
            (VMOVAPSZmr addr:$dst, VR512:$src)>;
  def : Pat<(alignedstore (v32bf16 VR512:$src), addr:$dst),
            (VMOVAPSZmr addr:$dst, VR512:$src)>;
  def : Pat<(alignedstore (v64i8 VR512:$src), addr:$dst),
            (VMOVDQA64Zmr addr:$dst, VR512:$src)>;
  def : Pat<(store (v16i32 VR512:$src), addr:$dst),
            (VMOVDQU64Zmr addr:$dst, VR512:$src)>;
  def : Pat<(store (v32i16 VR512:$src), addr:$dst),
            (VMOVDQU64Zmr addr:$dst, VR512:$src)>;
  def : Pat<(store (v32f16 VR512:$src), addr:$dst),
            (VMOVUPSZmr addr:$dst, VR512:$src)>;
  def : Pat<(store (v32bf16 VR512:$src), addr:$dst),
            (VMOVUPSZmr addr:$dst, VR512:$src)>;
  def : Pat<(store (v64i8 VR512:$src), addr:$dst),
            (VMOVDQU64Zmr addr:$dst, VR512:$src)>;
}

let Predicates = [HasVLX] in {
  // 128-bit load.
  def : Pat<(alignedloadv4i32 addr:$src),
            (VMOVDQA64Z128rm addr:$src)>;
  def : Pat<(alignedloadv8i16 addr:$src),
            (VMOVDQA64Z128rm addr:$src)>;
  def : Pat<(alignedloadv8f16 addr:$src),
            (VMOVAPSZ128rm addr:$src)>;
  def : Pat<(alignedloadv8bf16 addr:$src),
            (VMOVAPSZ128rm addr:$src)>;
  def : Pat<(alignedloadv16i8 addr:$src),
            (VMOVDQA64Z128rm addr:$src)>;
  def : Pat<(loadv4i32 addr:$src),
            (VMOVDQU64Z128rm addr:$src)>;
  def : Pat<(loadv8i16 addr:$src),
            (VMOVDQU64Z128rm addr:$src)>;
  def : Pat<(loadv8f16 addr:$src),
            (VMOVUPSZ128rm addr:$src)>;
  def : Pat<(loadv8bf16 addr:$src),
            (VMOVUPSZ128rm addr:$src)>;
  def : Pat<(loadv16i8 addr:$src),
            (VMOVDQU64Z128rm addr:$src)>;

  // 128-bit store.
  def : Pat<(alignedstore (v4i32 VR128X:$src), addr:$dst),
            (VMOVDQA64Z128mr addr:$dst, VR128X:$src)>;
  def : Pat<(alignedstore (v8i16 VR128X:$src), addr:$dst),
            (VMOVDQA64Z128mr addr:$dst, VR128X:$src)>;
  def : Pat<(alignedstore (v8f16 VR128X:$src), addr:$dst),
            (VMOVAPSZ128mr addr:$dst, VR128X:$src)>;
  def : Pat<(alignedstore (v8bf16 VR128X:$src), addr:$dst),
            (VMOVAPSZ128mr addr:$dst, VR128X:$src)>;
  def : Pat<(alignedstore (v16i8 VR128X:$src), addr:$dst),
            (VMOVDQA64Z128mr addr:$dst, VR128X:$src)>;
  def : Pat<(store (v4i32 VR128X:$src), addr:$dst),
            (VMOVDQU64Z128mr addr:$dst, VR128X:$src)>;
  def : Pat<(store (v8i16 VR128X:$src), addr:$dst),
            (VMOVDQU64Z128mr addr:$dst, VR128X:$src)>;
  def : Pat<(store (v8f16 VR128X:$src), addr:$dst),
            (VMOVUPSZ128mr addr:$dst, VR128X:$src)>;
  def : Pat<(store (v8bf16 VR128X:$src), addr:$dst),
            (VMOVUPSZ128mr addr:$dst, VR128X:$src)>;
  def : Pat<(store (v16i8 VR128X:$src), addr:$dst),
            (VMOVDQU64Z128mr addr:$dst, VR128X:$src)>;

  // 256-bit load.
  def : Pat<(alignedloadv8i32 addr:$src),
            (VMOVDQA64Z256rm addr:$src)>;
  def : Pat<(alignedloadv16i16 addr:$src),
            (VMOVDQA64Z256rm addr:$src)>;
  def : Pat<(alignedloadv16f16 addr:$src),
            (VMOVAPSZ256rm addr:$src)>;
  def : Pat<(alignedloadv16bf16 addr:$src),
            (VMOVAPSZ256rm addr:$src)>;
  def : Pat<(alignedloadv32i8 addr:$src),
            (VMOVDQA64Z256rm addr:$src)>;
  def : Pat<(loadv8i32 addr:$src),
            (VMOVDQU64Z256rm addr:$src)>;
  def : Pat<(loadv16i16 addr:$src),
            (VMOVDQU64Z256rm addr:$src)>;
  def : Pat<(loadv16f16 addr:$src),
            (VMOVUPSZ256rm addr:$src)>;
  def : Pat<(loadv16bf16 addr:$src),
            (VMOVUPSZ256rm addr:$src)>;
  def : Pat<(loadv32i8 addr:$src),
            (VMOVDQU64Z256rm addr:$src)>;

  // 256-bit store.
  def : Pat<(alignedstore (v8i32 VR256X:$src), addr:$dst),
            (VMOVDQA64Z256mr addr:$dst, VR256X:$src)>;
  def : Pat<(alignedstore (v16i16 VR256X:$src), addr:$dst),
            (VMOVDQA64Z256mr addr:$dst, VR256X:$src)>;
  def : Pat<(alignedstore (v16f16 VR256X:$src), addr:$dst),
            (VMOVAPSZ256mr addr:$dst, VR256X:$src)>;
  def : Pat<(alignedstore (v16bf16 VR256X:$src), addr:$dst),
            (VMOVAPSZ256mr addr:$dst, VR256X:$src)>;
  def : Pat<(alignedstore (v32i8 VR256X:$src), addr:$dst),
            (VMOVDQA64Z256mr addr:$dst, VR256X:$src)>;
  def : Pat<(store (v8i32 VR256X:$src), addr:$dst),
            (VMOVDQU64Z256mr addr:$dst, VR256X:$src)>;
  def : Pat<(store (v16i16 VR256X:$src), addr:$dst),
            (VMOVDQU64Z256mr addr:$dst, VR256X:$src)>;
  def : Pat<(store (v16f16 VR256X:$src), addr:$dst),
            (VMOVUPSZ256mr addr:$dst, VR256X:$src)>;
  def : Pat<(store (v16bf16 VR256X:$src), addr:$dst),
            (VMOVUPSZ256mr addr:$dst, VR256X:$src)>;
  def : Pat<(store (v32i8 VR256X:$src), addr:$dst),
            (VMOVDQU64Z256mr addr:$dst, VR256X:$src)>;
}

multiclass mask_move_lowering_f16_bf16<AVX512VLVectorVTInfo _> {
let Predicates = [HasBWI] in {
  def : Pat<(_.info512.VT (vselect VK32WM:$mask, (_.info512.VT VR512:$src1), (_.info512.VT VR512:$src0))),
            (VMOVDQU16Zrrk VR512:$src0, VK32WM:$mask, VR512:$src1)>;
  def : Pat<(_.info512.VT (vselect VK32WM:$mask, (_.info512.VT VR512:$src1), _.info512.ImmAllZerosV)),
            (VMOVDQU16Zrrkz VK32WM:$mask, VR512:$src1)>;
  def : Pat<(_.info512.VT (vselect VK32WM:$mask,
                     (_.info512.VT (_.info512.AlignedLdFrag addr:$src)), (_.info512.VT VR512:$src0))),
            (VMOVDQU16Zrmk VR512:$src0, VK32WM:$mask, addr:$src)>;
  def : Pat<(_.info512.VT (vselect VK32WM:$mask,
                     (_.info512.VT (_.info512.AlignedLdFrag addr:$src)), _.info512.ImmAllZerosV)),
            (VMOVDQU16Zrmkz VK32WM:$mask, addr:$src)>;
  def : Pat<(_.info512.VT (vselect VK32WM:$mask,
                     (_.info512.VT (_.info512.LdFrag addr:$src)), (_.info512.VT VR512:$src0))),
            (VMOVDQU16Zrmk VR512:$src0, VK32WM:$mask, addr:$src)>;
  def : Pat<(_.info512.VT (vselect VK32WM:$mask,
                     (_.info512.VT (_.info512.LdFrag addr:$src)), _.info512.ImmAllZerosV)),
            (VMOVDQU16Zrmkz VK32WM:$mask, addr:$src)>;
  def : Pat<(_.info512.VT (masked_load addr:$src, VK32WM:$mask, (_.info512.VT VR512:$src0))),
            (VMOVDQU16Zrmk VR512:$src0, VK32WM:$mask, addr:$src)>;
  def : Pat<(_.info512.VT (masked_load addr:$src, VK32WM:$mask, undef)),
            (VMOVDQU16Zrmkz VK32WM:$mask, addr:$src)>;
  def : Pat<(_.info512.VT (masked_load addr:$src, VK32WM:$mask, _.info512.ImmAllZerosV)),
            (VMOVDQU16Zrmkz VK32WM:$mask, addr:$src)>;

  def : Pat<(masked_store (_.info512.VT VR512:$src), addr:$dst, VK32WM:$mask),
            (VMOVDQU16Zmrk addr:$dst, VK32WM:$mask, VR512:$src)>;
}
let Predicates = [HasBWI, HasVLX] in {
  def : Pat<(_.info256.VT (vselect VK16WM:$mask, (_.info256.VT VR256X:$src1), (_.info256.VT VR256X:$src0))),
            (VMOVDQU16Z256rrk VR256X:$src0, VK16WM:$mask, VR256X:$src1)>;
  def : Pat<(_.info256.VT (vselect VK16WM:$mask, (_.info256.VT VR256X:$src1), _.info256.ImmAllZerosV)),
            (VMOVDQU16Z256rrkz VK16WM:$mask, VR256X:$src1)>;
  def : Pat<(_.info256.VT (vselect VK16WM:$mask,
                     (_.info256.VT (_.info256.AlignedLdFrag addr:$src)), (_.info256.VT VR256X:$src0))),
            (VMOVDQU16Z256rmk VR256X:$src0, VK16WM:$mask, addr:$src)>;
  def : Pat<(_.info256.VT (vselect VK16WM:$mask,
                     (_.info256.VT (_.info256.AlignedLdFrag addr:$src)), _.info256.ImmAllZerosV)),
            (VMOVDQU16Z256rmkz VK16WM:$mask, addr:$src)>;
  def : Pat<(_.info256.VT (vselect VK16WM:$mask,
                     (_.info256.VT (_.info256.LdFrag addr:$src)), (_.info256.VT VR256X:$src0))),
            (VMOVDQU16Z256rmk VR256X:$src0, VK16WM:$mask, addr:$src)>;
  def : Pat<(_.info256.VT (vselect VK16WM:$mask,
                     (_.info256.VT (_.info256.LdFrag addr:$src)), _.info256.ImmAllZerosV)),
            (VMOVDQU16Z256rmkz VK16WM:$mask, addr:$src)>;
  def : Pat<(_.info256.VT (masked_load addr:$src, VK16WM:$mask, (_.info256.VT VR256X:$src0))),
            (VMOVDQU16Z256rmk VR256X:$src0, VK16WM:$mask, addr:$src)>;
  def : Pat<(_.info256.VT (masked_load addr:$src, VK16WM:$mask, undef)),
            (VMOVDQU16Z256rmkz VK16WM:$mask, addr:$src)>;
  def : Pat<(_.info256.VT (masked_load addr:$src, VK16WM:$mask, _.info256.ImmAllZerosV)),
            (VMOVDQU16Z256rmkz VK16WM:$mask, addr:$src)>;

  def : Pat<(masked_store (_.info256.VT VR256X:$src), addr:$dst, VK16WM:$mask),
            (VMOVDQU16Z256mrk addr:$dst, VK16WM:$mask, VR256X:$src)>;

  def : Pat<(_.info128.VT (vselect VK8WM:$mask, (_.info128.VT VR128X:$src1), (_.info128.VT VR128X:$src0))),
            (VMOVDQU16Z128rrk VR128X:$src0, VK8WM:$mask, VR128X:$src1)>;
  def : Pat<(_.info128.VT (vselect VK8WM:$mask, (_.info128.VT VR128X:$src1), _.info128.ImmAllZerosV)),
            (VMOVDQU16Z128rrkz VK8WM:$mask, VR128X:$src1)>;
  def : Pat<(_.info128.VT (vselect VK8WM:$mask,
                     (_.info128.VT (_.info128.AlignedLdFrag addr:$src)), (_.info128.VT VR128X:$src0))),
            (VMOVDQU16Z128rmk VR128X:$src0, VK8WM:$mask, addr:$src)>;
  def : Pat<(_.info128.VT (vselect VK8WM:$mask,
                     (_.info128.VT (_.info128.AlignedLdFrag addr:$src)), _.info128.ImmAllZerosV)),
            (VMOVDQU16Z128rmkz VK8WM:$mask, addr:$src)>;
  def : Pat<(_.info128.VT (vselect VK8WM:$mask,
                     (_.info128.VT (_.info128.LdFrag addr:$src)), (_.info128.VT VR128X:$src0))),
            (VMOVDQU16Z128rmk VR128X:$src0, VK8WM:$mask, addr:$src)>;
  def : Pat<(_.info128.VT (vselect VK8WM:$mask,
                     (_.info128.VT (_.info128.LdFrag addr:$src)), _.info128.ImmAllZerosV)),
            (VMOVDQU16Z128rmkz VK8WM:$mask, addr:$src)>;
  def : Pat<(_.info128.VT (masked_load addr:$src, VK8WM:$mask, (_.info128.VT VR128X:$src0))),
            (VMOVDQU16Z128rmk VR128X:$src0, VK8WM:$mask, addr:$src)>;
  def : Pat<(_.info128.VT (masked_load addr:$src, VK8WM:$mask, undef)),
            (VMOVDQU16Z128rmkz VK8WM:$mask, addr:$src)>;
  def : Pat<(_.info128.VT (masked_load addr:$src, VK8WM:$mask, _.info128.ImmAllZerosV)),
            (VMOVDQU16Z128rmkz VK8WM:$mask, addr:$src)>;

  def : Pat<(masked_store (_.info128.VT VR128X:$src), addr:$dst, VK8WM:$mask),
            (VMOVDQU16Z128mrk addr:$dst, VK8WM:$mask, VR128X:$src)>;
}
}

defm : mask_move_lowering_f16_bf16<avx512vl_f16_info>;
defm : mask_move_lowering_f16_bf16<avx512vl_bf16_info>;

// Move Int Doubleword to Packed Double Int
//
let ExeDomain = SSEPackedInt in {
def VMOVDI2PDIZrr : AVX512BI<0x6E, MRMSrcReg, (outs VR128X:$dst), (ins GR32:$src),
                      "vmovd\t{$src, $dst|$dst, $src}",
                      [(set VR128X:$dst,
                        (v4i32 (scalar_to_vector GR32:$src)))]>,
                        EVEX, Sched<[WriteVecMoveFromGpr]>;
def VMOVDI2PDIZrm : AVX512BI<0x6E, MRMSrcMem, (outs VR128X:$dst), (ins i32mem:$src),
                      "vmovd\t{$src, $dst|$dst, $src}",
                      [(set VR128X:$dst,
                        (v4i32 (scalar_to_vector (loadi32 addr:$src))))]>,
                      EVEX, EVEX_CD8<32, CD8VT1>, Sched<[WriteVecLoad]>;
def VMOV64toPQIZrr : AVX512BI<0x6E, MRMSrcReg, (outs VR128X:$dst), (ins GR64:$src),
                      "vmovq\t{$src, $dst|$dst, $src}",
                        [(set VR128X:$dst,
                          (v2i64 (scalar_to_vector GR64:$src)))]>,
                      EVEX, REX_W, Sched<[WriteVecMoveFromGpr]>;
let isCodeGenOnly = 1, ForceDisassemble = 1, hasSideEffects = 0, mayLoad = 1 in
def VMOV64toPQIZrm : AVX512BI<0x6E, MRMSrcMem, (outs VR128X:$dst),
                      (ins i64mem:$src),
                      "vmovq\t{$src, $dst|$dst, $src}", []>,
                      EVEX, REX_W, EVEX_CD8<64, CD8VT1>, Sched<[WriteVecLoad]>;
let isCodeGenOnly = 1 in {
def VMOV64toSDZrr : AVX512BI<0x6E, MRMSrcReg, (outs FR64X:$dst), (ins GR64:$src),
                       "vmovq\t{$src, $dst|$dst, $src}",
                       [(set FR64X:$dst, (bitconvert GR64:$src))]>,
                       EVEX, REX_W, Sched<[WriteVecMoveFromGpr]>;
def VMOVSDto64Zrr : AVX512BI<0x7E, MRMDestReg, (outs GR64:$dst), (ins FR64X:$src),
                         "vmovq\t{$src, $dst|$dst, $src}",
                         [(set GR64:$dst, (bitconvert FR64X:$src))]>,
                         EVEX, REX_W, Sched<[WriteVecMoveFromGpr]>;
}
} // ExeDomain = SSEPackedInt

// Move Int Doubleword to Single Scalar
//
let ExeDomain = SSEPackedInt, isCodeGenOnly = 1 in {
def VMOVDI2SSZrr  : AVX512BI<0x6E, MRMSrcReg, (outs FR32X:$dst), (ins GR32:$src),
                      "vmovd\t{$src, $dst|$dst, $src}",
                      [(set FR32X:$dst, (bitconvert GR32:$src))]>,
                      EVEX, Sched<[WriteVecMoveFromGpr]>;
} // ExeDomain = SSEPackedInt, isCodeGenOnly = 1

// Move doubleword from xmm register to r/m32
//
let ExeDomain = SSEPackedInt in {
def VMOVPDI2DIZrr  : AVX512BI<0x7E, MRMDestReg, (outs GR32:$dst), (ins VR128X:$src),
                       "vmovd\t{$src, $dst|$dst, $src}",
                       [(set GR32:$dst, (extractelt (v4i32 VR128X:$src),
                                        (iPTR 0)))]>,
                       EVEX, Sched<[WriteVecMoveToGpr]>;
def VMOVPDI2DIZmr  : AVX512BI<0x7E, MRMDestMem, (outs),
                       (ins i32mem:$dst, VR128X:$src),
                       "vmovd\t{$src, $dst|$dst, $src}",
                       [(store (i32 (extractelt (v4i32 VR128X:$src),
                                     (iPTR 0))), addr:$dst)]>,
                       EVEX, EVEX_CD8<32, CD8VT1>, Sched<[WriteVecStore]>;
} // ExeDomain = SSEPackedInt

// Move quadword from xmm1 register to r/m64
//
let ExeDomain = SSEPackedInt in {
def VMOVPQIto64Zrr : I<0x7E, MRMDestReg, (outs GR64:$dst), (ins VR128X:$src),
                      "vmovq\t{$src, $dst|$dst, $src}",
                      [(set GR64:$dst, (extractelt (v2i64 VR128X:$src),
                                                   (iPTR 0)))]>,
                      TB, PD, EVEX, REX_W, Sched<[WriteVecMoveToGpr]>,
                      Requires<[HasAVX512]>;

let isCodeGenOnly = 1, ForceDisassemble = 1, hasSideEffects = 0, mayStore = 1 in
def VMOVPQIto64Zmr : I<0x7E, MRMDestMem, (outs), (ins i64mem:$dst, VR128X:$src),
                      "vmovq\t{$src, $dst|$dst, $src}", []>, TB, PD,
                      EVEX, REX_W, EVEX_CD8<64, CD8VT1>, Sched<[WriteVecStore]>,
                      Requires<[HasAVX512, In64BitMode]>;

def VMOVPQI2QIZmr : I<0xD6, MRMDestMem, (outs),
                      (ins i64mem:$dst, VR128X:$src),
                      "vmovq\t{$src, $dst|$dst, $src}",
                      [(store (extractelt (v2i64 VR128X:$src), (iPTR 0)),
                              addr:$dst)]>,
                      EVEX, TB, PD, REX_W, EVEX_CD8<64, CD8VT1>,
                      Sched<[WriteVecStore]>, Requires<[HasAVX512]>;

let hasSideEffects = 0, isCodeGenOnly = 1, ForceDisassemble = 1 in
def VMOVPQI2QIZrr : AVX512BI<0xD6, MRMDestReg, (outs VR128X:$dst),
                             (ins VR128X:$src),
                             "vmovq\t{$src, $dst|$dst, $src}", []>,
                             EVEX, REX_W, Sched<[SchedWriteVecLogic.XMM]>;
} // ExeDomain = SSEPackedInt

def : InstAlias<"vmovq.s\t{$src, $dst|$dst, $src}",
                (VMOVPQI2QIZrr VR128X:$dst, VR128X:$src), 0>;

let Predicates = [HasAVX512] in {
  def : Pat<(X86vextractstore64 (v2i64 VR128X:$src), addr:$dst),
            (VMOVPQI2QIZmr addr:$dst, VR128X:$src)>;
}

// Move Scalar Single to Double Int
//
let ExeDomain = SSEPackedInt, isCodeGenOnly = 1 in {
def VMOVSS2DIZrr  : AVX512BI<0x7E, MRMDestReg, (outs GR32:$dst),
                      (ins FR32X:$src),
                      "vmovd\t{$src, $dst|$dst, $src}",
                      [(set GR32:$dst, (bitconvert FR32X:$src))]>,
                      EVEX, Sched<[WriteVecMoveToGpr]>;
} // ExeDomain = SSEPackedInt, isCodeGenOnly = 1

// Move Quadword Int to Packed Quadword Int
//
let ExeDomain = SSEPackedInt in {
def VMOVQI2PQIZrm : AVX512XSI<0x7E, MRMSrcMem, (outs VR128X:$dst),
                      (ins i64mem:$src),
                      "vmovq\t{$src, $dst|$dst, $src}",
                      [(set VR128X:$dst,
                        (v2i64 (scalar_to_vector (loadi64 addr:$src))))]>,
                      EVEX, REX_W, EVEX_CD8<8, CD8VT8>, Sched<[WriteVecLoad]>;
} // ExeDomain = SSEPackedInt

// Allow "vmovd" but print "vmovq".
def : InstAlias<"vmovd\t{$src, $dst|$dst, $src}",
                (VMOV64toPQIZrr VR128X:$dst, GR64:$src), 0>;
def : InstAlias<"vmovd\t{$src, $dst|$dst, $src}",
                (VMOVPQIto64Zrr GR64:$dst, VR128X:$src), 0>;

// Conversions between masks and scalar fp.
def : Pat<(v32i1 (bitconvert FR32X:$src)),
          (KMOVDkr (VMOVSS2DIZrr FR32X:$src))>;
def : Pat<(f32 (bitconvert VK32:$src)),
          (VMOVDI2SSZrr (KMOVDrk VK32:$src))>;

def : Pat<(v64i1 (bitconvert FR64X:$src)),
          (KMOVQkr (VMOVSDto64Zrr FR64X:$src))>;
def : Pat<(f64 (bitconvert VK64:$src)),
          (VMOV64toSDZrr (KMOVQrk VK64:$src))>;

//===----------------------------------------------------------------------===//
// AVX-512  MOVSH, MOVSS, MOVSD
//===----------------------------------------------------------------------===//

multiclass avx512_move_scalar<string asm, SDNode OpNode, PatFrag vzload_frag,
                              X86VectorVTInfo _, Predicate prd = HasAVX512> {
  let Predicates = !if (!eq (prd, HasFP16), [HasFP16], [prd, OptForSize]) in
  def rr : AVX512PI<0x10, MRMSrcReg, (outs _.RC:$dst),
             (ins _.RC:$src1, _.RC:$src2),
             !strconcat(asm, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             [(set _.RC:$dst, (_.VT (OpNode _.RC:$src1, _.RC:$src2)))],
             _.ExeDomain>, EVEX, VVVV, Sched<[SchedWriteFShuffle.XMM]>;
  let Predicates = [prd] in {
  def rrkz : AVX512PI<0x10, MRMSrcReg, (outs _.RC:$dst),
              (ins _.KRCWM:$mask, _.RC:$src1, _.RC:$src2),
              !strconcat(asm, "\t{$src2, $src1, $dst {${mask}} {z}|",
              "$dst {${mask}} {z}, $src1, $src2}"),
              [(set _.RC:$dst, (_.VT (X86selects _.KRCWM:$mask,
                                      (_.VT (OpNode _.RC:$src1, _.RC:$src2)),
                                      _.ImmAllZerosV)))],
              _.ExeDomain>, EVEX, VVVV, EVEX_KZ, Sched<[SchedWriteFShuffle.XMM]>;
  let Constraints = "$src0 = $dst"  in
  def rrk : AVX512PI<0x10, MRMSrcReg, (outs _.RC:$dst),
             (ins _.RC:$src0, _.KRCWM:$mask, _.RC:$src1, _.RC:$src2),
             !strconcat(asm, "\t{$src2, $src1, $dst {${mask}}|",
             "$dst {${mask}}, $src1, $src2}"),
             [(set _.RC:$dst, (_.VT (X86selects _.KRCWM:$mask,
                                     (_.VT (OpNode _.RC:$src1, _.RC:$src2)),
                                     (_.VT _.RC:$src0))))],
             _.ExeDomain>, EVEX, VVVV, EVEX_K, Sched<[SchedWriteFShuffle.XMM]>;
  let canFoldAsLoad = 1, isReMaterializable = 1 in {
  def rm : AVX512PI<0x10, MRMSrcMem, (outs _.RC:$dst), (ins _.ScalarMemOp:$src),
             !strconcat(asm, "\t{$src, $dst|$dst, $src}"),
             [(set _.RC:$dst, (_.VT (vzload_frag addr:$src)))],
             _.ExeDomain>, EVEX, Sched<[WriteFLoad]>;
  // _alt version uses FR32/FR64 register class.
  let isCodeGenOnly = 1 in
  def rm_alt : AVX512PI<0x10, MRMSrcMem, (outs _.FRC:$dst), (ins _.ScalarMemOp:$src),
                 !strconcat(asm, "\t{$src, $dst|$dst, $src}"),
                 [(set _.FRC:$dst, (_.ScalarLdFrag addr:$src))],
                 _.ExeDomain>, EVEX, Sched<[WriteFLoad]>;
  }
  let mayLoad = 1, hasSideEffects = 0 in {
    let Constraints = "$src0 = $dst" in
    def rmk : AVX512PI<0x10, MRMSrcMem, (outs _.RC:$dst),
               (ins _.RC:$src0, _.KRCWM:$mask, _.ScalarMemOp:$src),
               !strconcat(asm, "\t{$src, $dst {${mask}}|",
               "$dst {${mask}}, $src}"),
               [], _.ExeDomain>, EVEX, EVEX_K, Sched<[WriteFLoad]>;
    def rmkz : AVX512PI<0x10, MRMSrcMem, (outs _.RC:$dst),
               (ins _.KRCWM:$mask, _.ScalarMemOp:$src),
               !strconcat(asm, "\t{$src, $dst {${mask}} {z}|",
               "$dst {${mask}} {z}, $src}"),
               [], _.ExeDomain>, EVEX, EVEX_KZ, Sched<[WriteFLoad]>;
  }
  def mr: AVX512PI<0x11, MRMDestMem, (outs), (ins _.ScalarMemOp:$dst, _.FRC:$src),
             !strconcat(asm, "\t{$src, $dst|$dst, $src}"),
             [(store _.FRC:$src, addr:$dst)],  _.ExeDomain>,
             EVEX, Sched<[WriteFStore]>;
  let mayStore = 1, hasSideEffects = 0 in
  def mrk: AVX512PI<0x11, MRMDestMem, (outs),
              (ins _.ScalarMemOp:$dst, VK1WM:$mask, _.RC:$src),
              !strconcat(asm, "\t{$src, $dst {${mask}}|$dst {${mask}}, $src}"),
              [], _.ExeDomain>, EVEX, EVEX_K, Sched<[WriteFStore]>;
  }
}

defm VMOVSSZ : avx512_move_scalar<"vmovss", X86Movss, X86vzload32, f32x_info>,
                                  VEX_LIG, TB, XS, EVEX_CD8<32, CD8VT1>;

defm VMOVSDZ : avx512_move_scalar<"vmovsd", X86Movsd, X86vzload64, f64x_info>,
                                  VEX_LIG, TB, XD, REX_W, EVEX_CD8<64, CD8VT1>;

defm VMOVSHZ : avx512_move_scalar<"vmovsh", X86Movsh, X86vzload16, f16x_info,
                                  HasFP16>,
                                  VEX_LIG, T_MAP5, XS, EVEX_CD8<16, CD8VT1>;

multiclass avx512_move_scalar_lowering<string InstrStr, SDNode OpNode,
                                       PatLeaf ZeroFP, X86VectorVTInfo _> {

def : Pat<(_.VT (OpNode _.RC:$src0,
                        (_.VT (scalar_to_vector
                                  (_.EltVT (X86selects VK1WM:$mask,
                                                       (_.EltVT _.FRC:$src1),
                                                       (_.EltVT _.FRC:$src2))))))),
          (!cast<Instruction>(InstrStr#rrk)
                        (_.VT (COPY_TO_REGCLASS _.FRC:$src2, _.RC)),
                        VK1WM:$mask,
                        (_.VT _.RC:$src0),
                        (_.VT (COPY_TO_REGCLASS _.FRC:$src1, _.RC)))>;

def : Pat<(_.VT (OpNode _.RC:$src0,
                        (_.VT (scalar_to_vector
                                  (_.EltVT (X86selects VK1WM:$mask,
                                                       (_.EltVT _.FRC:$src1),
                                                       (_.EltVT ZeroFP))))))),
          (!cast<Instruction>(InstrStr#rrkz)
                        VK1WM:$mask,
                        (_.VT _.RC:$src0),
                        (_.VT (COPY_TO_REGCLASS _.FRC:$src1, _.RC)))>;
}

multiclass avx512_store_scalar_lowering<string InstrStr, AVX512VLVectorVTInfo _,
                                        dag Mask, RegisterClass MaskRC> {

def : Pat<(masked_store
             (_.info512.VT (insert_subvector undef,
                               (_.info128.VT _.info128.RC:$src),
                               (iPTR 0))), addr:$dst, Mask),
          (!cast<Instruction>(InstrStr#mrk) addr:$dst,
                      (COPY_TO_REGCLASS MaskRC:$mask, VK1WM),
                      _.info128.RC:$src)>;

}

multiclass avx512_store_scalar_lowering_subreg<string InstrStr,
                                               AVX512VLVectorVTInfo _,
                                               dag Mask, RegisterClass MaskRC,
                                               SubRegIndex subreg> {

def : Pat<(masked_store
             (_.info512.VT (insert_subvector undef,
                               (_.info128.VT _.info128.RC:$src),
                               (iPTR 0))), addr:$dst, Mask),
          (!cast<Instruction>(InstrStr#mrk) addr:$dst,
                      (COPY_TO_REGCLASS (i32 (INSERT_SUBREG (IMPLICIT_DEF), MaskRC:$mask, subreg)), VK1WM),
                      _.info128.RC:$src)>;

}

// This matches the more recent codegen from clang that avoids emitting a 512
// bit masked store directly. Codegen will widen 128-bit masked store to 512
// bits on AVX512F only targets.
multiclass avx512_store_scalar_lowering_subreg2<string InstrStr,
                                               AVX512VLVectorVTInfo _,
                                               dag Mask512, dag Mask128,
                                               RegisterClass MaskRC,
                                               SubRegIndex subreg> {

// AVX512F pattern.
def : Pat<(masked_store
             (_.info512.VT (insert_subvector undef,
                               (_.info128.VT _.info128.RC:$src),
                               (iPTR 0))), addr:$dst, Mask512),
          (!cast<Instruction>(InstrStr#mrk) addr:$dst,
                      (COPY_TO_REGCLASS (i32 (INSERT_SUBREG (IMPLICIT_DEF), MaskRC:$mask, subreg)), VK1WM),
                      _.info128.RC:$src)>;

// AVX512VL pattern.
def : Pat<(masked_store (_.info128.VT _.info128.RC:$src), addr:$dst, Mask128),
          (!cast<Instruction>(InstrStr#mrk) addr:$dst,
                      (COPY_TO_REGCLASS (i32 (INSERT_SUBREG (IMPLICIT_DEF), MaskRC:$mask, subreg)), VK1WM),
                      _.info128.RC:$src)>;
}

multiclass avx512_load_scalar_lowering<string InstrStr, AVX512VLVectorVTInfo _,
                                       dag Mask, RegisterClass MaskRC> {

def : Pat<(_.info128.VT (extract_subvector
                         (_.info512.VT (masked_load addr:$srcAddr, Mask,
                                        _.info512.ImmAllZerosV)),
                           (iPTR 0))),
          (!cast<Instruction>(InstrStr#rmkz)
                      (COPY_TO_REGCLASS MaskRC:$mask, VK1WM),
                      addr:$srcAddr)>;

def : Pat<(_.info128.VT (extract_subvector
                (_.info512.VT (masked_load addr:$srcAddr, Mask,
                      (_.info512.VT (insert_subvector undef,
                            (_.info128.VT (X86vzmovl _.info128.RC:$src)),
                            (iPTR 0))))),
                (iPTR 0))),
          (!cast<Instruction>(InstrStr#rmk) _.info128.RC:$src,
                      (COPY_TO_REGCLASS MaskRC:$mask, VK1WM),
                      addr:$srcAddr)>;

}

multiclass avx512_load_scalar_lowering_subreg<string InstrStr,
                                              AVX512VLVectorVTInfo _,
                                              dag Mask, RegisterClass MaskRC,
                                              SubRegIndex subreg> {

def : Pat<(_.info128.VT (extract_subvector
                         (_.info512.VT (masked_load addr:$srcAddr, Mask,
                                        _.info512.ImmAllZerosV)),
                           (iPTR 0))),
          (!cast<Instruction>(InstrStr#rmkz)
                      (COPY_TO_REGCLASS (i32 (INSERT_SUBREG (IMPLICIT_DEF), MaskRC:$mask, subreg)), VK1WM),
                      addr:$srcAddr)>;

def : Pat<(_.info128.VT (extract_subvector
                (_.info512.VT (masked_load addr:$srcAddr, Mask,
                      (_.info512.VT (insert_subvector undef,
                            (_.info128.VT (X86vzmovl _.info128.RC:$src)),
                            (iPTR 0))))),
                (iPTR 0))),
          (!cast<Instruction>(InstrStr#rmk) _.info128.RC:$src,
                      (COPY_TO_REGCLASS (i32 (INSERT_SUBREG (IMPLICIT_DEF), MaskRC:$mask, subreg)), VK1WM),
                      addr:$srcAddr)>;

}

// This matches the more recent codegen from clang that avoids emitting a 512
// bit masked load directly. Codegen will widen 128-bit masked load to 512
// bits on AVX512F only targets.
multiclass avx512_load_scalar_lowering_subreg2<string InstrStr,
                                              AVX512VLVectorVTInfo _,
                                              dag Mask512, dag Mask128,
                                              RegisterClass MaskRC,
                                              SubRegIndex subreg> {
// AVX512F patterns.
def : Pat<(_.info128.VT (extract_subvector
                         (_.info512.VT (masked_load addr:$srcAddr, Mask512,
                                        _.info512.ImmAllZerosV)),
                           (iPTR 0))),
          (!cast<Instruction>(InstrStr#rmkz)
                      (COPY_TO_REGCLASS (i32 (INSERT_SUBREG (IMPLICIT_DEF), MaskRC:$mask, subreg)), VK1WM),
                      addr:$srcAddr)>;

def : Pat<(_.info128.VT (extract_subvector
                (_.info512.VT (masked_load addr:$srcAddr, Mask512,
                      (_.info512.VT (insert_subvector undef,
                            (_.info128.VT (X86vzmovl _.info128.RC:$src)),
                            (iPTR 0))))),
                (iPTR 0))),
          (!cast<Instruction>(InstrStr#rmk) _.info128.RC:$src,
                      (COPY_TO_REGCLASS (i32 (INSERT_SUBREG (IMPLICIT_DEF), MaskRC:$mask, subreg)), VK1WM),
                      addr:$srcAddr)>;

// AVX512Vl patterns.
def : Pat<(_.info128.VT (masked_load addr:$srcAddr, Mask128,
                         _.info128.ImmAllZerosV)),
          (!cast<Instruction>(InstrStr#rmkz)
                      (COPY_TO_REGCLASS (i32 (INSERT_SUBREG (IMPLICIT_DEF), MaskRC:$mask, subreg)), VK1WM),
                      addr:$srcAddr)>;

def : Pat<(_.info128.VT (masked_load addr:$srcAddr, Mask128,
                         (_.info128.VT (X86vzmovl _.info128.RC:$src)))),
          (!cast<Instruction>(InstrStr#rmk) _.info128.RC:$src,
                      (COPY_TO_REGCLASS (i32 (INSERT_SUBREG (IMPLICIT_DEF), MaskRC:$mask, subreg)), VK1WM),
                      addr:$srcAddr)>;
}

defm : avx512_move_scalar_lowering<"VMOVSSZ", X86Movss, fp32imm0, v4f32x_info>;
defm : avx512_move_scalar_lowering<"VMOVSDZ", X86Movsd, fp64imm0, v2f64x_info>;

defm : avx512_store_scalar_lowering<"VMOVSSZ", avx512vl_f32_info,
                   (v16i1 (bitconvert (i16 (trunc (and GR32:$mask, (i32 1)))))), GR32>;
defm : avx512_store_scalar_lowering_subreg<"VMOVSSZ", avx512vl_f32_info,
                   (v16i1 (bitconvert (i16 (and GR16:$mask, (i16 1))))), GR16, sub_16bit>;
defm : avx512_store_scalar_lowering_subreg<"VMOVSDZ", avx512vl_f64_info,
                   (v8i1 (bitconvert (i8 (and GR8:$mask, (i8 1))))), GR8, sub_8bit>;

let Predicates = [HasFP16] in {
defm : avx512_move_scalar_lowering<"VMOVSHZ", X86Movsh, fp16imm0, v8f16x_info>;
defm : avx512_store_scalar_lowering<"VMOVSHZ", avx512vl_f16_info,
                   (v32i1 (bitconvert (and GR32:$mask, (i32 1)))), GR32>;
defm : avx512_store_scalar_lowering_subreg<"VMOVSHZ", avx512vl_f16_info,
                   (v32i1 (bitconvert (and GR32:$mask, (i32 1)))), GR32, sub_32bit>;
defm : avx512_store_scalar_lowering_subreg2<"VMOVSHZ", avx512vl_f16_info,
                   (v32i1 (insert_subvector
                           (v32i1 immAllZerosV),
                           (v8i1 (bitconvert (and GR8:$mask, (i8 1)))),
                           (iPTR 0))),
                   (v8i1 (bitconvert (and GR8:$mask, (i8 1)))),
                   GR8, sub_8bit>;

defm : avx512_load_scalar_lowering<"VMOVSHZ", avx512vl_f16_info,
                   (v32i1 (bitconvert (and GR32:$mask, (i32 1)))), GR32>;
defm : avx512_load_scalar_lowering_subreg<"VMOVSHZ", avx512vl_f16_info,
                   (v32i1 (bitconvert (and GR32:$mask, (i32 1)))), GR32, sub_32bit>;
defm : avx512_load_scalar_lowering_subreg2<"VMOVSHZ", avx512vl_f16_info,
                   (v32i1 (insert_subvector
                           (v32i1 immAllZerosV),
                           (v8i1 (bitconvert (and GR8:$mask, (i8 1)))),
                           (iPTR 0))),
                   (v8i1 (bitconvert (and GR8:$mask, (i8 1)))),
                   GR8, sub_8bit>;

def : Pat<(f16 (X86selects VK1WM:$mask, (f16 FR16X:$src1), (f16 FR16X:$src2))),
          (COPY_TO_REGCLASS (v8f16 (VMOVSHZrrk
           (v8f16 (COPY_TO_REGCLASS FR16X:$src2, VR128X)),
           VK1WM:$mask, (v8f16 (IMPLICIT_DEF)),
           (v8f16 (COPY_TO_REGCLASS FR16X:$src1, VR128X)))), FR16X)>;

def : Pat<(f16 (X86selects VK1WM:$mask, (f16 FR16X:$src1), fp16imm0)),
          (COPY_TO_REGCLASS (v8f16 (VMOVSHZrrkz VK1WM:$mask, (v8f16 (IMPLICIT_DEF)),
           (v8f16 (COPY_TO_REGCLASS FR16X:$src1, VR128X)))), FR16X)>;
}

defm : avx512_store_scalar_lowering_subreg2<"VMOVSSZ", avx512vl_f32_info,
                   (v16i1 (insert_subvector
                           (v16i1 immAllZerosV),
                           (v4i1 (extract_subvector
                                  (v8i1 (bitconvert (and GR8:$mask, (i8 1)))),
                                  (iPTR 0))),
                           (iPTR 0))),
                   (v4i1 (extract_subvector
                          (v8i1 (bitconvert (and GR8:$mask, (i8 1)))),
                          (iPTR 0))), GR8, sub_8bit>;
defm : avx512_store_scalar_lowering_subreg2<"VMOVSDZ", avx512vl_f64_info,
                   (v8i1
                    (extract_subvector
                     (v16i1
                      (insert_subvector
                       (v16i1 immAllZerosV),
                       (v2i1 (extract_subvector
                              (v8i1 (bitconvert (i8 (and GR8:$mask, (i8 1))))),
                              (iPTR 0))),
                       (iPTR 0))),
                     (iPTR 0))),
                   (v2i1 (extract_subvector
                          (v8i1 (bitconvert (i8 (and GR8:$mask, (i8 1))))),
                          (iPTR 0))), GR8, sub_8bit>;

defm : avx512_load_scalar_lowering<"VMOVSSZ", avx512vl_f32_info,
                   (v16i1 (bitconvert (i16 (trunc (and GR32:$mask, (i32 1)))))), GR32>;
defm : avx512_load_scalar_lowering_subreg<"VMOVSSZ", avx512vl_f32_info,
                   (v16i1 (bitconvert (i16 (and GR16:$mask, (i16 1))))), GR16, sub_16bit>;
defm : avx512_load_scalar_lowering_subreg<"VMOVSDZ", avx512vl_f64_info,
                   (v8i1 (bitconvert (i8 (and GR8:$mask, (i8 1))))), GR8, sub_8bit>;

defm : avx512_load_scalar_lowering_subreg2<"VMOVSSZ", avx512vl_f32_info,
                   (v16i1 (insert_subvector
                           (v16i1 immAllZerosV),
                           (v4i1 (extract_subvector
                                  (v8i1 (bitconvert (and GR8:$mask, (i8 1)))),
                                  (iPTR 0))),
                           (iPTR 0))),
                   (v4i1 (extract_subvector
                          (v8i1 (bitconvert (and GR8:$mask, (i8 1)))),
                          (iPTR 0))), GR8, sub_8bit>;
defm : avx512_load_scalar_lowering_subreg2<"VMOVSDZ", avx512vl_f64_info,
                   (v8i1
                    (extract_subvector
                     (v16i1
                      (insert_subvector
                       (v16i1 immAllZerosV),
                       (v2i1 (extract_subvector
                              (v8i1 (bitconvert (i8 (and GR8:$mask, (i8 1))))),
                              (iPTR 0))),
                       (iPTR 0))),
                     (iPTR 0))),
                   (v2i1 (extract_subvector
                          (v8i1 (bitconvert (i8 (and GR8:$mask, (i8 1))))),
                          (iPTR 0))), GR8, sub_8bit>;

def : Pat<(f32 (X86selects VK1WM:$mask, (f32 FR32X:$src1), (f32 FR32X:$src2))),
          (COPY_TO_REGCLASS (v4f32 (VMOVSSZrrk
           (v4f32 (COPY_TO_REGCLASS FR32X:$src2, VR128X)),
           VK1WM:$mask, (v4f32 (IMPLICIT_DEF)),
           (v4f32 (COPY_TO_REGCLASS FR32X:$src1, VR128X)))), FR32X)>;

def : Pat<(f32 (X86selects VK1WM:$mask, (f32 FR32X:$src1), fp32imm0)),
          (COPY_TO_REGCLASS (v4f32 (VMOVSSZrrkz VK1WM:$mask, (v4f32 (IMPLICIT_DEF)),
           (v4f32 (COPY_TO_REGCLASS FR32X:$src1, VR128X)))), FR32X)>;

def : Pat<(f32 (X86selects VK1WM:$mask, (loadf32 addr:$src), (f32 FR32X:$src0))),
          (COPY_TO_REGCLASS
           (v4f32 (VMOVSSZrmk (v4f32 (COPY_TO_REGCLASS FR32X:$src0, VR128X)),
                                                       VK1WM:$mask, addr:$src)),
           FR32X)>;
def : Pat<(f32 (X86selects VK1WM:$mask, (loadf32 addr:$src), fp32imm0)),
          (COPY_TO_REGCLASS (v4f32 (VMOVSSZrmkz VK1WM:$mask, addr:$src)), FR32X)>;

def : Pat<(f64 (X86selects VK1WM:$mask, (f64 FR64X:$src1), (f64 FR64X:$src2))),
          (COPY_TO_REGCLASS (v2f64 (VMOVSDZrrk
           (v2f64 (COPY_TO_REGCLASS FR64X:$src2, VR128X)),
           VK1WM:$mask, (v2f64 (IMPLICIT_DEF)),
           (v2f64 (COPY_TO_REGCLASS FR64X:$src1, VR128X)))), FR64X)>;

def : Pat<(f64 (X86selects VK1WM:$mask, (f64 FR64X:$src1), fp64imm0)),
          (COPY_TO_REGCLASS (v2f64 (VMOVSDZrrkz VK1WM:$mask, (v2f64 (IMPLICIT_DEF)),
           (v2f64 (COPY_TO_REGCLASS FR64X:$src1, VR128X)))), FR64X)>;

def : Pat<(f64 (X86selects VK1WM:$mask, (loadf64 addr:$src), (f64 FR64X:$src0))),
          (COPY_TO_REGCLASS
           (v2f64 (VMOVSDZrmk (v2f64 (COPY_TO_REGCLASS FR64X:$src0, VR128X)),
                                                       VK1WM:$mask, addr:$src)),
           FR64X)>;
def : Pat<(f64 (X86selects VK1WM:$mask, (loadf64 addr:$src), fp64imm0)),
          (COPY_TO_REGCLASS (v2f64 (VMOVSDZrmkz VK1WM:$mask, addr:$src)), FR64X)>;


def : Pat<(v4f32 (X86selects VK1WM:$mask, (v4f32 VR128X:$src1), (v4f32 VR128X:$src2))),
          (VMOVSSZrrk VR128X:$src2, VK1WM:$mask, VR128X:$src1, VR128X:$src1)>;
def : Pat<(v2f64 (X86selects VK1WM:$mask, (v2f64 VR128X:$src1), (v2f64 VR128X:$src2))),
          (VMOVSDZrrk VR128X:$src2, VK1WM:$mask, VR128X:$src1, VR128X:$src1)>;

def : Pat<(v4f32 (X86selects VK1WM:$mask, (v4f32 VR128X:$src1), (v4f32 immAllZerosV))),
          (VMOVSSZrrkz VK1WM:$mask, VR128X:$src1, VR128X:$src1)>;
def : Pat<(v2f64 (X86selects VK1WM:$mask, (v2f64 VR128X:$src1), (v2f64 immAllZerosV))),
          (VMOVSDZrrkz VK1WM:$mask, VR128X:$src1, VR128X:$src1)>;

let hasSideEffects = 0, isCodeGenOnly = 1, ForceDisassemble = 1 in {
  let Predicates = [HasFP16] in {
    def VMOVSHZrr_REV: AVX512<0x11, MRMDestReg, (outs VR128X:$dst),
        (ins VR128X:$src1, VR128X:$src2),
        "vmovsh\t{$src2, $src1, $dst|$dst, $src1, $src2}",
        []>, T_MAP5, XS, EVEX, VVVV, VEX_LIG,
        Sched<[SchedWriteFShuffle.XMM]>;

    let Constraints = "$src0 = $dst" in
    def VMOVSHZrrk_REV: AVX512<0x11, MRMDestReg, (outs VR128X:$dst),
        (ins f16x_info.RC:$src0, f16x_info.KRCWM:$mask,
         VR128X:$src1, VR128X:$src2),
        "vmovsh\t{$src2, $src1, $dst {${mask}}|"#
          "$dst {${mask}}, $src1, $src2}",
        []>, T_MAP5, XS, EVEX_K, EVEX, VVVV, VEX_LIG,
        Sched<[SchedWriteFShuffle.XMM]>;

    def VMOVSHZrrkz_REV: AVX512<0x11, MRMDestReg, (outs VR128X:$dst),
        (ins f16x_info.KRCWM:$mask, VR128X:$src1, VR128X:$src2),
        "vmovsh\t{$src2, $src1, $dst {${mask}} {z}|"#
          "$dst {${mask}} {z}, $src1, $src2}",
        []>, EVEX_KZ, T_MAP5, XS, EVEX, VVVV, VEX_LIG,
        Sched<[SchedWriteFShuffle.XMM]>;
  }
  def VMOVSSZrr_REV: AVX512<0x11, MRMDestReg, (outs VR128X:$dst),
                           (ins VR128X:$src1, VR128X:$src2),
                           "vmovss\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                           []>, TB, XS, EVEX, VVVV, VEX_LIG,
                           Sched<[SchedWriteFShuffle.XMM]>;

  let Constraints = "$src0 = $dst" in
  def VMOVSSZrrk_REV: AVX512<0x11, MRMDestReg, (outs VR128X:$dst),
                             (ins f32x_info.RC:$src0, f32x_info.KRCWM:$mask,
                                                   VR128X:$src1, VR128X:$src2),
                             "vmovss\t{$src2, $src1, $dst {${mask}}|"#
                                        "$dst {${mask}}, $src1, $src2}",
                             []>, EVEX_K, TB, XS, EVEX, VVVV, VEX_LIG,
                             Sched<[SchedWriteFShuffle.XMM]>;

  def VMOVSSZrrkz_REV: AVX512<0x11, MRMDestReg, (outs VR128X:$dst),
                         (ins f32x_info.KRCWM:$mask, VR128X:$src1, VR128X:$src2),
                         "vmovss\t{$src2, $src1, $dst {${mask}} {z}|"#
                                    "$dst {${mask}} {z}, $src1, $src2}",
                         []>, EVEX_KZ, TB, XS, EVEX, VVVV, VEX_LIG,
                         Sched<[SchedWriteFShuffle.XMM]>;

  def VMOVSDZrr_REV: AVX512<0x11, MRMDestReg, (outs VR128X:$dst),
                           (ins VR128X:$src1, VR128X:$src2),
                           "vmovsd\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                           []>, TB, XD, EVEX, VVVV, VEX_LIG, REX_W,
                           Sched<[SchedWriteFShuffle.XMM]>;

  let Constraints = "$src0 = $dst" in
  def VMOVSDZrrk_REV: AVX512<0x11, MRMDestReg, (outs VR128X:$dst),
                             (ins f64x_info.RC:$src0, f64x_info.KRCWM:$mask,
                                                   VR128X:$src1, VR128X:$src2),
                             "vmovsd\t{$src2, $src1, $dst {${mask}}|"#
                                        "$dst {${mask}}, $src1, $src2}",
                             []>, EVEX_K, TB, XD, EVEX, VVVV, VEX_LIG,
                             REX_W, Sched<[SchedWriteFShuffle.XMM]>;

  def VMOVSDZrrkz_REV: AVX512<0x11, MRMDestReg, (outs VR128X:$dst),
                              (ins f64x_info.KRCWM:$mask, VR128X:$src1,
                                                          VR128X:$src2),
                              "vmovsd\t{$src2, $src1, $dst {${mask}} {z}|"#
                                         "$dst {${mask}} {z}, $src1, $src2}",
                              []>, EVEX_KZ, TB, XD, EVEX, VVVV, VEX_LIG,
                              REX_W, Sched<[SchedWriteFShuffle.XMM]>;
}

def : InstAlias<"vmovsh.s\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                (VMOVSHZrr_REV VR128X:$dst, VR128X:$src1, VR128X:$src2), 0>;
def : InstAlias<"vmovsh.s\t{$src2, $src1, $dst {${mask}}|"#
                             "$dst {${mask}}, $src1, $src2}",
                (VMOVSHZrrk_REV VR128X:$dst, VK1WM:$mask,
                                VR128X:$src1, VR128X:$src2), 0>;
def : InstAlias<"vmovsh.s\t{$src2, $src1, $dst {${mask}} {z}|"#
                             "$dst {${mask}} {z}, $src1, $src2}",
                (VMOVSHZrrkz_REV VR128X:$dst, VK1WM:$mask,
                                 VR128X:$src1, VR128X:$src2), 0>;
def : InstAlias<"vmovss.s\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                (VMOVSSZrr_REV VR128X:$dst, VR128X:$src1, VR128X:$src2), 0>;
def : InstAlias<"vmovss.s\t{$src2, $src1, $dst {${mask}}|"#
                             "$dst {${mask}}, $src1, $src2}",
                (VMOVSSZrrk_REV VR128X:$dst, VK1WM:$mask,
                                VR128X:$src1, VR128X:$src2), 0>;
def : InstAlias<"vmovss.s\t{$src2, $src1, $dst {${mask}} {z}|"#
                             "$dst {${mask}} {z}, $src1, $src2}",
                (VMOVSSZrrkz_REV VR128X:$dst, VK1WM:$mask,
                                 VR128X:$src1, VR128X:$src2), 0>;
def : InstAlias<"vmovsd.s\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                (VMOVSDZrr_REV VR128X:$dst, VR128X:$src1, VR128X:$src2), 0>;
def : InstAlias<"vmovsd.s\t{$src2, $src1, $dst {${mask}}|"#
                             "$dst {${mask}}, $src1, $src2}",
                (VMOVSDZrrk_REV VR128X:$dst, VK1WM:$mask,
                                VR128X:$src1, VR128X:$src2), 0>;
def : InstAlias<"vmovsd.s\t{$src2, $src1, $dst {${mask}} {z}|"#
                             "$dst {${mask}} {z}, $src1, $src2}",
                (VMOVSDZrrkz_REV VR128X:$dst, VK1WM:$mask,
                                 VR128X:$src1, VR128X:$src2), 0>;

let Predicates = [HasAVX512, OptForSize] in {
  def : Pat<(v4f32 (X86vzmovl (v4f32 VR128X:$src))),
            (VMOVSSZrr (v4f32 (AVX512_128_SET0)), VR128X:$src)>;
  def : Pat<(v4i32 (X86vzmovl (v4i32 VR128X:$src))),
            (VMOVSSZrr (v4i32 (AVX512_128_SET0)), VR128X:$src)>;

  // Move low f32 and clear high bits.
  def : Pat<(v8f32 (X86vzmovl (v8f32 VR256X:$src))),
            (SUBREG_TO_REG (i32 0),
             (v4f32 (VMOVSSZrr (v4f32 (AVX512_128_SET0)),
              (v4f32 (EXTRACT_SUBREG (v8f32 VR256X:$src), sub_xmm)))), sub_xmm)>;
  def : Pat<(v8i32 (X86vzmovl (v8i32 VR256X:$src))),
            (SUBREG_TO_REG (i32 0),
             (v4i32 (VMOVSSZrr (v4i32 (AVX512_128_SET0)),
              (v4i32 (EXTRACT_SUBREG (v8i32 VR256X:$src), sub_xmm)))), sub_xmm)>;

  def : Pat<(v16f32 (X86vzmovl (v16f32 VR512:$src))),
            (SUBREG_TO_REG (i32 0),
             (v4f32 (VMOVSSZrr (v4f32 (AVX512_128_SET0)),
              (v4f32 (EXTRACT_SUBREG (v16f32 VR512:$src), sub_xmm)))), sub_xmm)>;
  def : Pat<(v16i32 (X86vzmovl (v16i32 VR512:$src))),
            (SUBREG_TO_REG (i32 0),
             (v4i32 (VMOVSSZrr (v4i32 (AVX512_128_SET0)),
              (v4i32 (EXTRACT_SUBREG (v16i32 VR512:$src), sub_xmm)))), sub_xmm)>;
}

// Use 128-bit blends for OptForSpeed since BLENDs have better throughput than
// VMOVSS/SD. Unfortunately, loses the ability to use XMM16-31.
let Predicates = [HasAVX512, OptForSpeed] in {
  def : Pat<(v16f32 (X86vzmovl (v16f32 VR512:$src))),
            (SUBREG_TO_REG (i32 0),
             (v4f32 (VBLENDPSrri (v4f32 (V_SET0)),
                          (v4f32 (EXTRACT_SUBREG (v16f32 VR512:$src), sub_xmm)),
                          (i8 1))), sub_xmm)>;
  def : Pat<(v16i32 (X86vzmovl (v16i32 VR512:$src))),
            (SUBREG_TO_REG (i32 0),
             (v4i32 (VPBLENDWrri (v4i32 (V_SET0)),
                          (v4i32 (EXTRACT_SUBREG (v16i32 VR512:$src), sub_xmm)),
                          (i8 3))), sub_xmm)>;
}

let Predicates = [HasAVX512] in {
  def : Pat<(v4f32 (scalar_to_vector (loadf32 addr:$src))),
            (VMOVSSZrm addr:$src)>;
  def : Pat<(v2f64 (scalar_to_vector (loadf64 addr:$src))),
            (VMOVSDZrm addr:$src)>;

  // Represent the same patterns above but in the form they appear for
  // 256-bit types
  def : Pat<(v8f32 (X86vzload32 addr:$src)),
            (SUBREG_TO_REG (i32 0), (VMOVSSZrm addr:$src), sub_xmm)>;
  def : Pat<(v4f64 (X86vzload64 addr:$src)),
            (SUBREG_TO_REG (i32 0), (VMOVSDZrm addr:$src), sub_xmm)>;

  // Represent the same patterns above but in the form they appear for
  // 512-bit types
  def : Pat<(v16f32 (X86vzload32 addr:$src)),
            (SUBREG_TO_REG (i32 0), (VMOVSSZrm addr:$src), sub_xmm)>;
  def : Pat<(v8f64 (X86vzload64 addr:$src)),
            (SUBREG_TO_REG (i32 0), (VMOVSDZrm addr:$src), sub_xmm)>;
}
let Predicates = [HasFP16] in {
  def : Pat<(v8f16 (X86vzmovl (v8f16 VR128X:$src))),
            (VMOVSHZrr (v8f16 (AVX512_128_SET0)), VR128X:$src)>;
  def : Pat<(v8i16 (X86vzmovl (v8i16 VR128X:$src))),
            (VMOVSHZrr (v8i16 (AVX512_128_SET0)), VR128X:$src)>;

  // FIXME we need better canonicalization in dag combine
  def : Pat<(v16f16 (X86vzmovl (v16f16 VR256X:$src))),
            (SUBREG_TO_REG (i32 0),
             (v8f16 (VMOVSHZrr (v8f16 (AVX512_128_SET0)),
              (v8f16 (EXTRACT_SUBREG (v16f16 VR256X:$src), sub_xmm)))), sub_xmm)>;
  def : Pat<(v16i16 (X86vzmovl (v16i16 VR256X:$src))),
            (SUBREG_TO_REG (i32 0),
             (v8i16 (VMOVSHZrr (v8i16 (AVX512_128_SET0)),
              (v8i16 (EXTRACT_SUBREG (v16i16 VR256X:$src), sub_xmm)))), sub_xmm)>;

  // FIXME we need better canonicalization in dag combine
  def : Pat<(v32f16 (X86vzmovl (v32f16 VR512:$src))),
            (SUBREG_TO_REG (i32 0),
             (v8f16 (VMOVSHZrr (v8f16 (AVX512_128_SET0)),
              (v8f16 (EXTRACT_SUBREG (v32f16 VR512:$src), sub_xmm)))), sub_xmm)>;
  def : Pat<(v32i16 (X86vzmovl (v32i16 VR512:$src))),
            (SUBREG_TO_REG (i32 0),
             (v8i16 (VMOVSHZrr (v8i16 (AVX512_128_SET0)),
              (v8i16 (EXTRACT_SUBREG (v32i16 VR512:$src), sub_xmm)))), sub_xmm)>;

  def : Pat<(v8f16 (X86vzload16 addr:$src)),
            (VMOVSHZrm addr:$src)>;

  def : Pat<(v16f16 (X86vzload16 addr:$src)),
            (SUBREG_TO_REG (i32 0), (VMOVSHZrm addr:$src), sub_xmm)>;

  def : Pat<(v32f16 (X86vzload16 addr:$src)),
            (SUBREG_TO_REG (i32 0), (VMOVSHZrm addr:$src), sub_xmm)>;
}

let ExeDomain = SSEPackedInt, SchedRW = [SchedWriteVecLogic.XMM] in {
def VMOVZPQILo2PQIZrr : AVX512XSI<0x7E, MRMSrcReg, (outs VR128X:$dst),
                                (ins VR128X:$src),
                                "vmovq\t{$src, $dst|$dst, $src}",
                                [(set VR128X:$dst, (v2i64 (X86vzmovl
                                                   (v2i64 VR128X:$src))))]>,
                                EVEX, REX_W;
}

let Predicates = [HasAVX512] in {
  def : Pat<(v4i32 (scalar_to_vector (i32 (anyext GR8:$src)))),
            (VMOVDI2PDIZrr (i32 (INSERT_SUBREG (i32 (IMPLICIT_DEF)),
                                              GR8:$src, sub_8bit)))>;
  def : Pat<(v4i32 (X86vzmovl (v4i32 (scalar_to_vector GR32:$src)))),
            (VMOVDI2PDIZrr GR32:$src)>;

  def : Pat<(v2i64 (X86vzmovl (v2i64 (scalar_to_vector GR64:$src)))),
            (VMOV64toPQIZrr GR64:$src)>;

  // AVX 128-bit movd/movq instruction write zeros in the high 128-bit part.
  def : Pat<(v4i32 (X86vzload32 addr:$src)),
            (VMOVDI2PDIZrm addr:$src)>;
  def : Pat<(v8i32 (X86vzload32 addr:$src)),
            (SUBREG_TO_REG (i32 0), (v4i32 (VMOVDI2PDIZrm addr:$src)), sub_xmm)>;
  def : Pat<(v2f64 (X86vzmovl (v2f64 VR128X:$src))),
            (VMOVZPQILo2PQIZrr VR128X:$src)>;
  def : Pat<(v2i64 (X86vzload64 addr:$src)),
            (VMOVQI2PQIZrm addr:$src)>;
  def : Pat<(v4i64 (X86vzload64 addr:$src)),
            (SUBREG_TO_REG (i64 0), (v2i64 (VMOVQI2PQIZrm addr:$src)), sub_xmm)>;

  // Use regular 128-bit instructions to match 512-bit scalar_to_vec+zext.
  def : Pat<(v16i32 (X86vzload32 addr:$src)),
            (SUBREG_TO_REG (i32 0), (v4i32 (VMOVDI2PDIZrm addr:$src)), sub_xmm)>;
  def : Pat<(v8i64 (X86vzload64 addr:$src)),
            (SUBREG_TO_REG (i64 0), (v2i64 (VMOVQI2PQIZrm addr:$src)), sub_xmm)>;

  def : Pat<(v4f64 (X86vzmovl (v4f64 VR256X:$src))),
            (SUBREG_TO_REG (i32 0),
             (v2f64 (VMOVZPQILo2PQIZrr
                     (v2f64 (EXTRACT_SUBREG (v4f64 VR256X:$src), sub_xmm)))),
             sub_xmm)>;
  def : Pat<(v4i64 (X86vzmovl (v4i64 VR256X:$src))),
            (SUBREG_TO_REG (i32 0),
             (v2i64 (VMOVZPQILo2PQIZrr
                     (v2i64 (EXTRACT_SUBREG (v4i64 VR256X:$src), sub_xmm)))),
             sub_xmm)>;

  def : Pat<(v8f64 (X86vzmovl (v8f64 VR512:$src))),
            (SUBREG_TO_REG (i32 0),
             (v2f64 (VMOVZPQILo2PQIZrr
                     (v2f64 (EXTRACT_SUBREG (v8f64 VR512:$src), sub_xmm)))),
             sub_xmm)>;
  def : Pat<(v8i64 (X86vzmovl (v8i64 VR512:$src))),
            (SUBREG_TO_REG (i32 0),
             (v2i64 (VMOVZPQILo2PQIZrr
                     (v2i64 (EXTRACT_SUBREG (v8i64 VR512:$src), sub_xmm)))),
             sub_xmm)>;
}

//===----------------------------------------------------------------------===//
// AVX-512 - Non-temporals
//===----------------------------------------------------------------------===//

def VMOVNTDQAZrm : AVX512PI<0x2A, MRMSrcMem, (outs VR512:$dst),
                      (ins i512mem:$src), "vmovntdqa\t{$src, $dst|$dst, $src}",
                      [], SSEPackedInt>, Sched<[SchedWriteVecMoveLS.ZMM.RM]>,
                      EVEX, T8, PD, EVEX_V512, EVEX_CD8<64, CD8VF>;

let Predicates = [HasVLX] in {
  def VMOVNTDQAZ256rm : AVX512PI<0x2A, MRMSrcMem, (outs VR256X:$dst),
                       (ins i256mem:$src),
                       "vmovntdqa\t{$src, $dst|$dst, $src}",
                       [], SSEPackedInt>, Sched<[SchedWriteVecMoveLS.YMM.RM]>,
                       EVEX, T8, PD, EVEX_V256, EVEX_CD8<64, CD8VF>;

  def VMOVNTDQAZ128rm : AVX512PI<0x2A, MRMSrcMem, (outs VR128X:$dst),
                      (ins i128mem:$src),
                      "vmovntdqa\t{$src, $dst|$dst, $src}",
                      [], SSEPackedInt>, Sched<[SchedWriteVecMoveLS.XMM.RM]>,
                      EVEX, T8, PD, EVEX_V128, EVEX_CD8<64, CD8VF>;
}

multiclass avx512_movnt<bits<8> opc, string OpcodeStr, X86VectorVTInfo _,
                        X86SchedWriteMoveLS Sched,
                        PatFrag st_frag = alignednontemporalstore> {
  let SchedRW = [Sched.MR], AddedComplexity = 400 in
  def mr : AVX512PI<opc, MRMDestMem, (outs), (ins _.MemOp:$dst, _.RC:$src),
                    !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                    [(st_frag (_.VT _.RC:$src), addr:$dst)],
                    _.ExeDomain>, EVEX, EVEX_CD8<_.EltSize, CD8VF>;
}

multiclass avx512_movnt_vl<bits<8> opc, string OpcodeStr,
                           AVX512VLVectorVTInfo VTInfo,
                           X86SchedWriteMoveLSWidths Sched> {
  let Predicates = [HasAVX512] in
    defm Z : avx512_movnt<opc, OpcodeStr, VTInfo.info512, Sched.ZMM>, EVEX_V512;

  let Predicates = [HasAVX512, HasVLX] in {
    defm Z256 : avx512_movnt<opc, OpcodeStr, VTInfo.info256, Sched.YMM>, EVEX_V256;
    defm Z128 : avx512_movnt<opc, OpcodeStr, VTInfo.info128, Sched.XMM>, EVEX_V128;
  }
}

defm VMOVNTDQ : avx512_movnt_vl<0xE7, "vmovntdq", avx512vl_i64_info,
                                SchedWriteVecMoveLSNT>, TB, PD;
defm VMOVNTPD : avx512_movnt_vl<0x2B, "vmovntpd", avx512vl_f64_info,
                                SchedWriteFMoveLSNT>, TB, PD, REX_W;
defm VMOVNTPS : avx512_movnt_vl<0x2B, "vmovntps", avx512vl_f32_info,
                                SchedWriteFMoveLSNT>, TB;

let Predicates = [HasAVX512], AddedComplexity = 400 in {
  def : Pat<(alignednontemporalstore (v16i32 VR512:$src), addr:$dst),
            (VMOVNTDQZmr addr:$dst, VR512:$src)>;
  def : Pat<(alignednontemporalstore (v32i16 VR512:$src), addr:$dst),
            (VMOVNTDQZmr addr:$dst, VR512:$src)>;
  def : Pat<(alignednontemporalstore (v64i8 VR512:$src), addr:$dst),
            (VMOVNTDQZmr addr:$dst, VR512:$src)>;

  def : Pat<(v8f64 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZrm addr:$src)>;
  def : Pat<(v16f32 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZrm addr:$src)>;
  def : Pat<(v8i64 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZrm addr:$src)>;
  def : Pat<(v16i32 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZrm addr:$src)>;
  def : Pat<(v32i16 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZrm addr:$src)>;
  def : Pat<(v64i8 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZrm addr:$src)>;
}

let Predicates = [HasVLX], AddedComplexity = 400 in {
  def : Pat<(alignednontemporalstore (v8i32 VR256X:$src), addr:$dst),
            (VMOVNTDQZ256mr addr:$dst, VR256X:$src)>;
  def : Pat<(alignednontemporalstore (v16i16 VR256X:$src), addr:$dst),
            (VMOVNTDQZ256mr addr:$dst, VR256X:$src)>;
  def : Pat<(alignednontemporalstore (v32i8 VR256X:$src), addr:$dst),
            (VMOVNTDQZ256mr addr:$dst, VR256X:$src)>;

  def : Pat<(v4f64 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZ256rm addr:$src)>;
  def : Pat<(v8f32 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZ256rm addr:$src)>;
  def : Pat<(v4i64 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZ256rm addr:$src)>;
  def : Pat<(v8i32 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZ256rm addr:$src)>;
  def : Pat<(v16i16 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZ256rm addr:$src)>;
  def : Pat<(v32i8 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZ256rm addr:$src)>;

  def : Pat<(alignednontemporalstore (v4i32 VR128X:$src), addr:$dst),
            (VMOVNTDQZ128mr addr:$dst, VR128X:$src)>;
  def : Pat<(alignednontemporalstore (v8i16 VR128X:$src), addr:$dst),
            (VMOVNTDQZ128mr addr:$dst, VR128X:$src)>;
  def : Pat<(alignednontemporalstore (v16i8 VR128X:$src), addr:$dst),
            (VMOVNTDQZ128mr addr:$dst, VR128X:$src)>;

  def : Pat<(v2f64 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZ128rm addr:$src)>;
  def : Pat<(v4f32 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZ128rm addr:$src)>;
  def : Pat<(v2i64 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZ128rm addr:$src)>;
  def : Pat<(v4i32 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZ128rm addr:$src)>;
  def : Pat<(v8i16 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZ128rm addr:$src)>;
  def : Pat<(v16i8 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZ128rm addr:$src)>;
}

//===----------------------------------------------------------------------===//
// AVX-512 - Integer arithmetic
//
multiclass avx512_binop_rm<bits<8> opc, string OpcodeStr, SDNode OpNode,
                           X86VectorVTInfo _, X86FoldableSchedWrite sched,
                           bit IsCommutable = 0> {
  defm rr : AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                    (ins _.RC:$src1, _.RC:$src2), OpcodeStr,
                    "$src2, $src1", "$src1, $src2",
                    (_.VT (OpNode _.RC:$src1, _.RC:$src2)),
                    IsCommutable, IsCommutable>, AVX512BIBase, EVEX, VVVV,
                    Sched<[sched]>;

  defm rm : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                  (ins _.RC:$src1, _.MemOp:$src2), OpcodeStr,
                  "$src2, $src1", "$src1, $src2",
                  (_.VT (OpNode _.RC:$src1, (_.LdFrag addr:$src2)))>,
                  AVX512BIBase, EVEX, VVVV,
                  Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass avx512_binop_rmb<bits<8> opc, string OpcodeStr, SDNode OpNode,
                            X86VectorVTInfo _, X86FoldableSchedWrite sched,
                            bit IsCommutable = 0> :
           avx512_binop_rm<opc, OpcodeStr, OpNode, _, sched, IsCommutable> {
  defm rmb : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                  (ins _.RC:$src1, _.ScalarMemOp:$src2), OpcodeStr,
                  "${src2}"#_.BroadcastStr#", $src1",
                  "$src1, ${src2}"#_.BroadcastStr,
                  (_.VT (OpNode _.RC:$src1,
                                (_.BroadcastLdFrag addr:$src2)))>,
                  AVX512BIBase, EVEX, VVVV, EVEX_B,
                  Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass avx512_binop_rm_vl<bits<8> opc, string OpcodeStr, SDNode OpNode,
                              AVX512VLVectorVTInfo VTInfo,
                              X86SchedWriteWidths sched, Predicate prd,
                              bit IsCommutable = 0> {
  let Predicates = [prd] in
    defm Z : avx512_binop_rm<opc, OpcodeStr, OpNode, VTInfo.info512, sched.ZMM,
                             IsCommutable>, EVEX_V512;

  let Predicates = [prd, HasVLX] in {
    defm Z256 : avx512_binop_rm<opc, OpcodeStr, OpNode, VTInfo.info256,
                                sched.YMM, IsCommutable>, EVEX_V256;
    defm Z128 : avx512_binop_rm<opc, OpcodeStr, OpNode, VTInfo.info128,
                                sched.XMM, IsCommutable>, EVEX_V128;
  }
}

multiclass avx512_binop_rmb_vl<bits<8> opc, string OpcodeStr, SDNode OpNode,
                               AVX512VLVectorVTInfo VTInfo,
                               X86SchedWriteWidths sched, Predicate prd,
                               bit IsCommutable = 0> {
  let Predicates = [prd] in
    defm Z : avx512_binop_rmb<opc, OpcodeStr, OpNode, VTInfo.info512, sched.ZMM,
                             IsCommutable>, EVEX_V512;

  let Predicates = [prd, HasVLX] in {
    defm Z256 : avx512_binop_rmb<opc, OpcodeStr, OpNode, VTInfo.info256,
                                 sched.YMM, IsCommutable>, EVEX_V256;
    defm Z128 : avx512_binop_rmb<opc, OpcodeStr, OpNode, VTInfo.info128,
                                 sched.XMM, IsCommutable>, EVEX_V128;
  }
}

multiclass avx512_binop_rm_vl_q<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                X86SchedWriteWidths sched, Predicate prd,
                                bit IsCommutable = 0> {
  defm NAME : avx512_binop_rmb_vl<opc, OpcodeStr, OpNode, avx512vl_i64_info,
                                  sched, prd, IsCommutable>,
                                  REX_W, EVEX_CD8<64, CD8VF>;
}

multiclass avx512_binop_rm_vl_d<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                X86SchedWriteWidths sched, Predicate prd,
                                bit IsCommutable = 0> {
  defm NAME : avx512_binop_rmb_vl<opc, OpcodeStr, OpNode, avx512vl_i32_info,
                                  sched, prd, IsCommutable>, EVEX_CD8<32, CD8VF>;
}

multiclass avx512_binop_rm_vl_w<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                X86SchedWriteWidths sched, Predicate prd,
                                bit IsCommutable = 0> {
  defm NAME : avx512_binop_rm_vl<opc, OpcodeStr, OpNode, avx512vl_i16_info,
                                 sched, prd, IsCommutable>, EVEX_CD8<16, CD8VF>,
                                 WIG;
}

multiclass avx512_binop_rm_vl_b<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                X86SchedWriteWidths sched, Predicate prd,
                                bit IsCommutable = 0> {
  defm NAME : avx512_binop_rm_vl<opc, OpcodeStr, OpNode, avx512vl_i8_info,
                                 sched, prd, IsCommutable>, EVEX_CD8<8, CD8VF>,
                                 WIG;
}

multiclass avx512_binop_rm_vl_dq<bits<8> opc_d, bits<8> opc_q, string OpcodeStr,
                                 SDNode OpNode, X86SchedWriteWidths sched,
                                 Predicate prd, bit IsCommutable = 0> {
  defm Q : avx512_binop_rm_vl_q<opc_q, OpcodeStr#"q", OpNode, sched, prd,
                                   IsCommutable>;

  defm D : avx512_binop_rm_vl_d<opc_d, OpcodeStr#"d", OpNode, sched, prd,
                                   IsCommutable>;
}

multiclass avx512_binop_rm_vl_bw<bits<8> opc_b, bits<8> opc_w, string OpcodeStr,
                                 SDNode OpNode, X86SchedWriteWidths sched,
                                 Predicate prd, bit IsCommutable = 0> {
  defm W : avx512_binop_rm_vl_w<opc_w, OpcodeStr#"w", OpNode, sched, prd,
                                   IsCommutable>;

  defm B : avx512_binop_rm_vl_b<opc_b, OpcodeStr#"b", OpNode, sched, prd,
                                   IsCommutable>;
}

multiclass avx512_binop_rm_vl_all<bits<8> opc_b, bits<8> opc_w,
                                  bits<8> opc_d, bits<8> opc_q,
                                  string OpcodeStr, SDNode OpNode,
                                  X86SchedWriteWidths sched,
                                  bit IsCommutable = 0> {
  defm NAME : avx512_binop_rm_vl_dq<opc_d, opc_q, OpcodeStr, OpNode,
                                    sched, HasAVX512, IsCommutable>,
              avx512_binop_rm_vl_bw<opc_b, opc_w, OpcodeStr, OpNode,
                                    sched, HasBWI, IsCommutable>;
}

multiclass avx512_binop_rm2<bits<8> opc, string OpcodeStr,
                            X86FoldableSchedWrite sched,
                            SDNode OpNode,X86VectorVTInfo _Src,
                            X86VectorVTInfo _Dst, X86VectorVTInfo _Brdct,
                            bit IsCommutable = 0> {
  defm rr : AVX512_maskable<opc, MRMSrcReg, _Dst, (outs _Dst.RC:$dst),
                            (ins _Src.RC:$src1, _Src.RC:$src2), OpcodeStr,
                            "$src2, $src1","$src1, $src2",
                            (_Dst.VT (OpNode
                                         (_Src.VT _Src.RC:$src1),
                                         (_Src.VT _Src.RC:$src2))),
                            IsCommutable>,
                            AVX512BIBase, EVEX, VVVV, Sched<[sched]>;
  defm rm : AVX512_maskable<opc, MRMSrcMem, _Dst, (outs _Dst.RC:$dst),
                        (ins _Src.RC:$src1, _Src.MemOp:$src2), OpcodeStr,
                        "$src2, $src1", "$src1, $src2",
                        (_Dst.VT (OpNode (_Src.VT _Src.RC:$src1),
                                      (_Src.LdFrag addr:$src2)))>,
                        AVX512BIBase, EVEX, VVVV,
                        Sched<[sched.Folded, sched.ReadAfterFold]>;

  defm rmb : AVX512_maskable<opc, MRMSrcMem, _Dst, (outs _Dst.RC:$dst),
                    (ins _Src.RC:$src1, _Brdct.ScalarMemOp:$src2),
                    OpcodeStr,
                    "${src2}"#_Brdct.BroadcastStr#", $src1",
                     "$src1, ${src2}"#_Brdct.BroadcastStr,
                    (_Dst.VT (OpNode (_Src.VT _Src.RC:$src1), (bitconvert
                                 (_Brdct.VT (_Brdct.BroadcastLdFrag addr:$src2)))))>,
                    AVX512BIBase, EVEX, VVVV, EVEX_B,
                    Sched<[sched.Folded, sched.ReadAfterFold]>;
}

defm VPADD : avx512_binop_rm_vl_all<0xFC, 0xFD, 0xFE, 0xD4, "vpadd", add,
                                    SchedWriteVecALU, 1>;
defm VPSUB : avx512_binop_rm_vl_all<0xF8, 0xF9, 0xFA, 0xFB, "vpsub", sub,
                                    SchedWriteVecALU, 0>;
defm VPADDS : avx512_binop_rm_vl_bw<0xEC, 0xED, "vpadds", saddsat,
                                    SchedWriteVecALU, HasBWI, 1>;
defm VPSUBS : avx512_binop_rm_vl_bw<0xE8, 0xE9, "vpsubs", ssubsat,
                                    SchedWriteVecALU, HasBWI, 0>;
defm VPADDUS : avx512_binop_rm_vl_bw<0xDC, 0xDD, "vpaddus", uaddsat,
                                     SchedWriteVecALU, HasBWI, 1>;
defm VPSUBUS : avx512_binop_rm_vl_bw<0xD8, 0xD9, "vpsubus", usubsat,
                                     SchedWriteVecALU, HasBWI, 0>;
defm VPMULLD : avx512_binop_rm_vl_d<0x40, "vpmulld", mul,
                                    SchedWritePMULLD, HasAVX512, 1>, T8;
defm VPMULLW : avx512_binop_rm_vl_w<0xD5, "vpmullw", mul,
                                    SchedWriteVecIMul, HasBWI, 1>;
defm VPMULLQ : avx512_binop_rm_vl_q<0x40, "vpmullq", mul,
                                    SchedWriteVecIMul, HasDQI, 1>, T8;
defm VPMULHW : avx512_binop_rm_vl_w<0xE5, "vpmulhw", mulhs, SchedWriteVecIMul,
                                    HasBWI, 1>;
defm VPMULHUW : avx512_binop_rm_vl_w<0xE4, "vpmulhuw", mulhu, SchedWriteVecIMul,
                                     HasBWI, 1>;
defm VPMULHRSW : avx512_binop_rm_vl_w<0x0B, "vpmulhrsw", X86mulhrs,
                                      SchedWriteVecIMul, HasBWI, 1>, T8;
defm VPAVG : avx512_binop_rm_vl_bw<0xE0, 0xE3, "vpavg", avgceilu,
                                   SchedWriteVecALU, HasBWI, 1>;
defm VPMULDQ : avx512_binop_rm_vl_q<0x28, "vpmuldq", X86pmuldq,
                                    SchedWriteVecIMul, HasAVX512, 1>, T8;
defm VPMULUDQ : avx512_binop_rm_vl_q<0xF4, "vpmuludq", X86pmuludq,
                                     SchedWriteVecIMul, HasAVX512, 1>;

multiclass avx512_binop_all<bits<8> opc, string OpcodeStr,
                            X86SchedWriteWidths sched,
                            AVX512VLVectorVTInfo _SrcVTInfo,
                            AVX512VLVectorVTInfo _DstVTInfo,
                            SDNode OpNode, Predicate prd,  bit IsCommutable = 0> {
  let Predicates = [prd] in
    defm NAME#Z : avx512_binop_rm2<opc, OpcodeStr, sched.ZMM, OpNode,
                                 _SrcVTInfo.info512, _DstVTInfo.info512,
                                 v8i64_info, IsCommutable>,
                                  EVEX_V512, EVEX_CD8<64, CD8VF>, REX_W;
  let Predicates = [HasVLX, prd] in {
    defm NAME#Z256 : avx512_binop_rm2<opc, OpcodeStr, sched.YMM, OpNode,
                                      _SrcVTInfo.info256, _DstVTInfo.info256,
                                      v4i64x_info, IsCommutable>,
                                      EVEX_V256, EVEX_CD8<64, CD8VF>, REX_W;
    defm NAME#Z128 : avx512_binop_rm2<opc, OpcodeStr, sched.XMM, OpNode,
                                      _SrcVTInfo.info128, _DstVTInfo.info128,
                                      v2i64x_info, IsCommutable>,
                                     EVEX_V128, EVEX_CD8<64, CD8VF>, REX_W;
  }
}

defm VPMULTISHIFTQB : avx512_binop_all<0x83, "vpmultishiftqb", SchedWriteVecALU,
                                avx512vl_i8_info, avx512vl_i8_info,
                                X86multishift, HasVBMI, 0>, T8;

multiclass avx512_packs_rmb<bits<8> opc, string OpcodeStr, SDNode OpNode,
                            X86VectorVTInfo _Src, X86VectorVTInfo _Dst,
                            X86FoldableSchedWrite sched> {
  defm rmb : AVX512_maskable<opc, MRMSrcMem, _Dst, (outs _Dst.RC:$dst),
                    (ins _Src.RC:$src1, _Src.ScalarMemOp:$src2),
                    OpcodeStr,
                    "${src2}"#_Src.BroadcastStr#", $src1",
                     "$src1, ${src2}"#_Src.BroadcastStr,
                    (_Dst.VT (OpNode (_Src.VT _Src.RC:$src1), (bitconvert
                                 (_Src.VT (_Src.BroadcastLdFrag addr:$src2)))))>,
                    EVEX, VVVV, EVEX_B, EVEX_CD8<_Src.EltSize, CD8VF>,
                    Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass avx512_packs_rm<bits<8> opc, string OpcodeStr,
                            SDNode OpNode,X86VectorVTInfo _Src,
                            X86VectorVTInfo _Dst, X86FoldableSchedWrite sched,
                            bit IsCommutable = 0> {
  defm rr : AVX512_maskable<opc, MRMSrcReg, _Dst, (outs _Dst.RC:$dst),
                            (ins _Src.RC:$src1, _Src.RC:$src2), OpcodeStr,
                            "$src2, $src1","$src1, $src2",
                            (_Dst.VT (OpNode
                                         (_Src.VT _Src.RC:$src1),
                                         (_Src.VT _Src.RC:$src2))),
                            IsCommutable, IsCommutable>,
                            EVEX_CD8<_Src.EltSize, CD8VF>, EVEX, VVVV, Sched<[sched]>;
  defm rm : AVX512_maskable<opc, MRMSrcMem, _Dst, (outs _Dst.RC:$dst),
                        (ins _Src.RC:$src1, _Src.MemOp:$src2), OpcodeStr,
                        "$src2, $src1", "$src1, $src2",
                        (_Dst.VT (OpNode (_Src.VT _Src.RC:$src1),
                                      (_Src.LdFrag addr:$src2)))>,
                         EVEX, VVVV, EVEX_CD8<_Src.EltSize, CD8VF>,
                         Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass avx512_packs_all_i32_i16<bits<8> opc, string OpcodeStr,
                                    SDNode OpNode> {
  let Predicates = [HasBWI] in
  defm NAME#Z : avx512_packs_rm<opc, OpcodeStr, OpNode, v16i32_info,
                                 v32i16_info, SchedWriteShuffle.ZMM>,
                avx512_packs_rmb<opc, OpcodeStr, OpNode, v16i32_info,
                                 v32i16_info, SchedWriteShuffle.ZMM>, EVEX_V512;
  let Predicates = [HasBWI, HasVLX] in {
    defm NAME#Z256 : avx512_packs_rm<opc, OpcodeStr, OpNode, v8i32x_info,
                                     v16i16x_info, SchedWriteShuffle.YMM>,
                     avx512_packs_rmb<opc, OpcodeStr, OpNode, v8i32x_info,
                                      v16i16x_info, SchedWriteShuffle.YMM>,
                                      EVEX_V256;
    defm NAME#Z128 : avx512_packs_rm<opc, OpcodeStr, OpNode, v4i32x_info,
                                     v8i16x_info, SchedWriteShuffle.XMM>,
                     avx512_packs_rmb<opc, OpcodeStr, OpNode, v4i32x_info,
                                      v8i16x_info, SchedWriteShuffle.XMM>,
                                      EVEX_V128;
  }
}
multiclass avx512_packs_all_i16_i8<bits<8> opc, string OpcodeStr,
                            SDNode OpNode> {
  let Predicates = [HasBWI] in
  defm NAME#Z : avx512_packs_rm<opc, OpcodeStr, OpNode, v32i16_info, v64i8_info,
                                SchedWriteShuffle.ZMM>, EVEX_V512, WIG;
  let Predicates = [HasBWI, HasVLX] in {
    defm NAME#Z256 : avx512_packs_rm<opc, OpcodeStr, OpNode, v16i16x_info,
                                     v32i8x_info, SchedWriteShuffle.YMM>,
                                     EVEX_V256, WIG;
    defm NAME#Z128 : avx512_packs_rm<opc, OpcodeStr, OpNode, v8i16x_info,
                                     v16i8x_info, SchedWriteShuffle.XMM>,
                                     EVEX_V128, WIG;
  }
}

multiclass avx512_vpmadd<bits<8> opc, string OpcodeStr,
                            SDNode OpNode, AVX512VLVectorVTInfo _Src,
                            AVX512VLVectorVTInfo _Dst, bit IsCommutable = 0> {
  let Predicates = [HasBWI] in
  defm NAME#Z : avx512_packs_rm<opc, OpcodeStr, OpNode, _Src.info512,
                                _Dst.info512, SchedWriteVecIMul.ZMM,
                                IsCommutable>, EVEX_V512;
  let Predicates = [HasBWI, HasVLX] in {
    defm NAME#Z256 : avx512_packs_rm<opc, OpcodeStr, OpNode, _Src.info256,
                                     _Dst.info256, SchedWriteVecIMul.YMM,
                                     IsCommutable>, EVEX_V256;
    defm NAME#Z128 : avx512_packs_rm<opc, OpcodeStr, OpNode, _Src.info128,
                                     _Dst.info128, SchedWriteVecIMul.XMM,
                                     IsCommutable>, EVEX_V128;
  }
}

defm VPACKSSDW : avx512_packs_all_i32_i16<0x6B, "vpackssdw", X86Packss>, AVX512BIBase;
defm VPACKUSDW : avx512_packs_all_i32_i16<0x2b, "vpackusdw", X86Packus>, AVX5128IBase;
defm VPACKSSWB : avx512_packs_all_i16_i8 <0x63, "vpacksswb", X86Packss>, AVX512BIBase;
defm VPACKUSWB : avx512_packs_all_i16_i8 <0x67, "vpackuswb", X86Packus>, AVX512BIBase;

defm VPMADDUBSW : avx512_vpmadd<0x04, "vpmaddubsw", X86vpmaddubsw,
                     avx512vl_i8_info, avx512vl_i16_info>, AVX512BIBase, T8, WIG;
defm VPMADDWD   : avx512_vpmadd<0xF5, "vpmaddwd", X86vpmaddwd,
                     avx512vl_i16_info, avx512vl_i32_info, 1>, AVX512BIBase, WIG;

defm VPMAXSB : avx512_binop_rm_vl_b<0x3C, "vpmaxsb", smax,
                                    SchedWriteVecALU, HasBWI, 1>, T8;
defm VPMAXSW : avx512_binop_rm_vl_w<0xEE, "vpmaxsw", smax,
                                    SchedWriteVecALU, HasBWI, 1>;
defm VPMAXSD : avx512_binop_rm_vl_d<0x3D, "vpmaxsd", smax,
                                    SchedWriteVecALU, HasAVX512, 1>, T8;
defm VPMAXSQ : avx512_binop_rm_vl_q<0x3D, "vpmaxsq", smax,
                                    SchedWriteVecALU, HasAVX512, 1>, T8;

defm VPMAXUB : avx512_binop_rm_vl_b<0xDE, "vpmaxub", umax,
                                    SchedWriteVecALU, HasBWI, 1>;
defm VPMAXUW : avx512_binop_rm_vl_w<0x3E, "vpmaxuw", umax,
                                    SchedWriteVecALU, HasBWI, 1>, T8;
defm VPMAXUD : avx512_binop_rm_vl_d<0x3F, "vpmaxud", umax,
                                    SchedWriteVecALU, HasAVX512, 1>, T8;
defm VPMAXUQ : avx512_binop_rm_vl_q<0x3F, "vpmaxuq", umax,
                                    SchedWriteVecALU, HasAVX512, 1>, T8;

defm VPMINSB : avx512_binop_rm_vl_b<0x38, "vpminsb", smin,
                                    SchedWriteVecALU, HasBWI, 1>, T8;
defm VPMINSW : avx512_binop_rm_vl_w<0xEA, "vpminsw", smin,
                                    SchedWriteVecALU, HasBWI, 1>;
defm VPMINSD : avx512_binop_rm_vl_d<0x39, "vpminsd", smin,
                                    SchedWriteVecALU, HasAVX512, 1>, T8;
defm VPMINSQ : avx512_binop_rm_vl_q<0x39, "vpminsq", smin,
                                    SchedWriteVecALU, HasAVX512, 1>, T8;

defm VPMINUB : avx512_binop_rm_vl_b<0xDA, "vpminub", umin,
                                    SchedWriteVecALU, HasBWI, 1>;
defm VPMINUW : avx512_binop_rm_vl_w<0x3A, "vpminuw", umin,
                                    SchedWriteVecALU, HasBWI, 1>, T8;
defm VPMINUD : avx512_binop_rm_vl_d<0x3B, "vpminud", umin,
                                    SchedWriteVecALU, HasAVX512, 1>, T8;
defm VPMINUQ : avx512_binop_rm_vl_q<0x3B, "vpminuq", umin,
                                    SchedWriteVecALU, HasAVX512, 1>, T8;

// PMULLQ: Use 512bit version to implement 128/256 bit in case NoVLX, HasEVEX512.
let Predicates = [HasDQI, NoVLX, HasEVEX512] in {
  def : Pat<(v4i64 (mul (v4i64 VR256X:$src1), (v4i64 VR256X:$src2))),
            (EXTRACT_SUBREG
                (VPMULLQZrr
                    (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src1, sub_ymm),
                    (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src2, sub_ymm)),
             sub_ymm)>;
  def : Pat<(v4i64 (mul (v4i64 VR256X:$src1), (v4i64 (X86VBroadcastld64 addr:$src2)))),
            (EXTRACT_SUBREG
                (VPMULLQZrmb
                    (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src1, sub_ymm),
                    addr:$src2),
             sub_ymm)>;

  def : Pat<(v2i64 (mul (v2i64 VR128X:$src1), (v2i64 VR128X:$src2))),
            (EXTRACT_SUBREG
                (VPMULLQZrr
                    (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src1, sub_xmm),
                    (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src2, sub_xmm)),
             sub_xmm)>;
  def : Pat<(v2i64 (mul (v2i64 VR128X:$src1), (v2i64 (X86VBroadcastld64 addr:$src2)))),
            (EXTRACT_SUBREG
                (VPMULLQZrmb
                    (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src1, sub_xmm),
                    addr:$src2),
             sub_xmm)>;
}

multiclass avx512_min_max_lowering<string Instr, SDNode OpNode> {
  def : Pat<(v4i64 (OpNode VR256X:$src1, VR256X:$src2)),
            (EXTRACT_SUBREG
                (!cast<Instruction>(Instr#"rr")
                    (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src1, sub_ymm),
                    (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src2, sub_ymm)),
             sub_ymm)>;
  def : Pat<(v4i64 (OpNode (v4i64 VR256X:$src1), (v4i64 (X86VBroadcastld64 addr:$src2)))),
            (EXTRACT_SUBREG
                (!cast<Instruction>(Instr#"rmb")
                    (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src1, sub_ymm),
                    addr:$src2),
             sub_ymm)>;

  def : Pat<(v2i64 (OpNode VR128X:$src1, VR128X:$src2)),
            (EXTRACT_SUBREG
                (!cast<Instruction>(Instr#"rr")
                    (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src1, sub_xmm),
                    (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src2, sub_xmm)),
             sub_xmm)>;
  def : Pat<(v2i64 (OpNode (v2i64 VR128X:$src1), (v2i64 (X86VBroadcastld64 addr:$src2)))),
            (EXTRACT_SUBREG
                (!cast<Instruction>(Instr#"rmb")
                    (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src1, sub_xmm),
                    addr:$src2),
             sub_xmm)>;
}

let Predicates = [HasAVX512, NoVLX, HasEVEX512] in {
  defm : avx512_min_max_lowering<"VPMAXUQZ", umax>;
  defm : avx512_min_max_lowering<"VPMINUQZ", umin>;
  defm : avx512_min_max_lowering<"VPMAXSQZ", smax>;
  defm : avx512_min_max_lowering<"VPMINSQZ", smin>;
}

//===----------------------------------------------------------------------===//
// AVX-512  Logical Instructions
//===----------------------------------------------------------------------===//

defm VPAND : avx512_binop_rm_vl_dq<0xDB, 0xDB, "vpand", and,
                                   SchedWriteVecLogic, HasAVX512, 1>;
defm VPOR : avx512_binop_rm_vl_dq<0xEB, 0xEB, "vpor", or,
                                  SchedWriteVecLogic, HasAVX512, 1>;
defm VPXOR : avx512_binop_rm_vl_dq<0xEF, 0xEF, "vpxor", xor,
                                   SchedWriteVecLogic, HasAVX512, 1>;
defm VPANDN : avx512_binop_rm_vl_dq<0xDF, 0xDF, "vpandn", X86andnp,
                                    SchedWriteVecLogic, HasAVX512>;

let Predicates = [HasVLX] in {
  def : Pat<(v16i8 (and VR128X:$src1, VR128X:$src2)),
            (VPANDQZ128rr VR128X:$src1, VR128X:$src2)>;
  def : Pat<(v8i16 (and VR128X:$src1, VR128X:$src2)),
            (VPANDQZ128rr VR128X:$src1, VR128X:$src2)>;

  def : Pat<(v16i8 (or VR128X:$src1, VR128X:$src2)),
            (VPORQZ128rr VR128X:$src1, VR128X:$src2)>;
  def : Pat<(v8i16 (or VR128X:$src1, VR128X:$src2)),
            (VPORQZ128rr VR128X:$src1, VR128X:$src2)>;

  def : Pat<(v16i8 (xor VR128X:$src1, VR128X:$src2)),
            (VPXORQZ128rr VR128X:$src1, VR128X:$src2)>;
  def : Pat<(v8i16 (xor VR128X:$src1, VR128X:$src2)),
            (VPXORQZ128rr VR128X:$src1, VR128X:$src2)>;

  def : Pat<(v16i8 (X86andnp VR128X:$src1, VR128X:$src2)),
            (VPANDNQZ128rr VR128X:$src1, VR128X:$src2)>;
  def : Pat<(v8i16 (X86andnp VR128X:$src1, VR128X:$src2)),
            (VPANDNQZ128rr VR128X:$src1, VR128X:$src2)>;

  def : Pat<(and VR128X:$src1, (loadv16i8 addr:$src2)),
            (VPANDQZ128rm VR128X:$src1, addr:$src2)>;
  def : Pat<(and VR128X:$src1, (loadv8i16 addr:$src2)),
            (VPANDQZ128rm VR128X:$src1, addr:$src2)>;

  def : Pat<(or VR128X:$src1, (loadv16i8 addr:$src2)),
            (VPORQZ128rm VR128X:$src1, addr:$src2)>;
  def : Pat<(or VR128X:$src1, (loadv8i16 addr:$src2)),
            (VPORQZ128rm VR128X:$src1, addr:$src2)>;

  def : Pat<(xor VR128X:$src1, (loadv16i8 addr:$src2)),
            (VPXORQZ128rm VR128X:$src1, addr:$src2)>;
  def : Pat<(xor VR128X:$src1, (loadv8i16 addr:$src2)),
            (VPXORQZ128rm VR128X:$src1, addr:$src2)>;

  def : Pat<(X86andnp VR128X:$src1, (loadv16i8 addr:$src2)),
            (VPANDNQZ128rm VR128X:$src1, addr:$src2)>;
  def : Pat<(X86andnp VR128X:$src1, (loadv8i16 addr:$src2)),
            (VPANDNQZ128rm VR128X:$src1, addr:$src2)>;

  def : Pat<(v32i8 (and VR256X:$src1, VR256X:$src2)),
            (VPANDQZ256rr VR256X:$src1, VR256X:$src2)>;
  def : Pat<(v16i16 (and VR256X:$src1, VR256X:$src2)),
            (VPANDQZ256rr VR256X:$src1, VR256X:$src2)>;

  def : Pat<(v32i8 (or VR256X:$src1, VR256X:$src2)),
            (VPORQZ256rr VR256X:$src1, VR256X:$src2)>;
  def : Pat<(v16i16 (or VR256X:$src1, VR256X:$src2)),
            (VPORQZ256rr VR256X:$src1, VR256X:$src2)>;

  def : Pat<(v32i8 (xor VR256X:$src1, VR256X:$src2)),
            (VPXORQZ256rr VR256X:$src1, VR256X:$src2)>;
  def : Pat<(v16i16 (xor VR256X:$src1, VR256X:$src2)),
            (VPXORQZ256rr VR256X:$src1, VR256X:$src2)>;

  def : Pat<(v32i8 (X86andnp VR256X:$src1, VR256X:$src2)),
            (VPANDNQZ256rr VR256X:$src1, VR256X:$src2)>;
  def : Pat<(v16i16 (X86andnp VR256X:$src1, VR256X:$src2)),
            (VPANDNQZ256rr VR256X:$src1, VR256X:$src2)>;

  def : Pat<(and VR256X:$src1, (loadv32i8 addr:$src2)),
            (VPANDQZ256rm VR256X:$src1, addr:$src2)>;
  def : Pat<(and VR256X:$src1, (loadv16i16 addr:$src2)),
            (VPANDQZ256rm VR256X:$src1, addr:$src2)>;

  def : Pat<(or VR256X:$src1, (loadv32i8 addr:$src2)),
            (VPORQZ256rm VR256X:$src1, addr:$src2)>;
  def : Pat<(or VR256X:$src1, (loadv16i16 addr:$src2)),
            (VPORQZ256rm VR256X:$src1, addr:$src2)>;

  def : Pat<(xor VR256X:$src1, (loadv32i8 addr:$src2)),
            (VPXORQZ256rm VR256X:$src1, addr:$src2)>;
  def : Pat<(xor VR256X:$src1, (loadv16i16 addr:$src2)),
            (VPXORQZ256rm VR256X:$src1, addr:$src2)>;

  def : Pat<(X86andnp VR256X:$src1, (loadv32i8 addr:$src2)),
            (VPANDNQZ256rm VR256X:$src1, addr:$src2)>;
  def : Pat<(X86andnp VR256X:$src1, (loadv16i16 addr:$src2)),
            (VPANDNQZ256rm VR256X:$src1, addr:$src2)>;
}

let Predicates = [HasAVX512] in {
  def : Pat<(v64i8 (and VR512:$src1, VR512:$src2)),
            (VPANDQZrr VR512:$src1, VR512:$src2)>;
  def : Pat<(v32i16 (and VR512:$src1, VR512:$src2)),
            (VPANDQZrr VR512:$src1, VR512:$src2)>;

  def : Pat<(v64i8 (or VR512:$src1, VR512:$src2)),
            (VPORQZrr VR512:$src1, VR512:$src2)>;
  def : Pat<(v32i16 (or VR512:$src1, VR512:$src2)),
            (VPORQZrr VR512:$src1, VR512:$src2)>;

  def : Pat<(v64i8 (xor VR512:$src1, VR512:$src2)),
            (VPXORQZrr VR512:$src1, VR512:$src2)>;
  def : Pat<(v32i16 (xor VR512:$src1, VR512:$src2)),
            (VPXORQZrr VR512:$src1, VR512:$src2)>;

  def : Pat<(v64i8 (X86andnp VR512:$src1, VR512:$src2)),
            (VPANDNQZrr VR512:$src1, VR512:$src2)>;
  def : Pat<(v32i16 (X86andnp VR512:$src1, VR512:$src2)),
            (VPANDNQZrr VR512:$src1, VR512:$src2)>;

  def : Pat<(and VR512:$src1, (loadv64i8 addr:$src2)),
            (VPANDQZrm VR512:$src1, addr:$src2)>;
  def : Pat<(and VR512:$src1, (loadv32i16 addr:$src2)),
            (VPANDQZrm VR512:$src1, addr:$src2)>;

  def : Pat<(or VR512:$src1, (loadv64i8 addr:$src2)),
            (VPORQZrm VR512:$src1, addr:$src2)>;
  def : Pat<(or VR512:$src1, (loadv32i16 addr:$src2)),
            (VPORQZrm VR512:$src1, addr:$src2)>;

  def : Pat<(xor VR512:$src1, (loadv64i8 addr:$src2)),
            (VPXORQZrm VR512:$src1, addr:$src2)>;
  def : Pat<(xor VR512:$src1, (loadv32i16 addr:$src2)),
            (VPXORQZrm VR512:$src1, addr:$src2)>;

  def : Pat<(X86andnp VR512:$src1, (loadv64i8 addr:$src2)),
            (VPANDNQZrm VR512:$src1, addr:$src2)>;
  def : Pat<(X86andnp VR512:$src1, (loadv32i16 addr:$src2)),
            (VPANDNQZrm VR512:$src1, addr:$src2)>;
}

// Patterns to catch vselect with different type than logic op.
multiclass avx512_logical_lowering<string InstrStr, SDNode OpNode,
                                    X86VectorVTInfo _,
                                    X86VectorVTInfo IntInfo> {
  // Masked register-register logical operations.
  def : Pat<(_.VT (vselect_mask _.KRCWM:$mask,
                   (bitconvert (IntInfo.VT (OpNode _.RC:$src1, _.RC:$src2))),
                   _.RC:$src0)),
            (!cast<Instruction>(InstrStr#rrk) _.RC:$src0, _.KRCWM:$mask,
             _.RC:$src1, _.RC:$src2)>;

  def : Pat<(_.VT (vselect_mask _.KRCWM:$mask,
                   (bitconvert (IntInfo.VT (OpNode _.RC:$src1, _.RC:$src2))),
                   _.ImmAllZerosV)),
            (!cast<Instruction>(InstrStr#rrkz) _.KRCWM:$mask, _.RC:$src1,
             _.RC:$src2)>;

  // Masked register-memory logical operations.
  def : Pat<(_.VT (vselect_mask _.KRCWM:$mask,
                   (bitconvert (IntInfo.VT (OpNode _.RC:$src1,
                                            (load addr:$src2)))),
                   _.RC:$src0)),
            (!cast<Instruction>(InstrStr#rmk) _.RC:$src0, _.KRCWM:$mask,
             _.RC:$src1, addr:$src2)>;
  def : Pat<(_.VT (vselect_mask _.KRCWM:$mask,
                   (bitconvert (IntInfo.VT (OpNode _.RC:$src1,
                                            (load addr:$src2)))),
                   _.ImmAllZerosV)),
            (!cast<Instruction>(InstrStr#rmkz) _.KRCWM:$mask, _.RC:$src1,
             addr:$src2)>;
}

multiclass avx512_logical_lowering_bcast<string InstrStr, SDNode OpNode,
                                         X86VectorVTInfo _,
                                         X86VectorVTInfo IntInfo> {
  // Register-broadcast logical operations.
  def : Pat<(_.VT (vselect_mask _.KRCWM:$mask,
                   (bitconvert
                    (IntInfo.VT (OpNode _.RC:$src1,
                                 (IntInfo.VT (IntInfo.BroadcastLdFrag addr:$src2))))),
                   _.RC:$src0)),
            (!cast<Instruction>(InstrStr#rmbk) _.RC:$src0, _.KRCWM:$mask,
             _.RC:$src1, addr:$src2)>;
  def : Pat<(_.VT (vselect_mask _.KRCWM:$mask,
                   (bitconvert
                    (IntInfo.VT (OpNode _.RC:$src1,
                                 (IntInfo.VT (IntInfo.BroadcastLdFrag addr:$src2))))),
                   _.ImmAllZerosV)),
            (!cast<Instruction>(InstrStr#rmbkz)  _.KRCWM:$mask,
             _.RC:$src1, addr:$src2)>;
}

multiclass avx512_logical_lowering_sizes<string InstrStr, SDNode OpNode,
                                         AVX512VLVectorVTInfo SelectInfo,
                                         AVX512VLVectorVTInfo IntInfo> {
let Predicates = [HasVLX] in {
  defm : avx512_logical_lowering<InstrStr#"Z128", OpNode, SelectInfo.info128,
                                 IntInfo.info128>;
  defm : avx512_logical_lowering<InstrStr#"Z256", OpNode, SelectInfo.info256,
                                 IntInfo.info256>;
}
let Predicates = [HasAVX512] in {
  defm : avx512_logical_lowering<InstrStr#"Z", OpNode, SelectInfo.info512,
                                 IntInfo.info512>;
}
}

multiclass avx512_logical_lowering_sizes_bcast<string InstrStr, SDNode OpNode,
                                               AVX512VLVectorVTInfo SelectInfo,
                                               AVX512VLVectorVTInfo IntInfo> {
let Predicates = [HasVLX] in {
  defm : avx512_logical_lowering_bcast<InstrStr#"Z128", OpNode,
                                       SelectInfo.info128, IntInfo.info128>;
  defm : avx512_logical_lowering_bcast<InstrStr#"Z256", OpNode,
                                       SelectInfo.info256, IntInfo.info256>;
}
let Predicates = [HasAVX512] in {
  defm : avx512_logical_lowering_bcast<InstrStr#"Z", OpNode,
                                       SelectInfo.info512, IntInfo.info512>;
}
}

multiclass avx512_logical_lowering_types<string InstrStr, SDNode OpNode> {
  // i64 vselect with i32/i16/i8 logic op
  defm : avx512_logical_lowering_sizes<InstrStr#"Q", OpNode, avx512vl_i64_info,
                                       avx512vl_i32_info>;
  defm : avx512_logical_lowering_sizes<InstrStr#"Q", OpNode, avx512vl_i64_info,
                                       avx512vl_i16_info>;
  defm : avx512_logical_lowering_sizes<InstrStr#"Q", OpNode, avx512vl_i64_info,
                                       avx512vl_i8_info>;

  // i32 vselect with i64/i16/i8 logic op
  defm : avx512_logical_lowering_sizes<InstrStr#"D", OpNode, avx512vl_i32_info,
                                       avx512vl_i64_info>;
  defm : avx512_logical_lowering_sizes<InstrStr#"D", OpNode, avx512vl_i32_info,
                                       avx512vl_i16_info>;
  defm : avx512_logical_lowering_sizes<InstrStr#"D", OpNode, avx512vl_i32_info,
                                       avx512vl_i8_info>;

  // f32 vselect with i64/i32/i16/i8 logic op
  defm : avx512_logical_lowering_sizes<InstrStr#"D", OpNode, avx512vl_f32_info,
                                       avx512vl_i64_info>;
  defm : avx512_logical_lowering_sizes<InstrStr#"D", OpNode, avx512vl_f32_info,
                                       avx512vl_i32_info>;
  defm : avx512_logical_lowering_sizes<InstrStr#"D", OpNode, avx512vl_f32_info,
                                       avx512vl_i16_info>;
  defm : avx512_logical_lowering_sizes<InstrStr#"D", OpNode, avx512vl_f32_info,
                                       avx512vl_i8_info>;

  // f64 vselect with i64/i32/i16/i8 logic op
  defm : avx512_logical_lowering_sizes<InstrStr#"Q", OpNode, avx512vl_f64_info,
                                       avx512vl_i64_info>;
  defm : avx512_logical_lowering_sizes<InstrStr#"Q", OpNode, avx512vl_f64_info,
                                       avx512vl_i32_info>;
  defm : avx512_logical_lowering_sizes<InstrStr#"Q", OpNode, avx512vl_f64_info,
                                       avx512vl_i16_info>;
  defm : avx512_logical_lowering_sizes<InstrStr#"Q", OpNode, avx512vl_f64_info,
                                       avx512vl_i8_info>;

  defm : avx512_logical_lowering_sizes_bcast<InstrStr#"D", OpNode,
                                             avx512vl_f32_info,
                                             avx512vl_i32_info>;
  defm : avx512_logical_lowering_sizes_bcast<InstrStr#"Q", OpNode,
                                             avx512vl_f64_info,
                                             avx512vl_i64_info>;
}

defm : avx512_logical_lowering_types<"VPAND", and>;
defm : avx512_logical_lowering_types<"VPOR",  or>;
defm : avx512_logical_lowering_types<"VPXOR", xor>;
defm : avx512_logical_lowering_types<"VPANDN", X86andnp>;

//===----------------------------------------------------------------------===//
// AVX-512  FP arithmetic
//===----------------------------------------------------------------------===//

multiclass avx512_fp_scalar<bits<8> opc, string OpcodeStr,X86VectorVTInfo _,
                            SDPatternOperator OpNode, SDNode VecNode,
                            X86FoldableSchedWrite sched, bit IsCommutable> {
  let ExeDomain = _.ExeDomain, Uses = [MXCSR], mayRaiseFPException = 1 in {
  defm rr_Int : AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                           (ins _.RC:$src1, _.RC:$src2), OpcodeStr,
                           "$src2, $src1", "$src1, $src2",
                           (_.VT (VecNode _.RC:$src1, _.RC:$src2))>,
                           Sched<[sched]>;

  defm rm_Int : AVX512_maskable_scalar<opc, MRMSrcMem, _, (outs _.RC:$dst),
                         (ins _.RC:$src1, _.IntScalarMemOp:$src2), OpcodeStr,
                         "$src2, $src1", "$src1, $src2",
                         (_.VT (VecNode _.RC:$src1,
                                        (_.ScalarIntMemFrags addr:$src2)))>,
                         Sched<[sched.Folded, sched.ReadAfterFold]>;
  let isCodeGenOnly = 1, Predicates = [HasAVX512] in {
  def rr : I< opc, MRMSrcReg, (outs _.FRC:$dst),
                         (ins _.FRC:$src1, _.FRC:$src2),
                          OpcodeStr#"\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                          [(set _.FRC:$dst, (OpNode _.FRC:$src1, _.FRC:$src2))]>,
                          Sched<[sched]> {
    let isCommutable = IsCommutable;
  }
  def rm : I< opc, MRMSrcMem, (outs _.FRC:$dst),
                         (ins _.FRC:$src1, _.ScalarMemOp:$src2),
                         OpcodeStr#"\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                         [(set _.FRC:$dst, (OpNode _.FRC:$src1,
                         (_.ScalarLdFrag addr:$src2)))]>,
                         Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
  }
}

multiclass avx512_fp_scalar_round<bits<8> opc, string OpcodeStr,X86VectorVTInfo _,
                                  SDNode VecNode, X86FoldableSchedWrite sched> {
  let ExeDomain = _.ExeDomain, Uses = [MXCSR] in
  defm rrb_Int : AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                          (ins _.RC:$src1, _.RC:$src2, AVX512RC:$rc), OpcodeStr,
                          "$rc, $src2, $src1", "$src1, $src2, $rc",
                          (VecNode (_.VT _.RC:$src1), (_.VT _.RC:$src2),
                          (i32 timm:$rc))>,
                          EVEX_B, EVEX_RC, Sched<[sched]>;
}
multiclass avx512_fp_scalar_sae<bits<8> opc, string OpcodeStr,X86VectorVTInfo _,
                                SDNode OpNode, SDNode VecNode, SDNode SaeNode,
                                X86FoldableSchedWrite sched, bit IsCommutable> {
  let ExeDomain = _.ExeDomain in {
  defm rr_Int : AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                           (ins _.RC:$src1, _.RC:$src2), OpcodeStr,
                           "$src2, $src1", "$src1, $src2",
                           (_.VT (VecNode _.RC:$src1, _.RC:$src2))>,
                           Sched<[sched]>, SIMD_EXC;

  defm rm_Int : AVX512_maskable_scalar<opc, MRMSrcMem, _, (outs _.RC:$dst),
                         (ins _.RC:$src1, _.IntScalarMemOp:$src2), OpcodeStr,
                         "$src2, $src1", "$src1, $src2",
                         (_.VT (VecNode _.RC:$src1,
                                        (_.ScalarIntMemFrags addr:$src2)))>,
                         Sched<[sched.Folded, sched.ReadAfterFold]>, SIMD_EXC;

  let isCodeGenOnly = 1, Predicates = [HasAVX512],
      Uses = [MXCSR], mayRaiseFPException = 1 in {
  def rr : I< opc, MRMSrcReg, (outs _.FRC:$dst),
                         (ins _.FRC:$src1, _.FRC:$src2),
                          OpcodeStr#"\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                          [(set _.FRC:$dst, (OpNode _.FRC:$src1, _.FRC:$src2))]>,
                          Sched<[sched]> {
    let isCommutable = IsCommutable;
  }
  def rm : I< opc, MRMSrcMem, (outs _.FRC:$dst),
                         (ins _.FRC:$src1, _.ScalarMemOp:$src2),
                         OpcodeStr#"\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                         [(set _.FRC:$dst, (OpNode _.FRC:$src1,
                         (_.ScalarLdFrag addr:$src2)))]>,
                         Sched<[sched.Folded, sched.ReadAfterFold]>;
  }

  let Uses = [MXCSR] in
  defm rrb_Int : AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                            (ins _.RC:$src1, _.RC:$src2), OpcodeStr,
                            "{sae}, $src2, $src1", "$src1, $src2, {sae}",
                            (SaeNode (_.VT _.RC:$src1), (_.VT _.RC:$src2))>,
                            EVEX_B, Sched<[sched]>;
  }
}

multiclass avx512_binop_s_round<bits<8> opc, string OpcodeStr, SDPatternOperator OpNode,
                                SDNode VecNode, SDNode RndNode,
                                X86SchedWriteSizes sched, bit IsCommutable> {
  defm SSZ : avx512_fp_scalar<opc, OpcodeStr#"ss", f32x_info, OpNode, VecNode,
                              sched.PS.Scl, IsCommutable>,
             avx512_fp_scalar_round<opc, OpcodeStr#"ss", f32x_info, RndNode,
                              sched.PS.Scl>,
                              TB, XS, EVEX, VVVV, VEX_LIG,  EVEX_CD8<32, CD8VT1>;
  defm SDZ : avx512_fp_scalar<opc, OpcodeStr#"sd", f64x_info, OpNode, VecNode,
                              sched.PD.Scl, IsCommutable>,
             avx512_fp_scalar_round<opc, OpcodeStr#"sd", f64x_info, RndNode,
                              sched.PD.Scl>,
                              TB, XD, REX_W, EVEX, VVVV, VEX_LIG, EVEX_CD8<64, CD8VT1>;
  let Predicates = [HasFP16] in
    defm SHZ : avx512_fp_scalar<opc, OpcodeStr#"sh", f16x_info, OpNode,
                                VecNode, sched.PH.Scl, IsCommutable>,
               avx512_fp_scalar_round<opc, OpcodeStr#"sh", f16x_info, RndNode,
                                sched.PH.Scl>,
                                T_MAP5, XS, EVEX, VVVV, VEX_LIG, EVEX_CD8<16, CD8VT1>;
}

multiclass avx512_binop_s_sae<bits<8> opc, string OpcodeStr, SDNode OpNode,
                              SDNode VecNode, SDNode SaeNode,
                              X86SchedWriteSizes sched, bit IsCommutable> {
  defm SSZ : avx512_fp_scalar_sae<opc, OpcodeStr#"ss", f32x_info, OpNode,
                              VecNode, SaeNode, sched.PS.Scl, IsCommutable>,
                              TB, XS, EVEX, VVVV, VEX_LIG,  EVEX_CD8<32, CD8VT1>;
  defm SDZ : avx512_fp_scalar_sae<opc, OpcodeStr#"sd", f64x_info, OpNode,
                              VecNode, SaeNode, sched.PD.Scl, IsCommutable>,
                              TB, XD, REX_W, EVEX, VVVV, VEX_LIG, EVEX_CD8<64, CD8VT1>;
  let Predicates = [HasFP16] in {
    defm SHZ : avx512_fp_scalar_sae<opc, OpcodeStr#"sh", f16x_info, OpNode,
                                VecNode, SaeNode, sched.PH.Scl, IsCommutable>,
                                T_MAP5, XS, EVEX, VVVV, VEX_LIG, EVEX_CD8<16, CD8VT1>;
  }
}
defm VADD : avx512_binop_s_round<0x58, "vadd", any_fadd, X86fadds, X86faddRnds,
                                 SchedWriteFAddSizes, 1>;
defm VMUL : avx512_binop_s_round<0x59, "vmul", any_fmul, X86fmuls, X86fmulRnds,
                                 SchedWriteFMulSizes, 1>;
defm VSUB : avx512_binop_s_round<0x5C, "vsub", any_fsub, X86fsubs, X86fsubRnds,
                                 SchedWriteFAddSizes, 0>;
defm VDIV : avx512_binop_s_round<0x5E, "vdiv", any_fdiv, X86fdivs, X86fdivRnds,
                                 SchedWriteFDivSizes, 0>;
defm VMIN : avx512_binop_s_sae<0x5D, "vmin", X86fmin, X86fmins, X86fminSAEs,
                               SchedWriteFCmpSizes, 0>;
defm VMAX : avx512_binop_s_sae<0x5F, "vmax", X86fmax, X86fmaxs, X86fmaxSAEs,
                               SchedWriteFCmpSizes, 0>;

// MIN/MAX nodes are commutable under "unsafe-fp-math". In this case we use
// X86fminc and X86fmaxc instead of X86fmin and X86fmax
multiclass avx512_comutable_binop_s<bits<8> opc, string OpcodeStr,
                                    X86VectorVTInfo _, SDNode OpNode,
                                    X86FoldableSchedWrite sched> {
  let isCodeGenOnly = 1, Predicates = [HasAVX512], ExeDomain = _.ExeDomain in {
  def rr : I< opc, MRMSrcReg, (outs _.FRC:$dst),
                         (ins _.FRC:$src1, _.FRC:$src2),
                          OpcodeStr#"\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                          [(set _.FRC:$dst, (OpNode _.FRC:$src1, _.FRC:$src2))]>,
                          Sched<[sched]> {
    let isCommutable = 1;
  }
  def rm : I< opc, MRMSrcMem, (outs _.FRC:$dst),
                         (ins _.FRC:$src1, _.ScalarMemOp:$src2),
                         OpcodeStr#"\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                         [(set _.FRC:$dst, (OpNode _.FRC:$src1,
                         (_.ScalarLdFrag addr:$src2)))]>,
                         Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}
defm VMINCSSZ : avx512_comutable_binop_s<0x5D, "vminss", f32x_info, X86fminc,
                                         SchedWriteFCmp.Scl>, TB, XS,
                                         EVEX, VVVV, VEX_LIG, EVEX_CD8<32, CD8VT1>, SIMD_EXC;

defm VMINCSDZ : avx512_comutable_binop_s<0x5D, "vminsd", f64x_info, X86fminc,
                                         SchedWriteFCmp.Scl>, TB, XD,
                                         REX_W, EVEX, VVVV, VEX_LIG,
                                         EVEX_CD8<64, CD8VT1>, SIMD_EXC;

defm VMAXCSSZ : avx512_comutable_binop_s<0x5F, "vmaxss", f32x_info, X86fmaxc,
                                         SchedWriteFCmp.Scl>, TB, XS,
                                         EVEX, VVVV, VEX_LIG, EVEX_CD8<32, CD8VT1>, SIMD_EXC;

defm VMAXCSDZ : avx512_comutable_binop_s<0x5F, "vmaxsd", f64x_info, X86fmaxc,
                                         SchedWriteFCmp.Scl>, TB, XD,
                                         REX_W, EVEX, VVVV, VEX_LIG,
                                         EVEX_CD8<64, CD8VT1>, SIMD_EXC;

defm VMINCSHZ : avx512_comutable_binop_s<0x5D, "vminsh", f16x_info, X86fminc,
                                         SchedWriteFCmp.Scl>, T_MAP5, XS,
                                         EVEX, VVVV, VEX_LIG, EVEX_CD8<16, CD8VT1>, SIMD_EXC;

defm VMAXCSHZ : avx512_comutable_binop_s<0x5F, "vmaxsh", f16x_info, X86fmaxc,
                                         SchedWriteFCmp.Scl>, T_MAP5, XS,
                                         EVEX, VVVV, VEX_LIG, EVEX_CD8<16, CD8VT1>, SIMD_EXC;

multiclass avx512_fp_packed<bits<8> opc, string OpcodeStr, SDPatternOperator OpNode,
                            SDPatternOperator MaskOpNode,
                            X86VectorVTInfo _, X86FoldableSchedWrite sched,
                            bit IsCommutable,
                            bit IsKCommutable = IsCommutable,
                            string suffix = _.Suffix,
                            string ClobberConstraint = "",
                            bit MayRaiseFPException = 1> {
  let ExeDomain = _.ExeDomain, hasSideEffects = 0,
      Uses = [MXCSR], mayRaiseFPException = MayRaiseFPException in {
  defm rr: AVX512_maskable_split<opc, MRMSrcReg, _, (outs _.RC:$dst),
                                 (ins _.RC:$src1, _.RC:$src2), OpcodeStr#suffix,
                                 "$src2, $src1", "$src1, $src2",
                                 (_.VT (OpNode _.RC:$src1, _.RC:$src2)),
                                 (_.VT (MaskOpNode _.RC:$src1, _.RC:$src2)), ClobberConstraint,
                                 IsCommutable, IsKCommutable, IsKCommutable>, EVEX, VVVV, Sched<[sched]>;
  let mayLoad = 1 in {
    defm rm: AVX512_maskable_split<opc, MRMSrcMem, _, (outs _.RC:$dst),
                                   (ins _.RC:$src1, _.MemOp:$src2), OpcodeStr#suffix,
                                   "$src2, $src1", "$src1, $src2",
                                   (OpNode _.RC:$src1, (_.LdFrag addr:$src2)),
                                   (MaskOpNode _.RC:$src1, (_.LdFrag addr:$src2)),
                                   ClobberConstraint>, EVEX, VVVV, Sched<[sched.Folded, sched.ReadAfterFold]>;
    defm rmb: AVX512_maskable_split<opc, MRMSrcMem, _, (outs _.RC:$dst),
                                    (ins _.RC:$src1, _.ScalarMemOp:$src2), OpcodeStr#suffix,
                                    "${src2}"#_.BroadcastStr#", $src1",
                                    "$src1, ${src2}"#_.BroadcastStr,
                                    (OpNode  _.RC:$src1, (_.VT (_.BroadcastLdFrag addr:$src2))),
                                    (MaskOpNode  _.RC:$src1, (_.VT (_.BroadcastLdFrag addr:$src2))),
                                    ClobberConstraint>, EVEX, VVVV, EVEX_B, Sched<[sched.Folded, sched.ReadAfterFold]>;
    }
  }
}

multiclass avx512_fp_round_packed<bits<8> opc, string OpcodeStr,
                                  SDPatternOperator OpNodeRnd,
                                  X86FoldableSchedWrite sched, X86VectorVTInfo _,
                                  string suffix = _.Suffix,
                                  string ClobberConstraint = ""> {
  let ExeDomain = _.ExeDomain, Uses = [MXCSR] in
  defm rrb: AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                  (ins _.RC:$src1, _.RC:$src2, AVX512RC:$rc), OpcodeStr#suffix,
                  "$rc, $src2, $src1", "$src1, $src2, $rc",
                  (_.VT (OpNodeRnd _.RC:$src1, _.RC:$src2, (i32 timm:$rc))),
                  0, 0, 0, vselect_mask, ClobberConstraint>,
                  EVEX, VVVV, EVEX_B, EVEX_RC, Sched<[sched]>;
}

multiclass avx512_fp_sae_packed<bits<8> opc, string OpcodeStr,
                                SDPatternOperator OpNodeSAE,
                                X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain, Uses = [MXCSR] in
  defm rrb: AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                  (ins _.RC:$src1, _.RC:$src2), OpcodeStr#_.Suffix,
                  "{sae}, $src2, $src1", "$src1, $src2, {sae}",
                  (_.VT (OpNodeSAE _.RC:$src1, _.RC:$src2))>,
                  EVEX, VVVV, EVEX_B, Sched<[sched]>;
}

multiclass avx512_fp_binop_p<bits<8> opc, string OpcodeStr, SDPatternOperator OpNode,
                             SDPatternOperator MaskOpNode,
                             Predicate prd, X86SchedWriteSizes sched,
                             bit IsCommutable = 0,
                             bit IsPD128Commutable = IsCommutable> {
  let Predicates = [prd] in {
  defm PSZ : avx512_fp_packed<opc, OpcodeStr, OpNode, MaskOpNode, v16f32_info,
                              sched.PS.ZMM, IsCommutable>, EVEX_V512, TB,
                              EVEX_CD8<32, CD8VF>;
  defm PDZ : avx512_fp_packed<opc, OpcodeStr, OpNode, MaskOpNode, v8f64_info,
                              sched.PD.ZMM, IsCommutable>, EVEX_V512, TB, PD, REX_W,
                              EVEX_CD8<64, CD8VF>;
  }

    // Define only if AVX512VL feature is present.
  let Predicates = [prd, HasVLX] in {
    defm PSZ128 : avx512_fp_packed<opc, OpcodeStr, OpNode, MaskOpNode, v4f32x_info,
                                   sched.PS.XMM, IsCommutable>, EVEX_V128, TB,
                                   EVEX_CD8<32, CD8VF>;
    defm PSZ256 : avx512_fp_packed<opc, OpcodeStr, OpNode, MaskOpNode, v8f32x_info,
                                   sched.PS.YMM, IsCommutable>, EVEX_V256, TB,
                                   EVEX_CD8<32, CD8VF>;
    defm PDZ128 : avx512_fp_packed<opc, OpcodeStr, OpNode, MaskOpNode, v2f64x_info,
                                   sched.PD.XMM, IsPD128Commutable,
                                   IsCommutable>, EVEX_V128, TB, PD, REX_W,
                                   EVEX_CD8<64, CD8VF>;
    defm PDZ256 : avx512_fp_packed<opc, OpcodeStr, OpNode, MaskOpNode, v4f64x_info,
                                   sched.PD.YMM, IsCommutable>, EVEX_V256, TB, PD, REX_W,
                                   EVEX_CD8<64, CD8VF>;
  }
}

multiclass avx512_fp_binop_ph<bits<8> opc, string OpcodeStr, SDPatternOperator OpNode,
                              SDPatternOperator MaskOpNode,
                              X86SchedWriteSizes sched, bit IsCommutable = 0> {
  let Predicates = [HasFP16] in {
    defm PHZ : avx512_fp_packed<opc, OpcodeStr, OpNode, MaskOpNode, v32f16_info,
                                sched.PH.ZMM, IsCommutable>, EVEX_V512, T_MAP5,
                                EVEX_CD8<16, CD8VF>;
  }
  let Predicates = [HasVLX, HasFP16] in {
    defm PHZ128 : avx512_fp_packed<opc, OpcodeStr, OpNode, MaskOpNode, v8f16x_info,
                                   sched.PH.XMM, IsCommutable>, EVEX_V128, T_MAP5,
                                   EVEX_CD8<16, CD8VF>;
    defm PHZ256 : avx512_fp_packed<opc, OpcodeStr, OpNode, MaskOpNode, v16f16x_info,
                                   sched.PH.YMM, IsCommutable>, EVEX_V256, T_MAP5,
                                   EVEX_CD8<16, CD8VF>;
  }
}

let Uses = [MXCSR] in
multiclass avx512_fp_binop_p_round<bits<8> opc, string OpcodeStr, SDNode OpNodeRnd,
                                   X86SchedWriteSizes sched> {
  let Predicates = [HasFP16] in {
    defm PHZ : avx512_fp_round_packed<opc, OpcodeStr, OpNodeRnd, sched.PH.ZMM,
                                      v32f16_info>,
                                      EVEX_V512, T_MAP5, EVEX_CD8<16, CD8VF>;
  }
  defm PSZ : avx512_fp_round_packed<opc, OpcodeStr, OpNodeRnd, sched.PS.ZMM,
                                    v16f32_info>,
                                    EVEX_V512, TB, EVEX_CD8<32, CD8VF>;
  defm PDZ : avx512_fp_round_packed<opc, OpcodeStr, OpNodeRnd, sched.PD.ZMM,
                                    v8f64_info>,
                                    EVEX_V512, TB, PD, REX_W,EVEX_CD8<64, CD8VF>;
}

let Uses = [MXCSR] in
multiclass avx512_fp_binop_p_sae<bits<8> opc, string OpcodeStr, SDNode OpNodeRnd,
                                 X86SchedWriteSizes sched> {
  let Predicates = [HasFP16] in {
    defm PHZ : avx512_fp_sae_packed<opc, OpcodeStr, OpNodeRnd, sched.PH.ZMM,
                                    v32f16_info>,
                                    EVEX_V512, T_MAP5, EVEX_CD8<16, CD8VF>;
  }
  defm PSZ : avx512_fp_sae_packed<opc, OpcodeStr, OpNodeRnd, sched.PS.ZMM,
                                  v16f32_info>,
                                  EVEX_V512, TB, EVEX_CD8<32, CD8VF>;
  defm PDZ : avx512_fp_sae_packed<opc, OpcodeStr, OpNodeRnd, sched.PD.ZMM,
                                  v8f64_info>,
                                  EVEX_V512, TB, PD, REX_W,EVEX_CD8<64, CD8VF>;
}

defm VADD : avx512_fp_binop_p<0x58, "vadd", any_fadd, fadd, HasAVX512,
                              SchedWriteFAddSizes, 1>,
            avx512_fp_binop_ph<0x58, "vadd", any_fadd, fadd, SchedWriteFAddSizes, 1>,
            avx512_fp_binop_p_round<0x58, "vadd", X86faddRnd, SchedWriteFAddSizes>;
defm VMUL : avx512_fp_binop_p<0x59, "vmul", any_fmul, fmul, HasAVX512,
                              SchedWriteFMulSizes, 1>,
            avx512_fp_binop_ph<0x59, "vmul", any_fmul, fmul, SchedWriteFMulSizes, 1>,
            avx512_fp_binop_p_round<0x59, "vmul", X86fmulRnd, SchedWriteFMulSizes>;
defm VSUB : avx512_fp_binop_p<0x5C, "vsub", any_fsub, fsub, HasAVX512,
                              SchedWriteFAddSizes>,
            avx512_fp_binop_ph<0x5C, "vsub", any_fsub, fsub, SchedWriteFAddSizes>,
            avx512_fp_binop_p_round<0x5C, "vsub", X86fsubRnd, SchedWriteFAddSizes>;
defm VDIV : avx512_fp_binop_p<0x5E, "vdiv", any_fdiv, fdiv, HasAVX512,
                              SchedWriteFDivSizes>,
            avx512_fp_binop_ph<0x5E, "vdiv", any_fdiv, fdiv, SchedWriteFDivSizes>,
            avx512_fp_binop_p_round<0x5E, "vdiv", X86fdivRnd, SchedWriteFDivSizes>;
defm VMIN : avx512_fp_binop_p<0x5D, "vmin", X86fmin, X86fmin, HasAVX512,
                              SchedWriteFCmpSizes, 0>,
            avx512_fp_binop_ph<0x5D, "vmin", X86fmin, X86fmin, SchedWriteFCmpSizes, 0>,
            avx512_fp_binop_p_sae<0x5D, "vmin", X86fminSAE, SchedWriteFCmpSizes>;
defm VMAX : avx512_fp_binop_p<0x5F, "vmax", X86fmax, X86fmax, HasAVX512,
                              SchedWriteFCmpSizes, 0>,
            avx512_fp_binop_ph<0x5F, "vmax", X86fmax, X86fmax, SchedWriteFCmpSizes, 0>,
            avx512_fp_binop_p_sae<0x5F, "vmax", X86fmaxSAE, SchedWriteFCmpSizes>;
let isCodeGenOnly = 1 in {
  defm VMINC : avx512_fp_binop_p<0x5D, "vmin", X86fminc, X86fminc, HasAVX512,
                                 SchedWriteFCmpSizes, 1>,
               avx512_fp_binop_ph<0x5D, "vmin", X86fminc, X86fminc,
                                 SchedWriteFCmpSizes, 1>;
  defm VMAXC : avx512_fp_binop_p<0x5F, "vmax", X86fmaxc, X86fmaxc, HasAVX512,
                                 SchedWriteFCmpSizes, 1>,
               avx512_fp_binop_ph<0x5F, "vmax", X86fmaxc, X86fmaxc,
                                 SchedWriteFCmpSizes, 1>;
}
let Uses = []<Register>, mayRaiseFPException = 0 in {
defm VAND  : avx512_fp_binop_p<0x54, "vand", null_frag, null_frag, HasDQI,
                               SchedWriteFLogicSizes, 1>;
defm VANDN : avx512_fp_binop_p<0x55, "vandn", null_frag, null_frag, HasDQI,
                               SchedWriteFLogicSizes, 0>;
defm VOR   : avx512_fp_binop_p<0x56, "vor", null_frag, null_frag, HasDQI,
                               SchedWriteFLogicSizes, 1>;
defm VXOR  : avx512_fp_binop_p<0x57, "vxor", null_frag, null_frag, HasDQI,
                               SchedWriteFLogicSizes, 1>;
}

multiclass avx512_fp_scalef_p<bits<8> opc, string OpcodeStr, SDNode OpNode,
                              X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain, Uses = [MXCSR], mayRaiseFPException = 1 in {
  defm rr: AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                  (ins _.RC:$src1, _.RC:$src2), OpcodeStr#_.Suffix,
                  "$src2, $src1", "$src1, $src2",
                  (_.VT (OpNode _.RC:$src1, _.RC:$src2))>,
                  EVEX, VVVV, Sched<[sched]>;
  defm rm: AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                  (ins _.RC:$src1, _.MemOp:$src2), OpcodeStr#_.Suffix,
                  "$src2, $src1", "$src1, $src2",
                  (OpNode _.RC:$src1, (_.LdFrag addr:$src2))>,
                  EVEX, VVVV, Sched<[sched.Folded, sched.ReadAfterFold]>;
  defm rmb: AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                   (ins _.RC:$src1, _.ScalarMemOp:$src2), OpcodeStr#_.Suffix,
                   "${src2}"#_.BroadcastStr#", $src1",
                   "$src1, ${src2}"#_.BroadcastStr,
                   (OpNode  _.RC:$src1, (_.VT (_.BroadcastLdFrag addr:$src2)))>,
                   EVEX, VVVV, EVEX_B, Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

multiclass avx512_fp_scalef_scalar<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                   X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain, Uses = [MXCSR], mayRaiseFPException = 1 in {
  defm rr: AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                  (ins _.RC:$src1, _.RC:$src2), OpcodeStr#_.Suffix,
                  "$src2, $src1", "$src1, $src2",
                  (_.VT (OpNode _.RC:$src1, _.RC:$src2))>,
                  Sched<[sched]>;
  defm rm: AVX512_maskable_scalar<opc, MRMSrcMem, _, (outs _.RC:$dst),
                  (ins _.RC:$src1, _.IntScalarMemOp:$src2), OpcodeStr#_.Suffix,
                  "$src2, $src1", "$src1, $src2",
                  (OpNode _.RC:$src1, (_.ScalarIntMemFrags addr:$src2))>,
                  Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

multiclass avx512_fp_scalef_all<bits<8> opc, bits<8> opcScaler, string OpcodeStr,
                                X86SchedWriteWidths sched> {
  let Predicates = [HasFP16] in {
    defm PHZ : avx512_fp_scalef_p<opc, OpcodeStr, X86scalef, sched.ZMM, v32f16_info>,
               avx512_fp_round_packed<opc, OpcodeStr, X86scalefRnd, sched.ZMM, v32f16_info>,
                                EVEX_V512, T_MAP6, PD, EVEX_CD8<16, CD8VF>;
    defm SHZ : avx512_fp_scalef_scalar<opcScaler, OpcodeStr, X86scalefs, sched.Scl, f16x_info>,
               avx512_fp_scalar_round<opcScaler, OpcodeStr#"sh", f16x_info, X86scalefsRnd, sched.Scl>,
                             EVEX, VVVV, T_MAP6, PD, EVEX_CD8<16, CD8VT1>;
  }
  defm PSZ : avx512_fp_scalef_p<opc, OpcodeStr, X86scalef, sched.ZMM, v16f32_info>,
             avx512_fp_round_packed<opc, OpcodeStr, X86scalefRnd, sched.ZMM, v16f32_info>,
                              EVEX_V512, EVEX_CD8<32, CD8VF>, T8, PD;
  defm PDZ : avx512_fp_scalef_p<opc, OpcodeStr, X86scalef, sched.ZMM, v8f64_info>,
             avx512_fp_round_packed<opc, OpcodeStr, X86scalefRnd, sched.ZMM, v8f64_info>,
                              EVEX_V512, REX_W, EVEX_CD8<64, CD8VF>, T8, PD;
  defm SSZ : avx512_fp_scalef_scalar<opcScaler, OpcodeStr, X86scalefs, sched.Scl, f32x_info>,
             avx512_fp_scalar_round<opcScaler, OpcodeStr#"ss", f32x_info,
                                    X86scalefsRnd, sched.Scl>,
                                    EVEX, VVVV, VEX_LIG, EVEX_CD8<32, CD8VT1>, T8, PD;
  defm SDZ : avx512_fp_scalef_scalar<opcScaler, OpcodeStr, X86scalefs, sched.Scl, f64x_info>,
             avx512_fp_scalar_round<opcScaler, OpcodeStr#"sd", f64x_info,
                                    X86scalefsRnd, sched.Scl>,
                                    EVEX, VVVV, VEX_LIG, EVEX_CD8<64, CD8VT1>, REX_W, T8, PD;

  // Define only if AVX512VL feature is present.
  let Predicates = [HasVLX] in {
    defm PSZ128 : avx512_fp_scalef_p<opc, OpcodeStr, X86scalef, sched.XMM, v4f32x_info>,
                                   EVEX_V128, EVEX_CD8<32, CD8VF>, T8, PD;
    defm PSZ256 : avx512_fp_scalef_p<opc, OpcodeStr, X86scalef, sched.YMM, v8f32x_info>,
                                   EVEX_V256, EVEX_CD8<32, CD8VF>, T8, PD;
    defm PDZ128 : avx512_fp_scalef_p<opc, OpcodeStr, X86scalef, sched.XMM, v2f64x_info>,
                                   EVEX_V128, REX_W, EVEX_CD8<64, CD8VF>, T8, PD;
    defm PDZ256 : avx512_fp_scalef_p<opc, OpcodeStr, X86scalef, sched.YMM, v4f64x_info>,
                                   EVEX_V256, REX_W, EVEX_CD8<64, CD8VF>, T8, PD;
  }

  let Predicates = [HasFP16, HasVLX] in {
    defm PHZ128 : avx512_fp_scalef_p<opc, OpcodeStr, X86scalef, sched.XMM, v8f16x_info>,
                                   EVEX_V128, EVEX_CD8<16, CD8VF>, T_MAP6, PD;
    defm PHZ256 : avx512_fp_scalef_p<opc, OpcodeStr, X86scalef, sched.YMM, v16f16x_info>,
                                   EVEX_V256, EVEX_CD8<16, CD8VF>, T_MAP6, PD;
  }
}
defm VSCALEF : avx512_fp_scalef_all<0x2C, 0x2D, "vscalef", SchedWriteFAdd>;

//===----------------------------------------------------------------------===//
// AVX-512  VPTESTM instructions
//===----------------------------------------------------------------------===//

multiclass avx512_vptest<bits<8> opc, string OpcodeStr,
                         X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  // NOTE: Patterns are omitted in favor of manual selection in X86ISelDAGToDAG.
  // There are just too many permutations due to commutability and bitcasts.
  let ExeDomain = _.ExeDomain, hasSideEffects = 0 in {
  defm rr : AVX512_maskable_cmp<opc, MRMSrcReg, _, (outs _.KRC:$dst),
                   (ins _.RC:$src1, _.RC:$src2), OpcodeStr,
                      "$src2, $src1", "$src1, $src2",
                   (null_frag), (null_frag), 1>,
                   EVEX, VVVV, Sched<[sched]>;
  let mayLoad = 1 in
  defm rm : AVX512_maskable_cmp<opc, MRMSrcMem, _, (outs _.KRC:$dst),
                   (ins _.RC:$src1, _.MemOp:$src2), OpcodeStr,
                       "$src2, $src1", "$src1, $src2",
                   (null_frag), (null_frag)>,
                   EVEX, VVVV, EVEX_CD8<_.EltSize, CD8VF>,
                   Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

multiclass avx512_vptest_mb<bits<8> opc, string OpcodeStr,
                            X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain, mayLoad = 1, hasSideEffects = 0 in
  defm rmb : AVX512_maskable_cmp<opc, MRMSrcMem, _, (outs _.KRC:$dst),
                    (ins _.RC:$src1, _.ScalarMemOp:$src2), OpcodeStr,
                    "${src2}"#_.BroadcastStr#", $src1",
                    "$src1, ${src2}"#_.BroadcastStr,
                    (null_frag), (null_frag)>,
                    EVEX_B, EVEX, VVVV, EVEX_CD8<_.EltSize, CD8VF>,
                    Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass avx512_vptest_dq_sizes<bits<8> opc, string OpcodeStr,
                                  X86SchedWriteWidths sched,
                                  AVX512VLVectorVTInfo _> {
  let Predicates  = [HasAVX512] in
  defm Z : avx512_vptest<opc, OpcodeStr, sched.ZMM, _.info512>,
           avx512_vptest_mb<opc, OpcodeStr, sched.ZMM, _.info512>, EVEX_V512;

  let Predicates = [HasAVX512, HasVLX] in {
  defm Z256 : avx512_vptest<opc, OpcodeStr, sched.YMM, _.info256>,
              avx512_vptest_mb<opc, OpcodeStr, sched.YMM, _.info256>, EVEX_V256;
  defm Z128 : avx512_vptest<opc, OpcodeStr, sched.XMM, _.info128>,
              avx512_vptest_mb<opc, OpcodeStr, sched.XMM, _.info128>, EVEX_V128;
  }
}

multiclass avx512_vptest_dq<bits<8> opc, string OpcodeStr,
                            X86SchedWriteWidths sched> {
  defm D : avx512_vptest_dq_sizes<opc, OpcodeStr#"d", sched,
                                 avx512vl_i32_info>;
  defm Q : avx512_vptest_dq_sizes<opc, OpcodeStr#"q", sched,
                                 avx512vl_i64_info>, REX_W;
}

multiclass avx512_vptest_wb<bits<8> opc, string OpcodeStr,
                            X86SchedWriteWidths sched> {
  let Predicates = [HasBWI] in {
  defm WZ:    avx512_vptest<opc, OpcodeStr#"w", sched.ZMM,
                            v32i16_info>, EVEX_V512, REX_W;
  defm BZ:    avx512_vptest<opc, OpcodeStr#"b", sched.ZMM,
                            v64i8_info>, EVEX_V512;
  }

  let Predicates = [HasVLX, HasBWI] in {
  defm WZ256: avx512_vptest<opc, OpcodeStr#"w", sched.YMM,
                            v16i16x_info>, EVEX_V256, REX_W;
  defm WZ128: avx512_vptest<opc, OpcodeStr#"w", sched.XMM,
                            v8i16x_info>, EVEX_V128, REX_W;
  defm BZ256: avx512_vptest<opc, OpcodeStr#"b", sched.YMM,
                            v32i8x_info>, EVEX_V256;
  defm BZ128: avx512_vptest<opc, OpcodeStr#"b", sched.XMM,
                            v16i8x_info>, EVEX_V128;
  }
}

multiclass avx512_vptest_all_forms<bits<8> opc_wb, bits<8> opc_dq, string OpcodeStr,
                                   X86SchedWriteWidths sched> :
  avx512_vptest_wb<opc_wb, OpcodeStr, sched>,
  avx512_vptest_dq<opc_dq, OpcodeStr, sched>;

defm VPTESTM   : avx512_vptest_all_forms<0x26, 0x27, "vptestm",
                                         SchedWriteVecLogic>, T8, PD;
defm VPTESTNM  : avx512_vptest_all_forms<0x26, 0x27, "vptestnm",
                                         SchedWriteVecLogic>, T8, XS;

//===----------------------------------------------------------------------===//
// AVX-512  Shift instructions
//===----------------------------------------------------------------------===//

multiclass avx512_shift_rmi<bits<8> opc, Format ImmFormR, Format ImmFormM,
                            string OpcodeStr, SDNode OpNode,
                            X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain in {
  defm ri : AVX512_maskable<opc, ImmFormR, _, (outs _.RC:$dst),
                   (ins _.RC:$src1, u8imm:$src2), OpcodeStr,
                      "$src2, $src1", "$src1, $src2",
                   (_.VT (OpNode _.RC:$src1, (i8 timm:$src2)))>,
                   Sched<[sched]>;
  defm mi : AVX512_maskable<opc, ImmFormM, _, (outs _.RC:$dst),
                   (ins _.MemOp:$src1, u8imm:$src2), OpcodeStr,
                       "$src2, $src1", "$src1, $src2",
                   (_.VT (OpNode (_.VT (_.LdFrag addr:$src1)),
                          (i8 timm:$src2)))>,
                   Sched<[sched.Folded]>;
  }
}

multiclass avx512_shift_rmbi<bits<8> opc, Format ImmFormM,
                             string OpcodeStr, SDNode OpNode,
                             X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain in
  defm mbi : AVX512_maskable<opc, ImmFormM, _, (outs _.RC:$dst),
                   (ins _.ScalarMemOp:$src1, u8imm:$src2), OpcodeStr,
      "$src2, ${src1}"#_.BroadcastStr, "${src1}"#_.BroadcastStr#", $src2",
     (_.VT (OpNode (_.BroadcastLdFrag addr:$src1), (i8 timm:$src2)))>,
     EVEX_B, Sched<[sched.Folded]>;
}

multiclass avx512_shift_rrm<bits<8> opc, string OpcodeStr, SDNode OpNode,
                            X86FoldableSchedWrite sched, ValueType SrcVT,
                            X86VectorVTInfo _> {
   // src2 is always 128-bit
  let ExeDomain = _.ExeDomain in {
  defm rr : AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                   (ins _.RC:$src1, VR128X:$src2), OpcodeStr,
                      "$src2, $src1", "$src1, $src2",
                   (_.VT (OpNode _.RC:$src1, (SrcVT VR128X:$src2)))>,
                   AVX512BIBase, EVEX, VVVV, Sched<[sched]>;
  defm rm : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                   (ins _.RC:$src1, i128mem:$src2), OpcodeStr,
                       "$src2, $src1", "$src1, $src2",
                   (_.VT (OpNode _.RC:$src1, (SrcVT (load addr:$src2))))>,
                   AVX512BIBase,
                   EVEX, VVVV, Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

multiclass avx512_shift_sizes<bits<8> opc, string OpcodeStr, SDNode OpNode,
                              X86SchedWriteWidths sched, ValueType SrcVT,
                              AVX512VLVectorVTInfo VTInfo,
                              Predicate prd> {
  let Predicates = [prd] in
  defm Z    : avx512_shift_rrm<opc, OpcodeStr, OpNode, sched.ZMM, SrcVT,
                               VTInfo.info512>, EVEX_V512,
                               EVEX_CD8<VTInfo.info512.EltSize, CD8VQ> ;
  let Predicates = [prd, HasVLX] in {
  defm Z256 : avx512_shift_rrm<opc, OpcodeStr, OpNode, sched.YMM, SrcVT,
                               VTInfo.info256>, EVEX_V256,
                               EVEX_CD8<VTInfo.info256.EltSize, CD8VH>;
  defm Z128 : avx512_shift_rrm<opc, OpcodeStr, OpNode, sched.XMM, SrcVT,
                               VTInfo.info128>, EVEX_V128,
                               EVEX_CD8<VTInfo.info128.EltSize, CD8VF>;
  }
}

multiclass avx512_shift_types<bits<8> opcd, bits<8> opcq, bits<8> opcw,
                              string OpcodeStr, SDNode OpNode,
                              X86SchedWriteWidths sched> {
  defm D : avx512_shift_sizes<opcd, OpcodeStr#"d", OpNode, sched, v4i32,
                              avx512vl_i32_info, HasAVX512>;
  defm Q : avx512_shift_sizes<opcq, OpcodeStr#"q", OpNode, sched, v2i64,
                              avx512vl_i64_info, HasAVX512>, REX_W;
  defm W : avx512_shift_sizes<opcw, OpcodeStr#"w", OpNode, sched, v8i16,
                              avx512vl_i16_info, HasBWI>;
}

multiclass avx512_shift_rmi_sizes<bits<8> opc, Format ImmFormR, Format ImmFormM,
                                  string OpcodeStr, SDNode OpNode,
                                  X86SchedWriteWidths sched,
                                  AVX512VLVectorVTInfo VTInfo> {
  let Predicates = [HasAVX512] in
  defm Z:    avx512_shift_rmi<opc, ImmFormR, ImmFormM, OpcodeStr, OpNode,
                              sched.ZMM, VTInfo.info512>,
             avx512_shift_rmbi<opc, ImmFormM, OpcodeStr, OpNode, sched.ZMM,
                               VTInfo.info512>, EVEX_V512;
  let Predicates = [HasAVX512, HasVLX] in {
  defm Z256: avx512_shift_rmi<opc, ImmFormR, ImmFormM, OpcodeStr, OpNode,
                              sched.YMM, VTInfo.info256>,
             avx512_shift_rmbi<opc, ImmFormM, OpcodeStr, OpNode, sched.YMM,
                               VTInfo.info256>, EVEX_V256;
  defm Z128: avx512_shift_rmi<opc, ImmFormR, ImmFormM, OpcodeStr, OpNode,
                              sched.XMM, VTInfo.info128>,
             avx512_shift_rmbi<opc, ImmFormM, OpcodeStr, OpNode, sched.XMM,
                               VTInfo.info128>, EVEX_V128;
  }
}

multiclass avx512_shift_rmi_w<bits<8> opcw, Format ImmFormR, Format ImmFormM,
                              string OpcodeStr, SDNode OpNode,
                              X86SchedWriteWidths sched> {
  let Predicates = [HasBWI] in
  defm WZ:    avx512_shift_rmi<opcw, ImmFormR, ImmFormM, OpcodeStr, OpNode,
                               sched.ZMM, v32i16_info>, EVEX_V512, WIG;
  let Predicates = [HasVLX, HasBWI] in {
  defm WZ256: avx512_shift_rmi<opcw, ImmFormR, ImmFormM, OpcodeStr, OpNode,
                               sched.YMM, v16i16x_info>, EVEX_V256, WIG;
  defm WZ128: avx512_shift_rmi<opcw, ImmFormR, ImmFormM, OpcodeStr, OpNode,
                               sched.XMM, v8i16x_info>, EVEX_V128, WIG;
  }
}

multiclass avx512_shift_rmi_dq<bits<8> opcd, bits<8> opcq,
                               Format ImmFormR, Format ImmFormM,
                               string OpcodeStr, SDNode OpNode,
                               X86SchedWriteWidths sched> {
  defm D: avx512_shift_rmi_sizes<opcd, ImmFormR, ImmFormM, OpcodeStr#"d", OpNode,
                                 sched, avx512vl_i32_info>, EVEX_CD8<32, CD8VF>;
  defm Q: avx512_shift_rmi_sizes<opcq, ImmFormR, ImmFormM, OpcodeStr#"q", OpNode,
                                 sched, avx512vl_i64_info>, EVEX_CD8<64, CD8VF>, REX_W;
}

defm VPSRL : avx512_shift_rmi_dq<0x72, 0x73, MRM2r, MRM2m, "vpsrl", X86vsrli,
                                 SchedWriteVecShiftImm>,
             avx512_shift_rmi_w<0x71, MRM2r, MRM2m, "vpsrlw", X86vsrli,
                                SchedWriteVecShiftImm>, AVX512BIi8Base, EVEX, VVVV;

defm VPSLL : avx512_shift_rmi_dq<0x72, 0x73, MRM6r, MRM6m, "vpsll", X86vshli,
                                 SchedWriteVecShiftImm>,
             avx512_shift_rmi_w<0x71, MRM6r, MRM6m, "vpsllw", X86vshli,
                                SchedWriteVecShiftImm>, AVX512BIi8Base, EVEX, VVVV;

defm VPSRA : avx512_shift_rmi_dq<0x72, 0x72, MRM4r, MRM4m, "vpsra", X86vsrai,
                                 SchedWriteVecShiftImm>,
             avx512_shift_rmi_w<0x71, MRM4r, MRM4m, "vpsraw", X86vsrai,
                                SchedWriteVecShiftImm>, AVX512BIi8Base, EVEX, VVVV;

defm VPROR : avx512_shift_rmi_dq<0x72, 0x72, MRM0r, MRM0m, "vpror", X86vrotri,
                                 SchedWriteVecShiftImm>, AVX512BIi8Base, EVEX, VVVV;
defm VPROL : avx512_shift_rmi_dq<0x72, 0x72, MRM1r, MRM1m, "vprol", X86vrotli,
                                 SchedWriteVecShiftImm>, AVX512BIi8Base, EVEX, VVVV;

defm VPSLL : avx512_shift_types<0xF2, 0xF3, 0xF1, "vpsll", X86vshl,
                                SchedWriteVecShift>;
defm VPSRA : avx512_shift_types<0xE2, 0xE2, 0xE1, "vpsra", X86vsra,
                                SchedWriteVecShift>;
defm VPSRL : avx512_shift_types<0xD2, 0xD3, 0xD1, "vpsrl", X86vsrl,
                                SchedWriteVecShift>;

// Use 512bit VPSRA/VPSRAI version to implement v2i64/v4i64 in case NoVLX.
let Predicates = [HasAVX512, NoVLX, HasEVEX512] in {
  def : Pat<(v4i64 (X86vsra (v4i64 VR256X:$src1), (v2i64 VR128X:$src2))),
            (EXTRACT_SUBREG (v8i64
              (VPSRAQZrr
                (v8i64 (INSERT_SUBREG (IMPLICIT_DEF), VR256X:$src1, sub_ymm)),
                 VR128X:$src2)), sub_ymm)>;

  def : Pat<(v2i64 (X86vsra (v2i64 VR128X:$src1), (v2i64 VR128X:$src2))),
            (EXTRACT_SUBREG (v8i64
              (VPSRAQZrr
                (v8i64 (INSERT_SUBREG (IMPLICIT_DEF), VR128X:$src1, sub_xmm)),
                 VR128X:$src2)), sub_xmm)>;

  def : Pat<(v4i64 (X86vsrai (v4i64 VR256X:$src1), (i8 timm:$src2))),
            (EXTRACT_SUBREG (v8i64
              (VPSRAQZri
                (v8i64 (INSERT_SUBREG (IMPLICIT_DEF), VR256X:$src1, sub_ymm)),
                 timm:$src2)), sub_ymm)>;

  def : Pat<(v2i64 (X86vsrai (v2i64 VR128X:$src1), (i8 timm:$src2))),
            (EXTRACT_SUBREG (v8i64
              (VPSRAQZri
                (v8i64 (INSERT_SUBREG (IMPLICIT_DEF), VR128X:$src1, sub_xmm)),
                 timm:$src2)), sub_xmm)>;
}

//===-------------------------------------------------------------------===//
// Variable Bit Shifts
//===-------------------------------------------------------------------===//

multiclass avx512_var_shift<bits<8> opc, string OpcodeStr, SDNode OpNode,
                            X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain in {
  defm rr : AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                   (ins _.RC:$src1, _.RC:$src2), OpcodeStr,
                      "$src2, $src1", "$src1, $src2",
                   (_.VT (OpNode _.RC:$src1, (_.VT _.RC:$src2)))>,
                   AVX5128IBase, EVEX, VVVV, Sched<[sched]>;
  defm rm : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                   (ins _.RC:$src1, _.MemOp:$src2), OpcodeStr,
                       "$src2, $src1", "$src1, $src2",
                   (_.VT (OpNode _.RC:$src1,
                   (_.VT (_.LdFrag addr:$src2))))>,
                   AVX5128IBase, EVEX, VVVV, EVEX_CD8<_.EltSize, CD8VF>,
                   Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

multiclass avx512_var_shift_mb<bits<8> opc, string OpcodeStr, SDNode OpNode,
                               X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain in
  defm rmb : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                    (ins _.RC:$src1, _.ScalarMemOp:$src2), OpcodeStr,
                    "${src2}"#_.BroadcastStr#", $src1",
                    "$src1, ${src2}"#_.BroadcastStr,
                    (_.VT (OpNode _.RC:$src1, (_.VT (_.BroadcastLdFrag addr:$src2))))>,
                    AVX5128IBase, EVEX_B, EVEX, VVVV, EVEX_CD8<_.EltSize, CD8VF>,
                    Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass avx512_var_shift_sizes<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                  X86SchedWriteWidths sched, AVX512VLVectorVTInfo _> {
  let Predicates  = [HasAVX512] in
  defm Z : avx512_var_shift<opc, OpcodeStr, OpNode, sched.ZMM, _.info512>,
           avx512_var_shift_mb<opc, OpcodeStr, OpNode, sched.ZMM, _.info512>, EVEX_V512;

  let Predicates = [HasAVX512, HasVLX] in {
  defm Z256 : avx512_var_shift<opc, OpcodeStr, OpNode, sched.YMM, _.info256>,
              avx512_var_shift_mb<opc, OpcodeStr, OpNode, sched.YMM, _.info256>, EVEX_V256;
  defm Z128 : avx512_var_shift<opc, OpcodeStr, OpNode, sched.XMM, _.info128>,
              avx512_var_shift_mb<opc, OpcodeStr, OpNode, sched.XMM, _.info128>, EVEX_V128;
  }
}

multiclass avx512_var_shift_types<bits<8> opc, string OpcodeStr,
                                  SDNode OpNode, X86SchedWriteWidths sched> {
  defm D : avx512_var_shift_sizes<opc, OpcodeStr#"d", OpNode, sched,
                                 avx512vl_i32_info>;
  defm Q : avx512_var_shift_sizes<opc, OpcodeStr#"q", OpNode, sched,
                                 avx512vl_i64_info>, REX_W;
}

// Use 512bit version to implement 128/256 bit in case NoVLX.
multiclass avx512_var_shift_lowering<AVX512VLVectorVTInfo _, string OpcodeStr,
                                     SDNode OpNode, list<Predicate> p> {
  let Predicates = p in {
  def : Pat<(_.info256.VT (OpNode (_.info256.VT _.info256.RC:$src1),
                                  (_.info256.VT _.info256.RC:$src2))),
            (EXTRACT_SUBREG
                (!cast<Instruction>(OpcodeStr#"Zrr")
                    (INSERT_SUBREG (_.info512.VT (IMPLICIT_DEF)), VR256X:$src1, sub_ymm),
                    (INSERT_SUBREG (_.info512.VT (IMPLICIT_DEF)), VR256X:$src2, sub_ymm)),
             sub_ymm)>;

  def : Pat<(_.info128.VT (OpNode (_.info128.VT _.info128.RC:$src1),
                                  (_.info128.VT _.info128.RC:$src2))),
            (EXTRACT_SUBREG
                (!cast<Instruction>(OpcodeStr#"Zrr")
                    (INSERT_SUBREG (_.info512.VT (IMPLICIT_DEF)), VR128X:$src1, sub_xmm),
                    (INSERT_SUBREG (_.info512.VT (IMPLICIT_DEF)), VR128X:$src2, sub_xmm)),
             sub_xmm)>;
  }
}
multiclass avx512_var_shift_w<bits<8> opc, string OpcodeStr,
                              SDNode OpNode, X86SchedWriteWidths sched> {
  let Predicates = [HasBWI] in
  defm WZ:    avx512_var_shift<opc, OpcodeStr, OpNode, sched.ZMM, v32i16_info>,
              EVEX_V512, REX_W;
  let Predicates = [HasVLX, HasBWI] in {

  defm WZ256: avx512_var_shift<opc, OpcodeStr, OpNode, sched.YMM, v16i16x_info>,
              EVEX_V256, REX_W;
  defm WZ128: avx512_var_shift<opc, OpcodeStr, OpNode, sched.XMM, v8i16x_info>,
              EVEX_V128, REX_W;
  }
}

defm VPSLLV : avx512_var_shift_types<0x47, "vpsllv", X86vshlv, SchedWriteVarVecShift>,
              avx512_var_shift_w<0x12, "vpsllvw", X86vshlv, SchedWriteVarVecShift>;

defm VPSRAV : avx512_var_shift_types<0x46, "vpsrav", X86vsrav, SchedWriteVarVecShift>,
              avx512_var_shift_w<0x11, "vpsravw", X86vsrav, SchedWriteVarVecShift>;

defm VPSRLV : avx512_var_shift_types<0x45, "vpsrlv", X86vsrlv, SchedWriteVarVecShift>,
              avx512_var_shift_w<0x10, "vpsrlvw", X86vsrlv, SchedWriteVarVecShift>;

defm VPRORV : avx512_var_shift_types<0x14, "vprorv", rotr, SchedWriteVarVecShift>;
defm VPROLV : avx512_var_shift_types<0x15, "vprolv", rotl, SchedWriteVarVecShift>;

defm : avx512_var_shift_lowering<avx512vl_i64_info, "VPSRAVQ", X86vsrav, [HasAVX512, NoVLX, HasEVEX512]>;
defm : avx512_var_shift_lowering<avx512vl_i16_info, "VPSLLVW", X86vshlv, [HasBWI, NoVLX, HasEVEX512]>;
defm : avx512_var_shift_lowering<avx512vl_i16_info, "VPSRAVW", X86vsrav, [HasBWI, NoVLX, HasEVEX512]>;
defm : avx512_var_shift_lowering<avx512vl_i16_info, "VPSRLVW", X86vsrlv, [HasBWI, NoVLX, HasEVEX512]>;


// Use 512bit VPROL/VPROLI version to implement v2i64/v4i64 + v4i32/v8i32 in case NoVLX.
let Predicates = [HasAVX512, NoVLX, HasEVEX512] in {
  def : Pat<(v2i64 (rotl (v2i64 VR128X:$src1), (v2i64 VR128X:$src2))),
            (EXTRACT_SUBREG (v8i64
              (VPROLVQZrr
                (v8i64 (INSERT_SUBREG (IMPLICIT_DEF), VR128X:$src1, sub_xmm)),
                (v8i64 (INSERT_SUBREG (IMPLICIT_DEF), VR128X:$src2, sub_xmm)))),
                       sub_xmm)>;
  def : Pat<(v4i64 (rotl (v4i64 VR256X:$src1), (v4i64 VR256X:$src2))),
            (EXTRACT_SUBREG (v8i64
              (VPROLVQZrr
                (v8i64 (INSERT_SUBREG (IMPLICIT_DEF), VR256X:$src1, sub_ymm)),
                (v8i64 (INSERT_SUBREG (IMPLICIT_DEF), VR256X:$src2, sub_ymm)))),
                       sub_ymm)>;

  def : Pat<(v4i32 (rotl (v4i32 VR128X:$src1), (v4i32 VR128X:$src2))),
            (EXTRACT_SUBREG (v16i32
              (VPROLVDZrr
                (v16i32 (INSERT_SUBREG (IMPLICIT_DEF), VR128X:$src1, sub_xmm)),
                (v16i32 (INSERT_SUBREG (IMPLICIT_DEF), VR128X:$src2, sub_xmm)))),
                        sub_xmm)>;
  def : Pat<(v8i32 (rotl (v8i32 VR256X:$src1), (v8i32 VR256X:$src2))),
            (EXTRACT_SUBREG (v16i32
              (VPROLVDZrr
                (v16i32 (INSERT_SUBREG (IMPLICIT_DEF), VR256X:$src1, sub_ymm)),
                (v16i32 (INSERT_SUBREG (IMPLICIT_DEF), VR256X:$src2, sub_ymm)))),
                        sub_ymm)>;

  def : Pat<(v2i64 (X86vrotli (v2i64 VR128X:$src1), (i8 timm:$src2))),
            (EXTRACT_SUBREG (v8i64
              (VPROLQZri
                (v8i64 (INSERT_SUBREG (IMPLICIT_DEF), VR128X:$src1, sub_xmm)),
                        timm:$src2)), sub_xmm)>;
  def : Pat<(v4i64 (X86vrotli (v4i64 VR256X:$src1), (i8 timm:$src2))),
            (EXTRACT_SUBREG (v8i64
              (VPROLQZri
                (v8i64 (INSERT_SUBREG (IMPLICIT_DEF), VR256X:$src1, sub_ymm)),
                       timm:$src2)), sub_ymm)>;

  def : Pat<(v4i32 (X86vrotli (v4i32 VR128X:$src1), (i8 timm:$src2))),
            (EXTRACT_SUBREG (v16i32
              (VPROLDZri
                (v16i32 (INSERT_SUBREG (IMPLICIT_DEF), VR128X:$src1, sub_xmm)),
                        timm:$src2)), sub_xmm)>;
  def : Pat<(v8i32 (X86vrotli (v8i32 VR256X:$src1), (i8 timm:$src2))),
            (EXTRACT_SUBREG (v16i32
              (VPROLDZri
                (v16i32 (INSERT_SUBREG (IMPLICIT_DEF), VR256X:$src1, sub_ymm)),
                        timm:$src2)), sub_ymm)>;
}

// Use 512bit VPROR/VPRORI version to implement v2i64/v4i64 + v4i32/v8i32 in case NoVLX.
let Predicates = [HasAVX512, NoVLX, HasEVEX512] in {
  def : Pat<(v2i64 (rotr (v2i64 VR128X:$src1), (v2i64 VR128X:$src2))),
            (EXTRACT_SUBREG (v8i64
              (VPRORVQZrr
                (v8i64 (INSERT_SUBREG (IMPLICIT_DEF), VR128X:$src1, sub_xmm)),
                (v8i64 (INSERT_SUBREG (IMPLICIT_DEF), VR128X:$src2, sub_xmm)))),
                       sub_xmm)>;
  def : Pat<(v4i64 (rotr (v4i64 VR256X:$src1), (v4i64 VR256X:$src2))),
            (EXTRACT_SUBREG (v8i64
              (VPRORVQZrr
                (v8i64 (INSERT_SUBREG (IMPLICIT_DEF), VR256X:$src1, sub_ymm)),
                (v8i64 (INSERT_SUBREG (IMPLICIT_DEF), VR256X:$src2, sub_ymm)))),
                       sub_ymm)>;

  def : Pat<(v4i32 (rotr (v4i32 VR128X:$src1), (v4i32 VR128X:$src2))),
            (EXTRACT_SUBREG (v16i32
              (VPRORVDZrr
                (v16i32 (INSERT_SUBREG (IMPLICIT_DEF), VR128X:$src1, sub_xmm)),
                (v16i32 (INSERT_SUBREG (IMPLICIT_DEF), VR128X:$src2, sub_xmm)))),
                        sub_xmm)>;
  def : Pat<(v8i32 (rotr (v8i32 VR256X:$src1), (v8i32 VR256X:$src2))),
            (EXTRACT_SUBREG (v16i32
              (VPRORVDZrr
                (v16i32 (INSERT_SUBREG (IMPLICIT_DEF), VR256X:$src1, sub_ymm)),
                (v16i32 (INSERT_SUBREG (IMPLICIT_DEF), VR256X:$src2, sub_ymm)))),
                        sub_ymm)>;

  def : Pat<(v2i64 (X86vrotri (v2i64 VR128X:$src1), (i8 timm:$src2))),
            (EXTRACT_SUBREG (v8i64
              (VPRORQZri
                (v8i64 (INSERT_SUBREG (IMPLICIT_DEF), VR128X:$src1, sub_xmm)),
                        timm:$src2)), sub_xmm)>;
  def : Pat<(v4i64 (X86vrotri (v4i64 VR256X:$src1), (i8 timm:$src2))),
            (EXTRACT_SUBREG (v8i64
              (VPRORQZri
                (v8i64 (INSERT_SUBREG (IMPLICIT_DEF), VR256X:$src1, sub_ymm)),
                       timm:$src2)), sub_ymm)>;

  def : Pat<(v4i32 (X86vrotri (v4i32 VR128X:$src1), (i8 timm:$src2))),
            (EXTRACT_SUBREG (v16i32
              (VPRORDZri
                (v16i32 (INSERT_SUBREG (IMPLICIT_DEF), VR128X:$src1, sub_xmm)),
                        timm:$src2)), sub_xmm)>;
  def : Pat<(v8i32 (X86vrotri (v8i32 VR256X:$src1), (i8 timm:$src2))),
            (EXTRACT_SUBREG (v16i32
              (VPRORDZri
                (v16i32 (INSERT_SUBREG (IMPLICIT_DEF), VR256X:$src1, sub_ymm)),
                        timm:$src2)), sub_ymm)>;
}

//===-------------------------------------------------------------------===//
// 1-src variable permutation VPERMW/D/Q
//===-------------------------------------------------------------------===//

multiclass avx512_vperm_dq_sizes<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                 X86FoldableSchedWrite sched, AVX512VLVectorVTInfo _> {
  let Predicates  = [HasAVX512] in
  defm Z : avx512_var_shift<opc, OpcodeStr, OpNode, sched, _.info512>,
           avx512_var_shift_mb<opc, OpcodeStr, OpNode, sched, _.info512>, EVEX_V512;

  let Predicates = [HasAVX512, HasVLX] in
  defm Z256 : avx512_var_shift<opc, OpcodeStr, OpNode, sched, _.info256>,
              avx512_var_shift_mb<opc, OpcodeStr, OpNode, sched, _.info256>, EVEX_V256;
}

multiclass avx512_vpermi_dq_sizes<bits<8> opc, Format ImmFormR, Format ImmFormM,
                                 string OpcodeStr, SDNode OpNode,
                                 X86FoldableSchedWrite sched, AVX512VLVectorVTInfo VTInfo> {
  let Predicates = [HasAVX512] in
  defm Z:    avx512_shift_rmi<opc, ImmFormR, ImmFormM, OpcodeStr, OpNode,
                              sched, VTInfo.info512>,
             avx512_shift_rmbi<opc, ImmFormM, OpcodeStr, OpNode,
                               sched, VTInfo.info512>, EVEX_V512;
  let Predicates = [HasAVX512, HasVLX] in
  defm Z256: avx512_shift_rmi<opc, ImmFormR, ImmFormM, OpcodeStr, OpNode,
                              sched, VTInfo.info256>,
             avx512_shift_rmbi<opc, ImmFormM, OpcodeStr, OpNode,
                               sched, VTInfo.info256>, EVEX_V256;
}

multiclass avx512_vperm_bw<bits<8> opc, string OpcodeStr,
                              Predicate prd, SDNode OpNode,
                              X86FoldableSchedWrite sched, AVX512VLVectorVTInfo _> {
  let Predicates = [prd] in
  defm Z:    avx512_var_shift<opc, OpcodeStr, OpNode, sched, _.info512>,
              EVEX_V512 ;
  let Predicates = [HasVLX, prd] in {
  defm Z256: avx512_var_shift<opc, OpcodeStr, OpNode, sched, _.info256>,
              EVEX_V256 ;
  defm Z128: avx512_var_shift<opc, OpcodeStr, OpNode, sched, _.info128>,
              EVEX_V128 ;
  }
}

defm VPERMW  : avx512_vperm_bw<0x8D, "vpermw", HasBWI, X86VPermv,
                               WriteVarShuffle256, avx512vl_i16_info>, REX_W;
defm VPERMB  : avx512_vperm_bw<0x8D, "vpermb", HasVBMI, X86VPermv,
                               WriteVarShuffle256, avx512vl_i8_info>;

defm VPERMD : avx512_vperm_dq_sizes<0x36, "vpermd", X86VPermv,
                                    WriteVarShuffle256, avx512vl_i32_info>;
defm VPERMQ : avx512_vperm_dq_sizes<0x36, "vpermq", X86VPermv,
                                    WriteVarShuffle256, avx512vl_i64_info>, REX_W;
defm VPERMPS : avx512_vperm_dq_sizes<0x16, "vpermps", X86VPermv,
                                     WriteFVarShuffle256, avx512vl_f32_info>;
defm VPERMPD : avx512_vperm_dq_sizes<0x16, "vpermpd", X86VPermv,
                                     WriteFVarShuffle256, avx512vl_f64_info>, REX_W;

defm VPERMQ : avx512_vpermi_dq_sizes<0x00, MRMSrcReg, MRMSrcMem, "vpermq",
                             X86VPermi, WriteShuffle256, avx512vl_i64_info>,
                             EVEX, AVX512AIi8Base, EVEX_CD8<64, CD8VF>, REX_W;
defm VPERMPD : avx512_vpermi_dq_sizes<0x01, MRMSrcReg, MRMSrcMem, "vpermpd",
                             X86VPermi, WriteFShuffle256, avx512vl_f64_info>,
                             EVEX, AVX512AIi8Base, EVEX_CD8<64, CD8VF>, REX_W;

//===----------------------------------------------------------------------===//
// AVX-512 - VPERMIL
//===----------------------------------------------------------------------===//

multiclass avx512_permil_vec<bits<8> OpcVar, string OpcodeStr, SDNode OpNode,
                             X86FoldableSchedWrite sched, X86VectorVTInfo _,
                             X86VectorVTInfo Ctrl> {
  defm rr: AVX512_maskable<OpcVar, MRMSrcReg, _, (outs _.RC:$dst),
                  (ins _.RC:$src1, Ctrl.RC:$src2), OpcodeStr,
                  "$src2, $src1", "$src1, $src2",
                  (_.VT (OpNode _.RC:$src1,
                               (Ctrl.VT Ctrl.RC:$src2)))>,
                  T8, PD, EVEX, VVVV, Sched<[sched]>;
  defm rm: AVX512_maskable<OpcVar, MRMSrcMem, _, (outs _.RC:$dst),
                  (ins _.RC:$src1, Ctrl.MemOp:$src2), OpcodeStr,
                  "$src2, $src1", "$src1, $src2",
                  (_.VT (OpNode
                           _.RC:$src1,
                           (Ctrl.VT (Ctrl.LdFrag addr:$src2))))>,
                  T8, PD, EVEX, VVVV, EVEX_CD8<_.EltSize, CD8VF>,
                  Sched<[sched.Folded, sched.ReadAfterFold]>;
  defm rmb: AVX512_maskable<OpcVar, MRMSrcMem, _, (outs _.RC:$dst),
                   (ins _.RC:$src1, _.ScalarMemOp:$src2), OpcodeStr,
                   "${src2}"#_.BroadcastStr#", $src1",
                   "$src1, ${src2}"#_.BroadcastStr,
                   (_.VT (OpNode
                            _.RC:$src1,
                            (Ctrl.VT (Ctrl.BroadcastLdFrag addr:$src2))))>,
                   T8, PD, EVEX, VVVV, EVEX_B, EVEX_CD8<_.EltSize, CD8VF>,
                   Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass avx512_permil_vec_common<string OpcodeStr, bits<8> OpcVar,
                                    X86SchedWriteWidths sched,
                                    AVX512VLVectorVTInfo _,
                                    AVX512VLVectorVTInfo Ctrl> {
  let Predicates = [HasAVX512] in {
    defm Z    : avx512_permil_vec<OpcVar, OpcodeStr, X86VPermilpv, sched.ZMM,
                                  _.info512, Ctrl.info512>, EVEX_V512;
  }
  let Predicates = [HasAVX512, HasVLX] in {
    defm Z128 : avx512_permil_vec<OpcVar, OpcodeStr, X86VPermilpv, sched.XMM,
                                  _.info128, Ctrl.info128>, EVEX_V128;
    defm Z256 : avx512_permil_vec<OpcVar, OpcodeStr, X86VPermilpv, sched.YMM,
                                  _.info256, Ctrl.info256>, EVEX_V256;
  }
}

multiclass avx512_permil<string OpcodeStr, bits<8> OpcImm, bits<8> OpcVar,
                         AVX512VLVectorVTInfo _, AVX512VLVectorVTInfo Ctrl>{
  defm NAME: avx512_permil_vec_common<OpcodeStr, OpcVar, SchedWriteFVarShuffle,
                                      _, Ctrl>;
  defm NAME: avx512_shift_rmi_sizes<OpcImm, MRMSrcReg, MRMSrcMem, OpcodeStr,
                                    X86VPermilpi, SchedWriteFShuffle, _>,
                    EVEX, AVX512AIi8Base, EVEX_CD8<_.info128.EltSize, CD8VF>;
}

let ExeDomain = SSEPackedSingle in
defm VPERMILPS : avx512_permil<"vpermilps", 0x04, 0x0C, avx512vl_f32_info,
                               avx512vl_i32_info>;
let ExeDomain = SSEPackedDouble in
defm VPERMILPD : avx512_permil<"vpermilpd", 0x05, 0x0D, avx512vl_f64_info,
                               avx512vl_i64_info>, REX_W;

//===----------------------------------------------------------------------===//
// AVX-512 - VPSHUFD, VPSHUFLW, VPSHUFHW
//===----------------------------------------------------------------------===//

defm VPSHUFD : avx512_shift_rmi_sizes<0x70, MRMSrcReg, MRMSrcMem, "vpshufd",
                             X86PShufd, SchedWriteShuffle, avx512vl_i32_info>,
                             EVEX, AVX512BIi8Base, EVEX_CD8<32, CD8VF>;
defm VPSHUFH : avx512_shift_rmi_w<0x70, MRMSrcReg, MRMSrcMem, "vpshufhw",
                                  X86PShufhw, SchedWriteShuffle>,
                                  EVEX, AVX512XSIi8Base;
defm VPSHUFL : avx512_shift_rmi_w<0x70, MRMSrcReg, MRMSrcMem, "vpshuflw",
                                  X86PShuflw, SchedWriteShuffle>,
                                  EVEX, AVX512XDIi8Base;

//===----------------------------------------------------------------------===//
// AVX-512 - VPSHUFB
//===----------------------------------------------------------------------===//

multiclass avx512_pshufb_sizes<bits<8> opc, string OpcodeStr, SDNode OpNode,
                               X86SchedWriteWidths sched> {
  let Predicates = [HasBWI] in
  defm Z:    avx512_var_shift<opc, OpcodeStr, OpNode, sched.ZMM, v64i8_info>,
                              EVEX_V512;

  let Predicates = [HasVLX, HasBWI] in {
  defm Z256: avx512_var_shift<opc, OpcodeStr, OpNode, sched.YMM, v32i8x_info>,
                              EVEX_V256;
  defm Z128: avx512_var_shift<opc, OpcodeStr, OpNode, sched.XMM, v16i8x_info>,
                              EVEX_V128;
  }
}

defm VPSHUFB: avx512_pshufb_sizes<0x00, "vpshufb", X86pshufb,
                                  SchedWriteVarShuffle>, WIG;

//===----------------------------------------------------------------------===//
// Move Low to High and High to Low packed FP Instructions
//===----------------------------------------------------------------------===//

def VMOVLHPSZrr : AVX512PSI<0x16, MRMSrcReg, (outs VR128X:$dst),
          (ins VR128X:$src1, VR128X:$src2),
          "vmovlhps\t{$src2, $src1, $dst|$dst, $src1, $src2}",
          [(set VR128X:$dst, (v4f32 (X86Movlhps VR128X:$src1, VR128X:$src2)))]>,
          Sched<[SchedWriteFShuffle.XMM]>, EVEX, VVVV;
let isCommutable = 1 in
def VMOVHLPSZrr : AVX512PSI<0x12, MRMSrcReg, (outs VR128X:$dst),
          (ins VR128X:$src1, VR128X:$src2),
          "vmovhlps\t{$src2, $src1, $dst|$dst, $src1, $src2}",
          [(set VR128X:$dst, (v4f32 (X86Movhlps VR128X:$src1, VR128X:$src2)))]>,
          Sched<[SchedWriteFShuffle.XMM]>, EVEX, VVVV;

//===----------------------------------------------------------------------===//
// VMOVHPS/PD VMOVLPS Instructions
// All patterns was taken from SSS implementation.
//===----------------------------------------------------------------------===//

multiclass avx512_mov_hilo_packed<bits<8> opc, string OpcodeStr,
                                  SDPatternOperator OpNode,
                                  X86VectorVTInfo _> {
  let hasSideEffects = 0, mayLoad = 1, ExeDomain = _.ExeDomain in
  def rm : AVX512<opc, MRMSrcMem, (outs _.RC:$dst),
                  (ins _.RC:$src1, f64mem:$src2),
                  !strconcat(OpcodeStr,
                             "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
                  [(set _.RC:$dst,
                     (OpNode _.RC:$src1,
                       (_.VT (bitconvert
                         (v2f64 (scalar_to_vector (loadf64 addr:$src2)))))))]>,
                  Sched<[SchedWriteFShuffle.XMM.Folded, SchedWriteFShuffle.XMM.ReadAfterFold]>, EVEX, VVVV;
}

// No patterns for MOVLPS/MOVHPS as the Movlhps node should only be created in
// SSE1. And MOVLPS pattern is even more complex.
defm VMOVHPSZ128 : avx512_mov_hilo_packed<0x16, "vmovhps", null_frag,
                                  v4f32x_info>, EVEX_CD8<32, CD8VT2>, TB;
defm VMOVHPDZ128 : avx512_mov_hilo_packed<0x16, "vmovhpd", X86Unpckl,
                                  v2f64x_info>, EVEX_CD8<64, CD8VT1>, TB, PD, REX_W;
defm VMOVLPSZ128 : avx512_mov_hilo_packed<0x12, "vmovlps", null_frag,
                                  v4f32x_info>, EVEX_CD8<32, CD8VT2>, TB;
defm VMOVLPDZ128 : avx512_mov_hilo_packed<0x12, "vmovlpd", X86Movsd,
                                  v2f64x_info>, EVEX_CD8<64, CD8VT1>, TB, PD, REX_W;

let Predicates = [HasAVX512] in {
  // VMOVHPD patterns
  def : Pat<(v2f64 (X86Unpckl VR128X:$src1, (X86vzload64 addr:$src2))),
            (VMOVHPDZ128rm VR128X:$src1, addr:$src2)>;

  // VMOVLPD patterns
  def : Pat<(v2f64 (X86Movsd VR128X:$src1, (X86vzload64 addr:$src2))),
            (VMOVLPDZ128rm VR128X:$src1, addr:$src2)>;
}

let SchedRW = [WriteFStore] in {
let mayStore = 1, hasSideEffects = 0 in
def VMOVHPSZ128mr : AVX512PSI<0x17, MRMDestMem, (outs),
                       (ins f64mem:$dst, VR128X:$src),
                       "vmovhps\t{$src, $dst|$dst, $src}",
                       []>, EVEX, EVEX_CD8<32, CD8VT2>;
def VMOVHPDZ128mr : AVX512PDI<0x17, MRMDestMem, (outs),
                       (ins f64mem:$dst, VR128X:$src),
                       "vmovhpd\t{$src, $dst|$dst, $src}",
                       [(store (f64 (extractelt
                                     (v2f64 (X86Unpckh VR128X:$src, VR128X:$src)),
                                     (iPTR 0))), addr:$dst)]>,
                       EVEX, EVEX_CD8<64, CD8VT1>, REX_W;
let mayStore = 1, hasSideEffects = 0 in
def VMOVLPSZ128mr : AVX512PSI<0x13, MRMDestMem, (outs),
                       (ins f64mem:$dst, VR128X:$src),
                       "vmovlps\t{$src, $dst|$dst, $src}",
                       []>, EVEX, EVEX_CD8<32, CD8VT2>;
def VMOVLPDZ128mr : AVX512PDI<0x13, MRMDestMem, (outs),
                       (ins f64mem:$dst, VR128X:$src),
                       "vmovlpd\t{$src, $dst|$dst, $src}",
                       [(store (f64 (extractelt (v2f64 VR128X:$src),
                                     (iPTR 0))), addr:$dst)]>,
                       EVEX, EVEX_CD8<64, CD8VT1>, REX_W;
} // SchedRW

let Predicates = [HasAVX512] in {
  // VMOVHPD patterns
  def : Pat<(store (f64 (extractelt
                           (v2f64 (X86VPermilpi VR128X:$src, (i8 1))),
                           (iPTR 0))), addr:$dst),
           (VMOVHPDZ128mr addr:$dst, VR128X:$src)>;
}
//===----------------------------------------------------------------------===//
// FMA - Fused Multiply Operations
//

multiclass avx512_fma3p_213_rm<bits<8> opc, string OpcodeStr, SDPatternOperator OpNode,
                               SDNode MaskOpNode, X86FoldableSchedWrite sched,
                               X86VectorVTInfo _> {
  let Constraints = "$src1 = $dst", ExeDomain = _.ExeDomain, hasSideEffects = 0,
      Uses = [MXCSR], mayRaiseFPException = 1 in {
  defm r: AVX512_maskable_fma<opc, MRMSrcReg, _, (outs _.RC:$dst),
          (ins _.RC:$src2, _.RC:$src3),
          OpcodeStr, "$src3, $src2", "$src2, $src3",
          (_.VT (OpNode _.RC:$src2, _.RC:$src1, _.RC:$src3)),
          (_.VT (MaskOpNode _.RC:$src2, _.RC:$src1, _.RC:$src3)), 1, 1>,
          EVEX, VVVV, Sched<[sched]>;

  defm m: AVX512_maskable_fma<opc, MRMSrcMem, _, (outs _.RC:$dst),
          (ins _.RC:$src2, _.MemOp:$src3),
          OpcodeStr, "$src3, $src2", "$src2, $src3",
          (_.VT (OpNode _.RC:$src2, _.RC:$src1, (_.LdFrag addr:$src3))),
          (_.VT (MaskOpNode _.RC:$src2, _.RC:$src1, (_.LdFrag addr:$src3))), 1, 0>,
          EVEX, VVVV, Sched<[sched.Folded, sched.ReadAfterFold,
                          sched.ReadAfterFold]>;

  defm mb: AVX512_maskable_fma<opc, MRMSrcMem, _, (outs _.RC:$dst),
            (ins _.RC:$src2, _.ScalarMemOp:$src3),
            OpcodeStr,   !strconcat("${src3}", _.BroadcastStr,", $src2"),
            !strconcat("$src2, ${src3}", _.BroadcastStr ),
            (OpNode _.RC:$src2,
             _.RC:$src1,(_.VT (_.BroadcastLdFrag addr:$src3))),
            (MaskOpNode _.RC:$src2,
             _.RC:$src1,(_.VT (_.BroadcastLdFrag addr:$src3))), 1, 0>,
            EVEX, VVVV, EVEX_B, Sched<[sched.Folded, sched.ReadAfterFold,
                                    sched.ReadAfterFold]>;
  }
}

multiclass avx512_fma3_213_round<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                 X86FoldableSchedWrite sched,
                                 X86VectorVTInfo _> {
  let Constraints = "$src1 = $dst", ExeDomain = _.ExeDomain, hasSideEffects = 0,
      Uses = [MXCSR] in
  defm rb: AVX512_maskable_fma<opc, MRMSrcReg, _, (outs _.RC:$dst),
          (ins _.RC:$src2, _.RC:$src3, AVX512RC:$rc),
          OpcodeStr, "$rc, $src3, $src2", "$src2, $src3, $rc",
          (_.VT ( OpNode _.RC:$src2, _.RC:$src1, _.RC:$src3, (i32 timm:$rc))),
          (_.VT ( OpNode _.RC:$src2, _.RC:$src1, _.RC:$src3, (i32 timm:$rc))), 1, 1>,
          EVEX, VVVV, EVEX_B, EVEX_RC, Sched<[sched]>;
}

multiclass avx512_fma3p_213_common<bits<8> opc, string OpcodeStr, SDPatternOperator OpNode,
                                   SDNode MaskOpNode, SDNode OpNodeRnd,
                                   X86SchedWriteWidths sched,
                                   AVX512VLVectorVTInfo _,
                                   Predicate prd = HasAVX512> {
  let Predicates = [prd] in {
    defm Z      : avx512_fma3p_213_rm<opc, OpcodeStr, OpNode, MaskOpNode,
                                      sched.ZMM, _.info512>,
                  avx512_fma3_213_round<opc, OpcodeStr, OpNodeRnd, sched.ZMM,
                                        _.info512>,
                              EVEX_V512, EVEX_CD8<_.info512.EltSize, CD8VF>;
  }
  let Predicates = [HasVLX, prd] in {
    defm Z256 : avx512_fma3p_213_rm<opc, OpcodeStr, OpNode, MaskOpNode,
                                    sched.YMM, _.info256>,
                      EVEX_V256, EVEX_CD8<_.info256.EltSize, CD8VF>;
    defm Z128 : avx512_fma3p_213_rm<opc, OpcodeStr, OpNode, MaskOpNode,
                                    sched.XMM, _.info128>,
                      EVEX_V128, EVEX_CD8<_.info128.EltSize, CD8VF>;
  }
}

multiclass avx512_fma3p_213_f<bits<8> opc, string OpcodeStr, SDPatternOperator OpNode,
                              SDNode MaskOpNode, SDNode OpNodeRnd> {
    defm PH : avx512_fma3p_213_common<opc, OpcodeStr#"ph", OpNode, MaskOpNode,
                                      OpNodeRnd, SchedWriteFMA,
                                      avx512vl_f16_info, HasFP16>, T_MAP6, PD;
    defm PS : avx512_fma3p_213_common<opc, OpcodeStr#"ps", OpNode, MaskOpNode,
                                      OpNodeRnd, SchedWriteFMA,
                                      avx512vl_f32_info>, T8, PD;
    defm PD : avx512_fma3p_213_common<opc, OpcodeStr#"pd", OpNode, MaskOpNode,
                                      OpNodeRnd, SchedWriteFMA,
                                      avx512vl_f64_info>, T8, PD, REX_W;
}

defm VFMADD213    : avx512_fma3p_213_f<0xA8, "vfmadd213", any_fma,
                                       fma, X86FmaddRnd>;
defm VFMSUB213    : avx512_fma3p_213_f<0xAA, "vfmsub213", X86any_Fmsub,
                                       X86Fmsub, X86FmsubRnd>;
defm VFMADDSUB213 : avx512_fma3p_213_f<0xA6, "vfmaddsub213", X86Fmaddsub,
                                       X86Fmaddsub, X86FmaddsubRnd>;
defm VFMSUBADD213 : avx512_fma3p_213_f<0xA7, "vfmsubadd213", X86Fmsubadd,
                                       X86Fmsubadd, X86FmsubaddRnd>;
defm VFNMADD213   : avx512_fma3p_213_f<0xAC, "vfnmadd213", X86any_Fnmadd,
                                       X86Fnmadd, X86FnmaddRnd>;
defm VFNMSUB213   : avx512_fma3p_213_f<0xAE, "vfnmsub213", X86any_Fnmsub,
                                       X86Fnmsub, X86FnmsubRnd>;


multiclass avx512_fma3p_231_rm<bits<8> opc, string OpcodeStr, SDPatternOperator OpNode,
                               SDNode MaskOpNode, X86FoldableSchedWrite sched,
                               X86VectorVTInfo _> {
  let Constraints = "$src1 = $dst", ExeDomain = _.ExeDomain, hasSideEffects = 0,
      Uses = [MXCSR], mayRaiseFPException = 1 in {
  defm r: AVX512_maskable_fma<opc, MRMSrcReg, _, (outs _.RC:$dst),
          (ins _.RC:$src2, _.RC:$src3),
          OpcodeStr, "$src3, $src2", "$src2, $src3",
          (null_frag),
          (_.VT (MaskOpNode _.RC:$src2, _.RC:$src3, _.RC:$src1)), 1, 1>,
          EVEX, VVVV, Sched<[sched]>;

  defm m: AVX512_maskable_fma<opc, MRMSrcMem, _, (outs _.RC:$dst),
          (ins _.RC:$src2, _.MemOp:$src3),
          OpcodeStr, "$src3, $src2", "$src2, $src3",
          (_.VT (OpNode _.RC:$src2, (_.LdFrag addr:$src3), _.RC:$src1)),
          (_.VT (MaskOpNode _.RC:$src2, (_.LdFrag addr:$src3), _.RC:$src1)), 1, 0>,
          EVEX, VVVV, Sched<[sched.Folded, sched.ReadAfterFold,
                          sched.ReadAfterFold]>;

  defm mb: AVX512_maskable_fma<opc, MRMSrcMem, _, (outs _.RC:$dst),
         (ins _.RC:$src2, _.ScalarMemOp:$src3),
         OpcodeStr, "${src3}"#_.BroadcastStr#", $src2",
         "$src2, ${src3}"#_.BroadcastStr,
         (_.VT (OpNode _.RC:$src2,
                      (_.VT (_.BroadcastLdFrag addr:$src3)),
                      _.RC:$src1)),
         (_.VT (MaskOpNode _.RC:$src2,
                           (_.VT (_.BroadcastLdFrag addr:$src3)),
                           _.RC:$src1)), 1, 0>, EVEX, VVVV, EVEX_B,
         Sched<[sched.Folded, sched.ReadAfterFold,
                sched.ReadAfterFold]>;
  }
}

multiclass avx512_fma3_231_round<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                 X86FoldableSchedWrite sched,
                                 X86VectorVTInfo _> {
  let Constraints = "$src1 = $dst", ExeDomain = _.ExeDomain, hasSideEffects = 0,
      Uses = [MXCSR] in
  defm rb: AVX512_maskable_fma<opc, MRMSrcReg, _, (outs _.RC:$dst),
          (ins _.RC:$src2, _.RC:$src3, AVX512RC:$rc),
          OpcodeStr, "$rc, $src3, $src2", "$src2, $src3, $rc",
          (null_frag),
          (_.VT (OpNode _.RC:$src2, _.RC:$src3, _.RC:$src1, (i32 timm:$rc))),
          1, 1>, EVEX, VVVV, EVEX_B, EVEX_RC, Sched<[sched]>;
}

multiclass avx512_fma3p_231_common<bits<8> opc, string OpcodeStr, SDPatternOperator OpNode,
                                   SDNode MaskOpNode, SDNode OpNodeRnd,
                                   X86SchedWriteWidths sched,
                                   AVX512VLVectorVTInfo _,
                                   Predicate prd = HasAVX512> {
  let Predicates = [prd] in {
    defm Z      : avx512_fma3p_231_rm<opc, OpcodeStr, OpNode, MaskOpNode,
                                      sched.ZMM, _.info512>,
                  avx512_fma3_231_round<opc, OpcodeStr, OpNodeRnd, sched.ZMM,
                                        _.info512>,
                              EVEX_V512, EVEX_CD8<_.info512.EltSize, CD8VF>;
  }
  let Predicates = [HasVLX, prd] in {
    defm Z256 : avx512_fma3p_231_rm<opc, OpcodeStr, OpNode, MaskOpNode,
                                    sched.YMM, _.info256>,
                      EVEX_V256, EVEX_CD8<_.info256.EltSize, CD8VF>;
    defm Z128 : avx512_fma3p_231_rm<opc, OpcodeStr, OpNode, MaskOpNode,
                                    sched.XMM, _.info128>,
                      EVEX_V128, EVEX_CD8<_.info128.EltSize, CD8VF>;
  }
}

multiclass avx512_fma3p_231_f<bits<8> opc, string OpcodeStr, SDPatternOperator OpNode,
                              SDNode MaskOpNode, SDNode OpNodeRnd > {
    defm PH : avx512_fma3p_231_common<opc, OpcodeStr#"ph", OpNode, MaskOpNode,
                                      OpNodeRnd, SchedWriteFMA,
                                      avx512vl_f16_info, HasFP16>, T_MAP6, PD;
    defm PS : avx512_fma3p_231_common<opc, OpcodeStr#"ps", OpNode, MaskOpNode,
                                      OpNodeRnd, SchedWriteFMA,
                                      avx512vl_f32_info>, T8, PD;
    defm PD : avx512_fma3p_231_common<opc, OpcodeStr#"pd", OpNode, MaskOpNode,
                                      OpNodeRnd, SchedWriteFMA,
                                      avx512vl_f64_info>, T8, PD, REX_W;
}

defm VFMADD231    : avx512_fma3p_231_f<0xB8, "vfmadd231", any_fma,
                                       fma, X86FmaddRnd>;
defm VFMSUB231    : avx512_fma3p_231_f<0xBA, "vfmsub231", X86any_Fmsub,
                                       X86Fmsub, X86FmsubRnd>;
defm VFMADDSUB231 : avx512_fma3p_231_f<0xB6, "vfmaddsub231", X86Fmaddsub,
                                       X86Fmaddsub, X86FmaddsubRnd>;
defm VFMSUBADD231 : avx512_fma3p_231_f<0xB7, "vfmsubadd231", X86Fmsubadd,
                                       X86Fmsubadd, X86FmsubaddRnd>;
defm VFNMADD231   : avx512_fma3p_231_f<0xBC, "vfnmadd231", X86any_Fnmadd,
                                       X86Fnmadd, X86FnmaddRnd>;
defm VFNMSUB231   : avx512_fma3p_231_f<0xBE, "vfnmsub231", X86any_Fnmsub,
                                       X86Fnmsub, X86FnmsubRnd>;

multiclass avx512_fma3p_132_rm<bits<8> opc, string OpcodeStr, SDPatternOperator OpNode,
                               SDNode MaskOpNode, X86FoldableSchedWrite sched,
                               X86VectorVTInfo _> {
  let Constraints = "$src1 = $dst", ExeDomain = _.ExeDomain, hasSideEffects = 0,
      Uses = [MXCSR], mayRaiseFPException = 1 in {
  defm r: AVX512_maskable_fma<opc, MRMSrcReg, _, (outs _.RC:$dst),
          (ins _.RC:$src2, _.RC:$src3),
          OpcodeStr, "$src3, $src2", "$src2, $src3",
          (null_frag),
          (_.VT (MaskOpNode _.RC:$src1, _.RC:$src3, _.RC:$src2)), 1, 1>,
          EVEX, VVVV, Sched<[sched]>;

  // Pattern is 312 order so that the load is in a different place from the
  // 213 and 231 patterns this helps tablegen's duplicate pattern detection.
  defm m: AVX512_maskable_fma<opc, MRMSrcMem, _, (outs _.RC:$dst),
          (ins _.RC:$src2, _.MemOp:$src3),
          OpcodeStr, "$src3, $src2", "$src2, $src3",
          (_.VT (OpNode (_.LdFrag addr:$src3), _.RC:$src1, _.RC:$src2)),
          (_.VT (MaskOpNode (_.LdFrag addr:$src3), _.RC:$src1, _.RC:$src2)), 1, 0>,
          EVEX, VVVV, Sched<[sched.Folded, sched.ReadAfterFold,
                          sched.ReadAfterFold]>;

  // Pattern is 312 order so that the load is in a different place from the
  // 213 and 231 patterns this helps tablegen's duplicate pattern detection.
  defm mb: AVX512_maskable_fma<opc, MRMSrcMem, _, (outs _.RC:$dst),
         (ins _.RC:$src2, _.ScalarMemOp:$src3),
         OpcodeStr, "${src3}"#_.BroadcastStr#", $src2",
         "$src2, ${src3}"#_.BroadcastStr,
         (_.VT (OpNode (_.VT (_.BroadcastLdFrag addr:$src3)),
                       _.RC:$src1, _.RC:$src2)),
         (_.VT (MaskOpNode (_.VT (_.BroadcastLdFrag addr:$src3)),
                           _.RC:$src1, _.RC:$src2)), 1, 0>,
         EVEX, VVVV, EVEX_B, Sched<[sched.Folded, sched.ReadAfterFold,
                                 sched.ReadAfterFold]>;
  }
}

multiclass avx512_fma3_132_round<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                 X86FoldableSchedWrite sched,
                                 X86VectorVTInfo _> {
  let Constraints = "$src1 = $dst", ExeDomain = _.ExeDomain, hasSideEffects = 0,
      Uses = [MXCSR] in
  defm rb: AVX512_maskable_fma<opc, MRMSrcReg, _, (outs _.RC:$dst),
          (ins _.RC:$src2, _.RC:$src3, AVX512RC:$rc),
          OpcodeStr, "$rc, $src3, $src2", "$src2, $src3, $rc",
          (null_frag),
          (_.VT (OpNode _.RC:$src1, _.RC:$src3, _.RC:$src2, (i32 timm:$rc))),
          1, 1>, EVEX, VVVV, EVEX_B, EVEX_RC, Sched<[sched]>;
}

multiclass avx512_fma3p_132_common<bits<8> opc, string OpcodeStr, SDPatternOperator OpNode,
                                   SDNode MaskOpNode, SDNode OpNodeRnd,
                                   X86SchedWriteWidths sched,
                                   AVX512VLVectorVTInfo _,
                                   Predicate prd = HasAVX512> {
  let Predicates = [prd] in {
    defm Z      : avx512_fma3p_132_rm<opc, OpcodeStr, OpNode, MaskOpNode,
                                      sched.ZMM, _.info512>,
                  avx512_fma3_132_round<opc, OpcodeStr, OpNodeRnd, sched.ZMM,
                                        _.info512>,
                              EVEX_V512, EVEX_CD8<_.info512.EltSize, CD8VF>;
  }
  let Predicates = [HasVLX, prd] in {
    defm Z256 : avx512_fma3p_132_rm<opc, OpcodeStr, OpNode, MaskOpNode,
                                    sched.YMM, _.info256>,
                      EVEX_V256, EVEX_CD8<_.info256.EltSize, CD8VF>;
    defm Z128 : avx512_fma3p_132_rm<opc, OpcodeStr, OpNode, MaskOpNode,
                                    sched.XMM, _.info128>,
                      EVEX_V128, EVEX_CD8<_.info128.EltSize, CD8VF>;
  }
}

multiclass avx512_fma3p_132_f<bits<8> opc, string OpcodeStr, SDPatternOperator OpNode,
                              SDNode MaskOpNode, SDNode OpNodeRnd > {
    defm PH : avx512_fma3p_132_common<opc, OpcodeStr#"ph", OpNode, MaskOpNode,
                                      OpNodeRnd, SchedWriteFMA,
                                      avx512vl_f16_info, HasFP16>, T_MAP6, PD;
    defm PS : avx512_fma3p_132_common<opc, OpcodeStr#"ps", OpNode, MaskOpNode,
                                      OpNodeRnd, SchedWriteFMA,
                                      avx512vl_f32_info>, T8, PD;
    defm PD : avx512_fma3p_132_common<opc, OpcodeStr#"pd", OpNode, MaskOpNode,
                                      OpNodeRnd, SchedWriteFMA,
                                      avx512vl_f64_info>, T8, PD, REX_W;
}

defm VFMADD132    : avx512_fma3p_132_f<0x98, "vfmadd132", any_fma,
                                       fma, X86FmaddRnd>;
defm VFMSUB132    : avx512_fma3p_132_f<0x9A, "vfmsub132", X86any_Fmsub,
                                       X86Fmsub, X86FmsubRnd>;
defm VFMADDSUB132 : avx512_fma3p_132_f<0x96, "vfmaddsub132", X86Fmaddsub,
                                       X86Fmaddsub, X86FmaddsubRnd>;
defm VFMSUBADD132 : avx512_fma3p_132_f<0x97, "vfmsubadd132", X86Fmsubadd,
                                       X86Fmsubadd, X86FmsubaddRnd>;
defm VFNMADD132   : avx512_fma3p_132_f<0x9C, "vfnmadd132", X86any_Fnmadd,
                                       X86Fnmadd, X86FnmaddRnd>;
defm VFNMSUB132   : avx512_fma3p_132_f<0x9E, "vfnmsub132", X86any_Fnmsub,
                                       X86Fnmsub, X86FnmsubRnd>;

// Scalar FMA
multiclass avx512_fma3s_common<bits<8> opc, string OpcodeStr, X86VectorVTInfo _,
                               dag RHS_r, dag RHS_m, dag RHS_b, bit MaskOnlyReg> {
let Constraints = "$src1 = $dst", hasSideEffects = 0 in {
  defm r_Int: AVX512_maskable_3src_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
          (ins _.RC:$src2, _.RC:$src3), OpcodeStr,
          "$src3, $src2", "$src2, $src3", (null_frag), 1, 1>,
          EVEX, VVVV, Sched<[SchedWriteFMA.Scl]>, SIMD_EXC;

  let mayLoad = 1 in
  defm m_Int: AVX512_maskable_3src_scalar<opc, MRMSrcMem, _, (outs _.RC:$dst),
          (ins _.RC:$src2, _.IntScalarMemOp:$src3), OpcodeStr,
          "$src3, $src2", "$src2, $src3", (null_frag), 1, 1>,
          EVEX, VVVV, Sched<[SchedWriteFMA.Scl.Folded, SchedWriteFMA.Scl.ReadAfterFold,
                          SchedWriteFMA.Scl.ReadAfterFold]>, SIMD_EXC;

  let Uses = [MXCSR] in
  defm rb_Int: AVX512_maskable_3src_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
         (ins _.RC:$src2, _.RC:$src3, AVX512RC:$rc),
         OpcodeStr, "$rc, $src3, $src2", "$src2, $src3, $rc", (null_frag), 1, 1>,
         EVEX, VVVV, EVEX_B, EVEX_RC, Sched<[SchedWriteFMA.Scl]>;

  let isCodeGenOnly = 1, isCommutable = 1 in {
    def r     : AVX512<opc, MRMSrcReg, (outs _.FRC:$dst),
                     (ins _.FRC:$src1, _.FRC:$src2, _.FRC:$src3),
                     !strconcat(OpcodeStr,
                              "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
                     !if(MaskOnlyReg, [], [RHS_r])>, Sched<[SchedWriteFMA.Scl]>, EVEX, VVVV, SIMD_EXC;
    def m     : AVX512<opc, MRMSrcMem, (outs _.FRC:$dst),
                    (ins _.FRC:$src1, _.FRC:$src2, _.ScalarMemOp:$src3),
                    !strconcat(OpcodeStr,
                               "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
                    [RHS_m]>, Sched<[SchedWriteFMA.Scl.Folded, SchedWriteFMA.Scl.ReadAfterFold,
                                     SchedWriteFMA.Scl.ReadAfterFold]>, EVEX, VVVV, SIMD_EXC;

    let Uses = [MXCSR] in
    def rb    : AVX512<opc, MRMSrcReg, (outs _.FRC:$dst),
                     (ins _.FRC:$src1, _.FRC:$src2, _.FRC:$src3, AVX512RC:$rc),
                     !strconcat(OpcodeStr,
                              "\t{$rc, $src3, $src2, $dst|$dst, $src2, $src3, $rc}"),
                     !if(MaskOnlyReg, [], [RHS_b])>, EVEX_B, EVEX_RC,
                     Sched<[SchedWriteFMA.Scl]>, EVEX, VVVV;
  }// isCodeGenOnly = 1
}// Constraints = "$src1 = $dst"
}

multiclass avx512_fma3s_all<bits<8> opc213, bits<8> opc231, bits<8> opc132,
                            string OpcodeStr, SDPatternOperator OpNode, SDNode OpNodeRnd,
                            X86VectorVTInfo _, string SUFF> {
  let ExeDomain = _.ExeDomain in {
  defm NAME#213#SUFF#Z: avx512_fma3s_common<opc213, OpcodeStr#"213"#_.Suffix, _,
                // Operands for intrinsic are in 123 order to preserve passthu
                // semantics.
                (set _.FRC:$dst, (_.EltVT (OpNode _.FRC:$src2, _.FRC:$src1,
                         _.FRC:$src3))),
                (set _.FRC:$dst, (_.EltVT (OpNode _.FRC:$src2, _.FRC:$src1,
                         (_.ScalarLdFrag addr:$src3)))),
                (set _.FRC:$dst, (_.EltVT (OpNodeRnd _.FRC:$src2, _.FRC:$src1,
                         _.FRC:$src3, (i32 timm:$rc)))), 0>;

  defm NAME#231#SUFF#Z: avx512_fma3s_common<opc231, OpcodeStr#"231"#_.Suffix, _,
                (set _.FRC:$dst, (_.EltVT (OpNode _.FRC:$src2, _.FRC:$src3,
                                          _.FRC:$src1))),
                (set _.FRC:$dst, (_.EltVT (OpNode _.FRC:$src2,
                            (_.ScalarLdFrag addr:$src3), _.FRC:$src1))),
                (set _.FRC:$dst, (_.EltVT (OpNodeRnd _.FRC:$src2, _.FRC:$src3,
                         _.FRC:$src1, (i32 timm:$rc)))), 1>;

  // One pattern is 312 order so that the load is in a different place from the
  // 213 and 231 patterns this helps tablegen's duplicate pattern detection.
  defm NAME#132#SUFF#Z: avx512_fma3s_common<opc132, OpcodeStr#"132"#_.Suffix, _,
                (set _.FRC:$dst, (_.EltVT (OpNode _.FRC:$src1, _.FRC:$src3,
                         _.FRC:$src2))),
                (set _.FRC:$dst, (_.EltVT (OpNode (_.ScalarLdFrag addr:$src3),
                                 _.FRC:$src1, _.FRC:$src2))),
                (set _.FRC:$dst, (_.EltVT (OpNodeRnd _.FRC:$src1, _.FRC:$src3,
                         _.FRC:$src2, (i32 timm:$rc)))), 1>;
  }
}

multiclass avx512_fma3s<bits<8> opc213, bits<8> opc231, bits<8> opc132,
                        string OpcodeStr, SDPatternOperator OpNode, SDNode OpNodeRnd> {
  let Predicates = [HasAVX512] in {
    defm NAME : avx512_fma3s_all<opc213, opc231, opc132, OpcodeStr, OpNode,
                                 OpNodeRnd, f32x_info, "SS">,
                                 EVEX_CD8<32, CD8VT1>, VEX_LIG, T8, PD;
    defm NAME : avx512_fma3s_all<opc213, opc231, opc132, OpcodeStr, OpNode,
                                 OpNodeRnd, f64x_info, "SD">,
                                 EVEX_CD8<64, CD8VT1>, VEX_LIG, REX_W, T8, PD;
  }
  let Predicates = [HasFP16] in {
    defm NAME : avx512_fma3s_all<opc213, opc231, opc132, OpcodeStr, OpNode,
                                 OpNodeRnd, f16x_info, "SH">,
                                 EVEX_CD8<16, CD8VT1>, VEX_LIG, T_MAP6, PD;
  }
}

defm VFMADD  : avx512_fma3s<0xA9, 0xB9, 0x99, "vfmadd", any_fma, X86FmaddRnd>;
defm VFMSUB  : avx512_fma3s<0xAB, 0xBB, 0x9B, "vfmsub", X86any_Fmsub, X86FmsubRnd>;
defm VFNMADD : avx512_fma3s<0xAD, 0xBD, 0x9D, "vfnmadd", X86any_Fnmadd, X86FnmaddRnd>;
defm VFNMSUB : avx512_fma3s<0xAF, 0xBF, 0x9F, "vfnmsub", X86any_Fnmsub, X86FnmsubRnd>;

multiclass avx512_scalar_fma_patterns<SDPatternOperator Op, SDNode MaskedOp,
                                      SDNode RndOp, string Prefix,
                                      string Suffix, SDNode Move,
                                      X86VectorVTInfo _, PatLeaf ZeroFP,
                                      Predicate prd = HasAVX512> {
  let Predicates = [prd] in {
    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
                (Op _.FRC:$src2,
                    (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                    _.FRC:$src3))))),
              (!cast<I>(Prefix#"213"#Suffix#"Zr_Int")
               VR128X:$src1, (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)),
               (_.VT (COPY_TO_REGCLASS _.FRC:$src3, VR128X)))>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
                (Op _.FRC:$src2, _.FRC:$src3,
                    (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0)))))))),
              (!cast<I>(Prefix#"231"#Suffix#"Zr_Int")
               VR128X:$src1, (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)),
               (_.VT (COPY_TO_REGCLASS _.FRC:$src3, VR128X)))>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
                (Op _.FRC:$src2,
                    (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                    (_.ScalarLdFrag addr:$src3)))))),
              (!cast<I>(Prefix#"213"#Suffix#"Zm_Int")
               VR128X:$src1, (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)),
               addr:$src3)>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
                (Op (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                    (_.ScalarLdFrag addr:$src3), _.FRC:$src2))))),
              (!cast<I>(Prefix#"132"#Suffix#"Zm_Int")
               VR128X:$src1, (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)),
               addr:$src3)>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
                (Op _.FRC:$src2, (_.ScalarLdFrag addr:$src3),
                    (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0)))))))),
              (!cast<I>(Prefix#"231"#Suffix#"Zm_Int")
               VR128X:$src1, (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)),
               addr:$src3)>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
               (X86selects_mask VK1WM:$mask,
                (MaskedOp _.FRC:$src2,
                    (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                    _.FRC:$src3),
                (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0)))))))),
              (!cast<I>(Prefix#"213"#Suffix#"Zr_Intk")
               VR128X:$src1, VK1WM:$mask,
               (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)),
               (_.VT (COPY_TO_REGCLASS _.FRC:$src3, VR128X)))>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
               (X86selects_mask VK1WM:$mask,
                (MaskedOp _.FRC:$src2,
                    (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                    (_.ScalarLdFrag addr:$src3)),
                (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0)))))))),
              (!cast<I>(Prefix#"213"#Suffix#"Zm_Intk")
               VR128X:$src1, VK1WM:$mask,
               (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)), addr:$src3)>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
               (X86selects_mask VK1WM:$mask,
                (MaskedOp (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                          (_.ScalarLdFrag addr:$src3), _.FRC:$src2),
                (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0)))))))),
              (!cast<I>(Prefix#"132"#Suffix#"Zm_Intk")
               VR128X:$src1, VK1WM:$mask,
               (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)), addr:$src3)>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
               (X86selects_mask VK1WM:$mask,
                (MaskedOp _.FRC:$src2, _.FRC:$src3,
                          (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0)))),
                (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0)))))))),
              (!cast<I>(Prefix#"231"#Suffix#"Zr_Intk")
               VR128X:$src1, VK1WM:$mask,
               (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)),
               (_.VT (COPY_TO_REGCLASS _.FRC:$src3, VR128X)))>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
               (X86selects_mask VK1WM:$mask,
                (MaskedOp _.FRC:$src2, (_.ScalarLdFrag addr:$src3),
                          (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0)))),
                (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0)))))))),
              (!cast<I>(Prefix#"231"#Suffix#"Zm_Intk")
               VR128X:$src1, VK1WM:$mask,
               (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)), addr:$src3)>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
               (X86selects_mask VK1WM:$mask,
                (MaskedOp _.FRC:$src2,
                          (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                          _.FRC:$src3),
                (_.EltVT ZeroFP)))))),
              (!cast<I>(Prefix#"213"#Suffix#"Zr_Intkz")
               VR128X:$src1, VK1WM:$mask,
               (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)),
               (_.VT (COPY_TO_REGCLASS _.FRC:$src3, VR128X)))>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
               (X86selects_mask VK1WM:$mask,
                (MaskedOp _.FRC:$src2, _.FRC:$src3,
                          (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0)))),
                (_.EltVT ZeroFP)))))),
              (!cast<I>(Prefix#"231"#Suffix#"Zr_Intkz")
               VR128X:$src1, VK1WM:$mask,
               (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)),
               (_.VT (COPY_TO_REGCLASS _.FRC:$src3, VR128X)))>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
               (X86selects_mask VK1WM:$mask,
                (MaskedOp _.FRC:$src2,
                          (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                          (_.ScalarLdFrag addr:$src3)),
                (_.EltVT ZeroFP)))))),
              (!cast<I>(Prefix#"213"#Suffix#"Zm_Intkz")
               VR128X:$src1, VK1WM:$mask,
               (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)), addr:$src3)>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
               (X86selects_mask VK1WM:$mask,
                (MaskedOp (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                          _.FRC:$src2, (_.ScalarLdFrag addr:$src3)),
                (_.EltVT ZeroFP)))))),
              (!cast<I>(Prefix#"132"#Suffix#"Zm_Intkz")
               VR128X:$src1, VK1WM:$mask,
               (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)), addr:$src3)>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
               (X86selects_mask VK1WM:$mask,
                (MaskedOp _.FRC:$src2, (_.ScalarLdFrag addr:$src3),
                          (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0)))),
                (_.EltVT ZeroFP)))))),
              (!cast<I>(Prefix#"231"#Suffix#"Zm_Intkz")
               VR128X:$src1, VK1WM:$mask,
               (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)), addr:$src3)>;

    // Patterns with rounding mode.
    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
                (RndOp _.FRC:$src2,
                       (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                       _.FRC:$src3, (i32 timm:$rc)))))),
              (!cast<I>(Prefix#"213"#Suffix#"Zrb_Int")
               VR128X:$src1, (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)),
               (_.VT (COPY_TO_REGCLASS _.FRC:$src3, VR128X)), AVX512RC:$rc)>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
                (RndOp _.FRC:$src2, _.FRC:$src3,
                       (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                       (i32 timm:$rc)))))),
              (!cast<I>(Prefix#"231"#Suffix#"Zrb_Int")
               VR128X:$src1, (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)),
               (_.VT (COPY_TO_REGCLASS _.FRC:$src3, VR128X)), AVX512RC:$rc)>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
               (X86selects_mask VK1WM:$mask,
                (RndOp _.FRC:$src2,
                       (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                       _.FRC:$src3, (i32 timm:$rc)),
                (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0)))))))),
              (!cast<I>(Prefix#"213"#Suffix#"Zrb_Intk")
               VR128X:$src1, VK1WM:$mask,
               (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)),
               (_.VT (COPY_TO_REGCLASS _.FRC:$src3, VR128X)), AVX512RC:$rc)>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
               (X86selects_mask VK1WM:$mask,
                (RndOp _.FRC:$src2, _.FRC:$src3,
                       (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                       (i32 timm:$rc)),
                (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0)))))))),
              (!cast<I>(Prefix#"231"#Suffix#"Zrb_Intk")
               VR128X:$src1, VK1WM:$mask,
               (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)),
               (_.VT (COPY_TO_REGCLASS _.FRC:$src3, VR128X)), AVX512RC:$rc)>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
               (X86selects_mask VK1WM:$mask,
                (RndOp _.FRC:$src2,
                       (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                       _.FRC:$src3, (i32 timm:$rc)),
                (_.EltVT ZeroFP)))))),
              (!cast<I>(Prefix#"213"#Suffix#"Zrb_Intkz")
               VR128X:$src1, VK1WM:$mask,
               (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)),
               (_.VT (COPY_TO_REGCLASS _.FRC:$src3, VR128X)), AVX512RC:$rc)>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
               (X86selects_mask VK1WM:$mask,
                (RndOp _.FRC:$src2, _.FRC:$src3,
                       (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                       (i32 timm:$rc)),
                (_.EltVT ZeroFP)))))),
              (!cast<I>(Prefix#"231"#Suffix#"Zrb_Intkz")
               VR128X:$src1, VK1WM:$mask,
               (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)),
               (_.VT (COPY_TO_REGCLASS _.FRC:$src3, VR128X)), AVX512RC:$rc)>;
  }
}
defm : avx512_scalar_fma_patterns<any_fma, fma, X86FmaddRnd, "VFMADD", "SH",
                                  X86Movsh, v8f16x_info, fp16imm0, HasFP16>;
defm : avx512_scalar_fma_patterns<X86any_Fmsub, X86Fmsub, X86FmsubRnd, "VFMSUB", "SH",
                                  X86Movsh, v8f16x_info, fp16imm0, HasFP16>;
defm : avx512_scalar_fma_patterns<X86any_Fnmadd, X86Fnmadd, X86FnmaddRnd, "VFNMADD", "SH",
                                  X86Movsh, v8f16x_info, fp16imm0, HasFP16>;
defm : avx512_scalar_fma_patterns<X86any_Fnmsub, X86Fnmsub, X86FnmsubRnd, "VFNMSUB", "SH",
                                  X86Movsh, v8f16x_info, fp16imm0, HasFP16>;

defm : avx512_scalar_fma_patterns<any_fma, fma, X86FmaddRnd, "VFMADD",
                                  "SS", X86Movss, v4f32x_info, fp32imm0>;
defm : avx512_scalar_fma_patterns<X86any_Fmsub, X86Fmsub, X86FmsubRnd, "VFMSUB",
                                  "SS", X86Movss, v4f32x_info, fp32imm0>;
defm : avx512_scalar_fma_patterns<X86any_Fnmadd, X86Fnmadd, X86FnmaddRnd, "VFNMADD",
                                  "SS", X86Movss, v4f32x_info, fp32imm0>;
defm : avx512_scalar_fma_patterns<X86any_Fnmsub, X86Fnmsub, X86FnmsubRnd, "VFNMSUB",
                                  "SS", X86Movss, v4f32x_info, fp32imm0>;

defm : avx512_scalar_fma_patterns<any_fma, fma, X86FmaddRnd, "VFMADD",
                                  "SD", X86Movsd, v2f64x_info, fp64imm0>;
defm : avx512_scalar_fma_patterns<X86any_Fmsub, X86Fmsub, X86FmsubRnd, "VFMSUB",
                                  "SD", X86Movsd, v2f64x_info, fp64imm0>;
defm : avx512_scalar_fma_patterns<X86any_Fnmadd, X86Fnmadd, X86FnmaddRnd, "VFNMADD",
                                  "SD", X86Movsd, v2f64x_info, fp64imm0>;
defm : avx512_scalar_fma_patterns<X86any_Fnmsub, X86Fnmsub, X86FnmsubRnd, "VFNMSUB",
                                  "SD", X86Movsd, v2f64x_info, fp64imm0>;

//===----------------------------------------------------------------------===//
// AVX-512  Packed Multiply of Unsigned 52-bit Integers and Add the Low 52-bit IFMA
//===----------------------------------------------------------------------===//
let Constraints = "$src1 = $dst" in {
multiclass avx512_pmadd52_rm<bits<8> opc, string OpcodeStr, SDNode OpNode,
                             X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  // NOTE: The SDNode have the multiply operands first with the add last.
  // This enables commuted load patterns to be autogenerated by tablegen.
  let ExeDomain = _.ExeDomain in {
  defm r: AVX512_maskable_3src<opc, MRMSrcReg, _, (outs _.RC:$dst),
          (ins _.RC:$src2, _.RC:$src3),
          OpcodeStr, "$src3, $src2", "$src2, $src3",
          (_.VT (OpNode _.RC:$src2, _.RC:$src3, _.RC:$src1)), 1, 1>,
          T8, PD, EVEX, VVVV, Sched<[sched]>;

  defm m: AVX512_maskable_3src<opc, MRMSrcMem, _, (outs _.RC:$dst),
          (ins _.RC:$src2, _.MemOp:$src3),
          OpcodeStr, "$src3, $src2", "$src2, $src3",
          (_.VT (OpNode _.RC:$src2, (_.LdFrag addr:$src3), _.RC:$src1))>,
          T8, PD, EVEX, VVVV, Sched<[sched.Folded, sched.ReadAfterFold,
                                sched.ReadAfterFold]>;

  defm mb: AVX512_maskable_3src<opc, MRMSrcMem, _, (outs _.RC:$dst),
            (ins _.RC:$src2, _.ScalarMemOp:$src3),
            OpcodeStr,   !strconcat("${src3}", _.BroadcastStr,", $src2"),
            !strconcat("$src2, ${src3}", _.BroadcastStr ),
            (OpNode _.RC:$src2,
                    (_.VT (_.BroadcastLdFrag addr:$src3)),
                    _.RC:$src1)>,
            T8, PD, EVEX, VVVV, EVEX_B, Sched<[sched.Folded, sched.ReadAfterFold,
                                          sched.ReadAfterFold]>;
  }
}
} // Constraints = "$src1 = $dst"

multiclass avx512_pmadd52_common<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                 X86SchedWriteWidths sched, AVX512VLVectorVTInfo _> {
  let Predicates = [HasIFMA] in {
    defm Z      : avx512_pmadd52_rm<opc, OpcodeStr, OpNode, sched.ZMM, _.info512>,
                      EVEX_V512, EVEX_CD8<_.info512.EltSize, CD8VF>;
  }
  let Predicates = [HasVLX, HasIFMA] in {
    defm Z256 : avx512_pmadd52_rm<opc, OpcodeStr, OpNode, sched.YMM, _.info256>,
                      EVEX_V256, EVEX_CD8<_.info256.EltSize, CD8VF>;
    defm Z128 : avx512_pmadd52_rm<opc, OpcodeStr, OpNode, sched.XMM, _.info128>,
                      EVEX_V128, EVEX_CD8<_.info128.EltSize, CD8VF>;
  }
}

defm VPMADD52LUQ : avx512_pmadd52_common<0xb4, "vpmadd52luq", x86vpmadd52l,
                                         SchedWriteVecIMul, avx512vl_i64_info>,
                                         REX_W;
defm VPMADD52HUQ : avx512_pmadd52_common<0xb5, "vpmadd52huq", x86vpmadd52h,
                                         SchedWriteVecIMul, avx512vl_i64_info>,
                                         REX_W;

//===----------------------------------------------------------------------===//
// AVX-512  Scalar convert from sign integer to float/double
//===----------------------------------------------------------------------===//

multiclass avx512_vcvtsi<bits<8> opc, SDPatternOperator OpNode, X86FoldableSchedWrite sched,
                    RegisterClass SrcRC, X86VectorVTInfo DstVT,
                    X86MemOperand x86memop, PatFrag ld_frag, string asm,
                    string mem, list<Register> _Uses = [MXCSR],
                    bit _mayRaiseFPException = 1> {
let ExeDomain = DstVT.ExeDomain, Uses = _Uses,
    mayRaiseFPException = _mayRaiseFPException in {
  let hasSideEffects = 0, isCodeGenOnly = 1 in {
    def rr : SI<opc, MRMSrcReg, (outs DstVT.FRC:$dst),
              (ins DstVT.FRC:$src1, SrcRC:$src),
              !strconcat(asm,"\t{$src, $src1, $dst|$dst, $src1, $src}"), []>,
              EVEX, VVVV, Sched<[sched, ReadDefault, ReadInt2Fpu]>;
    let mayLoad = 1 in
      def rm : SI<opc, MRMSrcMem, (outs DstVT.FRC:$dst),
              (ins DstVT.FRC:$src1, x86memop:$src),
              asm#"{"#mem#"}\t{$src, $src1, $dst|$dst, $src1, $src}", []>,
              EVEX, VVVV, Sched<[sched.Folded, sched.ReadAfterFold]>;
  } // hasSideEffects = 0
  def rr_Int : SI<opc, MRMSrcReg, (outs DstVT.RC:$dst),
                (ins DstVT.RC:$src1, SrcRC:$src2),
                !strconcat(asm,"\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
                [(set DstVT.RC:$dst,
                      (OpNode (DstVT.VT DstVT.RC:$src1), SrcRC:$src2))]>,
               EVEX, VVVV, Sched<[sched, ReadDefault, ReadInt2Fpu]>;

  def rm_Int : SI<opc, MRMSrcMem, (outs DstVT.RC:$dst),
                (ins DstVT.RC:$src1, x86memop:$src2),
                asm#"{"#mem#"}\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                [(set DstVT.RC:$dst,
                      (OpNode (DstVT.VT DstVT.RC:$src1),
                               (ld_frag addr:$src2)))]>,
                EVEX, VVVV, Sched<[sched.Folded, sched.ReadAfterFold]>;
}
  def : InstAlias<"v"#asm#mem#"\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                  (!cast<Instruction>(NAME#"rr_Int") DstVT.RC:$dst,
                  DstVT.RC:$src1, SrcRC:$src2), 0, "att">;
}

multiclass avx512_vcvtsi_round<bits<8> opc, SDNode OpNode,
                               X86FoldableSchedWrite sched, RegisterClass SrcRC,
                               X86VectorVTInfo DstVT, string asm,
                               string mem> {
  let ExeDomain = DstVT.ExeDomain, Uses = [MXCSR] in
  def rrb_Int : SI<opc, MRMSrcReg, (outs DstVT.RC:$dst),
              (ins DstVT.RC:$src1, SrcRC:$src2, AVX512RC:$rc),
              !strconcat(asm,
                  "\t{$src2, $rc, $src1, $dst|$dst, $src1, $rc, $src2}"),
              [(set DstVT.RC:$dst,
                    (OpNode (DstVT.VT DstVT.RC:$src1),
                             SrcRC:$src2,
                             (i32 timm:$rc)))]>,
              EVEX, VVVV, EVEX_B, EVEX_RC, Sched<[sched, ReadDefault, ReadInt2Fpu]>;
  def : InstAlias<"v"#asm#mem#"\t{$src2, $rc, $src1, $dst|$dst, $src1, $rc, $src2}",
                  (!cast<Instruction>(NAME#"rrb_Int") DstVT.RC:$dst,
                  DstVT.RC:$src1, SrcRC:$src2, AVX512RC:$rc), 0, "att">;
}

multiclass avx512_vcvtsi_common<bits<8> opc, SDNode OpNode, SDNode OpNodeRnd,
                                X86FoldableSchedWrite sched,
                                RegisterClass SrcRC, X86VectorVTInfo DstVT,
                                X86MemOperand x86memop, PatFrag ld_frag,
                                string asm, string mem> {
  defm NAME : avx512_vcvtsi_round<opc, OpNodeRnd, sched, SrcRC, DstVT, asm, mem>,
              avx512_vcvtsi<opc, OpNode, sched, SrcRC, DstVT, x86memop,
                            ld_frag, asm, mem>, VEX_LIG;
}

let Predicates = [HasAVX512] in {
defm VCVTSI2SSZ  : avx512_vcvtsi_common<0x2A, X86SintToFp, X86SintToFpRnd,
                                 WriteCvtI2SS, GR32,
                                 v4f32x_info, i32mem, loadi32, "cvtsi2ss", "l">,
                                 TB, XS, EVEX_CD8<32, CD8VT1>;
defm VCVTSI642SSZ: avx512_vcvtsi_common<0x2A, X86SintToFp, X86SintToFpRnd,
                                 WriteCvtI2SS, GR64,
                                 v4f32x_info, i64mem, loadi64, "cvtsi2ss", "q">,
                                 TB, XS, REX_W, EVEX_CD8<64, CD8VT1>;
defm VCVTSI2SDZ  : avx512_vcvtsi<0x2A, null_frag, WriteCvtI2SD, GR32,
                                 v2f64x_info, i32mem, loadi32, "cvtsi2sd", "l", [], 0>,
                                 TB, XD, VEX_LIG, EVEX_CD8<32, CD8VT1>;
defm VCVTSI642SDZ: avx512_vcvtsi_common<0x2A, X86SintToFp, X86SintToFpRnd,
                                 WriteCvtI2SD, GR64,
                                 v2f64x_info, i64mem, loadi64, "cvtsi2sd", "q">,
                                 TB, XD, REX_W, EVEX_CD8<64, CD8VT1>;

def : InstAlias<"vcvtsi2ss\t{$src, $src1, $dst|$dst, $src1, $src}",
              (VCVTSI2SSZrm_Int VR128X:$dst, VR128X:$src1, i32mem:$src), 0, "att">;
def : InstAlias<"vcvtsi2sd\t{$src, $src1, $dst|$dst, $src1, $src}",
              (VCVTSI2SDZrm_Int VR128X:$dst, VR128X:$src1, i32mem:$src), 0, "att">;

def : Pat<(f32 (any_sint_to_fp (loadi32 addr:$src))),
          (VCVTSI2SSZrm (f32 (IMPLICIT_DEF)), addr:$src)>;
def : Pat<(f32 (any_sint_to_fp (loadi64 addr:$src))),
          (VCVTSI642SSZrm (f32 (IMPLICIT_DEF)), addr:$src)>;
def : Pat<(f64 (any_sint_to_fp (loadi32 addr:$src))),
          (VCVTSI2SDZrm (f64 (IMPLICIT_DEF)), addr:$src)>;
def : Pat<(f64 (any_sint_to_fp (loadi64 addr:$src))),
          (VCVTSI642SDZrm (f64 (IMPLICIT_DEF)), addr:$src)>;

def : Pat<(f32 (any_sint_to_fp GR32:$src)),
          (VCVTSI2SSZrr (f32 (IMPLICIT_DEF)), GR32:$src)>;
def : Pat<(f32 (any_sint_to_fp GR64:$src)),
          (VCVTSI642SSZrr (f32 (IMPLICIT_DEF)), GR64:$src)>;
def : Pat<(f64 (any_sint_to_fp GR32:$src)),
          (VCVTSI2SDZrr (f64 (IMPLICIT_DEF)), GR32:$src)>;
def : Pat<(f64 (any_sint_to_fp GR64:$src)),
          (VCVTSI642SDZrr (f64 (IMPLICIT_DEF)), GR64:$src)>;

defm VCVTUSI2SSZ   : avx512_vcvtsi_common<0x7B, X86UintToFp, X86UintToFpRnd,
                                  WriteCvtI2SS, GR32,
                                  v4f32x_info, i32mem, loadi32,
                                  "cvtusi2ss", "l">, TB, XS, EVEX_CD8<32, CD8VT1>;
defm VCVTUSI642SSZ : avx512_vcvtsi_common<0x7B, X86UintToFp, X86UintToFpRnd,
                                  WriteCvtI2SS, GR64,
                                  v4f32x_info, i64mem, loadi64, "cvtusi2ss", "q">,
                                  TB, XS, REX_W, EVEX_CD8<64, CD8VT1>;
defm VCVTUSI2SDZ   : avx512_vcvtsi<0x7B, null_frag, WriteCvtI2SD, GR32, v2f64x_info,
                                  i32mem, loadi32, "cvtusi2sd", "l", [], 0>,
                                  TB, XD, VEX_LIG, EVEX_CD8<32, CD8VT1>;
defm VCVTUSI642SDZ : avx512_vcvtsi_common<0x7B, X86UintToFp, X86UintToFpRnd,
                                  WriteCvtI2SD, GR64,
                                  v2f64x_info, i64mem, loadi64, "cvtusi2sd", "q">,
                                  TB, XD, REX_W, EVEX_CD8<64, CD8VT1>;

def : InstAlias<"vcvtusi2ss\t{$src, $src1, $dst|$dst, $src1, $src}",
              (VCVTUSI2SSZrm_Int VR128X:$dst, VR128X:$src1, i32mem:$src), 0, "att">;
def : InstAlias<"vcvtusi2sd\t{$src, $src1, $dst|$dst, $src1, $src}",
              (VCVTUSI2SDZrm_Int VR128X:$dst, VR128X:$src1, i32mem:$src), 0, "att">;

def : Pat<(f32 (any_uint_to_fp (loadi32 addr:$src))),
          (VCVTUSI2SSZrm (f32 (IMPLICIT_DEF)), addr:$src)>;
def : Pat<(f32 (any_uint_to_fp (loadi64 addr:$src))),
          (VCVTUSI642SSZrm (f32 (IMPLICIT_DEF)), addr:$src)>;
def : Pat<(f64 (any_uint_to_fp (loadi32 addr:$src))),
          (VCVTUSI2SDZrm (f64 (IMPLICIT_DEF)), addr:$src)>;
def : Pat<(f64 (any_uint_to_fp (loadi64 addr:$src))),
          (VCVTUSI642SDZrm (f64 (IMPLICIT_DEF)), addr:$src)>;

def : Pat<(f32 (any_uint_to_fp GR32:$src)),
          (VCVTUSI2SSZrr (f32 (IMPLICIT_DEF)), GR32:$src)>;
def : Pat<(f32 (any_uint_to_fp GR64:$src)),
          (VCVTUSI642SSZrr (f32 (IMPLICIT_DEF)), GR64:$src)>;
def : Pat<(f64 (any_uint_to_fp GR32:$src)),
          (VCVTUSI2SDZrr (f64 (IMPLICIT_DEF)), GR32:$src)>;
def : Pat<(f64 (any_uint_to_fp GR64:$src)),
          (VCVTUSI642SDZrr (f64 (IMPLICIT_DEF)), GR64:$src)>;
}

//===----------------------------------------------------------------------===//
// AVX-512  Scalar convert from float/double to integer
//===----------------------------------------------------------------------===//

multiclass avx512_cvt_s_int_round<bits<8> opc, X86VectorVTInfo SrcVT,
                                  X86VectorVTInfo DstVT, SDNode OpNode,
                                  SDNode OpNodeRnd,
                                  X86FoldableSchedWrite sched, string asm,
                                  string aliasStr, Predicate prd = HasAVX512> {
  let Predicates = [prd], ExeDomain = SrcVT.ExeDomain in {
    def rr_Int : SI<opc, MRMSrcReg, (outs DstVT.RC:$dst), (ins SrcVT.RC:$src),
                !strconcat(asm,"\t{$src, $dst|$dst, $src}"),
                [(set DstVT.RC:$dst, (OpNode (SrcVT.VT SrcVT.RC:$src)))]>,
                EVEX, VEX_LIG, Sched<[sched]>, SIMD_EXC;
    let Uses = [MXCSR] in
    def rrb_Int : SI<opc, MRMSrcReg, (outs DstVT.RC:$dst), (ins SrcVT.RC:$src, AVX512RC:$rc),
                 !strconcat(asm,"\t{$rc, $src, $dst|$dst, $src, $rc}"),
                 [(set DstVT.RC:$dst, (OpNodeRnd (SrcVT.VT SrcVT.RC:$src),(i32 timm:$rc)))]>,
                 EVEX, VEX_LIG, EVEX_B, EVEX_RC,
                 Sched<[sched]>;
    def rm_Int : SI<opc, MRMSrcMem, (outs DstVT.RC:$dst), (ins SrcVT.IntScalarMemOp:$src),
                !strconcat(asm,"\t{$src, $dst|$dst, $src}"),
                [(set DstVT.RC:$dst, (OpNode
                      (SrcVT.ScalarIntMemFrags addr:$src)))]>,
                EVEX, VEX_LIG, Sched<[sched.Folded, sched.ReadAfterFold]>, SIMD_EXC;
  } // Predicates = [prd]

  def : InstAlias<"v" # asm # aliasStr # "\t{$src, $dst|$dst, $src}",
          (!cast<Instruction>(NAME # "rr_Int") DstVT.RC:$dst, SrcVT.RC:$src), 0, "att">;
  def : InstAlias<"v" # asm # aliasStr # "\t{$rc, $src, $dst|$dst, $src, $rc}",
          (!cast<Instruction>(NAME # "rrb_Int") DstVT.RC:$dst, SrcVT.RC:$src, AVX512RC:$rc), 0, "att">;
  def : InstAlias<"v" # asm # aliasStr # "\t{$src, $dst|$dst, $src}",
          (!cast<Instruction>(NAME # "rm_Int") DstVT.RC:$dst,
                                          SrcVT.IntScalarMemOp:$src), 0, "att">;
}

// Convert float/double to signed/unsigned int 32/64
defm VCVTSS2SIZ: avx512_cvt_s_int_round<0x2D, f32x_info, i32x_info,X86cvts2si,
                                   X86cvts2siRnd, WriteCvtSS2I, "cvtss2si", "{l}">,
                                   TB, XS, EVEX_CD8<32, CD8VT1>;
defm VCVTSS2SI64Z: avx512_cvt_s_int_round<0x2D, f32x_info, i64x_info, X86cvts2si,
                                   X86cvts2siRnd, WriteCvtSS2I, "cvtss2si", "{q}">,
                                   TB, XS, REX_W, EVEX_CD8<32, CD8VT1>;
defm VCVTSS2USIZ: avx512_cvt_s_int_round<0x79, f32x_info, i32x_info, X86cvts2usi,
                                   X86cvts2usiRnd, WriteCvtSS2I, "cvtss2usi", "{l}">,
                                   TB, XS, EVEX_CD8<32, CD8VT1>;
defm VCVTSS2USI64Z: avx512_cvt_s_int_round<0x79, f32x_info, i64x_info, X86cvts2usi,
                                   X86cvts2usiRnd, WriteCvtSS2I, "cvtss2usi", "{q}">,
                                   TB, XS, REX_W, EVEX_CD8<32, CD8VT1>;
defm VCVTSD2SIZ: avx512_cvt_s_int_round<0x2D, f64x_info, i32x_info, X86cvts2si,
                                   X86cvts2siRnd, WriteCvtSD2I, "cvtsd2si", "{l}">,
                                   TB, XD, EVEX_CD8<64, CD8VT1>;
defm VCVTSD2SI64Z: avx512_cvt_s_int_round<0x2D, f64x_info, i64x_info, X86cvts2si,
                                   X86cvts2siRnd, WriteCvtSD2I, "cvtsd2si", "{q}">,
                                   TB, XD, REX_W, EVEX_CD8<64, CD8VT1>;
defm VCVTSD2USIZ:   avx512_cvt_s_int_round<0x79, f64x_info, i32x_info, X86cvts2usi,
                                   X86cvts2usiRnd, WriteCvtSD2I, "cvtsd2usi", "{l}">,
                                   TB, XD, EVEX_CD8<64, CD8VT1>;
defm VCVTSD2USI64Z: avx512_cvt_s_int_round<0x79, f64x_info, i64x_info, X86cvts2usi,
                                   X86cvts2usiRnd, WriteCvtSD2I, "cvtsd2usi", "{q}">,
                                   TB, XD, REX_W, EVEX_CD8<64, CD8VT1>;

multiclass avx512_cvt_s<bits<8> opc, string asm, X86VectorVTInfo SrcVT,
                        X86VectorVTInfo DstVT, SDNode OpNode,
                        X86FoldableSchedWrite sched> {
  let Predicates = [HasAVX512], ExeDomain = SrcVT.ExeDomain in {
    let isCodeGenOnly = 1 in {
    def rr : AVX512<opc, MRMSrcReg, (outs DstVT.RC:$dst), (ins SrcVT.FRC:$src),
                !strconcat(asm,"\t{$src, $dst|$dst, $src}"),
                [(set DstVT.RC:$dst, (OpNode SrcVT.FRC:$src))]>,
                EVEX, VEX_LIG, Sched<[sched]>, SIMD_EXC;
    def rm : AVX512<opc, MRMSrcMem, (outs DstVT.RC:$dst), (ins SrcVT.ScalarMemOp:$src),
                !strconcat(asm,"\t{$src, $dst|$dst, $src}"),
                [(set DstVT.RC:$dst, (OpNode (SrcVT.ScalarLdFrag addr:$src)))]>,
                EVEX, VEX_LIG, Sched<[sched.Folded, sched.ReadAfterFold]>, SIMD_EXC;
    }
  } // Predicates = [HasAVX512]
}

defm VCVTSS2SIZ: avx512_cvt_s<0x2D, "vcvtss2si", f32x_info, i32x_info,
                       lrint, WriteCvtSS2I>, TB, XS, EVEX_CD8<32, CD8VT1>;
defm VCVTSS2SI64Z: avx512_cvt_s<0x2D, "vcvtss2si", f32x_info, i64x_info,
                       llrint, WriteCvtSS2I>, REX_W, TB, XS, EVEX_CD8<32, CD8VT1>;
defm VCVTSD2SIZ: avx512_cvt_s<0x2D, "vcvtsd2si", f64x_info, i32x_info,
                       lrint, WriteCvtSD2I>, TB, XD, EVEX_CD8<64, CD8VT1>;
defm VCVTSD2SI64Z: avx512_cvt_s<0x2D, "vcvtsd2si", f64x_info, i64x_info,
                       llrint, WriteCvtSD2I>, REX_W, TB, XD, EVEX_CD8<64, CD8VT1>;

let Predicates = [HasAVX512] in {
  def : Pat<(i64 (lrint FR32:$src)), (VCVTSS2SI64Zrr FR32:$src)>;
  def : Pat<(i64 (lrint (loadf32 addr:$src))), (VCVTSS2SI64Zrm addr:$src)>;

  def : Pat<(i64 (lrint FR64:$src)), (VCVTSD2SI64Zrr FR64:$src)>;
  def : Pat<(i64 (lrint (loadf64 addr:$src))), (VCVTSD2SI64Zrm addr:$src)>;
}

// Patterns used for matching vcvtsi2s{s,d} intrinsic sequences from clang
// which produce unnecessary vmovs{s,d} instructions
let Predicates = [HasAVX512] in {
def : Pat<(v4f32 (X86Movss
                   (v4f32 VR128X:$dst),
                   (v4f32 (scalar_to_vector (f32 (any_sint_to_fp GR64:$src)))))),
          (VCVTSI642SSZrr_Int VR128X:$dst, GR64:$src)>;

def : Pat<(v4f32 (X86Movss
                   (v4f32 VR128X:$dst),
                   (v4f32 (scalar_to_vector (f32 (any_sint_to_fp (loadi64 addr:$src))))))),
          (VCVTSI642SSZrm_Int VR128X:$dst, addr:$src)>;

def : Pat<(v4f32 (X86Movss
                   (v4f32 VR128X:$dst),
                   (v4f32 (scalar_to_vector (f32 (any_sint_to_fp GR32:$src)))))),
          (VCVTSI2SSZrr_Int VR128X:$dst, GR32:$src)>;

def : Pat<(v4f32 (X86Movss
                   (v4f32 VR128X:$dst),
                   (v4f32 (scalar_to_vector (f32 (any_sint_to_fp (loadi32 addr:$src))))))),
          (VCVTSI2SSZrm_Int VR128X:$dst, addr:$src)>;

def : Pat<(v2f64 (X86Movsd
                   (v2f64 VR128X:$dst),
                   (v2f64 (scalar_to_vector (f64 (any_sint_to_fp GR64:$src)))))),
          (VCVTSI642SDZrr_Int VR128X:$dst, GR64:$src)>;

def : Pat<(v2f64 (X86Movsd
                   (v2f64 VR128X:$dst),
                   (v2f64 (scalar_to_vector (f64 (any_sint_to_fp (loadi64 addr:$src))))))),
          (VCVTSI642SDZrm_Int VR128X:$dst, addr:$src)>;

def : Pat<(v2f64 (X86Movsd
                   (v2f64 VR128X:$dst),
                   (v2f64 (scalar_to_vector (f64 (any_sint_to_fp GR32:$src)))))),
          (VCVTSI2SDZrr_Int VR128X:$dst, GR32:$src)>;

def : Pat<(v2f64 (X86Movsd
                   (v2f64 VR128X:$dst),
                   (v2f64 (scalar_to_vector (f64 (any_sint_to_fp (loadi32 addr:$src))))))),
          (VCVTSI2SDZrm_Int VR128X:$dst, addr:$src)>;

def : Pat<(v4f32 (X86Movss
                   (v4f32 VR128X:$dst),
                   (v4f32 (scalar_to_vector (f32 (any_uint_to_fp GR64:$src)))))),
          (VCVTUSI642SSZrr_Int VR128X:$dst, GR64:$src)>;

def : Pat<(v4f32 (X86Movss
                   (v4f32 VR128X:$dst),
                   (v4f32 (scalar_to_vector (f32 (any_uint_to_fp (loadi64 addr:$src))))))),
          (VCVTUSI642SSZrm_Int VR128X:$dst, addr:$src)>;

def : Pat<(v4f32 (X86Movss
                   (v4f32 VR128X:$dst),
                   (v4f32 (scalar_to_vector (f32 (any_uint_to_fp GR32:$src)))))),
          (VCVTUSI2SSZrr_Int VR128X:$dst, GR32:$src)>;

def : Pat<(v4f32 (X86Movss
                   (v4f32 VR128X:$dst),
                   (v4f32 (scalar_to_vector (f32 (any_uint_to_fp (loadi32 addr:$src))))))),
          (VCVTUSI2SSZrm_Int VR128X:$dst, addr:$src)>;

def : Pat<(v2f64 (X86Movsd
                   (v2f64 VR128X:$dst),
                   (v2f64 (scalar_to_vector (f64 (any_uint_to_fp GR64:$src)))))),
          (VCVTUSI642SDZrr_Int VR128X:$dst, GR64:$src)>;

def : Pat<(v2f64 (X86Movsd
                   (v2f64 VR128X:$dst),
                   (v2f64 (scalar_to_vector (f64 (any_uint_to_fp (loadi64 addr:$src))))))),
          (VCVTUSI642SDZrm_Int VR128X:$dst, addr:$src)>;

def : Pat<(v2f64 (X86Movsd
                   (v2f64 VR128X:$dst),
                   (v2f64 (scalar_to_vector (f64 (any_uint_to_fp GR32:$src)))))),
          (VCVTUSI2SDZrr_Int VR128X:$dst, GR32:$src)>;

def : Pat<(v2f64 (X86Movsd
                   (v2f64 VR128X:$dst),
                   (v2f64 (scalar_to_vector (f64 (any_uint_to_fp (loadi32 addr:$src))))))),
          (VCVTUSI2SDZrm_Int VR128X:$dst, addr:$src)>;
} // Predicates = [HasAVX512]

// Convert float/double to signed/unsigned int 32/64 with truncation
multiclass avx512_cvt_s_all<bits<8> opc, string asm, X86VectorVTInfo _SrcRC,
                            X86VectorVTInfo _DstRC, SDPatternOperator OpNode,
                            SDNode OpNodeInt, SDNode OpNodeSAE,
                            X86FoldableSchedWrite sched, string aliasStr,
                            Predicate prd = HasAVX512> {
let Predicates = [prd], ExeDomain = _SrcRC.ExeDomain in {
  let isCodeGenOnly = 1 in {
  def rr : AVX512<opc, MRMSrcReg, (outs _DstRC.RC:$dst), (ins _SrcRC.FRC:$src),
              !strconcat(asm,"\t{$src, $dst|$dst, $src}"),
              [(set _DstRC.RC:$dst, (OpNode _SrcRC.FRC:$src))]>,
              EVEX, VEX_LIG, Sched<[sched]>, SIMD_EXC;
  def rm : AVX512<opc, MRMSrcMem, (outs _DstRC.RC:$dst), (ins _SrcRC.ScalarMemOp:$src),
              !strconcat(asm,"\t{$src, $dst|$dst, $src}"),
              [(set _DstRC.RC:$dst, (OpNode (_SrcRC.ScalarLdFrag addr:$src)))]>,
              EVEX, VEX_LIG, Sched<[sched.Folded, sched.ReadAfterFold]>, SIMD_EXC;
  }

  def rr_Int : AVX512<opc, MRMSrcReg, (outs _DstRC.RC:$dst), (ins _SrcRC.RC:$src),
            !strconcat(asm,"\t{$src, $dst|$dst, $src}"),
           [(set _DstRC.RC:$dst, (OpNodeInt (_SrcRC.VT _SrcRC.RC:$src)))]>,
           EVEX, VEX_LIG, Sched<[sched]>, SIMD_EXC;
  let Uses = [MXCSR] in
  def rrb_Int : AVX512<opc, MRMSrcReg, (outs _DstRC.RC:$dst), (ins _SrcRC.RC:$src),
            !strconcat(asm,"\t{{sae}, $src, $dst|$dst, $src, {sae}}"),
            [(set _DstRC.RC:$dst, (OpNodeSAE (_SrcRC.VT _SrcRC.RC:$src)))]>,
                                  EVEX, VEX_LIG, EVEX_B, Sched<[sched]>;
  def rm_Int : AVX512<opc, MRMSrcMem, (outs _DstRC.RC:$dst),
              (ins _SrcRC.IntScalarMemOp:$src),
              !strconcat(asm,"\t{$src, $dst|$dst, $src}"),
              [(set _DstRC.RC:$dst,
                (OpNodeInt (_SrcRC.ScalarIntMemFrags addr:$src)))]>,
              EVEX, VEX_LIG, Sched<[sched.Folded, sched.ReadAfterFold]>, SIMD_EXC;
} // Predicates = [prd]

  def : InstAlias<asm # aliasStr # "\t{$src, $dst|$dst, $src}",
          (!cast<Instruction>(NAME # "rr_Int") _DstRC.RC:$dst, _SrcRC.RC:$src), 0, "att">;
  def : InstAlias<asm # aliasStr # "\t{{sae}, $src, $dst|$dst, $src, {sae}}",
          (!cast<Instruction>(NAME # "rrb_Int") _DstRC.RC:$dst, _SrcRC.RC:$src), 0, "att">;
  def : InstAlias<asm # aliasStr # "\t{$src, $dst|$dst, $src}",
          (!cast<Instruction>(NAME # "rm_Int") _DstRC.RC:$dst,
                                          _SrcRC.IntScalarMemOp:$src), 0, "att">;
}

defm VCVTTSS2SIZ: avx512_cvt_s_all<0x2C, "vcvttss2si", f32x_info, i32x_info,
                        any_fp_to_sint, X86cvtts2Int, X86cvtts2IntSAE, WriteCvtSS2I,
                        "{l}">, TB, XS, EVEX_CD8<32, CD8VT1>;
defm VCVTTSS2SI64Z: avx512_cvt_s_all<0x2C, "vcvttss2si", f32x_info, i64x_info,
                        any_fp_to_sint, X86cvtts2Int, X86cvtts2IntSAE, WriteCvtSS2I,
                        "{q}">, REX_W, TB, XS, EVEX_CD8<32, CD8VT1>;
defm VCVTTSD2SIZ: avx512_cvt_s_all<0x2C, "vcvttsd2si", f64x_info, i32x_info,
                        any_fp_to_sint, X86cvtts2Int, X86cvtts2IntSAE, WriteCvtSD2I,
                        "{l}">, TB, XD, EVEX_CD8<64, CD8VT1>;
defm VCVTTSD2SI64Z: avx512_cvt_s_all<0x2C, "vcvttsd2si", f64x_info, i64x_info,
                        any_fp_to_sint, X86cvtts2Int, X86cvtts2IntSAE, WriteCvtSD2I,
                        "{q}">, REX_W, TB, XD, EVEX_CD8<64, CD8VT1>;

defm VCVTTSS2USIZ: avx512_cvt_s_all<0x78, "vcvttss2usi", f32x_info, i32x_info,
                        any_fp_to_uint, X86cvtts2UInt, X86cvtts2UIntSAE, WriteCvtSS2I,
                        "{l}">, TB, XS, EVEX_CD8<32, CD8VT1>;
defm VCVTTSS2USI64Z: avx512_cvt_s_all<0x78, "vcvttss2usi", f32x_info, i64x_info,
                        any_fp_to_uint, X86cvtts2UInt, X86cvtts2UIntSAE, WriteCvtSS2I,
                        "{q}">, TB, XS,REX_W, EVEX_CD8<32, CD8VT1>;
defm VCVTTSD2USIZ: avx512_cvt_s_all<0x78, "vcvttsd2usi", f64x_info, i32x_info,
                        any_fp_to_uint, X86cvtts2UInt, X86cvtts2UIntSAE, WriteCvtSD2I,
                        "{l}">, TB, XD, EVEX_CD8<64, CD8VT1>;
defm VCVTTSD2USI64Z: avx512_cvt_s_all<0x78, "vcvttsd2usi", f64x_info, i64x_info,
                        any_fp_to_uint, X86cvtts2UInt, X86cvtts2UIntSAE, WriteCvtSD2I,
                        "{q}">, TB, XD, REX_W, EVEX_CD8<64, CD8VT1>;

//===----------------------------------------------------------------------===//
// AVX-512  Convert form float to double and back
//===----------------------------------------------------------------------===//

let Uses = [MXCSR], mayRaiseFPException = 1 in
multiclass avx512_cvt_fp_scalar<bits<8> opc, string OpcodeStr, X86VectorVTInfo _,
                                X86VectorVTInfo _Src, SDNode OpNode,
                                X86FoldableSchedWrite sched> {
  defm rr_Int : AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                         (ins _.RC:$src1, _Src.RC:$src2), OpcodeStr,
                         "$src2, $src1", "$src1, $src2",
                         (_.VT (OpNode (_.VT _.RC:$src1),
                                       (_Src.VT _Src.RC:$src2)))>,
                         EVEX, VVVV, VEX_LIG, Sched<[sched]>;
  defm rm_Int : AVX512_maskable_scalar<opc, MRMSrcMem, _, (outs _.RC:$dst),
                         (ins _.RC:$src1, _Src.IntScalarMemOp:$src2), OpcodeStr,
                         "$src2, $src1", "$src1, $src2",
                         (_.VT (OpNode (_.VT _.RC:$src1),
                                  (_Src.ScalarIntMemFrags addr:$src2)))>,
                         EVEX, VVVV, VEX_LIG,
                         Sched<[sched.Folded, sched.ReadAfterFold]>;

  let isCodeGenOnly = 1, hasSideEffects = 0 in {
    def rr : I<opc, MRMSrcReg, (outs _.FRC:$dst),
               (ins _.FRC:$src1, _Src.FRC:$src2),
               OpcodeStr#"\t{$src2, $src1, $dst|$dst, $src1, $src2}", []>,
               EVEX, VVVV, VEX_LIG, Sched<[sched]>;
    let mayLoad = 1 in
    def rm : I<opc, MRMSrcMem, (outs _.FRC:$dst),
               (ins _.FRC:$src1, _Src.ScalarMemOp:$src2),
               OpcodeStr#"\t{$src2, $src1, $dst|$dst, $src1, $src2}", []>,
               EVEX, VVVV, VEX_LIG, Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

// Scalar Conversion with SAE - suppress all exceptions
multiclass avx512_cvt_fp_sae_scalar<bits<8> opc, string OpcodeStr, X86VectorVTInfo _,
                                    X86VectorVTInfo _Src, SDNode OpNodeSAE,
                                    X86FoldableSchedWrite sched> {
  let Uses = [MXCSR] in
  defm rrb_Int : AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                        (ins _.RC:$src1, _Src.RC:$src2), OpcodeStr,
                        "{sae}, $src2, $src1", "$src1, $src2, {sae}",
                        (_.VT (OpNodeSAE (_.VT _.RC:$src1),
                                         (_Src.VT _Src.RC:$src2)))>,
                        EVEX, VVVV, VEX_LIG, EVEX_B, Sched<[sched]>;
}

// Scalar Conversion with rounding control (RC)
multiclass avx512_cvt_fp_rc_scalar<bits<8> opc, string OpcodeStr, X86VectorVTInfo _,
                                   X86VectorVTInfo _Src, SDNode OpNodeRnd,
                                   X86FoldableSchedWrite sched> {
  let Uses = [MXCSR] in
  defm rrb_Int : AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                        (ins _.RC:$src1, _Src.RC:$src2, AVX512RC:$rc), OpcodeStr,
                        "$rc, $src2, $src1", "$src1, $src2, $rc",
                        (_.VT (OpNodeRnd (_.VT _.RC:$src1),
                                         (_Src.VT _Src.RC:$src2), (i32 timm:$rc)))>,
                        EVEX, VVVV, VEX_LIG, Sched<[sched]>,
                        EVEX_B, EVEX_RC;
}
multiclass avx512_cvt_fp_scalar_trunc<bits<8> opc, string OpcodeStr,
                                      SDNode OpNode, SDNode OpNodeRnd,
                                      X86FoldableSchedWrite sched,
                                      X86VectorVTInfo _src, X86VectorVTInfo _dst,
                                      Predicate prd = HasAVX512> {
  let Predicates = [prd], ExeDomain = SSEPackedSingle in {
    defm Z : avx512_cvt_fp_scalar<opc, OpcodeStr, _dst, _src, OpNode, sched>,
             avx512_cvt_fp_rc_scalar<opc, OpcodeStr, _dst, _src,
                               OpNodeRnd, sched>, EVEX_CD8<_src.EltSize, CD8VT1>;
  }
}

multiclass avx512_cvt_fp_scalar_extend<bits<8> opc, string OpcodeStr,
                                       SDNode OpNode, SDNode OpNodeSAE,
                                       X86FoldableSchedWrite sched,
                                       X86VectorVTInfo _src, X86VectorVTInfo _dst,
                                       Predicate prd = HasAVX512> {
  let Predicates = [prd], ExeDomain = SSEPackedSingle in {
    defm Z : avx512_cvt_fp_scalar<opc, OpcodeStr, _dst, _src, OpNode, sched>,
             avx512_cvt_fp_sae_scalar<opc, OpcodeStr, _dst, _src, OpNodeSAE, sched>,
             EVEX_CD8<_src.EltSize, CD8VT1>;
  }
}
defm VCVTSD2SS : avx512_cvt_fp_scalar_trunc<0x5A, "vcvtsd2ss", X86frounds,
                                         X86froundsRnd, WriteCvtSD2SS, f64x_info,
                                         f32x_info>, TB, XD, REX_W;
defm VCVTSS2SD : avx512_cvt_fp_scalar_extend<0x5A, "vcvtss2sd", X86fpexts,
                                          X86fpextsSAE, WriteCvtSS2SD, f32x_info,
                                          f64x_info>, TB, XS;
defm VCVTSD2SH : avx512_cvt_fp_scalar_trunc<0x5A, "vcvtsd2sh", X86frounds,
                                          X86froundsRnd, WriteCvtSD2SS, f64x_info,
                                          f16x_info, HasFP16>, T_MAP5, XD, REX_W;
defm VCVTSH2SD : avx512_cvt_fp_scalar_extend<0x5A, "vcvtsh2sd", X86fpexts,
                                          X86fpextsSAE, WriteCvtSS2SD, f16x_info,
                                          f64x_info, HasFP16>, T_MAP5, XS;
defm VCVTSS2SH : avx512_cvt_fp_scalar_trunc<0x1D, "vcvtss2sh", X86frounds,
                                          X86froundsRnd, WriteCvtSD2SS, f32x_info,
                                          f16x_info, HasFP16>, T_MAP5;
defm VCVTSH2SS : avx512_cvt_fp_scalar_extend<0x13, "vcvtsh2ss", X86fpexts,
                                          X86fpextsSAE, WriteCvtSS2SD, f16x_info,
                                          f32x_info, HasFP16>, T_MAP6;

def : Pat<(f64 (any_fpextend FR32X:$src)),
          (VCVTSS2SDZrr (f64 (IMPLICIT_DEF)), FR32X:$src)>,
          Requires<[HasAVX512]>;
def : Pat<(f64 (any_fpextend (loadf32 addr:$src))),
          (VCVTSS2SDZrm (f64 (IMPLICIT_DEF)), addr:$src)>,
          Requires<[HasAVX512, OptForSize]>;

def : Pat<(f32 (any_fpround FR64X:$src)),
          (VCVTSD2SSZrr (f32 (IMPLICIT_DEF)), FR64X:$src)>,
           Requires<[HasAVX512]>;

def : Pat<(f32 (any_fpextend FR16X:$src)),
          (VCVTSH2SSZrr (f32 (IMPLICIT_DEF)), FR16X:$src)>,
          Requires<[HasFP16]>;
def : Pat<(f32 (any_fpextend (loadf16 addr:$src))),
          (VCVTSH2SSZrm (f32 (IMPLICIT_DEF)), addr:$src)>,
          Requires<[HasFP16, OptForSize]>;

def : Pat<(f64 (any_fpextend FR16X:$src)),
          (VCVTSH2SDZrr (f64 (IMPLICIT_DEF)), FR16X:$src)>,
          Requires<[HasFP16]>;
def : Pat<(f64 (any_fpextend (loadf16 addr:$src))),
          (VCVTSH2SDZrm (f64 (IMPLICIT_DEF)), addr:$src)>,
          Requires<[HasFP16, OptForSize]>;

def : Pat<(f16 (any_fpround FR32X:$src)),
          (VCVTSS2SHZrr (f16 (IMPLICIT_DEF)), FR32X:$src)>,
           Requires<[HasFP16]>;
def : Pat<(f16 (any_fpround FR64X:$src)),
          (VCVTSD2SHZrr (f16 (IMPLICIT_DEF)), FR64X:$src)>,
           Requires<[HasFP16]>;

def : Pat<(v4f32 (X86Movss
                   (v4f32 VR128X:$dst),
                   (v4f32 (scalar_to_vector
                     (f32 (any_fpround (f64 (extractelt VR128X:$src, (iPTR 0))))))))),
          (VCVTSD2SSZrr_Int VR128X:$dst, VR128X:$src)>,
          Requires<[HasAVX512]>;

def : Pat<(v2f64 (X86Movsd
                   (v2f64 VR128X:$dst),
                   (v2f64 (scalar_to_vector
                     (f64 (any_fpextend (f32 (extractelt VR128X:$src, (iPTR 0))))))))),
          (VCVTSS2SDZrr_Int VR128X:$dst, VR128X:$src)>,
          Requires<[HasAVX512]>;

//===----------------------------------------------------------------------===//
// AVX-512  Vector convert from signed/unsigned integer to float/double
//          and from float/double to signed/unsigned integer
//===----------------------------------------------------------------------===//

multiclass avx512_vcvt_fp<bits<8> opc, string OpcodeStr, X86VectorVTInfo _,
                          X86VectorVTInfo _Src, SDPatternOperator OpNode, SDPatternOperator MaskOpNode,
                          X86FoldableSchedWrite sched,
                          string Broadcast = _.BroadcastStr,
                          string Alias = "", X86MemOperand MemOp = _Src.MemOp,
                          RegisterClass MaskRC = _.KRCWM,
                          dag LdDAG = (_.VT (OpNode (_Src.VT (_Src.LdFrag addr:$src)))),
                          dag MaskLdDAG = (_.VT (MaskOpNode (_Src.VT (_Src.LdFrag addr:$src))))> {
let Uses = [MXCSR], mayRaiseFPException = 1 in {
  defm rr : AVX512_maskable_cvt<opc, MRMSrcReg, _, (outs _.RC:$dst),
                         (ins _Src.RC:$src),
                         (ins _.RC:$src0, MaskRC:$mask, _Src.RC:$src),
                         (ins MaskRC:$mask, _Src.RC:$src),
                          OpcodeStr, "$src", "$src",
                         (_.VT (OpNode (_Src.VT _Src.RC:$src))),
                         (vselect_mask MaskRC:$mask,
                                       (_.VT (MaskOpNode (_Src.VT _Src.RC:$src))),
                                       _.RC:$src0),
                         (vselect_mask MaskRC:$mask,
                                       (_.VT (MaskOpNode (_Src.VT _Src.RC:$src))),
                                       _.ImmAllZerosV)>,
                         EVEX, Sched<[sched]>;

  defm rm : AVX512_maskable_cvt<opc, MRMSrcMem, _, (outs _.RC:$dst),
                         (ins MemOp:$src),
                         (ins _.RC:$src0, MaskRC:$mask, MemOp:$src),
                         (ins MaskRC:$mask, MemOp:$src),
                         OpcodeStr#Alias, "$src", "$src",
                         LdDAG,
                         (vselect_mask MaskRC:$mask, MaskLdDAG, _.RC:$src0),
                         (vselect_mask MaskRC:$mask, MaskLdDAG, _.ImmAllZerosV)>,
                         EVEX, Sched<[sched.Folded]>;

  defm rmb : AVX512_maskable_cvt<opc, MRMSrcMem, _, (outs _.RC:$dst),
                         (ins _Src.ScalarMemOp:$src),
                         (ins _.RC:$src0, MaskRC:$mask, _Src.ScalarMemOp:$src),
                         (ins MaskRC:$mask, _Src.ScalarMemOp:$src),
                         OpcodeStr,
                         "${src}"#Broadcast, "${src}"#Broadcast,
                         (_.VT (OpNode (_Src.VT
                                  (_Src.BroadcastLdFrag addr:$src))
                            )),
                         (vselect_mask MaskRC:$mask,
                                       (_.VT
                                        (MaskOpNode
                                         (_Src.VT
                                          (_Src.BroadcastLdFrag addr:$src)))),
                                       _.RC:$src0),
                         (vselect_mask MaskRC:$mask,
                                       (_.VT
                                        (MaskOpNode
                                         (_Src.VT
                                          (_Src.BroadcastLdFrag addr:$src)))),
                                       _.ImmAllZerosV)>,
                         EVEX, EVEX_B, Sched<[sched.Folded]>;
  }
}
// Conversion with SAE - suppress all exceptions
multiclass avx512_vcvt_fp_sae<bits<8> opc, string OpcodeStr, X86VectorVTInfo _,
                              X86VectorVTInfo _Src, SDNode OpNodeSAE,
                              X86FoldableSchedWrite sched> {
  let Uses = [MXCSR] in
  defm rrb : AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                        (ins _Src.RC:$src), OpcodeStr,
                        "{sae}, $src", "$src, {sae}",
                        (_.VT (OpNodeSAE (_Src.VT _Src.RC:$src)))>,
                        EVEX, EVEX_B, Sched<[sched]>;
}

// Conversion with rounding control (RC)
multiclass avx512_vcvt_fp_rc<bits<8> opc, string OpcodeStr, X86VectorVTInfo _,
                         X86VectorVTInfo _Src, SDPatternOperator OpNodeRnd,
                         X86FoldableSchedWrite sched> {
  let Uses = [MXCSR] in
  defm rrb : AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                        (ins _Src.RC:$src, AVX512RC:$rc), OpcodeStr,
                        "$rc, $src", "$src, $rc",
                        (_.VT (OpNodeRnd (_Src.VT _Src.RC:$src), (i32 timm:$rc)))>,
                        EVEX, EVEX_B, EVEX_RC, Sched<[sched]>;
}

// Similar to avx512_vcvt_fp, but uses an extload for the memory form.
multiclass avx512_vcvt_fpextend<bits<8> opc, string OpcodeStr, X86VectorVTInfo _,
                                X86VectorVTInfo _Src, SDPatternOperator OpNode,
                                SDNode MaskOpNode,
                                X86FoldableSchedWrite sched,
                                string Broadcast = _.BroadcastStr,
                                string Alias = "", X86MemOperand MemOp = _Src.MemOp,
                                RegisterClass MaskRC = _.KRCWM>
  : avx512_vcvt_fp<opc, OpcodeStr, _, _Src, OpNode, MaskOpNode, sched, Broadcast,
                   Alias, MemOp, MaskRC,
                   (_.VT (!cast<PatFrag>("extload"#_Src.VTName) addr:$src)),
                   (_.VT (!cast<PatFrag>("extload"#_Src.VTName) addr:$src))>;

// Extend [Float to Double, Half to Float]
multiclass avx512_cvt_extend<bits<8> opc, string OpcodeStr,
                             AVX512VLVectorVTInfo _dst, AVX512VLVectorVTInfo _src,
                             X86SchedWriteWidths sched, Predicate prd = HasAVX512> {
  let Predicates = [prd] in {
    defm Z : avx512_vcvt_fpextend<opc, OpcodeStr,  _dst.info512, _src.info256,
                            any_fpextend, fpextend, sched.ZMM>,
             avx512_vcvt_fp_sae<opc, OpcodeStr, _dst.info512, _src.info256,
                                X86vfpextSAE, sched.ZMM>, EVEX_V512;
  }
  let Predicates = [prd, HasVLX] in {
    defm Z128 : avx512_vcvt_fpextend<opc, OpcodeStr, _dst.info128, _src.info128,
                               X86any_vfpext, X86vfpext, sched.XMM,
                               _dst.info128.BroadcastStr,
                               "", f64mem>, EVEX_V128;
    defm Z256 : avx512_vcvt_fpextend<opc, OpcodeStr, _dst.info256, _src.info128,
                               any_fpextend, fpextend, sched.YMM>, EVEX_V256;
  }
}

// Truncate [Double to Float, Float to Half]
multiclass avx512_cvt_trunc<bits<8> opc, string OpcodeStr,
                            AVX512VLVectorVTInfo _dst, AVX512VLVectorVTInfo _src,
                            X86SchedWriteWidths sched, Predicate prd = HasAVX512,
                            PatFrag bcast128 = _src.info128.BroadcastLdFrag,
                            PatFrag loadVT128 = _src.info128.LdFrag,
                            RegisterClass maskRC128 = _src.info128.KRCWM> {
  let Predicates = [prd] in {
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, _dst.info256, _src.info512,
                            X86any_vfpround, X86vfpround, sched.ZMM>,
             avx512_vcvt_fp_rc<opc, OpcodeStr, _dst.info256, _src.info512,
                               X86vfproundRnd, sched.ZMM>, EVEX_V512;
  }
  let Predicates = [prd, HasVLX] in {
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, _dst.info128, _src.info128,
                               null_frag, null_frag, sched.XMM,
                               _src.info128.BroadcastStr, "{x}",
                               f128mem, maskRC128>, EVEX_V128;
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, _dst.info128, _src.info256,
                               X86any_vfpround, X86vfpround,
                               sched.YMM, _src.info256.BroadcastStr, "{y}">, EVEX_V256;

    // Special patterns to allow use of X86vmfpround for masking. Instruction
    // patterns have been disabled with null_frag.
    def : Pat<(_dst.info128.VT (X86any_vfpround (_src.info128.VT VR128X:$src))),
              (!cast<Instruction>(NAME # "Z128rr") VR128X:$src)>;
    def : Pat<(X86vmfpround (_src.info128.VT VR128X:$src), (_dst.info128.VT VR128X:$src0),
                            maskRC128:$mask),
              (!cast<Instruction>(NAME # "Z128rrk") VR128X:$src0, maskRC128:$mask, VR128X:$src)>;
    def : Pat<(X86vmfpround (_src.info128.VT VR128X:$src), _dst.info128.ImmAllZerosV,
                            maskRC128:$mask),
              (!cast<Instruction>(NAME # "Z128rrkz") maskRC128:$mask, VR128X:$src)>;

    def : Pat<(_dst.info128.VT (X86any_vfpround (loadVT128 addr:$src))),
              (!cast<Instruction>(NAME # "Z128rm") addr:$src)>;
    def : Pat<(X86vmfpround (loadVT128 addr:$src), (_dst.info128.VT VR128X:$src0),
                            maskRC128:$mask),
              (!cast<Instruction>(NAME # "Z128rmk") VR128X:$src0, maskRC128:$mask, addr:$src)>;
    def : Pat<(X86vmfpround (loadVT128 addr:$src), _dst.info128.ImmAllZerosV,
                            maskRC128:$mask),
              (!cast<Instruction>(NAME # "Z128rmkz") maskRC128:$mask, addr:$src)>;

    def : Pat<(_dst.info128.VT (X86any_vfpround (_src.info128.VT (bcast128 addr:$src)))),
              (!cast<Instruction>(NAME # "Z128rmb") addr:$src)>;
    def : Pat<(X86vmfpround (_src.info128.VT (bcast128 addr:$src)),
                            (_dst.info128.VT VR128X:$src0), maskRC128:$mask),
              (!cast<Instruction>(NAME # "Z128rmbk") VR128X:$src0, maskRC128:$mask, addr:$src)>;
    def : Pat<(X86vmfpround (_src.info128.VT (bcast128 addr:$src)),
                            _dst.info128.ImmAllZerosV, maskRC128:$mask),
              (!cast<Instruction>(NAME # "Z128rmbkz") maskRC128:$mask, addr:$src)>;
  }

  def : InstAlias<OpcodeStr#"x\t{$src, $dst|$dst, $src}",
                  (!cast<Instruction>(NAME # "Z128rr") VR128X:$dst, VR128X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"x\t{$src, $dst {${mask}}|$dst {${mask}}, $src}",
                  (!cast<Instruction>(NAME # "Z128rrk") VR128X:$dst,
                  VK2WM:$mask, VR128X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"x\t{$src, $dst {${mask}} {z}|"
                  "$dst {${mask}} {z}, $src}",
                  (!cast<Instruction>(NAME # "Z128rrkz") VR128X:$dst,
                  VK2WM:$mask, VR128X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"x\t{${src}{1to2}, $dst|$dst, ${src}{1to2}}",
                  (!cast<Instruction>(NAME # "Z128rmb") VR128X:$dst, f64mem:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"x\t{${src}{1to2}, $dst {${mask}}|"
                  "$dst {${mask}}, ${src}{1to2}}",
                  (!cast<Instruction>(NAME # "Z128rmbk") VR128X:$dst,
                  VK2WM:$mask, f64mem:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"x\t{${src}{1to2}, $dst {${mask}} {z}|"
                  "$dst {${mask}} {z}, ${src}{1to2}}",
                  (!cast<Instruction>(NAME # "Z128rmbkz") VR128X:$dst,
                  VK2WM:$mask, f64mem:$src), 0, "att">;

  def : InstAlias<OpcodeStr#"y\t{$src, $dst|$dst, $src}",
                  (!cast<Instruction>(NAME # "Z256rr") VR128X:$dst, VR256X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"y\t{$src, $dst {${mask}}|$dst {${mask}}, $src}",
                  (!cast<Instruction>(NAME # "Z256rrk") VR128X:$dst,
                  VK4WM:$mask, VR256X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"y\t{$src, $dst {${mask}} {z}|"
                  "$dst {${mask}} {z}, $src}",
                  (!cast<Instruction>(NAME # "Z256rrkz") VR128X:$dst,
                  VK4WM:$mask, VR256X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"y\t{${src}{1to4}, $dst|$dst, ${src}{1to4}}",
                  (!cast<Instruction>(NAME # "Z256rmb") VR128X:$dst, f64mem:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"y\t{${src}{1to4}, $dst {${mask}}|"
                  "$dst {${mask}}, ${src}{1to4}}",
                  (!cast<Instruction>(NAME # "Z256rmbk") VR128X:$dst,
                  VK4WM:$mask, f64mem:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"y\t{${src}{1to4}, $dst {${mask}} {z}|"
                  "$dst {${mask}} {z}, ${src}{1to4}}",
                  (!cast<Instruction>(NAME # "Z256rmbkz") VR128X:$dst,
                  VK4WM:$mask, f64mem:$src), 0, "att">;
}

defm VCVTPD2PS : avx512_cvt_trunc<0x5A, "vcvtpd2ps",
                                  avx512vl_f32_info, avx512vl_f64_info, SchedWriteCvtPD2PS>,
                                  REX_W, TB, PD, EVEX_CD8<64, CD8VF>;
defm VCVTPS2PD : avx512_cvt_extend<0x5A, "vcvtps2pd",
                                   avx512vl_f64_info, avx512vl_f32_info, SchedWriteCvtPS2PD>,
                                   TB, EVEX_CD8<32, CD8VH>;

// Extend Half to Double
multiclass avx512_cvtph2pd<bits<8> opc, string OpcodeStr,
                            X86SchedWriteWidths sched> {
  let Predicates = [HasFP16] in {
    defm Z : avx512_vcvt_fpextend<opc, OpcodeStr, v8f64_info, v8f16x_info,
                                  any_fpextend, fpextend, sched.ZMM>,
             avx512_vcvt_fp_sae<opc, OpcodeStr, v8f64_info, v8f16x_info,
                                X86vfpextSAE, sched.ZMM>, EVEX_V512;
    def : Pat<(v8f64 (extloadv8f16 addr:$src)),
                (!cast<Instruction>(NAME # "Zrm") addr:$src)>;
  }
  let Predicates = [HasFP16, HasVLX] in {
    defm Z128 : avx512_vcvt_fpextend<opc, OpcodeStr, v2f64x_info, v8f16x_info,
                                     X86any_vfpext, X86vfpext, sched.XMM, "{1to2}", "",
                                     f32mem>, EVEX_V128;
    defm Z256 : avx512_vcvt_fpextend<opc, OpcodeStr, v4f64x_info, v8f16x_info,
                                     X86any_vfpext, X86vfpext, sched.YMM, "{1to4}", "",
                                     f64mem>, EVEX_V256;
  }
}

// Truncate Double to Half
multiclass avx512_cvtpd2ph<bits<8> opc, string OpcodeStr, X86SchedWriteWidths sched> {
  let Predicates = [HasFP16] in {
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, v8f16x_info, v8f64_info,
                            X86any_vfpround, X86vfpround, sched.ZMM, "{1to8}", "{z}">,
             avx512_vcvt_fp_rc<opc, OpcodeStr, v8f16x_info, v8f64_info,
                               X86vfproundRnd, sched.ZMM>, EVEX_V512;
  }
  let Predicates = [HasFP16, HasVLX] in {
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, v8f16x_info, v2f64x_info, null_frag,
                               null_frag, sched.XMM, "{1to2}", "{x}", f128mem,
                               VK2WM>, EVEX_V128;
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, v8f16x_info, v4f64x_info, null_frag,
                               null_frag, sched.YMM, "{1to4}", "{y}", f256mem,
                               VK4WM>, EVEX_V256;
  }
  def : InstAlias<OpcodeStr#"x\t{$src, $dst|$dst, $src}",
                  (!cast<Instruction>(NAME # "Z128rr") VR128X:$dst,
                  VR128X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"x\t{$src, $dst {${mask}}|$dst {${mask}}, $src}",
                  (!cast<Instruction>(NAME # "Z128rrk") VR128X:$dst,
                  VK2WM:$mask, VR128X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"x\t{$src, $dst {${mask}} {z}|$dst {${mask}} {z}, $src}",
                  (!cast<Instruction>(NAME # "Z128rrkz") VR128X:$dst,
                  VK2WM:$mask, VR128X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"x\t{${src}{1to2}, $dst|$dst, ${src}{1to2}}",
                  (!cast<Instruction>(NAME # "Z128rmb") VR128X:$dst,
                  i64mem:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"x\t{${src}{1to2}, $dst {${mask}}|"
                  "$dst {${mask}}, ${src}{1to2}}",
                  (!cast<Instruction>(NAME # "Z128rmbk") VR128X:$dst,
                  VK2WM:$mask, i64mem:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"x\t{${src}{1to2}, $dst {${mask}} {z}|"
                  "$dst {${mask}} {z}, ${src}{1to2}}",
                  (!cast<Instruction>(NAME # "Z128rmbkz") VR128X:$dst,
                  VK2WM:$mask, i64mem:$src), 0, "att">;

  def : InstAlias<OpcodeStr#"y\t{$src, $dst|$dst, $src}",
                  (!cast<Instruction>(NAME # "Z256rr") VR128X:$dst,
                  VR256X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"y\t{$src, $dst {${mask}}|"
                  "$dst {${mask}}, $src}",
                  (!cast<Instruction>(NAME # "Z256rrk") VR128X:$dst,
                  VK4WM:$mask, VR256X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"y\t{$src, $dst {${mask}} {z}|"
                  "$dst {${mask}} {z}, $src}",
                  (!cast<Instruction>(NAME # "Z256rrkz") VR128X:$dst,
                  VK4WM:$mask, VR256X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"y\t{${src}{1to4}, $dst|$dst, ${src}{1to4}}",
                  (!cast<Instruction>(NAME # "Z256rmb") VR128X:$dst,
                  i64mem:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"y\t{${src}{1to4}, $dst {${mask}}|"
                  "$dst {${mask}}, ${src}{1to4}}",
                  (!cast<Instruction>(NAME # "Z256rmbk") VR128X:$dst,
                  VK4WM:$mask, i64mem:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"y\t{${src}{1to4}, $dst {${mask}} {z}|"
                  "$dst {${mask}} {z}, ${src}{1to4}}",
                  (!cast<Instruction>(NAME # "Z256rmbkz") VR128X:$dst,
                  VK4WM:$mask, i64mem:$src), 0, "att">;

  def : InstAlias<OpcodeStr#"z\t{$src, $dst|$dst, $src}",
                  (!cast<Instruction>(NAME # "Zrr") VR128X:$dst,
                  VR512:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"z\t{$src, $dst {${mask}}|"
                  "$dst {${mask}}, $src}",
                  (!cast<Instruction>(NAME # "Zrrk") VR128X:$dst,
                  VK8WM:$mask, VR512:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"z\t{$src, $dst {${mask}} {z}|"
                  "$dst {${mask}} {z}, $src}",
                  (!cast<Instruction>(NAME # "Zrrkz") VR128X:$dst,
                  VK8WM:$mask, VR512:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"z\t{${src}{1to8}, $dst|$dst, ${src}{1to8}}",
                  (!cast<Instruction>(NAME # "Zrmb") VR128X:$dst,
                  i64mem:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"z\t{${src}{1to8}, $dst {${mask}}|"
                  "$dst {${mask}}, ${src}{1to8}}",
                  (!cast<Instruction>(NAME # "Zrmbk") VR128X:$dst,
                  VK8WM:$mask, i64mem:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"z\t{${src}{1to8}, $dst {${mask}} {z}|"
                  "$dst {${mask}} {z}, ${src}{1to8}}",
                  (!cast<Instruction>(NAME # "Zrmbkz") VR128X:$dst,
                  VK8WM:$mask, i64mem:$src), 0, "att">;
}

defm VCVTPS2PHX : avx512_cvt_trunc<0x1D, "vcvtps2phx", avx512vl_f16_info,
                                   avx512vl_f32_info, SchedWriteCvtPD2PS,
                                   HasFP16>, T_MAP5, PD, EVEX_CD8<32, CD8VF>;
defm VCVTPH2PSX : avx512_cvt_extend<0x13, "vcvtph2psx", avx512vl_f32_info,
                                    avx512vl_f16_info, SchedWriteCvtPS2PD,
                                    HasFP16>, T_MAP6, PD, EVEX_CD8<16, CD8VH>;
defm VCVTPD2PH : avx512_cvtpd2ph<0x5A, "vcvtpd2ph", SchedWriteCvtPD2PS>,
                                 REX_W, T_MAP5, PD, EVEX_CD8<64, CD8VF>;
defm VCVTPH2PD : avx512_cvtph2pd<0x5A, "vcvtph2pd", SchedWriteCvtPS2PD>,
                                 T_MAP5, EVEX_CD8<16, CD8VQ>;

let Predicates = [HasFP16, HasVLX] in {
  // Special patterns to allow use of X86vmfpround for masking. Instruction
  // patterns have been disabled with null_frag.
  def : Pat<(v8f16 (X86any_vfpround (v4f64 VR256X:$src))),
            (VCVTPD2PHZ256rr VR256X:$src)>;
  def : Pat<(v8f16 (X86vmfpround (v4f64 VR256X:$src), (v8f16 VR128X:$src0),
                          VK4WM:$mask)),
            (VCVTPD2PHZ256rrk VR128X:$src0, VK4WM:$mask, VR256X:$src)>;
  def : Pat<(X86vmfpround (v4f64 VR256X:$src), v8f16x_info.ImmAllZerosV,
                          VK4WM:$mask),
            (VCVTPD2PHZ256rrkz VK4WM:$mask, VR256X:$src)>;

  def : Pat<(v8f16 (X86any_vfpround (loadv4f64 addr:$src))),
            (VCVTPD2PHZ256rm addr:$src)>;
  def : Pat<(X86vmfpround (loadv4f64 addr:$src), (v8f16 VR128X:$src0),
                          VK4WM:$mask),
            (VCVTPD2PHZ256rmk VR128X:$src0, VK4WM:$mask, addr:$src)>;
  def : Pat<(X86vmfpround (loadv4f64 addr:$src), v8f16x_info.ImmAllZerosV,
                          VK4WM:$mask),
            (VCVTPD2PHZ256rmkz VK4WM:$mask, addr:$src)>;

  def : Pat<(v8f16 (X86any_vfpround (v4f64 (X86VBroadcastld64 addr:$src)))),
            (VCVTPD2PHZ256rmb addr:$src)>;
  def : Pat<(X86vmfpround (v4f64 (X86VBroadcastld64 addr:$src)),
                          (v8f16 VR128X:$src0), VK4WM:$mask),
            (VCVTPD2PHZ256rmbk VR128X:$src0, VK4WM:$mask, addr:$src)>;
  def : Pat<(X86vmfpround (v4f64 (X86VBroadcastld64 addr:$src)),
                          v8f16x_info.ImmAllZerosV, VK4WM:$mask),
            (VCVTPD2PHZ256rmbkz VK4WM:$mask, addr:$src)>;

  def : Pat<(v8f16 (X86any_vfpround (v2f64 VR128X:$src))),
            (VCVTPD2PHZ128rr VR128X:$src)>;
  def : Pat<(X86vmfpround (v2f64 VR128X:$src), (v8f16 VR128X:$src0),
                          VK2WM:$mask),
            (VCVTPD2PHZ128rrk VR128X:$src0, VK2WM:$mask, VR128X:$src)>;
  def : Pat<(X86vmfpround (v2f64 VR128X:$src), v8f16x_info.ImmAllZerosV,
                          VK2WM:$mask),
            (VCVTPD2PHZ128rrkz VK2WM:$mask, VR128X:$src)>;

  def : Pat<(v8f16 (X86any_vfpround (loadv2f64 addr:$src))),
            (VCVTPD2PHZ128rm addr:$src)>;
  def : Pat<(X86vmfpround (loadv2f64 addr:$src), (v8f16 VR128X:$src0),
                          VK2WM:$mask),
            (VCVTPD2PHZ128rmk VR128X:$src0, VK2WM:$mask, addr:$src)>;
  def : Pat<(X86vmfpround (loadv2f64 addr:$src), v8f16x_info.ImmAllZerosV,
                          VK2WM:$mask),
            (VCVTPD2PHZ128rmkz VK2WM:$mask, addr:$src)>;

  def : Pat<(v8f16 (X86any_vfpround (v2f64 (X86VBroadcastld64 addr:$src)))),
            (VCVTPD2PHZ128rmb addr:$src)>;
  def : Pat<(X86vmfpround (v2f64 (X86VBroadcastld64 addr:$src)),
                          (v8f16 VR128X:$src0), VK2WM:$mask),
            (VCVTPD2PHZ128rmbk VR128X:$src0, VK2WM:$mask, addr:$src)>;
  def : Pat<(X86vmfpround (v2f64 (X86VBroadcastld64 addr:$src)),
                          v8f16x_info.ImmAllZerosV, VK2WM:$mask),
            (VCVTPD2PHZ128rmbkz VK2WM:$mask, addr:$src)>;
}

// Convert Signed/Unsigned Doubleword to Double
let Uses = []<Register>, mayRaiseFPException = 0 in
multiclass avx512_cvtdq2pd<bits<8> opc, string OpcodeStr, SDPatternOperator OpNode,
                           SDNode MaskOpNode, SDPatternOperator OpNode128,
                           SDNode MaskOpNode128,
                           X86SchedWriteWidths sched> {
  // No rounding in this op
  let Predicates = [HasAVX512] in
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, v8f64_info, v8i32x_info, OpNode,
                            MaskOpNode, sched.ZMM>, EVEX_V512;

  let Predicates = [HasVLX] in {
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, v2f64x_info, v4i32x_info,
                               OpNode128, MaskOpNode128, sched.XMM, "{1to2}",
                               "", i64mem, VK2WM,
                               (v2f64 (OpNode128 (bc_v4i32
                                (v2i64
                                 (scalar_to_vector (loadi64 addr:$src)))))),
                               (v2f64 (MaskOpNode128 (bc_v4i32
                                (v2i64
                                 (scalar_to_vector (loadi64 addr:$src))))))>,
                               EVEX_V128;
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, v4f64x_info, v4i32x_info, OpNode,
                               MaskOpNode, sched.YMM>, EVEX_V256;
  }
}

// Convert Signed/Unsigned Doubleword to Float
multiclass avx512_cvtdq2ps<bits<8> opc, string OpcodeStr, SDPatternOperator OpNode,
                           SDNode MaskOpNode, SDNode OpNodeRnd,
                           X86SchedWriteWidths sched> {
  let Predicates = [HasAVX512] in
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, v16f32_info, v16i32_info, OpNode,
                            MaskOpNode, sched.ZMM>,
             avx512_vcvt_fp_rc<opc, OpcodeStr, v16f32_info, v16i32_info,
                               OpNodeRnd, sched.ZMM>, EVEX_V512;

  let Predicates = [HasVLX] in {
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, v4f32x_info, v4i32x_info, OpNode,
                               MaskOpNode, sched.XMM>, EVEX_V128;
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, v8f32x_info, v8i32x_info, OpNode,
                               MaskOpNode, sched.YMM>, EVEX_V256;
  }
}

// Convert Float to Signed/Unsigned Doubleword with truncation
multiclass avx512_cvttps2dq<bits<8> opc, string OpcodeStr, SDPatternOperator OpNode,
                            SDNode MaskOpNode,
                            SDNode OpNodeSAE, X86SchedWriteWidths sched> {
  let Predicates = [HasAVX512] in {
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, v16i32_info, v16f32_info, OpNode,
                            MaskOpNode, sched.ZMM>,
             avx512_vcvt_fp_sae<opc, OpcodeStr, v16i32_info, v16f32_info,
                                OpNodeSAE, sched.ZMM>, EVEX_V512;
  }
  let Predicates = [HasVLX] in {
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, v4i32x_info, v4f32x_info, OpNode,
                               MaskOpNode, sched.XMM>, EVEX_V128;
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, v8i32x_info, v8f32x_info, OpNode,
                               MaskOpNode, sched.YMM>, EVEX_V256;
  }
}

// Convert Float to Signed/Unsigned Doubleword
multiclass avx512_cvtps2dq<bits<8> opc, string OpcodeStr, SDNode OpNode,
                           SDNode MaskOpNode, SDNode OpNodeRnd,
                           X86SchedWriteWidths sched> {
  let Predicates = [HasAVX512] in {
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, v16i32_info, v16f32_info, OpNode,
                            MaskOpNode, sched.ZMM>,
             avx512_vcvt_fp_rc<opc, OpcodeStr, v16i32_info, v16f32_info,
                                OpNodeRnd, sched.ZMM>, EVEX_V512;
  }
  let Predicates = [HasVLX] in {
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, v4i32x_info, v4f32x_info, OpNode,
                               MaskOpNode, sched.XMM>, EVEX_V128;
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, v8i32x_info, v8f32x_info, OpNode,
                               MaskOpNode, sched.YMM>, EVEX_V256;
  }
}

// Convert Double to Signed/Unsigned Doubleword with truncation
multiclass avx512_cvttpd2dq<bits<8> opc, string OpcodeStr, SDPatternOperator OpNode,
                            SDNode MaskOpNode, SDNode OpNodeSAE,
                            X86SchedWriteWidths sched> {
  let Predicates = [HasAVX512] in {
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, v8i32x_info, v8f64_info, OpNode,
                            MaskOpNode, sched.ZMM>,
             avx512_vcvt_fp_sae<opc, OpcodeStr, v8i32x_info, v8f64_info,
                                OpNodeSAE, sched.ZMM>, EVEX_V512;
  }
  let Predicates = [HasVLX] in {
    // we need "x"/"y" suffixes in order to distinguish between 128 and 256
    // memory forms of these instructions in Asm Parser. They have the same
    // dest type - 'v4i32x_info'. We also specify the broadcast string explicitly
    // due to the same reason.
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, v4i32x_info, v2f64x_info,
                               null_frag, null_frag, sched.XMM, "{1to2}", "{x}", f128mem,
                               VK2WM>, EVEX_V128;
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, v4i32x_info, v4f64x_info, OpNode,
                               MaskOpNode, sched.YMM, "{1to4}", "{y}">, EVEX_V256;
  }

  def : InstAlias<OpcodeStr#"x\t{$src, $dst|$dst, $src}",
                  (!cast<Instruction>(NAME # "Z128rr") VR128X:$dst,
                  VR128X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"x\t{$src, $dst {${mask}}|$dst {${mask}}, $src}",
                  (!cast<Instruction>(NAME # "Z128rrk") VR128X:$dst,
                  VK2WM:$mask, VR128X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"x\t{$src, $dst {${mask}} {z}|$dst {${mask}} {z}, $src}",
                  (!cast<Instruction>(NAME # "Z128rrkz") VR128X:$dst,
                  VK2WM:$mask, VR128X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"x\t{${src}{1to2}, $dst|$dst, ${src}{1to2}}",
                  (!cast<Instruction>(NAME # "Z128rmb") VR128X:$dst,
                  f64mem:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"x\t{${src}{1to2}, $dst {${mask}}|"
                  "$dst {${mask}}, ${src}{1to2}}",
                  (!cast<Instruction>(NAME # "Z128rmbk") VR128X:$dst,
                  VK2WM:$mask, f64mem:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"x\t{${src}{1to2}, $dst {${mask}} {z}|"
                  "$dst {${mask}} {z}, ${src}{1to2}}",
                  (!cast<Instruction>(NAME # "Z128rmbkz") VR128X:$dst,
                  VK2WM:$mask, f64mem:$src), 0, "att">;

  def : InstAlias<OpcodeStr#"y\t{$src, $dst|$dst, $src}",
                  (!cast<Instruction>(NAME # "Z256rr") VR128X:$dst,
                  VR256X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"y\t{$src, $dst {${mask}}|$dst {${mask}}, $src}",
                  (!cast<Instruction>(NAME # "Z256rrk") VR128X:$dst,
                  VK4WM:$mask, VR256X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"y\t{$src, $dst {${mask}} {z}|$dst {${mask}} {z}, $src}",
                  (!cast<Instruction>(NAME # "Z256rrkz") VR128X:$dst,
                  VK4WM:$mask, VR256X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"y\t{${src}{1to4}, $dst|$dst, ${src}{1to4}}",
                  (!cast<Instruction>(NAME # "Z256rmb") VR128X:$dst,
                  f64mem:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"y\t{${src}{1to4}, $dst {${mask}}|"
                  "$dst {${mask}}, ${src}{1to4}}",
                  (!cast<Instruction>(NAME # "Z256rmbk") VR128X:$dst,
                  VK4WM:$mask, f64mem:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"y\t{${src}{1to4}, $dst {${mask}} {z}|"
                  "$dst {${mask}} {z}, ${src}{1to4}}",
                  (!cast<Instruction>(NAME # "Z256rmbkz") VR128X:$dst,
                  VK4WM:$mask, f64mem:$src), 0, "att">;
}

// Convert Double to Signed/Unsigned Doubleword
multiclass avx512_cvtpd2dq<bits<8> opc, string OpcodeStr, SDNode OpNode,
                           SDNode MaskOpNode, SDNode OpNodeRnd,
                           X86SchedWriteWidths sched> {
  let Predicates = [HasAVX512] in {
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, v8i32x_info, v8f64_info, OpNode,
                            MaskOpNode, sched.ZMM>,
             avx512_vcvt_fp_rc<opc, OpcodeStr, v8i32x_info, v8f64_info,
                               OpNodeRnd, sched.ZMM>, EVEX_V512;
  }
  let Predicates = [HasVLX] in {
    // we need "x"/"y" suffixes in order to distinguish between 128 and 256
    // memory forms of these instructions in Asm Parcer. They have the same
    // dest type - 'v4i32x_info'. We also specify the broadcast string explicitly
    // due to the same reason.
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, v4i32x_info, v2f64x_info,
                               null_frag, null_frag, sched.XMM, "{1to2}", "{x}", f128mem,
                               VK2WM>, EVEX_V128;
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, v4i32x_info, v4f64x_info, OpNode,
                               MaskOpNode, sched.YMM, "{1to4}", "{y}">, EVEX_V256;
  }

  def : InstAlias<OpcodeStr#"x\t{$src, $dst|$dst, $src}",
                  (!cast<Instruction>(NAME # "Z128rr") VR128X:$dst, VR128X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"x\t{$src, $dst {${mask}}|$dst {${mask}}, $src}",
                  (!cast<Instruction>(NAME # "Z128rrk") VR128X:$dst,
                  VK2WM:$mask, VR128X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"x\t{$src, $dst {${mask}} {z}|$dst {${mask}} {z}, $src}",
                  (!cast<Instruction>(NAME # "Z128rrkz") VR128X:$dst,
                  VK2WM:$mask, VR128X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"x\t{${src}{1to2}, $dst|$dst, ${src}{1to2}}",
                  (!cast<Instruction>(NAME # "Z128rmb") VR128X:$dst,
                  f64mem:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"x\t{${src}{1to2}, $dst {${mask}}|"
                  "$dst {${mask}}, ${src}{1to2}}",
                  (!cast<Instruction>(NAME # "Z128rmbk") VR128X:$dst,
                  VK2WM:$mask, f64mem:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"x\t{${src}{1to2}, $dst {${mask}} {z}|"
                  "$dst {${mask}} {z}, ${src}{1to2}}",
                  (!cast<Instruction>(NAME # "Z128rmbkz") VR128X:$dst,
                  VK2WM:$mask, f64mem:$src), 0, "att">;

  def : InstAlias<OpcodeStr#"y\t{$src, $dst|$dst, $src}",
                  (!cast<Instruction>(NAME # "Z256rr") VR128X:$dst, VR256X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"y\t{$src, $dst {${mask}}|$dst {${mask}}, $src}",
                  (!cast<Instruction>(NAME # "Z256rrk") VR128X:$dst,
                  VK4WM:$mask, VR256X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"y\t{$src, $dst {${mask}} {z}|$dst {${mask}} {z}, $src}",
                  (!cast<Instruction>(NAME # "Z256rrkz") VR128X:$dst,
                  VK4WM:$mask, VR256X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"y\t{${src}{1to4}, $dst|$dst, ${src}{1to4}}",
                  (!cast<Instruction>(NAME # "Z256rmb") VR128X:$dst,
                  f64mem:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"y\t{${src}{1to4}, $dst {${mask}}|"
                  "$dst {${mask}}, ${src}{1to4}}",
                  (!cast<Instruction>(NAME # "Z256rmbk") VR128X:$dst,
                  VK4WM:$mask, f64mem:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"y\t{${src}{1to4}, $dst {${mask}} {z}|"
                  "$dst {${mask}} {z}, ${src}{1to4}}",
                  (!cast<Instruction>(NAME # "Z256rmbkz") VR128X:$dst,
                  VK4WM:$mask, f64mem:$src), 0, "att">;
}

// Convert Double to Signed/Unsigned Quardword
multiclass avx512_cvtpd2qq<bits<8> opc, string OpcodeStr, SDNode OpNode,
                           SDNode MaskOpNode, SDNode OpNodeRnd,
                           X86SchedWriteWidths sched> {
  let Predicates = [HasDQI] in {
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, v8i64_info, v8f64_info, OpNode,
                            MaskOpNode, sched.ZMM>,
             avx512_vcvt_fp_rc<opc, OpcodeStr, v8i64_info, v8f64_info,
                               OpNodeRnd, sched.ZMM>, EVEX_V512;
  }
  let Predicates = [HasDQI, HasVLX] in {
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, v2i64x_info, v2f64x_info, OpNode,
                               MaskOpNode, sched.XMM>, EVEX_V128;
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, v4i64x_info, v4f64x_info, OpNode,
                               MaskOpNode, sched.YMM>, EVEX_V256;
  }
}

// Convert Double to Signed/Unsigned Quardword with truncation
multiclass avx512_cvttpd2qq<bits<8> opc, string OpcodeStr, SDPatternOperator OpNode,
                            SDNode MaskOpNode, SDNode OpNodeRnd,
                            X86SchedWriteWidths sched> {
  let Predicates = [HasDQI] in {
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, v8i64_info, v8f64_info, OpNode,
                            MaskOpNode, sched.ZMM>,
             avx512_vcvt_fp_sae<opc, OpcodeStr, v8i64_info, v8f64_info,
                                OpNodeRnd, sched.ZMM>, EVEX_V512;
  }
  let Predicates = [HasDQI, HasVLX] in {
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, v2i64x_info, v2f64x_info, OpNode,
                               MaskOpNode, sched.XMM>, EVEX_V128;
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, v4i64x_info, v4f64x_info, OpNode,
                               MaskOpNode, sched.YMM>, EVEX_V256;
  }
}

// Convert Signed/Unsigned Quardword to Double
multiclass avx512_cvtqq2pd<bits<8> opc, string OpcodeStr, SDPatternOperator OpNode,
                           SDNode MaskOpNode, SDNode OpNodeRnd,
                           X86SchedWriteWidths sched> {
  let Predicates = [HasDQI] in {
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, v8f64_info, v8i64_info, OpNode,
                            MaskOpNode, sched.ZMM>,
             avx512_vcvt_fp_rc<opc, OpcodeStr, v8f64_info, v8i64_info,
                               OpNodeRnd, sched.ZMM>, EVEX_V512;
  }
  let Predicates = [HasDQI, HasVLX] in {
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, v2f64x_info, v2i64x_info, OpNode,
                               MaskOpNode, sched.XMM>, EVEX_V128;
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, v4f64x_info, v4i64x_info, OpNode,
                               MaskOpNode, sched.YMM>, EVEX_V256;
  }
}

// Convert Float to Signed/Unsigned Quardword
multiclass avx512_cvtps2qq<bits<8> opc, string OpcodeStr, SDNode OpNode,
                           SDNode MaskOpNode, SDNode OpNodeRnd,
                           X86SchedWriteWidths sched> {
  let Predicates = [HasDQI] in {
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, v8i64_info, v8f32x_info, OpNode,
                            MaskOpNode, sched.ZMM>,
             avx512_vcvt_fp_rc<opc, OpcodeStr, v8i64_info, v8f32x_info,
                               OpNodeRnd, sched.ZMM>, EVEX_V512;
  }
  let Predicates = [HasDQI, HasVLX] in {
    // Explicitly specified broadcast string, since we take only 2 elements
    // from v4f32x_info source
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, v2i64x_info, v4f32x_info, OpNode,
                               MaskOpNode, sched.XMM, "{1to2}", "", f64mem, VK2WM,
                               (v2i64 (OpNode (bc_v4f32
                                (v2f64
                                 (scalar_to_vector (loadf64 addr:$src)))))),
                               (v2i64 (MaskOpNode (bc_v4f32
                                (v2f64
                                 (scalar_to_vector (loadf64 addr:$src))))))>,
                               EVEX_V128;
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, v4i64x_info, v4f32x_info, OpNode,
                               MaskOpNode, sched.YMM>, EVEX_V256;
  }
}

// Convert Float to Signed/Unsigned Quardword with truncation
multiclass avx512_cvttps2qq<bits<8> opc, string OpcodeStr, SDPatternOperator OpNode,
                            SDNode MaskOpNode, SDNode OpNodeRnd,
                            X86SchedWriteWidths sched> {
  let Predicates = [HasDQI] in {
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, v8i64_info, v8f32x_info, OpNode,
                            MaskOpNode, sched.ZMM>,
             avx512_vcvt_fp_sae<opc, OpcodeStr, v8i64_info, v8f32x_info,
                                OpNodeRnd, sched.ZMM>, EVEX_V512;
  }
  let Predicates = [HasDQI, HasVLX] in {
    // Explicitly specified broadcast string, since we take only 2 elements
    // from v4f32x_info source
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, v2i64x_info, v4f32x_info, OpNode,
                               MaskOpNode, sched.XMM, "{1to2}", "", f64mem, VK2WM,
                               (v2i64 (OpNode (bc_v4f32
                                (v2f64
                                 (scalar_to_vector (loadf64 addr:$src)))))),
                               (v2i64 (MaskOpNode (bc_v4f32
                                (v2f64
                                 (scalar_to_vector (loadf64 addr:$src))))))>,
                               EVEX_V128;
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, v4i64x_info, v4f32x_info, OpNode,
                               MaskOpNode, sched.YMM>, EVEX_V256;
  }
}

// Convert Signed/Unsigned Quardword to Float
// Also Convert Signed/Unsigned Doubleword to Half
multiclass avx512_cvtqq2ps_dq2ph<bits<8> opc, string OpcodeStr, SDPatternOperator OpNode,
                                 SDPatternOperator MaskOpNode, SDPatternOperator OpNode128,
                                 SDPatternOperator OpNode128M, SDPatternOperator OpNodeRnd,
                                 AVX512VLVectorVTInfo _dst, AVX512VLVectorVTInfo _src,
                                 X86SchedWriteWidths sched, Predicate prd = HasDQI> {
  let Predicates = [prd] in {
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, _dst.info256, _src.info512, OpNode,
                            MaskOpNode, sched.ZMM>,
             avx512_vcvt_fp_rc<opc, OpcodeStr, _dst.info256, _src.info512,
                               OpNodeRnd, sched.ZMM>, EVEX_V512;
  }
  let Predicates = [prd, HasVLX] in {
    // we need "x"/"y" suffixes in order to distinguish between 128 and 256
    // memory forms of these instructions in Asm Parcer. They have the same
    // dest type - 'v4i32x_info'. We also specify the broadcast string explicitly
    // due to the same reason.
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, _dst.info128, _src.info128, null_frag,
                               null_frag, sched.XMM, _src.info128.BroadcastStr,
                               "{x}", i128mem, _src.info128.KRCWM>,
                               EVEX_V128;
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, _dst.info128, _src.info256, OpNode,
                               MaskOpNode, sched.YMM, _src.info256.BroadcastStr,
                               "{y}">, EVEX_V256;

    // Special patterns to allow use of X86VM[SU]intToFP for masking. Instruction
    // patterns have been disabled with null_frag.
    def : Pat<(_dst.info128.VT (OpNode128 (_src.info128.VT VR128X:$src))),
              (!cast<Instruction>(NAME # "Z128rr") VR128X:$src)>;
    def : Pat<(OpNode128M (_src.info128.VT VR128X:$src), (_dst.info128.VT VR128X:$src0),
                             _src.info128.KRCWM:$mask),
              (!cast<Instruction>(NAME # "Z128rrk") VR128X:$src0, _src.info128.KRCWM:$mask, VR128X:$src)>;
    def : Pat<(OpNode128M (_src.info128.VT VR128X:$src), _dst.info128.ImmAllZerosV,
                             _src.info128.KRCWM:$mask),
              (!cast<Instruction>(NAME # "Z128rrkz") _src.info128.KRCWM:$mask, VR128X:$src)>;

    def : Pat<(_dst.info128.VT (OpNode128 (_src.info128.LdFrag addr:$src))),
              (!cast<Instruction>(NAME # "Z128rm") addr:$src)>;
    def : Pat<(OpNode128M (_src.info128.LdFrag addr:$src), (_dst.info128.VT VR128X:$src0),
                             _src.info128.KRCWM:$mask),
              (!cast<Instruction>(NAME # "Z128rmk") VR128X:$src0, _src.info128.KRCWM:$mask, addr:$src)>;
    def : Pat<(OpNode128M (_src.info128.LdFrag addr:$src), _dst.info128.ImmAllZerosV,
                             _src.info128.KRCWM:$mask),
              (!cast<Instruction>(NAME # "Z128rmkz") _src.info128.KRCWM:$mask, addr:$src)>;

    def : Pat<(_dst.info128.VT (OpNode128 (_src.info128.VT (X86VBroadcastld64 addr:$src)))),
              (!cast<Instruction>(NAME # "Z128rmb") addr:$src)>;
    def : Pat<(OpNode128M (_src.info128.VT (X86VBroadcastld64 addr:$src)),
                             (_dst.info128.VT VR128X:$src0), _src.info128.KRCWM:$mask),
              (!cast<Instruction>(NAME # "Z128rmbk") VR128X:$src0, _src.info128.KRCWM:$mask, addr:$src)>;
    def : Pat<(OpNode128M (_src.info128.VT (X86VBroadcastld64 addr:$src)),
                             _dst.info128.ImmAllZerosV, _src.info128.KRCWM:$mask),
              (!cast<Instruction>(NAME # "Z128rmbkz") _src.info128.KRCWM:$mask, addr:$src)>;
  }

  def : InstAlias<OpcodeStr#"x\t{$src, $dst|$dst, $src}",
                  (!cast<Instruction>(NAME # "Z128rr") VR128X:$dst,
                  VR128X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"x\t{$src, $dst {${mask}}|$dst {${mask}}, $src}",
                  (!cast<Instruction>(NAME # "Z128rrk") VR128X:$dst,
                  VK2WM:$mask, VR128X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"x\t{$src, $dst {${mask}} {z}|$dst {${mask}} {z}, $src}",
                  (!cast<Instruction>(NAME # "Z128rrkz") VR128X:$dst,
                  VK2WM:$mask, VR128X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"x\t{${src}{1to2}, $dst|$dst, ${src}{1to2}}",
                  (!cast<Instruction>(NAME # "Z128rmb") VR128X:$dst,
                  i64mem:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"x\t{${src}{1to2}, $dst {${mask}}|"
                  "$dst {${mask}}, ${src}{1to2}}",
                  (!cast<Instruction>(NAME # "Z128rmbk") VR128X:$dst,
                  VK2WM:$mask, i64mem:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"x\t{${src}{1to2}, $dst {${mask}} {z}|"
                  "$dst {${mask}} {z}, ${src}{1to2}}",
                  (!cast<Instruction>(NAME # "Z128rmbkz") VR128X:$dst,
                  VK2WM:$mask, i64mem:$src), 0, "att">;

  def : InstAlias<OpcodeStr#"y\t{$src, $dst|$dst, $src}",
                  (!cast<Instruction>(NAME # "Z256rr") VR128X:$dst,
                  VR256X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"y\t{$src, $dst {${mask}}|"
                  "$dst {${mask}}, $src}",
                  (!cast<Instruction>(NAME # "Z256rrk") VR128X:$dst,
                  VK4WM:$mask, VR256X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"y\t{$src, $dst {${mask}} {z}|"
                  "$dst {${mask}} {z}, $src}",
                  (!cast<Instruction>(NAME # "Z256rrkz") VR128X:$dst,
                  VK4WM:$mask, VR256X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"y\t{${src}{1to4}, $dst|$dst, ${src}{1to4}}",
                  (!cast<Instruction>(NAME # "Z256rmb") VR128X:$dst,
                  i64mem:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"y\t{${src}{1to4}, $dst {${mask}}|"
                  "$dst {${mask}}, ${src}{1to4}}",
                  (!cast<Instruction>(NAME # "Z256rmbk") VR128X:$dst,
                  VK4WM:$mask, i64mem:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"y\t{${src}{1to4}, $dst {${mask}} {z}|"
                  "$dst {${mask}} {z}, ${src}{1to4}}",
                  (!cast<Instruction>(NAME # "Z256rmbkz") VR128X:$dst,
                  VK4WM:$mask, i64mem:$src), 0, "att">;
}

defm VCVTDQ2PD : avx512_cvtdq2pd<0xE6, "vcvtdq2pd", any_sint_to_fp, sint_to_fp,
                                 X86any_VSintToFP, X86VSintToFP,
                                 SchedWriteCvtDQ2PD>, TB, XS, EVEX_CD8<32, CD8VH>;

defm VCVTDQ2PS : avx512_cvtdq2ps<0x5B, "vcvtdq2ps", any_sint_to_fp, sint_to_fp,
                                X86VSintToFpRnd, SchedWriteCvtDQ2PS>,
                                TB, EVEX_CD8<32, CD8VF>;

defm VCVTTPS2DQ : avx512_cvttps2dq<0x5B, "vcvttps2dq", X86any_cvttp2si,
                                 X86cvttp2si, X86cvttp2siSAE,
                                 SchedWriteCvtPS2DQ>, TB, XS, EVEX_CD8<32, CD8VF>;

defm VCVTTPD2DQ : avx512_cvttpd2dq<0xE6, "vcvttpd2dq", X86any_cvttp2si,
                                 X86cvttp2si, X86cvttp2siSAE,
                                 SchedWriteCvtPD2DQ>,
                                 TB, PD, REX_W, EVEX_CD8<64, CD8VF>;

defm VCVTTPS2UDQ : avx512_cvttps2dq<0x78, "vcvttps2udq", X86any_cvttp2ui,
                                 X86cvttp2ui, X86cvttp2uiSAE,
                                 SchedWriteCvtPS2DQ>, TB, EVEX_CD8<32, CD8VF>;

defm VCVTTPD2UDQ : avx512_cvttpd2dq<0x78, "vcvttpd2udq", X86any_cvttp2ui,
                                 X86cvttp2ui, X86cvttp2uiSAE,
                                 SchedWriteCvtPD2DQ>,
                                 TB, REX_W, EVEX_CD8<64, CD8VF>;

defm VCVTUDQ2PD : avx512_cvtdq2pd<0x7A, "vcvtudq2pd", any_uint_to_fp,
                                  uint_to_fp, X86any_VUintToFP, X86VUintToFP,
                                  SchedWriteCvtDQ2PD>, TB, XS, EVEX_CD8<32, CD8VH>;

defm VCVTUDQ2PS : avx512_cvtdq2ps<0x7A, "vcvtudq2ps", any_uint_to_fp,
                                 uint_to_fp, X86VUintToFpRnd,
                                 SchedWriteCvtDQ2PS>, TB, XD, EVEX_CD8<32, CD8VF>;

defm VCVTPS2DQ : avx512_cvtps2dq<0x5B, "vcvtps2dq", X86cvtp2Int, X86cvtp2Int,
                                 X86cvtp2IntRnd, SchedWriteCvtPS2DQ>, TB, PD,
                                 EVEX_CD8<32, CD8VF>;

defm VCVTPD2DQ : avx512_cvtpd2dq<0xE6, "vcvtpd2dq", X86cvtp2Int, X86cvtp2Int,
                                 X86cvtp2IntRnd, SchedWriteCvtPD2DQ>, TB, XD,
                                 REX_W, EVEX_CD8<64, CD8VF>;

defm VCVTPS2UDQ : avx512_cvtps2dq<0x79, "vcvtps2udq", X86cvtp2UInt, X86cvtp2UInt,
                                 X86cvtp2UIntRnd, SchedWriteCvtPS2DQ>,
                                 TB, EVEX_CD8<32, CD8VF>;

defm VCVTPD2UDQ : avx512_cvtpd2dq<0x79, "vcvtpd2udq", X86cvtp2UInt, X86cvtp2UInt,
                                 X86cvtp2UIntRnd, SchedWriteCvtPD2DQ>, REX_W,
                                 TB, EVEX_CD8<64, CD8VF>;

defm VCVTPD2QQ : avx512_cvtpd2qq<0x7B, "vcvtpd2qq", X86cvtp2Int, X86cvtp2Int,
                                 X86cvtp2IntRnd, SchedWriteCvtPD2DQ>, REX_W,
                                 TB, PD, EVEX_CD8<64, CD8VF>;

defm VCVTPS2QQ : avx512_cvtps2qq<0x7B, "vcvtps2qq", X86cvtp2Int, X86cvtp2Int,
                                 X86cvtp2IntRnd, SchedWriteCvtPS2DQ>, TB, PD,
                                 EVEX_CD8<32, CD8VH>;

defm VCVTPD2UQQ : avx512_cvtpd2qq<0x79, "vcvtpd2uqq", X86cvtp2UInt, X86cvtp2UInt,
                                 X86cvtp2UIntRnd, SchedWriteCvtPD2DQ>, REX_W,
                                 TB, PD, EVEX_CD8<64, CD8VF>;

defm VCVTPS2UQQ : avx512_cvtps2qq<0x79, "vcvtps2uqq", X86cvtp2UInt, X86cvtp2UInt,
                                 X86cvtp2UIntRnd, SchedWriteCvtPS2DQ>, TB, PD,
                                 EVEX_CD8<32, CD8VH>;

defm VCVTTPD2QQ : avx512_cvttpd2qq<0x7A, "vcvttpd2qq", X86any_cvttp2si,
                                 X86cvttp2si, X86cvttp2siSAE,
                                 SchedWriteCvtPD2DQ>, REX_W,
                                 TB, PD, EVEX_CD8<64, CD8VF>;

defm VCVTTPS2QQ : avx512_cvttps2qq<0x7A, "vcvttps2qq", X86any_cvttp2si,
                                 X86cvttp2si, X86cvttp2siSAE,
                                 SchedWriteCvtPS2DQ>, TB, PD,
                                 EVEX_CD8<32, CD8VH>;

defm VCVTTPD2UQQ : avx512_cvttpd2qq<0x78, "vcvttpd2uqq", X86any_cvttp2ui,
                                 X86cvttp2ui, X86cvttp2uiSAE,
                                 SchedWriteCvtPD2DQ>, REX_W,
                                 TB, PD, EVEX_CD8<64, CD8VF>;

defm VCVTTPS2UQQ : avx512_cvttps2qq<0x78, "vcvttps2uqq", X86any_cvttp2ui,
                                 X86cvttp2ui, X86cvttp2uiSAE,
                                 SchedWriteCvtPS2DQ>, TB, PD,
                                 EVEX_CD8<32, CD8VH>;

defm VCVTQQ2PD : avx512_cvtqq2pd<0xE6, "vcvtqq2pd", any_sint_to_fp,
                            sint_to_fp, X86VSintToFpRnd,
                            SchedWriteCvtDQ2PD>, REX_W, TB, XS, EVEX_CD8<64, CD8VF>;

defm VCVTUQQ2PD : avx512_cvtqq2pd<0x7A, "vcvtuqq2pd", any_uint_to_fp,
                            uint_to_fp, X86VUintToFpRnd, SchedWriteCvtDQ2PD>,
                            REX_W, TB, XS, EVEX_CD8<64, CD8VF>;

defm VCVTDQ2PH : avx512_cvtqq2ps_dq2ph<0x5B, "vcvtdq2ph", any_sint_to_fp, sint_to_fp,
                            X86any_VSintToFP, X86VMSintToFP,
                            X86VSintToFpRnd, avx512vl_f16_info, avx512vl_i32_info,
                            SchedWriteCvtDQ2PS, HasFP16>,
                            T_MAP5, EVEX_CD8<32, CD8VF>;

defm VCVTUDQ2PH : avx512_cvtqq2ps_dq2ph<0x7A, "vcvtudq2ph", any_uint_to_fp, uint_to_fp,
                            X86any_VUintToFP, X86VMUintToFP,
                            X86VUintToFpRnd, avx512vl_f16_info, avx512vl_i32_info,
                            SchedWriteCvtDQ2PS, HasFP16>, T_MAP5, XD,
                            EVEX_CD8<32, CD8VF>;

defm VCVTQQ2PS : avx512_cvtqq2ps_dq2ph<0x5B, "vcvtqq2ps", any_sint_to_fp, sint_to_fp,
                            X86any_VSintToFP, X86VMSintToFP,
                            X86VSintToFpRnd, avx512vl_f32_info, avx512vl_i64_info,
                            SchedWriteCvtDQ2PS>, REX_W, TB,
                            EVEX_CD8<64, CD8VF>;

defm VCVTUQQ2PS : avx512_cvtqq2ps_dq2ph<0x7A, "vcvtuqq2ps", any_uint_to_fp, uint_to_fp,
                            X86any_VUintToFP, X86VMUintToFP,
                            X86VUintToFpRnd, avx512vl_f32_info, avx512vl_i64_info,
                            SchedWriteCvtDQ2PS>, REX_W, TB, XD,
                            EVEX_CD8<64, CD8VF>;

let Predicates = [HasVLX] in {
  // Special patterns to allow use of X86mcvtp2Int for masking. Instruction
  // patterns have been disabled with null_frag.
  def : Pat<(v4i32 (X86cvtp2Int (v2f64 VR128X:$src))),
            (VCVTPD2DQZ128rr VR128X:$src)>;
  def : Pat<(X86mcvtp2Int (v2f64 VR128X:$src), (v4i32 VR128X:$src0),
                          VK2WM:$mask),
            (VCVTPD2DQZ128rrk VR128X:$src0, VK2WM:$mask, VR128X:$src)>;
  def : Pat<(X86mcvtp2Int (v2f64 VR128X:$src), v4i32x_info.ImmAllZerosV,
                          VK2WM:$mask),
            (VCVTPD2DQZ128rrkz VK2WM:$mask, VR128X:$src)>;

  def : Pat<(v4i32 (X86cvtp2Int (loadv2f64 addr:$src))),
            (VCVTPD2DQZ128rm addr:$src)>;
  def : Pat<(X86mcvtp2Int (loadv2f64 addr:$src), (v4i32 VR128X:$src0),
                          VK2WM:$mask),
            (VCVTPD2DQZ128rmk VR128X:$src0, VK2WM:$mask, addr:$src)>;
  def : Pat<(X86mcvtp2Int (loadv2f64 addr:$src), v4i32x_info.ImmAllZerosV,
                          VK2WM:$mask),
            (VCVTPD2DQZ128rmkz VK2WM:$mask, addr:$src)>;

  def : Pat<(v4i32 (X86cvtp2Int (v2f64 (X86VBroadcastld64 addr:$src)))),
            (VCVTPD2DQZ128rmb addr:$src)>;
  def : Pat<(X86mcvtp2Int (v2f64 (X86VBroadcastld64 addr:$src)),
                          (v4i32 VR128X:$src0), VK2WM:$mask),
            (VCVTPD2DQZ128rmbk VR128X:$src0, VK2WM:$mask, addr:$src)>;
  def : Pat<(X86mcvtp2Int (v2f64 (X86VBroadcastld64 addr:$src)),
                          v4i32x_info.ImmAllZerosV, VK2WM:$mask),
            (VCVTPD2DQZ128rmbkz VK2WM:$mask, addr:$src)>;

  // Special patterns to allow use of X86mcvttp2si for masking. Instruction
  // patterns have been disabled with null_frag.
  def : Pat<(v4i32 (X86any_cvttp2si (v2f64 VR128X:$src))),
            (VCVTTPD2DQZ128rr VR128X:$src)>;
  def : Pat<(X86mcvttp2si (v2f64 VR128X:$src), (v4i32 VR128X:$src0),
                          VK2WM:$mask),
            (VCVTTPD2DQZ128rrk VR128X:$src0, VK2WM:$mask, VR128X:$src)>;
  def : Pat<(X86mcvttp2si (v2f64 VR128X:$src), v4i32x_info.ImmAllZerosV,
                          VK2WM:$mask),
            (VCVTTPD2DQZ128rrkz VK2WM:$mask, VR128X:$src)>;

  def : Pat<(v4i32 (X86any_cvttp2si (loadv2f64 addr:$src))),
            (VCVTTPD2DQZ128rm addr:$src)>;
  def : Pat<(X86mcvttp2si (loadv2f64 addr:$src), (v4i32 VR128X:$src0),
                          VK2WM:$mask),
            (VCVTTPD2DQZ128rmk VR128X:$src0, VK2WM:$mask, addr:$src)>;
  def : Pat<(X86mcvttp2si (loadv2f64 addr:$src), v4i32x_info.ImmAllZerosV,
                          VK2WM:$mask),
            (VCVTTPD2DQZ128rmkz VK2WM:$mask, addr:$src)>;

  def : Pat<(v4i32 (X86any_cvttp2si (v2f64 (X86VBroadcastld64 addr:$src)))),
            (VCVTTPD2DQZ128rmb addr:$src)>;
  def : Pat<(X86mcvttp2si (v2f64 (X86VBroadcastld64 addr:$src)),
                          (v4i32 VR128X:$src0), VK2WM:$mask),
            (VCVTTPD2DQZ128rmbk VR128X:$src0, VK2WM:$mask, addr:$src)>;
  def : Pat<(X86mcvttp2si (v2f64 (X86VBroadcastld64 addr:$src)),
                          v4i32x_info.ImmAllZerosV, VK2WM:$mask),
            (VCVTTPD2DQZ128rmbkz VK2WM:$mask, addr:$src)>;

  // Special patterns to allow use of X86mcvtp2UInt for masking. Instruction
  // patterns have been disabled with null_frag.
  def : Pat<(v4i32 (X86cvtp2UInt (v2f64 VR128X:$src))),
            (VCVTPD2UDQZ128rr VR128X:$src)>;
  def : Pat<(X86mcvtp2UInt (v2f64 VR128X:$src), (v4i32 VR128X:$src0),
                           VK2WM:$mask),
            (VCVTPD2UDQZ128rrk VR128X:$src0, VK2WM:$mask, VR128X:$src)>;
  def : Pat<(X86mcvtp2UInt (v2f64 VR128X:$src), v4i32x_info.ImmAllZerosV,
                           VK2WM:$mask),
            (VCVTPD2UDQZ128rrkz VK2WM:$mask, VR128X:$src)>;

  def : Pat<(v4i32 (X86cvtp2UInt (loadv2f64 addr:$src))),
            (VCVTPD2UDQZ128rm addr:$src)>;
  def : Pat<(X86mcvtp2UInt (loadv2f64 addr:$src), (v4i32 VR128X:$src0),
                           VK2WM:$mask),
            (VCVTPD2UDQZ128rmk VR128X:$src0, VK2WM:$mask, addr:$src)>;
  def : Pat<(X86mcvtp2UInt (loadv2f64 addr:$src), v4i32x_info.ImmAllZerosV,
                           VK2WM:$mask),
            (VCVTPD2UDQZ128rmkz VK2WM:$mask, addr:$src)>;

  def : Pat<(v4i32 (X86cvtp2UInt (v2f64 (X86VBroadcastld64 addr:$src)))),
            (VCVTPD2UDQZ128rmb addr:$src)>;
  def : Pat<(X86mcvtp2UInt (v2f64 (X86VBroadcastld64 addr:$src)),
                           (v4i32 VR128X:$src0), VK2WM:$mask),
            (VCVTPD2UDQZ128rmbk VR128X:$src0, VK2WM:$mask, addr:$src)>;
  def : Pat<(X86mcvtp2UInt (v2f64 (X86VBroadcastld64 addr:$src)),
                           v4i32x_info.ImmAllZerosV, VK2WM:$mask),
            (VCVTPD2UDQZ128rmbkz VK2WM:$mask, addr:$src)>;

  // Special patterns to allow use of X86mcvtp2UInt for masking. Instruction
  // patterns have been disabled with null_frag.
  def : Pat<(v4i32 (X86any_cvttp2ui (v2f64 VR128X:$src))),
            (VCVTTPD2UDQZ128rr VR128X:$src)>;
  def : Pat<(X86mcvttp2ui (v2f64 VR128X:$src), (v4i32 VR128X:$src0),
                          VK2WM:$mask),
            (VCVTTPD2UDQZ128rrk VR128X:$src0, VK2WM:$mask, VR128X:$src)>;
  def : Pat<(X86mcvttp2ui (v2f64 VR128X:$src), v4i32x_info.ImmAllZerosV,
                          VK2WM:$mask),
            (VCVTTPD2UDQZ128rrkz VK2WM:$mask, VR128X:$src)>;

  def : Pat<(v4i32 (X86any_cvttp2ui (loadv2f64 addr:$src))),
            (VCVTTPD2UDQZ128rm addr:$src)>;
  def : Pat<(X86mcvttp2ui (loadv2f64 addr:$src), (v4i32 VR128X:$src0),
                          VK2WM:$mask),
            (VCVTTPD2UDQZ128rmk VR128X:$src0, VK2WM:$mask, addr:$src)>;
  def : Pat<(X86mcvttp2ui (loadv2f64 addr:$src), v4i32x_info.ImmAllZerosV,
                          VK2WM:$mask),
            (VCVTTPD2UDQZ128rmkz VK2WM:$mask, addr:$src)>;

  def : Pat<(v4i32 (X86any_cvttp2ui (v2f64 (X86VBroadcastld64 addr:$src)))),
            (VCVTTPD2UDQZ128rmb addr:$src)>;
  def : Pat<(X86mcvttp2ui (v2f64 (X86VBroadcastld64 addr:$src)),
                          (v4i32 VR128X:$src0), VK2WM:$mask),
            (VCVTTPD2UDQZ128rmbk VR128X:$src0, VK2WM:$mask, addr:$src)>;
  def : Pat<(X86mcvttp2ui (v2f64 (X86VBroadcastld64 addr:$src)),
                          v4i32x_info.ImmAllZerosV, VK2WM:$mask),
            (VCVTTPD2UDQZ128rmbkz VK2WM:$mask, addr:$src)>;

  def : Pat<(v4i32 (lrint VR128X:$src)), (VCVTPS2DQZ128rr VR128X:$src)>;
  def : Pat<(v4i32 (lrint (loadv4f32 addr:$src))), (VCVTPS2DQZ128rm addr:$src)>;
  def : Pat<(v8i32 (lrint VR256X:$src)), (VCVTPS2DQZ256rr VR256X:$src)>;
  def : Pat<(v8i32 (lrint (loadv8f32 addr:$src))), (VCVTPS2DQZ256rm addr:$src)>;
  def : Pat<(v4i32 (lrint VR256X:$src)), (VCVTPD2DQZ256rr VR256X:$src)>;
  def : Pat<(v4i32 (lrint (loadv4f64 addr:$src))), (VCVTPD2DQZ256rm addr:$src)>;
}
def : Pat<(v16i32 (lrint VR512:$src)), (VCVTPS2DQZrr VR512:$src)>;
def : Pat<(v16i32 (lrint (loadv16f32 addr:$src))), (VCVTPS2DQZrm addr:$src)>;
def : Pat<(v8i32 (lrint VR512:$src)), (VCVTPD2DQZrr VR512:$src)>;
def : Pat<(v8i32 (lrint (loadv8f64 addr:$src))), (VCVTPD2DQZrm addr:$src)>;

let Predicates = [HasDQI, HasVLX] in {
  def : Pat<(v2i64 (X86cvtp2Int (bc_v4f32 (v2f64 (X86vzload64 addr:$src))))),
            (VCVTPS2QQZ128rm addr:$src)>;
  def : Pat<(v2i64 (vselect_mask VK2WM:$mask,
                                 (X86cvtp2Int (bc_v4f32 (v2f64 (X86vzload64 addr:$src)))),
                                 VR128X:$src0)),
            (VCVTPS2QQZ128rmk VR128X:$src0, VK2WM:$mask, addr:$src)>;
  def : Pat<(v2i64 (vselect_mask VK2WM:$mask,
                                 (X86cvtp2Int (bc_v4f32 (v2f64 (X86vzload64 addr:$src)))),
                                 v2i64x_info.ImmAllZerosV)),
            (VCVTPS2QQZ128rmkz VK2WM:$mask, addr:$src)>;

  def : Pat<(v2i64 (X86cvtp2UInt (bc_v4f32 (v2f64 (X86vzload64 addr:$src))))),
            (VCVTPS2UQQZ128rm addr:$src)>;
  def : Pat<(v2i64 (vselect_mask VK2WM:$mask,
                                 (X86cvtp2UInt (bc_v4f32 (v2f64 (X86vzload64 addr:$src)))),
                                 VR128X:$src0)),
            (VCVTPS2UQQZ128rmk VR128X:$src0, VK2WM:$mask, addr:$src)>;
  def : Pat<(v2i64 (vselect_mask VK2WM:$mask,
                                 (X86cvtp2UInt (bc_v4f32 (v2f64 (X86vzload64 addr:$src)))),
                                 v2i64x_info.ImmAllZerosV)),
            (VCVTPS2UQQZ128rmkz VK2WM:$mask, addr:$src)>;

  def : Pat<(v2i64 (X86any_cvttp2si (bc_v4f32 (v2f64 (X86vzload64 addr:$src))))),
            (VCVTTPS2QQZ128rm addr:$src)>;
  def : Pat<(v2i64 (vselect_mask VK2WM:$mask,
                                 (X86cvttp2si (bc_v4f32 (v2f64 (X86vzload64 addr:$src)))),
                                 VR128X:$src0)),
            (VCVTTPS2QQZ128rmk VR128X:$src0, VK2WM:$mask, addr:$src)>;
  def : Pat<(v2i64 (vselect_mask VK2WM:$mask,
                                 (X86cvttp2si (bc_v4f32 (v2f64 (X86vzload64 addr:$src)))),
                                 v2i64x_info.ImmAllZerosV)),
            (VCVTTPS2QQZ128rmkz VK2WM:$mask, addr:$src)>;

  def : Pat<(v2i64 (X86any_cvttp2ui (bc_v4f32 (v2f64 (X86vzload64 addr:$src))))),
            (VCVTTPS2UQQZ128rm addr:$src)>;
  def : Pat<(v2i64 (vselect_mask VK2WM:$mask,
                                 (X86cvttp2ui (bc_v4f32 (v2f64 (X86vzload64 addr:$src)))),
                                 VR128X:$src0)),
            (VCVTTPS2UQQZ128rmk VR128X:$src0, VK2WM:$mask, addr:$src)>;
  def : Pat<(v2i64 (vselect_mask VK2WM:$mask,
                                 (X86cvttp2ui (bc_v4f32 (v2f64 (X86vzload64 addr:$src)))),
                                 v2i64x_info.ImmAllZerosV)),
            (VCVTTPS2UQQZ128rmkz VK2WM:$mask, addr:$src)>;

  def : Pat<(v4i64 (lrint VR128X:$src)), (VCVTPS2QQZ256rr VR128X:$src)>;
  def : Pat<(v4i64 (lrint (loadv4f32 addr:$src))), (VCVTPS2QQZ256rm addr:$src)>;
  def : Pat<(v4i64 (llrint VR128X:$src)), (VCVTPS2QQZ256rr VR128X:$src)>;
  def : Pat<(v4i64 (llrint (loadv4f32 addr:$src))), (VCVTPS2QQZ256rm addr:$src)>;
  def : Pat<(v2i64 (lrint VR128X:$src)), (VCVTPD2QQZ128rr VR128X:$src)>;
  def : Pat<(v2i64 (lrint (loadv2f64 addr:$src))), (VCVTPD2QQZ128rm addr:$src)>;
  def : Pat<(v4i64 (lrint VR256X:$src)), (VCVTPD2QQZ256rr VR256X:$src)>;
  def : Pat<(v4i64 (lrint (loadv4f64 addr:$src))), (VCVTPD2QQZ256rm addr:$src)>;
  def : Pat<(v2i64 (llrint VR128X:$src)), (VCVTPD2QQZ128rr VR128X:$src)>;
  def : Pat<(v2i64 (llrint (loadv2f64 addr:$src))), (VCVTPD2QQZ128rm addr:$src)>;
  def : Pat<(v4i64 (llrint VR256X:$src)), (VCVTPD2QQZ256rr VR256X:$src)>;
  def : Pat<(v4i64 (llrint (loadv4f64 addr:$src))), (VCVTPD2QQZ256rm addr:$src)>;
}

let Predicates = [HasDQI] in {
  def : Pat<(v8i64 (lrint VR256X:$src)), (VCVTPS2QQZrr VR256X:$src)>;
  def : Pat<(v8i64 (lrint (loadv8f32 addr:$src))), (VCVTPS2QQZrm addr:$src)>;
  def : Pat<(v8i64 (llrint VR256X:$src)), (VCVTPS2QQZrr VR256X:$src)>;
  def : Pat<(v8i64 (llrint (loadv8f32 addr:$src))), (VCVTPS2QQZrm addr:$src)>;
  def : Pat<(v8i64 (lrint VR512:$src)), (VCVTPD2QQZrr VR512:$src)>;
  def : Pat<(v8i64 (lrint (loadv8f64 addr:$src))), (VCVTPD2QQZrm addr:$src)>;
  def : Pat<(v8i64 (llrint VR512:$src)), (VCVTPD2QQZrr VR512:$src)>;
  def : Pat<(v8i64 (llrint (loadv8f64 addr:$src))), (VCVTPD2QQZrm addr:$src)>;
}

let Predicates = [HasVLX] in {
  def : Pat<(v2f64 (X86any_VSintToFP (bc_v4i32 (v2i64 (X86vzload64 addr:$src))))),
            (VCVTDQ2PDZ128rm addr:$src)>;
  def : Pat<(v2f64 (vselect_mask VK2WM:$mask,
                                 (X86VSintToFP (bc_v4i32 (v2i64 (X86vzload64 addr:$src)))),
                                 VR128X:$src0)),
            (VCVTDQ2PDZ128rmk VR128X:$src0, VK2WM:$mask, addr:$src)>;
  def : Pat<(v2f64 (vselect_mask VK2WM:$mask,
                                 (X86VSintToFP (bc_v4i32 (v2i64 (X86vzload64 addr:$src)))),
                                 v2f64x_info.ImmAllZerosV)),
            (VCVTDQ2PDZ128rmkz VK2WM:$mask, addr:$src)>;

  def : Pat<(v2f64 (X86any_VUintToFP (bc_v4i32 (v2i64 (X86vzload64 addr:$src))))),
            (VCVTUDQ2PDZ128rm addr:$src)>;
  def : Pat<(v2f64 (vselect_mask VK2WM:$mask,
                                 (X86VUintToFP (bc_v4i32 (v2i64 (X86vzload64 addr:$src)))),
                                 VR128X:$src0)),
            (VCVTUDQ2PDZ128rmk VR128X:$src0, VK2WM:$mask, addr:$src)>;
  def : Pat<(v2f64 (vselect_mask VK2WM:$mask,
                                 (X86VUintToFP (bc_v4i32 (v2i64 (X86vzload64 addr:$src)))),
                                 v2f64x_info.ImmAllZerosV)),
            (VCVTUDQ2PDZ128rmkz VK2WM:$mask, addr:$src)>;
}

//===----------------------------------------------------------------------===//
// Half precision conversion instructions
//===----------------------------------------------------------------------===//

let Uses = [MXCSR], mayRaiseFPException = 1 in
multiclass avx512_cvtph2ps<X86VectorVTInfo _dest, X86VectorVTInfo _src,
                           X86MemOperand x86memop, dag ld_dag,
                           X86FoldableSchedWrite sched> {
  defm rr : AVX512_maskable_split<0x13, MRMSrcReg, _dest ,(outs _dest.RC:$dst),
                            (ins _src.RC:$src), "vcvtph2ps", "$src", "$src",
                            (X86any_cvtph2ps (_src.VT _src.RC:$src)),
                            (X86cvtph2ps (_src.VT _src.RC:$src))>,
                            T8, PD, Sched<[sched]>;
  defm rm : AVX512_maskable_split<0x13, MRMSrcMem, _dest, (outs _dest.RC:$dst),
                            (ins x86memop:$src), "vcvtph2ps", "$src", "$src",
                            (X86any_cvtph2ps (_src.VT ld_dag)),
                            (X86cvtph2ps (_src.VT ld_dag))>,
                            T8, PD, Sched<[sched.Folded]>;
}

multiclass avx512_cvtph2ps_sae<X86VectorVTInfo _dest, X86VectorVTInfo _src,
                               X86FoldableSchedWrite sched> {
  let Uses = [MXCSR] in
  defm rrb : AVX512_maskable<0x13, MRMSrcReg, _dest, (outs _dest.RC:$dst),
                             (ins _src.RC:$src), "vcvtph2ps",
                             "{sae}, $src", "$src, {sae}",
                             (X86cvtph2psSAE (_src.VT _src.RC:$src))>,
                             T8, PD, EVEX_B, Sched<[sched]>;
}

let Predicates = [HasAVX512] in
  defm VCVTPH2PSZ : avx512_cvtph2ps<v16f32_info, v16i16x_info, f256mem,
                                    (load addr:$src), WriteCvtPH2PSZ>,
                    avx512_cvtph2ps_sae<v16f32_info, v16i16x_info, WriteCvtPH2PSZ>,
                    EVEX, EVEX_V512, EVEX_CD8<32, CD8VH>;

let Predicates = [HasVLX] in {
  defm VCVTPH2PSZ256 : avx512_cvtph2ps<v8f32x_info, v8i16x_info, f128mem,
                       (load addr:$src), WriteCvtPH2PSY>, EVEX, EVEX_V256,
                       EVEX_CD8<32, CD8VH>;
  defm VCVTPH2PSZ128 : avx512_cvtph2ps<v4f32x_info, v8i16x_info, f64mem,
                       (bitconvert (v2i64 (X86vzload64 addr:$src))),
                       WriteCvtPH2PS>, EVEX, EVEX_V128,
                       EVEX_CD8<32, CD8VH>;

  // Pattern match vcvtph2ps of a scalar i64 load.
  def : Pat<(v4f32 (X86any_cvtph2ps (v8i16 (bitconvert
              (v2i64 (scalar_to_vector (loadi64 addr:$src))))))),
            (VCVTPH2PSZ128rm addr:$src)>;
}

multiclass avx512_cvtps2ph<X86VectorVTInfo _dest, X86VectorVTInfo _src,
                           X86MemOperand x86memop, SchedWrite RR, SchedWrite MR> {
let ExeDomain = GenericDomain, Uses = [MXCSR], mayRaiseFPException = 1 in {
  def rr : AVX512AIi8<0x1D, MRMDestReg, (outs _dest.RC:$dst),
             (ins _src.RC:$src1, i32u8imm:$src2),
             "vcvtps2ph\t{$src2, $src1, $dst|$dst, $src1, $src2}",
             [(set _dest.RC:$dst,
                   (X86any_cvtps2ph (_src.VT _src.RC:$src1), (i32 timm:$src2)))]>,
             Sched<[RR]>;
  let Constraints = "$src0 = $dst" in
  def rrk : AVX512AIi8<0x1D, MRMDestReg, (outs _dest.RC:$dst),
             (ins _dest.RC:$src0, _src.KRCWM:$mask, _src.RC:$src1, i32u8imm:$src2),
             "vcvtps2ph\t{$src2, $src1, $dst {${mask}}|$dst {${mask}}, $src1, $src2}",
             [(set _dest.RC:$dst,
                   (X86mcvtps2ph (_src.VT _src.RC:$src1), (i32 timm:$src2),
                                 _dest.RC:$src0, _src.KRCWM:$mask))]>,
             Sched<[RR]>, EVEX_K;
  def rrkz : AVX512AIi8<0x1D, MRMDestReg, (outs _dest.RC:$dst),
             (ins _src.KRCWM:$mask, _src.RC:$src1, i32u8imm:$src2),
             "vcvtps2ph\t{$src2, $src1, $dst {${mask}} {z}|$dst {${mask}} {z}, $src1, $src2}",
             [(set _dest.RC:$dst,
                   (X86mcvtps2ph (_src.VT _src.RC:$src1), (i32 timm:$src2),
                                 _dest.ImmAllZerosV, _src.KRCWM:$mask))]>,
             Sched<[RR]>, EVEX_KZ;
  let hasSideEffects = 0, mayStore = 1 in {
    def mr : AVX512AIi8<0x1D, MRMDestMem, (outs),
               (ins x86memop:$dst, _src.RC:$src1, i32u8imm:$src2),
               "vcvtps2ph\t{$src2, $src1, $dst|$dst, $src1, $src2}", []>,
               Sched<[MR]>;
    def mrk : AVX512AIi8<0x1D, MRMDestMem, (outs),
               (ins x86memop:$dst, _dest.KRCWM:$mask, _src.RC:$src1, i32u8imm:$src2),
               "vcvtps2ph\t{$src2, $src1, $dst {${mask}}|$dst {${mask}}, $src1, $src2}", []>,
                EVEX_K, Sched<[MR]>;
  }
}
}

multiclass avx512_cvtps2ph_sae<X86VectorVTInfo _dest, X86VectorVTInfo _src,
                               SchedWrite Sched> {
  let hasSideEffects = 0, Uses = [MXCSR] in {
    def rrb : AVX512AIi8<0x1D, MRMDestReg, (outs _dest.RC:$dst),
              (ins _src.RC:$src1, i32u8imm:$src2),
              "vcvtps2ph\t{$src2, {sae}, $src1, $dst|$dst, $src1, {sae}, $src2}",
              [(set _dest.RC:$dst,
                    (X86cvtps2phSAE (_src.VT _src.RC:$src1), (i32 timm:$src2)))]>,
              EVEX_B, Sched<[Sched]>;
    let Constraints = "$src0 = $dst" in
    def rrbk : AVX512AIi8<0x1D, MRMDestReg, (outs _dest.RC:$dst),
              (ins _dest.RC:$src0, _src.KRCWM:$mask, _src.RC:$src1, i32u8imm:$src2),
              "vcvtps2ph\t{$src2, {sae}, $src1, $dst {${mask}}|$dst {${mask}}, $src1, {sae}, $src2}",
              [(set _dest.RC:$dst,
                    (X86mcvtps2phSAE (_src.VT _src.RC:$src1), (i32 timm:$src2),
                                  _dest.RC:$src0, _src.KRCWM:$mask))]>,
              EVEX_B, Sched<[Sched]>, EVEX_K;
    def rrbkz : AVX512AIi8<0x1D, MRMDestReg, (outs _dest.RC:$dst),
              (ins _src.KRCWM:$mask, _src.RC:$src1, i32u8imm:$src2),
              "vcvtps2ph\t{$src2, {sae}, $src1, $dst {${mask}} {z}|$dst {${mask}} {z}, $src1, {sae}, $src2}",
              [(set _dest.RC:$dst,
                    (X86mcvtps2phSAE (_src.VT _src.RC:$src1), (i32 timm:$src2),
                                  _dest.ImmAllZerosV, _src.KRCWM:$mask))]>,
              EVEX_B, Sched<[Sched]>, EVEX_KZ;
}
}

let Predicates = [HasAVX512] in {
  defm VCVTPS2PHZ : avx512_cvtps2ph<v16i16x_info, v16f32_info, f256mem,
                                    WriteCvtPS2PHZ, WriteCvtPS2PHZSt>,
                    avx512_cvtps2ph_sae<v16i16x_info, v16f32_info, WriteCvtPS2PHZ>,
                                        EVEX, EVEX_V512, EVEX_CD8<32, CD8VH>;

  def : Pat<(store (v16i16 (X86any_cvtps2ph VR512:$src1, timm:$src2)), addr:$dst),
            (VCVTPS2PHZmr addr:$dst, VR512:$src1, timm:$src2)>;
}

let Predicates = [HasVLX] in {
  defm VCVTPS2PHZ256 : avx512_cvtps2ph<v8i16x_info, v8f32x_info, f128mem,
                                       WriteCvtPS2PHY, WriteCvtPS2PHYSt>,
                                       EVEX, EVEX_V256, EVEX_CD8<32, CD8VH>;
  defm VCVTPS2PHZ128 : avx512_cvtps2ph<v8i16x_info, v4f32x_info, f64mem,
                                       WriteCvtPS2PH, WriteCvtPS2PHSt>,
                                       EVEX, EVEX_V128, EVEX_CD8<32, CD8VH>;

  def : Pat<(store (f64 (extractelt
                         (bc_v2f64 (v8i16 (X86any_cvtps2ph VR128X:$src1, timm:$src2))),
                         (iPTR 0))), addr:$dst),
            (VCVTPS2PHZ128mr addr:$dst, VR128X:$src1, timm:$src2)>;
  def : Pat<(store (i64 (extractelt
                         (bc_v2i64 (v8i16 (X86any_cvtps2ph VR128X:$src1, timm:$src2))),
                         (iPTR 0))), addr:$dst),
            (VCVTPS2PHZ128mr addr:$dst, VR128X:$src1, timm:$src2)>;
  def : Pat<(store (v8i16 (X86any_cvtps2ph VR256X:$src1, timm:$src2)), addr:$dst),
            (VCVTPS2PHZ256mr addr:$dst, VR256X:$src1, timm:$src2)>;
}

//  Unordered/Ordered scalar fp compare with Sae and set EFLAGS
multiclass avx512_ord_cmp_sae<bits<8> opc, X86VectorVTInfo _,
                              string OpcodeStr, Domain d,
                              X86FoldableSchedWrite sched = WriteFComX> {
  let ExeDomain = d, hasSideEffects = 0, Uses = [MXCSR] in
  def rrb: AVX512<opc, MRMSrcReg, (outs), (ins _.RC:$src1, _.RC:$src2),
                  !strconcat(OpcodeStr, "\t{{sae}, $src2, $src1|$src1, $src2, {sae}}"), []>,
                  EVEX, EVEX_B, VEX_LIG, EVEX_V128, Sched<[sched]>;
}

let Defs = [EFLAGS], Predicates = [HasAVX512] in {
  defm VUCOMISSZ : avx512_ord_cmp_sae<0x2E, v4f32x_info, "vucomiss", SSEPackedSingle>,
                                   AVX512PSIi8Base, EVEX_CD8<32, CD8VT1>;
  defm VUCOMISDZ : avx512_ord_cmp_sae<0x2E, v2f64x_info, "vucomisd", SSEPackedDouble>,
                                   AVX512PDIi8Base, REX_W, EVEX_CD8<64, CD8VT1>;
  defm VCOMISSZ : avx512_ord_cmp_sae<0x2F, v4f32x_info, "vcomiss", SSEPackedSingle>,
                                   AVX512PSIi8Base, EVEX_CD8<32, CD8VT1>;
  defm VCOMISDZ : avx512_ord_cmp_sae<0x2F, v2f64x_info, "vcomisd", SSEPackedDouble>,
                                   AVX512PDIi8Base, REX_W, EVEX_CD8<64, CD8VT1>;
}

let Defs = [EFLAGS], Predicates = [HasAVX512] in {
  defm VUCOMISSZ : sse12_ord_cmp<0x2E, FR32X, X86any_fcmp, f32, f32mem, loadf32,
                                 "ucomiss", SSEPackedSingle>, TB, EVEX, VEX_LIG,
                                 EVEX_CD8<32, CD8VT1>;
  defm VUCOMISDZ : sse12_ord_cmp<0x2E, FR64X, X86any_fcmp, f64, f64mem, loadf64,
                                  "ucomisd", SSEPackedDouble>, TB, PD, EVEX,
                                  VEX_LIG, REX_W, EVEX_CD8<64, CD8VT1>;
  defm VCOMISSZ  : sse12_ord_cmp<0x2F, FR32X, X86strict_fcmps, f32, f32mem, loadf32,
                                 "comiss", SSEPackedSingle>, TB, EVEX, VEX_LIG,
                                 EVEX_CD8<32, CD8VT1>;
  defm VCOMISDZ  : sse12_ord_cmp<0x2F, FR64X, X86strict_fcmps, f64, f64mem, loadf64,
                                 "comisd", SSEPackedDouble>, TB, PD, EVEX,
                                  VEX_LIG, REX_W, EVEX_CD8<64, CD8VT1>;
  let isCodeGenOnly = 1 in {
    defm VUCOMISSZ  : sse12_ord_cmp_int<0x2E, VR128X, X86ucomi, v4f32, ssmem,
                          sse_load_f32, "ucomiss", SSEPackedSingle>, TB, EVEX, VEX_LIG,
                          EVEX_CD8<32, CD8VT1>;
    defm VUCOMISDZ  : sse12_ord_cmp_int<0x2E, VR128X, X86ucomi, v2f64, sdmem,
                          sse_load_f64, "ucomisd", SSEPackedDouble>, TB, PD, EVEX,
                          VEX_LIG, REX_W, EVEX_CD8<64, CD8VT1>;

    defm VCOMISSZ  : sse12_ord_cmp_int<0x2F, VR128X, X86comi, v4f32, ssmem,
                          sse_load_f32, "comiss", SSEPackedSingle>, TB, EVEX, VEX_LIG,
                          EVEX_CD8<32, CD8VT1>;
    defm VCOMISDZ  : sse12_ord_cmp_int<0x2F, VR128X, X86comi, v2f64, sdmem,
                          sse_load_f64, "comisd", SSEPackedDouble>, TB, PD, EVEX,
                          VEX_LIG, REX_W, EVEX_CD8<64, CD8VT1>;
  }
}

let Defs = [EFLAGS], Predicates = [HasFP16] in {
  defm VUCOMISHZ : avx512_ord_cmp_sae<0x2E, v8f16x_info, "vucomish",
                                SSEPackedSingle>, AVX512PSIi8Base, T_MAP5,
                                EVEX_CD8<16, CD8VT1>;
  defm VCOMISHZ : avx512_ord_cmp_sae<0x2F, v8f16x_info, "vcomish",
                                SSEPackedSingle>, AVX512PSIi8Base, T_MAP5,
                                EVEX_CD8<16, CD8VT1>;
  defm VUCOMISHZ : sse12_ord_cmp<0x2E, FR16X, X86any_fcmp, f16, f16mem, loadf16,
                                "ucomish", SSEPackedSingle>, T_MAP5, EVEX,
                                VEX_LIG, EVEX_CD8<16, CD8VT1>;
  defm VCOMISHZ : sse12_ord_cmp<0x2F, FR16X, X86strict_fcmps, f16, f16mem, loadf16,
                                "comish", SSEPackedSingle>, T_MAP5, EVEX,
                                VEX_LIG, EVEX_CD8<16, CD8VT1>;
  let isCodeGenOnly = 1 in {
    defm VUCOMISHZ : sse12_ord_cmp_int<0x2E, VR128X, X86ucomi, v8f16, shmem,
                                sse_load_f16, "ucomish", SSEPackedSingle>,
                                T_MAP5, EVEX, VEX_LIG, EVEX_CD8<16, CD8VT1>;

    defm VCOMISHZ : sse12_ord_cmp_int<0x2F, VR128X, X86comi, v8f16, shmem,
                                sse_load_f16, "comish", SSEPackedSingle>,
                                T_MAP5, EVEX, VEX_LIG, EVEX_CD8<16, CD8VT1>;
  }
}

/// avx512_fp14_s rcp14ss, rcp14sd, rsqrt14ss, rsqrt14sd, rcpsh, rsqrtsh
multiclass avx512_fp14_s<bits<8> opc, string OpcodeStr, SDNode OpNode,
                         X86FoldableSchedWrite sched, X86VectorVTInfo _,
                         Predicate prd = HasAVX512> {
  let Predicates = [prd], ExeDomain = _.ExeDomain in {
  defm rr : AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                           (ins _.RC:$src1, _.RC:$src2), OpcodeStr,
                           "$src2, $src1", "$src1, $src2",
                           (OpNode (_.VT _.RC:$src1), (_.VT _.RC:$src2))>,
                           EVEX, VVVV, VEX_LIG, Sched<[sched]>;
  defm rm : AVX512_maskable_scalar<opc, MRMSrcMem, _, (outs _.RC:$dst),
                         (ins _.RC:$src1, _.IntScalarMemOp:$src2), OpcodeStr,
                         "$src2, $src1", "$src1, $src2",
                         (OpNode (_.VT _.RC:$src1),
                          (_.ScalarIntMemFrags addr:$src2))>, EVEX, VVVV, VEX_LIG,
                          Sched<[sched.Folded, sched.ReadAfterFold]>;
}
}

defm VRCPSHZ : avx512_fp14_s<0x4D, "vrcpsh", X86rcp14s, SchedWriteFRcp.Scl,
                               f16x_info, HasFP16>, EVEX_CD8<16, CD8VT1>,
                               T_MAP6, PD;
defm VRSQRTSHZ : avx512_fp14_s<0x4F, "vrsqrtsh", X86rsqrt14s,
                                 SchedWriteFRsqrt.Scl, f16x_info, HasFP16>,
                                 EVEX_CD8<16, CD8VT1>, T_MAP6, PD;
let Uses = [MXCSR] in {
defm VRCP14SSZ : avx512_fp14_s<0x4D, "vrcp14ss", X86rcp14s, SchedWriteFRcp.Scl,
                               f32x_info>, EVEX_CD8<32, CD8VT1>,
                               T8, PD;
defm VRCP14SDZ : avx512_fp14_s<0x4D, "vrcp14sd", X86rcp14s, SchedWriteFRcp.Scl,
                               f64x_info>, REX_W, EVEX_CD8<64, CD8VT1>,
                               T8, PD;
defm VRSQRT14SSZ : avx512_fp14_s<0x4F, "vrsqrt14ss", X86rsqrt14s,
                                 SchedWriteFRsqrt.Scl, f32x_info>,
                                 EVEX_CD8<32, CD8VT1>, T8, PD;
defm VRSQRT14SDZ : avx512_fp14_s<0x4F, "vrsqrt14sd", X86rsqrt14s,
                                 SchedWriteFRsqrt.Scl, f64x_info>, REX_W,
                                 EVEX_CD8<64, CD8VT1>, T8, PD;
}

/// avx512_fp14_p rcp14ps, rcp14pd, rsqrt14ps, rsqrt14pd
multiclass avx512_fp14_p<bits<8> opc, string OpcodeStr, SDNode OpNode,
                         X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain in {
  defm r: AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                         (ins _.RC:$src), OpcodeStr, "$src", "$src",
                         (_.VT (OpNode _.RC:$src))>, EVEX, T8, PD,
                         Sched<[sched]>;
  defm m: AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                         (ins _.MemOp:$src), OpcodeStr, "$src", "$src",
                         (OpNode (_.VT
                           (bitconvert (_.LdFrag addr:$src))))>, EVEX, T8, PD,
                         Sched<[sched.Folded, sched.ReadAfterFold]>;
  defm mb: AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                          (ins _.ScalarMemOp:$src), OpcodeStr,
                          "${src}"#_.BroadcastStr, "${src}"#_.BroadcastStr,
                          (OpNode (_.VT
                            (_.BroadcastLdFrag addr:$src)))>,
                          EVEX, T8, PD, EVEX_B, Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

multiclass avx512_fp14_p_vl_all<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                X86SchedWriteWidths sched> {
  let Uses = [MXCSR] in {
  defm 14PSZ : avx512_fp14_p<opc, !strconcat(OpcodeStr, "14ps"), OpNode, sched.ZMM,
                             v16f32_info>, EVEX_V512, EVEX_CD8<32, CD8VF>;
  defm 14PDZ : avx512_fp14_p<opc, !strconcat(OpcodeStr, "14pd"), OpNode, sched.ZMM,
                             v8f64_info>, EVEX_V512, REX_W, EVEX_CD8<64, CD8VF>;
  }
  let Predicates = [HasFP16] in
  defm PHZ : avx512_fp14_p<opc, !strconcat(OpcodeStr, "ph"), OpNode, sched.ZMM,
                           v32f16_info>, EVEX_V512, T_MAP6, EVEX_CD8<16, CD8VF>;

  // Define only if AVX512VL feature is present.
  let Predicates = [HasVLX], Uses = [MXCSR] in {
    defm 14PSZ128 : avx512_fp14_p<opc, !strconcat(OpcodeStr, "14ps"),
                                  OpNode, sched.XMM, v4f32x_info>,
                                  EVEX_V128, EVEX_CD8<32, CD8VF>;
    defm 14PSZ256 : avx512_fp14_p<opc, !strconcat(OpcodeStr, "14ps"),
                                  OpNode, sched.YMM, v8f32x_info>,
                                  EVEX_V256, EVEX_CD8<32, CD8VF>;
    defm 14PDZ128 : avx512_fp14_p<opc, !strconcat(OpcodeStr, "14pd"),
                                  OpNode, sched.XMM, v2f64x_info>,
                                  EVEX_V128, REX_W, EVEX_CD8<64, CD8VF>;
    defm 14PDZ256 : avx512_fp14_p<opc, !strconcat(OpcodeStr, "14pd"),
                                  OpNode, sched.YMM, v4f64x_info>,
                                  EVEX_V256, REX_W, EVEX_CD8<64, CD8VF>;
  }
  let Predicates = [HasFP16, HasVLX] in {
    defm PHZ128 : avx512_fp14_p<opc, !strconcat(OpcodeStr, "ph"),
                                OpNode, sched.XMM, v8f16x_info>,
                                EVEX_V128, T_MAP6, EVEX_CD8<16, CD8VF>;
    defm PHZ256 : avx512_fp14_p<opc, !strconcat(OpcodeStr, "ph"),
                                OpNode, sched.YMM, v16f16x_info>,
                                EVEX_V256, T_MAP6, EVEX_CD8<16, CD8VF>;
  }
}

defm VRSQRT : avx512_fp14_p_vl_all<0x4E, "vrsqrt", X86rsqrt14, SchedWriteFRsqrt>;
defm VRCP : avx512_fp14_p_vl_all<0x4C, "vrcp", X86rcp14, SchedWriteFRcp>;

/// avx512_fp28_s rcp28ss, rcp28sd, rsqrt28ss, rsqrt28sd
multiclass avx512_fp28_s<bits<8> opc, string OpcodeStr,X86VectorVTInfo _,
                         SDNode OpNode, SDNode OpNodeSAE,
                         X86FoldableSchedWrite sched> {
  let ExeDomain = _.ExeDomain, Uses = [MXCSR] in {
  defm r : AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                           (ins _.RC:$src1, _.RC:$src2), OpcodeStr,
                           "$src2, $src1", "$src1, $src2",
                           (OpNode (_.VT _.RC:$src1), (_.VT _.RC:$src2))>,
                           Sched<[sched]>, SIMD_EXC;

  defm rb : AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                            (ins _.RC:$src1, _.RC:$src2), OpcodeStr,
                            "{sae}, $src2, $src1", "$src1, $src2, {sae}",
                            (OpNodeSAE (_.VT _.RC:$src1), (_.VT _.RC:$src2))>,
                            EVEX_B, Sched<[sched]>;

  defm m : AVX512_maskable_scalar<opc, MRMSrcMem, _, (outs _.RC:$dst),
                         (ins _.RC:$src1, _.IntScalarMemOp:$src2), OpcodeStr,
                         "$src2, $src1", "$src1, $src2",
                         (OpNode (_.VT _.RC:$src1), (_.ScalarIntMemFrags addr:$src2))>,
                         Sched<[sched.Folded, sched.ReadAfterFold]>, SIMD_EXC;
  }
}

multiclass avx512_fp28_s_ass<bits<8> opc, string OpcodeStr, X86VectorVTInfo _,
                             X86FoldableSchedWrite sched> {
  let ExeDomain = _.ExeDomain, Uses = [MXCSR], hasSideEffects = 0 in {
  defm r : AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                           (ins _.RC:$src1, _.RC:$src2), OpcodeStr,
                           "$src2, $src1", "$src1, $src2",
                           (null_frag)>, Sched<[sched]>, SIMD_EXC;
  defm rb : AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                            (ins _.RC:$src1, _.RC:$src2), OpcodeStr,
                            "{sae}, $src2, $src1", "$src1, $src2, {sae}",
                            (null_frag)>, EVEX_B, Sched<[sched]>;
  let mayLoad = 1 in
  defm m : AVX512_maskable_scalar<opc, MRMSrcMem, _, (outs _.RC:$dst),
                         (ins _.RC:$src1, _.IntScalarMemOp:$src2), OpcodeStr,
                         "$src2, $src1", "$src1, $src2",
                         (null_frag)>,
                         Sched<[sched.Folded, sched.ReadAfterFold]>, SIMD_EXC;
  }
}

multiclass avx512_eri_s_ass<bits<8> opc, string OpcodeStr,
                            X86FoldableSchedWrite sched> {
  defm SSZ : avx512_fp28_s_ass<opc, OpcodeStr#"ss", f32x_info, sched>,
             EVEX_CD8<32, CD8VT1>, VEX_LIG, T8, PD, EVEX, VVVV;
  defm SDZ : avx512_fp28_s_ass<opc, OpcodeStr#"sd", f64x_info, sched>,
             EVEX_CD8<64, CD8VT1>, VEX_LIG, REX_W, T8, PD, EVEX, VVVV;
}

defm VRCP28   : avx512_eri_s_ass<0xCB, "vrcp28", SchedWriteFRcp.Scl>;
defm VRSQRT28 : avx512_eri_s_ass<0xCD, "vrsqrt28", SchedWriteFRsqrt.Scl>;

multiclass avx512_eri_s<bits<8> opc, string OpcodeStr, SDNode OpNode,
                        SDNode OpNodeSAE, X86FoldableSchedWrite sched> {
  defm SSZ : avx512_fp28_s<opc, OpcodeStr#"ss", f32x_info, OpNode, OpNodeSAE,
                           sched>, EVEX_CD8<32, CD8VT1>, VEX_LIG, T8, PD, EVEX, VVVV;
  defm SDZ : avx512_fp28_s<opc, OpcodeStr#"sd", f64x_info, OpNode, OpNodeSAE,
                           sched>, EVEX_CD8<64, CD8VT1>, VEX_LIG, REX_W, T8, PD, EVEX, VVVV;
}

multiclass avx512_vgetexpsh<bits<8> opc, string OpcodeStr, SDNode OpNode,
                        SDNode OpNodeSAE, X86FoldableSchedWrite sched> {
  let Predicates = [HasFP16] in
  defm SHZ : avx512_fp28_s<opc, OpcodeStr#"sh", f16x_info, OpNode,  OpNodeSAE, sched>,
               EVEX_CD8<16, CD8VT1>, T_MAP6, PD, EVEX, VVVV;
}

defm VGETEXP   : avx512_eri_s<0x43, "vgetexp", X86fgetexps, X86fgetexpSAEs,
                              SchedWriteFRnd.Scl>,
                 avx512_vgetexpsh<0x43, "vgetexp", X86fgetexps, X86fgetexpSAEs,
                                  SchedWriteFRnd.Scl>;
/// avx512_fp28_p rcp28ps, rcp28pd, rsqrt28ps, rsqrt28pd

multiclass avx512_fp28_p<bits<8> opc, string OpcodeStr, X86VectorVTInfo _,
                         SDNode OpNode, X86FoldableSchedWrite sched> {
  let ExeDomain = _.ExeDomain, Uses = [MXCSR], mayRaiseFPException = 1 in {
  defm r : AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                         (ins _.RC:$src), OpcodeStr, "$src", "$src",
                         (OpNode (_.VT _.RC:$src))>,
                         Sched<[sched]>;

  defm m : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                         (ins _.MemOp:$src), OpcodeStr, "$src", "$src",
                         (OpNode (_.VT
                             (bitconvert (_.LdFrag addr:$src))))>,
                          Sched<[sched.Folded, sched.ReadAfterFold]>;

  defm mb : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                         (ins _.ScalarMemOp:$src), OpcodeStr,
                         "${src}"#_.BroadcastStr, "${src}"#_.BroadcastStr,
                         (OpNode (_.VT
                                  (_.BroadcastLdFrag addr:$src)))>,
                         EVEX_B, Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}
multiclass avx512_fp28_p_sae<bits<8> opc, string OpcodeStr, X86VectorVTInfo _,
                         SDNode OpNode, X86FoldableSchedWrite sched> {
  let ExeDomain = _.ExeDomain, Uses = [MXCSR] in
  defm rb : AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                        (ins _.RC:$src), OpcodeStr,
                        "{sae}, $src", "$src, {sae}",
                        (OpNode (_.VT _.RC:$src))>,
                        EVEX_B, Sched<[sched]>;
}

multiclass avx512_fp28_p_ass<bits<8> opc, string OpcodeStr, X86VectorVTInfo _,
                             X86FoldableSchedWrite sched> {
  let ExeDomain = _.ExeDomain, Uses = [MXCSR], mayRaiseFPException = 1,
    hasSideEffects = 0 in {
  defm r : AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                         (ins _.RC:$src), OpcodeStr, "$src", "$src",
                         (null_frag)>, Sched<[sched]>;
  let mayLoad = 1 in
  defm m : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                         (ins _.MemOp:$src), OpcodeStr, "$src", "$src",
                         (null_frag)>,
                         Sched<[sched.Folded, sched.ReadAfterFold]>;
  let mayLoad = 1 in
  defm mb : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                         (ins _.ScalarMemOp:$src), OpcodeStr,
                         "${src}"#_.BroadcastStr, "${src}"#_.BroadcastStr,
                         (null_frag)>,
                         EVEX_B, Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}
multiclass avx512_fp28_p_sae_ass<bits<8> opc, string OpcodeStr, X86VectorVTInfo _,
                                X86FoldableSchedWrite sched> {
  let ExeDomain = _.ExeDomain, Uses = [MXCSR], hasSideEffects = 0 in
  defm rb : AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                        (ins _.RC:$src), OpcodeStr,
                        "{sae}, $src", "$src, {sae}",
                        (null_frag)>, Sched<[sched]>, EVEX_B;
}

multiclass  avx512_eri_ass<bits<8> opc, string OpcodeStr,
                           X86SchedWriteWidths sched> {
   defm PSZ : avx512_fp28_p_ass<opc, OpcodeStr#"ps", v16f32_info, sched.ZMM>,
              avx512_fp28_p_sae_ass<opc, OpcodeStr#"ps", v16f32_info, sched.ZMM>,
              T8, PD, EVEX_V512, EVEX_CD8<32, CD8VF>;
   defm PDZ : avx512_fp28_p_ass<opc, OpcodeStr#"pd", v8f64_info, sched.ZMM>,
              avx512_fp28_p_sae_ass<opc, OpcodeStr#"pd", v8f64_info, sched.ZMM>,
              T8, PD, EVEX_V512, REX_W, EVEX_CD8<64, CD8VF>;
}

defm VRSQRT28 : avx512_eri_ass<0xCC, "vrsqrt28", SchedWriteFRsqrt>, EVEX;
defm VRCP28   : avx512_eri_ass<0xCA, "vrcp28", SchedWriteFRcp>, EVEX;
defm VEXP2    : avx512_eri_ass<0xC8, "vexp2", SchedWriteFAdd>, EVEX;

multiclass  avx512_eri<bits<8> opc, string OpcodeStr, SDNode OpNode,
                       SDNode OpNodeSAE, X86SchedWriteWidths sched> {
   defm PSZ : avx512_fp28_p<opc, OpcodeStr#"ps", v16f32_info, OpNode, sched.ZMM>,
              avx512_fp28_p_sae<opc, OpcodeStr#"ps", v16f32_info, OpNodeSAE, sched.ZMM>,
              T8, PD, EVEX_V512, EVEX_CD8<32, CD8VF>;
   defm PDZ : avx512_fp28_p<opc, OpcodeStr#"pd", v8f64_info, OpNode, sched.ZMM>,
              avx512_fp28_p_sae<opc, OpcodeStr#"pd", v8f64_info, OpNodeSAE, sched.ZMM>,
              T8, PD, EVEX_V512, REX_W, EVEX_CD8<64, CD8VF>;
}

multiclass avx512_fp_unaryop_packed<bits<8> opc, string OpcodeStr,
                                  SDNode OpNode, X86SchedWriteWidths sched> {
  // Define only if AVX512VL feature is present.
  let Predicates = [HasVLX] in {
    defm PSZ128 : avx512_fp28_p<opc, OpcodeStr#"ps", v4f32x_info, OpNode,
                                sched.XMM>,
                                EVEX_V128, T8, PD, EVEX_CD8<32, CD8VF>;
    defm PSZ256 : avx512_fp28_p<opc, OpcodeStr#"ps", v8f32x_info, OpNode,
                                sched.YMM>,
                                EVEX_V256, T8, PD, EVEX_CD8<32, CD8VF>;
    defm PDZ128 : avx512_fp28_p<opc, OpcodeStr#"pd", v2f64x_info, OpNode,
                                sched.XMM>,
                                EVEX_V128, REX_W, T8, PD, EVEX_CD8<64, CD8VF>;
    defm PDZ256 : avx512_fp28_p<opc, OpcodeStr#"pd", v4f64x_info, OpNode,
                                sched.YMM>,
                                EVEX_V256, REX_W, T8, PD, EVEX_CD8<64, CD8VF>;
  }
}

multiclass  avx512_vgetexp_fp16<bits<8> opc, string OpcodeStr, SDNode OpNode,
                       SDNode OpNodeSAE, X86SchedWriteWidths sched> {
  let Predicates = [HasFP16] in
  defm PHZ : avx512_fp28_p<opc, OpcodeStr#"ph", v32f16_info, OpNode, sched.ZMM>,
              avx512_fp28_p_sae<opc, OpcodeStr#"ph", v32f16_info, OpNodeSAE, sched.ZMM>,
              T_MAP6, PD, EVEX_V512, EVEX_CD8<16, CD8VF>;
  let Predicates = [HasFP16, HasVLX] in {
    defm PHZ128 : avx512_fp28_p<opc, OpcodeStr#"ph", v8f16x_info, OpNode, sched.XMM>,
                                     EVEX_V128, T_MAP6, PD, EVEX_CD8<16, CD8VF>;
    defm PHZ256 : avx512_fp28_p<opc, OpcodeStr#"ph", v16f16x_info, OpNode, sched.YMM>,
                                     EVEX_V256, T_MAP6, PD, EVEX_CD8<16, CD8VF>;
  }
}
defm VGETEXP   : avx512_eri<0x42, "vgetexp", X86fgetexp, X86fgetexpSAE,
                            SchedWriteFRnd>,
                 avx512_vgetexp_fp16<0x42, "vgetexp", X86fgetexp, X86fgetexpSAE,
                                     SchedWriteFRnd>,
                 avx512_fp_unaryop_packed<0x42, "vgetexp", X86fgetexp,
                                          SchedWriteFRnd>, EVEX;

multiclass avx512_sqrt_packed_round<bits<8> opc, string OpcodeStr,
                                    X86FoldableSchedWrite sched, X86VectorVTInfo _>{
  let ExeDomain = _.ExeDomain in
  defm rb: AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                         (ins _.RC:$src, AVX512RC:$rc), OpcodeStr, "$rc, $src", "$src, $rc",
                         (_.VT (X86fsqrtRnd _.RC:$src, (i32 timm:$rc)))>,
                         EVEX, EVEX_B, EVEX_RC, Sched<[sched]>;
}

multiclass avx512_sqrt_packed<bits<8> opc, string OpcodeStr,
                              X86FoldableSchedWrite sched, X86VectorVTInfo _>{
  let ExeDomain = _.ExeDomain, Uses = [MXCSR], mayRaiseFPException = 1 in {
  defm r: AVX512_maskable_split<opc, MRMSrcReg, _, (outs _.RC:$dst),
                         (ins _.RC:$src), OpcodeStr, "$src", "$src",
                         (_.VT (any_fsqrt _.RC:$src)),
                         (_.VT (fsqrt _.RC:$src))>, EVEX,
                         Sched<[sched]>;
  defm m: AVX512_maskable_split<opc, MRMSrcMem, _, (outs _.RC:$dst),
                         (ins _.MemOp:$src), OpcodeStr, "$src", "$src",
                         (any_fsqrt (_.VT (_.LdFrag addr:$src))),
                         (fsqrt (_.VT (_.LdFrag addr:$src)))>, EVEX,
                         Sched<[sched.Folded, sched.ReadAfterFold]>;
  defm mb: AVX512_maskable_split<opc, MRMSrcMem, _, (outs _.RC:$dst),
                          (ins _.ScalarMemOp:$src), OpcodeStr,
                          "${src}"#_.BroadcastStr, "${src}"#_.BroadcastStr,
                          (any_fsqrt (_.VT (_.BroadcastLdFrag addr:$src))),
                          (fsqrt (_.VT (_.BroadcastLdFrag addr:$src)))>,
                          EVEX, EVEX_B, Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

let Uses = [MXCSR], mayRaiseFPException = 1 in
multiclass avx512_sqrt_packed_all<bits<8> opc, string OpcodeStr,
                                  X86SchedWriteSizes sched> {
  let Predicates = [HasFP16] in
  defm PHZ : avx512_sqrt_packed<opc, !strconcat(OpcodeStr, "ph"),
                                sched.PH.ZMM, v32f16_info>,
                                EVEX_V512, T_MAP5, EVEX_CD8<16, CD8VF>;
  let Predicates = [HasFP16, HasVLX] in {
    defm PHZ128 : avx512_sqrt_packed<opc, !strconcat(OpcodeStr, "ph"),
                                     sched.PH.XMM, v8f16x_info>,
                                     EVEX_V128, T_MAP5, EVEX_CD8<16, CD8VF>;
    defm PHZ256 : avx512_sqrt_packed<opc, !strconcat(OpcodeStr, "ph"),
                                     sched.PH.YMM, v16f16x_info>,
                                     EVEX_V256, T_MAP5, EVEX_CD8<16, CD8VF>;
  }
  defm PSZ : avx512_sqrt_packed<opc, !strconcat(OpcodeStr, "ps"),
                                sched.PS.ZMM, v16f32_info>,
                                EVEX_V512, TB, EVEX_CD8<32, CD8VF>;
  defm PDZ : avx512_sqrt_packed<opc, !strconcat(OpcodeStr, "pd"),
                                sched.PD.ZMM, v8f64_info>,
                                EVEX_V512, REX_W, TB, PD, EVEX_CD8<64, CD8VF>;
  // Define only if AVX512VL feature is present.
  let Predicates = [HasVLX] in {
    defm PSZ128 : avx512_sqrt_packed<opc, !strconcat(OpcodeStr, "ps"),
                                     sched.PS.XMM, v4f32x_info>,
                                     EVEX_V128, TB, EVEX_CD8<32, CD8VF>;
    defm PSZ256 : avx512_sqrt_packed<opc, !strconcat(OpcodeStr, "ps"),
                                     sched.PS.YMM, v8f32x_info>,
                                     EVEX_V256, TB, EVEX_CD8<32, CD8VF>;
    defm PDZ128 : avx512_sqrt_packed<opc, !strconcat(OpcodeStr, "pd"),
                                     sched.PD.XMM, v2f64x_info>,
                                     EVEX_V128, REX_W, TB, PD, EVEX_CD8<64, CD8VF>;
    defm PDZ256 : avx512_sqrt_packed<opc, !strconcat(OpcodeStr, "pd"),
                                     sched.PD.YMM, v4f64x_info>,
                                     EVEX_V256, REX_W, TB, PD, EVEX_CD8<64, CD8VF>;
  }
}

let Uses = [MXCSR] in
multiclass avx512_sqrt_packed_all_round<bits<8> opc, string OpcodeStr,
                                        X86SchedWriteSizes sched> {
  let Predicates = [HasFP16] in
  defm PHZ : avx512_sqrt_packed_round<opc, !strconcat(OpcodeStr, "ph"),
                                      sched.PH.ZMM, v32f16_info>,
                                      EVEX_V512, T_MAP5, EVEX_CD8<16, CD8VF>;
  defm PSZ : avx512_sqrt_packed_round<opc, !strconcat(OpcodeStr, "ps"),
                                      sched.PS.ZMM, v16f32_info>,
                                      EVEX_V512, TB, EVEX_CD8<32, CD8VF>;
  defm PDZ : avx512_sqrt_packed_round<opc, !strconcat(OpcodeStr, "pd"),
                                      sched.PD.ZMM, v8f64_info>,
                                      EVEX_V512, REX_W, TB, PD, EVEX_CD8<64, CD8VF>;
}

multiclass avx512_sqrt_scalar<bits<8> opc, string OpcodeStr, X86FoldableSchedWrite sched,
                              X86VectorVTInfo _, string Name, Predicate prd = HasAVX512> {
  let ExeDomain = _.ExeDomain, Predicates = [prd] in {
    defm r_Int : AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                         (ins _.RC:$src1, _.RC:$src2), OpcodeStr,
                         "$src2, $src1", "$src1, $src2",
                         (X86fsqrts (_.VT _.RC:$src1),
                                    (_.VT _.RC:$src2))>,
                         Sched<[sched]>, SIMD_EXC;
    defm m_Int : AVX512_maskable_scalar<opc, MRMSrcMem, _, (outs _.RC:$dst),
                         (ins _.RC:$src1, _.IntScalarMemOp:$src2), OpcodeStr,
                         "$src2, $src1", "$src1, $src2",
                         (X86fsqrts (_.VT _.RC:$src1),
                                    (_.ScalarIntMemFrags addr:$src2))>,
                         Sched<[sched.Folded, sched.ReadAfterFold]>, SIMD_EXC;
    let Uses = [MXCSR] in
    defm rb_Int : AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                         (ins _.RC:$src1, _.RC:$src2, AVX512RC:$rc), OpcodeStr,
                         "$rc, $src2, $src1", "$src1, $src2, $rc",
                         (X86fsqrtRnds (_.VT _.RC:$src1),
                                     (_.VT _.RC:$src2),
                                     (i32 timm:$rc))>,
                         EVEX_B, EVEX_RC, Sched<[sched]>;

    let isCodeGenOnly = 1, hasSideEffects = 0 in {
      def r : I<opc, MRMSrcReg, (outs _.FRC:$dst),
                (ins _.FRC:$src1, _.FRC:$src2),
                OpcodeStr#"\t{$src2, $src1, $dst|$dst, $src1, $src2}", []>,
                Sched<[sched]>, SIMD_EXC;
      let mayLoad = 1 in
        def m : I<opc, MRMSrcMem, (outs _.FRC:$dst),
                  (ins _.FRC:$src1, _.ScalarMemOp:$src2),
                  OpcodeStr#"\t{$src2, $src1, $dst|$dst, $src1, $src2}", []>,
                  Sched<[sched.Folded, sched.ReadAfterFold]>, SIMD_EXC;
    }
  }

  let Predicates = [prd] in {
    def : Pat<(_.EltVT (any_fsqrt _.FRC:$src)),
              (!cast<Instruction>(Name#Zr)
                  (_.EltVT (IMPLICIT_DEF)), _.FRC:$src)>;
  }

  let Predicates = [prd, OptForSize] in {
    def : Pat<(_.EltVT (any_fsqrt (load addr:$src))),
              (!cast<Instruction>(Name#Zm)
                  (_.EltVT (IMPLICIT_DEF)), addr:$src)>;
  }
}

multiclass avx512_sqrt_scalar_all<bits<8> opc, string OpcodeStr,
                                  X86SchedWriteSizes sched> {
  defm SHZ : avx512_sqrt_scalar<opc, OpcodeStr#"sh", sched.PH.Scl, f16x_info, NAME#"SH", HasFP16>,
                        EVEX_CD8<16, CD8VT1>, EVEX, VVVV, T_MAP5, XS;
  defm SSZ : avx512_sqrt_scalar<opc, OpcodeStr#"ss", sched.PS.Scl, f32x_info, NAME#"SS">,
                        EVEX_CD8<32, CD8VT1>, EVEX, VVVV, TB, XS;
  defm SDZ : avx512_sqrt_scalar<opc, OpcodeStr#"sd", sched.PD.Scl, f64x_info, NAME#"SD">,
                        EVEX_CD8<64, CD8VT1>, EVEX, VVVV, TB, XD, REX_W;
}

defm VSQRT : avx512_sqrt_packed_all<0x51, "vsqrt", SchedWriteFSqrtSizes>,
             avx512_sqrt_packed_all_round<0x51, "vsqrt", SchedWriteFSqrtSizes>;

defm VSQRT : avx512_sqrt_scalar_all<0x51, "vsqrt", SchedWriteFSqrtSizes>, VEX_LIG;

multiclass avx512_rndscale_scalar<bits<8> opc, string OpcodeStr,
                                  X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain in {
  defm r_Int : AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                           (ins _.RC:$src1, _.RC:$src2, i32u8imm:$src3), OpcodeStr,
                           "$src3, $src2, $src1", "$src1, $src2, $src3",
                           (_.VT (X86RndScales (_.VT _.RC:$src1), (_.VT _.RC:$src2),
                           (i32 timm:$src3)))>,
                           Sched<[sched]>, SIMD_EXC;

  let Uses = [MXCSR] in
  defm rb_Int : AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                         (ins _.RC:$src1, _.RC:$src2, i32u8imm:$src3), OpcodeStr,
                         "$src3, {sae}, $src2, $src1", "$src1, $src2, {sae}, $src3",
                         (_.VT (X86RndScalesSAE (_.VT _.RC:$src1), (_.VT _.RC:$src2),
                         (i32 timm:$src3)))>, EVEX_B,
                         Sched<[sched]>;

  defm m_Int : AVX512_maskable_scalar<opc, MRMSrcMem, _, (outs _.RC:$dst),
                         (ins _.RC:$src1, _.IntScalarMemOp:$src2, i32u8imm:$src3),
                         OpcodeStr,
                         "$src3, $src2, $src1", "$src1, $src2, $src3",
                         (_.VT (X86RndScales _.RC:$src1,
                                (_.ScalarIntMemFrags addr:$src2), (i32 timm:$src3)))>,
                         Sched<[sched.Folded, sched.ReadAfterFold]>, SIMD_EXC;

  let isCodeGenOnly = 1, hasSideEffects = 0, Predicates = [HasAVX512] in {
    def r : I<opc, MRMSrcReg, (outs _.FRC:$dst),
               (ins _.FRC:$src1, _.FRC:$src2, i32u8imm:$src3),
               OpcodeStr#"\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}",
               []>, Sched<[sched]>, SIMD_EXC;

    let mayLoad = 1 in
      def m : I<opc, MRMSrcMem, (outs _.FRC:$dst),
                 (ins _.FRC:$src1, _.ScalarMemOp:$src2, i32u8imm:$src3),
                 OpcodeStr#"\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}",
                 []>, Sched<[sched.Folded, sched.ReadAfterFold]>, SIMD_EXC;
  }
  }

  let Predicates = [HasAVX512] in {
    def : Pat<(X86any_VRndScale _.FRC:$src1, timm:$src2),
              (_.EltVT (!cast<Instruction>(NAME#r) (_.EltVT (IMPLICIT_DEF)),
               _.FRC:$src1, timm:$src2))>;
  }

  let Predicates = [HasAVX512, OptForSize] in {
    def : Pat<(X86any_VRndScale (_.ScalarLdFrag addr:$src1), timm:$src2),
              (_.EltVT (!cast<Instruction>(NAME#m) (_.EltVT (IMPLICIT_DEF)),
               addr:$src1, timm:$src2))>;
  }
}

let Predicates = [HasFP16] in
defm VRNDSCALESHZ : avx512_rndscale_scalar<0x0A, "vrndscalesh",
                                           SchedWriteFRnd.Scl, f16x_info>,
                                           AVX512PSIi8Base, TA, EVEX, VVVV,
                                           EVEX_CD8<16, CD8VT1>;

defm VRNDSCALESSZ : avx512_rndscale_scalar<0x0A, "vrndscaless",
                                           SchedWriteFRnd.Scl, f32x_info>,
                                           AVX512AIi8Base, EVEX, VVVV, VEX_LIG,
                                           EVEX_CD8<32, CD8VT1>;

defm VRNDSCALESDZ : avx512_rndscale_scalar<0x0B, "vrndscalesd",
                                           SchedWriteFRnd.Scl, f64x_info>,
                                           REX_W, AVX512AIi8Base, EVEX, VVVV, VEX_LIG,
                                           EVEX_CD8<64, CD8VT1>;

multiclass avx512_masked_scalar<SDNode OpNode, string OpcPrefix, SDNode Move,
                                dag Mask, X86VectorVTInfo _, PatLeaf ZeroFP,
                                dag OutMask, Predicate BasePredicate> {
  let Predicates = [BasePredicate] in {
    def : Pat<(Move _.VT:$src1, (scalar_to_vector (X86selects_mask Mask,
               (OpNode (extractelt _.VT:$src2, (iPTR 0))),
               (extractelt _.VT:$dst, (iPTR 0))))),
              (!cast<Instruction>("V"#OpcPrefix#r_Intk)
               _.VT:$dst, OutMask, _.VT:$src2, _.VT:$src1)>;

    def : Pat<(Move _.VT:$src1, (scalar_to_vector (X86selects_mask Mask,
               (OpNode (extractelt _.VT:$src2, (iPTR 0))),
               ZeroFP))),
              (!cast<Instruction>("V"#OpcPrefix#r_Intkz)
               OutMask, _.VT:$src2, _.VT:$src1)>;
  }
}

defm : avx512_masked_scalar<fsqrt, "SQRTSHZ", X86Movsh,
                            (v1i1 (scalar_to_vector (i8 (trunc (i32 GR32:$mask))))), v8f16x_info,
                            fp16imm0, (COPY_TO_REGCLASS  $mask, VK1WM), HasFP16>;
defm : avx512_masked_scalar<fsqrt, "SQRTSSZ", X86Movss,
                            (v1i1 (scalar_to_vector (i8 (trunc (i32 GR32:$mask))))), v4f32x_info,
                            fp32imm0, (COPY_TO_REGCLASS  $mask, VK1WM), HasAVX512>;
defm : avx512_masked_scalar<fsqrt, "SQRTSDZ", X86Movsd,
                            (v1i1 (scalar_to_vector (i8 (trunc (i32 GR32:$mask))))), v2f64x_info,
                            fp64imm0, (COPY_TO_REGCLASS  $mask, VK1WM), HasAVX512>;


//-------------------------------------------------
// Integer truncate and extend operations
//-------------------------------------------------

multiclass avx512_trunc_common<bits<8> opc, string OpcodeStr, SDNode OpNode,
                              SDPatternOperator MaskNode,
                              X86FoldableSchedWrite sched, X86VectorVTInfo SrcInfo,
                              X86VectorVTInfo DestInfo, X86MemOperand x86memop> {
  let ExeDomain = DestInfo.ExeDomain in {
  def rr : AVX512XS8I<opc, MRMDestReg, (outs DestInfo.RC:$dst),
             (ins SrcInfo.RC:$src),
             OpcodeStr # "\t{$src, $dst|$dst, $src}",
             [(set DestInfo.RC:$dst,
                   (DestInfo.VT (OpNode (SrcInfo.VT SrcInfo.RC:$src))))]>,
             EVEX, Sched<[sched]>;
  let Constraints = "$src0 = $dst" in
  def rrk : AVX512XS8I<opc, MRMDestReg, (outs DestInfo.RC:$dst),
             (ins DestInfo.RC:$src0, SrcInfo.KRCWM:$mask, SrcInfo.RC:$src),
             OpcodeStr # "\t{$src, $dst {${mask}}|$dst {${mask}}, $src}",
             [(set DestInfo.RC:$dst,
                   (MaskNode (SrcInfo.VT SrcInfo.RC:$src),
                             (DestInfo.VT DestInfo.RC:$src0),
                             SrcInfo.KRCWM:$mask))]>,
             EVEX, EVEX_K, Sched<[sched]>;
  def rrkz : AVX512XS8I<opc, MRMDestReg, (outs DestInfo.RC:$dst),
             (ins SrcInfo.KRCWM:$mask, SrcInfo.RC:$src),
             OpcodeStr # "\t{$src, $dst {${mask}} {z}|$dst {${mask}} {z}, $src}",
             [(set DestInfo.RC:$dst,
                   (DestInfo.VT (MaskNode (SrcInfo.VT SrcInfo.RC:$src),
                             DestInfo.ImmAllZerosV, SrcInfo.KRCWM:$mask)))]>,
             EVEX, EVEX_KZ, Sched<[sched]>;
  }

  let mayStore = 1, hasSideEffects = 0, ExeDomain = DestInfo.ExeDomain in {
    def mr : AVX512XS8I<opc, MRMDestMem, (outs),
               (ins x86memop:$dst, SrcInfo.RC:$src),
               OpcodeStr # "\t{$src, $dst|$dst, $src}", []>,
               EVEX, Sched<[sched.Folded]>;

    def mrk : AVX512XS8I<opc, MRMDestMem, (outs),
               (ins x86memop:$dst, SrcInfo.KRCWM:$mask, SrcInfo.RC:$src),
               OpcodeStr # "\t{$src, $dst {${mask}}|$dst {${mask}}, $src}", []>,
               EVEX, EVEX_K, Sched<[sched.Folded]>;
  }//mayStore = 1, hasSideEffects = 0
}

multiclass avx512_trunc_mr_lowering<X86VectorVTInfo SrcInfo,
                                    PatFrag truncFrag, PatFrag mtruncFrag,
                                    string Name> {

  def : Pat<(truncFrag (SrcInfo.VT SrcInfo.RC:$src), addr:$dst),
            (!cast<Instruction>(Name#SrcInfo.ZSuffix#mr)
                                    addr:$dst, SrcInfo.RC:$src)>;

  def : Pat<(mtruncFrag (SrcInfo.VT SrcInfo.RC:$src), addr:$dst,
                        SrcInfo.KRCWM:$mask),
            (!cast<Instruction>(Name#SrcInfo.ZSuffix#mrk)
                            addr:$dst, SrcInfo.KRCWM:$mask, SrcInfo.RC:$src)>;
}

multiclass avx512_trunc<bits<8> opc, string OpcodeStr, SDNode OpNode128,
                        SDNode OpNode256, SDNode OpNode512,
                        SDPatternOperator MaskNode128,
                        SDPatternOperator MaskNode256,
                        SDPatternOperator MaskNode512,
                        X86SchedWriteWidths sched,
                        AVX512VLVectorVTInfo VTSrcInfo,
                        X86VectorVTInfo DestInfoZ128,
                        X86VectorVTInfo DestInfoZ256, X86VectorVTInfo DestInfoZ,
                        X86MemOperand x86memopZ128, X86MemOperand x86memopZ256,
                        X86MemOperand x86memopZ, PatFrag truncFrag,
                        PatFrag mtruncFrag, Predicate prd = HasAVX512>{

  let Predicates = [HasVLX, prd] in {
    defm Z128:  avx512_trunc_common<opc, OpcodeStr, OpNode128, MaskNode128, sched.XMM,
                             VTSrcInfo.info128, DestInfoZ128, x86memopZ128>,
                avx512_trunc_mr_lowering<VTSrcInfo.info128, truncFrag,
                                         mtruncFrag, NAME>, EVEX_V128;

    defm Z256:  avx512_trunc_common<opc, OpcodeStr, OpNode256, MaskNode256, sched.YMM,
                             VTSrcInfo.info256, DestInfoZ256, x86memopZ256>,
                avx512_trunc_mr_lowering<VTSrcInfo.info256, truncFrag,
                                         mtruncFrag, NAME>, EVEX_V256;
  }
  let Predicates = [prd] in
    defm Z:     avx512_trunc_common<opc, OpcodeStr, OpNode512, MaskNode512, sched.ZMM,
                             VTSrcInfo.info512, DestInfoZ, x86memopZ>,
                avx512_trunc_mr_lowering<VTSrcInfo.info512, truncFrag,
                                         mtruncFrag, NAME>, EVEX_V512;
}

multiclass avx512_trunc_qb<bits<8> opc, string OpcodeStr,
                           X86SchedWriteWidths sched, PatFrag StoreNode,
                           PatFrag MaskedStoreNode, SDNode InVecNode,
                           SDPatternOperator InVecMaskNode> {
  defm NAME: avx512_trunc<opc, OpcodeStr, InVecNode, InVecNode, InVecNode,
                          InVecMaskNode, InVecMaskNode, InVecMaskNode, sched,
                          avx512vl_i64_info, v16i8x_info, v16i8x_info,
                          v16i8x_info, i16mem, i32mem, i64mem, StoreNode,
                          MaskedStoreNode>, EVEX_CD8<8, CD8VO>;
}

multiclass avx512_trunc_qw<bits<8> opc, string OpcodeStr, SDNode OpNode,
                           SDPatternOperator MaskNode,
                           X86SchedWriteWidths sched, PatFrag StoreNode,
                           PatFrag MaskedStoreNode, SDNode InVecNode,
                           SDPatternOperator InVecMaskNode> {
  defm NAME: avx512_trunc<opc, OpcodeStr, InVecNode, InVecNode, OpNode,
                          InVecMaskNode, InVecMaskNode, MaskNode, sched,
                          avx512vl_i64_info, v8i16x_info, v8i16x_info,
                          v8i16x_info, i32mem, i64mem, i128mem, StoreNode,
                          MaskedStoreNode>, EVEX_CD8<16, CD8VQ>;
}

multiclass avx512_trunc_qd<bits<8> opc, string OpcodeStr, SDNode OpNode,
                           SDPatternOperator MaskNode,
                           X86SchedWriteWidths sched, PatFrag StoreNode,
                           PatFrag MaskedStoreNode, SDNode InVecNode,
                           SDPatternOperator InVecMaskNode> {
  defm NAME: avx512_trunc<opc, OpcodeStr, InVecNode, OpNode, OpNode,
                          InVecMaskNode, MaskNode, MaskNode, sched,
                          avx512vl_i64_info, v4i32x_info, v4i32x_info,
                          v8i32x_info, i64mem, i128mem, i256mem, StoreNode,
                          MaskedStoreNode>, EVEX_CD8<32, CD8VH>;
}

multiclass avx512_trunc_db<bits<8> opc, string OpcodeStr, SDNode OpNode,
                           SDPatternOperator MaskNode,
                           X86SchedWriteWidths sched, PatFrag StoreNode,
                           PatFrag MaskedStoreNode, SDNode InVecNode,
                           SDPatternOperator InVecMaskNode> {
  defm NAME: avx512_trunc<opc, OpcodeStr, InVecNode, InVecNode, OpNode,
                          InVecMaskNode, InVecMaskNode, MaskNode, sched,
                          avx512vl_i32_info, v16i8x_info, v16i8x_info,
                          v16i8x_info, i32mem, i64mem, i128mem, StoreNode,
                          MaskedStoreNode>, EVEX_CD8<8, CD8VQ>;
}

multiclass avx512_trunc_dw<bits<8> opc, string OpcodeStr, SDNode OpNode,
                           SDPatternOperator MaskNode,
                           X86SchedWriteWidths sched, PatFrag StoreNode,
                           PatFrag MaskedStoreNode, SDNode InVecNode,
                           SDPatternOperator InVecMaskNode> {
  defm NAME: avx512_trunc<opc, OpcodeStr, InVecNode, OpNode, OpNode,
                          InVecMaskNode, MaskNode, MaskNode, sched,
                          avx512vl_i32_info, v8i16x_info, v8i16x_info,
                          v16i16x_info, i64mem, i128mem, i256mem, StoreNode,
                          MaskedStoreNode>, EVEX_CD8<16, CD8VH>;
}

multiclass avx512_trunc_wb<bits<8> opc, string OpcodeStr, SDNode OpNode,
                           SDPatternOperator MaskNode,
                           X86SchedWriteWidths sched, PatFrag StoreNode,
                           PatFrag MaskedStoreNode, SDNode InVecNode,
                           SDPatternOperator InVecMaskNode> {
  defm NAME: avx512_trunc<opc, OpcodeStr, InVecNode, OpNode, OpNode,
                          InVecMaskNode, MaskNode, MaskNode, sched,
                          avx512vl_i16_info, v16i8x_info, v16i8x_info,
                          v32i8x_info, i64mem, i128mem, i256mem, StoreNode,
                          MaskedStoreNode, HasBWI>, EVEX_CD8<16, CD8VH>;
}

defm VPMOVQB    : avx512_trunc_qb<0x32, "vpmovqb",
                                  SchedWriteVecTruncate, truncstorevi8,
                                  masked_truncstorevi8, X86vtrunc, X86vmtrunc>;
defm VPMOVSQB   : avx512_trunc_qb<0x22, "vpmovsqb",
                                  SchedWriteVecTruncate, truncstore_s_vi8,
                                  masked_truncstore_s_vi8, X86vtruncs,
                                  X86vmtruncs>;
defm VPMOVUSQB  : avx512_trunc_qb<0x12, "vpmovusqb",
                                  SchedWriteVecTruncate, truncstore_us_vi8,
                                  masked_truncstore_us_vi8, X86vtruncus, X86vmtruncus>;

defm VPMOVQW    : avx512_trunc_qw<0x34, "vpmovqw", trunc, select_trunc,
                                  SchedWriteVecTruncate, truncstorevi16,
                                  masked_truncstorevi16, X86vtrunc, X86vmtrunc>;
defm VPMOVSQW   : avx512_trunc_qw<0x24, "vpmovsqw",  X86vtruncs, select_truncs,
                                  SchedWriteVecTruncate, truncstore_s_vi16,
                                  masked_truncstore_s_vi16, X86vtruncs,
                                  X86vmtruncs>;
defm VPMOVUSQW  : avx512_trunc_qw<0x14, "vpmovusqw", X86vtruncus,
                                  select_truncus, SchedWriteVecTruncate,
                                  truncstore_us_vi16, masked_truncstore_us_vi16,
                                  X86vtruncus, X86vmtruncus>;

defm VPMOVQD    : avx512_trunc_qd<0x35, "vpmovqd", trunc, select_trunc,
                                  SchedWriteVecTruncate, truncstorevi32,
                                  masked_truncstorevi32, X86vtrunc, X86vmtrunc>;
defm VPMOVSQD   : avx512_trunc_qd<0x25, "vpmovsqd",  X86vtruncs, select_truncs,
                                  SchedWriteVecTruncate, truncstore_s_vi32,
                                  masked_truncstore_s_vi32, X86vtruncs,
                                  X86vmtruncs>;
defm VPMOVUSQD  : avx512_trunc_qd<0x15, "vpmovusqd", X86vtruncus,
                                  select_truncus, SchedWriteVecTruncate,
                                  truncstore_us_vi32, masked_truncstore_us_vi32,
                                  X86vtruncus, X86vmtruncus>;

defm VPMOVDB    : avx512_trunc_db<0x31, "vpmovdb", trunc, select_trunc,
                                  SchedWriteVecTruncate, truncstorevi8,
                                  masked_truncstorevi8, X86vtrunc, X86vmtrunc>;
defm VPMOVSDB   : avx512_trunc_db<0x21, "vpmovsdb", X86vtruncs, select_truncs,
                                  SchedWriteVecTruncate, truncstore_s_vi8,
                                  masked_truncstore_s_vi8, X86vtruncs,
                                  X86vmtruncs>;
defm VPMOVUSDB  : avx512_trunc_db<0x11, "vpmovusdb",  X86vtruncus,
                                  select_truncus, SchedWriteVecTruncate,
                                  truncstore_us_vi8, masked_truncstore_us_vi8,
                                  X86vtruncus, X86vmtruncus>;

defm VPMOVDW    : avx512_trunc_dw<0x33, "vpmovdw", trunc, select_trunc,
                                  SchedWriteVecTruncate, truncstorevi16,
                                  masked_truncstorevi16, X86vtrunc, X86vmtrunc>;
defm VPMOVSDW   : avx512_trunc_dw<0x23, "vpmovsdw", X86vtruncs, select_truncs,
                                  SchedWriteVecTruncate, truncstore_s_vi16,
                                  masked_truncstore_s_vi16, X86vtruncs,
                                  X86vmtruncs>;
defm VPMOVUSDW  : avx512_trunc_dw<0x13, "vpmovusdw", X86vtruncus,
                                  select_truncus, SchedWriteVecTruncate,
                                  truncstore_us_vi16, masked_truncstore_us_vi16,
                                  X86vtruncus, X86vmtruncus>;

defm VPMOVWB    : avx512_trunc_wb<0x30, "vpmovwb", trunc, select_trunc,
                                  SchedWriteVecTruncate, truncstorevi8,
                                  masked_truncstorevi8, X86vtrunc,
                                  X86vmtrunc>;
defm VPMOVSWB   : avx512_trunc_wb<0x20, "vpmovswb", X86vtruncs, select_truncs,
                                  SchedWriteVecTruncate, truncstore_s_vi8,
                                  masked_truncstore_s_vi8, X86vtruncs,
                                  X86vmtruncs>;
defm VPMOVUSWB  : avx512_trunc_wb<0x10, "vpmovuswb", X86vtruncus,
                                  select_truncus, SchedWriteVecTruncate,
                                  truncstore_us_vi8, masked_truncstore_us_vi8,
                                  X86vtruncus, X86vmtruncus>;

let Predicates = [HasAVX512, NoVLX, HasEVEX512] in {
def: Pat<(v8i16 (trunc (v8i32 VR256X:$src))),
         (v8i16 (EXTRACT_SUBREG
                 (v16i16 (VPMOVDWZrr (v16i32 (INSERT_SUBREG (IMPLICIT_DEF),
                                          VR256X:$src, sub_ymm)))), sub_xmm))>;
def: Pat<(v4i32 (trunc (v4i64 VR256X:$src))),
         (v4i32 (EXTRACT_SUBREG
                 (v8i32 (VPMOVQDZrr (v8i64 (INSERT_SUBREG (IMPLICIT_DEF),
                                           VR256X:$src, sub_ymm)))), sub_xmm))>;
}

let Predicates = [HasBWI, NoVLX, HasEVEX512] in {
def: Pat<(v16i8 (trunc (v16i16 VR256X:$src))),
         (v16i8 (EXTRACT_SUBREG (VPMOVWBZrr (v32i16 (INSERT_SUBREG (IMPLICIT_DEF),
                                            VR256X:$src, sub_ymm))), sub_xmm))>;
}

// Without BWI we can't use vXi16/vXi8 vselect so we have to use vmtrunc nodes.
multiclass mtrunc_lowering<string InstrName, SDNode OpNode,
                           X86VectorVTInfo DestInfo,
                           X86VectorVTInfo SrcInfo> {
  def : Pat<(DestInfo.VT (OpNode (SrcInfo.VT SrcInfo.RC:$src),
                                 DestInfo.RC:$src0,
                                 SrcInfo.KRCWM:$mask)),
            (!cast<Instruction>(InstrName#"rrk") DestInfo.RC:$src0,
                                                 SrcInfo.KRCWM:$mask,
                                                 SrcInfo.RC:$src)>;

  def : Pat<(DestInfo.VT (OpNode (SrcInfo.VT SrcInfo.RC:$src),
                                 DestInfo.ImmAllZerosV,
                                 SrcInfo.KRCWM:$mask)),
            (!cast<Instruction>(InstrName#"rrkz") SrcInfo.KRCWM:$mask,
                                                  SrcInfo.RC:$src)>;
}

let Predicates = [HasVLX] in {
defm : mtrunc_lowering<"VPMOVDWZ256", X86vmtrunc, v8i16x_info, v8i32x_info>;
defm : mtrunc_lowering<"VPMOVSDWZ256", X86vmtruncs, v8i16x_info, v8i32x_info>;
defm : mtrunc_lowering<"VPMOVUSDWZ256", X86vmtruncus, v8i16x_info, v8i32x_info>;
}

let Predicates = [HasAVX512] in {
defm : mtrunc_lowering<"VPMOVDWZ", X86vmtrunc, v16i16x_info, v16i32_info>;
defm : mtrunc_lowering<"VPMOVSDWZ", X86vmtruncs, v16i16x_info, v16i32_info>;
defm : mtrunc_lowering<"VPMOVUSDWZ", X86vmtruncus, v16i16x_info, v16i32_info>;

defm : mtrunc_lowering<"VPMOVDBZ", X86vmtrunc, v16i8x_info, v16i32_info>;
defm : mtrunc_lowering<"VPMOVSDBZ", X86vmtruncs, v16i8x_info, v16i32_info>;
defm : mtrunc_lowering<"VPMOVUSDBZ", X86vmtruncus, v16i8x_info, v16i32_info>;

defm : mtrunc_lowering<"VPMOVQWZ", X86vmtrunc, v8i16x_info, v8i64_info>;
defm : mtrunc_lowering<"VPMOVSQWZ", X86vmtruncs, v8i16x_info, v8i64_info>;
defm : mtrunc_lowering<"VPMOVUSQWZ", X86vmtruncus, v8i16x_info, v8i64_info>;
}

multiclass avx512_pmovx_common<bits<8> opc, string OpcodeStr, X86FoldableSchedWrite sched,
              X86VectorVTInfo DestInfo, X86VectorVTInfo SrcInfo,
              X86MemOperand x86memop, PatFrag LdFrag, SDNode OpNode>{
  let ExeDomain = DestInfo.ExeDomain in {
  defm rr   : AVX512_maskable<opc, MRMSrcReg, DestInfo, (outs DestInfo.RC:$dst),
                    (ins SrcInfo.RC:$src), OpcodeStr ,"$src", "$src",
                    (DestInfo.VT (OpNode (SrcInfo.VT SrcInfo.RC:$src)))>,
                  EVEX, Sched<[sched]>;

  defm rm : AVX512_maskable<opc, MRMSrcMem, DestInfo, (outs DestInfo.RC:$dst),
                  (ins x86memop:$src), OpcodeStr ,"$src", "$src",
                  (DestInfo.VT (LdFrag addr:$src))>,
                EVEX, Sched<[sched.Folded]>;
  }
}

multiclass avx512_pmovx_bw<bits<8> opc, string OpcodeStr,
          SDNode OpNode, SDNode InVecNode, string ExtTy,
          X86SchedWriteWidths sched,
          PatFrag LdFrag = !cast<PatFrag>(ExtTy#"extloadvi8")> {
  let Predicates = [HasVLX, HasBWI] in {
    defm Z128:  avx512_pmovx_common<opc, OpcodeStr, sched.XMM, v8i16x_info,
                    v16i8x_info, i64mem, LdFrag, InVecNode>,
                     EVEX_CD8<8, CD8VH>, T8, PD, EVEX_V128, WIG;

    defm Z256:  avx512_pmovx_common<opc, OpcodeStr, sched.YMM, v16i16x_info,
                    v16i8x_info, i128mem, LdFrag, OpNode>,
                     EVEX_CD8<8, CD8VH>, T8, PD, EVEX_V256, WIG;
  }
  let Predicates = [HasBWI] in {
    defm Z   :  avx512_pmovx_common<opc, OpcodeStr, sched.ZMM, v32i16_info,
                    v32i8x_info, i256mem, LdFrag, OpNode>,
                     EVEX_CD8<8, CD8VH>, T8, PD, EVEX_V512, WIG;
  }
}

multiclass avx512_pmovx_bd<bits<8> opc, string OpcodeStr,
          SDNode OpNode, SDNode InVecNode, string ExtTy,
          X86SchedWriteWidths sched,
          PatFrag LdFrag = !cast<PatFrag>(ExtTy#"extloadvi8")> {
  let Predicates = [HasVLX, HasAVX512] in {
    defm Z128:  avx512_pmovx_common<opc, OpcodeStr, sched.XMM, v4i32x_info,
                   v16i8x_info, i32mem, LdFrag, InVecNode>,
                         EVEX_CD8<8, CD8VQ>, T8, PD, EVEX_V128, WIG;

    defm Z256:  avx512_pmovx_common<opc, OpcodeStr, sched.YMM, v8i32x_info,
                   v16i8x_info, i64mem, LdFrag, InVecNode>,
                         EVEX_CD8<8, CD8VQ>, T8, PD, EVEX_V256, WIG;
  }
  let Predicates = [HasAVX512] in {
    defm Z   :  avx512_pmovx_common<opc, OpcodeStr, sched.ZMM, v16i32_info,
                   v16i8x_info, i128mem, LdFrag, OpNode>,
                         EVEX_CD8<8, CD8VQ>, T8, PD, EVEX_V512, WIG;
  }
}

multiclass avx512_pmovx_bq<bits<8> opc, string OpcodeStr,
                              SDNode InVecNode, string ExtTy,
                              X86SchedWriteWidths sched,
                              PatFrag LdFrag = !cast<PatFrag>(ExtTy#"extloadvi8")> {
  let Predicates = [HasVLX, HasAVX512] in {
    defm Z128:  avx512_pmovx_common<opc, OpcodeStr, sched.XMM, v2i64x_info,
                   v16i8x_info, i16mem, LdFrag, InVecNode>,
                     EVEX_CD8<8, CD8VO>, T8, PD, EVEX_V128, WIG;

    defm Z256:  avx512_pmovx_common<opc, OpcodeStr, sched.YMM, v4i64x_info,
                   v16i8x_info, i32mem, LdFrag, InVecNode>,
                     EVEX_CD8<8, CD8VO>, T8, PD, EVEX_V256, WIG;
  }
  let Predicates = [HasAVX512] in {
    defm Z   :  avx512_pmovx_common<opc, OpcodeStr, sched.ZMM, v8i64_info,
                   v16i8x_info, i64mem, LdFrag, InVecNode>,
                     EVEX_CD8<8, CD8VO>, T8, PD, EVEX_V512, WIG;
  }
}

multiclass avx512_pmovx_wd<bits<8> opc, string OpcodeStr,
         SDNode OpNode, SDNode InVecNode, string ExtTy,
         X86SchedWriteWidths sched,
         PatFrag LdFrag = !cast<PatFrag>(ExtTy#"extloadvi16")> {
  let Predicates = [HasVLX, HasAVX512] in {
    defm Z128:  avx512_pmovx_common<opc, OpcodeStr, sched.XMM, v4i32x_info,
                   v8i16x_info, i64mem, LdFrag, InVecNode>,
                     EVEX_CD8<16, CD8VH>, T8, PD, EVEX_V128, WIG;

    defm Z256:  avx512_pmovx_common<opc, OpcodeStr, sched.YMM, v8i32x_info,
                   v8i16x_info, i128mem, LdFrag, OpNode>,
                     EVEX_CD8<16, CD8VH>, T8, PD, EVEX_V256, WIG;
  }
  let Predicates = [HasAVX512] in {
    defm Z   :  avx512_pmovx_common<opc, OpcodeStr, sched.ZMM, v16i32_info,
                   v16i16x_info, i256mem, LdFrag, OpNode>,
                     EVEX_CD8<16, CD8VH>, T8, PD, EVEX_V512, WIG;
  }
}

multiclass avx512_pmovx_wq<bits<8> opc, string OpcodeStr,
         SDNode OpNode, SDNode InVecNode, string ExtTy,
         X86SchedWriteWidths sched,
         PatFrag LdFrag = !cast<PatFrag>(ExtTy#"extloadvi16")> {
  let Predicates = [HasVLX, HasAVX512] in {
    defm Z128:  avx512_pmovx_common<opc, OpcodeStr, sched.XMM, v2i64x_info,
                   v8i16x_info, i32mem, LdFrag, InVecNode>,
                     EVEX_CD8<16, CD8VQ>, T8, PD, EVEX_V128, WIG;

    defm Z256:  avx512_pmovx_common<opc, OpcodeStr, sched.YMM, v4i64x_info,
                   v8i16x_info, i64mem, LdFrag, InVecNode>,
                     EVEX_CD8<16, CD8VQ>, T8, PD, EVEX_V256, WIG;
  }
  let Predicates = [HasAVX512] in {
    defm Z   :  avx512_pmovx_common<opc, OpcodeStr, sched.ZMM, v8i64_info,
                   v8i16x_info, i128mem, LdFrag, OpNode>,
                     EVEX_CD8<16, CD8VQ>, T8, PD, EVEX_V512, WIG;
  }
}

multiclass avx512_pmovx_dq<bits<8> opc, string OpcodeStr,
         SDNode OpNode, SDNode InVecNode, string ExtTy,
         X86SchedWriteWidths sched,
         PatFrag LdFrag = !cast<PatFrag>(ExtTy#"extloadvi32")> {

  let Predicates = [HasVLX, HasAVX512] in {
    defm Z128:  avx512_pmovx_common<opc, OpcodeStr, sched.XMM, v2i64x_info,
                   v4i32x_info, i64mem, LdFrag, InVecNode>,
                     EVEX_CD8<32, CD8VH>, T8, PD, EVEX_V128;

    defm Z256:  avx512_pmovx_common<opc, OpcodeStr, sched.YMM, v4i64x_info,
                   v4i32x_info, i128mem, LdFrag, OpNode>,
                     EVEX_CD8<32, CD8VH>, T8, PD, EVEX_V256;
  }
  let Predicates = [HasAVX512] in {
    defm Z   :  avx512_pmovx_common<opc, OpcodeStr, sched.ZMM, v8i64_info,
                   v8i32x_info, i256mem, LdFrag, OpNode>,
                     EVEX_CD8<32, CD8VH>, T8, PD, EVEX_V512;
  }
}

defm VPMOVZXBW : avx512_pmovx_bw<0x30, "vpmovzxbw", zext, zext_invec, "z", SchedWriteVecExtend>;
defm VPMOVZXBD : avx512_pmovx_bd<0x31, "vpmovzxbd", zext, zext_invec, "z", SchedWriteVecExtend>;
defm VPMOVZXBQ : avx512_pmovx_bq<0x32, "vpmovzxbq",       zext_invec, "z", SchedWriteVecExtend>;
defm VPMOVZXWD : avx512_pmovx_wd<0x33, "vpmovzxwd", zext, zext_invec, "z", SchedWriteVecExtend>;
defm VPMOVZXWQ : avx512_pmovx_wq<0x34, "vpmovzxwq", zext, zext_invec, "z", SchedWriteVecExtend>;
defm VPMOVZXDQ : avx512_pmovx_dq<0x35, "vpmovzxdq", zext, zext_invec, "z", SchedWriteVecExtend>;

defm VPMOVSXBW: avx512_pmovx_bw<0x20, "vpmovsxbw", sext, sext_invec, "s", SchedWriteVecExtend>;
defm VPMOVSXBD: avx512_pmovx_bd<0x21, "vpmovsxbd", sext, sext_invec, "s", SchedWriteVecExtend>;
defm VPMOVSXBQ: avx512_pmovx_bq<0x22, "vpmovsxbq",       sext_invec, "s", SchedWriteVecExtend>;
defm VPMOVSXWD: avx512_pmovx_wd<0x23, "vpmovsxwd", sext, sext_invec, "s", SchedWriteVecExtend>;
defm VPMOVSXWQ: avx512_pmovx_wq<0x24, "vpmovsxwq", sext, sext_invec, "s", SchedWriteVecExtend>;
defm VPMOVSXDQ: avx512_pmovx_dq<0x25, "vpmovsxdq", sext, sext_invec, "s", SchedWriteVecExtend>;


// Patterns that we also need any extend versions of. aext_vector_inreg
// is currently legalized to zext_vector_inreg.
multiclass AVX512_pmovx_patterns_base<string OpcPrefix, SDNode ExtOp> {
  // 256-bit patterns
  let Predicates = [HasVLX, HasBWI] in {
    def : Pat<(v16i16 (ExtOp (loadv16i8 addr:$src))),
              (!cast<I>(OpcPrefix#BWZ256rm) addr:$src)>;
  }

  let Predicates = [HasVLX] in {
    def : Pat<(v8i32 (ExtOp (loadv8i16 addr:$src))),
              (!cast<I>(OpcPrefix#WDZ256rm) addr:$src)>;

    def : Pat<(v4i64 (ExtOp (loadv4i32 addr:$src))),
              (!cast<I>(OpcPrefix#DQZ256rm) addr:$src)>;
  }

  // 512-bit patterns
  let Predicates = [HasBWI] in {
    def : Pat<(v32i16 (ExtOp (loadv32i8 addr:$src))),
              (!cast<I>(OpcPrefix#BWZrm) addr:$src)>;
  }
  let Predicates = [HasAVX512] in {
    def : Pat<(v16i32 (ExtOp (loadv16i8 addr:$src))),
              (!cast<I>(OpcPrefix#BDZrm) addr:$src)>;
    def : Pat<(v16i32 (ExtOp (loadv16i16 addr:$src))),
              (!cast<I>(OpcPrefix#WDZrm) addr:$src)>;

    def : Pat<(v8i64 (ExtOp (loadv8i16 addr:$src))),
              (!cast<I>(OpcPrefix#WQZrm) addr:$src)>;

    def : Pat<(v8i64 (ExtOp (loadv8i32 addr:$src))),
              (!cast<I>(OpcPrefix#DQZrm) addr:$src)>;
  }
}

multiclass AVX512_pmovx_patterns<string OpcPrefix, SDNode ExtOp,
                                 SDNode InVecOp> :
    AVX512_pmovx_patterns_base<OpcPrefix, ExtOp> {
  // 128-bit patterns
  let Predicates = [HasVLX, HasBWI] in {
  def : Pat<(v8i16 (InVecOp (bc_v16i8 (v2i64 (scalar_to_vector (loadi64 addr:$src)))))),
            (!cast<I>(OpcPrefix#BWZ128rm) addr:$src)>;
  def : Pat<(v8i16 (InVecOp (bc_v16i8 (v2f64 (scalar_to_vector (loadf64 addr:$src)))))),
            (!cast<I>(OpcPrefix#BWZ128rm) addr:$src)>;
  def : Pat<(v8i16 (InVecOp (bc_v16i8 (v2i64 (X86vzload64 addr:$src))))),
            (!cast<I>(OpcPrefix#BWZ128rm) addr:$src)>;
  }
  let Predicates = [HasVLX] in {
  def : Pat<(v4i32 (InVecOp (bc_v16i8 (v4i32 (scalar_to_vector (loadi32 addr:$src)))))),
            (!cast<I>(OpcPrefix#BDZ128rm) addr:$src)>;
  def : Pat<(v4i32 (InVecOp (bc_v16i8 (v4i32 (X86vzload32 addr:$src))))),
            (!cast<I>(OpcPrefix#BDZ128rm) addr:$src)>;

  def : Pat<(v2i64 (InVecOp (bc_v16i8 (v4i32 (scalar_to_vector (extloadi32i16 addr:$src)))))),
            (!cast<I>(OpcPrefix#BQZ128rm) addr:$src)>;

  def : Pat<(v4i32 (InVecOp (bc_v8i16 (v2i64 (scalar_to_vector (loadi64 addr:$src)))))),
            (!cast<I>(OpcPrefix#WDZ128rm) addr:$src)>;
  def : Pat<(v4i32 (InVecOp (bc_v8i16 (v2f64 (scalar_to_vector (loadf64 addr:$src)))))),
            (!cast<I>(OpcPrefix#WDZ128rm) addr:$src)>;
  def : Pat<(v4i32 (InVecOp (bc_v8i16 (v2i64 (X86vzload64 addr:$src))))),
            (!cast<I>(OpcPrefix#WDZ128rm) addr:$src)>;

  def : Pat<(v2i64 (InVecOp (bc_v8i16 (v4i32 (scalar_to_vector (loadi32 addr:$src)))))),
            (!cast<I>(OpcPrefix#WQZ128rm) addr:$src)>;
  def : Pat<(v2i64 (InVecOp (bc_v8i16 (v4i32 (X86vzload32 addr:$src))))),
            (!cast<I>(OpcPrefix#WQZ128rm) addr:$src)>;

  def : Pat<(v2i64 (InVecOp (bc_v4i32 (v2i64 (scalar_to_vector (loadi64 addr:$src)))))),
            (!cast<I>(OpcPrefix#DQZ128rm) addr:$src)>;
  def : Pat<(v2i64 (InVecOp (bc_v4i32 (v2f64 (scalar_to_vector (loadf64 addr:$src)))))),
            (!cast<I>(OpcPrefix#DQZ128rm) addr:$src)>;
  def : Pat<(v2i64 (InVecOp (bc_v4i32 (v2i64 (X86vzload64 addr:$src))))),
            (!cast<I>(OpcPrefix#DQZ128rm) addr:$src)>;
  }
  let Predicates = [HasVLX] in {
  def : Pat<(v8i32 (InVecOp (bc_v16i8 (v2i64 (scalar_to_vector (loadi64 addr:$src)))))),
            (!cast<I>(OpcPrefix#BDZ256rm) addr:$src)>;
  def : Pat<(v8i32 (InVecOp (bc_v16i8 (v2i64 (scalar_to_vector (loadf64 addr:$src)))))),
            (!cast<I>(OpcPrefix#BDZ256rm) addr:$src)>;
  def : Pat<(v8i32 (InVecOp (bc_v16i8 (v2i64 (X86vzload64 addr:$src))))),
            (!cast<I>(OpcPrefix#BDZ256rm) addr:$src)>;

  def : Pat<(v4i64 (InVecOp (bc_v16i8 (v4i32 (scalar_to_vector (loadi32 addr:$src)))))),
            (!cast<I>(OpcPrefix#BQZ256rm) addr:$src)>;
  def : Pat<(v4i64 (InVecOp (bc_v16i8 (v4i32 (X86vzload32 addr:$src))))),
            (!cast<I>(OpcPrefix#BQZ256rm) addr:$src)>;

  def : Pat<(v4i64 (InVecOp (bc_v8i16 (v2i64 (scalar_to_vector (loadi64 addr:$src)))))),
            (!cast<I>(OpcPrefix#WQZ256rm) addr:$src)>;
  def : Pat<(v4i64 (InVecOp (bc_v8i16 (v2i64 (scalar_to_vector (loadf64 addr:$src)))))),
            (!cast<I>(OpcPrefix#WQZ256rm) addr:$src)>;
  def : Pat<(v4i64 (InVecOp (bc_v8i16 (v2i64 (X86vzload64 addr:$src))))),
            (!cast<I>(OpcPrefix#WQZ256rm) addr:$src)>;
  }
  // 512-bit patterns
  let Predicates = [HasAVX512] in {
  def : Pat<(v8i64 (InVecOp (bc_v16i8 (v2i64 (scalar_to_vector (loadi64 addr:$src)))))),
            (!cast<I>(OpcPrefix#BQZrm) addr:$src)>;
  def : Pat<(v8i64 (InVecOp (bc_v16i8 (v2f64 (scalar_to_vector (loadf64 addr:$src)))))),
            (!cast<I>(OpcPrefix#BQZrm) addr:$src)>;
  def : Pat<(v8i64 (InVecOp (bc_v16i8 (v2i64 (X86vzload64 addr:$src))))),
            (!cast<I>(OpcPrefix#BQZrm) addr:$src)>;
  }
}

defm : AVX512_pmovx_patterns<"VPMOVSX", sext, sext_invec>;
defm : AVX512_pmovx_patterns<"VPMOVZX", zext, zext_invec>;

// Without BWI we can't do a trunc from v16i16 to v16i8. DAG combine can merge
// ext+trunc aggressively making it impossible to legalize the DAG to this
// pattern directly.
let Predicates = [HasAVX512, NoBWI] in {
def: Pat<(v16i8 (trunc (v16i16 VR256X:$src))),
         (VPMOVDBZrr (v16i32 (VPMOVZXWDZrr VR256X:$src)))>;
def: Pat<(v16i8 (trunc (loadv16i16 addr:$src))),
         (VPMOVDBZrr (v16i32 (VPMOVZXWDZrm addr:$src)))>;
}

//===----------------------------------------------------------------------===//
// GATHER - SCATTER Operations

// FIXME: Improve scheduling of gather/scatter instructions.
multiclass avx512_gather<bits<8> opc, string OpcodeStr, X86VectorVTInfo _,
                         X86MemOperand memop, RegisterClass MaskRC = _.KRCWM> {
  let Constraints = "@earlyclobber $dst, $src1 = $dst, $mask = $mask_wb",
      ExeDomain = _.ExeDomain, mayLoad = 1, hasSideEffects = 0 in
  def rm  : AVX5128I<opc, MRMSrcMem, (outs _.RC:$dst, MaskRC:$mask_wb),
            (ins _.RC:$src1, MaskRC:$mask, memop:$src2),
            !strconcat(OpcodeStr#_.Suffix,
            "\t{$src2, ${dst} {${mask}}|${dst} {${mask}}, $src2}"),
            []>, EVEX, EVEX_K, EVEX_CD8<_.EltSize, CD8VT1>,
            Sched<[WriteLoad, WriteVecMaskedGatherWriteback]>;
}

multiclass avx512_gather_q_pd<bits<8> dopc, bits<8> qopc,
                        AVX512VLVectorVTInfo _, string OpcodeStr, string SUFF> {
  defm NAME#D#SUFF#Z: avx512_gather<dopc, OpcodeStr#"d", _.info512,
                                      vy512xmem>, EVEX_V512, REX_W;
  defm NAME#Q#SUFF#Z: avx512_gather<qopc, OpcodeStr#"q", _.info512,
                                      vz512mem>, EVEX_V512, REX_W;
let Predicates = [HasVLX] in {
  defm NAME#D#SUFF#Z256: avx512_gather<dopc, OpcodeStr#"d", _.info256,
                              vx256xmem>, EVEX_V256, REX_W;
  defm NAME#Q#SUFF#Z256: avx512_gather<qopc, OpcodeStr#"q", _.info256,
                              vy256xmem>, EVEX_V256, REX_W;
  defm NAME#D#SUFF#Z128: avx512_gather<dopc, OpcodeStr#"d", _.info128,
                              vx128xmem>, EVEX_V128, REX_W;
  defm NAME#Q#SUFF#Z128: avx512_gather<qopc, OpcodeStr#"q", _.info128,
                              vx128xmem>, EVEX_V128, REX_W;
}
}

multiclass avx512_gather_d_ps<bits<8> dopc, bits<8> qopc,
                       AVX512VLVectorVTInfo _, string OpcodeStr, string SUFF> {
  defm NAME#D#SUFF#Z: avx512_gather<dopc, OpcodeStr#"d", _.info512, vz512mem>,
                                       EVEX_V512;
  defm NAME#Q#SUFF#Z: avx512_gather<qopc, OpcodeStr#"q", _.info256, vz256mem>,
                                       EVEX_V512;
let Predicates = [HasVLX] in {
  defm NAME#D#SUFF#Z256: avx512_gather<dopc, OpcodeStr#"d", _.info256,
                                          vy256xmem>, EVEX_V256;
  defm NAME#Q#SUFF#Z256: avx512_gather<qopc, OpcodeStr#"q", _.info128,
                                          vy128xmem>, EVEX_V256;
  defm NAME#D#SUFF#Z128: avx512_gather<dopc, OpcodeStr#"d", _.info128,
                                          vx128xmem>, EVEX_V128;
  defm NAME#Q#SUFF#Z128: avx512_gather<qopc, OpcodeStr#"q", _.info128,
                                          vx64xmem, VK2WM>, EVEX_V128;
}
}


defm VGATHER : avx512_gather_q_pd<0x92, 0x93, avx512vl_f64_info, "vgather", "PD">,
               avx512_gather_d_ps<0x92, 0x93, avx512vl_f32_info, "vgather", "PS">;

defm VPGATHER : avx512_gather_q_pd<0x90, 0x91, avx512vl_i64_info, "vpgather", "Q">,
                avx512_gather_d_ps<0x90, 0x91, avx512vl_i32_info, "vpgather", "D">;

multiclass avx512_scatter<bits<8> opc, string OpcodeStr, X86VectorVTInfo _,
                          X86MemOperand memop, RegisterClass MaskRC = _.KRCWM> {

let mayStore = 1, Constraints = "$mask = $mask_wb", ExeDomain = _.ExeDomain,
    hasSideEffects = 0 in

  def mr  : AVX5128I<opc, MRMDestMem, (outs MaskRC:$mask_wb),
            (ins memop:$dst, MaskRC:$mask, _.RC:$src),
            !strconcat(OpcodeStr#_.Suffix,
            "\t{$src, ${dst} {${mask}}|${dst} {${mask}}, $src}"),
            []>, EVEX, EVEX_K, EVEX_CD8<_.EltSize, CD8VT1>,
            Sched<[WriteStore]>;
}

multiclass avx512_scatter_q_pd<bits<8> dopc, bits<8> qopc,
                        AVX512VLVectorVTInfo _, string OpcodeStr, string SUFF> {
  defm NAME#D#SUFF#Z: avx512_scatter<dopc, OpcodeStr#"d", _.info512,
                                      vy512xmem>, EVEX_V512, REX_W;
  defm NAME#Q#SUFF#Z: avx512_scatter<qopc, OpcodeStr#"q", _.info512,
                                      vz512mem>, EVEX_V512, REX_W;
let Predicates = [HasVLX] in {
  defm NAME#D#SUFF#Z256: avx512_scatter<dopc, OpcodeStr#"d", _.info256,
                              vx256xmem>, EVEX_V256, REX_W;
  defm NAME#Q#SUFF#Z256: avx512_scatter<qopc, OpcodeStr#"q", _.info256,
                              vy256xmem>, EVEX_V256, REX_W;
  defm NAME#D#SUFF#Z128: avx512_scatter<dopc, OpcodeStr#"d", _.info128,
                              vx128xmem>, EVEX_V128, REX_W;
  defm NAME#Q#SUFF#Z128: avx512_scatter<qopc, OpcodeStr#"q", _.info128,
                              vx128xmem>, EVEX_V128, REX_W;
}
}

multiclass avx512_scatter_d_ps<bits<8> dopc, bits<8> qopc,
                       AVX512VLVectorVTInfo _, string OpcodeStr, string SUFF> {
  defm NAME#D#SUFF#Z: avx512_scatter<dopc, OpcodeStr#"d", _.info512, vz512mem>,
                                       EVEX_V512;
  defm NAME#Q#SUFF#Z: avx512_scatter<qopc, OpcodeStr#"q", _.info256, vz256mem>,
                                       EVEX_V512;
let Predicates = [HasVLX] in {
  defm NAME#D#SUFF#Z256: avx512_scatter<dopc, OpcodeStr#"d", _.info256,
                                          vy256xmem>, EVEX_V256;
  defm NAME#Q#SUFF#Z256: avx512_scatter<qopc, OpcodeStr#"q", _.info128,
                                          vy128xmem>, EVEX_V256;
  defm NAME#D#SUFF#Z128: avx512_scatter<dopc, OpcodeStr#"d", _.info128,
                                          vx128xmem>, EVEX_V128;
  defm NAME#Q#SUFF#Z128: avx512_scatter<qopc, OpcodeStr#"q", _.info128,
                                          vx64xmem, VK2WM>, EVEX_V128;
}
}

defm VSCATTER : avx512_scatter_q_pd<0xA2, 0xA3, avx512vl_f64_info, "vscatter", "PD">,
               avx512_scatter_d_ps<0xA2, 0xA3, avx512vl_f32_info, "vscatter", "PS">;

defm VPSCATTER : avx512_scatter_q_pd<0xA0, 0xA1, avx512vl_i64_info, "vpscatter", "Q">,
                avx512_scatter_d_ps<0xA0, 0xA1, avx512vl_i32_info, "vpscatter", "D">;

// prefetch
multiclass avx512_gather_scatter_prefetch<bits<8> opc, Format F, string OpcodeStr,
                       RegisterClass KRC, X86MemOperand memop> {
  let mayLoad = 1, mayStore = 1 in
  def m  : AVX5128I<opc, F, (outs), (ins KRC:$mask, memop:$src),
            !strconcat(OpcodeStr, "\t{$src {${mask}}|{${mask}}, $src}"), []>,
            EVEX, EVEX_K, Sched<[WriteLoad]>;
}

defm VGATHERPF0DPS: avx512_gather_scatter_prefetch<0xC6, MRM1m, "vgatherpf0dps",
                     VK16WM, vz512mem>, EVEX_V512, EVEX_CD8<32, CD8VT1>;

defm VGATHERPF0QPS: avx512_gather_scatter_prefetch<0xC7, MRM1m, "vgatherpf0qps",
                     VK8WM, vz256mem>, EVEX_V512, EVEX_CD8<64, CD8VT1>;

defm VGATHERPF0DPD: avx512_gather_scatter_prefetch<0xC6, MRM1m, "vgatherpf0dpd",
                     VK8WM, vy512xmem>, EVEX_V512, REX_W, EVEX_CD8<32, CD8VT1>;

defm VGATHERPF0QPD: avx512_gather_scatter_prefetch<0xC7, MRM1m, "vgatherpf0qpd",
                     VK8WM, vz512mem>, EVEX_V512, REX_W, EVEX_CD8<64, CD8VT1>;

defm VGATHERPF1DPS: avx512_gather_scatter_prefetch<0xC6, MRM2m, "vgatherpf1dps",
                     VK16WM, vz512mem>, EVEX_V512, EVEX_CD8<32, CD8VT1>;

defm VGATHERPF1QPS: avx512_gather_scatter_prefetch<0xC7, MRM2m, "vgatherpf1qps",
                     VK8WM, vz256mem>, EVEX_V512, EVEX_CD8<64, CD8VT1>;

defm VGATHERPF1DPD: avx512_gather_scatter_prefetch<0xC6, MRM2m, "vgatherpf1dpd",
                     VK8WM, vy512xmem>, EVEX_V512, REX_W, EVEX_CD8<32, CD8VT1>;

defm VGATHERPF1QPD: avx512_gather_scatter_prefetch<0xC7, MRM2m, "vgatherpf1qpd",
                     VK8WM, vz512mem>, EVEX_V512, REX_W, EVEX_CD8<64, CD8VT1>;

defm VSCATTERPF0DPS: avx512_gather_scatter_prefetch<0xC6, MRM5m, "vscatterpf0dps",
                     VK16WM, vz512mem>, EVEX_V512, EVEX_CD8<32, CD8VT1>;

defm VSCATTERPF0QPS: avx512_gather_scatter_prefetch<0xC7, MRM5m, "vscatterpf0qps",
                     VK8WM, vz256mem>, EVEX_V512, EVEX_CD8<64, CD8VT1>;

defm VSCATTERPF0DPD: avx512_gather_scatter_prefetch<0xC6, MRM5m, "vscatterpf0dpd",
                     VK8WM, vy512xmem>, EVEX_V512, REX_W, EVEX_CD8<32, CD8VT1>;

defm VSCATTERPF0QPD: avx512_gather_scatter_prefetch<0xC7, MRM5m, "vscatterpf0qpd",
                     VK8WM, vz512mem>, EVEX_V512, REX_W, EVEX_CD8<64, CD8VT1>;

defm VSCATTERPF1DPS: avx512_gather_scatter_prefetch<0xC6, MRM6m, "vscatterpf1dps",
                     VK16WM, vz512mem>, EVEX_V512, EVEX_CD8<32, CD8VT1>;

defm VSCATTERPF1QPS: avx512_gather_scatter_prefetch<0xC7, MRM6m, "vscatterpf1qps",
                     VK8WM, vz256mem>, EVEX_V512, EVEX_CD8<64, CD8VT1>;

defm VSCATTERPF1DPD: avx512_gather_scatter_prefetch<0xC6, MRM6m, "vscatterpf1dpd",
                     VK8WM, vy512xmem>, EVEX_V512, REX_W, EVEX_CD8<32, CD8VT1>;

defm VSCATTERPF1QPD: avx512_gather_scatter_prefetch<0xC7, MRM6m, "vscatterpf1qpd",
                     VK8WM, vz512mem>, EVEX_V512, REX_W, EVEX_CD8<64, CD8VT1>;

multiclass cvt_by_vec_width<bits<8> opc, X86VectorVTInfo Vec, string OpcodeStr, SchedWrite Sched> {
def rr : AVX512XS8I<opc, MRMSrcReg, (outs Vec.RC:$dst), (ins Vec.KRC:$src),
                  !strconcat(OpcodeStr#Vec.Suffix, "\t{$src, $dst|$dst, $src}"),
                  [(set Vec.RC:$dst, (Vec.VT (sext Vec.KRC:$src)))]>,
                  EVEX, Sched<[Sched]>;
}

multiclass cvt_mask_by_elt_width<bits<8> opc, AVX512VLVectorVTInfo VTInfo,
                                 string OpcodeStr, Predicate prd> {
let Predicates = [prd] in
  defm Z : cvt_by_vec_width<opc, VTInfo.info512, OpcodeStr, WriteVecMoveZ>, EVEX_V512;

  let Predicates = [prd, HasVLX] in {
    defm Z256 : cvt_by_vec_width<opc, VTInfo.info256, OpcodeStr, WriteVecMoveY>, EVEX_V256;
    defm Z128 : cvt_by_vec_width<opc, VTInfo.info128, OpcodeStr, WriteVecMoveX>, EVEX_V128;
  }
}

defm VPMOVM2B : cvt_mask_by_elt_width<0x28, avx512vl_i8_info, "vpmovm2" , HasBWI>;
defm VPMOVM2W : cvt_mask_by_elt_width<0x28, avx512vl_i16_info, "vpmovm2", HasBWI> , REX_W;
defm VPMOVM2D : cvt_mask_by_elt_width<0x38, avx512vl_i32_info, "vpmovm2", HasDQI>;
defm VPMOVM2Q : cvt_mask_by_elt_width<0x38, avx512vl_i64_info, "vpmovm2", HasDQI> , REX_W;

multiclass convert_vector_to_mask_common<bits<8> opc, X86VectorVTInfo _, string OpcodeStr > {
    def rr : AVX512XS8I<opc, MRMSrcReg, (outs _.KRC:$dst), (ins _.RC:$src),
                        !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                        [(set _.KRC:$dst, (X86pcmpgtm _.ImmAllZerosV, (_.VT _.RC:$src)))]>,
                        EVEX, Sched<[WriteMove]>;
}

// Use 512bit version to implement 128/256 bit in case NoVLX.
multiclass convert_vector_to_mask_lowering<X86VectorVTInfo ExtendInfo,
                                           X86VectorVTInfo _,
                                           string Name> {

  def : Pat<(_.KVT (X86pcmpgtm _.ImmAllZerosV, (_.VT _.RC:$src))),
            (_.KVT (COPY_TO_REGCLASS
                     (!cast<Instruction>(Name#"Zrr")
                       (INSERT_SUBREG (ExtendInfo.VT (IMPLICIT_DEF)),
                                      _.RC:$src, _.SubRegIdx)),
                   _.KRC))>;
}

multiclass avx512_convert_vector_to_mask<bits<8> opc, string OpcodeStr,
                                   AVX512VLVectorVTInfo VTInfo, Predicate prd> {
  let Predicates = [prd] in
    defm Z : convert_vector_to_mask_common <opc, VTInfo.info512, OpcodeStr>,
                                            EVEX_V512;

  let Predicates = [prd, HasVLX] in {
    defm Z256 : convert_vector_to_mask_common<opc, VTInfo.info256, OpcodeStr>,
                                              EVEX_V256;
    defm Z128 : convert_vector_to_mask_common<opc, VTInfo.info128, OpcodeStr>,
                                               EVEX_V128;
  }
  let Predicates = [prd, NoVLX, HasEVEX512] in {
    defm Z256_Alt : convert_vector_to_mask_lowering<VTInfo.info512, VTInfo.info256, NAME>;
    defm Z128_Alt : convert_vector_to_mask_lowering<VTInfo.info512, VTInfo.info128, NAME>;
  }
}

defm VPMOVB2M : avx512_convert_vector_to_mask<0x29, "vpmovb2m",
                                              avx512vl_i8_info, HasBWI>;
defm VPMOVW2M : avx512_convert_vector_to_mask<0x29, "vpmovw2m",
                                              avx512vl_i16_info, HasBWI>, REX_W;
defm VPMOVD2M : avx512_convert_vector_to_mask<0x39, "vpmovd2m",
                                              avx512vl_i32_info, HasDQI>;
defm VPMOVQ2M : avx512_convert_vector_to_mask<0x39, "vpmovq2m",
                                              avx512vl_i64_info, HasDQI>, REX_W;

// Patterns for handling sext from a mask register to v16i8/v16i16 when DQI
// is available, but BWI is not. We can't handle this in lowering because
// a target independent DAG combine likes to combine sext and trunc.
let Predicates = [HasDQI, NoBWI] in {
  def : Pat<(v16i8 (sext (v16i1 VK16:$src))),
            (VPMOVDBZrr (v16i32 (VPMOVM2DZrr VK16:$src)))>;
  def : Pat<(v16i16 (sext (v16i1 VK16:$src))),
            (VPMOVDWZrr (v16i32 (VPMOVM2DZrr VK16:$src)))>;
}

let Predicates = [HasDQI, NoBWI, HasVLX] in {
  def : Pat<(v8i16 (sext (v8i1 VK8:$src))),
            (VPMOVDWZ256rr (v8i32 (VPMOVM2DZ256rr VK8:$src)))>;
}

//===----------------------------------------------------------------------===//
// AVX-512 - COMPRESS and EXPAND
//

multiclass compress_by_vec_width_common<bits<8> opc, X86VectorVTInfo _,
                                 string OpcodeStr, X86FoldableSchedWrite sched> {
  defm rr : AVX512_maskable<opc, MRMDestReg, _, (outs _.RC:$dst),
              (ins _.RC:$src1), OpcodeStr, "$src1", "$src1",
              (null_frag)>, AVX5128IBase,
              Sched<[sched]>;

  let mayStore = 1, hasSideEffects = 0 in
  def mr : AVX5128I<opc, MRMDestMem, (outs),
              (ins _.MemOp:$dst, _.RC:$src),
              OpcodeStr # "\t{$src, $dst|$dst, $src}",
              []>, EVEX_CD8<_.EltSize, CD8VT1>,
              Sched<[sched.Folded]>;

  def mrk : AVX5128I<opc, MRMDestMem, (outs),
              (ins _.MemOp:$dst, _.KRCWM:$mask, _.RC:$src),
              OpcodeStr # "\t{$src, $dst {${mask}}|$dst {${mask}}, $src}",
              []>,
              EVEX_K, EVEX_CD8<_.EltSize, CD8VT1>,
              Sched<[sched.Folded]>;
}

multiclass compress_by_vec_width_lowering<X86VectorVTInfo _, string Name> {
  def : Pat<(X86mCompressingStore (_.VT _.RC:$src), addr:$dst, _.KRCWM:$mask),
            (!cast<Instruction>(Name#_.ZSuffix#mrk)
                            addr:$dst, _.KRCWM:$mask, _.RC:$src)>;

  def : Pat<(X86compress (_.VT _.RC:$src), _.RC:$src0, _.KRCWM:$mask),
            (!cast<Instruction>(Name#_.ZSuffix#rrk)
                            _.RC:$src0, _.KRCWM:$mask, _.RC:$src)>;
  def : Pat<(X86compress (_.VT _.RC:$src), _.ImmAllZerosV, _.KRCWM:$mask),
            (!cast<Instruction>(Name#_.ZSuffix#rrkz)
                            _.KRCWM:$mask, _.RC:$src)>;
}

multiclass compress_by_elt_width<bits<8> opc, string OpcodeStr,
                                 X86FoldableSchedWrite sched,
                                 AVX512VLVectorVTInfo VTInfo,
                                 Predicate Pred = HasAVX512> {
  let Predicates = [Pred] in
  defm Z : compress_by_vec_width_common<opc, VTInfo.info512, OpcodeStr, sched>,
           compress_by_vec_width_lowering<VTInfo.info512, NAME>, EVEX_V512;

  let Predicates = [Pred, HasVLX] in {
    defm Z256 : compress_by_vec_width_common<opc, VTInfo.info256, OpcodeStr, sched>,
                compress_by_vec_width_lowering<VTInfo.info256, NAME>, EVEX_V256;
    defm Z128 : compress_by_vec_width_common<opc, VTInfo.info128, OpcodeStr, sched>,
                compress_by_vec_width_lowering<VTInfo.info128, NAME>, EVEX_V128;
  }
}

// FIXME: Is there a better scheduler class for VPCOMPRESS?
defm VPCOMPRESSD : compress_by_elt_width <0x8B, "vpcompressd", WriteVarShuffle256,
                                          avx512vl_i32_info>, EVEX;
defm VPCOMPRESSQ : compress_by_elt_width <0x8B, "vpcompressq", WriteVarShuffle256,
                                          avx512vl_i64_info>, EVEX, REX_W;
defm VCOMPRESSPS : compress_by_elt_width <0x8A, "vcompressps", WriteVarShuffle256,
                                          avx512vl_f32_info>, EVEX;
defm VCOMPRESSPD : compress_by_elt_width <0x8A, "vcompresspd", WriteVarShuffle256,
                                          avx512vl_f64_info>, EVEX, REX_W;

// expand
multiclass expand_by_vec_width<bits<8> opc, X86VectorVTInfo _,
                                 string OpcodeStr, X86FoldableSchedWrite sched> {
  defm rr : AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
              (ins _.RC:$src1), OpcodeStr, "$src1", "$src1",
              (null_frag)>, AVX5128IBase,
              Sched<[sched]>;

  defm rm : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
              (ins _.MemOp:$src1), OpcodeStr, "$src1", "$src1",
              (null_frag)>,
            AVX5128IBase, EVEX_CD8<_.EltSize, CD8VT1>,
            Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass expand_by_vec_width_lowering<X86VectorVTInfo _, string Name> {

  def : Pat<(_.VT (X86mExpandingLoad addr:$src, _.KRCWM:$mask, undef)),
            (!cast<Instruction>(Name#_.ZSuffix#rmkz)
                                        _.KRCWM:$mask, addr:$src)>;

  def : Pat<(_.VT (X86mExpandingLoad addr:$src, _.KRCWM:$mask, _.ImmAllZerosV)),
            (!cast<Instruction>(Name#_.ZSuffix#rmkz)
                                        _.KRCWM:$mask, addr:$src)>;

  def : Pat<(_.VT (X86mExpandingLoad addr:$src, _.KRCWM:$mask,
                                               (_.VT _.RC:$src0))),
            (!cast<Instruction>(Name#_.ZSuffix#rmk)
                            _.RC:$src0, _.KRCWM:$mask, addr:$src)>;

  def : Pat<(X86expand (_.VT _.RC:$src), _.RC:$src0, _.KRCWM:$mask),
            (!cast<Instruction>(Name#_.ZSuffix#rrk)
                            _.RC:$src0, _.KRCWM:$mask, _.RC:$src)>;
  def : Pat<(X86expand (_.VT _.RC:$src), _.ImmAllZerosV, _.KRCWM:$mask),
            (!cast<Instruction>(Name#_.ZSuffix#rrkz)
                            _.KRCWM:$mask, _.RC:$src)>;
}

multiclass expand_by_elt_width<bits<8> opc, string OpcodeStr,
                               X86FoldableSchedWrite sched,
                               AVX512VLVectorVTInfo VTInfo,
                               Predicate Pred = HasAVX512> {
  let Predicates = [Pred] in
  defm Z : expand_by_vec_width<opc, VTInfo.info512, OpcodeStr, sched>,
           expand_by_vec_width_lowering<VTInfo.info512, NAME>, EVEX_V512;

  let Predicates = [Pred, HasVLX] in {
    defm Z256 : expand_by_vec_width<opc, VTInfo.info256, OpcodeStr, sched>,
                expand_by_vec_width_lowering<VTInfo.info256, NAME>, EVEX_V256;
    defm Z128 : expand_by_vec_width<opc, VTInfo.info128, OpcodeStr, sched>,
                expand_by_vec_width_lowering<VTInfo.info128, NAME>, EVEX_V128;
  }
}

// FIXME: Is there a better scheduler class for VPEXPAND?
defm VPEXPANDD : expand_by_elt_width <0x89, "vpexpandd", WriteVarShuffle256,
                                      avx512vl_i32_info>, EVEX;
defm VPEXPANDQ : expand_by_elt_width <0x89, "vpexpandq", WriteVarShuffle256,
                                      avx512vl_i64_info>, EVEX, REX_W;
defm VEXPANDPS : expand_by_elt_width <0x88, "vexpandps", WriteVarShuffle256,
                                      avx512vl_f32_info>, EVEX;
defm VEXPANDPD : expand_by_elt_width <0x88, "vexpandpd", WriteVarShuffle256,
                                      avx512vl_f64_info>, EVEX, REX_W;

//handle instruction  reg_vec1 = op(reg_vec,imm)
//                               op(mem_vec,imm)
//                               op(broadcast(eltVt),imm)
//all instruction created with FROUND_CURRENT
multiclass avx512_unary_fp_packed_imm<bits<8> opc, string OpcodeStr,
                                      SDPatternOperator OpNode,
                                      SDPatternOperator MaskOpNode,
                                      X86FoldableSchedWrite sched,
                                      X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain, Uses = [MXCSR], mayRaiseFPException = 1 in {
  defm rri : AVX512_maskable_split<opc, MRMSrcReg, _, (outs _.RC:$dst),
                      (ins _.RC:$src1, i32u8imm:$src2),
                      OpcodeStr#_.Suffix, "$src2, $src1", "$src1, $src2",
                      (OpNode (_.VT _.RC:$src1), (i32 timm:$src2)),
                      (MaskOpNode (_.VT _.RC:$src1), (i32 timm:$src2))>,
                      Sched<[sched]>;
  defm rmi : AVX512_maskable_split<opc, MRMSrcMem, _, (outs _.RC:$dst),
                    (ins _.MemOp:$src1, i32u8imm:$src2),
                    OpcodeStr#_.Suffix, "$src2, $src1", "$src1, $src2",
                    (OpNode (_.VT (bitconvert (_.LdFrag addr:$src1))),
                            (i32 timm:$src2)),
                    (MaskOpNode (_.VT (bitconvert (_.LdFrag addr:$src1))),
                                (i32 timm:$src2))>,
                    Sched<[sched.Folded, sched.ReadAfterFold]>;
  defm rmbi : AVX512_maskable_split<opc, MRMSrcMem, _, (outs _.RC:$dst),
                    (ins _.ScalarMemOp:$src1, i32u8imm:$src2),
                    OpcodeStr#_.Suffix, "$src2, ${src1}"#_.BroadcastStr,
                    "${src1}"#_.BroadcastStr#", $src2",
                    (OpNode (_.VT (_.BroadcastLdFrag addr:$src1)),
                            (i32 timm:$src2)),
                    (MaskOpNode (_.VT (_.BroadcastLdFrag addr:$src1)),
                                (i32 timm:$src2))>, EVEX_B,
                    Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

//handle instruction  reg_vec1 = op(reg_vec2,reg_vec3,imm),{sae}
multiclass avx512_unary_fp_sae_packed_imm<bits<8> opc, string OpcodeStr,
                                          SDNode OpNode, X86FoldableSchedWrite sched,
                                          X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain, Uses = [MXCSR] in
  defm rrib : AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                      (ins _.RC:$src1, i32u8imm:$src2),
                      OpcodeStr#_.Suffix, "$src2, {sae}, $src1",
                      "$src1, {sae}, $src2",
                      (OpNode (_.VT _.RC:$src1),
                              (i32 timm:$src2))>,
                      EVEX_B, Sched<[sched]>;
}

multiclass avx512_common_unary_fp_sae_packed_imm<string OpcodeStr,
            AVX512VLVectorVTInfo _, bits<8> opc, SDPatternOperator OpNode,
            SDPatternOperator MaskOpNode, SDNode OpNodeSAE, X86SchedWriteWidths sched,
            Predicate prd>{
  let Predicates = [prd] in {
    defm Z    : avx512_unary_fp_packed_imm<opc, OpcodeStr, OpNode, MaskOpNode,
                                           sched.ZMM, _.info512>,
                avx512_unary_fp_sae_packed_imm<opc, OpcodeStr, OpNodeSAE,
                                               sched.ZMM, _.info512>, EVEX_V512;
  }
  let Predicates = [prd, HasVLX] in {
    defm Z128 : avx512_unary_fp_packed_imm<opc, OpcodeStr, OpNode, MaskOpNode,
                                           sched.XMM, _.info128>, EVEX_V128;
    defm Z256 : avx512_unary_fp_packed_imm<opc, OpcodeStr, OpNode, MaskOpNode,
                                           sched.YMM, _.info256>, EVEX_V256;
  }
}

//handle instruction  reg_vec1 = op(reg_vec2,reg_vec3,imm)
//                               op(reg_vec2,mem_vec,imm)
//                               op(reg_vec2,broadcast(eltVt),imm)
//all instruction created with FROUND_CURRENT
multiclass avx512_fp_packed_imm<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                X86FoldableSchedWrite sched, X86VectorVTInfo _>{
  let ExeDomain = _.ExeDomain, Uses = [MXCSR], mayRaiseFPException = 1 in {
  defm rri : AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                      (ins _.RC:$src1, _.RC:$src2, i32u8imm:$src3),
                      OpcodeStr, "$src3, $src2, $src1", "$src1, $src2, $src3",
                      (OpNode (_.VT _.RC:$src1),
                              (_.VT _.RC:$src2),
                              (i32 timm:$src3))>,
                      Sched<[sched]>;
  defm rmi : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                    (ins _.RC:$src1, _.MemOp:$src2, i32u8imm:$src3),
                    OpcodeStr, "$src3, $src2, $src1", "$src1, $src2, $src3",
                    (OpNode (_.VT _.RC:$src1),
                            (_.VT (bitconvert (_.LdFrag addr:$src2))),
                            (i32 timm:$src3))>,
                    Sched<[sched.Folded, sched.ReadAfterFold]>;
  defm rmbi : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                    (ins _.RC:$src1, _.ScalarMemOp:$src2, i32u8imm:$src3),
                    OpcodeStr, "$src3, ${src2}"#_.BroadcastStr#", $src1",
                    "$src1, ${src2}"#_.BroadcastStr#", $src3",
                    (OpNode (_.VT _.RC:$src1),
                            (_.VT (_.BroadcastLdFrag addr:$src2)),
                            (i32 timm:$src3))>, EVEX_B,
                    Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

//handle instruction  reg_vec1 = op(reg_vec2,reg_vec3,imm)
//                               op(reg_vec2,mem_vec,imm)
multiclass avx512_3Op_rm_imm8<bits<8> opc, string OpcodeStr, SDNode OpNode,
                              X86FoldableSchedWrite sched, X86VectorVTInfo DestInfo,
                              X86VectorVTInfo SrcInfo>{
  let ExeDomain = DestInfo.ExeDomain, ImmT = Imm8 in {
  defm rri : AVX512_maskable<opc, MRMSrcReg, DestInfo, (outs DestInfo.RC:$dst),
                  (ins SrcInfo.RC:$src1, SrcInfo.RC:$src2, u8imm:$src3),
                  OpcodeStr, "$src3, $src2, $src1", "$src1, $src2, $src3",
                  (DestInfo.VT (OpNode (SrcInfo.VT SrcInfo.RC:$src1),
                               (SrcInfo.VT SrcInfo.RC:$src2),
                               (i8 timm:$src3)))>,
                  Sched<[sched]>;
  defm rmi : AVX512_maskable<opc, MRMSrcMem, DestInfo, (outs DestInfo.RC:$dst),
                (ins SrcInfo.RC:$src1, SrcInfo.MemOp:$src2, u8imm:$src3),
                OpcodeStr, "$src3, $src2, $src1", "$src1, $src2, $src3",
                (DestInfo.VT (OpNode (SrcInfo.VT SrcInfo.RC:$src1),
                             (SrcInfo.VT (bitconvert
                                                (SrcInfo.LdFrag addr:$src2))),
                             (i8 timm:$src3)))>,
                Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

//handle instruction  reg_vec1 = op(reg_vec2,reg_vec3,imm)
//                               op(reg_vec2,mem_vec,imm)
//                               op(reg_vec2,broadcast(eltVt),imm)
multiclass avx512_3Op_imm8<bits<8> opc, string OpcodeStr, SDNode OpNode,
                           X86FoldableSchedWrite sched, X86VectorVTInfo _>:
  avx512_3Op_rm_imm8<opc, OpcodeStr, OpNode, sched, _, _>{

  let ExeDomain = _.ExeDomain, ImmT = Imm8 in
  defm rmbi : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                    (ins _.RC:$src1, _.ScalarMemOp:$src2, u8imm:$src3),
                    OpcodeStr, "$src3, ${src2}"#_.BroadcastStr#", $src1",
                    "$src1, ${src2}"#_.BroadcastStr#", $src3",
                    (OpNode (_.VT _.RC:$src1),
                            (_.VT (_.BroadcastLdFrag addr:$src2)),
                            (i8 timm:$src3))>, EVEX_B,
                    Sched<[sched.Folded, sched.ReadAfterFold]>;
}

//handle scalar instruction  reg_vec1 = op(reg_vec2,reg_vec3,imm)
//                                      op(reg_vec2,mem_scalar,imm)
multiclass avx512_fp_scalar_imm<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain, Uses = [MXCSR], mayRaiseFPException = 1 in {
  defm rri : AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                      (ins _.RC:$src1, _.RC:$src2, i32u8imm:$src3),
                      OpcodeStr, "$src3, $src2, $src1", "$src1, $src2, $src3",
                      (OpNode (_.VT _.RC:$src1),
                              (_.VT _.RC:$src2),
                              (i32 timm:$src3))>,
                      Sched<[sched]>;
  defm rmi : AVX512_maskable_scalar<opc, MRMSrcMem, _, (outs _.RC:$dst),
                    (ins _.RC:$src1, _.IntScalarMemOp:$src2, i32u8imm:$src3),
                    OpcodeStr, "$src3, $src2, $src1", "$src1, $src2, $src3",
                    (OpNode (_.VT _.RC:$src1),
                            (_.ScalarIntMemFrags addr:$src2),
                            (i32 timm:$src3))>,
                    Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

//handle instruction  reg_vec1 = op(reg_vec2,reg_vec3,imm),{sae}
multiclass avx512_fp_sae_packed_imm<bits<8> opc, string OpcodeStr,
                                    SDNode OpNode, X86FoldableSchedWrite sched,
                                    X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain, Uses = [MXCSR] in
  defm rrib : AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                      (ins _.RC:$src1, _.RC:$src2, i32u8imm:$src3),
                      OpcodeStr, "$src3, {sae}, $src2, $src1",
                      "$src1, $src2, {sae}, $src3",
                      (OpNode (_.VT _.RC:$src1),
                              (_.VT _.RC:$src2),
                              (i32 timm:$src3))>,
                      EVEX_B, Sched<[sched]>;
}

//handle scalar instruction  reg_vec1 = op(reg_vec2,reg_vec3,imm),{sae}
multiclass avx512_fp_sae_scalar_imm<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                    X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain, Uses = [MXCSR] in
  defm NAME#rrib : AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                      (ins _.RC:$src1, _.RC:$src2, i32u8imm:$src3),
                      OpcodeStr, "$src3, {sae}, $src2, $src1",
                      "$src1, $src2, {sae}, $src3",
                      (OpNode (_.VT _.RC:$src1),
                              (_.VT _.RC:$src2),
                              (i32 timm:$src3))>,
                      EVEX_B, Sched<[sched]>;
}

multiclass avx512_common_fp_sae_packed_imm<string OpcodeStr,
            AVX512VLVectorVTInfo _, bits<8> opc, SDNode OpNode,
            SDNode OpNodeSAE, X86SchedWriteWidths sched, Predicate prd>{
  let Predicates = [prd] in {
    defm Z    : avx512_fp_packed_imm<opc, OpcodeStr, OpNode, sched.ZMM, _.info512>,
                avx512_fp_sae_packed_imm<opc, OpcodeStr, OpNodeSAE, sched.ZMM, _.info512>,
                                  EVEX_V512;

  }
  let Predicates = [prd, HasVLX] in {
    defm Z128 : avx512_fp_packed_imm<opc, OpcodeStr, OpNode, sched.XMM, _.info128>,
                                  EVEX_V128;
    defm Z256 : avx512_fp_packed_imm<opc, OpcodeStr, OpNode, sched.YMM, _.info256>,
                                  EVEX_V256;
  }
}

multiclass avx512_common_3Op_rm_imm8<bits<8> opc, SDNode OpNode, string OpStr,
                   X86SchedWriteWidths sched, AVX512VLVectorVTInfo DestInfo,
                   AVX512VLVectorVTInfo SrcInfo, Predicate Pred = HasBWI> {
  let Predicates = [Pred] in {
    defm Z    : avx512_3Op_rm_imm8<opc, OpStr, OpNode, sched.ZMM, DestInfo.info512,
                           SrcInfo.info512>, EVEX_V512, AVX512AIi8Base, EVEX, VVVV;
  }
  let Predicates = [Pred, HasVLX] in {
    defm Z128 : avx512_3Op_rm_imm8<opc, OpStr, OpNode, sched.XMM, DestInfo.info128,
                           SrcInfo.info128>, EVEX_V128, AVX512AIi8Base, EVEX, VVVV;
    defm Z256 : avx512_3Op_rm_imm8<opc, OpStr, OpNode, sched.YMM, DestInfo.info256,
                           SrcInfo.info256>, EVEX_V256, AVX512AIi8Base, EVEX, VVVV;
  }
}

multiclass avx512_common_3Op_imm8<string OpcodeStr, AVX512VLVectorVTInfo _,
                                  bits<8> opc, SDNode OpNode, X86SchedWriteWidths sched,
                                  Predicate Pred = HasAVX512> {
  let Predicates = [Pred] in {
    defm Z    : avx512_3Op_imm8<opc, OpcodeStr, OpNode, sched.ZMM, _.info512>,
                                EVEX_V512;
  }
  let Predicates = [Pred, HasVLX] in {
    defm Z128 : avx512_3Op_imm8<opc, OpcodeStr, OpNode, sched.XMM, _.info128>,
                                EVEX_V128;
    defm Z256 : avx512_3Op_imm8<opc, OpcodeStr, OpNode, sched.YMM, _.info256>,
                                EVEX_V256;
  }
}

multiclass avx512_common_fp_sae_scalar_imm<string OpcodeStr,
                  X86VectorVTInfo _, bits<8> opc, SDNode OpNode,
                  SDNode OpNodeSAE, X86SchedWriteWidths sched, Predicate prd> {
  let Predicates = [prd] in {
     defm Z : avx512_fp_scalar_imm<opc, OpcodeStr, OpNode, sched.XMM, _>,
              avx512_fp_sae_scalar_imm<opc, OpcodeStr, OpNodeSAE, sched.XMM, _>;
  }
}

multiclass avx512_common_unary_fp_sae_packed_imm_all<string OpcodeStr,
                    bits<8> opcPs, bits<8> opcPd, SDPatternOperator OpNode,
                    SDPatternOperator MaskOpNode, SDNode OpNodeSAE,
                    X86SchedWriteWidths sched, Predicate prd>{
  defm PH : avx512_common_unary_fp_sae_packed_imm<OpcodeStr, avx512vl_f16_info,
                            opcPs, OpNode, MaskOpNode, OpNodeSAE, sched, HasFP16>,
                            AVX512PSIi8Base, TA, EVEX, EVEX_CD8<16, CD8VF>;
  defm PS : avx512_common_unary_fp_sae_packed_imm<OpcodeStr, avx512vl_f32_info,
                            opcPs, OpNode, MaskOpNode, OpNodeSAE, sched, prd>,
                            AVX512AIi8Base, EVEX, EVEX_CD8<32, CD8VF>;
  defm PD : avx512_common_unary_fp_sae_packed_imm<OpcodeStr, avx512vl_f64_info,
                            opcPd, OpNode, MaskOpNode, OpNodeSAE, sched, prd>,
                            AVX512AIi8Base, EVEX, EVEX_CD8<64, CD8VF>, REX_W;
}

defm VREDUCE   : avx512_common_unary_fp_sae_packed_imm_all<"vreduce", 0x56, 0x56,
                              X86VReduce, X86VReduce, X86VReduceSAE,
                              SchedWriteFRnd, HasDQI>;
defm VRNDSCALE : avx512_common_unary_fp_sae_packed_imm_all<"vrndscale", 0x08, 0x09,
                              X86any_VRndScale, X86VRndScale, X86VRndScaleSAE,
                              SchedWriteFRnd, HasAVX512>;
defm VGETMANT : avx512_common_unary_fp_sae_packed_imm_all<"vgetmant", 0x26, 0x26,
                              X86VGetMant, X86VGetMant, X86VGetMantSAE,
                              SchedWriteFRnd, HasAVX512>;

defm VRANGEPD : avx512_common_fp_sae_packed_imm<"vrangepd", avx512vl_f64_info,
                                                0x50, X86VRange, X86VRangeSAE,
                                                SchedWriteFAdd, HasDQI>,
      AVX512AIi8Base, EVEX, VVVV, EVEX_CD8<64, CD8VF>, REX_W;
defm VRANGEPS : avx512_common_fp_sae_packed_imm<"vrangeps", avx512vl_f32_info,
                                                0x50, X86VRange, X86VRangeSAE,
                                                SchedWriteFAdd, HasDQI>,
      AVX512AIi8Base, EVEX, VVVV, EVEX_CD8<32, CD8VF>;

defm VRANGESD: avx512_common_fp_sae_scalar_imm<"vrangesd",
      f64x_info, 0x51, X86Ranges, X86RangesSAE, SchedWriteFAdd, HasDQI>,
      AVX512AIi8Base, VEX_LIG, EVEX, VVVV, EVEX_CD8<64, CD8VT1>, REX_W;
defm VRANGESS: avx512_common_fp_sae_scalar_imm<"vrangess", f32x_info,
      0x51, X86Ranges, X86RangesSAE, SchedWriteFAdd, HasDQI>,
      AVX512AIi8Base, VEX_LIG, EVEX, VVVV, EVEX_CD8<32, CD8VT1>;

defm VREDUCESD: avx512_common_fp_sae_scalar_imm<"vreducesd", f64x_info,
      0x57, X86Reduces, X86ReducesSAE, SchedWriteFRnd, HasDQI>,
      AVX512AIi8Base, VEX_LIG, EVEX, VVVV, EVEX_CD8<64, CD8VT1>, REX_W;
defm VREDUCESS: avx512_common_fp_sae_scalar_imm<"vreducess", f32x_info,
      0x57, X86Reduces, X86ReducesSAE, SchedWriteFRnd, HasDQI>,
      AVX512AIi8Base, VEX_LIG, EVEX, VVVV, EVEX_CD8<32, CD8VT1>;
defm VREDUCESH: avx512_common_fp_sae_scalar_imm<"vreducesh", f16x_info,
      0x57, X86Reduces, X86ReducesSAE, SchedWriteFRnd, HasFP16>,
      AVX512PSIi8Base, TA, VEX_LIG, EVEX, VVVV, EVEX_CD8<16, CD8VT1>;

defm VGETMANTSD: avx512_common_fp_sae_scalar_imm<"vgetmantsd", f64x_info,
      0x27, X86GetMants, X86GetMantsSAE, SchedWriteFRnd, HasAVX512>,
      AVX512AIi8Base, VEX_LIG, EVEX, VVVV, EVEX_CD8<64, CD8VT1>, REX_W;
defm VGETMANTSS: avx512_common_fp_sae_scalar_imm<"vgetmantss", f32x_info,
      0x27, X86GetMants, X86GetMantsSAE, SchedWriteFRnd, HasAVX512>,
      AVX512AIi8Base, VEX_LIG, EVEX, VVVV, EVEX_CD8<32, CD8VT1>;
defm VGETMANTSH: avx512_common_fp_sae_scalar_imm<"vgetmantsh", f16x_info,
      0x27, X86GetMants, X86GetMantsSAE, SchedWriteFRnd, HasFP16>,
      AVX512PSIi8Base, TA, VEX_LIG, EVEX, VVVV, EVEX_CD8<16, CD8VT1>;

multiclass avx512_shuff_packed_128_common<bits<8> opc, string OpcodeStr,
                                          X86FoldableSchedWrite sched,
                                          X86VectorVTInfo _,
                                          X86VectorVTInfo CastInfo> {
  let ExeDomain = _.ExeDomain in {
  defm rri : AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                  (ins _.RC:$src1, _.RC:$src2, u8imm:$src3),
                  OpcodeStr, "$src3, $src2, $src1", "$src1, $src2, $src3",
                  (_.VT (bitconvert
                         (CastInfo.VT (X86Shuf128 _.RC:$src1, _.RC:$src2,
                                                  (i8 timm:$src3)))))>,
                  Sched<[sched]>;
  defm rmi : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                (ins _.RC:$src1, _.MemOp:$src2, u8imm:$src3),
                OpcodeStr, "$src3, $src2, $src1", "$src1, $src2, $src3",
                (_.VT
                 (bitconvert
                  (CastInfo.VT (X86Shuf128 _.RC:$src1,
                                           (CastInfo.LdFrag addr:$src2),
                                           (i8 timm:$src3)))))>,
                Sched<[sched.Folded, sched.ReadAfterFold]>;
  defm rmbi : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                    (ins _.RC:$src1, _.ScalarMemOp:$src2, u8imm:$src3),
                    OpcodeStr, "$src3, ${src2}"#_.BroadcastStr#", $src1",
                    "$src1, ${src2}"#_.BroadcastStr#", $src3",
                    (_.VT
                     (bitconvert
                      (CastInfo.VT
                       (X86Shuf128 _.RC:$src1,
                                   (_.BroadcastLdFrag addr:$src2),
                                   (i8 timm:$src3)))))>, EVEX_B,
                    Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

multiclass avx512_shuff_packed_128<string OpcodeStr, X86FoldableSchedWrite sched,
                                   AVX512VLVectorVTInfo _,
                                   AVX512VLVectorVTInfo CastInfo, bits<8> opc>{
  let Predicates = [HasAVX512] in
  defm Z : avx512_shuff_packed_128_common<opc, OpcodeStr, sched,
                                          _.info512, CastInfo.info512>, EVEX_V512;

  let Predicates = [HasAVX512, HasVLX] in
  defm Z256 : avx512_shuff_packed_128_common<opc, OpcodeStr, sched,
                                             _.info256, CastInfo.info256>, EVEX_V256;
}

defm VSHUFF32X4 : avx512_shuff_packed_128<"vshuff32x4", WriteFShuffle256,
      avx512vl_f32_info, avx512vl_f64_info, 0x23>, AVX512AIi8Base, EVEX, VVVV, EVEX_CD8<32, CD8VF>;
defm VSHUFF64X2 : avx512_shuff_packed_128<"vshuff64x2", WriteFShuffle256,
      avx512vl_f64_info, avx512vl_f64_info, 0x23>, AVX512AIi8Base, EVEX, VVVV, EVEX_CD8<64, CD8VF>, REX_W;
defm VSHUFI32X4 : avx512_shuff_packed_128<"vshufi32x4", WriteFShuffle256,
      avx512vl_i32_info, avx512vl_i64_info, 0x43>, AVX512AIi8Base, EVEX, VVVV, EVEX_CD8<32, CD8VF>;
defm VSHUFI64X2 : avx512_shuff_packed_128<"vshufi64x2", WriteFShuffle256,
      avx512vl_i64_info, avx512vl_i64_info, 0x43>, AVX512AIi8Base, EVEX, VVVV, EVEX_CD8<64, CD8VF>, REX_W;

multiclass avx512_valign<bits<8> opc, string OpcodeStr,
                         X86FoldableSchedWrite sched, X86VectorVTInfo _>{
  let ExeDomain = _.ExeDomain in {
  defm rri : AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                  (ins _.RC:$src1, _.RC:$src2, u8imm:$src3),
                  OpcodeStr, "$src3, $src2, $src1", "$src1, $src2, $src3",
                  (_.VT (X86VAlign _.RC:$src1, _.RC:$src2, (i8 timm:$src3)))>,
                  Sched<[sched]>;
  defm rmi : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                (ins _.RC:$src1, _.MemOp:$src2, u8imm:$src3),
                OpcodeStr, "$src3, $src2, $src1", "$src1, $src2, $src3",
                (_.VT (X86VAlign _.RC:$src1,
                                 (bitconvert (_.LdFrag addr:$src2)),
                                 (i8 timm:$src3)))>,
                Sched<[sched.Folded, sched.ReadAfterFold]>;

  defm rmbi : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                   (ins _.RC:$src1, _.ScalarMemOp:$src2, u8imm:$src3),
                   OpcodeStr, "$src3, ${src2}"#_.BroadcastStr#", $src1",
                   "$src1, ${src2}"#_.BroadcastStr#", $src3",
                   (X86VAlign _.RC:$src1,
                              (_.VT (_.BroadcastLdFrag addr:$src2)),
                              (i8 timm:$src3))>, EVEX_B,
                   Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

multiclass avx512_valign_common<string OpcodeStr, X86SchedWriteWidths sched,
                                AVX512VLVectorVTInfo _> {
  let Predicates = [HasAVX512] in {
    defm Z    : avx512_valign<0x03, OpcodeStr, sched.ZMM, _.info512>,
                                AVX512AIi8Base, EVEX, VVVV, EVEX_V512;
  }
  let Predicates = [HasAVX512, HasVLX] in {
    defm Z128 : avx512_valign<0x03, OpcodeStr, sched.XMM, _.info128>,
                                AVX512AIi8Base, EVEX, VVVV, EVEX_V128;
    // We can't really override the 256-bit version so change it back to unset.
    defm Z256 : avx512_valign<0x03, OpcodeStr, sched.YMM, _.info256>,
                                AVX512AIi8Base, EVEX, VVVV, EVEX_V256;
  }
}

defm VALIGND: avx512_valign_common<"valignd", SchedWriteShuffle,
                                   avx512vl_i32_info>, EVEX_CD8<32, CD8VF>;
defm VALIGNQ: avx512_valign_common<"valignq", SchedWriteShuffle,
                                   avx512vl_i64_info>, EVEX_CD8<64, CD8VF>,
                                   REX_W;

defm VPALIGNR: avx512_common_3Op_rm_imm8<0x0F, X86PAlignr, "vpalignr",
                                         SchedWriteShuffle, avx512vl_i8_info,
                                         avx512vl_i8_info>, EVEX_CD8<8, CD8VF>;

// Fragments to help convert valignq into masked valignd. Or valignq/valignd
// into vpalignr.
def ValignqImm32XForm : SDNodeXForm<timm, [{
  return getI8Imm(N->getZExtValue() * 2, SDLoc(N));
}]>;
def ValignqImm8XForm : SDNodeXForm<timm, [{
  return getI8Imm(N->getZExtValue() * 8, SDLoc(N));
}]>;
def ValigndImm8XForm : SDNodeXForm<timm, [{
  return getI8Imm(N->getZExtValue() * 4, SDLoc(N));
}]>;

multiclass avx512_vpalign_mask_lowering<string OpcodeStr, SDNode OpNode,
                                        X86VectorVTInfo From, X86VectorVTInfo To,
                                        SDNodeXForm ImmXForm> {
  def : Pat<(To.VT (vselect_mask To.KRCWM:$mask,
                                 (bitconvert
                                  (From.VT (OpNode From.RC:$src1, From.RC:$src2,
                                                   timm:$src3))),
                                 To.RC:$src0)),
            (!cast<Instruction>(OpcodeStr#"rrik") To.RC:$src0, To.KRCWM:$mask,
                                                  To.RC:$src1, To.RC:$src2,
                                                  (ImmXForm timm:$src3))>;

  def : Pat<(To.VT (vselect_mask To.KRCWM:$mask,
                                 (bitconvert
                                  (From.VT (OpNode From.RC:$src1, From.RC:$src2,
                                                   timm:$src3))),
                                 To.ImmAllZerosV)),
            (!cast<Instruction>(OpcodeStr#"rrikz") To.KRCWM:$mask,
                                                   To.RC:$src1, To.RC:$src2,
                                                   (ImmXForm timm:$src3))>;

  def : Pat<(To.VT (vselect_mask To.KRCWM:$mask,
                                 (bitconvert
                                  (From.VT (OpNode From.RC:$src1,
                                                   (From.LdFrag addr:$src2),
                                           timm:$src3))),
                                 To.RC:$src0)),
            (!cast<Instruction>(OpcodeStr#"rmik") To.RC:$src0, To.KRCWM:$mask,
                                                  To.RC:$src1, addr:$src2,
                                                  (ImmXForm timm:$src3))>;

  def : Pat<(To.VT (vselect_mask To.KRCWM:$mask,
                                 (bitconvert
                                  (From.VT (OpNode From.RC:$src1,
                                                   (From.LdFrag addr:$src2),
                                           timm:$src3))),
                                 To.ImmAllZerosV)),
            (!cast<Instruction>(OpcodeStr#"rmikz") To.KRCWM:$mask,
                                                   To.RC:$src1, addr:$src2,
                                                   (ImmXForm timm:$src3))>;
}

multiclass avx512_vpalign_mask_lowering_mb<string OpcodeStr, SDNode OpNode,
                                           X86VectorVTInfo From,
                                           X86VectorVTInfo To,
                                           SDNodeXForm ImmXForm> :
      avx512_vpalign_mask_lowering<OpcodeStr, OpNode, From, To, ImmXForm> {
  def : Pat<(From.VT (OpNode From.RC:$src1,
                             (bitconvert (To.VT (To.BroadcastLdFrag addr:$src2))),
                             timm:$src3)),
            (!cast<Instruction>(OpcodeStr#"rmbi") To.RC:$src1, addr:$src2,
                                                  (ImmXForm timm:$src3))>;

  def : Pat<(To.VT (vselect_mask To.KRCWM:$mask,
                                 (bitconvert
                                  (From.VT (OpNode From.RC:$src1,
                                           (bitconvert
                                            (To.VT (To.BroadcastLdFrag addr:$src2))),
                                           timm:$src3))),
                                 To.RC:$src0)),
            (!cast<Instruction>(OpcodeStr#"rmbik") To.RC:$src0, To.KRCWM:$mask,
                                                   To.RC:$src1, addr:$src2,
                                                   (ImmXForm timm:$src3))>;

  def : Pat<(To.VT (vselect_mask To.KRCWM:$mask,
                                 (bitconvert
                                  (From.VT (OpNode From.RC:$src1,
                                           (bitconvert
                                            (To.VT (To.BroadcastLdFrag addr:$src2))),
                                           timm:$src3))),
                                 To.ImmAllZerosV)),
            (!cast<Instruction>(OpcodeStr#"rmbikz") To.KRCWM:$mask,
                                                    To.RC:$src1, addr:$src2,
                                                    (ImmXForm timm:$src3))>;
}

let Predicates = [HasAVX512] in {
  // For 512-bit we lower to the widest element type we can. So we only need
  // to handle converting valignq to valignd.
  defm : avx512_vpalign_mask_lowering_mb<"VALIGNDZ", X86VAlign, v8i64_info,
                                         v16i32_info, ValignqImm32XForm>;
}

let Predicates = [HasVLX] in {
  // For 128-bit we lower to the widest element type we can. So we only need
  // to handle converting valignq to valignd.
  defm : avx512_vpalign_mask_lowering_mb<"VALIGNDZ128", X86VAlign, v2i64x_info,
                                         v4i32x_info, ValignqImm32XForm>;
  // For 256-bit we lower to the widest element type we can. So we only need
  // to handle converting valignq to valignd.
  defm : avx512_vpalign_mask_lowering_mb<"VALIGNDZ256", X86VAlign, v4i64x_info,
                                         v8i32x_info, ValignqImm32XForm>;
}

let Predicates = [HasVLX, HasBWI] in {
  // We can turn 128 and 256 bit VALIGND/VALIGNQ into VPALIGNR.
  defm : avx512_vpalign_mask_lowering<"VPALIGNRZ128", X86VAlign, v2i64x_info,
                                      v16i8x_info, ValignqImm8XForm>;
  defm : avx512_vpalign_mask_lowering<"VPALIGNRZ128", X86VAlign, v4i32x_info,
                                      v16i8x_info, ValigndImm8XForm>;
}

defm VDBPSADBW: avx512_common_3Op_rm_imm8<0x42, X86dbpsadbw, "vdbpsadbw",
                SchedWritePSADBW, avx512vl_i16_info, avx512vl_i8_info>,
                EVEX_CD8<8, CD8VF>;

multiclass avx512_unary_rm<bits<8> opc, string OpcodeStr, SDNode OpNode,
                           X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain in {
  defm rr : AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                    (ins _.RC:$src1), OpcodeStr,
                    "$src1", "$src1",
                    (_.VT (OpNode (_.VT _.RC:$src1)))>, EVEX, AVX5128IBase,
                    Sched<[sched]>;

  defm rm : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                  (ins _.MemOp:$src1), OpcodeStr,
                  "$src1", "$src1",
                  (_.VT (OpNode (_.VT (bitconvert (_.LdFrag addr:$src1)))))>,
            EVEX, AVX5128IBase, EVEX_CD8<_.EltSize, CD8VF>,
            Sched<[sched.Folded]>;
  }
}

multiclass avx512_unary_rmb<bits<8> opc, string OpcodeStr, SDNode OpNode,
                            X86FoldableSchedWrite sched, X86VectorVTInfo _> :
           avx512_unary_rm<opc, OpcodeStr, OpNode, sched, _> {
  defm rmb : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                  (ins _.ScalarMemOp:$src1), OpcodeStr,
                  "${src1}"#_.BroadcastStr,
                  "${src1}"#_.BroadcastStr,
                  (_.VT (OpNode (_.VT (_.BroadcastLdFrag addr:$src1))))>,
             EVEX, AVX5128IBase, EVEX_B, EVEX_CD8<_.EltSize, CD8VF>,
             Sched<[sched.Folded]>;
}

multiclass avx512_unary_rm_vl<bits<8> opc, string OpcodeStr, SDNode OpNode,
                              X86SchedWriteWidths sched,
                              AVX512VLVectorVTInfo VTInfo, Predicate prd> {
  let Predicates = [prd] in
    defm Z : avx512_unary_rm<opc, OpcodeStr, OpNode, sched.ZMM, VTInfo.info512>,
                             EVEX_V512;

  let Predicates = [prd, HasVLX] in {
    defm Z256 : avx512_unary_rm<opc, OpcodeStr, OpNode, sched.YMM, VTInfo.info256>,
                              EVEX_V256;
    defm Z128 : avx512_unary_rm<opc, OpcodeStr, OpNode, sched.XMM, VTInfo.info128>,
                              EVEX_V128;
  }
}

multiclass avx512_unary_rmb_vl<bits<8> opc, string OpcodeStr, SDNode OpNode,
                               X86SchedWriteWidths sched, AVX512VLVectorVTInfo VTInfo,
                               Predicate prd> {
  let Predicates = [prd] in
    defm Z : avx512_unary_rmb<opc, OpcodeStr, OpNode, sched.ZMM, VTInfo.info512>,
                              EVEX_V512;

  let Predicates = [prd, HasVLX] in {
    defm Z256 : avx512_unary_rmb<opc, OpcodeStr, OpNode, sched.YMM, VTInfo.info256>,
                                 EVEX_V256;
    defm Z128 : avx512_unary_rmb<opc, OpcodeStr, OpNode, sched.XMM, VTInfo.info128>,
                                 EVEX_V128;
  }
}

multiclass avx512_unary_rm_vl_dq<bits<8> opc_d, bits<8> opc_q, string OpcodeStr,
                                 SDNode OpNode, X86SchedWriteWidths sched,
                                 Predicate prd> {
  defm Q : avx512_unary_rmb_vl<opc_q, OpcodeStr#"q", OpNode, sched,
                               avx512vl_i64_info, prd>, REX_W;
  defm D : avx512_unary_rmb_vl<opc_d, OpcodeStr#"d", OpNode, sched,
                               avx512vl_i32_info, prd>;
}

multiclass avx512_unary_rm_vl_bw<bits<8> opc_b, bits<8> opc_w, string OpcodeStr,
                                 SDNode OpNode, X86SchedWriteWidths sched,
                                 Predicate prd> {
  defm W : avx512_unary_rm_vl<opc_w, OpcodeStr#"w", OpNode, sched,
                              avx512vl_i16_info, prd>, WIG;
  defm B : avx512_unary_rm_vl<opc_b, OpcodeStr#"b", OpNode, sched,
                              avx512vl_i8_info, prd>, WIG;
}

multiclass avx512_unary_rm_vl_all<bits<8> opc_b, bits<8> opc_w,
                                  bits<8> opc_d, bits<8> opc_q,
                                  string OpcodeStr, SDNode OpNode,
                                  X86SchedWriteWidths sched> {
  defm NAME : avx512_unary_rm_vl_dq<opc_d, opc_q, OpcodeStr, OpNode, sched,
                                    HasAVX512>,
              avx512_unary_rm_vl_bw<opc_b, opc_w, OpcodeStr, OpNode, sched,
                                    HasBWI>;
}

defm VPABS : avx512_unary_rm_vl_all<0x1C, 0x1D, 0x1E, 0x1F, "vpabs", abs,
                                    SchedWriteVecALU>;

// VPABS: Use 512bit version to implement 128/256 bit in case NoVLX.
let Predicates = [HasAVX512, NoVLX, HasEVEX512] in {
  def : Pat<(v4i64 (abs VR256X:$src)),
            (EXTRACT_SUBREG
                (VPABSQZrr
                    (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src, sub_ymm)),
             sub_ymm)>;
  def : Pat<(v2i64 (abs VR128X:$src)),
            (EXTRACT_SUBREG
                (VPABSQZrr
                    (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src, sub_xmm)),
             sub_xmm)>;
}

// Use 512bit version to implement 128/256 bit.
multiclass avx512_unary_lowering<string InstrStr, SDNode OpNode,
                                 AVX512VLVectorVTInfo _, Predicate prd> {
  let Predicates = [prd, NoVLX, HasEVEX512] in {
    def : Pat<(_.info256.VT (OpNode (_.info256.VT _.info256.RC:$src1))),
              (EXTRACT_SUBREG
                (!cast<Instruction>(InstrStr # "Zrr")
                  (INSERT_SUBREG(_.info512.VT(IMPLICIT_DEF)),
                                 _.info256.RC:$src1,
                                 _.info256.SubRegIdx)),
              _.info256.SubRegIdx)>;

    def : Pat<(_.info128.VT (OpNode (_.info128.VT _.info128.RC:$src1))),
              (EXTRACT_SUBREG
                (!cast<Instruction>(InstrStr # "Zrr")
                  (INSERT_SUBREG(_.info512.VT(IMPLICIT_DEF)),
                                 _.info128.RC:$src1,
                                 _.info128.SubRegIdx)),
              _.info128.SubRegIdx)>;
  }
}

defm VPLZCNT    : avx512_unary_rm_vl_dq<0x44, 0x44, "vplzcnt", ctlz,
                                        SchedWriteVecIMul, HasCDI>;

// FIXME: Is there a better scheduler class for VPCONFLICT?
defm VPCONFLICT : avx512_unary_rm_vl_dq<0xC4, 0xC4, "vpconflict", X86Conflict,
                                        SchedWriteVecALU, HasCDI>;

// VPLZCNT: Use 512bit version to implement 128/256 bit in case NoVLX.
defm : avx512_unary_lowering<"VPLZCNTQ", ctlz, avx512vl_i64_info, HasCDI>;
defm : avx512_unary_lowering<"VPLZCNTD", ctlz, avx512vl_i32_info, HasCDI>;

//===---------------------------------------------------------------------===//
// Counts number of ones - VPOPCNTD and VPOPCNTQ
//===---------------------------------------------------------------------===//

// FIXME: Is there a better scheduler class for VPOPCNTD/VPOPCNTQ?
defm VPOPCNT : avx512_unary_rm_vl_dq<0x55, 0x55, "vpopcnt", ctpop,
                                     SchedWriteVecALU, HasVPOPCNTDQ>;

defm : avx512_unary_lowering<"VPOPCNTQ", ctpop, avx512vl_i64_info, HasVPOPCNTDQ>;
defm : avx512_unary_lowering<"VPOPCNTD", ctpop, avx512vl_i32_info, HasVPOPCNTDQ>;

//===---------------------------------------------------------------------===//
// Replicate Single FP - MOVSHDUP and MOVSLDUP
//===---------------------------------------------------------------------===//

multiclass avx512_replicate<bits<8> opc, string OpcodeStr, SDNode OpNode,
                            X86SchedWriteWidths sched> {
  defm NAME:       avx512_unary_rm_vl<opc, OpcodeStr, OpNode, sched,
                                      avx512vl_f32_info, HasAVX512>, TB, XS;
}

defm VMOVSHDUP : avx512_replicate<0x16, "vmovshdup", X86Movshdup,
                                  SchedWriteFShuffle>;
defm VMOVSLDUP : avx512_replicate<0x12, "vmovsldup", X86Movsldup,
                                  SchedWriteFShuffle>;

//===----------------------------------------------------------------------===//
// AVX-512 - MOVDDUP
//===----------------------------------------------------------------------===//

multiclass avx512_movddup_128<bits<8> opc, string OpcodeStr,
                              X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain in {
  defm rr : AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                   (ins _.RC:$src), OpcodeStr, "$src", "$src",
                   (_.VT (X86VBroadcast (_.VT _.RC:$src)))>, EVEX,
                   Sched<[sched]>;
  defm rm : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                 (ins _.ScalarMemOp:$src), OpcodeStr, "$src", "$src",
                 (_.VT (_.BroadcastLdFrag addr:$src))>,
                 EVEX, EVEX_CD8<_.EltSize, CD8VH>,
                 Sched<[sched.Folded]>;
  }
}

multiclass avx512_movddup_common<bits<8> opc, string OpcodeStr,
                                 X86SchedWriteWidths sched, AVX512VLVectorVTInfo VTInfo> {
  defm Z : avx512_unary_rm<opc, OpcodeStr, X86Movddup, sched.ZMM,
                           VTInfo.info512>, EVEX_V512;

  let Predicates = [HasAVX512, HasVLX] in {
    defm Z256 : avx512_unary_rm<opc, OpcodeStr, X86Movddup, sched.YMM,
                                VTInfo.info256>, EVEX_V256;
    defm Z128 : avx512_movddup_128<opc, OpcodeStr, sched.XMM,
                                   VTInfo.info128>, EVEX_V128;
  }
}

multiclass avx512_movddup<bits<8> opc, string OpcodeStr,
                          X86SchedWriteWidths sched> {
  defm NAME:      avx512_movddup_common<opc, OpcodeStr, sched,
                                        avx512vl_f64_info>, TB, XD, REX_W;
}

defm VMOVDDUP : avx512_movddup<0x12, "vmovddup", SchedWriteFShuffle>;

let Predicates = [HasVLX] in {
def : Pat<(v2f64 (X86VBroadcast f64:$src)),
          (VMOVDDUPZ128rr (v2f64 (COPY_TO_REGCLASS FR64X:$src, VR128X)))>;

def : Pat<(vselect_mask (v2i1 VK2WM:$mask), (v2f64 (X86VBroadcast f64:$src)),
                        (v2f64 VR128X:$src0)),
          (VMOVDDUPZ128rrk VR128X:$src0, VK2WM:$mask,
                           (v2f64 (COPY_TO_REGCLASS FR64X:$src, VR128X)))>;
def : Pat<(vselect_mask (v2i1 VK2WM:$mask), (v2f64 (X86VBroadcast f64:$src)),
                        immAllZerosV),
          (VMOVDDUPZ128rrkz VK2WM:$mask, (v2f64 (COPY_TO_REGCLASS FR64X:$src, VR128X)))>;
}

//===----------------------------------------------------------------------===//
// AVX-512 - Unpack Instructions
//===----------------------------------------------------------------------===//

let Uses = []<Register>, mayRaiseFPException = 0 in {
defm VUNPCKH : avx512_fp_binop_p<0x15, "vunpckh", X86Unpckh, X86Unpckh, HasAVX512,
                                 SchedWriteFShuffleSizes, 0, 1>;
defm VUNPCKL : avx512_fp_binop_p<0x14, "vunpckl", X86Unpckl, X86Unpckl, HasAVX512,
                                 SchedWriteFShuffleSizes>;
}

defm VPUNPCKLBW : avx512_binop_rm_vl_b<0x60, "vpunpcklbw", X86Unpckl,
                                       SchedWriteShuffle, HasBWI>;
defm VPUNPCKHBW : avx512_binop_rm_vl_b<0x68, "vpunpckhbw", X86Unpckh,
                                       SchedWriteShuffle, HasBWI>;
defm VPUNPCKLWD : avx512_binop_rm_vl_w<0x61, "vpunpcklwd", X86Unpckl,
                                       SchedWriteShuffle, HasBWI>;
defm VPUNPCKHWD : avx512_binop_rm_vl_w<0x69, "vpunpckhwd", X86Unpckh,
                                       SchedWriteShuffle, HasBWI>;

defm VPUNPCKLDQ : avx512_binop_rm_vl_d<0x62, "vpunpckldq", X86Unpckl,
                                       SchedWriteShuffle, HasAVX512>;
defm VPUNPCKHDQ : avx512_binop_rm_vl_d<0x6A, "vpunpckhdq", X86Unpckh,
                                       SchedWriteShuffle, HasAVX512>;
defm VPUNPCKLQDQ : avx512_binop_rm_vl_q<0x6C, "vpunpcklqdq", X86Unpckl,
                                        SchedWriteShuffle, HasAVX512>;
defm VPUNPCKHQDQ : avx512_binop_rm_vl_q<0x6D, "vpunpckhqdq", X86Unpckh,
                                        SchedWriteShuffle, HasAVX512>;

//===----------------------------------------------------------------------===//
// AVX-512 - Extract & Insert Integer Instructions
//===----------------------------------------------------------------------===//

multiclass avx512_extract_elt_bw_m<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                                            X86VectorVTInfo _> {
  def mr : AVX512Ii8<opc, MRMDestMem, (outs),
              (ins _.ScalarMemOp:$dst, _.RC:$src1, u8imm:$src2),
              OpcodeStr#"\t{$src2, $src1, $dst|$dst, $src1, $src2}",
              [(store (_.EltVT (trunc (OpNode (_.VT _.RC:$src1), timm:$src2))),
                       addr:$dst)]>,
              EVEX, EVEX_CD8<_.EltSize, CD8VT1>, Sched<[WriteVecExtractSt]>;
}

multiclass avx512_extract_elt_b<string OpcodeStr, X86VectorVTInfo _> {
  let Predicates = [HasBWI] in {
    def rr : AVX512Ii8<0x14, MRMDestReg, (outs GR32orGR64:$dst),
                  (ins _.RC:$src1, u8imm:$src2),
                  OpcodeStr#"\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                  [(set GR32orGR64:$dst,
                        (X86pextrb (_.VT _.RC:$src1), timm:$src2))]>,
                  EVEX, TA, PD, Sched<[WriteVecExtract]>;

    defm NAME : avx512_extract_elt_bw_m<0x14, OpcodeStr, X86pextrb, _>, TA, PD;
  }
}

multiclass avx512_extract_elt_w<string OpcodeStr, X86VectorVTInfo _> {
  let Predicates = [HasBWI] in {
    def rr : AVX512Ii8<0xC5, MRMSrcReg, (outs GR32orGR64:$dst),
                  (ins _.RC:$src1, u8imm:$src2),
                  OpcodeStr#"\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                  [(set GR32orGR64:$dst,
                        (X86pextrw (_.VT _.RC:$src1), timm:$src2))]>,
                  EVEX, TB, PD, Sched<[WriteVecExtract]>;

    let hasSideEffects = 0, isCodeGenOnly = 1, ForceDisassemble = 1 in
    def rr_REV : AVX512Ii8<0x15, MRMDestReg, (outs GR32orGR64:$dst),
                   (ins _.RC:$src1, u8imm:$src2),
                   OpcodeStr#"\t{$src2, $src1, $dst|$dst, $src1, $src2}", []>,
                   EVEX, TA, PD, Sched<[WriteVecExtract]>;

    defm NAME : avx512_extract_elt_bw_m<0x15, OpcodeStr, X86pextrw, _>, TA, PD;
  }
}

multiclass avx512_extract_elt_dq<string OpcodeStr, X86VectorVTInfo _,
                                                            RegisterClass GRC> {
  let Predicates = [HasDQI] in {
    def rr : AVX512Ii8<0x16, MRMDestReg, (outs GRC:$dst),
                  (ins _.RC:$src1, u8imm:$src2),
                  OpcodeStr#"\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                  [(set GRC:$dst,
                      (extractelt (_.VT _.RC:$src1), imm:$src2))]>,
                  EVEX, TA, PD, Sched<[WriteVecExtract]>;

    def mr : AVX512Ii8<0x16, MRMDestMem, (outs),
                (ins _.ScalarMemOp:$dst, _.RC:$src1, u8imm:$src2),
                OpcodeStr#"\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                [(store (extractelt (_.VT _.RC:$src1),
                                    imm:$src2),addr:$dst)]>,
                EVEX, EVEX_CD8<_.EltSize, CD8VT1>, TA, PD,
                Sched<[WriteVecExtractSt]>;
  }
}

defm VPEXTRBZ : avx512_extract_elt_b<"vpextrb", v16i8x_info>, WIG;
defm VPEXTRWZ : avx512_extract_elt_w<"vpextrw", v8i16x_info>, WIG;
defm VPEXTRDZ : avx512_extract_elt_dq<"vpextrd", v4i32x_info, GR32>;
defm VPEXTRQZ : avx512_extract_elt_dq<"vpextrq", v2i64x_info, GR64>, REX_W;

multiclass avx512_insert_elt_m<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                            X86VectorVTInfo _, PatFrag LdFrag,
                                            SDPatternOperator immoperator> {
  def rm : AVX512Ii8<opc, MRMSrcMem, (outs _.RC:$dst),
      (ins _.RC:$src1,  _.ScalarMemOp:$src2, u8imm:$src3),
      OpcodeStr#"\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}",
      [(set _.RC:$dst,
          (_.VT (OpNode _.RC:$src1, (LdFrag addr:$src2), immoperator:$src3)))]>,
      EVEX, VVVV, EVEX_CD8<_.EltSize, CD8VT1>, Sched<[WriteVecInsert.Folded, WriteVecInsert.ReadAfterFold]>;
}

multiclass avx512_insert_elt_bw<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                            X86VectorVTInfo _, PatFrag LdFrag> {
  let Predicates = [HasBWI] in {
    def rr : AVX512Ii8<opc, MRMSrcReg, (outs _.RC:$dst),
        (ins _.RC:$src1, GR32orGR64:$src2, u8imm:$src3),
        OpcodeStr#"\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}",
        [(set _.RC:$dst,
            (OpNode _.RC:$src1, GR32orGR64:$src2, timm:$src3))]>, EVEX, VVVV,
        Sched<[WriteVecInsert]>;

    defm NAME : avx512_insert_elt_m<opc, OpcodeStr, OpNode, _, LdFrag, timm>;
  }
}

multiclass avx512_insert_elt_dq<bits<8> opc, string OpcodeStr,
                                         X86VectorVTInfo _, RegisterClass GRC> {
  let Predicates = [HasDQI] in {
    def rr : AVX512Ii8<opc, MRMSrcReg, (outs _.RC:$dst),
        (ins _.RC:$src1, GRC:$src2, u8imm:$src3),
        OpcodeStr#"\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}",
        [(set _.RC:$dst,
            (_.VT (insertelt _.RC:$src1, GRC:$src2, imm:$src3)))]>,
        EVEX, VVVV, TA, PD, Sched<[WriteVecInsert]>;

    defm NAME : avx512_insert_elt_m<opc, OpcodeStr, insertelt, _,
                                    _.ScalarLdFrag, imm>, TA, PD;
  }
}

defm VPINSRBZ : avx512_insert_elt_bw<0x20, "vpinsrb", X86pinsrb, v16i8x_info,
                                     extloadi8>, TA, PD, WIG;
defm VPINSRWZ : avx512_insert_elt_bw<0xC4, "vpinsrw", X86pinsrw, v8i16x_info,
                                     extloadi16>, TB, PD, WIG;
defm VPINSRDZ : avx512_insert_elt_dq<0x22, "vpinsrd", v4i32x_info, GR32>;
defm VPINSRQZ : avx512_insert_elt_dq<0x22, "vpinsrq", v2i64x_info, GR64>, REX_W;

let Predicates = [HasAVX512, NoBWI] in {
  def : Pat<(X86pinsrb VR128:$src1,
                       (i32 (anyext (i8 (bitconvert v8i1:$src2)))),
                       timm:$src3),
            (VPINSRBrr VR128:$src1, (i32 (COPY_TO_REGCLASS VK8:$src2, GR32)),
                       timm:$src3)>;
}

let Predicates = [HasBWI] in {
  def : Pat<(X86pinsrb VR128:$src1, (i32 (anyext (i8 GR8:$src2))), timm:$src3),
            (VPINSRBZrr VR128:$src1, (INSERT_SUBREG (i32 (IMPLICIT_DEF)),
                        GR8:$src2, sub_8bit), timm:$src3)>;
  def : Pat<(X86pinsrb VR128:$src1,
                       (i32 (anyext (i8 (bitconvert v8i1:$src2)))),
                       timm:$src3),
            (VPINSRBZrr VR128:$src1, (i32 (COPY_TO_REGCLASS VK8:$src2, GR32)),
                        timm:$src3)>;
}

// Always select FP16 instructions if available.
let Predicates = [HasBWI], AddedComplexity = -10 in {
  def : Pat<(f16 (load addr:$src)), (COPY_TO_REGCLASS (VPINSRWZrm (v8i16 (IMPLICIT_DEF)), addr:$src, 0), FR16X)>;
  def : Pat<(store f16:$src, addr:$dst), (VPEXTRWZmr addr:$dst, (v8i16 (COPY_TO_REGCLASS FR16:$src, VR128)), 0)>;
  def : Pat<(i16 (bitconvert f16:$src)), (EXTRACT_SUBREG (VPEXTRWZrr (v8i16 (COPY_TO_REGCLASS FR16X:$src, VR128X)), 0), sub_16bit)>;
  def : Pat<(f16 (bitconvert i16:$src)), (COPY_TO_REGCLASS (VPINSRWZrr (v8i16 (IMPLICIT_DEF)), (INSERT_SUBREG (IMPLICIT_DEF), GR16:$src, sub_16bit), 0), FR16X)>;
}

//===----------------------------------------------------------------------===//
// VSHUFPS - VSHUFPD Operations
//===----------------------------------------------------------------------===//

multiclass avx512_shufp<string OpcodeStr, AVX512VLVectorVTInfo VTInfo_FP>{
  defm NAME: avx512_common_3Op_imm8<OpcodeStr, VTInfo_FP, 0xC6, X86Shufp,
                                    SchedWriteFShuffle>,
                                    EVEX_CD8<VTInfo_FP.info512.EltSize, CD8VF>,
                                    TA, EVEX, VVVV;
}

defm VSHUFPS: avx512_shufp<"vshufps", avx512vl_f32_info>, TB;
defm VSHUFPD: avx512_shufp<"vshufpd", avx512vl_f64_info>, TB, PD, REX_W;

//===----------------------------------------------------------------------===//
// AVX-512 - Byte shift Left/Right
//===----------------------------------------------------------------------===//

multiclass avx512_shift_packed<bits<8> opc, SDNode OpNode, Format MRMr,
                               Format MRMm, string OpcodeStr,
                               X86FoldableSchedWrite sched, X86VectorVTInfo _>{
  def ri : AVX512<opc, MRMr,
             (outs _.RC:$dst), (ins _.RC:$src1, u8imm:$src2),
             !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             [(set _.RC:$dst,(_.VT (OpNode _.RC:$src1, (i8 timm:$src2))))]>,
             Sched<[sched]>;
  def mi : AVX512<opc, MRMm,
           (outs _.RC:$dst), (ins _.MemOp:$src1, u8imm:$src2),
           !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
           [(set _.RC:$dst,(_.VT (OpNode
                                 (_.VT (bitconvert (_.LdFrag addr:$src1))),
                                 (i8 timm:$src2))))]>,
           Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass avx512_shift_packed_all<bits<8> opc, SDNode OpNode, Format MRMr,
                                   Format MRMm, string OpcodeStr,
                                   X86SchedWriteWidths sched, Predicate prd>{
  let Predicates = [prd] in
    defm Z : avx512_shift_packed<opc, OpNode, MRMr, MRMm, OpcodeStr,
                                 sched.ZMM, v64i8_info>, EVEX_V512;
  let Predicates = [prd, HasVLX] in {
    defm Z256 : avx512_shift_packed<opc, OpNode, MRMr, MRMm, OpcodeStr,
                                    sched.YMM, v32i8x_info>, EVEX_V256;
    defm Z128 : avx512_shift_packed<opc, OpNode, MRMr, MRMm, OpcodeStr,
                                    sched.XMM, v16i8x_info>, EVEX_V128;
  }
}
defm VPSLLDQ : avx512_shift_packed_all<0x73, X86vshldq, MRM7r, MRM7m, "vpslldq",
                                       SchedWriteShuffle, HasBWI>,
                                       AVX512PDIi8Base, EVEX, VVVV, WIG;
defm VPSRLDQ : avx512_shift_packed_all<0x73, X86vshrdq, MRM3r, MRM3m, "vpsrldq",
                                       SchedWriteShuffle, HasBWI>,
                                       AVX512PDIi8Base, EVEX, VVVV, WIG;

multiclass avx512_psadbw_packed<bits<8> opc, SDNode OpNode,
                                string OpcodeStr, X86FoldableSchedWrite sched,
                                X86VectorVTInfo _dst, X86VectorVTInfo _src> {
  let isCommutable = 1 in
  def rr : AVX512BI<opc, MRMSrcReg,
             (outs _dst.RC:$dst), (ins _src.RC:$src1, _src.RC:$src2),
             !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             [(set _dst.RC:$dst,(_dst.VT
                                (OpNode (_src.VT _src.RC:$src1),
                                        (_src.VT _src.RC:$src2))))]>,
             Sched<[sched]>;
  def rm : AVX512BI<opc, MRMSrcMem,
           (outs _dst.RC:$dst), (ins _src.RC:$src1, _src.MemOp:$src2),
           !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
           [(set _dst.RC:$dst,(_dst.VT
                              (OpNode (_src.VT _src.RC:$src1),
                              (_src.VT (bitconvert
                                        (_src.LdFrag addr:$src2))))))]>,
           Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass avx512_psadbw_packed_all<bits<8> opc, SDNode OpNode,
                                    string OpcodeStr, X86SchedWriteWidths sched,
                                    Predicate prd> {
  let Predicates = [prd] in
    defm Z : avx512_psadbw_packed<opc, OpNode, OpcodeStr, sched.ZMM,
                                  v8i64_info, v64i8_info>, EVEX_V512;
  let Predicates = [prd, HasVLX] in {
    defm Z256 : avx512_psadbw_packed<opc, OpNode, OpcodeStr, sched.YMM,
                                     v4i64x_info, v32i8x_info>, EVEX_V256;
    defm Z128 : avx512_psadbw_packed<opc, OpNode, OpcodeStr, sched.XMM,
                                     v2i64x_info, v16i8x_info>, EVEX_V128;
  }
}

defm VPSADBW : avx512_psadbw_packed_all<0xf6, X86psadbw, "vpsadbw",
                                        SchedWritePSADBW, HasBWI>, EVEX, VVVV, WIG;

// Transforms to swizzle an immediate to enable better matching when
// memory operand isn't in the right place.
def VPTERNLOG321_imm8 : SDNodeXForm<timm, [{
  // Convert a VPTERNLOG immediate by swapping operand 0 and operand 2.
  uint8_t Imm = N->getZExtValue();
  // Swap bits 1/4 and 3/6.
  uint8_t NewImm = Imm & 0xa5;
  if (Imm & 0x02) NewImm |= 0x10;
  if (Imm & 0x10) NewImm |= 0x02;
  if (Imm & 0x08) NewImm |= 0x40;
  if (Imm & 0x40) NewImm |= 0x08;
  return getI8Imm(NewImm, SDLoc(N));
}]>;
def VPTERNLOG213_imm8 : SDNodeXForm<timm, [{
  // Convert a VPTERNLOG immediate by swapping operand 1 and operand 2.
  uint8_t Imm = N->getZExtValue();
  // Swap bits 2/4 and 3/5.
  uint8_t NewImm = Imm & 0xc3;
  if (Imm & 0x04) NewImm |= 0x10;
  if (Imm & 0x10) NewImm |= 0x04;
  if (Imm & 0x08) NewImm |= 0x20;
  if (Imm & 0x20) NewImm |= 0x08;
  return getI8Imm(NewImm, SDLoc(N));
}]>;
def VPTERNLOG132_imm8 : SDNodeXForm<timm, [{
  // Convert a VPTERNLOG immediate by swapping operand 1 and operand 2.
  uint8_t Imm = N->getZExtValue();
  // Swap bits 1/2 and 5/6.
  uint8_t NewImm = Imm & 0x99;
  if (Imm & 0x02) NewImm |= 0x04;
  if (Imm & 0x04) NewImm |= 0x02;
  if (Imm & 0x20) NewImm |= 0x40;
  if (Imm & 0x40) NewImm |= 0x20;
  return getI8Imm(NewImm, SDLoc(N));
}]>;
def VPTERNLOG231_imm8 : SDNodeXForm<timm, [{
  // Convert a VPTERNLOG immediate by moving operand 1 to the end.
  uint8_t Imm = N->getZExtValue();
  // Move bits 1->2, 2->4, 3->6, 4->1, 5->3, 6->5
  uint8_t NewImm = Imm & 0x81;
  if (Imm & 0x02) NewImm |= 0x04;
  if (Imm & 0x04) NewImm |= 0x10;
  if (Imm & 0x08) NewImm |= 0x40;
  if (Imm & 0x10) NewImm |= 0x02;
  if (Imm & 0x20) NewImm |= 0x08;
  if (Imm & 0x40) NewImm |= 0x20;
  return getI8Imm(NewImm, SDLoc(N));
}]>;
def VPTERNLOG312_imm8 : SDNodeXForm<timm, [{
  // Convert a VPTERNLOG immediate by moving operand 2 to the beginning.
  uint8_t Imm = N->getZExtValue();
  // Move bits 1->4, 2->1, 3->5, 4->2, 5->6, 6->3
  uint8_t NewImm = Imm & 0x81;
  if (Imm & 0x02) NewImm |= 0x10;
  if (Imm & 0x04) NewImm |= 0x02;
  if (Imm & 0x08) NewImm |= 0x20;
  if (Imm & 0x10) NewImm |= 0x04;
  if (Imm & 0x20) NewImm |= 0x40;
  if (Imm & 0x40) NewImm |= 0x08;
  return getI8Imm(NewImm, SDLoc(N));
}]>;

multiclass avx512_ternlog<bits<8> opc, string OpcodeStr, SDNode OpNode,
                          X86FoldableSchedWrite sched, X86VectorVTInfo _,
                          string Name>{
  let Constraints = "$src1 = $dst", ExeDomain = _.ExeDomain in {
  defm rri : AVX512_maskable_3src<opc, MRMSrcReg, _, (outs _.RC:$dst),
                      (ins _.RC:$src2, _.RC:$src3, u8imm:$src4),
                      OpcodeStr, "$src4, $src3, $src2", "$src2, $src3, $src4",
                      (OpNode (_.VT _.RC:$src1),
                              (_.VT _.RC:$src2),
                              (_.VT _.RC:$src3),
                              (i8 timm:$src4)), 1, 1>,
                      AVX512AIi8Base, EVEX, VVVV, Sched<[sched]>;
  defm rmi : AVX512_maskable_3src<opc, MRMSrcMem, _, (outs _.RC:$dst),
                    (ins _.RC:$src2, _.MemOp:$src3, u8imm:$src4),
                    OpcodeStr, "$src4, $src3, $src2", "$src2, $src3, $src4",
                    (OpNode (_.VT _.RC:$src1),
                            (_.VT _.RC:$src2),
                            (_.VT (bitconvert (_.LdFrag addr:$src3))),
                            (i8 timm:$src4)), 1, 0>,
                    AVX512AIi8Base, EVEX, VVVV, EVEX_CD8<_.EltSize, CD8VF>,
                    Sched<[sched.Folded, sched.ReadAfterFold]>;
  defm rmbi : AVX512_maskable_3src<opc, MRMSrcMem, _, (outs _.RC:$dst),
                    (ins _.RC:$src2, _.ScalarMemOp:$src3, u8imm:$src4),
                    OpcodeStr, "$src4, ${src3}"#_.BroadcastStr#", $src2",
                    "$src2, ${src3}"#_.BroadcastStr#", $src4",
                    (OpNode (_.VT _.RC:$src1),
                            (_.VT _.RC:$src2),
                            (_.VT (_.BroadcastLdFrag addr:$src3)),
                            (i8 timm:$src4)), 1, 0>, EVEX_B,
                    AVX512AIi8Base, EVEX, VVVV, EVEX_CD8<_.EltSize, CD8VF>,
                    Sched<[sched.Folded, sched.ReadAfterFold]>;
  }// Constraints = "$src1 = $dst"

  // Additional patterns for matching passthru operand in other positions.
  def : Pat<(_.VT (vselect_mask _.KRCWM:$mask,
                   (OpNode _.RC:$src3, _.RC:$src2, _.RC:$src1, (i8 timm:$src4)),
                   _.RC:$src1)),
            (!cast<Instruction>(Name#_.ZSuffix#rrik) _.RC:$src1, _.KRCWM:$mask,
             _.RC:$src2, _.RC:$src3, (VPTERNLOG321_imm8 timm:$src4))>;
  def : Pat<(_.VT (vselect_mask _.KRCWM:$mask,
                   (OpNode _.RC:$src2, _.RC:$src1, _.RC:$src3, (i8 timm:$src4)),
                   _.RC:$src1)),
            (!cast<Instruction>(Name#_.ZSuffix#rrik) _.RC:$src1, _.KRCWM:$mask,
             _.RC:$src2, _.RC:$src3, (VPTERNLOG213_imm8 timm:$src4))>;

  // Additional patterns for matching zero masking with loads in other
  // positions.
  def : Pat<(_.VT (vselect_mask _.KRCWM:$mask,
                   (OpNode (bitconvert (_.LdFrag addr:$src3)),
                    _.RC:$src2, _.RC:$src1, (i8 timm:$src4)),
                   _.ImmAllZerosV)),
            (!cast<Instruction>(Name#_.ZSuffix#rmikz) _.RC:$src1, _.KRCWM:$mask,
             _.RC:$src2, addr:$src3, (VPTERNLOG321_imm8 timm:$src4))>;
  def : Pat<(_.VT (vselect_mask _.KRCWM:$mask,
                   (OpNode _.RC:$src1, (bitconvert (_.LdFrag addr:$src3)),
                    _.RC:$src2, (i8 timm:$src4)),
                   _.ImmAllZerosV)),
            (!cast<Instruction>(Name#_.ZSuffix#rmikz) _.RC:$src1, _.KRCWM:$mask,
             _.RC:$src2, addr:$src3, (VPTERNLOG132_imm8 timm:$src4))>;

  // Additional patterns for matching masked loads with different
  // operand orders.
  def : Pat<(_.VT (vselect_mask _.KRCWM:$mask,
                   (OpNode _.RC:$src1, (bitconvert (_.LdFrag addr:$src3)),
                    _.RC:$src2, (i8 timm:$src4)),
                   _.RC:$src1)),
            (!cast<Instruction>(Name#_.ZSuffix#rmik) _.RC:$src1, _.KRCWM:$mask,
             _.RC:$src2, addr:$src3, (VPTERNLOG132_imm8 timm:$src4))>;
  def : Pat<(_.VT (vselect_mask _.KRCWM:$mask,
                   (OpNode (bitconvert (_.LdFrag addr:$src3)),
                    _.RC:$src2, _.RC:$src1, (i8 timm:$src4)),
                   _.RC:$src1)),
            (!cast<Instruction>(Name#_.ZSuffix#rmik) _.RC:$src1, _.KRCWM:$mask,
             _.RC:$src2, addr:$src3, (VPTERNLOG321_imm8 timm:$src4))>;
  def : Pat<(_.VT (vselect_mask _.KRCWM:$mask,
                   (OpNode _.RC:$src2, _.RC:$src1,
                    (bitconvert (_.LdFrag addr:$src3)), (i8 timm:$src4)),
                   _.RC:$src1)),
            (!cast<Instruction>(Name#_.ZSuffix#rmik) _.RC:$src1, _.KRCWM:$mask,
             _.RC:$src2, addr:$src3, (VPTERNLOG213_imm8 timm:$src4))>;
  def : Pat<(_.VT (vselect_mask _.KRCWM:$mask,
                   (OpNode _.RC:$src2, (bitconvert (_.LdFrag addr:$src3)),
                    _.RC:$src1, (i8 timm:$src4)),
                   _.RC:$src1)),
            (!cast<Instruction>(Name#_.ZSuffix#rmik) _.RC:$src1, _.KRCWM:$mask,
             _.RC:$src2, addr:$src3, (VPTERNLOG231_imm8 timm:$src4))>;
  def : Pat<(_.VT (vselect_mask _.KRCWM:$mask,
                   (OpNode (bitconvert (_.LdFrag addr:$src3)),
                    _.RC:$src1, _.RC:$src2, (i8 timm:$src4)),
                   _.RC:$src1)),
            (!cast<Instruction>(Name#_.ZSuffix#rmik) _.RC:$src1, _.KRCWM:$mask,
             _.RC:$src2, addr:$src3, (VPTERNLOG312_imm8 timm:$src4))>;

  // Additional patterns for matching zero masking with broadcasts in other
  // positions.
  def : Pat<(_.VT (vselect_mask _.KRCWM:$mask,
                   (OpNode (_.BroadcastLdFrag addr:$src3),
                    _.RC:$src2, _.RC:$src1, (i8 timm:$src4)),
                   _.ImmAllZerosV)),
            (!cast<Instruction>(Name#_.ZSuffix#rmbikz) _.RC:$src1,
             _.KRCWM:$mask, _.RC:$src2, addr:$src3,
             (VPTERNLOG321_imm8 timm:$src4))>;
  def : Pat<(_.VT (vselect_mask _.KRCWM:$mask,
                   (OpNode _.RC:$src1,
                    (_.BroadcastLdFrag addr:$src3),
                    _.RC:$src2, (i8 timm:$src4)),
                   _.ImmAllZerosV)),
            (!cast<Instruction>(Name#_.ZSuffix#rmbikz) _.RC:$src1,
             _.KRCWM:$mask, _.RC:$src2, addr:$src3,
             (VPTERNLOG132_imm8 timm:$src4))>;

  // Additional patterns for matching masked broadcasts with different
  // operand orders.
  def : Pat<(_.VT (vselect_mask _.KRCWM:$mask,
                   (OpNode _.RC:$src1, (_.BroadcastLdFrag addr:$src3),
                    _.RC:$src2, (i8 timm:$src4)),
                   _.RC:$src1)),
            (!cast<Instruction>(Name#_.ZSuffix#rmbik) _.RC:$src1, _.KRCWM:$mask,
             _.RC:$src2, addr:$src3, (VPTERNLOG132_imm8 timm:$src4))>;
  def : Pat<(_.VT (vselect_mask _.KRCWM:$mask,
                   (OpNode (_.BroadcastLdFrag addr:$src3),
                    _.RC:$src2, _.RC:$src1, (i8 timm:$src4)),
                   _.RC:$src1)),
            (!cast<Instruction>(Name#_.ZSuffix#rmbik) _.RC:$src1, _.KRCWM:$mask,
             _.RC:$src2, addr:$src3, (VPTERNLOG321_imm8 timm:$src4))>;
  def : Pat<(_.VT (vselect_mask _.KRCWM:$mask,
                   (OpNode _.RC:$src2, _.RC:$src1,
                    (_.BroadcastLdFrag addr:$src3),
                    (i8 timm:$src4)), _.RC:$src1)),
            (!cast<Instruction>(Name#_.ZSuffix#rmbik) _.RC:$src1, _.KRCWM:$mask,
             _.RC:$src2, addr:$src3, (VPTERNLOG213_imm8 timm:$src4))>;
  def : Pat<(_.VT (vselect_mask _.KRCWM:$mask,
                   (OpNode _.RC:$src2,
                    (_.BroadcastLdFrag addr:$src3),
                    _.RC:$src1, (i8 timm:$src4)),
                   _.RC:$src1)),
            (!cast<Instruction>(Name#_.ZSuffix#rmbik) _.RC:$src1, _.KRCWM:$mask,
             _.RC:$src2, addr:$src3, (VPTERNLOG231_imm8 timm:$src4))>;
  def : Pat<(_.VT (vselect_mask _.KRCWM:$mask,
                   (OpNode (_.BroadcastLdFrag addr:$src3),
                    _.RC:$src1, _.RC:$src2, (i8 timm:$src4)),
                   _.RC:$src1)),
            (!cast<Instruction>(Name#_.ZSuffix#rmbik) _.RC:$src1, _.KRCWM:$mask,
             _.RC:$src2, addr:$src3, (VPTERNLOG312_imm8 timm:$src4))>;
}

multiclass avx512_common_ternlog<string OpcodeStr, X86SchedWriteWidths sched,
                                 AVX512VLVectorVTInfo _> {
  let Predicates = [HasAVX512] in
    defm Z    : avx512_ternlog<0x25, OpcodeStr, X86vpternlog, sched.ZMM,
                               _.info512, NAME>, EVEX_V512;
  let Predicates = [HasAVX512, HasVLX] in {
    defm Z128 : avx512_ternlog<0x25, OpcodeStr, X86vpternlog, sched.XMM,
                               _.info128, NAME>, EVEX_V128;
    defm Z256 : avx512_ternlog<0x25, OpcodeStr, X86vpternlog, sched.YMM,
                               _.info256, NAME>, EVEX_V256;
  }
}

defm VPTERNLOGD : avx512_common_ternlog<"vpternlogd", SchedWriteVecALU,
                                        avx512vl_i32_info>;
defm VPTERNLOGQ : avx512_common_ternlog<"vpternlogq", SchedWriteVecALU,
                                        avx512vl_i64_info>, REX_W;

// Patterns to implement vnot using vpternlog instead of creating all ones
// using pcmpeq or vpternlog and then xoring with that. The value 15 is chosen
// so that the result is only dependent on src0. But we use the same source
// for all operands to prevent a false dependency.
// TODO: We should maybe have a more generalized algorithm for folding to
// vpternlog.
let Predicates = [HasAVX512] in {
  def : Pat<(v64i8 (vnot VR512:$src)),
            (VPTERNLOGQZrri VR512:$src, VR512:$src, VR512:$src, (i8 15))>;
  def : Pat<(v32i16 (vnot VR512:$src)),
            (VPTERNLOGQZrri VR512:$src, VR512:$src, VR512:$src, (i8 15))>;
  def : Pat<(v16i32 (vnot VR512:$src)),
            (VPTERNLOGQZrri VR512:$src, VR512:$src, VR512:$src, (i8 15))>;
  def : Pat<(v8i64 (vnot VR512:$src)),
            (VPTERNLOGQZrri VR512:$src, VR512:$src, VR512:$src, (i8 15))>;
}

let Predicates = [HasAVX512, NoVLX, HasEVEX512] in {
  def : Pat<(v16i8 (vnot VR128X:$src)),
            (EXTRACT_SUBREG
             (VPTERNLOGQZrri
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
              (i8 15)), sub_xmm)>;
  def : Pat<(v8i16 (vnot VR128X:$src)),
            (EXTRACT_SUBREG
             (VPTERNLOGQZrri
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
              (i8 15)), sub_xmm)>;
  def : Pat<(v4i32 (vnot VR128X:$src)),
            (EXTRACT_SUBREG
             (VPTERNLOGQZrri
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
              (i8 15)), sub_xmm)>;
  def : Pat<(v2i64 (vnot VR128X:$src)),
            (EXTRACT_SUBREG
             (VPTERNLOGQZrri
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
              (i8 15)), sub_xmm)>;

  def : Pat<(v32i8 (vnot VR256X:$src)),
            (EXTRACT_SUBREG
             (VPTERNLOGQZrri
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src, sub_ymm),
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src, sub_ymm),
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src, sub_ymm),
              (i8 15)), sub_ymm)>;
  def : Pat<(v16i16 (vnot VR256X:$src)),
            (EXTRACT_SUBREG
             (VPTERNLOGQZrri
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src, sub_ymm),
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src, sub_ymm),
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src, sub_ymm),
              (i8 15)), sub_ymm)>;
  def : Pat<(v8i32 (vnot VR256X:$src)),
            (EXTRACT_SUBREG
             (VPTERNLOGQZrri
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src, sub_ymm),
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src, sub_ymm),
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src, sub_ymm),
              (i8 15)), sub_ymm)>;
  def : Pat<(v4i64 (vnot VR256X:$src)),
            (EXTRACT_SUBREG
             (VPTERNLOGQZrri
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src, sub_ymm),
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src, sub_ymm),
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src, sub_ymm),
              (i8 15)), sub_ymm)>;
}

let Predicates = [HasVLX] in {
  def : Pat<(v16i8 (vnot VR128X:$src)),
            (VPTERNLOGQZ128rri VR128X:$src, VR128X:$src, VR128X:$src, (i8 15))>;
  def : Pat<(v8i16 (vnot VR128X:$src)),
            (VPTERNLOGQZ128rri VR128X:$src, VR128X:$src, VR128X:$src, (i8 15))>;
  def : Pat<(v4i32 (vnot VR128X:$src)),
            (VPTERNLOGQZ128rri VR128X:$src, VR128X:$src, VR128X:$src, (i8 15))>;
  def : Pat<(v2i64 (vnot VR128X:$src)),
            (VPTERNLOGQZ128rri VR128X:$src, VR128X:$src, VR128X:$src, (i8 15))>;

  def : Pat<(v32i8 (vnot VR256X:$src)),
            (VPTERNLOGQZ256rri VR256X:$src, VR256X:$src, VR256X:$src, (i8 15))>;
  def : Pat<(v16i16 (vnot VR256X:$src)),
            (VPTERNLOGQZ256rri VR256X:$src, VR256X:$src, VR256X:$src, (i8 15))>;
  def : Pat<(v8i32 (vnot VR256X:$src)),
            (VPTERNLOGQZ256rri VR256X:$src, VR256X:$src, VR256X:$src, (i8 15))>;
  def : Pat<(v4i64 (vnot VR256X:$src)),
            (VPTERNLOGQZ256rri VR256X:$src, VR256X:$src, VR256X:$src, (i8 15))>;
}

//===----------------------------------------------------------------------===//
// AVX-512 - FixupImm
//===----------------------------------------------------------------------===//

multiclass avx512_fixupimm_packed<bits<8> opc, string OpcodeStr,
                                  X86FoldableSchedWrite sched, X86VectorVTInfo _,
                                  X86VectorVTInfo TblVT>{
  let Constraints = "$src1 = $dst", ExeDomain = _.ExeDomain,
      Uses = [MXCSR], mayRaiseFPException = 1 in {
    defm rri : AVX512_maskable_3src<opc, MRMSrcReg, _, (outs _.RC:$dst),
                        (ins _.RC:$src2, _.RC:$src3, i32u8imm:$src4),
                         OpcodeStr#_.Suffix, "$src4, $src3, $src2", "$src2, $src3, $src4",
                        (X86VFixupimm (_.VT _.RC:$src1),
                                      (_.VT _.RC:$src2),
                                      (TblVT.VT _.RC:$src3),
                                      (i32 timm:$src4))>, Sched<[sched]>;
    defm rmi : AVX512_maskable_3src<opc, MRMSrcMem, _, (outs _.RC:$dst),
                      (ins _.RC:$src2, _.MemOp:$src3, i32u8imm:$src4),
                      OpcodeStr#_.Suffix, "$src4, $src3, $src2", "$src2, $src3, $src4",
                      (X86VFixupimm (_.VT _.RC:$src1),
                                    (_.VT _.RC:$src2),
                                    (TblVT.VT (bitconvert (TblVT.LdFrag addr:$src3))),
                                    (i32 timm:$src4))>,
                      Sched<[sched.Folded, sched.ReadAfterFold]>;
    defm rmbi : AVX512_maskable_3src<opc, MRMSrcMem, _, (outs _.RC:$dst),
                      (ins _.RC:$src2, _.ScalarMemOp:$src3, i32u8imm:$src4),
                    OpcodeStr#_.Suffix, "$src4, ${src3}"#_.BroadcastStr#", $src2",
                    "$src2, ${src3}"#_.BroadcastStr#", $src4",
                      (X86VFixupimm (_.VT _.RC:$src1),
                                    (_.VT _.RC:$src2),
                                    (TblVT.VT (TblVT.BroadcastLdFrag addr:$src3)),
                                    (i32 timm:$src4))>,
                    EVEX_B, Sched<[sched.Folded, sched.ReadAfterFold]>;
  } // Constraints = "$src1 = $dst"
}

multiclass avx512_fixupimm_packed_sae<bits<8> opc, string OpcodeStr,
                                      X86FoldableSchedWrite sched,
                                      X86VectorVTInfo _, X86VectorVTInfo TblVT>
  : avx512_fixupimm_packed<opc, OpcodeStr, sched, _, TblVT> {
let Constraints = "$src1 = $dst", ExeDomain = _.ExeDomain, Uses = [MXCSR] in {
  defm rrib : AVX512_maskable_3src<opc, MRMSrcReg, _, (outs _.RC:$dst),
                      (ins _.RC:$src2, _.RC:$src3, i32u8imm:$src4),
                      OpcodeStr#_.Suffix, "$src4, {sae}, $src3, $src2",
                      "$src2, $src3, {sae}, $src4",
                      (X86VFixupimmSAE (_.VT _.RC:$src1),
                                       (_.VT _.RC:$src2),
                                       (TblVT.VT _.RC:$src3),
                                       (i32 timm:$src4))>,
                      EVEX_B, Sched<[sched]>;
  }
}

multiclass avx512_fixupimm_scalar<bits<8> opc, string OpcodeStr,
                                  X86FoldableSchedWrite sched, X86VectorVTInfo _,
                                  X86VectorVTInfo _src3VT> {
  let Constraints = "$src1 = $dst" , Predicates = [HasAVX512],
      ExeDomain = _.ExeDomain in {
    defm rri : AVX512_maskable_3src_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                      (ins _.RC:$src2, _.RC:$src3, i32u8imm:$src4),
                      OpcodeStr#_.Suffix, "$src4, $src3, $src2", "$src2, $src3, $src4",
                      (X86VFixupimms (_.VT _.RC:$src1),
                                     (_.VT _.RC:$src2),
                                     (_src3VT.VT _src3VT.RC:$src3),
                                     (i32 timm:$src4))>, Sched<[sched]>, SIMD_EXC;
    let Uses = [MXCSR] in
    defm rrib : AVX512_maskable_3src_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                      (ins _.RC:$src2, _.RC:$src3, i32u8imm:$src4),
                      OpcodeStr#_.Suffix, "$src4, {sae}, $src3, $src2",
                      "$src2, $src3, {sae}, $src4",
                      (X86VFixupimmSAEs (_.VT _.RC:$src1),
                                        (_.VT _.RC:$src2),
                                        (_src3VT.VT _src3VT.RC:$src3),
                                        (i32 timm:$src4))>,
                      EVEX_B, Sched<[sched.Folded, sched.ReadAfterFold]>;
    defm rmi : AVX512_maskable_3src_scalar<opc, MRMSrcMem, _, (outs _.RC:$dst),
                     (ins _.RC:$src2, _.ScalarMemOp:$src3, i32u8imm:$src4),
                     OpcodeStr#_.Suffix, "$src4, $src3, $src2", "$src2, $src3, $src4",
                     (X86VFixupimms (_.VT _.RC:$src1),
                                    (_.VT _.RC:$src2),
                                    (_src3VT.VT (scalar_to_vector
                                              (_src3VT.ScalarLdFrag addr:$src3))),
                                    (i32 timm:$src4))>,
                     Sched<[sched.Folded, sched.ReadAfterFold]>, SIMD_EXC;
  }
}

multiclass avx512_fixupimm_packed_all<X86SchedWriteWidths sched,
                                      AVX512VLVectorVTInfo _Vec,
                                      AVX512VLVectorVTInfo _Tbl> {
  let Predicates = [HasAVX512] in
    defm Z    : avx512_fixupimm_packed_sae<0x54, "vfixupimm", sched.ZMM,
                                _Vec.info512, _Tbl.info512>, AVX512AIi8Base,
                                EVEX, VVVV, EVEX_V512;
  let Predicates = [HasAVX512, HasVLX] in {
    defm Z128 : avx512_fixupimm_packed<0x54, "vfixupimm", sched.XMM,
                            _Vec.info128, _Tbl.info128>, AVX512AIi8Base,
                            EVEX, VVVV, EVEX_V128;
    defm Z256 : avx512_fixupimm_packed<0x54, "vfixupimm", sched.YMM,
                            _Vec.info256, _Tbl.info256>, AVX512AIi8Base,
                            EVEX, VVVV, EVEX_V256;
  }
}

defm VFIXUPIMMSSZ : avx512_fixupimm_scalar<0x55, "vfixupimm",
                                           SchedWriteFAdd.Scl, f32x_info, v4i32x_info>,
                          AVX512AIi8Base, VEX_LIG, EVEX, VVVV, EVEX_CD8<32, CD8VT1>;
defm VFIXUPIMMSDZ : avx512_fixupimm_scalar<0x55, "vfixupimm",
                                           SchedWriteFAdd.Scl, f64x_info, v2i64x_info>,
                          AVX512AIi8Base, VEX_LIG, EVEX, VVVV, EVEX_CD8<64, CD8VT1>, REX_W;
defm VFIXUPIMMPS : avx512_fixupimm_packed_all<SchedWriteFAdd, avx512vl_f32_info,
                         avx512vl_i32_info>, EVEX_CD8<32, CD8VF>;
defm VFIXUPIMMPD : avx512_fixupimm_packed_all<SchedWriteFAdd, avx512vl_f64_info,
                         avx512vl_i64_info>, EVEX_CD8<64, CD8VF>, REX_W;

// Patterns used to select SSE scalar fp arithmetic instructions from
// either:
//
// (1) a scalar fp operation followed by a blend
//
// The effect is that the backend no longer emits unnecessary vector
// insert instructions immediately after SSE scalar fp instructions
// like addss or mulss.
//
// For example, given the following code:
//   __m128 foo(__m128 A, __m128 B) {
//     A[0] += B[0];
//     return A;
//   }
//
// Previously we generated:
//   addss %xmm0, %xmm1
//   movss %xmm1, %xmm0
//
// We now generate:
//   addss %xmm1, %xmm0
//
// (2) a vector packed single/double fp operation followed by a vector insert
//
// The effect is that the backend converts the packed fp instruction
// followed by a vector insert into a single SSE scalar fp instruction.
//
// For example, given the following code:
//   __m128 foo(__m128 A, __m128 B) {
//     __m128 C = A + B;
//     return (__m128) {c[0], a[1], a[2], a[3]};
//   }
//
// Previously we generated:
//   addps %xmm0, %xmm1
//   movss %xmm1, %xmm0
//
// We now generate:
//   addss %xmm1, %xmm0

// TODO: Some canonicalization in lowering would simplify the number of
// patterns we have to try to match.
multiclass AVX512_scalar_math_fp_patterns<SDPatternOperator Op, SDNode MaskedOp,
                                          string OpcPrefix, SDNode MoveNode,
                                          X86VectorVTInfo _, PatLeaf ZeroFP> {
  let Predicates = [HasAVX512] in {
    // extracted scalar math op with insert via movss
    def : Pat<(MoveNode
               (_.VT VR128X:$dst),
               (_.VT (scalar_to_vector
                      (Op (_.EltVT (extractelt (_.VT VR128X:$dst), (iPTR 0))),
                          _.FRC:$src)))),
              (!cast<Instruction>("V"#OpcPrefix#"Zrr_Int") _.VT:$dst,
               (_.VT (COPY_TO_REGCLASS _.FRC:$src, VR128X)))>;
    def : Pat<(MoveNode
               (_.VT VR128X:$dst),
               (_.VT (scalar_to_vector
                      (Op (_.EltVT (extractelt (_.VT VR128X:$dst), (iPTR 0))),
                          (_.ScalarLdFrag addr:$src))))),
              (!cast<Instruction>("V"#OpcPrefix#"Zrm_Int") _.VT:$dst, addr:$src)>;

    // extracted masked scalar math op with insert via movss
    def : Pat<(MoveNode (_.VT VR128X:$src1),
               (scalar_to_vector
                (X86selects_mask VK1WM:$mask,
                            (MaskedOp (_.EltVT
                                       (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                                      _.FRC:$src2),
                            _.FRC:$src0))),
              (!cast<Instruction>("V"#OpcPrefix#"Zrr_Intk")
               (_.VT (COPY_TO_REGCLASS _.FRC:$src0, VR128X)),
               VK1WM:$mask, _.VT:$src1,
               (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)))>;
    def : Pat<(MoveNode (_.VT VR128X:$src1),
               (scalar_to_vector
                (X86selects_mask VK1WM:$mask,
                            (MaskedOp (_.EltVT
                                       (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                                      (_.ScalarLdFrag addr:$src2)),
                            _.FRC:$src0))),
              (!cast<Instruction>("V"#OpcPrefix#"Zrm_Intk")
               (_.VT (COPY_TO_REGCLASS _.FRC:$src0, VR128X)),
               VK1WM:$mask, _.VT:$src1, addr:$src2)>;

    // extracted masked scalar math op with insert via movss
    def : Pat<(MoveNode (_.VT VR128X:$src1),
               (scalar_to_vector
                (X86selects_mask VK1WM:$mask,
                            (MaskedOp (_.EltVT
                                       (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                                      _.FRC:$src2), (_.EltVT ZeroFP)))),
      (!cast<I>("V"#OpcPrefix#"Zrr_Intkz")
          VK1WM:$mask, _.VT:$src1,
          (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)))>;
    def : Pat<(MoveNode (_.VT VR128X:$src1),
               (scalar_to_vector
                (X86selects_mask VK1WM:$mask,
                            (MaskedOp (_.EltVT
                                       (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                                      (_.ScalarLdFrag addr:$src2)), (_.EltVT ZeroFP)))),
      (!cast<I>("V"#OpcPrefix#"Zrm_Intkz") VK1WM:$mask, _.VT:$src1, addr:$src2)>;
  }
}

defm : AVX512_scalar_math_fp_patterns<any_fadd, fadd, "ADDSS", X86Movss, v4f32x_info, fp32imm0>;
defm : AVX512_scalar_math_fp_patterns<any_fsub, fsub, "SUBSS", X86Movss, v4f32x_info, fp32imm0>;
defm : AVX512_scalar_math_fp_patterns<any_fmul, fmul, "MULSS", X86Movss, v4f32x_info, fp32imm0>;
defm : AVX512_scalar_math_fp_patterns<any_fdiv, fdiv, "DIVSS", X86Movss, v4f32x_info, fp32imm0>;

defm : AVX512_scalar_math_fp_patterns<any_fadd, fadd, "ADDSD", X86Movsd, v2f64x_info, fp64imm0>;
defm : AVX512_scalar_math_fp_patterns<any_fsub, fsub, "SUBSD", X86Movsd, v2f64x_info, fp64imm0>;
defm : AVX512_scalar_math_fp_patterns<any_fmul, fmul, "MULSD", X86Movsd, v2f64x_info, fp64imm0>;
defm : AVX512_scalar_math_fp_patterns<any_fdiv, fdiv, "DIVSD", X86Movsd, v2f64x_info, fp64imm0>;

defm : AVX512_scalar_math_fp_patterns<any_fadd, fadd, "ADDSH", X86Movsh, v8f16x_info, fp16imm0>;
defm : AVX512_scalar_math_fp_patterns<any_fsub, fsub, "SUBSH", X86Movsh, v8f16x_info, fp16imm0>;
defm : AVX512_scalar_math_fp_patterns<any_fmul, fmul, "MULSH", X86Movsh, v8f16x_info, fp16imm0>;
defm : AVX512_scalar_math_fp_patterns<any_fdiv, fdiv, "DIVSH", X86Movsh, v8f16x_info, fp16imm0>;

multiclass AVX512_scalar_unary_math_patterns<SDPatternOperator OpNode, string OpcPrefix,
                                             SDNode Move, X86VectorVTInfo _> {
  let Predicates = [HasAVX512] in {
    def : Pat<(_.VT (Move _.VT:$dst,
                     (scalar_to_vector (OpNode (extractelt _.VT:$src, 0))))),
              (!cast<Instruction>("V"#OpcPrefix#"Zr_Int") _.VT:$dst, _.VT:$src)>;
  }
}

defm : AVX512_scalar_unary_math_patterns<any_fsqrt, "SQRTSS", X86Movss, v4f32x_info>;
defm : AVX512_scalar_unary_math_patterns<any_fsqrt, "SQRTSD", X86Movsd, v2f64x_info>;
defm : AVX512_scalar_unary_math_patterns<any_fsqrt, "SQRTSH", X86Movsh, v8f16x_info>;

//===----------------------------------------------------------------------===//
// AES instructions
//===----------------------------------------------------------------------===//

multiclass avx512_vaes<bits<8> Op, string OpStr, string IntPrefix> {
  let Predicates = [HasVLX, HasVAES] in {
    defm Z128 : AESI_binop_rm_int<Op, OpStr,
                                  !cast<Intrinsic>(IntPrefix),
                                  loadv2i64, 0, VR128X, i128mem>,
                  EVEX, VVVV, EVEX_CD8<64, CD8VF>, EVEX_V128, WIG;
    defm Z256 : AESI_binop_rm_int<Op, OpStr,
                                  !cast<Intrinsic>(IntPrefix#"_256"),
                                  loadv4i64, 0, VR256X, i256mem>,
                  EVEX, VVVV, EVEX_CD8<64, CD8VF>, EVEX_V256, WIG;
    }
    let Predicates = [HasAVX512, HasVAES] in
    defm Z    : AESI_binop_rm_int<Op, OpStr,
                                  !cast<Intrinsic>(IntPrefix#"_512"),
                                  loadv8i64, 0, VR512, i512mem>,
                  EVEX, VVVV, EVEX_CD8<64, CD8VF>, EVEX_V512, WIG;
}

defm VAESENC      : avx512_vaes<0xDC, "vaesenc", "int_x86_aesni_aesenc">;
defm VAESENCLAST  : avx512_vaes<0xDD, "vaesenclast", "int_x86_aesni_aesenclast">;
defm VAESDEC      : avx512_vaes<0xDE, "vaesdec", "int_x86_aesni_aesdec">;
defm VAESDECLAST  : avx512_vaes<0xDF, "vaesdeclast", "int_x86_aesni_aesdeclast">;

//===----------------------------------------------------------------------===//
// PCLMUL instructions - Carry less multiplication
//===----------------------------------------------------------------------===//

let Predicates = [HasAVX512, HasVPCLMULQDQ] in
defm VPCLMULQDQZ : vpclmulqdq<VR512, i512mem, loadv8i64, int_x86_pclmulqdq_512>,
                              EVEX, VVVV, EVEX_V512, EVEX_CD8<64, CD8VF>, WIG;

let Predicates = [HasVLX, HasVPCLMULQDQ] in {
defm VPCLMULQDQZ128 : vpclmulqdq<VR128X, i128mem, loadv2i64, int_x86_pclmulqdq>,
                              EVEX, VVVV, EVEX_V128, EVEX_CD8<64, CD8VF>, WIG;

defm VPCLMULQDQZ256: vpclmulqdq<VR256X, i256mem, loadv4i64,
                                int_x86_pclmulqdq_256>, EVEX, VVVV, EVEX_V256,
                                EVEX_CD8<64, CD8VF>, WIG;
}

// Aliases
defm : vpclmulqdq_aliases<"VPCLMULQDQZ", VR512, i512mem>;
defm : vpclmulqdq_aliases<"VPCLMULQDQZ128", VR128X, i128mem>;
defm : vpclmulqdq_aliases<"VPCLMULQDQZ256", VR256X, i256mem>;

//===----------------------------------------------------------------------===//
// VBMI2
//===----------------------------------------------------------------------===//

multiclass VBMI2_shift_var_rm<bits<8> Op, string OpStr, SDNode OpNode,
                              X86FoldableSchedWrite sched, X86VectorVTInfo VTI> {
  let Constraints = "$src1 = $dst",
      ExeDomain   = VTI.ExeDomain in {
    defm r:   AVX512_maskable_3src<Op, MRMSrcReg, VTI, (outs VTI.RC:$dst),
                (ins VTI.RC:$src2, VTI.RC:$src3), OpStr,
                "$src3, $src2", "$src2, $src3",
                (VTI.VT (OpNode VTI.RC:$src1, VTI.RC:$src2, VTI.RC:$src3))>,
                T8, PD, EVEX, VVVV, Sched<[sched]>;
    defm m:   AVX512_maskable_3src<Op, MRMSrcMem, VTI, (outs VTI.RC:$dst),
                (ins VTI.RC:$src2, VTI.MemOp:$src3), OpStr,
                "$src3, $src2", "$src2, $src3",
                (VTI.VT (OpNode VTI.RC:$src1, VTI.RC:$src2,
                        (VTI.VT (VTI.LdFrag addr:$src3))))>,
                T8, PD, EVEX, VVVV,
                Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

multiclass VBMI2_shift_var_rmb<bits<8> Op, string OpStr, SDNode OpNode,
                               X86FoldableSchedWrite sched, X86VectorVTInfo VTI>
         : VBMI2_shift_var_rm<Op, OpStr, OpNode, sched, VTI> {
  let Constraints = "$src1 = $dst",
      ExeDomain   = VTI.ExeDomain in
  defm mb:  AVX512_maskable_3src<Op, MRMSrcMem, VTI, (outs VTI.RC:$dst),
              (ins VTI.RC:$src2, VTI.ScalarMemOp:$src3), OpStr,
              "${src3}"#VTI.BroadcastStr#", $src2",
              "$src2, ${src3}"#VTI.BroadcastStr,
              (OpNode VTI.RC:$src1, VTI.RC:$src2,
               (VTI.VT (VTI.BroadcastLdFrag addr:$src3)))>,
              T8, PD, EVEX, VVVV, EVEX_B,
              Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass VBMI2_shift_var_rm_common<bits<8> Op, string OpStr, SDNode OpNode,
                                     X86SchedWriteWidths sched, AVX512VLVectorVTInfo VTI> {
  let Predicates = [HasVBMI2] in
  defm Z      : VBMI2_shift_var_rm<Op, OpStr, OpNode, sched.ZMM, VTI.info512>,
                                   EVEX_V512;
  let Predicates = [HasVBMI2, HasVLX] in {
    defm Z256 : VBMI2_shift_var_rm<Op, OpStr, OpNode, sched.YMM, VTI.info256>,
                                   EVEX_V256;
    defm Z128 : VBMI2_shift_var_rm<Op, OpStr, OpNode, sched.XMM, VTI.info128>,
                                   EVEX_V128;
  }
}

multiclass VBMI2_shift_var_rmb_common<bits<8> Op, string OpStr, SDNode OpNode,
                                      X86SchedWriteWidths sched, AVX512VLVectorVTInfo VTI> {
  let Predicates = [HasVBMI2] in
  defm Z      : VBMI2_shift_var_rmb<Op, OpStr, OpNode, sched.ZMM, VTI.info512>,
                                    EVEX_V512;
  let Predicates = [HasVBMI2, HasVLX] in {
    defm Z256 : VBMI2_shift_var_rmb<Op, OpStr, OpNode, sched.YMM, VTI.info256>,
                                    EVEX_V256;
    defm Z128 : VBMI2_shift_var_rmb<Op, OpStr, OpNode, sched.XMM, VTI.info128>,
                                    EVEX_V128;
  }
}
multiclass VBMI2_shift_var<bits<8> wOp, bits<8> dqOp, string Prefix,
                           SDNode OpNode, X86SchedWriteWidths sched> {
  defm W : VBMI2_shift_var_rm_common<wOp, Prefix#"w", OpNode, sched,
             avx512vl_i16_info>, REX_W, EVEX_CD8<16, CD8VF>;
  defm D : VBMI2_shift_var_rmb_common<dqOp, Prefix#"d", OpNode, sched,
             avx512vl_i32_info>, EVEX_CD8<32, CD8VF>;
  defm Q : VBMI2_shift_var_rmb_common<dqOp, Prefix#"q", OpNode, sched,
             avx512vl_i64_info>, REX_W, EVEX_CD8<64, CD8VF>;
}

multiclass VBMI2_shift_imm<bits<8> wOp, bits<8> dqOp, string Prefix,
                           SDNode OpNode, X86SchedWriteWidths sched> {
  defm W : avx512_common_3Op_rm_imm8<wOp, OpNode, Prefix#"w", sched,
             avx512vl_i16_info, avx512vl_i16_info, HasVBMI2>,
             REX_W, EVEX_CD8<16, CD8VF>;
  defm D : avx512_common_3Op_imm8<Prefix#"d", avx512vl_i32_info, dqOp,
             OpNode, sched, HasVBMI2>, AVX512AIi8Base, EVEX, VVVV, EVEX_CD8<32, CD8VF>;
  defm Q : avx512_common_3Op_imm8<Prefix#"q", avx512vl_i64_info, dqOp, OpNode,
             sched, HasVBMI2>, AVX512AIi8Base, EVEX, VVVV, EVEX_CD8<64, CD8VF>, REX_W;
}

// Concat & Shift
defm VPSHLDV : VBMI2_shift_var<0x70, 0x71, "vpshldv", X86VShldv, SchedWriteVecIMul>;
defm VPSHRDV : VBMI2_shift_var<0x72, 0x73, "vpshrdv", X86VShrdv, SchedWriteVecIMul>;
defm VPSHLD  : VBMI2_shift_imm<0x70, 0x71, "vpshld", X86VShld, SchedWriteVecIMul>;
defm VPSHRD  : VBMI2_shift_imm<0x72, 0x73, "vpshrd", X86VShrd, SchedWriteVecIMul>;

// Compress
defm VPCOMPRESSB : compress_by_elt_width<0x63, "vpcompressb", WriteVarShuffle256,
                                         avx512vl_i8_info, HasVBMI2>, EVEX;
defm VPCOMPRESSW : compress_by_elt_width <0x63, "vpcompressw", WriteVarShuffle256,
                                          avx512vl_i16_info, HasVBMI2>, EVEX, REX_W;
// Expand
defm VPEXPANDB : expand_by_elt_width <0x62, "vpexpandb", WriteVarShuffle256,
                                      avx512vl_i8_info, HasVBMI2>, EVEX;
defm VPEXPANDW : expand_by_elt_width <0x62, "vpexpandw", WriteVarShuffle256,
                                      avx512vl_i16_info, HasVBMI2>, EVEX, REX_W;

//===----------------------------------------------------------------------===//
// VNNI
//===----------------------------------------------------------------------===//

let Constraints = "$src1 = $dst" in
multiclass VNNI_rmb<bits<8> Op, string OpStr, SDNode OpNode,
                    X86FoldableSchedWrite sched, X86VectorVTInfo VTI,
                    bit IsCommutable> {
  let ExeDomain = VTI.ExeDomain in {
  defm r  :   AVX512_maskable_3src<Op, MRMSrcReg, VTI, (outs VTI.RC:$dst),
                                   (ins VTI.RC:$src2, VTI.RC:$src3), OpStr,
                                   "$src3, $src2", "$src2, $src3",
                                   (VTI.VT (OpNode VTI.RC:$src1,
                                            VTI.RC:$src2, VTI.RC:$src3)),
                                   IsCommutable, IsCommutable>,
                                   EVEX, VVVV, T8, PD, Sched<[sched]>;
  defm m  :   AVX512_maskable_3src<Op, MRMSrcMem, VTI, (outs VTI.RC:$dst),
                                   (ins VTI.RC:$src2, VTI.MemOp:$src3), OpStr,
                                   "$src3, $src2", "$src2, $src3",
                                   (VTI.VT (OpNode VTI.RC:$src1, VTI.RC:$src2,
                                            (VTI.VT (VTI.LdFrag addr:$src3))))>,
                                   EVEX, VVVV, EVEX_CD8<32, CD8VF>, T8, PD,
                                   Sched<[sched.Folded, sched.ReadAfterFold,
                                          sched.ReadAfterFold]>;
  defm mb :   AVX512_maskable_3src<Op, MRMSrcMem, VTI, (outs VTI.RC:$dst),
                                   (ins VTI.RC:$src2, VTI.ScalarMemOp:$src3),
                                   OpStr, "${src3}"#VTI.BroadcastStr#", $src2",
                                   "$src2, ${src3}"#VTI.BroadcastStr,
                                   (OpNode VTI.RC:$src1, VTI.RC:$src2,
                                    (VTI.VT (VTI.BroadcastLdFrag addr:$src3)))>,
                                   EVEX, VVVV, EVEX_CD8<32, CD8VF>, EVEX_B,
                                   T8, PD, Sched<[sched.Folded, sched.ReadAfterFold,
                                                sched.ReadAfterFold]>;
  }
}

multiclass VNNI_common<bits<8> Op, string OpStr, SDNode OpNode,
                       X86SchedWriteWidths sched, bit IsCommutable> {
  let Predicates = [HasVNNI] in
  defm Z      :   VNNI_rmb<Op, OpStr, OpNode, sched.ZMM, v16i32_info,
                           IsCommutable>, EVEX_V512;
  let Predicates = [HasVNNI, HasVLX] in {
    defm Z256 :   VNNI_rmb<Op, OpStr, OpNode, sched.YMM, v8i32x_info,
                           IsCommutable>, EVEX_V256;
    defm Z128 :   VNNI_rmb<Op, OpStr, OpNode, sched.XMM, v4i32x_info,
                           IsCommutable>, EVEX_V128;
  }
}

// FIXME: Is there a better scheduler class for VPDP?
defm VPDPBUSD   : VNNI_common<0x50, "vpdpbusd", X86Vpdpbusd, SchedWriteVecIMul, 0>;
defm VPDPBUSDS  : VNNI_common<0x51, "vpdpbusds", X86Vpdpbusds, SchedWriteVecIMul, 0>;
defm VPDPWSSD   : VNNI_common<0x52, "vpdpwssd", X86Vpdpwssd, SchedWriteVecIMul, 1>;
defm VPDPWSSDS  : VNNI_common<0x53, "vpdpwssds", X86Vpdpwssds, SchedWriteVecIMul, 1>;

// Patterns to match VPDPWSSD from existing instructions/intrinsics.
let Predicates = [HasVNNI] in {
  def : Pat<(v16i32 (add VR512:$src1,
                         (X86vpmaddwd_su VR512:$src2, VR512:$src3))),
            (VPDPWSSDZr VR512:$src1, VR512:$src2, VR512:$src3)>;
  def : Pat<(v16i32 (add VR512:$src1,
                         (X86vpmaddwd_su VR512:$src2, (load addr:$src3)))),
            (VPDPWSSDZm VR512:$src1, VR512:$src2, addr:$src3)>;
}
let Predicates = [HasVNNI,HasVLX] in {
  def : Pat<(v8i32 (add VR256X:$src1,
                        (X86vpmaddwd_su VR256X:$src2, VR256X:$src3))),
            (VPDPWSSDZ256r VR256X:$src1, VR256X:$src2, VR256X:$src3)>;
  def : Pat<(v8i32 (add VR256X:$src1,
                        (X86vpmaddwd_su VR256X:$src2, (load addr:$src3)))),
            (VPDPWSSDZ256m VR256X:$src1, VR256X:$src2, addr:$src3)>;
  def : Pat<(v4i32 (add VR128X:$src1,
                        (X86vpmaddwd_su VR128X:$src2, VR128X:$src3))),
            (VPDPWSSDZ128r VR128X:$src1, VR128X:$src2, VR128X:$src3)>;
  def : Pat<(v4i32 (add VR128X:$src1,
                        (X86vpmaddwd_su VR128X:$src2, (load addr:$src3)))),
            (VPDPWSSDZ128m VR128X:$src1, VR128X:$src2, addr:$src3)>;
}

//===----------------------------------------------------------------------===//
// Bit Algorithms
//===----------------------------------------------------------------------===//

// FIXME: Is there a better scheduler class for VPOPCNTB/VPOPCNTW?
defm VPOPCNTB : avx512_unary_rm_vl<0x54, "vpopcntb", ctpop, SchedWriteVecALU,
                                   avx512vl_i8_info, HasBITALG>;
defm VPOPCNTW : avx512_unary_rm_vl<0x54, "vpopcntw", ctpop, SchedWriteVecALU,
                                   avx512vl_i16_info, HasBITALG>, REX_W;

defm : avx512_unary_lowering<"VPOPCNTB", ctpop, avx512vl_i8_info, HasBITALG>;
defm : avx512_unary_lowering<"VPOPCNTW", ctpop, avx512vl_i16_info, HasBITALG>;

multiclass VPSHUFBITQMB_rm<X86FoldableSchedWrite sched, X86VectorVTInfo VTI> {
  defm rr : AVX512_maskable_cmp<0x8F, MRMSrcReg, VTI, (outs VTI.KRC:$dst),
                                (ins VTI.RC:$src1, VTI.RC:$src2),
                                "vpshufbitqmb",
                                "$src2, $src1", "$src1, $src2",
                                (X86Vpshufbitqmb (VTI.VT VTI.RC:$src1),
                                (VTI.VT VTI.RC:$src2)),
                                (X86Vpshufbitqmb_su (VTI.VT VTI.RC:$src1),
                                (VTI.VT VTI.RC:$src2))>, EVEX, VVVV, T8, PD,
                                Sched<[sched]>;
  defm rm : AVX512_maskable_cmp<0x8F, MRMSrcMem, VTI, (outs VTI.KRC:$dst),
                                (ins VTI.RC:$src1, VTI.MemOp:$src2),
                                "vpshufbitqmb",
                                "$src2, $src1", "$src1, $src2",
                                (X86Vpshufbitqmb (VTI.VT VTI.RC:$src1),
                                (VTI.VT (VTI.LdFrag addr:$src2))),
                                (X86Vpshufbitqmb_su (VTI.VT VTI.RC:$src1),
                                (VTI.VT (VTI.LdFrag addr:$src2)))>,
                                EVEX, VVVV, EVEX_CD8<8, CD8VF>, T8, PD,
                                Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass VPSHUFBITQMB_common<X86SchedWriteWidths sched, AVX512VLVectorVTInfo VTI> {
  let Predicates = [HasBITALG] in
  defm Z      : VPSHUFBITQMB_rm<sched.ZMM, VTI.info512>, EVEX_V512;
  let Predicates = [HasBITALG, HasVLX] in {
    defm Z256 : VPSHUFBITQMB_rm<sched.YMM, VTI.info256>, EVEX_V256;
    defm Z128 : VPSHUFBITQMB_rm<sched.XMM, VTI.info128>, EVEX_V128;
  }
}

// FIXME: Is there a better scheduler class for VPSHUFBITQMB?
defm VPSHUFBITQMB : VPSHUFBITQMB_common<SchedWriteVecIMul, avx512vl_i8_info>;

//===----------------------------------------------------------------------===//
// GFNI
//===----------------------------------------------------------------------===//

multiclass GF2P8MULB_avx512_common<bits<8> Op, string OpStr, SDNode OpNode,
                                   X86SchedWriteWidths sched> {
  let Predicates = [HasGFNI, HasAVX512] in
  defm Z      : avx512_binop_rm<Op, OpStr, OpNode, v64i8_info, sched.ZMM, 1>,
                                EVEX_V512;
  let Predicates = [HasGFNI, HasVLX] in {
    defm Z256 : avx512_binop_rm<Op, OpStr, OpNode, v32i8x_info, sched.YMM, 1>,
                                EVEX_V256;
    defm Z128 : avx512_binop_rm<Op, OpStr, OpNode, v16i8x_info, sched.XMM, 1>,
                                EVEX_V128;
  }
}

defm VGF2P8MULB : GF2P8MULB_avx512_common<0xCF, "vgf2p8mulb", X86GF2P8mulb,
                                          SchedWriteVecALU>,
                                          EVEX_CD8<8, CD8VF>, T8;

multiclass GF2P8AFFINE_avx512_rmb_imm<bits<8> Op, string OpStr, SDNode OpNode,
                                      X86FoldableSchedWrite sched, X86VectorVTInfo VTI,
                                      X86VectorVTInfo BcstVTI>
           : avx512_3Op_rm_imm8<Op, OpStr, OpNode, sched, VTI, VTI> {
  let ExeDomain = VTI.ExeDomain in
  defm rmbi : AVX512_maskable<Op, MRMSrcMem, VTI, (outs VTI.RC:$dst),
                (ins VTI.RC:$src1, BcstVTI.ScalarMemOp:$src2, u8imm:$src3),
                OpStr, "$src3, ${src2}"#BcstVTI.BroadcastStr#", $src1",
                "$src1, ${src2}"#BcstVTI.BroadcastStr#", $src3",
                (OpNode (VTI.VT VTI.RC:$src1),
                 (bitconvert (BcstVTI.VT (X86VBroadcastld64 addr:$src2))),
                 (i8 timm:$src3))>, EVEX_B,
                 Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass GF2P8AFFINE_avx512_common<bits<8> Op, string OpStr, SDNode OpNode,
                                     X86SchedWriteWidths sched> {
  let Predicates = [HasGFNI, HasAVX512] in
  defm Z      : GF2P8AFFINE_avx512_rmb_imm<Op, OpStr, OpNode, sched.ZMM,
                                           v64i8_info, v8i64_info>, EVEX_V512;
  let Predicates = [HasGFNI, HasVLX] in {
    defm Z256 : GF2P8AFFINE_avx512_rmb_imm<Op, OpStr, OpNode, sched.YMM,
                                           v32i8x_info, v4i64x_info>, EVEX_V256;
    defm Z128 : GF2P8AFFINE_avx512_rmb_imm<Op, OpStr, OpNode, sched.XMM,
                                           v16i8x_info, v2i64x_info>, EVEX_V128;
  }
}

defm VGF2P8AFFINEINVQB : GF2P8AFFINE_avx512_common<0xCF, "vgf2p8affineinvqb",
                         X86GF2P8affineinvqb, SchedWriteVecIMul>,
                         EVEX, VVVV, EVEX_CD8<8, CD8VF>, REX_W, AVX512AIi8Base;
defm VGF2P8AFFINEQB    : GF2P8AFFINE_avx512_common<0xCE, "vgf2p8affineqb",
                         X86GF2P8affineqb, SchedWriteVecIMul>,
                         EVEX, VVVV, EVEX_CD8<8, CD8VF>, REX_W, AVX512AIi8Base;


//===----------------------------------------------------------------------===//
// AVX5124FMAPS
//===----------------------------------------------------------------------===//

let hasSideEffects = 0, mayLoad = 1, ExeDomain = SSEPackedSingle,
    Constraints = "$src1 = $dst", Uses = [MXCSR], mayRaiseFPException = 1 in {
defm V4FMADDPSrm : AVX512_maskable_3src_in_asm<0x9A, MRMSrcMem, v16f32_info,
                    (outs VR512:$dst), (ins VR512:$src2, f128mem:$src3),
                    "v4fmaddps", "$src3, $src2", "$src2, $src3",
                    []>, EVEX_V512, EVEX, VVVV, T8, XD, EVEX_CD8<32, CD8VQ>,
                    Sched<[SchedWriteFMA.ZMM.Folded]>;

defm V4FNMADDPSrm : AVX512_maskable_3src_in_asm<0xAA, MRMSrcMem, v16f32_info,
                     (outs VR512:$dst), (ins VR512:$src2, f128mem:$src3),
                     "v4fnmaddps", "$src3, $src2", "$src2, $src3",
                     []>, EVEX_V512, EVEX, VVVV, T8, XD, EVEX_CD8<32, CD8VQ>,
                     Sched<[SchedWriteFMA.ZMM.Folded]>;

defm V4FMADDSSrm : AVX512_maskable_3src_in_asm<0x9B, MRMSrcMem, f32x_info,
                    (outs VR128X:$dst), (ins  VR128X:$src2, f128mem:$src3),
                    "v4fmaddss", "$src3, $src2", "$src2, $src3",
                    []>, VEX_LIG, EVEX, VVVV, T8, XD, EVEX_CD8<32, CD8VF>,
                    Sched<[SchedWriteFMA.Scl.Folded]>;

defm V4FNMADDSSrm : AVX512_maskable_3src_in_asm<0xAB, MRMSrcMem, f32x_info,
                     (outs VR128X:$dst), (ins VR128X:$src2, f128mem:$src3),
                     "v4fnmaddss", "$src3, $src2", "$src2, $src3",
                     []>, VEX_LIG, EVEX, VVVV, T8, XD, EVEX_CD8<32, CD8VF>,
                     Sched<[SchedWriteFMA.Scl.Folded]>;
}

//===----------------------------------------------------------------------===//
// AVX5124VNNIW
//===----------------------------------------------------------------------===//

let hasSideEffects = 0, mayLoad = 1, ExeDomain = SSEPackedInt,
    Constraints = "$src1 = $dst" in {
defm VP4DPWSSDrm : AVX512_maskable_3src_in_asm<0x52, MRMSrcMem, v16i32_info,
                    (outs VR512:$dst), (ins VR512:$src2, f128mem:$src3),
                     "vp4dpwssd", "$src3, $src2", "$src2, $src3",
                    []>, EVEX_V512, EVEX, VVVV, T8, XD, EVEX_CD8<32, CD8VQ>,
                    Sched<[SchedWriteFMA.ZMM.Folded]>;

defm VP4DPWSSDSrm : AVX512_maskable_3src_in_asm<0x53, MRMSrcMem, v16i32_info,
                     (outs VR512:$dst), (ins VR512:$src2, f128mem:$src3),
                     "vp4dpwssds", "$src3, $src2", "$src2, $src3",
                     []>, EVEX_V512, EVEX, VVVV, T8, XD, EVEX_CD8<32, CD8VQ>,
                     Sched<[SchedWriteFMA.ZMM.Folded]>;
}

let hasSideEffects = 0 in {
  let mayStore = 1, SchedRW = [WriteFStoreX] in
  def MASKPAIR16STORE : PseudoI<(outs), (ins anymem:$dst, VK16PAIR:$src), []>;
  let mayLoad = 1, SchedRW = [WriteFLoadX] in
  def MASKPAIR16LOAD : PseudoI<(outs VK16PAIR:$dst), (ins anymem:$src), []>;
}

//===----------------------------------------------------------------------===//
// VP2INTERSECT
//===----------------------------------------------------------------------===//

multiclass avx512_vp2intersect_modes<X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  def rr : I<0x68, MRMSrcReg,
                  (outs _.KRPC:$dst),
                  (ins _.RC:$src1, _.RC:$src2),
                  !strconcat("vp2intersect", _.Suffix,
                             "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
                  [(set _.KRPC:$dst, (X86vp2intersect
                            _.RC:$src1, (_.VT _.RC:$src2)))]>,
                  EVEX, VVVV, T8, XD, Sched<[sched]>;

  def rm : I<0x68, MRMSrcMem,
                  (outs _.KRPC:$dst),
                  (ins  _.RC:$src1, _.MemOp:$src2),
                  !strconcat("vp2intersect", _.Suffix,
                             "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
                  [(set _.KRPC:$dst, (X86vp2intersect
                            _.RC:$src1, (_.VT (bitconvert (_.LdFrag addr:$src2)))))]>,
                  EVEX, VVVV, T8, XD, EVEX_CD8<_.EltSize, CD8VF>,
                  Sched<[sched.Folded, sched.ReadAfterFold]>;

  def rmb : I<0x68, MRMSrcMem,
                  (outs _.KRPC:$dst),
                  (ins _.RC:$src1, _.ScalarMemOp:$src2),
                  !strconcat("vp2intersect", _.Suffix, "\t{${src2}", _.BroadcastStr,
                             ", $src1, $dst|$dst, $src1, ${src2}", _.BroadcastStr ,"}"),
                  [(set _.KRPC:$dst, (X86vp2intersect
                             _.RC:$src1, (_.VT (_.BroadcastLdFrag addr:$src2))))]>,
                  EVEX, VVVV, T8, XD, EVEX_B, EVEX_CD8<_.EltSize, CD8VF>,
                  Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass avx512_vp2intersect<X86SchedWriteWidths sched, AVX512VLVectorVTInfo _> {
  let Predicates  = [HasAVX512, HasVP2INTERSECT] in
    defm Z : avx512_vp2intersect_modes<sched.ZMM, _.info512>, EVEX_V512;

  let Predicates = [HasAVX512, HasVP2INTERSECT, HasVLX] in {
    defm Z256 : avx512_vp2intersect_modes<sched.YMM, _.info256>, EVEX_V256;
    defm Z128 : avx512_vp2intersect_modes<sched.XMM, _.info128>, EVEX_V128;
  }
}

let ExeDomain = SSEPackedInt in {
defm VP2INTERSECTD : avx512_vp2intersect<SchedWriteVecALU, avx512vl_i32_info>;
defm VP2INTERSECTQ : avx512_vp2intersect<SchedWriteVecALU, avx512vl_i64_info>, REX_W;
}

multiclass avx512_binop_all2<bits<8> opc, string OpcodeStr,
                             X86SchedWriteWidths sched,
                             AVX512VLVectorVTInfo _SrcVTInfo,
                             AVX512VLVectorVTInfo _DstVTInfo,
                             SDNode OpNode, Predicate prd,
                             bit IsCommutable = 0> {
  let Predicates = [prd] in
    defm NAME#Z : avx512_binop_rm2<opc, OpcodeStr, sched.ZMM, OpNode,
                                   _SrcVTInfo.info512, _DstVTInfo.info512,
                                   _SrcVTInfo.info512, IsCommutable>,
                                   EVEX_V512, EVEX_CD8<32, CD8VF>;
  let Predicates = [HasVLX, prd] in {
    defm NAME#Z256 : avx512_binop_rm2<opc, OpcodeStr, sched.YMM, OpNode,
                                      _SrcVTInfo.info256, _DstVTInfo.info256,
                                      _SrcVTInfo.info256, IsCommutable>,
                                     EVEX_V256, EVEX_CD8<32, CD8VF>;
    defm NAME#Z128 : avx512_binop_rm2<opc, OpcodeStr, sched.XMM, OpNode,
                                      _SrcVTInfo.info128, _DstVTInfo.info128,
                                      _SrcVTInfo.info128, IsCommutable>,
                                      EVEX_V128, EVEX_CD8<32, CD8VF>;
  }
}

let ExeDomain = SSEPackedSingle in
defm VCVTNE2PS2BF16 : avx512_binop_all2<0x72, "vcvtne2ps2bf16",
                                        SchedWriteCvtPD2PS, //FIXME: Should be SchedWriteCvtPS2BF
                                        avx512vl_f32_info, avx512vl_bf16_info,
                                        X86cvtne2ps2bf16, HasBF16, 0>, T8, XD;

// Truncate Float to BFloat16
multiclass avx512_cvtps2bf16<bits<8> opc, string OpcodeStr,
                             X86SchedWriteWidths sched> {
  let ExeDomain = SSEPackedSingle in {
  let Predicates = [HasBF16], Uses = []<Register>, mayRaiseFPException = 0 in {
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, v16bf16x_info, v16f32_info,
                            X86cvtneps2bf16, X86cvtneps2bf16, sched.ZMM>, EVEX_V512;
  }
  let Predicates = [HasBF16, HasVLX] in {
    let Uses = []<Register>, mayRaiseFPException = 0 in {
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, v8bf16x_info, v4f32x_info,
                               null_frag, null_frag, sched.XMM, "{1to4}", "{x}", f128mem,
                               VK4WM>, EVEX_V128;
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, v8bf16x_info, v8f32x_info,
                               X86cvtneps2bf16, X86cvtneps2bf16,
                               sched.YMM, "{1to8}", "{y}">, EVEX_V256;
    }
  } // Predicates = [HasBF16, HasVLX]
  } // ExeDomain = SSEPackedSingle

  def : InstAlias<OpcodeStr#"x\t{$src, $dst|$dst, $src}",
                  (!cast<Instruction>(NAME # "Z128rr") VR128X:$dst,
                  VR128X:$src), 0>;
  def : InstAlias<OpcodeStr#"x\t{$src, $dst|$dst, $src}",
                  (!cast<Instruction>(NAME # "Z128rm") VR128X:$dst,
                  f128mem:$src), 0, "intel">;
  def : InstAlias<OpcodeStr#"y\t{$src, $dst|$dst, $src}",
                  (!cast<Instruction>(NAME # "Z256rr") VR128X:$dst,
                  VR256X:$src), 0>;
  def : InstAlias<OpcodeStr#"y\t{$src, $dst|$dst, $src}",
                  (!cast<Instruction>(NAME # "Z256rm") VR128X:$dst,
                  f256mem:$src), 0, "intel">;
}

defm VCVTNEPS2BF16 : avx512_cvtps2bf16<0x72, "vcvtneps2bf16",
                                       SchedWriteCvtPD2PS>, T8, XS,
                                       EVEX_CD8<32, CD8VF>;

let Predicates = [HasBF16, HasVLX] in {
  // Special patterns to allow use of X86mcvtneps2bf16 for masking. Instruction
  // patterns have been disabled with null_frag.
  def : Pat<(v8bf16 (X86cvtneps2bf16 (v4f32 VR128X:$src))),
            (VCVTNEPS2BF16Z128rr VR128X:$src)>;
  def : Pat<(X86mcvtneps2bf16 (v4f32 VR128X:$src), (v8bf16 VR128X:$src0),
                              VK4WM:$mask),
            (VCVTNEPS2BF16Z128rrk VR128X:$src0, VK4WM:$mask, VR128X:$src)>;
  def : Pat<(X86mcvtneps2bf16 (v4f32 VR128X:$src), v8bf16x_info.ImmAllZerosV,
                              VK4WM:$mask),
            (VCVTNEPS2BF16Z128rrkz VK4WM:$mask, VR128X:$src)>;

  def : Pat<(v8bf16 (X86cvtneps2bf16 (loadv4f32 addr:$src))),
            (VCVTNEPS2BF16Z128rm addr:$src)>;
  def : Pat<(X86mcvtneps2bf16 (loadv4f32 addr:$src), (v8bf16 VR128X:$src0),
                              VK4WM:$mask),
            (VCVTNEPS2BF16Z128rmk VR128X:$src0, VK4WM:$mask, addr:$src)>;
  def : Pat<(X86mcvtneps2bf16 (loadv4f32 addr:$src), v8bf16x_info.ImmAllZerosV,
                              VK4WM:$mask),
            (VCVTNEPS2BF16Z128rmkz VK4WM:$mask, addr:$src)>;

  def : Pat<(v8bf16 (X86cvtneps2bf16 (v4f32
                                     (X86VBroadcastld32 addr:$src)))),
            (VCVTNEPS2BF16Z128rmb addr:$src)>;
  def : Pat<(X86mcvtneps2bf16 (v4f32 (X86VBroadcastld32 addr:$src)),
                              (v8bf16 VR128X:$src0), VK4WM:$mask),
            (VCVTNEPS2BF16Z128rmbk VR128X:$src0, VK4WM:$mask, addr:$src)>;
  def : Pat<(X86mcvtneps2bf16 (v4f32 (X86VBroadcastld32 addr:$src)),
                              v8bf16x_info.ImmAllZerosV, VK4WM:$mask),
            (VCVTNEPS2BF16Z128rmbkz VK4WM:$mask, addr:$src)>;

  def : Pat<(v8bf16 (int_x86_vcvtneps2bf16128 (v4f32 VR128X:$src))),
            (VCVTNEPS2BF16Z128rr VR128X:$src)>;
  def : Pat<(v8bf16 (int_x86_vcvtneps2bf16128 (loadv4f32 addr:$src))),
            (VCVTNEPS2BF16Z128rm addr:$src)>;

  def : Pat<(v8bf16 (int_x86_vcvtneps2bf16256 (v8f32 VR256X:$src))),
            (VCVTNEPS2BF16Z256rr VR256X:$src)>;
  def : Pat<(v8bf16 (int_x86_vcvtneps2bf16256 (loadv8f32 addr:$src))),
            (VCVTNEPS2BF16Z256rm addr:$src)>;

  def : Pat<(v8bf16 (X86VBroadcastld16 addr:$src)),
            (VPBROADCASTWZ128rm addr:$src)>;
  def : Pat<(v16bf16 (X86VBroadcastld16 addr:$src)),
            (VPBROADCASTWZ256rm addr:$src)>;

  def : Pat<(v8bf16 (X86VBroadcast (v8bf16 VR128X:$src))),
            (VPBROADCASTWZ128rr VR128X:$src)>;
  def : Pat<(v16bf16 (X86VBroadcast (v8bf16 VR128X:$src))),
            (VPBROADCASTWZ256rr VR128X:$src)>;

  def : Pat<(v8bf16 (X86vfpround (v8f32 VR256X:$src))),
            (VCVTNEPS2BF16Z256rr VR256X:$src)>;
  def : Pat<(v8bf16 (X86vfpround (loadv8f32 addr:$src))),
            (VCVTNEPS2BF16Z256rm addr:$src)>;

  // TODO: No scalar broadcast due to we don't support legal scalar bf16 so far.
}

let Predicates = [HasBF16] in {
  def : Pat<(v32bf16 (X86VBroadcastld16 addr:$src)),
            (VPBROADCASTWZrm addr:$src)>;

  def : Pat<(v32bf16 (X86VBroadcast (v8bf16 VR128X:$src))),
            (VPBROADCASTWZrr VR128X:$src)>;

  def : Pat<(v16bf16 (X86vfpround (v16f32 VR512:$src))),
            (VCVTNEPS2BF16Zrr VR512:$src)>;
  def : Pat<(v16bf16 (X86vfpround (loadv16f32 addr:$src))),
            (VCVTNEPS2BF16Zrm addr:$src)>;
  // TODO: No scalar broadcast due to we don't support legal scalar bf16 so far.
}

let Constraints = "$src1 = $dst" in {
multiclass avx512_dpbf16ps_rm<bits<8> opc, string OpcodeStr, SDNode OpNode,
                              X86FoldableSchedWrite sched,
                              X86VectorVTInfo _, X86VectorVTInfo src_v> {
  defm r: AVX512_maskable_3src<opc, MRMSrcReg, _, (outs _.RC:$dst),
                           (ins src_v.RC:$src2, src_v.RC:$src3),
                           OpcodeStr, "$src3, $src2", "$src2, $src3",
                           (_.VT (OpNode _.RC:$src1, src_v.RC:$src2, src_v.RC:$src3))>,
                           EVEX, VVVV, Sched<[sched]>;

  defm m: AVX512_maskable_3src<opc, MRMSrcMem, _, (outs _.RC:$dst),
                               (ins src_v.RC:$src2, src_v.MemOp:$src3),
                               OpcodeStr, "$src3, $src2", "$src2, $src3",
                               (_.VT (OpNode _.RC:$src1, src_v.RC:$src2,
                               (src_v.LdFrag addr:$src3)))>, EVEX, VVVV,
                               Sched<[sched.Folded, sched.ReadAfterFold]>;

  let mayLoad = 1, hasSideEffects = 0 in
  defm mb: AVX512_maskable_3src<opc, MRMSrcMem, _, (outs _.RC:$dst),
                  (ins src_v.RC:$src2, f32mem:$src3),
                  OpcodeStr,
                  !strconcat("${src3}", _.BroadcastStr,", $src2"),
                  !strconcat("$src2, ${src3}", _.BroadcastStr),
                  (null_frag)>,
                  EVEX_B, EVEX, VVVV, Sched<[sched.Folded, sched.ReadAfterFold]>;

}
} // Constraints = "$src1 = $dst"

multiclass avx512_dpbf16ps_sizes<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                 X86SchedWriteWidths sched, AVX512VLVectorVTInfo _,
                                 AVX512VLVectorVTInfo src_v, Predicate prd> {
  let Predicates = [prd] in {
    defm Z    : avx512_dpbf16ps_rm<opc, OpcodeStr, OpNode, sched.ZMM, _.info512,
                                   src_v.info512>, EVEX_V512;
  }
  let Predicates = [HasVLX, prd] in {
    defm Z256 : avx512_dpbf16ps_rm<opc, OpcodeStr, OpNode, sched.YMM, _.info256,
                                   src_v.info256>, EVEX_V256;
    defm Z128 : avx512_dpbf16ps_rm<opc, OpcodeStr, OpNode, sched.XMM, _.info128,
                                   src_v.info128>, EVEX_V128;
  }
}

let ExeDomain = SSEPackedSingle in
defm VDPBF16PS : avx512_dpbf16ps_sizes<0x52, "vdpbf16ps", X86dpbf16ps, SchedWriteFMA,
                                       avx512vl_f32_info, avx512vl_bf16_info,
                                       HasBF16>, T8, XS, EVEX_CD8<32, CD8VF>;

//===----------------------------------------------------------------------===//
// AVX512FP16
//===----------------------------------------------------------------------===//

let Predicates = [HasFP16] in {
// Move word ( r/m16) to Packed word
def VMOVW2SHrr : AVX512<0x6E, MRMSrcReg, (outs VR128X:$dst), (ins GR32:$src),
                      "vmovw\t{$src, $dst|$dst, $src}", []>, T_MAP5, PD, EVEX, Sched<[WriteVecMoveFromGpr]>;
def VMOVWrm : AVX512<0x6E, MRMSrcMem, (outs VR128X:$dst), (ins i16mem:$src),
                      "vmovw\t{$src, $dst|$dst, $src}",
                      [(set VR128X:$dst,
                        (v8i16 (scalar_to_vector (loadi16 addr:$src))))]>,
                      T_MAP5, PD, EVEX, EVEX_CD8<16, CD8VT1>, Sched<[WriteFLoad]>;

def : Pat<(f16 (bitconvert GR16:$src)),
          (f16 (COPY_TO_REGCLASS
                (VMOVW2SHrr
                 (INSERT_SUBREG (IMPLICIT_DEF), GR16:$src, sub_16bit)),
                FR16X))>;
def : Pat<(v8i16 (scalar_to_vector (i16 GR16:$src))),
          (VMOVW2SHrr (INSERT_SUBREG (IMPLICIT_DEF), GR16:$src, sub_16bit))>;
def : Pat<(v4i32 (X86vzmovl (scalar_to_vector (and GR32:$src, 0xffff)))),
          (VMOVW2SHrr GR32:$src)>;
// FIXME: We should really find a way to improve these patterns.
def : Pat<(v8i32 (X86vzmovl
                  (insert_subvector undef,
                                    (v4i32 (scalar_to_vector
                                            (and GR32:$src, 0xffff))),
                                    (iPTR 0)))),
          (SUBREG_TO_REG (i32 0), (VMOVW2SHrr GR32:$src), sub_xmm)>;
def : Pat<(v16i32 (X86vzmovl
                   (insert_subvector undef,
                                     (v4i32 (scalar_to_vector
                                             (and GR32:$src, 0xffff))),
                                     (iPTR 0)))),
          (SUBREG_TO_REG (i32 0), (VMOVW2SHrr GR32:$src), sub_xmm)>;

def : Pat<(v8i16 (X86vzmovl (scalar_to_vector (i16 GR16:$src)))),
          (VMOVW2SHrr (INSERT_SUBREG (IMPLICIT_DEF), GR16:$src, sub_16bit))>;

// AVX 128-bit movw instruction write zeros in the high 128-bit part.
def : Pat<(v8i16 (X86vzload16 addr:$src)),
          (VMOVWrm addr:$src)>;
def : Pat<(v16i16 (X86vzload16 addr:$src)),
          (SUBREG_TO_REG (i32 0), (v8i16 (VMOVWrm addr:$src)), sub_xmm)>;

// Use regular 128-bit instructions to match 512-bit scalar_to_vec+zext.
def : Pat<(v32i16 (X86vzload16 addr:$src)),
          (SUBREG_TO_REG (i32 0), (v8i16 (VMOVWrm addr:$src)), sub_xmm)>;

def : Pat<(v4i32 (scalar_to_vector (i32 (extloadi16 addr:$src)))),
          (VMOVWrm addr:$src)>;
def : Pat<(v4i32 (X86vzmovl (scalar_to_vector (i32 (zextloadi16 addr:$src))))),
          (VMOVWrm addr:$src)>;
def : Pat<(v8i32 (X86vzmovl
                  (insert_subvector undef,
                                    (v4i32 (scalar_to_vector
                                            (i32 (zextloadi16 addr:$src)))),
                                    (iPTR 0)))),
          (SUBREG_TO_REG (i32 0), (VMOVWrm addr:$src), sub_xmm)>;
def : Pat<(v16i32 (X86vzmovl
                   (insert_subvector undef,
                                     (v4i32 (scalar_to_vector
                                             (i32 (zextloadi16 addr:$src)))),
                                     (iPTR 0)))),
          (SUBREG_TO_REG (i32 0), (VMOVWrm addr:$src), sub_xmm)>;

// Move word from xmm register to r/m16
def VMOVSH2Wrr  : AVX512<0x7E, MRMDestReg, (outs GR32:$dst), (ins VR128X:$src),
                       "vmovw\t{$src, $dst|$dst, $src}", []>, T_MAP5, PD, EVEX, Sched<[WriteVecMoveToGpr]>;
def VMOVWmr  : AVX512<0x7E, MRMDestMem, (outs),
                       (ins i16mem:$dst, VR128X:$src),
                       "vmovw\t{$src, $dst|$dst, $src}",
                       [(store (i16 (extractelt (v8i16 VR128X:$src),
                                     (iPTR 0))), addr:$dst)]>,
                       T_MAP5, PD, EVEX, EVEX_CD8<16, CD8VT1>, Sched<[WriteFStore]>;

def : Pat<(i16 (bitconvert FR16X:$src)),
          (i16 (EXTRACT_SUBREG
                (VMOVSH2Wrr (COPY_TO_REGCLASS FR16X:$src, VR128X)),
                sub_16bit))>;
def : Pat<(i16 (extractelt (v8i16 VR128X:$src), (iPTR 0))),
          (i16 (EXTRACT_SUBREG (VMOVSH2Wrr VR128X:$src), sub_16bit))>;

// Allow "vmovw" to use GR64
let hasSideEffects = 0 in {
  def VMOVW64toSHrr : AVX512<0x6E, MRMSrcReg, (outs VR128X:$dst), (ins GR64:$src),
                     "vmovw\t{$src, $dst|$dst, $src}", []>, T_MAP5, PD, EVEX, REX_W, Sched<[WriteVecMoveFromGpr]>;
  def VMOVSHtoW64rr : AVX512<0x7E, MRMDestReg, (outs GR64:$dst), (ins VR128X:$src),
                     "vmovw\t{$src, $dst|$dst, $src}", []>, T_MAP5, PD, EVEX, REX_W, Sched<[WriteVecMoveToGpr]>;
}
}

// Convert 16-bit float to i16/u16
multiclass avx512_cvtph2w<bits<8> opc, string OpcodeStr, SDPatternOperator OpNode,
                          SDPatternOperator MaskOpNode, SDNode OpNodeRnd,
                          AVX512VLVectorVTInfo _Dst,
                          AVX512VLVectorVTInfo _Src,
                          X86SchedWriteWidths sched> {
  let Predicates = [HasFP16] in {
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, _Dst.info512, _Src.info512,
                            OpNode, MaskOpNode, sched.ZMM>,
             avx512_vcvt_fp_rc<opc, OpcodeStr, _Dst.info512, _Src.info512,
                               OpNodeRnd, sched.ZMM>, EVEX_V512;
  }
  let Predicates = [HasFP16, HasVLX] in {
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, _Dst.info128, _Src.info128,
                               OpNode, MaskOpNode, sched.XMM>, EVEX_V128;
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, _Dst.info256, _Src.info256,
                               OpNode, MaskOpNode, sched.YMM>, EVEX_V256;
  }
}

// Convert 16-bit float to i16/u16 truncate
multiclass avx512_cvttph2w<bits<8> opc, string OpcodeStr, SDPatternOperator OpNode,
                           SDPatternOperator MaskOpNode, SDNode OpNodeRnd,
                           AVX512VLVectorVTInfo _Dst, AVX512VLVectorVTInfo _Src,
                           X86SchedWriteWidths sched> {
  let Predicates = [HasFP16] in {
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, _Dst.info512, _Src.info512,
                            OpNode, MaskOpNode, sched.ZMM>,
             avx512_vcvt_fp_sae<opc, OpcodeStr, _Dst.info512, _Src.info512,
                               OpNodeRnd, sched.ZMM>, EVEX_V512;
  }
  let Predicates = [HasFP16, HasVLX] in {
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, _Dst.info128, _Src.info128,
                               OpNode, MaskOpNode, sched.XMM>, EVEX_V128;
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, _Dst.info256, _Src.info256,
                               OpNode, MaskOpNode, sched.YMM>, EVEX_V256;
  }
}

defm VCVTPH2UW : avx512_cvtph2w<0x7D, "vcvtph2uw", X86cvtp2UInt, X86cvtp2UInt,
                                X86cvtp2UIntRnd, avx512vl_i16_info,
                                avx512vl_f16_info, SchedWriteCvtPD2DQ>,
                                T_MAP5, EVEX_CD8<16, CD8VF>;
defm VCVTUW2PH : avx512_cvtph2w<0x7D, "vcvtuw2ph", any_uint_to_fp, uint_to_fp,
                                X86VUintToFpRnd, avx512vl_f16_info,
                                avx512vl_i16_info, SchedWriteCvtPD2DQ>,
                                T_MAP5, XD, EVEX_CD8<16, CD8VF>;
defm VCVTTPH2W : avx512_cvttph2w<0x7C, "vcvttph2w", X86any_cvttp2si,
                                X86cvttp2si, X86cvttp2siSAE,
                                avx512vl_i16_info, avx512vl_f16_info,
                                SchedWriteCvtPD2DQ>, T_MAP5, PD, EVEX_CD8<16, CD8VF>;
defm VCVTTPH2UW : avx512_cvttph2w<0x7C, "vcvttph2uw", X86any_cvttp2ui,
                                X86cvttp2ui, X86cvttp2uiSAE,
                                avx512vl_i16_info, avx512vl_f16_info,
                                SchedWriteCvtPD2DQ>, T_MAP5, EVEX_CD8<16, CD8VF>;
defm VCVTPH2W : avx512_cvtph2w<0x7D, "vcvtph2w", X86cvtp2Int, X86cvtp2Int,
                                X86cvtp2IntRnd, avx512vl_i16_info,
                                avx512vl_f16_info, SchedWriteCvtPD2DQ>,
                                T_MAP5, PD, EVEX_CD8<16, CD8VF>;
defm VCVTW2PH : avx512_cvtph2w<0x7D, "vcvtw2ph", any_sint_to_fp, sint_to_fp,
                                X86VSintToFpRnd, avx512vl_f16_info,
                                avx512vl_i16_info, SchedWriteCvtPD2DQ>,
                                T_MAP5, XS, EVEX_CD8<16, CD8VF>;

// Convert Half to Signed/Unsigned Doubleword
multiclass avx512_cvtph2dq<bits<8> opc, string OpcodeStr, SDPatternOperator OpNode,
                           SDPatternOperator MaskOpNode, SDNode OpNodeRnd,
                           X86SchedWriteWidths sched> {
  let Predicates = [HasFP16] in {
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, v16i32_info, v16f16x_info, OpNode,
                            MaskOpNode, sched.ZMM>,
             avx512_vcvt_fp_rc<opc, OpcodeStr, v16i32_info, v16f16x_info,
                                OpNodeRnd, sched.ZMM>, EVEX_V512;
  }
  let Predicates = [HasFP16, HasVLX] in {
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, v4i32x_info, v8f16x_info, OpNode,
                               MaskOpNode, sched.XMM, "{1to4}", "", f64mem>, EVEX_V128;
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, v8i32x_info, v8f16x_info, OpNode,
                               MaskOpNode, sched.YMM>, EVEX_V256;
  }
}

// Convert Half to Signed/Unsigned Doubleword with truncation
multiclass avx512_cvttph2dq<bits<8> opc, string OpcodeStr, SDPatternOperator OpNode,
                            SDPatternOperator MaskOpNode, SDNode OpNodeRnd,
                            X86SchedWriteWidths sched> {
  let Predicates = [HasFP16] in {
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, v16i32_info, v16f16x_info, OpNode,
                            MaskOpNode, sched.ZMM>,
             avx512_vcvt_fp_sae<opc, OpcodeStr, v16i32_info, v16f16x_info,
                                OpNodeRnd, sched.ZMM>, EVEX_V512;
  }
  let Predicates = [HasFP16, HasVLX] in {
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, v4i32x_info, v8f16x_info, OpNode,
                               MaskOpNode, sched.XMM, "{1to4}", "", f64mem>, EVEX_V128;
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, v8i32x_info, v8f16x_info, OpNode,
                               MaskOpNode, sched.YMM>, EVEX_V256;
  }
}


defm VCVTPH2DQ : avx512_cvtph2dq<0x5B, "vcvtph2dq", X86cvtp2Int, X86cvtp2Int,
                                 X86cvtp2IntRnd, SchedWriteCvtPS2DQ>, T_MAP5, PD,
                                 EVEX_CD8<16, CD8VH>;
defm VCVTPH2UDQ : avx512_cvtph2dq<0x79, "vcvtph2udq", X86cvtp2UInt, X86cvtp2UInt,
                                 X86cvtp2UIntRnd, SchedWriteCvtPS2DQ>, T_MAP5,
                                 EVEX_CD8<16, CD8VH>;

defm VCVTTPH2DQ : avx512_cvttph2dq<0x5B, "vcvttph2dq", X86any_cvttp2si,
                                X86cvttp2si, X86cvttp2siSAE,
                                SchedWriteCvtPS2DQ>, T_MAP5, XS,
                                EVEX_CD8<16, CD8VH>;

defm VCVTTPH2UDQ : avx512_cvttph2dq<0x78, "vcvttph2udq", X86any_cvttp2ui,
                                 X86cvttp2ui, X86cvttp2uiSAE,
                                 SchedWriteCvtPS2DQ>, T_MAP5,
                                 EVEX_CD8<16, CD8VH>;

// Convert Half to Signed/Unsigned Quardword
multiclass avx512_cvtph2qq<bits<8> opc, string OpcodeStr, SDPatternOperator OpNode,
                           SDPatternOperator MaskOpNode, SDNode OpNodeRnd,
                           X86SchedWriteWidths sched> {
  let Predicates = [HasFP16] in {
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, v8i64_info, v8f16x_info, OpNode,
                            MaskOpNode, sched.ZMM>,
             avx512_vcvt_fp_rc<opc, OpcodeStr, v8i64_info, v8f16x_info,
                               OpNodeRnd, sched.ZMM>, EVEX_V512;
  }
  let Predicates = [HasFP16, HasVLX] in {
    // Explicitly specified broadcast string, since we take only 2 elements
    // from v8f16x_info source
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, v2i64x_info, v8f16x_info, OpNode,
                               MaskOpNode, sched.XMM, "{1to2}", "", f32mem>,
                               EVEX_V128;
    // Explicitly specified broadcast string, since we take only 4 elements
    // from v8f16x_info source
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, v4i64x_info, v8f16x_info, OpNode,
                               MaskOpNode, sched.YMM, "{1to4}", "", f64mem>,
                               EVEX_V256;
  }
}

// Convert Half to Signed/Unsigned Quardword with truncation
multiclass avx512_cvttph2qq<bits<8> opc, string OpcodeStr, SDPatternOperator OpNode,
                            SDPatternOperator MaskOpNode, SDNode OpNodeRnd,
                            X86SchedWriteWidths sched> {
  let Predicates = [HasFP16] in {
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, v8i64_info, v8f16x_info, OpNode,
                            MaskOpNode, sched.ZMM>,
             avx512_vcvt_fp_sae<opc, OpcodeStr, v8i64_info, v8f16x_info,
                                OpNodeRnd, sched.ZMM>, EVEX_V512;
  }
  let Predicates = [HasFP16, HasVLX] in {
    // Explicitly specified broadcast string, since we take only 2 elements
    // from v8f16x_info source
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, v2i64x_info, v8f16x_info, OpNode,
                               MaskOpNode, sched.XMM, "{1to2}", "", f32mem>, EVEX_V128;
    // Explicitly specified broadcast string, since we take only 4 elements
    // from v8f16x_info source
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, v4i64x_info, v8f16x_info, OpNode,
                               MaskOpNode, sched.YMM, "{1to4}", "", f64mem>, EVEX_V256;
  }
}

defm VCVTPH2QQ : avx512_cvtph2qq<0x7B, "vcvtph2qq", X86cvtp2Int, X86cvtp2Int,
                                 X86cvtp2IntRnd, SchedWriteCvtPS2DQ>, T_MAP5, PD,
                                 EVEX_CD8<16, CD8VQ>;

defm VCVTPH2UQQ : avx512_cvtph2qq<0x79, "vcvtph2uqq", X86cvtp2UInt, X86cvtp2UInt,
                                 X86cvtp2UIntRnd, SchedWriteCvtPS2DQ>, T_MAP5, PD,
                                 EVEX_CD8<16, CD8VQ>;

defm VCVTTPH2QQ : avx512_cvttph2qq<0x7A, "vcvttph2qq", X86any_cvttp2si,
                                 X86cvttp2si, X86cvttp2siSAE,
                                 SchedWriteCvtPS2DQ>, T_MAP5, PD,
                                 EVEX_CD8<16, CD8VQ>;

defm VCVTTPH2UQQ : avx512_cvttph2qq<0x78, "vcvttph2uqq", X86any_cvttp2ui,
                                 X86cvttp2ui, X86cvttp2uiSAE,
                                 SchedWriteCvtPS2DQ>, T_MAP5, PD,
                                 EVEX_CD8<16, CD8VQ>;

// Convert Signed/Unsigned Quardword to Half
multiclass avx512_cvtqq2ph<bits<8> opc, string OpcodeStr, SDPatternOperator OpNode,
                           SDPatternOperator MaskOpNode, SDNode OpNodeRnd,
                           X86SchedWriteWidths sched> {
  // we need "x"/"y"/"z" suffixes in order to distinguish between 128, 256 and
  // 512 memory forms of these instructions in Asm Parcer. They have the same
  // dest type - 'v8f16x_info'. We also specify the broadcast string explicitly
  // due to the same reason.
  let Predicates = [HasFP16] in {
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, v8f16x_info, v8i64_info, OpNode,
                            MaskOpNode, sched.ZMM, "{1to8}", "{z}">,
             avx512_vcvt_fp_rc<opc, OpcodeStr, v8f16x_info, v8i64_info,
                               OpNodeRnd, sched.ZMM>, EVEX_V512;
  }
  let Predicates = [HasFP16, HasVLX] in {
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, v8f16x_info, v2i64x_info,
                               null_frag, null_frag, sched.XMM, "{1to2}", "{x}",
                               i128mem, VK2WM>, EVEX_V128;
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, v8f16x_info, v4i64x_info,
                               null_frag, null_frag, sched.YMM, "{1to4}", "{y}",
                               i256mem, VK4WM>, EVEX_V256;
  }

  def : InstAlias<OpcodeStr#"x\t{$src, $dst|$dst, $src}",
                  (!cast<Instruction>(NAME # "Z128rr") VR128X:$dst,
                  VR128X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"x\t{$src, $dst {${mask}}|$dst {${mask}}, $src}",
                  (!cast<Instruction>(NAME # "Z128rrk") VR128X:$dst,
                  VK2WM:$mask, VR128X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"x\t{$src, $dst {${mask}} {z}|$dst {${mask}} {z}, $src}",
                  (!cast<Instruction>(NAME # "Z128rrkz") VR128X:$dst,
                  VK2WM:$mask, VR128X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"x\t{${src}{1to2}, $dst|$dst, ${src}{1to2}}",
                  (!cast<Instruction>(NAME # "Z128rmb") VR128X:$dst,
                  i64mem:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"x\t{${src}{1to2}, $dst {${mask}}|"
                  "$dst {${mask}}, ${src}{1to2}}",
                  (!cast<Instruction>(NAME # "Z128rmbk") VR128X:$dst,
                  VK2WM:$mask, i64mem:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"x\t{${src}{1to2}, $dst {${mask}} {z}|"
                  "$dst {${mask}} {z}, ${src}{1to2}}",
                  (!cast<Instruction>(NAME # "Z128rmbkz") VR128X:$dst,
                  VK2WM:$mask, i64mem:$src), 0, "att">;

  def : InstAlias<OpcodeStr#"y\t{$src, $dst|$dst, $src}",
                  (!cast<Instruction>(NAME # "Z256rr") VR128X:$dst,
                  VR256X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"y\t{$src, $dst {${mask}}|"
                  "$dst {${mask}}, $src}",
                  (!cast<Instruction>(NAME # "Z256rrk") VR128X:$dst,
                  VK4WM:$mask, VR256X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"y\t{$src, $dst {${mask}} {z}|"
                  "$dst {${mask}} {z}, $src}",
                  (!cast<Instruction>(NAME # "Z256rrkz") VR128X:$dst,
                  VK4WM:$mask, VR256X:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"y\t{${src}{1to4}, $dst|$dst, ${src}{1to4}}",
                  (!cast<Instruction>(NAME # "Z256rmb") VR128X:$dst,
                  i64mem:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"y\t{${src}{1to4}, $dst {${mask}}|"
                  "$dst {${mask}}, ${src}{1to4}}",
                  (!cast<Instruction>(NAME # "Z256rmbk") VR128X:$dst,
                  VK4WM:$mask, i64mem:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"y\t{${src}{1to4}, $dst {${mask}} {z}|"
                  "$dst {${mask}} {z}, ${src}{1to4}}",
                  (!cast<Instruction>(NAME # "Z256rmbkz") VR128X:$dst,
                  VK4WM:$mask, i64mem:$src), 0, "att">;

  def : InstAlias<OpcodeStr#"z\t{$src, $dst|$dst, $src}",
                  (!cast<Instruction>(NAME # "Zrr") VR128X:$dst,
                  VR512:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"z\t{$src, $dst {${mask}}|"
                  "$dst {${mask}}, $src}",
                  (!cast<Instruction>(NAME # "Zrrk") VR128X:$dst,
                  VK8WM:$mask, VR512:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"z\t{$src, $dst {${mask}} {z}|"
                  "$dst {${mask}} {z}, $src}",
                  (!cast<Instruction>(NAME # "Zrrkz") VR128X:$dst,
                  VK8WM:$mask, VR512:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"z\t{${src}{1to8}, $dst|$dst, ${src}{1to8}}",
                  (!cast<Instruction>(NAME # "Zrmb") VR128X:$dst,
                  i64mem:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"z\t{${src}{1to8}, $dst {${mask}}|"
                  "$dst {${mask}}, ${src}{1to8}}",
                  (!cast<Instruction>(NAME # "Zrmbk") VR128X:$dst,
                  VK8WM:$mask, i64mem:$src), 0, "att">;
  def : InstAlias<OpcodeStr#"z\t{${src}{1to8}, $dst {${mask}} {z}|"
                  "$dst {${mask}} {z}, ${src}{1to8}}",
                  (!cast<Instruction>(NAME # "Zrmbkz") VR128X:$dst,
                  VK8WM:$mask, i64mem:$src), 0, "att">;
}

defm VCVTQQ2PH : avx512_cvtqq2ph<0x5B, "vcvtqq2ph", any_sint_to_fp, sint_to_fp,
                            X86VSintToFpRnd, SchedWriteCvtDQ2PS>, REX_W, T_MAP5,
                            EVEX_CD8<64, CD8VF>;

defm VCVTUQQ2PH : avx512_cvtqq2ph<0x7A, "vcvtuqq2ph", any_uint_to_fp, uint_to_fp,
                            X86VUintToFpRnd, SchedWriteCvtDQ2PS>, REX_W, T_MAP5, XD,
                            EVEX_CD8<64, CD8VF>;

// Convert half to signed/unsigned int 32/64
defm VCVTSH2SIZ: avx512_cvt_s_int_round<0x2D, f16x_info, i32x_info, X86cvts2si,
                                   X86cvts2siRnd, WriteCvtSS2I, "cvtsh2si", "{l}", HasFP16>,
                                   T_MAP5, XS, EVEX_CD8<16, CD8VT1>;
defm VCVTSH2SI64Z: avx512_cvt_s_int_round<0x2D, f16x_info, i64x_info, X86cvts2si,
                                   X86cvts2siRnd, WriteCvtSS2I, "cvtsh2si", "{q}", HasFP16>,
                                   T_MAP5, XS, REX_W, EVEX_CD8<16, CD8VT1>;
defm VCVTSH2USIZ: avx512_cvt_s_int_round<0x79, f16x_info, i32x_info, X86cvts2usi,
                                   X86cvts2usiRnd, WriteCvtSS2I, "cvtsh2usi", "{l}", HasFP16>,
                                   T_MAP5, XS, EVEX_CD8<16, CD8VT1>;
defm VCVTSH2USI64Z: avx512_cvt_s_int_round<0x79, f16x_info, i64x_info, X86cvts2usi,
                                   X86cvts2usiRnd, WriteCvtSS2I, "cvtsh2usi", "{q}", HasFP16>,
                                   T_MAP5, XS, REX_W, EVEX_CD8<16, CD8VT1>;

defm VCVTTSH2SIZ: avx512_cvt_s_all<0x2C, "vcvttsh2si", f16x_info, i32x_info,
                        any_fp_to_sint, X86cvtts2Int, X86cvtts2IntSAE, WriteCvtSS2I,
                        "{l}", HasFP16>, T_MAP5, XS, EVEX_CD8<16, CD8VT1>;
defm VCVTTSH2SI64Z: avx512_cvt_s_all<0x2C, "vcvttsh2si", f16x_info, i64x_info,
                        any_fp_to_sint, X86cvtts2Int, X86cvtts2IntSAE, WriteCvtSS2I,
                        "{q}", HasFP16>, REX_W, T_MAP5, XS, EVEX_CD8<16, CD8VT1>;
defm VCVTTSH2USIZ: avx512_cvt_s_all<0x78, "vcvttsh2usi", f16x_info, i32x_info,
                        any_fp_to_uint, X86cvtts2UInt, X86cvtts2UIntSAE, WriteCvtSS2I,
                        "{l}", HasFP16>, T_MAP5, XS, EVEX_CD8<16, CD8VT1>;
defm VCVTTSH2USI64Z: avx512_cvt_s_all<0x78, "vcvttsh2usi", f16x_info, i64x_info,
                        any_fp_to_uint, X86cvtts2UInt, X86cvtts2UIntSAE, WriteCvtSS2I,
                        "{q}", HasFP16>, T_MAP5, XS, REX_W, EVEX_CD8<16, CD8VT1>;

let Predicates = [HasFP16] in {
  defm VCVTSI2SHZ  : avx512_vcvtsi_common<0x2A,  X86SintToFp, X86SintToFpRnd, WriteCvtI2SS, GR32,
                                   v8f16x_info, i32mem, loadi32, "cvtsi2sh", "l">,
                                   T_MAP5, XS, EVEX_CD8<32, CD8VT1>;
  defm VCVTSI642SHZ: avx512_vcvtsi_common<0x2A,  X86SintToFp, X86SintToFpRnd, WriteCvtI2SS, GR64,
                                   v8f16x_info, i64mem, loadi64, "cvtsi2sh","q">,
                                   T_MAP5, XS, REX_W, EVEX_CD8<64, CD8VT1>;
  defm VCVTUSI2SHZ   : avx512_vcvtsi_common<0x7B,  X86UintToFp, X86UintToFpRnd, WriteCvtI2SS, GR32,
                                    v8f16x_info, i32mem, loadi32,
                                    "cvtusi2sh","l">, T_MAP5, XS, EVEX_CD8<32, CD8VT1>;
  defm VCVTUSI642SHZ : avx512_vcvtsi_common<0x7B,  X86UintToFp, X86UintToFpRnd, WriteCvtI2SS, GR64,
                                    v8f16x_info, i64mem, loadi64, "cvtusi2sh", "q">,
                                    T_MAP5, XS, REX_W, EVEX_CD8<64, CD8VT1>;
  def : InstAlias<"vcvtsi2sh\t{$src, $src1, $dst|$dst, $src1, $src}",
              (VCVTSI2SHZrm_Int VR128X:$dst, VR128X:$src1, i32mem:$src), 0, "att">;

  def : InstAlias<"vcvtusi2sh\t{$src, $src1, $dst|$dst, $src1, $src}",
              (VCVTUSI2SHZrm_Int VR128X:$dst, VR128X:$src1, i32mem:$src), 0, "att">;


  def : Pat<(f16 (any_sint_to_fp (loadi32 addr:$src))),
            (VCVTSI2SHZrm (f16 (IMPLICIT_DEF)), addr:$src)>;
  def : Pat<(f16 (any_sint_to_fp (loadi64 addr:$src))),
            (VCVTSI642SHZrm (f16 (IMPLICIT_DEF)), addr:$src)>;

  def : Pat<(f16 (any_sint_to_fp GR32:$src)),
            (VCVTSI2SHZrr (f16 (IMPLICIT_DEF)), GR32:$src)>;
  def : Pat<(f16 (any_sint_to_fp GR64:$src)),
            (VCVTSI642SHZrr (f16 (IMPLICIT_DEF)), GR64:$src)>;

  def : Pat<(f16 (any_uint_to_fp (loadi32 addr:$src))),
            (VCVTUSI2SHZrm (f16 (IMPLICIT_DEF)), addr:$src)>;
  def : Pat<(f16 (any_uint_to_fp (loadi64 addr:$src))),
            (VCVTUSI642SHZrm (f16 (IMPLICIT_DEF)), addr:$src)>;

  def : Pat<(f16 (any_uint_to_fp GR32:$src)),
            (VCVTUSI2SHZrr (f16 (IMPLICIT_DEF)), GR32:$src)>;
  def : Pat<(f16 (any_uint_to_fp GR64:$src)),
            (VCVTUSI642SHZrr (f16 (IMPLICIT_DEF)), GR64:$src)>;

  // Patterns used for matching vcvtsi2sh intrinsic sequences from clang
  // which produce unnecessary vmovsh instructions
  def : Pat<(v8f16 (X86Movsh
                     (v8f16 VR128X:$dst),
                     (v8f16 (scalar_to_vector (f16 (any_sint_to_fp GR64:$src)))))),
            (VCVTSI642SHZrr_Int VR128X:$dst, GR64:$src)>;

  def : Pat<(v8f16 (X86Movsh
                     (v8f16 VR128X:$dst),
                     (v8f16 (scalar_to_vector (f16 (any_sint_to_fp (loadi64 addr:$src))))))),
            (VCVTSI642SHZrm_Int VR128X:$dst, addr:$src)>;

  def : Pat<(v8f16 (X86Movsh
                     (v8f16 VR128X:$dst),
                     (v8f16 (scalar_to_vector (f16 (any_sint_to_fp GR32:$src)))))),
            (VCVTSI2SHZrr_Int VR128X:$dst, GR32:$src)>;

  def : Pat<(v8f16 (X86Movsh
                     (v8f16 VR128X:$dst),
                     (v8f16 (scalar_to_vector (f16 (any_sint_to_fp (loadi32 addr:$src))))))),
            (VCVTSI2SHZrm_Int VR128X:$dst, addr:$src)>;

  def : Pat<(v8f16 (X86Movsh
                     (v8f16 VR128X:$dst),
                     (v8f16 (scalar_to_vector (f16 (any_uint_to_fp GR64:$src)))))),
            (VCVTUSI642SHZrr_Int VR128X:$dst, GR64:$src)>;

  def : Pat<(v8f16 (X86Movsh
                     (v8f16 VR128X:$dst),
                     (v8f16 (scalar_to_vector (f16 (any_uint_to_fp (loadi64 addr:$src))))))),
            (VCVTUSI642SHZrm_Int VR128X:$dst, addr:$src)>;

  def : Pat<(v8f16 (X86Movsh
                     (v8f16 VR128X:$dst),
                     (v8f16 (scalar_to_vector (f16 (any_uint_to_fp GR32:$src)))))),
            (VCVTUSI2SHZrr_Int VR128X:$dst, GR32:$src)>;

  def : Pat<(v8f16 (X86Movsh
                     (v8f16 VR128X:$dst),
                     (v8f16 (scalar_to_vector (f16 (any_uint_to_fp (loadi32 addr:$src))))))),
            (VCVTUSI2SHZrm_Int VR128X:$dst, addr:$src)>;
} // Predicates = [HasFP16]

let Predicates = [HasFP16, HasVLX] in {
  // Special patterns to allow use of X86VMSintToFP for masking. Instruction
  // patterns have been disabled with null_frag.
  def : Pat<(v8f16 (X86any_VSintToFP (v4i64 VR256X:$src))),
            (VCVTQQ2PHZ256rr VR256X:$src)>;
  def : Pat<(X86VMSintToFP (v4i64 VR256X:$src), (v8f16 VR128X:$src0),
                           VK4WM:$mask),
            (VCVTQQ2PHZ256rrk VR128X:$src0, VK4WM:$mask, VR256X:$src)>;
  def : Pat<(X86VMSintToFP (v4i64 VR256X:$src), v8f16x_info.ImmAllZerosV,
                           VK4WM:$mask),
            (VCVTQQ2PHZ256rrkz VK4WM:$mask, VR256X:$src)>;

  def : Pat<(v8f16 (X86any_VSintToFP (loadv4i64 addr:$src))),
            (VCVTQQ2PHZ256rm addr:$src)>;
  def : Pat<(X86VMSintToFP (loadv4i64 addr:$src), (v8f16 VR128X:$src0),
                           VK4WM:$mask),
            (VCVTQQ2PHZ256rmk VR128X:$src0, VK4WM:$mask, addr:$src)>;
  def : Pat<(X86VMSintToFP (loadv4i64 addr:$src), v8f16x_info.ImmAllZerosV,
                           VK4WM:$mask),
            (VCVTQQ2PHZ256rmkz VK4WM:$mask, addr:$src)>;

  def : Pat<(v8f16 (X86any_VSintToFP (v4i64 (X86VBroadcastld64 addr:$src)))),
            (VCVTQQ2PHZ256rmb addr:$src)>;
  def : Pat<(X86VMSintToFP (v4i64 (X86VBroadcastld64 addr:$src)),
                           (v8f16 VR128X:$src0), VK4WM:$mask),
            (VCVTQQ2PHZ256rmbk VR128X:$src0, VK4WM:$mask, addr:$src)>;
  def : Pat<(X86VMSintToFP (v4i64 (X86VBroadcastld64 addr:$src)),
                           v8f16x_info.ImmAllZerosV, VK4WM:$mask),
            (VCVTQQ2PHZ256rmbkz VK4WM:$mask, addr:$src)>;

  def : Pat<(v8f16 (X86any_VSintToFP (v2i64 VR128X:$src))),
            (VCVTQQ2PHZ128rr VR128X:$src)>;
  def : Pat<(X86VMSintToFP (v2i64 VR128X:$src), (v8f16 VR128X:$src0),
                           VK2WM:$mask),
            (VCVTQQ2PHZ128rrk VR128X:$src0, VK2WM:$mask, VR128X:$src)>;
  def : Pat<(X86VMSintToFP (v2i64 VR128X:$src), v8f16x_info.ImmAllZerosV,
                           VK2WM:$mask),
            (VCVTQQ2PHZ128rrkz VK2WM:$mask, VR128X:$src)>;

  def : Pat<(v8f16 (X86any_VSintToFP (loadv2i64 addr:$src))),
            (VCVTQQ2PHZ128rm addr:$src)>;
  def : Pat<(X86VMSintToFP (loadv2i64 addr:$src), (v8f16 VR128X:$src0),
                           VK2WM:$mask),
            (VCVTQQ2PHZ128rmk VR128X:$src0, VK2WM:$mask, addr:$src)>;
  def : Pat<(X86VMSintToFP (loadv2i64 addr:$src), v8f16x_info.ImmAllZerosV,
                           VK2WM:$mask),
            (VCVTQQ2PHZ128rmkz VK2WM:$mask, addr:$src)>;

  def : Pat<(v8f16 (X86any_VSintToFP (v2i64 (X86VBroadcastld64 addr:$src)))),
            (VCVTQQ2PHZ128rmb addr:$src)>;
  def : Pat<(X86VMSintToFP (v2i64 (X86VBroadcastld64 addr:$src)),
                           (v8f16 VR128X:$src0), VK2WM:$mask),
            (VCVTQQ2PHZ128rmbk VR128X:$src0, VK2WM:$mask, addr:$src)>;
  def : Pat<(X86VMSintToFP (v2i64 (X86VBroadcastld64 addr:$src)),
                           v8f16x_info.ImmAllZerosV, VK2WM:$mask),
            (VCVTQQ2PHZ128rmbkz VK2WM:$mask, addr:$src)>;

  // Special patterns to allow use of X86VMUintToFP for masking. Instruction
  // patterns have been disabled with null_frag.
  def : Pat<(v8f16 (X86any_VUintToFP (v4i64 VR256X:$src))),
            (VCVTUQQ2PHZ256rr VR256X:$src)>;
  def : Pat<(X86VMUintToFP (v4i64 VR256X:$src), (v8f16 VR128X:$src0),
                           VK4WM:$mask),
            (VCVTUQQ2PHZ256rrk VR128X:$src0, VK4WM:$mask, VR256X:$src)>;
  def : Pat<(X86VMUintToFP (v4i64 VR256X:$src), v8f16x_info.ImmAllZerosV,
                           VK4WM:$mask),
            (VCVTUQQ2PHZ256rrkz VK4WM:$mask, VR256X:$src)>;

  def : Pat<(v8f16 (X86any_VUintToFP (loadv4i64 addr:$src))),
            (VCVTUQQ2PHZ256rm addr:$src)>;
  def : Pat<(X86VMUintToFP (loadv4i64 addr:$src), (v8f16 VR128X:$src0),
                           VK4WM:$mask),
            (VCVTUQQ2PHZ256rmk VR128X:$src0, VK4WM:$mask, addr:$src)>;
  def : Pat<(X86VMUintToFP (loadv4i64 addr:$src), v8f16x_info.ImmAllZerosV,
                           VK4WM:$mask),
            (VCVTUQQ2PHZ256rmkz VK4WM:$mask, addr:$src)>;

  def : Pat<(v8f16 (X86any_VUintToFP (v4i64 (X86VBroadcastld64 addr:$src)))),
            (VCVTUQQ2PHZ256rmb addr:$src)>;
  def : Pat<(X86VMUintToFP (v4i64 (X86VBroadcastld64 addr:$src)),
                           (v8f16 VR128X:$src0), VK4WM:$mask),
            (VCVTUQQ2PHZ256rmbk VR128X:$src0, VK4WM:$mask, addr:$src)>;
  def : Pat<(X86VMUintToFP (v4i64 (X86VBroadcastld64 addr:$src)),
                           v8f16x_info.ImmAllZerosV, VK4WM:$mask),
            (VCVTUQQ2PHZ256rmbkz VK4WM:$mask, addr:$src)>;

  def : Pat<(v8f16 (X86any_VUintToFP (v2i64 VR128X:$src))),
            (VCVTUQQ2PHZ128rr VR128X:$src)>;
  def : Pat<(X86VMUintToFP (v2i64 VR128X:$src), (v8f16 VR128X:$src0),
                           VK2WM:$mask),
            (VCVTUQQ2PHZ128rrk VR128X:$src0, VK2WM:$mask, VR128X:$src)>;
  def : Pat<(X86VMUintToFP (v2i64 VR128X:$src), v8f16x_info.ImmAllZerosV,
                           VK2WM:$mask),
            (VCVTUQQ2PHZ128rrkz VK2WM:$mask, VR128X:$src)>;

  def : Pat<(v8f16 (X86any_VUintToFP (loadv2i64 addr:$src))),
            (VCVTUQQ2PHZ128rm addr:$src)>;
  def : Pat<(X86VMUintToFP (loadv2i64 addr:$src), (v8f16 VR128X:$src0),
                           VK2WM:$mask),
            (VCVTUQQ2PHZ128rmk VR128X:$src0, VK2WM:$mask, addr:$src)>;
  def : Pat<(X86VMUintToFP (loadv2i64 addr:$src), v8f16x_info.ImmAllZerosV,
                           VK2WM:$mask),
            (VCVTUQQ2PHZ128rmkz VK2WM:$mask, addr:$src)>;

  def : Pat<(v8f16 (X86any_VUintToFP (v2i64 (X86VBroadcastld64 addr:$src)))),
            (VCVTUQQ2PHZ128rmb addr:$src)>;
  def : Pat<(X86VMUintToFP (v2i64 (X86VBroadcastld64 addr:$src)),
                           (v8f16 VR128X:$src0), VK2WM:$mask),
            (VCVTUQQ2PHZ128rmbk VR128X:$src0, VK2WM:$mask, addr:$src)>;
  def : Pat<(X86VMUintToFP (v2i64 (X86VBroadcastld64 addr:$src)),
                           v8f16x_info.ImmAllZerosV, VK2WM:$mask),
            (VCVTUQQ2PHZ128rmbkz VK2WM:$mask, addr:$src)>;
}

let Constraints = "@earlyclobber $dst, $src1 = $dst" in {
  multiclass avx512_cfmaop_rm<bits<8> opc, string OpcodeStr, SDNode OpNode, X86VectorVTInfo _, bit IsCommutable> {
    defm r: AVX512_maskable_3src<opc, MRMSrcReg, _, (outs _.RC:$dst),
            (ins _.RC:$src2, _.RC:$src3),
            OpcodeStr, "$src3, $src2", "$src2, $src3",
            (_.VT (OpNode _.RC:$src2, _.RC:$src3, _.RC:$src1)), IsCommutable>, EVEX, VVVV;

    defm m: AVX512_maskable_3src<opc, MRMSrcMem, _, (outs _.RC:$dst),
            (ins _.RC:$src2, _.MemOp:$src3),
            OpcodeStr, "$src3, $src2", "$src2, $src3",
            (_.VT (OpNode _.RC:$src2, (_.LdFrag addr:$src3), _.RC:$src1))>, EVEX, VVVV;

    defm mb: AVX512_maskable_3src<opc, MRMSrcMem, _, (outs _.RC:$dst),
            (ins _.RC:$src2, _.ScalarMemOp:$src3),
            OpcodeStr, !strconcat("${src3}", _.BroadcastStr,", $src2"), !strconcat("$src2, ${src3}", _.BroadcastStr),
            (_.VT (OpNode _.RC:$src2, (_.VT (_.BroadcastLdFrag addr:$src3)), _.RC:$src1))>, EVEX_B, EVEX, VVVV;
  }
} // Constraints = "@earlyclobber $dst, $src1 = $dst"

multiclass avx512_cfmaop_round<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                 X86VectorVTInfo _> {
  let Constraints = "@earlyclobber $dst, $src1 = $dst" in
  defm rb: AVX512_maskable_3src<opc, MRMSrcReg, _, (outs _.RC:$dst),
          (ins _.RC:$src2, _.RC:$src3, AVX512RC:$rc),
          OpcodeStr, "$rc, $src3, $src2", "$src2, $src3, $rc",
          (_.VT (OpNode _.RC:$src2, _.RC:$src3, _.RC:$src1, (i32 timm:$rc)))>,
          EVEX, VVVV, EVEX_B, EVEX_RC;
}


multiclass avx512_cfmaop_common<bits<8> opc, string OpcodeStr, SDNode OpNode, SDNode OpNodeRnd, bit IsCommutable> {
  let Predicates = [HasFP16] in {
    defm Z    : avx512_cfmaop_rm<opc, OpcodeStr, OpNode, v16f32_info, IsCommutable>,
                avx512_cfmaop_round<opc, OpcodeStr, OpNodeRnd, v16f32_info>,
                      EVEX_V512, Sched<[WriteFMAZ]>;
  }
  let Predicates = [HasVLX, HasFP16] in {
    defm Z256 : avx512_cfmaop_rm<opc, OpcodeStr, OpNode, v8f32x_info, IsCommutable>, EVEX_V256, Sched<[WriteFMAY]>;
    defm Z128 : avx512_cfmaop_rm<opc, OpcodeStr, OpNode, v4f32x_info, IsCommutable>, EVEX_V128, Sched<[WriteFMAX]>;
  }
}

multiclass avx512_cfmulop_common<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                 SDNode MaskOpNode, SDNode OpNodeRnd, bit IsCommutable> {
  let Predicates = [HasFP16] in {
    defm Z    : avx512_fp_packed<opc, OpcodeStr, OpNode, MaskOpNode, v16f32_info,
                                 WriteFMAZ, IsCommutable, IsCommutable, "", "@earlyclobber $dst", 0>,
                avx512_fp_round_packed<opc, OpcodeStr, OpNodeRnd, WriteFMAZ, v16f32_info,
                                       "", "@earlyclobber $dst">, EVEX_V512;
  }
  let Predicates = [HasVLX, HasFP16] in {
    defm Z256 : avx512_fp_packed<opc, OpcodeStr, OpNode, MaskOpNode, v8f32x_info,
                                 WriteFMAY, IsCommutable, IsCommutable, "", "@earlyclobber $dst", 0>, EVEX_V256;
    defm Z128 : avx512_fp_packed<opc, OpcodeStr, OpNode, MaskOpNode, v4f32x_info,
                                 WriteFMAX, IsCommutable, IsCommutable, "", "@earlyclobber $dst", 0>, EVEX_V128;
  }
}


let Uses = [MXCSR] in {
  defm VFMADDCPH  : avx512_cfmaop_common<0x56, "vfmaddcph", x86vfmaddc, x86vfmaddcRnd, 1>,
                                    T_MAP6, XS, EVEX_CD8<32, CD8VF>;
  defm VFCMADDCPH : avx512_cfmaop_common<0x56, "vfcmaddcph", x86vfcmaddc, x86vfcmaddcRnd, 0>,
                                    T_MAP6, XD, EVEX_CD8<32, CD8VF>;

  defm VFMULCPH  : avx512_cfmulop_common<0xD6, "vfmulcph", x86vfmulc, x86vfmulc,
                                         x86vfmulcRnd, 1>, T_MAP6, XS, EVEX_CD8<32, CD8VF>;
  defm VFCMULCPH : avx512_cfmulop_common<0xD6, "vfcmulcph", x86vfcmulc,
                                         x86vfcmulc, x86vfcmulcRnd, 0>, T_MAP6, XD, EVEX_CD8<32, CD8VF>;
}


multiclass avx512_cfmaop_sh_common<bits<8> opc, string OpcodeStr, SDNode OpNode, SDNode OpNodeRnd,
                                   bit IsCommutable> {
  let Predicates = [HasFP16], Constraints = "@earlyclobber $dst, $src1 = $dst" in {
    defm r : AVX512_maskable_3src<opc, MRMSrcReg, v4f32x_info, (outs VR128X:$dst),
                        (ins VR128X:$src2, VR128X:$src3), OpcodeStr,
                        "$src3, $src2", "$src2, $src3",
                        (v4f32 (OpNode VR128X:$src2, VR128X:$src3, VR128X:$src1)), IsCommutable>,
                        Sched<[WriteFMAX]>;
    defm m : AVX512_maskable_3src<opc, MRMSrcMem, v4f32x_info, (outs VR128X:$dst),
                        (ins VR128X:$src2, ssmem:$src3), OpcodeStr,
                        "$src3, $src2", "$src2, $src3",
                        (v4f32 (OpNode VR128X:$src2, (sse_load_f32 addr:$src3), VR128X:$src1))>,
                        Sched<[WriteFMAX.Folded, WriteFMAX.ReadAfterFold]>;
    defm rb : AVX512_maskable_3src<opc,  MRMSrcReg, v4f32x_info, (outs VR128X:$dst),
                        (ins VR128X:$src2, VR128X:$src3, AVX512RC:$rc), OpcodeStr,
                        "$rc, $src3, $src2", "$src2, $src3, $rc",
                        (v4f32 (OpNodeRnd VR128X:$src2, VR128X:$src3, VR128X:$src1, (i32 timm:$rc)))>,
                        EVEX_B, EVEX_RC, Sched<[WriteFMAX]>;
  }
}

multiclass avx512_cfmbinop_sh_common<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                     SDNode OpNodeRnd, bit IsCommutable> {
  let Predicates = [HasFP16] in {
    defm rr : AVX512_maskable<opc, MRMSrcReg, f32x_info, (outs VR128X:$dst),
                        (ins VR128X:$src1, VR128X:$src2), OpcodeStr,
                        "$src2, $src1", "$src1, $src2",
                        (v4f32 (OpNode VR128X:$src1, VR128X:$src2)),
                        IsCommutable, IsCommutable, IsCommutable,
                        X86selects, "@earlyclobber $dst">, Sched<[WriteFMAX]>;
    defm rm : AVX512_maskable<opc, MRMSrcMem, f32x_info, (outs VR128X:$dst),
                        (ins VR128X:$src1, ssmem:$src2), OpcodeStr,
                        "$src2, $src1", "$src1, $src2",
                        (v4f32 (OpNode VR128X:$src1, (sse_load_f32 addr:$src2))),
                        0, 0, 0, X86selects, "@earlyclobber $dst">,
                        Sched<[WriteFMAX.Folded, WriteFMAX.ReadAfterFold]>;
    defm rrb : AVX512_maskable<opc, MRMSrcReg, f32x_info, (outs VR128X:$dst),
                        (ins VR128X:$src1, VR128X:$src2, AVX512RC:$rc), OpcodeStr,
                        "$rc, $src2, $src1", "$src1, $src2, $rc",
                        (OpNodeRnd (v4f32 VR128X:$src1), (v4f32 VR128X:$src2), (i32 timm:$rc)),
                        0, 0, 0, X86selects, "@earlyclobber $dst">,
                        EVEX_B, EVEX_RC, Sched<[WriteFMAX]>;
  }
}

let Uses = [MXCSR] in {
  defm VFMADDCSHZ  : avx512_cfmaop_sh_common<0x57, "vfmaddcsh", x86vfmaddcSh, x86vfmaddcShRnd, 1>,
                                    T_MAP6, XS, EVEX_CD8<32, CD8VT1>, EVEX_V128, EVEX, VVVV;
  defm VFCMADDCSHZ : avx512_cfmaop_sh_common<0x57, "vfcmaddcsh", x86vfcmaddcSh, x86vfcmaddcShRnd, 0>,
                                    T_MAP6, XD, EVEX_CD8<32, CD8VT1>, EVEX_V128, EVEX, VVVV;

  defm VFMULCSHZ  : avx512_cfmbinop_sh_common<0xD7, "vfmulcsh", x86vfmulcSh, x86vfmulcShRnd, 1>,
                                    T_MAP6, XS, EVEX_CD8<32, CD8VT1>, EVEX_V128, VEX_LIG, EVEX, VVVV;
  defm VFCMULCSHZ : avx512_cfmbinop_sh_common<0xD7, "vfcmulcsh", x86vfcmulcSh, x86vfcmulcShRnd, 0>,
                                    T_MAP6, XD, EVEX_CD8<32, CD8VT1>, EVEX_V128, VEX_LIG, EVEX, VVVV;
}
