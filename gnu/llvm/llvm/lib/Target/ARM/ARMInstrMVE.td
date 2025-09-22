//===-- ARMInstrMVE.td - MVE support for ARM ---------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the ARM MVE instruction set.
//
//===----------------------------------------------------------------------===//

// VPT condition mask
def vpt_mask : Operand<i32> {
  let PrintMethod = "printVPTMask";
  let ParserMatchClass = it_mask_asmoperand;
  let EncoderMethod = "getVPTMaskOpValue";
  let DecoderMethod = "DecodeVPTMaskOperand";
}

// VPT/VCMP restricted predicate for sign invariant types
def pred_restricted_i_asmoperand : AsmOperandClass {
  let Name = "CondCodeRestrictedI";
  let RenderMethod = "addITCondCodeOperands";
  let PredicateMethod = "isITCondCodeRestrictedI";
  let ParserMethod = "parseITCondCode";
  let DiagnosticString = "condition code for sign-independent integer "#
                         "comparison must be EQ or NE";
}

// VPT/VCMP restricted predicate for signed types
def pred_restricted_s_asmoperand : AsmOperandClass {
  let Name = "CondCodeRestrictedS";
  let RenderMethod = "addITCondCodeOperands";
  let PredicateMethod = "isITCondCodeRestrictedS";
  let ParserMethod = "parseITCondCode";
  let DiagnosticString = "condition code for signed integer "#
                         "comparison must be EQ, NE, LT, GT, LE or GE";
}

// VPT/VCMP restricted predicate for unsigned types
def pred_restricted_u_asmoperand : AsmOperandClass {
  let Name = "CondCodeRestrictedU";
  let RenderMethod = "addITCondCodeOperands";
  let PredicateMethod = "isITCondCodeRestrictedU";
  let ParserMethod = "parseITCondCode";
  let DiagnosticString = "condition code for unsigned integer "#
                         "comparison must be EQ, NE, HS or HI";
}

// VPT/VCMP restricted predicate for floating point
def pred_restricted_fp_asmoperand : AsmOperandClass {
  let Name = "CondCodeRestrictedFP";
  let RenderMethod = "addITCondCodeOperands";
  let PredicateMethod = "isITCondCodeRestrictedFP";
  let ParserMethod = "parseITCondCode";
  let DiagnosticString = "condition code for floating-point "#
                         "comparison must be EQ, NE, LT, GT, LE or GE";
}

class VCMPPredicateOperand : Operand<i32>;

def pred_basic_i : VCMPPredicateOperand {
  let PrintMethod = "printMandatoryRestrictedPredicateOperand";
  let ParserMatchClass = pred_restricted_i_asmoperand;
  let DecoderMethod = "DecodeRestrictedIPredicateOperand";
  let EncoderMethod = "getRestrictedCondCodeOpValue";
}

def pred_basic_u : VCMPPredicateOperand {
  let PrintMethod = "printMandatoryRestrictedPredicateOperand";
  let ParserMatchClass = pred_restricted_u_asmoperand;
  let DecoderMethod = "DecodeRestrictedUPredicateOperand";
  let EncoderMethod = "getRestrictedCondCodeOpValue";
}

def pred_basic_s : VCMPPredicateOperand {
  let PrintMethod = "printMandatoryRestrictedPredicateOperand";
  let ParserMatchClass = pred_restricted_s_asmoperand;
  let DecoderMethod = "DecodeRestrictedSPredicateOperand";
  let EncoderMethod = "getRestrictedCondCodeOpValue";
}

def pred_basic_fp : VCMPPredicateOperand {
  let PrintMethod = "printMandatoryRestrictedPredicateOperand";
  let ParserMatchClass = pred_restricted_fp_asmoperand;
  let DecoderMethod = "DecodeRestrictedFPPredicateOperand";
  let EncoderMethod = "getRestrictedCondCodeOpValue";
}

// Register list operands for interleaving load/stores
def VecList2QAsmOperand : AsmOperandClass {
  let Name = "VecListTwoMQ";
  let ParserMethod = "parseVectorList";
  let RenderMethod = "addMVEVecListOperands";
  let DiagnosticString = "operand must be a list of two consecutive "#
                         "q-registers in range [q0,q7]";
}

def VecList2Q : RegisterOperand<MQQPR, "printMVEVectorListTwoQ"> {
  let ParserMatchClass = VecList2QAsmOperand;
  let PrintMethod = "printMVEVectorList<2>";
}

def VecList4QAsmOperand : AsmOperandClass {
  let Name = "VecListFourMQ";
  let ParserMethod = "parseVectorList";
  let RenderMethod = "addMVEVecListOperands";
  let DiagnosticString = "operand must be a list of four consecutive "#
                         "q-registers in range [q0,q7]";
}

def VecList4Q : RegisterOperand<MQQQQPR, "printMVEVectorListFourQ"> {
  let ParserMatchClass = VecList4QAsmOperand;
  let PrintMethod = "printMVEVectorList<4>";
}

// taddrmode_imm7  := reg[r0-r7] +/- (imm7 << shift)
class TMemImm7ShiftOffsetAsmOperand<int shift> : AsmOperandClass {
  let Name = "TMemImm7Shift"#shift#"Offset";
  let PredicateMethod = "isMemImm7ShiftedOffset<"#shift#",ARM::tGPRRegClassID>";
  let RenderMethod = "addMemImmOffsetOperands";
}

class taddrmode_imm7<int shift> : MemOperand,
    ComplexPattern<i32, 2, "SelectTAddrModeImm7<"#shift#">", []>  {
  let ParserMatchClass = TMemImm7ShiftOffsetAsmOperand<shift>;
  // They are printed the same way as the T2 imm8 version
  let PrintMethod = "printT2AddrModeImm8Operand<false>";
  // This can also be the same as the T2 version.
  let EncoderMethod = "getT2AddrModeImmOpValue<7,"#shift#">";
  let DecoderMethod = "DecodeTAddrModeImm7<"#shift#">";
  let MIOperandInfo = (ops tGPR:$base, i32imm:$offsimm);
}

// t2addrmode_imm7  := reg +/- (imm7)
class MemImm7ShiftOffsetAsmOperand<int shift> : AsmOperandClass {
  let Name = "MemImm7Shift"#shift#"Offset";
  let PredicateMethod = "isMemImm7ShiftedOffset<" # shift #
                        ",ARM::GPRnopcRegClassID>";
  let RenderMethod = "addMemImmOffsetOperands";
}

def MemImm7Shift0OffsetAsmOperand : MemImm7ShiftOffsetAsmOperand<0>;
def MemImm7Shift1OffsetAsmOperand : MemImm7ShiftOffsetAsmOperand<1>;
def MemImm7Shift2OffsetAsmOperand : MemImm7ShiftOffsetAsmOperand<2>;
class T2AddrMode_Imm7<int shift> : MemOperand,
      ComplexPattern<i32, 2, "SelectT2AddrModeImm7<"#shift#">", []> {
  let EncoderMethod = "getT2AddrModeImmOpValue<7,"#shift#">";
  let DecoderMethod = "DecodeT2AddrModeImm7<"#shift#", 0>";
  let ParserMatchClass =
    !cast<AsmOperandClass>("MemImm7Shift"#shift#"OffsetAsmOperand");
  let MIOperandInfo = (ops GPRnopc:$base, i32imm:$offsimm);
}

class t2addrmode_imm7<int shift> : T2AddrMode_Imm7<shift> {
  // They are printed the same way as the imm8 version
  let PrintMethod = "printT2AddrModeImm8Operand<false>";
}

class MemImm7ShiftOffsetWBAsmOperand<int shift> : AsmOperandClass {
  let Name = "MemImm7Shift"#shift#"OffsetWB";
  let PredicateMethod = "isMemImm7ShiftedOffset<" # shift #
                        ",ARM::rGPRRegClassID>";
  let RenderMethod = "addMemImmOffsetOperands";
}

def MemImm7Shift0OffsetWBAsmOperand : MemImm7ShiftOffsetWBAsmOperand<0>;
def MemImm7Shift1OffsetWBAsmOperand : MemImm7ShiftOffsetWBAsmOperand<1>;
def MemImm7Shift2OffsetWBAsmOperand : MemImm7ShiftOffsetWBAsmOperand<2>;

class t2addrmode_imm7_pre<int shift> : T2AddrMode_Imm7<shift> {
  // They are printed the same way as the imm8 version
  let PrintMethod = "printT2AddrModeImm8Operand<true>";
  let ParserMatchClass =
    !cast<AsmOperandClass>("MemImm7Shift"#shift#"OffsetWBAsmOperand");
  let DecoderMethod = "DecodeT2AddrModeImm7<"#shift#", 1>";
  let MIOperandInfo = (ops rGPR:$base, i32imm:$offsim);
}

class t2am_imm7shiftOffsetAsmOperand<int shift>
  : AsmOperandClass { let Name = "Imm7Shift"#shift; }
def t2am_imm7shift0OffsetAsmOperand : t2am_imm7shiftOffsetAsmOperand<0>;
def t2am_imm7shift1OffsetAsmOperand : t2am_imm7shiftOffsetAsmOperand<1>;
def t2am_imm7shift2OffsetAsmOperand : t2am_imm7shiftOffsetAsmOperand<2>;

class t2am_imm7_offset<int shift> : MemOperand,
      ComplexPattern<i32, 1, "SelectT2AddrModeImm7Offset<"#shift#">",
                     [], [SDNPWantRoot]> {
  // They are printed the same way as the imm8 version
  let PrintMethod = "printT2AddrModeImm8OffsetOperand";
  let ParserMatchClass =
    !cast<AsmOperandClass>("t2am_imm7shift"#shift#"OffsetAsmOperand");
  let EncoderMethod = "getT2ScaledImmOpValue<7,"#shift#">";
  let DecoderMethod = "DecodeT2Imm7<"#shift#">";
}

// Operands for gather/scatter loads of the form [Rbase, Qoffsets]
class MemRegRQOffsetAsmOperand<int shift> : AsmOperandClass {
  let Name = "MemRegRQS"#shift#"Offset";
  let PredicateMethod = "isMemRegRQOffset<"#shift#">";
  let RenderMethod = "addMemRegRQOffsetOperands";
}

def MemRegRQS0OffsetAsmOperand : MemRegRQOffsetAsmOperand<0>;
def MemRegRQS1OffsetAsmOperand : MemRegRQOffsetAsmOperand<1>;
def MemRegRQS2OffsetAsmOperand : MemRegRQOffsetAsmOperand<2>;
def MemRegRQS3OffsetAsmOperand : MemRegRQOffsetAsmOperand<3>;

// mve_addr_rq_shift  := reg + vreg{ << UXTW #shift}
class mve_addr_rq_shift<int shift> : MemOperand {
  let EncoderMethod = "getMveAddrModeRQOpValue";
  let PrintMethod = "printMveAddrModeRQOperand<"#shift#">";
  let ParserMatchClass =
    !cast<AsmOperandClass>("MemRegRQS"#shift#"OffsetAsmOperand");
  let DecoderMethod = "DecodeMveAddrModeRQ";
  let MIOperandInfo = (ops GPRnopc:$base, MQPR:$offsreg);
}

class MemRegQOffsetAsmOperand<int shift> : AsmOperandClass {
  let Name = "MemRegQS"#shift#"Offset";
  let PredicateMethod = "isMemRegQOffset<"#shift#">";
  let RenderMethod = "addMemImmOffsetOperands";
}

def MemRegQS2OffsetAsmOperand : MemRegQOffsetAsmOperand<2>;
def MemRegQS3OffsetAsmOperand : MemRegQOffsetAsmOperand<3>;

// mve_addr_q_shift  := vreg {+ #imm7s2/4}
class mve_addr_q_shift<int shift> : MemOperand {
  let EncoderMethod = "getMveAddrModeQOpValue<"#shift#">";
  // Can be printed same way as other reg + imm operands
  let PrintMethod = "printT2AddrModeImm8Operand<false>";
  let ParserMatchClass =
    !cast<AsmOperandClass>("MemRegQS"#shift#"OffsetAsmOperand");
  let DecoderMethod = "DecodeMveAddrModeQ<"#shift#">";
  let MIOperandInfo = (ops MQPR:$base, i32imm:$imm);
}

// A family of classes wrapping up information about the vector types
// used by MVE.
class MVEVectorVTInfo<ValueType vec, ValueType dblvec,
                      ValueType pred, ValueType dblpred,
                      bits<2> size, string suffixletter, bit unsigned> {
  // The LLVM ValueType representing the vector, so we can use it in
  // ISel patterns.
  ValueType Vec = vec;

  // The LLVM ValueType representing a vector with elements double the size
  // of those in Vec, so we can use it in ISel patterns. It is up to the
  // invoker of this class to ensure that this is a correct choice.
  ValueType DblVec = dblvec;

  // An LLVM ValueType representing a corresponding vector of
  // predicate bits, for use in ISel patterns that handle an IR
  // intrinsic describing the predicated form of the instruction.
  ValueType Pred = pred;

  // Same as Pred but for DblVec rather than Vec.
  ValueType DblPred = dblpred;

  // The most common representation of the vector element size in MVE
  // instruction encodings: a 2-bit value V representing an (8<<V)-bit
  // vector element.
  bits<2> Size = size;

  // For vectors explicitly mentioning a signedness of integers: 0 for
  // signed and 1 for unsigned. For anything else, undefined.
  bit Unsigned = unsigned;

  // The number of bits in a vector element, in integer form.
  int LaneBits = !shl(8, Size);

  // The suffix used in assembly language on an instruction operating
  // on this lane if it only cares about number of bits.
  string BitsSuffix = !if(!eq(suffixletter, "p"),
                          !if(!eq(unsigned, 0b0), "8", "16"),
                          !cast<string>(LaneBits));

  // The suffix used on an instruction that mentions the whole type.
  string Suffix = suffixletter # BitsSuffix;

  // The letter part of the suffix only.
  string SuffixLetter = suffixletter;
}

// Integer vector types that don't treat signed and unsigned differently.
def MVE_v16i8 : MVEVectorVTInfo<v16i8, v8i16, v16i1, v8i1, 0b00, "i", ?>;
def MVE_v8i16 : MVEVectorVTInfo<v8i16, v4i32, v8i1,  v4i1, 0b01, "i", ?>;
def MVE_v4i32 : MVEVectorVTInfo<v4i32, v2i64, v4i1,  v2i1, 0b10, "i", ?>;
def MVE_v2i64 : MVEVectorVTInfo<v2i64, ?,     v2i1,  ?,    0b11, "i", ?>;

// Explicitly signed and unsigned integer vectors. They map to the
// same set of LLVM ValueTypes as above, but are represented
// differently in assembly and instruction encodings.
def MVE_v16s8 : MVEVectorVTInfo<v16i8, v8i16, v16i1, v8i1, 0b00, "s", 0b0>;
def MVE_v8s16 : MVEVectorVTInfo<v8i16, v4i32, v8i1,  v4i1, 0b01, "s", 0b0>;
def MVE_v4s32 : MVEVectorVTInfo<v4i32, v2i64, v4i1,  v2i1, 0b10, "s", 0b0>;
def MVE_v2s64 : MVEVectorVTInfo<v2i64, ?,     v2i1,  ?,    0b11, "s", 0b0>;
def MVE_v16u8 : MVEVectorVTInfo<v16i8, v8i16, v16i1, v8i1, 0b00, "u", 0b1>;
def MVE_v8u16 : MVEVectorVTInfo<v8i16, v4i32, v8i1,  v4i1, 0b01, "u", 0b1>;
def MVE_v4u32 : MVEVectorVTInfo<v4i32, v2i64, v4i1,  v2i1, 0b10, "u", 0b1>;
def MVE_v2u64 : MVEVectorVTInfo<v2i64, ?,     v2i1,  ?,    0b11, "u", 0b1>;

// FP vector types.
def MVE_v8f16 : MVEVectorVTInfo<v8f16, v4f32, v8i1,  v4i1, 0b01, "f", ?>;
def MVE_v4f32 : MVEVectorVTInfo<v4f32, v2f64, v4i1,  v2i1, 0b10, "f", ?>;
def MVE_v2f64 : MVEVectorVTInfo<v2f64, ?,     v2i1,  ?,    0b11, "f", ?>;

// Polynomial vector types.
def MVE_v16p8 : MVEVectorVTInfo<v16i8, v8i16, v16i1, v8i1, 0b11, "p", 0b0>;
def MVE_v8p16 : MVEVectorVTInfo<v8i16, v4i32, v8i1,  v4i1, 0b11, "p", 0b1>;

multiclass MVE_TwoOpPattern<MVEVectorVTInfo VTI, SDPatternOperator Op, Intrinsic PredInt,
                            dag PredOperands, Instruction Inst,
                            SDPatternOperator IdentityVec = null_frag> {
  // Unpredicated
  def : Pat<(VTI.Vec (Op (VTI.Vec MQPR:$Qm), (VTI.Vec MQPR:$Qn))),
            (VTI.Vec (Inst (VTI.Vec MQPR:$Qm), (VTI.Vec MQPR:$Qn)))>;

  // Predicated with select
  if !ne(VTI.Size, 0b11) then {
    def : Pat<(VTI.Vec (vselect (VTI.Pred VCCR:$mask),
                                (VTI.Vec (Op (VTI.Vec MQPR:$Qm),
                                             (VTI.Vec MQPR:$Qn))),
                                (VTI.Vec MQPR:$inactive))),
              (VTI.Vec (Inst (VTI.Vec MQPR:$Qm), (VTI.Vec MQPR:$Qn),
                              ARMVCCThen, (VTI.Pred VCCR:$mask), zero_reg,
                              (VTI.Vec MQPR:$inactive)))>;

    // Optionally with the select folded through the op
    def : Pat<(VTI.Vec (Op (VTI.Vec MQPR:$Qm),
                           (VTI.Vec (vselect (VTI.Pred VCCR:$mask),
                                             (VTI.Vec MQPR:$Qn),
                                             (VTI.Vec IdentityVec))))),
              (VTI.Vec (Inst (VTI.Vec MQPR:$Qm), (VTI.Vec MQPR:$Qn),
                              ARMVCCThen, (VTI.Pred VCCR:$mask), zero_reg,
                              (VTI.Vec MQPR:$Qm)))>;
  }

  // Predicated with intrinsic
  def : Pat<(VTI.Vec !con((PredInt (VTI.Vec MQPR:$Qm), (VTI.Vec MQPR:$Qn)),
                          PredOperands,
                          (? (VTI.Pred VCCR:$mask), (VTI.Vec MQPR:$inactive)))),
            (VTI.Vec (Inst (VTI.Vec MQPR:$Qm), (VTI.Vec MQPR:$Qn),
                            ARMVCCThen, (VTI.Pred VCCR:$mask), zero_reg,
                            (VTI.Vec MQPR:$inactive)))>;
}

multiclass MVE_TwoOpPatternDup<MVEVectorVTInfo VTI, SDPatternOperator Op, Intrinsic PredInt,
                               dag PredOperands, Instruction Inst,
                               SDPatternOperator IdentityVec = null_frag> {
  // Unpredicated
  def : Pat<(VTI.Vec (Op (VTI.Vec MQPR:$Qm), (VTI.Vec (ARMvdup rGPR:$Rn)))),
            (VTI.Vec (Inst (VTI.Vec MQPR:$Qm), rGPR:$Rn))>;

  // Predicated with select
  if !ne(VTI.Size, 0b11) then {
    def : Pat<(VTI.Vec (vselect (VTI.Pred VCCR:$mask),
                                (VTI.Vec (Op (VTI.Vec MQPR:$Qm),
                                             (VTI.Vec (ARMvdup rGPR:$Rn)))),
                                (VTI.Vec MQPR:$inactive))),
              (VTI.Vec (Inst (VTI.Vec MQPR:$Qm), rGPR:$Rn,
                              ARMVCCThen, (VTI.Pred VCCR:$mask), zero_reg,
                              (VTI.Vec MQPR:$inactive)))>;

    // Optionally with the select folded through the op
    def : Pat<(VTI.Vec (Op (VTI.Vec MQPR:$Qm),
                           (VTI.Vec (vselect (VTI.Pred VCCR:$mask),
                                             (ARMvdup rGPR:$Rn),
                                             (VTI.Vec IdentityVec))))),
              (VTI.Vec (Inst (VTI.Vec MQPR:$Qm), rGPR:$Rn,
                              ARMVCCThen, (VTI.Pred VCCR:$mask), zero_reg,
                              (VTI.Vec MQPR:$Qm)))>;
  }

  // Predicated with intrinsic
  def : Pat<(VTI.Vec !con((PredInt (VTI.Vec MQPR:$Qm), (VTI.Vec (ARMvdup rGPR:$Rn))),
                          PredOperands,
                          (? (VTI.Pred VCCR:$mask), (VTI.Vec MQPR:$inactive)))),
            (VTI.Vec (Inst (VTI.Vec MQPR:$Qm), rGPR:$Rn,
                            ARMVCCThen, (VTI.Pred VCCR:$mask), zero_reg,
                            (VTI.Vec MQPR:$inactive)))>;
}

// --------- Start of base classes for the instructions themselves

class MVE_MI<dag oops, dag iops, InstrItinClass itin, string asm,
             string ops, string cstr, bits<2> vecsize, list<dag> pattern>
  : Thumb2XI<oops, iops, AddrModeNone, 4, itin, !strconcat(asm, "\t", ops), cstr,
             pattern>,
    Requires<[HasMVEInt]> {
  let D = MVEDomain;
  let DecoderNamespace = "MVE";
  let VecSize = vecsize;
}

// MVE_p is used for most predicated instructions, to add the cluster
// of input operands that provides the VPT suffix (none, T or E) and
// the input predicate register.
class MVE_p<dag oops, dag iops, InstrItinClass itin, string iname,
            string suffix, string ops, vpred_ops vpred, string cstr,
            bits<2> vecsize, list<dag> pattern=[]>
  : MVE_MI<oops, !con(iops, (ins vpred:$vp)), itin,
           // If the instruction has a suffix, like vadd.f32, then the
           // VPT predication suffix goes before the dot, so the full
           // name has to be "vadd${vp}.f32".
           !strconcat(iname, "${vp}",
                      !if(!eq(suffix, ""), "", !strconcat(".", suffix))),
           ops, !strconcat(cstr, vpred.vpred_constraint), vecsize, pattern> {
  let Inst{31-29} = 0b111;
  let Inst{27-26} = 0b11;
}

class MVE_f<dag oops, dag iops, InstrItinClass itin, string iname,
            string suffix, string ops, vpred_ops vpred, string cstr,
            bits<2> vecsize, list<dag> pattern=[]>
  : MVE_p<oops, iops, itin, iname, suffix, ops, vpred, cstr, vecsize, pattern> {
  let Predicates = [HasMVEFloat];
}

class MVE_MI_with_pred<dag oops, dag iops, InstrItinClass itin, string asm,
                       string ops, string cstr, list<dag> pattern>
  : Thumb2I<oops, iops, AddrModeNone, 4, itin, asm, !strconcat("\t", ops), cstr,
             pattern>,
    Requires<[HasV8_1MMainline, HasMVEInt]> {
  let D = MVEDomain;
  let DecoderNamespace = "MVE";
}

class MVE_VMOV_lane_base<dag oops, dag iops, InstrItinClass itin, string asm,
                         string suffix, string ops, string cstr,
                         list<dag> pattern>
  : Thumb2I<oops, iops, AddrModeNone, 4, itin, asm,
            !if(!eq(suffix, ""), "", "." # suffix) # "\t" # ops,
            cstr, pattern>,
    Requires<[HasV8_1MMainline, HasMVEInt]> {
  let D = MVEDomain;
  let DecoderNamespace = "MVE";
}

class MVE_ScalarShift<string iname, dag oops, dag iops, string asm, string cstr,
            list<dag> pattern=[]>
  : MVE_MI_with_pred<oops, iops, NoItinerary, iname, asm, cstr, pattern> {
  let Inst{31-20} = 0b111010100101;
  let Inst{8} = 0b1;
  let validForTailPredication=1;
}

class MVE_ScalarShiftSingleReg<string iname, dag iops, string asm, string cstr,
                    list<dag> pattern=[]>
  : MVE_ScalarShift<iname, (outs rGPR:$RdaDest), iops, asm, cstr, pattern> {
  bits<4> RdaDest;

  let Inst{19-16} = RdaDest{3-0};
}

class MVE_ScalarShiftSRegImm<string iname, bits<2> op5_4>
  : MVE_ScalarShiftSingleReg<iname, (ins rGPR:$RdaSrc, long_shift:$imm),
                     "$RdaSrc, $imm", "$RdaDest = $RdaSrc",
                     [(set rGPR:$RdaDest,
                          (i32 (!cast<Intrinsic>("int_arm_mve_" # iname)
                                    (i32 rGPR:$RdaSrc), (i32 imm:$imm))))]> {
  bits<5> imm;

  let Inst{15} = 0b0;
  let Inst{14-12} = imm{4-2};
  let Inst{11-8} = 0b1111;
  let Inst{7-6} = imm{1-0};
  let Inst{5-4} = op5_4{1-0};
  let Inst{3-0} = 0b1111;
}

def MVE_SQSHL : MVE_ScalarShiftSRegImm<"sqshl", 0b11>;
def MVE_SRSHR : MVE_ScalarShiftSRegImm<"srshr", 0b10>;
def MVE_UQSHL : MVE_ScalarShiftSRegImm<"uqshl", 0b00>;
def MVE_URSHR : MVE_ScalarShiftSRegImm<"urshr", 0b01>;

class MVE_ScalarShiftSRegReg<string iname, bits<2> op5_4>
  : MVE_ScalarShiftSingleReg<iname, (ins rGPR:$RdaSrc, rGPR:$Rm),
                     "$RdaSrc, $Rm", "@earlyclobber $RdaDest,$RdaDest = $RdaSrc",
                     [(set rGPR:$RdaDest,
                         (i32 (!cast<Intrinsic>("int_arm_mve_" # iname)
                                   (i32 rGPR:$RdaSrc), (i32 rGPR:$Rm))))]> {
  bits<4> Rm;

  let Inst{15-12} = Rm{3-0};
  let Inst{11-8} = 0b1111;
  let Inst{7-6} = 0b00;
  let Inst{5-4} = op5_4{1-0};
  let Inst{3-0} = 0b1101;

  let Unpredictable{8-6} = 0b111;
}

def MVE_SQRSHR : MVE_ScalarShiftSRegReg<"sqrshr", 0b10>;
def MVE_UQRSHL : MVE_ScalarShiftSRegReg<"uqrshl", 0b00>;

class MVE_ScalarShiftDoubleReg<string iname, dag iops, string asm,
                               string cstr, list<dag> pattern=[]>
  : MVE_ScalarShift<iname, (outs tGPREven:$RdaLo, tGPROdd:$RdaHi),
                    iops, asm, cstr, pattern> {
  bits<4> RdaLo;
  bits<4> RdaHi;

  let Inst{19-17} = RdaLo{3-1};
  let Inst{11-9} = RdaHi{3-1};

  let hasSideEffects = 0;
}

class MVE_ScalarShiftDRegImm<string iname, bits<2> op5_4, bit op16,
                             list<dag> pattern=[]>
  : MVE_ScalarShiftDoubleReg<
      iname, (ins tGPREven:$RdaLo_src, tGPROdd:$RdaHi_src, long_shift:$imm),
      "$RdaLo, $RdaHi, $imm", "$RdaLo = $RdaLo_src,$RdaHi = $RdaHi_src",
      pattern> {
  bits<5> imm;

  let Inst{16} = op16;
  let Inst{15} = 0b0;
  let Inst{14-12} = imm{4-2};
  let Inst{7-6} = imm{1-0};
  let Inst{5-4} = op5_4{1-0};
  let Inst{3-0} = 0b1111;
}

class MVE_ScalarShiftDRegRegBase<string iname, dag iops, string asm,
                                 bit op5, bit op16, list<dag> pattern=[]>
  : MVE_ScalarShiftDoubleReg<
     iname, iops, asm, "@earlyclobber $RdaHi,@earlyclobber $RdaLo,"
                       "$RdaLo = $RdaLo_src,$RdaHi = $RdaHi_src",
     pattern> {
  bits<4> Rm;

  let Inst{16} = op16;
  let Inst{15-12} = Rm{3-0};
  let Inst{6} = 0b0;
  let Inst{5} = op5;
  let Inst{4} = 0b0;
  let Inst{3-0} = 0b1101;

  // Custom decoder method because of the following overlapping encodings:
  // ASRL and SQRSHR
  // LSLL and UQRSHL
  // SQRSHRL and SQRSHR
  // UQRSHLL and UQRSHL
  let DecoderMethod = "DecodeMVEOverlappingLongShift";
}

class MVE_ScalarShiftDRegReg<string iname, bit op5, list<dag> pattern=[]>
  : MVE_ScalarShiftDRegRegBase<
     iname, (ins tGPREven:$RdaLo_src, tGPROdd:$RdaHi_src, rGPR:$Rm),
     "$RdaLo, $RdaHi, $Rm", op5, 0b0, pattern> {

  let Inst{7} = 0b0;
}

class MVE_ScalarShiftDRegRegWithSat<string iname, bit op5, list<dag> pattern=[]>
  : MVE_ScalarShiftDRegRegBase<
     iname, (ins tGPREven:$RdaLo_src, tGPROdd:$RdaHi_src, rGPR:$Rm, saturateop:$sat),
     "$RdaLo, $RdaHi, $sat, $Rm", op5, 0b1, pattern> {
  bit sat;

  let Inst{7} = sat;
}

def MVE_ASRLr   : MVE_ScalarShiftDRegReg<"asrl",    0b1,  [(set tGPREven:$RdaLo, tGPROdd:$RdaHi,
                                        (ARMasrl tGPREven:$RdaLo_src,
                                        tGPROdd:$RdaHi_src, rGPR:$Rm))]>;
def MVE_ASRLi   : MVE_ScalarShiftDRegImm<"asrl",    0b10, ?, [(set tGPREven:$RdaLo, tGPROdd:$RdaHi,
                                        (ARMasrl tGPREven:$RdaLo_src,
                                        tGPROdd:$RdaHi_src, (i32 long_shift:$imm)))]>;
def MVE_LSLLr   : MVE_ScalarShiftDRegReg<"lsll",    0b0,  [(set tGPREven:$RdaLo, tGPROdd:$RdaHi,
                                        (ARMlsll tGPREven:$RdaLo_src,
                                        tGPROdd:$RdaHi_src, rGPR:$Rm))]>;
def MVE_LSLLi   : MVE_ScalarShiftDRegImm<"lsll",    0b00, ?, [(set tGPREven:$RdaLo, tGPROdd:$RdaHi,
                                        (ARMlsll tGPREven:$RdaLo_src,
                                        tGPROdd:$RdaHi_src, (i32 long_shift:$imm)))]>;
def MVE_LSRL    : MVE_ScalarShiftDRegImm<"lsrl",    0b01, ?, [(set tGPREven:$RdaLo, tGPROdd:$RdaHi,
                                        (ARMlsrl tGPREven:$RdaLo_src,
                                        tGPROdd:$RdaHi_src, (i32 long_shift:$imm)))]>;

def MVE_SQRSHRL : MVE_ScalarShiftDRegRegWithSat<"sqrshrl", 0b1>;
def MVE_SQSHLL  : MVE_ScalarShiftDRegImm<"sqshll",  0b11, 0b1>;
def MVE_SRSHRL  : MVE_ScalarShiftDRegImm<"srshrl",  0b10, 0b1>;

def MVE_UQRSHLL : MVE_ScalarShiftDRegRegWithSat<"uqrshll", 0b0>;
def MVE_UQSHLL  : MVE_ScalarShiftDRegImm<"uqshll",  0b00, 0b1>;
def MVE_URSHRL  : MVE_ScalarShiftDRegImm<"urshrl",  0b01, 0b1>;

// start of mve_rDest instructions

class MVE_rDest<dag oops, dag iops, InstrItinClass itin,
                string iname, string suffix,
                string ops, string cstr, bits<2> vecsize, list<dag> pattern=[]>
// Always use vpred_n and not vpred_r: with the output register being
// a GPR and not a vector register, there can't be any question of
// what to put in its inactive lanes.
  : MVE_p<oops, iops, itin, iname, suffix, ops, vpred_n, cstr, vecsize, pattern> {

  let Inst{25-23} = 0b101;
  let Inst{11-9} = 0b111;
  let Inst{4} = 0b0;
}

class MVE_VABAV<string suffix, bit U, bits<2> size>
  : MVE_rDest<(outs rGPR:$Rda), (ins rGPR:$Rda_src, MQPR:$Qn, MQPR:$Qm),
              NoItinerary, "vabav", suffix, "$Rda, $Qn, $Qm", "$Rda = $Rda_src",
              size, []> {
  bits<4> Qm;
  bits<4> Qn;
  bits<4> Rda;

  let Inst{28} = U;
  let Inst{22} = 0b0;
  let Inst{21-20} = size{1-0};
  let Inst{19-17} = Qn{2-0};
  let Inst{16} = 0b0;
  let Inst{15-12} = Rda{3-0};
  let Inst{8} = 0b1;
  let Inst{7} = Qn{3};
  let Inst{6} = 0b0;
  let Inst{5} = Qm{3};
  let Inst{3-1} = Qm{2-0};
  let Inst{0} = 0b1;
  let horizontalReduction = 1;
}

multiclass MVE_VABAV_m<MVEVectorVTInfo VTI> {
  def "" : MVE_VABAV<VTI.Suffix, VTI.Unsigned, VTI.Size>;
  defvar Inst = !cast<Instruction>(NAME);

  let Predicates = [HasMVEInt] in {
    def : Pat<(i32 (int_arm_mve_vabav
                         (i32 VTI.Unsigned),
                         (i32 rGPR:$Rda_src),
                         (VTI.Vec MQPR:$Qn), (VTI.Vec MQPR:$Qm))),
              (i32 (Inst (i32 rGPR:$Rda_src),
                         (VTI.Vec MQPR:$Qn), (VTI.Vec MQPR:$Qm)))>;

    def : Pat<(i32 (int_arm_mve_vabav_predicated
                         (i32 VTI.Unsigned),
                         (i32 rGPR:$Rda_src),
                         (VTI.Vec MQPR:$Qn), (VTI.Vec MQPR:$Qm),
                         (VTI.Pred VCCR:$mask))),
              (i32 (Inst (i32 rGPR:$Rda_src),
                         (VTI.Vec MQPR:$Qn), (VTI.Vec MQPR:$Qm),
                         ARMVCCThen, (VTI.Pred VCCR:$mask), zero_reg))>;
  }
}

defm MVE_VABAVs8  : MVE_VABAV_m<MVE_v16s8>;
defm MVE_VABAVs16 : MVE_VABAV_m<MVE_v8s16>;
defm MVE_VABAVs32 : MVE_VABAV_m<MVE_v4s32>;
defm MVE_VABAVu8  : MVE_VABAV_m<MVE_v16u8>;
defm MVE_VABAVu16 : MVE_VABAV_m<MVE_v8u16>;
defm MVE_VABAVu32 : MVE_VABAV_m<MVE_v4u32>;

class MVE_VADDV<string iname, string suffix, dag iops, string cstr,
              bit A, bit U, bits<2> size, list<dag> pattern=[]>
  : MVE_rDest<(outs tGPREven:$Rda), iops, NoItinerary,
              iname, suffix, "$Rda, $Qm", cstr, size, pattern> {
  bits<3> Qm;
  bits<4> Rda;

  let Inst{28} = U;
  let Inst{22-20} = 0b111;
  let Inst{19-18} = size{1-0};
  let Inst{17-16} = 0b01;
  let Inst{15-13} = Rda{3-1};
  let Inst{12} = 0b0;
  let Inst{8-6} = 0b100;
  let Inst{5} = A;
  let Inst{3-1} = Qm{2-0};
  let Inst{0} = 0b0;
  let horizontalReduction = 1;
  let validForTailPredication = 1;
}

def SDTVecReduceP : SDTypeProfile<1, 2, [    // VADDLVp
  SDTCisInt<0>, SDTCisVec<1>, SDTCisVec<2>
]>;
def ARMVADDVs       : SDNode<"ARMISD::VADDVs", SDTVecReduce>;
def ARMVADDVu       : SDNode<"ARMISD::VADDVu", SDTVecReduce>;
def ARMVADDVps      : SDNode<"ARMISD::VADDVps", SDTVecReduceP>;
def ARMVADDVpu      : SDNode<"ARMISD::VADDVpu", SDTVecReduceP>;

multiclass MVE_VADDV_A<MVEVectorVTInfo VTI> {
  def acc    : MVE_VADDV<"vaddva", VTI.Suffix,
                         (ins tGPREven:$Rda_src, MQPR:$Qm), "$Rda = $Rda_src",
                         0b1, VTI.Unsigned, VTI.Size>;
  def no_acc : MVE_VADDV<"vaddv", VTI.Suffix,
                         (ins MQPR:$Qm), "",
                         0b0, VTI.Unsigned, VTI.Size>;

  defvar InstA = !cast<Instruction>(NAME # "acc");
  defvar InstN = !cast<Instruction>(NAME # "no_acc");

  let Predicates = [HasMVEInt] in {
    if VTI.Unsigned then {
      def : Pat<(i32 (vecreduce_add (VTI.Vec MQPR:$vec))),
                (i32 (InstN $vec))>;
      def : Pat<(i32 (vecreduce_add (VTI.Vec (vselect (VTI.Pred VCCR:$pred),
                                                      (VTI.Vec MQPR:$vec),
                                                      (VTI.Vec ARMimmAllZerosV))))),
                (i32 (InstN $vec, ARMVCCThen, $pred, zero_reg))>;
      def : Pat<(i32 (ARMVADDVu (VTI.Vec MQPR:$vec))),
                (i32 (InstN $vec))>;
      def : Pat<(i32 (ARMVADDVpu (VTI.Vec MQPR:$vec), (VTI.Pred VCCR:$pred))),
                (i32 (InstN $vec, ARMVCCThen, $pred, zero_reg))>;
      def : Pat<(i32 (add (i32 (vecreduce_add (VTI.Vec MQPR:$vec))),
                          (i32 tGPREven:$acc))),
                (i32 (InstA $acc, $vec))>;
      def : Pat<(i32 (add (i32 (vecreduce_add (VTI.Vec (vselect (VTI.Pred VCCR:$pred),
                                                                (VTI.Vec MQPR:$vec),
                                                                (VTI.Vec ARMimmAllZerosV))))),
                          (i32 tGPREven:$acc))),
                (i32 (InstA $acc, $vec, ARMVCCThen, $pred, zero_reg))>;
      def : Pat<(i32 (add (i32 (ARMVADDVu (VTI.Vec MQPR:$vec))),
                          (i32 tGPREven:$acc))),
                (i32 (InstA $acc, $vec))>;
      def : Pat<(i32 (add (i32 (ARMVADDVpu (VTI.Vec MQPR:$vec), (VTI.Pred VCCR:$pred))),
                          (i32 tGPREven:$acc))),
                (i32 (InstA $acc, $vec, ARMVCCThen, $pred, zero_reg))>;
    } else {
      def : Pat<(i32 (ARMVADDVs (VTI.Vec MQPR:$vec))),
                (i32 (InstN $vec))>;
      def : Pat<(i32 (add (i32 (ARMVADDVs (VTI.Vec MQPR:$vec))),
                          (i32 tGPREven:$acc))),
                (i32 (InstA $acc, $vec))>;
      def : Pat<(i32 (ARMVADDVps (VTI.Vec MQPR:$vec), (VTI.Pred VCCR:$pred))),
                (i32 (InstN $vec, ARMVCCThen, $pred, zero_reg))>;
      def : Pat<(i32 (add (i32 (ARMVADDVps (VTI.Vec MQPR:$vec), (VTI.Pred VCCR:$pred))),
                          (i32 tGPREven:$acc))),
                (i32 (InstA $acc, $vec, ARMVCCThen, $pred, zero_reg))>;
    }

    def : Pat<(i32 (int_arm_mve_addv_predicated (VTI.Vec MQPR:$vec),
                                                (i32 VTI.Unsigned),
                                                (VTI.Pred VCCR:$pred))),
              (i32 (InstN $vec, ARMVCCThen, $pred, zero_reg))>;
    def : Pat<(i32 (add (int_arm_mve_addv_predicated (VTI.Vec MQPR:$vec),
                                                     (i32 VTI.Unsigned),
                                                     (VTI.Pred VCCR:$pred)),
                        (i32 tGPREven:$acc))),
              (i32 (InstA $acc, $vec, ARMVCCThen, $pred, zero_reg))>;
  }
}

defm MVE_VADDVs8  : MVE_VADDV_A<MVE_v16s8>;
defm MVE_VADDVs16 : MVE_VADDV_A<MVE_v8s16>;
defm MVE_VADDVs32 : MVE_VADDV_A<MVE_v4s32>;
defm MVE_VADDVu8  : MVE_VADDV_A<MVE_v16u8>;
defm MVE_VADDVu16 : MVE_VADDV_A<MVE_v8u16>;
defm MVE_VADDVu32 : MVE_VADDV_A<MVE_v4u32>;

class MVE_VADDLV<string iname, string suffix, dag iops, string cstr,
               bit A, bit U, list<dag> pattern=[]>
  : MVE_rDest<(outs tGPREven:$RdaLo, tGPROdd:$RdaHi), iops, NoItinerary, iname,
              suffix, "$RdaLo, $RdaHi, $Qm", cstr, 0b10, pattern> {
  bits<3> Qm;
  bits<4> RdaLo;
  bits<4> RdaHi;

  let Inst{28} = U;
  let Inst{22-20} = RdaHi{3-1};
  let Inst{19-18} = 0b10;
  let Inst{17-16} = 0b01;
  let Inst{15-13} = RdaLo{3-1};
  let Inst{12} = 0b0;
  let Inst{8-6} = 0b100;
  let Inst{5} = A;
  let Inst{3-1} = Qm{2-0};
  let Inst{0} = 0b0;
  let horizontalReduction = 1;
}

def SDTVecReduceL : SDTypeProfile<2, 1, [    // VADDLV
  SDTCisInt<0>, SDTCisInt<1>, SDTCisVec<2>
]>;
def SDTVecReduceLA : SDTypeProfile<2, 3, [    // VADDLVA
  SDTCisInt<0>, SDTCisInt<1>, SDTCisInt<2>, SDTCisInt<3>,
  SDTCisVec<4>
]>;
def SDTVecReduceLP : SDTypeProfile<2, 2, [    // VADDLVp
  SDTCisInt<0>, SDTCisInt<1>, SDTCisVec<2>, SDTCisVec<2>
]>;
def SDTVecReduceLPA : SDTypeProfile<2, 4, [    // VADDLVAp
  SDTCisInt<0>, SDTCisInt<1>, SDTCisInt<2>, SDTCisInt<3>,
  SDTCisVec<4>, SDTCisVec<5>
]>;

multiclass MVE_VADDLV_A<MVEVectorVTInfo VTI> {
  def acc    : MVE_VADDLV<"vaddlva", VTI.Suffix,
                        (ins tGPREven:$RdaLo_src, tGPROdd:$RdaHi_src, MQPR:$Qm),
                        "$RdaLo = $RdaLo_src,$RdaHi = $RdaHi_src",
                        0b1, VTI.Unsigned>;
  def no_acc : MVE_VADDLV<"vaddlv", VTI.Suffix,
                        (ins MQPR:$Qm), "",
                        0b0, VTI.Unsigned>;

  defvar InstA = !cast<Instruction>(NAME # "acc");
  defvar InstN = !cast<Instruction>(NAME # "no_acc");

  defvar letter = VTI.SuffixLetter;
  defvar ARMVADDLV = SDNode<"ARMISD::VADDLV" # letter, SDTVecReduceL>;
  defvar ARMVADDLVA = SDNode<"ARMISD::VADDLVA" # letter, SDTVecReduceLA>;
  defvar ARMVADDLVp = SDNode<"ARMISD::VADDLVp" # letter, SDTVecReduceLP>;
  defvar ARMVADDLVAp = SDNode<"ARMISD::VADDLVAp" # letter, SDTVecReduceLPA>;

  let Predicates = [HasMVEInt] in {
    def : Pat<(ARMVADDLV (v4i32 MQPR:$vec)),
              (InstN (v4i32 MQPR:$vec))>;
    def : Pat<(ARMVADDLVA tGPREven:$acclo, tGPROdd:$acchi, (v4i32 MQPR:$vec)),
              (InstA tGPREven:$acclo, tGPROdd:$acchi, (v4i32 MQPR:$vec))>;
    def : Pat<(ARMVADDLVp (v4i32 MQPR:$vec), (VTI.Pred VCCR:$pred)),
              (InstN (v4i32 MQPR:$vec), ARMVCCThen, (VTI.Pred VCCR:$pred), zero_reg)>;
    def : Pat<(ARMVADDLVAp tGPREven:$acclo, tGPROdd:$acchi, (v4i32 MQPR:$vec),
                           (VTI.Pred VCCR:$pred)),
              (InstA tGPREven:$acclo, tGPROdd:$acchi, (v4i32 MQPR:$vec),
                     ARMVCCThen, (VTI.Pred VCCR:$pred), zero_reg)>;
  }
}

defm MVE_VADDLVs32 : MVE_VADDLV_A<MVE_v4s32>;
defm MVE_VADDLVu32 : MVE_VADDLV_A<MVE_v4u32>;

class MVE_VMINMAXNMV<string iname, string suffix, bit sz,
                     bit bit_17, bit bit_7, list<dag> pattern=[]>
  : MVE_rDest<(outs rGPR:$RdaDest), (ins rGPR:$RdaSrc, MQPR:$Qm),
              NoItinerary, iname, suffix, "$RdaSrc, $Qm",
              "$RdaDest = $RdaSrc", !if(sz, 0b01, 0b10), pattern> {
  bits<3> Qm;
  bits<4> RdaDest;

  let Inst{28} = sz;
  let Inst{22-20} = 0b110;
  let Inst{19-18} = 0b11;
  let Inst{17} = bit_17;
  let Inst{16} = 0b0;
  let Inst{15-12} = RdaDest{3-0};
  let Inst{8} = 0b1;
  let Inst{7} = bit_7;
  let Inst{6-5} = 0b00;
  let Inst{3-1} = Qm{2-0};
  let Inst{0} = 0b0;
  let horizontalReduction = 1;

  let Predicates = [HasMVEFloat];
  let hasSideEffects = 0;
}

multiclass MVE_VMINMAXNMV_p<string iname, bit notAbs, bit isMin,
                            MVEVectorVTInfo VTI, string intrBaseName,
                            ValueType Scalar, RegisterClass ScalarReg> {
  def "": MVE_VMINMAXNMV<iname, VTI.Suffix, VTI.Size{0}, notAbs, isMin>;
  defvar Inst        = !cast<Instruction>(NAME);
  defvar unpred_intr = !cast<Intrinsic>(intrBaseName);
  defvar pred_intr   = !cast<Intrinsic>(intrBaseName#"_predicated");

  let Predicates = [HasMVEFloat] in {
    def : Pat<(Scalar (unpred_intr (Scalar ScalarReg:$prev),
                                   (VTI.Vec MQPR:$vec))),
           (COPY_TO_REGCLASS (Inst (COPY_TO_REGCLASS ScalarReg:$prev, rGPR),
                                   (VTI.Vec MQPR:$vec)),
                              ScalarReg)>;
    def : Pat<(Scalar (pred_intr   (Scalar ScalarReg:$prev),
                                   (VTI.Vec MQPR:$vec),
                                   (VTI.Pred VCCR:$pred))),
           (COPY_TO_REGCLASS (Inst (COPY_TO_REGCLASS ScalarReg:$prev, rGPR),
                                   (VTI.Vec MQPR:$vec),
                                   ARMVCCThen, (VTI.Pred VCCR:$pred), zero_reg),
                              ScalarReg)>;
  }
}

multiclass MVE_VMINMAXNMV_fty<string iname, bit notAbs, bit isMin,
                              string intrBase> {
  defm f32 : MVE_VMINMAXNMV_p<iname, notAbs, isMin, MVE_v4f32, intrBase,
                              f32, SPR>;
  defm f16 : MVE_VMINMAXNMV_p<iname, notAbs, isMin, MVE_v8f16, intrBase,
                              f16, HPR>;
}

defm MVE_VMINNMV : MVE_VMINMAXNMV_fty<"vminnmv",  1, 1, "int_arm_mve_minnmv">;
defm MVE_VMAXNMV : MVE_VMINMAXNMV_fty<"vmaxnmv",  1, 0, "int_arm_mve_maxnmv">;
defm MVE_VMINNMAV: MVE_VMINMAXNMV_fty<"vminnmav", 0, 1, "int_arm_mve_minnmav">;
defm MVE_VMAXNMAV: MVE_VMINMAXNMV_fty<"vmaxnmav", 0, 0, "int_arm_mve_maxnmav">;

class MVE_VMINMAXV<string iname, string suffix, bit U, bits<2> size,
                 bit bit_17, bit bit_7, list<dag> pattern=[]>
  : MVE_rDest<(outs rGPR:$RdaDest), (ins rGPR:$RdaSrc, MQPR:$Qm), NoItinerary,
              iname, suffix, "$RdaSrc, $Qm", "$RdaDest = $RdaSrc", size, pattern> {
  bits<3> Qm;
  bits<4> RdaDest;

  let Inst{28} = U;
  let Inst{22-20} = 0b110;
  let Inst{19-18} = size{1-0};
  let Inst{17} = bit_17;
  let Inst{16} = 0b0;
  let Inst{15-12} = RdaDest{3-0};
  let Inst{8} = 0b1;
  let Inst{7} = bit_7;
  let Inst{6-5} = 0b00;
  let Inst{3-1} = Qm{2-0};
  let Inst{0} = 0b0;
  let horizontalReduction = 1;
}

multiclass MVE_VMINMAXV_p<string iname, bit notAbs, bit isMin,
                          MVEVectorVTInfo VTI, string intrBaseName> {
  def "": MVE_VMINMAXV<iname, VTI.Suffix, VTI.Unsigned, VTI.Size,
                       notAbs, isMin>;
  defvar Inst        = !cast<Instruction>(NAME);
  defvar unpred_intr = !cast<Intrinsic>(intrBaseName);
  defvar pred_intr   = !cast<Intrinsic>(intrBaseName#"_predicated");
  defvar base_args   = (? (i32 rGPR:$prev), (VTI.Vec MQPR:$vec));
  defvar args        = !if(notAbs, !con(base_args, (? (i32 VTI.Unsigned))),
                           base_args);

  let Predicates = [HasMVEInt] in {
    def : Pat<(i32 !con(args, (unpred_intr))),
              (i32 (Inst (i32 rGPR:$prev), (VTI.Vec MQPR:$vec)))>;
    def : Pat<(i32 !con(args, (pred_intr (VTI.Pred VCCR:$pred)))),
              (i32 (Inst (i32 rGPR:$prev), (VTI.Vec MQPR:$vec),
                         ARMVCCThen, (VTI.Pred VCCR:$pred), zero_reg))>;
  }
}

multiclass MVE_VMINMAXV_ty<string iname, bit isMin, string intrBaseName> {
  defm s8 : MVE_VMINMAXV_p<iname, 1, isMin, MVE_v16s8, intrBaseName>;
  defm s16: MVE_VMINMAXV_p<iname, 1, isMin, MVE_v8s16, intrBaseName>;
  defm s32: MVE_VMINMAXV_p<iname, 1, isMin, MVE_v4s32, intrBaseName>;
  defm u8 : MVE_VMINMAXV_p<iname, 1, isMin, MVE_v16u8, intrBaseName>;
  defm u16: MVE_VMINMAXV_p<iname, 1, isMin, MVE_v8u16, intrBaseName>;
  defm u32: MVE_VMINMAXV_p<iname, 1, isMin, MVE_v4u32, intrBaseName>;
}

def SDTVecReduceR : SDTypeProfile<1, 2, [   // Reduction of an integer and vector into an integer
  SDTCisInt<0>, SDTCisInt<1>, SDTCisVec<2>
]>;
def ARMVMINVu       : SDNode<"ARMISD::VMINVu", SDTVecReduceR>;
def ARMVMINVs       : SDNode<"ARMISD::VMINVs", SDTVecReduceR>;
def ARMVMAXVu       : SDNode<"ARMISD::VMAXVu", SDTVecReduceR>;
def ARMVMAXVs       : SDNode<"ARMISD::VMAXVs", SDTVecReduceR>;

defm MVE_VMINV : MVE_VMINMAXV_ty<"vminv", 1, "int_arm_mve_minv">;
defm MVE_VMAXV : MVE_VMINMAXV_ty<"vmaxv", 0, "int_arm_mve_maxv">;

let Predicates = [HasMVEInt] in {
  def : Pat<(i32 (vecreduce_smax (v16i8 MQPR:$src))),
            (i32 (MVE_VMAXVs8 (t2MVNi (i32 127)), $src))>;
  def : Pat<(i32 (vecreduce_smax (v8i16 MQPR:$src))),
            (i32 (MVE_VMAXVs16 (t2MOVi32imm (i32 -32768)), $src))>;
  def : Pat<(i32 (vecreduce_smax (v4i32 MQPR:$src))),
            (i32 (MVE_VMAXVs32 (t2MOVi (i32 -2147483648)), $src))>;
  def : Pat<(i32 (vecreduce_umax (v16i8 MQPR:$src))),
            (i32 (MVE_VMAXVu8 (t2MOVi (i32 0)), $src))>;
  def : Pat<(i32 (vecreduce_umax (v8i16 MQPR:$src))),
            (i32 (MVE_VMAXVu16 (t2MOVi (i32 0)), $src))>;
  def : Pat<(i32 (vecreduce_umax (v4i32 MQPR:$src))),
            (i32 (MVE_VMAXVu32 (t2MOVi (i32 0)), $src))>;

  def : Pat<(i32 (vecreduce_smin (v16i8 MQPR:$src))),
            (i32 (MVE_VMINVs8 (t2MOVi (i32 127)), $src))>;
  def : Pat<(i32 (vecreduce_smin (v8i16 MQPR:$src))),
            (i32 (MVE_VMINVs16 (t2MOVi16 (i32 32767)), $src))>;
  def : Pat<(i32 (vecreduce_smin (v4i32 MQPR:$src))),
            (i32 (MVE_VMINVs32 (t2MVNi (i32 -2147483648)), $src))>;
  def : Pat<(i32 (vecreduce_umin (v16i8 MQPR:$src))),
            (i32 (MVE_VMINVu8 (t2MOVi (i32 255)), $src))>;
  def : Pat<(i32 (vecreduce_umin (v8i16 MQPR:$src))),
            (i32 (MVE_VMINVu16 (t2MOVi16 (i32 65535)), $src))>;
  def : Pat<(i32 (vecreduce_umin (v4i32 MQPR:$src))),
            (i32 (MVE_VMINVu32 (t2MOVi (i32 4294967295)), $src))>;

  def : Pat<(i32 (ARMVMINVu (i32 rGPR:$x), (v16i8 MQPR:$src))),
            (i32 (MVE_VMINVu8 $x, $src))>;
  def : Pat<(i32 (ARMVMINVu (i32 rGPR:$x), (v8i16 MQPR:$src))),
            (i32 (MVE_VMINVu16 $x, $src))>;
  def : Pat<(i32 (ARMVMINVu (i32 rGPR:$x), (v4i32 MQPR:$src))),
            (i32 (MVE_VMINVu32 $x, $src))>;
  def : Pat<(i32 (ARMVMINVs (i32 rGPR:$x), (v16i8 MQPR:$src))),
            (i32 (MVE_VMINVs8 $x, $src))>;
  def : Pat<(i32 (ARMVMINVs (i32 rGPR:$x), (v8i16 MQPR:$src))),
            (i32 (MVE_VMINVs16 $x, $src))>;
  def : Pat<(i32 (ARMVMINVs (i32 rGPR:$x), (v4i32 MQPR:$src))),
            (i32 (MVE_VMINVs32 $x, $src))>;

  def : Pat<(i32 (ARMVMAXVu (i32 rGPR:$x), (v16i8 MQPR:$src))),
            (i32 (MVE_VMAXVu8 $x, $src))>;
  def : Pat<(i32 (ARMVMAXVu (i32 rGPR:$x), (v8i16 MQPR:$src))),
            (i32 (MVE_VMAXVu16 $x, $src))>;
  def : Pat<(i32 (ARMVMAXVu (i32 rGPR:$x), (v4i32 MQPR:$src))),
            (i32 (MVE_VMAXVu32 $x, $src))>;
  def : Pat<(i32 (ARMVMAXVs (i32 rGPR:$x), (v16i8 MQPR:$src))),
            (i32 (MVE_VMAXVs8 $x, $src))>;
  def : Pat<(i32 (ARMVMAXVs (i32 rGPR:$x), (v8i16 MQPR:$src))),
            (i32 (MVE_VMAXVs16 $x, $src))>;
  def : Pat<(i32 (ARMVMAXVs (i32 rGPR:$x), (v4i32 MQPR:$src))),
            (i32 (MVE_VMAXVs32 $x, $src))>;

}

multiclass MVE_VMINMAXAV_ty<string iname, bit isMin, string intrBaseName> {
  defm s8 : MVE_VMINMAXV_p<iname, 0, isMin, MVE_v16s8, intrBaseName>;
  defm s16: MVE_VMINMAXV_p<iname, 0, isMin, MVE_v8s16, intrBaseName>;
  defm s32: MVE_VMINMAXV_p<iname, 0, isMin, MVE_v4s32, intrBaseName>;
}

defm MVE_VMINAV : MVE_VMINMAXAV_ty<"vminav", 1, "int_arm_mve_minav">;
defm MVE_VMAXAV : MVE_VMINMAXAV_ty<"vmaxav", 0, "int_arm_mve_maxav">;

class MVE_VMLAMLSDAV<string iname, string suffix, dag iops, string cstr,
                   bit sz, bit bit_28, bit A, bit X, bit bit_8, bit bit_0,
                   bits<2> vecsize>
  : MVE_rDest<(outs tGPREven:$RdaDest), iops, NoItinerary, iname, suffix,
              "$RdaDest, $Qn, $Qm", cstr, vecsize, []> {
  bits<4> RdaDest;
  bits<3> Qm;
  bits<3> Qn;

  let Inst{28} = bit_28;
  let Inst{22-20} = 0b111;
  let Inst{19-17} = Qn{2-0};
  let Inst{16} = sz;
  let Inst{15-13} = RdaDest{3-1};
  let Inst{12} = X;
  let Inst{8} = bit_8;
  let Inst{7-6} = 0b00;
  let Inst{5} = A;
  let Inst{3-1} = Qm{2-0};
  let Inst{0} = bit_0;
  let horizontalReduction = 1;
  // Allow tail predication for non-exchanging versions. As this is also a
  // horizontalReduction, ARMLowOverheadLoops will also have to check that
  // the vector operands contain zeros in their false lanes for the instruction
  // to be properly valid.
  let validForTailPredication = !eq(X, 0);
}

multiclass MVE_VMLAMLSDAV_A<string iname, string x, MVEVectorVTInfo VTI,
                            bit sz, bit bit_28, bit X, bit bit_8, bit bit_0> {
  def ""#x#VTI.Suffix : MVE_VMLAMLSDAV<iname # x, VTI.Suffix,
                                   (ins MQPR:$Qn, MQPR:$Qm), "",
                                   sz, bit_28, 0b0, X, bit_8, bit_0, VTI.Size>;
  def "a"#x#VTI.Suffix : MVE_VMLAMLSDAV<iname # "a" # x, VTI.Suffix,
                                    (ins tGPREven:$RdaSrc, MQPR:$Qn, MQPR:$Qm),
                                    "$RdaDest = $RdaSrc",
                                    sz, bit_28, 0b1, X, bit_8, bit_0, VTI.Size>;
  let Predicates = [HasMVEInt] in {
    def : Pat<(i32 (int_arm_mve_vmldava
                            (i32 VTI.Unsigned),
                            (i32 bit_0) /* subtract */,
                            (i32 X) /* exchange */,
                            (i32 0) /* accumulator */,
                            (VTI.Vec MQPR:$Qn), (VTI.Vec MQPR:$Qm))),
              (i32 (!cast<Instruction>(NAME # x # VTI.Suffix)
                            (VTI.Vec MQPR:$Qn), (VTI.Vec MQPR:$Qm)))>;

    def : Pat<(i32 (int_arm_mve_vmldava_predicated
                            (i32 VTI.Unsigned),
                            (i32 bit_0) /* subtract */,
                            (i32 X) /* exchange */,
                            (i32 0) /* accumulator */,
                            (VTI.Vec MQPR:$Qn), (VTI.Vec MQPR:$Qm),
                            (VTI.Pred VCCR:$mask))),
              (i32 (!cast<Instruction>(NAME # x # VTI.Suffix)
                            (VTI.Vec MQPR:$Qn), (VTI.Vec MQPR:$Qm),
                             ARMVCCThen, (VTI.Pred VCCR:$mask), zero_reg))>;

    def : Pat<(i32 (int_arm_mve_vmldava
                            (i32 VTI.Unsigned),
                            (i32 bit_0) /* subtract */,
                            (i32 X) /* exchange */,
                            (i32 tGPREven:$RdaSrc),
                            (VTI.Vec MQPR:$Qn), (VTI.Vec MQPR:$Qm))),
              (i32 (!cast<Instruction>(NAME # "a" # x # VTI.Suffix)
                            (i32 tGPREven:$RdaSrc),
                            (VTI.Vec MQPR:$Qn), (VTI.Vec MQPR:$Qm)))>;

    def : Pat<(i32 (int_arm_mve_vmldava_predicated
                            (i32 VTI.Unsigned),
                            (i32 bit_0) /* subtract */,
                            (i32 X) /* exchange */,
                            (i32 tGPREven:$RdaSrc),
                            (VTI.Vec MQPR:$Qn), (VTI.Vec MQPR:$Qm),
                            (VTI.Pred VCCR:$mask))),
              (i32 (!cast<Instruction>(NAME # "a" # x # VTI.Suffix)
                            (i32 tGPREven:$RdaSrc),
                            (VTI.Vec MQPR:$Qn), (VTI.Vec MQPR:$Qm),
                             ARMVCCThen, (VTI.Pred VCCR:$mask), zero_reg))>;
  }
}

multiclass MVE_VMLAMLSDAV_AX<string iname, MVEVectorVTInfo VTI, bit sz,
                             bit bit_28, bit bit_8, bit bit_0> {
  defm "" : MVE_VMLAMLSDAV_A<iname, "", VTI, sz, bit_28,
                             0b0, bit_8, bit_0>;
  defm "" : MVE_VMLAMLSDAV_A<iname, "x", VTI, sz, bit_28,
                             0b1, bit_8, bit_0>;
}

multiclass MVE_VMLADAV_multi<MVEVectorVTInfo SVTI, MVEVectorVTInfo UVTI,
                             bit sz, bit bit_8> {
  defm "" : MVE_VMLAMLSDAV_AX<"vmladav", SVTI,
                              sz, 0b0, bit_8, 0b0>;
  defm "" : MVE_VMLAMLSDAV_A<"vmladav", "", UVTI,
                             sz, 0b1, 0b0, bit_8, 0b0>;
}

multiclass MVE_VMLSDAV_multi<MVEVectorVTInfo VTI, bit sz, bit bit_28> {
  defm "" : MVE_VMLAMLSDAV_AX<"vmlsdav", VTI,
                              sz, bit_28, 0b0, 0b1>;
}

defm MVE_VMLADAV : MVE_VMLADAV_multi<MVE_v16s8, MVE_v16u8, 0b0, 0b1>;
defm MVE_VMLADAV : MVE_VMLADAV_multi<MVE_v8s16, MVE_v8u16, 0b0, 0b0>;
defm MVE_VMLADAV : MVE_VMLADAV_multi<MVE_v4s32, MVE_v4u32, 0b1, 0b0>;

defm MVE_VMLSDAV : MVE_VMLSDAV_multi<MVE_v16s8, 0b0, 0b1>;
defm MVE_VMLSDAV : MVE_VMLSDAV_multi<MVE_v8s16, 0b0, 0b0>;
defm MVE_VMLSDAV : MVE_VMLSDAV_multi<MVE_v4s32, 0b1, 0b0>;

def SDTVecReduce2 : SDTypeProfile<1, 2, [    // VMLAV
  SDTCisInt<0>, SDTCisVec<1>, SDTCisVec<2>
]>;
def SDTVecReduce2L : SDTypeProfile<2, 2, [    // VMLALV
  SDTCisInt<0>, SDTCisInt<1>, SDTCisVec<2>, SDTCisVec<3>
]>;
def SDTVecReduce2LA : SDTypeProfile<2, 4, [    // VMLALVA
  SDTCisInt<0>, SDTCisInt<1>, SDTCisInt<2>, SDTCisInt<3>,
  SDTCisVec<4>, SDTCisVec<5>
]>;
def SDTVecReduce2P : SDTypeProfile<1, 3, [    // VMLAV
  SDTCisInt<0>, SDTCisVec<1>, SDTCisVec<2>, SDTCisVec<3>
]>;
def SDTVecReduce2LP : SDTypeProfile<2, 3, [    // VMLALV
  SDTCisInt<0>, SDTCisInt<1>, SDTCisVec<2>, SDTCisVec<3>, SDTCisVec<4>
]>;
def SDTVecReduce2LAP : SDTypeProfile<2, 5, [    // VMLALVA
  SDTCisInt<0>, SDTCisInt<1>, SDTCisInt<2>, SDTCisInt<3>,
  SDTCisVec<4>, SDTCisVec<5>, SDTCisVec<6>
]>;
def ARMVMLAVs       : SDNode<"ARMISD::VMLAVs", SDTVecReduce2>;
def ARMVMLAVu       : SDNode<"ARMISD::VMLAVu", SDTVecReduce2>;
def ARMVMLALVs      : SDNode<"ARMISD::VMLALVs", SDTVecReduce2L>;
def ARMVMLALVu      : SDNode<"ARMISD::VMLALVu", SDTVecReduce2L>;
def ARMVMLALVAs     : SDNode<"ARMISD::VMLALVAs", SDTVecReduce2LA>;
def ARMVMLALVAu     : SDNode<"ARMISD::VMLALVAu", SDTVecReduce2LA>;
def ARMVMLAVps      : SDNode<"ARMISD::VMLAVps", SDTVecReduce2P>;
def ARMVMLAVpu      : SDNode<"ARMISD::VMLAVpu", SDTVecReduce2P>;
def ARMVMLALVps     : SDNode<"ARMISD::VMLALVps", SDTVecReduce2LP>;
def ARMVMLALVpu     : SDNode<"ARMISD::VMLALVpu", SDTVecReduce2LP>;
def ARMVMLALVAps    : SDNode<"ARMISD::VMLALVAps", SDTVecReduce2LAP>;
def ARMVMLALVApu    : SDNode<"ARMISD::VMLALVApu", SDTVecReduce2LAP>;

let Predicates = [HasMVEInt] in {
  def : Pat<(i32 (vecreduce_add (mul (v4i32 MQPR:$src1), (v4i32 MQPR:$src2)))),
            (i32 (MVE_VMLADAVu32 $src1, $src2))>;
  def : Pat<(i32 (vecreduce_add (mul (v8i16 MQPR:$src1), (v8i16 MQPR:$src2)))),
            (i32 (MVE_VMLADAVu16 $src1, $src2))>;
  def : Pat<(i32 (ARMVMLAVs (v8i16 MQPR:$val1), (v8i16 MQPR:$val2))),
            (i32 (MVE_VMLADAVs16 (v8i16 MQPR:$val1), (v8i16 MQPR:$val2)))>;
  def : Pat<(i32 (ARMVMLAVu (v8i16 MQPR:$val1), (v8i16 MQPR:$val2))),
            (i32 (MVE_VMLADAVu16 (v8i16 MQPR:$val1), (v8i16 MQPR:$val2)))>;
  def : Pat<(i32 (vecreduce_add (mul (v16i8 MQPR:$src1), (v16i8 MQPR:$src2)))),
            (i32 (MVE_VMLADAVu8 $src1, $src2))>;
  def : Pat<(i32 (ARMVMLAVs (v16i8 MQPR:$val1), (v16i8 MQPR:$val2))),
            (i32 (MVE_VMLADAVs8 (v16i8 MQPR:$val1), (v16i8 MQPR:$val2)))>;
  def : Pat<(i32 (ARMVMLAVu (v16i8 MQPR:$val1), (v16i8 MQPR:$val2))),
            (i32 (MVE_VMLADAVu8 (v16i8 MQPR:$val1), (v16i8 MQPR:$val2)))>;

  def : Pat<(i32 (add (i32 (vecreduce_add (mul (v4i32 MQPR:$src1), (v4i32 MQPR:$src2)))),
                      (i32 tGPREven:$src3))),
            (i32 (MVE_VMLADAVau32 $src3, $src1, $src2))>;
  def : Pat<(i32 (add (i32 (vecreduce_add (mul (v8i16 MQPR:$src1), (v8i16 MQPR:$src2)))),
                      (i32 tGPREven:$src3))),
            (i32 (MVE_VMLADAVau16 $src3, $src1, $src2))>;
  def : Pat<(i32 (add (ARMVMLAVs (v8i16 MQPR:$val1), (v8i16 MQPR:$val2)), tGPREven:$Rd)),
            (i32 (MVE_VMLADAVas16 tGPREven:$Rd, (v8i16 MQPR:$val1), (v8i16 MQPR:$val2)))>;
  def : Pat<(i32 (add (ARMVMLAVu (v8i16 MQPR:$val1), (v8i16 MQPR:$val2)), tGPREven:$Rd)),
            (i32 (MVE_VMLADAVau16 tGPREven:$Rd, (v8i16 MQPR:$val1), (v8i16 MQPR:$val2)))>;
  def : Pat<(i32 (add (i32 (vecreduce_add (mul (v16i8 MQPR:$src1), (v16i8 MQPR:$src2)))),
                      (i32 tGPREven:$src3))),
            (i32 (MVE_VMLADAVau8 $src3, $src1, $src2))>;
  def : Pat<(i32 (add (ARMVMLAVs (v16i8 MQPR:$val1), (v16i8 MQPR:$val2)), tGPREven:$Rd)),
            (i32 (MVE_VMLADAVas8 tGPREven:$Rd, (v16i8 MQPR:$val1), (v16i8 MQPR:$val2)))>;
  def : Pat<(i32 (add (ARMVMLAVu (v16i8 MQPR:$val1), (v16i8 MQPR:$val2)), tGPREven:$Rd)),
            (i32 (MVE_VMLADAVau8 tGPREven:$Rd, (v16i8 MQPR:$val1), (v16i8 MQPR:$val2)))>;

  // Predicated
  def : Pat<(i32 (vecreduce_add (vselect (v4i1 VCCR:$pred),
                                         (mul (v4i32 MQPR:$src1), (v4i32 MQPR:$src2)),
                                         (v4i32 ARMimmAllZerosV)))),
            (i32 (MVE_VMLADAVu32 $src1, $src2, ARMVCCThen, $pred, zero_reg))>;
  def : Pat<(i32 (vecreduce_add (vselect (v8i1 VCCR:$pred),
                                         (mul (v8i16 MQPR:$src1), (v8i16 MQPR:$src2)),
                                         (v8i16 ARMimmAllZerosV)))),
            (i32 (MVE_VMLADAVu16 $src1, $src2, ARMVCCThen, $pred, zero_reg))>;
  def : Pat<(i32 (ARMVMLAVps (v8i16 MQPR:$val1), (v8i16 MQPR:$val2), (v8i1 VCCR:$pred))),
            (i32 (MVE_VMLADAVs16 (v8i16 MQPR:$val1), (v8i16 MQPR:$val2), ARMVCCThen, $pred, zero_reg))>;
  def : Pat<(i32 (ARMVMLAVpu (v8i16 MQPR:$val1), (v8i16 MQPR:$val2), (v8i1 VCCR:$pred))),
            (i32 (MVE_VMLADAVu16 (v8i16 MQPR:$val1), (v8i16 MQPR:$val2), ARMVCCThen, $pred, zero_reg))>;
  def : Pat<(i32 (vecreduce_add (vselect (v16i1 VCCR:$pred),
                                         (mul (v16i8 MQPR:$src1), (v16i8 MQPR:$src2)),
                                         (v16i8 ARMimmAllZerosV)))),
            (i32 (MVE_VMLADAVu8 $src1, $src2, ARMVCCThen, $pred, zero_reg))>;
  def : Pat<(i32 (ARMVMLAVps (v16i8 MQPR:$val1), (v16i8 MQPR:$val2), (v16i1 VCCR:$pred))),
            (i32 (MVE_VMLADAVs8 (v16i8 MQPR:$val1), (v16i8 MQPR:$val2), ARMVCCThen, $pred, zero_reg))>;
  def : Pat<(i32 (ARMVMLAVpu (v16i8 MQPR:$val1), (v16i8 MQPR:$val2), (v16i1 VCCR:$pred))),
            (i32 (MVE_VMLADAVu8 (v16i8 MQPR:$val1), (v16i8 MQPR:$val2), ARMVCCThen, $pred, zero_reg))>;

  def : Pat<(i32 (add (i32 (vecreduce_add (vselect (v4i1 VCCR:$pred),
                                                   (mul (v4i32 MQPR:$src1), (v4i32 MQPR:$src2)),
                                                   (v4i32 ARMimmAllZerosV)))),
                      (i32 tGPREven:$src3))),
            (i32 (MVE_VMLADAVau32 $src3, $src1, $src2, ARMVCCThen, $pred, zero_reg))>;
  def : Pat<(i32 (add (i32 (vecreduce_add (vselect (v8i1 VCCR:$pred),
                                                   (mul (v8i16 MQPR:$src1), (v8i16 MQPR:$src2)),
                                                   (v8i16 ARMimmAllZerosV)))),
                      (i32 tGPREven:$src3))),
            (i32 (MVE_VMLADAVau16 $src3, $src1, $src2, ARMVCCThen, $pred, zero_reg))>;
  def : Pat<(i32 (add (ARMVMLAVps (v8i16 MQPR:$val1), (v8i16 MQPR:$val2), (v8i1 VCCR:$pred)), tGPREven:$Rd)),
            (i32 (MVE_VMLADAVas16 tGPREven:$Rd, (v8i16 MQPR:$val1), (v8i16 MQPR:$val2), ARMVCCThen, $pred, zero_reg))>;
  def : Pat<(i32 (add (ARMVMLAVpu (v8i16 MQPR:$val1), (v8i16 MQPR:$val2), (v8i1 VCCR:$pred)), tGPREven:$Rd)),
            (i32 (MVE_VMLADAVau16 tGPREven:$Rd, (v8i16 MQPR:$val1), (v8i16 MQPR:$val2), ARMVCCThen, $pred, zero_reg))>;
  def : Pat<(i32 (add (i32 (vecreduce_add (vselect (v16i1 VCCR:$pred),
                                                   (mul (v16i8 MQPR:$src1), (v16i8 MQPR:$src2)),
                                                   (v16i8 ARMimmAllZerosV)))),
                      (i32 tGPREven:$src3))),
            (i32 (MVE_VMLADAVau8 $src3, $src1, $src2, ARMVCCThen, $pred, zero_reg))>;
  def : Pat<(i32 (add (ARMVMLAVps (v16i8 MQPR:$val1), (v16i8 MQPR:$val2), (v16i1 VCCR:$pred)), tGPREven:$Rd)),
            (i32 (MVE_VMLADAVas8 tGPREven:$Rd, (v16i8 MQPR:$val1), (v16i8 MQPR:$val2), ARMVCCThen, $pred, zero_reg))>;
  def : Pat<(i32 (add (ARMVMLAVpu (v16i8 MQPR:$val1), (v16i8 MQPR:$val2), (v16i1 VCCR:$pred)), tGPREven:$Rd)),
            (i32 (MVE_VMLADAVau8 tGPREven:$Rd, (v16i8 MQPR:$val1), (v16i8 MQPR:$val2), ARMVCCThen, $pred, zero_reg))>;
}

// vmlav aliases vmladav
foreach acc = ["", "a"] in {
  foreach suffix = ["s8", "s16", "s32", "u8", "u16", "u32"] in {
    def : MVEInstAlias<"vmlav"#acc#"${vp}."#suffix#"\t$RdaDest, $Qn, $Qm",
                       (!cast<Instruction>("MVE_VMLADAV"#acc#suffix)
                        tGPREven:$RdaDest, MQPR:$Qn, MQPR:$Qm, vpred_n:$vp)>;
  }
}

// Base class for VMLALDAV and VMLSLDAV, VRMLALDAVH, VRMLSLDAVH
class MVE_VMLALDAVBase<string iname, string suffix, dag iops, string cstr,
                       bit sz, bit bit_28, bit A, bit X, bit bit_8, bit bit_0,
                       bits<2> vecsize, list<dag> pattern=[]>
  : MVE_rDest<(outs tGPREven:$RdaLoDest, tGPROdd:$RdaHiDest), iops, NoItinerary,
              iname, suffix, "$RdaLoDest, $RdaHiDest, $Qn, $Qm", cstr, vecsize, pattern> {
  bits<4> RdaLoDest;
  bits<4> RdaHiDest;
  bits<3> Qm;
  bits<3> Qn;

  let Inst{28} = bit_28;
  let Inst{22-20} = RdaHiDest{3-1};
  let Inst{19-17} = Qn{2-0};
  let Inst{16} = sz;
  let Inst{15-13} = RdaLoDest{3-1};
  let Inst{12} = X;
  let Inst{8} = bit_8;
  let Inst{7-6} = 0b00;
  let Inst{5} = A;
  let Inst{3-1} = Qm{2-0};
  let Inst{0} = bit_0;
  let horizontalReduction = 1;
  // Allow tail predication for non-exchanging versions. As this is also a
  // horizontalReduction, ARMLowOverheadLoops will also have to check that
  // the vector operands contain zeros in their false lanes for the instruction
  // to be properly valid.
  let validForTailPredication = !eq(X, 0);

  let hasSideEffects = 0;
}

multiclass MVE_VMLALDAVBase_A<string iname, string x, string suffix,
                              bit sz, bit bit_28, bit X, bit bit_8, bit bit_0,
                              bits<2> vecsize, list<dag> pattern=[]> {
  def ""#x#suffix : MVE_VMLALDAVBase<
     iname # x, suffix, (ins MQPR:$Qn, MQPR:$Qm), "",
     sz, bit_28, 0b0, X, bit_8, bit_0, vecsize, pattern>;
  def "a"#x#suffix : MVE_VMLALDAVBase<
     iname # "a" # x, suffix,
     (ins tGPREven:$RdaLoSrc, tGPROdd:$RdaHiSrc, MQPR:$Qn, MQPR:$Qm),
     "$RdaLoDest = $RdaLoSrc,$RdaHiDest = $RdaHiSrc",
     sz, bit_28, 0b1, X, bit_8, bit_0, vecsize, pattern>;
}


multiclass MVE_VMLALDAVBase_AX<string iname, string suffix, bit sz, bit bit_28,
                               bit bit_8, bit bit_0, bits<2> vecsize, list<dag> pattern=[]> {
  defm "" : MVE_VMLALDAVBase_A<iname, "", suffix, sz,
                               bit_28, 0b0, bit_8, bit_0, vecsize, pattern>;
  defm "" : MVE_VMLALDAVBase_A<iname, "x", suffix, sz,
                               bit_28, 0b1, bit_8, bit_0, vecsize, pattern>;
}

multiclass MVE_VRMLALDAVH_multi<MVEVectorVTInfo VTI, list<dag> pattern=[]> {
  defm "" : MVE_VMLALDAVBase_AX<"vrmlaldavh", "s"#VTI.BitsSuffix,
                                0b0, 0b0, 0b1, 0b0, VTI.Size, pattern>;
  defm "" : MVE_VMLALDAVBase_A<"vrmlaldavh", "", "u"#VTI.BitsSuffix,
                               0b0, 0b1, 0b0, 0b1, 0b0, VTI.Size, pattern>;
}

defm MVE_VRMLALDAVH : MVE_VRMLALDAVH_multi<MVE_v4i32>;

// vrmlalvh aliases for vrmlaldavh
def : MVEInstAlias<"vrmlalvh${vp}.s32\t$RdaLo, $RdaHi, $Qn, $Qm",
                  (MVE_VRMLALDAVHs32
                   tGPREven:$RdaLo, tGPROdd:$RdaHi,
                   MQPR:$Qn, MQPR:$Qm, vpred_n:$vp)>;
def : MVEInstAlias<"vrmlalvha${vp}.s32\t$RdaLo, $RdaHi, $Qn, $Qm",
                  (MVE_VRMLALDAVHas32
                   tGPREven:$RdaLo, tGPROdd:$RdaHi,
                   MQPR:$Qn, MQPR:$Qm, vpred_n:$vp)>;
def : MVEInstAlias<"vrmlalvh${vp}.u32\t$RdaLo, $RdaHi, $Qn, $Qm",
                  (MVE_VRMLALDAVHu32
                   tGPREven:$RdaLo, tGPROdd:$RdaHi,
                   MQPR:$Qn, MQPR:$Qm, vpred_n:$vp)>;
def : MVEInstAlias<"vrmlalvha${vp}.u32\t$RdaLo, $RdaHi, $Qn, $Qm",
                  (MVE_VRMLALDAVHau32
                   tGPREven:$RdaLo, tGPROdd:$RdaHi,
                   MQPR:$Qn, MQPR:$Qm, vpred_n:$vp)>;

multiclass MVE_VMLALDAV_multi<MVEVectorVTInfo VTI, list<dag> pattern=[]> {
  defm "" : MVE_VMLALDAVBase_AX<"vmlaldav", "s"#VTI.BitsSuffix,
                                VTI.Size{1}, 0b0, 0b0, 0b0, VTI.Size, pattern>;
  defm "" : MVE_VMLALDAVBase_A<"vmlaldav", "", "u"#VTI.BitsSuffix,
                               VTI.Size{1}, 0b1, 0b0, 0b0, 0b0, VTI.Size, pattern>;
}

defm MVE_VMLALDAV : MVE_VMLALDAV_multi<MVE_v8i16>;
defm MVE_VMLALDAV : MVE_VMLALDAV_multi<MVE_v4i32>;

let Predicates = [HasMVEInt] in {
  def : Pat<(ARMVMLALVs (v4i32 MQPR:$val1), (v4i32 MQPR:$val2)),
            (MVE_VMLALDAVs32 (v4i32 MQPR:$val1), (v4i32 MQPR:$val2))>;
  def : Pat<(ARMVMLALVu (v4i32 MQPR:$val1), (v4i32 MQPR:$val2)),
            (MVE_VMLALDAVu32 (v4i32 MQPR:$val1), (v4i32 MQPR:$val2))>;
  def : Pat<(ARMVMLALVs (v8i16 MQPR:$val1), (v8i16 MQPR:$val2)),
            (MVE_VMLALDAVs16 (v8i16 MQPR:$val1), (v8i16 MQPR:$val2))>;
  def : Pat<(ARMVMLALVu (v8i16 MQPR:$val1), (v8i16 MQPR:$val2)),
            (MVE_VMLALDAVu16 (v8i16 MQPR:$val1), (v8i16 MQPR:$val2))>;

  def : Pat<(ARMVMLALVAs tGPREven:$Rda, tGPROdd:$Rdb, (v4i32 MQPR:$val1), (v4i32 MQPR:$val2)),
            (MVE_VMLALDAVas32 tGPREven:$Rda, tGPROdd:$Rdb, (v4i32 MQPR:$val1), (v4i32 MQPR:$val2))>;
  def : Pat<(ARMVMLALVAu tGPREven:$Rda, tGPROdd:$Rdb, (v4i32 MQPR:$val1), (v4i32 MQPR:$val2)),
            (MVE_VMLALDAVau32 tGPREven:$Rda, tGPROdd:$Rdb, (v4i32 MQPR:$val1), (v4i32 MQPR:$val2))>;
  def : Pat<(ARMVMLALVAs tGPREven:$Rda, tGPROdd:$Rdb, (v8i16 MQPR:$val1), (v8i16 MQPR:$val2)),
            (MVE_VMLALDAVas16 tGPREven:$Rda, tGPROdd:$Rdb, (v8i16 MQPR:$val1), (v8i16 MQPR:$val2))>;
  def : Pat<(ARMVMLALVAu tGPREven:$Rda, tGPROdd:$Rdb, (v8i16 MQPR:$val1), (v8i16 MQPR:$val2)),
            (MVE_VMLALDAVau16 tGPREven:$Rda, tGPROdd:$Rdb, (v8i16 MQPR:$val1), (v8i16 MQPR:$val2))>;

  // Predicated
  def : Pat<(ARMVMLALVps (v4i32 MQPR:$val1), (v4i32 MQPR:$val2), (v4i1 VCCR:$pred)),
            (MVE_VMLALDAVs32 (v4i32 MQPR:$val1), (v4i32 MQPR:$val2), ARMVCCThen, $pred, zero_reg)>;
  def : Pat<(ARMVMLALVpu (v4i32 MQPR:$val1), (v4i32 MQPR:$val2), (v4i1 VCCR:$pred)),
            (MVE_VMLALDAVu32 (v4i32 MQPR:$val1), (v4i32 MQPR:$val2), ARMVCCThen, $pred, zero_reg)>;
  def : Pat<(ARMVMLALVps (v8i16 MQPR:$val1), (v8i16 MQPR:$val2), (v8i1 VCCR:$pred)),
            (MVE_VMLALDAVs16 (v8i16 MQPR:$val1), (v8i16 MQPR:$val2), ARMVCCThen, $pred, zero_reg)>;
  def : Pat<(ARMVMLALVpu (v8i16 MQPR:$val1), (v8i16 MQPR:$val2), (v8i1 VCCR:$pred)),
            (MVE_VMLALDAVu16 (v8i16 MQPR:$val1), (v8i16 MQPR:$val2), ARMVCCThen, $pred, zero_reg)>;

  def : Pat<(ARMVMLALVAps tGPREven:$Rda, tGPROdd:$Rdb, (v4i32 MQPR:$val1), (v4i32 MQPR:$val2), (v4i1 VCCR:$pred)),
            (MVE_VMLALDAVas32 tGPREven:$Rda, tGPROdd:$Rdb, (v4i32 MQPR:$val1), (v4i32 MQPR:$val2), ARMVCCThen, $pred, zero_reg)>;
  def : Pat<(ARMVMLALVApu tGPREven:$Rda, tGPROdd:$Rdb, (v4i32 MQPR:$val1), (v4i32 MQPR:$val2), (v4i1 VCCR:$pred)),
            (MVE_VMLALDAVau32 tGPREven:$Rda, tGPROdd:$Rdb, (v4i32 MQPR:$val1), (v4i32 MQPR:$val2), ARMVCCThen, $pred, zero_reg)>;
  def : Pat<(ARMVMLALVAps tGPREven:$Rda, tGPROdd:$Rdb, (v8i16 MQPR:$val1), (v8i16 MQPR:$val2), (v8i1 VCCR:$pred)),
            (MVE_VMLALDAVas16 tGPREven:$Rda, tGPROdd:$Rdb, (v8i16 MQPR:$val1), (v8i16 MQPR:$val2), ARMVCCThen, $pred, zero_reg)>;
  def : Pat<(ARMVMLALVApu tGPREven:$Rda, tGPROdd:$Rdb, (v8i16 MQPR:$val1), (v8i16 MQPR:$val2), (v8i1 VCCR:$pred)),
            (MVE_VMLALDAVau16 tGPREven:$Rda, tGPROdd:$Rdb, (v8i16 MQPR:$val1), (v8i16 MQPR:$val2), ARMVCCThen, $pred, zero_reg)>;
}

// vmlalv aliases vmlaldav
foreach acc = ["", "a"] in {
  foreach suffix = ["s16", "s32", "u16", "u32"] in {
    def : MVEInstAlias<"vmlalv" # acc # "${vp}." # suffix #
                          "\t$RdaLoDest, $RdaHiDest, $Qn, $Qm",
                       (!cast<Instruction>("MVE_VMLALDAV"#acc#suffix)
                       tGPREven:$RdaLoDest, tGPROdd:$RdaHiDest,
                       MQPR:$Qn, MQPR:$Qm, vpred_n:$vp)>;
  }
}

multiclass MVE_VMLSLDAV_multi<string iname, string suffix, bit sz,
                              bit bit_28, bits<2> vecsize, list<dag> pattern=[]> {
  defm "" : MVE_VMLALDAVBase_AX<iname, suffix, sz, bit_28, 0b0, 0b1, vecsize, pattern>;
}

defm MVE_VMLSLDAV   : MVE_VMLSLDAV_multi<"vmlsldav", "s16", 0b0, 0b0, 0b01>;
defm MVE_VMLSLDAV   : MVE_VMLSLDAV_multi<"vmlsldav", "s32", 0b1, 0b0, 0b10>;
defm MVE_VRMLSLDAVH : MVE_VMLSLDAV_multi<"vrmlsldavh", "s32", 0b0, 0b1, 0b10>;

// end of mve_rDest instructions

// start of mve_comp instructions

class MVE_comp<InstrItinClass itin, string iname, string suffix,
               string cstr, bits<2> vecsize, list<dag> pattern=[]>
  : MVE_p<(outs MQPR:$Qd), (ins MQPR:$Qn, MQPR:$Qm), itin, iname, suffix,
           "$Qd, $Qn, $Qm", vpred_r, cstr, vecsize, pattern> {
  bits<4> Qd;
  bits<4> Qn;
  bits<4> Qm;

  let Inst{22} = Qd{3};
  let Inst{19-17} = Qn{2-0};
  let Inst{16} = 0b0;
  let Inst{15-13} = Qd{2-0};
  let Inst{12} = 0b0;
  let Inst{10-9} = 0b11;
  let Inst{7} = Qn{3};
  let Inst{5} = Qm{3};
  let Inst{3-1} = Qm{2-0};
  let Inst{0} = 0b0;
}

class MVE_VMINMAXNM<string iname, string suffix, bits<2> sz, bit bit_21,
                    list<dag> pattern=[]>
  : MVE_comp<NoItinerary, iname, suffix, "", sz, pattern> {

  let Inst{28} = 0b1;
  let Inst{25-24} = 0b11;
  let Inst{23} = 0b0;
  let Inst{21} = bit_21;
  let Inst{20} = sz{0};
  let Inst{11} = 0b1;
  let Inst{8} = 0b1;
  let Inst{6} = 0b1;
  let Inst{4} = 0b1;

  let Predicates = [HasMVEFloat];
  let validForTailPredication = 1;
}

multiclass MVE_VMINMAXNM_m<string iname, bit bit_4, MVEVectorVTInfo VTI, SDNode Op, Intrinsic PredInt> {
  def "" : MVE_VMINMAXNM<iname, VTI.Suffix, VTI.Size, bit_4>;

  let Predicates = [HasMVEFloat] in {
    defm : MVE_TwoOpPattern<VTI, Op, PredInt, (? (i32 0)), !cast<Instruction>(NAME)>;
  }
}

defm MVE_VMAXNMf32 : MVE_VMINMAXNM_m<"vmaxnm", 0b0, MVE_v4f32, fmaxnum, int_arm_mve_max_predicated>;
defm MVE_VMAXNMf16 : MVE_VMINMAXNM_m<"vmaxnm", 0b0, MVE_v8f16, fmaxnum, int_arm_mve_max_predicated>;
defm MVE_VMINNMf32 : MVE_VMINMAXNM_m<"vminnm", 0b1, MVE_v4f32, fminnum, int_arm_mve_min_predicated>;
defm MVE_VMINNMf16 : MVE_VMINMAXNM_m<"vminnm", 0b1, MVE_v8f16, fminnum, int_arm_mve_min_predicated>;


class MVE_VMINMAX<string iname, string suffix, bit U, bits<2> size,
              bit bit_4, list<dag> pattern=[]>
  : MVE_comp<NoItinerary, iname, suffix, "", size, pattern> {

  let Inst{28} = U;
  let Inst{25-24} = 0b11;
  let Inst{23} = 0b0;
  let Inst{21-20} = size{1-0};
  let Inst{11} = 0b0;
  let Inst{8} = 0b0;
  let Inst{6} = 0b1;
  let Inst{4} = bit_4;
  let validForTailPredication = 1;
}

multiclass MVE_VMINMAX_m<string iname, bit bit_4, MVEVectorVTInfo VTI,
                      SDNode Op, Intrinsic PredInt> {
  def "" : MVE_VMINMAX<iname, VTI.Suffix, VTI.Unsigned, VTI.Size, bit_4>;

  let Predicates = [HasMVEInt] in {
    defm : MVE_TwoOpPattern<VTI, Op, PredInt, (? (i32 VTI.Unsigned)), !cast<Instruction>(NAME)>;
  }
}

multiclass MVE_VMAX<MVEVectorVTInfo VTI>
  : MVE_VMINMAX_m<"vmax", 0b0, VTI, !if(VTI.Unsigned, umax, smax), int_arm_mve_max_predicated>;
multiclass MVE_VMIN<MVEVectorVTInfo VTI>
  : MVE_VMINMAX_m<"vmin", 0b1, VTI, !if(VTI.Unsigned, umin, smin), int_arm_mve_min_predicated>;

defm MVE_VMINs8   : MVE_VMIN<MVE_v16s8>;
defm MVE_VMINs16  : MVE_VMIN<MVE_v8s16>;
defm MVE_VMINs32  : MVE_VMIN<MVE_v4s32>;
defm MVE_VMINu8   : MVE_VMIN<MVE_v16u8>;
defm MVE_VMINu16  : MVE_VMIN<MVE_v8u16>;
defm MVE_VMINu32  : MVE_VMIN<MVE_v4u32>;

defm MVE_VMAXs8   : MVE_VMAX<MVE_v16s8>;
defm MVE_VMAXs16  : MVE_VMAX<MVE_v8s16>;
defm MVE_VMAXs32  : MVE_VMAX<MVE_v4s32>;
defm MVE_VMAXu8   : MVE_VMAX<MVE_v16u8>;
defm MVE_VMAXu16  : MVE_VMAX<MVE_v8u16>;
defm MVE_VMAXu32  : MVE_VMAX<MVE_v4u32>;

// end of mve_comp instructions

// start of mve_bit instructions

class MVE_bit_arith<dag oops, dag iops, string iname, string suffix,
                    string ops, string cstr, bits<2> vecsize, list<dag> pattern=[]>
  : MVE_p<oops, iops, NoItinerary, iname, suffix, ops, vpred_r, cstr, vecsize, pattern> {
  bits<4> Qd;
  bits<4> Qm;

  let Inst{22} = Qd{3};
  let Inst{15-13} = Qd{2-0};
  let Inst{5} = Qm{3};
  let Inst{3-1} = Qm{2-0};
}

def MVE_VBIC : MVE_bit_arith<(outs MQPR:$Qd), (ins MQPR:$Qn, MQPR:$Qm),
                             "vbic", "", "$Qd, $Qn, $Qm", "", 0b00> {
  bits<4> Qn;

  let Inst{28} = 0b0;
  let Inst{25-23} = 0b110;
  let Inst{21-20} = 0b01;
  let Inst{19-17} = Qn{2-0};
  let Inst{16} = 0b0;
  let Inst{12-8} = 0b00001;
  let Inst{7} = Qn{3};
  let Inst{6} = 0b1;
  let Inst{4} = 0b1;
  let Inst{0} = 0b0;
  let validForTailPredication = 1;
}

class MVE_VREV<string iname, string suffix, bits<2> size, bits<2> bit_8_7,
               bits<2> vecsize, string cstr="">
  : MVE_bit_arith<(outs MQPR:$Qd), (ins MQPR:$Qm), iname,
                  suffix, "$Qd, $Qm", cstr, vecsize> {

  let Inst{28} = 0b1;
  let Inst{25-23} = 0b111;
  let Inst{21-20} = 0b11;
  let Inst{19-18} = size;
  let Inst{17-16} = 0b00;
  let Inst{12-9} = 0b0000;
  let Inst{8-7} = bit_8_7;
  let Inst{6} = 0b1;
  let Inst{4} = 0b0;
  let Inst{0} = 0b0;
}

def MVE_VREV64_8  : MVE_VREV<"vrev64", "8", 0b00, 0b00, 0b11, "@earlyclobber $Qd">;
def MVE_VREV64_16 : MVE_VREV<"vrev64", "16", 0b01, 0b00, 0b11, "@earlyclobber $Qd">;
def MVE_VREV64_32 : MVE_VREV<"vrev64", "32", 0b10, 0b00, 0b11, "@earlyclobber $Qd">;

def MVE_VREV32_8  : MVE_VREV<"vrev32", "8", 0b00, 0b01, 0b10>;
def MVE_VREV32_16 : MVE_VREV<"vrev32", "16", 0b01, 0b01, 0b10>;

def MVE_VREV16_8  : MVE_VREV<"vrev16", "8", 0b00, 0b10, 0b01>;

let Predicates = [HasMVEInt] in {
  def : Pat<(v8i16 (bswap (v8i16 MQPR:$src))),
            (v8i16 (MVE_VREV16_8 (v8i16 MQPR:$src)))>;
  def : Pat<(v4i32 (bswap (v4i32 MQPR:$src))),
            (v4i32 (MVE_VREV32_8 (v4i32 MQPR:$src)))>;
}

multiclass MVE_VREV_basic_patterns<int revbits, list<MVEVectorVTInfo> VTIs,
                                   Instruction Inst> {
  defvar unpred_op = !cast<SDNode>("ARMvrev" # revbits);

  foreach VTI = VTIs in {
    def : Pat<(VTI.Vec (unpred_op (VTI.Vec MQPR:$src))),
              (VTI.Vec (Inst (VTI.Vec MQPR:$src)))>;
    def : Pat<(VTI.Vec (int_arm_mve_vrev_predicated (VTI.Vec MQPR:$src),
                  revbits, (VTI.Pred VCCR:$pred), (VTI.Vec MQPR:$inactive))),
              (VTI.Vec (Inst (VTI.Vec MQPR:$src), ARMVCCThen,
                  (VTI.Pred VCCR:$pred), zero_reg, (VTI.Vec MQPR:$inactive)))>;
  }
}

let Predicates = [HasMVEInt] in {
  defm: MVE_VREV_basic_patterns<64, [MVE_v4i32, MVE_v4f32], MVE_VREV64_32>;
  defm: MVE_VREV_basic_patterns<64, [MVE_v8i16, MVE_v8f16], MVE_VREV64_16>;
  defm: MVE_VREV_basic_patterns<64, [MVE_v16i8           ], MVE_VREV64_8>;

  defm: MVE_VREV_basic_patterns<32, [MVE_v8i16, MVE_v8f16], MVE_VREV32_16>;
  defm: MVE_VREV_basic_patterns<32, [MVE_v16i8           ], MVE_VREV32_8>;

  defm: MVE_VREV_basic_patterns<16, [MVE_v16i8           ], MVE_VREV16_8>;
}

def MVE_VMVN : MVE_bit_arith<(outs MQPR:$Qd), (ins MQPR:$Qm),
                             "vmvn", "", "$Qd, $Qm", "", 0b00> {
  let Inst{28} = 0b1;
  let Inst{25-23} = 0b111;
  let Inst{21-16} = 0b110000;
  let Inst{12-6} = 0b0010111;
  let Inst{4} = 0b0;
  let Inst{0} = 0b0;
  let validForTailPredication = 1;
}

let Predicates = [HasMVEInt] in {
  foreach VTI = [ MVE_v16i8, MVE_v8i16, MVE_v4i32, MVE_v2i64 ] in {
    def : Pat<(VTI.Vec (vnotq    (VTI.Vec MQPR:$val1))),
              (VTI.Vec (MVE_VMVN (VTI.Vec MQPR:$val1)))>;
    def : Pat<(VTI.Vec (int_arm_mve_mvn_predicated (VTI.Vec MQPR:$val1),
                       (VTI.Pred VCCR:$pred), (VTI.Vec MQPR:$inactive))),
              (VTI.Vec (MVE_VMVN (VTI.Vec MQPR:$val1), ARMVCCThen,
                       (VTI.Pred VCCR:$pred), zero_reg, (VTI.Vec MQPR:$inactive)))>;
  }
}

class MVE_bit_ops<string iname, bits<2> bit_21_20, bit bit_28>
  : MVE_bit_arith<(outs MQPR:$Qd), (ins MQPR:$Qn, MQPR:$Qm),
                  iname, "", "$Qd, $Qn, $Qm", "", 0b00> {
  bits<4> Qn;

  let Inst{28} = bit_28;
  let Inst{25-23} = 0b110;
  let Inst{21-20} = bit_21_20;
  let Inst{19-17} = Qn{2-0};
  let Inst{16} = 0b0;
  let Inst{12-8} = 0b00001;
  let Inst{7} = Qn{3};
  let Inst{6} = 0b1;
  let Inst{4} = 0b1;
  let Inst{0} = 0b0;
  let validForTailPredication = 1;
}

def MVE_VEOR : MVE_bit_ops<"veor", 0b00, 0b1>;
def MVE_VORN : MVE_bit_ops<"vorn", 0b11, 0b0>;
def MVE_VORR : MVE_bit_ops<"vorr", 0b10, 0b0>;
def MVE_VAND : MVE_bit_ops<"vand", 0b00, 0b0>;

// add ignored suffixes as aliases

foreach s=["s8", "s16", "s32", "u8", "u16", "u32", "i8", "i16", "i32", "f16", "f32"] in {
  def : MVEInstAlias<"vbic${vp}." # s # "\t$QdSrc, $QnSrc, $QmSrc",
        (MVE_VBIC MQPR:$QdSrc, MQPR:$QnSrc, MQPR:$QmSrc, vpred_r:$vp)>;
  def : MVEInstAlias<"veor${vp}." # s # "\t$QdSrc, $QnSrc, $QmSrc",
        (MVE_VEOR MQPR:$QdSrc, MQPR:$QnSrc, MQPR:$QmSrc, vpred_r:$vp)>;
  def : MVEInstAlias<"vorn${vp}." # s # "\t$QdSrc, $QnSrc, $QmSrc",
        (MVE_VORN MQPR:$QdSrc, MQPR:$QnSrc, MQPR:$QmSrc, vpred_r:$vp)>;
  def : MVEInstAlias<"vorr${vp}." # s # "\t$QdSrc, $QnSrc, $QmSrc",
        (MVE_VORR MQPR:$QdSrc, MQPR:$QnSrc, MQPR:$QmSrc, vpred_r:$vp)>;
  def : MVEInstAlias<"vand${vp}." # s # "\t$QdSrc, $QnSrc, $QmSrc",
        (MVE_VAND MQPR:$QdSrc, MQPR:$QnSrc, MQPR:$QmSrc, vpred_r:$vp)>;
}

let Predicates = [HasMVEInt] in {
  defm : MVE_TwoOpPattern<MVE_v16i8, and, int_arm_mve_and_predicated, (? ), MVE_VAND, ARMimmAllOnesV>;
  defm : MVE_TwoOpPattern<MVE_v8i16, and, int_arm_mve_and_predicated, (? ), MVE_VAND, ARMimmAllOnesV>;
  defm : MVE_TwoOpPattern<MVE_v4i32, and, int_arm_mve_and_predicated, (? ), MVE_VAND, ARMimmAllOnesV>;
  defm : MVE_TwoOpPattern<MVE_v2i64, and, int_arm_mve_and_predicated, (? ), MVE_VAND, ARMimmAllOnesV>;

  defm : MVE_TwoOpPattern<MVE_v16i8, or, int_arm_mve_orr_predicated, (? ), MVE_VORR, ARMimmAllZerosV>;
  defm : MVE_TwoOpPattern<MVE_v8i16, or, int_arm_mve_orr_predicated, (? ), MVE_VORR, ARMimmAllZerosV>;
  defm : MVE_TwoOpPattern<MVE_v4i32, or, int_arm_mve_orr_predicated, (? ), MVE_VORR, ARMimmAllZerosV>;
  defm : MVE_TwoOpPattern<MVE_v2i64, or, int_arm_mve_orr_predicated, (? ), MVE_VORR, ARMimmAllZerosV>;

  defm : MVE_TwoOpPattern<MVE_v16i8, xor, int_arm_mve_eor_predicated, (? ), MVE_VEOR, ARMimmAllZerosV>;
  defm : MVE_TwoOpPattern<MVE_v8i16, xor, int_arm_mve_eor_predicated, (? ), MVE_VEOR, ARMimmAllZerosV>;
  defm : MVE_TwoOpPattern<MVE_v4i32, xor, int_arm_mve_eor_predicated, (? ), MVE_VEOR, ARMimmAllZerosV>;
  defm : MVE_TwoOpPattern<MVE_v2i64, xor, int_arm_mve_eor_predicated, (? ), MVE_VEOR, ARMimmAllZerosV>;

  defm : MVE_TwoOpPattern<MVE_v16i8, BinOpFrag<(and node:$LHS, (vnotq node:$RHS))>,
                          int_arm_mve_bic_predicated, (? ), MVE_VBIC>;
  defm : MVE_TwoOpPattern<MVE_v8i16, BinOpFrag<(and node:$LHS, (vnotq node:$RHS))>,
                          int_arm_mve_bic_predicated, (? ), MVE_VBIC>;
  defm : MVE_TwoOpPattern<MVE_v4i32, BinOpFrag<(and node:$LHS, (vnotq node:$RHS))>,
                          int_arm_mve_bic_predicated, (? ), MVE_VBIC>;
  defm : MVE_TwoOpPattern<MVE_v2i64, BinOpFrag<(and node:$LHS, (vnotq node:$RHS))>,
                          int_arm_mve_bic_predicated, (? ), MVE_VBIC>;

  defm : MVE_TwoOpPattern<MVE_v16i8, BinOpFrag<(or node:$LHS, (vnotq node:$RHS))>,
                          int_arm_mve_orn_predicated, (? ), MVE_VORN>;
  defm : MVE_TwoOpPattern<MVE_v8i16, BinOpFrag<(or node:$LHS, (vnotq node:$RHS))>,
                          int_arm_mve_orn_predicated, (? ), MVE_VORN>;
  defm : MVE_TwoOpPattern<MVE_v4i32, BinOpFrag<(or node:$LHS, (vnotq node:$RHS))>,
                          int_arm_mve_orn_predicated, (? ), MVE_VORN>;
  defm : MVE_TwoOpPattern<MVE_v2i64, BinOpFrag<(or node:$LHS, (vnotq node:$RHS))>,
                          int_arm_mve_orn_predicated, (? ), MVE_VORN>;
}

class MVE_bit_cmode<string iname, string suffix, bit halfword, dag inOps, bits<2> vecsize>
  : MVE_p<(outs MQPR:$Qd), inOps, NoItinerary,
          iname, suffix, "$Qd, $imm", vpred_n, "$Qd = $Qd_src", vecsize> {
  bits<12> imm;
  bits<4> Qd;

  let Inst{28} = imm{7};
  let Inst{27-23} = 0b11111;
  let Inst{22} = Qd{3};
  let Inst{21-19} = 0b000;
  let Inst{18-16} = imm{6-4};
  let Inst{15-13} = Qd{2-0};
  let Inst{12} = 0b0;
  let Inst{11} = halfword;
  let Inst{10} = !if(halfword, 0, imm{10});
  let Inst{9} = imm{9};
  let Inst{8} = 0b1;
  let Inst{7-6} = 0b01;
  let Inst{4} = 0b1;
  let Inst{3-0} = imm{3-0};
}

multiclass MVE_bit_cmode_p<string iname, bit opcode,
                           MVEVectorVTInfo VTI, Operand imm_type, SDNode op> {
  def "" : MVE_bit_cmode<iname, VTI.Suffix, VTI.Size{0},
                         (ins MQPR:$Qd_src, imm_type:$imm), VTI.Size> {
    let Inst{5} = opcode;
    let validForTailPredication = 1;
  }

  defvar Inst = !cast<Instruction>(NAME);
  defvar UnpredPat = (VTI.Vec (op (VTI.Vec MQPR:$src), timm:$simm));

  let Predicates = [HasMVEInt] in {
    def : Pat<UnpredPat,
              (VTI.Vec (Inst (VTI.Vec MQPR:$src), imm_type:$simm))>;
    def : Pat<(VTI.Vec (vselect (VTI.Pred VCCR:$pred),
                          UnpredPat, (VTI.Vec MQPR:$src))),
              (VTI.Vec (Inst (VTI.Vec MQPR:$src), imm_type:$simm,
                             ARMVCCThen, (VTI.Pred VCCR:$pred), zero_reg))>;
  }
}

multiclass MVE_VORRimm<MVEVectorVTInfo VTI, Operand imm_type> {
  defm "": MVE_bit_cmode_p<"vorr", 0, VTI, imm_type, ARMvorrImm>;
}
multiclass MVE_VBICimm<MVEVectorVTInfo VTI, Operand imm_type> {
  defm "": MVE_bit_cmode_p<"vbic", 1, VTI, imm_type, ARMvbicImm>;
}

defm MVE_VORRimmi16 : MVE_VORRimm<MVE_v8i16, nImmSplatI16>;
defm MVE_VORRimmi32 : MVE_VORRimm<MVE_v4i32, nImmSplatI32>;
defm MVE_VBICimmi16 : MVE_VBICimm<MVE_v8i16, nImmSplatI16>;
defm MVE_VBICimmi32 : MVE_VBICimm<MVE_v4i32, nImmSplatI32>;

def MVE_VORNimmi16 : MVEInstAlias<"vorn${vp}.i16\t$Qd, $imm",
    (MVE_VORRimmi16 MQPR:$Qd, nImmSplatNotI16:$imm, vpred_n:$vp), 0>;
def MVE_VORNimmi32 : MVEInstAlias<"vorn${vp}.i32\t$Qd, $imm",
    (MVE_VORRimmi32 MQPR:$Qd, nImmSplatNotI32:$imm, vpred_n:$vp), 0>;

def MVE_VANDimmi16 : MVEInstAlias<"vand${vp}.i16\t$Qd, $imm",
    (MVE_VBICimmi16 MQPR:$Qd, nImmSplatNotI16:$imm, vpred_n:$vp), 0>;
def MVE_VANDimmi32 : MVEInstAlias<"vand${vp}.i32\t$Qd, $imm",
    (MVE_VBICimmi32 MQPR:$Qd, nImmSplatNotI32:$imm, vpred_n:$vp), 0>;

def MVE_VMOV : MVEInstAlias<"vmov${vp}\t$Qd, $Qm",
    (MVE_VORR MQPR:$Qd, MQPR:$Qm, MQPR:$Qm, vpred_r:$vp)>;

class MVE_VMOV_lane_direction {
  bit bit_20;
  dag oops;
  dag iops;
  string ops;
  string cstr;
}
def MVE_VMOV_from_lane : MVE_VMOV_lane_direction {
  let bit_20 = 0b1;
  let oops = (outs rGPR:$Rt);
  let iops = (ins MQPR:$Qd);
  let ops = "$Rt, $Qd$Idx";
  let cstr = "";
}
def MVE_VMOV_to_lane : MVE_VMOV_lane_direction {
  let bit_20 = 0b0;
  let oops = (outs MQPR:$Qd);
  let iops = (ins MQPR:$Qd_src, rGPR:$Rt);
  let ops = "$Qd$Idx, $Rt";
  let cstr = "$Qd = $Qd_src";
}

class MVE_VMOV_lane<string suffix, bit U, dag indexop,
                    MVE_VMOV_lane_direction dir>
  : MVE_VMOV_lane_base<dir.oops, !con(dir.iops, indexop), NoItinerary,
                       "vmov", suffix, dir.ops, dir.cstr, []> {
  bits<4> Qd;
  bits<4> Rt;

  let Inst{31-24} = 0b11101110;
  let Inst{23} = U;
  let Inst{20} = dir.bit_20;
  let Inst{19-17} = Qd{2-0};
  let Inst{15-12} = Rt{3-0};
  let Inst{11-8} = 0b1011;
  let Inst{7} = Qd{3};
  let Inst{4-0} = 0b10000;

  let hasSideEffects = 0;
}

class MVE_VMOV_lane_32<MVE_VMOV_lane_direction dir>
    : MVE_VMOV_lane<"32", 0b0, (ins MVEVectorIndex<4>:$Idx), dir> {
  bits<2> Idx;
  let Inst{22} = 0b0;
  let Inst{6-5} = 0b00;
  let Inst{16} = Idx{1};
  let Inst{21} = Idx{0};

  let VecSize = 0b10;
  let Predicates = [HasFPRegsV8_1M];
}

class MVE_VMOV_lane_16<string suffix, bit U, MVE_VMOV_lane_direction dir>
  : MVE_VMOV_lane<suffix, U, (ins MVEVectorIndex<8>:$Idx), dir> {
  bits<3> Idx;
  let Inst{22} = 0b0;
  let Inst{5} = 0b1;
  let Inst{16} = Idx{2};
  let Inst{21} = Idx{1};
  let Inst{6} = Idx{0};

  let VecSize = 0b01;
}

class MVE_VMOV_lane_8<string suffix, bit U, MVE_VMOV_lane_direction dir>
  : MVE_VMOV_lane<suffix, U, (ins MVEVectorIndex<16>:$Idx), dir> {
  bits<4> Idx;
  let Inst{22} = 0b1;
  let Inst{16} = Idx{3};
  let Inst{21} = Idx{2};
  let Inst{6} = Idx{1};
  let Inst{5} = Idx{0};

  let VecSize = 0b00;
}

def MVE_VMOV_from_lane_32  : MVE_VMOV_lane_32<            MVE_VMOV_from_lane>;
def MVE_VMOV_from_lane_s16 : MVE_VMOV_lane_16<"s16", 0b0, MVE_VMOV_from_lane>;
def MVE_VMOV_from_lane_u16 : MVE_VMOV_lane_16<"u16", 0b1, MVE_VMOV_from_lane>;
def MVE_VMOV_from_lane_s8  : MVE_VMOV_lane_8 < "s8", 0b0, MVE_VMOV_from_lane>;
def MVE_VMOV_from_lane_u8  : MVE_VMOV_lane_8 < "u8", 0b1, MVE_VMOV_from_lane>;
let isInsertSubreg = 1 in
def MVE_VMOV_to_lane_32    : MVE_VMOV_lane_32<            MVE_VMOV_to_lane>;
def MVE_VMOV_to_lane_16    : MVE_VMOV_lane_16< "16", 0b0, MVE_VMOV_to_lane>;
def MVE_VMOV_to_lane_8     : MVE_VMOV_lane_8 <  "8", 0b0, MVE_VMOV_to_lane>;

// This is the same as insertelt but allows the inserted value to be an i32 as
// will be used when it is the only legal type.
def ARMVecInsert : SDTypeProfile<1, 3, [
  SDTCisVT<2, i32>, SDTCisSameAs<0, 1>, SDTCisPtrTy<3>
]>;
def ARMinsertelt  : SDNode<"ISD::INSERT_VECTOR_ELT", ARMVecInsert>;

let Predicates = [HasMVEInt] in {
  def : Pat<(extractelt (v2f64 MQPR:$src), imm:$lane),
            (f64 (EXTRACT_SUBREG MQPR:$src, (DSubReg_f64_reg imm:$lane)))>;
  def : Pat<(insertelt (v2f64 MQPR:$src1), DPR:$src2, imm:$lane),
            (INSERT_SUBREG (v2f64 (COPY_TO_REGCLASS MQPR:$src1, MQPR)), DPR:$src2, (DSubReg_f64_reg imm:$lane))>;

  def : Pat<(extractelt (v4i32 MQPR:$src), imm:$lane),
            (COPY_TO_REGCLASS
              (i32 (EXTRACT_SUBREG MQPR:$src, (SSubReg_f32_reg imm:$lane))), rGPR)>;
  def : Pat<(insertelt (v4i32 MQPR:$src1), rGPR:$src2, imm:$lane),
            (MVE_VMOV_to_lane_32 MQPR:$src1, rGPR:$src2, imm:$lane)>;
  // This tries to copy from one lane to another, without going via GPR regs
  def : Pat<(insertelt (v4i32 MQPR:$src1), (extractelt (v4i32 MQPR:$src2), imm:$extlane), imm:$inslane),
            (v4i32 (COPY_TO_REGCLASS
                     (INSERT_SUBREG (v4f32 (COPY_TO_REGCLASS (v4i32 MQPR:$src1), MQPR)),
                                    (f32 (EXTRACT_SUBREG (v4f32 (COPY_TO_REGCLASS (v4i32 MQPR:$src2), MQPR)),
                                                         (SSubReg_f32_reg imm:$extlane))),
                                    (SSubReg_f32_reg imm:$inslane)),
                      MQPR))>;

  def : Pat<(vector_insert (v16i8 MQPR:$src1), rGPR:$src2, imm:$lane),
            (MVE_VMOV_to_lane_8  MQPR:$src1, rGPR:$src2, imm:$lane)>;
  def : Pat<(vector_insert (v8i16 MQPR:$src1), rGPR:$src2, imm:$lane),
            (MVE_VMOV_to_lane_16 MQPR:$src1, rGPR:$src2, imm:$lane)>;

  def : Pat<(ARMvgetlanes (v16i8 MQPR:$src), imm:$lane),
            (MVE_VMOV_from_lane_s8 MQPR:$src, imm:$lane)>;
  def : Pat<(ARMvgetlanes (v8i16 MQPR:$src), imm:$lane),
            (MVE_VMOV_from_lane_s16 MQPR:$src, imm:$lane)>;
  def : Pat<(ARMvgetlanes (v8f16 MQPR:$src), imm:$lane),
            (MVE_VMOV_from_lane_s16 MQPR:$src, imm:$lane)>;
  def : Pat<(ARMvgetlaneu (v16i8 MQPR:$src), imm:$lane),
            (MVE_VMOV_from_lane_u8 MQPR:$src, imm:$lane)>;
  def : Pat<(ARMvgetlaneu (v8i16 MQPR:$src), imm:$lane),
            (MVE_VMOV_from_lane_u16 MQPR:$src, imm:$lane)>;
  def : Pat<(ARMvgetlaneu (v8f16 MQPR:$src), imm:$lane),
            (MVE_VMOV_from_lane_u16 MQPR:$src, imm:$lane)>;
  // For i16's inserts being extracted from low lanes, then may use VINS.
  let Predicates = [HasFullFP16] in {
  def : Pat<(ARMinsertelt (v8i16 MQPR:$src1),
                          (ARMvgetlaneu (v8i16 MQPR:$src2), imm_even:$extlane),
                          imm_odd:$inslane),
            (COPY_TO_REGCLASS (INSERT_SUBREG (v4f32 (COPY_TO_REGCLASS MQPR:$src1, MQPR)),
                                (VINSH (EXTRACT_SUBREG MQPR:$src1, (SSubReg_f16_reg imm_odd:$inslane)),
                                       (EXTRACT_SUBREG MQPR:$src2, (SSubReg_f16_reg imm_even:$extlane))),
                                (SSubReg_f16_reg imm_odd:$inslane)), MQPR)>;
  }

  def : Pat<(v16i8 (scalar_to_vector GPR:$src)),
            (MVE_VMOV_to_lane_8  (v16i8 (IMPLICIT_DEF)), rGPR:$src, (i32 0))>;
  def : Pat<(v8i16 (scalar_to_vector GPR:$src)),
            (MVE_VMOV_to_lane_16 (v8i16 (IMPLICIT_DEF)), rGPR:$src, (i32 0))>;
  def : Pat<(v4i32 (scalar_to_vector GPR:$src)),
            (MVE_VMOV_to_lane_32 (v4i32 (IMPLICIT_DEF)), rGPR:$src, (i32 0))>;

  // Floating point patterns, still enabled under HasMVEInt
  def : Pat<(extractelt (v4f32 MQPR:$src), imm:$lane),
            (COPY_TO_REGCLASS (f32 (EXTRACT_SUBREG MQPR:$src, (SSubReg_f32_reg imm:$lane))), SPR)>;
  def : Pat<(insertelt (v4f32 MQPR:$src1), (f32 SPR:$src2), imm:$lane),
            (INSERT_SUBREG (v4f32 (COPY_TO_REGCLASS MQPR:$src1, MQPR)), SPR:$src2, (SSubReg_f32_reg imm:$lane))>;

  def : Pat<(insertelt (v8f16 MQPR:$src1), (f16 HPR:$src2), imm_even:$lane),
            (MVE_VMOV_to_lane_16 MQPR:$src1, (COPY_TO_REGCLASS (f16 HPR:$src2), rGPR), imm:$lane)>;
  let Predicates = [HasFullFP16] in {
  def : Pat<(insertelt (v8f16 MQPR:$src1), (f16 HPR:$src2), imm_odd:$lane),
            (COPY_TO_REGCLASS (INSERT_SUBREG (v4f32 (COPY_TO_REGCLASS MQPR:$src1, MQPR)),
                                (VINSH (EXTRACT_SUBREG MQPR:$src1, (SSubReg_f16_reg imm_odd:$lane)),
                                       (COPY_TO_REGCLASS HPR:$src2, SPR)),
                                (SSubReg_f16_reg imm_odd:$lane)), MQPR)>;
  }
  def : Pat<(extractelt (v8f16 MQPR:$src), imm_even:$lane),
            (EXTRACT_SUBREG MQPR:$src, (SSubReg_f16_reg imm_even:$lane))>;
  let Predicates = [HasFullFP16] in {
  def : Pat<(extractelt (v8f16 MQPR:$src), imm_odd:$lane),
            (COPY_TO_REGCLASS
              (VMOVH (EXTRACT_SUBREG MQPR:$src, (SSubReg_f16_reg imm_odd:$lane))),
              HPR)>;
  }

  def : Pat<(v2f64 (scalar_to_vector (f64 DPR:$src))),
            (INSERT_SUBREG (v2f64 (IMPLICIT_DEF)), DPR:$src, dsub_0)>;
  def : Pat<(v4f32 (scalar_to_vector SPR:$src)),
            (INSERT_SUBREG (v4f32 (IMPLICIT_DEF)), SPR:$src, ssub_0)>;
  def : Pat<(v4f32 (scalar_to_vector GPR:$src)),
            (MVE_VMOV_to_lane_32 (v4f32 (IMPLICIT_DEF)), rGPR:$src, (i32 0))>;
  def : Pat<(v8f16 (scalar_to_vector (f16 HPR:$src))),
            (INSERT_SUBREG (v8f16 (IMPLICIT_DEF)), (f16 HPR:$src), ssub_0)>;
  def : Pat<(v8f16 (scalar_to_vector GPR:$src)),
            (MVE_VMOV_to_lane_16 (v8f16 (IMPLICIT_DEF)), rGPR:$src, (i32 0))>;
}

// end of mve_bit instructions

// start of MVE Integer instructions

class MVE_int<string iname, string suffix, bits<2> size, list<dag> pattern=[]>
  : MVE_p<(outs MQPR:$Qd), (ins MQPR:$Qn, MQPR:$Qm), NoItinerary,
          iname, suffix, "$Qd, $Qn, $Qm", vpred_r, "", size, pattern> {
  bits<4> Qd;
  bits<4> Qn;
  bits<4> Qm;

  let Inst{22} = Qd{3};
  let Inst{21-20} = size;
  let Inst{19-17} = Qn{2-0};
  let Inst{15-13} = Qd{2-0};
  let Inst{7} = Qn{3};
  let Inst{6} = 0b1;
  let Inst{5} = Qm{3};
  let Inst{3-1} = Qm{2-0};
}

class MVE_VMULt1<string iname, string suffix, bits<2> size,
                   list<dag> pattern=[]>
  : MVE_int<iname, suffix, size, pattern> {

  let Inst{28} = 0b0;
  let Inst{25-23} = 0b110;
  let Inst{16} = 0b0;
  let Inst{12-8} = 0b01001;
  let Inst{4} = 0b1;
  let Inst{0} = 0b0;
  let validForTailPredication = 1;
}

multiclass MVE_VMUL_m<MVEVectorVTInfo VTI> {
  def "" : MVE_VMULt1<"vmul", VTI.Suffix, VTI.Size>;

  let Predicates = [HasMVEInt] in {
    defm : MVE_TwoOpPattern<VTI, mul, int_arm_mve_mul_predicated, (? ),
                            !cast<Instruction>(NAME), ARMimmOneV>;
  }
}

defm MVE_VMULi8  : MVE_VMUL_m<MVE_v16i8>;
defm MVE_VMULi16 : MVE_VMUL_m<MVE_v8i16>;
defm MVE_VMULi32 : MVE_VMUL_m<MVE_v4i32>;

class MVE_VQxDMULH_Base<string iname, string suffix, bits<2> size, bit rounding,
                  list<dag> pattern=[]>
  : MVE_int<iname, suffix, size, pattern> {

  let Inst{28} = rounding;
  let Inst{25-23} = 0b110;
  let Inst{16} = 0b0;
  let Inst{12-8} = 0b01011;
  let Inst{4} = 0b0;
  let Inst{0} = 0b0;
  let validForTailPredication = 1;
}

def MVEvqdmulh : SDNode<"ARMISD::VQDMULH", SDTIntBinOp>;

multiclass MVE_VQxDMULH_m<string iname, MVEVectorVTInfo VTI,
                      SDNode Op, Intrinsic unpred_int, Intrinsic pred_int,
                      bit rounding> {
  def "" : MVE_VQxDMULH_Base<iname, VTI.Suffix, VTI.Size, rounding>;
  defvar Inst = !cast<Instruction>(NAME);

  let Predicates = [HasMVEInt] in {
    defm : MVE_TwoOpPattern<VTI, Op, pred_int, (? ), Inst>;

    // Extra unpredicated multiply intrinsic patterns
    def : Pat<(VTI.Vec (unpred_int (VTI.Vec MQPR:$Qm), (VTI.Vec MQPR:$Qn))),
              (VTI.Vec (Inst (VTI.Vec MQPR:$Qm), (VTI.Vec MQPR:$Qn)))>;
  }
}

multiclass MVE_VQxDMULH<string iname, MVEVectorVTInfo VTI, bit rounding>
  : MVE_VQxDMULH_m<iname, VTI, !if(rounding, null_frag,
                                             MVEvqdmulh),
                               !if(rounding, int_arm_mve_vqrdmulh,
                                             int_arm_mve_vqdmulh),
                               !if(rounding, int_arm_mve_qrdmulh_predicated,
                                             int_arm_mve_qdmulh_predicated),
                   rounding>;

defm MVE_VQDMULHi8  : MVE_VQxDMULH<"vqdmulh", MVE_v16s8, 0b0>;
defm MVE_VQDMULHi16 : MVE_VQxDMULH<"vqdmulh", MVE_v8s16, 0b0>;
defm MVE_VQDMULHi32 : MVE_VQxDMULH<"vqdmulh", MVE_v4s32, 0b0>;

defm MVE_VQRDMULHi8  : MVE_VQxDMULH<"vqrdmulh", MVE_v16s8, 0b1>;
defm MVE_VQRDMULHi16 : MVE_VQxDMULH<"vqrdmulh", MVE_v8s16, 0b1>;
defm MVE_VQRDMULHi32 : MVE_VQxDMULH<"vqrdmulh", MVE_v4s32, 0b1>;

class MVE_VADDSUB<string iname, string suffix, bits<2> size, bit subtract,
                    list<dag> pattern=[]>
  : MVE_int<iname, suffix, size, pattern> {

  let Inst{28} = subtract;
  let Inst{25-23} = 0b110;
  let Inst{16} = 0b0;
  let Inst{12-8} = 0b01000;
  let Inst{4} = 0b0;
  let Inst{0} = 0b0;
  let validForTailPredication = 1;
}

multiclass MVE_VADDSUB_m<string iname, MVEVectorVTInfo VTI, bit subtract,
                         SDNode Op, Intrinsic PredInt> {
  def "" : MVE_VADDSUB<iname, VTI.Suffix, VTI.Size, subtract>;
  defvar Inst = !cast<Instruction>(NAME);

  let Predicates = [HasMVEInt] in {
    defm : MVE_TwoOpPattern<VTI, Op, PredInt, (? ), !cast<Instruction>(NAME), ARMimmAllZerosV>;
  }
}

multiclass MVE_VADD<MVEVectorVTInfo VTI>
  : MVE_VADDSUB_m<"vadd", VTI, 0b0, add, int_arm_mve_add_predicated>;
multiclass MVE_VSUB<MVEVectorVTInfo VTI>
  : MVE_VADDSUB_m<"vsub", VTI, 0b1, sub, int_arm_mve_sub_predicated>;

defm MVE_VADDi8  : MVE_VADD<MVE_v16i8>;
defm MVE_VADDi16 : MVE_VADD<MVE_v8i16>;
defm MVE_VADDi32 : MVE_VADD<MVE_v4i32>;

defm MVE_VSUBi8  : MVE_VSUB<MVE_v16i8>;
defm MVE_VSUBi16 : MVE_VSUB<MVE_v8i16>;
defm MVE_VSUBi32 : MVE_VSUB<MVE_v4i32>;

class MVE_VQADDSUB<string iname, string suffix, bit U, bit subtract,
                   bits<2> size>
  : MVE_int<iname, suffix, size, []> {

  let Inst{28} = U;
  let Inst{25-23} = 0b110;
  let Inst{16} = 0b0;
  let Inst{12-10} = 0b000;
  let Inst{9} = subtract;
  let Inst{8} = 0b0;
  let Inst{4} = 0b1;
  let Inst{0} = 0b0;
  let validForTailPredication = 1;
}

class MVE_VQADD_<string suffix, bit U, bits<2> size>
  : MVE_VQADDSUB<"vqadd", suffix, U, 0b0, size>;
class MVE_VQSUB_<string suffix, bit U, bits<2> size>
  : MVE_VQADDSUB<"vqsub", suffix, U, 0b1, size>;

multiclass MVE_VQADD_m<MVEVectorVTInfo VTI,
                      SDNode Op, Intrinsic PredInt> {
  def "" : MVE_VQADD_<VTI.Suffix, VTI.Unsigned, VTI.Size>;
  defvar Inst = !cast<Instruction>(NAME);

  let Predicates = [HasMVEInt] in {
    defm : MVE_TwoOpPattern<VTI, Op, PredInt, (? (i32 VTI.Unsigned)),
                            !cast<Instruction>(NAME)>;
  }
}

multiclass MVE_VQADD<MVEVectorVTInfo VTI, SDNode unpred_op>
  : MVE_VQADD_m<VTI, unpred_op, int_arm_mve_qadd_predicated>;

defm MVE_VQADDs8  : MVE_VQADD<MVE_v16s8, saddsat>;
defm MVE_VQADDs16 : MVE_VQADD<MVE_v8s16, saddsat>;
defm MVE_VQADDs32 : MVE_VQADD<MVE_v4s32, saddsat>;
defm MVE_VQADDu8  : MVE_VQADD<MVE_v16u8, uaddsat>;
defm MVE_VQADDu16 : MVE_VQADD<MVE_v8u16, uaddsat>;
defm MVE_VQADDu32 : MVE_VQADD<MVE_v4u32, uaddsat>;

multiclass MVE_VQSUB_m<MVEVectorVTInfo VTI,
                      SDNode Op, Intrinsic PredInt> {
  def "" : MVE_VQSUB_<VTI.Suffix, VTI.Unsigned, VTI.Size>;
  defvar Inst = !cast<Instruction>(NAME);

  let Predicates = [HasMVEInt] in {
    defm : MVE_TwoOpPattern<VTI, Op, PredInt, (? (i32 VTI.Unsigned)),
                            !cast<Instruction>(NAME)>;
  }
}

multiclass MVE_VQSUB<MVEVectorVTInfo VTI, SDNode unpred_op>
  : MVE_VQSUB_m<VTI, unpred_op, int_arm_mve_qsub_predicated>;

defm MVE_VQSUBs8  : MVE_VQSUB<MVE_v16s8, ssubsat>;
defm MVE_VQSUBs16 : MVE_VQSUB<MVE_v8s16, ssubsat>;
defm MVE_VQSUBs32 : MVE_VQSUB<MVE_v4s32, ssubsat>;
defm MVE_VQSUBu8  : MVE_VQSUB<MVE_v16u8, usubsat>;
defm MVE_VQSUBu16 : MVE_VQSUB<MVE_v8u16, usubsat>;
defm MVE_VQSUBu32 : MVE_VQSUB<MVE_v4u32, usubsat>;

class MVE_VABD_int<string suffix, bit U, bits<2> size,
                     list<dag> pattern=[]>
  : MVE_int<"vabd", suffix, size, pattern> {

  let Inst{28} = U;
  let Inst{25-23} = 0b110;
  let Inst{16} = 0b0;
  let Inst{12-8} = 0b00111;
  let Inst{4} = 0b0;
  let Inst{0} = 0b0;
  let validForTailPredication = 1;
}

multiclass MVE_VABD_m<MVEVectorVTInfo VTI, SDNode Op,
                      Intrinsic unpred_int, Intrinsic PredInt> {
  def "" : MVE_VABD_int<VTI.Suffix, VTI.Unsigned, VTI.Size>;
  defvar Inst = !cast<Instruction>(NAME);

  let Predicates = [HasMVEInt] in {
    defm : MVE_TwoOpPattern<VTI, Op, PredInt, (? (i32 VTI.Unsigned)),
                            !cast<Instruction>(NAME)>;

    // Unpredicated absolute difference
    def : Pat<(VTI.Vec (unpred_int (VTI.Vec MQPR:$Qm), (VTI.Vec MQPR:$Qn),
                            (i32 VTI.Unsigned))),
              (VTI.Vec (Inst (VTI.Vec MQPR:$Qm), (VTI.Vec MQPR:$Qn)))>;
  }
}

multiclass MVE_VABD<MVEVectorVTInfo VTI, SDNode Op>
  : MVE_VABD_m<VTI, Op, int_arm_mve_vabd, int_arm_mve_abd_predicated>;

defm MVE_VABDs8  : MVE_VABD<MVE_v16s8, abds>;
defm MVE_VABDs16 : MVE_VABD<MVE_v8s16, abds>;
defm MVE_VABDs32 : MVE_VABD<MVE_v4s32, abds>;
defm MVE_VABDu8  : MVE_VABD<MVE_v16u8, abdu>;
defm MVE_VABDu16 : MVE_VABD<MVE_v8u16, abdu>;
defm MVE_VABDu32 : MVE_VABD<MVE_v4u32, abdu>;

class MVE_VRHADD_Base<string suffix, bit U, bits<2> size, list<dag> pattern=[]>
  : MVE_int<"vrhadd", suffix, size, pattern> {

  let Inst{28} = U;
  let Inst{25-23} = 0b110;
  let Inst{16} = 0b0;
  let Inst{12-8} = 0b00001;
  let Inst{4} = 0b0;
  let Inst{0} = 0b0;
  let validForTailPredication = 1;
}

def addnuw : PatFrag<(ops node:$lhs, node:$rhs),
                     (add node:$lhs, node:$rhs), [{
  return N->getFlags().hasNoUnsignedWrap();
}]>;

def addnsw : PatFrag<(ops node:$lhs, node:$rhs),
                     (add node:$lhs, node:$rhs), [{
  return N->getFlags().hasNoSignedWrap();
}]>;

def subnuw : PatFrag<(ops node:$lhs, node:$rhs),
                     (sub node:$lhs, node:$rhs), [{
  return N->getFlags().hasNoUnsignedWrap();
}]>;

def subnsw : PatFrag<(ops node:$lhs, node:$rhs),
                     (sub node:$lhs, node:$rhs), [{
  return N->getFlags().hasNoSignedWrap();
}]>;

multiclass MVE_VRHADD_m<MVEVectorVTInfo VTI, SDNode Op,
                      SDNode unpred_op, Intrinsic PredInt> {
  def "" : MVE_VRHADD_Base<VTI.Suffix, VTI.Unsigned, VTI.Size>;
  defvar Inst = !cast<Instruction>(NAME);
  defm : MVE_TwoOpPattern<VTI, Op, PredInt, (? (i32 VTI.Unsigned)), !cast<Instruction>(NAME)>;

  let Predicates = [HasMVEInt] in {
    // Unpredicated rounding add-with-divide-by-two intrinsic
    def : Pat<(VTI.Vec (unpred_op (VTI.Vec MQPR:$Qm), (VTI.Vec MQPR:$Qn),
                            (i32 VTI.Unsigned))),
              (VTI.Vec (Inst (VTI.Vec MQPR:$Qm), (VTI.Vec MQPR:$Qn)))>;
  }
}

multiclass MVE_VRHADD<MVEVectorVTInfo VTI, SDNode rhadd>
  : MVE_VRHADD_m<VTI, rhadd, int_arm_mve_vrhadd, int_arm_mve_rhadd_predicated>;

defm MVE_VRHADDs8  : MVE_VRHADD<MVE_v16s8, avgceils>;
defm MVE_VRHADDs16 : MVE_VRHADD<MVE_v8s16, avgceils>;
defm MVE_VRHADDs32 : MVE_VRHADD<MVE_v4s32, avgceils>;
defm MVE_VRHADDu8  : MVE_VRHADD<MVE_v16u8, avgceilu>;
defm MVE_VRHADDu16 : MVE_VRHADD<MVE_v8u16, avgceilu>;
defm MVE_VRHADDu32 : MVE_VRHADD<MVE_v4u32, avgceilu>;

// Rounding Halving Add perform the arithemtic operation with an extra bit of
// precision, before performing the shift, to void clipping errors. We're not
// modelling that here with these patterns, but we're using no wrap forms of
// add to ensure that the extra bit of information is not needed for the
// arithmetic or the rounding.
let Predicates = [HasMVEInt] in {
  def : Pat<(v16i8 (ARMvshrsImm (addnsw (addnsw (v16i8 MQPR:$Qm), (v16i8 MQPR:$Qn)),
                                        (v16i8 (ARMvmovImm (i32 3585)))),
                                (i32 1))),
            (MVE_VRHADDs8 MQPR:$Qm, MQPR:$Qn)>;
  def : Pat<(v8i16 (ARMvshrsImm (addnsw (addnsw (v8i16 MQPR:$Qm), (v8i16 MQPR:$Qn)),
                                        (v8i16 (ARMvmovImm (i32 2049)))),
                                (i32 1))),
            (MVE_VRHADDs16 MQPR:$Qm, MQPR:$Qn)>;
  def : Pat<(v4i32 (ARMvshrsImm (addnsw (addnsw (v4i32 MQPR:$Qm), (v4i32 MQPR:$Qn)),
                                        (v4i32 (ARMvmovImm (i32 1)))),
                                (i32 1))),
            (MVE_VRHADDs32 MQPR:$Qm, MQPR:$Qn)>;
  def : Pat<(v16i8 (ARMvshruImm (addnuw (addnuw (v16i8 MQPR:$Qm), (v16i8 MQPR:$Qn)),
                                        (v16i8 (ARMvmovImm (i32 3585)))),
                                (i32 1))),
            (MVE_VRHADDu8 MQPR:$Qm, MQPR:$Qn)>;
  def : Pat<(v8i16 (ARMvshruImm (addnuw (addnuw (v8i16 MQPR:$Qm), (v8i16 MQPR:$Qn)),
                                        (v8i16 (ARMvmovImm (i32 2049)))),
                                (i32 1))),
            (MVE_VRHADDu16 MQPR:$Qm, MQPR:$Qn)>;
  def : Pat<(v4i32 (ARMvshruImm (addnuw (addnuw (v4i32 MQPR:$Qm), (v4i32 MQPR:$Qn)),
                                        (v4i32 (ARMvmovImm (i32 1)))),
                                (i32 1))),
            (MVE_VRHADDu32 MQPR:$Qm, MQPR:$Qn)>;

  def : Pat<(v16i8 (ARMvshrsImm (addnsw (addnsw (v16i8 MQPR:$Qm), (v16i8 MQPR:$Qn)),
                                        (v16i8 (ARMvdup (i32 1)))),
                                (i32 1))),
            (MVE_VRHADDs8 MQPR:$Qm, MQPR:$Qn)>;
  def : Pat<(v8i16 (ARMvshrsImm (addnsw (addnsw (v8i16 MQPR:$Qm), (v8i16 MQPR:$Qn)),
                                        (v8i16 (ARMvdup (i32 1)))),
                                (i32 1))),
            (MVE_VRHADDs16 MQPR:$Qm, MQPR:$Qn)>;
  def : Pat<(v4i32 (ARMvshrsImm (addnsw (addnsw (v4i32 MQPR:$Qm), (v4i32 MQPR:$Qn)),
                                        (v4i32 (ARMvdup (i32 1)))),
                                (i32 1))),
            (MVE_VRHADDs32 MQPR:$Qm, MQPR:$Qn)>;
  def : Pat<(v16i8 (ARMvshruImm (addnuw (addnuw (v16i8 MQPR:$Qm), (v16i8 MQPR:$Qn)),
                                        (v16i8 (ARMvdup (i32 1)))),
                                (i32 1))),
            (MVE_VRHADDu8 MQPR:$Qm, MQPR:$Qn)>;
  def : Pat<(v8i16 (ARMvshruImm (addnuw (addnuw (v8i16 MQPR:$Qm), (v8i16 MQPR:$Qn)),
                                        (v8i16 (ARMvdup (i32 1)))),
                                (i32 1))),
            (MVE_VRHADDu16 MQPR:$Qm, MQPR:$Qn)>;
  def : Pat<(v4i32 (ARMvshruImm (addnuw (addnuw (v4i32 MQPR:$Qm), (v4i32 MQPR:$Qn)),
                                        (v4i32 (ARMvdup (i32 1)))),
                                (i32 1))),
            (MVE_VRHADDu32 MQPR:$Qm, MQPR:$Qn)>;
}


class MVE_VHADDSUB<string iname, string suffix, bit U, bit subtract,
                   bits<2> size, list<dag> pattern=[]>
  : MVE_int<iname, suffix, size, pattern> {

  let Inst{28} = U;
  let Inst{25-23} = 0b110;
  let Inst{16} = 0b0;
  let Inst{12-10} = 0b000;
  let Inst{9} = subtract;
  let Inst{8} = 0b0;
  let Inst{4} = 0b0;
  let Inst{0} = 0b0;
  let validForTailPredication = 1;
}

class MVE_VHADD_<string suffix, bit U, bits<2> size,
              list<dag> pattern=[]>
  : MVE_VHADDSUB<"vhadd", suffix, U, 0b0, size, pattern>;
class MVE_VHSUB_<string suffix, bit U, bits<2> size,
              list<dag> pattern=[]>
  : MVE_VHADDSUB<"vhsub", suffix, U, 0b1, size, pattern>;

multiclass MVE_VHADD_m<MVEVectorVTInfo VTI, SDNode Op,
                      SDNode unpred_op, Intrinsic PredInt, PatFrag add_op,
                      SDNode shift_op> {
  def "" : MVE_VHADD_<VTI.Suffix, VTI.Unsigned, VTI.Size>;
  defvar Inst = !cast<Instruction>(NAME);
  defm : MVE_TwoOpPattern<VTI, Op, PredInt, (? (i32 VTI.Unsigned)), !cast<Instruction>(NAME)>;

  let Predicates = [HasMVEInt] in {
    // Unpredicated add-and-divide-by-two
    def : Pat<(VTI.Vec (unpred_op (VTI.Vec MQPR:$Qm), (VTI.Vec MQPR:$Qn), (i32 VTI.Unsigned))),
              (VTI.Vec (Inst (VTI.Vec MQPR:$Qm), (VTI.Vec MQPR:$Qn)))>;

    def : Pat<(VTI.Vec (shift_op (add_op (VTI.Vec MQPR:$Qm), (VTI.Vec MQPR:$Qn)), (i32 1))),
              (Inst MQPR:$Qm, MQPR:$Qn)>;
  }
}

multiclass MVE_VHADD<MVEVectorVTInfo VTI, SDNode Op, PatFrag add_op, SDNode shift_op>
  : MVE_VHADD_m<VTI, Op, int_arm_mve_vhadd, int_arm_mve_hadd_predicated, add_op,
                shift_op>;

// Halving add/sub perform the arithemtic operation with an extra bit of
// precision, before performing the shift, to void clipping errors. We're not
// modelling that here with these patterns, but we're using no wrap forms of
// add/sub to ensure that the extra bit of information is not needed.
defm MVE_VHADDs8  : MVE_VHADD<MVE_v16s8, avgfloors, addnsw, ARMvshrsImm>;
defm MVE_VHADDs16 : MVE_VHADD<MVE_v8s16, avgfloors, addnsw, ARMvshrsImm>;
defm MVE_VHADDs32 : MVE_VHADD<MVE_v4s32, avgfloors, addnsw, ARMvshrsImm>;
defm MVE_VHADDu8  : MVE_VHADD<MVE_v16u8, avgflooru, addnuw, ARMvshruImm>;
defm MVE_VHADDu16 : MVE_VHADD<MVE_v8u16, avgflooru, addnuw, ARMvshruImm>;
defm MVE_VHADDu32 : MVE_VHADD<MVE_v4u32, avgflooru, addnuw, ARMvshruImm>;

multiclass MVE_VHSUB_m<MVEVectorVTInfo VTI,
                      SDNode unpred_op, Intrinsic pred_int, PatFrag sub_op,
                      SDNode shift_op> {
  def "" : MVE_VHSUB_<VTI.Suffix, VTI.Unsigned, VTI.Size>;
  defvar Inst = !cast<Instruction>(NAME);

  let Predicates = [HasMVEInt] in {
    // Unpredicated subtract-and-divide-by-two
    def : Pat<(VTI.Vec (unpred_op (VTI.Vec MQPR:$Qm), (VTI.Vec MQPR:$Qn),
                            (i32 VTI.Unsigned))),
              (VTI.Vec (Inst (VTI.Vec MQPR:$Qm), (VTI.Vec MQPR:$Qn)))>;

    def : Pat<(VTI.Vec (shift_op (sub_op (VTI.Vec MQPR:$Qm), (VTI.Vec MQPR:$Qn)), (i32 1))),
              (Inst MQPR:$Qm, MQPR:$Qn)>;


    // Predicated subtract-and-divide-by-two
    def : Pat<(VTI.Vec (pred_int (VTI.Vec MQPR:$Qm), (VTI.Vec MQPR:$Qn),
                            (i32 VTI.Unsigned), (VTI.Pred VCCR:$mask),
                            (VTI.Vec MQPR:$inactive))),
              (VTI.Vec (Inst (VTI.Vec MQPR:$Qm), (VTI.Vec MQPR:$Qn),
                             ARMVCCThen, (VTI.Pred VCCR:$mask), zero_reg,
                             (VTI.Vec MQPR:$inactive)))>;
  }
}

multiclass MVE_VHSUB<MVEVectorVTInfo VTI, PatFrag sub_op, SDNode shift_op>
  : MVE_VHSUB_m<VTI, int_arm_mve_vhsub, int_arm_mve_hsub_predicated, sub_op,
                shift_op>;

defm MVE_VHSUBs8  : MVE_VHSUB<MVE_v16s8, subnsw, ARMvshrsImm>;
defm MVE_VHSUBs16 : MVE_VHSUB<MVE_v8s16, subnsw, ARMvshrsImm>;
defm MVE_VHSUBs32 : MVE_VHSUB<MVE_v4s32, subnsw, ARMvshrsImm>;
defm MVE_VHSUBu8  : MVE_VHSUB<MVE_v16u8, subnuw, ARMvshruImm>;
defm MVE_VHSUBu16 : MVE_VHSUB<MVE_v8u16, subnuw, ARMvshruImm>;
defm MVE_VHSUBu32 : MVE_VHSUB<MVE_v4u32, subnuw, ARMvshruImm>;

class MVE_VDUP<string suffix, bit B, bit E, bits<2> vecsize, list<dag> pattern=[]>
  : MVE_p<(outs MQPR:$Qd), (ins rGPR:$Rt), NoItinerary,
          "vdup", suffix, "$Qd, $Rt", vpred_r, "", vecsize, pattern> {
  bits<4> Qd;
  bits<4> Rt;

  let Inst{28} = 0b0;
  let Inst{25-23} = 0b101;
  let Inst{22} = B;
  let Inst{21-20} = 0b10;
  let Inst{19-17} = Qd{2-0};
  let Inst{16} = 0b0;
  let Inst{15-12} = Rt;
  let Inst{11-8} = 0b1011;
  let Inst{7} = Qd{3};
  let Inst{6} = 0b0;
  let Inst{5} = E;
  let Inst{4-0} = 0b10000;
  let validForTailPredication = 1;
}

def MVE_VDUP32 : MVE_VDUP<"32", 0b0, 0b0, 0b10>;
def MVE_VDUP16 : MVE_VDUP<"16", 0b0, 0b1, 0b01>;
def MVE_VDUP8  : MVE_VDUP<"8",  0b1, 0b0, 0b00>;

let Predicates = [HasMVEInt] in {
  def : Pat<(v16i8 (ARMvdup (i32 rGPR:$elem))),
            (MVE_VDUP8  rGPR:$elem)>;
  def : Pat<(v8i16 (ARMvdup (i32 rGPR:$elem))),
            (MVE_VDUP16 rGPR:$elem)>;
  def : Pat<(v4i32 (ARMvdup (i32 rGPR:$elem))),
            (MVE_VDUP32 rGPR:$elem)>;

  def : Pat<(v8f16 (ARMvdup (i32 rGPR:$elem))),
            (MVE_VDUP16 rGPR:$elem)>;
  def : Pat<(v4f32 (ARMvdup (i32 rGPR:$elem))),
            (MVE_VDUP32 rGPR:$elem)>;

  // Match a vselect with an ARMvdup as a predicated MVE_VDUP
  def : Pat<(v16i8 (vselect (v16i1 VCCR:$pred),
                            (v16i8 (ARMvdup (i32 rGPR:$elem))),
                            (v16i8 MQPR:$inactive))),
            (MVE_VDUP8  rGPR:$elem, ARMVCCThen, (v16i1 VCCR:$pred), zero_reg,
                        (v16i8 MQPR:$inactive))>;
  def : Pat<(v8i16 (vselect (v8i1 VCCR:$pred),
                            (v8i16 (ARMvdup (i32 rGPR:$elem))),
                            (v8i16 MQPR:$inactive))),
            (MVE_VDUP16 rGPR:$elem, ARMVCCThen, (v8i1 VCCR:$pred), zero_reg,
                            (v8i16 MQPR:$inactive))>;
  def : Pat<(v4i32 (vselect (v4i1 VCCR:$pred),
                            (v4i32 (ARMvdup (i32 rGPR:$elem))),
                            (v4i32 MQPR:$inactive))),
            (MVE_VDUP32 rGPR:$elem, ARMVCCThen, (v4i1 VCCR:$pred), zero_reg,
                            (v4i32 MQPR:$inactive))>;
  def : Pat<(v4f32 (vselect (v4i1 VCCR:$pred),
                            (v4f32 (ARMvdup (i32 rGPR:$elem))),
                            (v4f32 MQPR:$inactive))),
            (MVE_VDUP32 rGPR:$elem, ARMVCCThen, (v4i1 VCCR:$pred), zero_reg,
                            (v4f32 MQPR:$inactive))>;
  def : Pat<(v8f16 (vselect (v8i1 VCCR:$pred),
                            (v8f16 (ARMvdup (i32 rGPR:$elem))),
                            (v8f16 MQPR:$inactive))),
            (MVE_VDUP16 rGPR:$elem, ARMVCCThen, (v8i1 VCCR:$pred), zero_reg,
                            (v8f16 MQPR:$inactive))>;
}


class MVEIntSingleSrc<string iname, string suffix, bits<2> size,
                         list<dag> pattern=[]>
  : MVE_p<(outs MQPR:$Qd), (ins MQPR:$Qm), NoItinerary,
          iname, suffix, "$Qd, $Qm", vpred_r, "", size, pattern> {
  bits<4> Qd;
  bits<4> Qm;

  let Inst{22} = Qd{3};
  let Inst{19-18} = size{1-0};
  let Inst{15-13} = Qd{2-0};
  let Inst{5} = Qm{3};
  let Inst{3-1} = Qm{2-0};
}

class MVE_VCLSCLZ<string iname, string suffix, bits<2> size,
                   bit count_zeroes, list<dag> pattern=[]>
  : MVEIntSingleSrc<iname, suffix, size, pattern> {

  let Inst{28} = 0b1;
  let Inst{25-23} = 0b111;
  let Inst{21-20} = 0b11;
  let Inst{17-16} = 0b00;
  let Inst{12-8} = 0b00100;
  let Inst{7} = count_zeroes;
  let Inst{6} = 0b1;
  let Inst{4} = 0b0;
  let Inst{0} = 0b0;
  let validForTailPredication = 1;
}

multiclass MVE_VCLSCLZ_p<string opname, bit opcode, MVEVectorVTInfo VTI,
                         SDPatternOperator unpred_op> {
  def "": MVE_VCLSCLZ<"v"#opname, VTI.Suffix, VTI.Size, opcode>;

  defvar Inst     = !cast<Instruction>(NAME);
  defvar pred_int = !cast<Intrinsic>("int_arm_mve_"#opname#"_predicated");

  let Predicates = [HasMVEInt] in {
    def : Pat<(VTI.Vec (unpred_op (VTI.Vec MQPR:$val))),
              (VTI.Vec (Inst (VTI.Vec MQPR:$val)))>;
    def : Pat<(VTI.Vec (pred_int (VTI.Vec MQPR:$val), (VTI.Pred VCCR:$pred),
                                 (VTI.Vec MQPR:$inactive))),
              (VTI.Vec (Inst (VTI.Vec MQPR:$val), ARMVCCThen,
                             (VTI.Pred VCCR:$pred), zero_reg, (VTI.Vec MQPR:$inactive)))>;
  }
}

defm MVE_VCLSs8  : MVE_VCLSCLZ_p<"cls", 0, MVE_v16s8, int_arm_mve_vcls>;
defm MVE_VCLSs16 : MVE_VCLSCLZ_p<"cls", 0, MVE_v8s16, int_arm_mve_vcls>;
defm MVE_VCLSs32 : MVE_VCLSCLZ_p<"cls", 0, MVE_v4s32, int_arm_mve_vcls>;

defm MVE_VCLZs8  : MVE_VCLSCLZ_p<"clz", 1, MVE_v16i8, ctlz>;
defm MVE_VCLZs16 : MVE_VCLSCLZ_p<"clz", 1, MVE_v8i16, ctlz>;
defm MVE_VCLZs32 : MVE_VCLSCLZ_p<"clz", 1, MVE_v4i32, ctlz>;

class MVE_VABSNEG_int<string iname, string suffix, bits<2> size, bit negate,
                      bit saturate, list<dag> pattern=[]>
  : MVEIntSingleSrc<iname, suffix, size, pattern> {

  let Inst{28} = 0b1;
  let Inst{25-23} = 0b111;
  let Inst{21-20} = 0b11;
  let Inst{17} = 0b0;
  let Inst{16} = !eq(saturate, 0);
  let Inst{12-11} = 0b00;
  let Inst{10} = saturate;
  let Inst{9-8} = 0b11;
  let Inst{7} = negate;
  let Inst{6} = 0b1;
  let Inst{4} = 0b0;
  let Inst{0} = 0b0;
  let validForTailPredication = 1;
}

multiclass MVE_VABSNEG_int_m<string iname, bit negate, bit saturate,
                             SDPatternOperator unpred_op, Intrinsic pred_int,
                             MVEVectorVTInfo VTI> {
  def "" : MVE_VABSNEG_int<iname, VTI.Suffix, VTI.Size, negate, saturate>;
  defvar Inst = !cast<Instruction>(NAME);

  let Predicates = [HasMVEInt] in {
    // VQABS and VQNEG have more difficult isel patterns defined elsewhere
    if !not(saturate) then {
      def : Pat<(VTI.Vec (unpred_op (VTI.Vec MQPR:$v))),
                (VTI.Vec (Inst $v))>;
    }

    def : Pat<(VTI.Vec (pred_int  (VTI.Vec MQPR:$v), (VTI.Pred VCCR:$mask),
                                  (VTI.Vec MQPR:$inactive))),
              (VTI.Vec (Inst $v, ARMVCCThen, $mask, zero_reg, $inactive))>;
  }
}

foreach VTI = [ MVE_v16s8, MVE_v8s16, MVE_v4s32 ] in {
  defm "MVE_VABS" # VTI.Suffix : MVE_VABSNEG_int_m<
     "vabs",  0, 0, abs,   int_arm_mve_abs_predicated,  VTI>;
  defm "MVE_VQABS" # VTI.Suffix : MVE_VABSNEG_int_m<
     "vqabs", 0, 1, ?,     int_arm_mve_qabs_predicated, VTI>;
  defm "MVE_VNEG" # VTI.Suffix : MVE_VABSNEG_int_m<
     "vneg",  1, 0, vnegq, int_arm_mve_neg_predicated,  VTI>;
  defm "MVE_VQNEG" # VTI.Suffix : MVE_VABSNEG_int_m<
     "vqneg", 1, 1, ?,     int_arm_mve_qneg_predicated, VTI>;
}

// int_min/int_max: vector containing INT_MIN/INT_MAX VTI.Size times
// zero_vec: v4i32-initialized zero vector, potentially wrapped in a bitconvert
multiclass vqabsneg_pattern<MVEVectorVTInfo VTI, dag int_min, dag int_max,
                         dag zero_vec,  MVE_VABSNEG_int vqabs_instruction,
                         MVE_VABSNEG_int vqneg_instruction> {
  let Predicates = [HasMVEInt] in {
    // The below tree can be replaced by a vqabs instruction, as it represents
    // the following vectorized expression (r being the value in $reg):
    // r > 0 ? r : (r == INT_MIN ? INT_MAX : -r)
    def : Pat<(VTI.Vec (vselect
                      (VTI.Pred (ARMvcmpz (VTI.Vec MQPR:$reg), ARMCCgt)),
                      (VTI.Vec MQPR:$reg),
                      (VTI.Vec (vselect
                                (VTI.Pred (ARMvcmp (VTI.Vec MQPR:$reg), int_min, ARMCCeq)),
                                int_max,
                                (sub (VTI.Vec zero_vec), (VTI.Vec MQPR:$reg)))))),
            (VTI.Vec (vqabs_instruction (VTI.Vec MQPR:$reg)))>;
    // Similarly, this tree represents vqneg, i.e. the following vectorized expression:
    // r == INT_MIN ? INT_MAX : -r
    def : Pat<(VTI.Vec (vselect
                        (VTI.Pred (ARMvcmp (VTI.Vec MQPR:$reg), int_min, ARMCCeq)),
                        int_max,
                        (sub (VTI.Vec zero_vec), (VTI.Vec MQPR:$reg)))),
               (VTI.Vec (vqneg_instruction (VTI.Vec MQPR:$reg)))>;
  }
}

defm MVE_VQABSNEG_Ps8  : vqabsneg_pattern<MVE_v16i8,
                                    (v16i8 (ARMvmovImm (i32 3712))),
                                    (v16i8 (ARMvmovImm (i32 3711))),
                                    (bitconvert (v4i32 (ARMvmovImm (i32 0)))),
                                    MVE_VQABSs8, MVE_VQNEGs8>;
defm MVE_VQABSNEG_Ps16 : vqabsneg_pattern<MVE_v8i16,
                                    (v8i16 (ARMvmovImm (i32 2688))),
                                    (v8i16 (ARMvmvnImm (i32 2688))),
                                    (bitconvert (v4i32 (ARMvmovImm (i32 0)))),
                                    MVE_VQABSs16, MVE_VQNEGs16>;
defm MVE_VQABSNEG_Ps32 : vqabsneg_pattern<MVE_v4i32,
                                    (v4i32 (ARMvmovImm (i32 1664))),
                                    (v4i32 (ARMvmvnImm (i32 1664))),
                                    (ARMvmovImm (i32 0)),
                                    MVE_VQABSs32, MVE_VQNEGs32>;

class MVE_mod_imm<string iname, string suffix, bits<4> cmode, bit op,
                  dag iops, bits<2> vecsize, list<dag> pattern=[]>
  : MVE_p<(outs MQPR:$Qd), iops, NoItinerary, iname, suffix, "$Qd, $imm",
          vpred_r, "", vecsize, pattern> {
  bits<13> imm;
  bits<4> Qd;

  let Inst{28} = imm{7};
  let Inst{25-23} = 0b111;
  let Inst{22} = Qd{3};
  let Inst{21-19} = 0b000;
  let Inst{18-16} = imm{6-4};
  let Inst{15-13} = Qd{2-0};
  let Inst{12} = 0b0;
  let Inst{11-8} = cmode{3-0};
  let Inst{7-6} = 0b01;
  let Inst{5} = op;
  let Inst{4} = 0b1;
  let Inst{3-0} = imm{3-0};

  let DecoderMethod = "DecodeMVEModImmInstruction";
  let validForTailPredication = 1;
}

let isReMaterializable = 1 in {
let isAsCheapAsAMove = 1 in {
def MVE_VMOVimmi8  : MVE_mod_imm<"vmov", "i8",  {1,1,1,0}, 0b0, (ins nImmSplatI8:$imm), 0b00>;
def MVE_VMOVimmi16 : MVE_mod_imm<"vmov", "i16", {1,0,?,0}, 0b0, (ins nImmSplatI16:$imm), 0b01> {
  let Inst{9} = imm{9};
}
def MVE_VMOVimmi32 : MVE_mod_imm<"vmov", "i32", {?,?,?,?}, 0b0, (ins nImmVMOVI32:$imm), 0b10> {
  let Inst{11-8} = imm{11-8};
}
def MVE_VMOVimmi64 : MVE_mod_imm<"vmov", "i64", {1,1,1,0}, 0b1, (ins nImmSplatI64:$imm), 0b11>;
def MVE_VMOVimmf32 : MVE_mod_imm<"vmov", "f32", {1,1,1,1}, 0b0, (ins nImmVMOVF32:$imm), 0b10>;
} // let isAsCheapAsAMove = 1

def MVE_VMVNimmi16 : MVE_mod_imm<"vmvn", "i16", {1,0,?,0}, 0b1, (ins nImmSplatI16:$imm), 0b01> {
  let Inst{9} = imm{9};
}
def MVE_VMVNimmi32 : MVE_mod_imm<"vmvn", "i32", {?,?,?,?}, 0b1, (ins nImmVMOVI32:$imm), 0b10> {
  let Inst{11-8} = imm{11-8};
}
} // let isReMaterializable = 1

let Predicates = [HasMVEInt] in {
  def : Pat<(v16i8 (ARMvmovImm timm:$simm)),
            (v16i8 (MVE_VMOVimmi8  nImmSplatI8:$simm))>;
  def : Pat<(v8i16 (ARMvmovImm timm:$simm)),
            (v8i16 (MVE_VMOVimmi16 nImmSplatI16:$simm))>;
  def : Pat<(v4i32 (ARMvmovImm timm:$simm)),
            (v4i32 (MVE_VMOVimmi32 nImmVMOVI32:$simm))>;
  def : Pat<(v2i64 (ARMvmovImm timm:$simm)),
            (v2i64 (MVE_VMOVimmi64 nImmSplatI64:$simm))>;

  def : Pat<(v8i16 (ARMvmvnImm timm:$simm)),
            (v8i16 (MVE_VMVNimmi16 nImmSplatI16:$simm))>;
  def : Pat<(v4i32 (ARMvmvnImm timm:$simm)),
            (v4i32 (MVE_VMVNimmi32 nImmVMOVI32:$simm))>;

  def : Pat<(v4f32 (ARMvmovFPImm timm:$simm)),
            (v4f32 (MVE_VMOVimmf32 nImmVMOVF32:$simm))>;

  def : Pat<(v8i16 (vselect (v8i1 VCCR:$pred), (ARMvmvnImm timm:$simm),
                            MQPR:$inactive)),
            (v8i16 (MVE_VMVNimmi16 nImmSplatI16:$simm,
                            ARMVCCThen, VCCR:$pred, zero_reg, MQPR:$inactive))>;
  def : Pat<(v4i32 (vselect (v4i1 VCCR:$pred), (ARMvmvnImm timm:$simm),
                            MQPR:$inactive)),
            (v4i32 (MVE_VMVNimmi32 nImmSplatI32:$simm,
                            ARMVCCThen, VCCR:$pred, zero_reg, MQPR:$inactive))>;
}

class MVE_VMINMAXA<string iname, string suffix, bits<2> size,
                   bit bit_12, list<dag> pattern=[]>
  : MVE_p<(outs MQPR:$Qd), (ins MQPR:$Qd_src, MQPR:$Qm),
          NoItinerary, iname, suffix, "$Qd, $Qm", vpred_n, "$Qd = $Qd_src",
          size, pattern> {
  bits<4> Qd;
  bits<4> Qm;

  let Inst{28} = 0b0;
  let Inst{25-23} = 0b100;
  let Inst{22} = Qd{3};
  let Inst{21-20} = 0b11;
  let Inst{19-18} = size;
  let Inst{17-16} = 0b11;
  let Inst{15-13} = Qd{2-0};
  let Inst{12} = bit_12;
  let Inst{11-6} = 0b111010;
  let Inst{5} = Qm{3};
  let Inst{4} = 0b0;
  let Inst{3-1} = Qm{2-0};
  let Inst{0} = 0b1;
  let validForTailPredication = 1;
}

multiclass MVE_VMINMAXA_m<string iname, MVEVectorVTInfo VTI,
                      SDNode unpred_op, Intrinsic pred_int, bit bit_12> {
  def "" : MVE_VMINMAXA<iname, VTI.Suffix, VTI.Size, bit_12>;
  defvar Inst = !cast<Instruction>(NAME);

  let Predicates = [HasMVEInt] in {
    // Unpredicated v(min|max)a
    def : Pat<(VTI.Vec (unpred_op (VTI.Vec MQPR:$Qd), (abs (VTI.Vec MQPR:$Qm)))),
              (VTI.Vec (Inst (VTI.Vec MQPR:$Qd), (VTI.Vec MQPR:$Qm)))>;

    // Predicated v(min|max)a
    def : Pat<(VTI.Vec (pred_int (VTI.Vec MQPR:$Qd), (VTI.Vec MQPR:$Qm),
                            (VTI.Pred VCCR:$mask))),
              (VTI.Vec (Inst (VTI.Vec MQPR:$Qd), (VTI.Vec MQPR:$Qm),
                            ARMVCCThen, (VTI.Pred VCCR:$mask), zero_reg))>;
  }
}

multiclass MVE_VMINA<MVEVectorVTInfo VTI>
  : MVE_VMINMAXA_m<"vmina", VTI, umin, int_arm_mve_vmina_predicated, 0b1>;

defm MVE_VMINAs8  : MVE_VMINA<MVE_v16s8>;
defm MVE_VMINAs16 : MVE_VMINA<MVE_v8s16>;
defm MVE_VMINAs32 : MVE_VMINA<MVE_v4s32>;

multiclass MVE_VMAXA<MVEVectorVTInfo VTI>
  : MVE_VMINMAXA_m<"vmaxa", VTI, umax, int_arm_mve_vmaxa_predicated, 0b0>;

defm MVE_VMAXAs8  : MVE_VMAXA<MVE_v16s8>;
defm MVE_VMAXAs16 : MVE_VMAXA<MVE_v8s16>;
defm MVE_VMAXAs32 : MVE_VMAXA<MVE_v4s32>;

// end of MVE Integer instructions

// start of mve_imm_shift instructions

def MVE_VSHLC : MVE_p<(outs rGPR:$RdmDest, MQPR:$Qd),
                      (ins MQPR:$QdSrc, rGPR:$RdmSrc, long_shift:$imm),
                      NoItinerary, "vshlc", "", "$QdSrc, $RdmSrc, $imm",
                      vpred_n, "$RdmDest = $RdmSrc,$Qd = $QdSrc", 0b10> {
  bits<5> imm;
  bits<4> Qd;
  bits<4> RdmDest;

  let Inst{28} = 0b0;
  let Inst{25-23} = 0b101;
  let Inst{22} = Qd{3};
  let Inst{21} = 0b1;
  let Inst{20-16} = imm{4-0};
  let Inst{15-13} = Qd{2-0};
  let Inst{12-4} = 0b011111100;
  let Inst{3-0} = RdmDest{3-0};
}

class MVE_shift_imm<dag oops, dag iops, string iname, string suffix,
                    string ops, vpred_ops vpred, string cstr,
                    bits<2> vecsize, list<dag> pattern=[]>
  : MVE_p<oops, iops, NoItinerary, iname, suffix, ops, vpred, cstr, vecsize, pattern> {
  bits<4> Qd;
  bits<4> Qm;

  let Inst{22} = Qd{3};
  let Inst{15-13} = Qd{2-0};
  let Inst{5} = Qm{3};
  let Inst{3-1} = Qm{2-0};
}

class MVE_VMOVL<string iname, string suffix, bits<2> sz, bit U, bit top,
              list<dag> pattern=[]>
  : MVE_shift_imm<(outs MQPR:$Qd), (ins MQPR:$Qm),
                  iname, suffix, "$Qd, $Qm", vpred_r, "",
                  sz, pattern> {
  let Inst{28} = U;
  let Inst{25-23} = 0b101;
  let Inst{21} = 0b1;
  let Inst{20-19} = sz{1-0};
  let Inst{18-16} = 0b000;
  let Inst{12} = top;
  let Inst{11-6} = 0b111101;
  let Inst{4} = 0b0;
  let Inst{0} = 0b0;
  let doubleWidthResult = 1;
}

multiclass MVE_VMOVL_m<bit top, string chr, MVEVectorVTInfo OutVTI,
                       MVEVectorVTInfo InVTI> {
  def "": MVE_VMOVL<"vmovl" # chr, InVTI.Suffix, OutVTI.Size,
                    InVTI.Unsigned, top>;
  defvar Inst = !cast<Instruction>(NAME);

  def : Pat<(OutVTI.Vec (int_arm_mve_vmovl_predicated (InVTI.Vec MQPR:$src),
                            (i32 InVTI.Unsigned), (i32 top),
                            (OutVTI.Pred VCCR:$pred),
                            (OutVTI.Vec MQPR:$inactive))),
            (OutVTI.Vec (Inst (InVTI.Vec MQPR:$src), ARMVCCThen,
                            (OutVTI.Pred VCCR:$pred), zero_reg,
                            (OutVTI.Vec MQPR:$inactive)))>;
}

defm MVE_VMOVLs8bh  : MVE_VMOVL_m<0, "b", MVE_v8s16, MVE_v16s8>;
defm MVE_VMOVLs8th  : MVE_VMOVL_m<1, "t", MVE_v8s16, MVE_v16s8>;
defm MVE_VMOVLu8bh  : MVE_VMOVL_m<0, "b", MVE_v8u16, MVE_v16u8>;
defm MVE_VMOVLu8th  : MVE_VMOVL_m<1, "t", MVE_v8u16, MVE_v16u8>;
defm MVE_VMOVLs16bh : MVE_VMOVL_m<0, "b", MVE_v4s32, MVE_v8s16>;
defm MVE_VMOVLs16th : MVE_VMOVL_m<1, "t", MVE_v4s32, MVE_v8s16>;
defm MVE_VMOVLu16bh : MVE_VMOVL_m<0, "b", MVE_v4s32, MVE_v8u16>;
defm MVE_VMOVLu16th : MVE_VMOVL_m<1, "t", MVE_v4s32, MVE_v8u16>;

let Predicates = [HasMVEInt] in {
  def : Pat<(sext_inreg (v4i32 MQPR:$src), v4i16),
            (MVE_VMOVLs16bh MQPR:$src)>;
  def : Pat<(sext_inreg (v8i16 MQPR:$src), v8i8),
            (MVE_VMOVLs8bh MQPR:$src)>;
  def : Pat<(sext_inreg (v4i32 MQPR:$src), v4i8),
            (MVE_VMOVLs16bh (MVE_VMOVLs8bh MQPR:$src))>;

  def : Pat<(sext_inreg (v8i16 (ARMVectorRegCast (ARMvrev16 (v16i8 MQPR:$src)))), v8i8),
            (MVE_VMOVLs8th MQPR:$src)>;
  def : Pat<(sext_inreg (v4i32 (ARMVectorRegCast (ARMvrev32 (v8i16 MQPR:$src)))), v4i16),
            (MVE_VMOVLs16th MQPR:$src)>;

  // zext_inreg 8 -> 16
  def : Pat<(ARMvbicImm (v8i16 MQPR:$src), (i32 0xAFF)),
            (MVE_VMOVLu8bh MQPR:$src)>;
  // zext_inreg 16 -> 32
  def : Pat<(and (v4i32 MQPR:$src), (v4i32 (ARMvmovImm (i32 0xCFF)))),
            (MVE_VMOVLu16bh MQPR:$src)>;
  // Same zext_inreg with vrevs, picking the top half
  def : Pat<(ARMvbicImm (v8i16 (ARMVectorRegCast (ARMvrev16 (v16i8 MQPR:$src)))), (i32 0xAFF)),
            (MVE_VMOVLu8th MQPR:$src)>;
  def : Pat<(and (v4i32 (ARMVectorRegCast (ARMvrev32 (v8i16 MQPR:$src)))),
                 (v4i32 (ARMvmovImm (i32 0xCFF)))),
            (MVE_VMOVLu16th MQPR:$src)>;
}


class MVE_VSHLL_imm<string iname, string suffix, bit U, bit th,
                    Operand immtype, bits<2> vecsize, list<dag> pattern=[]>
  : MVE_shift_imm<(outs MQPR:$Qd), (ins MQPR:$Qm, immtype:$imm),
                  iname, suffix, "$Qd, $Qm, $imm", vpred_r, "", vecsize, pattern> {
  let Inst{28} = U;
  let Inst{25-23} = 0b101;
  let Inst{21} = 0b1;
  let Inst{12} = th;
  let Inst{11-6} = 0b111101;
  let Inst{4} = 0b0;
  let Inst{0} = 0b0;

  // For the MVE_VSHLL_patterns multiclass to refer to
  Operand immediateType = immtype;

  let doubleWidthResult = 1;
}

// The immediate VSHLL instructions accept shift counts from 1 up to
// the lane width (8 or 16), but the full-width shifts have an
// entirely separate encoding, given below with 'lw' in the name.

class MVE_VSHLL_imm8<string iname, string suffix,
                     bit U, bit th, list<dag> pattern=[]>
  : MVE_VSHLL_imm<iname, suffix, U, th, mve_shift_imm1_7, 0b01, pattern> {
  bits<3> imm;
  let Inst{20-19} = 0b01;
  let Inst{18-16} = imm;
}

class MVE_VSHLL_imm16<string iname, string suffix,
                      bit U, bit th, list<dag> pattern=[]>
  : MVE_VSHLL_imm<iname, suffix, U, th, mve_shift_imm1_15, 0b10, pattern> {
  bits<4> imm;
  let Inst{20} = 0b1;
  let Inst{19-16} = imm;
}

def MVE_VSHLL_imms8bh  : MVE_VSHLL_imm8 <"vshllb", "s8", 0b0, 0b0>;
def MVE_VSHLL_imms8th  : MVE_VSHLL_imm8 <"vshllt", "s8", 0b0, 0b1>;
def MVE_VSHLL_immu8bh  : MVE_VSHLL_imm8 <"vshllb", "u8", 0b1, 0b0>;
def MVE_VSHLL_immu8th  : MVE_VSHLL_imm8 <"vshllt", "u8", 0b1, 0b1>;
def MVE_VSHLL_imms16bh : MVE_VSHLL_imm16<"vshllb", "s16", 0b0, 0b0>;
def MVE_VSHLL_imms16th : MVE_VSHLL_imm16<"vshllt", "s16", 0b0, 0b1>;
def MVE_VSHLL_immu16bh : MVE_VSHLL_imm16<"vshllb", "u16", 0b1, 0b0>;
def MVE_VSHLL_immu16th : MVE_VSHLL_imm16<"vshllt", "u16", 0b1, 0b1>;

class MVE_VSHLL_by_lane_width<string iname, string suffix, bits<2> size,
                              bit U, string ops, list<dag> pattern=[]>
  : MVE_shift_imm<(outs MQPR:$Qd), (ins MQPR:$Qm),
                  iname, suffix, ops, vpred_r, "", !if(size, 0b10, 0b01), pattern> {
  let Inst{28} = U;
  let Inst{25-23} = 0b100;
  let Inst{21-20} = 0b11;
  let Inst{19-18} = size{1-0};
  let Inst{17-16} = 0b01;
  let Inst{11-6} = 0b111000;
  let Inst{4} = 0b0;
  let Inst{0} = 0b1;
  let doubleWidthResult = 1;
}

multiclass MVE_VSHLL_lw<string iname, string suffix, bits<2> sz, bit U,
                              string ops, list<dag> pattern=[]> {
  def bh : MVE_VSHLL_by_lane_width<iname#"b", suffix, sz, U, ops, pattern> {
    let Inst{12} = 0b0;
  }
  def th : MVE_VSHLL_by_lane_width<iname#"t", suffix, sz, U, ops, pattern> {
    let Inst{12} = 0b1;
  }
}

defm MVE_VSHLL_lws8  : MVE_VSHLL_lw<"vshll", "s8",  0b00, 0b0, "$Qd, $Qm, #8">;
defm MVE_VSHLL_lws16 : MVE_VSHLL_lw<"vshll", "s16", 0b01, 0b0, "$Qd, $Qm, #16">;
defm MVE_VSHLL_lwu8  : MVE_VSHLL_lw<"vshll", "u8",  0b00, 0b1, "$Qd, $Qm, #8">;
defm MVE_VSHLL_lwu16 : MVE_VSHLL_lw<"vshll", "u16", 0b01, 0b1, "$Qd, $Qm, #16">;

multiclass MVE_VSHLL_patterns<MVEVectorVTInfo VTI, int top> {
  defvar suffix     = !strconcat(VTI.Suffix, !if(top, "th", "bh"));
  defvar inst_imm   = !cast<MVE_VSHLL_imm>("MVE_VSHLL_imm" # suffix);
  defvar inst_lw    = !cast<MVE_VSHLL_by_lane_width>("MVE_VSHLL_lw" # suffix);
  defvar unpred_int = int_arm_mve_vshll_imm;
  defvar pred_int   = int_arm_mve_vshll_imm_predicated;
  defvar imm        = inst_imm.immediateType;

  def : Pat<(VTI.DblVec (unpred_int (VTI.Vec MQPR:$src), imm:$imm,
                                    (i32 VTI.Unsigned), (i32 top))),
            (VTI.DblVec (inst_imm   (VTI.Vec MQPR:$src), imm:$imm))>;
  def : Pat<(VTI.DblVec (unpred_int (VTI.Vec MQPR:$src), (i32 VTI.LaneBits),
                                    (i32 VTI.Unsigned), (i32 top))),
            (VTI.DblVec (inst_lw    (VTI.Vec MQPR:$src)))>;

  def : Pat<(VTI.DblVec (pred_int   (VTI.Vec MQPR:$src), imm:$imm,
                                    (i32 VTI.Unsigned), (i32 top),
                                    (VTI.DblPred VCCR:$mask),
                                    (VTI.DblVec MQPR:$inactive))),
            (VTI.DblVec (inst_imm   (VTI.Vec MQPR:$src), imm:$imm,
                                    ARMVCCThen, (VTI.DblPred VCCR:$mask), zero_reg,
                                    (VTI.DblVec MQPR:$inactive)))>;
  def : Pat<(VTI.DblVec (pred_int   (VTI.Vec MQPR:$src), (i32 VTI.LaneBits),
                                    (i32 VTI.Unsigned), (i32 top),
                                    (VTI.DblPred VCCR:$mask),
                                    (VTI.DblVec MQPR:$inactive))),
            (VTI.DblVec (inst_lw    (VTI.Vec MQPR:$src), ARMVCCThen,
                                    (VTI.DblPred VCCR:$mask), zero_reg,
                                    (VTI.DblVec MQPR:$inactive)))>;
}

foreach VTI = [MVE_v16s8, MVE_v8s16, MVE_v16u8, MVE_v8u16] in
  foreach top = [0, 1] in
    defm : MVE_VSHLL_patterns<VTI, top>;

class MVE_shift_imm_partial<Operand imm, string iname, string suffix, bits<2> vecsize>
  : MVE_shift_imm<(outs MQPR:$Qd), (ins MQPR:$QdSrc, MQPR:$Qm, imm:$imm),
                  iname, suffix, "$Qd, $Qm, $imm", vpred_n, "$Qd = $QdSrc", vecsize> {
  Operand immediateType = imm;
}

class MVE_VxSHRN<string iname, string suffix, bit bit_12, bit bit_28,
                 Operand imm, bits<2> vecsize>
  : MVE_shift_imm_partial<imm, iname, suffix, vecsize> {
  bits<5> imm;

  let Inst{28} = bit_28;
  let Inst{25-23} = 0b101;
  let Inst{21} = 0b0;
  let Inst{20-16} = imm{4-0};
  let Inst{12} = bit_12;
  let Inst{11-6} = 0b111111;
  let Inst{4} = 0b0;
  let Inst{0} = 0b1;
  let validForTailPredication = 1;
  let retainsPreviousHalfElement = 1;
}

def MVE_VRSHRNi16bh : MVE_VxSHRN<"vrshrnb", "i16", 0b0, 0b1, shr_imm8, 0b01> {
  let Inst{20-19} = 0b01;
}
def MVE_VRSHRNi16th : MVE_VxSHRN<"vrshrnt", "i16", 0b1, 0b1, shr_imm8, 0b01> {
  let Inst{20-19} = 0b01;
}
def MVE_VRSHRNi32bh : MVE_VxSHRN<"vrshrnb", "i32", 0b0, 0b1, shr_imm16, 0b10> {
  let Inst{20} = 0b1;
}
def MVE_VRSHRNi32th : MVE_VxSHRN<"vrshrnt", "i32", 0b1, 0b1, shr_imm16, 0b10> {
  let Inst{20} = 0b1;
}

def MVE_VSHRNi16bh : MVE_VxSHRN<"vshrnb", "i16", 0b0, 0b0, shr_imm8, 0b01> {
  let Inst{20-19} = 0b01;
}
def MVE_VSHRNi16th : MVE_VxSHRN<"vshrnt", "i16", 0b1, 0b0, shr_imm8, 0b01> {
  let Inst{20-19} = 0b01;
}
def MVE_VSHRNi32bh : MVE_VxSHRN<"vshrnb", "i32", 0b0, 0b0, shr_imm16, 0b10> {
  let Inst{20} = 0b1;
}
def MVE_VSHRNi32th : MVE_VxSHRN<"vshrnt", "i32", 0b1, 0b0, shr_imm16, 0b10> {
  let Inst{20} = 0b1;
}

class MVE_VxQRSHRUN<string iname, string suffix, bit bit_28, bit bit_12,
                    Operand imm, bits<2> vecsize>
  : MVE_shift_imm_partial<imm, iname, suffix, vecsize> {
  bits<5> imm;

  let Inst{28} = bit_28;
  let Inst{25-23} = 0b101;
  let Inst{21} = 0b0;
  let Inst{20-16} = imm{4-0};
  let Inst{12} = bit_12;
  let Inst{11-6} = 0b111111;
  let Inst{4} = 0b0;
  let Inst{0} = 0b0;
  let validForTailPredication = 1;
  let retainsPreviousHalfElement = 1;
}

def MVE_VQRSHRUNs16bh : MVE_VxQRSHRUN<
    "vqrshrunb", "s16", 0b1, 0b0, shr_imm8, 0b01> {
  let Inst{20-19} = 0b01;
}
def MVE_VQRSHRUNs16th : MVE_VxQRSHRUN<
    "vqrshrunt", "s16", 0b1, 0b1, shr_imm8, 0b01> {
  let Inst{20-19} = 0b01;
}
def MVE_VQRSHRUNs32bh : MVE_VxQRSHRUN<
    "vqrshrunb", "s32", 0b1, 0b0, shr_imm16, 0b10> {
  let Inst{20} = 0b1;
}
def MVE_VQRSHRUNs32th : MVE_VxQRSHRUN<
    "vqrshrunt", "s32", 0b1, 0b1, shr_imm16, 0b10> {
  let Inst{20} = 0b1;
}

def MVE_VQSHRUNs16bh : MVE_VxQRSHRUN<
    "vqshrunb", "s16", 0b0, 0b0, shr_imm8, 0b01> {
  let Inst{20-19} = 0b01;
}
def MVE_VQSHRUNs16th : MVE_VxQRSHRUN<
    "vqshrunt", "s16", 0b0, 0b1, shr_imm8, 0b01> {
  let Inst{20-19} = 0b01;
}
def MVE_VQSHRUNs32bh : MVE_VxQRSHRUN<
    "vqshrunb", "s32", 0b0, 0b0, shr_imm16, 0b10> {
  let Inst{20} = 0b1;
}
def MVE_VQSHRUNs32th : MVE_VxQRSHRUN<
    "vqshrunt", "s32", 0b0, 0b1, shr_imm16, 0b10> {
  let Inst{20} = 0b1;
}

class MVE_VxQRSHRN<string iname, string suffix, bit bit_0, bit bit_12,
                   Operand imm, bits<2> vecsize>
  : MVE_shift_imm_partial<imm, iname, suffix, vecsize> {
  bits<5> imm;

  let Inst{25-23} = 0b101;
  let Inst{21} = 0b0;
  let Inst{20-16} = imm{4-0};
  let Inst{12} = bit_12;
  let Inst{11-6} = 0b111101;
  let Inst{4} = 0b0;
  let Inst{0} = bit_0;
  let validForTailPredication = 1;
  let retainsPreviousHalfElement = 1;
}

multiclass MVE_VxQRSHRN_types<string iname, bit bit_0, bit bit_12> {
  def s16 : MVE_VxQRSHRN<iname, "s16", bit_0, bit_12, shr_imm8, 0b01> {
    let Inst{28} = 0b0;
    let Inst{20-19} = 0b01;
  }
  def u16 : MVE_VxQRSHRN<iname, "u16", bit_0, bit_12, shr_imm8, 0b01> {
    let Inst{28} = 0b1;
    let Inst{20-19} = 0b01;
  }
  def s32 : MVE_VxQRSHRN<iname, "s32", bit_0, bit_12, shr_imm16, 0b10> {
    let Inst{28} = 0b0;
    let Inst{20} = 0b1;
  }
  def u32 : MVE_VxQRSHRN<iname, "u32", bit_0, bit_12, shr_imm16, 0b10> {
    let Inst{28} = 0b1;
    let Inst{20} = 0b1;
  }
}

defm MVE_VQRSHRNbh : MVE_VxQRSHRN_types<"vqrshrnb", 0b1, 0b0>;
defm MVE_VQRSHRNth : MVE_VxQRSHRN_types<"vqrshrnt", 0b1, 0b1>;
defm MVE_VQSHRNbh  : MVE_VxQRSHRN_types<"vqshrnb", 0b0, 0b0>;
defm MVE_VQSHRNth  : MVE_VxQRSHRN_types<"vqshrnt", 0b0, 0b1>;

multiclass MVE_VSHRN_patterns<MVE_shift_imm_partial inst,
                              MVEVectorVTInfo OutVTI, MVEVectorVTInfo InVTI,
                              bit q, bit r, bit top> {
  defvar inparams = (? (OutVTI.Vec MQPR:$QdSrc), (InVTI.Vec MQPR:$Qm),
                       (inst.immediateType:$imm), (i32 q), (i32 r),
                       (i32 OutVTI.Unsigned), (i32 InVTI.Unsigned), (i32 top));
  defvar outparams = (inst (OutVTI.Vec MQPR:$QdSrc), (InVTI.Vec MQPR:$Qm),
                           (imm:$imm));

  def : Pat<(OutVTI.Vec !setdagop(inparams, int_arm_mve_vshrn)),
            (OutVTI.Vec outparams)>;
  def : Pat<(OutVTI.Vec !con(inparams, (int_arm_mve_vshrn_predicated
                                           (InVTI.Pred VCCR:$pred)))),
            (OutVTI.Vec !con(outparams, (? ARMVCCThen, VCCR:$pred, zero_reg)))>;
}

defm : MVE_VSHRN_patterns<MVE_VSHRNi16bh,    MVE_v16s8, MVE_v8s16, 0,0,0>;
defm : MVE_VSHRN_patterns<MVE_VSHRNi16th,    MVE_v16s8, MVE_v8s16, 0,0,1>;
defm : MVE_VSHRN_patterns<MVE_VSHRNi32bh,    MVE_v8s16, MVE_v4s32, 0,0,0>;
defm : MVE_VSHRN_patterns<MVE_VSHRNi32th,    MVE_v8s16, MVE_v4s32, 0,0,1>;
defm : MVE_VSHRN_patterns<MVE_VSHRNi16bh,    MVE_v16u8, MVE_v8u16, 0,0,0>;
defm : MVE_VSHRN_patterns<MVE_VSHRNi16th,    MVE_v16u8, MVE_v8u16, 0,0,1>;
defm : MVE_VSHRN_patterns<MVE_VSHRNi32bh,    MVE_v8u16, MVE_v4u32, 0,0,0>;
defm : MVE_VSHRN_patterns<MVE_VSHRNi32th,    MVE_v8u16, MVE_v4u32, 0,0,1>;
defm : MVE_VSHRN_patterns<MVE_VRSHRNi16bh,   MVE_v16s8, MVE_v8s16, 0,1,0>;
defm : MVE_VSHRN_patterns<MVE_VRSHRNi16th,   MVE_v16s8, MVE_v8s16, 0,1,1>;
defm : MVE_VSHRN_patterns<MVE_VRSHRNi32bh,   MVE_v8s16, MVE_v4s32, 0,1,0>;
defm : MVE_VSHRN_patterns<MVE_VRSHRNi32th,   MVE_v8s16, MVE_v4s32, 0,1,1>;
defm : MVE_VSHRN_patterns<MVE_VRSHRNi16bh,   MVE_v16u8, MVE_v8u16, 0,1,0>;
defm : MVE_VSHRN_patterns<MVE_VRSHRNi16th,   MVE_v16u8, MVE_v8u16, 0,1,1>;
defm : MVE_VSHRN_patterns<MVE_VRSHRNi32bh,   MVE_v8u16, MVE_v4u32, 0,1,0>;
defm : MVE_VSHRN_patterns<MVE_VRSHRNi32th,   MVE_v8u16, MVE_v4u32, 0,1,1>;
defm : MVE_VSHRN_patterns<MVE_VQSHRNbhs16,   MVE_v16s8, MVE_v8s16, 1,0,0>;
defm : MVE_VSHRN_patterns<MVE_VQSHRNths16,   MVE_v16s8, MVE_v8s16, 1,0,1>;
defm : MVE_VSHRN_patterns<MVE_VQSHRNbhs32,   MVE_v8s16, MVE_v4s32, 1,0,0>;
defm : MVE_VSHRN_patterns<MVE_VQSHRNths32,   MVE_v8s16, MVE_v4s32, 1,0,1>;
defm : MVE_VSHRN_patterns<MVE_VQSHRNbhu16,   MVE_v16u8, MVE_v8u16, 1,0,0>;
defm : MVE_VSHRN_patterns<MVE_VQSHRNthu16,   MVE_v16u8, MVE_v8u16, 1,0,1>;
defm : MVE_VSHRN_patterns<MVE_VQSHRNbhu32,   MVE_v8u16, MVE_v4u32, 1,0,0>;
defm : MVE_VSHRN_patterns<MVE_VQSHRNthu32,   MVE_v8u16, MVE_v4u32, 1,0,1>;
defm : MVE_VSHRN_patterns<MVE_VQRSHRNbhs16,  MVE_v16s8, MVE_v8s16, 1,1,0>;
defm : MVE_VSHRN_patterns<MVE_VQRSHRNths16,  MVE_v16s8, MVE_v8s16, 1,1,1>;
defm : MVE_VSHRN_patterns<MVE_VQRSHRNbhs32,  MVE_v8s16, MVE_v4s32, 1,1,0>;
defm : MVE_VSHRN_patterns<MVE_VQRSHRNths32,  MVE_v8s16, MVE_v4s32, 1,1,1>;
defm : MVE_VSHRN_patterns<MVE_VQRSHRNbhu16,  MVE_v16u8, MVE_v8u16, 1,1,0>;
defm : MVE_VSHRN_patterns<MVE_VQRSHRNthu16,  MVE_v16u8, MVE_v8u16, 1,1,1>;
defm : MVE_VSHRN_patterns<MVE_VQRSHRNbhu32,  MVE_v8u16, MVE_v4u32, 1,1,0>;
defm : MVE_VSHRN_patterns<MVE_VQRSHRNthu32,  MVE_v8u16, MVE_v4u32, 1,1,1>;
defm : MVE_VSHRN_patterns<MVE_VQSHRUNs16bh,  MVE_v16u8, MVE_v8s16, 1,0,0>;
defm : MVE_VSHRN_patterns<MVE_VQSHRUNs16th,  MVE_v16u8, MVE_v8s16, 1,0,1>;
defm : MVE_VSHRN_patterns<MVE_VQSHRUNs32bh,  MVE_v8u16, MVE_v4s32, 1,0,0>;
defm : MVE_VSHRN_patterns<MVE_VQSHRUNs32th,  MVE_v8u16, MVE_v4s32, 1,0,1>;
defm : MVE_VSHRN_patterns<MVE_VQRSHRUNs16bh, MVE_v16u8, MVE_v8s16, 1,1,0>;
defm : MVE_VSHRN_patterns<MVE_VQRSHRUNs16th, MVE_v16u8, MVE_v8s16, 1,1,1>;
defm : MVE_VSHRN_patterns<MVE_VQRSHRUNs32bh, MVE_v8u16, MVE_v4s32, 1,1,0>;
defm : MVE_VSHRN_patterns<MVE_VQRSHRUNs32th, MVE_v8u16, MVE_v4s32, 1,1,1>;

// end of mve_imm_shift instructions

// start of mve_shift instructions

class MVE_shift_by_vec<string iname, string suffix, bit U,
                       bits<2> size, bit bit_4, bit bit_8>
  : MVE_p<(outs MQPR:$Qd), (ins MQPR:$Qm, MQPR:$Qn), NoItinerary,
           iname, suffix, "$Qd, $Qm, $Qn", vpred_r, "", size, []> {
  // Shift instructions which take a vector of shift counts
  bits<4> Qd;
  bits<4> Qm;
  bits<4> Qn;

  let Inst{28} = U;
  let Inst{25-24} = 0b11;
  let Inst{23} = 0b0;
  let Inst{22} = Qd{3};
  let Inst{21-20} = size;
  let Inst{19-17} = Qn{2-0};
  let Inst{16} = 0b0;
  let Inst{15-13} = Qd{2-0};
  let Inst{12-9} = 0b0010;
  let Inst{8} = bit_8;
  let Inst{7} = Qn{3};
  let Inst{6} = 0b1;
  let Inst{5} = Qm{3};
  let Inst{4} = bit_4;
  let Inst{3-1} = Qm{2-0};
  let Inst{0} = 0b0;
  let validForTailPredication = 1;
}

multiclass MVE_shift_by_vec_p<string iname, MVEVectorVTInfo VTI, bit q, bit r> {
  def "" : MVE_shift_by_vec<iname, VTI.Suffix, VTI.Unsigned, VTI.Size, q, r>;
  defvar Inst = !cast<Instruction>(NAME);

  def : Pat<(VTI.Vec (int_arm_mve_vshl_vector
                         (VTI.Vec MQPR:$in), (VTI.Vec MQPR:$sh),
                         (i32 q), (i32 r), (i32 VTI.Unsigned))),
            (VTI.Vec (Inst (VTI.Vec MQPR:$in), (VTI.Vec MQPR:$sh)))>;

  def : Pat<(VTI.Vec (int_arm_mve_vshl_vector_predicated
                         (VTI.Vec MQPR:$in), (VTI.Vec MQPR:$sh),
                         (i32 q), (i32 r), (i32 VTI.Unsigned),
                         (VTI.Pred VCCR:$mask), (VTI.Vec MQPR:$inactive))),
            (VTI.Vec (Inst (VTI.Vec MQPR:$in), (VTI.Vec MQPR:$sh),
                           ARMVCCThen, (VTI.Pred VCCR:$mask), zero_reg,
                           (VTI.Vec MQPR:$inactive)))>;
}

multiclass mve_shift_by_vec_multi<string iname, bit bit_4, bit bit_8> {
  defm s8  : MVE_shift_by_vec_p<iname, MVE_v16s8, bit_4, bit_8>;
  defm s16 : MVE_shift_by_vec_p<iname, MVE_v8s16, bit_4, bit_8>;
  defm s32 : MVE_shift_by_vec_p<iname, MVE_v4s32, bit_4, bit_8>;
  defm u8  : MVE_shift_by_vec_p<iname, MVE_v16u8, bit_4, bit_8>;
  defm u16 : MVE_shift_by_vec_p<iname, MVE_v8u16, bit_4, bit_8>;
  defm u32 : MVE_shift_by_vec_p<iname, MVE_v4u32, bit_4, bit_8>;
}

defm MVE_VSHL_by_vec   : mve_shift_by_vec_multi<"vshl",   0b0, 0b0>;
defm MVE_VQSHL_by_vec  : mve_shift_by_vec_multi<"vqshl",  0b1, 0b0>;
defm MVE_VQRSHL_by_vec : mve_shift_by_vec_multi<"vqrshl", 0b1, 0b1>;
defm MVE_VRSHL_by_vec  : mve_shift_by_vec_multi<"vrshl",  0b0, 0b1>;

let Predicates = [HasMVEInt] in {
  defm : MVE_TwoOpPattern<MVE_v16i8, ARMvshlu, int_arm_mve_vshl_vector_predicated,
                          (? (i32 0), (i32 0), (i32 1)), MVE_VSHL_by_vecu8, null_frag>;
  defm : MVE_TwoOpPattern<MVE_v8i16, ARMvshlu, int_arm_mve_vshl_vector_predicated,
                          (? (i32 0), (i32 0), (i32 1)), MVE_VSHL_by_vecu16, null_frag>;
  defm : MVE_TwoOpPattern<MVE_v4i32, ARMvshlu, int_arm_mve_vshl_vector_predicated,
                          (? (i32 0), (i32 0), (i32 1)), MVE_VSHL_by_vecu32, null_frag>;
  defm : MVE_TwoOpPattern<MVE_v16i8, ARMvshls, int_arm_mve_vshl_vector_predicated,
                          (? (i32 0), (i32 0), (i32 0)), MVE_VSHL_by_vecs8, null_frag>;
  defm : MVE_TwoOpPattern<MVE_v8i16, ARMvshls, int_arm_mve_vshl_vector_predicated,
                          (? (i32 0), (i32 0), (i32 0)), MVE_VSHL_by_vecs16, null_frag>;
  defm : MVE_TwoOpPattern<MVE_v4i32, ARMvshls, int_arm_mve_vshl_vector_predicated,
                          (? (i32 0), (i32 0), (i32 0)), MVE_VSHL_by_vecs32, null_frag>;
}

class MVE_shift_with_imm<string iname, string suffix, dag oops, dag iops,
                         string ops, vpred_ops vpred, string cstr,
                         bits<2> vecsize, list<dag> pattern=[]>
  : MVE_p<oops, iops, NoItinerary, iname, suffix, ops, vpred, cstr, vecsize, pattern> {
  bits<4> Qd;
  bits<4> Qm;

  let Inst{23} = 0b1;
  let Inst{22} = Qd{3};
  let Inst{15-13} = Qd{2-0};
  let Inst{12-11} = 0b00;
  let Inst{7-6} = 0b01;
  let Inst{5} = Qm{3};
  let Inst{4} = 0b1;
  let Inst{3-1} = Qm{2-0};
  let Inst{0} = 0b0;
  let validForTailPredication = 1;

  // For the MVE_shift_imm_patterns multiclass to refer to
  MVEVectorVTInfo VTI;
  Operand immediateType;
  Intrinsic unpred_int;
  Intrinsic pred_int;
  dag unsignedFlag = (?);
}

class MVE_VSxI_imm<string iname, string suffix, bit bit_8, Operand immType, bits<2> vecsize>
  : MVE_shift_with_imm<iname, suffix, (outs MQPR:$Qd),
                       (ins MQPR:$Qd_src, MQPR:$Qm, immType:$imm),
                       "$Qd, $Qm, $imm", vpred_n, "$Qd = $Qd_src", vecsize> {
  bits<6> imm;
  let Inst{28} = 0b1;
  let Inst{25-24} = 0b11;
  let Inst{21-16} = imm;
  let Inst{10-9} = 0b10;
  let Inst{8} = bit_8;
  let validForTailPredication = 1;

  Operand immediateType = immType;
}

def MVE_VSRIimm8 : MVE_VSxI_imm<"vsri", "8", 0b0, shr_imm8, 0b00> {
  let Inst{21-19} = 0b001;
}

def MVE_VSRIimm16 : MVE_VSxI_imm<"vsri", "16", 0b0, shr_imm16, 0b01> {
  let Inst{21-20} = 0b01;
}

def MVE_VSRIimm32 : MVE_VSxI_imm<"vsri", "32", 0b0, shr_imm32, 0b10> {
  let Inst{21} = 0b1;
}

def MVE_VSLIimm8 : MVE_VSxI_imm<"vsli", "8", 0b1, imm0_7, 0b00> {
  let Inst{21-19} = 0b001;
}

def MVE_VSLIimm16 : MVE_VSxI_imm<"vsli", "16", 0b1, imm0_15, 0b01> {
  let Inst{21-20} = 0b01;
}

def MVE_VSLIimm32 : MVE_VSxI_imm<"vsli", "32", 0b1,imm0_31, 0b10> {
  let Inst{21} = 0b1;
}

multiclass MVE_VSxI_patterns<MVE_VSxI_imm inst, string name,
                              MVEVectorVTInfo VTI> {
  defvar inparams = (? (VTI.Vec MQPR:$QdSrc), (VTI.Vec MQPR:$Qm),
                       (inst.immediateType:$imm));
  defvar outparams = (inst (VTI.Vec MQPR:$QdSrc), (VTI.Vec MQPR:$Qm),
                           (inst.immediateType:$imm));
  defvar unpred_int = !cast<Intrinsic>("int_arm_mve_" # name);
  defvar pred_int = !cast<Intrinsic>("int_arm_mve_" # name # "_predicated");

  def : Pat<(VTI.Vec !setdagop(inparams, unpred_int)),
            (VTI.Vec outparams)>;
  def : Pat<(VTI.Vec !con(inparams, (pred_int (VTI.Pred VCCR:$pred)))),
            (VTI.Vec !con(outparams, (? ARMVCCThen, VCCR:$pred, zero_reg)))>;
}

defm : MVE_VSxI_patterns<MVE_VSLIimm8,  "vsli", MVE_v16i8>;
defm : MVE_VSxI_patterns<MVE_VSLIimm16, "vsli", MVE_v8i16>;
defm : MVE_VSxI_patterns<MVE_VSLIimm32, "vsli", MVE_v4i32>;
defm : MVE_VSxI_patterns<MVE_VSRIimm8,  "vsri", MVE_v16i8>;
defm : MVE_VSxI_patterns<MVE_VSRIimm16, "vsri", MVE_v8i16>;
defm : MVE_VSxI_patterns<MVE_VSRIimm32, "vsri", MVE_v4i32>;

class MVE_VQSHL_imm<MVEVectorVTInfo VTI_, Operand immType>
  : MVE_shift_with_imm<"vqshl", VTI_.Suffix, (outs MQPR:$Qd),
                       (ins MQPR:$Qm, immType:$imm), "$Qd, $Qm, $imm",
                       vpred_r, "", VTI_.Size> {
  bits<6> imm;

  let Inst{28} = VTI_.Unsigned;
  let Inst{25-24} = 0b11;
  let Inst{21-16} = imm;
  let Inst{10-8} = 0b111;

  let VTI = VTI_;
  let immediateType = immType;
  let unsignedFlag = (? (i32 VTI.Unsigned));
}

let unpred_int = int_arm_mve_vqshl_imm,
    pred_int = int_arm_mve_vqshl_imm_predicated in {
  def MVE_VQSHLimms8 : MVE_VQSHL_imm<MVE_v16s8, imm0_7> {
    let Inst{21-19} = 0b001;
  }
  def MVE_VQSHLimmu8 : MVE_VQSHL_imm<MVE_v16u8, imm0_7> {
    let Inst{21-19} = 0b001;
  }
  
  def MVE_VQSHLimms16 : MVE_VQSHL_imm<MVE_v8s16, imm0_15> {
    let Inst{21-20} = 0b01;
  }
  def MVE_VQSHLimmu16 : MVE_VQSHL_imm<MVE_v8u16, imm0_15> {
    let Inst{21-20} = 0b01;
  }
  
  def MVE_VQSHLimms32 : MVE_VQSHL_imm<MVE_v4s32, imm0_31> {
    let Inst{21} = 0b1;
  }
  def MVE_VQSHLimmu32 : MVE_VQSHL_imm<MVE_v4u32, imm0_31> {
    let Inst{21} = 0b1;
  }
}

class MVE_VQSHLU_imm<MVEVectorVTInfo VTI_, Operand immType>
  : MVE_shift_with_imm<"vqshlu", VTI_.Suffix, (outs MQPR:$Qd),
                       (ins MQPR:$Qm, immType:$imm), "$Qd, $Qm, $imm",
                       vpred_r, "", VTI_.Size> {
  bits<6> imm;

  let Inst{28} = 0b1;
  let Inst{25-24} = 0b11;
  let Inst{21-16} = imm;
  let Inst{10-8} = 0b110;

  let VTI = VTI_;
  let immediateType = immType;
}

let unpred_int = int_arm_mve_vqshlu_imm,
    pred_int = int_arm_mve_vqshlu_imm_predicated in {
  def MVE_VQSHLU_imms8 : MVE_VQSHLU_imm<MVE_v16s8, imm0_7> {
    let Inst{21-19} = 0b001;
  }

  def MVE_VQSHLU_imms16 : MVE_VQSHLU_imm<MVE_v8s16, imm0_15> {
    let Inst{21-20} = 0b01;
  }

  def MVE_VQSHLU_imms32 : MVE_VQSHLU_imm<MVE_v4s32, imm0_31> {
    let Inst{21} = 0b1;
  }
}

class MVE_VRSHR_imm<MVEVectorVTInfo VTI_, Operand immType>
  : MVE_shift_with_imm<"vrshr", VTI_.Suffix, (outs MQPR:$Qd),
                       (ins MQPR:$Qm, immType:$imm), "$Qd, $Qm, $imm",
                       vpred_r, "", VTI_.Size> {
  bits<6> imm;

  let Inst{28} = VTI_.Unsigned;
  let Inst{25-24} = 0b11;
  let Inst{21-16} = imm;
  let Inst{10-8} = 0b010;

  let VTI = VTI_;
  let immediateType = immType;
  let unsignedFlag = (? (i32 VTI.Unsigned));
}

let unpred_int = int_arm_mve_vrshr_imm,
    pred_int = int_arm_mve_vrshr_imm_predicated in {
  def MVE_VRSHR_imms8 : MVE_VRSHR_imm<MVE_v16s8, shr_imm8> {
    let Inst{21-19} = 0b001;
  }

  def MVE_VRSHR_immu8 : MVE_VRSHR_imm<MVE_v16u8, shr_imm8> {
    let Inst{21-19} = 0b001;
  }

  def MVE_VRSHR_imms16 : MVE_VRSHR_imm<MVE_v8s16, shr_imm16> {
    let Inst{21-20} = 0b01;
  }

  def MVE_VRSHR_immu16 : MVE_VRSHR_imm<MVE_v8u16, shr_imm16> {
    let Inst{21-20} = 0b01;
  }

  def MVE_VRSHR_imms32 : MVE_VRSHR_imm<MVE_v4s32, shr_imm32> {
    let Inst{21} = 0b1;
  }

  def MVE_VRSHR_immu32 : MVE_VRSHR_imm<MVE_v4u32, shr_imm32> {
    let Inst{21} = 0b1;
  }
}

multiclass MVE_shift_imm_patterns<MVE_shift_with_imm inst> {
  def : Pat<(inst.VTI.Vec !con((inst.unpred_int (inst.VTI.Vec MQPR:$src),
                                                inst.immediateType:$imm),
                               inst.unsignedFlag)),
            (inst.VTI.Vec (inst (inst.VTI.Vec MQPR:$src),
                                inst.immediateType:$imm))>;

  def : Pat<(inst.VTI.Vec !con((inst.pred_int (inst.VTI.Vec MQPR:$src),
                                              inst.immediateType:$imm),
                               inst.unsignedFlag,
                               (? (inst.VTI.Pred VCCR:$mask),
                                  (inst.VTI.Vec MQPR:$inactive)))),
            (inst.VTI.Vec (inst (inst.VTI.Vec MQPR:$src),
                                inst.immediateType:$imm,
                                ARMVCCThen, (inst.VTI.Pred VCCR:$mask), zero_reg,
                                (inst.VTI.Vec MQPR:$inactive)))>;
}

defm : MVE_shift_imm_patterns<MVE_VQSHLimms8>;
defm : MVE_shift_imm_patterns<MVE_VQSHLimmu8>;
defm : MVE_shift_imm_patterns<MVE_VQSHLimms16>;
defm : MVE_shift_imm_patterns<MVE_VQSHLimmu16>;
defm : MVE_shift_imm_patterns<MVE_VQSHLimms32>;
defm : MVE_shift_imm_patterns<MVE_VQSHLimmu32>;
defm : MVE_shift_imm_patterns<MVE_VQSHLU_imms8>;
defm : MVE_shift_imm_patterns<MVE_VQSHLU_imms16>;
defm : MVE_shift_imm_patterns<MVE_VQSHLU_imms32>;
defm : MVE_shift_imm_patterns<MVE_VRSHR_imms8>;
defm : MVE_shift_imm_patterns<MVE_VRSHR_immu8>;
defm : MVE_shift_imm_patterns<MVE_VRSHR_imms16>;
defm : MVE_shift_imm_patterns<MVE_VRSHR_immu16>;
defm : MVE_shift_imm_patterns<MVE_VRSHR_imms32>;
defm : MVE_shift_imm_patterns<MVE_VRSHR_immu32>;

class MVE_VSHR_imm<string suffix, dag imm, bits<2> vecsize>
  : MVE_shift_with_imm<"vshr", suffix, (outs MQPR:$Qd),
                       !con((ins MQPR:$Qm), imm), "$Qd, $Qm, $imm",
                       vpred_r, "", vecsize> {
  bits<6> imm;

  let Inst{25-24} = 0b11;
  let Inst{21-16} = imm;
  let Inst{10-8} = 0b000;
}

def MVE_VSHR_imms8 : MVE_VSHR_imm<"s8", (ins shr_imm8:$imm), 0b00> {
  let Inst{28} = 0b0;
  let Inst{21-19} = 0b001;
}

def MVE_VSHR_immu8 : MVE_VSHR_imm<"u8", (ins shr_imm8:$imm), 0b00> {
  let Inst{28} = 0b1;
  let Inst{21-19} = 0b001;
}

def MVE_VSHR_imms16 : MVE_VSHR_imm<"s16", (ins shr_imm16:$imm), 0b01> {
  let Inst{28} = 0b0;
  let Inst{21-20} = 0b01;
}

def MVE_VSHR_immu16 : MVE_VSHR_imm<"u16", (ins shr_imm16:$imm), 0b01> {
  let Inst{28} = 0b1;
  let Inst{21-20} = 0b01;
}

def MVE_VSHR_imms32 : MVE_VSHR_imm<"s32", (ins shr_imm32:$imm), 0b10> {
  let Inst{28} = 0b0;
  let Inst{21} = 0b1;
}

def MVE_VSHR_immu32 : MVE_VSHR_imm<"u32", (ins shr_imm32:$imm), 0b10> {
  let Inst{28} = 0b1;
  let Inst{21} = 0b1;
}

class MVE_VSHL_imm<string suffix, dag imm, bits<2> vecsize>
  : MVE_shift_with_imm<"vshl", suffix, (outs MQPR:$Qd),
                       !con((ins MQPR:$Qm), imm), "$Qd, $Qm, $imm",
                       vpred_r, "", vecsize> {
  bits<6> imm;

  let Inst{28} = 0b0;
  let Inst{25-24} = 0b11;
  let Inst{21-16} = imm;
  let Inst{10-8} = 0b101;
}

def MVE_VSHL_immi8 : MVE_VSHL_imm<"i8", (ins imm0_7:$imm), 0b00> {
  let Inst{21-19} = 0b001;
}

def MVE_VSHL_immi16 : MVE_VSHL_imm<"i16", (ins imm0_15:$imm), 0b01> {
  let Inst{21-20} = 0b01;
}

def MVE_VSHL_immi32 : MVE_VSHL_imm<"i32", (ins imm0_31:$imm), 0b10> {
  let Inst{21} = 0b1;
}

multiclass MVE_immediate_shift_patterns_inner<
    MVEVectorVTInfo VTI, Operand imm_operand_type, SDNode unpred_op,
    Intrinsic pred_int, Instruction inst, list<int> unsignedFlag = []> {

  def : Pat<(VTI.Vec (unpred_op (VTI.Vec MQPR:$src), imm_operand_type:$imm)),
            (VTI.Vec (inst (VTI.Vec MQPR:$src), imm_operand_type:$imm))>;

  def : Pat<(VTI.Vec !con((pred_int (VTI.Vec MQPR:$src), imm_operand_type:$imm),
                          !dag(pred_int, unsignedFlag, ?),
                          (pred_int (VTI.Pred VCCR:$mask),
                                   (VTI.Vec MQPR:$inactive)))),
            (VTI.Vec (inst (VTI.Vec MQPR:$src), imm_operand_type:$imm,
                           ARMVCCThen, (VTI.Pred VCCR:$mask), zero_reg,
                           (VTI.Vec MQPR:$inactive)))>;
}

multiclass MVE_immediate_shift_patterns<MVEVectorVTInfo VTI,
                                        Operand imm_operand_type> {
  defm : MVE_immediate_shift_patterns_inner<VTI, imm_operand_type,
      ARMvshlImm, int_arm_mve_shl_imm_predicated,
      !cast<Instruction>("MVE_VSHL_immi" # VTI.BitsSuffix)>;
  defm : MVE_immediate_shift_patterns_inner<VTI, imm_operand_type,
      ARMvshruImm, int_arm_mve_shr_imm_predicated,
      !cast<Instruction>("MVE_VSHR_immu" # VTI.BitsSuffix), [1]>;
  defm : MVE_immediate_shift_patterns_inner<VTI, imm_operand_type,
      ARMvshrsImm, int_arm_mve_shr_imm_predicated,
      !cast<Instruction>("MVE_VSHR_imms" # VTI.BitsSuffix), [0]>;
}

let Predicates = [HasMVEInt] in {
  defm : MVE_immediate_shift_patterns<MVE_v16i8, imm0_7>;
  defm : MVE_immediate_shift_patterns<MVE_v8i16, imm0_15>;
  defm : MVE_immediate_shift_patterns<MVE_v4i32, imm0_31>;
}

// end of mve_shift instructions

// start of MVE Floating Point instructions

class MVE_float<string iname, string suffix, dag oops, dag iops, string ops,
                vpred_ops vpred, string cstr, bits<2> vecsize, list<dag> pattern=[]>
  : MVE_f<oops, iops, NoItinerary, iname, suffix, ops, vpred, cstr, vecsize, pattern> {
  bits<4> Qm;

  let Inst{12} = 0b0;
  let Inst{6} = 0b1;
  let Inst{5} = Qm{3};
  let Inst{3-1} = Qm{2-0};
  let Inst{0} = 0b0;
}

class MVE_VRINT<string rmode, bits<3> op, string suffix, bits<2> size,
                list<dag> pattern=[]>
  : MVE_float<!strconcat("vrint", rmode), suffix, (outs MQPR:$Qd),
              (ins MQPR:$Qm), "$Qd, $Qm", vpred_r, "", size, pattern> {
  bits<4> Qd;

  let Inst{28} = 0b1;
  let Inst{25-23} = 0b111;
  let Inst{22} = Qd{3};
  let Inst{21-20} = 0b11;
  let Inst{19-18} = size;
  let Inst{17-16} = 0b10;
  let Inst{15-13} = Qd{2-0};
  let Inst{11-10} = 0b01;
  let Inst{9-7} = op{2-0};
  let Inst{4} = 0b0;
  let validForTailPredication = 1;

}

multiclass MVE_VRINT_m<MVEVectorVTInfo VTI, string suffix, bits<3> opcode,
                       SDPatternOperator unpred_op> {
  def "": MVE_VRINT<suffix, opcode, VTI.Suffix, VTI.Size>;
  defvar Inst = !cast<Instruction>(NAME);
  defvar pred_int = !cast<Intrinsic>("int_arm_mve_vrint"#suffix#"_predicated");

  let Predicates = [HasMVEFloat] in {
    def : Pat<(VTI.Vec (unpred_op (VTI.Vec MQPR:$val))),
              (VTI.Vec (Inst (VTI.Vec MQPR:$val)))>;
    def : Pat<(VTI.Vec (pred_int (VTI.Vec MQPR:$val), (VTI.Pred VCCR:$pred),
                                 (VTI.Vec MQPR:$inactive))),
              (VTI.Vec (Inst (VTI.Vec MQPR:$val), ARMVCCThen,
                             (VTI.Pred VCCR:$pred), zero_reg, (VTI.Vec MQPR:$inactive)))>;
  }
}

multiclass MVE_VRINT_ops<MVEVectorVTInfo VTI> {
  defm N : MVE_VRINT_m<VTI, "n", 0b000, int_arm_mve_vrintn>;
  defm X : MVE_VRINT_m<VTI, "x", 0b001, frint>;
  defm A : MVE_VRINT_m<VTI, "a", 0b010, fround>;
  defm Z : MVE_VRINT_m<VTI, "z", 0b011, ftrunc>;
  defm M : MVE_VRINT_m<VTI, "m", 0b101, ffloor>;
  defm P : MVE_VRINT_m<VTI, "p", 0b111, fceil>;
}

defm MVE_VRINTf16 : MVE_VRINT_ops<MVE_v8f16>;
defm MVE_VRINTf32 : MVE_VRINT_ops<MVE_v4f32>;

class MVEFloatArithNeon<string iname, string suffix, bit size,
                           dag oops, dag iops, string ops,
                           vpred_ops vpred, string cstr, bits<2> vecsize, list<dag> pattern=[]>
  : MVE_float<iname, suffix, oops, iops, ops, vpred, cstr, vecsize, pattern> {
  let Inst{20} = size;
  let Inst{16} = 0b0;
}

class MVE_VMUL_fp<string iname, string suffix, bits<2> size, list<dag> pattern=[]>
  : MVEFloatArithNeon<iname, suffix, size{0}, (outs MQPR:$Qd),
                      (ins MQPR:$Qn, MQPR:$Qm), "$Qd, $Qn, $Qm", vpred_r, "",
                      size, pattern> {
  bits<4> Qd;
  bits<4> Qn;

  let Inst{28} = 0b1;
  let Inst{25-23} = 0b110;
  let Inst{22} = Qd{3};
  let Inst{21} = 0b0;
  let Inst{19-17} = Qn{2-0};
  let Inst{15-13} = Qd{2-0};
  let Inst{12-8} = 0b01101;
  let Inst{7} = Qn{3};
  let Inst{4} = 0b1;
  let validForTailPredication = 1;
}

multiclass MVE_VMULT_fp_m<string iname, MVEVectorVTInfo VTI, SDNode Op,
                          Intrinsic PredInt, SDPatternOperator IdentityVec> {
  def "" : MVE_VMUL_fp<iname, VTI.Suffix, VTI.Size>;
  defvar Inst = !cast<Instruction>(NAME);

  let Predicates = [HasMVEFloat] in {
    defm : MVE_TwoOpPattern<VTI, Op, PredInt, (? ), !cast<Instruction>(NAME), IdentityVec>;
  }
}

multiclass MVE_VMUL_fp_m<MVEVectorVTInfo VTI, SDPatternOperator IdentityVec>
  : MVE_VMULT_fp_m<"vmul", VTI, fmul, int_arm_mve_mul_predicated, IdentityVec>;

def ARMimmOneF: PatLeaf<(bitconvert (v4f32 (ARMvmovFPImm (i32 112))))>; // 1.0 float
def ARMimmOneH: PatLeaf<(bitconvert (v8i16 (ARMvmovImm (i32 2620))))>; // 1.0 half

defm MVE_VMULf32 : MVE_VMUL_fp_m<MVE_v4f32, ARMimmOneF>;
defm MVE_VMULf16 : MVE_VMUL_fp_m<MVE_v8f16, ARMimmOneH>;

class MVE_VCMLA<string suffix, bits<2> size>
  : MVEFloatArithNeon<"vcmla", suffix, size{1}, (outs MQPR:$Qd),
                         (ins MQPR:$Qd_src, MQPR:$Qn, MQPR:$Qm, complexrotateop:$rot),
                         "$Qd, $Qn, $Qm, $rot", vpred_n, "$Qd = $Qd_src", size, []> {
  bits<4> Qd;
  bits<4> Qn;
  bits<2> rot;

  let Inst{28} = 0b1;
  let Inst{25} = 0b0;
  let Inst{24-23} = rot;
  let Inst{22} = Qd{3};
  let Inst{21} = 0b1;
  let Inst{19-17} = Qn{2-0};
  let Inst{15-13} = Qd{2-0};
  let Inst{12-8} = 0b01000;
  let Inst{7} = Qn{3};
  let Inst{4} = 0b0;
}

multiclass MVE_VCMLA_m<MVEVectorVTInfo VTI> {
  def "" : MVE_VCMLA<VTI.Suffix, VTI.Size>;
  defvar Inst = !cast<Instruction>(NAME);

  let Predicates = [HasMVEFloat] in {
    def : Pat<(VTI.Vec (int_arm_mve_vcmlaq
                            imm:$rot, (VTI.Vec MQPR:$Qd_src),
                            (VTI.Vec MQPR:$Qn), (VTI.Vec MQPR:$Qm))),
              (VTI.Vec (Inst (VTI.Vec MQPR:$Qd_src),
                             (VTI.Vec MQPR:$Qn), (VTI.Vec MQPR:$Qm),
                             imm:$rot))>;

    def: Pat<(VTI.Vec (fadd_contract MQPR:$Qd_src,
                            (int_arm_mve_vcmulq imm:$rot,
                              (VTI.Vec MQPR:$Qn), (VTI.Vec MQPR:$Qm)))),
              (VTI.Vec (Inst (VTI.Vec MQPR:$Qd_src),
                             (VTI.Vec MQPR:$Qn), (VTI.Vec MQPR:$Qm),
                             imm:$rot))>;

    def : Pat<(VTI.Vec (int_arm_mve_vcmlaq_predicated
                            imm:$rot, (VTI.Vec MQPR:$Qd_src),
                            (VTI.Vec MQPR:$Qn), (VTI.Vec MQPR:$Qm),
                            (VTI.Pred VCCR:$mask))),
              (VTI.Vec (Inst (VTI.Vec MQPR:$Qd_src), (VTI.Vec MQPR:$Qn),
                             (VTI.Vec MQPR:$Qm), imm:$rot,
                             ARMVCCThen, (VTI.Pred VCCR:$mask), zero_reg))>;
  }
}

defm MVE_VCMLAf16 : MVE_VCMLA_m<MVE_v8f16>;
defm MVE_VCMLAf32 : MVE_VCMLA_m<MVE_v4f32>;

class MVE_VADDSUBFMA_fp<string iname, string suffix, bits<2> size, bit bit_4,
                        bit bit_8, bit bit_21, dag iops=(ins),
                        vpred_ops vpred=vpred_r, string cstr="",
                        list<dag> pattern=[]>
  : MVEFloatArithNeon<iname, suffix, size{0}, (outs MQPR:$Qd),
                      !con(iops, (ins MQPR:$Qn, MQPR:$Qm)), "$Qd, $Qn, $Qm",
                      vpred, cstr, size, pattern> {
  bits<4> Qd;
  bits<4> Qn;

  let Inst{28} = 0b0;
  let Inst{25-23} = 0b110;
  let Inst{22} = Qd{3};
  let Inst{21} = bit_21;
  let Inst{19-17} = Qn{2-0};
  let Inst{15-13} = Qd{2-0};
  let Inst{11-9} = 0b110;
  let Inst{8} = bit_8;
  let Inst{7} = Qn{3};
  let Inst{4} = bit_4;
  let validForTailPredication = 1;
}

multiclass MVE_VFMA_fp_multi<string iname, bit fms, MVEVectorVTInfo VTI> {
  def "" : MVE_VADDSUBFMA_fp<iname, VTI.Suffix, VTI.Size, 0b1, 0b0, fms,
                             (ins MQPR:$Qd_src), vpred_n, "$Qd = $Qd_src">;
  defvar Inst = !cast<Instruction>(NAME);
  defvar pred_int = int_arm_mve_fma_predicated;
  defvar m1   = (VTI.Vec MQPR:$m1);
  defvar m2   = (VTI.Vec MQPR:$m2);
  defvar add  = (VTI.Vec MQPR:$add);
  defvar pred = (VTI.Pred VCCR:$pred);

  let Predicates = [HasMVEFloat] in {
    if fms then {
      def : Pat<(VTI.Vec (fma (fneg m1), m2, add)),
                (Inst $add, $m1, $m2)>;
      def : Pat<(VTI.Vec (vselect (VTI.Pred VCCR:$pred),
                                  (VTI.Vec (fma (fneg m1), m2, add)),
                                  add)),
                (Inst $add, $m1, $m2, ARMVCCThen, $pred, zero_reg)>;
      def : Pat<(VTI.Vec (pred_int (fneg m1), m2, add, pred)),
                (Inst $add, $m1, $m2, ARMVCCThen, $pred, zero_reg)>;
      def : Pat<(VTI.Vec (pred_int m1, (fneg m2), add, pred)),
                (Inst $add, $m1, $m2, ARMVCCThen, $pred, zero_reg)>;
    } else {
      def : Pat<(VTI.Vec (fma m1, m2, add)),
                (Inst $add, $m1, $m2)>;
      def : Pat<(VTI.Vec (vselect (VTI.Pred VCCR:$pred),
                                  (VTI.Vec (fma m1, m2, add)),
                                  add)),
                (Inst $add, $m1, $m2, ARMVCCThen, $pred, zero_reg)>;
      def : Pat<(VTI.Vec (pred_int m1, m2, add, pred)),
                (Inst $add, $m1, $m2, ARMVCCThen, $pred, zero_reg)>;
    }
  }
}

defm MVE_VFMAf32 : MVE_VFMA_fp_multi<"vfma", 0, MVE_v4f32>;
defm MVE_VFMAf16 : MVE_VFMA_fp_multi<"vfma", 0, MVE_v8f16>;
defm MVE_VFMSf32 : MVE_VFMA_fp_multi<"vfms", 1, MVE_v4f32>;
defm MVE_VFMSf16 : MVE_VFMA_fp_multi<"vfms", 1, MVE_v8f16>;

multiclass MVE_VADDSUB_fp_m<string iname, bit bit_21, MVEVectorVTInfo VTI,
                            SDNode Op, Intrinsic PredInt, SDPatternOperator IdentityVec> {
  def "" : MVE_VADDSUBFMA_fp<iname, VTI.Suffix, VTI.Size, 0, 1, bit_21> {
    let validForTailPredication = 1;
  }
  defvar Inst = !cast<Instruction>(NAME);

  let Predicates = [HasMVEFloat] in {
    defm : MVE_TwoOpPattern<VTI, Op, PredInt, (? ), !cast<Instruction>(NAME), IdentityVec>;
  }
}

multiclass MVE_VADD_fp_m<MVEVectorVTInfo VTI, SDPatternOperator IdentityVec>
  : MVE_VADDSUB_fp_m<"vadd", 0, VTI, fadd, int_arm_mve_add_predicated, IdentityVec>;
multiclass MVE_VSUB_fp_m<MVEVectorVTInfo VTI, SDPatternOperator IdentityVec>
  : MVE_VADDSUB_fp_m<"vsub", 1, VTI, fsub, int_arm_mve_sub_predicated, IdentityVec>;

def ARMimmMinusZeroF: PatLeaf<(bitconvert (v4i32 (ARMvmovImm (i32 1664))))>; // -0.0 float
def ARMimmMinusZeroH: PatLeaf<(bitconvert (v8i16 (ARMvmovImm (i32 2688))))>; // -0.0 half

defm MVE_VADDf32 : MVE_VADD_fp_m<MVE_v4f32, ARMimmMinusZeroF>;
defm MVE_VADDf16 : MVE_VADD_fp_m<MVE_v8f16, ARMimmMinusZeroH>;

defm MVE_VSUBf32 : MVE_VSUB_fp_m<MVE_v4f32, ARMimmAllZerosV>;
defm MVE_VSUBf16 : MVE_VSUB_fp_m<MVE_v8f16, ARMimmAllZerosV>;

class MVE_VCADD<string suffix, bits<2> size, string cstr="">
  : MVEFloatArithNeon<"vcadd", suffix, size{1}, (outs MQPR:$Qd),
                         (ins MQPR:$Qn, MQPR:$Qm, complexrotateopodd:$rot),
                         "$Qd, $Qn, $Qm, $rot", vpred_r, cstr, size, []> {
  bits<4> Qd;
  bits<4> Qn;
  bit rot;

  let Inst{28} = 0b1;
  let Inst{25} = 0b0;
  let Inst{24} = rot;
  let Inst{23} = 0b1;
  let Inst{22} = Qd{3};
  let Inst{21} = 0b0;
  let Inst{19-17} = Qn{2-0};
  let Inst{15-13} = Qd{2-0};
  let Inst{12-8} = 0b01000;
  let Inst{7} = Qn{3};
  let Inst{4} = 0b0;
}

multiclass MVE_VCADD_m<MVEVectorVTInfo VTI, string cstr=""> {
  def "" : MVE_VCADD<VTI.Suffix, VTI.Size, cstr>;
  defvar Inst = !cast<Instruction>(NAME);

  let Predicates = [HasMVEFloat] in {
    def : Pat<(VTI.Vec (int_arm_mve_vcaddq (i32 1),
                            imm:$rot, (VTI.Vec MQPR:$Qn), (VTI.Vec MQPR:$Qm))),
              (VTI.Vec (Inst (VTI.Vec MQPR:$Qn), (VTI.Vec MQPR:$Qm),
                             imm:$rot))>;

    def : Pat<(VTI.Vec (int_arm_mve_vcaddq_predicated (i32 1),
                            imm:$rot, (VTI.Vec MQPR:$inactive),
                            (VTI.Vec MQPR:$Qn), (VTI.Vec MQPR:$Qm),
                            (VTI.Pred VCCR:$mask))),
              (VTI.Vec (Inst (VTI.Vec MQPR:$Qn), (VTI.Vec MQPR:$Qm),
                             imm:$rot, ARMVCCThen, (VTI.Pred VCCR:$mask), zero_reg,
                             (VTI.Vec MQPR:$inactive)))>;

  }
}

defm MVE_VCADDf16 : MVE_VCADD_m<MVE_v8f16>;
defm MVE_VCADDf32 : MVE_VCADD_m<MVE_v4f32, "@earlyclobber $Qd">;

class MVE_VABD_fp<string suffix, bits<2> size>
  : MVE_float<"vabd", suffix, (outs MQPR:$Qd), (ins MQPR:$Qn, MQPR:$Qm),
              "$Qd, $Qn, $Qm", vpred_r, "", size> {
  bits<4> Qd;
  bits<4> Qn;

  let Inst{28} = 0b1;
  let Inst{25-23} = 0b110;
  let Inst{22} = Qd{3};
  let Inst{21} = 0b1;
  let Inst{20} = size{0};
  let Inst{19-17} = Qn{2-0};
  let Inst{16} = 0b0;
  let Inst{15-13} = Qd{2-0};
  let Inst{11-8} = 0b1101;
  let Inst{7} = Qn{3};
  let Inst{4} = 0b0;
  let validForTailPredication = 1;
}

multiclass MVE_VABDT_fp_m<MVEVectorVTInfo VTI,
                            Intrinsic unpred_int, Intrinsic pred_int> {
  def "" : MVE_VABD_fp<VTI.Suffix, VTI.Size>;
  defvar Inst = !cast<Instruction>(NAME);

  let Predicates = [HasMVEFloat] in {
    def : Pat<(VTI.Vec (unpred_int (VTI.Vec MQPR:$Qm), (VTI.Vec MQPR:$Qn),
                            (i32 0))),
              (VTI.Vec (Inst (VTI.Vec MQPR:$Qm), (VTI.Vec MQPR:$Qn)))>;
    def : Pat<(VTI.Vec (pred_int (VTI.Vec MQPR:$Qm), (VTI.Vec MQPR:$Qn),
                            (i32 0), (VTI.Pred VCCR:$mask),
                            (VTI.Vec MQPR:$inactive))),
              (VTI.Vec (Inst (VTI.Vec MQPR:$Qm), (VTI.Vec MQPR:$Qn),
                             ARMVCCThen, (VTI.Pred VCCR:$mask), zero_reg,
                             (VTI.Vec MQPR:$inactive)))>;
  }
}

multiclass MVE_VABD_fp_m<MVEVectorVTInfo VTI>
  : MVE_VABDT_fp_m<VTI, int_arm_mve_vabd, int_arm_mve_abd_predicated>;

defm MVE_VABDf32 : MVE_VABD_fp_m<MVE_v4f32>;
defm MVE_VABDf16 : MVE_VABD_fp_m<MVE_v8f16>;

let Predicates = [HasMVEFloat] in {
  def : Pat<(v8f16 (fabs (fsub (v8f16 MQPR:$Qm), (v8f16 MQPR:$Qn)))),
            (MVE_VABDf16 MQPR:$Qm, MQPR:$Qn)>;
  def : Pat<(v4f32 (fabs (fsub (v4f32 MQPR:$Qm), (v4f32 MQPR:$Qn)))),
            (MVE_VABDf32 MQPR:$Qm, MQPR:$Qn)>;
}

class MVE_VCVT_fix<string suffix, bit fsi, bit U, bit op,
                   Operand imm_operand_type>
  : MVE_float<"vcvt", suffix,
              (outs MQPR:$Qd), (ins MQPR:$Qm, imm_operand_type:$imm6),
              "$Qd, $Qm, $imm6", vpred_r, "", !if(fsi, 0b10, 0b01), []> {
  bits<4> Qd;
  bits<6> imm6;

  let Inst{28} = U;
  let Inst{25-23} = 0b111;
  let Inst{22} = Qd{3};
  let Inst{21} = 0b1;
  let Inst{19-16} = imm6{3-0};
  let Inst{15-13} = Qd{2-0};
  let Inst{11-10} = 0b11;
  let Inst{9} = fsi;
  let Inst{8} = op;
  let Inst{7} = 0b0;
  let Inst{4} = 0b1;

  let DecoderMethod = "DecodeMVEVCVTt1fp";
  let validForTailPredication = 1;
}

class MVE_VCVT_imm_asmop<int Bits> : AsmOperandClass {
  let PredicateMethod = "isImmediate<1," # Bits # ">";
  let DiagnosticString =
      "MVE fixed-point immediate operand must be between 1 and " # Bits;
  let Name = "MVEVcvtImm" # Bits;
  let RenderMethod = "addImmOperands";
}
class MVE_VCVT_imm<int Bits>: Operand<i32> {
  let ParserMatchClass = MVE_VCVT_imm_asmop<Bits>;
  let EncoderMethod = "getNEONVcvtImm32OpValue";
  let DecoderMethod = "DecodeVCVTImmOperand";
}

class MVE_VCVT_fix_f32<string suffix, bit U, bit op>
    : MVE_VCVT_fix<suffix, 0b1, U, op, MVE_VCVT_imm<32>> {
  let Inst{20} = imm6{4};
}
class MVE_VCVT_fix_f16<string suffix, bit U, bit op>
    : MVE_VCVT_fix<suffix, 0b0, U, op, MVE_VCVT_imm<16>> {
  let Inst{20} = 0b1;
}

multiclass MVE_VCVT_fix_patterns<Instruction Inst, bit U, MVEVectorVTInfo DestVTI,
                                 MVEVectorVTInfo SrcVTI> {
  let Predicates = [HasMVEFloat] in {
    def : Pat<(DestVTI.Vec (int_arm_mve_vcvt_fix
                              (i32 U), (SrcVTI.Vec MQPR:$Qm), imm:$scale)),
              (DestVTI.Vec (Inst (SrcVTI.Vec MQPR:$Qm), imm:$scale))>;
    def : Pat<(DestVTI.Vec (int_arm_mve_vcvt_fix_predicated (i32 U),
                              (DestVTI.Vec MQPR:$inactive),
                              (SrcVTI.Vec MQPR:$Qm),
                              imm:$scale,
                              (DestVTI.Pred VCCR:$mask))),
              (DestVTI.Vec (Inst (SrcVTI.Vec MQPR:$Qm), imm:$scale,
                             ARMVCCThen, (DestVTI.Pred VCCR:$mask), zero_reg,
                             (DestVTI.Vec MQPR:$inactive)))>;
  }
}

multiclass MVE_VCVT_fix_f32_m<bit U, bit op,
                              MVEVectorVTInfo DestVTI, MVEVectorVTInfo SrcVTI> {
  def "" : MVE_VCVT_fix_f32<DestVTI.Suffix#"."#SrcVTI.Suffix, U, op>;
  defm : MVE_VCVT_fix_patterns<!cast<Instruction>(NAME), U, DestVTI, SrcVTI>;
}

multiclass MVE_VCVT_fix_f16_m<bit U, bit op,
                              MVEVectorVTInfo DestVTI, MVEVectorVTInfo SrcVTI> {
  def "" : MVE_VCVT_fix_f16<DestVTI.Suffix#"."#SrcVTI.Suffix, U, op>;
  defm : MVE_VCVT_fix_patterns<!cast<Instruction>(NAME), U, DestVTI, SrcVTI>;
}

defm MVE_VCVTf16s16_fix : MVE_VCVT_fix_f16_m<0b0, 0b0, MVE_v8f16, MVE_v8s16>;
defm MVE_VCVTs16f16_fix : MVE_VCVT_fix_f16_m<0b0, 0b1, MVE_v8s16, MVE_v8f16>;
defm MVE_VCVTf16u16_fix : MVE_VCVT_fix_f16_m<0b1, 0b0, MVE_v8f16, MVE_v8u16>;
defm MVE_VCVTu16f16_fix : MVE_VCVT_fix_f16_m<0b1, 0b1, MVE_v8u16, MVE_v8f16>;
defm MVE_VCVTf32s32_fix : MVE_VCVT_fix_f32_m<0b0, 0b0, MVE_v4f32, MVE_v4s32>;
defm MVE_VCVTs32f32_fix : MVE_VCVT_fix_f32_m<0b0, 0b1, MVE_v4s32, MVE_v4f32>;
defm MVE_VCVTf32u32_fix : MVE_VCVT_fix_f32_m<0b1, 0b0, MVE_v4f32, MVE_v4u32>;
defm MVE_VCVTu32f32_fix : MVE_VCVT_fix_f32_m<0b1, 0b1, MVE_v4u32, MVE_v4f32>;

class MVE_VCVT_fp_int_anpm<string suffix, bits<2> size, bit op, string anpm,
                bits<2> rm, list<dag> pattern=[]>
  : MVE_float<!strconcat("vcvt", anpm), suffix, (outs MQPR:$Qd),
              (ins MQPR:$Qm), "$Qd, $Qm", vpred_r, "", size, pattern> {
  bits<4> Qd;

  let Inst{28} = 0b1;
  let Inst{25-23} = 0b111;
  let Inst{22} = Qd{3};
  let Inst{21-20} = 0b11;
  let Inst{19-18} = size;
  let Inst{17-16} = 0b11;
  let Inst{15-13} = Qd{2-0};
  let Inst{12-10} = 0b000;
  let Inst{9-8} = rm;
  let Inst{7} = op;
  let Inst{4} = 0b0;
  let validForTailPredication = 1;
}

multiclass MVE_VCVT_fp_int_anpm_inner<MVEVectorVTInfo Int, MVEVectorVTInfo Flt,
                                      string anpm, bits<2> rm> {
  def "": MVE_VCVT_fp_int_anpm<Int.Suffix # "." # Flt.Suffix, Int.Size,
                               Int.Unsigned, anpm, rm>;

  defvar Inst         = !cast<Instruction>(NAME);
  defvar IntrBaseName = "int_arm_mve_vcvt" # anpm;
  defvar UnpredIntr   = !cast<Intrinsic>(IntrBaseName);
  defvar PredIntr     = !cast<Intrinsic>(IntrBaseName # "_predicated");

  let Predicates = [HasMVEFloat] in {
    def : Pat<(Int.Vec (UnpredIntr (i32 Int.Unsigned), (Flt.Vec MQPR:$in))),
              (Int.Vec (Inst (Flt.Vec MQPR:$in)))>;

    def : Pat<(Int.Vec (PredIntr (i32 Int.Unsigned), (Int.Vec MQPR:$inactive),
                                 (Flt.Vec MQPR:$in), (Flt.Pred VCCR:$pred))),
              (Int.Vec (Inst (Flt.Vec MQPR:$in), ARMVCCThen,
                             (Flt.Pred VCCR:$pred), zero_reg, (Int.Vec MQPR:$inactive)))>;
  }
}

multiclass MVE_VCVT_fp_int_anpm_outer<MVEVectorVTInfo Int,
                                      MVEVectorVTInfo Flt> {
  defm a : MVE_VCVT_fp_int_anpm_inner<Int, Flt, "a", 0b00>;
  defm n : MVE_VCVT_fp_int_anpm_inner<Int, Flt, "n", 0b01>;
  defm p : MVE_VCVT_fp_int_anpm_inner<Int, Flt, "p", 0b10>;
  defm m : MVE_VCVT_fp_int_anpm_inner<Int, Flt, "m", 0b11>;
}

// This defines instructions such as MVE_VCVTu16f16a, with an explicit
// rounding-mode suffix on the mnemonic. The class below will define
// the bare MVE_VCVTu16f16 (with implied rounding toward zero).
defm MVE_VCVTs16f16 : MVE_VCVT_fp_int_anpm_outer<MVE_v8s16, MVE_v8f16>;
defm MVE_VCVTu16f16 : MVE_VCVT_fp_int_anpm_outer<MVE_v8u16, MVE_v8f16>;
defm MVE_VCVTs32f32 : MVE_VCVT_fp_int_anpm_outer<MVE_v4s32, MVE_v4f32>;
defm MVE_VCVTu32f32 : MVE_VCVT_fp_int_anpm_outer<MVE_v4u32, MVE_v4f32>;

class MVE_VCVT_fp_int<string suffix, bits<2> size, bit toint, bit unsigned,
                      list<dag> pattern=[]>
  : MVE_float<"vcvt", suffix, (outs MQPR:$Qd),
              (ins MQPR:$Qm), "$Qd, $Qm", vpred_r, "", size, pattern> {
  bits<4> Qd;

  let Inst{28} = 0b1;
  let Inst{25-23} = 0b111;
  let Inst{22} = Qd{3};
  let Inst{21-20} = 0b11;
  let Inst{19-18} = size;
  let Inst{17-16} = 0b11;
  let Inst{15-13} = Qd{2-0};
  let Inst{12-9} = 0b0011;
  let Inst{8} = toint;
  let Inst{7} = unsigned;
  let Inst{4} = 0b0;
  let validForTailPredication = 1;
}

multiclass MVE_VCVT_fp_int_m<MVEVectorVTInfo Dest, MVEVectorVTInfo Src,
                             SDNode unpred_op> {
  defvar Unsigned = !or(!eq(Dest.SuffixLetter,"u"), !eq(Src.SuffixLetter,"u"));
  defvar ToInt = !eq(Src.SuffixLetter,"f");

  def "" : MVE_VCVT_fp_int<Dest.Suffix # "." # Src.Suffix, Dest.Size,
                           ToInt, Unsigned>;
  defvar Inst = !cast<Instruction>(NAME);

  let Predicates = [HasMVEFloat] in {
    def : Pat<(Dest.Vec (unpred_op (Src.Vec MQPR:$src))),
              (Dest.Vec (Inst (Src.Vec MQPR:$src)))>;
    def : Pat<(Dest.Vec (int_arm_mve_vcvt_fp_int_predicated
                             (Src.Vec MQPR:$src), (i32 Unsigned),
                             (Src.Pred VCCR:$mask), (Dest.Vec MQPR:$inactive))),
              (Dest.Vec (Inst (Src.Vec MQPR:$src), ARMVCCThen,
                              (Src.Pred VCCR:$mask), zero_reg,
                              (Dest.Vec MQPR:$inactive)))>;
  }
}
// The unsuffixed VCVT for float->int implicitly rounds toward zero,
// which I reflect here in the llvm instruction names
defm MVE_VCVTs16f16z : MVE_VCVT_fp_int_m<MVE_v8s16, MVE_v8f16, fp_to_sint>;
defm MVE_VCVTu16f16z : MVE_VCVT_fp_int_m<MVE_v8u16, MVE_v8f16, fp_to_uint>;
defm MVE_VCVTs32f32z : MVE_VCVT_fp_int_m<MVE_v4s32, MVE_v4f32, fp_to_sint>;
defm MVE_VCVTu32f32z : MVE_VCVT_fp_int_m<MVE_v4u32, MVE_v4f32, fp_to_uint>;
// Whereas VCVT for int->float rounds to nearest
defm MVE_VCVTf16s16n : MVE_VCVT_fp_int_m<MVE_v8f16, MVE_v8s16, sint_to_fp>;
defm MVE_VCVTf16u16n : MVE_VCVT_fp_int_m<MVE_v8f16, MVE_v8u16, uint_to_fp>;
defm MVE_VCVTf32s32n : MVE_VCVT_fp_int_m<MVE_v4f32, MVE_v4s32, sint_to_fp>;
defm MVE_VCVTf32u32n : MVE_VCVT_fp_int_m<MVE_v4f32, MVE_v4u32, uint_to_fp>;

let Predicates = [HasMVEFloat] in {
  def : Pat<(v4i32 (fp_to_sint_sat v4f32:$src, i32)),
            (MVE_VCVTs32f32z v4f32:$src)>;
  def : Pat<(v4i32 (fp_to_uint_sat v4f32:$src, i32)),
            (MVE_VCVTu32f32z v4f32:$src)>;
  def : Pat<(v8i16 (fp_to_sint_sat v8f16:$src, i16)),
            (MVE_VCVTs16f16z v8f16:$src)>;
  def : Pat<(v8i16 (fp_to_uint_sat v8f16:$src, i16)),
            (MVE_VCVTu16f16z v8f16:$src)>;
}

class MVE_VABSNEG_fp<string iname, string suffix, bits<2> size, bit negate,
                   list<dag> pattern=[]>
  : MVE_float<iname, suffix, (outs MQPR:$Qd),
              (ins MQPR:$Qm), "$Qd, $Qm", vpred_r, "", size, pattern> {
  bits<4> Qd;

  let Inst{28} = 0b1;
  let Inst{25-23} = 0b111;
  let Inst{22} = Qd{3};
  let Inst{21-20} = 0b11;
  let Inst{19-18} = size;
  let Inst{17-16} = 0b01;
  let Inst{15-13} = Qd{2-0};
  let Inst{11-8} = 0b0111;
  let Inst{7} = negate;
  let Inst{4} = 0b0;
  let validForTailPredication = 1;
}

multiclass MVE_VABSNEG_fp_m<string iname, SDNode unpred_op, Intrinsic pred_int,
                            MVEVectorVTInfo VTI, bit opcode> {
  def "" : MVE_VABSNEG_fp<iname, VTI.Suffix, VTI.Size, opcode>;
  defvar Inst = !cast<Instruction>(NAME);

  let Predicates = [HasMVEInt] in {
    def : Pat<(VTI.Vec (unpred_op (VTI.Vec MQPR:$v))),
              (VTI.Vec (Inst $v))>;
    def : Pat<(VTI.Vec (pred_int  (VTI.Vec MQPR:$v), (VTI.Pred VCCR:$mask),
                                  (VTI.Vec MQPR:$inactive))),
              (VTI.Vec (Inst $v, ARMVCCThen, $mask, zero_reg, $inactive))>;
  }
}

defm MVE_VABSf16 : MVE_VABSNEG_fp_m<"vabs", fabs, int_arm_mve_abs_predicated,
                                    MVE_v8f16, 0>;
defm MVE_VABSf32 : MVE_VABSNEG_fp_m<"vabs", fabs, int_arm_mve_abs_predicated,
                                    MVE_v4f32, 0>;
defm MVE_VNEGf16 : MVE_VABSNEG_fp_m<"vneg", fneg, int_arm_mve_neg_predicated,
                                    MVE_v8f16, 1>;
defm MVE_VNEGf32 : MVE_VABSNEG_fp_m<"vneg", fneg, int_arm_mve_neg_predicated,
                                    MVE_v4f32, 1>;

class MVE_VMAXMINNMA<string iname, string suffix, bits<2> size, bit bit_12,
                     list<dag> pattern=[]>
  : MVE_f<(outs MQPR:$Qd), (ins MQPR:$Qd_src, MQPR:$Qm),
          NoItinerary, iname, suffix, "$Qd, $Qm", vpred_n, "$Qd = $Qd_src",
          size, pattern> {
  bits<4> Qd;
  bits<4> Qm;

  let Inst{28} = size{0};
  let Inst{25-23} = 0b100;
  let Inst{22} = Qd{3};
  let Inst{21-16} = 0b111111;
  let Inst{15-13} = Qd{2-0};
  let Inst{12} = bit_12;
  let Inst{11-6} = 0b111010;
  let Inst{5} = Qm{3};
  let Inst{4} = 0b0;
  let Inst{3-1} = Qm{2-0};
  let Inst{0} = 0b1;

  let isCommutable = 1;
  let validForTailPredication = 1;
}

multiclass MVE_VMAXMINNMA_m<string iname, MVEVectorVTInfo VTI,
                      SDNode unpred_op, Intrinsic pred_int,
                      bit bit_12> {
  def "" : MVE_VMAXMINNMA<iname, VTI.Suffix, VTI.Size, bit_12>;
  defvar Inst = !cast<Instruction>(NAME);

  let Predicates = [HasMVEInt] in {
    // Unpredicated v(max|min)nma
    def : Pat<(VTI.Vec (unpred_op (fabs (VTI.Vec MQPR:$Qd)),
                                  (fabs (VTI.Vec MQPR:$Qm)))),
              (VTI.Vec (Inst (VTI.Vec MQPR:$Qd), (VTI.Vec MQPR:$Qm)))>;

    // Predicated v(max|min)nma
    def : Pat<(VTI.Vec (pred_int (VTI.Vec MQPR:$Qd), (VTI.Vec MQPR:$Qm),
                            (VTI.Pred VCCR:$mask))),
              (VTI.Vec (Inst (VTI.Vec MQPR:$Qd), (VTI.Vec MQPR:$Qm),
                            ARMVCCThen, (VTI.Pred VCCR:$mask), zero_reg))>;
  }
}

multiclass MVE_VMAXNMA<MVEVectorVTInfo VTI, bit bit_12>
  : MVE_VMAXMINNMA_m<"vmaxnma", VTI, fmaxnum, int_arm_mve_vmaxnma_predicated, bit_12>;

defm MVE_VMAXNMAf32 : MVE_VMAXNMA<MVE_v4f32, 0b0>;
defm MVE_VMAXNMAf16 : MVE_VMAXNMA<MVE_v8f16, 0b0>;

multiclass MVE_VMINNMA<MVEVectorVTInfo VTI, bit bit_12>
  : MVE_VMAXMINNMA_m<"vminnma", VTI, fminnum, int_arm_mve_vminnma_predicated, bit_12>;

defm MVE_VMINNMAf32 : MVE_VMINNMA<MVE_v4f32, 0b1>;
defm MVE_VMINNMAf16 : MVE_VMINNMA<MVE_v8f16, 0b1>;

// end of MVE Floating Point instructions

// start of MVE compares

class MVE_VCMPqq<string suffix, bit bit_28, bits<2> bits_21_20,
                 VCMPPredicateOperand predtype, bits<2> vecsize, list<dag> pattern=[]>
  : MVE_p<(outs VCCR:$P0), (ins MQPR:$Qn, MQPR:$Qm, predtype:$fc),
           NoItinerary, "vcmp", suffix, "$fc, $Qn, $Qm", vpred_n, "", vecsize, pattern> {
  // Base class for comparing two vector registers
  bits<3> fc;
  bits<4> Qn;
  bits<4> Qm;

  let Inst{28} = bit_28;
  let Inst{25-22} = 0b1000;
  let Inst{21-20} = bits_21_20;
  let Inst{19-17} = Qn{2-0};
  let Inst{16-13} = 0b1000;
  let Inst{12} = fc{2};
  let Inst{11-8} = 0b1111;
  let Inst{7} = fc{0};
  let Inst{6} = 0b0;
  let Inst{5} = Qm{3};
  let Inst{4} = 0b0;
  let Inst{3-1} = Qm{2-0};
  let Inst{0} = fc{1};

  let Constraints = "";

  // We need a custom decoder method for these instructions because of
  // the output VCCR operand, which isn't encoded in the instruction
  // bits anywhere (there is only one choice for it) but has to be
  // included in the MC operands so that codegen will be able to track
  // its data flow between instructions, spill/reload it when
  // necessary, etc. There seems to be no way to get the Tablegen
  // decoder to emit an operand that isn't affected by any instruction
  // bit.
  let DecoderMethod = "DecodeMVEVCMP<false," # predtype.DecoderMethod # ">";
  let validForTailPredication = 1;
}

class MVE_VCMPqqf<string suffix, bit size>
    : MVE_VCMPqq<suffix, size, 0b11, pred_basic_fp, !if(size, 0b01, 0b10)> {
  let Predicates = [HasMVEFloat];
}

class MVE_VCMPqqi<string suffix, bits<2> size>
    : MVE_VCMPqq<suffix, 0b1, size, pred_basic_i, size> {
  let Inst{12} = 0b0;
  let Inst{0} = 0b0;
}

class MVE_VCMPqqu<string suffix, bits<2> size>
    : MVE_VCMPqq<suffix, 0b1, size, pred_basic_u, size> {
  let Inst{12} = 0b0;
  let Inst{0} = 0b1;
}

class MVE_VCMPqqs<string suffix, bits<2> size>
    : MVE_VCMPqq<suffix, 0b1, size, pred_basic_s, size> {
  let Inst{12} = 0b1;
}

def MVE_VCMPf32 : MVE_VCMPqqf<"f32", 0b0>;
def MVE_VCMPf16 : MVE_VCMPqqf<"f16", 0b1>;

def MVE_VCMPi8  : MVE_VCMPqqi<"i8",  0b00>;
def MVE_VCMPi16 : MVE_VCMPqqi<"i16", 0b01>;
def MVE_VCMPi32 : MVE_VCMPqqi<"i32", 0b10>;

def MVE_VCMPu8  : MVE_VCMPqqu<"u8",  0b00>;
def MVE_VCMPu16 : MVE_VCMPqqu<"u16", 0b01>;
def MVE_VCMPu32 : MVE_VCMPqqu<"u32", 0b10>;

def MVE_VCMPs8  : MVE_VCMPqqs<"s8",  0b00>;
def MVE_VCMPs16 : MVE_VCMPqqs<"s16", 0b01>;
def MVE_VCMPs32 : MVE_VCMPqqs<"s32", 0b10>;

class MVE_VCMPqr<string suffix, bit bit_28, bits<2> bits_21_20,
                 VCMPPredicateOperand predtype, bits<2> vecsize, list<dag> pattern=[]>
  : MVE_p<(outs VCCR:$P0), (ins MQPR:$Qn, GPRwithZR:$Rm, predtype:$fc),
           NoItinerary, "vcmp", suffix, "$fc, $Qn, $Rm", vpred_n, "", vecsize, pattern> {
  // Base class for comparing a vector register with a scalar
  bits<3> fc;
  bits<4> Qn;
  bits<4> Rm;

  let Inst{28} = bit_28;
  let Inst{25-22} = 0b1000;
  let Inst{21-20} = bits_21_20;
  let Inst{19-17} = Qn{2-0};
  let Inst{16-13} = 0b1000;
  let Inst{12} = fc{2};
  let Inst{11-8} = 0b1111;
  let Inst{7} = fc{0};
  let Inst{6} = 0b1;
  let Inst{5} = fc{1};
  let Inst{4} = 0b0;
  let Inst{3-0} = Rm{3-0};

  let Constraints = "";
  // Custom decoder method, for the same reason as MVE_VCMPqq
  let DecoderMethod = "DecodeMVEVCMP<true," # predtype.DecoderMethod # ">";
  let validForTailPredication = 1;
}

class MVE_VCMPqrf<string suffix, bit size>
    : MVE_VCMPqr<suffix, size, 0b11, pred_basic_fp, !if(size, 0b01, 0b10)> {
  let Predicates = [HasMVEFloat];
}

class MVE_VCMPqri<string suffix, bits<2> size>
    : MVE_VCMPqr<suffix, 0b1, size, pred_basic_i, size> {
  let Inst{12} = 0b0;
  let Inst{5} = 0b0;
}

class MVE_VCMPqru<string suffix, bits<2> size>
    : MVE_VCMPqr<suffix, 0b1, size, pred_basic_u, size> {
  let Inst{12} = 0b0;
  let Inst{5} = 0b1;
}

class MVE_VCMPqrs<string suffix, bits<2> size>
    : MVE_VCMPqr<suffix, 0b1, size, pred_basic_s, size> {
  let Inst{12} = 0b1;
}

def MVE_VCMPf32r : MVE_VCMPqrf<"f32", 0b0>;
def MVE_VCMPf16r : MVE_VCMPqrf<"f16", 0b1>;

def MVE_VCMPi8r  : MVE_VCMPqri<"i8",  0b00>;
def MVE_VCMPi16r : MVE_VCMPqri<"i16", 0b01>;
def MVE_VCMPi32r : MVE_VCMPqri<"i32", 0b10>;

def MVE_VCMPu8r  : MVE_VCMPqru<"u8",  0b00>;
def MVE_VCMPu16r : MVE_VCMPqru<"u16", 0b01>;
def MVE_VCMPu32r : MVE_VCMPqru<"u32", 0b10>;

def MVE_VCMPs8r  : MVE_VCMPqrs<"s8",  0b00>;
def MVE_VCMPs16r : MVE_VCMPqrs<"s16", 0b01>;
def MVE_VCMPs32r : MVE_VCMPqrs<"s32", 0b10>;

multiclass unpred_vcmp_z<string suffix, PatLeaf fc> {
  def i8  : Pat<(v16i1 (ARMvcmpz (v16i8 MQPR:$v1), fc)),
                (v16i1 (!cast<Instruction>("MVE_VCMP"#suffix#"8r") (v16i8 MQPR:$v1), ZR, fc))>;
  def i16 : Pat<(v8i1 (ARMvcmpz (v8i16 MQPR:$v1), fc)),
                (v8i1 (!cast<Instruction>("MVE_VCMP"#suffix#"16r") (v8i16 MQPR:$v1), ZR, fc))>;
  def i32 : Pat<(v4i1 (ARMvcmpz (v4i32 MQPR:$v1), fc)),
                (v4i1 (!cast<Instruction>("MVE_VCMP"#suffix#"32r") (v4i32 MQPR:$v1), ZR, fc))>;

  def : Pat<(v16i1 (and (v16i1 VCCR:$p1), (v16i1 (ARMvcmpz (v16i8 MQPR:$v1), fc)))),
            (v16i1 (!cast<Instruction>("MVE_VCMP"#suffix#"8r") (v16i8 MQPR:$v1), ZR, fc, ARMVCCThen, VCCR:$p1, zero_reg))>;
  def : Pat<(v8i1 (and (v8i1 VCCR:$p1), (v8i1 (ARMvcmpz (v8i16 MQPR:$v1), fc)))),
            (v8i1 (!cast<Instruction>("MVE_VCMP"#suffix#"16r") (v8i16 MQPR:$v1), ZR, fc, ARMVCCThen, VCCR:$p1, zero_reg))>;
  def : Pat<(v4i1 (and (v4i1 VCCR:$p1), (v4i1 (ARMvcmpz (v4i32 MQPR:$v1), fc)))),
            (v4i1 (!cast<Instruction>("MVE_VCMP"#suffix#"32r") (v4i32 MQPR:$v1), ZR, fc, ARMVCCThen, VCCR:$p1, zero_reg))>;
}

multiclass unpred_vcmp_r<string suffix, PatLeaf fc> {
  def i8  : Pat<(v16i1 (ARMvcmp (v16i8 MQPR:$v1), (v16i8 MQPR:$v2), fc)),
                (v16i1 (!cast<Instruction>("MVE_VCMP"#suffix#"8") (v16i8 MQPR:$v1), (v16i8 MQPR:$v2), fc))>;
  def i16 : Pat<(v8i1 (ARMvcmp (v8i16 MQPR:$v1), (v8i16 MQPR:$v2), fc)),
                (v8i1 (!cast<Instruction>("MVE_VCMP"#suffix#"16") (v8i16 MQPR:$v1), (v8i16 MQPR:$v2), fc))>;
  def i32 : Pat<(v4i1 (ARMvcmp (v4i32 MQPR:$v1), (v4i32 MQPR:$v2), fc)),
                (v4i1 (!cast<Instruction>("MVE_VCMP"#suffix#"32") (v4i32 MQPR:$v1), (v4i32 MQPR:$v2), fc))>;

  def i8r  : Pat<(v16i1 (ARMvcmp (v16i8 MQPR:$v1), (v16i8 (ARMvdup rGPR:$v2)), fc)),
                 (v16i1 (!cast<Instruction>("MVE_VCMP"#suffix#"8r") (v16i8 MQPR:$v1), (i32 rGPR:$v2), fc))>;
  def i16r : Pat<(v8i1 (ARMvcmp (v8i16 MQPR:$v1), (v8i16 (ARMvdup rGPR:$v2)), fc)),
                 (v8i1 (!cast<Instruction>("MVE_VCMP"#suffix#"16r") (v8i16 MQPR:$v1), (i32 rGPR:$v2), fc))>;
  def i32r : Pat<(v4i1 (ARMvcmp (v4i32 MQPR:$v1), (v4i32 (ARMvdup rGPR:$v2)), fc)),
                 (v4i1 (!cast<Instruction>("MVE_VCMP"#suffix#"32r") (v4i32 MQPR:$v1), (i32 rGPR:$v2), fc))>;

  def : Pat<(v16i1 (and (v16i1 VCCR:$p1), (v16i1 (ARMvcmp (v16i8 MQPR:$v1), (v16i8 MQPR:$v2), fc)))),
            (v16i1 (!cast<Instruction>("MVE_VCMP"#suffix#"8") (v16i8 MQPR:$v1), (v16i8 MQPR:$v2), fc, ARMVCCThen, VCCR:$p1, zero_reg))>;
  def : Pat<(v8i1 (and (v8i1 VCCR:$p1), (v8i1 (ARMvcmp (v8i16 MQPR:$v1), (v8i16 MQPR:$v2), fc)))),
            (v8i1 (!cast<Instruction>("MVE_VCMP"#suffix#"16") (v8i16 MQPR:$v1), (v8i16 MQPR:$v2), fc, ARMVCCThen, VCCR:$p1, zero_reg))>;
  def : Pat<(v4i1 (and (v4i1 VCCR:$p1), (v4i1 (ARMvcmp (v4i32 MQPR:$v1), (v4i32 MQPR:$v2), fc)))),
            (v4i1 (!cast<Instruction>("MVE_VCMP"#suffix#"32") (v4i32 MQPR:$v1), (v4i32 MQPR:$v2), fc, ARMVCCThen, VCCR:$p1, zero_reg))>;

  def : Pat<(v16i1 (and (v16i1 VCCR:$p1), (v16i1 (ARMvcmp (v16i8 MQPR:$v1), (v16i8 (ARMvdup rGPR:$v2)), fc)))),
            (v16i1 (!cast<Instruction>("MVE_VCMP"#suffix#"8r") (v16i8 MQPR:$v1), (i32 rGPR:$v2), fc, ARMVCCThen, VCCR:$p1, zero_reg))>;
  def : Pat<(v8i1 (and (v8i1 VCCR:$p1), (v8i1 (ARMvcmp (v8i16 MQPR:$v1), (v8i16 (ARMvdup rGPR:$v2)), fc)))),
            (v8i1 (!cast<Instruction>("MVE_VCMP"#suffix#"16r") (v8i16 MQPR:$v1), (i32 rGPR:$v2), fc, ARMVCCThen, VCCR:$p1, zero_reg))>;
  def : Pat<(v4i1 (and (v4i1 VCCR:$p1), (v4i1 (ARMvcmp (v4i32 MQPR:$v1), (v4i32 (ARMvdup rGPR:$v2)), fc)))),
            (v4i1 (!cast<Instruction>("MVE_VCMP"#suffix#"32r") (v4i32 MQPR:$v1), (i32 rGPR:$v2), fc, ARMVCCThen, VCCR:$p1, zero_reg))>;
}

multiclass unpred_vcmpf_z<PatLeaf fc> {
  def f16 : Pat<(v8i1 (ARMvcmpz (v8f16 MQPR:$v1), fc)),
                (v8i1 (MVE_VCMPf16r (v8f16 MQPR:$v1), ZR, fc))>;
  def f32 : Pat<(v4i1 (ARMvcmpz (v4f32 MQPR:$v1), fc)),
                (v4i1 (MVE_VCMPf32r (v4f32 MQPR:$v1), ZR, fc))>;

  def : Pat<(v8i1 (and (v8i1 VCCR:$p1), (v8i1 (ARMvcmpz (v8f16 MQPR:$v1), fc)))),
            (v8i1 (MVE_VCMPf16r (v8f16 MQPR:$v1), ZR, fc, ARMVCCThen, VCCR:$p1, zero_reg))>;
  def : Pat<(v4i1 (and (v4i1 VCCR:$p1), (v4i1 (ARMvcmpz (v4f32 MQPR:$v1), fc)))),
            (v4i1 (MVE_VCMPf32r (v4f32 MQPR:$v1), ZR, fc, ARMVCCThen, VCCR:$p1, zero_reg))>;
}

multiclass unpred_vcmpf_r<PatLeaf fc> {
  def : Pat<(v8i1 (ARMvcmp (v8f16 MQPR:$v1), (v8f16 MQPR:$v2), fc)),
            (v8i1 (MVE_VCMPf16 (v8f16 MQPR:$v1), (v8f16 MQPR:$v2), fc))>;
  def : Pat<(v4i1 (ARMvcmp (v4f32 MQPR:$v1), (v4f32 MQPR:$v2), fc)),
            (v4i1 (MVE_VCMPf32 (v4f32 MQPR:$v1), (v4f32 MQPR:$v2), fc))>;

  def : Pat<(v8i1 (ARMvcmp (v8f16 MQPR:$v1), (v8f16 (ARMvdup rGPR:$v2)), fc)),
            (v8i1 (MVE_VCMPf16r (v8f16 MQPR:$v1), (i32 rGPR:$v2), fc))>;
  def : Pat<(v4i1 (ARMvcmp (v4f32 MQPR:$v1), (v4f32 (ARMvdup rGPR:$v2)), fc)),
            (v4i1 (MVE_VCMPf32r (v4f32 MQPR:$v1), (i32 rGPR:$v2), fc))>;

  def : Pat<(v8i1 (and (v8i1 VCCR:$p1), (v8i1 (ARMvcmp (v8f16 MQPR:$v1), (v8f16 MQPR:$v2), fc)))),
            (v8i1 (MVE_VCMPf16 (v8f16 MQPR:$v1), (v8f16 MQPR:$v2), fc, ARMVCCThen, VCCR:$p1, zero_reg))>;
  def : Pat<(v4i1 (and (v4i1 VCCR:$p1), (v4i1 (ARMvcmp (v4f32 MQPR:$v1), (v4f32 MQPR:$v2), fc)))),
            (v4i1 (MVE_VCMPf32 (v4f32 MQPR:$v1), (v4f32 MQPR:$v2), fc, ARMVCCThen, VCCR:$p1, zero_reg))>;

  def : Pat<(v8i1 (and (v8i1 VCCR:$p1), (v8i1 (ARMvcmp (v8f16 MQPR:$v1), (v8f16 (ARMvdup rGPR:$v2)), fc)))),
            (v8i1 (MVE_VCMPf16r (v8f16 MQPR:$v1), (i32 rGPR:$v2), fc, ARMVCCThen, VCCR:$p1, zero_reg))>;
  def : Pat<(v4i1 (and (v4i1 VCCR:$p1), (v4i1 (ARMvcmp (v4f32 MQPR:$v1), (v4f32 (ARMvdup rGPR:$v2)), fc)))),
            (v4i1 (MVE_VCMPf32r (v4f32 MQPR:$v1), (i32 rGPR:$v2), fc, ARMVCCThen, VCCR:$p1, zero_reg))>;
}

let Predicates = [HasMVEInt] in {
  defm MVE_VCEQZ  : unpred_vcmp_z<"i", ARMCCeq>;
  defm MVE_VCNEZ  : unpred_vcmp_z<"i", ARMCCne>;
  defm MVE_VCGEZ  : unpred_vcmp_z<"s", ARMCCge>;
  defm MVE_VCLTZ  : unpred_vcmp_z<"s", ARMCClt>;
  defm MVE_VCGTZ  : unpred_vcmp_z<"s", ARMCCgt>;
  defm MVE_VCLEZ  : unpred_vcmp_z<"s", ARMCCle>;
  defm MVE_VCGTUZ : unpred_vcmp_z<"u", ARMCChi>;
  defm MVE_VCGEUZ : unpred_vcmp_z<"u", ARMCChs>;

  defm MVE_VCEQ   : unpred_vcmp_r<"i", ARMCCeq>;
  defm MVE_VCNE   : unpred_vcmp_r<"i", ARMCCne>;
  defm MVE_VCGE   : unpred_vcmp_r<"s", ARMCCge>;
  defm MVE_VCLT   : unpred_vcmp_r<"s", ARMCClt>;
  defm MVE_VCGT   : unpred_vcmp_r<"s", ARMCCgt>;
  defm MVE_VCLE   : unpred_vcmp_r<"s", ARMCCle>;
  defm MVE_VCGTU  : unpred_vcmp_r<"u", ARMCChi>;
  defm MVE_VCGEU  : unpred_vcmp_r<"u", ARMCChs>;
}

let Predicates = [HasMVEFloat] in {
  defm MVE_VFCEQZ  : unpred_vcmpf_z<ARMCCeq>;
  defm MVE_VFCNEZ  : unpred_vcmpf_z<ARMCCne>;
  defm MVE_VFCGEZ  : unpred_vcmpf_z<ARMCCge>;
  defm MVE_VFCLTZ  : unpred_vcmpf_z<ARMCClt>;
  defm MVE_VFCGTZ  : unpred_vcmpf_z<ARMCCgt>;
  defm MVE_VFCLEZ  : unpred_vcmpf_z<ARMCCle>;

  defm MVE_VFCEQ   : unpred_vcmpf_r<ARMCCeq>;
  defm MVE_VFCNE   : unpred_vcmpf_r<ARMCCne>;
  defm MVE_VFCGE   : unpred_vcmpf_r<ARMCCge>;
  defm MVE_VFCLT   : unpred_vcmpf_r<ARMCClt>;
  defm MVE_VFCGT   : unpred_vcmpf_r<ARMCCgt>;
  defm MVE_VFCLE   : unpred_vcmpf_r<ARMCCle>;
}


// Extra "worst case" and/or/xor patterns, going into and out of GRP
multiclass two_predops<SDPatternOperator opnode, Instruction insn> {
  def v16i1 : Pat<(v16i1 (opnode (v16i1 VCCR:$p1), (v16i1 VCCR:$p2))),
                  (v16i1 (COPY_TO_REGCLASS
                           (insn (i32 (COPY_TO_REGCLASS (v16i1 VCCR:$p1), rGPR)),
                                 (i32 (COPY_TO_REGCLASS (v16i1 VCCR:$p2), rGPR))),
                           VCCR))>;
  def v8i1  : Pat<(v8i1 (opnode (v8i1 VCCR:$p1), (v8i1 VCCR:$p2))),
                  (v8i1 (COPY_TO_REGCLASS
                          (insn (i32 (COPY_TO_REGCLASS (v8i1 VCCR:$p1), rGPR)),
                                (i32 (COPY_TO_REGCLASS (v8i1 VCCR:$p2), rGPR))),
                          VCCR))>;
  def v4i1  : Pat<(v4i1 (opnode (v4i1 VCCR:$p1), (v4i1 VCCR:$p2))),
                  (v4i1 (COPY_TO_REGCLASS
                          (insn (i32 (COPY_TO_REGCLASS (v4i1 VCCR:$p1), rGPR)),
                                (i32 (COPY_TO_REGCLASS (v4i1 VCCR:$p2), rGPR))),
                          VCCR))>;
  def v2i1  : Pat<(v2i1 (opnode (v2i1 VCCR:$p1), (v2i1 VCCR:$p2))),
                  (v2i1 (COPY_TO_REGCLASS
                          (insn (i32 (COPY_TO_REGCLASS (v2i1 VCCR:$p1), rGPR)),
                                (i32 (COPY_TO_REGCLASS (v2i1 VCCR:$p2), rGPR))),
                          VCCR))>;
}

let Predicates = [HasMVEInt] in {
  defm POR    : two_predops<or,  t2ORRrr>;
  defm PAND   : two_predops<and, t2ANDrr>;
  defm PEOR   : two_predops<xor, t2EORrr>;
}

// Occasionally we need to cast between a i32 and a boolean vector, for
// example when moving between rGPR and VPR.P0 as part of predicate vector
// shuffles. We also sometimes need to cast between different predicate
// vector types (v4i1<>v8i1, etc.) also as part of lowering vector shuffles.
def predicate_cast : SDNode<"ARMISD::PREDICATE_CAST", SDTUnaryOp>;

def load_align4 : PatFrag<(ops node:$ptr), (load node:$ptr), [{
  return cast<LoadSDNode>(N)->getAlign() >= 4;
}]>;

let Predicates = [HasMVEInt] in {
  foreach VT = [ v2i1, v4i1, v8i1, v16i1 ] in {
    def : Pat<(i32 (predicate_cast (VT VCCR:$src))),
              (i32 (COPY_TO_REGCLASS (VT VCCR:$src), VCCR))>;
    def : Pat<(VT  (predicate_cast (i32 VCCR:$src))),
              (VT  (COPY_TO_REGCLASS (i32 VCCR:$src), VCCR))>;

    foreach VT2 = [ v2i1, v4i1, v8i1, v16i1 ] in
      def : Pat<(VT  (predicate_cast (VT2 VCCR:$src))),
                (VT  (COPY_TO_REGCLASS (VT2 VCCR:$src), VCCR))>;
  }

  // If we happen to be casting from a load we can convert that straight
  // into a predicate load, so long as the load is of the correct type.
  foreach VT = [ v2i1, v4i1, v8i1, v16i1 ] in {
    def : Pat<(VT (predicate_cast (i32 (load_align4 taddrmode_imm7<2>:$addr)))),
              (VT (VLDR_P0_off taddrmode_imm7<2>:$addr))>;
  }

  // Here we match the specific SDNode type 'ARMVectorRegCastImpl'
  // rather than the more general 'ARMVectorRegCast' which would also
  // match some bitconverts. If we use the latter in cases where the
  // input and output types are the same, the bitconvert gets elided
  // and we end up generating a nonsense match of nothing.

  foreach VT = [ v16i8, v8i16, v8f16, v4i32, v4f32, v2i64, v2f64 ] in
    foreach VT2 = [ v16i8, v8i16, v8f16, v4i32, v4f32, v2i64, v2f64 ] in
      def : Pat<(VT (ARMVectorRegCastImpl (VT2 MQPR:$src))),
                (VT MQPR:$src)>;
}

// end of MVE compares

// start of MVE_qDest_qSrc

class MVE_qDest_qSrc<string iname, string suffix, dag oops, dag iops,
                     string ops, vpred_ops vpred, string cstr,
                     bits<2> vecsize, list<dag> pattern=[]>
  : MVE_p<oops, iops, NoItinerary, iname, suffix,
          ops, vpred, cstr, vecsize, pattern> {
  bits<4> Qd;
  bits<4> Qm;

  let Inst{25-23} = 0b100;
  let Inst{22} = Qd{3};
  let Inst{15-13} = Qd{2-0};
  let Inst{11-9} = 0b111;
  let Inst{6} = 0b0;
  let Inst{5} = Qm{3};
  let Inst{4} = 0b0;
  let Inst{3-1} = Qm{2-0};
}

class MVE_VQxDMLxDH<string iname, bit exch, bit round, bit subtract,
                    string suffix, bits<2> size, string cstr="",
                    list<dag> pattern=[]>
  : MVE_qDest_qSrc<iname, suffix, (outs MQPR:$Qd),
                   (ins MQPR:$Qd_src, MQPR:$Qn, MQPR:$Qm), "$Qd, $Qn, $Qm",
                   vpred_n, "$Qd = $Qd_src"#cstr, size, pattern> {
  bits<4> Qn;

  let Inst{28} = subtract;
  let Inst{21-20} = size;
  let Inst{19-17} = Qn{2-0};
  let Inst{16} = 0b0;
  let Inst{12} = exch;
  let Inst{8} = 0b0;
  let Inst{7} = Qn{3};
  let Inst{0} = round;
}

multiclass MVE_VQxDMLxDH_p<string iname, bit exch, bit round, bit subtract,
                           MVEVectorVTInfo VTI> {
  def "": MVE_VQxDMLxDH<iname, exch, round, subtract, VTI.Suffix, VTI.Size,
                        !if(!eq(VTI.LaneBits, 32), ",@earlyclobber $Qd", "")>;
  defvar Inst = !cast<Instruction>(NAME);
  defvar ConstParams = (? (i32 exch), (i32 round), (i32 subtract));
  defvar unpred_intr = int_arm_mve_vqdmlad;
  defvar pred_intr = int_arm_mve_vqdmlad_predicated;

  def : Pat<(VTI.Vec !con((unpred_intr (VTI.Vec MQPR:$a), (VTI.Vec MQPR:$b),
                                       (VTI.Vec MQPR:$c)), ConstParams)),
            (VTI.Vec (Inst (VTI.Vec MQPR:$a), (VTI.Vec MQPR:$b),
                           (VTI.Vec MQPR:$c)))>;
  def : Pat<(VTI.Vec !con((pred_intr (VTI.Vec MQPR:$a), (VTI.Vec MQPR:$b),
                                     (VTI.Vec MQPR:$c)), ConstParams,
                          (? (VTI.Pred VCCR:$pred)))),
            (VTI.Vec (Inst (VTI.Vec MQPR:$a), (VTI.Vec MQPR:$b),
                           (VTI.Vec MQPR:$c),
                           ARMVCCThen, (VTI.Pred VCCR:$pred), zero_reg))>;
}

multiclass MVE_VQxDMLxDH_multi<string iname, bit exch,
                               bit round, bit subtract> {
  defm s8  : MVE_VQxDMLxDH_p<iname, exch, round, subtract, MVE_v16s8>;
  defm s16 : MVE_VQxDMLxDH_p<iname, exch, round, subtract, MVE_v8s16>;
  defm s32 : MVE_VQxDMLxDH_p<iname, exch, round, subtract, MVE_v4s32>;
}

defm MVE_VQDMLADH   : MVE_VQxDMLxDH_multi<"vqdmladh",   0b0, 0b0, 0b0>;
defm MVE_VQDMLADHX  : MVE_VQxDMLxDH_multi<"vqdmladhx",  0b1, 0b0, 0b0>;
defm MVE_VQRDMLADH  : MVE_VQxDMLxDH_multi<"vqrdmladh",  0b0, 0b1, 0b0>;
defm MVE_VQRDMLADHX : MVE_VQxDMLxDH_multi<"vqrdmladhx", 0b1, 0b1, 0b0>;
defm MVE_VQDMLSDH   : MVE_VQxDMLxDH_multi<"vqdmlsdh",   0b0, 0b0, 0b1>;
defm MVE_VQDMLSDHX  : MVE_VQxDMLxDH_multi<"vqdmlsdhx",  0b1, 0b0, 0b1>;
defm MVE_VQRDMLSDH  : MVE_VQxDMLxDH_multi<"vqrdmlsdh",  0b0, 0b1, 0b1>;
defm MVE_VQRDMLSDHX : MVE_VQxDMLxDH_multi<"vqrdmlsdhx", 0b1, 0b1, 0b1>;

class MVE_VCMUL<string iname, string suffix, bits<2> size, string cstr="">
  : MVE_qDest_qSrc<iname, suffix, (outs MQPR:$Qd),
                   (ins MQPR:$Qn, MQPR:$Qm, complexrotateop:$rot),
                   "$Qd, $Qn, $Qm, $rot", vpred_r, cstr, size,
                   []> {
  bits<4> Qn;
  bits<2> rot;

  let Inst{28} = size{1};
  let Inst{21-20} = 0b11;
  let Inst{19-17} = Qn{2-0};
  let Inst{16} = 0b0;
  let Inst{12} = rot{1};
  let Inst{8} = 0b0;
  let Inst{7} = Qn{3};
  let Inst{0} = rot{0};

  let Predicates = [HasMVEFloat];
}

multiclass MVE_VCMUL_m<string iname, MVEVectorVTInfo VTI,
                       string cstr=""> {
  def "" : MVE_VCMUL<iname, VTI.Suffix, VTI.Size, cstr>;
  defvar Inst = !cast<Instruction>(NAME);

  let Predicates = [HasMVEFloat] in {
    def : Pat<(VTI.Vec (int_arm_mve_vcmulq
                            imm:$rot, (VTI.Vec MQPR:$Qn), (VTI.Vec MQPR:$Qm))),
              (VTI.Vec (Inst (VTI.Vec MQPR:$Qn), (VTI.Vec MQPR:$Qm),
                             imm:$rot))>;

    def : Pat<(VTI.Vec (int_arm_mve_vcmulq_predicated
                            imm:$rot, (VTI.Vec MQPR:$inactive),
                            (VTI.Vec MQPR:$Qn), (VTI.Vec MQPR:$Qm),
                            (VTI.Pred VCCR:$mask))),
              (VTI.Vec (Inst (VTI.Vec MQPR:$Qn), (VTI.Vec MQPR:$Qm),
                             imm:$rot, ARMVCCThen, (VTI.Pred VCCR:$mask), zero_reg,
                             (VTI.Vec MQPR:$inactive)))>;

  }
}

defm MVE_VCMULf16 : MVE_VCMUL_m<"vcmul", MVE_v8f16>;
defm MVE_VCMULf32 : MVE_VCMUL_m<"vcmul", MVE_v4f32, "@earlyclobber $Qd">;

class MVE_VMULL<string iname, string suffix, bit bit_28, bits<2> bits_21_20,
                bit T, string cstr, bits<2> vecsize, list<dag> pattern=[]>
  : MVE_qDest_qSrc<iname, suffix, (outs MQPR:$Qd),
                   (ins MQPR:$Qn, MQPR:$Qm), "$Qd, $Qn, $Qm",
                   vpred_r, cstr, vecsize, pattern> {
  bits<4> Qd;
  bits<4> Qn;
  bits<4> Qm;

  let Inst{28} = bit_28;
  let Inst{21-20} = bits_21_20;
  let Inst{19-17} = Qn{2-0};
  let Inst{16} = 0b1;
  let Inst{12} = T;
  let Inst{8} = 0b0;
  let Inst{7} = Qn{3};
  let Inst{0} = 0b0;
  let validForTailPredication = 1;
  let doubleWidthResult = 1;
}

multiclass MVE_VMULL_m<MVEVectorVTInfo VTI,
                       SDPatternOperator unpred_op, Intrinsic pred_int,
                       bit Top, bits<2> vecsize, string cstr=""> {
  def "" : MVE_VMULL<"vmull" # !if(Top, "t", "b"), VTI.Suffix, VTI.Unsigned,
                     VTI.Size, Top, cstr, vecsize>;
  defvar Inst = !cast<Instruction>(NAME);

  let Predicates = [HasMVEInt] in {
    defvar uflag = !if(!eq(VTI.SuffixLetter, "p"), (?), (? (i32 VTI.Unsigned)));

    // Unpredicated multiply
    def : Pat<(VTI.DblVec !con((unpred_op (VTI.Vec MQPR:$Qm),
                                          (VTI.Vec MQPR:$Qn)),
                               uflag, (? (i32 Top)))),
              (VTI.DblVec (Inst (VTI.Vec MQPR:$Qm), (VTI.Vec MQPR:$Qn)))>;

    // Predicated multiply
    def : Pat<(VTI.DblVec !con((pred_int (VTI.Vec MQPR:$Qm),
                                         (VTI.Vec MQPR:$Qn)),
                               uflag, (? (i32 Top), (VTI.DblPred VCCR:$mask),
                                         (VTI.DblVec MQPR:$inactive)))),
              (VTI.DblVec (Inst (VTI.Vec MQPR:$Qm), (VTI.Vec MQPR:$Qn),
                                ARMVCCThen, (VTI.DblPred VCCR:$mask), zero_reg,
                                (VTI.DblVec MQPR:$inactive)))>;
  }
}

// For polynomial multiplies, the size bits take the unused value 0b11, and
// the unsigned bit switches to encoding the size.

defm MVE_VMULLBs8  : MVE_VMULL_m<MVE_v16s8, int_arm_mve_vmull,
                                 int_arm_mve_mull_int_predicated, 0b0, 0b01>;
defm MVE_VMULLTs8  : MVE_VMULL_m<MVE_v16s8, int_arm_mve_vmull,
                                 int_arm_mve_mull_int_predicated, 0b1, 0b01>;
defm MVE_VMULLBs16 : MVE_VMULL_m<MVE_v8s16, int_arm_mve_vmull,
                                 int_arm_mve_mull_int_predicated, 0b0, 0b10>;
defm MVE_VMULLTs16 : MVE_VMULL_m<MVE_v8s16, int_arm_mve_vmull,
                                 int_arm_mve_mull_int_predicated, 0b1, 0b10>;
defm MVE_VMULLBs32 : MVE_VMULL_m<MVE_v4s32, int_arm_mve_vmull,
                                 int_arm_mve_mull_int_predicated, 0b0, 0b11,
                                 "@earlyclobber $Qd">;
defm MVE_VMULLTs32 : MVE_VMULL_m<MVE_v4s32, int_arm_mve_vmull,
                                 int_arm_mve_mull_int_predicated, 0b1, 0b11,
                                 "@earlyclobber $Qd">;

defm MVE_VMULLBu8  : MVE_VMULL_m<MVE_v16u8, int_arm_mve_vmull,
                                 int_arm_mve_mull_int_predicated, 0b0, 0b01>;
defm MVE_VMULLTu8  : MVE_VMULL_m<MVE_v16u8, int_arm_mve_vmull,
                                 int_arm_mve_mull_int_predicated, 0b1, 0b01>;
defm MVE_VMULLBu16 : MVE_VMULL_m<MVE_v8u16, int_arm_mve_vmull,
                                 int_arm_mve_mull_int_predicated, 0b0, 0b10>;
defm MVE_VMULLTu16 : MVE_VMULL_m<MVE_v8u16, int_arm_mve_vmull,
                                 int_arm_mve_mull_int_predicated, 0b1, 0b10>;
defm MVE_VMULLBu32 : MVE_VMULL_m<MVE_v4u32, int_arm_mve_vmull,
                                 int_arm_mve_mull_int_predicated, 0b0, 0b11,
                                 "@earlyclobber $Qd">;
defm MVE_VMULLTu32 : MVE_VMULL_m<MVE_v4u32, int_arm_mve_vmull,
                                 int_arm_mve_mull_int_predicated, 0b1, 0b11,
                                 "@earlyclobber $Qd">;

defm MVE_VMULLBp8  : MVE_VMULL_m<MVE_v16p8, int_arm_mve_vmull_poly,
                                 int_arm_mve_mull_poly_predicated, 0b0, 0b01>;
defm MVE_VMULLTp8  : MVE_VMULL_m<MVE_v16p8, int_arm_mve_vmull_poly,
                                 int_arm_mve_mull_poly_predicated, 0b1, 0b01>;
defm MVE_VMULLBp16 : MVE_VMULL_m<MVE_v8p16, int_arm_mve_vmull_poly,
                                 int_arm_mve_mull_poly_predicated, 0b0, 0b10>;
defm MVE_VMULLTp16 : MVE_VMULL_m<MVE_v8p16, int_arm_mve_vmull_poly,
                                 int_arm_mve_mull_poly_predicated, 0b1, 0b10>;

let Predicates = [HasMVEInt] in {
  def : Pat<(v2i64 (ARMvmulls (v4i32 MQPR:$src1), (v4i32 MQPR:$src2))),
            (MVE_VMULLBs32 MQPR:$src1, MQPR:$src2)>;
  def : Pat<(v2i64 (ARMvmulls (v4i32 (ARMvrev64 (v4i32 MQPR:$src1))),
                              (v4i32 (ARMvrev64 (v4i32 MQPR:$src2))))),
            (MVE_VMULLTs32 MQPR:$src1, MQPR:$src2)>;

  def : Pat<(mul (sext_inreg (v4i32 MQPR:$src1), v4i16),
                 (sext_inreg (v4i32 MQPR:$src2), v4i16)),
            (MVE_VMULLBs16 MQPR:$src1, MQPR:$src2)>;
  def : Pat<(mul (sext_inreg (v4i32 (ARMVectorRegCast (ARMvrev32 (v8i16 MQPR:$src1)))), v4i16),
                 (sext_inreg (v4i32 (ARMVectorRegCast (ARMvrev32 (v8i16 MQPR:$src2)))), v4i16)),
            (MVE_VMULLTs16 MQPR:$src1, MQPR:$src2)>;

  def : Pat<(mul (sext_inreg (v8i16 MQPR:$src1), v8i8),
                 (sext_inreg (v8i16 MQPR:$src2), v8i8)),
            (MVE_VMULLBs8 MQPR:$src1, MQPR:$src2)>;
  def : Pat<(mul (sext_inreg (v8i16 (ARMVectorRegCast (ARMvrev16 (v16i8 MQPR:$src1)))), v8i8),
                 (sext_inreg (v8i16 (ARMVectorRegCast (ARMvrev16 (v16i8 MQPR:$src2)))), v8i8)),
            (MVE_VMULLTs8 MQPR:$src1, MQPR:$src2)>;

  def : Pat<(v2i64 (ARMvmullu (v4i32 MQPR:$src1), (v4i32 MQPR:$src2))),
            (MVE_VMULLBu32 MQPR:$src1, MQPR:$src2)>;
  def : Pat<(v2i64 (ARMvmullu (v4i32 (ARMvrev64 (v4i32 MQPR:$src1))),
                              (v4i32 (ARMvrev64 (v4i32 MQPR:$src2))))),
            (MVE_VMULLTu32 MQPR:$src1, MQPR:$src2)>;

  def : Pat<(mul (and (v4i32 MQPR:$src1), (v4i32 (ARMvmovImm (i32 0xCFF)))),
                 (and (v4i32 MQPR:$src2), (v4i32 (ARMvmovImm (i32 0xCFF))))),
            (MVE_VMULLBu16 MQPR:$src1, MQPR:$src2)>;
  def : Pat<(mul (and (v4i32 (ARMVectorRegCast (ARMvrev32 (v8i16 MQPR:$src1)))),
                      (v4i32 (ARMvmovImm (i32 0xCFF)))),
                 (and (v4i32 (ARMVectorRegCast (ARMvrev32 (v8i16 MQPR:$src2)))),
                      (v4i32 (ARMvmovImm (i32 0xCFF))))),
            (MVE_VMULLTu16 MQPR:$src1, MQPR:$src2)>;

  def : Pat<(mul (ARMvbicImm (v8i16 MQPR:$src1), (i32 0xAFF)),
                 (ARMvbicImm (v8i16 MQPR:$src2), (i32 0xAFF))),
            (MVE_VMULLBu8 MQPR:$src1, MQPR:$src2)>;
  def : Pat<(mul (ARMvbicImm (v8i16 (ARMVectorRegCast (ARMvrev16 (v16i8 MQPR:$src1)))), (i32 0xAFF)),
                 (ARMvbicImm (v8i16 (ARMVectorRegCast (ARMvrev16 (v16i8 MQPR:$src2)))), (i32 0xAFF))),
            (MVE_VMULLTu8 MQPR:$src1, MQPR:$src2)>;
}

class MVE_VxMULH<string iname, string suffix, bit U, bits<2> size, bit round,
                 list<dag> pattern=[]>
  : MVE_qDest_qSrc<iname, suffix, (outs MQPR:$Qd),
                   (ins MQPR:$Qn, MQPR:$Qm), "$Qd, $Qn, $Qm",
                   vpred_r, "", size, pattern> {
  bits<4> Qn;

  let Inst{28} = U;
  let Inst{21-20} = size;
  let Inst{19-17} = Qn{2-0};
  let Inst{16} = 0b1;
  let Inst{12} = round;
  let Inst{8} = 0b0;
  let Inst{7} = Qn{3};
  let Inst{0} = 0b1;
  let validForTailPredication = 1;
}

multiclass MVE_VxMULH_m<string iname, MVEVectorVTInfo VTI, SDNode unpred_op,
                        Intrinsic PredInt, bit round> {
  def "" : MVE_VxMULH<iname, VTI.Suffix, VTI.Unsigned, VTI.Size, round>;
  defvar Inst = !cast<Instruction>(NAME);

  let Predicates = [HasMVEInt] in {
    if !eq(round, 0b0) then {
      defvar mulh = !if(VTI.Unsigned, mulhu, mulhs);
      defm : MVE_TwoOpPattern<VTI, mulh, PredInt, (? (i32 VTI.Unsigned)),
                              !cast<Instruction>(NAME)>;
    } else {
      // Predicated multiply returning high bits
      def : Pat<(VTI.Vec (PredInt (VTI.Vec MQPR:$Qm), (VTI.Vec MQPR:$Qn),
                              (i32 VTI.Unsigned), (VTI.Pred VCCR:$mask),
                              (VTI.Vec MQPR:$inactive))),
                (VTI.Vec (Inst (VTI.Vec MQPR:$Qm), (VTI.Vec MQPR:$Qn),
                              ARMVCCThen, (VTI.Pred VCCR:$mask), zero_reg,
                              (VTI.Vec MQPR:$inactive)))>;
    }

    // Unpredicated intrinsic
    def : Pat<(VTI.Vec (unpred_op (VTI.Vec MQPR:$Qm), (VTI.Vec MQPR:$Qn),
                            (i32 VTI.Unsigned))),
              (VTI.Vec (Inst (VTI.Vec MQPR:$Qm), (VTI.Vec MQPR:$Qn)))>;
  }
}

multiclass MVE_VMULT<string iname, MVEVectorVTInfo VTI, bit round>
  : MVE_VxMULH_m<iname, VTI, !if(round, int_arm_mve_vrmulh, int_arm_mve_vmulh),
                 !if(round, int_arm_mve_rmulh_predicated,
                            int_arm_mve_mulh_predicated),
                 round>;

defm MVE_VMULHs8   : MVE_VMULT<"vmulh",  MVE_v16s8, 0b0>;
defm MVE_VMULHs16  : MVE_VMULT<"vmulh",  MVE_v8s16, 0b0>;
defm MVE_VMULHs32  : MVE_VMULT<"vmulh",  MVE_v4s32, 0b0>;
defm MVE_VMULHu8   : MVE_VMULT<"vmulh",  MVE_v16u8, 0b0>;
defm MVE_VMULHu16  : MVE_VMULT<"vmulh",  MVE_v8u16, 0b0>;
defm MVE_VMULHu32  : MVE_VMULT<"vmulh",  MVE_v4u32, 0b0>;

defm MVE_VRMULHs8  : MVE_VMULT<"vrmulh", MVE_v16s8, 0b1>;
defm MVE_VRMULHs16 : MVE_VMULT<"vrmulh", MVE_v8s16, 0b1>;
defm MVE_VRMULHs32 : MVE_VMULT<"vrmulh", MVE_v4s32, 0b1>;
defm MVE_VRMULHu8  : MVE_VMULT<"vrmulh", MVE_v16u8, 0b1>;
defm MVE_VRMULHu16 : MVE_VMULT<"vrmulh", MVE_v8u16, 0b1>;
defm MVE_VRMULHu32 : MVE_VMULT<"vrmulh", MVE_v4u32, 0b1>;

class MVE_VxMOVxN<string iname, string suffix, bit bit_28, bit bit_17,
                  bits<2> size, bit T, list<dag> pattern=[]>
  : MVE_qDest_qSrc<iname, suffix, (outs MQPR:$Qd),
                   (ins MQPR:$Qd_src, MQPR:$Qm), "$Qd, $Qm",
                   vpred_n, "$Qd = $Qd_src", !if(size, 0b10, 0b01), pattern> {

  let Inst{28} = bit_28;
  let Inst{21-20} = 0b11;
  let Inst{19-18} = size;
  let Inst{17} = bit_17;
  let Inst{16} = 0b1;
  let Inst{12} = T;
  let Inst{8} = 0b0;
  let Inst{7} = !not(bit_17);
  let Inst{0} = 0b1;
  let validForTailPredication = 1;
  let retainsPreviousHalfElement = 1;
}

multiclass MVE_VxMOVxN_halves<string iname, string suffix,
                              bit bit_28, bit bit_17, bits<2> size> {
  def bh : MVE_VxMOVxN<iname # "b", suffix, bit_28, bit_17, size, 0b0>;
  def th : MVE_VxMOVxN<iname # "t", suffix, bit_28, bit_17, size, 0b1>;
}

defm MVE_VMOVNi16   : MVE_VxMOVxN_halves<"vmovn",   "i16", 0b1, 0b0, 0b00>;
defm MVE_VMOVNi32   : MVE_VxMOVxN_halves<"vmovn",   "i32", 0b1, 0b0, 0b01>;
defm MVE_VQMOVNs16  : MVE_VxMOVxN_halves<"vqmovn",  "s16", 0b0, 0b1, 0b00>;
defm MVE_VQMOVNs32  : MVE_VxMOVxN_halves<"vqmovn",  "s32", 0b0, 0b1, 0b01>;
defm MVE_VQMOVNu16  : MVE_VxMOVxN_halves<"vqmovn",  "u16", 0b1, 0b1, 0b00>;
defm MVE_VQMOVNu32  : MVE_VxMOVxN_halves<"vqmovn",  "u32", 0b1, 0b1, 0b01>;
defm MVE_VQMOVUNs16 : MVE_VxMOVxN_halves<"vqmovun", "s16", 0b0, 0b0, 0b00>;
defm MVE_VQMOVUNs32 : MVE_VxMOVxN_halves<"vqmovun", "s32", 0b0, 0b0, 0b01>;

def MVEvmovn       : SDNode<"ARMISD::VMOVN", SDTARMVEXT>;

multiclass MVE_VMOVN_p<Instruction Inst, bit top,
                       MVEVectorVTInfo VTI, MVEVectorVTInfo InVTI> {
  // Match the most obvious MVEvmovn(a,b,t), which overwrites the odd or even
  // lanes of a (depending on t) with the even lanes of b.
  def : Pat<(VTI.Vec (MVEvmovn (VTI.Vec MQPR:$Qd_src),
                               (VTI.Vec MQPR:$Qm), (i32 top))),
            (VTI.Vec (Inst (VTI.Vec MQPR:$Qd_src), (VTI.Vec MQPR:$Qm)))>;

  if !not(top) then {
    // If we see MVEvmovn(a,ARMvrev(b),1), that wants to overwrite the odd
    // lanes of a with the odd lanes of b. In other words, the lanes we're
    // _keeping_ from a are the even ones. So we can flip it round and say that
    // this is the same as overwriting the even lanes of b with the even lanes
    // of a, i.e. it's a VMOVNB with the operands reversed.
    defvar vrev = !cast<SDNode>("ARMvrev" # InVTI.LaneBits);
    def : Pat<(VTI.Vec (MVEvmovn (VTI.Vec MQPR:$Qm),
                                 (VTI.Vec (vrev MQPR:$Qd_src)), (i32 1))),
              (VTI.Vec (Inst (VTI.Vec MQPR:$Qd_src), (VTI.Vec MQPR:$Qm)))>;
  }

  // Match the IR intrinsic for a predicated VMOVN. This regards the Qm input
  // as having wider lanes that we're narrowing, instead of already-narrow
  // lanes that we're taking every other one of.
  def : Pat<(VTI.Vec (int_arm_mve_vmovn_predicated (VTI.Vec MQPR:$Qd_src),
                                  (InVTI.Vec MQPR:$Qm), (i32 top),
                                  (InVTI.Pred VCCR:$pred))),
            (VTI.Vec (Inst (VTI.Vec MQPR:$Qd_src),
                              (InVTI.Vec MQPR:$Qm),
                              ARMVCCThen, (InVTI.Pred VCCR:$pred), zero_reg))>;
}

defm : MVE_VMOVN_p<MVE_VMOVNi32bh, 0, MVE_v8i16, MVE_v4i32>;
defm : MVE_VMOVN_p<MVE_VMOVNi32th, 1, MVE_v8i16, MVE_v4i32>;
defm : MVE_VMOVN_p<MVE_VMOVNi16bh, 0, MVE_v16i8, MVE_v8i16>;
defm : MVE_VMOVN_p<MVE_VMOVNi16th, 1, MVE_v16i8, MVE_v8i16>;

multiclass MVE_VQMOVN_p<Instruction Inst, bit outU, bit inU, bit top,
                        MVEVectorVTInfo VTI, MVEVectorVTInfo InVTI> {
  def : Pat<(VTI.Vec (int_arm_mve_vqmovn (VTI.Vec MQPR:$Qd_src),
                                  (InVTI.Vec MQPR:$Qm),
                                  (i32 outU), (i32 inU), (i32 top))),
            (VTI.Vec (Inst (VTI.Vec MQPR:$Qd_src),
                              (InVTI.Vec MQPR:$Qm)))>;

  def : Pat<(VTI.Vec (int_arm_mve_vqmovn_predicated (VTI.Vec MQPR:$Qd_src),
                                  (InVTI.Vec MQPR:$Qm),
                                  (i32 outU), (i32 inU), (i32 top),
                                  (InVTI.Pred VCCR:$pred))),
            (VTI.Vec (Inst (VTI.Vec MQPR:$Qd_src),
                              (InVTI.Vec MQPR:$Qm),
                              ARMVCCThen, (InVTI.Pred VCCR:$pred), zero_reg))>;
}

defm : MVE_VQMOVN_p<MVE_VQMOVNs32bh,  0, 0, 0, MVE_v8i16, MVE_v4i32>;
defm : MVE_VQMOVN_p<MVE_VQMOVNs32th,  0, 0, 1, MVE_v8i16, MVE_v4i32>;
defm : MVE_VQMOVN_p<MVE_VQMOVNs16bh,  0, 0, 0, MVE_v16i8, MVE_v8i16>;
defm : MVE_VQMOVN_p<MVE_VQMOVNs16th,  0, 0, 1, MVE_v16i8, MVE_v8i16>;
defm : MVE_VQMOVN_p<MVE_VQMOVNu32bh,  1, 1, 0, MVE_v8i16, MVE_v4i32>;
defm : MVE_VQMOVN_p<MVE_VQMOVNu32th,  1, 1, 1, MVE_v8i16, MVE_v4i32>;
defm : MVE_VQMOVN_p<MVE_VQMOVNu16bh,  1, 1, 0, MVE_v16i8, MVE_v8i16>;
defm : MVE_VQMOVN_p<MVE_VQMOVNu16th,  1, 1, 1, MVE_v16i8, MVE_v8i16>;
defm : MVE_VQMOVN_p<MVE_VQMOVUNs32bh, 1, 0, 0, MVE_v8i16, MVE_v4i32>;
defm : MVE_VQMOVN_p<MVE_VQMOVUNs32th, 1, 0, 1, MVE_v8i16, MVE_v4i32>;
defm : MVE_VQMOVN_p<MVE_VQMOVUNs16bh, 1, 0, 0, MVE_v16i8, MVE_v8i16>;
defm : MVE_VQMOVN_p<MVE_VQMOVUNs16th, 1, 0, 1, MVE_v16i8, MVE_v8i16>;

def SDTARMVMOVNQ : SDTypeProfile<1, 3, [SDTCisVec<0>, SDTCisSameAs<0, 1>,
                                        SDTCisVec<2>, SDTCisVT<3, i32>]>;
def MVEvqmovns   : SDNode<"ARMISD::VQMOVNs", SDTARMVMOVNQ>;
def MVEvqmovnu   : SDNode<"ARMISD::VQMOVNu", SDTARMVMOVNQ>;

let Predicates = [HasMVEInt] in {
  def : Pat<(v8i16 (MVEvqmovns (v8i16 MQPR:$Qd_src), (v4i32 MQPR:$Qm), (i32 0))),
            (v8i16 (MVE_VQMOVNs32bh (v8i16 MQPR:$Qd_src), (v4i32 MQPR:$Qm)))>;
  def : Pat<(v8i16 (MVEvqmovns (v8i16 MQPR:$Qd_src), (v4i32 MQPR:$Qm), (i32 1))),
            (v8i16 (MVE_VQMOVNs32th (v8i16 MQPR:$Qd_src), (v4i32 MQPR:$Qm)))>;
  def : Pat<(v16i8 (MVEvqmovns (v16i8 MQPR:$Qd_src), (v8i16 MQPR:$Qm), (i32 0))),
            (v16i8 (MVE_VQMOVNs16bh (v16i8 MQPR:$Qd_src), (v8i16 MQPR:$Qm)))>;
  def : Pat<(v16i8 (MVEvqmovns (v16i8 MQPR:$Qd_src), (v8i16 MQPR:$Qm), (i32 1))),
            (v16i8 (MVE_VQMOVNs16th (v16i8 MQPR:$Qd_src), (v8i16 MQPR:$Qm)))>;

  def : Pat<(v8i16 (MVEvqmovnu (v8i16 MQPR:$Qd_src), (v4i32 MQPR:$Qm), (i32 0))),
            (v8i16 (MVE_VQMOVNu32bh (v8i16 MQPR:$Qd_src), (v4i32 MQPR:$Qm)))>;
  def : Pat<(v8i16 (MVEvqmovnu (v8i16 MQPR:$Qd_src), (v4i32 MQPR:$Qm), (i32 1))),
            (v8i16 (MVE_VQMOVNu32th (v8i16 MQPR:$Qd_src), (v4i32 MQPR:$Qm)))>;
  def : Pat<(v16i8 (MVEvqmovnu (v16i8 MQPR:$Qd_src), (v8i16 MQPR:$Qm), (i32 0))),
            (v16i8 (MVE_VQMOVNu16bh (v16i8 MQPR:$Qd_src), (v8i16 MQPR:$Qm)))>;
  def : Pat<(v16i8 (MVEvqmovnu (v16i8 MQPR:$Qd_src), (v8i16 MQPR:$Qm), (i32 1))),
            (v16i8 (MVE_VQMOVNu16th (v16i8 MQPR:$Qd_src), (v8i16 MQPR:$Qm)))>;

  def : Pat<(v8i16 (MVEvqmovns (v8i16 MQPR:$Qd_src), (v4i32 (ARMvshrsImm (v4i32 MQPR:$Qm), imm0_31:$imm)), (i32 0))),
            (v8i16 (MVE_VQSHRNbhs32 (v8i16 MQPR:$Qd_src), (v4i32 MQPR:$Qm), imm0_31:$imm))>;
  def : Pat<(v16i8 (MVEvqmovns (v16i8 MQPR:$Qd_src), (v8i16 (ARMvshrsImm (v8i16 MQPR:$Qm), imm0_15:$imm)), (i32 0))),
            (v16i8 (MVE_VQSHRNbhs16 (v16i8 MQPR:$Qd_src), (v8i16 MQPR:$Qm), imm0_15:$imm))>;
  def : Pat<(v8i16 (MVEvqmovns (v8i16 MQPR:$Qd_src), (v4i32 (ARMvshrsImm (v4i32 MQPR:$Qm), imm0_31:$imm)), (i32 1))),
            (v8i16 (MVE_VQSHRNths32 (v8i16 MQPR:$Qd_src), (v4i32 MQPR:$Qm), imm0_31:$imm))>;
  def : Pat<(v16i8 (MVEvqmovns (v16i8 MQPR:$Qd_src), (v8i16 (ARMvshrsImm (v8i16 MQPR:$Qm), imm0_15:$imm)), (i32 1))),
            (v16i8 (MVE_VQSHRNths16 (v16i8 MQPR:$Qd_src), (v8i16 MQPR:$Qm), imm0_15:$imm))>;

  def : Pat<(v8i16 (MVEvqmovnu (v8i16 MQPR:$Qd_src), (v4i32 (ARMvshruImm (v4i32 MQPR:$Qm), imm0_31:$imm)), (i32 0))),
            (v8i16 (MVE_VQSHRNbhu32 (v8i16 MQPR:$Qd_src), (v4i32 MQPR:$Qm), imm0_31:$imm))>;
  def : Pat<(v16i8 (MVEvqmovnu (v16i8 MQPR:$Qd_src), (v8i16 (ARMvshruImm (v8i16 MQPR:$Qm), imm0_15:$imm)), (i32 0))),
            (v16i8 (MVE_VQSHRNbhu16 (v16i8 MQPR:$Qd_src), (v8i16 MQPR:$Qm), imm0_15:$imm))>;
  def : Pat<(v8i16 (MVEvqmovnu (v8i16 MQPR:$Qd_src), (v4i32 (ARMvshruImm (v4i32 MQPR:$Qm), imm0_31:$imm)), (i32 1))),
            (v8i16 (MVE_VQSHRNthu32 (v8i16 MQPR:$Qd_src), (v4i32 MQPR:$Qm), imm0_31:$imm))>;
  def : Pat<(v16i8 (MVEvqmovnu (v16i8 MQPR:$Qd_src), (v8i16 (ARMvshruImm (v8i16 MQPR:$Qm), imm0_15:$imm)), (i32 1))),
            (v16i8 (MVE_VQSHRNthu16 (v16i8 MQPR:$Qd_src), (v8i16 MQPR:$Qm), imm0_15:$imm))>;
}

class MVE_VCVT_ff<string iname, string suffix, bit op, bit T,
                  dag iops_extra, vpred_ops vpred, string cstr>
  : MVE_qDest_qSrc<iname, suffix, (outs MQPR:$Qd),
                   !con(iops_extra, (ins MQPR:$Qm)), "$Qd, $Qm",
                   vpred, cstr, 0b10, []> {
  let Inst{28} = op;
  let Inst{21-16} = 0b111111;
  let Inst{12} = T;
  let Inst{8-7} = 0b00;
  let Inst{0} = 0b1;

  let Predicates = [HasMVEFloat];
  let retainsPreviousHalfElement = 1;
}

def SDTARMVCVTL    : SDTypeProfile<1, 2, [SDTCisVec<0>, SDTCisVec<1>,
                                         SDTCisVT<2, i32>]>;
def MVEvcvtn       : SDNode<"ARMISD::VCVTN", SDTARMVMOVNQ>;
def MVEvcvtl       : SDNode<"ARMISD::VCVTL", SDTARMVCVTL>;

multiclass MVE_VCVT_f2h_m<string iname, int half> {
  def "": MVE_VCVT_ff<iname, "f16.f32", 0b0, half,
                      (ins MQPR:$Qd_src), vpred_n, "$Qd = $Qd_src">;
  defvar Inst = !cast<Instruction>(NAME);

  let Predicates = [HasMVEFloat] in {
    def : Pat<(v8f16 (int_arm_mve_vcvt_narrow
                         (v8f16 MQPR:$Qd_src), (v4f32 MQPR:$Qm), (i32 half))),
              (v8f16 (Inst (v8f16 MQPR:$Qd_src), (v4f32 MQPR:$Qm)))>;
    def : Pat<(v8f16 (int_arm_mve_vcvt_narrow_predicated
                         (v8f16 MQPR:$Qd_src), (v4f32 MQPR:$Qm), (i32 half),
                         (v4i1 VCCR:$mask))),
              (v8f16 (Inst (v8f16 MQPR:$Qd_src), (v4f32 MQPR:$Qm),
                           ARMVCCThen, (v4i1 VCCR:$mask), zero_reg))>;

    def : Pat<(v8f16 (MVEvcvtn (v8f16 MQPR:$Qd_src), (v4f32 MQPR:$Qm), (i32 half))),
              (v8f16 (Inst (v8f16 MQPR:$Qd_src), (v4f32 MQPR:$Qm)))>;
  }
}

multiclass MVE_VCVT_h2f_m<string iname, int half> {
  def "": MVE_VCVT_ff<iname, "f32.f16", 0b1, half, (ins), vpred_r, "">;
  defvar Inst = !cast<Instruction>(NAME);

  let Predicates = [HasMVEFloat] in {
    def : Pat<(v4f32 (int_arm_mve_vcvt_widen (v8f16 MQPR:$Qm), (i32 half))),
              (v4f32 (Inst (v8f16 MQPR:$Qm)))>;
    def : Pat<(v4f32 (int_arm_mve_vcvt_widen_predicated
                         (v4f32 MQPR:$inactive), (v8f16 MQPR:$Qm), (i32 half),
                         (v4i1 VCCR:$mask))),
              (v4f32 (Inst (v8f16 MQPR:$Qm), ARMVCCThen,
                           (v4i1 VCCR:$mask), zero_reg, (v4f32 MQPR:$inactive)))>;

    def : Pat<(v4f32 (MVEvcvtl (v8f16 MQPR:$Qm), (i32 half))),
              (v4f32 (Inst (v8f16 MQPR:$Qm)))>;
  }
}

defm MVE_VCVTf16f32bh : MVE_VCVT_f2h_m<"vcvtb", 0b0>;
defm MVE_VCVTf16f32th : MVE_VCVT_f2h_m<"vcvtt", 0b1>;
defm MVE_VCVTf32f16bh : MVE_VCVT_h2f_m<"vcvtb", 0b0>;
defm MVE_VCVTf32f16th : MVE_VCVT_h2f_m<"vcvtt", 0b1>;

class MVE_VxCADD<string iname, string suffix, bits<2> size, bit halve,
                 string cstr="">
  : MVE_qDest_qSrc<iname, suffix, (outs MQPR:$Qd),
                   (ins MQPR:$Qn, MQPR:$Qm, complexrotateopodd:$rot),
                   "$Qd, $Qn, $Qm, $rot", vpred_r, cstr, size, []> {
  bits<4> Qn;
  bit rot;

  let Inst{28} = halve;
  let Inst{21-20} = size;
  let Inst{19-17} = Qn{2-0};
  let Inst{16} = 0b0;
  let Inst{12} = rot;
  let Inst{8} = 0b1;
  let Inst{7} = Qn{3};
  let Inst{0} = 0b0;
}

multiclass MVE_VxCADD_m<string iname, MVEVectorVTInfo VTI,
                        bit halve, string cstr=""> {
  def "" : MVE_VxCADD<iname, VTI.Suffix, VTI.Size, halve, cstr>;
  defvar Inst = !cast<Instruction>(NAME);

  let Predicates = [HasMVEInt] in {
    def : Pat<(VTI.Vec (int_arm_mve_vcaddq halve,
                            imm:$rot, (VTI.Vec MQPR:$Qn), (VTI.Vec MQPR:$Qm))),
              (VTI.Vec (Inst (VTI.Vec MQPR:$Qn), (VTI.Vec MQPR:$Qm),
                             imm:$rot))>;

    def : Pat<(VTI.Vec (int_arm_mve_vcaddq_predicated halve,
                            imm:$rot, (VTI.Vec MQPR:$inactive),
                            (VTI.Vec MQPR:$Qn), (VTI.Vec MQPR:$Qm),
                            (VTI.Pred VCCR:$mask))),
              (VTI.Vec (Inst (VTI.Vec MQPR:$Qn), (VTI.Vec MQPR:$Qm),
                             imm:$rot, ARMVCCThen, (VTI.Pred VCCR:$mask), zero_reg,
                             (VTI.Vec MQPR:$inactive)))>;

  }
}

defm MVE_VCADDi8   : MVE_VxCADD_m<"vcadd", MVE_v16i8, 0b1>;
defm MVE_VCADDi16  : MVE_VxCADD_m<"vcadd", MVE_v8i16, 0b1>;
defm MVE_VCADDi32  : MVE_VxCADD_m<"vcadd", MVE_v4i32, 0b1, "@earlyclobber $Qd">;

defm MVE_VHCADDs8  : MVE_VxCADD_m<"vhcadd", MVE_v16s8, 0b0>;
defm MVE_VHCADDs16 : MVE_VxCADD_m<"vhcadd", MVE_v8s16, 0b0>;
defm MVE_VHCADDs32 : MVE_VxCADD_m<"vhcadd", MVE_v4s32, 0b0, "@earlyclobber $Qd">;

class MVE_VADCSBC<string iname, bit I, bit subtract,
                  dag carryin, list<dag> pattern=[]>
  : MVE_qDest_qSrc<iname, "i32", (outs MQPR:$Qd, cl_FPSCR_NZCV:$carryout),
                   !con((ins MQPR:$Qn, MQPR:$Qm), carryin),
                   "$Qd, $Qn, $Qm", vpred_r, "", 0b10, pattern> {
  bits<4> Qn;

  let Inst{28} = subtract;
  let Inst{21-20} = 0b11;
  let Inst{19-17} = Qn{2-0};
  let Inst{16} = 0b0;
  let Inst{12} = I;
  let Inst{8} = 0b1;
  let Inst{7} = Qn{3};
  let Inst{0} = 0b0;

  // Custom decoder method in order to add the FPSCR operand(s), which
  // Tablegen won't do right
  let DecoderMethod = "DecodeMVEVADCInstruction";
}

def MVE_VADC  : MVE_VADCSBC<"vadc",  0b0, 0b0, (ins cl_FPSCR_NZCV:$carryin)>;
def MVE_VADCI : MVE_VADCSBC<"vadci", 0b1, 0b0, (ins)>;

def MVE_VSBC  : MVE_VADCSBC<"vsbc",  0b0, 0b1, (ins cl_FPSCR_NZCV:$carryin)>;
def MVE_VSBCI : MVE_VADCSBC<"vsbci", 0b1, 0b1, (ins)>;

class MVE_VQDMULL<string iname, string suffix, bit size, bit T,
                  string cstr="", list<dag> pattern=[]>
  : MVE_qDest_qSrc<iname, suffix, (outs MQPR:$Qd),
                   (ins MQPR:$Qn, MQPR:$Qm), "$Qd, $Qn, $Qm",
                   vpred_r, cstr, !if(size, 0b10, 0b01), pattern> {
  bits<4> Qn;

  let Inst{28} = size;
  let Inst{21-20} = 0b11;
  let Inst{19-17} = Qn{2-0};
  let Inst{16} = 0b0;
  let Inst{12} = T;
  let Inst{8} = 0b1;
  let Inst{7} = Qn{3};
  let Inst{0} = 0b1;
  let validForTailPredication = 1;
  let doubleWidthResult = 1;
}

multiclass MVE_VQDMULL_m<string iname, MVEVectorVTInfo VTI, bit size, bit T,
                         string cstr> {
  def "" : MVE_VQDMULL<iname, VTI.Suffix, size, T, cstr>;
  defvar Inst = !cast<Instruction>(NAME);

  let Predicates = [HasMVEInt] in {
    // Unpredicated saturating multiply
    def : Pat<(VTI.DblVec (int_arm_mve_vqdmull (VTI.Vec MQPR:$Qm),
                                               (VTI.Vec MQPR:$Qn), (i32 T))),
              (VTI.DblVec (Inst (VTI.Vec MQPR:$Qm), (VTI.Vec MQPR:$Qn)))>;
    // Predicated saturating multiply
    def : Pat<(VTI.DblVec (int_arm_mve_vqdmull_predicated
                                    (VTI.Vec MQPR:$Qm), (VTI.Vec MQPR:$Qn),
                                    (i32 T), (VTI.DblPred VCCR:$mask),
                                    (VTI.DblVec MQPR:$inactive))),
              (VTI.DblVec (Inst (VTI.Vec MQPR:$Qm), (VTI.Vec MQPR:$Qn),
                                ARMVCCThen, (VTI.DblPred VCCR:$mask), zero_reg,
                                (VTI.DblVec MQPR:$inactive)))>;
  }
}

multiclass MVE_VQDMULL_halves<MVEVectorVTInfo VTI, bit size, string cstr=""> {
  defm bh : MVE_VQDMULL_m<"vqdmullb", VTI, size, 0b0, cstr>;
  defm th : MVE_VQDMULL_m<"vqdmullt", VTI, size, 0b1, cstr>;
}

defm MVE_VQDMULLs16 : MVE_VQDMULL_halves<MVE_v8s16, 0b0>;
defm MVE_VQDMULLs32 : MVE_VQDMULL_halves<MVE_v4s32, 0b1, "@earlyclobber $Qd">;

// end of mve_qDest_qSrc

// start of mve_qDest_rSrc

class MVE_qr_base<dag oops, dag iops, string iname, string suffix, string ops,
                  vpred_ops vpred, string cstr, bits<2> vecsize, list<dag> pattern=[]>
   : MVE_p<oops, iops, NoItinerary, iname, suffix, ops, vpred, cstr, vecsize, pattern> {
  bits<4> Qd;
  bits<4> Qn;
  bits<4> Rm;

  let Inst{25-23} = 0b100;
  let Inst{22} = Qd{3};
  let Inst{19-17} = Qn{2-0};
  let Inst{15-13} = Qd{2-0};
  let Inst{11-9} = 0b111;
  let Inst{7} = Qn{3};
  let Inst{6} = 0b1;
  let Inst{4} = 0b0;
  let Inst{3-0} = Rm{3-0};
}

class MVE_qDest_rSrc<string iname, string suffix, string cstr="", bits<2> vecsize, list<dag> pattern=[]>
  : MVE_qr_base<(outs MQPR:$Qd), (ins MQPR:$Qn, rGPR:$Rm),
                 iname, suffix, "$Qd, $Qn, $Rm", vpred_r, cstr,
                 vecsize, pattern>;

class MVE_qDestSrc_rSrc<string iname, string suffix, bits<2> vecsize, list<dag> pattern=[]>
  : MVE_qr_base<(outs MQPR:$Qd), (ins MQPR:$Qd_src, MQPR:$Qn, rGPR:$Rm),
                 iname, suffix, "$Qd, $Qn, $Rm", vpred_n, "$Qd = $Qd_src",
                 vecsize, pattern>;

class MVE_qDest_single_rSrc<string iname, string suffix, bits<2> vecsize, list<dag> pattern=[]>
  : MVE_p<(outs MQPR:$Qd), (ins MQPR:$Qd_src, rGPR:$Rm), NoItinerary, iname,
          suffix, "$Qd, $Rm", vpred_n, "$Qd = $Qd_src", vecsize, pattern> {
  bits<4> Qd;
  bits<4> Rm;

  let Inst{22} = Qd{3};
  let Inst{15-13} = Qd{2-0};
  let Inst{3-0} = Rm{3-0};
}

// Patterns for vector-scalar instructions with integer operands
multiclass MVE_vec_scalar_int_pat_m<Instruction inst, MVEVectorVTInfo VTI,
                                    SDPatternOperator unpred_op,
                                    SDPatternOperator pred_op,
                                    bit unpred_has_sign = 0,
                                    bit pred_has_sign = 0> {
  defvar UnpredSign = !if(unpred_has_sign, (? (i32 VTI.Unsigned)), (?));
  defvar PredSign = !if(pred_has_sign, (? (i32 VTI.Unsigned)), (?));

  let Predicates = [HasMVEInt] in {
    // Unpredicated version
    def : Pat<(VTI.Vec !con((unpred_op (VTI.Vec MQPR:$Qm),
                                       (VTI.Vec (ARMvdup rGPR:$val))),
                            UnpredSign)),
              (VTI.Vec (inst (VTI.Vec MQPR:$Qm), (i32 rGPR:$val)))>;
    // Predicated version
    def : Pat<(VTI.Vec !con((pred_op (VTI.Vec MQPR:$Qm),
                                     (VTI.Vec (ARMvdup rGPR:$val))),
                            PredSign,
                            (pred_op (VTI.Pred VCCR:$mask),
                                     (VTI.Vec MQPR:$inactive)))),
              (VTI.Vec (inst (VTI.Vec MQPR:$Qm), (i32 rGPR:$val),
                             ARMVCCThen, (VTI.Pred VCCR:$mask), zero_reg,
                             (VTI.Vec MQPR:$inactive)))>;
  }
}

class MVE_VADDSUB_qr<string iname, string suffix, bits<2> size,
                     bit bit_5, bit bit_12, bit bit_16, bit bit_28>
  : MVE_qDest_rSrc<iname, suffix, "", size> {

  let Inst{28} = bit_28;
  let Inst{21-20} = size;
  let Inst{16} = bit_16;
  let Inst{12} = bit_12;
  let Inst{8} = 0b1;
  let Inst{5} = bit_5;
  let validForTailPredication = 1;
}

// Vector-scalar add/sub
multiclass MVE_VADDSUB_qr_m<string iname, MVEVectorVTInfo VTI, bit subtract,
                            SDNode Op, Intrinsic PredInt> {
  def "" : MVE_VADDSUB_qr<iname, VTI.Suffix, VTI.Size, 0b0, subtract, 0b1, 0b0>;
  let Predicates = [HasMVEInt] in {
    defm : MVE_TwoOpPatternDup<VTI, Op, PredInt, (? ), !cast<Instruction>(NAME), ARMimmAllZerosV>;
  }
}

multiclass MVE_VADD_qr_m<MVEVectorVTInfo VTI>
  : MVE_VADDSUB_qr_m<"vadd", VTI, 0b0, add, int_arm_mve_add_predicated>;

multiclass MVE_VSUB_qr_m<MVEVectorVTInfo VTI>
  : MVE_VADDSUB_qr_m<"vsub", VTI, 0b1, sub, int_arm_mve_sub_predicated>;

defm MVE_VADD_qr_i8  : MVE_VADD_qr_m<MVE_v16i8>;
defm MVE_VADD_qr_i16 : MVE_VADD_qr_m<MVE_v8i16>;
defm MVE_VADD_qr_i32 : MVE_VADD_qr_m<MVE_v4i32>;

defm MVE_VSUB_qr_i8  : MVE_VSUB_qr_m<MVE_v16i8>;
defm MVE_VSUB_qr_i16 : MVE_VSUB_qr_m<MVE_v8i16>;
defm MVE_VSUB_qr_i32 : MVE_VSUB_qr_m<MVE_v4i32>;

// Vector-scalar saturating add/sub
multiclass MVE_VQADDSUB_qr_m<string iname, MVEVectorVTInfo VTI, bit subtract,
                             SDNode Op, Intrinsic PredInt> {
  def "" : MVE_VADDSUB_qr<iname, VTI.Suffix, VTI.Size, 0b1, subtract,
                          0b0, VTI.Unsigned>;

  let Predicates = [HasMVEInt] in {
    defm : MVE_TwoOpPatternDup<VTI, Op, PredInt, (? (i32 VTI.Unsigned)),
                               !cast<Instruction>(NAME)>;
  }
}

multiclass MVE_VQADD_qr_m<MVEVectorVTInfo VTI, SDNode Op>
  : MVE_VQADDSUB_qr_m<"vqadd", VTI, 0b0, Op, int_arm_mve_qadd_predicated>;

multiclass MVE_VQSUB_qr_m<MVEVectorVTInfo VTI, SDNode Op>
  : MVE_VQADDSUB_qr_m<"vqsub", VTI, 0b1, Op, int_arm_mve_qsub_predicated>;

defm MVE_VQADD_qr_s8  : MVE_VQADD_qr_m<MVE_v16s8, saddsat>;
defm MVE_VQADD_qr_s16 : MVE_VQADD_qr_m<MVE_v8s16, saddsat>;
defm MVE_VQADD_qr_s32 : MVE_VQADD_qr_m<MVE_v4s32, saddsat>;
defm MVE_VQADD_qr_u8  : MVE_VQADD_qr_m<MVE_v16u8, uaddsat>;
defm MVE_VQADD_qr_u16 : MVE_VQADD_qr_m<MVE_v8u16, uaddsat>;
defm MVE_VQADD_qr_u32 : MVE_VQADD_qr_m<MVE_v4u32, uaddsat>;

defm MVE_VQSUB_qr_s8  : MVE_VQSUB_qr_m<MVE_v16s8, ssubsat>;
defm MVE_VQSUB_qr_s16 : MVE_VQSUB_qr_m<MVE_v8s16, ssubsat>;
defm MVE_VQSUB_qr_s32 : MVE_VQSUB_qr_m<MVE_v4s32, ssubsat>;
defm MVE_VQSUB_qr_u8  : MVE_VQSUB_qr_m<MVE_v16u8, usubsat>;
defm MVE_VQSUB_qr_u16 : MVE_VQSUB_qr_m<MVE_v8u16, usubsat>;
defm MVE_VQSUB_qr_u32 : MVE_VQSUB_qr_m<MVE_v4u32, usubsat>;

class MVE_VQDMULL_qr<string iname, string suffix, bit size,
                     bit T, string cstr="", list<dag> pattern=[]>
  : MVE_qDest_rSrc<iname, suffix, cstr, !if(size, 0b10, 0b01), pattern> {

  let Inst{28} = size;
  let Inst{21-20} = 0b11;
  let Inst{16} = 0b0;
  let Inst{12} = T;
  let Inst{8} = 0b1;
  let Inst{5} = 0b1;
  let validForTailPredication = 1;
  let doubleWidthResult = 1;
}

multiclass MVE_VQDMULL_qr_m<string iname, MVEVectorVTInfo VTI, bit size,
                            bit T, string cstr> {
  def "" : MVE_VQDMULL_qr<iname, VTI.Suffix, size, T, cstr>;
  defvar Inst = !cast<Instruction>(NAME);

  let Predicates = [HasMVEInt] in {
    // Unpredicated saturating multiply
    def : Pat<(VTI.DblVec (int_arm_mve_vqdmull (VTI.Vec MQPR:$Qm),
                                               (VTI.Vec (ARMvdup rGPR:$val)),
                                               (i32 T))),
              (VTI.DblVec (Inst (VTI.Vec MQPR:$Qm), (i32 rGPR:$val)))>;
    // Predicated saturating multiply
    def : Pat<(VTI.DblVec (int_arm_mve_vqdmull_predicated
                                    (VTI.Vec MQPR:$Qm),
                                    (VTI.Vec (ARMvdup rGPR:$val)),
                                    (i32 T),
                                    (VTI.DblPred VCCR:$mask),
                                    (VTI.DblVec MQPR:$inactive))),
              (VTI.DblVec (Inst (VTI.Vec MQPR:$Qm), (i32 rGPR:$val),
                             ARMVCCThen, (VTI.DblPred VCCR:$mask), zero_reg,
                             (VTI.DblVec MQPR:$inactive)))>;
  }
}

multiclass MVE_VQDMULL_qr_halves<MVEVectorVTInfo VTI, bit size, string cstr=""> {
  defm bh : MVE_VQDMULL_qr_m<"vqdmullb", VTI, size, 0b0, cstr>;
  defm th : MVE_VQDMULL_qr_m<"vqdmullt", VTI, size, 0b1, cstr>;
}

defm MVE_VQDMULL_qr_s16 : MVE_VQDMULL_qr_halves<MVE_v8s16, 0b0>;
defm MVE_VQDMULL_qr_s32 : MVE_VQDMULL_qr_halves<MVE_v4s32, 0b1, "@earlyclobber $Qd">;

class MVE_VxADDSUB_qr<string iname, string suffix,
                      bit bit_28, bits<2> size, bit subtract,
                      bits<2> vecsize, list<dag> pattern=[]>
  : MVE_qDest_rSrc<iname, suffix, "", vecsize, pattern> {

  let Inst{28} = bit_28;
  let Inst{21-20} = size;
  let Inst{16} = 0b0;
  let Inst{12} = subtract;
  let Inst{8} = 0b1;
  let Inst{5} = 0b0;
  let validForTailPredication = 1;
}

multiclass MVE_VHADDSUB_qr_m<string iname, MVEVectorVTInfo VTI, bit subtract, SDNode Op,
                             Intrinsic unpred_int, Intrinsic pred_int, PatFrag add_op, PatFrag shift_op> {
  def "" : MVE_VxADDSUB_qr<iname, VTI.Suffix, VTI.Unsigned, VTI.Size, subtract, VTI.Size>;
  defm : MVE_TwoOpPatternDup<VTI, Op, pred_int, (? (i32 VTI.Unsigned)), !cast<Instruction>(NAME)>;
  defm : MVE_vec_scalar_int_pat_m<!cast<Instruction>(NAME),
                                  VTI, unpred_int, pred_int, 1, 1>;
  defvar Inst = !cast<Instruction>(NAME);

  let Predicates = [HasMVEInt] in {
    def : Pat<(VTI.Vec (shift_op (add_op (VTI.Vec MQPR:$Qm), (VTI.Vec (ARMvdup rGPR:$Rn))), (i32 1))),
              (Inst MQPR:$Qm, rGPR:$Rn)>;
  }
}

multiclass MVE_VHADD_qr_m<MVEVectorVTInfo VTI, PatFrag add_op, SDNode shift_op, SDNode Op> :
  MVE_VHADDSUB_qr_m<"vhadd", VTI, 0b0, Op, int_arm_mve_vhadd,
                    int_arm_mve_hadd_predicated, add_op, shift_op>;

multiclass MVE_VHSUB_qr_m<MVEVectorVTInfo VTI, PatFrag add_op, SDNode shift_op> :
  MVE_VHADDSUB_qr_m<"vhsub", VTI, 0b1, null_frag, int_arm_mve_vhsub,
                    int_arm_mve_hsub_predicated, add_op, shift_op>;

defm MVE_VHADD_qr_s8  : MVE_VHADD_qr_m<MVE_v16s8, addnsw, ARMvshrsImm, avgfloors>;
defm MVE_VHADD_qr_s16 : MVE_VHADD_qr_m<MVE_v8s16, addnsw, ARMvshrsImm, avgfloors>;
defm MVE_VHADD_qr_s32 : MVE_VHADD_qr_m<MVE_v4s32, addnsw, ARMvshrsImm, avgfloors>;
defm MVE_VHADD_qr_u8  : MVE_VHADD_qr_m<MVE_v16u8, addnuw, ARMvshruImm, avgflooru>;
defm MVE_VHADD_qr_u16 : MVE_VHADD_qr_m<MVE_v8u16, addnuw, ARMvshruImm, avgflooru>;
defm MVE_VHADD_qr_u32 : MVE_VHADD_qr_m<MVE_v4u32, addnuw, ARMvshruImm, avgflooru>;

defm MVE_VHSUB_qr_s8  : MVE_VHSUB_qr_m<MVE_v16s8, subnsw, ARMvshrsImm>;
defm MVE_VHSUB_qr_s16 : MVE_VHSUB_qr_m<MVE_v8s16, subnsw, ARMvshrsImm>;
defm MVE_VHSUB_qr_s32 : MVE_VHSUB_qr_m<MVE_v4s32, subnsw, ARMvshrsImm>;
defm MVE_VHSUB_qr_u8  : MVE_VHSUB_qr_m<MVE_v16u8, subnuw, ARMvshruImm>;
defm MVE_VHSUB_qr_u16 : MVE_VHSUB_qr_m<MVE_v8u16, subnuw, ARMvshruImm>;
defm MVE_VHSUB_qr_u32 : MVE_VHSUB_qr_m<MVE_v4u32, subnuw, ARMvshruImm>;

multiclass MVE_VADDSUB_qr_f<string iname, MVEVectorVTInfo VTI, bit subtract,
                            SDNode Op, Intrinsic PredInt, SDPatternOperator IdentityVec> {
  def "" : MVE_VxADDSUB_qr<iname, VTI.Suffix, VTI.Size{0}, 0b11, subtract, VTI.Size>;
  defm : MVE_TwoOpPatternDup<VTI, Op, PredInt, (? ),
                              !cast<Instruction>(NAME), IdentityVec>;
}

let Predicates = [HasMVEFloat] in {
  defm MVE_VADD_qr_f32 : MVE_VADDSUB_qr_f<"vadd", MVE_v4f32, 0b0, fadd,
                                          int_arm_mve_add_predicated, ARMimmMinusZeroF>;
  defm MVE_VADD_qr_f16 : MVE_VADDSUB_qr_f<"vadd", MVE_v8f16, 0b0, fadd,
                                          int_arm_mve_add_predicated, ARMimmMinusZeroH>;

  defm MVE_VSUB_qr_f32 : MVE_VADDSUB_qr_f<"vsub", MVE_v4f32, 0b1, fsub,
                                          int_arm_mve_sub_predicated, ARMimmAllZerosV>;
  defm MVE_VSUB_qr_f16 : MVE_VADDSUB_qr_f<"vsub", MVE_v8f16, 0b1, fsub,
                                          int_arm_mve_sub_predicated, ARMimmAllZerosV>;
}

class MVE_VxSHL_qr<string iname, string suffix, bit U, bits<2> size,
                   bit bit_7, bit bit_17, list<dag> pattern=[]>
  : MVE_qDest_single_rSrc<iname, suffix, size, pattern> {

  let Inst{28} = U;
  let Inst{25-23} = 0b100;
  let Inst{21-20} = 0b11;
  let Inst{19-18} = size;
  let Inst{17} = bit_17;
  let Inst{16} = 0b1;
  let Inst{12-8} = 0b11110;
  let Inst{7} = bit_7;
  let Inst{6-4} = 0b110;
  let validForTailPredication = 1;
}

multiclass MVE_VxSHL_qr_p<string iname, MVEVectorVTInfo VTI, bit q, bit r> {
  def "" : MVE_VxSHL_qr<iname, VTI.Suffix, VTI.Unsigned, VTI.Size, q, r>;
  defvar Inst = !cast<Instruction>(NAME);

  def : Pat<(VTI.Vec (int_arm_mve_vshl_scalar
                         (VTI.Vec MQPR:$in), (i32 rGPR:$sh),
                         (i32 q), (i32 r), (i32 VTI.Unsigned))),
            (VTI.Vec (Inst (VTI.Vec MQPR:$in), (i32 rGPR:$sh)))>;

  def : Pat<(VTI.Vec (int_arm_mve_vshl_scalar_predicated
                         (VTI.Vec MQPR:$in), (i32 rGPR:$sh),
                         (i32 q), (i32 r), (i32 VTI.Unsigned),
                         (VTI.Pred VCCR:$mask))),
            (VTI.Vec (Inst (VTI.Vec MQPR:$in), (i32 rGPR:$sh),
                           ARMVCCThen, (VTI.Pred VCCR:$mask), zero_reg))>;
}

multiclass MVE_VxSHL_qr_types<string iname, bit bit_7, bit bit_17> {
  defm s8  : MVE_VxSHL_qr_p<iname, MVE_v16s8, bit_7, bit_17>;
  defm s16 : MVE_VxSHL_qr_p<iname, MVE_v8s16, bit_7, bit_17>;
  defm s32 : MVE_VxSHL_qr_p<iname, MVE_v4s32, bit_7, bit_17>;
  defm u8  : MVE_VxSHL_qr_p<iname, MVE_v16u8, bit_7, bit_17>;
  defm u16 : MVE_VxSHL_qr_p<iname, MVE_v8u16, bit_7, bit_17>;
  defm u32 : MVE_VxSHL_qr_p<iname, MVE_v4u32, bit_7, bit_17>;
}

defm MVE_VSHL_qr   : MVE_VxSHL_qr_types<"vshl",   0b0, 0b0>;
defm MVE_VRSHL_qr  : MVE_VxSHL_qr_types<"vrshl",  0b0, 0b1>;
defm MVE_VQSHL_qr  : MVE_VxSHL_qr_types<"vqshl",  0b1, 0b0>;
defm MVE_VQRSHL_qr : MVE_VxSHL_qr_types<"vqrshl", 0b1, 0b1>;

let Predicates = [HasMVEInt] in {
  def : Pat<(v4i32 (ARMvshlu (v4i32 MQPR:$Qm), (v4i32 (ARMvdup rGPR:$Rm)))),
            (v4i32 (MVE_VSHL_qru32 (v4i32 MQPR:$Qm), rGPR:$Rm))>;
  def : Pat<(v8i16 (ARMvshlu (v8i16 MQPR:$Qm), (v8i16 (ARMvdup rGPR:$Rm)))),
            (v8i16 (MVE_VSHL_qru16 (v8i16 MQPR:$Qm), rGPR:$Rm))>;
  def : Pat<(v16i8 (ARMvshlu (v16i8 MQPR:$Qm), (v16i8 (ARMvdup rGPR:$Rm)))),
            (v16i8 (MVE_VSHL_qru8 (v16i8 MQPR:$Qm), rGPR:$Rm))>;

  def : Pat<(v4i32 (ARMvshls (v4i32 MQPR:$Qm), (v4i32 (ARMvdup rGPR:$Rm)))),
            (v4i32 (MVE_VSHL_qrs32 (v4i32 MQPR:$Qm), rGPR:$Rm))>;
  def : Pat<(v8i16 (ARMvshls (v8i16 MQPR:$Qm), (v8i16 (ARMvdup rGPR:$Rm)))),
            (v8i16 (MVE_VSHL_qrs16 (v8i16 MQPR:$Qm), rGPR:$Rm))>;
  def : Pat<(v16i8 (ARMvshls (v16i8 MQPR:$Qm), (v16i8 (ARMvdup rGPR:$Rm)))),
            (v16i8 (MVE_VSHL_qrs8 (v16i8 MQPR:$Qm), rGPR:$Rm))>;
}

class MVE_VBRSR<string iname, string suffix, bits<2> size, list<dag> pattern=[]>
  : MVE_qDest_rSrc<iname, suffix, "", size, pattern> {

  let Inst{28} = 0b1;
  let Inst{21-20} = size;
  let Inst{16} = 0b1;
  let Inst{12} = 0b1;
  let Inst{8} = 0b0;
  let Inst{5} = 0b1;
  let validForTailPredication = 1;
}

def MVE_VBRSR8  : MVE_VBRSR<"vbrsr", "8", 0b00>;
def MVE_VBRSR16 : MVE_VBRSR<"vbrsr", "16", 0b01>;
def MVE_VBRSR32 : MVE_VBRSR<"vbrsr", "32", 0b10>;

multiclass MVE_VBRSR_pat_m<MVEVectorVTInfo VTI, Instruction Inst> {
  // Unpredicated
  def : Pat<(VTI.Vec (int_arm_mve_vbrsr (VTI.Vec MQPR:$Qn), (i32 rGPR:$Rm))),
            (VTI.Vec (Inst (VTI.Vec MQPR:$Qn), (i32 rGPR:$Rm)))>;
  // Predicated
  def : Pat<(VTI.Vec (int_arm_mve_vbrsr_predicated
                          (VTI.Vec MQPR:$inactive),
                          (VTI.Vec MQPR:$Qn), (i32 rGPR:$Rm),
                          (VTI.Pred VCCR:$mask))),
            (VTI.Vec (Inst (VTI.Vec MQPR:$Qn), (i32 rGPR:$Rm),
                          ARMVCCThen, (VTI.Pred VCCR:$mask), zero_reg,
                          (VTI.Vec MQPR:$inactive)))>;
}

let Predicates = [HasMVEInt] in {
  def : Pat<(v16i8 ( bitreverse (v16i8 MQPR:$val1))),
            (v16i8 ( MVE_VBRSR8 (v16i8 MQPR:$val1), (t2MOVi (i32 8)) ))>;

  def : Pat<(v4i32 ( bitreverse (v4i32 MQPR:$val1))),
            (v4i32 ( MVE_VBRSR32 (v4i32 MQPR:$val1), (t2MOVi (i32 32)) ))>;

  def : Pat<(v8i16 ( bitreverse (v8i16 MQPR:$val1))),
            (v8i16 ( MVE_VBRSR16 (v8i16 MQPR:$val1), (t2MOVi (i32 16)) ))>;

  defm : MVE_VBRSR_pat_m<MVE_v16i8, MVE_VBRSR8>;
  defm : MVE_VBRSR_pat_m<MVE_v8i16, MVE_VBRSR16>;
  defm : MVE_VBRSR_pat_m<MVE_v4i32, MVE_VBRSR32>;
}

let Predicates = [HasMVEFloat] in {
  defm : MVE_VBRSR_pat_m<MVE_v8f16, MVE_VBRSR16>;
  defm : MVE_VBRSR_pat_m<MVE_v4f32, MVE_VBRSR32>;
}

class MVE_VMUL_qr_int<string iname, string suffix, bits<2> size>
  : MVE_qDest_rSrc<iname, suffix, "", size> {

  let Inst{28} = 0b0;
  let Inst{21-20} = size;
  let Inst{16} = 0b1;
  let Inst{12} = 0b1;
  let Inst{8} = 0b0;
  let Inst{5} = 0b1;
  let validForTailPredication = 1;
}

multiclass MVE_VMUL_qr_int_m<MVEVectorVTInfo VTI> {
  def "" : MVE_VMUL_qr_int<"vmul", VTI.Suffix, VTI.Size>;
  let Predicates = [HasMVEInt] in {
    defm : MVE_TwoOpPatternDup<VTI, mul, int_arm_mve_mul_predicated, (? ),
                               !cast<Instruction>(NAME), ARMimmOneV>;
  }
}

defm MVE_VMUL_qr_i8  : MVE_VMUL_qr_int_m<MVE_v16i8>;
defm MVE_VMUL_qr_i16 : MVE_VMUL_qr_int_m<MVE_v8i16>;
defm MVE_VMUL_qr_i32 : MVE_VMUL_qr_int_m<MVE_v4i32>;

class MVE_VxxMUL_qr<string iname, string suffix,
                    bit bit_28, bits<2> size, bits<2> vecsize, list<dag> pattern=[]>
  : MVE_qDest_rSrc<iname, suffix, "", vecsize, pattern> {

  let Inst{28} = bit_28;
  let Inst{21-20} = size;
  let Inst{16} = 0b1;
  let Inst{12} = 0b0;
  let Inst{8} = 0b0;
  let Inst{5} = 0b1;
  let validForTailPredication = 1;
}

multiclass MVE_VxxMUL_qr_m<string iname, MVEVectorVTInfo VTI, bit bit_28,
                           PatFrag Op, Intrinsic int_unpred, Intrinsic int_pred> {
  def "" : MVE_VxxMUL_qr<iname, VTI.Suffix, bit_28, VTI.Size, VTI.Size>;

  let Predicates = [HasMVEInt] in {
    defm : MVE_TwoOpPatternDup<VTI, Op, int_pred, (? ), !cast<Instruction>(NAME)>;
  }
  defm : MVE_vec_scalar_int_pat_m<!cast<Instruction>(NAME), VTI, int_unpred, int_pred>;
}

multiclass MVE_VQDMULH_qr_m<MVEVectorVTInfo VTI> :
  MVE_VxxMUL_qr_m<"vqdmulh", VTI, 0b0, MVEvqdmulh,
                  int_arm_mve_vqdmulh, int_arm_mve_qdmulh_predicated>;

multiclass MVE_VQRDMULH_qr_m<MVEVectorVTInfo VTI> :
  MVE_VxxMUL_qr_m<"vqrdmulh", VTI, 0b1, null_frag,
                  int_arm_mve_vqrdmulh, int_arm_mve_qrdmulh_predicated>;

defm MVE_VQDMULH_qr_s8    : MVE_VQDMULH_qr_m<MVE_v16s8>;
defm MVE_VQDMULH_qr_s16   : MVE_VQDMULH_qr_m<MVE_v8s16>;
defm MVE_VQDMULH_qr_s32   : MVE_VQDMULH_qr_m<MVE_v4s32>;

defm MVE_VQRDMULH_qr_s8   : MVE_VQRDMULH_qr_m<MVE_v16s8>;
defm MVE_VQRDMULH_qr_s16  : MVE_VQRDMULH_qr_m<MVE_v8s16>;
defm MVE_VQRDMULH_qr_s32  : MVE_VQRDMULH_qr_m<MVE_v4s32>;

multiclass MVE_VxxMUL_qr_f_m<MVEVectorVTInfo VTI, SDPatternOperator IdentityVec> {
  let validForTailPredication = 1 in
  def "" : MVE_VxxMUL_qr<"vmul", VTI.Suffix, VTI.Size{0}, 0b11, VTI.Size>;
  defm : MVE_TwoOpPatternDup<VTI, fmul, int_arm_mve_mul_predicated, (? ),
                             !cast<Instruction>(NAME), IdentityVec>;
}

let Predicates = [HasMVEFloat] in {
  defm MVE_VMUL_qr_f16   : MVE_VxxMUL_qr_f_m<MVE_v8f16, ARMimmOneH>;
  defm MVE_VMUL_qr_f32   : MVE_VxxMUL_qr_f_m<MVE_v4f32, ARMimmOneF>;
}

class MVE_VFMAMLA_qr<string iname, string suffix,
                     bit bit_28, bits<2> bits_21_20, bit S,
                     bits<2> vecsize, list<dag> pattern=[]>
  : MVE_qDestSrc_rSrc<iname, suffix, vecsize, pattern> {

  let Inst{28} = bit_28;
  let Inst{21-20} = bits_21_20;
  let Inst{16} = 0b1;
  let Inst{12} = S;
  let Inst{8} = 0b0;
  let Inst{5} = 0b0;
  let validForTailPredication = 1;
  let hasSideEffects = 0;
}

multiclass MVE_VMLA_qr_multi<string iname, MVEVectorVTInfo VTI,
                             bit scalar_addend> {
  def "": MVE_VFMAMLA_qr<iname, VTI.Suffix, 0b0, VTI.Size,
                         scalar_addend, VTI.Size>;
  defvar Inst = !cast<Instruction>(NAME);
  defvar pred_int = !cast<Intrinsic>("int_arm_mve_" # iname # "_n_predicated");
  defvar v1   = (VTI.Vec MQPR:$v1);
  defvar v2   = (VTI.Vec MQPR:$v2);
  defvar vs   = (VTI.Vec (ARMvdup rGPR:$s));
  defvar s    = (i32 rGPR:$s);
  defvar pred = (VTI.Pred VCCR:$pred);

  let Predicates = [HasMVEInt] in {
    if scalar_addend then {
      def : Pat<(VTI.Vec (add (mul v1, v2), vs)),
                (VTI.Vec (Inst v1, v2, s))>;
    } else {
      def : Pat<(VTI.Vec (add (mul v2, vs), v1)),
                (VTI.Vec (Inst v1, v2, s))>;
    }

    def : Pat<(VTI.Vec (pred_int v1, v2, s, pred)),
              (VTI.Vec (Inst v1, v2, s, ARMVCCThen, pred, zero_reg))>;
  }
}

defm MVE_VMLA_qr_i8   : MVE_VMLA_qr_multi<"vmla", MVE_v16i8, 0b0>;
defm MVE_VMLA_qr_i16  : MVE_VMLA_qr_multi<"vmla", MVE_v8i16, 0b0>;
defm MVE_VMLA_qr_i32  : MVE_VMLA_qr_multi<"vmla", MVE_v4i32, 0b0>;

defm MVE_VMLAS_qr_i8  : MVE_VMLA_qr_multi<"vmlas", MVE_v16i8, 0b1>;
defm MVE_VMLAS_qr_i16 : MVE_VMLA_qr_multi<"vmlas", MVE_v8i16, 0b1>;
defm MVE_VMLAS_qr_i32 : MVE_VMLA_qr_multi<"vmlas", MVE_v4i32, 0b1>;

multiclass MVE_VFMA_qr_multi<string iname, MVEVectorVTInfo VTI,
                             bit scalar_addend> {
  def "": MVE_VFMAMLA_qr<iname, VTI.Suffix, VTI.Size{0}, 0b11, scalar_addend, VTI.Size>;
  defvar Inst = !cast<Instruction>(NAME);
  defvar pred_int = int_arm_mve_fma_predicated;
  defvar v1   = (VTI.Vec MQPR:$v1);
  defvar v2   = (VTI.Vec MQPR:$v2);
  defvar vs   = (VTI.Vec (ARMvdup (i32 rGPR:$s)));
  defvar is   = (i32 rGPR:$s);
  defvar pred = (VTI.Pred VCCR:$pred);

  let Predicates = [HasMVEFloat] in {
    if scalar_addend then {
      def : Pat<(VTI.Vec (fma v1, v2, vs)),
                (VTI.Vec (Inst v1, v2, is))>;
      def : Pat<(VTI.Vec (vselect (VTI.Pred VCCR:$pred),
                                  (VTI.Vec (fma v1, v2, vs)),
                                  v1)),
                (VTI.Vec (Inst v1, v2, is, ARMVCCThen, $pred, zero_reg))>;
      def : Pat<(VTI.Vec (pred_int v1, v2, vs, pred)),
                (VTI.Vec (Inst v1, v2, is, ARMVCCThen, pred, zero_reg))>;
    } else {
      def : Pat<(VTI.Vec (fma v1, vs, v2)),
                (VTI.Vec (Inst v2, v1, is))>;
      def : Pat<(VTI.Vec (fma vs, v1, v2)),
                (VTI.Vec (Inst v2, v1, is))>;
      def : Pat<(VTI.Vec (vselect (VTI.Pred VCCR:$pred),
                                  (VTI.Vec (fma vs, v2, v1)),
                                  v1)),
                (VTI.Vec (Inst v1, v2, is, ARMVCCThen, $pred, zero_reg))>;
      def : Pat<(VTI.Vec (vselect (VTI.Pred VCCR:$pred),
                                  (VTI.Vec (fma v2, vs, v1)),
                                  v1)),
                (VTI.Vec (Inst v1, v2, is, ARMVCCThen, $pred, zero_reg))>;
      def : Pat<(VTI.Vec (pred_int v1, vs, v2, pred)),
                (VTI.Vec (Inst v2, v1, is, ARMVCCThen, pred, zero_reg))>;
      def : Pat<(VTI.Vec (pred_int vs, v1, v2, pred)),
                (VTI.Vec (Inst v2, v1, is, ARMVCCThen, pred, zero_reg))>;
    }
  }
}

let Predicates = [HasMVEFloat] in {
  defm MVE_VFMA_qr_f16  : MVE_VFMA_qr_multi<"vfma",  MVE_v8f16, 0>;
  defm MVE_VFMA_qr_f32  : MVE_VFMA_qr_multi<"vfma",  MVE_v4f32, 0>;
  defm MVE_VFMA_qr_Sf16 : MVE_VFMA_qr_multi<"vfmas", MVE_v8f16, 1>;
  defm MVE_VFMA_qr_Sf32 : MVE_VFMA_qr_multi<"vfmas", MVE_v4f32, 1>;
}

class MVE_VQDMLAH_qr<string iname, string suffix, bit U, bits<2> size,
                     bit bit_5, bit bit_12, list<dag> pattern=[]>
  : MVE_qDestSrc_rSrc<iname, suffix, size, pattern> {

  let Inst{28} = U;
  let Inst{21-20} = size;
  let Inst{16} = 0b0;
  let Inst{12} = bit_12;
  let Inst{8} = 0b0;
  let Inst{5} = bit_5;
}

multiclass MVE_VQDMLAH_qr_multi<string iname, MVEVectorVTInfo VTI,
                                bit bit_5, bit bit_12> {
  def "": MVE_VQDMLAH_qr<iname, VTI.Suffix, 0b0, VTI.Size, bit_5, bit_12>;
  defvar Inst = !cast<Instruction>(NAME);
  defvar unpred_int = !cast<Intrinsic>("int_arm_mve_" # iname);
  defvar pred_int = !cast<Intrinsic>("int_arm_mve_" # iname # "_predicated");

  let Predicates = [HasMVEInt] in {
    def : Pat<(VTI.Vec (unpred_int (VTI.Vec MQPR:$v1), (VTI.Vec MQPR:$v2),
                                   (i32 rGPR:$s))),
              (VTI.Vec (Inst       (VTI.Vec MQPR:$v1), (VTI.Vec MQPR:$v2),
                                   (i32 rGPR:$s)))>;
    def : Pat<(VTI.Vec (pred_int   (VTI.Vec MQPR:$v1), (VTI.Vec MQPR:$v2),
                                   (i32 rGPR:$s), (VTI.Pred VCCR:$pred))),
              (VTI.Vec (Inst       (VTI.Vec MQPR:$v1), (VTI.Vec MQPR:$v2),
                                   (i32 rGPR:$s), ARMVCCThen,
                                   (VTI.Pred VCCR:$pred), zero_reg))>;
  }
}

multiclass MVE_VQDMLAH_qr_types<string iname, bit bit_5, bit bit_12> {
  defm s8  : MVE_VQDMLAH_qr_multi<iname, MVE_v16s8, bit_5, bit_12>;
  defm s16 : MVE_VQDMLAH_qr_multi<iname, MVE_v8s16, bit_5, bit_12>;
  defm s32 : MVE_VQDMLAH_qr_multi<iname, MVE_v4s32, bit_5, bit_12>;
}

defm MVE_VQDMLAH_qr   : MVE_VQDMLAH_qr_types<"vqdmlah",   0b1, 0b0>;
defm MVE_VQRDMLAH_qr  : MVE_VQDMLAH_qr_types<"vqrdmlah",  0b0, 0b0>;
defm MVE_VQDMLASH_qr  : MVE_VQDMLAH_qr_types<"vqdmlash",  0b1, 0b1>;
defm MVE_VQRDMLASH_qr : MVE_VQDMLAH_qr_types<"vqrdmlash", 0b0, 0b1>;

class MVE_VxDUP<string iname, string suffix, bits<2> size, bit bit_12,
                ValueType VT, SDPatternOperator vxdup>
  : MVE_p<(outs MQPR:$Qd, tGPREven:$Rn),
          (ins tGPREven:$Rn_src, MVE_VIDUP_imm:$imm), NoItinerary,
          iname, suffix, "$Qd, $Rn, $imm", vpred_r, "$Rn = $Rn_src", size,
          [(set (VT MQPR:$Qd), (i32 tGPREven:$Rn),
              (vxdup (i32 tGPREven:$Rn_src), (i32 imm:$imm)))]> {
  bits<4> Qd;
  bits<4> Rn;
  bits<2> imm;

  let Inst{28} = 0b0;
  let Inst{25-23} = 0b100;
  let Inst{22} = Qd{3};
  let Inst{21-20} = size;
  let Inst{19-17} = Rn{3-1};
  let Inst{16} = 0b1;
  let Inst{15-13} = Qd{2-0};
  let Inst{12} = bit_12;
  let Inst{11-8} = 0b1111;
  let Inst{7} = imm{1};
  let Inst{6-1} = 0b110111;
  let Inst{0} = imm{0};
  let validForTailPredication = 1;
  let hasSideEffects = 0;
}

def MVE_VIDUPu8  : MVE_VxDUP<"vidup", "u8",  0b00, 0b0, v16i8, ARMvidup>;
def MVE_VIDUPu16 : MVE_VxDUP<"vidup", "u16", 0b01, 0b0, v8i16, ARMvidup>;
def MVE_VIDUPu32 : MVE_VxDUP<"vidup", "u32", 0b10, 0b0, v4i32, ARMvidup>;

def MVE_VDDUPu8  : MVE_VxDUP<"vddup", "u8",  0b00, 0b1, v16i8, null_frag>;
def MVE_VDDUPu16 : MVE_VxDUP<"vddup", "u16", 0b01, 0b1, v8i16, null_frag>;
def MVE_VDDUPu32 : MVE_VxDUP<"vddup", "u32", 0b10, 0b1, v4i32, null_frag>;

class MVE_VxWDUP<string iname, string suffix, bits<2> size, bit bit_12,
                 list<dag> pattern=[]>
  : MVE_p<(outs MQPR:$Qd, tGPREven:$Rn),
          (ins tGPREven:$Rn_src, tGPROdd:$Rm, MVE_VIDUP_imm:$imm), NoItinerary,
          iname, suffix, "$Qd, $Rn, $Rm, $imm", vpred_r, "$Rn = $Rn_src", size,
          pattern> {
  bits<4> Qd;
  bits<4> Rm;
  bits<4> Rn;
  bits<2> imm;

  let Inst{28} = 0b0;
  let Inst{25-23} = 0b100;
  let Inst{22} = Qd{3};
  let Inst{21-20} = size;
  let Inst{19-17} = Rn{3-1};
  let Inst{16} = 0b1;
  let Inst{15-13} = Qd{2-0};
  let Inst{12} = bit_12;
  let Inst{11-8} = 0b1111;
  let Inst{7} = imm{1};
  let Inst{6-4} = 0b110;
  let Inst{3-1} = Rm{3-1};
  let Inst{0} = imm{0};
  let validForTailPredication = 1;
  let hasSideEffects = 0;
}

def MVE_VIWDUPu8  : MVE_VxWDUP<"viwdup", "u8",  0b00, 0b0>;
def MVE_VIWDUPu16 : MVE_VxWDUP<"viwdup", "u16", 0b01, 0b0>;
def MVE_VIWDUPu32 : MVE_VxWDUP<"viwdup", "u32", 0b10, 0b0>;

def MVE_VDWDUPu8  : MVE_VxWDUP<"vdwdup", "u8",  0b00, 0b1>;
def MVE_VDWDUPu16 : MVE_VxWDUP<"vdwdup", "u16", 0b01, 0b1>;
def MVE_VDWDUPu32 : MVE_VxWDUP<"vdwdup", "u32", 0b10, 0b1>;

let isReMaterializable = 1 in
class MVE_VCTPInst<string suffix, bits<2> size, list<dag> pattern=[]>
  : MVE_p<(outs VCCR:$P0), (ins rGPR:$Rn), NoItinerary, "vctp", suffix,
          "$Rn", vpred_n, "", size, pattern> {
  bits<4> Rn;

  let Inst{28-27} = 0b10;
  let Inst{26-22} = 0b00000;
  let Inst{21-20} = size;
  let Inst{19-16} = Rn{3-0};
  let Inst{15-11} = 0b11101;
  let Inst{10-0}  = 0b00000000001;
  let Unpredictable{10-0} = 0b11111111111;

  let Constraints = "";
  let DecoderMethod = "DecodeMveVCTP";
  let validForTailPredication = 1;
}

multiclass MVE_VCTP<MVEVectorVTInfo VTI, Intrinsic intr> {
  def "": MVE_VCTPInst<VTI.BitsSuffix, VTI.Size>;
  defvar Inst = !cast<Instruction>(NAME);

  let Predicates = [HasMVEInt] in {
    def : Pat<(intr rGPR:$Rn),
              (VTI.Pred (Inst rGPR:$Rn))>;
    def : Pat<(and (intr rGPR:$Rn), (VTI.Pred VCCR:$mask)),
              (VTI.Pred (Inst rGPR:$Rn, ARMVCCThen, VCCR:$mask, zero_reg))>;
  }
}

defm MVE_VCTP8  : MVE_VCTP<MVE_v16i8, int_arm_mve_vctp8>;
defm MVE_VCTP16 : MVE_VCTP<MVE_v8i16, int_arm_mve_vctp16>;
defm MVE_VCTP32 : MVE_VCTP<MVE_v4i32, int_arm_mve_vctp32>;
defm MVE_VCTP64 : MVE_VCTP<MVE_v2i64, int_arm_mve_vctp64>;

// end of mve_qDest_rSrc

// start of coproc mov

class MVE_VMOV_64bit<dag oops, dag iops, bit to_qreg, string ops, string cstr>
  : MVE_VMOV_lane_base<oops, !con(iops, (ins MVEPairVectorIndex2:$idx,
                                             MVEPairVectorIndex0:$idx2)),
                       NoItinerary, "vmov", "", ops, cstr, []> {
  bits<5> Rt;
  bits<5> Rt2;
  bits<4> Qd;
  bit idx;
  bit idx2;

  let Inst{31-23} = 0b111011000;
  let Inst{22} = Qd{3};
  let Inst{21} = 0b0;
  let Inst{20} = to_qreg;
  let Inst{19-16} = Rt2{3-0};
  let Inst{15-13} = Qd{2-0};
  let Inst{12-5} = 0b01111000;
  let Inst{4} = idx2;
  let Inst{3-0} = Rt{3-0};

  let VecSize = 0b10;
  let hasSideEffects = 0;
}

// The assembly syntax for these instructions mentions the vector
// register name twice, e.g.
//
//    vmov q2[2], q2[0], r0, r1
//    vmov r0, r1, q2[2], q2[0]
//
// which needs a bit of juggling with MC operand handling.
//
// For the move _into_ a vector register, the MC operand list also has
// to mention the register name twice: once as the output, and once as
// an extra input to represent where the unchanged half of the output
// register comes from (when this instruction is used in code
// generation). So we arrange that the first mention of the vector reg
// in the instruction is considered by the AsmMatcher to be the output
// ($Qd), and the second one is the input ($QdSrc). Binding them
// together with the existing 'tie' constraint is enough to enforce at
// register allocation time that they have to be the same register.
//
// For the move _from_ a vector register, there's no way to get round
// the fact that both instances of that register name have to be
// inputs. They have to be the same register again, but this time, we
// can't use a tie constraint, because that has to be between an
// output and an input operand. So this time, we have to arrange that
// the q-reg appears just once in the MC operand list, in spite of
// being mentioned twice in the asm syntax - which needs a custom
// AsmMatchConverter.

def MVE_VMOV_q_rr : MVE_VMOV_64bit<(outs MQPR:$Qd),
                                   (ins MQPR:$QdSrc, rGPR:$Rt, rGPR:$Rt2),
                                   0b1, "$Qd$idx, $QdSrc$idx2, $Rt, $Rt2",
                                   "$Qd = $QdSrc"> {
  let DecoderMethod = "DecodeMVEVMOVDRegtoQ";
}

def MVE_VMOV_rr_q : MVE_VMOV_64bit<(outs rGPR:$Rt, rGPR:$Rt2), (ins MQPR:$Qd),
                                   0b0, "$Rt, $Rt2, $Qd$idx, $Qd$idx2", ""> {
  let DecoderMethod = "DecodeMVEVMOVQtoDReg";
  let AsmMatchConverter = "cvtMVEVMOVQtoDReg";
}

let Predicates = [HasMVEInt] in {
  // Double lane moves. There are a number of patterns here. We know that the
  // insertelt's will be in descending order by index, and need to match the 5
  // patterns that might contain 2-0 or 3-1 pairs. These are:
  // 3 2 1 0    -> vmovqrr 31; vmovqrr 20
  // 3 2 1      -> vmovqrr 31; vmov 2
  // 3 1        -> vmovqrr 31
  // 2 1 0      -> vmovqrr 20; vmov 1
  // 2 0        -> vmovqrr 20
  // The other potential patterns will be handled by single lane inserts.
  def : Pat<(insertelt (insertelt (insertelt (insertelt (v4i32 MQPR:$src1),
                                                        rGPR:$srcA, (i32 0)),
                                             rGPR:$srcB, (i32 1)),
                                  rGPR:$srcC, (i32 2)),
                       rGPR:$srcD, (i32 3)),
            (MVE_VMOV_q_rr (MVE_VMOV_q_rr MQPR:$src1, rGPR:$srcA, rGPR:$srcC, (i32 2), (i32 0)),
                           rGPR:$srcB, rGPR:$srcD, (i32 3), (i32 1))>;
  def : Pat<(insertelt (insertelt (insertelt (v4i32 MQPR:$src1),
                                             rGPR:$srcB, (i32 1)),
                                  rGPR:$srcC, (i32 2)),
                       rGPR:$srcD, (i32 3)),
            (MVE_VMOV_q_rr (MVE_VMOV_to_lane_32 MQPR:$src1, rGPR:$srcC, (i32 2)),
                           rGPR:$srcB, rGPR:$srcD, (i32 3), (i32 1))>;
  def : Pat<(insertelt (insertelt (v4i32 MQPR:$src1), rGPR:$srcA, (i32 1)), rGPR:$srcB, (i32 3)),
            (MVE_VMOV_q_rr MQPR:$src1, rGPR:$srcA, rGPR:$srcB, (i32 3), (i32 1))>;
  def : Pat<(insertelt (insertelt (insertelt (v4i32 MQPR:$src1),
                                             rGPR:$srcB, (i32 0)),
                                  rGPR:$srcC, (i32 1)),
                       rGPR:$srcD, (i32 2)),
            (MVE_VMOV_q_rr (MVE_VMOV_to_lane_32 MQPR:$src1, rGPR:$srcC, (i32 1)),
                           rGPR:$srcB, rGPR:$srcD, (i32 2), (i32 0))>;
  def : Pat<(insertelt (insertelt (v4i32 MQPR:$src1), rGPR:$srcA, (i32 0)), rGPR:$srcB, (i32 2)),
            (MVE_VMOV_q_rr MQPR:$src1, rGPR:$srcA, rGPR:$srcB, (i32 2), (i32 0))>;
}

// end of coproc mov

// start of MVE interleaving load/store

// Base class for the family of interleaving/deinterleaving
// load/stores with names like VLD20.8 and VST43.32.
class MVE_vldst24_base<bit writeback, bit fourregs, bits<2> stage, bits<2> size,
                       bit load, dag Oops, dag loadIops, dag wbIops,
                       string iname, string ops,
                       string cstr, list<dag> pattern=[]>
  : MVE_MI<Oops, !con(loadIops, wbIops), NoItinerary, iname, ops, cstr, size, pattern> {
  bits<4> VQd;
  bits<4> Rn;

  let Inst{31-22} = 0b1111110010;
  let Inst{21} = writeback;
  let Inst{20} = load;
  let Inst{19-16} = Rn;
  let Inst{15-13} = VQd{2-0};
  let Inst{12-9} = 0b1111;
  let Inst{8-7} = size;
  let Inst{6-5} = stage;
  let Inst{4-1} = 0b0000;
  let Inst{0} = fourregs;

  let mayLoad = load;
  let mayStore = !eq(load,0);
  let hasSideEffects = 0;
  let validForTailPredication = load;
}

// A parameter class used to encapsulate all the ways the writeback
// variants of VLD20 and friends differ from the non-writeback ones.
class MVE_vldst24_writeback<bit b, dag Oo, dag Io,
                            string sy="", string c="", string n=""> {
  bit writeback = b;
  dag Oops = Oo;
  dag Iops = Io;
  string syntax = sy;
  string cstr = c;
  string id_suffix = n;
}

// Another parameter class that encapsulates the differences between VLD2x
// and VLD4x.
class MVE_vldst24_nvecs<int n, list<int> s, bit b, RegisterOperand vl> {
  int nvecs = n;
  list<int> stages = s;
  bit bit0 = b;
  RegisterOperand VecList = vl;
}

// A third parameter class that distinguishes VLDnn.8 from .16 from .32.
class MVE_vldst24_lanesize<int i, bits<2> b> {
  int lanesize = i;
  bits<2> sizebits = b;
}

// A base class for each direction of transfer: one for load, one for
// store. I can't make these a fourth independent parametric tuple
// class, because they have to take the nvecs tuple class as a
// parameter, in order to find the right VecList operand type.

class MVE_vld24_base<MVE_vldst24_nvecs n, bits<2> pat, bits<2> size,
                     MVE_vldst24_writeback wb, string iname,
                     list<dag> pattern=[]>
  : MVE_vldst24_base<wb.writeback, n.bit0, pat, size, 1,
                     !con((outs n.VecList:$VQd), wb.Oops),
                     (ins n.VecList:$VQdSrc), wb.Iops,
                     iname, "$VQd, $Rn" # wb.syntax,
                     wb.cstr # ",$VQdSrc = $VQd", pattern>;

class MVE_vst24_base<MVE_vldst24_nvecs n, bits<2> pat, bits<2> size,
                     MVE_vldst24_writeback wb, string iname,
                     list<dag> pattern=[]>
  : MVE_vldst24_base<wb.writeback, n.bit0, pat, size, 0,
                     wb.Oops, (ins n.VecList:$VQd), wb.Iops,
                     iname, "$VQd, $Rn" # wb.syntax,
                     wb.cstr, pattern>;

// Actually define all the interleaving loads and stores, by a series
// of nested foreaches over number of vectors (VLD2/VLD4); stage
// within one of those series (VLDx0/VLDx1/VLDx2/VLDx3); size of
// vector lane; writeback or no writeback.
foreach n = [MVE_vldst24_nvecs<2, [0,1],     0, VecList2Q>,
             MVE_vldst24_nvecs<4, [0,1,2,3], 1, VecList4Q>] in
foreach stage = n.stages in
foreach s = [MVE_vldst24_lanesize< 8, 0b00>,
             MVE_vldst24_lanesize<16, 0b01>,
             MVE_vldst24_lanesize<32, 0b10>] in
foreach wb = [MVE_vldst24_writeback<
                1, (outs rGPR:$wb), (ins t2_nosp_addr_offset_none:$Rn),
                "!", "$Rn.base = $wb", "_wb">,
              MVE_vldst24_writeback<0, (outs), (ins t2_addr_offset_none:$Rn)>] in {

  // For each case within all of those foreaches, define the actual
  // instructions. The def names are made by gluing together pieces
  // from all the parameter classes, and will end up being things like
  // MVE_VLD20_8 and MVE_VST43_16_wb.

  def "MVE_VLD" # n.nvecs # stage # "_" # s.lanesize # wb.id_suffix
    : MVE_vld24_base<n, stage, s.sizebits, wb,
                     "vld" # n.nvecs # stage # "." # s.lanesize>;

  def "MVE_VST" # n.nvecs # stage # "_" # s.lanesize # wb.id_suffix
    : MVE_vst24_base<n, stage, s.sizebits, wb,
                     "vst" # n.nvecs # stage # "." # s.lanesize>;
}

def SDTARMVST2    : SDTypeProfile<1, 5, [SDTCisPtrTy<0>, SDTCisPtrTy<1>, SDTCisVT<2, i32>, SDTCisVec<3>,
                                         SDTCisSameAs<3, 4>, SDTCisVT<5, i32>]>;
def SDTARMVST4    : SDTypeProfile<1, 7, [SDTCisPtrTy<0>, SDTCisPtrTy<1>, SDTCisVT<2, i32>, SDTCisVec<3>,
                                         SDTCisSameAs<3, 4>, SDTCisSameAs<3, 5>,
                                         SDTCisSameAs<3, 6>, SDTCisVT<7, i32>]>;
def MVEVST2UPD       : SDNode<"ARMISD::VST2_UPD", SDTARMVST2, [SDNPHasChain, SDNPMemOperand]>;
def MVEVST4UPD       : SDNode<"ARMISD::VST4_UPD", SDTARMVST4, [SDNPHasChain, SDNPMemOperand]>;

multiclass MVE_vst24_patterns<int lanesize, ValueType VT> {
  foreach stage = [0,1] in
    def : Pat<(int_arm_mve_vst2q i32:$addr,
                (VT MQPR:$v0), (VT MQPR:$v1), (i32 stage)),
              (!cast<Instruction>("MVE_VST2"#stage#"_"#lanesize)
                (REG_SEQUENCE MQQPR, VT:$v0, qsub_0, VT:$v1, qsub_1),
                t2_addr_offset_none:$addr)>;
  foreach stage = [0,1] in
    def : Pat<(i32 (MVEVST2UPD i32:$addr, (i32 32),
                (VT MQPR:$v0), (VT MQPR:$v1), (i32 stage))),
              (i32 (!cast<Instruction>("MVE_VST2"#stage#"_"#lanesize#_wb)
                (REG_SEQUENCE MQQPR, VT:$v0, qsub_0, VT:$v1, qsub_1),
                t2_addr_offset_none:$addr))>;

  foreach stage = [0,1,2,3] in
    def : Pat<(int_arm_mve_vst4q i32:$addr,
                (VT MQPR:$v0), (VT MQPR:$v1),
                (VT MQPR:$v2), (VT MQPR:$v3), (i32 stage)),
              (!cast<Instruction>("MVE_VST4"#stage#"_"#lanesize)
                (REG_SEQUENCE MQQQQPR, VT:$v0, qsub_0, VT:$v1, qsub_1,
                                       VT:$v2, qsub_2, VT:$v3, qsub_3),
                t2_addr_offset_none:$addr)>;
  foreach stage = [0,1,2,3] in
    def : Pat<(i32 (MVEVST4UPD i32:$addr, (i32 64),
                (VT MQPR:$v0), (VT MQPR:$v1),
                (VT MQPR:$v2), (VT MQPR:$v3), (i32 stage))),
              (i32 (!cast<Instruction>("MVE_VST4"#stage#"_"#lanesize#_wb)
                (REG_SEQUENCE MQQQQPR, VT:$v0, qsub_0, VT:$v1, qsub_1,
                                       VT:$v2, qsub_2, VT:$v3, qsub_3),
                t2_addr_offset_none:$addr))>;
}
defm : MVE_vst24_patterns<8, v16i8>;
defm : MVE_vst24_patterns<16, v8i16>;
defm : MVE_vst24_patterns<32, v4i32>;
defm : MVE_vst24_patterns<16, v8f16>;
defm : MVE_vst24_patterns<32, v4f32>;

// end of MVE interleaving load/store

// start of MVE predicable load/store

// A parameter class for the direction of transfer.
class MVE_ldst_direction<bit b, dag Oo, dag Io, string c=""> {
  bit load = b;
  dag Oops = Oo;
  dag Iops = Io;
  string cstr = c;
}
def MVE_ld: MVE_ldst_direction<1, (outs MQPR:$Qd), (ins), ",@earlyclobber $Qd">;
def MVE_st: MVE_ldst_direction<0, (outs), (ins MQPR:$Qd)>;

// A parameter class for the size of memory access in a load.
class MVE_memsz<bits<2> e, int s, AddrMode m, string mn, list<string> types> {
  bits<2> encoding = e;         // opcode bit(s) for encoding
  int shift = s;                // shift applied to immediate load offset
  AddrMode AM = m;

  // For instruction aliases: define the complete list of type
  // suffixes at this size, and the canonical ones for loads and
  // stores.
  string MnemonicLetter = mn;
  int TypeBits = !shl(8, s);
  string CanonLoadSuffix = ".u" # TypeBits;
  string CanonStoreSuffix = "." # TypeBits;
  list<string> suffixes = !foreach(letter, types, "." # letter # TypeBits);
}

// Instances of MVE_memsz.
//
// (memD doesn't need an AddrMode, because those are only for
// contiguous loads, and memD is only used by gather/scatters.)
def MVE_memB: MVE_memsz<0b00, 0, AddrModeT2_i7,   "b", ["", "u", "s"]>;
def MVE_memH: MVE_memsz<0b01, 1, AddrModeT2_i7s2, "h", ["", "u", "s", "f"]>;
def MVE_memW: MVE_memsz<0b10, 2, AddrModeT2_i7s4, "w", ["", "u", "s", "f"]>;
def MVE_memD: MVE_memsz<0b11, 3, ?,               "d", ["", "u", "s", "f"]>;

// This is the base class for all the MVE loads and stores other than
// the interleaving ones. All the non-interleaving loads/stores share
// the characteristic that they operate on just one vector register,
// so they are VPT-predicable.
//
// The predication operand is vpred_n, for both loads and stores. For
// store instructions, the reason is obvious: if there is no output
// register, there can't be a need for an input parameter giving the
// output register's previous value. Load instructions also don't need
// that input parameter, because unlike MVE data processing
// instructions, predicated loads are defined to set the inactive
// lanes of the output register to zero, instead of preserving their
// input values.
class MVE_VLDRSTR_base<MVE_ldst_direction dir, bit U, bit P, bit W, bit opc,
                       dag oops, dag iops, string asm, string suffix,
                       string ops, string cstr, bits<2> vecsize, list<dag> pattern=[]>
 : MVE_p<oops, iops, NoItinerary, asm, suffix, ops, vpred_n, cstr, vecsize, pattern> {
  bits<3> Qd;

  let Inst{28} = U;
  let Inst{25} = 0b0;
  let Inst{24} = P;
  let Inst{22} = 0b0;
  let Inst{21} = W;
  let Inst{20} = dir.load;
  let Inst{15-13} = Qd{2-0};
  let Inst{12} = opc;
  let Inst{11-9} = 0b111;

  let mayLoad = dir.load;
  let mayStore = !eq(dir.load,0);
  let hasSideEffects = 0;
  let validForTailPredication = 1;
}

// Contiguous load and store instructions. These come in two main
// categories: same-size loads/stores in which 128 bits of vector
// register is transferred to or from 128 bits of memory in the most
// obvious way, and widening loads / narrowing stores, in which the
// size of memory accessed is less than the size of a vector register,
// so the load instructions sign- or zero-extend each memory value
// into a wider vector lane, and the store instructions truncate
// correspondingly.
//
// The instruction mnemonics for these two classes look reasonably
// similar, but the actual encodings are different enough to need two
// separate base classes.

// Contiguous, same size
class MVE_VLDRSTR_cs<MVE_ldst_direction dir, MVE_memsz memsz, bit P, bit W,
                     dag oops, dag iops, string asm, string suffix,
                     IndexMode im, string ops, string cstr>
  : MVE_VLDRSTR_base<dir, 0, P, W, 1, oops, iops, asm, suffix, ops, cstr, memsz.encoding> {
  bits<12> addr;
  let Inst{23} = addr{7};
  let Inst{19-16} = addr{11-8};
  let Inst{8-7} = memsz.encoding;
  let Inst{6-0} = addr{6-0};

  let IM = im;
}

// Contiguous, widening/narrowing
class MVE_VLDRSTR_cw<MVE_ldst_direction dir, MVE_memsz memsz, bit U,
                     bit P, bit W, bits<2> size, dag oops, dag iops,
                     string asm, string suffix, IndexMode im,
                     string ops, string cstr>
  : MVE_VLDRSTR_base<dir, U, P, W, 0, oops, iops, asm, suffix, ops, cstr, size> {
  bits<11> addr;
  let Inst{23} = addr{7};
  let Inst{19} = memsz.encoding{0}; // enough to tell 16- from 32-bit
  let Inst{18-16} = addr{10-8};
  let Inst{8-7} = size;
  let Inst{6-0} = addr{6-0};

  let IM = im;
}

// Multiclass wrapper on each of the _cw and _cs base classes, to
// generate three writeback modes (none, preindex, postindex).

multiclass MVE_VLDRSTR_cw_m<MVE_ldst_direction dir, MVE_memsz memsz,
                            string asm, string suffix, bit U, bits<2> size> {
  let AM = memsz.AM in {
    def "" : MVE_VLDRSTR_cw<
        dir, memsz, U, 1, 0, size,
        dir.Oops, !con(dir.Iops, (ins taddrmode_imm7<memsz.shift>:$addr)),
        asm, suffix, IndexModeNone, "$Qd, $addr", "">;

    def _pre : MVE_VLDRSTR_cw<
        dir, memsz, U, 1, 1, size,
        !con((outs tGPR:$wb), dir.Oops),
        !con(dir.Iops, (ins taddrmode_imm7<memsz.shift>:$addr)),
        asm, suffix, IndexModePre, "$Qd, $addr!", "$addr.base = $wb"> {
      let DecoderMethod = "DecodeMVE_MEM_1_pre<"#memsz.shift#">";
    }

    def _post : MVE_VLDRSTR_cw<
        dir, memsz, U, 0, 1, size,
        !con((outs tGPR:$wb), dir.Oops),
        !con(dir.Iops, (ins t_addr_offset_none:$Rn,
                            t2am_imm7_offset<memsz.shift>:$addr)),
        asm, suffix, IndexModePost, "$Qd, $Rn$addr", "$Rn.base = $wb"> {
      bits<4> Rn;
      let Inst{18-16} = Rn{2-0};
    }
  }
}

multiclass MVE_VLDRSTR_cs_m<MVE_ldst_direction dir, MVE_memsz memsz,
                            string asm, string suffix> {
  let AM = memsz.AM in {
    def "" : MVE_VLDRSTR_cs<
        dir, memsz, 1, 0,
        dir.Oops, !con(dir.Iops, (ins t2addrmode_imm7<memsz.shift>:$addr)),
        asm, suffix, IndexModeNone, "$Qd, $addr", "">;

    def _pre : MVE_VLDRSTR_cs<
        dir, memsz, 1, 1,
        !con((outs rGPR:$wb), dir.Oops),
        !con(dir.Iops, (ins t2addrmode_imm7_pre<memsz.shift>:$addr)),
        asm, suffix, IndexModePre, "$Qd, $addr!", "$addr.base = $wb"> {
      let DecoderMethod = "DecodeMVE_MEM_2_pre<"#memsz.shift#">";
    }

    def _post : MVE_VLDRSTR_cs<
        dir, memsz, 0, 1,
        !con((outs rGPR:$wb), dir.Oops),
        !con(dir.Iops, (ins t2_nosp_addr_offset_none:$Rn,
                            t2am_imm7_offset<memsz.shift>:$addr)),
        asm, suffix, IndexModePost, "$Qd, $Rn$addr", "$Rn.base = $wb"> {
      bits<4> Rn;
      let Inst{19-16} = Rn{3-0};
    }
  }
}

// Now actually declare all the contiguous load/stores, via those
// multiclasses. The instruction ids coming out of this are the bare
// names shown in the defm, with _pre or _post appended for writeback,
// e.g. MVE_VLDRBS16, MVE_VSTRB16_pre, MVE_VSTRHU16_post.

defm MVE_VLDRBS16: MVE_VLDRSTR_cw_m<MVE_ld, MVE_memB, "vldrb", "s16", 0, 0b01>;
defm MVE_VLDRBS32: MVE_VLDRSTR_cw_m<MVE_ld, MVE_memB, "vldrb", "s32", 0, 0b10>;
defm MVE_VLDRBU16: MVE_VLDRSTR_cw_m<MVE_ld, MVE_memB, "vldrb", "u16", 1, 0b01>;
defm MVE_VLDRBU32: MVE_VLDRSTR_cw_m<MVE_ld, MVE_memB, "vldrb", "u32", 1, 0b10>;
defm MVE_VLDRHS32: MVE_VLDRSTR_cw_m<MVE_ld, MVE_memH, "vldrh", "s32", 0, 0b10>;
defm MVE_VLDRHU32: MVE_VLDRSTR_cw_m<MVE_ld, MVE_memH, "vldrh", "u32", 1, 0b10>;

defm MVE_VLDRBU8:  MVE_VLDRSTR_cs_m<MVE_ld, MVE_memB, "vldrb", "u8">;
defm MVE_VLDRHU16: MVE_VLDRSTR_cs_m<MVE_ld, MVE_memH, "vldrh", "u16">;
defm MVE_VLDRWU32: MVE_VLDRSTR_cs_m<MVE_ld, MVE_memW, "vldrw", "u32">;

defm MVE_VSTRB16:  MVE_VLDRSTR_cw_m<MVE_st, MVE_memB, "vstrb", "16",  0, 0b01>;
defm MVE_VSTRB32:  MVE_VLDRSTR_cw_m<MVE_st, MVE_memB, "vstrb", "32",  0, 0b10>;
defm MVE_VSTRH32:  MVE_VLDRSTR_cw_m<MVE_st, MVE_memH, "vstrh", "32",  0, 0b10>;

defm MVE_VSTRBU8 : MVE_VLDRSTR_cs_m<MVE_st, MVE_memB, "vstrb", "8">;
defm MVE_VSTRHU16: MVE_VLDRSTR_cs_m<MVE_st, MVE_memH, "vstrh", "16">;
defm MVE_VSTRWU32: MVE_VLDRSTR_cs_m<MVE_st, MVE_memW, "vstrw", "32">;

// Gather loads / scatter stores whose address operand is of the form
// [Rn,Qm], i.e. a single GPR as the common base address, plus a
// vector of offset from it. ('Load/store this sequence of elements of
// the same array.')
//
// Like the contiguous family, these loads and stores can widen the
// loaded values / truncate the stored ones, or they can just
// load/store the same size of memory and vector lane. But unlike the
// contiguous family, there's no particular difference in encoding
// between those two cases.
//
// This family also comes with the option to scale the offset values
// in Qm by the size of the loaded memory (i.e. to treat them as array
// indices), or not to scale them (to treat them as plain byte offsets
// in memory, so that perhaps the loaded values are unaligned). The
// scaled instructions' address operand in assembly looks like
// [Rn,Qm,UXTW #2] or similar.

// Base class.
class MVE_VLDRSTR_rq<MVE_ldst_direction dir, MVE_memsz memsz, bit U,
                     bits<2> size, bit os, string asm, string suffix, int shift>
  : MVE_VLDRSTR_base<dir, U, 0b0, 0b0, 0, dir.Oops,
                     !con(dir.Iops, (ins mve_addr_rq_shift<shift>:$addr)),
                     asm, suffix, "$Qd, $addr", dir.cstr, size> {
  bits<7> addr;
  let Inst{23} = 0b1;
  let Inst{19-16} = addr{6-3};
  let Inst{8-7} = size;
  let Inst{6} = memsz.encoding{1};
  let Inst{5} = 0;
  let Inst{4} = memsz.encoding{0};
  let Inst{3-1} = addr{2-0};
  let Inst{0} = os;
}

// Multiclass that defines the scaled and unscaled versions of an
// instruction, when the memory size is wider than a byte. The scaled
// version gets the default name like MVE_VLDRBU16_rq; the unscaled /
// potentially unaligned version gets a "_u" suffix, e.g.
// MVE_VLDRBU16_rq_u.
multiclass MVE_VLDRSTR_rq_w<MVE_ldst_direction dir, MVE_memsz memsz,
                            string asm, string suffix, bit U, bits<2> size> {
  def _u : MVE_VLDRSTR_rq<dir, memsz, U, size, 0, asm, suffix, 0>;
  def "" : MVE_VLDRSTR_rq<dir, memsz, U, size, 1, asm, suffix, memsz.shift>;
}

// Subclass of MVE_VLDRSTR_rq with the same API as that multiclass,
// for use when the memory size is one byte, so there's no 'scaled'
// version of the instruction at all. (This is encoded as if it were
// unscaled, but named in the default way with no _u suffix.)
class MVE_VLDRSTR_rq_b<MVE_ldst_direction dir, MVE_memsz memsz,
                       string asm, string suffix, bit U, bits<2> size>
  : MVE_VLDRSTR_rq<dir, memsz, U, size, 0, asm, suffix, 0>;

// Multiclasses wrapping that to add ISel patterns for intrinsics.
multiclass MVE_VLDR_rq_w<MVE_memsz memsz, list<MVEVectorVTInfo> VTIs> {
  defm "": MVE_VLDRSTR_rq_w<MVE_ld, memsz, "vldr" # memsz.MnemonicLetter,
                            VTIs[0].Suffix, VTIs[0].Unsigned, VTIs[0].Size>;
  defvar Inst = !cast<Instruction>(NAME);
  defvar InstU = !cast<Instruction>(NAME # "_u");

  foreach VTI = VTIs in
  foreach UnsignedFlag = !if(!eq(VTI.Size, memsz.encoding),
                             [0,1], [VTI.Unsigned]) in {
    def : Pat<(VTI.Vec (int_arm_mve_vldr_gather_offset GPR:$base, (VTIs[0].Vec MQPR:$offsets), memsz.TypeBits, 0, UnsignedFlag)),
              (VTI.Vec (InstU GPR:$base, MQPR:$offsets))>;
    def : Pat<(VTI.Vec (int_arm_mve_vldr_gather_offset GPR:$base, (VTIs[0].Vec MQPR:$offsets), memsz.TypeBits, memsz.shift, UnsignedFlag)),
              (VTI.Vec (Inst GPR:$base, MQPR:$offsets))>;
    def : Pat<(VTI.Vec (int_arm_mve_vldr_gather_offset_predicated GPR:$base, (VTIs[0].Vec MQPR:$offsets), memsz.TypeBits, 0, UnsignedFlag, (VTI.Pred VCCR:$pred))),
              (VTI.Vec (InstU GPR:$base, MQPR:$offsets, ARMVCCThen, VCCR:$pred, zero_reg))>;
    def : Pat<(VTI.Vec (int_arm_mve_vldr_gather_offset_predicated GPR:$base, (VTIs[0].Vec MQPR:$offsets), memsz.TypeBits, memsz.shift, UnsignedFlag, (VTI.Pred VCCR:$pred))),
              (VTI.Vec (Inst GPR:$base, MQPR:$offsets, ARMVCCThen, VCCR:$pred, zero_reg))>;
  }
}
multiclass MVE_VLDR_rq_b<list<MVEVectorVTInfo> VTIs> {
  def "": MVE_VLDRSTR_rq_b<MVE_ld, MVE_memB, "vldrb",
                           VTIs[0].Suffix, VTIs[0].Unsigned, VTIs[0].Size>;
  defvar Inst = !cast<Instruction>(NAME);

  foreach VTI = VTIs in {
    def : Pat<(VTI.Vec (int_arm_mve_vldr_gather_offset GPR:$base, (VTIs[0].Vec MQPR:$offsets), 8, 0, VTI.Unsigned)),
              (VTI.Vec (Inst GPR:$base, MQPR:$offsets))>;
    def : Pat<(VTI.Vec (int_arm_mve_vldr_gather_offset_predicated GPR:$base, (VTIs[0].Vec MQPR:$offsets), 8, 0, VTI.Unsigned, (VTI.Pred VCCR:$pred))),
              (VTI.Vec (Inst GPR:$base, MQPR:$offsets, ARMVCCThen, VCCR:$pred, zero_reg))>;
  }
}
multiclass MVE_VSTR_rq_w<MVE_memsz memsz, list<MVEVectorVTInfo> VTIs> {
  defm "": MVE_VLDRSTR_rq_w<MVE_st, memsz, "vstr" # memsz.MnemonicLetter,
                            VTIs[0].BitsSuffix, 0, VTIs[0].Size>;
  defvar Inst = !cast<Instruction>(NAME);
  defvar InstU = !cast<Instruction>(NAME # "_u");

  foreach VTI = VTIs in {
    def : Pat<(int_arm_mve_vstr_scatter_offset GPR:$base, (VTIs[0].Vec MQPR:$offsets), (VTI.Vec MQPR:$data), memsz.TypeBits, 0),
              (InstU MQPR:$data, GPR:$base, MQPR:$offsets)>;
    def : Pat<(int_arm_mve_vstr_scatter_offset GPR:$base, (VTIs[0].Vec MQPR:$offsets), (VTI.Vec MQPR:$data), memsz.TypeBits, memsz.shift),
              (Inst MQPR:$data, GPR:$base, MQPR:$offsets)>;
    def : Pat<(int_arm_mve_vstr_scatter_offset_predicated GPR:$base, (VTIs[0].Vec MQPR:$offsets), (VTI.Vec MQPR:$data), memsz.TypeBits, 0, (VTI.Pred VCCR:$pred)),
              (InstU MQPR:$data, GPR:$base, MQPR:$offsets, ARMVCCThen, VCCR:$pred, zero_reg)>;
    def : Pat<(int_arm_mve_vstr_scatter_offset_predicated GPR:$base, (VTIs[0].Vec MQPR:$offsets), (VTI.Vec MQPR:$data), memsz.TypeBits, memsz.shift, (VTI.Pred VCCR:$pred)),
              (Inst MQPR:$data, GPR:$base, MQPR:$offsets, ARMVCCThen, VCCR:$pred, zero_reg)>;
  }
}
multiclass MVE_VSTR_rq_b<list<MVEVectorVTInfo> VTIs> {
  def "": MVE_VLDRSTR_rq_b<MVE_st, MVE_memB, "vstrb",
                           VTIs[0].BitsSuffix, 0, VTIs[0].Size>;
  defvar Inst = !cast<Instruction>(NAME);

  foreach VTI = VTIs in {
    def : Pat<(int_arm_mve_vstr_scatter_offset GPR:$base, (VTIs[0].Vec MQPR:$offsets), (VTI.Vec MQPR:$data), 8, 0),
              (Inst MQPR:$data, GPR:$base, MQPR:$offsets)>;
    def : Pat<(int_arm_mve_vstr_scatter_offset_predicated GPR:$base, (VTIs[0].Vec MQPR:$offsets), (VTI.Vec MQPR:$data), 8, 0, (VTI.Pred VCCR:$pred)),
              (Inst MQPR:$data, GPR:$base, MQPR:$offsets, ARMVCCThen, VCCR:$pred, zero_reg)>;
  }
}

// Actually define all the loads and stores in this family.

defm MVE_VLDRBU8_rq : MVE_VLDR_rq_b<[MVE_v16u8,MVE_v16s8]>;
defm MVE_VLDRBU16_rq: MVE_VLDR_rq_b<[MVE_v8u16]>;
defm MVE_VLDRBS16_rq: MVE_VLDR_rq_b<[MVE_v8s16]>;
defm MVE_VLDRBU32_rq: MVE_VLDR_rq_b<[MVE_v4u32]>;
defm MVE_VLDRBS32_rq: MVE_VLDR_rq_b<[MVE_v4s32]>;

defm MVE_VLDRHU16_rq: MVE_VLDR_rq_w<MVE_memH, [MVE_v8u16,MVE_v8s16,MVE_v8f16]>;
defm MVE_VLDRHU32_rq: MVE_VLDR_rq_w<MVE_memH, [MVE_v4u32]>;
defm MVE_VLDRHS32_rq: MVE_VLDR_rq_w<MVE_memH, [MVE_v4s32]>;
defm MVE_VLDRWU32_rq: MVE_VLDR_rq_w<MVE_memW, [MVE_v4u32,MVE_v4s32,MVE_v4f32]>;
defm MVE_VLDRDU64_rq: MVE_VLDR_rq_w<MVE_memD, [MVE_v2u64,MVE_v2s64]>;

defm MVE_VSTRB8_rq  : MVE_VSTR_rq_b<[MVE_v16i8]>;
defm MVE_VSTRB16_rq : MVE_VSTR_rq_b<[MVE_v8i16]>;
defm MVE_VSTRB32_rq : MVE_VSTR_rq_b<[MVE_v4i32]>;

defm MVE_VSTRH16_rq : MVE_VSTR_rq_w<MVE_memH, [MVE_v8i16,MVE_v8f16]>;
defm MVE_VSTRH32_rq : MVE_VSTR_rq_w<MVE_memH, [MVE_v4i32]>;
defm MVE_VSTRW32_rq : MVE_VSTR_rq_w<MVE_memW, [MVE_v4i32,MVE_v4f32]>;
defm MVE_VSTRD64_rq : MVE_VSTR_rq_w<MVE_memD, [MVE_v2i64]>;

// Gather loads / scatter stores whose address operand is of the form
// [Qm,#imm], i.e. a vector containing a full base address for each
// loaded item, plus an immediate offset applied consistently to all
// of them. ('Load/store the same field from this vector of pointers
// to a structure type.')
//
// This family requires the vector lane size to be at least 32 bits
// (so there's room for an address in each lane at all). It has no
// widening/narrowing variants. But it does support preindex
// writeback, in which the address vector is updated to hold the
// addresses actually loaded from.

// Base class.
class MVE_VLDRSTR_qi<MVE_ldst_direction dir, MVE_memsz memsz, bit W, dag wbops,
                     string asm, string wbAsm, string suffix, string cstr = "">
  : MVE_VLDRSTR_base<dir, 1, 1, W, 1, !con(wbops, dir.Oops),
                     !con(dir.Iops, (ins mve_addr_q_shift<memsz.shift>:$addr)),
                     asm, suffix, "$Qd, $addr" # wbAsm, cstr # dir.cstr, memsz.encoding> {
  bits<11> addr;
  let Inst{23} = addr{7};
  let Inst{19-17} = addr{10-8};
  let Inst{16} = 0;
  let Inst{8} = memsz.encoding{0}; // enough to distinguish 32- from 64-bit
  let Inst{7} = 0;
  let Inst{6-0} = addr{6-0};
}

// Multiclass that generates the non-writeback and writeback variants.
multiclass MVE_VLDRSTR_qi_m<MVE_ldst_direction dir, MVE_memsz memsz,
                            string asm, string suffix> {
  def ""   : MVE_VLDRSTR_qi<dir, memsz, 0, (outs),          asm, "",  suffix>;
  def _pre : MVE_VLDRSTR_qi<dir, memsz, 1, (outs MQPR:$wb), asm, "!", suffix,
                            "$addr.base = $wb"> {
    let DecoderMethod="DecodeMVE_MEM_3_pre<"#memsz.shift#">";
  }
}

// Multiclasses wrapping that one, adding selection patterns for the
// non-writeback loads and all the stores. (The writeback loads must
// deliver multiple output values, so they have to be selected by C++
// code.)
multiclass MVE_VLDR_qi<MVE_memsz memsz, MVEVectorVTInfo AVTI,
                       list<MVEVectorVTInfo> DVTIs> {
  defm "" : MVE_VLDRSTR_qi_m<MVE_ld, memsz, "vldr" # memsz.MnemonicLetter,
                             "u" # memsz.TypeBits>;
  defvar Inst = !cast<Instruction>(NAME);

  foreach DVTI = DVTIs in {
    def : Pat<(DVTI.Vec (int_arm_mve_vldr_gather_base
                 (AVTI.Vec MQPR:$addr), (i32 imm:$offset))),
              (DVTI.Vec (Inst (AVTI.Vec MQPR:$addr), (i32 imm:$offset)))>;
    def : Pat<(DVTI.Vec (int_arm_mve_vldr_gather_base_predicated
                 (AVTI.Vec MQPR:$addr), (i32 imm:$offset), (AVTI.Pred VCCR:$pred))),
              (DVTI.Vec (Inst (AVTI.Vec MQPR:$addr), (i32 imm:$offset),
                        ARMVCCThen, VCCR:$pred, zero_reg))>;
  }
}
multiclass MVE_VSTR_qi<MVE_memsz memsz, MVEVectorVTInfo AVTI,
                       list<MVEVectorVTInfo> DVTIs> {
  defm "" : MVE_VLDRSTR_qi_m<MVE_st, memsz, "vstr" # memsz.MnemonicLetter,
                             !cast<string>(memsz.TypeBits)>;
  defvar Inst = !cast<Instruction>(NAME);
  defvar InstPre = !cast<Instruction>(NAME # "_pre");

  foreach DVTI = DVTIs in {
    def : Pat<(int_arm_mve_vstr_scatter_base
                (AVTI.Vec MQPR:$addr), (i32 imm:$offset), (DVTI.Vec MQPR:$data)),
              (Inst (DVTI.Vec MQPR:$data), (AVTI.Vec MQPR:$addr),
                    (i32 imm:$offset))>;
    def : Pat<(int_arm_mve_vstr_scatter_base_predicated
                (AVTI.Vec MQPR:$addr), (i32 imm:$offset), (DVTI.Vec MQPR:$data), (AVTI.Pred VCCR:$pred)),
              (Inst (DVTI.Vec MQPR:$data), (AVTI.Vec MQPR:$addr),
                    (i32 imm:$offset), ARMVCCThen, VCCR:$pred, zero_reg)>;
    def : Pat<(AVTI.Vec (int_arm_mve_vstr_scatter_base_wb
                (AVTI.Vec MQPR:$addr), (i32 imm:$offset), (DVTI.Vec MQPR:$data))),
              (AVTI.Vec (InstPre (DVTI.Vec MQPR:$data), (AVTI.Vec MQPR:$addr),
                                 (i32 imm:$offset)))>;
    def : Pat<(AVTI.Vec (int_arm_mve_vstr_scatter_base_wb_predicated
                (AVTI.Vec MQPR:$addr), (i32 imm:$offset), (DVTI.Vec MQPR:$data), (AVTI.Pred VCCR:$pred))),
              (AVTI.Vec (InstPre (DVTI.Vec MQPR:$data), (AVTI.Vec MQPR:$addr),
                                 (i32 imm:$offset), ARMVCCThen, VCCR:$pred, zero_reg))>;
  }
}

// Actual instruction definitions.
defm MVE_VLDRWU32_qi: MVE_VLDR_qi<MVE_memW, MVE_v4i32, [MVE_v4i32,MVE_v4f32]>;
defm MVE_VLDRDU64_qi: MVE_VLDR_qi<MVE_memD, MVE_v2i64, [MVE_v2i64,MVE_v2f64]>;
defm MVE_VSTRW32_qi:  MVE_VSTR_qi<MVE_memW, MVE_v4i32, [MVE_v4i32,MVE_v4f32]>;
defm MVE_VSTRD64_qi:  MVE_VSTR_qi<MVE_memD, MVE_v2i64, [MVE_v2i64,MVE_v2f64]>;

// Define aliases for all the instructions where memory size and
// vector lane size are the same. These are mnemonic aliases, so they
// apply consistently across all of the above families - contiguous
// loads, and both the rq and qi types of gather/scatter.
//
// Rationale: As long as you're loading (for example) 16-bit memory
// values into 16-bit vector lanes, you can think of them as signed or
// unsigned integers, fp16 or just raw 16-bit blobs and it makes no
// difference. So we permit all of vldrh.16, vldrh.u16, vldrh.s16,
// vldrh.f16 and treat them all as equivalent to the canonical
// spelling (which happens to be .u16 for loads, and just .16 for
// stores).

foreach vpt_cond = ["", "t", "e"] in
foreach memsz = [MVE_memB, MVE_memH, MVE_memW, MVE_memD] in
foreach suffix = memsz.suffixes in {
  // Define an alias with every suffix in the list, except for the one
  // used by the real Instruction record (i.e. the one that all the
  // rest are aliases *for*).

  if !ne(suffix, memsz.CanonLoadSuffix) then {
    def : MnemonicAlias<
      "vldr" # memsz.MnemonicLetter # vpt_cond # suffix,
      "vldr" # memsz.MnemonicLetter # vpt_cond # memsz.CanonLoadSuffix>;
  }

  if !ne(suffix, memsz.CanonStoreSuffix) then {
    def : MnemonicAlias<
      "vstr" # memsz.MnemonicLetter # vpt_cond # suffix,
      "vstr" # memsz.MnemonicLetter # vpt_cond # memsz.CanonStoreSuffix>;
  }
}

// end of MVE predicable load/store

class MVE_VPT<string suffix, bits<2> size, dag iops, string asm, list<dag> pattern=[]>
  : MVE_MI<(outs ), iops, NoItinerary, !strconcat("vpt", "${Mk}", ".", suffix), asm, "", size, pattern> {
  bits<3> fc;
  bits<4> Mk;
  bits<3> Qn;

  let Inst{31-23} = 0b111111100;
  let Inst{22} = Mk{3};
  let Inst{21-20} = size;
  let Inst{19-17} = Qn{2-0};
  let Inst{16} = 0b1;
  let Inst{15-13} = Mk{2-0};
  let Inst{12} = fc{2};
  let Inst{11-8} = 0b1111;
  let Inst{7} = fc{0};
  let Inst{4} = 0b0;

  let Defs = [VPR];
  let validForTailPredication=1;
}

class MVE_VPTt1<string suffix, bits<2> size, dag iops>
  : MVE_VPT<suffix, size, iops, "$fc, $Qn, $Qm"> {
  bits<4> Qm;
  bits<4> Mk;

  let Inst{6} = 0b0;
  let Inst{5} = Qm{3};
  let Inst{3-1} = Qm{2-0};
  let Inst{0} = fc{1};
}

class MVE_VPTt1i<string suffix, bits<2> size>
 : MVE_VPTt1<suffix, size,
           (ins vpt_mask:$Mk, MQPR:$Qn, MQPR:$Qm, pred_basic_i:$fc)> {
  let Inst{12} = 0b0;
  let Inst{0} = 0b0;
}

def MVE_VPTv4i32 : MVE_VPTt1i<"i32", 0b10>;
def MVE_VPTv8i16 : MVE_VPTt1i<"i16", 0b01>;
def MVE_VPTv16i8 : MVE_VPTt1i<"i8", 0b00>;

class MVE_VPTt1u<string suffix, bits<2> size>
 : MVE_VPTt1<suffix, size,
           (ins vpt_mask:$Mk, MQPR:$Qn, MQPR:$Qm, pred_basic_u:$fc)> {
  let Inst{12} = 0b0;
  let Inst{0} = 0b1;
}

def MVE_VPTv4u32 : MVE_VPTt1u<"u32", 0b10>;
def MVE_VPTv8u16 : MVE_VPTt1u<"u16", 0b01>;
def MVE_VPTv16u8 : MVE_VPTt1u<"u8", 0b00>;

class MVE_VPTt1s<string suffix, bits<2> size>
 : MVE_VPTt1<suffix, size,
           (ins vpt_mask:$Mk, MQPR:$Qn, MQPR:$Qm, pred_basic_s:$fc)> {
  let Inst{12} = 0b1;
}

def MVE_VPTv4s32 : MVE_VPTt1s<"s32", 0b10>;
def MVE_VPTv8s16 : MVE_VPTt1s<"s16", 0b01>;
def MVE_VPTv16s8 : MVE_VPTt1s<"s8", 0b00>;

class MVE_VPTt2<string suffix, bits<2> size, dag iops>
  : MVE_VPT<suffix, size, iops,
          "$fc, $Qn, $Rm"> {
  bits<4> Rm;
  bits<3> fc;
  bits<4> Mk;

  let Inst{6} = 0b1;
  let Inst{5} = fc{1};
  let Inst{3-0} = Rm{3-0};
}

class MVE_VPTt2i<string suffix, bits<2> size>
  : MVE_VPTt2<suffix, size,
            (ins vpt_mask:$Mk, MQPR:$Qn, GPRwithZR:$Rm, pred_basic_i:$fc)> {
  let Inst{12} = 0b0;
  let Inst{5} = 0b0;
}

def MVE_VPTv4i32r : MVE_VPTt2i<"i32", 0b10>;
def MVE_VPTv8i16r : MVE_VPTt2i<"i16", 0b01>;
def MVE_VPTv16i8r : MVE_VPTt2i<"i8", 0b00>;

class MVE_VPTt2u<string suffix, bits<2> size>
  : MVE_VPTt2<suffix, size,
            (ins vpt_mask:$Mk, MQPR:$Qn, GPRwithZR:$Rm, pred_basic_u:$fc)> {
  let Inst{12} = 0b0;
  let Inst{5} = 0b1;
}

def MVE_VPTv4u32r : MVE_VPTt2u<"u32", 0b10>;
def MVE_VPTv8u16r : MVE_VPTt2u<"u16", 0b01>;
def MVE_VPTv16u8r : MVE_VPTt2u<"u8", 0b00>;

class MVE_VPTt2s<string suffix, bits<2> size>
  : MVE_VPTt2<suffix, size,
            (ins vpt_mask:$Mk, MQPR:$Qn, GPRwithZR:$Rm, pred_basic_s:$fc)> {
  let Inst{12} = 0b1;
}

def MVE_VPTv4s32r : MVE_VPTt2s<"s32", 0b10>;
def MVE_VPTv8s16r : MVE_VPTt2s<"s16", 0b01>;
def MVE_VPTv16s8r : MVE_VPTt2s<"s8", 0b00>;


class MVE_VPTf<string suffix, bit size, dag iops, string asm, list<dag> pattern=[]>
  : MVE_MI<(outs ), iops, NoItinerary, !strconcat("vpt", "${Mk}", ".", suffix), asm,
            "", !if(size, 0b01, 0b10), pattern> {
  bits<3> fc;
  bits<4> Mk;
  bits<3> Qn;

  let Inst{31-29} = 0b111;
  let Inst{28} = size;
  let Inst{27-23} = 0b11100;
  let Inst{22} = Mk{3};
  let Inst{21-20} = 0b11;
  let Inst{19-17} = Qn{2-0};
  let Inst{16} = 0b1;
  let Inst{15-13} = Mk{2-0};
  let Inst{12} = fc{2};
  let Inst{11-8} = 0b1111;
  let Inst{7} = fc{0};
  let Inst{4} = 0b0;

  let Defs = [VPR];
  let Predicates = [HasMVEFloat];
  let validForTailPredication=1;
}

class MVE_VPTft1<string suffix, bit size>
  : MVE_VPTf<suffix, size, (ins vpt_mask:$Mk, MQPR:$Qn, MQPR:$Qm, pred_basic_fp:$fc),
          "$fc, $Qn, $Qm"> {
  bits<3> fc;
  bits<4> Qm;

  let Inst{6} = 0b0;
  let Inst{5} = Qm{3};
  let Inst{3-1} = Qm{2-0};
  let Inst{0} = fc{1};
}

def MVE_VPTv4f32         : MVE_VPTft1<"f32", 0b0>;
def MVE_VPTv8f16         : MVE_VPTft1<"f16", 0b1>;

class MVE_VPTft2<string suffix, bit size>
  : MVE_VPTf<suffix, size, (ins vpt_mask:$Mk, MQPR:$Qn, GPRwithZR:$Rm, pred_basic_fp:$fc),
          "$fc, $Qn, $Rm"> {
  bits<3> fc;
  bits<4> Rm;

  let Inst{6} = 0b1;
  let Inst{5} = fc{1};
  let Inst{3-0} = Rm{3-0};
}

def MVE_VPTv4f32r        : MVE_VPTft2<"f32", 0b0>;
def MVE_VPTv8f16r        : MVE_VPTft2<"f16", 0b1>;

def MVE_VPST : MVE_MI<(outs ), (ins vpt_mask:$Mk), NoItinerary,
       !strconcat("vpst", "${Mk}"), "", "", 0b00, []> {
  bits<4> Mk;

  let Inst{31-23} = 0b111111100;
  let Inst{22} = Mk{3};
  let Inst{21-16} = 0b110001;
  let Inst{15-13} = Mk{2-0};
  let Inst{12-0} = 0b0111101001101;
  let Unpredictable{12} = 0b1;
  let Unpredictable{7} = 0b1;
  let Unpredictable{5} = 0b1;

  let Uses = [VPR];
  let validForTailPredication = 1;
}

def MVE_VPSEL : MVE_p<(outs MQPR:$Qd), (ins MQPR:$Qn, MQPR:$Qm), NoItinerary,
                      "vpsel", "", "$Qd, $Qn, $Qm", vpred_n, "", 0b00, []> {
  bits<4> Qn;
  bits<4> Qd;
  bits<4> Qm;

  let Inst{28} = 0b1;
  let Inst{25-23} = 0b100;
  let Inst{22} = Qd{3};
  let Inst{21-20} = 0b11;
  let Inst{19-17} = Qn{2-0};
  let Inst{16} = 0b1;
  let Inst{15-13} = Qd{2-0};
  let Inst{12-9} = 0b0111;
  let Inst{8} = 0b1;
  let Inst{7} = Qn{3};
  let Inst{6} = 0b0;
  let Inst{5} = Qm{3};
  let Inst{4} = 0b0;
  let Inst{3-1} = Qm{2-0};
  let Inst{0} = 0b1;
}

foreach suffix = ["s8", "s16", "s32", "u8", "u16", "u32",
                  "i8", "i16", "i32",       "f16", "f32"] in
def : MVEInstAlias<"vpsel${vp}." # suffix # "\t$Qd, $Qn, $Qm",
                   (MVE_VPSEL MQPR:$Qd, MQPR:$Qn, MQPR:$Qm, vpred_n:$vp)>;

let Predicates = [HasMVEInt] in {
  def : Pat<(v16i8 (vselect (v16i1 VCCR:$pred), (v16i8 MQPR:$v1), (v16i8 MQPR:$v2))),
            (v16i8 (MVE_VPSEL MQPR:$v1, MQPR:$v2, ARMVCCNone, VCCR:$pred, zero_reg))>;
  def : Pat<(v8i16 (vselect (v8i1 VCCR:$pred), (v8i16 MQPR:$v1), (v8i16 MQPR:$v2))),
            (v8i16 (MVE_VPSEL MQPR:$v1, MQPR:$v2, ARMVCCNone, VCCR:$pred, zero_reg))>;
  def : Pat<(v4i32 (vselect (v4i1 VCCR:$pred), (v4i32 MQPR:$v1), (v4i32 MQPR:$v2))),
            (v4i32 (MVE_VPSEL MQPR:$v1, MQPR:$v2, ARMVCCNone, VCCR:$pred, zero_reg))>;
  def : Pat<(v2i64 (vselect (v2i1 VCCR:$pred), (v2i64 MQPR:$v1), (v2i64 MQPR:$v2))),
            (v2i64 (MVE_VPSEL MQPR:$v1, MQPR:$v2, ARMVCCNone, VCCR:$pred, zero_reg))>;

  def : Pat<(v8f16 (vselect (v8i1 VCCR:$pred), (v8f16 MQPR:$v1), (v8f16 MQPR:$v2))),
            (v8f16 (MVE_VPSEL MQPR:$v1, MQPR:$v2, ARMVCCNone, VCCR:$pred, zero_reg))>;
  def : Pat<(v4f32 (vselect (v4i1 VCCR:$pred), (v4f32 MQPR:$v1), (v4f32 MQPR:$v2))),
            (v4f32 (MVE_VPSEL MQPR:$v1, MQPR:$v2, ARMVCCNone, VCCR:$pred, zero_reg))>;
  def : Pat<(v2f64 (vselect (v2i1 VCCR:$pred), (v2f64 MQPR:$v1), (v2f64 MQPR:$v2))),
            (v2f64 (MVE_VPSEL MQPR:$v1, MQPR:$v2, ARMVCCNone, VCCR:$pred, zero_reg))>;

  def : Pat<(v16i8 (vselect (v16i8 MQPR:$pred), (v16i8 MQPR:$v1), (v16i8 MQPR:$v2))),
            (v16i8 (MVE_VPSEL MQPR:$v1, MQPR:$v2, ARMVCCNone,
                              (MVE_VCMPi8 (v16i8 MQPR:$pred), (MVE_VMOVimmi8 0), ARMCCne), zero_reg))>;
  def : Pat<(v8i16 (vselect (v8i16 MQPR:$pred), (v8i16 MQPR:$v1), (v8i16 MQPR:$v2))),
            (v8i16 (MVE_VPSEL MQPR:$v1, MQPR:$v2, ARMVCCNone,
                              (MVE_VCMPi16 (v8i16 MQPR:$pred), (MVE_VMOVimmi16 0), ARMCCne), zero_reg))>;
  def : Pat<(v4i32 (vselect (v4i32 MQPR:$pred), (v4i32 MQPR:$v1), (v4i32 MQPR:$v2))),
            (v4i32 (MVE_VPSEL MQPR:$v1, MQPR:$v2, ARMVCCNone,
                              (MVE_VCMPi32 (v4i32 MQPR:$pred), (MVE_VMOVimmi32 0), ARMCCne), zero_reg))>;

  def : Pat<(v8f16 (vselect (v8i16 MQPR:$pred), (v8f16 MQPR:$v1), (v8f16 MQPR:$v2))),
            (v8f16 (MVE_VPSEL MQPR:$v1, MQPR:$v2, ARMVCCNone,
                              (MVE_VCMPi16 (v8i16 MQPR:$pred), (MVE_VMOVimmi16 0), ARMCCne), zero_reg))>;
  def : Pat<(v4f32 (vselect (v4i32 MQPR:$pred), (v4f32 MQPR:$v1), (v4f32 MQPR:$v2))),
            (v4f32 (MVE_VPSEL MQPR:$v1, MQPR:$v2, ARMVCCNone,
                              (MVE_VCMPi32 (v4i32 MQPR:$pred), (MVE_VMOVimmi32 0), ARMCCne), zero_reg))>;

  // Pred <-> Int
  def : Pat<(v16i8 (zext  (v16i1 VCCR:$pred))),
            (v16i8 (MVE_VPSEL (MVE_VMOVimmi8 1), (MVE_VMOVimmi8 0), ARMVCCNone, VCCR:$pred, zero_reg))>;
  def : Pat<(v8i16 (zext  (v8i1  VCCR:$pred))),
            (v8i16 (MVE_VPSEL (MVE_VMOVimmi16 1), (MVE_VMOVimmi16 0), ARMVCCNone, VCCR:$pred, zero_reg))>;
  def : Pat<(v4i32 (zext  (v4i1  VCCR:$pred))),
            (v4i32 (MVE_VPSEL (MVE_VMOVimmi32 1), (MVE_VMOVimmi32 0), ARMVCCNone, VCCR:$pred, zero_reg))>;
  def : Pat<(v2i64 (zext  (v2i1  VCCR:$pred))),
            (v2i64 (MVE_VPSEL (MVE_VMOVimmi64 1), (MVE_VMOVimmi32 0), ARMVCCNone, VCCR:$pred, zero_reg))>;

  def : Pat<(v16i8 (sext  (v16i1 VCCR:$pred))),
            (v16i8 (MVE_VPSEL (MVE_VMOVimmi8 255), (MVE_VMOVimmi8 0), ARMVCCNone, VCCR:$pred, zero_reg))>;
  def : Pat<(v8i16 (sext  (v8i1  VCCR:$pred))),
            (v8i16 (MVE_VPSEL (MVE_VMOVimmi8 255), (MVE_VMOVimmi16 0), ARMVCCNone, VCCR:$pred, zero_reg))>;
  def : Pat<(v4i32 (sext  (v4i1  VCCR:$pred))),
            (v4i32 (MVE_VPSEL (MVE_VMOVimmi8 255), (MVE_VMOVimmi32 0), ARMVCCNone, VCCR:$pred, zero_reg))>;
  def : Pat<(v2i64 (sext  (v2i1  VCCR:$pred))),
            (v2i64 (MVE_VPSEL (MVE_VMOVimmi8 255), (MVE_VMOVimmi32 0), ARMVCCNone, VCCR:$pred, zero_reg))>;

  def : Pat<(v16i8 (anyext  (v16i1 VCCR:$pred))),
            (v16i8 (MVE_VPSEL (MVE_VMOVimmi8 1), (MVE_VMOVimmi8 0), ARMVCCNone, VCCR:$pred, zero_reg))>;
  def : Pat<(v8i16 (anyext  (v8i1  VCCR:$pred))),
            (v8i16 (MVE_VPSEL (MVE_VMOVimmi16 1), (MVE_VMOVimmi16 0), ARMVCCNone, VCCR:$pred, zero_reg))>;
  def : Pat<(v4i32 (anyext  (v4i1  VCCR:$pred))),
            (v4i32 (MVE_VPSEL (MVE_VMOVimmi32 1), (MVE_VMOVimmi32 0), ARMVCCNone, VCCR:$pred, zero_reg))>;
  def : Pat<(v2i64 (anyext  (v2i1  VCCR:$pred))),
            (v2i64 (MVE_VPSEL (MVE_VMOVimmi64 1), (MVE_VMOVimmi32 0), ARMVCCNone, VCCR:$pred, zero_reg))>;
}

let Predicates = [HasMVEFloat] in {
  // Pred <-> Float
  // 112 is 1.0 in float
  def : Pat<(v4f32 (uint_to_fp (v4i1 VCCR:$pred))),
            (v4f32 (MVE_VPSEL (v4f32 (MVE_VMOVimmf32 112)), (v4f32 (MVE_VMOVimmi32 0)), ARMVCCNone, VCCR:$pred, zero_reg))>;
  // 2620 in 1.0 in half
  def : Pat<(v8f16 (uint_to_fp (v8i1 VCCR:$pred))),
            (v8f16 (MVE_VPSEL (v8f16 (MVE_VMOVimmi16 2620)), (v8f16 (MVE_VMOVimmi16 0)), ARMVCCNone, VCCR:$pred, zero_reg))>;
  // 240 is -1.0 in float
  def : Pat<(v4f32 (sint_to_fp (v4i1 VCCR:$pred))),
            (v4f32 (MVE_VPSEL (v4f32 (MVE_VMOVimmf32 240)), (v4f32 (MVE_VMOVimmi32 0)), ARMVCCNone, VCCR:$pred, zero_reg))>;
  // 2748 is -1.0 in half
  def : Pat<(v8f16 (sint_to_fp (v8i1 VCCR:$pred))),
            (v8f16 (MVE_VPSEL (v8f16 (MVE_VMOVimmi16 2748)), (v8f16 (MVE_VMOVimmi16 0)), ARMVCCNone, VCCR:$pred, zero_reg))>;

  def : Pat<(v4i1 (fp_to_uint (v4f32 MQPR:$v1))),
            (v4i1 (MVE_VCMPf32r (v4f32 MQPR:$v1), ZR, ARMCCne))>;
  def : Pat<(v8i1 (fp_to_uint (v8f16 MQPR:$v1))),
            (v8i1 (MVE_VCMPf16r (v8f16 MQPR:$v1), ZR, ARMCCne))>;
  def : Pat<(v4i1 (fp_to_sint (v4f32 MQPR:$v1))),
            (v4i1 (MVE_VCMPf32r (v4f32 MQPR:$v1), ZR, ARMCCne))>;
  def : Pat<(v8i1 (fp_to_sint (v8f16 MQPR:$v1))),
            (v8i1 (MVE_VCMPf16r (v8f16 MQPR:$v1), ZR, ARMCCne))>;
}

def MVE_VPNOT : MVE_p<(outs VCCR:$P0), (ins VCCR:$P0_in), NoItinerary,
                      "vpnot", "", "", vpred_n, "", 0b00, []> {
  let Inst{31-0} = 0b11111110001100010000111101001101;
  let Unpredictable{19-17} = 0b111;
  let Unpredictable{12} = 0b1;
  let Unpredictable{7} = 0b1;
  let Unpredictable{5} = 0b1;

  let Constraints = "";
  let DecoderMethod = "DecodeMVEVPNOT";
}

let Predicates = [HasMVEInt] in {
  def : Pat<(v2i1 (xor (v2i1 VCCR:$pred), (v2i1 (predicate_cast (i32 65535))))),
            (v2i1 (MVE_VPNOT (v2i1 VCCR:$pred)))>;
  def : Pat<(v4i1 (xor (v4i1 VCCR:$pred), (v4i1 (predicate_cast (i32 65535))))),
            (v4i1 (MVE_VPNOT (v4i1 VCCR:$pred)))>;
  def : Pat<(v8i1 (xor (v8i1 VCCR:$pred), (v8i1 (predicate_cast (i32 65535))))),
            (v8i1 (MVE_VPNOT (v8i1 VCCR:$pred)))>;
  def : Pat<(v16i1 (xor (v16i1 VCCR:$pred), (v16i1 (predicate_cast (i32 65535))))),
            (v16i1 (MVE_VPNOT (v16i1 VCCR:$pred)))>;
}


class MVE_loltp_start<dag iops, string asm, string ops, bits<2> size>
  : t2LOL<(outs GPRlr:$LR), iops, asm, ops> {
  bits<4> Rn;
  let Predicates = [HasMVEInt];
  let Inst{22} = 0b0;
  let Inst{21-20} = size;
  let Inst{19-16} = Rn{3-0};
  let Inst{12} = 0b0;
}

class MVE_DLSTP<string asm, bits<2> size>
  : MVE_loltp_start<(ins rGPR:$Rn), asm, "$LR, $Rn", size> {
  let Inst{13} = 0b1;
  let Inst{11-1} = 0b00000000000;
  let Unpredictable{10-1} = 0b1111111111;
}

class MVE_WLSTP<string asm, bits<2> size>
  : MVE_loltp_start<(ins rGPR:$Rn, wlslabel_u11:$label),
                    asm, "$LR, $Rn, $label", size> {
  bits<11> label;
  let Inst{13} = 0b0;
  let Inst{11} = label{0};
  let Inst{10-1} = label{10-1};
  let isBranch = 1;
  let isTerminator = 1;
}

def SDT_MVEMEMCPYLOOPNODE
    : SDTypeProfile<0, 3, [SDTCisPtrTy<0>, SDTCisPtrTy<1>, SDTCisVT<2, i32>]>;
def MVE_MEMCPYLOOPNODE : SDNode<"ARMISD::MEMCPYLOOP", SDT_MVEMEMCPYLOOPNODE,
                                [SDNPHasChain, SDNPMayStore, SDNPMayLoad]>;

let usesCustomInserter = 1, hasNoSchedulingInfo = 1, Defs = [CPSR] in {
  def MVE_MEMCPYLOOPINST : PseudoInst<(outs),
        (ins rGPR:$dst, rGPR:$src, rGPR:$sz),
        NoItinerary,
        [(MVE_MEMCPYLOOPNODE rGPR:$dst, rGPR:$src, rGPR:$sz)]>;
}

def SDT_MVEMEMSETLOOPNODE
    : SDTypeProfile<0, 3, [SDTCisPtrTy<0>, SDTCisVT<1, v16i8>, SDTCisVT<2, i32>]>;
def MVE_MEMSETLOOPNODE : SDNode<"ARMISD::MEMSETLOOP", SDT_MVEMEMSETLOOPNODE,
                                [SDNPHasChain, SDNPMayStore, SDNPMayLoad]>;

let usesCustomInserter = 1, hasNoSchedulingInfo = 1, Defs = [CPSR] in {
  def MVE_MEMSETLOOPINST : PseudoInst<(outs),
        (ins rGPR:$dst, MQPR:$src, rGPR:$sz),
        NoItinerary,
        [(MVE_MEMSETLOOPNODE rGPR:$dst, MQPR:$src, rGPR:$sz)]>;
}

def MVE_DLSTP_8  : MVE_DLSTP<"dlstp.8",  0b00>;
def MVE_DLSTP_16 : MVE_DLSTP<"dlstp.16", 0b01>;
def MVE_DLSTP_32 : MVE_DLSTP<"dlstp.32", 0b10>;
def MVE_DLSTP_64 : MVE_DLSTP<"dlstp.64", 0b11>;

def MVE_WLSTP_8  : MVE_WLSTP<"wlstp.8",  0b00>;
def MVE_WLSTP_16 : MVE_WLSTP<"wlstp.16", 0b01>;
def MVE_WLSTP_32 : MVE_WLSTP<"wlstp.32", 0b10>;
def MVE_WLSTP_64 : MVE_WLSTP<"wlstp.64", 0b11>;

class MVE_loltp_end<dag oops, dag iops, string asm, string ops>
  : t2LOL<oops, iops, asm, ops> {
  let Predicates = [HasMVEInt];
  let Inst{22-21} = 0b00;
  let Inst{19-16} = 0b1111;
  let Inst{12} = 0b0;
}

def MVE_LETP : MVE_loltp_end<(outs GPRlr:$LRout),
                             (ins GPRlr:$LRin, lelabel_u11:$label),
                             "letp", "$LRin, $label"> {
  bits<11> label;
  let Inst{20} = 0b1;
  let Inst{13} = 0b0;
  let Inst{11} = label{0};
  let Inst{10-1} = label{10-1};
  let isBranch = 1;
  let isTerminator = 1;
}

def MVE_LCTP : MVE_loltp_end<(outs), (ins pred:$p), "lctp${p}", ""> {
  let Inst{20} = 0b0;
  let Inst{13} = 0b1;
  let Inst{11-1} = 0b00000000000;
  let Unpredictable{21-20} = 0b11;
  let Unpredictable{11-1} = 0b11111111111;
}


// Pseudo instructions for lowering MQQPR and MQQQQPR stack spills and reloads.
// They are equivalent to VLDMDIA/VSTMDIA with a single reg, as opposed to multiple
// dreg subregs.

let Predicates = [HasMVEInt], AM = AddrMode4 in {
let mayStore = 1, hasSideEffects = 0 in {
  def MQQPRStore : t2PseudoInst<(outs), (ins MQQPR:$val, GPRnopc:$ptr),
                                4, NoItinerary, []>;
  def MQQQQPRStore : t2PseudoInst<(outs), (ins MQQQQPR:$val, GPRnopc:$ptr),
                                  4, NoItinerary, []>;
}
let mayLoad = 1, hasSideEffects = 0 in {
  def MQQPRLoad : t2PseudoInst<(outs MQQPR:$val), (ins GPRnopc:$ptr),
                               4, NoItinerary, []>;
  def MQQQQPRLoad : t2PseudoInst<(outs MQQQQPR:$val), (ins GPRnopc:$ptr),
                                 4, NoItinerary, []>;
}
}

// Pseudo for lowering MVE Q register COPYs. These will usually get converted
// to a "MVE_VORR dst, src, src", but may behave differently in tail predicated
// loops to ensure the whole register is copied, not a subset from a
// tail-predicated MVE_VORR. In the event we cannot prove a MVE_VORR is valid,
// it will become a pair of VMOVD instructions for each half of the Q register.
let Predicates = [HasMVEInt], hasSideEffects = 0, isMoveReg = 1,
    D = MVEDomain in {
  def MQPRCopy : t2PseudoInst<(outs MQPR:$dst), (ins MQPR:$src),
                              8, NoItinerary, []>;
}


//===----------------------------------------------------------------------===//
// Patterns
//===----------------------------------------------------------------------===//

// PatFrags for loads and stores. Often trying to keep semi-consistent names.

def aligned32_pre_store : PatFrag<(ops node:$val, node:$ptr, node:$offset),
                                  (pre_store node:$val, node:$ptr, node:$offset), [{
  return cast<StoreSDNode>(N)->getAlign() >= 4;
}]>;
def aligned32_post_store : PatFrag<(ops node:$val, node:$ptr, node:$offset),
                                   (post_store node:$val, node:$ptr, node:$offset), [{
  return cast<StoreSDNode>(N)->getAlign() >= 4;
}]>;
def aligned16_pre_store : PatFrag<(ops node:$val, node:$ptr, node:$offset),
                                  (pre_store node:$val, node:$ptr, node:$offset), [{
  return cast<StoreSDNode>(N)->getAlign() >= 2;
}]>;
def aligned16_post_store : PatFrag<(ops node:$val, node:$ptr, node:$offset),
                                   (post_store node:$val, node:$ptr, node:$offset), [{
  return cast<StoreSDNode>(N)->getAlign() >= 2;
}]>;


def aligned_maskedloadvi8 : PatFrag<(ops node:$ptr, node:$pred, node:$passthru),
                                    (masked_ld node:$ptr, undef, node:$pred, node:$passthru), [{
  auto *Ld = cast<MaskedLoadSDNode>(N);
  return Ld->getMemoryVT().getScalarType() == MVT::i8;
}]>;
def aligned_sextmaskedloadvi8 : PatFrag<(ops node:$ptr, node:$pred, node:$passthru),
                                        (aligned_maskedloadvi8 node:$ptr, node:$pred, node:$passthru), [{
  return cast<MaskedLoadSDNode>(N)->getExtensionType() == ISD::SEXTLOAD;
}]>;
def aligned_zextmaskedloadvi8 : PatFrag<(ops node:$ptr, node:$pred, node:$passthru),
                                        (aligned_maskedloadvi8 node:$ptr, node:$pred, node:$passthru), [{
  return cast<MaskedLoadSDNode>(N)->getExtensionType() == ISD::ZEXTLOAD;
}]>;
def aligned_extmaskedloadvi8 : PatFrag<(ops node:$ptr, node:$pred, node:$passthru),
                                       (aligned_maskedloadvi8 node:$ptr, node:$pred, node:$passthru), [{
  auto *Ld = cast<MaskedLoadSDNode>(N);
  EVT ScalarVT = Ld->getMemoryVT().getScalarType();
  return ScalarVT.isInteger() && Ld->getExtensionType() == ISD::EXTLOAD;
}]>;
def aligned_maskedloadvi16: PatFrag<(ops node:$ptr, node:$pred, node:$passthru),
                                    (masked_ld node:$ptr, undef, node:$pred, node:$passthru), [{
  auto *Ld = cast<MaskedLoadSDNode>(N);
  EVT ScalarVT = Ld->getMemoryVT().getScalarType();
  return (ScalarVT == MVT::i16 || ScalarVT == MVT::f16) && Ld->getAlign() >= 2;
}]>;
def aligned_sextmaskedloadvi16 : PatFrag<(ops node:$ptr, node:$pred, node:$passthru),
                                         (aligned_maskedloadvi16 node:$ptr, node:$pred, node:$passthru), [{
  return cast<MaskedLoadSDNode>(N)->getExtensionType() == ISD::SEXTLOAD;
}]>;
def aligned_zextmaskedloadvi16 : PatFrag<(ops node:$ptr, node:$pred, node:$passthru),
                                         (aligned_maskedloadvi16 node:$ptr, node:$pred, node:$passthru), [{
  return cast<MaskedLoadSDNode>(N)->getExtensionType() == ISD::ZEXTLOAD;
}]>;
def aligned_extmaskedloadvi16 : PatFrag<(ops node:$ptr, node:$pred, node:$passthru),
                                        (aligned_maskedloadvi16 node:$ptr, node:$pred, node:$passthru), [{
  auto *Ld = cast<MaskedLoadSDNode>(N);
  EVT ScalarVT = Ld->getMemoryVT().getScalarType();
  return ScalarVT.isInteger() && Ld->getExtensionType() == ISD::EXTLOAD;
}]>;
def aligned_maskedloadvi32: PatFrag<(ops node:$ptr, node:$pred, node:$passthru),
                                    (masked_ld node:$ptr, undef, node:$pred, node:$passthru), [{
  auto *Ld = cast<MaskedLoadSDNode>(N);
  EVT ScalarVT = Ld->getMemoryVT().getScalarType();
  return (ScalarVT == MVT::i32 || ScalarVT == MVT::f32) && Ld->getAlign() >= 4;
}]>;

def aligned_maskedstvi8 : PatFrag<(ops node:$val, node:$ptr, node:$pred),
                                  (masked_st node:$val, node:$ptr, undef, node:$pred), [{
  return cast<MaskedStoreSDNode>(N)->getMemoryVT().getScalarType() == MVT::i8;
}]>;
def aligned_maskedstvi16 : PatFrag<(ops node:$val, node:$ptr, node:$pred),
                                   (masked_st node:$val, node:$ptr, undef, node:$pred), [{
  auto *St = cast<MaskedStoreSDNode>(N);
  EVT ScalarVT = St->getMemoryVT().getScalarType();
  return (ScalarVT == MVT::i16 || ScalarVT == MVT::f16) && St->getAlign() >= 2;
}]>;
def aligned_maskedstvi32 : PatFrag<(ops node:$val, node:$ptr, node:$pred),
                                   (masked_st node:$val, node:$ptr, undef, node:$pred), [{
  auto *St = cast<MaskedStoreSDNode>(N);
  EVT ScalarVT = St->getMemoryVT().getScalarType();
  return (ScalarVT == MVT::i32 || ScalarVT == MVT::f32) && St->getAlign() >= 4;
}]>;

def pre_maskedstore : PatFrag<(ops node:$val, node:$base, node:$offset, node:$mask),
                              (masked_st node:$val, node:$base, node:$offset, node:$mask), [{
  ISD::MemIndexedMode AM = cast<MaskedStoreSDNode>(N)->getAddressingMode();
  return AM == ISD::PRE_INC || AM == ISD::PRE_DEC;
}]>;
def post_maskedstore : PatFrag<(ops node:$val, node:$base, node:$offset, node:$mask),
                               (masked_st node:$val, node:$base, node:$offset, node:$mask), [{
  ISD::MemIndexedMode AM = cast<MaskedStoreSDNode>(N)->getAddressingMode();
  return AM == ISD::POST_INC || AM == ISD::POST_DEC;
}]>;
def aligned_pre_maskedstorevi8 : PatFrag<(ops node:$val, node:$ptr, node:$offset, node:$mask),
                                         (pre_maskedstore node:$val, node:$ptr, node:$offset, node:$mask), [{
  return cast<MaskedStoreSDNode>(N)->getMemoryVT().getScalarType() == MVT::i8;
}]>;
def aligned_post_maskedstorevi8 : PatFrag<(ops node:$val, node:$ptr, node:$offset, node:$mask),
                                          (post_maskedstore node:$val, node:$ptr, node:$offset, node:$mask), [{
  return cast<MaskedStoreSDNode>(N)->getMemoryVT().getScalarType() == MVT::i8;
}]>;
def aligned_pre_maskedstorevi16 : PatFrag<(ops node:$val, node:$ptr, node:$offset, node:$mask),
                                          (pre_maskedstore node:$val, node:$ptr, node:$offset, node:$mask), [{
  auto *St = cast<MaskedStoreSDNode>(N);
  EVT ScalarVT = St->getMemoryVT().getScalarType();
  return (ScalarVT == MVT::i16 || ScalarVT == MVT::f16) && St->getAlign() >= 2;
}]>;
def aligned_post_maskedstorevi16 : PatFrag<(ops node:$val, node:$ptr, node:$offset, node:$mask),
                                           (post_maskedstore node:$val, node:$ptr, node:$offset, node:$mask), [{
  auto *St = cast<MaskedStoreSDNode>(N);
  EVT ScalarVT = St->getMemoryVT().getScalarType();
  return (ScalarVT == MVT::i16 || ScalarVT == MVT::f16) && St->getAlign() >= 2;
}]>;
def aligned_pre_maskedstorevi32 : PatFrag<(ops node:$val, node:$ptr, node:$offset, node:$mask),
                                          (pre_maskedstore node:$val, node:$ptr, node:$offset, node:$mask), [{
  auto *St = cast<MaskedStoreSDNode>(N);
  EVT ScalarVT = St->getMemoryVT().getScalarType();
  return (ScalarVT == MVT::i32 || ScalarVT == MVT::f32) && St->getAlign() >= 4;
}]>;
def aligned_post_maskedstorevi32 : PatFrag<(ops node:$val, node:$ptr, node:$offset, node:$mask),
                                           (post_maskedstore node:$val, node:$ptr, node:$offset, node:$mask), [{
  auto *St = cast<MaskedStoreSDNode>(N);
  EVT ScalarVT = St->getMemoryVT().getScalarType();
  return (ScalarVT == MVT::i32 || ScalarVT == MVT::f32) && St->getAlign() >= 4;
}]>;


// PatFrags for "Aligned" extending / truncating

def aligned_extloadvi8  : PatFrag<(ops node:$ptr), (extloadvi8 node:$ptr)>;
def aligned_sextloadvi8 : PatFrag<(ops node:$ptr), (sextloadvi8 node:$ptr)>;
def aligned_zextloadvi8 : PatFrag<(ops node:$ptr), (zextloadvi8 node:$ptr)>;

def aligned_truncstvi8 : PatFrag<(ops node:$val, node:$ptr),
                                 (truncstorevi8 node:$val, node:$ptr)>;
def aligned_post_truncstvi8 : PatFrag<(ops node:$val, node:$base, node:$offset),
                                      (post_truncstvi8 node:$val, node:$base, node:$offset)>;
def aligned_pre_truncstvi8 : PatFrag<(ops node:$val, node:$base, node:$offset),
                                     (pre_truncstvi8 node:$val, node:$base, node:$offset)>;

let MinAlignment = 2 in {
  def aligned_extloadvi16  : PatFrag<(ops node:$ptr), (extloadvi16 node:$ptr)>;
  def aligned_sextloadvi16 : PatFrag<(ops node:$ptr), (sextloadvi16 node:$ptr)>;
  def aligned_zextloadvi16 : PatFrag<(ops node:$ptr), (zextloadvi16 node:$ptr)>;

  def aligned_truncstvi16 : PatFrag<(ops node:$val, node:$ptr),
                                    (truncstorevi16 node:$val, node:$ptr)>;
  def aligned_post_truncstvi16 : PatFrag<(ops node:$val, node:$base, node:$offset),
                                         (post_truncstvi16 node:$val, node:$base, node:$offset)>;
  def aligned_pre_truncstvi16 : PatFrag<(ops node:$val, node:$base, node:$offset),
                                        (pre_truncstvi16 node:$val, node:$base, node:$offset)>;
}

def truncmaskedst : PatFrag<(ops node:$val, node:$base, node:$pred),
                            (masked_st node:$val, node:$base, undef, node:$pred), [{
  return cast<MaskedStoreSDNode>(N)->isTruncatingStore();
}]>;
def aligned_truncmaskedstvi8 : PatFrag<(ops node:$val, node:$base, node:$pred),
                                       (truncmaskedst node:$val, node:$base, node:$pred), [{
  return cast<MaskedStoreSDNode>(N)->getMemoryVT().getScalarType() == MVT::i8;
}]>;
def aligned_truncmaskedstvi16 : PatFrag<(ops node:$val, node:$base, node:$pred),
                                        (truncmaskedst node:$val, node:$base, node:$pred), [{
  auto *St = cast<MaskedStoreSDNode>(N);
  EVT ScalarVT = St->getMemoryVT().getScalarType();
  return (ScalarVT == MVT::i16 || ScalarVT == MVT::f16) && St->getAlign() >= 2;
}]>;
def pre_truncmaskedst : PatFrag<(ops node:$val, node:$base, node:$offset, node:$pred),
                                (masked_st node:$val, node:$base, node:$offset, node:$pred), [{
  ISD::MemIndexedMode AM = cast<MaskedStoreSDNode>(N)->getAddressingMode();
  return cast<MaskedStoreSDNode>(N)->isTruncatingStore() && (AM == ISD::PRE_INC || AM == ISD::PRE_DEC);
}]>;
def aligned_pre_truncmaskedstvi8 : PatFrag<(ops node:$val, node:$base, node:$offset, node:$pred),
                                           (pre_truncmaskedst node:$val, node:$base, node:$offset, node:$pred), [{
  return cast<MaskedStoreSDNode>(N)->getMemoryVT().getScalarType() == MVT::i8;
}]>;
def aligned_pre_truncmaskedstvi16 : PatFrag<(ops node:$val, node:$base, node:$offset, node:$pred),
                                            (pre_truncmaskedst node:$val, node:$base, node:$offset, node:$pred), [{
  auto *St = cast<MaskedStoreSDNode>(N);
  EVT ScalarVT = St->getMemoryVT().getScalarType();
  return (ScalarVT == MVT::i16 || ScalarVT == MVT::f16) && St->getAlign() >= 2;
}]>;
def post_truncmaskedst : PatFrag<(ops node:$val, node:$base, node:$offset, node:$postd),
                                 (masked_st node:$val, node:$base, node:$offset, node:$postd), [{
  ISD::MemIndexedMode AM = cast<MaskedStoreSDNode>(N)->getAddressingMode();
  return cast<MaskedStoreSDNode>(N)->isTruncatingStore() && (AM == ISD::POST_INC || AM == ISD::POST_DEC);
}]>;
def aligned_post_truncmaskedstvi8 : PatFrag<(ops node:$val, node:$base, node:$offset, node:$postd),
                                            (post_truncmaskedst node:$val, node:$base, node:$offset, node:$postd), [{
  return cast<MaskedStoreSDNode>(N)->getMemoryVT().getScalarType() == MVT::i8;
}]>;
def aligned_post_truncmaskedstvi16 : PatFrag<(ops node:$val, node:$base, node:$offset, node:$postd),
                                             (post_truncmaskedst node:$val, node:$base, node:$offset, node:$postd), [{
  auto *St = cast<MaskedStoreSDNode>(N);
  EVT ScalarVT = St->getMemoryVT().getScalarType();
  return (ScalarVT == MVT::i16 || ScalarVT == MVT::f16) && St->getAlign() >= 2;
}]>;

// Load/store patterns

class MVE_vector_store_typed<ValueType Ty, Instruction RegImmInst,
                             PatFrag StoreKind, int shift>
  : Pat<(StoreKind (Ty MQPR:$val), t2addrmode_imm7<shift>:$addr),
        (RegImmInst (Ty MQPR:$val), t2addrmode_imm7<shift>:$addr)>;

class MVE_vector_maskedstore_typed<ValueType Ty, Instruction RegImmInst,
                                   PatFrag StoreKind, int shift>
  : Pat<(StoreKind (Ty MQPR:$val), t2addrmode_imm7<shift>:$addr, VCCR:$pred),
        (RegImmInst (Ty MQPR:$val), t2addrmode_imm7<shift>:$addr, ARMVCCThen, VCCR:$pred, zero_reg)>;

multiclass MVE_vector_store<Instruction RegImmInst, PatFrag StoreKind,
                            int shift> {
  def : MVE_vector_store_typed<v16i8, RegImmInst, StoreKind, shift>;
  def : MVE_vector_store_typed<v8i16, RegImmInst, StoreKind, shift>;
  def : MVE_vector_store_typed<v8f16, RegImmInst, StoreKind, shift>;
  def : MVE_vector_store_typed<v4i32, RegImmInst, StoreKind, shift>;
  def : MVE_vector_store_typed<v4f32, RegImmInst, StoreKind, shift>;
  def : MVE_vector_store_typed<v2i64, RegImmInst, StoreKind, shift>;
  def : MVE_vector_store_typed<v2f64, RegImmInst, StoreKind, shift>;
}

class MVE_vector_load_typed<ValueType Ty, Instruction RegImmInst,
                            PatFrag LoadKind, int shift>
  : Pat<(Ty (LoadKind t2addrmode_imm7<shift>:$addr)),
        (Ty (RegImmInst t2addrmode_imm7<shift>:$addr))>;

class MVE_vector_maskedload_typed<ValueType Ty, Instruction RegImmInst,
                                  PatFrag LoadKind, int shift>
  : Pat<(Ty (LoadKind t2addrmode_imm7<shift>:$addr, VCCR:$pred, (Ty (ARMvmovImm (i32 0))))),
        (Ty (RegImmInst t2addrmode_imm7<shift>:$addr, ARMVCCThen, VCCR:$pred, zero_reg))>;

multiclass MVE_vector_load<Instruction RegImmInst, PatFrag LoadKind,
                           int shift> {
  def : MVE_vector_load_typed<v16i8, RegImmInst, LoadKind, shift>;
  def : MVE_vector_load_typed<v8i16, RegImmInst, LoadKind, shift>;
  def : MVE_vector_load_typed<v8f16, RegImmInst, LoadKind, shift>;
  def : MVE_vector_load_typed<v4i32, RegImmInst, LoadKind, shift>;
  def : MVE_vector_load_typed<v4f32, RegImmInst, LoadKind, shift>;
  def : MVE_vector_load_typed<v2i64, RegImmInst, LoadKind, shift>;
  def : MVE_vector_load_typed<v2f64, RegImmInst, LoadKind, shift>;
}

class MVE_vector_offset_store_typed<ValueType Ty, Instruction Opcode,
                                    PatFrag StoreKind, int shift>
  : Pat<(StoreKind (Ty MQPR:$Rt), tGPR:$Rn, t2am_imm7_offset<shift>:$addr),
        (Opcode MQPR:$Rt, tGPR:$Rn, t2am_imm7_offset<shift>:$addr)>;

class MVE_vector_offset_maskedstore_typed<ValueType Ty, Instruction Opcode,
                                          PatFrag StoreKind, int shift>
  : Pat<(StoreKind (Ty MQPR:$Rt), tGPR:$Rn, t2am_imm7_offset<shift>:$addr, VCCR:$pred),
        (Opcode MQPR:$Rt, tGPR:$Rn, t2am_imm7_offset<shift>:$addr, ARMVCCThen, VCCR:$pred, zero_reg)>;

multiclass MVE_vector_offset_store<Instruction RegImmInst, PatFrag StoreKind,
                                   int shift> {
  def : MVE_vector_offset_store_typed<v16i8, RegImmInst, StoreKind, shift>;
  def : MVE_vector_offset_store_typed<v8i16, RegImmInst, StoreKind, shift>;
  def : MVE_vector_offset_store_typed<v8f16, RegImmInst, StoreKind, shift>;
  def : MVE_vector_offset_store_typed<v4i32, RegImmInst, StoreKind, shift>;
  def : MVE_vector_offset_store_typed<v4f32, RegImmInst, StoreKind, shift>;
  def : MVE_vector_offset_store_typed<v2i64, RegImmInst, StoreKind, shift>;
  def : MVE_vector_offset_store_typed<v2f64, RegImmInst, StoreKind, shift>;
}


let Predicates = [HasMVEInt, IsLE] in {
  // Stores
  defm : MVE_vector_store<MVE_VSTRBU8, byte_alignedstore, 0>;
  defm : MVE_vector_store<MVE_VSTRHU16, hword_alignedstore, 1>;
  defm : MVE_vector_store<MVE_VSTRWU32, alignedstore32, 2>;

  // Loads
  defm : MVE_vector_load<MVE_VLDRBU8, byte_alignedload, 0>;
  defm : MVE_vector_load<MVE_VLDRHU16, hword_alignedload, 1>;
  defm : MVE_vector_load<MVE_VLDRWU32, alignedload32, 2>;

  // Pre/post inc stores
  defm : MVE_vector_offset_store<MVE_VSTRBU8_pre, pre_store, 0>;
  defm : MVE_vector_offset_store<MVE_VSTRBU8_post, post_store, 0>;
  defm : MVE_vector_offset_store<MVE_VSTRHU16_pre, aligned16_pre_store, 1>;
  defm : MVE_vector_offset_store<MVE_VSTRHU16_post, aligned16_post_store, 1>;
  defm : MVE_vector_offset_store<MVE_VSTRWU32_pre, aligned32_pre_store, 2>;
  defm : MVE_vector_offset_store<MVE_VSTRWU32_post, aligned32_post_store, 2>;
}

let Predicates = [HasMVEInt, IsBE] in {
  // Aligned Stores
  def : MVE_vector_store_typed<v16i8, MVE_VSTRBU8, store, 0>;
  def : MVE_vector_store_typed<v8i16, MVE_VSTRHU16, alignedstore16, 1>;
  def : MVE_vector_store_typed<v8f16, MVE_VSTRHU16, alignedstore16, 1>;
  def : MVE_vector_store_typed<v4i32, MVE_VSTRWU32, alignedstore32, 2>;
  def : MVE_vector_store_typed<v4f32, MVE_VSTRWU32, alignedstore32, 2>;

  // Aligned Loads
  def : MVE_vector_load_typed<v16i8, MVE_VLDRBU8, load, 0>;
  def : MVE_vector_load_typed<v8i16, MVE_VLDRHU16, alignedload16, 1>;
  def : MVE_vector_load_typed<v8f16, MVE_VLDRHU16, alignedload16, 1>;
  def : MVE_vector_load_typed<v4i32, MVE_VLDRWU32, alignedload32, 2>;
  def : MVE_vector_load_typed<v4f32, MVE_VLDRWU32, alignedload32, 2>;

  // Other unaligned loads/stores need to go though a VREV
  def : Pat<(v2f64 (load t2addrmode_imm7<0>:$addr)),
            (v2f64 (MVE_VREV64_8 (MVE_VLDRBU8 t2addrmode_imm7<0>:$addr)))>;
  def : Pat<(v2i64 (load t2addrmode_imm7<0>:$addr)),
            (v2i64 (MVE_VREV64_8 (MVE_VLDRBU8 t2addrmode_imm7<0>:$addr)))>;
  def : Pat<(v4i32 (load t2addrmode_imm7<0>:$addr)),
            (v4i32 (MVE_VREV32_8 (MVE_VLDRBU8 t2addrmode_imm7<0>:$addr)))>;
  def : Pat<(v4f32 (load t2addrmode_imm7<0>:$addr)),
            (v4f32 (MVE_VREV32_8 (MVE_VLDRBU8 t2addrmode_imm7<0>:$addr)))>;
  def : Pat<(v8i16 (load t2addrmode_imm7<0>:$addr)),
            (v8i16 (MVE_VREV16_8 (MVE_VLDRBU8 t2addrmode_imm7<0>:$addr)))>;
  def : Pat<(v8f16 (load t2addrmode_imm7<0>:$addr)),
            (v8f16 (MVE_VREV16_8 (MVE_VLDRBU8 t2addrmode_imm7<0>:$addr)))>;
  def : Pat<(store (v2f64 MQPR:$val), t2addrmode_imm7<0>:$addr),
            (MVE_VSTRBU8 (MVE_VREV64_8 MQPR:$val), t2addrmode_imm7<0>:$addr)>;
  def : Pat<(store (v2i64 MQPR:$val), t2addrmode_imm7<0>:$addr),
            (MVE_VSTRBU8 (MVE_VREV64_8 MQPR:$val), t2addrmode_imm7<0>:$addr)>;
  def : Pat<(store (v4i32 MQPR:$val), t2addrmode_imm7<0>:$addr),
            (MVE_VSTRBU8 (MVE_VREV32_8 MQPR:$val), t2addrmode_imm7<0>:$addr)>;
  def : Pat<(store (v4f32 MQPR:$val), t2addrmode_imm7<0>:$addr),
            (MVE_VSTRBU8 (MVE_VREV32_8 MQPR:$val), t2addrmode_imm7<0>:$addr)>;
  def : Pat<(store (v8i16 MQPR:$val), t2addrmode_imm7<0>:$addr),
            (MVE_VSTRBU8 (MVE_VREV16_8 MQPR:$val), t2addrmode_imm7<0>:$addr)>;
  def : Pat<(store (v8f16 MQPR:$val), t2addrmode_imm7<0>:$addr),
            (MVE_VSTRBU8 (MVE_VREV16_8 MQPR:$val), t2addrmode_imm7<0>:$addr)>;

  // Pre/Post inc stores
  def : MVE_vector_offset_store_typed<v16i8, MVE_VSTRBU8_pre, pre_store, 0>;
  def : MVE_vector_offset_store_typed<v16i8, MVE_VSTRBU8_post, post_store, 0>;
  def : MVE_vector_offset_store_typed<v8i16, MVE_VSTRHU16_pre, aligned16_pre_store, 1>;
  def : MVE_vector_offset_store_typed<v8i16, MVE_VSTRHU16_post, aligned16_post_store, 1>;
  def : MVE_vector_offset_store_typed<v8f16, MVE_VSTRHU16_pre, aligned16_pre_store, 1>;
  def : MVE_vector_offset_store_typed<v8f16, MVE_VSTRHU16_post, aligned16_post_store, 1>;
  def : MVE_vector_offset_store_typed<v4i32, MVE_VSTRWU32_pre, aligned32_pre_store, 2>;
  def : MVE_vector_offset_store_typed<v4i32, MVE_VSTRWU32_post, aligned32_post_store, 2>;
  def : MVE_vector_offset_store_typed<v4f32, MVE_VSTRWU32_pre, aligned32_pre_store, 2>;
  def : MVE_vector_offset_store_typed<v4f32, MVE_VSTRWU32_post, aligned32_post_store, 2>;
}

let Predicates = [HasMVEInt] in {
  // Aligned masked store, shared between LE and BE
  def : MVE_vector_maskedstore_typed<v16i8, MVE_VSTRBU8, aligned_maskedstvi8, 0>;
  def : MVE_vector_maskedstore_typed<v8i16, MVE_VSTRHU16, aligned_maskedstvi16, 1>;
  def : MVE_vector_maskedstore_typed<v8f16, MVE_VSTRHU16, aligned_maskedstvi16, 1>;
  def : MVE_vector_maskedstore_typed<v4i32, MVE_VSTRWU32, aligned_maskedstvi32, 2>;
  def : MVE_vector_maskedstore_typed<v4f32, MVE_VSTRWU32, aligned_maskedstvi32, 2>;

  // Pre/Post inc masked stores
  def : MVE_vector_offset_maskedstore_typed<v16i8, MVE_VSTRBU8_pre, aligned_pre_maskedstorevi8, 0>;
  def : MVE_vector_offset_maskedstore_typed<v16i8, MVE_VSTRBU8_post, aligned_post_maskedstorevi8, 0>;
  def : MVE_vector_offset_maskedstore_typed<v8i16, MVE_VSTRHU16_pre, aligned_pre_maskedstorevi16, 1>;
  def : MVE_vector_offset_maskedstore_typed<v8i16, MVE_VSTRHU16_post, aligned_post_maskedstorevi16, 1>;
  def : MVE_vector_offset_maskedstore_typed<v8f16, MVE_VSTRHU16_pre, aligned_pre_maskedstorevi16, 1>;
  def : MVE_vector_offset_maskedstore_typed<v8f16, MVE_VSTRHU16_post, aligned_post_maskedstorevi16, 1>;
  def : MVE_vector_offset_maskedstore_typed<v4i32, MVE_VSTRWU32_pre, aligned_pre_maskedstorevi32, 2>;
  def : MVE_vector_offset_maskedstore_typed<v4i32, MVE_VSTRWU32_post, aligned_post_maskedstorevi32, 2>;
  def : MVE_vector_offset_maskedstore_typed<v4f32, MVE_VSTRWU32_pre, aligned_pre_maskedstorevi32, 2>;
  def : MVE_vector_offset_maskedstore_typed<v4f32, MVE_VSTRWU32_post, aligned_post_maskedstorevi32, 2>;

  // Aligned masked loads
  def : MVE_vector_maskedload_typed<v16i8, MVE_VLDRBU8, aligned_maskedloadvi8, 0>;
  def : MVE_vector_maskedload_typed<v8i16, MVE_VLDRHU16, aligned_maskedloadvi16, 1>;
  def : MVE_vector_maskedload_typed<v8f16, MVE_VLDRHU16, aligned_maskedloadvi16, 1>;
  def : MVE_vector_maskedload_typed<v4i32, MVE_VLDRWU32, aligned_maskedloadvi32, 2>;
  def : MVE_vector_maskedload_typed<v4f32, MVE_VLDRWU32, aligned_maskedloadvi32, 2>;
}

// Widening/Narrowing Loads/Stores

multiclass MVEExtLoadStore<Instruction LoadSInst, Instruction LoadUInst, string StoreInst,
                         string Amble, ValueType VT, int Shift> {
  // Trunc stores
  def : Pat<(!cast<PatFrag>("aligned_truncst"#Amble) (VT MQPR:$val), taddrmode_imm7<Shift>:$addr),
            (!cast<Instruction>(StoreInst) MQPR:$val, taddrmode_imm7<Shift>:$addr)>;
  def : Pat<(!cast<PatFrag>("aligned_post_truncst"#Amble) (VT MQPR:$Rt), tGPR:$Rn, t2am_imm7_offset<Shift>:$addr),
            (!cast<Instruction>(StoreInst#"_post") MQPR:$Rt, tGPR:$Rn, t2am_imm7_offset<Shift>:$addr)>;
  def : Pat<(!cast<PatFrag>("aligned_pre_truncst"#Amble) (VT MQPR:$Rt), tGPR:$Rn, t2am_imm7_offset<Shift>:$addr),
            (!cast<Instruction>(StoreInst#"_pre") MQPR:$Rt, tGPR:$Rn, t2am_imm7_offset<Shift>:$addr)>;

  // Masked trunc stores
  def : Pat<(!cast<PatFrag>("aligned_truncmaskedst"#Amble) (VT MQPR:$val), taddrmode_imm7<Shift>:$addr, VCCR:$pred),
            (!cast<Instruction>(StoreInst) MQPR:$val, taddrmode_imm7<Shift>:$addr, ARMVCCThen, VCCR:$pred, zero_reg)>;
  def : Pat<(!cast<PatFrag>("aligned_post_truncmaskedst"#Amble) (VT MQPR:$Rt), tGPR:$Rn, t2am_imm7_offset<Shift>:$addr, VCCR:$pred),
            (!cast<Instruction>(StoreInst#"_post") MQPR:$Rt, tGPR:$Rn, t2am_imm7_offset<Shift>:$addr, ARMVCCThen, VCCR:$pred, zero_reg)>;
  def : Pat<(!cast<PatFrag>("aligned_pre_truncmaskedst"#Amble) (VT MQPR:$Rt), tGPR:$Rn, t2am_imm7_offset<Shift>:$addr, VCCR:$pred),
            (!cast<Instruction>(StoreInst#"_pre") MQPR:$Rt, tGPR:$Rn, t2am_imm7_offset<Shift>:$addr, ARMVCCThen, VCCR:$pred, zero_reg)>;

  // Ext loads
  def : Pat<(VT (!cast<PatFrag>("aligned_extload"#Amble) taddrmode_imm7<Shift>:$addr)),
            (VT (LoadUInst taddrmode_imm7<Shift>:$addr))>;
  def : Pat<(VT (!cast<PatFrag>("aligned_sextload"#Amble) taddrmode_imm7<Shift>:$addr)),
            (VT (LoadSInst taddrmode_imm7<Shift>:$addr))>;
  def : Pat<(VT (!cast<PatFrag>("aligned_zextload"#Amble) taddrmode_imm7<Shift>:$addr)),
            (VT (LoadUInst taddrmode_imm7<Shift>:$addr))>;

  // Masked ext loads
  def : Pat<(VT (!cast<PatFrag>("aligned_extmaskedload"#Amble) taddrmode_imm7<Shift>:$addr, VCCR:$pred, (VT (ARMvmovImm (i32 0))))),
            (VT (LoadUInst taddrmode_imm7<Shift>:$addr, ARMVCCThen, VCCR:$pred, zero_reg))>;
  def : Pat<(VT (!cast<PatFrag>("aligned_sextmaskedload"#Amble) taddrmode_imm7<Shift>:$addr, VCCR:$pred, (VT (ARMvmovImm (i32 0))))),
            (VT (LoadSInst taddrmode_imm7<Shift>:$addr, ARMVCCThen, VCCR:$pred, zero_reg))>;
  def : Pat<(VT (!cast<PatFrag>("aligned_zextmaskedload"#Amble) taddrmode_imm7<Shift>:$addr, VCCR:$pred, (VT (ARMvmovImm (i32 0))))),
            (VT (LoadUInst taddrmode_imm7<Shift>:$addr, ARMVCCThen, VCCR:$pred, zero_reg))>;
}

let Predicates = [HasMVEInt] in {
  defm : MVEExtLoadStore<MVE_VLDRBS16, MVE_VLDRBU16, "MVE_VSTRB16", "vi8", v8i16, 0>;
  defm : MVEExtLoadStore<MVE_VLDRBS32, MVE_VLDRBU32, "MVE_VSTRB32", "vi8", v4i32, 0>;
  defm : MVEExtLoadStore<MVE_VLDRHS32, MVE_VLDRHU32, "MVE_VSTRH32", "vi16", v4i32, 1>;
}


// Bit convert patterns

let Predicates = [HasMVEInt] in {
  def : Pat<(v2f64 (bitconvert (v2i64 MQPR:$src))), (v2f64 MQPR:$src)>;
  def : Pat<(v2i64 (bitconvert (v2f64 MQPR:$src))), (v2i64 MQPR:$src)>;

  def : Pat<(v4i32 (bitconvert (v4f32 MQPR:$src))), (v4i32 MQPR:$src)>;
  def : Pat<(v4f32 (bitconvert (v4i32 MQPR:$src))), (v4f32 MQPR:$src)>;

  def : Pat<(v8i16 (bitconvert (v8f16 MQPR:$src))), (v8i16  MQPR:$src)>;
  def : Pat<(v8f16 (bitconvert (v8i16 MQPR:$src))), (v8f16  MQPR:$src)>;
}

let Predicates = [IsLE,HasMVEInt] in {
  def : Pat<(v2f64 (bitconvert (v4f32 MQPR:$src))), (v2f64 MQPR:$src)>;
  def : Pat<(v2f64 (bitconvert (v4i32 MQPR:$src))), (v2f64 MQPR:$src)>;
  def : Pat<(v2f64 (bitconvert (v8f16 MQPR:$src))), (v2f64 MQPR:$src)>;
  def : Pat<(v2f64 (bitconvert (v8i16 MQPR:$src))), (v2f64 MQPR:$src)>;
  def : Pat<(v2f64 (bitconvert (v16i8 MQPR:$src))), (v2f64 MQPR:$src)>;

  def : Pat<(v2i64 (bitconvert (v4f32 MQPR:$src))), (v2i64 MQPR:$src)>;
  def : Pat<(v2i64 (bitconvert (v4i32 MQPR:$src))), (v2i64 MQPR:$src)>;
  def : Pat<(v2i64 (bitconvert (v8f16 MQPR:$src))), (v2i64 MQPR:$src)>;
  def : Pat<(v2i64 (bitconvert (v8i16 MQPR:$src))), (v2i64 MQPR:$src)>;
  def : Pat<(v2i64 (bitconvert (v16i8 MQPR:$src))), (v2i64 MQPR:$src)>;

  def : Pat<(v4f32 (bitconvert (v2f64 MQPR:$src))), (v4f32 MQPR:$src)>;
  def : Pat<(v4f32 (bitconvert (v2i64 MQPR:$src))), (v4f32 MQPR:$src)>;
  def : Pat<(v4f32 (bitconvert (v8f16 MQPR:$src))), (v4f32 MQPR:$src)>;
  def : Pat<(v4f32 (bitconvert (v8i16 MQPR:$src))), (v4f32 MQPR:$src)>;
  def : Pat<(v4f32 (bitconvert (v16i8 MQPR:$src))), (v4f32 MQPR:$src)>;

  def : Pat<(v4i32 (bitconvert (v2f64 MQPR:$src))), (v4i32 MQPR:$src)>;
  def : Pat<(v4i32 (bitconvert (v2i64 MQPR:$src))), (v4i32 MQPR:$src)>;
  def : Pat<(v4i32 (bitconvert (v8f16 MQPR:$src))), (v4i32 MQPR:$src)>;
  def : Pat<(v4i32 (bitconvert (v8i16 MQPR:$src))), (v4i32 MQPR:$src)>;
  def : Pat<(v4i32 (bitconvert (v16i8 MQPR:$src))), (v4i32 MQPR:$src)>;

  def : Pat<(v8f16 (bitconvert (v2f64 MQPR:$src))), (v8f16 MQPR:$src)>;
  def : Pat<(v8f16 (bitconvert (v2i64 MQPR:$src))), (v8f16 MQPR:$src)>;
  def : Pat<(v8f16 (bitconvert (v4f32 MQPR:$src))), (v8f16 MQPR:$src)>;
  def : Pat<(v8f16 (bitconvert (v4i32 MQPR:$src))), (v8f16 MQPR:$src)>;
  def : Pat<(v8f16 (bitconvert (v16i8 MQPR:$src))), (v8f16 MQPR:$src)>;

  def : Pat<(v8i16 (bitconvert (v2f64 MQPR:$src))), (v8i16 MQPR:$src)>;
  def : Pat<(v8i16 (bitconvert (v2i64 MQPR:$src))), (v8i16 MQPR:$src)>;
  def : Pat<(v8i16 (bitconvert (v4f32 MQPR:$src))), (v8i16 MQPR:$src)>;
  def : Pat<(v8i16 (bitconvert (v4i32 MQPR:$src))), (v8i16 MQPR:$src)>;
  def : Pat<(v8i16 (bitconvert (v16i8 MQPR:$src))), (v8i16 MQPR:$src)>;

  def : Pat<(v16i8 (bitconvert (v2f64 MQPR:$src))), (v16i8 MQPR:$src)>;
  def : Pat<(v16i8 (bitconvert (v2i64 MQPR:$src))), (v16i8 MQPR:$src)>;
  def : Pat<(v16i8 (bitconvert (v4f32 MQPR:$src))), (v16i8 MQPR:$src)>;
  def : Pat<(v16i8 (bitconvert (v4i32 MQPR:$src))), (v16i8 MQPR:$src)>;
  def : Pat<(v16i8 (bitconvert (v8f16 MQPR:$src))), (v16i8 MQPR:$src)>;
  def : Pat<(v16i8 (bitconvert (v8i16 MQPR:$src))), (v16i8 MQPR:$src)>;
}

let Predicates = [IsBE,HasMVEInt] in {
  def : Pat<(v2f64 (bitconvert (v4f32 MQPR:$src))), (v2f64 (MVE_VREV64_32 MQPR:$src))>;
  def : Pat<(v2f64 (bitconvert (v4i32 MQPR:$src))), (v2f64 (MVE_VREV64_32 MQPR:$src))>;
  def : Pat<(v2f64 (bitconvert (v8f16 MQPR:$src))), (v2f64 (MVE_VREV64_16 MQPR:$src))>;
  def : Pat<(v2f64 (bitconvert (v8i16 MQPR:$src))), (v2f64 (MVE_VREV64_16 MQPR:$src))>;
  def : Pat<(v2f64 (bitconvert (v16i8 MQPR:$src))), (v2f64 (MVE_VREV64_8 MQPR:$src))>;

  def : Pat<(v2i64 (bitconvert (v4f32 MQPR:$src))), (v2i64 (MVE_VREV64_32 MQPR:$src))>;
  def : Pat<(v2i64 (bitconvert (v4i32 MQPR:$src))), (v2i64 (MVE_VREV64_32 MQPR:$src))>;
  def : Pat<(v2i64 (bitconvert (v8f16 MQPR:$src))), (v2i64 (MVE_VREV64_16 MQPR:$src))>;
  def : Pat<(v2i64 (bitconvert (v8i16 MQPR:$src))), (v2i64 (MVE_VREV64_16 MQPR:$src))>;
  def : Pat<(v2i64 (bitconvert (v16i8 MQPR:$src))), (v2i64 (MVE_VREV64_8 MQPR:$src))>;

  def : Pat<(v4f32 (bitconvert (v2f64 MQPR:$src))), (v4f32 (MVE_VREV64_32 MQPR:$src))>;
  def : Pat<(v4f32 (bitconvert (v2i64 MQPR:$src))), (v4f32 (MVE_VREV64_32 MQPR:$src))>;
  def : Pat<(v4f32 (bitconvert (v8f16 MQPR:$src))), (v4f32 (MVE_VREV32_16 MQPR:$src))>;
  def : Pat<(v4f32 (bitconvert (v8i16 MQPR:$src))), (v4f32 (MVE_VREV32_16 MQPR:$src))>;
  def : Pat<(v4f32 (bitconvert (v16i8 MQPR:$src))), (v4f32 (MVE_VREV32_8 MQPR:$src))>;

  def : Pat<(v4i32 (bitconvert (v2f64 MQPR:$src))), (v4i32 (MVE_VREV64_32 MQPR:$src))>;
  def : Pat<(v4i32 (bitconvert (v2i64 MQPR:$src))), (v4i32 (MVE_VREV64_32 MQPR:$src))>;
  def : Pat<(v4i32 (bitconvert (v8f16 MQPR:$src))), (v4i32 (MVE_VREV32_16 MQPR:$src))>;
  def : Pat<(v4i32 (bitconvert (v8i16 MQPR:$src))), (v4i32 (MVE_VREV32_16 MQPR:$src))>;
  def : Pat<(v4i32 (bitconvert (v16i8 MQPR:$src))), (v4i32 (MVE_VREV32_8 MQPR:$src))>;

  def : Pat<(v8f16 (bitconvert (v2f64 MQPR:$src))), (v8f16 (MVE_VREV64_16 MQPR:$src))>;
  def : Pat<(v8f16 (bitconvert (v2i64 MQPR:$src))), (v8f16 (MVE_VREV64_16 MQPR:$src))>;
  def : Pat<(v8f16 (bitconvert (v4f32 MQPR:$src))), (v8f16 (MVE_VREV32_16 MQPR:$src))>;
  def : Pat<(v8f16 (bitconvert (v4i32 MQPR:$src))), (v8f16 (MVE_VREV32_16 MQPR:$src))>;
  def : Pat<(v8f16 (bitconvert (v16i8 MQPR:$src))), (v8f16 (MVE_VREV16_8 MQPR:$src))>;

  def : Pat<(v8i16 (bitconvert (v2f64 MQPR:$src))), (v8i16 (MVE_VREV64_16 MQPR:$src))>;
  def : Pat<(v8i16 (bitconvert (v2i64 MQPR:$src))), (v8i16 (MVE_VREV64_16 MQPR:$src))>;
  def : Pat<(v8i16 (bitconvert (v4f32 MQPR:$src))), (v8i16 (MVE_VREV32_16 MQPR:$src))>;
  def : Pat<(v8i16 (bitconvert (v4i32 MQPR:$src))), (v8i16 (MVE_VREV32_16 MQPR:$src))>;
  def : Pat<(v8i16 (bitconvert (v16i8 MQPR:$src))), (v8i16 (MVE_VREV16_8 MQPR:$src))>;

  def : Pat<(v16i8 (bitconvert (v2f64 MQPR:$src))), (v16i8 (MVE_VREV64_8 MQPR:$src))>;
  def : Pat<(v16i8 (bitconvert (v2i64 MQPR:$src))), (v16i8 (MVE_VREV64_8 MQPR:$src))>;
  def : Pat<(v16i8 (bitconvert (v4f32 MQPR:$src))), (v16i8 (MVE_VREV32_8 MQPR:$src))>;
  def : Pat<(v16i8 (bitconvert (v4i32 MQPR:$src))), (v16i8 (MVE_VREV32_8 MQPR:$src))>;
  def : Pat<(v16i8 (bitconvert (v8f16 MQPR:$src))), (v16i8 (MVE_VREV16_8 MQPR:$src))>;
  def : Pat<(v16i8 (bitconvert (v8i16 MQPR:$src))), (v16i8 (MVE_VREV16_8 MQPR:$src))>;
}
