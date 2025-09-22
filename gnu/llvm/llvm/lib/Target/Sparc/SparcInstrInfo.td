//===-- SparcInstrInfo.td - Target Description for Sparc Target -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the Sparc instructions in TableGen format.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Instruction format superclass
//===----------------------------------------------------------------------===//

include "SparcInstrFormats.td"

//===----------------------------------------------------------------------===//
// Feature predicates.
//===----------------------------------------------------------------------===//

// True when generating 32-bit code.
def Is32Bit : Predicate<"!Subtarget->is64Bit()">;

// True when generating 64-bit code. This also implies HasV9.
def Is64Bit : Predicate<"Subtarget->is64Bit()">;

def UseSoftMulDiv : Predicate<"Subtarget->useSoftMulDiv()">,
              AssemblerPredicate<(all_of FeatureSoftMulDiv)>;

// HasV9 - This predicate is true when the target processor supports V9
// instructions.  Note that the machine may be running in 32-bit mode.
def HasV9   : Predicate<"Subtarget->isV9()">,
              AssemblerPredicate<(all_of FeatureV9)>;

// HasNoV9 - This predicate is true when the target doesn't have V9
// instructions.  Use of this is just a hack for the isel not having proper
// costs for V8 instructions that are more expensive than their V9 ones.
def HasNoV9 : Predicate<"!Subtarget->isV9()">;

// HasVIS - This is true when the target processor has VIS extensions.
def HasVIS : Predicate<"Subtarget->isVIS()">,
             AssemblerPredicate<(all_of FeatureVIS)>;
def HasVIS2 : Predicate<"Subtarget->isVIS2()">,
             AssemblerPredicate<(all_of FeatureVIS2)>;
def HasVIS3 : Predicate<"Subtarget->isVIS3()">,
             AssemblerPredicate<(all_of FeatureVIS3)>;

// HasHardQuad - This is true when the target processor supports quad floating
// point instructions.
def HasHardQuad : Predicate<"Subtarget->hasHardQuad()">;

// HasLeonCASA - This is true when the target processor supports the Leon CASA
// instruction.
def HasLeonCASA : Predicate<"Subtarget->hasLeonCasa()">;

// HasCASA - This is true when the target processor supports CASA instruction.
def HasCASA : Predicate<"Subtarget->hasLeonCasa() || Subtarget->isV9()">,
              AssemblerPredicate<(any_of LeonCASA, FeatureV9)>;

// HasPWRPSR - This is true when the target processor supports partial
// writes to the PSR register that only affects the ET field.
def HasPWRPSR : Predicate<"Subtarget->hasPWRPSR()">,
                AssemblerPredicate<(all_of FeaturePWRPSR)>;

// HasUMAC_SMAC - This is true when the target processor supports the
// UMAC and SMAC instructions
def HasUMAC_SMAC : Predicate<"Subtarget->hasUmacSmac()">;

def HasNoFdivSqrtFix : Predicate<"!Subtarget->fixAllFDIVSQRT()">;
def HasFMULS : Predicate<"!Subtarget->hasNoFMULS()">;
def HasFSMULD : Predicate<"!Subtarget->hasNoFSMULD()">;

// UseDeprecatedInsts - This predicate is true when the target processor is a
// V8, or when it is V9 but the V8 deprecated instructions are efficient enough
// to use when appropriate.  In either of these cases, the instruction selector
// will pick deprecated instructions.
def UseDeprecatedInsts : Predicate<"Subtarget->useV8DeprecatedInsts()">;

//===----------------------------------------------------------------------===//
// Instruction Pattern Stuff
//===----------------------------------------------------------------------===//

def simm10  : PatLeaf<(imm), [{ return isInt<10>(N->getSExtValue()); }]>;

def simm11  : PatLeaf<(imm), [{ return isInt<11>(N->getSExtValue()); }]>;

def simm13  : PatLeaf<(imm), [{ return isInt<13>(N->getSExtValue()); }]>;

def LO10 : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant((unsigned)N->getZExtValue() & 1023, SDLoc(N),
                                   MVT::i32);
}]>;

def HI22 : SDNodeXForm<imm, [{
  // Transformation function: shift the immediate value down into the low bits.
  return CurDAG->getTargetConstant((unsigned)N->getZExtValue() >> 10, SDLoc(N),
                                   MVT::i32);
}]>;

// Return the complement of a HI22 immediate value.
def HI22_not : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(~(unsigned)N->getZExtValue() >> 10, SDLoc(N),
                                   MVT::i32);
}]>;

def SETHIimm : PatLeaf<(imm), [{
  return isShiftedUInt<22, 10>(N->getZExtValue());
}], HI22>;

// The N->hasOneUse() prevents the immediate from being instantiated in both
// normal and complement form.
def SETHIimm_not : PatLeaf<(i32 imm), [{
  return N->hasOneUse() && isShiftedUInt<22, 10>(~(unsigned)N->getZExtValue());
}], HI22_not>;

// Addressing modes.
def ADDRrr : ComplexPattern<iPTR, 2, "SelectADDRrr", [], []>;
def ADDRri : ComplexPattern<iPTR, 2, "SelectADDRri", [], []>;

// Constrained operands for the shift operations.
class ShiftAmtImmAsmOperand<int Bits> : AsmOperandClass {
    let Name = "ShiftAmtImm" # Bits;
    let ParserMethod = "parseShiftAmtImm<" # Bits # ">";
}
def shift_imm5 : Operand<i32> {
  let ParserMatchClass = ShiftAmtImmAsmOperand<5>;
}
def shift_imm6 : Operand<i32> {
  let ParserMatchClass = ShiftAmtImmAsmOperand<6>;
}

// Address operands
def SparcMEMrrAsmOperand : AsmOperandClass {
  let Name = "MEMrr";
  let ParserMethod = "parseMEMOperand";
}

def SparcMEMriAsmOperand : AsmOperandClass {
  let Name = "MEMri";
  let ParserMethod = "parseMEMOperand";
}

def MEMrr : Operand<iPTR> {
  let PrintMethod = "printMemOperand";
  let MIOperandInfo = (ops ptr_rc, ptr_rc);
  let ParserMatchClass = SparcMEMrrAsmOperand;
}
def MEMri : Operand<iPTR> {
  let PrintMethod = "printMemOperand";
  let MIOperandInfo = (ops ptr_rc, i32imm);
  let ParserMatchClass = SparcMEMriAsmOperand;
}

// Represents a tail relocation operand for instructions such as add, ld, call.
class SparcTailRelocSymAsmOperand<string Kind> : AsmOperandClass {
  let Name = "TailRelocSym" # Kind;
  let RenderMethod = "addTailRelocSymOperands";
  let PredicateMethod = "isTailRelocSym";
  let ParserMethod = "parseTailRelocSym<TailRelocKind::" # Kind # ">";
}

def TailRelocSymGOTLoad : Operand<iPTR> {
  let ParserMatchClass = SparcTailRelocSymAsmOperand<"Load_GOT">;
}

def TailRelocSymTLSAdd : Operand<iPTR> {
  let ParserMatchClass = SparcTailRelocSymAsmOperand<"Add_TLS">;
}

def TailRelocSymTLSLoad : Operand<iPTR> {
  let ParserMatchClass = SparcTailRelocSymAsmOperand<"Load_TLS">;
}

def TailRelocSymTLSCall : Operand<iPTR> {
  let ParserMatchClass = SparcTailRelocSymAsmOperand<"Call_TLS">;
}

def SparcMembarTagAsmOperand : AsmOperandClass {
  let Name = "MembarTag";
  let ParserMethod = "parseMembarTag";
}

def MembarTag : Operand<i32> {
  let PrintMethod = "printMembarTag";
  let ParserMatchClass = SparcMembarTagAsmOperand;
}

def SparcASITagAsmOperand : AsmOperandClass {
  let Name = "ASITag";
  let ParserMethod = "parseASITag";
}

def ASITag : Operand<i32> {
  let PrintMethod = "printASITag";
  let ParserMatchClass = SparcASITagAsmOperand;
}

def SparcPrefetchTagAsmOperand : AsmOperandClass {
  let Name = "PrefetchTag";
  let ParserMethod = "parsePrefetchTag";
}

def PrefetchTag : Operand<i32> {
  let PrintMethod = "printPrefetchTag";
  let ParserMatchClass = SparcPrefetchTagAsmOperand;
}

// Branch targets have OtherVT type.
def brtarget : Operand<OtherVT> {
  let EncoderMethod = "getBranchTargetOpValue";
}

def bprtarget : Operand<OtherVT> {
  let EncoderMethod = "getBranchPredTargetOpValue";
}

def bprtarget16 : Operand<OtherVT> {
  let EncoderMethod = "getBranchOnRegTargetOpValue";
}

def SparcCallTargetAsmOperand : AsmOperandClass {
  let Name = "CallTarget";
  let ParserMethod = "parseCallTarget";
}

def calltarget : Operand<i32> {
  let EncoderMethod = "getCallTargetOpValue";
  let DecoderMethod = "DecodeCall";
  let ParserMatchClass = SparcCallTargetAsmOperand;
}

def simm13Op : Operand<iPTR> {
  let OperandType = "OPERAND_IMMEDIATE";
  let DecoderMethod = "DecodeSIMM13";
  let EncoderMethod = "getSImm13OpValue";
}

// Operand for printing out a condition code.
let PrintMethod = "printCCOperand" in {
  def CCOp : Operand<i32>;
  def RegCCOp : Operand<i32>;
}

def SDTSPcmpicc :
SDTypeProfile<0, 2, [SDTCisInt<0>, SDTCisSameAs<0, 1>]>;
def SDTSPcmpfcc :
SDTypeProfile<0, 2, [SDTCisFP<0>, SDTCisSameAs<0, 1>]>;
def SDTSPbrcc :
SDTypeProfile<0, 2, [SDTCisVT<0, OtherVT>, SDTCisVT<1, i32>]>;
def SDTSPbrreg :
SDTypeProfile<0, 3, [SDTCisVT<0, OtherVT>, SDTCisVT<1, i32>, SDTCisVT<2, i64>]>;
def SDTSPselectcc :
SDTypeProfile<1, 3, [SDTCisSameAs<0, 1>, SDTCisSameAs<1, 2>, SDTCisVT<3, i32>]>;
def SDTSPselectreg :
SDTypeProfile<1, 4, [SDTCisSameAs<0, 1>, SDTCisSameAs<1, 2>, SDTCisVT<3, i32>, SDTCisVT<4, i64>]>;
def SDTSPFTOI :
SDTypeProfile<1, 1, [SDTCisVT<0, f32>, SDTCisFP<1>]>;
def SDTSPITOF :
SDTypeProfile<1, 1, [SDTCisFP<0>, SDTCisVT<1, f32>]>;
def SDTSPFTOX :
SDTypeProfile<1, 1, [SDTCisVT<0, f64>, SDTCisFP<1>]>;
def SDTSPXTOF :
SDTypeProfile<1, 1, [SDTCisFP<0>, SDTCisVT<1, f64>]>;

def SDTSPtlsadd :
SDTypeProfile<1, 3, [SDTCisInt<0>, SDTCisSameAs<0, 1>, SDTCisPtrTy<2>]>;
def SDTSPtlsld :
SDTypeProfile<1, 2, [SDTCisPtrTy<0>, SDTCisPtrTy<1>]>;

def SDTSPloadgdop :
SDTypeProfile<1, 2, [SDTCisPtrTy<0>, SDTCisPtrTy<1>]>;

def SPcmpicc : SDNode<"SPISD::CMPICC", SDTSPcmpicc, [SDNPOutGlue]>;
def SPcmpfcc : SDNode<"SPISD::CMPFCC", SDTSPcmpfcc, [SDNPOutGlue]>;
def SPcmpfccv9 : SDNode<"SPISD::CMPFCC_V9", SDTSPcmpfcc, [SDNPOutGlue]>;
def SPbricc : SDNode<"SPISD::BRICC", SDTSPbrcc, [SDNPHasChain, SDNPInGlue]>;
def SPbpicc : SDNode<"SPISD::BPICC", SDTSPbrcc, [SDNPHasChain, SDNPInGlue]>;
def SPbpxcc : SDNode<"SPISD::BPXCC", SDTSPbrcc, [SDNPHasChain, SDNPInGlue]>;
def SPbrfcc : SDNode<"SPISD::BRFCC", SDTSPbrcc, [SDNPHasChain, SDNPInGlue]>;
def SPbrfccv9 : SDNode<"SPISD::BRFCC_V9", SDTSPbrcc, [SDNPHasChain, SDNPInGlue]>;
def SPbrreg : SDNode<"SPISD::BR_REG", SDTSPbrreg, [SDNPHasChain, SDNPInGlue]>;

def SPhi    : SDNode<"SPISD::Hi", SDTIntUnaryOp>;
def SPlo    : SDNode<"SPISD::Lo", SDTIntUnaryOp>;

def SPftoi  : SDNode<"SPISD::FTOI", SDTSPFTOI>;
def SPitof  : SDNode<"SPISD::ITOF", SDTSPITOF>;
def SPftox  : SDNode<"SPISD::FTOX", SDTSPFTOX>;
def SPxtof  : SDNode<"SPISD::XTOF", SDTSPXTOF>;

def SPselecticc : SDNode<"SPISD::SELECT_ICC", SDTSPselectcc, [SDNPInGlue]>;
def SPselectxcc : SDNode<"SPISD::SELECT_XCC", SDTSPselectcc, [SDNPInGlue]>;
def SPselectfcc : SDNode<"SPISD::SELECT_FCC", SDTSPselectcc, [SDNPInGlue]>;
def SPselectreg : SDNode<"SPISD::SELECT_REG", SDTSPselectreg, [SDNPInGlue]>;

//  These are target-independent nodes, but have target-specific formats.
def SDT_SPCallSeqStart : SDCallSeqStart<[ SDTCisVT<0, i32>,
                                          SDTCisVT<1, i32> ]>;
def SDT_SPCallSeqEnd   : SDCallSeqEnd<[ SDTCisVT<0, i32>,
                                        SDTCisVT<1, i32> ]>;

def callseq_start : SDNode<"ISD::CALLSEQ_START", SDT_SPCallSeqStart,
                           [SDNPHasChain, SDNPOutGlue]>;
def callseq_end   : SDNode<"ISD::CALLSEQ_END",   SDT_SPCallSeqEnd,
                           [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

def SDT_SPCall    : SDTypeProfile<0, -1, [SDTCisVT<0, i32>]>;
def call          : SDNode<"SPISD::CALL", SDT_SPCall,
                           [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                            SDNPVariadic]>;

def tailcall      : SDNode<"SPISD::TAIL_CALL", SDT_SPCall,
                           [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                            SDNPVariadic]>;

def SDT_SPRet     : SDTypeProfile<0, 1, [SDTCisVT<0, i32>]>;
def retglue       : SDNode<"SPISD::RET_GLUE", SDT_SPRet,
                           [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;

def flushw        : SDNode<"SPISD::FLUSHW", SDTNone,
                           [SDNPHasChain, SDNPSideEffect, SDNPMayStore]>;

def tlsadd        : SDNode<"SPISD::TLS_ADD", SDTSPtlsadd>;
def tlsld         : SDNode<"SPISD::TLS_LD",  SDTSPtlsld>;
def tlscall       : SDNode<"SPISD::TLS_CALL", SDT_SPCall,
                            [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                             SDNPVariadic]>;

def load_gdop : SDNode<"SPISD::LOAD_GDOP",  SDTSPloadgdop>;

def getPCX        : Operand<iPTR> {
  let PrintMethod = "printGetPCX";
}

//===----------------------------------------------------------------------===//
// SPARC Flag Conditions
//===----------------------------------------------------------------------===//

// Note that these values must be kept in sync with the CCOp::CondCode enum
// values.
class ICC_VAL<int N> : PatLeaf<(i32 N)>;
def ICC_NE  : ICC_VAL< 9>;  // Not Equal
def ICC_E   : ICC_VAL< 1>;  // Equal
def ICC_G   : ICC_VAL<10>;  // Greater
def ICC_LE  : ICC_VAL< 2>;  // Less or Equal
def ICC_GE  : ICC_VAL<11>;  // Greater or Equal
def ICC_L   : ICC_VAL< 3>;  // Less
def ICC_GU  : ICC_VAL<12>;  // Greater Unsigned
def ICC_LEU : ICC_VAL< 4>;  // Less or Equal Unsigned
def ICC_CC  : ICC_VAL<13>;  // Carry Clear/Great or Equal Unsigned
def ICC_CS  : ICC_VAL< 5>;  // Carry Set/Less Unsigned
def ICC_POS : ICC_VAL<14>;  // Positive
def ICC_NEG : ICC_VAL< 6>;  // Negative
def ICC_VC  : ICC_VAL<15>;  // Overflow Clear
def ICC_VS  : ICC_VAL< 7>;  // Overflow Set

class FCC_VAL<int N> : PatLeaf<(i32 N)>;
def FCC_U   : FCC_VAL<23>;  // Unordered
def FCC_G   : FCC_VAL<22>;  // Greater
def FCC_UG  : FCC_VAL<21>;  // Unordered or Greater
def FCC_L   : FCC_VAL<20>;  // Less
def FCC_UL  : FCC_VAL<19>;  // Unordered or Less
def FCC_LG  : FCC_VAL<18>;  // Less or Greater
def FCC_NE  : FCC_VAL<17>;  // Not Equal
def FCC_E   : FCC_VAL<25>;  // Equal
def FCC_UE  : FCC_VAL<26>;  // Unordered or Equal
def FCC_GE  : FCC_VAL<27>;  // Greater or Equal
def FCC_UGE : FCC_VAL<28>;  // Unordered or Greater or Equal
def FCC_LE  : FCC_VAL<29>;  // Less or Equal
def FCC_ULE : FCC_VAL<30>;  // Unordered or Less or Equal
def FCC_O   : FCC_VAL<31>;  // Ordered

class CPCC_VAL<int N> : PatLeaf<(i32 N)>;
def CPCC_3   : CPCC_VAL<39>;  // 3
def CPCC_2   : CPCC_VAL<38>;  // 2
def CPCC_23  : CPCC_VAL<37>;  // 2 or 3
def CPCC_1   : CPCC_VAL<36>;  // 1
def CPCC_13  : CPCC_VAL<35>;  // 1 or 3
def CPCC_12  : CPCC_VAL<34>;  // 1 or 2
def CPCC_123 : CPCC_VAL<33>;  // 1 or 2 or 3
def CPCC_0   : CPCC_VAL<41>;  // 0
def CPCC_03  : CPCC_VAL<42>;  // 0 or 3
def CPCC_02  : CPCC_VAL<43>;  // 0 or 2
def CPCC_023 : CPCC_VAL<44>;  // 0 or 2 or 3
def CPCC_01  : CPCC_VAL<45>;  // 0 or 1
def CPCC_013 : CPCC_VAL<46>;  // 0 or 1 or 3
def CPCC_012 : CPCC_VAL<47>;  // 0 or 1 or 2

class RegCC_VAL<int N> : PatLeaf<(i32 N)>;
def RegCC_Z   : RegCC_VAL<49>;  // Zero
def RegCC_LEZ : RegCC_VAL<50>;  // Lees or equal than zero
def RegCC_LZ  : RegCC_VAL<51>;  // Less than zero
def RegCC_NZ  : RegCC_VAL<53>;  // Not zero
def RegCC_GZ  : RegCC_VAL<54>;  // Greater than zero
def RegCC_GEZ : RegCC_VAL<55>;  // Greater or equal to zero

//===----------------------------------------------------------------------===//
// Instruction Class Templates
//===----------------------------------------------------------------------===//

/// F3_12 multiclass - Define a normal F3_1/F3_2 pattern in one shot.
multiclass F3_12<string OpcStr, bits<6> Op3Val, SDNode OpNode,
                 RegisterClass RC, ValueType Ty, Operand immOp,
                 InstrItinClass itin = IIC_iu_instr> {
  def rr  : F3_1<2, Op3Val,
                 (outs RC:$rd), (ins RC:$rs1, RC:$rs2),
                 !strconcat(OpcStr, " $rs1, $rs2, $rd"),
                 [(set Ty:$rd, (OpNode Ty:$rs1, Ty:$rs2))],
                 itin>;
  def ri  : F3_2<2, Op3Val,
                 (outs RC:$rd), (ins RC:$rs1, immOp:$simm13),
                 !strconcat(OpcStr, " $rs1, $simm13, $rd"),
                 [(set Ty:$rd, (OpNode Ty:$rs1, (Ty simm13:$simm13)))],
                 itin>;
}

/// F3_12np multiclass - Define a normal F3_1/F3_2 pattern in one shot, with no
/// pattern.
multiclass F3_12np<string OpcStr, bits<6> Op3Val, InstrItinClass itin = IIC_iu_instr> {
  def rr  : F3_1<2, Op3Val,
                 (outs IntRegs:$rd), (ins IntRegs:$rs1, IntRegs:$rs2),
                 !strconcat(OpcStr, " $rs1, $rs2, $rd"), [],
                 itin>;
  def ri  : F3_2<2, Op3Val,
                 (outs IntRegs:$rd), (ins IntRegs:$rs1, simm13Op:$simm13),
                 !strconcat(OpcStr, " $rs1, $simm13, $rd"), [],
                 itin>;
}

// Load multiclass - Define both Reg+Reg/Reg+Imm patterns in one shot.
multiclass Load<string OpcStr, bits<6> Op3Val, SDPatternOperator OpNode,
           RegisterClass RC, ValueType Ty, InstrItinClass itin = IIC_iu_instr> {
  def rr  : F3_1<3, Op3Val,
                 (outs RC:$rd), (ins (MEMrr $rs1, $rs2):$addr),
                 !strconcat(OpcStr, " [$addr], $rd"),
                 [(set Ty:$rd, (OpNode ADDRrr:$addr))],
                 itin>;
  def ri  : F3_2<3, Op3Val,
                 (outs RC:$rd), (ins (MEMri $rs1, $simm13):$addr),
                 !strconcat(OpcStr, " [$addr], $rd"),
                 [(set Ty:$rd, (OpNode ADDRri:$addr))],
                 itin>;
}

// TODO: Instructions of the LoadASI class are currently asm only; hooking up
// CodeGen's address spaces to use these is a future task.
multiclass LoadASI<string OpcStr, bits<6> Op3Val, RegisterClass RC> {
  def rr  : F3_1_asi<3, Op3Val, (outs RC:$rd), (ins (MEMrr $rs1, $rs2):$addr, ASITag:$asi),
                     !strconcat(OpcStr, "a [$addr] $asi, $rd"),
                     []>;

  let Predicates = [HasV9], Uses = [ASR3] in
  def ri  : F3_2<3, Op3Val, (outs RC:$rd), (ins (MEMri $rs1, $simm13):$addr),
                 !strconcat(OpcStr, "a [$addr] %asi, $rd"),
                 []>;
}

// LoadA multiclass - As above, but also define alternate address space variant
multiclass LoadA<string OpcStr, bits<6> Op3Val, bits<6> LoadAOp3Val,
                 SDPatternOperator OpNode, RegisterClass RC, ValueType Ty,
                 InstrItinClass itin = NoItinerary> :
             Load<OpcStr, Op3Val, OpNode, RC, Ty, itin> {
  defm A   : LoadASI<OpcStr, LoadAOp3Val, RC>;
}


// The LDSTUB instruction is supported for asm only.
// It is unlikely that general-purpose code could make use of it.
// CAS is preferred for sparc v9.
def LDSTUBrr : F3_1<3, 0b001101, (outs IntRegs:$rd), (ins (MEMrr $rs1, $rs2):$addr),
                    "ldstub [$addr], $rd", []>;
def LDSTUBri : F3_2<3, 0b001101, (outs IntRegs:$rd), (ins (MEMri $rs1, $simm13):$addr),
                    "ldstub [$addr], $rd", []>;
def LDSTUBArr : F3_1_asi<3, 0b011101, (outs IntRegs:$rd),
                         (ins (MEMrr $rs1, $rs2):$addr, ASITag:$asi),
                         "ldstuba [$addr] $asi, $rd", []>;
let Predicates = [HasV9], Uses = [ASR3] in
def LDSTUBAri : F3_2<3, 0b011101, (outs IntRegs:$rd),
                         (ins (MEMri $rs1, $simm13):$addr),
                         "ldstuba [$addr] %asi, $rd", []>;

// Store multiclass - Define both Reg+Reg/Reg+Imm patterns in one shot.
multiclass Store<string OpcStr, bits<6> Op3Val, SDPatternOperator OpNode,
           RegisterClass RC, ValueType Ty, InstrItinClass itin = IIC_st> {
  def rr  : F3_1<3, Op3Val,
                 (outs), (ins (MEMrr $rs1, $rs2):$addr, RC:$rd),
                 !strconcat(OpcStr, " $rd, [$addr]"),
                 [(OpNode Ty:$rd, ADDRrr:$addr)],
                 itin>;
  def ri  : F3_2<3, Op3Val,
                 (outs), (ins (MEMri $rs1, $simm13):$addr, RC:$rd),
                 !strconcat(OpcStr, " $rd, [$addr]"),
                 [(OpNode Ty:$rd, ADDRri:$addr)],
                 itin>;
}

// TODO: Instructions of the StoreASI class are currently asm only; hooking up
// CodeGen's address spaces to use these is a future task.
multiclass StoreASI<string OpcStr, bits<6> Op3Val, RegisterClass RC,
               InstrItinClass itin = IIC_st> {
  def rr : F3_1_asi<3, Op3Val, (outs), (ins (MEMrr $rs1, $rs2):$addr, RC:$rd, ASITag:$asi),
           !strconcat(OpcStr, "a $rd, [$addr] $asi"),
           [],
           itin>;

  let Predicates = [HasV9], Uses = [ASR3] in
  def ri : F3_2<3, Op3Val, (outs), (ins (MEMri $rs1, $simm13):$addr, RC:$rd),
           !strconcat(OpcStr, "a $rd, [$addr] %asi"),
           [],
           itin>;
}

multiclass StoreA<string OpcStr, bits<6> Op3Val, bits<6> StoreAOp3Val,
                  SDPatternOperator OpNode, RegisterClass RC, ValueType Ty> :
             Store<OpcStr, Op3Val, OpNode, RC, Ty> {
  defm A   : StoreASI<OpcStr, StoreAOp3Val, RC>;
}

//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//

// Pseudo instructions.
class Pseudo<dag outs, dag ins, string asmstr, list<dag> pattern>
   : InstSP<outs, ins, asmstr, pattern> {
  let isCodeGenOnly = 1;
  let isPseudo = 1;
}

// GETPCX for PIC
let Defs = [O7] in {
  def GETPCX : Pseudo<(outs getPCX:$getpcseq), (ins), "$getpcseq", [] >;
}

let Defs = [O6], Uses = [O6] in {
def ADJCALLSTACKDOWN : Pseudo<(outs), (ins i32imm:$amt1, i32imm:$amt2),
                               "!ADJCALLSTACKDOWN $amt1, $amt2",
                               [(callseq_start timm:$amt1, timm:$amt2)]>;
def ADJCALLSTACKUP : Pseudo<(outs), (ins i32imm:$amt1, i32imm:$amt2),
                            "!ADJCALLSTACKUP $amt1",
                            [(callseq_end timm:$amt1, timm:$amt2)]>;
}

let hasSideEffects = 1, mayStore = 1 in {
  let rd = 0, rs1 = 0, rs2 = 0 in
    def FLUSHW : F3_1<0b10, 0b101011, (outs), (ins),
                      "flushw",
                      [(flushw)]>, Requires<[HasV9]>;
  let rd = 8, rs1 = 0, simm13 = 3 in
    def TA3 : F3_2<0b10, 0b111010, (outs), (ins),
                   "ta 3",
                   [(flushw)]>;
}

// SELECT_CC_* - Used to implement the SELECT_CC DAG operation.  Expanded after
// instruction selection into a branch sequence.  This has to handle all
// permutations of selection between i32/f32/f64 on ICC and FCC.
// Expanded after instruction selection.
let Uses = [ICC], usesCustomInserter = 1 in {
  def SELECT_CC_Int_ICC
   : Pseudo<(outs IntRegs:$dst), (ins IntRegs:$T, IntRegs:$F, i32imm:$Cond),
            "; SELECT_CC_Int_ICC PSEUDO!",
            [(set i32:$dst, (SPselecticc i32:$T, i32:$F, imm:$Cond))]>;
  def SELECT_CC_FP_ICC
   : Pseudo<(outs FPRegs:$dst), (ins FPRegs:$T, FPRegs:$F, i32imm:$Cond),
            "; SELECT_CC_FP_ICC PSEUDO!",
            [(set f32:$dst, (SPselecticc f32:$T, f32:$F, imm:$Cond))]>;

  def SELECT_CC_DFP_ICC
   : Pseudo<(outs DFPRegs:$dst), (ins DFPRegs:$T, DFPRegs:$F, i32imm:$Cond),
            "; SELECT_CC_DFP_ICC PSEUDO!",
            [(set f64:$dst, (SPselecticc f64:$T, f64:$F, imm:$Cond))]>;

  def SELECT_CC_QFP_ICC
   : Pseudo<(outs QFPRegs:$dst), (ins QFPRegs:$T, QFPRegs:$F, i32imm:$Cond),
            "; SELECT_CC_QFP_ICC PSEUDO!",
            [(set f128:$dst, (SPselecticc f128:$T, f128:$F, imm:$Cond))]>;
}

let Uses = [ICC], usesCustomInserter = 1 in {
  def SELECT_CC_Int_XCC
   : Pseudo<(outs IntRegs:$dst), (ins IntRegs:$T, IntRegs:$F, i32imm:$Cond),
            "; SELECT_CC_Int_XCC PSEUDO!",
            [(set i32:$dst, (SPselectxcc i32:$T, i32:$F, imm:$Cond))]>;
  def SELECT_CC_FP_XCC
   : Pseudo<(outs FPRegs:$dst), (ins FPRegs:$T, FPRegs:$F, i32imm:$Cond),
            "; SELECT_CC_FP_XCC PSEUDO!",
            [(set f32:$dst, (SPselectxcc f32:$T, f32:$F, imm:$Cond))]>;

  def SELECT_CC_DFP_XCC
   : Pseudo<(outs DFPRegs:$dst), (ins DFPRegs:$T, DFPRegs:$F, i32imm:$Cond),
            "; SELECT_CC_DFP_XCC PSEUDO!",
            [(set f64:$dst, (SPselectxcc f64:$T, f64:$F, imm:$Cond))]>;

  def SELECT_CC_QFP_XCC
   : Pseudo<(outs QFPRegs:$dst), (ins QFPRegs:$T, QFPRegs:$F, i32imm:$Cond),
            "; SELECT_CC_QFP_XCC PSEUDO!",
            [(set f128:$dst, (SPselectxcc f128:$T, f128:$F, imm:$Cond))]>;
}

let usesCustomInserter = 1, Uses = [FCC0] in {

  def SELECT_CC_Int_FCC
   : Pseudo<(outs IntRegs:$dst), (ins IntRegs:$T, IntRegs:$F, i32imm:$Cond),
            "; SELECT_CC_Int_FCC PSEUDO!",
            [(set i32:$dst, (SPselectfcc i32:$T, i32:$F, imm:$Cond))]>;

  def SELECT_CC_FP_FCC
   : Pseudo<(outs FPRegs:$dst), (ins FPRegs:$T, FPRegs:$F, i32imm:$Cond),
            "; SELECT_CC_FP_FCC PSEUDO!",
            [(set f32:$dst, (SPselectfcc f32:$T, f32:$F, imm:$Cond))]>;
  def SELECT_CC_DFP_FCC
   : Pseudo<(outs DFPRegs:$dst), (ins DFPRegs:$T, DFPRegs:$F, i32imm:$Cond),
            "; SELECT_CC_DFP_FCC PSEUDO!",
            [(set f64:$dst, (SPselectfcc f64:$T, f64:$F, imm:$Cond))]>;
  def SELECT_CC_QFP_FCC
   : Pseudo<(outs QFPRegs:$dst), (ins QFPRegs:$T, QFPRegs:$F, i32imm:$Cond),
            "; SELECT_CC_QFP_FCC PSEUDO!",
            [(set f128:$dst, (SPselectfcc f128:$T, f128:$F, imm:$Cond))]>;
}

// Section B.1 - Load Integer Instructions, p. 90
defm LDSB : LoadA<"ldsb", 0b001001, 0b011001, sextloadi8,  IntRegs, i32>;
defm LDSH : LoadA<"ldsh", 0b001010, 0b011010, sextloadi16, IntRegs, i32>;
defm LDUB : LoadA<"ldub", 0b000001, 0b010001, zextloadi8,  IntRegs, i32>;
defm LDUH : LoadA<"lduh", 0b000010, 0b010010, zextloadi16, IntRegs, i32>;
defm LD   : LoadA<"ld",   0b000000, 0b010000, load,        IntRegs, i32>;
defm LDD : LoadA<"ldd", 0b000011, 0b010011, load, IntPair, v2i32, IIC_ldd>;

// Section B.2 - Load Floating-point Instructions, p. 92
defm LDF     : Load<"ld",  0b100000, load,    FPRegs,  f32, IIC_iu_or_fpu_instr>;
defm LDDF    : Load<"ldd", 0b100011, load,    DFPRegs, f64, IIC_ldd>;

let DecoderNamespace = "SparcV9", Predicates = [HasV9] in {
  defm LDFA    : LoadASI<"ld",  0b110000, FPRegs>;
  defm LDDFA   : LoadASI<"ldd", 0b110011, DFPRegs>;
  defm LDQF    : LoadA<"ldq", 0b100010, 0b110010, load, QFPRegs, f128>,
                 Requires<[HasHardQuad]>;
}

// Coprocessor instructions were removed in v9.
let DecoderNamespace = "SparcV8", Predicates = [HasNoV9] in {
  defm LDC    : Load<"ld", 0b110000, load, CoprocRegs, i32>;
  defm LDDC   : Load<"ldd", 0b110011, load, CoprocPair, v2i32, IIC_ldd>;
}

let Defs = [CPSR] in {
  let rd = 0 in {
    def LDCSRrr : F3_1<3, 0b110001, (outs), (ins (MEMrr $rs1, $rs2):$addr),
                       "ld [$addr], %csr", []>;
    def LDCSRri : F3_2<3, 0b110001, (outs), (ins (MEMri $rs1, $simm13):$addr),
                       "ld [$addr], %csr", []>;
  }
}

let Defs = [FSR] in {
  let rd = 0 in {
    def LDFSRrr : F3_1<3, 0b100001, (outs), (ins (MEMrr $rs1, $rs2):$addr),
		   "ld [$addr], %fsr", [], IIC_iu_or_fpu_instr>;
    def LDFSRri : F3_2<3, 0b100001, (outs), (ins (MEMri $rs1, $simm13):$addr),
		   "ld [$addr], %fsr", [], IIC_iu_or_fpu_instr>;
  }
  let rd = 1 in {
    def LDXFSRrr : F3_1<3, 0b100001, (outs), (ins (MEMrr $rs1, $rs2):$addr),
		   "ldx [$addr], %fsr", []>, Requires<[HasV9]>;
    def LDXFSRri : F3_2<3, 0b100001, (outs), (ins (MEMri $rs1, $simm13):$addr),
		   "ldx [$addr], %fsr", []>, Requires<[HasV9]>;
  }
}

let mayLoad = 1, isAsmParserOnly = 1 in {
  def GDOP_LDrr : F3_1<3, 0b000000,
                      (outs IntRegs:$rd),
                      (ins (MEMrr $rs1, $rs2):$addr, TailRelocSymGOTLoad:$sym),
                      "ld [$addr], $rd, $sym",
                      [(set i32:$rd,
                          (load_gdop ADDRrr:$addr, tglobaladdr:$sym))]>;
}

// Section B.4 - Store Integer Instructions, p. 95
defm STB   : StoreA<"stb", 0b000101, 0b010101, truncstorei8,  IntRegs, i32>;
defm STH   : StoreA<"sth", 0b000110, 0b010110, truncstorei16, IntRegs, i32>;
defm ST    : StoreA<"st",  0b000100, 0b010100, store,         IntRegs, i32>;
defm STD   : StoreA<"std", 0b000111, 0b010111, store, IntPair, v2i32>;

// Section B.5 - Store Floating-point Instructions, p. 97
defm STF    : Store<"st",  0b100100, store,         FPRegs,  f32>;
defm STDF   : Store<"std", 0b100111, store,         DFPRegs, f64, IIC_std>;

let DecoderNamespace = "SparcV9", Predicates = [HasV9] in {
  defm STFA  : StoreASI<"st",  0b110100, FPRegs>;
  defm STDFA : StoreASI<"std", 0b110111, DFPRegs>;
  defm STQF  : StoreA<"stq", 0b100110, 0b110110, store, QFPRegs, f128>,
              Requires<[HasHardQuad]>;
}

// Coprocessor instructions were removed in v9.
let DecoderNamespace = "SparcV8", Predicates = [HasNoV9] in {
  defm STC   : Store<"st", 0b110100, store, CoprocRegs, i32>;
  defm STDC   : Store<"std", 0b110111, store, CoprocPair, v2i32, IIC_std>;
}

let rd = 0 in {
  let mayStore = 1, Uses = [CPSR] in {
    def STCSRrr : F3_1<3, 0b110101, (outs), (ins (MEMrr $rs1, $rs2):$addr),
                       "st %csr, [$addr]", [], IIC_st>;
    def STCSRri : F3_2<3, 0b110101, (outs), (ins (MEMri $rs1, $simm13):$addr),
                       "st %csr, [$addr]", [], IIC_st>;
  }
  let mayStore = 1, Uses = [CPQ] in {
    def STDCQrr : F3_1<3, 0b110110, (outs), (ins (MEMrr $rs1, $rs2):$addr),
                       "std %cq, [$addr]", [], IIC_std>;
    def STDCQri : F3_2<3, 0b110110, (outs), (ins (MEMri $rs1, $simm13):$addr),
                       "std %cq, [$addr]", [], IIC_std>;
  }
}

let rd = 0 in {
  let mayStore = 1, Uses = [FSR] in {
    def STFSRrr : F3_1<3, 0b100101, (outs), (ins (MEMrr $rs1, $rs2):$addr),
		   "st %fsr, [$addr]", [], IIC_st>;
    def STFSRri : F3_2<3, 0b100101, (outs), (ins (MEMri $rs1, $simm13):$addr),
		   "st %fsr, [$addr]", [], IIC_st>;
  }
  let mayStore = 1, Defs = [FQ] in {
    def STDFQrr : F3_1<3, 0b100110, (outs), (ins (MEMrr $rs1, $rs2):$addr),
		   "std %fq, [$addr]", [], IIC_std>;
    def STDFQri : F3_2<3, 0b100110, (outs), (ins (MEMri $rs1, $simm13):$addr),
		   "std %fq, [$addr]", [], IIC_std>;
  }
}
let rd = 1, mayStore = 1, Uses = [FSR] in {
  def STXFSRrr : F3_1<3, 0b100101, (outs), (ins (MEMrr $rs1, $rs2):$addr),
		 "stx %fsr, [$addr]", []>, Requires<[HasV9]>;
  def STXFSRri : F3_2<3, 0b100101, (outs), (ins (MEMri $rs1, $simm13):$addr),
		 "stx %fsr, [$addr]", []>, Requires<[HasV9]>;
}

// Section B.8 - SWAP Register with Memory Instruction
// (Atomic swap)
let Constraints = "$val = $rd" in {
  def SWAPrr : F3_1<3, 0b001111,
                 (outs IntRegs:$rd), (ins (MEMrr $rs1, $rs2):$addr, IntRegs:$val),
                 "swap [$addr], $rd",
                 [(set i32:$rd, (atomic_swap_i32 ADDRrr:$addr, i32:$val))]>;
  def SWAPri : F3_2<3, 0b001111,
                 (outs IntRegs:$rd), (ins (MEMri $rs1, $simm13):$addr, IntRegs:$val),
                 "swap [$addr], $rd",
                 [(set i32:$rd, (atomic_swap_i32 ADDRri:$addr, i32:$val))]>;
  def SWAPArr : F3_1_asi<3, 0b011111,
                 (outs IntRegs:$rd), (ins (MEMrr $rs1, $rs2):$addr, ASITag:$asi, IntRegs:$val),
                 "swapa [$addr] $asi, $rd",
                 [/*FIXME: pattern?*/]>;
let Predicates = [HasV9], Uses = [ASR3] in
  def SWAPAri : F3_2<3, 0b011111,
                 (outs IntRegs:$rd), (ins (MEMri $rs1, $simm13):$addr, IntRegs:$val),
                 "swapa [$addr] %asi, $rd",
                 [/*FIXME: pattern?*/]>;
}


// Section B.9 - SETHI Instruction, p. 104
def SETHIi: F2_1<0b100,
                 (outs IntRegs:$rd), (ins i32imm:$imm22),
                 "sethi $imm22, $rd",
                 [(set i32:$rd, SETHIimm:$imm22)],
                 IIC_iu_instr>;

// Section B.10 - NOP Instruction, p. 105
// (It's a special case of SETHI)
let rd = 0, imm22 = 0 in
  def NOP : F2_1<0b100, (outs), (ins), "nop", []>;

// Section B.11 - Logical Instructions, p. 106
defm AND    : F3_12<"and", 0b000001, and, IntRegs, i32, simm13Op>;

def ANDNrr  : F3_1<2, 0b000101,
                   (outs IntRegs:$rd), (ins IntRegs:$rs1, IntRegs:$rs2),
                   "andn $rs1, $rs2, $rd",
                   [(set i32:$rd, (and i32:$rs1, (not i32:$rs2)))]>;
def ANDNri  : F3_2<2, 0b000101,
                   (outs IntRegs:$rd), (ins IntRegs:$rs1, simm13Op:$simm13),
                   "andn $rs1, $simm13, $rd", []>;

defm OR     : F3_12<"or", 0b000010, or, IntRegs, i32, simm13Op>;

def ORNrr   : F3_1<2, 0b000110,
                   (outs IntRegs:$rd), (ins IntRegs:$rs1, IntRegs:$rs2),
                   "orn $rs1, $rs2, $rd",
                   [(set i32:$rd, (or i32:$rs1, (not i32:$rs2)))]>;
def ORNri   : F3_2<2, 0b000110,
                   (outs IntRegs:$rd), (ins IntRegs:$rs1, simm13Op:$simm13),
                   "orn $rs1, $simm13, $rd", []>;
defm XOR    : F3_12<"xor", 0b000011, xor, IntRegs, i32, simm13Op>;

def XNORrr  : F3_1<2, 0b000111,
                   (outs IntRegs:$rd), (ins IntRegs:$rs1, IntRegs:$rs2),
                   "xnor $rs1, $rs2, $rd",
                   [(set i32:$rd, (not (xor i32:$rs1, i32:$rs2)))]>;
def XNORri  : F3_2<2, 0b000111,
                   (outs IntRegs:$rd), (ins IntRegs:$rs1, simm13Op:$simm13),
                   "xnor $rs1, $simm13, $rd", []>;

def : Pat<(and IntRegs:$rs1, SETHIimm_not:$rs2),
          (ANDNrr i32:$rs1, (SETHIi SETHIimm_not:$rs2))>;

def : Pat<(or IntRegs:$rs1, SETHIimm_not:$rs2),
          (ORNrr i32:$rs1,  (SETHIi SETHIimm_not:$rs2))>;

let Defs = [ICC] in {
  defm ANDCC  : F3_12np<"andcc",  0b010001>;
  defm ANDNCC : F3_12np<"andncc", 0b010101>;
  defm ORCC   : F3_12np<"orcc",   0b010010>;
  defm ORNCC  : F3_12np<"orncc",  0b010110>;
  defm XORCC  : F3_12np<"xorcc",  0b010011>;
  defm XNORCC : F3_12np<"xnorcc", 0b010111>;
}

// Section B.12 - Shift Instructions, p. 107
defm SLL : F3_S<"sll", 0b100101, 0, shl, i32, shift_imm5, IntRegs>;
defm SRL : F3_S<"srl", 0b100110, 0, srl, i32, shift_imm5, IntRegs>;
defm SRA : F3_S<"sra", 0b100111, 0, sra, i32, shift_imm5, IntRegs>;

// Section B.13 - Add Instructions, p. 108
defm ADD   : F3_12<"add", 0b000000, add, IntRegs, i32, simm13Op>;

let Defs = [ICC] in
  defm ADDCC  : F3_12<"addcc", 0b010000, addc, IntRegs, i32, simm13Op>;

let Uses = [ICC] in
  defm ADDC   : F3_12np<"addx", 0b001000>;

let Uses = [ICC], Defs = [ICC] in
  defm ADDE  : F3_12<"addxcc", 0b011000, adde, IntRegs, i32, simm13Op>;

// Section B.15 - Subtract Instructions, p. 110
defm SUB    : F3_12  <"sub"  , 0b000100, sub, IntRegs, i32, simm13Op>;
let Uses = [ICC], Defs = [ICC] in
  defm SUBE   : F3_12  <"subxcc" , 0b011100, sube, IntRegs, i32, simm13Op>;

let Defs = [ICC], hasPostISelHook = true in
  defm SUBCC  : F3_12  <"subcc", 0b010100, subc, IntRegs, i32, simm13Op>;

let Uses = [ICC] in
  defm SUBC   : F3_12np <"subx", 0b001100>;

def : Pat<(SPcmpicc i32:$lhs, i32:$rhs), (SUBCCrr $lhs, $rhs)>;
def : Pat<(SPcmpicc i32:$lhs, (i32 simm13:$rhs)), (SUBCCri $lhs, imm:$rhs)>;

// Section B.18 - Multiply Instructions, p. 113
let Defs = [Y] in {
  defm UMUL : F3_12<"umul", 0b001010, umullohi, IntRegs, i32, simm13Op, IIC_iu_umul>;
  defm SMUL : F3_12<"smul", 0b001011, smullohi, IntRegs, i32, simm13Op, IIC_iu_smul>;
}

let Defs = [Y, ICC] in {
  defm UMULCC : F3_12np<"umulcc", 0b011010, IIC_iu_umul>;
  defm SMULCC : F3_12np<"smulcc", 0b011011, IIC_iu_smul>;
}

let Defs = [Y, ICC], Uses = [Y, ICC] in {
  defm MULSCC : F3_12np<"mulscc", 0b100100>;
}

// Section B.19 - Divide Instructions, p. 115
let Uses = [Y], Defs = [Y] in {
  defm UDIV : F3_12np<"udiv", 0b001110, IIC_iu_div>;
  defm SDIV : F3_12np<"sdiv", 0b001111, IIC_iu_div>;
}

let Uses = [Y], Defs = [Y, ICC] in {
  defm UDIVCC : F3_12np<"udivcc", 0b011110, IIC_iu_div>;
  defm SDIVCC : F3_12np<"sdivcc", 0b011111, IIC_iu_div>;
}

// Section B.20 - SAVE and RESTORE, p. 117
defm SAVE    : F3_12np<"save"   , 0b111100>;
defm RESTORE : F3_12np<"restore", 0b111101>;

// Section B.21 - Branch on Integer Condition Codes Instructions, p. 119
// Section A.7 - Branch on Integer Condition Codes with Prediction (SPARC v9)

let isBranch = 1, isTerminator = 1, hasDelaySlot = 1, isBarrier = 1 in {
// unconditional branch class.
class BranchAlways<dag ins, string asmstr, list<dag> pattern>
  : F2_2<0b010, 0, (outs), ins, asmstr, pattern>;

// Same as BranchAlways but uses the new v9 encoding
class BranchPredictAlways<dag ins, string asmstr, list<dag> pattern>
  : F2_3<0b001, 0, 1, (outs), ins, asmstr, pattern>;
}

let cond = 8 in
  def BA : BranchAlways<(ins brtarget:$imm22), "ba $imm22", [(br bb:$imm22)]>;

let isBranch = 1, isTerminator = 1, hasDelaySlot = 1 in {

// conditional branch class:
class BranchSP<dag ins, string asmstr, list<dag> pattern>
 : F2_2<0b010, 0, (outs), ins, asmstr, pattern, IIC_iu_instr>;

// conditional branch with annul class:
class BranchSPA<dag ins, string asmstr, list<dag> pattern>
 : F2_2<0b010, 1, (outs), ins, asmstr, pattern, IIC_iu_instr>;

// Conditional branch class on %icc|%xcc with predication:
multiclass IPredBranch<string regstr, list<dag> CCPattern> {
  def CC    : F2_3<0b001, 0, 1, (outs), (ins bprtarget:$imm19, CCOp:$cond),
                   !strconcat("b$cond ", !strconcat(regstr, ", $imm19")),
                   CCPattern,
                   IIC_iu_instr>;
  def CCA   : F2_3<0b001, 1, 1, (outs), (ins bprtarget:$imm19, CCOp:$cond),
                   !strconcat("b$cond,a ", !strconcat(regstr, ", $imm19")),
                   [],
                   IIC_iu_instr>;
  def CCNT  : F2_3<0b001, 0, 0, (outs), (ins bprtarget:$imm19, CCOp:$cond),
                   !strconcat("b$cond,pn ", !strconcat(regstr, ", $imm19")),
                   [],
                   IIC_iu_instr>;
  def CCANT : F2_3<0b001, 1, 0, (outs), (ins bprtarget:$imm19, CCOp:$cond),
                   !strconcat("b$cond,a,pn ", !strconcat(regstr, ", $imm19")),
                   [],
                   IIC_iu_instr>;
}

} // let isBranch = 1, isTerminator = 1, hasDelaySlot = 1


// Indirect branch instructions.
let isTerminator = 1, isBarrier = 1,  hasDelaySlot = 1, isBranch =1,
     isIndirectBranch = 1, rd = 0, isCodeGenOnly = 1 in {
  def BINDrr  : F3_1<2, 0b111000,
                   (outs), (ins (MEMrr $rs1, $rs2):$addr),
                   "jmp $addr",
                   [(brind ADDRrr:$addr)]>;
  def BINDri  : F3_2<2, 0b111000,
                   (outs), (ins (MEMri $rs1, $simm13):$addr),
                   "jmp $addr",
                   [(brind ADDRri:$addr)]>;
}

let Uses = [ICC] in {
  def BCOND : BranchSP<(ins brtarget:$imm22, CCOp:$cond),
                         "b$cond $imm22",
                        [(SPbricc bb:$imm22, imm:$cond)]>;
  def BCONDA : BranchSPA<(ins brtarget:$imm22, CCOp:$cond),
                         "b$cond,a $imm22", []>;

  let Predicates = [HasV9], cc = 0b00 in
    defm BPI : IPredBranch<"%icc", [(SPbpicc bb:$imm19, imm:$cond)]>;
}

// Section B.22 - Branch on Floating-point Condition Codes Instructions, p. 121

let isBranch = 1, isTerminator = 1, hasDelaySlot = 1 in {

// floating-point conditional branch class:
class FPBranchSP<dag ins, string asmstr, list<dag> pattern>
 : F2_2<0b110, 0, (outs), ins, asmstr, pattern, IIC_fpu_normal_instr>;

// floating-point conditional branch with annul class:
class FPBranchSPA<dag ins, string asmstr, list<dag> pattern>
 : F2_2<0b110, 1, (outs), ins, asmstr, pattern, IIC_fpu_normal_instr>;

// Conditional branch class on %fcc0-%fcc3 with predication:
multiclass FPredBranch {
  def CC    : F2_3<0b101, 0, 1, (outs), (ins bprtarget:$imm19, CCOp:$cond,
                                         FCCRegs:$cc),
                  "fb$cond $cc, $imm19", [], IIC_fpu_normal_instr>;
  def CCA   : F2_3<0b101, 1, 1, (outs), (ins bprtarget:$imm19, CCOp:$cond,
                                         FCCRegs:$cc),
                  "fb$cond,a $cc, $imm19", [], IIC_fpu_normal_instr>;
  def CCNT  : F2_3<0b101, 0, 0, (outs), (ins bprtarget:$imm19, CCOp:$cond,
                                         FCCRegs:$cc),
                  "fb$cond,pn $cc, $imm19", [], IIC_fpu_normal_instr>;
  def CCANT : F2_3<0b101, 1, 0, (outs), (ins bprtarget:$imm19, CCOp:$cond,
                                         FCCRegs:$cc),
                  "fb$cond,a,pn $cc, $imm19", [], IIC_fpu_normal_instr>;
}
} // let isBranch = 1, isTerminator = 1, hasDelaySlot = 1

let Uses = [FCC0] in {
  def FBCOND  : FPBranchSP<(ins brtarget:$imm22, CCOp:$cond),
                              "fb$cond $imm22",
                              [(SPbrfcc bb:$imm22, imm:$cond)]>;
  def FBCONDA : FPBranchSPA<(ins brtarget:$imm22, CCOp:$cond),
                             "fb$cond,a $imm22", []>;
}

// Variants of FBCOND that uses V9 opcode
let Predicates = [HasV9], Uses = [FCC0], cc = 0,
    isBranch = 1, isTerminator = 1, hasDelaySlot = 1 in {
  def FBCOND_V9  : F2_3<0b101, 0, 1, (outs),
                    (ins bprtarget:$imm19, CCOp:$cond),
                    "fb$cond %fcc0, $imm19",
                    [(SPbrfccv9 bb:$imm19, imm:$cond)], IIC_fpu_normal_instr>;
  def FBCONDA_V9 : F2_3<0b101, 1, 1, (outs),
                    (ins bprtarget:$imm19, CCOp:$cond),
                    "fb$cond,a %fcc0, $imm19",
                    [(SPbrfccv9 bb:$imm19, imm:$cond)], IIC_fpu_normal_instr>;
}

let Predicates = [HasV9] in
  defm BPF : FPredBranch;

// Section B.22 - Branch on Co-processor Condition Codes Instructions, p. 123
let isBranch = 1, isTerminator = 1, hasDelaySlot = 1 in {

// co-processor conditional branch class:
class CPBranchSP<dag ins, string asmstr, list<dag> pattern>
 : F2_2<0b111, 0, (outs), ins, asmstr, pattern>;

// co-processor conditional branch with annul class:
class CPBranchSPA<dag ins, string asmstr, list<dag> pattern>
 : F2_2<0b111, 1, (outs), ins, asmstr, pattern>;

} // let isBranch = 1, isTerminator = 1, hasDelaySlot = 1

def CBCOND  : CPBranchSP<(ins brtarget:$imm22, CCOp:$cond),
                          "cb$cond $imm22",
                          [(SPbrfcc bb:$imm22, imm:$cond)]>;
def CBCONDA : CPBranchSPA<(ins brtarget:$imm22, CCOp:$cond),
                           "cb$cond,a $imm22", []>;

// Section B.24 - Call and Link Instruction, p. 125
// This is the only Format 1 instruction
let Uses = [O6],
    hasDelaySlot = 1, isCall = 1 in {
  def CALL : InstSP<(outs), (ins calltarget:$disp, variable_ops),
                    "call $disp",
                    [],
                    IIC_jmp_or_call> {
    bits<30> disp;
    let op = 1;
    let Inst{29-0} = disp;
  }

  // indirect calls: special cases of JMPL.
  let isCodeGenOnly = 1, rd = 15 in {
    def CALLrr : F3_1<2, 0b111000,
                      (outs), (ins (MEMrr $rs1, $rs2):$addr, variable_ops),
                      "call $addr",
                      [(call ADDRrr:$addr)],
                      IIC_jmp_or_call>;
    def CALLri : F3_2<2, 0b111000,
                      (outs), (ins (MEMri $rs1, $simm13):$addr, variable_ops),
                      "call $addr",
                      [(call ADDRri:$addr)],
                      IIC_jmp_or_call>;
  }
}

// Section B.25 - Jump and Link Instruction

// JMPL Instruction.
let isTerminator = 1, hasDelaySlot = 1, isBarrier = 1 in {
  def JMPLrr: F3_1<2, 0b111000,
                   (outs IntRegs:$rd), (ins (MEMrr $rs1, $rs2):$addr),
                   "jmpl $addr, $rd",
                   [],
                   IIC_jmp_or_call>;
  def JMPLri: F3_2<2, 0b111000,
                   (outs IntRegs:$rd), (ins (MEMri $rs1, $simm13):$addr),
                   "jmpl $addr, $rd",
                   [],
                   IIC_jmp_or_call>;
}

// Section A.3 - Synthetic Instructions, p. 85
// special cases of JMPL:
let isReturn = 1, isTerminator = 1, hasDelaySlot = 1, isBarrier = 1,
    isCodeGenOnly = 1 in {
  let rd = 0, rs1 = 15 in
    def RETL: F3_2<2, 0b111000,
                   (outs), (ins i32imm:$simm13),
                   "jmp %o7+$simm13",
                   [(retglue simm13:$simm13)],
                   IIC_jmp_or_call>;

  let rd = 0, rs1 = 31 in
    def RET: F3_2<2, 0b111000,
                  (outs), (ins i32imm:$simm13),
                  "jmp %i7+$simm13",
                  [],
                  IIC_jmp_or_call>;
}

// Section B.26 - Return from Trap Instruction
let isReturn = 1, isTerminator = 1, hasDelaySlot = 1,
     isBarrier = 1, rd = 0 in {
  def RETTrr : F3_1<2, 0b111001,
                   (outs), (ins (MEMrr $rs1, $rs2):$addr),
                   "rett $addr",
                   [],
                   IIC_jmp_or_call>;
  def RETTri : F3_2<2, 0b111001,
                    (outs), (ins (MEMri $rs1, $simm13):$addr),
                    "rett $addr",
                    [],
                    IIC_jmp_or_call>;
}


// Section B.27 - Trap on Integer Condition Codes Instruction
// conditional branch class:
let DecoderNamespace = "SparcV8", hasSideEffects = 1, Uses = [ICC], cc = 0b00 in
{
  def TRAPrr : TRAPSPrr<0b111010,
                        (outs), (ins IntRegs:$rs1, IntRegs:$rs2, CCOp:$cond),
                        "t$cond $rs1 + $rs2",
                        []>;
  def TRAPri : TRAPSPri<0b111010,
                        (outs), (ins IntRegs:$rs1, i32imm:$imm, CCOp:$cond),
                        "t$cond $rs1 + $imm",
                        []>;
}

multiclass TRAP<string regStr> {
  def rr : TRAPSPrr<0b111010,
                    (outs), (ins IntRegs:$rs1, IntRegs:$rs2, CCOp:$cond),
                    !strconcat(!strconcat("t$cond ", regStr), ", $rs1 + $rs2"),
                    []>;
  def ri : TRAPSPri<0b111010,
                    (outs), (ins IntRegs:$rs1, i32imm:$imm, CCOp:$cond),
                    !strconcat(!strconcat("t$cond ", regStr), ", $rs1 + $imm"),
                    []>;
}

let DecoderNamespace = "SparcV9", Predicates = [HasV9], hasSideEffects = 1, Uses = [ICC], cc = 0b00 in
  defm TICC : TRAP<"%icc">;


let isBarrier = 1, isTerminator = 1, rd = 0b01000, rs1 = 0, simm13 = 5 in
  def TA5 : F3_2<0b10, 0b111010, (outs), (ins), "ta 5", [(trap)]>;

let hasSideEffects = 1, rd = 0b01000, rs1 = 0, simm13 = 1 in
  def TA1 : F3_2<0b10, 0b111010, (outs), (ins), "ta 1", [(debugtrap)]>;

// Section B.28 - Read State Register Instructions
let rs2 = 0 in
  def RDASR : F3_1<2, 0b101000,
                 (outs IntRegs:$rd), (ins ASRRegs:$rs1),
                 "rd $rs1, $rd", []>;

// PSR, WIM, and TBR don't exist on the SparcV9, only the V8.
let Predicates = [HasNoV9] in {
  let rs2 = 0, rs1 = 0, Uses=[PSR] in
    def RDPSR : F3_1<2, 0b101001,
		     (outs IntRegs:$rd), (ins),
		     "rd %psr, $rd", []>;

  let rs2 = 0, rs1 = 0, Uses=[WIM] in
    def RDWIM : F3_1<2, 0b101010,
		     (outs IntRegs:$rd), (ins),
		     "rd %wim, $rd", []>;

  let rs2 = 0, rs1 = 0, Uses=[TBR] in
    def RDTBR : F3_1<2, 0b101011,
		     (outs IntRegs:$rd), (ins),
		     "rd %tbr, $rd", []>;
}

// Section B.29 - Write State Register Instructions
def WRASRrr : F3_1<2, 0b110000,
                 (outs ASRRegs:$rd), (ins IntRegs:$rs1, IntRegs:$rs2),
                 "wr $rs1, $rs2, $rd", []>;
def WRASRri : F3_2<2, 0b110000,
                 (outs ASRRegs:$rd), (ins IntRegs:$rs1, simm13Op:$simm13),
                 "wr $rs1, $simm13, $rd", []>;

// PSR, WIM, and TBR don't exist on the SparcV9, only the V8.
let Predicates = [HasNoV9] in {
  let Defs = [PSR], rd=0 in {
    def WRPSRrr : F3_1<2, 0b110001,
		     (outs), (ins IntRegs:$rs1, IntRegs:$rs2),
		     "wr $rs1, $rs2, %psr", []>;
    def WRPSRri : F3_2<2, 0b110001,
		     (outs), (ins IntRegs:$rs1, simm13Op:$simm13),
		     "wr $rs1, $simm13, %psr", []>;
  }

  let Defs = [WIM], rd=0 in {
    def WRWIMrr : F3_1<2, 0b110010,
		     (outs), (ins IntRegs:$rs1, IntRegs:$rs2),
		     "wr $rs1, $rs2, %wim", []>;
    def WRWIMri : F3_2<2, 0b110010,
		     (outs), (ins IntRegs:$rs1, simm13Op:$simm13),
		     "wr $rs1, $simm13, %wim", []>;
  }

  let Defs = [TBR], rd=0 in {
    def WRTBRrr : F3_1<2, 0b110011,
		     (outs), (ins IntRegs:$rs1, IntRegs:$rs2),
		     "wr $rs1, $rs2, %tbr", []>;
    def WRTBRri : F3_2<2, 0b110011,
		     (outs), (ins IntRegs:$rs1, simm13Op:$simm13),
		     "wr $rs1, $simm13, %tbr", []>;
  }
}

// Section B.30 - STBAR Instruction
let hasSideEffects = 1, rd = 0, rs1 = 0b01111, rs2 = 0 in
  def STBAR : F3_1<2, 0b101000, (outs), (ins), "stbar", []>;


// Section B.31 - Unimplemented Instruction
let rd = 0 in
  def UNIMP : F2_1<0b000, (outs), (ins i32imm:$imm22),
                  "unimp $imm22", []>;

// Section B.32 - Flush Instruction Memory
let rd = 0 in {
  def FLUSHrr : F3_1<2, 0b111011, (outs), (ins (MEMrr $rs1, $rs2):$addr),
                       "flush $addr", []>;
  def FLUSHri : F3_2<2, 0b111011, (outs), (ins (MEMri $rs1, $simm13):$addr),
                       "flush $addr", []>;

  // The no-arg FLUSH is only here for the benefit of the InstAlias
  // "flush", which cannot seem to use FLUSHrr, due to the inability
  // to construct a MEMrr with fixed G0 registers.
  let rs1 = 0, rs2 = 0 in
    def FLUSH   : F3_1<2, 0b111011, (outs), (ins), "flush %g0", []>;
}

// Section B.33 - Floating-point Operate (FPop) Instructions

// Convert Integer to Floating-point Instructions, p. 141
def FITOS : F3_3u<2, 0b110100, 0b011000100,
                 (outs FPRegs:$rd), (ins FPRegs:$rs2),
                 "fitos $rs2, $rd",
                 [(set FPRegs:$rd, (SPitof FPRegs:$rs2))],
                 IIC_fpu_fast_instr>;
def FITOD : F3_3u<2, 0b110100, 0b011001000,
                 (outs DFPRegs:$rd), (ins FPRegs:$rs2),
                 "fitod $rs2, $rd",
                 [(set DFPRegs:$rd, (SPitof FPRegs:$rs2))],
                 IIC_fpu_fast_instr>;
def FITOQ : F3_3u<2, 0b110100, 0b011001100,
                 (outs QFPRegs:$rd), (ins FPRegs:$rs2),
                 "fitoq $rs2, $rd",
                 [(set QFPRegs:$rd, (SPitof FPRegs:$rs2))]>,
                 Requires<[HasHardQuad]>;

// Convert Floating-point to Integer Instructions, p. 142
def FSTOI : F3_3u<2, 0b110100, 0b011010001,
                 (outs FPRegs:$rd), (ins FPRegs:$rs2),
                 "fstoi $rs2, $rd",
                 [(set FPRegs:$rd, (SPftoi FPRegs:$rs2))],
                 IIC_fpu_fast_instr>;
def FDTOI : F3_3u<2, 0b110100, 0b011010010,
                 (outs FPRegs:$rd), (ins DFPRegs:$rs2),
                 "fdtoi $rs2, $rd",
                 [(set FPRegs:$rd, (SPftoi DFPRegs:$rs2))],
                 IIC_fpu_fast_instr>;
def FQTOI : F3_3u<2, 0b110100, 0b011010011,
                 (outs FPRegs:$rd), (ins QFPRegs:$rs2),
                 "fqtoi $rs2, $rd",
                 [(set FPRegs:$rd, (SPftoi QFPRegs:$rs2))]>,
                 Requires<[HasHardQuad]>;

// Convert between Floating-point Formats Instructions, p. 143
def FSTOD : F3_3u<2, 0b110100, 0b011001001,
                 (outs DFPRegs:$rd), (ins FPRegs:$rs2),
                 "fstod $rs2, $rd",
                 [(set f64:$rd, (fpextend f32:$rs2))],
                 IIC_fpu_stod>;
def FSTOQ : F3_3u<2, 0b110100, 0b011001101,
                 (outs QFPRegs:$rd), (ins FPRegs:$rs2),
                 "fstoq $rs2, $rd",
                 [(set f128:$rd, (fpextend f32:$rs2))]>,
                 Requires<[HasHardQuad]>;
def FDTOS : F3_3u<2, 0b110100, 0b011000110,
                 (outs FPRegs:$rd), (ins DFPRegs:$rs2),
                 "fdtos $rs2, $rd",
                 [(set f32:$rd, (fpround f64:$rs2))],
                 IIC_fpu_fast_instr>;
def FDTOQ : F3_3u<2, 0b110100, 0b011001110,
                 (outs QFPRegs:$rd), (ins DFPRegs:$rs2),
                 "fdtoq $rs2, $rd",
                 [(set f128:$rd, (fpextend f64:$rs2))]>,
                 Requires<[HasHardQuad]>;
def FQTOS : F3_3u<2, 0b110100, 0b011000111,
                 (outs FPRegs:$rd), (ins QFPRegs:$rs2),
                 "fqtos $rs2, $rd",
                 [(set f32:$rd, (fpround f128:$rs2))]>,
                 Requires<[HasHardQuad]>;
def FQTOD : F3_3u<2, 0b110100, 0b011001011,
                 (outs DFPRegs:$rd), (ins QFPRegs:$rs2),
                 "fqtod $rs2, $rd",
                 [(set f64:$rd, (fpround f128:$rs2))]>,
                 Requires<[HasHardQuad]>;

// Floating-point Move Instructions, p. 144
def FMOVS : F3_3u<2, 0b110100, 0b000000001,
                 (outs FPRegs:$rd), (ins FPRegs:$rs2),
                 "fmovs $rs2, $rd", []>;
def FNEGS : F3_3u<2, 0b110100, 0b000000101,
                 (outs FPRegs:$rd), (ins FPRegs:$rs2),
                 "fnegs $rs2, $rd",
                 [(set f32:$rd, (fneg f32:$rs2))],
                 IIC_fpu_negs>;
def FABSS : F3_3u<2, 0b110100, 0b000001001,
                 (outs FPRegs:$rd), (ins FPRegs:$rs2),
                 "fabss $rs2, $rd",
                 [(set f32:$rd, (fabs f32:$rs2))],
                 IIC_fpu_abs>;


// Floating-point Square Root Instructions, p.145
// FSQRTS generates an erratum on LEON processors, so by disabling this instruction
// this will be promoted to use FSQRTD with doubles instead.
let Predicates = [HasNoFdivSqrtFix] in
def FSQRTS : F3_3u<2, 0b110100, 0b000101001,
                  (outs FPRegs:$rd), (ins FPRegs:$rs2),
                  "fsqrts $rs2, $rd",
                  [(set f32:$rd, (fsqrt f32:$rs2))],
                  IIC_fpu_sqrts>;
def FSQRTD : F3_3u<2, 0b110100, 0b000101010,
                  (outs DFPRegs:$rd), (ins DFPRegs:$rs2),
                  "fsqrtd $rs2, $rd",
                  [(set f64:$rd, (fsqrt f64:$rs2))],
                  IIC_fpu_sqrtd>;
def FSQRTQ : F3_3u<2, 0b110100, 0b000101011,
                  (outs QFPRegs:$rd), (ins QFPRegs:$rs2),
                  "fsqrtq $rs2, $rd",
                  [(set f128:$rd, (fsqrt f128:$rs2))]>,
                  Requires<[HasHardQuad]>;



// Floating-point Add and Subtract Instructions, p. 146
def FADDS  : F3_3<2, 0b110100, 0b001000001,
                  (outs FPRegs:$rd), (ins FPRegs:$rs1, FPRegs:$rs2),
                  "fadds $rs1, $rs2, $rd",
                  [(set f32:$rd, (fadd f32:$rs1, f32:$rs2))],
                  IIC_fpu_fast_instr>;
def FADDD  : F3_3<2, 0b110100, 0b001000010,
                  (outs DFPRegs:$rd), (ins DFPRegs:$rs1, DFPRegs:$rs2),
                  "faddd $rs1, $rs2, $rd",
                  [(set f64:$rd, (fadd f64:$rs1, f64:$rs2))],
                  IIC_fpu_fast_instr>;
def FADDQ  : F3_3<2, 0b110100, 0b001000011,
                  (outs QFPRegs:$rd), (ins QFPRegs:$rs1, QFPRegs:$rs2),
                  "faddq $rs1, $rs2, $rd",
                  [(set f128:$rd, (fadd f128:$rs1, f128:$rs2))]>,
                  Requires<[HasHardQuad]>;

def FSUBS  : F3_3<2, 0b110100, 0b001000101,
                  (outs FPRegs:$rd), (ins FPRegs:$rs1, FPRegs:$rs2),
                  "fsubs $rs1, $rs2, $rd",
                  [(set f32:$rd, (fsub f32:$rs1, f32:$rs2))],
                  IIC_fpu_fast_instr>;
def FSUBD  : F3_3<2, 0b110100, 0b001000110,
                  (outs DFPRegs:$rd), (ins DFPRegs:$rs1, DFPRegs:$rs2),
                  "fsubd $rs1, $rs2, $rd",
                  [(set f64:$rd, (fsub f64:$rs1, f64:$rs2))],
                  IIC_fpu_fast_instr>;
def FSUBQ  : F3_3<2, 0b110100, 0b001000111,
                  (outs QFPRegs:$rd), (ins QFPRegs:$rs1, QFPRegs:$rs2),
                  "fsubq $rs1, $rs2, $rd",
                  [(set f128:$rd, (fsub f128:$rs1, f128:$rs2))]>,
                  Requires<[HasHardQuad]>;


// Floating-point Multiply and Divide Instructions, p. 147
def FMULS  : F3_3<2, 0b110100, 0b001001001,
                  (outs FPRegs:$rd), (ins FPRegs:$rs1, FPRegs:$rs2),
                  "fmuls $rs1, $rs2, $rd",
                  [(set f32:$rd, (fmul f32:$rs1, f32:$rs2))],
                  IIC_fpu_muls>,
		  Requires<[HasFMULS]>;
def FMULD  : F3_3<2, 0b110100, 0b001001010,
                  (outs DFPRegs:$rd), (ins DFPRegs:$rs1, DFPRegs:$rs2),
                  "fmuld $rs1, $rs2, $rd",
                  [(set f64:$rd, (fmul f64:$rs1, f64:$rs2))],
                  IIC_fpu_muld>;
def FMULQ  : F3_3<2, 0b110100, 0b001001011,
                  (outs QFPRegs:$rd), (ins QFPRegs:$rs1, QFPRegs:$rs2),
                  "fmulq $rs1, $rs2, $rd",
                  [(set f128:$rd, (fmul f128:$rs1, f128:$rs2))]>,
                  Requires<[HasHardQuad]>;

def FSMULD : F3_3<2, 0b110100, 0b001101001,
                  (outs DFPRegs:$rd), (ins FPRegs:$rs1, FPRegs:$rs2),
                  "fsmuld $rs1, $rs2, $rd",
                  [(set f64:$rd, (fmul (fpextend f32:$rs1),
                                        (fpextend f32:$rs2)))],
                  IIC_fpu_muld>,
		  Requires<[HasFSMULD]>;
def FDMULQ : F3_3<2, 0b110100, 0b001101110,
                  (outs QFPRegs:$rd), (ins DFPRegs:$rs1, DFPRegs:$rs2),
                  "fdmulq $rs1, $rs2, $rd",
                  [(set f128:$rd, (fmul (fpextend f64:$rs1),
                                         (fpextend f64:$rs2)))]>,
                  Requires<[HasHardQuad]>;

// FDIVS generates an erratum on LEON processors, so by disabling this instruction
// this will be promoted to use FDIVD with doubles instead.
def FDIVS  : F3_3<2, 0b110100, 0b001001101,
                 (outs FPRegs:$rd), (ins FPRegs:$rs1, FPRegs:$rs2),
                 "fdivs $rs1, $rs2, $rd",
                 [(set f32:$rd, (fdiv f32:$rs1, f32:$rs2))],
                 IIC_fpu_divs>;
def FDIVD  : F3_3<2, 0b110100, 0b001001110,
                 (outs DFPRegs:$rd), (ins DFPRegs:$rs1, DFPRegs:$rs2),
                 "fdivd $rs1, $rs2, $rd",
                 [(set f64:$rd, (fdiv f64:$rs1, f64:$rs2))],
                 IIC_fpu_divd>;
def FDIVQ  : F3_3<2, 0b110100, 0b001001111,
                 (outs QFPRegs:$rd), (ins QFPRegs:$rs1, QFPRegs:$rs2),
                 "fdivq $rs1, $rs2, $rd",
                 [(set f128:$rd, (fdiv f128:$rs1, f128:$rs2))]>,
                 Requires<[HasHardQuad]>;

// Floating-point Compare Instructions, p. 148
// Note: the 2nd template arg is different for these guys.
// Note 2: the result of a FCMP is not available until the 2nd cycle
// after the instr is retired, but there is no interlock in Sparc V8.
// This behavior is modeled with a forced noop after the instruction in
// DelaySlotFiller.

let Defs = [FCC0], rd = 0, isCodeGenOnly = 1 in {
  def FCMPS  : F3_3c<2, 0b110101, 0b001010001,
                   (outs), (ins FPRegs:$rs1, FPRegs:$rs2),
                   "fcmps $rs1, $rs2",
                   [(SPcmpfcc f32:$rs1, f32:$rs2)],
                   IIC_fpu_fast_instr>;
  def FCMPD  : F3_3c<2, 0b110101, 0b001010010,
                   (outs), (ins DFPRegs:$rs1, DFPRegs:$rs2),
                   "fcmpd $rs1, $rs2",
                   [(SPcmpfcc f64:$rs1, f64:$rs2)],
                   IIC_fpu_fast_instr>;
  def FCMPQ  : F3_3c<2, 0b110101, 0b001010011,
                   (outs), (ins QFPRegs:$rs1, QFPRegs:$rs2),
                   "fcmpq $rs1, $rs2",
                   [(SPcmpfcc f128:$rs1, f128:$rs2)]>,
                   Requires<[HasHardQuad]>;
}

// A.13 Floating-Point Compare (SPARC v9)
// Note that these always write to %fcc0 instead of having its destination
// allocated automatically.
// This avoids complications with the scheduler sometimes wanting to spill
// the contents of an FCC, since SPARC v9 doesn't have facilities to spill
// an individual FCC.

let Predicates = [HasV9], Defs = [FCC0], rd = 0, isCodeGenOnly = 1 in {
  def FCMPS_V9  : F3_3c<2, 0b110101, 0b001010001,
                   (outs), (ins FPRegs:$rs1, FPRegs:$rs2),
                   "fcmps %fcc0, $rs1, $rs2",
                   [(SPcmpfccv9 f32:$rs1, f32:$rs2)],
                   IIC_fpu_fast_instr>;
  def FCMPD_V9  : F3_3c<2, 0b110101, 0b001010010,
                   (outs), (ins DFPRegs:$rs1, DFPRegs:$rs2),
                   "fcmpd %fcc0, $rs1, $rs2",
                   [(SPcmpfccv9 f64:$rs1, f64:$rs2)],
                   IIC_fpu_fast_instr>;
  def FCMPQ_V9  : F3_3c<2, 0b110101, 0b001010011,
                   (outs), (ins QFPRegs:$rs1, QFPRegs:$rs2),
                   "fcmpq %fcc0, $rs1, $rs2",
                   [(SPcmpfccv9 f128:$rs1, f128:$rs2)]>,
                   Requires<[HasHardQuad]>;
}

//===----------------------------------------------------------------------===//
// Instructions for Thread Local Storage(TLS).
//===----------------------------------------------------------------------===//
let isAsmParserOnly = 1 in {
def TLS_ADDrr : F3_1<2, 0b000000,
                    (outs IntRegs:$rd),
                    (ins IntRegs:$rs1, IntRegs:$rs2, TailRelocSymTLSAdd:$sym),
                    "add $rs1, $rs2, $rd, $sym",
                    [(set i32:$rd,
                        (tlsadd i32:$rs1, i32:$rs2, tglobaltlsaddr:$sym))]>;

let mayLoad = 1 in {
  def TLS_LDrr : F3_1<3, 0b000000,
                      (outs IntRegs:$rd),
                      (ins (MEMrr $rs1, $rs2):$addr, TailRelocSymTLSLoad:$sym),
                      "ld [$addr], $rd, $sym",
                      [(set i32:$rd,
                          (tlsld ADDRrr:$addr, tglobaltlsaddr:$sym))]>;
}

let Uses = [O6], isCall = 1, hasDelaySlot = 1 in
  def TLS_CALL : InstSP<(outs),
                        (ins calltarget:$disp, TailRelocSymTLSCall:$sym,
                         variable_ops),
                        "call $disp, $sym",
                        [(tlscall texternalsym:$disp, tglobaltlsaddr:$sym)],
                        IIC_jmp_or_call> {
  bits<30> disp;
  let op = 1;
  let Inst{29-0} = disp;
}
}

//===----------------------------------------------------------------------===//
// Instructions for tail calls.
//===----------------------------------------------------------------------===//
let isCodeGenOnly = 1, isReturn = 1,  hasDelaySlot = 1,
    isTerminator = 1, isBarrier = 1 in {
  def TAIL_CALL : InstSP<(outs), (ins calltarget:$disp, variable_ops),
                         "call $disp",
                         [(tailcall tglobaladdr:$disp)]> {
  bits<30> disp;
  let op = 1;
  let Inst{29-0} = disp;
  }
}

def : Pat<(tailcall (iPTR texternalsym:$dst)),
          (TAIL_CALL texternalsym:$dst)>;

let isCodeGenOnly = 1, isReturn = 1,  hasDelaySlot = 1,  isTerminator = 1,
    isBarrier = 1, rd = 0 in {
  def TAIL_CALLri : F3_2<2, 0b111000,
                         (outs), (ins (MEMri $rs1, $simm13):$addr, variable_ops),
                         "jmp $addr",
                         [(tailcall ADDRri:$addr)]>;
}

//===----------------------------------------------------------------------===//
// V9 Instructions
//===----------------------------------------------------------------------===//

// V9 Conditional Moves.
let Predicates = [HasV9], Constraints = "$f = $rd" in {
  // Move Integer Register on Condition (MOVcc) p. 194 of the V9 manual.
  let Uses = [ICC], intcc = 1, cc = 0b00 in {
    def MOVICCrr
      : F4_1<0b101100, (outs IntRegs:$rd),
             (ins IntRegs:$rs2, IntRegs:$f, CCOp:$cond),
             "mov$cond %icc, $rs2, $rd",
             [(set i32:$rd, (SPselecticc i32:$rs2, i32:$f, imm:$cond))]>;

    def MOVICCri
      : F4_2<0b101100, (outs IntRegs:$rd),
             (ins i32imm:$simm11, IntRegs:$f, CCOp:$cond),
             "mov$cond %icc, $simm11, $rd",
             [(set i32:$rd,
                    (SPselecticc simm11:$simm11, i32:$f, imm:$cond))]>;
  }

  let Uses = [FCC0], intcc = 0, cc = 0b00 in {
    def MOVFCCrr
      : F4_1<0b101100, (outs IntRegs:$rd),
             (ins IntRegs:$rs2, IntRegs:$f, CCOp:$cond),
             "mov$cond %fcc0, $rs2, $rd",
             [(set i32:$rd, (SPselectfcc i32:$rs2, i32:$f, imm:$cond))]>;
    def MOVFCCri
      : F4_2<0b101100, (outs IntRegs:$rd),
             (ins i32imm:$simm11, IntRegs:$f, CCOp:$cond),
             "mov$cond %fcc0, $simm11, $rd",
             [(set i32:$rd,
                    (SPselectfcc simm11:$simm11, i32:$f, imm:$cond))]>;
  }

  let Uses = [ICC], intcc = 1, opf_cc = 0b00 in {
    def FMOVS_ICC
      : F4_3<0b110101, 0b000001, (outs FPRegs:$rd),
             (ins FPRegs:$rs2, FPRegs:$f, CCOp:$cond),
             "fmovs$cond %icc, $rs2, $rd",
             [(set f32:$rd, (SPselecticc f32:$rs2, f32:$f, imm:$cond))]>;
    def FMOVD_ICC
      : F4_3<0b110101, 0b000010, (outs DFPRegs:$rd),
               (ins DFPRegs:$rs2, DFPRegs:$f, CCOp:$cond),
               "fmovd$cond %icc, $rs2, $rd",
               [(set f64:$rd, (SPselecticc f64:$rs2, f64:$f, imm:$cond))]>;
    let Predicates = [HasV9, HasHardQuad] in
    def FMOVQ_ICC
      : F4_3<0b110101, 0b000011, (outs QFPRegs:$rd),
               (ins QFPRegs:$rs2, QFPRegs:$f, CCOp:$cond),
               "fmovq$cond %icc, $rs2, $rd",
               [(set f128:$rd, (SPselecticc f128:$rs2, f128:$f, imm:$cond))]>;
  }

  let Uses = [FCC0], intcc = 0, opf_cc = 0b00 in {
    def FMOVS_FCC
      : F4_3<0b110101, 0b000001, (outs FPRegs:$rd),
             (ins FPRegs:$rs2, FPRegs:$f, CCOp:$cond),
             "fmovs$cond %fcc0, $rs2, $rd",
             [(set f32:$rd, (SPselectfcc f32:$rs2, f32:$f, imm:$cond))]>;
    def FMOVD_FCC
      : F4_3<0b110101, 0b000010, (outs DFPRegs:$rd),
             (ins DFPRegs:$rs2, DFPRegs:$f, CCOp:$cond),
             "fmovd$cond %fcc0, $rs2, $rd",
             [(set f64:$rd, (SPselectfcc f64:$rs2, f64:$f, imm:$cond))]>;
    let Predicates = [HasV9, HasHardQuad] in
    def FMOVQ_FCC
      : F4_3<0b110101, 0b000011, (outs QFPRegs:$rd),
             (ins QFPRegs:$rs2, QFPRegs:$f, CCOp:$cond),
             "fmovq$cond %fcc0, $rs2, $rd",
             [(set f128:$rd, (SPselectfcc f128:$rs2, f128:$f, imm:$cond))]>;
  }

}

// Floating-Point Move Instructions, p. 164 of the V9 manual.
let Predicates = [HasV9] in {
  def FMOVD : F3_3u<2, 0b110100, 0b000000010,
                   (outs DFPRegs:$rd), (ins DFPRegs:$rs2),
                   "fmovd $rs2, $rd", []>;
  let Predicates = [HasV9, HasHardQuad] in
  def FMOVQ : F3_3u<2, 0b110100, 0b000000011,
                   (outs QFPRegs:$rd), (ins QFPRegs:$rs2),
                   "fmovq $rs2, $rd", []>;
  def FNEGD : F3_3u<2, 0b110100, 0b000000110,
                   (outs DFPRegs:$rd), (ins DFPRegs:$rs2),
                   "fnegd $rs2, $rd",
                   [(set f64:$rd, (fneg f64:$rs2))]>;
  let Predicates = [HasV9, HasHardQuad] in
  def FNEGQ : F3_3u<2, 0b110100, 0b000000111,
                   (outs QFPRegs:$rd), (ins QFPRegs:$rs2),
                   "fnegq $rs2, $rd",
                   [(set f128:$rd, (fneg f128:$rs2))]>;
  def FABSD : F3_3u<2, 0b110100, 0b000001010,
                   (outs DFPRegs:$rd), (ins DFPRegs:$rs2),
                   "fabsd $rs2, $rd",
                   [(set f64:$rd, (fabs f64:$rs2))]>;
  let Predicates = [HasV9, HasHardQuad] in
  def FABSQ : F3_3u<2, 0b110100, 0b000001011,
                   (outs QFPRegs:$rd), (ins QFPRegs:$rs2),
                   "fabsq $rs2, $rd",
                   [(set f128:$rd, (fabs f128:$rs2))]>;
}

// Floating-point compare instruction with %fcc0-%fcc3.
def V9FCMPS  : F3_3c<2, 0b110101, 0b001010001,
               (outs FCCRegs:$rd), (ins FPRegs:$rs1, FPRegs:$rs2),
               "fcmps $rd, $rs1, $rs2", []>;
def V9FCMPD  : F3_3c<2, 0b110101, 0b001010010,
                (outs FCCRegs:$rd), (ins DFPRegs:$rs1, DFPRegs:$rs2),
                "fcmpd $rd, $rs1, $rs2", []>;
def V9FCMPQ  : F3_3c<2, 0b110101, 0b001010011,
                (outs FCCRegs:$rd), (ins QFPRegs:$rs1, QFPRegs:$rs2),
                "fcmpq $rd, $rs1, $rs2", []>,
                 Requires<[HasHardQuad]>;

let hasSideEffects = 1 in {
  def V9FCMPES  : F3_3c<2, 0b110101, 0b001010101,
                   (outs FCCRegs:$rd), (ins FPRegs:$rs1, FPRegs:$rs2),
                   "fcmpes $rd, $rs1, $rs2", []>;
  def V9FCMPED  : F3_3c<2, 0b110101, 0b001010110,
                   (outs FCCRegs:$rd), (ins DFPRegs:$rs1, DFPRegs:$rs2),
                   "fcmped $rd, $rs1, $rs2", []>;
  def V9FCMPEQ  : F3_3c<2, 0b110101, 0b001010111,
                   (outs FCCRegs:$rd), (ins QFPRegs:$rs1, QFPRegs:$rs2),
                   "fcmpeq $rd, $rs1, $rs2", []>,
                   Requires<[HasHardQuad]>;
}

// Floating point conditional move instrucitons with %fcc0-%fcc3.
let Predicates = [HasV9] in {
  let Constraints = "$f = $rd", intcc = 0 in {
    def V9MOVFCCrr
      : F4_1<0b101100, (outs IntRegs:$rd),
             (ins FCCRegs:$cc, IntRegs:$rs2, IntRegs:$f, CCOp:$cond),
             "mov$cond $cc, $rs2, $rd", []>;
    def V9MOVFCCri
      : F4_2<0b101100, (outs IntRegs:$rd),
             (ins FCCRegs:$cc, i32imm:$simm11, IntRegs:$f, CCOp:$cond),
             "mov$cond $cc, $simm11, $rd", []>;
    def V9FMOVS_FCC
      : F4_3<0b110101, 0b000001, (outs FPRegs:$rd),
             (ins FCCRegs:$opf_cc, FPRegs:$rs2, FPRegs:$f, CCOp:$cond),
             "fmovs$cond $opf_cc, $rs2, $rd", []>;
    def V9FMOVD_FCC
      : F4_3<0b110101, 0b000010, (outs DFPRegs:$rd),
             (ins FCCRegs:$opf_cc, DFPRegs:$rs2, DFPRegs:$f, CCOp:$cond),
             "fmovd$cond $opf_cc, $rs2, $rd", []>;
    let Predicates = [HasV9, HasHardQuad] in
    def V9FMOVQ_FCC
      : F4_3<0b110101, 0b000011, (outs QFPRegs:$rd),
             (ins FCCRegs:$opf_cc, QFPRegs:$rs2, QFPRegs:$f, CCOp:$cond),
             "fmovq$cond $opf_cc, $rs2, $rd", []>;
  } // Constraints = "$f = $rd", ...
} // let Predicates = [hasV9]


// POPCrr - This does a ctpop of a 64-bit register.  As such, we have to clear
// the top 32-bits before using it.  To do this clearing, we use a SRLri X,0.
let rs1 = 0 in
  def POPCrr : F3_1<2, 0b101110,
                    (outs IntRegs:$rd), (ins IntRegs:$rs2),
                    "popc $rs2, $rd", []>, Requires<[HasV9]>;
def : Pat<(i32 (ctpop i32:$src)),
          (POPCrr (SRLri $src, 0))>;

let Predicates = [HasV9], hasSideEffects = 1, rd = 0, rs1 = 0b01111 in
 def MEMBARi : F3_2<2, 0b101000, (outs), (ins MembarTag:$simm13),
                    "membar $simm13", []>;

let Predicates = [HasV9], rd = 15, rs1 = 0b00000 in
  def SIR: F3_2<2, 0b110000, (outs),
                (ins simm13Op:$simm13),
                 "sir $simm13", []>;

// CASA supported on all V9, some LEON3 and all LEON4 processors.
let Predicates = [HasCASA], Constraints = "$swap = $rd" in
  def CASArr: F3_1_asi<3, 0b111100,
                (outs IntRegs:$rd), (ins IntRegs:$rs1, IntRegs:$rs2,
                                     IntRegs:$swap, ASITag:$asi),
                 "casa [$rs1] $asi, $rs2, $rd", []>;

// On the other hand, CASA that takes its ASI from a register
// is only supported on V9 processors.
let Predicates = [HasV9], Uses = [ASR3], Constraints = "$swap = $rd" in
  def CASAri: F3_1_cas_asi<3, 0b111100,
                (outs IntRegs:$rd), (ins IntRegs:$rs1, IntRegs:$rs2,
                                     IntRegs:$swap),
                 "casa [$rs1] %asi, $rs2, $rd", []>;

// TODO: Add DAG sequence to lower these instructions. Currently, only provided
// as inline assembler-supported instructions.
let Predicates = [HasUMAC_SMAC], Defs = [Y, ASR18], Uses = [Y, ASR18] in {
  def SMACrr :  F3_1<2, 0b111111,
                   (outs IntRegs:$rd), (ins IntRegs:$rs1, IntRegs:$rs2, ASRRegs:$asr18),
                   "smac $rs1, $rs2, $rd",
                   [], IIC_smac_umac>;

  def SMACri :  F3_2<2, 0b111111,
                  (outs IntRegs:$rd), (ins IntRegs:$rs1, simm13Op:$simm13, ASRRegs:$asr18),
                   "smac $rs1, $simm13, $rd",
                   [], IIC_smac_umac>;

  def UMACrr :  F3_1<2, 0b111110,
                  (outs IntRegs:$rd), (ins IntRegs:$rs1, IntRegs:$rs2, ASRRegs:$asr18),
                   "umac $rs1, $rs2, $rd",
                   [], IIC_smac_umac>;

  def UMACri :  F3_2<2, 0b111110,
                  (outs IntRegs:$rd), (ins IntRegs:$rs1, simm13Op:$simm13, ASRRegs:$asr18),
                   "umac $rs1, $simm13, $rd",
                   [], IIC_smac_umac>;
}

// The partial write WRPSR instruction has a non-zero destination
// register value to separate it from the standard instruction.
let Predicates = [HasPWRPSR], Defs = [PSR], rd=1 in {
  def PWRPSRrr : F3_1<2, 0b110001,
     (outs), (ins IntRegs:$rs1, IntRegs:$rs2),
     "pwr $rs1, $rs2, %psr", []>;
  def PWRPSRri : F3_2<2, 0b110001,
     (outs), (ins IntRegs:$rs1, simm13Op:$simm13),
     "pwr $rs1, $simm13, %psr", []>;
}

let Defs = [ICC] in {
defm TADDCC   : F3_12np<"taddcc",   0b100000>;
defm TSUBCC   : F3_12np<"tsubcc",   0b100001>;

let hasSideEffects = 1 in {
  defm TADDCCTV : F3_12np<"taddcctv", 0b100010>;
  defm TSUBCCTV : F3_12np<"tsubcctv", 0b100011>;
}
}

// Section A.11 - DONE and RETRY
// Section A.47 - SAVED and RESTORED
let Predicates = [HasV9], rs1 = 0, rs2 = 0 in {
  let rd = 0 in
    def DONE : F3_1<2, 0b111110, (outs), (ins), "done", []>;

  let rd = 1 in
    def RETRY : F3_1<2, 0b111110, (outs), (ins), "retry", []>;

  let rd = 0 in
    def SAVED : F3_1<2, 0b110001, (outs), (ins), "saved", []>;

  let rd = 1 in
    def RESTORED : F3_1<2, 0b110001, (outs), (ins), "restored", []>;
}

// Section A.42 - Prefetch Data
let Predicates = [HasV9] in {
  def PREFETCHr : F3_1<3, 0b101101,
                   (outs), (ins (MEMrr $rs1, $rs2):$addr, PrefetchTag:$rd),
                   "prefetch [$addr], $rd", []>;
  def PREFETCHi : F3_2<3, 0b101101,
                   (outs), (ins (MEMri $rs1, $simm13):$addr, PrefetchTag:$rd),
                   "prefetch [$addr], $rd", []>;
  def PREFETCHAr : F3_1_asi<3, 0b111101, (outs),
                    (ins (MEMrr $rs1, $rs2):$addr, ASITag:$asi, PrefetchTag:$rd),
                    "prefetcha [$addr] $asi, $rd", []>;
  let Uses = [ASR3] in
  def PREFETCHAi : F3_2<3, 0b111101, (outs),
                    (ins (MEMri $rs1, $simm13):$addr, PrefetchTag:$rd),
                    "prefetcha [$addr] %asi, $rd", []>;
}



// Section A.43 - Read Privileged Register Instructions
let Predicates = [HasV9] in {
let rs2 = 0 in
  def RDPR : F3_1<2, 0b101010,
                 (outs IntRegs:$rd), (ins PRRegs:$rs1),
                 "rdpr $rs1, $rd", []>;

// Special case %fq as the register is also used in V8
// (albeit with different instructions and encoding).
// This allows us to reuse the register definition and
// the "%fq" designation while giving it a different encoding.
let Uses = [FQ], rs1 = 15, rs2 = 0 in
  def RDFQ : F3_1<2, 0b101010,
                 (outs IntRegs:$rd), (ins),
                 "rdpr %fq, $rd", []>;
}

// Section A.62 - Write Privileged Register Instructions
let Predicates = [HasV9] in {
  def WRPRrr : F3_1<2, 0b110010,
                   (outs PRRegs:$rd), (ins IntRegs:$rs1, IntRegs:$rs2),
                   "wrpr $rs1, $rs2, $rd", []>;
  def WRPRri : F3_2<2, 0b110010,
                   (outs PRRegs:$rd), (ins IntRegs:$rs1, simm13Op:$simm13),
                   "wrpr $rs1, $simm13, $rd", []>;
}

//===----------------------------------------------------------------------===//
// Non-Instruction Patterns
//===----------------------------------------------------------------------===//

// Zero immediate.
def : Pat<(i32 0), (COPY (i32 G0))>;
// Small immediates.
def : Pat<(i32 simm13:$val),
          (ORri (i32 G0), imm:$val)>;
// Arbitrary immediates.
def : Pat<(i32 imm:$val),
          (ORri (SETHIi (HI22 imm:$val)), (LO10 imm:$val))>;

// Frame index.
def to_tframeindex : SDNodeXForm<frameindex, [{
  return CurDAG->getTargetFrameIndex(N->getIndex(), N->getValueType(0));
}]>;
def : Pat<(i32 (frameindex:$ptr)), (ADDri (i32 (to_tframeindex $ptr)), (i32 0))>;
def : Pat<(i64 (frameindex:$ptr)), (ADDri (i64 (to_tframeindex $ptr)), (i64 0))>;

// Global addresses, constant pool entries
let Predicates = [Is32Bit] in {

def : Pat<(SPhi tglobaladdr:$in), (SETHIi tglobaladdr:$in)>;
def : Pat<(SPlo tglobaladdr:$in), (ORri (i32 G0), tglobaladdr:$in)>;
def : Pat<(SPhi tconstpool:$in), (SETHIi tconstpool:$in)>;
def : Pat<(SPlo tconstpool:$in), (ORri (i32 G0), tconstpool:$in)>;

// GlobalTLS addresses
def : Pat<(SPhi tglobaltlsaddr:$in), (SETHIi tglobaltlsaddr:$in)>;
def : Pat<(SPlo tglobaltlsaddr:$in), (ORri (i32 G0), tglobaltlsaddr:$in)>;
def : Pat<(add (SPhi tglobaltlsaddr:$in1), (SPlo tglobaltlsaddr:$in2)),
          (ADDri (SETHIi tglobaltlsaddr:$in1), (tglobaltlsaddr:$in2))>;
def : Pat<(xor (SPhi tglobaltlsaddr:$in1), (SPlo tglobaltlsaddr:$in2)),
          (XORri (SETHIi tglobaltlsaddr:$in1), (tglobaltlsaddr:$in2))>;

// Blockaddress
def : Pat<(SPhi tblockaddress:$in), (SETHIi tblockaddress:$in)>;
def : Pat<(SPlo tblockaddress:$in), (ORri (i32 G0), tblockaddress:$in)>;

// Add reg, lo.  This is used when taking the addr of a global/constpool entry.
def : Pat<(add iPTR:$r, (SPlo tglobaladdr:$in)), (ADDri $r, tglobaladdr:$in)>;
def : Pat<(add iPTR:$r, (SPlo tconstpool:$in)),  (ADDri $r, tconstpool:$in)>;
def : Pat<(add iPTR:$r, (SPlo tblockaddress:$in)),
                        (ADDri $r, tblockaddress:$in)>;
}

// Calls:
def : Pat<(call tglobaladdr:$dst),
          (CALL tglobaladdr:$dst)>;
def : Pat<(call texternalsym:$dst),
          (CALL texternalsym:$dst)>;

// Map integer extload's to zextloads.
def : Pat<(i32 (extloadi1 ADDRrr:$src)), (LDUBrr ADDRrr:$src)>;
def : Pat<(i32 (extloadi1 ADDRri:$src)), (LDUBri ADDRri:$src)>;
def : Pat<(i32 (extloadi8 ADDRrr:$src)), (LDUBrr ADDRrr:$src)>;
def : Pat<(i32 (extloadi8 ADDRri:$src)), (LDUBri ADDRri:$src)>;
def : Pat<(i32 (extloadi16 ADDRrr:$src)), (LDUHrr ADDRrr:$src)>;
def : Pat<(i32 (extloadi16 ADDRri:$src)), (LDUHri ADDRri:$src)>;

// zextload bool -> zextload byte
def : Pat<(i32 (zextloadi1 ADDRrr:$src)), (LDUBrr ADDRrr:$src)>;
def : Pat<(i32 (zextloadi1 ADDRri:$src)), (LDUBri ADDRri:$src)>;

// store 0, addr -> store %g0, addr
def : Pat<(store (i32 0), ADDRrr:$dst), (STrr ADDRrr:$dst, (i32 G0))>;
def : Pat<(store (i32 0), ADDRri:$dst), (STri ADDRri:$dst, (i32 G0))>;

// store bar for all atomic_fence in V8.
let Predicates = [HasNoV9] in
  def : Pat<(atomic_fence timm, timm), (STBAR)>;

let Predicates = [HasV9] in
  def : Pat<(atomic_fence timm, timm), (MEMBARi 0xf)>;

// atomic_load addr -> load addr
def : Pat<(i32 (atomic_load_8 ADDRrr:$src)), (LDUBrr ADDRrr:$src)>;
def : Pat<(i32 (atomic_load_8 ADDRri:$src)), (LDUBri ADDRri:$src)>;
def : Pat<(i32 (atomic_load_16 ADDRrr:$src)), (LDUHrr ADDRrr:$src)>;
def : Pat<(i32 (atomic_load_16 ADDRri:$src)), (LDUHri ADDRri:$src)>;
def : Pat<(i32 (atomic_load_32 ADDRrr:$src)), (LDrr ADDRrr:$src)>;
def : Pat<(i32 (atomic_load_32 ADDRri:$src)), (LDri ADDRri:$src)>;

// atomic_store val, addr -> store val, addr
def : Pat<(atomic_store_8 i32:$val, ADDRrr:$dst), (STBrr ADDRrr:$dst, $val)>;
def : Pat<(atomic_store_8 i32:$val, ADDRri:$dst), (STBri ADDRri:$dst, $val)>;
def : Pat<(atomic_store_16 i32:$val, ADDRrr:$dst), (STHrr ADDRrr:$dst, $val)>;
def : Pat<(atomic_store_16 i32:$val, ADDRri:$dst), (STHri ADDRri:$dst, $val)>;
def : Pat<(atomic_store_32 i32:$val, ADDRrr:$dst), (STrr ADDRrr:$dst, $val)>;
def : Pat<(atomic_store_32 i32:$val, ADDRri:$dst), (STri ADDRri:$dst, $val)>;

let Predicates = [HasV9] in
def : Pat<(atomic_cmp_swap_i32 iPTR:$rs1, i32:$rs2, i32:$swap),
          (CASArr $rs1, $rs2, $swap, 0x80)>;

// Same pattern as CASArr above, but with a different ASI.
let Predicates = [HasLeonCASA] in
def : Pat<(atomic_cmp_swap_i32 iPTR:$rs1, i32:$rs2, i32:$swap),
          (CASArr $rs1, $rs2, $swap, 0x0A)>;

// A register pair with zero upper half.
// The upper part is done with ORrr instead of `COPY G0`
// or a normal register copy, since `COPY G0`s in that place
// will be converted into `COPY G0_G1` later on, which is not
// what we want in this case.
def : Pat<(build_vector (i32 0), (i32 IntRegs:$a2)),
          (INSERT_SUBREG (INSERT_SUBREG (v2i32 (IMPLICIT_DEF)),
            (ORrr (i32 G0), (i32 G0)), sub_even),
            (i32 IntRegs:$a2), sub_odd)>;

// extract_vector
def : Pat<(extractelt (v2i32 IntPair:$Rn), 0),
          (i32 (EXTRACT_SUBREG IntPair:$Rn, sub_even))>;
def : Pat<(extractelt (v2i32 IntPair:$Rn), 1),
          (i32 (EXTRACT_SUBREG IntPair:$Rn, sub_odd))>;

// build_vector
def : Pat<(build_vector (i32 IntRegs:$a1), (i32 IntRegs:$a2)),
          (INSERT_SUBREG
	    (INSERT_SUBREG (v2i32 (IMPLICIT_DEF)), (i32 IntRegs:$a1), sub_even),
            (i32 IntRegs:$a2), sub_odd)>;


include "SparcInstr64Bit.td"
include "SparcInstrVIS.td"
include "SparcInstrAliases.td"
