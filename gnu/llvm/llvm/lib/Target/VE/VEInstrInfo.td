//===-- VEInstrInfo.td - Target Description for VE Target -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the VE instructions in TableGen format.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Instruction format superclass
//===----------------------------------------------------------------------===//

include "VEInstrFormats.td"

//===----------------------------------------------------------------------===//
// Helper functions to retrieve target constants.
//
// VE instructions have a space to hold following immediates
//   $sy has 7 bits to represent simm7, uimm7, simm7fp, or uimm7fp.
//   $sz also has 7 bits to represent mimm or mimmfp.
//   $disp has 32 bits to represent simm32.
//
// The mimm is a special immediate value of sequential bit stream of 0 or 1.
//     `(m)0`: Represents 0 sequence then 1 sequence like 0b00...0011...11,
//             where `m` is equal to the number of leading zeros.
//     `(m)1`: Represents 1 sequence then 0 sequence like 0b11...1100...00,
//             where `m` is equal to the number of leading ones.
// Each bit of mimm's 7 bits is used like below:
//     bit 6  : If `(m)0`, this bit is 1.  Otherwise, this bit is 0.
//     bit 5-0: Represents the m (0-63).
// Use `!add(m, 64)` to generates an immediate value in pattern matchings.
//
// The floating point immediate value is not something like compacted value.
// It is simple integer representation, so it works rarely.
//     e.g. 0.0 (0x00000000) or -2.0 (0xC0000000=(2)1).
//===----------------------------------------------------------------------===//

def ULO7 : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(N->getZExtValue() & 0x7f,
                                   SDLoc(N), MVT::i32);
}]>;
def LO7 : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(SignExtend32(N->getSExtValue(), 7),
                                   SDLoc(N), MVT::i32);
}]>;
def MIMM : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(val2MImm(getImmVal(N)),
                                   SDLoc(N), MVT::i32);
}]>;
def LO32 : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(Lo_32(N->getZExtValue()),
                                   SDLoc(N), MVT::i32);
}]>;
def HI32 : SDNodeXForm<imm, [{
  // Transformation function: shift the immediate value down into the low bits.
  return CurDAG->getTargetConstant(Hi_32(N->getZExtValue()),
                                   SDLoc(N), MVT::i32);
}]>;

def LO7FP : SDNodeXForm<fpimm, [{
  uint64_t Val = getFpImmVal(N);
  return CurDAG->getTargetConstant(SignExtend32(Val, 7), SDLoc(N), MVT::i32);
}]>;
def MIMMFP : SDNodeXForm<fpimm, [{
  return CurDAG->getTargetConstant(val2MImm(getFpImmVal(N)),
                                   SDLoc(N), MVT::i32);
}]>;
def LOFP32 : SDNodeXForm<fpimm, [{
  return CurDAG->getTargetConstant(Lo_32(getFpImmVal(N) & 0xffffffff),
                                   SDLoc(N), MVT::i32);
}]>;
def HIFP32 : SDNodeXForm<fpimm, [{
  return CurDAG->getTargetConstant(Hi_32(getFpImmVal(N)), SDLoc(N), MVT::i32);
}]>;

def icond2cc : SDNodeXForm<cond, [{
  VECC::CondCode VECC = intCondCode2Icc(N->get());
  return CurDAG->getTargetConstant(VECC, SDLoc(N), MVT::i32);
}]>;

def icond2ccSwap : SDNodeXForm<cond, [{
  ISD::CondCode CC = getSetCCSwappedOperands(N->get());
  VECC::CondCode VECC = intCondCode2Icc(CC);
  return CurDAG->getTargetConstant(VECC, SDLoc(N), MVT::i32);
}]>;

def fcond2cc : SDNodeXForm<cond, [{
  VECC::CondCode VECC = fpCondCode2Fcc(N->get());
  return CurDAG->getTargetConstant(VECC, SDLoc(N), MVT::i32);
}]>;

def fcond2ccSwap : SDNodeXForm<cond, [{
  ISD::CondCode CC = getSetCCSwappedOperands(N->get());
  VECC::CondCode VECC = fpCondCode2Fcc(CC);
  return CurDAG->getTargetConstant(VECC, SDLoc(N), MVT::i32);
}]>;

def CCOP : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(N->getZExtValue(),
                                   SDLoc(N), MVT::i32);
}]>;

//===----------------------------------------------------------------------===//
// Feature predicates.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Instruction Pattern Stuff
//===----------------------------------------------------------------------===//

// zero
def ZeroAsmOperand : AsmOperandClass {
  let Name = "Zero";
}
def zero : Operand<i32>, PatLeaf<(imm), [{
    return N->getSExtValue() == 0; }]> {
  let ParserMatchClass = ZeroAsmOperand;
}

// uimm0to2 - Special immediate value represents 0, 1, and 2.
def UImm0to2AsmOperand : AsmOperandClass {
  let Name = "UImm0to2";
}
def uimm0to2 : Operand<i32>, PatLeaf<(imm), [{
    return N->getZExtValue() < 3; }], ULO7> {
  let ParserMatchClass = UImm0to2AsmOperand;
}

// uimm1 - Generic immediate value.
def UImm1AsmOperand : AsmOperandClass {
  let Name = "UImm1";
}
def uimm1 : Operand<i32>, PatLeaf<(imm), [{
    return isUInt<1>(N->getZExtValue()); }], ULO7> {
  let ParserMatchClass = UImm1AsmOperand;
}

// uimm2 - Generic immediate value.
def UImm2AsmOperand : AsmOperandClass {
  let Name = "UImm2";
}
def uimm2 : Operand<i32>, PatLeaf<(imm), [{
    return isUInt<2>(N->getZExtValue()); }], ULO7> {
  let ParserMatchClass = UImm2AsmOperand;
}

// uimm3 - Generic immediate value.
def UImm3AsmOperand : AsmOperandClass {
  let Name = "UImm3";
}
def uimm3 : Operand<i32>, PatLeaf<(imm), [{
    return isUInt<3>(N->getZExtValue()); }], ULO7> {
  let ParserMatchClass = UImm3AsmOperand;
}

// uimm4 - Generic immediate value.
def UImm4AsmOperand : AsmOperandClass {
  let Name = "UImm4";
}
def uimm4 : Operand<i32>, PatLeaf<(imm), [{
    return isUInt<4>(N->getZExtValue()); }], ULO7> {
  let ParserMatchClass = UImm4AsmOperand;
}

// uimm6 - Generic immediate value.
def UImm6AsmOperand : AsmOperandClass {
  let Name = "UImm6";
}
def uimm6 : Operand<i32>, PatLeaf<(imm), [{
    return isUInt<6>(N->getZExtValue()); }], ULO7> {
  let ParserMatchClass = UImm6AsmOperand;
}

// uimm7 - Generic immediate value.
def UImm7AsmOperand : AsmOperandClass {
  let Name = "UImm7";
}
def uimm7 : Operand<i32>, PatLeaf<(imm), [{
    return isUInt<7>(N->getZExtValue()); }], ULO7> {
  let ParserMatchClass = UImm7AsmOperand;
}

// simm7 - Generic immediate value.
def SImm7AsmOperand : AsmOperandClass {
  let Name = "SImm7";
}
def simm7 : Operand<i32>, PatLeaf<(imm), [{
    return isInt<7>(N->getSExtValue()); }], LO7> {
  let ParserMatchClass = SImm7AsmOperand;
  let DecoderMethod = "DecodeSIMM7";
}

// mimm - Special immediate value of sequential bit stream of 0 or 1.
def MImmAsmOperand : AsmOperandClass {
  let Name = "MImm";
  let ParserMethod = "parseMImmOperand";
}
def mimm : Operand<i32>, PatLeaf<(imm), [{
    return isMImmVal(getImmVal(N)); }], MIMM> {
  let ParserMatchClass = MImmAsmOperand;
  let PrintMethod = "printMImmOperand";
}

// zerofp - Generic fp immediate zero value.
def zerofp : Operand<i32>, PatLeaf<(fpimm), [{
    return getFpImmVal(N) == 0; }]> {
  let ParserMatchClass = ZeroAsmOperand;
}

// simm7fp - Generic fp immediate value.
def simm7fp : Operand<i32>, PatLeaf<(fpimm), [{
    return isInt<7>(getFpImmVal(N));
  }], LO7FP> {
  let ParserMatchClass = SImm7AsmOperand;
  let DecoderMethod = "DecodeSIMM7";
}

// mimmfp - Special fp immediate value of sequential bit stream of 0 or 1.
def mimmfp : Operand<i32>, PatLeaf<(fpimm), [{
    return isMImmVal(getFpImmVal(N)); }], MIMMFP> {
  let ParserMatchClass = MImmAsmOperand;
  let PrintMethod = "printMImmOperand";
}

// mimmfp32 - 32 bit width mimmfp
//   Float value places at higher bits, so ignore lower 32 bits.
def mimmfp32 : Operand<i32>, PatLeaf<(fpimm), [{
    return isMImm32Val(getFpImmVal(N) >> 32); }], MIMMFP> {
  let ParserMatchClass = MImmAsmOperand;
  let PrintMethod = "printMImmOperand";
}

// other generic patterns to use in pattern matchings
def simm32      : PatLeaf<(imm), [{ return isInt<32>(N->getSExtValue()); }]>;
def uimm32      : PatLeaf<(imm), [{ return isUInt<32>(N->getZExtValue()); }]>;
def lomsbzero   : PatLeaf<(imm), [{ return (N->getZExtValue() & 0x80000000)
                                      == 0; }]>;
def lozero      : PatLeaf<(imm), [{ return (N->getZExtValue() & 0xffffffff)
                                      == 0; }]>;
def fplomsbzero : PatLeaf<(fpimm), [{ return (getFpImmVal(N) & 0x80000000)
                                        == 0; }]>;
def fplozero    : PatLeaf<(fpimm), [{ return (getFpImmVal(N) & 0xffffffff)
                                        == 0; }]>;
def nonzero     : PatLeaf<(imm), [{ return N->getSExtValue() !=0 ; }]>;

def CCSIOp : PatLeaf<(cond), [{
  switch (N->get()) {
  default:          return true;
  case ISD::SETULT:
  case ISD::SETULE:
  case ISD::SETUGT:
  case ISD::SETUGE: return false;
  }
}]>;

def CCUIOp : PatLeaf<(cond), [{
  switch (N->get()) {
  default:         return true;
  case ISD::SETLT:
  case ISD::SETLE:
  case ISD::SETGT:
  case ISD::SETGE: return false;
  }
}]>;

//===----------------------------------------------------------------------===//
// Addressing modes.
// SX-Aurora has following fields.
//    sz: register or 0
//    sy: register or immediate (-64 to 63)
//    disp: immediate (-2147483648 to 2147483647)
//
// There are two kinds of instruction.
//    ASX format uses sz + sy + disp.
//    AS format uses sz + disp.
//
// Moreover, there are four kinds of assembly instruction format.
//    ASX format uses "disp", "disp(, sz)", "disp(sy)", "disp(sy, sz)",
//    "(, sz)", "(sy)", or "(sy, sz)".
//    AS format uses "disp", "disp(, sz)", or "(, sz)" in general.
//    AS format in RRM format uses "disp", "disp(sz)", or "(sz)".
//    AS format in RRM format for host memory access uses "sz", "(sz)",
//    or "disp(sz)".
//
// We defined them below.
//
// ASX format:
//    MEMrri, MEMrii, MEMzri, MEMzii
// AS format:
//    MEMriASX, MEMziASX    : simple AS format
//    MEMriRRM, MEMziRRM    : AS format in RRM format
//    MEMriHM, MEMziHM      : AS format in RRM format for host memory access
//===----------------------------------------------------------------------===//

// DAG selections for both ASX and AS formats.
def ADDRrri : ComplexPattern<iPTR, 3, "selectADDRrri", [frameindex], []>;
def ADDRrii : ComplexPattern<iPTR, 3, "selectADDRrii", [frameindex], []>;
def ADDRzri : ComplexPattern<iPTR, 3, "selectADDRzri", [], []>;
def ADDRzii : ComplexPattern<iPTR, 3, "selectADDRzii", [], []>;
def ADDRri : ComplexPattern<iPTR, 2, "selectADDRri", [frameindex], []>;
def ADDRzi : ComplexPattern<iPTR, 2, "selectADDRzi", [], []>;

// ASX format.
def VEMEMrriAsmOperand : AsmOperandClass {
  let Name = "MEMrri";
  let ParserMethod = "parseMEMOperand";
}
def VEMEMriiAsmOperand : AsmOperandClass {
  let Name = "MEMrii";
  let ParserMethod = "parseMEMOperand";
}
def VEMEMzriAsmOperand : AsmOperandClass {
  let Name = "MEMzri";
  let ParserMethod = "parseMEMOperand";
}
def VEMEMziiAsmOperand : AsmOperandClass {
  let Name = "MEMzii";
  let ParserMethod = "parseMEMOperand";
}

// ASX format uses single assembly instruction format.
def MEMrri : Operand<iPTR> {
  let PrintMethod = "printMemASXOperand";
  let MIOperandInfo = (ops ptr_rc, ptr_rc, i64imm);
  let ParserMatchClass = VEMEMrriAsmOperand;
}
def MEMrii : Operand<iPTR> {
  let PrintMethod = "printMemASXOperand";
  let MIOperandInfo = (ops ptr_rc, i32imm, i64imm);
  let ParserMatchClass = VEMEMriiAsmOperand;
}
def MEMzri : Operand<iPTR> {
  let PrintMethod = "printMemASXOperand";
  let MIOperandInfo = (ops i32imm /* = 0 */, ptr_rc, i64imm);
  let ParserMatchClass = VEMEMzriAsmOperand;
}
def MEMzii : Operand<iPTR> {
  let PrintMethod = "printMemASXOperand";
  let MIOperandInfo = (ops i32imm /* = 0 */, i32imm, i64imm);
  let ParserMatchClass = VEMEMziiAsmOperand;
}

// AS format.
def VEMEMriAsmOperand : AsmOperandClass {
  let Name = "MEMri";
  let ParserMethod = "parseMEMAsOperand";
}
def VEMEMziAsmOperand : AsmOperandClass {
  let Name = "MEMzi";
  let ParserMethod = "parseMEMAsOperand";
}

// AS format uses multiple assembly instruction formats
//   1. AS generic assembly instruction format:
def MEMriASX : Operand<iPTR> {
  let PrintMethod = "printMemASOperandASX";
  let MIOperandInfo = (ops ptr_rc, i32imm);
  let ParserMatchClass = VEMEMriAsmOperand;
}
def MEMziASX : Operand<iPTR> {
  let PrintMethod = "printMemASOperandASX";
  let MIOperandInfo = (ops i32imm /* = 0 */, i32imm);
  let ParserMatchClass = VEMEMziAsmOperand;
}

//   2. AS RRM style assembly instruction format:
def MEMriRRM : Operand<iPTR> {
  let PrintMethod = "printMemASOperandRRM";
  let MIOperandInfo = (ops ptr_rc, i32imm);
  let ParserMatchClass = VEMEMriAsmOperand;
}
def MEMziRRM : Operand<iPTR> {
  let PrintMethod = "printMemASOperandRRM";
  let MIOperandInfo = (ops i32imm /* = 0 */, i32imm);
  let ParserMatchClass = VEMEMziAsmOperand;
}

//   3. AS HM style assembly instruction format:
def MEMriHM : Operand<iPTR> {
  let PrintMethod = "printMemASOperandHM";
  let MIOperandInfo = (ops ptr_rc, i32imm);
  let ParserMatchClass = VEMEMriAsmOperand;
}
def MEMziHM : Operand<iPTR> {
  let PrintMethod = "printMemASOperandHM";
  let MIOperandInfo = (ops i32imm /* = 0 */, i32imm);
  let ParserMatchClass = VEMEMziAsmOperand;
}

//===----------------------------------------------------------------------===//
// Other operands.
//===----------------------------------------------------------------------===//

// Branch targets have OtherVT type.
def brtarget32 : Operand<OtherVT> {
  let EncoderMethod = "getBranchTargetOpValue";
  let DecoderMethod = "DecodeSIMM32";
}

// Operand for printing out a condition code.
def CCOpAsmOperand : AsmOperandClass { let Name = "CCOp"; }
def CCOp : Operand<i32>, ImmLeaf<i32, [{
    return Imm >= 0 && Imm < 22; }], CCOP> {
  let PrintMethod = "printCCOperand";
  let DecoderMethod = "DecodeCCOperand";
  let EncoderMethod = "getCCOpValue";
  let ParserMatchClass = CCOpAsmOperand;
}

// Operand for a rounding mode code.
def RDOpAsmOperand : AsmOperandClass {
  let Name = "RDOp";
}
def RDOp : Operand<i32> {
  let PrintMethod = "printRDOperand";
  let DecoderMethod = "DecodeRDOperand";
  let EncoderMethod = "getRDOpValue";
  let ParserMatchClass = RDOpAsmOperand;
}

def VEhi    : SDNode<"VEISD::Hi", SDTIntUnaryOp>;
def VElo    : SDNode<"VEISD::Lo", SDTIntUnaryOp>;

//  These are target-independent nodes, but have target-specific formats.
def SDT_SPCallSeqStart : SDCallSeqStart<[ SDTCisVT<0, i64>,
                                          SDTCisVT<1, i64> ]>;
def SDT_SPCallSeqEnd   : SDCallSeqEnd<[ SDTCisVT<0, i64>,
                                        SDTCisVT<1, i64> ]>;

def callseq_start : SDNode<"ISD::CALLSEQ_START", SDT_SPCallSeqStart,
                           [SDNPHasChain, SDNPOutGlue]>;
def callseq_end   : SDNode<"ISD::CALLSEQ_END",   SDT_SPCallSeqEnd,
                           [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

def SDT_SPCall    : SDTypeProfile<0, -1, [SDTCisVT<0, i64>]>;
def call          : SDNode<"VEISD::CALL", SDT_SPCall,
                           [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                            SDNPVariadic]>;

def retglue       : SDNode<"VEISD::RET_GLUE", SDTNone,
                           [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;

def getGOT        : Operand<iPTR>;

// Comparisons
def cmpi          : SDNode<"VEISD::CMPI", SDTIntBinOp>;
def cmpu          : SDNode<"VEISD::CMPU", SDTIntBinOp>;
def cmpf          : SDNode<"VEISD::CMPF", SDTFPBinOp>;
def SDT_Cmpq      : SDTypeProfile<1, 2, [SDTCisSameAs<1, 2>, SDTCisFP<0>,
                                  SDTCisFP<2>]>;
def cmpq          : SDNode<"VEISD::CMPQ", SDT_Cmpq>;

// res = cmov cmp, t, f, cond
def SDT_Cmov      : SDTypeProfile<1, 4, [SDTCisSameAs<0, 2>, SDTCisSameAs<2, 3>,
                                  SDTCisVT<4, i32>]>;
def cmov          : SDNode<"VEISD::CMOV", SDT_Cmov>;

def VEeh_sjlj_setjmp: SDNode<"VEISD::EH_SJLJ_SETJMP",
                             SDTypeProfile<1, 1, [SDTCisInt<0>,
                                                  SDTCisPtrTy<1>]>,
                             [SDNPHasChain, SDNPSideEffect]>;
def VEeh_sjlj_longjmp: SDNode<"VEISD::EH_SJLJ_LONGJMP",
                              SDTypeProfile<0, 1, [SDTCisPtrTy<0>]>,
                              [SDNPHasChain, SDNPSideEffect]>;
def VEeh_sjlj_setup_dispatch: SDNode<"VEISD::EH_SJLJ_SETUP_DISPATCH",
                                     SDTypeProfile<0, 0, []>,
                                     [SDNPHasChain, SDNPSideEffect]>;

// GETFUNPLT for PIC
def GetFunPLT : SDNode<"VEISD::GETFUNPLT", SDTIntUnaryOp>;

// GETTLSADDR for TLS
def GetTLSAddr : SDNode<"VEISD::GETTLSADDR", SDT_SPCall,
                        [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                         SDNPVariadic]>;

// GETSTACKTOP
def GetStackTop : SDNode<"VEISD::GETSTACKTOP", SDTNone,
                        [SDNPHasChain, SDNPSideEffect]>;

// TS1AM
def SDT_TS1AM : SDTypeProfile<1, 3, [SDTCisSameAs<0, 3>, SDTCisPtrTy<1>,
                                     SDTCisVT<2, i32>, SDTCisInt<3>]>;
def ts1am     : SDNode<"VEISD::TS1AM", SDT_TS1AM,
                       [SDNPHasChain, SDNPMayStore, SDNPMayLoad,
                        SDNPMemOperand]>;

//===----------------------------------------------------------------------===//
// VE Flag Conditions
//===----------------------------------------------------------------------===//

// Note that these values must be kept in sync with the CCOp::CondCode enum
// values.
class CC_VAL<int N> : PatLeaf<(i32 N)>;
def CC_IG    : CC_VAL< 0>;  // Greater
def CC_IL    : CC_VAL< 1>;  // Less
def CC_INE   : CC_VAL< 2>;  // Not Equal
def CC_IEQ   : CC_VAL< 3>;  // Equal
def CC_IGE   : CC_VAL< 4>;  // Greater or Equal
def CC_ILE   : CC_VAL< 5>;  // Less or Equal
def CC_AF    : CC_VAL< 6>;  // Always false
def CC_G     : CC_VAL< 7>;  // Greater
def CC_L     : CC_VAL< 8>;  // Less
def CC_NE    : CC_VAL< 9>;  // Not Equal
def CC_EQ    : CC_VAL<10>;  // Equal
def CC_GE    : CC_VAL<11>;  // Greater or Equal
def CC_LE    : CC_VAL<12>;  // Less or Equal
def CC_NUM   : CC_VAL<13>;  // Number
def CC_NAN   : CC_VAL<14>;  // NaN
def CC_GNAN  : CC_VAL<15>;  // Greater or NaN
def CC_LNAN  : CC_VAL<16>;  // Less or NaN
def CC_NENAN : CC_VAL<17>;  // Not Equal or NaN
def CC_EQNAN : CC_VAL<18>;  // Equal or NaN
def CC_GENAN : CC_VAL<19>;  // Greater or Equal or NaN
def CC_LENAN : CC_VAL<20>;  // Less or Equal or NaN
def CC_AT    : CC_VAL<21>;  // Always true

//===----------------------------------------------------------------------===//
// VE Rounding Mode
//===----------------------------------------------------------------------===//

// Note that these values must be kept in sync with the VERD::RoundingMode enum
// values.
class RD_VAL<int N> : PatLeaf<(i32 N)>;
def RD_NONE  : RD_VAL< 0>;  // According to PSW
def RD_RZ    : RD_VAL< 8>;  // Round toward Zero
def RD_RP    : RD_VAL< 9>;  // Round toward Plus infinity
def RD_RM    : RD_VAL<10>;  // Round toward Minus infinity
def RD_RN    : RD_VAL<11>;  // Round to Nearest (ties to Even)
def RD_RA    : RD_VAL<12>;  // Round to Nearest (ties to Away)

//===----------------------------------------------------------------------===//
// VE Multiclasses for common instruction formats
//===----------------------------------------------------------------------===//

// Multiclass for generic RR type instructions
let hasSideEffects = 0 in
multiclass RRbm<string opcStr, bits<8>opc,
                RegisterClass RCo, ValueType Tyo,
                RegisterClass RCi, ValueType Tyi,
                SDPatternOperator OpNode = null_frag,
                Operand immOp = simm7, Operand mOp = mimm,
                bit MoveImm = 0> {
  def rr : RR<opc, (outs RCo:$sx), (ins RCi:$sy, RCi:$sz),
              !strconcat(opcStr, " $sx, $sy, $sz"),
              [(set Tyo:$sx, (OpNode Tyi:$sy, Tyi:$sz))]>;
  // VE calculates (OpNode $sy, $sz), but llvm requires to have immediate
  // in RHS, so we use following definition.
  let cy = 0 in
  def ri : RR<opc, (outs RCo:$sx), (ins RCi:$sz, immOp:$sy),
              !strconcat(opcStr, " $sx, $sy, $sz"),
              [(set Tyo:$sx, (OpNode Tyi:$sz, (Tyi immOp:$sy)))]>;
  let cz = 0 in
  def rm : RR<opc, (outs RCo:$sx), (ins RCi:$sy, mOp:$sz),
              !strconcat(opcStr, " $sx, $sy, $sz"),
              [(set Tyo:$sx, (OpNode Tyi:$sy, (Tyi mOp:$sz)))]>;
  let cy = 0, cz = 0 in
  def im : RR<opc, (outs RCo:$sx), (ins immOp:$sy, mOp:$sz),
              !strconcat(opcStr, " $sx, $sy, $sz"),
              [(set Tyo:$sx, (OpNode (Tyi immOp:$sy), (Tyi mOp:$sz)))]> {
    // VE uses ORim as a move immediate instruction, so declare it here.
    // An instruction declared as MoveImm will be optimized in FoldImmediate
    // later.
    let isMoveImm = MoveImm;
  }
}

// Multiclass for non-commutative RR type instructions
let hasSideEffects = 0 in
multiclass RRNCbm<string opcStr, bits<8>opc,
                RegisterClass RCo, ValueType Tyo,
                RegisterClass RCi, ValueType Tyi,
                SDPatternOperator OpNode = null_frag,
                Operand immOp = simm7, Operand mOp = mimm> {
  def rr : RR<opc, (outs RCo:$sx), (ins RCi:$sy, RCi:$sz),
              !strconcat(opcStr, " $sx, $sy, $sz"),
              [(set Tyo:$sx, (OpNode Tyi:$sy, Tyi:$sz))]>;
  let cy = 0 in
  def ir : RR<opc, (outs RCo:$sx), (ins immOp:$sy, RCi:$sz),
              !strconcat(opcStr, " $sx, $sy, $sz"),
              [(set Tyo:$sx, (OpNode (Tyi immOp:$sy), Tyi:$sz))]>;
  let cz = 0 in
  def rm : RR<opc, (outs RCo:$sx), (ins RCi:$sy, mOp:$sz),
              !strconcat(opcStr, " $sx, $sy, $sz"),
              [(set Tyo:$sx, (OpNode Tyi:$sy, (Tyi mOp:$sz)))]>;
  let cy = 0, cz = 0 in
  def im : RR<opc, (outs RCo:$sx), (ins immOp:$sy, mOp:$sz),
              !strconcat(opcStr, " $sx, $sy, $sz"),
              [(set Tyo:$sx, (OpNode (Tyi immOp:$sy), (Tyi mOp:$sz)))]>;
}

// Generic RR multiclass with 2 arguments.
//   e.g. ADDUL, ADDSWSX, ADDSWZX, and etc.
multiclass RRm<string opcStr, bits<8>opc,
               RegisterClass RC, ValueType Ty,
               SDPatternOperator OpNode = null_frag,
               Operand immOp = simm7, Operand mOp = mimm, bit MoveImm = 0> :
  RRbm<opcStr, opc, RC, Ty, RC, Ty, OpNode, immOp, mOp, MoveImm>;

// Generic RR multiclass for non-commutative instructions with 2 arguments.
//   e.g. SUBUL, SUBUW, SUBSWSX, and etc.
multiclass RRNCm<string opcStr, bits<8>opc,
                 RegisterClass RC, ValueType Ty,
                 SDPatternOperator OpNode = null_frag,
                 Operand immOp = simm7, Operand mOp = mimm> :
  RRNCbm<opcStr, opc, RC, Ty, RC, Ty, OpNode, immOp, mOp>;

// Generic RR multiclass for floating point instructions with 2 arguments.
//   e.g. FADDD, FADDS, FSUBD, and etc.
multiclass RRFm<string opcStr, bits<8>opc,
                RegisterClass RC, ValueType Ty,
                SDPatternOperator OpNode = null_frag,
                Operand immOp = simm7fp, Operand mOp = mimmfp> :
  RRNCbm<opcStr, opc, RC, Ty, RC, Ty, OpNode, immOp, mOp>;

// Generic RR multiclass for shift instructions with 2 arguments.
//   e.g. SLL, SRL, SLAWSX, and etc.
let hasSideEffects = 0 in
multiclass RRIm<string opcStr, bits<8>opc,
                RegisterClass RC, ValueType Ty,
                SDPatternOperator OpNode = null_frag> {
  def rr : RR<opc, (outs RC:$sx), (ins RC:$sz, I32:$sy),
              !strconcat(opcStr, " $sx, $sz, $sy"),
              [(set Ty:$sx, (OpNode Ty:$sz, i32:$sy))]>;
  let cz = 0 in
  def mr : RR<opc, (outs RC:$sx), (ins mimm:$sz, I32:$sy),
              !strconcat(opcStr, " $sx, $sz, $sy"),
              [(set Ty:$sx, (OpNode (Ty mimm:$sz), i32:$sy))]>;
  let cy = 0 in
  def ri : RR<opc, (outs RC:$sx), (ins RC:$sz, uimm7:$sy),
              !strconcat(opcStr, " $sx, $sz, $sy"),
              [(set Ty:$sx, (OpNode Ty:$sz, (i32 uimm7:$sy)))]>;
  let cy = 0, cz = 0 in
  def mi : RR<opc, (outs RC:$sx), (ins mimm:$sz, uimm7:$sy),
              !strconcat(opcStr, " $sx, $sz, $sy"),
              [(set Ty:$sx, (OpNode (Ty mimm:$sz), (i32 uimm7:$sy)))]>;
}

// Special RR multiclass for 128 bits shift left instruction.
//   e.g. SLD
let Constraints = "$hi = $sx", DisableEncoding = "$hi", hasSideEffects = 0 in
multiclass RRILDm<string opcStr, bits<8>opc, RegisterClass RC> {
  def rrr : RR<opc, (outs RC:$sx), (ins RC:$hi, RC:$sz, I32:$sy),
              !strconcat(opcStr, " $sx, $sz, $sy")>;
  let cz = 0 in
  def rmr : RR<opc, (outs RC:$sx), (ins RC:$hi, mimm:$sz, I32:$sy),
              !strconcat(opcStr, " $sx, $sz, $sy")>;
  let cy = 0 in
  def rri : RR<opc, (outs RC:$sx), (ins RC:$hi, RC:$sz, uimm7:$sy),
              !strconcat(opcStr, " $sx, $sz, $sy")>;
  let cy = 0, cz = 0 in
  def rmi : RR<opc, (outs RC:$sx), (ins RC:$hi, mimm:$sz, uimm7:$sy),
              !strconcat(opcStr, " $sx, $sz, $sy")>;
}

// Special RR multiclass for 128 bits shift right instruction.
//   e.g. SRD
let Constraints = "$low = $sx", DisableEncoding = "$low", hasSideEffects = 0 in
multiclass RRIRDm<string opcStr, bits<8>opc, RegisterClass RC> {
  def rrr : RR<opc, (outs RC:$sx), (ins RC:$sz, RC:$low, I32:$sy),
              !strconcat(opcStr, " $sx, $sz, $sy")>;
  let cz = 0 in
  def mrr : RR<opc, (outs RC:$sx), (ins mimm:$sz, RC:$low, I32:$sy),
              !strconcat(opcStr, " $sx, $sz, $sy")>;
  let cy = 0 in
  def rri : RR<opc, (outs RC:$sx), (ins RC:$sz, RC:$low, uimm7:$sy),
              !strconcat(opcStr, " $sx, $sz, $sy")>;
  let cy = 0, cz = 0 in
  def mri : RR<opc, (outs RC:$sx), (ins mimm:$sz, RC:$low, uimm7:$sy),
              !strconcat(opcStr, " $sx, $sz, $sy")>;
}

// Generic RR multiclass with an argument.
//   e.g. LDZ, PCNT, and  BRV
let cy = 0, sy = 0, hasSideEffects = 0 in
multiclass RRI1m<string opcStr, bits<8>opc, RegisterClass RC, ValueType Ty,
                 SDPatternOperator OpNode = null_frag> {
  def r : RR<opc, (outs RC:$sx), (ins RC:$sz), !strconcat(opcStr, " $sx, $sz"),
             [(set Ty:$sx, (OpNode Ty:$sz))]>;
  let cz = 0 in
  def m : RR<opc, (outs RC:$sx), (ins mimm:$sz),
             !strconcat(opcStr, " $sx, $sz"),
             [(set Ty:$sx, (OpNode (Ty mimm:$sz)))]>;
}

// Special RR multiclass for MRG instruction.
//   e.g. MRG
let Constraints = "$sx = $sd", DisableEncoding = "$sd", hasSideEffects = 0 in
multiclass RRMRGm<string opcStr, bits<8>opc, RegisterClass RC> {
  def rr : RR<opc, (outs RC:$sx), (ins RC:$sy, RC:$sz, RC:$sd),
              !strconcat(opcStr, " $sx, $sy, $sz")>;
  let cy = 0 in
  def ir : RR<opc, (outs RC:$sx), (ins simm7:$sy, RC:$sz, RC:$sd),
              !strconcat(opcStr, " $sx, $sy, $sz")>;
  let cz = 0 in
  def rm : RR<opc, (outs RC:$sx), (ins RC:$sy, mimm:$sz, RC:$sd),
              !strconcat(opcStr, " $sx, $sy, $sz")>;
  let cy = 0, cz = 0 in
  def im : RR<opc, (outs RC:$sx), (ins simm7:$sy, mimm:$sz, RC:$sd),
              !strconcat(opcStr, " $sx, $sy, $sz")>;
}

// Special RR multiclass for BSWP instruction.
//   e.g. BSWP
let hasSideEffects = 0 in
multiclass RRSWPm<string opcStr, bits<8>opc,
                  RegisterClass RC, ValueType Ty,
                  SDPatternOperator OpNode = null_frag> {
  let cy = 0 in
  def ri : RR<opc, (outs RC:$sx), (ins RC:$sz, uimm1:$sy),
              !strconcat(opcStr, " $sx, $sz, $sy"),
              [(set Ty:$sx, (OpNode Ty:$sz, (i32 uimm1:$sy)))]>;
  let cy = 0, cz = 0 in
  def mi : RR<opc, (outs RC:$sx), (ins mimm:$sz, uimm1:$sy),
              !strconcat(opcStr, " $sx, $sz, $sy"),
              [(set Ty:$sx, (OpNode (Ty mimm:$sz), (i32 uimm1:$sy)))]>;
}

// Multiclass for CMOV instructions.
//   e.g. CMOVL, CMOVW, CMOVD, and etc.
let Constraints = "$sx = $sd", DisableEncoding = "$sd", hasSideEffects = 0,
    cfw = ? in
multiclass RRCMOVm<string opcStr, bits<8>opc, RegisterClass RC, ValueType Ty,
                   SDPatternOperator OpNode = null_frag,
                   Operand immOp = simm7> {
  def rr : RR<opc, (outs I64:$sx), (ins CCOp:$cfw, RC:$sy, I64:$sz, I64:$sd),
              !strconcat(opcStr, " $sx, $sz, $sy"),
              [(set i64:$sx, (OpNode Ty:$sy, i64:$sz, i64:$sd,
                                     (i32 CCOp:$cfw)))]>;
  let cy = 0 in
  def ir : RR<opc, (outs I64:$sx),
              (ins CCOp:$cfw, immOp:$sy, I64:$sz, I64:$sd),
              !strconcat(opcStr, " $sx, $sz, $sy"),
              [(set i64:$sx, (OpNode (Ty immOp:$sy), i64:$sz, i64:$sd,
                                     (i32 CCOp:$cfw)))]>;
  let cz = 0 in
  def rm : RR<opc, (outs I64:$sx),
              (ins CCOp:$cfw, RC:$sy, mimm:$sz, I64:$sd),
              !strconcat(opcStr, " $sx, $sz, $sy"),
              [(set i64:$sx, (OpNode Ty:$sy, (i64 mimm:$sz), i64:$sd,
                                     (i32 CCOp:$cfw)))]>;
  let cy = 0, cz = 0 in
  def im : RR<opc, (outs I64:$sx),
              (ins CCOp:$cfw, immOp:$sy, mimm:$sz, I64:$sd),
              !strconcat(opcStr, " $sx, $sz, $sy"),
              [(set i64:$sx, (OpNode (Ty immOp:$sy), (i64 mimm:$sz), i64:$sd,
                                     (i32 CCOp:$cfw)))]>;
}

// Multiclass for floating point conversion instructions.
//   e.g. CVTWDSX, CVTWDZX, CVTWSSX, and etc.
// sz{3-0} = rounding mode
let cz = 0, hasSideEffects = 0 in
multiclass CVTRDm<string opcStr, bits<8> opc, RegisterClass RCo,
                  RegisterClass RCi> {
  def r : RR<opc, (outs RCo:$sx), (ins RDOp:$rd, RCi:$sy),
             !strconcat(opcStr, "${rd} $sx, $sy")> {
    bits<4> rd;
    let sz{6-4} = 0;
    let sz{3-0} = rd;
  }
  let cy = 0 in
  def i : RR<opc, (outs RCo:$sx), (ins RDOp:$rd, simm7:$sy),
             !strconcat(opcStr, "${rd} $sx, $sy")> {
    bits<4> rd;
    let sz{6-4} = 0;
    let sz{3-0} = rd;
  }
}

// Multiclass for floating point conversion instructions.
//   e.g. CVTDW, CVTSW, CVTDL, and etc.
let cz = 0, sz = 0, hasSideEffects = 0 in
multiclass CVTm<string opcStr, bits<8> opc, RegisterClass RCo, ValueType Tyo,
                RegisterClass RCi, ValueType Tyi,
                SDPatternOperator OpNode = null_frag> {
  def r : RR<opc, (outs RCo:$sx), (ins RCi:$sy),
             !strconcat(opcStr, " $sx, $sy"),
             [(set Tyo:$sx, (OpNode Tyi:$sy))]>;
  let cy = 0 in
  def i : RR<opc, (outs RCo:$sx), (ins simm7:$sy),
             !strconcat(opcStr, " $sx, $sy")>;
}

// Multiclass for PFCH instructions.
//   e.g. PFCH
let sx = 0, hasSideEffects = 0 in
multiclass PFCHm<string opcStr, bits<8>opc> {
  def rri : RM<opc, (outs), (ins (MEMrri $sz, $sy, $imm32):$addr), !strconcat(opcStr, " $addr"),
               [(prefetch ADDRrri:$addr, imm, imm, (i32 1))]>;
  let cy = 0 in
  def rii : RM<opc, (outs), (ins (MEMrii $sz, $sy, $imm32):$addr), !strconcat(opcStr, " $addr"),
               [(prefetch ADDRrii:$addr, imm, imm, (i32 1))]>;
  let cz = 0 in
  def zri : RM<opc, (outs), (ins (MEMzri $sz, $sy, $imm32):$addr), !strconcat(opcStr, " $addr"),
               [(prefetch ADDRzri:$addr, imm, imm, (i32 1))]>;
  let cy = 0, cz = 0 in
  def zii : RM<opc, (outs), (ins (MEMzii $sz, $sy, $imm32):$addr), !strconcat(opcStr, " $addr"),
               [(prefetch ADDRzii:$addr, imm, imm, (i32 1))]>;
}

// Multiclass for CAS instructions.
//   e.g. TS1AML, TS1AMW, TS2AM, and etc.
let Constraints = "$sx = $sd", DisableEncoding = "$sd",
    mayStore=1, mayLoad = 1, hasSideEffects = 0 in
multiclass RRCAStgm<string opcStr, bits<8>opc, RegisterClass RC, ValueType Ty,
                    Operand immOp, Operand MEM, ComplexPattern ADDR,
                    SDPatternOperator OpNode = null_frag> {
  def r : RRM<opc, (outs RC:$sx), (ins (MEM $sz, $imm32):$addr, RC:$sy, RC:$sd),
              !strconcat(opcStr, " $sx, $addr, $sy"),
              [(set Ty:$sx, (OpNode ADDR:$addr, Ty:$sy, Ty:$sd))]>;
  let cy = 0 in
  def i : RRM<opc, (outs RC:$sx), (ins (MEM $sz, $imm32):$addr, immOp:$sy, RC:$sd),
              !strconcat(opcStr, " $sx, $addr, $sy"),
              [(set Ty:$sx, (OpNode ADDR:$addr, (Ty immOp:$sy), Ty:$sd))]>;
}
multiclass RRCASm<string opcStr, bits<8>opc, RegisterClass RC, ValueType Ty,
                  Operand immOp, SDPatternOperator OpNode = null_frag> {
  defm ri : RRCAStgm<opcStr, opc, RC, Ty, immOp, MEMriRRM, ADDRri, OpNode>;
  let cz = 0 in
  defm zi : RRCAStgm<opcStr, opc, RC, Ty, immOp, MEMziRRM, ADDRzi, OpNode>;
}

// Multiclass for branch instructions
//   e.g. BCFL, BCFW, BCFD, and etc.
let isBranch = 1, isTerminator = 1, isIndirectBranch = 1, hasSideEffects = 0 in
multiclass BCbpfm<string opcStr, string cmpStr, bits<8> opc, dag cond,
                  Operand ADDR> {
  let bpf = 0 /* NONE */ in
  def "" : CF<opc, (outs), !con(cond, (ins (ADDR $sz, $imm32):$addr)),
              !strconcat(opcStr, " ", cmpStr, "$addr")>;
  let bpf = 2 /* NOT TAKEN */ in
  def _nt : CF<opc, (outs), !con(cond, (ins (ADDR $sz, $imm32):$addr)),
               !strconcat(opcStr, ".nt ", cmpStr, "$addr")>;
  let bpf = 3 /* TAKEN */ in
  def _t : CF<opc, (outs), !con(cond, (ins (ADDR $sz, $imm32):$addr)),
              !strconcat(opcStr, ".t ", cmpStr, "$addr")>;
}
multiclass BCtgm<string opcStr, string cmpStr, bits<8> opc, dag cond> {
  defm ri : BCbpfm<opcStr, cmpStr, opc, cond, MEMriASX>;
  let cz = 0 in defm zi : BCbpfm<opcStr, cmpStr, opc, cond, MEMziASX>;
}
multiclass BCm<string opcStr, string opcStrAt, string opcStrAf, bits<8> opc,
               RegisterClass RC, Operand immOp> {
  let DecoderMethod = "DecodeBranchCondition" in
  defm r : BCtgm<opcStr, "$sy, ", opc, (ins CCOp:$cond, RC:$sy)>;
  let DecoderMethod = "DecodeBranchCondition", cy = 0 in
  defm i : BCtgm<opcStr, "$sy, ", opc, (ins CCOp:$cond, immOp:$sy)>;
  let DecoderMethod = "DecodeBranchConditionAlways", cy = 0, sy = 0,
      cond = 15 /* AT */, isBarrier = 1 in
  defm a : BCtgm<opcStrAt, "", opc, (ins)>;
  let DecoderMethod = "DecodeBranchConditionAlways", cy = 0, sy = 0,
      cond = 0 /* AF */ in
  defm na : BCtgm<opcStrAf, "", opc, (ins)>;
}

// Multiclass for relative branch instructions
//   e.g. BRCFL, BRCFW, BRCFD, and etc.
let isBranch = 1, isTerminator = 1, hasSideEffects = 0 in
multiclass BCRbpfm<string opcStr, string cmpStr, bits<8> opc, dag cond> {
  let bpf = 0 /* NONE */ in
  def "" : CF<opc, (outs), !con(cond, (ins brtarget32:$imm32)),
              !strconcat(opcStr, " ", cmpStr, "$imm32")>;
  let bpf = 2 /* NOT TAKEN */ in
  def _nt : CF<opc, (outs), !con(cond, (ins brtarget32:$imm32)),
               !strconcat(opcStr, ".nt ", cmpStr, "$imm32")>;
  let bpf = 3 /* TAKEN */ in
  def _t : CF<opc, (outs), !con(cond, (ins brtarget32:$imm32)),
              !strconcat(opcStr, ".t ", cmpStr, "$imm32")>;
}
multiclass BCRm<string opcStr, string opcStrAt, string opcStrAf, bits<8> opc,
               RegisterClass RC, Operand immOp, Operand zeroOp> {
  defm rr : BCRbpfm<opcStr, "$sy, $sz, ", opc, (ins CCOp:$cond, RC:$sy, RC:$sz)>;
  let cy = 0 in
  defm ir : BCRbpfm<opcStr, "$sy, $sz, ", opc, (ins CCOp:$cond, immOp:$sy,
                                                    RC:$sz)>;
  let cz = 0 in
  defm rz : BCRbpfm<opcStr, "$sy, $sz, ", opc, (ins CCOp:$cond, RC:$sy,
                                                    zeroOp:$sz)>;
  let cy = 0, cz = 0 in
  defm iz : BCRbpfm<opcStr, "$sy, $sz, ", opc, (ins CCOp:$cond, immOp:$sy,
                                                    zeroOp:$sz)>;
  let cy = 0, sy = 0, cz = 0, sz = 0, cond = 15 /* AT */, isBarrier = 1 in
  defm a : BCRbpfm<opcStrAt, "", opc, (ins)>;
  let cy = 0, sy = 0, cz = 0, sz = 0, cond = 0 /* AF */ in
  defm na : BCRbpfm<opcStrAf, "", opc, (ins)>;
}

// Multiclass for communication register instructions.
//   e.g. LCR
let hasSideEffects = 1 in
multiclass LOADCRm<string opcStr, bits<8>opc, RegisterClass RC> {
  def rr : RR<opc, (outs RC:$sx), (ins RC:$sy, RC:$sz),
              !strconcat(opcStr, " $sx, $sy, $sz")>;
  let cy = 0 in def ir : RR<opc, (outs RC:$sx), (ins simm7:$sy, RC:$sz),
                            !strconcat(opcStr, " $sx, $sy, $sz")>;
  let cz = 0 in def rz : RR<opc, (outs RC:$sx), (ins RC:$sy, zero:$sz),
                            !strconcat(opcStr, " $sx, $sy, $sz")>;
  let cy = 0, cz = 0 in
  def iz : RR<opc, (outs RC:$sx), (ins simm7:$sy, zero:$sz),
              !strconcat(opcStr, " $sx, $sy, $sz")>;
}

// Multiclass for communication register instructions.
//   e.g. SCR
let hasSideEffects = 1 in
multiclass STORECRm<string opcStr, bits<8>opc, RegisterClass RC> {
  def rrr : RR<opc, (outs), (ins RC:$sy, RC:$sz, RC:$sx),
              !strconcat(opcStr, " $sx, $sy, $sz")>;
  let cy = 0 in def irr : RR<opc, (outs), (ins simm7:$sy, RC:$sz, RC:$sx),
                             !strconcat(opcStr, " $sx, $sy, $sz")>;
  let cz = 0 in def rzr : RR<opc, (outs), (ins RC:$sy, zero:$sz, RC:$sx),
                             !strconcat(opcStr, " $sx, $sy, $sz")>;
  let cy = 0, cz = 0 in
  def izr : RR<opc, (outs), (ins simm7:$sy, zero:$sz, RC:$sx),
               !strconcat(opcStr, " $sx, $sy, $sz")>;
}

let hasSideEffects = 1, Constraints = "$sx = $sx_in", DisableEncoding = "$sx_in" in
multiclass TSCRm<string opcStr, bits<8>opc, RegisterClass RC> {
  def rrr : RR<opc, (outs RC:$sx), (ins RC:$sy, RC:$sz, RC:$sx_in),
               !strconcat(opcStr, " $sx, $sy, $sz")>;
  let cy = 0 in def irr : RR<opc, (outs RC:$sx), (ins simm7:$sy, RC:$sz, RC:$sx_in),
                             !strconcat(opcStr, " $sx, $sy, $sz")>;
  let cz = 0 in def rzr : RR<opc, (outs RC:$sx), (ins RC:$sy, zero:$sz, RC:$sx_in),
                             !strconcat(opcStr, " $sx, $sy, $sz")>;
  let cy = 0, cz = 0 in
  def izr : RR<opc, (outs RC:$sx), (ins simm7:$sy, zero:$sz, RC:$sx_in),
               !strconcat(opcStr, " $sx, $sy, $sz")>;
}


// Multiclass for communication register instructions.
//   e.g. FIDCR
let cz = 0, hasSideEffects = 1 in
multiclass FIDCRm<string opcStr, bits<8>opc, RegisterClass RC> {
  def ri : RR<opc, (outs RC:$sx), (ins RC:$sy, uimm3:$sz),
              !strconcat(opcStr, " $sx, $sy, $sz")>;
  let cy = 0 in def ii : RR<opc, (outs RC:$sx), (ins simm7:$sy, uimm3:$sz),
                            !strconcat(opcStr, " $sx, $sy, $sz")>;
}

// Multiclass for LHM instruction.
let mayLoad = 1, hasSideEffects = 0 in
multiclass LHMm<string opcStr, bits<8> opc, RegisterClass RC> {
  def ri : RRMHM<opc, (outs RC:$sx), (ins (MEMriHM $sz, $imm32):$addr),
                 !strconcat(opcStr, " $sx, $addr")>;
  let cz = 0 in
  def zi : RRMHM<opc, (outs RC:$sx), (ins (MEMziHM $sz, $imm32):$addr),
                 !strconcat(opcStr, " $sx, $addr")>;
}

// Multiclass for SHM instruction.
let mayStore = 1, hasSideEffects = 0 in
multiclass SHMm<string opcStr, bits<8> opc, RegisterClass RC> {
  def ri : RRMHM<opc, (outs), (ins (MEMriHM $sz, $imm32):$addr, RC:$sx),
                 !strconcat(opcStr, " $sx, $addr")>;
  let cz = 0 in
  def zi : RRMHM<opc, (outs), (ins (MEMziHM $sz, $imm32):$addr, RC:$sx),
                 !strconcat(opcStr, " $sx, $addr")>;
}

//===----------------------------------------------------------------------===//
// Instructions
//
// Define all scalar instructions defined in SX-Aurora TSUBASA Architecture
// Guide here.  As those mnemonics, we use mnemonics defined in Vector Engine
// Assembly Language Reference Manual.
//===----------------------------------------------------------------------===//

//-----------------------------------------------------------------------------
// Section 8.2 - Load/Store instructions
//-----------------------------------------------------------------------------

// Multiclass for generic RM instructions
multiclass RMm<string opcStr, bits<8>opc, RegisterClass RC, bit MoveImm = 0> {
  def rri : RM<opc, (outs RC:$sx), (ins (MEMrri $sz, $sy, $imm32):$addr),
               !strconcat(opcStr, " $sx, $addr"), []>;
  let cy = 0 in
  def rii : RM<opc, (outs RC:$sx), (ins (MEMrii $sz, $sy, $imm32):$addr),
               !strconcat(opcStr, " $sx, $addr"), []>;
  let cz = 0 in
  def zri : RM<opc, (outs RC:$sx), (ins (MEMzri $sz, $sy, $imm32):$addr),
               !strconcat(opcStr, " $sx, $addr"), []>;
  let cy = 0, cz = 0 in
  def zii : RM<opc, (outs RC:$sx), (ins (MEMzii $sz, $sy, $imm32):$addr),
               !strconcat(opcStr, " $sx, $addr"), []> {
    // VE uses LEAzii and LEASLzii as a move immediate instruction, so declare
    // it here.  An instruction declared as MoveImm will be optimized in
    // FoldImmediate later.
    let isMoveImm = MoveImm;
  }
}

// Section 8.2.1 - LEA
let isReMaterializable = 1, isAsCheapAsAMove = 1,
    DecoderMethod = "DecodeLoadI64" in {
  let cx = 0 in defm LEA : RMm<"lea", 0x06, I64, /* MoveImm */ 1>;
  let cx = 1 in defm LEASL : RMm<"lea.sl", 0x06, I64, /* MoveImm */ 1>;
}

// LEA basic patterns.
//   Need to be defined here to prioritize LEA over ADX.
def : Pat<(iPTR ADDRrri:$addr), (LEArri MEMrri:$addr)>;
def : Pat<(iPTR ADDRrii:$addr), (LEArii MEMrii:$addr)>;
def : Pat<(add I64:$base, simm32:$disp), (LEArii $base, 0, (LO32 $disp))>;
def : Pat<(add I64:$base, lozero:$disp), (LEASLrii $base, 0, (HI32 $disp))>;

// Multiclass for load instructions.
let mayLoad = 1, hasSideEffects = 0 in
multiclass LOADm<string opcStr, bits<8> opc, RegisterClass RC, ValueType Ty,
                 SDPatternOperator OpNode = null_frag> {
  def rri : RM<opc, (outs RC:$sx), (ins (MEMrri $sz, $sy, $imm32):$addr),
               !strconcat(opcStr, " $sx, $addr"),
               [(set Ty:$sx, (OpNode ADDRrri:$addr))]>;
  let cy = 0 in
  def rii : RM<opc, (outs RC:$sx), (ins (MEMrii $sz, $sy, $imm32):$addr),
               !strconcat(opcStr, " $sx, $addr"),
               [(set Ty:$sx, (OpNode ADDRrii:$addr))]>;
  let cz = 0 in
  def zri : RM<opc, (outs RC:$sx), (ins (MEMzri $sz, $sy, $imm32):$addr),
               !strconcat(opcStr, " $sx, $addr"),
               [(set Ty:$sx, (OpNode ADDRzri:$addr))]>;
  let cy = 0, cz = 0 in
  def zii : RM<opc, (outs RC:$sx), (ins (MEMzii $sz, $sy, $imm32):$addr),
               !strconcat(opcStr, " $sx, $addr"),
               [(set Ty:$sx, (OpNode ADDRzii:$addr))]>;
}

// Section 8.2.2 - LDS
let DecoderMethod = "DecodeLoadI64" in
defm LD : LOADm<"ld", 0x01, I64, i64, load>;
def : Pat<(f64 (load ADDRrri:$addr)), (LDrri MEMrri:$addr)>;
def : Pat<(f64 (load ADDRrii:$addr)), (LDrii MEMrii:$addr)>;
def : Pat<(f64 (load ADDRzri:$addr)), (LDzri MEMzri:$addr)>;
def : Pat<(f64 (load ADDRzii:$addr)), (LDzii MEMzii:$addr)>;

// Section 8.2.3 - LDU
let DecoderMethod = "DecodeLoadF32" in
defm LDU : LOADm<"ldu", 0x02, F32, f32, load>;

// Section 8.2.4 - LDL
let DecoderMethod = "DecodeLoadI32" in
defm LDLSX : LOADm<"ldl.sx", 0x03, I32, i32, load>;
let cx = 1, DecoderMethod = "DecodeLoadI32" in
defm LDLZX : LOADm<"ldl.zx", 0x03, I32, i32, load>;

// Section 8.2.5 - LD2B
let DecoderMethod = "DecodeLoadI32" in
defm LD2BSX : LOADm<"ld2b.sx", 0x04, I32, i32, sextloadi16>;
let cx = 1, DecoderMethod = "DecodeLoadI32" in
defm LD2BZX : LOADm<"ld2b.zx", 0x04, I32, i32, zextloadi16>;

// Section 8.2.6 - LD1B
let DecoderMethod = "DecodeLoadI32" in
defm LD1BSX : LOADm<"ld1b.sx", 0x05, I32, i32, sextloadi8>;
let cx = 1, DecoderMethod = "DecodeLoadI32" in
defm LD1BZX : LOADm<"ld1b.zx", 0x05, I32, i32, zextloadi8>;

// LDQ pseudo instructions
let mayLoad = 1, hasSideEffects = 0 in {
  def LDQrii : Pseudo<(outs F128:$dest), (ins MEMrii:$addr),
                      "# pseudo ldq $dest, $addr",
                      [(set f128:$dest, (load ADDRrii:$addr))]>;
}

// Multiclass for store instructions.
let mayStore = 1 in
multiclass STOREm<string opcStr, bits<8> opc, RegisterClass RC, ValueType Ty,
                  SDPatternOperator OpNode = null_frag> {
  def rri : RM<opc, (outs), (ins (MEMrri $sz, $sy, $imm32):$addr, RC:$sx),
               !strconcat(opcStr, " $sx, $addr"),
               [(OpNode Ty:$sx, ADDRrri:$addr)]>;
  let cy = 0 in
  def rii : RM<opc, (outs), (ins (MEMrii $sz, $sy, $imm32):$addr, RC:$sx),
               !strconcat(opcStr, " $sx, $addr"),
               [(OpNode Ty:$sx, ADDRrii:$addr)]>;
  let cz = 0 in
  def zri : RM<opc, (outs), (ins (MEMzri $sz, $sy, $imm32):$addr, RC:$sx),
               !strconcat(opcStr, " $sx, $addr"),
               [(OpNode Ty:$sx, ADDRzri:$addr)]>;
  let cy = 0, cz = 0 in
  def zii : RM<opc, (outs), (ins (MEMzii $sz, $sy, $imm32):$addr, RC:$sx),
               !strconcat(opcStr, " $sx, $addr"),
               [(OpNode Ty:$sx, ADDRzii:$addr)]>;
}

// Section 8.2.7 - STS
let DecoderMethod = "DecodeStoreI64" in
defm ST : STOREm<"st", 0x11, I64, i64, store>;
def : Pat<(store f64:$src, ADDRrri:$addr), (STrri MEMrri:$addr, $src)>;
def : Pat<(store f64:$src, ADDRrii:$addr), (STrii MEMrii:$addr, $src)>;
def : Pat<(store f64:$src, ADDRzri:$addr), (STzri MEMzri:$addr, $src)>;
def : Pat<(store f64:$src, ADDRzii:$addr), (STzii MEMzii:$addr, $src)>;

// Section 8.2.8 - STU
let DecoderMethod = "DecodeStoreF32" in
defm STU : STOREm<"stu", 0x12, F32, f32, store>;

// Section 8.2.9 - STL
let DecoderMethod = "DecodeStoreI32" in
defm STL : STOREm<"stl", 0x13, I32, i32, store>;

// Section 8.2.10 - ST2B
let DecoderMethod = "DecodeStoreI32" in
defm ST2B : STOREm<"st2b", 0x14, I32, i32, truncstorei16>;

// Section 8.2.11 - ST1B
let DecoderMethod = "DecodeStoreI32" in
defm ST1B : STOREm<"st1b", 0x15, I32, i32, truncstorei8>;

// STQ pseudo instructions
let mayStore = 1, hasSideEffects = 0 in {
  def STQrii : Pseudo<(outs), (ins MEMrii:$addr, F128:$sx),
                      "# pseudo stq $sx, $addr",
                      [(store f128:$sx, ADDRrii:$addr)]>;
}

// Section 8.2.12 - DLDS
let DecoderMethod = "DecodeLoadI64" in
defm DLD : LOADm<"dld", 0x09, I64, i64, load>;

// Section 8.2.13 - DLDU
let DecoderMethod = "DecodeLoadF32" in
defm DLDU : LOADm<"dldu", 0x0a, F32, f32, load>;

// Section 8.2.14 - DLDL
let DecoderMethod = "DecodeLoadI32" in
defm DLDLSX : LOADm<"dldl.sx", 0x0b, I32, i32, load>;
let cx = 1, DecoderMethod = "DecodeLoadI32" in
defm DLDLZX : LOADm<"dldl.zx", 0x0b, I32, i32, load>;

// Section 8.2.15 - PFCH
let DecoderMethod = "DecodeASX" in
defm PFCH : PFCHm<"pfch", 0x0c>;

// Section 8.2.16 - TS1AM (Test and Set 1 AM)
let DecoderMethod = "DecodeTS1AMI64" in
defm TS1AML : RRCASm<"ts1am.l", 0x42, I64, i64, uimm7>;
let DecoderMethod = "DecodeTS1AMI32", cx = 1 in
defm TS1AMW : RRCASm<"ts1am.w", 0x42, I32, i32, uimm7>;

// Section 8.2.17 - TS2AM (Test and Set 2 AM)
let DecoderMethod = "DecodeTS1AMI64" in
defm TS2AM : RRCASm<"ts2am", 0x43, I64, i64, uimm7>;

// Section 8.2.18 - TS3AM (Test and Set 3 AM)
let DecoderMethod = "DecodeTS1AMI64" in
defm TS3AM : RRCASm<"ts3am", 0x52, I64, i64, uimm1>;

// Section 8.2.19 - ATMAM (Atomic AM)
let DecoderMethod = "DecodeTS1AMI64" in
defm ATMAM : RRCASm<"atmam", 0x53, I64, i64, uimm0to2>;

// Section 8.2.20 - CAS (Compare and Swap)
let DecoderMethod = "DecodeCASI64" in
defm CASL : RRCASm<"cas.l", 0x62, I64, i64, simm7, atomic_cmp_swap_i64>;
let DecoderMethod = "DecodeCASI32", cx = 1 in
defm CASW : RRCASm<"cas.w", 0x62, I32, i32, simm7, atomic_cmp_swap_i32>;

//-----------------------------------------------------------------------------
// Section 8.3 - Transfer Control Instructions
//-----------------------------------------------------------------------------

// Section 8.3.1 - FENCE (Fence)
let hasSideEffects = 1 in {
  let avo = 1 in def FENCEI : RRFENCE<0x20, (outs), (ins), "fencei">;
  def FENCEM : RRFENCE<0x20, (outs), (ins uimm2:$kind), "fencem $kind"> {
    bits<2> kind;
    let lf = kind{1};
    let sf = kind{0};
  }
  def FENCEC : RRFENCE<0x20, (outs), (ins uimm3:$kind), "fencec $kind"> {
    bits<3> kind;
    let c2 = kind{2};
    let c1 = kind{1};
    let c0 = kind{0};
  }
}

// Section 8.3.2 - SVOB (Set Vector Out-of-order memory access Boundary)
let sx = 0, cy = 0, sy = 0, cz = 0, sz = 0, hasSideEffects = 1 in
def SVOB : RR<0x30, (outs), (ins), "svob">;

//-----------------------------------------------------------------------------
// Section 8.4 - Fixed-point Operation Instructions
//-----------------------------------------------------------------------------

// Section 8.4.1 - ADD (Add)
defm ADDUL : RRm<"addu.l", 0x48, I64, i64>;
let cx = 1 in defm ADDUW : RRm<"addu.w", 0x48, I32, i32>;

// Section 8.4.2 - ADS (Add Single)
defm ADDSWSX : RRm<"adds.w.sx", 0x4A, I32, i32, add>;
let cx = 1 in defm ADDSWZX : RRm<"adds.w.zx", 0x4A, I32, i32>;

// Section 8.4.3 - ADX (Add)
defm ADDSL : RRm<"adds.l", 0x59, I64, i64, add>;

// Section 8.4.4 - SUB (Subtract)
defm SUBUL : RRNCm<"subu.l", 0x58, I64, i64>;
let cx = 1 in defm SUBUW : RRNCm<"subu.w", 0x58, I32, i32>;

// Section 8.4.5 - SBS (Subtract Single)
defm SUBSWSX : RRNCm<"subs.w.sx", 0x5A, I32, i32, sub>;
let cx = 1 in defm SUBSWZX : RRNCm<"subs.w.zx", 0x5A, I32, i32>;

// Section 8.4.6 - SBX (Subtract)
defm SUBSL : RRNCm<"subs.l", 0x5B, I64, i64, sub>;

// Section 8.4.7 - MPY (Multiply)
defm MULUL : RRm<"mulu.l", 0x49, I64, i64>;
let cx = 1 in defm MULUW : RRm<"mulu.w", 0x49, I32, i32>;

// Section 8.4.8 - MPS (Multiply Single)
defm MULSWSX : RRm<"muls.w.sx", 0x4B, I32, i32, mul>;
let cx = 1 in defm MULSWZX : RRm<"muls.w.zx", 0x4B, I32, i32>;

// Section 8.4.9 - MPX (Multiply)
defm MULSL : RRm<"muls.l", 0x6E, I64, i64, mul>;

// Section 8.4.10 - MPD (Multiply)
defm MULSLW : RRbm<"muls.l.w", 0x6B, I64, i64, I32, i32>;

// Section 8.4.11 - DIV (Divide)
defm DIVUL : RRNCm<"divu.l", 0x6F, I64, i64, udiv>;
let cx = 1 in defm DIVUW : RRNCm<"divu.w", 0x6F, I32, i32, udiv>;

// Section 8.4.12 - DVS (Divide Single)
defm DIVSWSX : RRNCm<"divs.w.sx", 0x7B, I32, i32, sdiv>;
let cx = 1 in defm DIVSWZX : RRNCm<"divs.w.zx", 0x7B, I32, i32>;

// Section 8.4.13 - DVX (Divide)
defm DIVSL : RRNCm<"divs.l", 0x7F, I64, i64, sdiv>;

// Section 8.4.14 - CMP (Compare)
defm CMPUL : RRNCm<"cmpu.l", 0x55, I64, i64, cmpu>;
let cx = 1 in defm CMPUW : RRNCm<"cmpu.w", 0x55, I32, i32, cmpu>;

// Section 8.4.15 - CPS (Compare Single)
defm CMPSWSX : RRNCm<"cmps.w.sx", 0x7A, I32, i32>;
let cx = 1 in defm CMPSWZX : RRNCm<"cmps.w.zx", 0x7A, I32, i32, cmpi>;

// Section 8.4.16 - CPX (Compare)
defm CMPSL : RRNCm<"cmps.l", 0x6A, I64, i64, cmpi>;

// Section 8.4.17 - CMS (Compare and Select Maximum/Minimum Single)
// cx: sx/zx, cw: max/min
defm MAXSWSX : RRm<"maxs.w.sx", 0x78, I32, i32, smax>;
let cx = 1 in defm MAXSWZX : RRm<"maxs.w.zx", 0x78, I32, i32>;
let cw = 1 in defm MINSWSX : RRm<"mins.w.sx", 0x78, I32, i32, smin>;
let cx = 1, cw = 1 in defm MINSWZX : RRm<"mins.w.zx", 0x78, I32, i32>;

// Section 8.4.18 - CMX (Compare and Select Maximum/Minimum)
defm MAXSL : RRm<"maxs.l", 0x68, I64, i64, smax>;
let cw = 1 in defm MINSL : RRm<"mins.l", 0x68, I64, i64, smin>;

//-----------------------------------------------------------------------------
// Section 8.5 - Logical Operation Instructions
//-----------------------------------------------------------------------------

// Section 8.5.1 - AND (AND)
defm AND : RRm<"and", 0x44, I64, i64, and>;

// Section 8.5.2 - OR (OR)
let isReMaterializable = 1, isAsCheapAsAMove = 1 in
defm OR : RRm<"or", 0x45, I64, i64, or, simm7, mimm, /* MoveImm */ 1>;

// Section 8.5.3 - XOR (Exclusive OR)
defm XOR : RRm<"xor", 0x46, I64, i64, xor>;

// Section 8.5.4 - EQV (Equivalence)
defm EQV : RRm<"eqv", 0x47, I64, i64>;

// Section 8.5.5 - NND (Negate AND)
def and_not : PatFrags<(ops node:$x, node:$y),
                       [(and (not node:$x), node:$y)]>;
defm NND : RRNCm<"nnd", 0x54, I64, i64, and_not>;

// Section 8.5.6 - MRG (Merge)
defm MRG : RRMRGm<"mrg", 0x56, I64>;

// Section 8.5.7 - LDZ (Leading Zero Count)
def ctlz_pat : PatFrags<(ops node:$src),
                        [(ctlz node:$src),
                         (ctlz_zero_undef node:$src)]>;
defm LDZ : RRI1m<"ldz", 0x67, I64, i64, ctlz_pat>;

// Section 8.5.8 - PCNT (Population Count)
defm PCNT : RRI1m<"pcnt", 0x38, I64, i64, ctpop>;

// Section 8.5.9 - BRV (Bit Reverse)
defm BRV : RRI1m<"brv", 0x39, I64, i64, bitreverse>;

// Section 8.5.10 - BSWP (Byte Swap)
defm BSWP : RRSWPm<"bswp", 0x2B, I64, i64>;

def : Pat<(i64 (bswap i64:$src)),
          (BSWPri $src, 0)>;
def : Pat<(i64 (bswap (i64 mimm:$src))),
          (BSWPmi (MIMM $src), 0)>;
def : Pat<(i32 (bswap i32:$src)),
          (EXTRACT_SUBREG
              (BSWPri (INSERT_SUBREG (i64 (IMPLICIT_DEF)), $src, sub_i32), 1),
              sub_i32)>;
def : Pat<(i32 (bswap (i32 mimm:$src))),
          (EXTRACT_SUBREG (BSWPmi (MIMM $src), 1), sub_i32)>;

// Section 8.5.11 - CMOV (Conditional Move)
let cw = 0, cw2 = 0 in
defm CMOVL : RRCMOVm<"cmov.l.${cfw}", 0x3B, I64, i64, cmov>;
let cw = 1, cw2 = 0 in
defm CMOVW : RRCMOVm<"cmov.w.${cfw}", 0x3B, I32, i32, cmov>;
let cw = 0, cw2 = 1 in
defm CMOVD : RRCMOVm<"cmov.d.${cfw}", 0x3B, I64, f64, cmov, simm7fp>;
let cw = 1, cw2 = 1 in
defm CMOVS : RRCMOVm<"cmov.s.${cfw}", 0x3B, F32, f32, cmov, simm7fp>;
def : MnemonicAlias<"cmov.l", "cmov.l.at">;
def : MnemonicAlias<"cmov.w", "cmov.w.at">;
def : MnemonicAlias<"cmov.d", "cmov.d.at">;
def : MnemonicAlias<"cmov.s", "cmov.s.at">;

//-----------------------------------------------------------------------------
// Section 8.6 - Shift Operation Instructions
//-----------------------------------------------------------------------------

// Section 8.6.1 - SLL (Shift Left Logical)
defm SLL : RRIm<"sll", 0x65, I64, i64, shl>;

// Section 8.6.2 - SLD (Shift Left Double)
defm SLD : RRILDm<"sld", 0x64, I64>;

// Section 8.6.3 - SRL (Shift Right Logical)
defm SRL : RRIm<"srl", 0x75, I64, i64, srl>;

// Section 8.6.4 - SRD (Shift Right Double)
defm SRD : RRIRDm<"srd", 0x74, I64>;

// Section 8.6.5 - SLA (Shift Left Arithmetic)
defm SLAWSX : RRIm<"sla.w.sx", 0x66, I32, i32, shl>;
let cx = 1 in defm SLAWZX : RRIm<"sla.w.zx", 0x66, I32, i32>;

// Section 8.6.6 - SLAX (Shift Left Arithmetic)
defm SLAL : RRIm<"sla.l", 0x57, I64, i64>;

// Section 8.6.7 - SRA (Shift Right Arithmetic)
defm SRAWSX : RRIm<"sra.w.sx", 0x76, I32, i32, sra>;
let cx = 1 in defm SRAWZX : RRIm<"sra.w.zx", 0x76, I32, i32>;

// Section 8.6.8 - SRAX (Shift Right Arithmetic)
defm SRAL : RRIm<"sra.l", 0x77, I64, i64, sra>;

def : Pat<(i32 (srl i32:$src, (i32 simm7:$val))),
          (EXTRACT_SUBREG (SRLri (ANDrm (INSERT_SUBREG (i64 (IMPLICIT_DEF)),
            $src, sub_i32), !add(32, 64)), imm:$val), sub_i32)>;
def : Pat<(i32 (srl i32:$src, i32:$val)),
          (EXTRACT_SUBREG (SRLrr (ANDrm (INSERT_SUBREG (i64 (IMPLICIT_DEF)),
            $src, sub_i32), !add(32, 64)), $val), sub_i32)>;

//-----------------------------------------------------------------------------
// Section 8.7 - Floating-point Arithmetic Instructions
//-----------------------------------------------------------------------------

// Section 8.7.1 - FAD (Floating Add)
defm FADDD : RRFm<"fadd.d", 0x4C, I64, f64, fadd>;
let cx = 1 in
defm FADDS : RRFm<"fadd.s", 0x4C, F32, f32, fadd, simm7fp, mimmfp32>;

// Section 8.7.2 - FSB (Floating Subtract)
defm FSUBD : RRFm<"fsub.d", 0x5C, I64, f64, fsub>;
let cx = 1 in
defm FSUBS : RRFm<"fsub.s", 0x5C, F32, f32, fsub, simm7fp, mimmfp32>;

// Section 8.7.3 - FMP (Floating Multiply)
defm FMULD : RRFm<"fmul.d", 0x4D, I64, f64, fmul>;
let cx = 1 in
defm FMULS : RRFm<"fmul.s", 0x4D, F32, f32, fmul, simm7fp, mimmfp32>;

// Section 8.7.4 - FDV (Floating Divide)
defm FDIVD : RRFm<"fdiv.d", 0x5D, I64, f64, fdiv>;
let cx = 1 in
defm FDIVS : RRFm<"fdiv.s", 0x5D, F32, f32, fdiv, simm7fp, mimmfp32>;

// Section 8.7.5 - FCP (Floating Compare)
defm FCMPD : RRFm<"fcmp.d", 0x7E, I64, f64, cmpf>;
let cx = 1 in
defm FCMPS : RRFm<"fcmp.s", 0x7E, F32, f32, cmpf, simm7fp, mimmfp32>;

// Section 8.7.6 - CMS (Compare and Select Maximum/Minimum Single)
// cx: double/float, cw: max/min
let cw = 0, cx = 0 in
defm FMAXD : RRFm<"fmax.d", 0x3E, I64, f64, fmaxnum>;
let cw = 0, cx = 1 in
defm FMAXS : RRFm<"fmax.s", 0x3E, F32, f32, fmaxnum, simm7fp, mimmfp32>;
let cw = 1, cx = 0 in
defm FMIND : RRFm<"fmin.d", 0x3E, I64, f64, fminnum>;
let cw = 1, cx = 1 in
defm FMINS : RRFm<"fmin.s", 0x3E, F32, f32, fminnum, simm7fp, mimmfp32>;

// Section 8.7.7 - FAQ (Floating Add Quadruple)
defm FADDQ : RRFm<"fadd.q", 0x6C, F128, f128, fadd>;

// Section 8.7.8 - FSQ (Floating Subtract Quadruple)
defm FSUBQ : RRFm<"fsub.q", 0x7C, F128, f128, fsub>;

// Section 8.7.9 - FMQ (Floating Subtract Quadruple)
defm FMULQ : RRFm<"fmul.q", 0x6D, F128, f128, fmul>;

// Section 8.7.10 - FCQ (Floating Compare Quadruple)
defm FCMPQ : RRNCbm<"fcmp.q", 0x7D, I64, f64, F128, f128, cmpq, simm7fp,
                    mimmfp>;

// Section 8.7.11 - FIX (Convert to Fixed Point)
// cx: double/float, cw: sx/zx, sz{0-3} = round
let cx = 0, cw = 0 /* sign extend */ in
defm CVTWDSX : CVTRDm<"cvt.w.d.sx", 0x4E, I32, I64>;
let cx = 0, cw = 1 /* zero extend */ in
defm CVTWDZX : CVTRDm<"cvt.w.d.zx", 0x4E, I32, I64>;
let cx = 1, cw = 0 /* sign extend */ in
defm CVTWSSX : CVTRDm<"cvt.w.s.sx", 0x4E, I32, F32>;
let cx = 1, cw = 1 /* zero extend */ in
defm CVTWSZX : CVTRDm<"cvt.w.s.zx", 0x4E, I32, F32>;

// Section 8.7.12 - FIXX (Convert to Fixed Point)
defm CVTLD : CVTRDm<"cvt.l.d", 0x4F, I64, I64>;

// Section 8.7.13 - FLT (Convert to Floating Point)
defm CVTDW : CVTm<"cvt.d.w", 0x5E, I64, f64, I32, i32, sint_to_fp>;
let cx = 1 in
defm CVTSW : CVTm<"cvt.s.w", 0x5E, F32, f32, I32, i32, sint_to_fp>;

// Section 8.7.14 - FLTX (Convert to Floating Point)
defm CVTDL : CVTm<"cvt.d.l", 0x5F, I64, f64, I64, i64, sint_to_fp>;

// Section 8.7.15 - CVS (Convert to Single-format)
defm CVTSD : CVTm<"cvt.s.d", 0x1F, F32, f32, I64, f64, fpround>;
let cx = 1 in
defm CVTSQ : CVTm<"cvt.s.q", 0x1F, F32, f32, F128, f128, fpround>;

// Section 8.7.16 - CVD (Convert to Double-format)
defm CVTDS : CVTm<"cvt.d.s", 0x0F, I64, f64, F32, f32, fpextend>;
let cx = 1 in
defm CVTDQ : CVTm<"cvt.d.q", 0x0F, I64, f64, F128, f128, fpround>;

// Section 8.7.17 - CVQ (Convert to Single-format)
defm CVTQD : CVTm<"cvt.q.d", 0x2D, F128, f128, I64, f64, fpextend>;
let cx = 1 in
defm CVTQS : CVTm<"cvt.q.s", 0x2D, F128, f128, F32, f32, fpextend>;

//-----------------------------------------------------------------------------
// Section 8.8 - Branch instructions
//-----------------------------------------------------------------------------

// Section 8.8.1 - BC (Branch on Codition)
defm BCFL : BCm<"b${cond}.l", "b.l", "baf.l", 0x19, I64, simm7>;

// Indirect branch aliases
def : Pat<(brind I64:$reg), (BCFLari_t $reg, 0)>;
def : Pat<(brind tblockaddress:$imm), (BCFLazi_t 0, $imm)>;

// Return instruction is a special case of jump.
let Uses = [SX10], bpf = 3 /* TAKEN */, cond = 15 /* AT */, cy = 0, sy = 0,
    sz = 10 /* SX10 */, imm32 = 0, isReturn = 1, isTerminator = 1,
    isBarrier = 1, isCodeGenOnly = 1, hasSideEffects = 0 in
def RET : CF<0x19, (outs), (ins), "b.l.t (, %s10)", [(retglue)]>;

// Section 8.8.2 - BCS (Branch on Condition Single)
defm BCFW : BCm<"b${cond}.w", "b.w", "baf.w", 0x1B, I32, simm7>;

// Section 8.8.3 - BCF (Branch on Condition Floating Point)
defm BCFD : BCm<"b${cond}.d", "b.d", "baf.d", 0x1C, I64, simm7fp>;
let cx = 1 in
defm BCFS : BCm<"b${cond}.s", "b.s", "baf.s", 0x1C, F32, simm7fp>;

// Section 8.8.4 - BCR (Branch on Condition Relative)
let cx = 0, cx2 = 0 in
defm BRCFL : BCRm<"br${cond}.l", "br.l", "braf.l", 0x18, I64, simm7, zero>;
let cx = 1, cx2 = 0 in
defm BRCFW : BCRm<"br${cond}.w", "br.w", "braf.w", 0x18, I32, simm7, zero>;
let cx = 0, cx2 = 1 in
defm BRCFD : BCRm<"br${cond}.d", "br.d", "braf.d", 0x18, I64, simm7fp, zerofp>;
let cx = 1, cx2 = 1 in
defm BRCFS : BCRm<"br${cond}.s", "br.s", "braf.s", 0x18, F32, simm7fp, zerofp>;

// Section 8.8.5 - BSIC (Branch and Save IC)
let isCall = 1, hasSideEffects = 0, DecoderMethod = "DecodeCall" in
defm BSIC : RMm<"bsic", 0x08, I64>;

// Call instruction is a special case of BSIC.
let Defs = [SX10], sx = 10 /* SX10 */, cy = 0, sy = 0, imm32 = 0,
    isCall = 1, isCodeGenOnly = 1, hasSideEffects = 0 in
def CALLr : RM<0x08, (outs), (ins I64:$sz, variable_ops),
               "bsic %s10, (, $sz)", [(call i64:$sz)]>;

//-----------------------------------------------------------------------------
// Section 8.19 - Control Instructions
//-----------------------------------------------------------------------------

// Section 8.19.1 - SIC (Save Instruction Counter)
let cy = 0, sy = 0, cz = 0, sz = 0, hasSideEffects = 1, Uses = [IC] in
def SIC : RR<0x28, (outs I32:$sx), (ins), "sic $sx">;

// Section 8.19.2 - LPM (Load Program Mode Flags)
let sx = 0, cz = 0, sz = 0, hasSideEffects = 1, Defs = [PSW] in
def LPM : RR<0x3a, (outs), (ins I64:$sy), "lpm $sy">;

// Section 8.19.3 - SPM (Save Program Mode Flags)
let cy = 0, sy = 0, cz = 0, sz = 0, hasSideEffects = 1, Uses = [PSW] in
def SPM : RR<0x2a, (outs I64:$sx), (ins), "spm $sx">;

// Section 8.19.4 - LFR (Load Flag Register)
let sx = 0, cz = 0, sz = 0, hasSideEffects = 1, Defs = [PSW] in {
  def LFRr : RR<0x69, (outs), (ins I64:$sy), "lfr $sy">;
  let cy = 0 in def LFRi : RR<0x69, (outs), (ins uimm6:$sy), "lfr $sy">;
}

// Section 8.19.5 - SFR (Save Flag Register)
let cy = 0, sy = 0, cz = 0, sz = 0, hasSideEffects = 1, Uses = [PSW] in
def SFR : RR<0x29, (outs I64:$sx), (ins), "sfr $sx">;

// Section 8.19.6 - SMIR (Save Miscellaneous Register)
let cy = 0, cz = 0, sz = 0, hasSideEffects = 1 in {
  def SMIR : RR<0x22, (outs I64:$sx), (ins MISC:$sy), "smir $sx, $sy">;
}

// Section 8.19.7 - NOP (No Operation)
let sx = 0, cy = 0, sy = 0, cz = 0, sz = 0, hasSideEffects = 0 in
def NOP : RR<0x79, (outs), (ins), "nop">;

// Section 8.19.8 - MONC (Monitor Call)
let sx = 0, cy = 0, sy = 0, cz = 0, sz = 0, hasSideEffects = 1 in {
  def MONC : RR<0x3F, (outs), (ins), "monc">;
  let cx = 1, isTrap = 1 in def MONCHDB : RR<0x3F, (outs), (ins), "monc.hdb">;
}

// Section 8.19.9 - LCR (Load Communication Register)
defm LCR : LOADCRm<"lcr", 0x40, I64>;

// Section 8.19.10 - SCR (Save Communication Register)
defm SCR : STORECRm<"scr", 0x50, I64>;

// Section 8.19.11 - TSCR (Test & Set Communication Register)
defm TSCR : TSCRm<"tscr", 0x41, I64>;

// Section 8.19.12 - FIDCR (Fetch & Increment/Decrement CR)
defm FIDCR : FIDCRm<"fidcr", 0x51, I64>;

//-----------------------------------------------------------------------------
// Section 8.20 - Host Memory Access Instructions
//-----------------------------------------------------------------------------

// Section 8.20.1 - LHM (Load Host Memory)
let ry = 3, DecoderMethod = "DecodeLoadASI64" in
defm LHML : LHMm<"lhm.l", 0x21, I64>;
let ry = 2, DecoderMethod = "DecodeLoadASI64" in
defm LHMW : LHMm<"lhm.w", 0x21, I64>;
let ry = 1, DecoderMethod = "DecodeLoadASI64" in
defm LHMH : LHMm<"lhm.h", 0x21, I64>;
let ry = 0, DecoderMethod = "DecodeLoadASI64" in
defm LHMB : LHMm<"lhm.b", 0x21, I64>;

// Section 8.20.2 - SHM (Store Host Memory)
let ry = 3, DecoderMethod = "DecodeStoreASI64" in
defm SHML : SHMm<"shm.l", 0x31, I64>;
let ry = 2, DecoderMethod = "DecodeStoreASI64" in
defm SHMW : SHMm<"shm.w", 0x31, I64>;
let ry = 1, DecoderMethod = "DecodeStoreASI64" in
defm SHMH : SHMm<"shm.h", 0x31, I64>;
let ry = 0, DecoderMethod = "DecodeStoreASI64" in
defm SHMB : SHMm<"shm.b", 0x31, I64>;

//===----------------------------------------------------------------------===//
// Instructions for CodeGenOnly
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Pattern Matchings
//===----------------------------------------------------------------------===//

// Basic cast between registers.  This is often used in ISel patterns, so make
// them as OutPatFrag.
def i2l : OutPatFrag<(ops node:$exp),
                     (INSERT_SUBREG (i64 (IMPLICIT_DEF)), $exp, sub_i32)>;
def l2i : OutPatFrag<(ops node:$exp),
                     (EXTRACT_SUBREG $exp, sub_i32)>;
def f2l : OutPatFrag<(ops node:$exp),
                     (INSERT_SUBREG (i64 (IMPLICIT_DEF)), $exp, sub_f32)>;
def l2f : OutPatFrag<(ops node:$exp),
                     (EXTRACT_SUBREG $exp, sub_f32)>;

// Zero out subregisters.
def zero_i32 : OutPatFrag<(ops node:$expr),
                          (ANDrm $expr, 32)>;
def zero_f32 : OutPatFrag<(ops node:$expr),
                          (ANDrm $expr, !add(32, 64))>;

// Small immediates.
def : Pat<(i32 simm7:$val), (l2i (ORim (LO7 $val), 0))>;
def : Pat<(i64 simm7:$val), (ORim (LO7 $val), 0)>;
// Medium immediates.
def : Pat<(i32 simm32:$val), (l2i (LEAzii 0, 0, (LO32 $val)))>;
def : Pat<(i64 simm32:$val), (LEAzii 0, 0, (LO32 $val))>;
def : Pat<(i64 uimm32:$val), (zero_f32 (LEAzii 0, 0, (LO32 $val)))>;
// Arbitrary immediates.
def : Pat<(i64 lozero:$val),
          (LEASLzii 0, 0, (HI32 imm:$val))>;
def : Pat<(i64 lomsbzero:$val),
          (LEASLrii (LEAzii 0, 0, (LO32 imm:$val)), 0, (HI32 imm:$val))>;
def : Pat<(i64 imm:$val),
          (LEASLrii (ANDrm (LEAzii 0, 0, (LO32 imm:$val)), !add(32, 64)), 0,
                    (HI32 imm:$val))>;

// LEA patterns
def lea_add : PatFrags<(ops node:$base, node:$idx, node:$disp),
                       [(add (add node:$base, node:$idx), node:$disp),
                        (add (add node:$base, node:$disp), node:$idx),
                        (add node:$base, (add $idx, $disp))]>;
def : Pat<(lea_add I64:$base, simm7:$idx, simm32:$disp),
          (LEArii $base, (LO7 $idx), (LO32 $disp))>;
def : Pat<(lea_add I64:$base, I64:$idx, simm32:$disp),
          (LEArri $base, $idx, (LO32 $disp))>;
def : Pat<(lea_add I64:$base, simm7:$idx, lozero:$disp),
          (LEASLrii $base, (LO7 $idx), (HI32 $disp))>;
def : Pat<(lea_add I64:$base, I64:$idx, lozero:$disp),
          (LEASLrri $base, $idx, (HI32 $disp))>;

// Address calculation patterns and optimizations
//
// Generate following instructions:
//   1. LEA %reg, label@LO32
//      AND %reg, %reg, (32)0
//   2. LEASL %reg, label@HI32
//   3. (LEA %reg, label@LO32)
//      (AND %reg, %reg, (32)0)
//      LEASL %reg, label@HI32(, %reg)
//   4. (LEA %reg, label@LO32)
//      (AND %reg, %reg, (32)0)
//      LEASL %reg, label@HI32(%reg, %got)
//
def velo_only : OutPatFrag<(ops node:$lo),
                           (ANDrm (LEAzii 0, 0, $lo), !add(32, 64))>;
def vehi_only : OutPatFrag<(ops node:$hi),
                           (LEASLzii 0, 0, $hi)>;
def vehi_lo : OutPatFrag<(ops node:$hi, node:$lo),
                         (LEASLrii $lo, 0, $hi)>;
def vehi_lo_imm : OutPatFrag<(ops node:$hi, node:$lo, node:$idx),
                             (LEASLrii $lo, $idx, $hi)>;
def vehi_baselo : OutPatFrag<(ops node:$base, node:$hi, node:$lo),
                             (LEASLrri $base, $lo, $hi)>;
foreach type = [ "tblockaddress", "tconstpool", "texternalsym", "tglobaladdr",
                 "tglobaltlsaddr", "tjumptable" ] in {
  def : Pat<(VElo !cast<SDNode>(type):$lo), (velo_only $lo)>;
  def : Pat<(VEhi !cast<SDNode>(type):$hi), (vehi_only $hi)>;
  def : Pat<(add (VEhi !cast<SDNode>(type):$hi), I64:$lo), (vehi_lo $hi, $lo)>;
  def : Pat<(add (add (VEhi !cast<SDNode>(type):$hi), I64:$lo), simm7:$val),
            (vehi_lo_imm $hi, $lo, (LO7 $val))>;
  def : Pat<(add I64:$base, (add (VEhi !cast<SDNode>(type):$hi), I64:$lo)),
            (vehi_baselo $base, $hi, $lo)>;
}

// floating point
def : Pat<(f32 fpimm:$val),
          (EXTRACT_SUBREG (LEASLzii 0, 0, (HIFP32 $val)), sub_f32)>;
def : Pat<(f64 fplozero:$val),
          (LEASLzii 0, 0, (HIFP32 $val))>;
def : Pat<(f64 fplomsbzero:$val),
          (LEASLrii (LEAzii 0, 0, (LOFP32 $val)), 0, (HIFP32 $val))>;
def : Pat<(f64 fpimm:$val),
          (LEASLrii (ANDrm (LEAzii 0, 0, (LOFP32 $val)), !add(32, 64)), 0,
                    (HIFP32 $val))>;

// The same integer registers are used for i32 and i64 values.
// When registers hold i32 values, the high bits are unused.

// TODO Use standard expansion for shift-based lowering of sext_inreg

// Cast to i1
def : Pat<(sext_inreg I32:$src, i1),
          (SRAWSXri (SLAWSXri $src, 31), 31)>;
def : Pat<(sext_inreg I64:$src, i1),
          (SRALri (SLLri $src, 63), 63)>;

// Cast to i8
def : Pat<(sext_inreg I32:$src, i8),
          (SRAWSXri (SLAWSXri $src, 24), 24)>;
def : Pat<(sext_inreg I64:$src, i8),
          (SRALri (SLLri $src, 56), 56)>;
def : Pat<(sext_inreg (i32 (trunc i64:$src)), i8),
          (EXTRACT_SUBREG (SRALri (SLLri $src, 56), 56), sub_i32)>;
def : Pat<(i32 (and (trunc i64:$src), 0xff)),
          (EXTRACT_SUBREG (ANDrm $src, !add(56, 64)), sub_i32)>;

// Cast to i16
def : Pat<(sext_inreg I32:$src, i16),
          (SRAWSXri (SLAWSXri $src, 16), 16)>;
def : Pat<(sext_inreg I64:$src, i16),
          (SRALri (SLLri $src, 48), 48)>;
def : Pat<(sext_inreg (i32 (trunc i64:$src)), i16),
          (EXTRACT_SUBREG (SRALri (SLLri $src, 48), 48), sub_i32)>;
def : Pat<(i32 (and (trunc i64:$src), 0xffff)),
          (EXTRACT_SUBREG (ANDrm $src, !add(48, 64)), sub_i32)>;

// Cast to i32
def : Pat<(i32 (trunc i64:$src)), (l2i (zero_f32 $src))>;
def : Pat<(i32 (fp_to_sint f32:$src)), (CVTWSSXr RD_RZ, $src)>;
def : Pat<(i32 (fp_to_sint f64:$src)), (CVTWDSXr RD_RZ, $src)>;
def : Pat<(i32 (fp_to_sint f128:$src)), (CVTWDSXr RD_RZ, (CVTDQr $src))>;

// Cast to i64
def : Pat<(sext_inreg i64:$src, i32), (i2l (ADDSWSXrm (l2i $src), 0))>;
def : Pat<(i64 (sext i32:$src)), (i2l (ADDSWSXrm $src, 0))>;
def : Pat<(i64 (zext i32:$src)), (i2l (ADDSWZXrm $src, 0))>;
def : Pat<(i64 (anyext i32:$sy)), (i2l $sy)>;
def : Pat<(i64 (fp_to_sint f32:$src)), (CVTLDr RD_RZ, (CVTDSr $src))>;
def : Pat<(i64 (fp_to_sint f64:$src)), (CVTLDr RD_RZ, $src)>;
def : Pat<(i64 (fp_to_sint f128:$src)), (CVTLDr RD_RZ, (CVTDQr $src))>;

// Cast to f32
def : Pat<(f32 (sint_to_fp i64:$src)), (CVTSDr (CVTDLr i64:$src))>;

// Cast to f128
def : Pat<(f128 (sint_to_fp i32:$src)), (CVTQDr (CVTDWr $src))>;
def : Pat<(f128 (sint_to_fp i64:$src)), (CVTQDr (CVTDLr $src))>;


// extload, sextload and zextload stuff
multiclass EXT64m<SDPatternOperator from,
                  RM torri,
                  RM torii,
                  RM tozri,
                  RM tozii> {
  def : Pat<(i64 (from ADDRrri:$addr)),
            (i2l (torri MEMrri:$addr))>;
  def : Pat<(i64 (from ADDRrii:$addr)),
            (i2l (torii MEMrii:$addr))>;
  def : Pat<(i64 (from ADDRzri:$addr)),
            (i2l (tozri MEMzri:$addr))>;
  def : Pat<(i64 (from ADDRzii:$addr)),
            (i2l (tozii MEMzii:$addr))>;
}
defm : EXT64m<sextloadi8, LD1BSXrri, LD1BSXrii, LD1BSXzri, LD1BSXzii>;
defm : EXT64m<zextloadi8, LD1BZXrri, LD1BZXrii, LD1BZXzri, LD1BZXzii>;
defm : EXT64m<extloadi8, LD1BZXrri, LD1BZXrii, LD1BZXzri, LD1BZXzii>;
defm : EXT64m<sextloadi16, LD2BSXrri, LD2BSXrii, LD2BSXzri, LD2BSXzii>;
defm : EXT64m<zextloadi16, LD2BZXrri, LD2BZXrii, LD2BZXzri, LD2BZXzii>;
defm : EXT64m<extloadi16, LD2BZXrri, LD2BZXrii, LD2BZXzri, LD2BZXzii>;
defm : EXT64m<sextloadi32, LDLSXrri, LDLSXrii, LDLSXzri, LDLSXzii>;
defm : EXT64m<zextloadi32, LDLZXrri, LDLZXrii, LDLZXzri, LDLZXzii>;
defm : EXT64m<extloadi32, LDLSXrri, LDLSXrii, LDLSXzri, LDLSXzii>;

// anyextload
multiclass EXT32m<SDPatternOperator from,
                  RM torri,
                  RM torii,
                  RM tozri,
                  RM tozii> {
  def : Pat<(from ADDRrri:$addr), (torri MEMrri:$addr)>;
  def : Pat<(from ADDRrii:$addr), (torii MEMrii:$addr)>;
  def : Pat<(from ADDRzri:$addr), (tozri MEMzri:$addr)>;
  def : Pat<(from ADDRzii:$addr), (tozii MEMzii:$addr)>;
}
defm : EXT32m<extloadi8, LD1BZXrri, LD1BZXrii, LD1BZXzri, LD1BZXzii>;
defm : EXT32m<extloadi16, LD2BZXrri, LD2BZXrii, LD2BZXzri, LD2BZXzii>;

// truncstore
multiclass TRUNC64m<SDPatternOperator from,
                    RM torri,
                    RM torii,
                    RM tozri,
                    RM tozii> {
  def : Pat<(from i64:$src, ADDRrri:$addr),
            (torri MEMrri:$addr, (l2i $src))>;
  def : Pat<(from i64:$src, ADDRrii:$addr),
            (torii MEMrii:$addr, (l2i $src))>;
  def : Pat<(from i64:$src, ADDRzri:$addr),
            (tozri MEMzri:$addr, (l2i $src))>;
  def : Pat<(from i64:$src, ADDRzii:$addr),
            (tozii MEMzii:$addr, (l2i $src))>;
}
defm : TRUNC64m<truncstorei8, ST1Brri, ST1Brii, ST1Bzri, ST1Bzii>;
defm : TRUNC64m<truncstorei16, ST2Brri, ST2Brii, ST2Bzri, ST2Bzii>;
defm : TRUNC64m<truncstorei32, STLrri, STLrii, STLzri, ST1Bzii>;

// Atomic loads (FIXME: replace iAny with the correct integer VT:)
multiclass ATMLDm<SDPatternOperator from,
                  RM torri, RM torii,
                  RM tozri, RM tozii> {
  def : Pat<(iAny (from ADDRrri:$addr)), (torri MEMrri:$addr)>;
  def : Pat<(iAny (from ADDRrii:$addr)), (torii MEMrii:$addr)>;
  def : Pat<(iAny (from ADDRzri:$addr)), (tozri MEMzri:$addr)>;
  def : Pat<(iAny (from ADDRzii:$addr)), (tozii MEMzii:$addr)>;
}
defm : ATMLDm<atomic_load_8, LD1BZXrri, LD1BZXrii, LD1BZXzri, LD1BZXzii>;
defm : ATMLDm<atomic_load_16, LD2BZXrri, LD2BZXrii, LD2BZXzri, LD2BZXzii>;
defm : ATMLDm<atomic_load_32, LDLZXrri, LDLZXrii, LDLZXzri, LDLZXzii>;
defm : ATMLDm<atomic_load_64, LDrri, LDrii, LDzri, LDzii>;

// Optimized atomic loads with sext
multiclass SXATMLDm<SDPatternOperator from, ValueType TY,
                    RM torri, RM torii,
                    RM tozri, RM tozii> {
  def : Pat<(i64 (sext_inreg (i64 (anyext (from ADDRrri:$addr))), TY)),
            (i2l (torri MEMrri:$addr))>;
  def : Pat<(i64 (sext_inreg (i64 (anyext (from ADDRrii:$addr))), TY)),
            (i2l (torii MEMrii:$addr))>;
  def : Pat<(i64 (sext_inreg (i64 (anyext (from ADDRzri:$addr))), TY)),
            (i2l (tozri MEMzri:$addr))>;
  def : Pat<(i64 (sext_inreg (i64 (anyext (from ADDRzii:$addr))), TY)),
            (i2l (tozii MEMzii:$addr))>;
}
multiclass SXATMLD32m<SDPatternOperator from,
                      RM torri, RM torii,
                      RM tozri, RM tozii> {
  def : Pat<(i64 (sext (from ADDRrri:$addr))),
            (i2l (torri MEMrri:$addr))>;
  def : Pat<(i64 (sext (from ADDRrii:$addr))),
            (i2l (torii MEMrii:$addr))>;
  def : Pat<(i64 (sext (from ADDRzri:$addr))),
            (i2l (tozri MEMzri:$addr))>;
  def : Pat<(i64 (sext (from ADDRzii:$addr))),
            (i2l (tozii MEMzii:$addr))>;
}
defm : SXATMLDm<atomic_load_8, i8, LD1BSXrri, LD1BSXrii, LD1BSXzri, LD1BSXzii>;
defm : SXATMLDm<atomic_load_16, i16, LD2BSXrri, LD2BSXrii, LD2BSXzri,
                LD2BSXzii>;
defm : SXATMLD32m<atomic_load_32, LDLSXrri, LDLSXrii, LDLSXzri, LDLSXzii>;

// Optimized atomic loads with zext
multiclass ZXATMLDm<SDPatternOperator from, int VAL,
                    RM torri, RM torii,
                    RM tozri, RM tozii> {
  def : Pat<(i64 (and (anyext (from ADDRrri:$addr)), VAL)),
            (i2l (torri MEMrri:$addr))>;
  def : Pat<(i64 (and (anyext (from ADDRrii:$addr)), VAL)),
            (i2l (torii MEMrii:$addr))>;
  def : Pat<(i64 (and (anyext (from ADDRzri:$addr)), VAL)),
            (i2l (tozri MEMzri:$addr))>;
  def : Pat<(i64 (and (anyext (from ADDRzii:$addr)), VAL)),
            (i2l (tozii MEMzii:$addr))>;
}
multiclass ZXATMLD32m<SDPatternOperator from,
                      RM torri, RM torii,
                      RM tozri, RM tozii> {
  def : Pat<(i64 (zext (from ADDRrri:$addr))),
            (i2l (torri MEMrri:$addr))>;
  def : Pat<(i64 (zext (from ADDRrii:$addr))),
            (i2l (torii MEMrii:$addr))>;
  def : Pat<(i64 (zext (from ADDRzri:$addr))),
            (i2l (tozri MEMzri:$addr))>;
  def : Pat<(i64 (zext (from ADDRzii:$addr))),
            (i2l (tozii MEMzii:$addr))>;
}
defm : ZXATMLDm<atomic_load_8, 0xFF, LD1BZXrri, LD1BZXrii, LD1BZXzri,
                LD1BZXzii>;
defm : ZXATMLDm<atomic_load_16, 0xFFFF, LD2BZXrri, LD2BZXrii, LD2BZXzri,
                LD2BZXzii>;
defm : ZXATMLD32m<atomic_load_32, LDLZXrri, LDLZXrii, LDLZXzri, LDLZXzii>;

// Atomic stores
multiclass ATMSTm<SDPatternOperator from, ValueType ty,
                  RM torri, RM torii,
                  RM tozri, RM tozii> {
  def : Pat<(from ty:$src, ADDRrri:$addr), (torri MEMrri:$addr, $src)>;
  def : Pat<(from ty:$src, ADDRrii:$addr), (torii MEMrii:$addr, $src)>;
  def : Pat<(from ty:$src, ADDRzri:$addr), (tozri MEMzri:$addr, $src)>;
  def : Pat<(from ty:$src, ADDRzii:$addr), (tozii MEMzii:$addr, $src)>;
}
defm : ATMSTm<atomic_store_8, i32, ST1Brri, ST1Brii, ST1Bzri, ST1Bzii>;
defm : ATMSTm<atomic_store_16, i32, ST2Brri, ST2Brii, ST2Bzri, ST2Bzii>;
defm : ATMSTm<atomic_store_32, i32, STLrri, STLrii, STLzri, STLzii>;
defm : ATMSTm<atomic_store_64, i64, STrri, STrii, STzri, STzii>;

// Optimized atomic stores with truncate
multiclass TRATMSTm<SDPatternOperator from,
                  RM torri,
                  RM torii,
                  RM tozri,
                  RM tozii> {
  def : Pat<(from (i32 (trunc i64:$src)), ADDRrri:$addr),
            (torri MEMrri:$addr, (EXTRACT_SUBREG $src, sub_i32))>;
  def : Pat<(from (i32 (trunc i64:$src)), ADDRrii:$addr),
            (torii MEMrii:$addr, (EXTRACT_SUBREG $src, sub_i32))>;
  def : Pat<(from (i32 (trunc i64:$src)), ADDRzri:$addr),
            (tozri MEMzri:$addr, (EXTRACT_SUBREG $src, sub_i32))>;
  def : Pat<(from (i32 (trunc i64:$src)), ADDRzii:$addr),
            (tozii MEMzii:$addr, (EXTRACT_SUBREG $src, sub_i32))>;
}
defm : TRATMSTm<atomic_store_8, ST1Brri, ST1Brii, ST1Bzri, ST1Bzii>;
defm : TRATMSTm<atomic_store_16, ST2Brri, ST2Brii, ST2Bzri, ST2Bzii>;
defm : TRATMSTm<atomic_store_32, STLrri, STLrii, STLzri, STLzii>;

// Atomic swaps
def : Pat<(i32 (ts1am i64:$src, i32:$flag, i32:$new)),
          (TS1AMWrir $src, 0, $flag, $new)>;
def : Pat<(i32 (atomic_swap_i32 ADDRri:$src, i32:$new)),
          (TS1AMWrii MEMriRRM:$src, 15, $new)>;
def : Pat<(i64 (atomic_swap_i64 ADDRri:$src, i64:$new)),
          (TS1AMLrir MEMriRRM:$src, (LEAzii 0, 0, 255), i64:$new)>;

//===----------------------------------------------------------------------===//
// SJLJ Exception handling patterns
//===----------------------------------------------------------------------===//

let hasSideEffects = 1, isBarrier = 1, isCodeGenOnly = 1,
    usesCustomInserter = 1 in {
  let isTerminator = 1 in
  def EH_SjLj_LongJmp : Pseudo<(outs), (ins I64:$buf),
                               "# EH_SJLJ_LONGJMP",
                               [(VEeh_sjlj_longjmp I64:$buf)]>;

  def EH_SjLj_SetJmp  : Pseudo<(outs I32:$dst), (ins I64:$buf),
                               "# EH_SJLJ_SETJMP",
                               [(set I32:$dst, (VEeh_sjlj_setjmp I64:$buf))]>;

  def EH_SjLj_Setup_Dispatch : Pseudo<(outs), (ins), "# EH_SJLJ_SETUP_DISPATCH",
                                      [(VEeh_sjlj_setup_dispatch)]>;
}

let isTerminator = 1, isBranch = 1, isCodeGenOnly = 1 in
  def EH_SjLj_Setup : Pseudo<(outs), (ins brtarget32:$dst),
                             "# EH_SJlJ_SETUP $dst">;

//===----------------------------------------------------------------------===//
// Branch related patterns
//===----------------------------------------------------------------------===//

// Branches
def : Pat<(br bb:$addr), (BRCFLa bb:$addr)>;

// brcc
// integer brcc
multiclass BRCCIm<ValueType ty, CF BrOpNode1,
                 CF BrOpNode2,
                 RR CmpOpNode1,
                 RR CmpOpNode2> {
  def : Pat<(brcc CCSIOp:$cond, ty:$l, simm7:$r, bb:$addr),
            (BrOpNode2 (icond2ccSwap $cond), (LO7 $r), $l, bb:$addr)>;
  def : Pat<(brcc CCSIOp:$cond, ty:$l, ty:$r, bb:$addr),
            (BrOpNode1 (icond2cc $cond), $l, $r, bb:$addr)>;
  def : Pat<(brcc CCUIOp:$cond, ty:$l, simm7:$r, bb:$addr),
            (BrOpNode2 (icond2cc $cond), 0, (CmpOpNode2 (LO7 $r), $l),
                       bb:$addr)>;
  def : Pat<(brcc CCUIOp:$cond, ty:$l, ty:$r, bb:$addr),
            (BrOpNode2 (icond2cc $cond), 0, (CmpOpNode1 $r, $l), bb:$addr)>;
}
defm : BRCCIm<i32, BRCFWrr, BRCFWir, CMPUWrr, CMPUWir>;
defm : BRCCIm<i64, BRCFLrr, BRCFLir, CMPULrr, CMPULir>;

// floating point brcc
multiclass BRCCFm<ValueType ty, CF BrOpNode1, CF BrOpNode2> {
  def : Pat<(brcc cond:$cond, ty:$l, simm7fp:$r, bb:$addr),
            (BrOpNode2 (fcond2ccSwap $cond), (LO7FP $r), $l, bb:$addr)>;
  def : Pat<(brcc cond:$cond, ty:$l, ty:$r, bb:$addr),
            (BrOpNode1 (fcond2cc $cond), $l, $r, bb:$addr)>;
}
defm : BRCCFm<f32, BRCFSrr, BRCFSir>;
defm : BRCCFm<f64, BRCFDrr, BRCFDir>;
def : Pat<(brcc cond:$cond, f128:$l, f128:$r, bb:$addr),
          (BRCFDir (fcond2cc $cond), 0, (FCMPQrr $r, $l), bb:$addr)>;

//===----------------------------------------------------------------------===//
// Pseudo Instructions
//===----------------------------------------------------------------------===//

// GETGOT for PIC
let Defs = [SX15 /* %got */, SX16 /* %plt */], hasSideEffects = 0 in {
  def GETGOT : Pseudo<(outs getGOT:$getpcseq), (ins), "$getpcseq">;
}

// GETFUNPLT for PIC
let hasSideEffects = 0 in
def GETFUNPLT : Pseudo<(outs I64:$dst), (ins i64imm:$addr),
                       "$dst, $addr",
                       [(set iPTR:$dst, (GetFunPLT tglobaladdr:$addr))] >;

def : Pat<(GetFunPLT tglobaladdr:$dst),
          (GETFUNPLT tglobaladdr:$dst)>;
def : Pat<(GetFunPLT texternalsym:$dst),
          (GETFUNPLT texternalsym:$dst)>;

// GETTLSADDR for TLS
let Defs = [SX0, SX10, SX12], hasSideEffects = 0 in
def GETTLSADDR : Pseudo<(outs), (ins i64imm:$addr),
                        "# GETTLSADDR $addr",
                        [(GetTLSAddr tglobaltlsaddr:$addr)] >;

def : Pat<(GetTLSAddr tglobaltlsaddr:$dst),
          (GETTLSADDR tglobaltlsaddr:$dst)>;

let Defs = [SX11], Uses = [SX11], hasSideEffects = 0 in {
def ADJCALLSTACKDOWN : Pseudo<(outs), (ins i64imm:$amt, i64imm:$amt2),
                              "# ADJCALLSTACKDOWN $amt, $amt2",
                              [(callseq_start timm:$amt, timm:$amt2)]>;
def ADJCALLSTACKUP : Pseudo<(outs), (ins i64imm:$amt1, i64imm:$amt2),
                            "# ADJCALLSTACKUP $amt1",
                            [(callseq_end timm:$amt1, timm:$amt2)]>;
}

let Defs = [SX8], Uses = [SX8, SX11], hasSideEffects = 0 in
def EXTEND_STACK : Pseudo<(outs), (ins),
                          "# EXTEND STACK",
                          []>;
let  hasSideEffects = 0 in
def EXTEND_STACK_GUARD : Pseudo<(outs), (ins),
                                "# EXTEND STACK GUARD",
                                []>;

// Dynamic stack allocation yields a __llvm_grow_stack for VE targets.
// These calls are needed to probe the stack when allocating more over
// %s8 (%sl - stack limit).

let Uses = [SX11], hasSideEffects = 1 in
def GETSTACKTOP : Pseudo<(outs I64:$dst), (ins),
                         "# GET STACK TOP",
                         [(set iPTR:$dst, (GetStackTop))]>;

//===----------------------------------------------------------------------===//
// Other patterns
//===----------------------------------------------------------------------===//

// SETCC pattern matches
//
//   CMP  %tmp, lhs, rhs     ; compare lhs and rhs
//   or   %res, 0, (0)1      ; initialize by 0
//   CMOV %res, (63)0, %tmp  ; set 1 if %tmp is true

class setccrr<Instruction INSN> :
    OutPatFrag<(ops node:$cond, node:$comp),
               (EXTRACT_SUBREG
                   (INSN $cond, $comp,
                         !add(63, 64), // means (63)0 == 1
                         (ORim 0, 0)), sub_i32)>;

def : Pat<(i32 (setcc i32:$l, i32:$r, CCSIOp:$cond)),
          (setccrr<CMOVWrm> (icond2cc $cond), (CMPSWSXrr $l, $r))>;
def : Pat<(i32 (setcc i32:$l, i32:$r, CCUIOp:$cond)),
          (setccrr<CMOVWrm> (icond2cc $cond), (CMPUWrr $l, $r))>;
def : Pat<(i32 (setcc i64:$l, i64:$r, CCSIOp:$cond)),
          (setccrr<CMOVLrm> (icond2cc $cond), (CMPSLrr $l, $r))>;
def : Pat<(i32 (setcc i64:$l, i64:$r, CCUIOp:$cond)),
          (setccrr<CMOVLrm> (icond2cc $cond), (CMPULrr $l, $r))>;
def : Pat<(i32 (setcc f32:$l, f32:$r, cond:$cond)),
          (setccrr<CMOVSrm> (fcond2cc $cond), (FCMPSrr $l, $r))>;
def : Pat<(i32 (setcc f64:$l, f64:$r, cond:$cond)),
          (setccrr<CMOVDrm> (fcond2cc $cond), (FCMPDrr $l, $r))>;
def : Pat<(i32 (setcc f128:$l, f128:$r, cond:$cond)),
          (setccrr<CMOVDrm> (fcond2cc $cond), (FCMPQrr $l, $r))>;

// Generic CMOV pattern matches
//   CMOV accepts i64 $t, $f, and result.  So, we extend it to support
//   i32/f32/f64/f128 $t, $f, and result.

// CMOV for i32
multiclass CMOVI32m<ValueType TY, string Insn> {
  def : Pat<(i32 (cmov TY:$cmp, i32:$t, i32:$f, (i32 CCOp:$cond))),
            (EXTRACT_SUBREG
                (!cast<Instruction>(Insn#"rr") (CCOP $cond), $cmp,
                           (INSERT_SUBREG (i64 (IMPLICIT_DEF)), $t, sub_i32),
                           (INSERT_SUBREG (i64 (IMPLICIT_DEF)), $f, sub_i32)),
                sub_i32)>;
  def : Pat<(i32 (cmov TY:$cmp, (i32 mimm:$t), i32:$f, (i32 CCOp:$cond))),
            (EXTRACT_SUBREG
                (!cast<Instruction>(Insn#"rm") (CCOP $cond), $cmp,
                           (MIMM $t),
                           (INSERT_SUBREG (i64 (IMPLICIT_DEF)), $f, sub_i32)),
                sub_i32)>;
}
defm : CMOVI32m<i64, "CMOVL">;
defm : CMOVI32m<i32, "CMOVW">;
defm : CMOVI32m<f64, "CMOVD">;
defm : CMOVI32m<f32, "CMOVS">;

// CMOV for f32
multiclass CMOVF32m<ValueType TY, string Insn> {
  def : Pat<(f32 (cmov TY:$cmp, f32:$t, f32:$f, (i32 CCOp:$cond))),
            (EXTRACT_SUBREG
                (!cast<Instruction>(Insn#"rr")
                    (CCOP $cond), $cmp,
                    (INSERT_SUBREG (i64 (IMPLICIT_DEF)), $t, sub_f32),
                    (INSERT_SUBREG (i64 (IMPLICIT_DEF)), $f, sub_f32)),
                sub_f32)>;
  def : Pat<(f32 (cmov TY:$cmp, (f32 mimmfp:$t), f32:$f, (i32 CCOp:$cond))),
            (EXTRACT_SUBREG
                (!cast<Instruction>(Insn#"rm")
                    (CCOP $cond), $cmp, (MIMMFP $t),
                    (INSERT_SUBREG (i64 (IMPLICIT_DEF)), $f, sub_f32)),
                sub_f32)>;
}
defm : CMOVF32m<i64, "CMOVL">;
defm : CMOVF32m<i32, "CMOVW">;
defm : CMOVF32m<f64, "CMOVD">;
defm : CMOVF32m<f32, "CMOVS">;

// CMOV for f64
multiclass CMOVF64m<ValueType TY, string Insn> {
  def : Pat<(f64 (cmov TY:$cmp, f64:$t, f64:$f, (i32 CCOp:$cond))),
            (!cast<Instruction>(Insn#"rr") (CCOP $cond), $cmp, $t, $f)>;
  def : Pat<(f64 (cmov TY:$cmp, (f64 mimmfp:$t), f64:$f, (i32 CCOp:$cond))),
            (!cast<Instruction>(Insn#"rm") (CCOP $cond), $cmp, (MIMMFP $t),
                                           $f)>;
}
defm : CMOVF64m<i64, "CMOVL">;
defm : CMOVF64m<i32, "CMOVW">;
defm : CMOVF64m<f64, "CMOVD">;
defm : CMOVF64m<f32, "CMOVS">;

// CMOV for f128
multiclass CMOVF128m<ValueType TY, string Insn> {
  def : Pat<(f128 (cmov TY:$cmp, f128:$t, f128:$f, (i32 CCOp:$cond))),
            (INSERT_SUBREG
              (INSERT_SUBREG (f128 (IMPLICIT_DEF)),
                (!cast<Instruction>(Insn#"rr") (CCOP $cond), $cmp,
                  (EXTRACT_SUBREG $t, sub_odd),
                  (EXTRACT_SUBREG $f, sub_odd)), sub_odd),
              (!cast<Instruction>(Insn#"rr") (CCOP $cond), $cmp,
                (EXTRACT_SUBREG $t, sub_even),
                (EXTRACT_SUBREG $f, sub_even)), sub_even)>;
}
defm : CMOVF128m<i64, "CMOVL">;
defm : CMOVF128m<i32, "CMOVW">;
defm : CMOVF128m<f64, "CMOVD">;
defm : CMOVF128m<f32, "CMOVS">;

// bitconvert
def : Pat<(f64 (bitconvert i64:$src)), (COPY_TO_REGCLASS $src, I64)>;
def : Pat<(i64 (bitconvert f64:$src)), (COPY_TO_REGCLASS $src, I64)>;

def : Pat<(i32 (bitconvert f32:$op)), (l2i (SRALri (f2l $op), 32))>;
def : Pat<(f32 (bitconvert i32:$op)), (l2f (SLLri (i2l $op), 32))>;

//===----------------------------------------------------------------------===//
// Vector Instruction Pattern Stuff
//===----------------------------------------------------------------------===//

// Custom intermediate ISDs.
class IsVLVT<int OpIdx> : SDTCisVT<OpIdx,i32>;
def vec_broadcast       : SDNode<"VEISD::VEC_BROADCAST", SDTypeProfile<1, 2,
                                 [SDTCisVec<0>, IsVLVT<2>]>>;

///// Packed mode Support /////
// unpack the lo part of this vector
def vec_unpack_lo   : SDNode<"VEISD::VEC_UNPACK_LO", SDTypeProfile<1, 2,
                             [SDTCisVec<0>, SDTCisVec<1>, IsVLVT<2>]>>;
// unpack the hipart of this vector
def vec_unpack_hi   : SDNode<"VEISD::VEC_UNPACK_HI", SDTypeProfile<1, 2,
                             [SDTCisVec<0>, SDTCisVec<1>, IsVLVT<2>]>>;
// re-pack v256i32, v256f32 back into tone v512.32
def vec_pack        : SDNode<"VEISD::VEC_PACK", SDTypeProfile<1, 3,
                             [SDTCisVec<0>, SDTCisVec<1>, SDTCisVec<2>,
                              SDTCisSameNumEltsAs<1,2>, IsVLVT<3>]>>;

// replicate lower 32bit to upper 32bit (f32 scalar replication).
def repl_f32            : SDNode<"VEISD::REPL_F32",
                            SDTypeProfile<1, 1,
                              [SDTCisInt<0>, SDTCisFP<1>]>>;
// replicate upper 32bit to lower 32 bit (i32 scalar replication).
def repl_i32            : SDNode<"VEISD::REPL_I32",
                            SDTypeProfile<1, 1,
                              [SDTCisInt<0>, SDTCisInt<1>]>>;


// Whether this is an all-true mask (assuming undef-bits above VL are all-true).
def true_mask           : PatLeaf<
                            (vec_broadcast (i32 nonzero), (i32 srcvalue))>;
// Match any broadcast (ignoring VL).
def any_broadcast       : PatFrag<(ops node:$sx),
                                  (vec_broadcast node:$sx, (i32 srcvalue))>;

// Vector instructions.
include "VEInstrVec.td"

// The vevlintrin
include "VEInstrIntrinsicVL.td"

// Patterns and intermediate SD nodes (VEC_*).
include "VEInstrPatternsVec.td"

// Patterns and intermediate SD nodes (VVP_*).
include "VVPInstrPatternsVec.td"
