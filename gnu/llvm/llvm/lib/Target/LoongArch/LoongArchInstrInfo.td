//== LoongArchInstrInfo.td - Target Description for LoongArch -*- tablegen -*-//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the LoongArch instructions in TableGen format.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// LoongArch specific DAG Nodes.
//===----------------------------------------------------------------------===//

// Target-independent type requirements, but with target-specific formats.
def SDT_CallSeqStart : SDCallSeqStart<[SDTCisVT<0, i32>,
                                       SDTCisVT<1, i32>]>;
def SDT_CallSeqEnd : SDCallSeqEnd<[SDTCisVT<0, i32>,
                                   SDTCisVT<1, i32>]>;

// Target-dependent type requirements.
def SDT_LoongArchCall : SDTypeProfile<0, -1, [SDTCisVT<0, GRLenVT>]>;
def SDT_LoongArchIntBinOpW : SDTypeProfile<1, 2, [
  SDTCisSameAs<0, 1>, SDTCisSameAs<0, 2>, SDTCisVT<0, i64>
]>;

def SDT_LoongArchBStrIns: SDTypeProfile<1, 4, [
  SDTCisInt<0>, SDTCisSameAs<0, 1>, SDTCisSameAs<0, 2>, SDTCisInt<3>,
  SDTCisSameAs<3, 4>
]>;

def SDT_LoongArchBStrPick: SDTypeProfile<1, 3, [
  SDTCisInt<0>, SDTCisSameAs<0, 1>, SDTCisInt<2>, SDTCisSameAs<2, 3>
]>;

// "VI" means no output and an integer input.
def SDT_LoongArchVI : SDTypeProfile<0, 1, [SDTCisVT<0, GRLenVT>]>;

def SDT_LoongArchCsrrd : SDTypeProfile<1, 1, [SDTCisInt<0>,
                                              SDTCisVT<1, GRLenVT>]>;
def SDT_LoongArchCsrwr : SDTypeProfile<1, 2, [SDTCisInt<0>, SDTCisSameAs<0, 1>,
                                              SDTCisVT<2, GRLenVT>]>;
def SDT_LoongArchCsrxchg : SDTypeProfile<1, 3, [SDTCisInt<0>,
                                                SDTCisSameAs<0, 1>,
                                                SDTCisSameAs<0, 2>,
                                                SDTCisVT<3, GRLenVT>]>;
def SDT_LoongArchIocsrwr : SDTypeProfile<0, 2, [SDTCisInt<0>,
                                                SDTCisSameAs<0, 1>]>;
def SDT_LoongArchMovgr2fcsr : SDTypeProfile<0, 2, [SDTCisVT<0, GRLenVT>,
                                                   SDTCisSameAs<0, 1>]>;
def SDT_LoongArchMovfcsr2gr : SDTypeProfile<1, 1, [SDTCisVT<0, GRLenVT>,
                                                   SDTCisSameAs<0, 1>]>;

// TODO: Add LoongArch specific DAG Nodes
// Target-independent nodes, but with target-specific formats.
def callseq_start : SDNode<"ISD::CALLSEQ_START", SDT_CallSeqStart,
                           [SDNPHasChain, SDNPOutGlue]>;
def callseq_end : SDNode<"ISD::CALLSEQ_END", SDT_CallSeqEnd,
                         [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

// Target-dependent nodes.
def loongarch_call : SDNode<"LoongArchISD::CALL", SDT_LoongArchCall,
                            [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                             SDNPVariadic]>;
def loongarch_ret : SDNode<"LoongArchISD::RET", SDTNone,
                           [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
def loongarch_tail : SDNode<"LoongArchISD::TAIL", SDT_LoongArchCall,
                            [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                             SDNPVariadic]>;
def loongarch_call_medium : SDNode<"LoongArchISD::CALL_MEDIUM", SDT_LoongArchCall,
                                   [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                                    SDNPVariadic]>;
def loongarch_tail_medium : SDNode<"LoongArchISD::TAIL_MEDIUM", SDT_LoongArchCall,
                                   [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                                    SDNPVariadic]>;
def loongarch_call_large : SDNode<"LoongArchISD::CALL_LARGE", SDT_LoongArchCall,
                                  [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                                   SDNPVariadic]>;
def loongarch_tail_large : SDNode<"LoongArchISD::TAIL_LARGE", SDT_LoongArchCall,
                                  [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                                   SDNPVariadic]>;
def loongarch_sll_w : SDNode<"LoongArchISD::SLL_W", SDT_LoongArchIntBinOpW>;
def loongarch_sra_w : SDNode<"LoongArchISD::SRA_W", SDT_LoongArchIntBinOpW>;
def loongarch_srl_w : SDNode<"LoongArchISD::SRL_W", SDT_LoongArchIntBinOpW>;
def loongarch_rotr_w : SDNode<"LoongArchISD::ROTR_W", SDT_LoongArchIntBinOpW>;
def loongarch_div_wu : SDNode<"LoongArchISD::DIV_WU", SDT_LoongArchIntBinOpW>;
def loongarch_mod_wu : SDNode<"LoongArchISD::MOD_WU", SDT_LoongArchIntBinOpW>;
def loongarch_crc_w_b_w
    : SDNode<"LoongArchISD::CRC_W_B_W", SDT_LoongArchIntBinOpW, [SDNPHasChain]>;
def loongarch_crc_w_h_w
    : SDNode<"LoongArchISD::CRC_W_H_W", SDT_LoongArchIntBinOpW, [SDNPHasChain]>;
def loongarch_crc_w_w_w
    : SDNode<"LoongArchISD::CRC_W_W_W", SDT_LoongArchIntBinOpW, [SDNPHasChain]>;
def loongarch_crc_w_d_w
    : SDNode<"LoongArchISD::CRC_W_D_W", SDT_LoongArchIntBinOpW, [SDNPHasChain]>;
def loongarch_crcc_w_b_w : SDNode<"LoongArchISD::CRCC_W_B_W",
                                  SDT_LoongArchIntBinOpW, [SDNPHasChain]>;
def loongarch_crcc_w_h_w : SDNode<"LoongArchISD::CRCC_W_H_W",
                                  SDT_LoongArchIntBinOpW, [SDNPHasChain]>;
def loongarch_crcc_w_w_w : SDNode<"LoongArchISD::CRCC_W_W_W",
                                  SDT_LoongArchIntBinOpW, [SDNPHasChain]>;
def loongarch_crcc_w_d_w : SDNode<"LoongArchISD::CRCC_W_D_W",
                                  SDT_LoongArchIntBinOpW, [SDNPHasChain]>;
def loongarch_bstrins
    : SDNode<"LoongArchISD::BSTRINS", SDT_LoongArchBStrIns>;
def loongarch_bstrpick
    : SDNode<"LoongArchISD::BSTRPICK", SDT_LoongArchBStrPick>;
def loongarch_revb_2h : SDNode<"LoongArchISD::REVB_2H", SDTUnaryOp>;
def loongarch_revb_2w : SDNode<"LoongArchISD::REVB_2W", SDTUnaryOp>;
def loongarch_bitrev_4b : SDNode<"LoongArchISD::BITREV_4B", SDTUnaryOp>;
def loongarch_bitrev_w : SDNode<"LoongArchISD::BITREV_W", SDTUnaryOp>;
def loongarch_clzw : SDNode<"LoongArchISD::CLZ_W", SDTIntBitCountUnaryOp>;
def loongarch_ctzw : SDNode<"LoongArchISD::CTZ_W", SDTIntBitCountUnaryOp>;
def loongarch_dbar : SDNode<"LoongArchISD::DBAR", SDT_LoongArchVI,
                             [SDNPHasChain, SDNPSideEffect]>;
def loongarch_ibar : SDNode<"LoongArchISD::IBAR", SDT_LoongArchVI,
                             [SDNPHasChain, SDNPSideEffect]>;
def loongarch_break : SDNode<"LoongArchISD::BREAK", SDT_LoongArchVI,
                              [SDNPHasChain, SDNPSideEffect]>;
def loongarch_movfcsr2gr : SDNode<"LoongArchISD::MOVFCSR2GR",
                                  SDT_LoongArchMovfcsr2gr, [SDNPHasChain]>;
def loongarch_movgr2fcsr : SDNode<"LoongArchISD::MOVGR2FCSR",
                                  SDT_LoongArchMovgr2fcsr,
                                  [SDNPHasChain, SDNPSideEffect]>;
def loongarch_syscall : SDNode<"LoongArchISD::SYSCALL", SDT_LoongArchVI,
                                [SDNPHasChain, SDNPSideEffect]>;
def loongarch_csrrd : SDNode<"LoongArchISD::CSRRD", SDT_LoongArchCsrrd,
                              [SDNPHasChain, SDNPSideEffect]>;
def loongarch_csrwr : SDNode<"LoongArchISD::CSRWR", SDT_LoongArchCsrwr,
                              [SDNPHasChain, SDNPSideEffect]>;
def loongarch_csrxchg : SDNode<"LoongArchISD::CSRXCHG",
                                SDT_LoongArchCsrxchg,
                                [SDNPHasChain, SDNPSideEffect]>;
def loongarch_iocsrrd_b : SDNode<"LoongArchISD::IOCSRRD_B", SDTUnaryOp,
                                  [SDNPHasChain, SDNPSideEffect]>;
def loongarch_iocsrrd_h : SDNode<"LoongArchISD::IOCSRRD_H", SDTUnaryOp,
                                  [SDNPHasChain, SDNPSideEffect]>;
def loongarch_iocsrrd_w : SDNode<"LoongArchISD::IOCSRRD_W", SDTUnaryOp,
                                  [SDNPHasChain, SDNPSideEffect]>;
def loongarch_iocsrrd_d : SDNode<"LoongArchISD::IOCSRRD_D", SDTUnaryOp,
                                  [SDNPHasChain, SDNPSideEffect]>;
def loongarch_iocsrwr_b : SDNode<"LoongArchISD::IOCSRWR_B",
                                  SDT_LoongArchIocsrwr,
                                  [SDNPHasChain, SDNPSideEffect]>;
def loongarch_iocsrwr_h : SDNode<"LoongArchISD::IOCSRWR_H",
                                  SDT_LoongArchIocsrwr,
                                  [SDNPHasChain, SDNPSideEffect]>;
def loongarch_iocsrwr_w : SDNode<"LoongArchISD::IOCSRWR_W",
                                  SDT_LoongArchIocsrwr,
                                  [SDNPHasChain, SDNPSideEffect]>;
def loongarch_iocsrwr_d : SDNode<"LoongArchISD::IOCSRWR_D",
                                  SDT_LoongArchIocsrwr,
                                  [SDNPHasChain, SDNPSideEffect]>;
def loongarch_cpucfg : SDNode<"LoongArchISD::CPUCFG", SDTUnaryOp,
                               [SDNPHasChain]>;

def to_fclass_mask: SDNodeXForm<timm, [{
  uint64_t Check = N->getZExtValue();
  unsigned Mask = 0;
  if (Check & fcSNan)
    Mask |= LoongArch::FClassMaskSignalingNaN;
  if (Check & fcQNan)
    Mask |= LoongArch::FClassMaskQuietNaN;
  if (Check & fcPosInf)
    Mask |= LoongArch::FClassMaskPositiveInfinity;
  if (Check & fcNegInf)
    Mask |= LoongArch::FClassMaskNegativeInfinity;
  if (Check & fcPosNormal)
    Mask |= LoongArch::FClassMaskPositiveNormal;
  if (Check & fcNegNormal)
    Mask |= LoongArch::FClassMaskNegativeNormal;
  if (Check & fcPosSubnormal)
    Mask |= LoongArch::FClassMaskPositiveSubnormal;
  if (Check & fcNegSubnormal)
    Mask |= LoongArch::FClassMaskNegativeSubnormal;
  if (Check & fcPosZero)
    Mask |= LoongArch::FClassMaskPositiveZero;
  if (Check & fcNegZero)
    Mask |= LoongArch::FClassMaskNegativeZero;
  return CurDAG->getTargetConstant(Mask, SDLoc(N), Subtarget->getGRLenVT());
}]>;

//===----------------------------------------------------------------------===//
// Operand and SDNode transformation definitions.
//===----------------------------------------------------------------------===//

class ImmAsmOperand<string prefix, int width, string suffix>
    : AsmOperandClass {
  let Name = prefix # "Imm" # width # suffix;
  let DiagnosticType = !strconcat("Invalid", Name);
  let RenderMethod = "addImmOperands";
}

class SImmAsmOperand<int width, string suffix = "">
    : ImmAsmOperand<"S", width, suffix> {
}

class UImmAsmOperand<int width, string suffix = "">
    : ImmAsmOperand<"U", width, suffix> {
}

// A parse method for "$r*" or "$r*, 0", where the 0 is be silently ignored.
// Only used for "AM*" instructions, in order to be compatible with GAS.
def AtomicMemAsmOperand : AsmOperandClass {
  let Name = "AtomicMemAsmOperand";
  let RenderMethod = "addRegOperands";
  let PredicateMethod = "isGPR";
  let ParserMethod = "parseAtomicMemOp";
}

def GPRMemAtomic : RegisterOperand<GPR> {
  let ParserMatchClass = AtomicMemAsmOperand;
  let PrintMethod = "printAtomicMemOp";
}

// A parameterized register class alternative to i32imm/i64imm from Target.td.
def grlenimm : Operand<GRLenVT>;
def imm32 : Operand<GRLenVT> {
  let ParserMatchClass = ImmAsmOperand<"", 32, "">;
}
def imm64 : Operand<i64> {
  let ParserMatchClass = ImmAsmOperand<"", 64, "">;
}

def uimm1 : Operand<GRLenVT>, ImmLeaf<GRLenVT, [{return isUInt<1>(Imm);}]>{
  let ParserMatchClass = UImmAsmOperand<1>;
}

def uimm2 : Operand<GRLenVT>, ImmLeaf<GRLenVT, [{return isUInt<2>(Imm);}]> {
  let ParserMatchClass = UImmAsmOperand<2>;
}

def uimm2_plus1 : Operand<GRLenVT>,
    ImmLeaf<GRLenVT, [{return isUInt<2>(Imm - 1);}]> {
  let ParserMatchClass = UImmAsmOperand<2, "plus1">;
  let EncoderMethod = "getImmOpValueSub1";
  let DecoderMethod = "decodeUImmOperand<2, 1>";
}

def uimm3 : Operand<GRLenVT>, ImmLeaf<GRLenVT, [{return isUInt<3>(Imm);}]> {
  let ParserMatchClass = UImmAsmOperand<3>;
}

def uimm4 : Operand<GRLenVT>, ImmLeaf<GRLenVT, [{return isUInt<4>(Imm);}]> {
  let ParserMatchClass = UImmAsmOperand<4>;
}

def uimm5 : Operand<GRLenVT>, ImmLeaf<GRLenVT, [{return isUInt<5>(Imm);}]> {
  let ParserMatchClass = UImmAsmOperand<5>;
}

def uimm6 : Operand<GRLenVT>, ImmLeaf<GRLenVT, [{return isUInt<6>(Imm);}]> {
  let ParserMatchClass = UImmAsmOperand<6>;
}

def uimm7 : Operand<GRLenVT> {
  let ParserMatchClass = UImmAsmOperand<7>;
}

def uimm8 : Operand<GRLenVT>, ImmLeaf<GRLenVT, [{return isUInt<8>(Imm);}]> {
  let ParserMatchClass = UImmAsmOperand<8>;
}

class UImm12Operand : Operand<GRLenVT>,
                      ImmLeaf <GRLenVT, [{return isUInt<12>(Imm);}]> {
  let DecoderMethod = "decodeUImmOperand<12>";
}

def uimm12 : UImm12Operand {
  let ParserMatchClass = UImmAsmOperand<12>;
}

def uimm12_ori : UImm12Operand {
  let ParserMatchClass = UImmAsmOperand<12, "ori">;
}

def uimm14 : Operand<GRLenVT>,
             ImmLeaf <GRLenVT, [{return isUInt<14>(Imm);}]> {
  let ParserMatchClass = UImmAsmOperand<14>;
}

def uimm15 : Operand<GRLenVT>,
             ImmLeaf <GRLenVT, [{return isUInt<15>(Imm);}]> {
  let ParserMatchClass = UImmAsmOperand<15>;
}

def simm5 : Operand<GRLenVT> {
  let ParserMatchClass = SImmAsmOperand<5>;
  let DecoderMethod = "decodeSImmOperand<5>";
}

def simm8 : Operand<GRLenVT> {
  let ParserMatchClass = SImmAsmOperand<8>;
  let DecoderMethod = "decodeSImmOperand<8>";
}

foreach I = [1, 2, 3] in {
def simm8_lsl # I : Operand<GRLenVT> {
  let ParserMatchClass = SImmAsmOperand<8, "lsl" # I>;
  let EncoderMethod = "getImmOpValueAsr<" # I # ">";
  let DecoderMethod = "decodeSImmOperand<8," # I # ">";
}
}

def simm9_lsl3 : Operand<GRLenVT> {
  let ParserMatchClass = SImmAsmOperand<9, "lsl3">;
  let EncoderMethod = "getImmOpValueAsr<3>";
  let DecoderMethod = "decodeSImmOperand<9, 3>";
}

def simm10 : Operand<GRLenVT> {
  let ParserMatchClass = SImmAsmOperand<10>;
}

def simm10_lsl2 : Operand<GRLenVT> {
  let ParserMatchClass = SImmAsmOperand<10, "lsl2">;
  let EncoderMethod = "getImmOpValueAsr<2>";
  let DecoderMethod = "decodeSImmOperand<10, 2>";
}

def simm11_lsl1 : Operand<GRLenVT> {
  let ParserMatchClass = SImmAsmOperand<11, "lsl1">;
  let EncoderMethod = "getImmOpValueAsr<1>";
  let DecoderMethod = "decodeSImmOperand<11, 1>";
}

class SImm12Operand : Operand<GRLenVT>,
                      ImmLeaf <GRLenVT, [{return isInt<12>(Imm);}]> {
  let DecoderMethod = "decodeSImmOperand<12>";
}

def simm12 : SImm12Operand {
  let ParserMatchClass = SImmAsmOperand<12>;
}

def simm12_addlike : SImm12Operand {
  let ParserMatchClass = SImmAsmOperand<12, "addlike">;
}

def simm12_lu52id : SImm12Operand {
  let ParserMatchClass = SImmAsmOperand<12, "lu52id">;
}

def simm13 : Operand<GRLenVT> {
  let ParserMatchClass = SImmAsmOperand<13>;
  let DecoderMethod = "decodeSImmOperand<13>";
}

def simm14_lsl2 : Operand<GRLenVT>,
    ImmLeaf<GRLenVT, [{return isShiftedInt<14,2>(Imm);}]> {
  let ParserMatchClass = SImmAsmOperand<14, "lsl2">;
  let EncoderMethod = "getImmOpValueAsr<2>";
  let DecoderMethod = "decodeSImmOperand<14, 2>";
}

def simm16 : Operand<GRLenVT> {
  let ParserMatchClass = SImmAsmOperand<16>;
  let DecoderMethod = "decodeSImmOperand<16>";
}

def simm16_lsl2 : Operand<GRLenVT>,
    ImmLeaf<GRLenVT, [{return isInt<16>(Imm>>2);}]> {
  let ParserMatchClass = SImmAsmOperand<16, "lsl2">;
  let EncoderMethod = "getImmOpValueAsr<2>";
  let DecoderMethod = "decodeSImmOperand<16, 2>";
}

def simm16_lsl2_br : Operand<OtherVT> {
  let ParserMatchClass = SImmAsmOperand<16, "lsl2">;
  let EncoderMethod = "getImmOpValueAsr<2>";
  let DecoderMethod = "decodeSImmOperand<16, 2>";
}

class SImm20Operand : Operand<GRLenVT> {
  let DecoderMethod = "decodeSImmOperand<20>";
}

def simm20 : SImm20Operand {
  let ParserMatchClass = SImmAsmOperand<20>;
}

def simm20_pcalau12i : SImm20Operand {
  let ParserMatchClass = SImmAsmOperand<20, "pcalau12i">;
}

def simm20_lu12iw : SImm20Operand {
  let ParserMatchClass = SImmAsmOperand<20, "lu12iw">;
}

def simm20_lu32id : SImm20Operand {
  let ParserMatchClass = SImmAsmOperand<20, "lu32id">;
}

def simm20_pcaddu18i : SImm20Operand {
  let ParserMatchClass = SImmAsmOperand<20, "pcaddu18i">;
}

def simm20_pcaddi : SImm20Operand {
  let ParserMatchClass = SImmAsmOperand<20, "pcaddi">;
}

def simm21_lsl2 : Operand<OtherVT> {
  let ParserMatchClass = SImmAsmOperand<21, "lsl2">;
  let EncoderMethod = "getImmOpValueAsr<2>";
  let DecoderMethod = "decodeSImmOperand<21, 2>";
}

def SImm26OperandB: AsmOperandClass {
  let Name = "SImm26OperandB";
  let PredicateMethod = "isSImm26Operand";
  let RenderMethod = "addImmOperands";
  let DiagnosticType = "InvalidSImm26Operand";
  let ParserMethod = "parseImmediate";
}

// A symbol or an imm used in B/PseudoBR.
def simm26_b : Operand<OtherVT> {
  let ParserMatchClass = SImm26OperandB;
  let EncoderMethod = "getImmOpValueAsr<2>";
  let DecoderMethod = "decodeSImmOperand<26, 2>";
}

def SImm26OperandBL: AsmOperandClass {
  let Name = "SImm26OperandBL";
  let PredicateMethod = "isSImm26Operand";
  let RenderMethod = "addImmOperands";
  let DiagnosticType = "InvalidSImm26Operand";
  let ParserMethod = "parseSImm26Operand";
}

// A symbol or an imm used in BL/PseudoCALL/PseudoTAIL.
def simm26_symbol : Operand<GRLenVT> {
  let ParserMatchClass = SImm26OperandBL;
  let EncoderMethod = "getImmOpValueAsr<2>";
  let DecoderMethod = "decodeSImmOperand<26, 2>";
}

// A 32-bit signed immediate with the lowest 16 bits zeroed, suitable for
// direct use with `addu16i.d`.
def simm16_lsl16 : Operand<GRLenVT>,
    ImmLeaf<GRLenVT, [{return isShiftedInt<16, 16>(Imm);}]>;

// A 32-bit signed immediate expressible with a pair of `addu16i.d + addi` for
// use in additions.
def simm32_hi16_lo12: Operand<GRLenVT>, ImmLeaf<GRLenVT, [{
  return !isInt<12>(Imm) && isShiftedInt<16, 16>(Imm - SignExtend64<12>(Imm));
}]>;

def BareSymbol : AsmOperandClass {
  let Name = "BareSymbol";
  let RenderMethod = "addImmOperands";
  let DiagnosticType = "InvalidBareSymbol";
  let ParserMethod = "parseImmediate";
}

// A bare symbol used in "PseudoLA_*" instructions.
def bare_symbol : Operand<GRLenVT> {
  let ParserMatchClass = BareSymbol;
}

def TPRelAddSymbol : AsmOperandClass {
  let Name = "TPRelAddSymbol";
  let RenderMethod = "addImmOperands";
  let DiagnosticType = "InvalidTPRelAddSymbol";
  let ParserMethod = "parseOperandWithModifier";
}

// A bare symbol with the %le_add_r variant.
def tprel_add_symbol : Operand<GRLenVT> {
  let ParserMatchClass = TPRelAddSymbol;
}


// Standalone (codegen-only) immleaf patterns.

// A 12-bit signed immediate plus one where the imm range will be [-2047, 2048].
def simm12_plus1 : ImmLeaf<GRLenVT,
  [{return (isInt<12>(Imm) && Imm != -2048) || Imm == 2048;}]>;

// Return the negation of an immediate value.
def NegImm : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(-N->getSExtValue(), SDLoc(N),
                                   N->getValueType(0));
}]>;

// FP immediate patterns.
def fpimm0    : PatLeaf<(fpimm), [{return N->isExactlyValue(+0.0);}]>;
def fpimm0neg : PatLeaf<(fpimm), [{return N->isExactlyValue(-0.0);}]>;
def fpimm1    : PatLeaf<(fpimm), [{return N->isExactlyValue(+1.0);}]>;

// Return an immediate subtracted from 32.
def ImmSubFrom32 : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(32 - N->getZExtValue(), SDLoc(N),
                                   N->getValueType(0));
}]>;

// Return the lowest 12 bits of the signed immediate.
def LO12: SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(SignExtend64<12>(N->getSExtValue()),
                                   SDLoc(N), N->getValueType(0));
}]>;

// Return the higher 16 bits of the signed immediate.
def HI16 : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(N->getSExtValue() >> 16, SDLoc(N),
                                   N->getValueType(0));
}]>;

// Return the higher 16 bits of the signed immediate, adjusted for use within an
// `addu16i.d + addi` pair.
def HI16ForAddu16idAddiPair: SDNodeXForm<imm, [{
  auto Imm = N->getSExtValue();
  return CurDAG->getTargetConstant((Imm - SignExtend64<12>(Imm)) >> 16,
                                   SDLoc(N), N->getValueType(0));
}]>;

def BaseAddr : ComplexPattern<iPTR, 1, "SelectBaseAddr">;
def AddrConstant : ComplexPattern<iPTR, 2, "SelectAddrConstant">;
def NonFIBaseAddr : ComplexPattern<iPTR, 1, "selectNonFIBaseAddr">;

def fma_nsz : PatFrag<(ops node:$fj, node:$fk, node:$fa),
                      (fma node:$fj, node:$fk, node:$fa), [{
  return N->getFlags().hasNoSignedZeros();
}]>;

// Check if (add r, imm) can be optimized to (ADDI (ADDI r, imm0), imm1),
// in which imm = imm0 + imm1, and both imm0 & imm1 are simm12.
def AddiPair : PatLeaf<(imm), [{
  if (!N->hasOneUse())
    return false;
  // The immediate operand must be in range [-4096,-2049] or [2048,4094].
  int64_t Imm = N->getSExtValue();
  return (-4096 <= Imm && Imm <= -2049) || (2048 <= Imm && Imm <= 4094);
}]>;

// Return -2048 if immediate is negative or 2047 if positive.
def AddiPairImmLarge : SDNodeXForm<imm, [{
  int64_t Imm = N->getSExtValue() < 0 ? -2048 : 2047;
  return CurDAG->getTargetConstant(Imm, SDLoc(N),
                                   N->getValueType(0));
}]>;

// Return imm - (imm < 0 ? -2048 : 2047).
def AddiPairImmSmall : SDNodeXForm<imm, [{
  int64_t Imm = N->getSExtValue();
  int64_t Adj = Imm < 0 ? -2048 : 2047;
  return CurDAG->getTargetConstant(Imm - Adj, SDLoc(N),
                                   N->getValueType(0));
}]>;

// Check if (mul r, imm) can be optimized to (SLLI (ALSL r, r, i0), i1),
// in which imm = (1 + (1 << i0)) << i1.
def AlslSlliImm : PatLeaf<(imm), [{
  if (!N->hasOneUse())
    return false;
  uint64_t Imm = N->getZExtValue();
  unsigned I1 = llvm::countr_zero(Imm);
  uint64_t Rem = Imm >> I1;
  return Rem == 3 || Rem == 5 || Rem == 9 || Rem == 17;
}]>;

def AlslSlliImmI1 : SDNodeXForm<imm, [{
  uint64_t Imm = N->getZExtValue();
  unsigned I1 = llvm::countr_zero(Imm);
  return CurDAG->getTargetConstant(I1, SDLoc(N),
                                   N->getValueType(0));
}]>;

def AlslSlliImmI0 : SDNodeXForm<imm, [{
  uint64_t Imm = N->getZExtValue();
  unsigned I1 = llvm::countr_zero(Imm);
  uint64_t I0;
  switch (Imm >> I1) {
  case 3:  I0 = 1; break;
  case 5:  I0 = 2; break;
  case 9:  I0 = 3; break;
  default: I0 = 4; break;
  }
  return CurDAG->getTargetConstant(I0, SDLoc(N),
                                   N->getValueType(0));
}]>;

// Check if (and r, imm) can be optimized to (BSTRINS r, R0, msb, lsb),
// in which imm = ~((2^^(msb-lsb+1) - 1) << lsb).
def BstrinsImm : PatLeaf<(imm), [{
  if (!N->hasOneUse())
    return false;
  uint64_t Imm = N->getZExtValue();
  // andi can be used instead if Imm <= 0xfff.
  if (Imm <= 0xfff)
    return false;
  unsigned MaskIdx, MaskLen;
  return N->getValueType(0).getSizeInBits() == 32
             ? llvm::isShiftedMask_32(~Imm, MaskIdx, MaskLen)
             : llvm::isShiftedMask_64(~Imm, MaskIdx, MaskLen);
}]>;

def BstrinsMsb: SDNodeXForm<imm, [{
  uint64_t Imm = N->getZExtValue();
  unsigned MaskIdx, MaskLen;
  N->getValueType(0).getSizeInBits() == 32
      ? llvm::isShiftedMask_32(~Imm, MaskIdx, MaskLen)
      : llvm::isShiftedMask_64(~Imm, MaskIdx, MaskLen);
  return CurDAG->getTargetConstant(MaskIdx + MaskLen - 1, SDLoc(N),
                                   N->getValueType(0));
}]>;

def BstrinsLsb: SDNodeXForm<imm, [{
  uint64_t Imm = N->getZExtValue();
  unsigned MaskIdx, MaskLen;
  N->getValueType(0).getSizeInBits() == 32
      ? llvm::isShiftedMask_32(~Imm, MaskIdx, MaskLen)
      : llvm::isShiftedMask_64(~Imm, MaskIdx, MaskLen);
  return CurDAG->getTargetConstant(MaskIdx, SDLoc(N), N->getValueType(0));
}]>;

//===----------------------------------------------------------------------===//
// Instruction Formats
//===----------------------------------------------------------------------===//

include "LoongArchInstrFormats.td"
include "LoongArchFloatInstrFormats.td"
include "LoongArchLSXInstrFormats.td"
include "LoongArchLASXInstrFormats.td"
include "LoongArchLBTInstrFormats.td"

//===----------------------------------------------------------------------===//
// Instruction Class Templates
//===----------------------------------------------------------------------===//

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in {
class ALU_3R<bits<32> op>
    : Fmt3R<op, (outs GPR:$rd), (ins GPR:$rj, GPR:$rk), "$rd, $rj, $rk">;
class ALU_2R<bits<32> op>
    : Fmt2R<op, (outs GPR:$rd), (ins GPR:$rj), "$rd, $rj">;

class ALU_3RI2<bits<32> op, Operand ImmOpnd>
    : Fmt3RI2<op, (outs GPR:$rd), (ins GPR:$rj, GPR:$rk, ImmOpnd:$imm2),
              "$rd, $rj, $rk, $imm2">;
class ALU_3RI3<bits<32> op, Operand ImmOpnd>
    : Fmt3RI3<op, (outs GPR:$rd), (ins GPR:$rj, GPR:$rk, ImmOpnd:$imm3),
              "$rd, $rj, $rk, $imm3">;
class ALU_2RI5<bits<32> op, Operand ImmOpnd>
    : Fmt2RI5<op, (outs GPR:$rd), (ins GPR:$rj, ImmOpnd:$imm5),
              "$rd, $rj, $imm5">;
class ALU_2RI6<bits<32> op, Operand ImmOpnd>
    : Fmt2RI6<op, (outs GPR:$rd), (ins GPR:$rj, ImmOpnd:$imm6),
              "$rd, $rj, $imm6">;
class ALU_2RI12<bits<32> op, Operand ImmOpnd>
    : Fmt2RI12<op, (outs GPR:$rd), (ins GPR:$rj, ImmOpnd:$imm12),
               "$rd, $rj, $imm12">;
class ALU_2RI16<bits<32> op, Operand ImmOpnd>
    : Fmt2RI16<op, (outs GPR:$rd), (ins GPR:$rj, ImmOpnd:$imm16),
               "$rd, $rj, $imm16">;
class ALU_1RI20<bits<32> op, Operand ImmOpnd>
    : Fmt1RI20<op, (outs GPR:$rd), (ins ImmOpnd:$imm20), "$rd, $imm20">;
} // hasSideEffects = 0, mayLoad = 0, mayStore = 0

let hasSideEffects = 1, mayLoad = 0, mayStore = 0 in
class MISC_I15<bits<32> op>
    : FmtI15<op, (outs), (ins uimm15:$imm15), "$imm15">;

let hasSideEffects = 1, mayLoad = 0, mayStore = 0 in
class RDTIME_2R<bits<32> op>
    : Fmt2R<op, (outs GPR:$rd, GPR:$rj), (ins), "$rd, $rj">;

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in {
class BrCC_2RI16<bits<32> op>
    : Fmt2RI16<op, (outs), (ins GPR:$rj, GPR:$rd, simm16_lsl2_br:$imm16),
               "$rj, $rd, $imm16"> {
  let isBranch = 1;
  let isTerminator = 1;
}
class BrCCZ_1RI21<bits<32> op>
    : Fmt1RI21<op, (outs), (ins GPR:$rj, simm21_lsl2:$imm21),
               "$rj, $imm21"> {
  let isBranch = 1;
  let isTerminator = 1;
}
class Br_I26<bits<32> op>
    : FmtI26<op, (outs), (ins simm26_b:$imm26), "$imm26"> {
  let isBranch = 1;
  let isTerminator = 1;
  let isBarrier = 1;
}
} // hasSideEffects = 0, mayLoad = 0, mayStore = 0

let hasSideEffects = 0, mayLoad = 1, mayStore = 0 in {
class LOAD_3R<bits<32> op>
    : Fmt3R<op, (outs GPR:$rd), (ins GPR:$rj, GPR:$rk), "$rd, $rj, $rk">;
class LOAD_2RI12<bits<32> op>
    : Fmt2RI12<op, (outs GPR:$rd), (ins GPR:$rj, simm12_addlike:$imm12),
               "$rd, $rj, $imm12">;
class LOAD_2RI14<bits<32> op>
    : Fmt2RI14<op, (outs GPR:$rd), (ins GPR:$rj, simm14_lsl2:$imm14),
               "$rd, $rj, $imm14">;
} // hasSideEffects = 0, mayLoad = 1, mayStore = 0

let hasSideEffects = 0, mayLoad = 0, mayStore = 1 in {
class STORE_3R<bits<32> op>
    : Fmt3R<op, (outs), (ins GPR:$rd, GPR:$rj, GPR:$rk),
            "$rd, $rj, $rk">;
class STORE_2RI12<bits<32> op>
    : Fmt2RI12<op, (outs), (ins GPR:$rd, GPR:$rj, simm12_addlike:$imm12),
               "$rd, $rj, $imm12">;
class STORE_2RI14<bits<32> op>
    : Fmt2RI14<op, (outs), (ins GPR:$rd, GPR:$rj, simm14_lsl2:$imm14),
               "$rd, $rj, $imm14">;
} // hasSideEffects = 0, mayLoad = 0, mayStore = 1

let hasSideEffects = 0, mayLoad = 1, mayStore = 1, Constraints = "@earlyclobber $rd" in
class AM_3R<bits<32> op>
    : Fmt3R<op, (outs GPR:$rd), (ins GPR:$rk, GPRMemAtomic:$rj),
            "$rd, $rk, $rj">;

let hasSideEffects = 0, mayLoad = 1, mayStore = 0 in {
class LLBase<bits<32> op>
    : Fmt2RI14<op, (outs GPR:$rd), (ins GPR:$rj, simm14_lsl2:$imm14),
               "$rd, $rj, $imm14">;
class LLBase_ACQ<bits<32> op>
    : Fmt2R<op, (outs GPR:$rd), (ins GPR:$rj), "$rd, $rj">;
}

let hasSideEffects = 0, mayLoad = 0, mayStore = 1, Constraints = "$rd = $dst" in {
class SCBase<bits<32> op>
    : Fmt2RI14<op, (outs GPR:$dst), (ins GPR:$rd, GPR:$rj, simm14_lsl2:$imm14),
               "$rd, $rj, $imm14">;
class SCBase_128<bits<32> op>
    : Fmt3R<op, (outs GPR:$dst), (ins GPR:$rd, GPR:$rk, GPR:$rj),
               "$rd, $rk, $rj">;
class SCBase_REL<bits<32> op>
    : Fmt2R<op, (outs GPR:$dst), (ins GPR:$rd, GPR:$rj), "$rd, $rj">;
}

let hasSideEffects = 1 in
class IOCSRRD<bits<32> op>
    : Fmt2R<op, (outs GPR:$rd), (ins GPR:$rj), "$rd, $rj">;

let hasSideEffects = 1 in
class IOCSRWR<bits<32> op>
    : Fmt2R<op, (outs), (ins GPR:$rd, GPR:$rj), "$rd, $rj">;

//===----------------------------------------------------------------------===//
// Basic Integer Instructions
//===----------------------------------------------------------------------===//

// Arithmetic Operation Instructions
def ADD_W : ALU_3R<0x00100000>;
def SUB_W : ALU_3R<0x00110000>;
def ADDI_W : ALU_2RI12<0x02800000, simm12_addlike>;
def ALSL_W : ALU_3RI2<0x00040000, uimm2_plus1>;
let isReMaterializable = 1 in {
def LU12I_W : ALU_1RI20<0x14000000, simm20_lu12iw>;
}
def SLT  : ALU_3R<0x00120000>;
def SLTU : ALU_3R<0x00128000>;
def SLTI  : ALU_2RI12<0x02000000, simm12>;
def SLTUI : ALU_2RI12<0x02400000, simm12>;
def PCADDI    : ALU_1RI20<0x18000000, simm20_pcaddi>;
def PCADDU12I : ALU_1RI20<0x1c000000, simm20>;
def PCALAU12I : ALU_1RI20<0x1a000000, simm20_pcalau12i>;
def AND  : ALU_3R<0x00148000>;
def OR   : ALU_3R<0x00150000>;
def NOR  : ALU_3R<0x00140000>;
def XOR  : ALU_3R<0x00158000>;
def ANDN : ALU_3R<0x00168000>;
def ORN  : ALU_3R<0x00160000>;
def ANDI : ALU_2RI12<0x03400000, uimm12>;
// See LoongArchInstrInfo::isAsCheapAsAMove for more details.
let isReMaterializable = 1, isAsCheapAsAMove = 1 in {
def ORI  : ALU_2RI12<0x03800000, uimm12_ori>;
def XORI : ALU_2RI12<0x03c00000, uimm12>;
}
def MUL_W   : ALU_3R<0x001c0000>;
def MULH_W  : ALU_3R<0x001c8000>;
def MULH_WU : ALU_3R<0x001d0000>;
let usesCustomInserter = true in {
def DIV_W   : ALU_3R<0x00200000>;
def MOD_W   : ALU_3R<0x00208000>;
def DIV_WU  : ALU_3R<0x00210000>;
def MOD_WU  : ALU_3R<0x00218000>;
} // usesCustomInserter = true

// Bit-shift Instructions
def SLL_W  : ALU_3R<0x00170000>;
def SRL_W  : ALU_3R<0x00178000>;
def SRA_W  : ALU_3R<0x00180000>;
def ROTR_W : ALU_3R<0x001b0000>;

def SLLI_W  : ALU_2RI5<0x00408000, uimm5>;
def SRLI_W  : ALU_2RI5<0x00448000, uimm5>;
def SRAI_W  : ALU_2RI5<0x00488000, uimm5>;
def ROTRI_W : ALU_2RI5<0x004c8000, uimm5>;

// Bit-manipulation Instructions
def EXT_W_B : ALU_2R<0x00005c00>;
def EXT_W_H : ALU_2R<0x00005800>;
def CLO_W   : ALU_2R<0x00001000>;
def CLZ_W   : ALU_2R<0x00001400>;
def CTO_W   : ALU_2R<0x00001800>;
def CTZ_W   : ALU_2R<0x00001c00>;
def BYTEPICK_W : ALU_3RI2<0x00080000, uimm2>;
def REVB_2H   : ALU_2R<0x00003000>;
def BITREV_4B : ALU_2R<0x00004800>;
def BITREV_W  : ALU_2R<0x00005000>;
let Constraints = "$rd = $dst" in {
def BSTRINS_W  : FmtBSTR_W<0x00600000, (outs GPR:$dst),
                           (ins GPR:$rd, GPR:$rj, uimm5:$msbw, uimm5:$lsbw),
                           "$rd, $rj, $msbw, $lsbw">;
}
def BSTRPICK_W : FmtBSTR_W<0x00608000, (outs GPR:$rd),
                           (ins GPR:$rj, uimm5:$msbw, uimm5:$lsbw),
                           "$rd, $rj, $msbw, $lsbw">;
def MASKEQZ : ALU_3R<0x00130000>;
def MASKNEZ : ALU_3R<0x00138000>;

// Branch Instructions
def BEQ  : BrCC_2RI16<0x58000000>;
def BNE  : BrCC_2RI16<0x5c000000>;
def BLT  : BrCC_2RI16<0x60000000>;
def BGE  : BrCC_2RI16<0x64000000>;
def BLTU : BrCC_2RI16<0x68000000>;
def BGEU : BrCC_2RI16<0x6c000000>;
def BEQZ : BrCCZ_1RI21<0x40000000>;
def BNEZ : BrCCZ_1RI21<0x44000000>;
def B : Br_I26<0x50000000>;

let hasSideEffects = 0, mayLoad = 0, mayStore = 0, isCall = 1, Defs=[R1] in
def BL : FmtI26<0x54000000, (outs), (ins simm26_symbol:$imm26), "$imm26">;
let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
def JIRL : Fmt2RI16<0x4c000000, (outs GPR:$rd),
                    (ins GPR:$rj, simm16_lsl2:$imm16), "$rd, $rj, $imm16">;

// Common Memory Access Instructions
def LD_B  : LOAD_2RI12<0x28000000>;
def LD_H  : LOAD_2RI12<0x28400000>;
def LD_W  : LOAD_2RI12<0x28800000>;
def LD_BU : LOAD_2RI12<0x2a000000>;
def LD_HU : LOAD_2RI12<0x2a400000>;
def ST_B : STORE_2RI12<0x29000000>;
def ST_H : STORE_2RI12<0x29400000>;
def ST_W : STORE_2RI12<0x29800000>;
let hasSideEffects = 0, mayLoad = 1, mayStore = 1 in
def PRELD : FmtPRELD<(outs), (ins uimm5:$imm5, GPR:$rj, simm12:$imm12),
                     "$imm5, $rj, $imm12">;

// Atomic Memory Access Instructions
def LL_W : LLBase<0x20000000>;
def SC_W : SCBase<0x21000000>;
def LLACQ_W : LLBase_ACQ<0x38578000>;
def SCREL_W : SCBase_REL<0x38578400>;

// Barrier Instructions
def DBAR : MISC_I15<0x38720000>;
def IBAR : MISC_I15<0x38728000>;

// Other Miscellaneous Instructions
def SYSCALL : MISC_I15<0x002b0000>;
def BREAK   : MISC_I15<0x002a0000>;
def RDTIMEL_W : RDTIME_2R<0x00006000>;
def RDTIMEH_W : RDTIME_2R<0x00006400>;
def CPUCFG : ALU_2R<0x00006c00>;

// Cache Maintenance Instructions
def CACOP : FmtCACOP<(outs), (ins uimm5:$op, GPR:$rj, simm12:$imm12),
                     "$op, $rj, $imm12">;

/// LA64 instructions

let Predicates = [IsLA64] in {

// Arithmetic Operation Instructions for 64-bits
def ADD_D : ALU_3R<0x00108000>;
def SUB_D : ALU_3R<0x00118000>;
// ADDI_D isn't always rematerializable, but isReMaterializable will be used as
// a hint which is verified in isReallyTriviallyReMaterializable.
// See LoongArchInstrInfo::isAsCheapAsAMove for more details.
let isReMaterializable = 1, isAsCheapAsAMove = 1 in {
def ADDI_D : ALU_2RI12<0x02c00000, simm12_addlike>;
}
def ADDU16I_D : ALU_2RI16<0x10000000, simm16>;
def ALSL_WU : ALU_3RI2<0x00060000, uimm2_plus1>;
def ALSL_D  : ALU_3RI2<0x002c0000, uimm2_plus1>;
let Constraints = "$rd = $dst" in {
let hasSideEffects = 0, mayLoad = 0, mayStore = 0,
    isReMaterializable = 1 in
def LU32I_D : Fmt1RI20<0x16000000, (outs GPR:$dst),
                       (ins GPR:$rd, simm20_lu32id:$imm20),
                       "$rd, $imm20">;
}
let isReMaterializable = 1 in {
def LU52I_D : ALU_2RI12<0x03000000, simm12_lu52id>;
}
def PCADDU18I : ALU_1RI20<0x1e000000, simm20_pcaddu18i>;
def MUL_D     : ALU_3R<0x001d8000>;
def MULH_D    : ALU_3R<0x001e0000>;
def MULH_DU   : ALU_3R<0x001e8000>;
def MULW_D_W  : ALU_3R<0x001f0000>;
def MULW_D_WU : ALU_3R<0x001f8000>;
let usesCustomInserter = true in {
def DIV_D     : ALU_3R<0x00220000>;
def MOD_D     : ALU_3R<0x00228000>;
def DIV_DU    : ALU_3R<0x00230000>;
def MOD_DU    : ALU_3R<0x00238000>;
} // usesCustomInserter = true

// Bit-shift Instructions for 64-bits
def SLL_D  : ALU_3R<0x00188000>;
def SRL_D  : ALU_3R<0x00190000>;
def SRA_D  : ALU_3R<0x00198000>;
def ROTR_D : ALU_3R<0x001b8000>;
def SLLI_D  : ALU_2RI6<0x00410000, uimm6>;
def SRLI_D  : ALU_2RI6<0x00450000, uimm6>;
def SRAI_D  : ALU_2RI6<0x00490000, uimm6>;
def ROTRI_D : ALU_2RI6<0x004d0000, uimm6>;

// Bit-manipulation Instructions for 64-bits
def CLO_D : ALU_2R<0x00002000>;
def CLZ_D : ALU_2R<0x00002400>;
def CTO_D : ALU_2R<0x00002800>;
def CTZ_D : ALU_2R<0x00002c00>;
def BYTEPICK_D : ALU_3RI3<0x000c0000, uimm3>;
def REVB_4H   : ALU_2R<0x00003400>;
def REVB_2W   : ALU_2R<0x00003800>;
def REVB_D    : ALU_2R<0x00003c00>;
def REVH_2W   : ALU_2R<0x00004000>;
def REVH_D    : ALU_2R<0x00004400>;
def BITREV_8B : ALU_2R<0x00004c00>;
def BITREV_D  : ALU_2R<0x00005400>;
let Constraints = "$rd = $dst" in {
def BSTRINS_D  : FmtBSTR_D<0x00800000, (outs GPR:$dst),
                           (ins GPR:$rd, GPR:$rj, uimm6:$msbd, uimm6:$lsbd),
                           "$rd, $rj, $msbd, $lsbd">;
}
def BSTRPICK_D : FmtBSTR_D<0x00c00000, (outs GPR:$rd),
                           (ins GPR:$rj, uimm6:$msbd, uimm6:$lsbd),
                           "$rd, $rj, $msbd, $lsbd">;

// Common Memory Access Instructions for 64-bits
def LD_WU : LOAD_2RI12<0x2a800000>;
def LD_D  : LOAD_2RI12<0x28c00000>;
def ST_D : STORE_2RI12<0x29c00000>;
def LDX_B  : LOAD_3R<0x38000000>;
def LDX_H  : LOAD_3R<0x38040000>;
def LDX_W  : LOAD_3R<0x38080000>;
def LDX_D  : LOAD_3R<0x380c0000>;
def LDX_BU : LOAD_3R<0x38200000>;
def LDX_HU : LOAD_3R<0x38240000>;
def LDX_WU : LOAD_3R<0x38280000>;
def STX_B : STORE_3R<0x38100000>;
def STX_H : STORE_3R<0x38140000>;
def STX_W : STORE_3R<0x38180000>;
def STX_D : STORE_3R<0x381c0000>;
def LDPTR_W : LOAD_2RI14<0x24000000>;
def LDPTR_D : LOAD_2RI14<0x26000000>;
def STPTR_W : STORE_2RI14<0x25000000>;
def STPTR_D : STORE_2RI14<0x27000000>;
let hasSideEffects = 0, mayLoad = 1, mayStore = 1 in
def PRELDX : FmtPRELDX<(outs), (ins uimm5:$imm5, GPR:$rj, GPR:$rk),
                       "$imm5, $rj, $rk">;

// Bound Check Memory Access Instructions
def LDGT_B : LOAD_3R<0x38780000>;
def LDGT_H : LOAD_3R<0x38788000>;
def LDGT_W : LOAD_3R<0x38790000>;
def LDGT_D : LOAD_3R<0x38798000>;
def LDLE_B : LOAD_3R<0x387a0000>;
def LDLE_H : LOAD_3R<0x387a8000>;
def LDLE_W : LOAD_3R<0x387b0000>;
def LDLE_D : LOAD_3R<0x387b8000>;
def STGT_B : STORE_3R<0x387c0000>;
def STGT_H : STORE_3R<0x387c8000>;
def STGT_W : STORE_3R<0x387d0000>;
def STGT_D : STORE_3R<0x387d8000>;
def STLE_B : STORE_3R<0x387e0000>;
def STLE_H : STORE_3R<0x387e8000>;
def STLE_W : STORE_3R<0x387f0000>;
def STLE_D : STORE_3R<0x387f8000>;

// Atomic Memory Access Instructions for 64-bits
def AMSWAP_B     : AM_3R<0x385c0000>;
def AMSWAP_H     : AM_3R<0x385c8000>;
def AMSWAP_W     : AM_3R<0x38600000>;
def AMSWAP_D     : AM_3R<0x38608000>;
def AMADD_B      : AM_3R<0x385d0000>;
def AMADD_H      : AM_3R<0x385d8000>;
def AMADD_W      : AM_3R<0x38610000>;
def AMADD_D      : AM_3R<0x38618000>;
def AMAND_W      : AM_3R<0x38620000>;
def AMAND_D      : AM_3R<0x38628000>;
def AMOR_W       : AM_3R<0x38630000>;
def AMOR_D       : AM_3R<0x38638000>;
def AMXOR_W      : AM_3R<0x38640000>;
def AMXOR_D      : AM_3R<0x38648000>;
def AMMAX_W      : AM_3R<0x38650000>;
def AMMAX_D      : AM_3R<0x38658000>;
def AMMIN_W      : AM_3R<0x38660000>;
def AMMIN_D      : AM_3R<0x38668000>;
def AMMAX_WU     : AM_3R<0x38670000>;
def AMMAX_DU     : AM_3R<0x38678000>;
def AMMIN_WU     : AM_3R<0x38680000>;
def AMMIN_DU     : AM_3R<0x38688000>;
def AMSWAP__DB_B : AM_3R<0x385e0000>;
def AMSWAP__DB_H : AM_3R<0x385e8000>;
def AMSWAP__DB_W : AM_3R<0x38690000>;
def AMSWAP__DB_D : AM_3R<0x38698000>;
def AMADD__DB_B  : AM_3R<0x385f0000>;
def AMADD__DB_H  : AM_3R<0x385f8000>;
def AMADD__DB_W  : AM_3R<0x386a0000>;
def AMADD__DB_D  : AM_3R<0x386a8000>;
def AMAND__DB_W  : AM_3R<0x386b0000>;
def AMAND__DB_D  : AM_3R<0x386b8000>;
def AMOR__DB_W   : AM_3R<0x386c0000>;
def AMOR__DB_D   : AM_3R<0x386c8000>;
def AMXOR__DB_W  : AM_3R<0x386d0000>;
def AMXOR__DB_D  : AM_3R<0x386d8000>;
def AMMAX__DB_W  : AM_3R<0x386e0000>;
def AMMAX__DB_D  : AM_3R<0x386e8000>;
def AMMIN__DB_W  : AM_3R<0x386f0000>;
def AMMIN__DB_D  : AM_3R<0x386f8000>;
def AMMAX__DB_WU : AM_3R<0x38700000>;
def AMMAX__DB_DU : AM_3R<0x38708000>;
def AMMIN__DB_WU : AM_3R<0x38710000>;
def AMMIN__DB_DU : AM_3R<0x38718000>;
def AMCAS_B     : AM_3R<0x38580000>;
def AMCAS_H     : AM_3R<0x38588000>;
def AMCAS_W     : AM_3R<0x38590000>;
def AMCAS_D     : AM_3R<0x38598000>;
def AMCAS__DB_B     : AM_3R<0x385a0000>;
def AMCAS__DB_H     : AM_3R<0x385a8000>;
def AMCAS__DB_W     : AM_3R<0x385b0000>;
def AMCAS__DB_D     : AM_3R<0x385b8000>;
def LL_D : LLBase<0x22000000>;
def SC_D : SCBase<0x23000000>;
def SC_Q : SCBase_128<0x38570000>;
def LLACQ_D : LLBase_ACQ<0x38578800>;
def SCREL_D : SCBase_REL<0x38578C00>;

// CRC Check Instructions
def CRC_W_B_W  : ALU_3R<0x00240000>;
def CRC_W_H_W  : ALU_3R<0x00248000>;
def CRC_W_W_W  : ALU_3R<0x00250000>;
def CRC_W_D_W  : ALU_3R<0x00258000>;
def CRCC_W_B_W : ALU_3R<0x00260000>;
def CRCC_W_H_W : ALU_3R<0x00268000>;
def CRCC_W_W_W : ALU_3R<0x00270000>;
def CRCC_W_D_W : ALU_3R<0x00278000>;

// Other Miscellaneous Instructions for 64-bits
def ASRTLE_D : FmtASRT<0x00010000, (outs), (ins GPR:$rj, GPR:$rk),
                       "$rj, $rk">;
def ASRTGT_D : FmtASRT<0x00018000, (outs), (ins GPR:$rj, GPR:$rk),
                       "$rj, $rk">;
def RDTIME_D : RDTIME_2R<0x00006800>;
} // Predicates = [IsLA64]

//===----------------------------------------------------------------------===//
// Pseudo-instructions and codegen patterns
//
// Naming convention: For 'generic' pattern classes, we use the naming
// convention PatTy1Ty2.
//===----------------------------------------------------------------------===//

/// Generic pattern classes

def assertsexti32 : PatFrag<(ops node:$src), (assertsext node:$src), [{
  return cast<VTSDNode>(N->getOperand(1))->getVT().bitsLE(MVT::i32);
}]>;
class PatGprGpr<SDPatternOperator OpNode, LAInst Inst>
    : Pat<(OpNode GPR:$rj, GPR:$rk), (Inst GPR:$rj, GPR:$rk)>;
class PatGprGpr_32<SDPatternOperator OpNode, LAInst Inst>
    : Pat<(sext_inreg (OpNode (assertsexti32 GPR:$rj), (assertsexti32 GPR:$rk)), i32), (Inst GPR:$rj, GPR:$rk)>;
class PatGpr<SDPatternOperator OpNode, LAInst Inst>
    : Pat<(OpNode GPR:$rj), (Inst GPR:$rj)>;

class PatGprImm<SDPatternOperator OpNode, LAInst Inst, Operand ImmOpnd>
    : Pat<(OpNode GPR:$rj, ImmOpnd:$imm),
          (Inst GPR:$rj, ImmOpnd:$imm)>;
class PatGprImm_32<SDPatternOperator OpNode, LAInst Inst, Operand ImmOpnd>
    : Pat<(sext_inreg (OpNode GPR:$rj, ImmOpnd:$imm), i32),
          (Inst GPR:$rj, ImmOpnd:$imm)>;

/// Predicates
def AddLike: PatFrags<(ops node:$A, node:$B),
                      [(add node:$A, node:$B), (or node:$A, node:$B)], [{
    return CurDAG->isBaseWithConstantOffset(SDValue(N, 0));
}]>;

/// Simple arithmetic operations

// Match both a plain shift and one where the shift amount is masked (this is
// typically introduced when the legalizer promotes the shift amount and
// zero-extends it). For LoongArch, the mask is unnecessary as shifts in the
// base ISA only read the least significant 5 bits (LA32) or 6 bits (LA64).
def shiftMaskGRLen
    : ComplexPattern<GRLenVT, 1, "selectShiftMaskGRLen", [], [], 0>;
def shiftMask32 : ComplexPattern<i64, 1, "selectShiftMask32", [], [], 0>;

def sexti32 : ComplexPattern<i64, 1, "selectSExti32">;
def zexti32 : ComplexPattern<i64, 1, "selectZExti32">;

class shiftop<SDPatternOperator operator>
    : PatFrag<(ops node:$val, node:$count),
              (operator node:$val, (GRLenVT (shiftMaskGRLen node:$count)))>;
class shiftopw<SDPatternOperator operator>
    : PatFrag<(ops node:$val, node:$count),
              (operator node:$val, (i64 (shiftMask32 node:$count)))>;

def mul_const_oneuse : PatFrag<(ops node:$A, node:$B),
                               (mul node:$A, node:$B), [{
  if (auto *N1C = dyn_cast<ConstantSDNode>(N->getOperand(1)))
    return N1C->hasOneUse();
  return false;
}]>;

let Predicates = [IsLA32] in {
def : PatGprGpr<add, ADD_W>;
def : PatGprImm<add, ADDI_W, simm12>;
def : PatGprGpr<sub, SUB_W>;
def : PatGprGpr<sdiv, DIV_W>;
def : PatGprGpr<udiv, DIV_WU>;
def : PatGprGpr<srem, MOD_W>;
def : PatGprGpr<urem, MOD_WU>;
def : PatGprGpr<mul, MUL_W>;
def : PatGprGpr<mulhs, MULH_W>;
def : PatGprGpr<mulhu, MULH_WU>;
def : PatGprGpr<rotr, ROTR_W>;
def : PatGprImm<rotr, ROTRI_W, uimm5>;

foreach Idx = 1...3 in {
  defvar ShamtA = !mul(8, Idx);
  defvar ShamtB = !mul(8, !sub(4, Idx));
  def : Pat<(or (shl GPR:$rk, (i32 ShamtA)), (srl GPR:$rj, (i32 ShamtB))),
            (BYTEPICK_W GPR:$rj, GPR:$rk, Idx)>;
}
} // Predicates = [IsLA32]

let Predicates = [IsLA64] in {
def : PatGprGpr<add, ADD_D>;
def : PatGprImm<add, ADDI_D, simm12>;
def : PatGprGpr<sub, SUB_D>;
def : PatGprGpr<sdiv, DIV_D>;
def : PatGprGpr_32<sdiv, DIV_W>;
def : PatGprGpr<udiv, DIV_DU>;
def : PatGprGpr<loongarch_div_wu, DIV_WU>;
def : PatGprGpr<srem, MOD_D>;
def : PatGprGpr_32<srem, MOD_W>;
def : PatGprGpr<urem, MOD_DU>;
def : PatGprGpr<loongarch_mod_wu, MOD_WU>;
def : PatGprGpr<rotr, ROTR_D>;
def : PatGprGpr<loongarch_rotr_w, ROTR_W>;
def : PatGprImm<rotr, ROTRI_D, uimm6>;
def : PatGprImm_32<rotr, ROTRI_W, uimm5>;
def : PatGprImm<loongarch_rotr_w, ROTRI_W, uimm5>;
// TODO: Select "_W[U]" instructions for i32xi32 if only lower 32 bits of the
// product are used.
def : PatGprGpr<mul, MUL_D>;
def : PatGprGpr<mulhs, MULH_D>;
def : PatGprGpr<mulhu, MULH_DU>;
// Select MULW_D_W for calculating the full 64 bits product of i32xi32 signed
// multiplication.
def : Pat<(i64 (mul (sext_inreg GPR:$rj, i32), (sext_inreg GPR:$rk, i32))),
          (MULW_D_W GPR:$rj, GPR:$rk)>;
// Select MULW_D_WU for calculating the full 64 bits product of i32xi32
// unsigned multiplication.
def : Pat<(i64 (mul (loongarch_bstrpick GPR:$rj, (i64 31), (i64 0)),
                    (loongarch_bstrpick GPR:$rk, (i64 31), (i64 0)))),
          (MULW_D_WU GPR:$rj, GPR:$rk)>;

def : Pat<(add GPR:$rj, simm16_lsl16:$imm),
          (ADDU16I_D GPR:$rj, (HI16 $imm))>;
def : Pat<(add GPR:$rj, simm32_hi16_lo12:$imm),
          (ADDI_D (ADDU16I_D GPR:$rj, (HI16ForAddu16idAddiPair $imm)),
                  (LO12 $imm))>;
def : Pat<(sext_inreg (add GPR:$rj, simm32_hi16_lo12:$imm), i32),
          (ADDI_W (ADDU16I_D GPR:$rj, (HI16ForAddu16idAddiPair $imm)),
                  (LO12 $imm))>;

let Predicates = [IsLA32] in {
def : Pat<(add GPR:$rj, (AddiPair:$im)),
          (ADDI_W (ADDI_W GPR:$rj, (AddiPairImmLarge AddiPair:$im)),
                  (AddiPairImmSmall AddiPair:$im))>;
} // Predicates = [IsLA32]

let Predicates = [IsLA64] in {
def : Pat<(add GPR:$rj, (AddiPair:$im)),
          (ADDI_D (ADDI_D GPR:$rj, (AddiPairImmLarge AddiPair:$im)),
                  (AddiPairImmSmall AddiPair:$im))>;
def : Pat<(sext_inreg (add GPR:$rj, (AddiPair:$im)), i32),
          (ADDI_W (ADDI_W GPR:$rj, (AddiPairImmLarge AddiPair:$im)),
                  (AddiPairImmSmall AddiPair:$im))>;
} // Predicates = [IsLA64]

let Predicates = [IsLA32] in {
foreach Idx0 = 1...4 in {
  foreach Idx1 = 1...4 in {
    defvar CImm = !add(1, !shl(!add(1, !shl(1, Idx0)), Idx1));
    def : Pat<(mul_const_oneuse GPR:$r, (i32 CImm)),
              (ALSL_W (ALSL_W GPR:$r, GPR:$r, (i32 Idx0)),
                      GPR:$r, (i32 Idx1))>;
  }
}
foreach Idx0 = 1...4 in {
  foreach Idx1 = 1...4 in {
    defvar Cb = !add(1, !shl(1, Idx0));
    defvar CImm = !add(Cb, !shl(Cb, Idx1));
    def : Pat<(mul_const_oneuse GPR:$r, (i32 CImm)),
              (ALSL_W (ALSL_W GPR:$r, GPR:$r, (i32 Idx0)),
                      (ALSL_W GPR:$r, GPR:$r, (i32 Idx0)), (i32 Idx1))>;
  }
}
} // Predicates = [IsLA32]

let Predicates = [IsLA64] in {
foreach Idx0 = 1...4 in {
  foreach Idx1 = 1...4 in {
    defvar CImm = !add(1, !shl(!add(1, !shl(1, Idx0)), Idx1));
    def : Pat<(sext_inreg (mul_const_oneuse GPR:$r, (i64 CImm)), i32),
              (ALSL_W (ALSL_W GPR:$r, GPR:$r, (i64 Idx0)),
                      GPR:$r, (i64 Idx1))>;
    def : Pat<(mul_const_oneuse GPR:$r, (i64 CImm)),
              (ALSL_D (ALSL_D GPR:$r, GPR:$r, (i64 Idx0)),
                      GPR:$r, (i64 Idx1))>;
  }
}
foreach Idx0 = 1...4 in {
  foreach Idx1 = 1...4 in {
    defvar Cb = !add(1, !shl(1, Idx0));
    defvar CImm = !add(Cb, !shl(Cb, Idx1));
    def : Pat<(sext_inreg (mul_const_oneuse GPR:$r, (i64 CImm)), i32),
              (ALSL_W (ALSL_W GPR:$r, GPR:$r, (i64 Idx0)),
                      (ALSL_W GPR:$r, GPR:$r, (i64 Idx0)), (i64 Idx1))>;
    def : Pat<(mul_const_oneuse GPR:$r, (i64 CImm)),
              (ALSL_D (ALSL_D GPR:$r, GPR:$r, (i64 Idx0)),
                      (ALSL_D GPR:$r, GPR:$r, (i64 Idx0)), (i64 Idx1))>;
  }
}
} // Predicates = [IsLA64]

let Predicates = [IsLA32] in {
def : Pat<(mul GPR:$rj, (AlslSlliImm:$im)),
          (SLLI_W (ALSL_W GPR:$rj, GPR:$rj, (AlslSlliImmI0 AlslSlliImm:$im)),
                  (AlslSlliImmI1 AlslSlliImm:$im))>;
} // Predicates = [IsLA32]

let Predicates = [IsLA64] in {
def : Pat<(sext_inreg (mul GPR:$rj, (AlslSlliImm:$im)), i32),
          (SLLI_W (ALSL_W GPR:$rj, GPR:$rj, (AlslSlliImmI0 AlslSlliImm:$im)),
                  (AlslSlliImmI1 AlslSlliImm:$im))>;
def : Pat<(mul GPR:$rj, (AlslSlliImm:$im)),
          (SLLI_D (ALSL_D GPR:$rj, GPR:$rj, (AlslSlliImmI0 AlslSlliImm:$im)),
                  (AlslSlliImmI1 AlslSlliImm:$im))>;
} // Predicates = [IsLA64]

foreach Idx = 1...7 in {
  defvar ShamtA = !mul(8, Idx);
  defvar ShamtB = !mul(8, !sub(8, Idx));
  def : Pat<(or (shl GPR:$rk, (i64 ShamtA)), (srl GPR:$rj, (i64 ShamtB))),
            (BYTEPICK_D GPR:$rj, GPR:$rk, Idx)>;
}

foreach Idx = 1...3 in {
  defvar ShamtA = !mul(8, Idx);
  defvar ShamtB = !mul(8, !sub(4, Idx));
  // NOTE: the srl node would already be transformed into a loongarch_bstrpick
  // by the time this pattern gets to execute, hence the weird construction.
  def : Pat<(sext_inreg (or (shl GPR:$rk, (i64 ShamtA)),
                            (loongarch_bstrpick GPR:$rj, (i64 31),
                                                         (i64 ShamtB))), i32),
            (BYTEPICK_W GPR:$rj, GPR:$rk, Idx)>;
}
} // Predicates = [IsLA64]

def : PatGprGpr<and, AND>;
def : PatGprImm<and, ANDI, uimm12>;
def : PatGprGpr<or, OR>;
def : PatGprImm<or, ORI, uimm12>;
def : PatGprGpr<xor, XOR>;
def : PatGprImm<xor, XORI, uimm12>;
def : Pat<(not GPR:$rj), (NOR GPR:$rj, R0)>;
def : Pat<(not (or GPR:$rj, GPR:$rk)), (NOR GPR:$rj, GPR:$rk)>;
def : Pat<(or GPR:$rj, (not GPR:$rk)), (ORN GPR:$rj, GPR:$rk)>;
def : Pat<(and GPR:$rj, (not GPR:$rk)), (ANDN GPR:$rj, GPR:$rk)>;

let Predicates = [IsLA32] in {
def : Pat<(and GPR:$rj, BstrinsImm:$imm),
          (BSTRINS_W GPR:$rj, R0, (BstrinsMsb BstrinsImm:$imm),
                     (BstrinsLsb BstrinsImm:$imm))>;
} // Predicates = [IsLA32]

let Predicates = [IsLA64] in {
def : Pat<(and GPR:$rj, BstrinsImm:$imm),
          (BSTRINS_D GPR:$rj, R0, (BstrinsMsb BstrinsImm:$imm),
                     (BstrinsLsb BstrinsImm:$imm))>;
} // Predicates = [IsLA64]

/// Traps

// We lower `trap` to `amswap.w rd:$r0, rk:$r1, rj:$r0`, as this is guaranteed
// to trap with an INE (non-existent on LA32, explicitly documented to INE on
// LA64). And the resulting signal is different from `debugtrap` like on some
// other existing ports so programs/porters might have an easier time.
def PseudoUNIMP : Pseudo<(outs), (ins), [(trap)]>,
                  PseudoInstExpansion<(AMSWAP_W R0, R1, R0)>;

// We lower `debugtrap` to `break 0`, as this is guaranteed to exist and work,
// even for LA32 Primary. Also, because so far the ISA does not provide a
// specific trap instruction/kind exclusively for alerting the debugger,
// every other project uses the generic immediate of 0 for this.
def : Pat<(debugtrap), (BREAK 0)>;

/// Bit counting operations

let Predicates = [IsLA64] in {
def : PatGpr<ctlz, CLZ_D>;
def : PatGpr<cttz, CTZ_D>;
def : Pat<(ctlz (not GPR:$rj)), (CLO_D GPR:$rj)>;
def : Pat<(cttz (not GPR:$rj)), (CTO_D GPR:$rj)>;
def : PatGpr<loongarch_clzw, CLZ_W>;
def : PatGpr<loongarch_ctzw, CTZ_W>;
def : Pat<(loongarch_clzw (not GPR:$rj)), (CLO_W GPR:$rj)>;
def : Pat<(loongarch_ctzw (not GPR:$rj)), (CTO_W GPR:$rj)>;
} // Predicates = [IsLA64]

let Predicates = [IsLA32] in {
def : PatGpr<ctlz, CLZ_W>;
def : PatGpr<cttz, CTZ_W>;
def : Pat<(ctlz (not GPR:$rj)), (CLO_W GPR:$rj)>;
def : Pat<(cttz (not GPR:$rj)), (CTO_W GPR:$rj)>;
} // Predicates = [IsLA32]

/// FrameIndex calculations
let Predicates = [IsLA32] in {
def : Pat<(AddLike (i32 BaseAddr:$rj), simm12:$imm12),
          (ADDI_W (i32 BaseAddr:$rj), simm12:$imm12)>;
} // Predicates = [IsLA32]
let Predicates = [IsLA64] in {
def : Pat<(AddLike (i64 BaseAddr:$rj), simm12:$imm12),
          (ADDI_D (i64 BaseAddr:$rj), simm12:$imm12)>;
} // Predicates = [IsLA64]

/// Shifted addition
let Predicates = [IsLA32] in {
def : Pat<(add GPR:$rk, (shl GPR:$rj, uimm2_plus1:$imm2)),
          (ALSL_W GPR:$rj, GPR:$rk, uimm2_plus1:$imm2)>;
} // Predicates = [IsLA32]
let Predicates = [IsLA64] in {
def : Pat<(add GPR:$rk, (shl GPR:$rj, uimm2_plus1:$imm2)),
          (ALSL_D GPR:$rj, GPR:$rk, uimm2_plus1:$imm2)>;
def : Pat<(sext_inreg (add GPR:$rk, (shl GPR:$rj, uimm2_plus1:$imm2)), i32),
          (ALSL_W GPR:$rj, GPR:$rk, uimm2_plus1:$imm2)>;
def : Pat<(loongarch_bstrpick (add GPR:$rk, (shl GPR:$rj, uimm2_plus1:$imm2)),
                              (i64 31), (i64 0)),
          (ALSL_WU GPR:$rj, GPR:$rk, uimm2_plus1:$imm2)>;
} // Predicates = [IsLA64]

/// Shift

let Predicates = [IsLA32] in {
def : PatGprGpr<shiftop<shl>, SLL_W>;
def : PatGprGpr<shiftop<sra>, SRA_W>;
def : PatGprGpr<shiftop<srl>, SRL_W>;
def : PatGprImm<shl, SLLI_W, uimm5>;
def : PatGprImm<sra, SRAI_W, uimm5>;
def : PatGprImm<srl, SRLI_W, uimm5>;
} // Predicates = [IsLA32]

let Predicates = [IsLA64] in {
def : PatGprGpr<shiftopw<loongarch_sll_w>, SLL_W>;
def : PatGprGpr<shiftopw<loongarch_sra_w>, SRA_W>;
def : PatGprGpr<shiftopw<loongarch_srl_w>, SRL_W>;
def : PatGprGpr<shiftop<shl>, SLL_D>;
def : PatGprGpr<shiftop<sra>, SRA_D>;
def : PatGprGpr<shiftop<srl>, SRL_D>;
def : PatGprImm<shl, SLLI_D, uimm6>;
def : PatGprImm<sra, SRAI_D, uimm6>;
def : PatGprImm<srl, SRLI_D, uimm6>;
} // Predicates = [IsLA64]

/// sext and zext

def : Pat<(sext_inreg GPR:$rj, i8), (EXT_W_B GPR:$rj)>;
def : Pat<(sext_inreg GPR:$rj, i16), (EXT_W_H GPR:$rj)>;

let Predicates = [IsLA64] in {
def : Pat<(sext_inreg GPR:$rj, i32), (ADDI_W GPR:$rj, 0)>;
} // Predicates = [IsLA64]

/// Setcc

def : PatGprGpr<setlt, SLT>;
def : PatGprImm<setlt, SLTI, simm12>;
def : PatGprGpr<setult, SLTU>;
def : PatGprImm<setult, SLTUI, simm12>;

// Define pattern expansions for setcc operations that aren't directly
// handled by a LoongArch instruction.
def : Pat<(seteq GPR:$rj, 0), (SLTUI GPR:$rj, 1)>;
def : Pat<(seteq GPR:$rj, GPR:$rk), (SLTUI (XOR GPR:$rj, GPR:$rk), 1)>;
let Predicates = [IsLA32] in {
def : Pat<(seteq GPR:$rj, simm12_plus1:$imm12),
          (SLTUI (ADDI_W GPR:$rj, (NegImm simm12_plus1:$imm12)), 1)>;
} // Predicates = [IsLA32]
let Predicates = [IsLA64] in {
def : Pat<(seteq GPR:$rj, simm12_plus1:$imm12),
          (SLTUI (ADDI_D GPR:$rj, (NegImm simm12_plus1:$imm12)), 1)>;
} // Predicates = [IsLA64]
def : Pat<(setne GPR:$rj, 0), (SLTU R0, GPR:$rj)>;
def : Pat<(setne GPR:$rj, GPR:$rk), (SLTU R0, (XOR GPR:$rj, GPR:$rk))>;
let Predicates = [IsLA32] in {
def : Pat<(setne GPR:$rj, simm12_plus1:$imm12),
          (SLTU R0, (ADDI_W GPR:$rj, (NegImm simm12_plus1:$imm12)))>;
} // Predicates = [IsLA32]
let Predicates = [IsLA64] in {
def : Pat<(setne GPR:$rj, simm12_plus1:$imm12),
          (SLTU R0, (ADDI_D GPR:$rj, (NegImm simm12_plus1:$imm12)))>;
} // Predicates = [IsLA64]
def : Pat<(setugt GPR:$rj, GPR:$rk), (SLTU GPR:$rk, GPR:$rj)>;
def : Pat<(setuge GPR:$rj, GPR:$rk), (XORI (SLTU GPR:$rj, GPR:$rk), 1)>;
def : Pat<(setule GPR:$rj, GPR:$rk), (XORI (SLTU GPR:$rk, GPR:$rj), 1)>;
def : Pat<(setgt GPR:$rj, GPR:$rk), (SLT GPR:$rk, GPR:$rj)>;
def : Pat<(setge GPR:$rj, GPR:$rk), (XORI (SLT GPR:$rj, GPR:$rk), 1)>;
def : Pat<(setle GPR:$rj, GPR:$rk), (XORI (SLT GPR:$rk, GPR:$rj), 1)>;

/// Select

def : Pat<(select GPR:$cond, GPR:$t, 0), (MASKEQZ GPR:$t, GPR:$cond)>;
def : Pat<(select GPR:$cond, 0, GPR:$f), (MASKNEZ GPR:$f, GPR:$cond)>;
def : Pat<(select GPR:$cond, GPR:$t, GPR:$f),
          (OR (MASKEQZ GPR:$t, GPR:$cond), (MASKNEZ GPR:$f, GPR:$cond))>;

/// Branches and jumps

class BccPat<PatFrag CondOp, LAInst Inst>
    : Pat<(brcond (GRLenVT (CondOp GPR:$rj, GPR:$rd)), bb:$imm16),
          (Inst GPR:$rj, GPR:$rd, bb:$imm16)>;

def : BccPat<seteq, BEQ>;
def : BccPat<setne, BNE>;
def : BccPat<setlt, BLT>;
def : BccPat<setge, BGE>;
def : BccPat<setult, BLTU>;
def : BccPat<setuge, BGEU>;

class BccSwapPat<PatFrag CondOp, LAInst InstBcc>
    : Pat<(brcond (GRLenVT (CondOp GPR:$rd, GPR:$rj)), bb:$imm16),
          (InstBcc GPR:$rj, GPR:$rd, bb:$imm16)>;

// Condition codes that don't have matching LoongArch branch instructions, but
// are trivially supported by swapping the two input operands.
def : BccSwapPat<setgt, BLT>;
def : BccSwapPat<setle, BGE>;
def : BccSwapPat<setugt, BLTU>;
def : BccSwapPat<setule, BGEU>;

// An extra pattern is needed for a brcond without a setcc (i.e. where the
// condition was calculated elsewhere).
def : Pat<(brcond GPR:$rj, bb:$imm21), (BNEZ GPR:$rj, bb:$imm21)>;

def : Pat<(brcond (GRLenVT (seteq GPR:$rj, 0)), bb:$imm21),
          (BEQZ GPR:$rj, bb:$imm21)>;
def : Pat<(brcond (GRLenVT (setne GPR:$rj, 0)), bb:$imm21),
          (BNEZ GPR:$rj, bb:$imm21)>;

let isBarrier = 1, isBranch = 1, isTerminator = 1 in
def PseudoBR : Pseudo<(outs), (ins simm26_b:$imm26), [(br bb:$imm26)]>,
               PseudoInstExpansion<(B simm26_b:$imm26)>;

let isBarrier = 1, isBranch = 1, isIndirectBranch = 1, isTerminator = 1 in
def PseudoBRIND : Pseudo<(outs), (ins GPR:$rj, simm16_lsl2:$imm16)>,
                  PseudoInstExpansion<(JIRL R0, GPR:$rj, simm16_lsl2:$imm16)>;

def : Pat<(brind GPR:$rj), (PseudoBRIND GPR:$rj, 0)>;
def : Pat<(brind (add GPR:$rj, simm16_lsl2:$imm16)),
          (PseudoBRIND GPR:$rj, simm16_lsl2:$imm16)>;

// Function call with 'Small' code model.
let isCall = 1, Defs = [R1] in
def PseudoCALL : Pseudo<(outs), (ins bare_symbol:$func)>;

def : Pat<(loongarch_call tglobaladdr:$func), (PseudoCALL tglobaladdr:$func)>;
def : Pat<(loongarch_call texternalsym:$func), (PseudoCALL texternalsym:$func)>;

// Function call with 'Medium' code model.
let isCall = 1, Defs = [R1, R20], Size = 8 in
def PseudoCALL_MEDIUM : Pseudo<(outs), (ins bare_symbol:$func)>;

let Predicates = [IsLA64] in {
def : Pat<(loongarch_call_medium tglobaladdr:$func),
          (PseudoCALL_MEDIUM tglobaladdr:$func)>;
def : Pat<(loongarch_call_medium texternalsym:$func),
          (PseudoCALL_MEDIUM texternalsym:$func)>;
} // Predicates = [IsLA64]

// Function call with 'Large' code model.
let isCall = 1, Defs = [R1, R20], Size = 24 in
def PseudoCALL_LARGE: Pseudo<(outs), (ins bare_symbol:$func)>;

let Predicates = [IsLA64] in {
def : Pat<(loongarch_call_large tglobaladdr:$func),
          (PseudoCALL_LARGE tglobaladdr:$func)>;
def : Pat<(loongarch_call_large texternalsym:$func),
          (PseudoCALL_LARGE texternalsym:$func)>;
} // Predicates = [IsLA64]

let isCall = 1, Defs = [R1] in
def PseudoCALLIndirect : Pseudo<(outs), (ins GPR:$rj),
                                [(loongarch_call GPR:$rj)]>,
                         PseudoInstExpansion<(JIRL R1, GPR:$rj, 0)>;
let Predicates = [IsLA64] in {
def : Pat<(loongarch_call_medium GPR:$rj), (PseudoCALLIndirect GPR:$rj)>;
def : Pat<(loongarch_call_large GPR:$rj), (PseudoCALLIndirect GPR:$rj)>;
}

let isCall = 1, hasSideEffects = 0, mayStore = 0, mayLoad = 0, Defs = [R1] in
def PseudoJIRL_CALL : Pseudo<(outs), (ins GPR:$rj, simm16_lsl2:$imm16)>,
                      PseudoInstExpansion<(JIRL R1, GPR:$rj,
                                           simm16_lsl2:$imm16)>;

let isBarrier = 1, isReturn = 1, isTerminator = 1 in
def PseudoRET : Pseudo<(outs), (ins), [(loongarch_ret)]>,
                PseudoInstExpansion<(JIRL R0, R1, 0)>;

// Tail call with 'Small' code model.
let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1, Uses = [R3] in
def PseudoTAIL : Pseudo<(outs), (ins bare_symbol:$dst)>;

def : Pat<(loongarch_tail (iPTR tglobaladdr:$dst)),
          (PseudoTAIL tglobaladdr:$dst)>;
def : Pat<(loongarch_tail (iPTR texternalsym:$dst)),
          (PseudoTAIL texternalsym:$dst)>;

// Tail call with 'Medium' code model.
let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1,
    Uses = [R3], Defs = [R20], Size = 8 in
def PseudoTAIL_MEDIUM : Pseudo<(outs), (ins bare_symbol:$dst)>;

let Predicates = [IsLA64] in {
def : Pat<(loongarch_tail_medium (iPTR tglobaladdr:$dst)),
          (PseudoTAIL_MEDIUM tglobaladdr:$dst)>;
def : Pat<(loongarch_tail_medium (iPTR texternalsym:$dst)),
          (PseudoTAIL_MEDIUM texternalsym:$dst)>;
} // Predicates = [IsLA64]

// Tail call with 'Large' code model.
let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1,
    Uses = [R3], Defs = [R19, R20], Size = 24 in
def PseudoTAIL_LARGE : Pseudo<(outs), (ins bare_symbol:$dst)>;

let Predicates = [IsLA64] in {
def : Pat<(loongarch_tail_large (iPTR tglobaladdr:$dst)),
          (PseudoTAIL_LARGE tglobaladdr:$dst)>;
def : Pat<(loongarch_tail_large (iPTR texternalsym:$dst)),
          (PseudoTAIL_LARGE texternalsym:$dst)>;
} // Predicates = [IsLA64]

let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1, Uses = [R3] in
def PseudoTAILIndirect : Pseudo<(outs), (ins GPRT:$rj),
                                [(loongarch_tail GPRT:$rj)]>,
                         PseudoInstExpansion<(JIRL R0, GPR:$rj, 0)>;
let Predicates = [IsLA64] in {
def : Pat<(loongarch_tail_medium GPR:$rj), (PseudoTAILIndirect GPR:$rj)>;
def : Pat<(loongarch_tail_large GPR:$rj), (PseudoTAILIndirect GPR:$rj)>;
}

let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1,
    hasSideEffects = 0, mayStore = 0, mayLoad = 0, Uses = [R3] in
def PseudoB_TAIL : Pseudo<(outs), (ins simm26_b:$imm26)>,
                   PseudoInstExpansion<(B simm26_b:$imm26)>;

let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1,
    hasSideEffects = 0, mayStore = 0, mayLoad = 0, Uses = [R3] in
def PseudoJIRL_TAIL : Pseudo<(outs), (ins GPR:$rj, simm16_lsl2:$imm16)>,
                      PseudoInstExpansion<(JIRL R0, GPR:$rj,
                                           simm16_lsl2:$imm16)>;

/// call36/taill36 macro instructions
let isCall = 1, isBarrier = 1, isCodeGenOnly = 0, isAsmParserOnly = 1,
    Defs = [R1], Size = 8, hasSideEffects = 0, mayStore = 0, mayLoad = 0 in
def PseudoCALL36 : Pseudo<(outs), (ins bare_symbol:$dst), [],
                          "call36", "$dst">,
                   Requires<[IsLA64]>;
let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1, Uses = [R3],
    isCodeGenOnly = 0, isAsmParserOnly = 1, Size = 8, hasSideEffects = 0,
    mayStore = 0, mayLoad = 0 in
def PseudoTAIL36 : Pseudo<(outs), (ins GPR:$tmp, bare_symbol:$dst), [],
                          "tail36", "$tmp, $dst">,
                   Requires<[IsLA64]>;

// This is a special case of the ADD_W/D instruction used to facilitate the use
// of a fourth operand to emit a relocation on a symbol relating to this
// instruction. The relocation does not affect any bits of the instruction itself
// but is used as a hint to the linker.
let hasSideEffects = 0, mayLoad = 0, mayStore = 0, isCodeGenOnly = 0 in {
def PseudoAddTPRel_W : Pseudo<(outs GPR:$rd),
                              (ins GPR:$rj, GPR:$rk, tprel_add_symbol:$sym), [],
                              "add.w", "$rd, $rj, $rk, $sym">,
                              Requires<[IsLA32]>;
def PseudoAddTPRel_D : Pseudo<(outs GPR:$rd),
                              (ins GPR:$rj, GPR:$rk, tprel_add_symbol:$sym), [],
                              "add.d", "$rd, $rj, $rk, $sym">,
                              Requires<[IsLA64]>;
}

/// Load address (la*) macro instructions.

// Define isCodeGenOnly = 0 to expose them to tablegened assembly parser.
let hasSideEffects = 0, mayLoad = 0, mayStore = 0, isCodeGenOnly = 0,
    isAsmParserOnly = 1 in {
def PseudoLA_ABS : Pseudo<(outs GPR:$dst), (ins bare_symbol:$src), [],
                          "la.abs", "$dst, $src">;
def PseudoLA_ABS_LARGE : Pseudo<(outs GPR:$dst),
                                (ins GPR:$tmp, bare_symbol:$src), [],
                                "la.abs", "$dst, $src">;
def PseudoLA_PCREL : Pseudo<(outs GPR:$dst), (ins bare_symbol:$src), [],
                            "la.pcrel", "$dst, $src">;
def PseudoLA_TLS_LD : Pseudo<(outs GPR:$dst), (ins bare_symbol:$src), [],
                             "la.tls.ld", "$dst, $src">;
def PseudoLA_TLS_GD : Pseudo<(outs GPR:$dst), (ins bare_symbol:$src), [],
                             "la.tls.gd", "$dst, $src">;
let Defs = [R20], Size = 20 in {
def PseudoLA_PCREL_LARGE : Pseudo<(outs GPR:$dst),
                                  (ins GPR:$tmp, bare_symbol:$src), [],
                                  "la.pcrel", "$dst, $tmp, $src">,
                           Requires<[IsLA64]>;
def PseudoLA_TLS_LE : Pseudo<(outs GPR:$dst), (ins bare_symbol:$src), [],
                             "la.tls.le", "$dst, $src">;
def PseudoLA_TLS_LD_LARGE : Pseudo<(outs GPR:$dst),
                                   (ins GPR:$tmp, bare_symbol:$src), [],
                                   "la.tls.ld", "$dst, $tmp, $src">,
                            Requires<[IsLA64]>;
def PseudoLA_TLS_GD_LARGE : Pseudo<(outs GPR:$dst),
                                   (ins GPR:$tmp, bare_symbol:$src), [],
                                   "la.tls.gd", "$dst, $tmp, $src">,
                            Requires<[IsLA64]>;
} // Defs = [R20], Size = 20
}
let hasSideEffects = 0, mayLoad = 1, mayStore = 0, isCodeGenOnly = 0,
    isAsmParserOnly = 1 in {
def PseudoLA_GOT : Pseudo<(outs GPR:$dst), (ins bare_symbol:$src), [],
                          "la.got", "$dst, $src">;
def PseudoLA_TLS_IE : Pseudo<(outs GPR:$dst), (ins bare_symbol:$src), [],
                             "la.tls.ie", "$dst, $src">;
let Defs = [R20], Size = 20 in {
def PseudoLA_GOT_LARGE : Pseudo<(outs GPR:$dst),
                                (ins GPR:$tmp, bare_symbol:$src), [],
                                "la.got", "$dst, $tmp, $src">,
                         Requires<[IsLA64]>;
def PseudoLA_TLS_IE_LARGE : Pseudo<(outs GPR:$dst),
                                   (ins GPR:$tmp, bare_symbol:$src), [],
                                   "la.tls.ie", "$dst, $tmp, $src">,
                            Requires<[IsLA64]>;
} // Defs = [R20], Size = 20
}

// Used for expand PseudoLA_TLS_DESC_* instructions.
let isCall = 1, isBarrier = 1, hasSideEffects = 0, mayStore = 0, mayLoad = 0,
    Defs = [R4], Uses = [R4] in
def PseudoDESC_CALL : Pseudo<(outs GPR:$rd), (ins GPR:$rj, simm16_lsl2:$imm16)>,
                      PseudoInstExpansion<(JIRL GPR:$rd, GPR:$rj,
                                           simm16_lsl2:$imm16)>;

// TLSDESC
let hasSideEffects = 0, mayLoad = 1, mayStore = 0, isCodeGenOnly = 0,
    isAsmParserOnly = 1, Defs = [R1] in {
def PseudoLA_TLS_DESC_ABS : Pseudo<(outs GPR:$dst), (ins bare_symbol:$src),
                                   [], "la.tls.desc", "$dst, $src">,
                                   Requires<[IsLA32, HasLaGlobalWithAbs]>;
def PseudoLA_TLS_DESC_ABS_LARGE : Pseudo<(outs GPR:$dst),
                                         (ins GPR:$tmp, bare_symbol:$src), [],
                                         "la.tls.desc", "$dst, $src">,
                                  Requires<[IsLA64, HasLaGlobalWithAbs]>;
def PseudoLA_TLS_DESC_PC : Pseudo<(outs GPR:$dst), (ins bare_symbol:$src), [],
                                  "la.tls.desc", "$dst, $src">;
}

let isCall = 1, isBarrier = 1, hasSideEffects = 0, mayStore = 0, mayLoad = 0,
    isCodeGenOnly = 0, isAsmParserOnly = 1, Defs = [R1, R4, R20], Size = 32 in
def PseudoLA_TLS_DESC_PC_LARGE : Pseudo<(outs GPR:$dst),
                                        (ins GPR:$tmp, bare_symbol:$src), [],
                                        "la.tls.desc", "$dst, $tmp, $src">,
                                 Requires<[IsLA64]>;

// Load address inst alias: "la", "la.global" and "la.local".
// Default:
//     la = la.global = la.got
//     la.local = la.pcrel
// With feature "+la-global-with-pcrel":
//     la = la.global = la.pcrel
// With feature "+la-global-with-abs":
//     la = la.global = la.abs
// With feature "+la-local-with-abs":
//     la.local = la.abs
// With features "+la-global-with-pcrel,+la-global-with-abs"(disorder):
//     la = la.global = la.pcrel
// Note: To keep consistent with gnu-as behavior, the "la" can only have one
//       register operand.
def : InstAlias<"la $dst, $src", (PseudoLA_GOT GPR:$dst, bare_symbol:$src)>;
def : InstAlias<"la.global $dst, $src",
                (PseudoLA_GOT GPR:$dst, bare_symbol:$src)>;
def : InstAlias<"la.global $dst, $tmp, $src",
                (PseudoLA_GOT_LARGE GPR:$dst, GPR:$tmp, bare_symbol:$src)>;
def : InstAlias<"la.local $dst, $src",
                (PseudoLA_PCREL GPR:$dst, bare_symbol:$src)>;
def : InstAlias<"la.local $dst, $tmp, $src",
                (PseudoLA_PCREL_LARGE GPR:$dst, GPR:$tmp, bare_symbol:$src)>;

// Note: Keep HasLaGlobalWithPcrel before HasLaGlobalWithAbs to ensure
// "la-global-with-pcrel" takes effect when bose "la-global-with-pcrel" and
// "la-global-with-abs" are enabled.
let Predicates = [HasLaGlobalWithPcrel] in {
def : InstAlias<"la $dst, $src", (PseudoLA_PCREL GPR:$dst, bare_symbol:$src)>;
def : InstAlias<"la.global $dst, $src",
                (PseudoLA_PCREL GPR:$dst, bare_symbol:$src)>;
def : InstAlias<"la.global $dst, $tmp, $src",
                (PseudoLA_PCREL_LARGE GPR:$dst, GPR:$tmp, bare_symbol:$src)>;
} // Predicates = [HasLaGlobalWithPcrel]

let Predicates = [HasLaGlobalWithAbs] in {
def : InstAlias<"la $dst, $src", (PseudoLA_ABS GPR:$dst, bare_symbol:$src)>;
def : InstAlias<"la.global $dst, $src",
                (PseudoLA_ABS GPR:$dst, bare_symbol:$src)>;
def : InstAlias<"la.global $dst, $tmp, $src",
                (PseudoLA_ABS_LARGE GPR:$dst, GPR:$tmp, bare_symbol:$src)>;
} // Predicates = [HasLaGlobalWithAbs]

let Predicates = [HasLaLocalWithAbs] in {
def : InstAlias<"la.local $dst, $src",
                (PseudoLA_ABS GPR:$dst, bare_symbol:$src)>;
def : InstAlias<"la.local $dst, $tmp, $src",
                (PseudoLA_ABS_LARGE GPR:$dst, GPR:$tmp, bare_symbol:$src)>;
} // Predicates = [HasLaLocalWithAbs]

/// BSTRINS and BSTRPICK

let Predicates = [IsLA32] in {
def : Pat<(loongarch_bstrins GPR:$rd, GPR:$rj, uimm5:$msbd, uimm5:$lsbd),
          (BSTRINS_W GPR:$rd, GPR:$rj, uimm5:$msbd, uimm5:$lsbd)>;
def : Pat<(loongarch_bstrpick GPR:$rj, uimm5:$msbd, uimm5:$lsbd),
          (BSTRPICK_W GPR:$rj, uimm5:$msbd, uimm5:$lsbd)>;
} // Predicates = [IsLA32]

let Predicates = [IsLA64] in {
def : Pat<(loongarch_bstrins GPR:$rd, GPR:$rj, uimm6:$msbd, uimm6:$lsbd),
          (BSTRINS_D GPR:$rd, GPR:$rj, uimm6:$msbd, uimm6:$lsbd)>;
def : Pat<(loongarch_bstrpick GPR:$rj, uimm6:$msbd, uimm6:$lsbd),
          (BSTRPICK_D GPR:$rj, uimm6:$msbd, uimm6:$lsbd)>;
} // Predicates = [IsLA64]

/// Byte-swapping and bit-reversal

def : Pat<(loongarch_revb_2h GPR:$rj), (REVB_2H GPR:$rj)>;
def : Pat<(loongarch_bitrev_4b GPR:$rj), (BITREV_4B GPR:$rj)>;

let Predicates = [IsLA32] in {
def : Pat<(bswap GPR:$rj), (ROTRI_W (REVB_2H GPR:$rj), 16)>;
def : Pat<(bitreverse GPR:$rj), (BITREV_W GPR:$rj)>;
def : Pat<(bswap (bitreverse GPR:$rj)), (BITREV_4B GPR:$rj)>;
def : Pat<(bitreverse (bswap GPR:$rj)), (BITREV_4B GPR:$rj)>;
} // Predicates = [IsLA32]

let Predicates = [IsLA64] in {
def : Pat<(loongarch_revb_2w GPR:$rj), (REVB_2W GPR:$rj)>;
def : Pat<(bswap GPR:$rj), (REVB_D GPR:$rj)>;
def : Pat<(loongarch_bitrev_w GPR:$rj), (BITREV_W GPR:$rj)>;
def : Pat<(bitreverse GPR:$rj), (BITREV_D GPR:$rj)>;
def : Pat<(bswap (bitreverse GPR:$rj)), (BITREV_8B GPR:$rj)>;
def : Pat<(bitreverse (bswap GPR:$rj)), (BITREV_8B GPR:$rj)>;
} // Predicates = [IsLA64]

/// Loads

multiclass LdPat<PatFrag LoadOp, LAInst Inst, ValueType vt = GRLenVT> {
  def : Pat<(vt (LoadOp BaseAddr:$rj)), (Inst BaseAddr:$rj, 0)>;
  def : Pat<(vt (LoadOp (AddrConstant GPR:$rj, simm12:$imm12))),
            (Inst GPR:$rj, simm12:$imm12)>;
  def : Pat<(vt (LoadOp (AddLike BaseAddr:$rj, simm12:$imm12))),
            (Inst BaseAddr:$rj, simm12:$imm12)>;
}

defm : LdPat<sextloadi8, LD_B>;
defm : LdPat<extloadi8, LD_B>;
defm : LdPat<sextloadi16, LD_H>;
defm : LdPat<extloadi16, LD_H>;
defm : LdPat<load, LD_W>, Requires<[IsLA32]>;
defm : LdPat<zextloadi8, LD_BU>;
defm : LdPat<zextloadi16, LD_HU>;
let Predicates = [IsLA64] in {
defm : LdPat<sextloadi32, LD_W, i64>;
defm : LdPat<extloadi32, LD_W, i64>;
defm : LdPat<zextloadi32, LD_WU, i64>;
defm : LdPat<load, LD_D, i64>;
} // Predicates = [IsLA64]

// LA64 register-register-addressed loads
let Predicates = [IsLA64] in {
class RegRegLdPat<PatFrag LoadOp, LAInst Inst, ValueType vt>
  : Pat<(vt (LoadOp (add NonFIBaseAddr:$rj, GPR:$rk))),
        (Inst NonFIBaseAddr:$rj, GPR:$rk)>;

def : RegRegLdPat<extloadi8, LDX_B, i64>;
def : RegRegLdPat<sextloadi8, LDX_B, i64>;
def : RegRegLdPat<zextloadi8, LDX_BU, i64>;
def : RegRegLdPat<extloadi16, LDX_H, i64>;
def : RegRegLdPat<sextloadi16, LDX_H, i64>;
def : RegRegLdPat<zextloadi16, LDX_HU, i64>;
def : RegRegLdPat<extloadi32, LDX_W, i64>;
def : RegRegLdPat<sextloadi32, LDX_W, i64>;
def : RegRegLdPat<zextloadi32, LDX_WU, i64>;
def : RegRegLdPat<load, LDX_D, i64>;
} // Predicates = [IsLA64]

/// Stores

multiclass StPat<PatFrag StoreOp, LAInst Inst, RegisterClass StTy,
                 ValueType vt> {
  def : Pat<(StoreOp (vt StTy:$rd), BaseAddr:$rj),
            (Inst StTy:$rd, BaseAddr:$rj, 0)>;
  def : Pat<(StoreOp (vt StTy:$rs2), (AddrConstant GPR:$rj, simm12:$imm12)),
            (Inst StTy:$rs2, GPR:$rj, simm12:$imm12)>;
  def : Pat<(StoreOp (vt StTy:$rd), (AddLike BaseAddr:$rj, simm12:$imm12)),
            (Inst StTy:$rd, BaseAddr:$rj, simm12:$imm12)>;
}

defm : StPat<truncstorei8, ST_B, GPR, GRLenVT>;
defm : StPat<truncstorei16, ST_H, GPR, GRLenVT>;
defm : StPat<store, ST_W, GPR, i32>, Requires<[IsLA32]>;
let Predicates = [IsLA64] in {
defm : StPat<truncstorei32, ST_W, GPR, i64>;
defm : StPat<store, ST_D, GPR, i64>;
} // Predicates = [IsLA64]

let Predicates = [IsLA64] in {
def : Pat<(i64 (sextloadi32 (AddLike BaseAddr:$rj, simm14_lsl2:$imm14))),
          (LDPTR_W BaseAddr:$rj, simm14_lsl2:$imm14)>;
def : Pat<(i64 (load (AddLike BaseAddr:$rj, simm14_lsl2:$imm14))),
          (LDPTR_D BaseAddr:$rj, simm14_lsl2:$imm14)>;
def : Pat<(truncstorei32 (i64 GPR:$rd),
                         (AddLike BaseAddr:$rj, simm14_lsl2:$imm14)),
          (STPTR_W GPR:$rd, BaseAddr:$rj, simm14_lsl2:$imm14)>;
def : Pat<(store (i64 GPR:$rd), (AddLike BaseAddr:$rj, simm14_lsl2:$imm14)),
          (STPTR_D GPR:$rd, BaseAddr:$rj, simm14_lsl2:$imm14)>;
} // Predicates = [IsLA64]

// LA64 register-register-addressed stores
let Predicates = [IsLA64] in {
class RegRegStPat<PatFrag StoreOp, LAInst Inst, RegisterClass StTy,
                  ValueType vt>
  : Pat<(StoreOp (vt StTy:$rd), (add NonFIBaseAddr:$rj, GPR:$rk)),
        (Inst StTy:$rd, NonFIBaseAddr:$rj, GPR:$rk)>;

def : RegRegStPat<truncstorei8, STX_B, GPR, i64>;
def : RegRegStPat<truncstorei16, STX_H, GPR, i64>;
def : RegRegStPat<truncstorei32, STX_W, GPR, i64>;
def : RegRegStPat<store, STX_D, GPR, i64>;
} // Predicates = [IsLA64]

/// Atomic loads and stores

// DBAR hint encoding for LA664 and later micro-architectures, paraphrased from
// the Linux patch revealing it [1]:
//
// - Bit 4: kind of constraint (0: completion, 1: ordering)
// - Bit 3: barrier for previous read (0: true, 1: false)
// - Bit 2: barrier for previous write (0: true, 1: false)
// - Bit 1: barrier for succeeding read (0: true, 1: false)
// - Bit 0: barrier for succeeding write (0: true, 1: false)
//
// Hint 0x700: barrier for "read after read" from the same address, which is
// e.g. needed by LL-SC loops on older models. (DBAR 0x700 behaves the same as
// nop if such reordering is disabled on supporting newer models.)
//
// [1]: https://lore.kernel.org/loongarch/20230516124536.535343-1-chenhuacai@loongson.cn/
//
// Implementations without support for the finer-granularity hints simply treat
// all as the full barrier (DBAR 0), so we can unconditionally start emiting the
// more precise hints right away.

def : Pat<(atomic_fence 4, timm), (DBAR 0b10100)>; // acquire
def : Pat<(atomic_fence 5, timm), (DBAR 0b10010)>; // release
def : Pat<(atomic_fence 6, timm), (DBAR 0b10000)>; // acqrel
def : Pat<(atomic_fence 7, timm), (DBAR 0b10000)>; // seqcst

defm : LdPat<atomic_load_8, LD_B>;
defm : LdPat<atomic_load_16, LD_H>;
defm : LdPat<atomic_load_32, LD_W>;

class release_seqcst_store<PatFrag base>
    : PatFrag<(ops node:$val, node:$ptr), (base node:$val, node:$ptr), [{
  AtomicOrdering Ordering = cast<AtomicSDNode>(N)->getSuccessOrdering();
  return isReleaseOrStronger(Ordering);
}]>;

class unordered_monotonic_store<PatFrag base>
    : PatFrag<(ops node:$val, node:$ptr), (base node:$val, node:$ptr), [{
  AtomicOrdering Ordering = cast<AtomicSDNode>(N)->getSuccessOrdering();
  return !isReleaseOrStronger(Ordering);
}]>;

def atomic_store_release_seqcst_32 : release_seqcst_store<atomic_store_32>;
def atomic_store_release_seqcst_64 : release_seqcst_store<atomic_store_64>;
def atomic_store_unordered_monotonic_32
    : unordered_monotonic_store<atomic_store_32>;
def atomic_store_unordered_monotonic_64
    : unordered_monotonic_store<atomic_store_64>;

defm : StPat<atomic_store_8, ST_B, GPR, GRLenVT>;
defm : StPat<atomic_store_16, ST_H, GPR, GRLenVT>;
defm : StPat<atomic_store_unordered_monotonic_32, ST_W, GPR, i32>,
                   Requires<[IsLA32]>;

def PseudoAtomicStoreW
  : Pseudo<(outs GPR:$dst), (ins GPR:$rk, GPR:$rj)>,
           PseudoInstExpansion<(AMSWAP__DB_W R0, GPR:$rk, GPRMemAtomic:$rj)>;

def : Pat<(atomic_store_release_seqcst_32 GPR:$rj, GPR:$rk),
          (PseudoAtomicStoreW GPR:$rj, GPR:$rk)>;

let Predicates = [IsLA64] in {
def PseudoAtomicStoreD
  : Pseudo<(outs GPR:$dst), (ins GPR:$rk, GPR:$rj)>,
           PseudoInstExpansion<(AMSWAP__DB_D R0, GPR:$rk, GPRMemAtomic:$rj)>;

def : Pat<(atomic_store_release_seqcst_64 GPR:$rj, GPR:$rk),
          (PseudoAtomicStoreD GPR:$rj, GPR:$rk)>;

defm : LdPat<atomic_load_64, LD_D>;
defm : StPat<atomic_store_unordered_monotonic_32, ST_W, GPR, i64>;
defm : StPat<atomic_store_unordered_monotonic_64, ST_D, GPR, i64>;
} // Predicates = [IsLA64]

/// Atomic Ops

class PseudoMaskedAM
    : Pseudo<(outs GPR:$res, GPR:$scratch),
             (ins GPR:$addr, GPR:$incr, GPR:$mask, grlenimm:$ordering)> {
  let Constraints = "@earlyclobber $res,@earlyclobber $scratch";
  let mayLoad = 1;
  let mayStore = 1;
  let hasSideEffects = 0;
  let Size = 36;
}

def PseudoMaskedAtomicSwap32 : PseudoMaskedAM;
def PseudoMaskedAtomicLoadAdd32 : PseudoMaskedAM;
def PseudoMaskedAtomicLoadSub32 : PseudoMaskedAM;
def PseudoMaskedAtomicLoadNand32 : PseudoMaskedAM;

class PseudoAM : Pseudo<(outs GPR:$res, GPR:$scratch),
                        (ins GPR:$addr, GPR:$incr, grlenimm:$ordering)> {
  let Constraints = "@earlyclobber $res,@earlyclobber $scratch";
  let mayLoad = 1;
  let mayStore = 1;
  let hasSideEffects = 0;
  let Size = 24;
}

def PseudoAtomicSwap32 : PseudoAM;
def PseudoAtomicLoadNand32 : PseudoAM;
def PseudoAtomicLoadNand64 : PseudoAM;
def PseudoAtomicLoadAdd32 : PseudoAM;
def PseudoAtomicLoadSub32 : PseudoAM;
def PseudoAtomicLoadAnd32 : PseudoAM;
def PseudoAtomicLoadOr32 : PseudoAM;
def PseudoAtomicLoadXor32 : PseudoAM;

multiclass PseudoBinPat<string Op, Pseudo BinInst> {
  def : Pat<(!cast<PatFrag>(Op#"_monotonic") GPR:$addr, GPR:$incr),
            (BinInst GPR:$addr, GPR:$incr, 2)>;
  def : Pat<(!cast<PatFrag>(Op#"_acquire") GPR:$addr, GPR:$incr),
            (BinInst GPR:$addr, GPR:$incr, 4)>;
  def : Pat<(!cast<PatFrag>(Op#"_release") GPR:$addr, GPR:$incr),
            (BinInst GPR:$addr, GPR:$incr, 5)>;
  def : Pat<(!cast<PatFrag>(Op#"_acq_rel") GPR:$addr, GPR:$incr),
            (BinInst GPR:$addr, GPR:$incr, 6)>;
  def : Pat<(!cast<PatFrag>(Op#"_seq_cst") GPR:$addr, GPR:$incr),
            (BinInst GPR:$addr, GPR:$incr, 7)>;
}

class PseudoMaskedAMUMinUMax
    : Pseudo<(outs GPR:$res, GPR:$scratch1, GPR:$scratch2),
             (ins GPR:$addr, GPR:$incr, GPR:$mask, grlenimm:$ordering)> {
  let Constraints = "@earlyclobber $res,@earlyclobber $scratch1,"
                    "@earlyclobber $scratch2";
  let mayLoad = 1;
  let mayStore = 1;
  let hasSideEffects = 0;
  let Size = 48;
}

def PseudoMaskedAtomicLoadUMax32 : PseudoMaskedAMUMinUMax;
def PseudoMaskedAtomicLoadUMin32 : PseudoMaskedAMUMinUMax;

class PseudoMaskedAMMinMax
    : Pseudo<(outs GPR:$res, GPR:$scratch1, GPR:$scratch2),
             (ins GPR:$addr, GPR:$incr, GPR:$mask, grlenimm:$sextshamt,
              grlenimm:$ordering)> {
  let Constraints = "@earlyclobber $res,@earlyclobber $scratch1,"
                    "@earlyclobber $scratch2";
  let mayLoad = 1;
  let mayStore = 1;
  let hasSideEffects = 0;
  let Size = 56;
}

def PseudoMaskedAtomicLoadMax32 : PseudoMaskedAMMinMax;
def PseudoMaskedAtomicLoadMin32 : PseudoMaskedAMMinMax;

/// Compare and exchange

class PseudoCmpXchg
    : Pseudo<(outs GPR:$res, GPR:$scratch),
             (ins GPR:$addr, GPR:$cmpval, GPR:$newval, grlenimm:$fail_order)> {
  let Constraints = "@earlyclobber $res,@earlyclobber $scratch";
  let mayLoad = 1;
  let mayStore = 1;
  let hasSideEffects = 0;
  let Size = 36;
}

def PseudoCmpXchg32 : PseudoCmpXchg;
def PseudoCmpXchg64 : PseudoCmpXchg;

def PseudoMaskedCmpXchg32
    : Pseudo<(outs GPR:$res, GPR:$scratch),
             (ins GPR:$addr, GPR:$cmpval, GPR:$newval, GPR:$mask,
              grlenimm:$fail_order)> {
  let Constraints = "@earlyclobber $res,@earlyclobber $scratch";
  let mayLoad = 1;
  let mayStore = 1;
  let hasSideEffects = 0;
  let Size = 44;
}

class PseudoMaskedAMMinMaxPat<Intrinsic intrin, Pseudo AMInst>
    : Pat<(intrin GPR:$addr, GPR:$incr, GPR:$mask, GPR:$shiftamt,
           timm:$ordering),
          (AMInst GPR:$addr, GPR:$incr, GPR:$mask, GPR:$shiftamt,
           timm:$ordering)>;

class AtomicPat<Intrinsic intrin, Pseudo AMInst>
    : Pat<(intrin GPR:$addr, GPR:$incr, GPR:$mask, timm:$ordering),
          (AMInst GPR:$addr, GPR:$incr, GPR:$mask, timm:$ordering)>;

// These atomic cmpxchg PatFrags only care about the failure ordering.
// The PatFrags defined by multiclass `ternary_atomic_op_ord` in
// TargetSelectionDAG.td care about the merged memory ordering that is the
// stronger one between success and failure. But for LoongArch LL-SC we only
// need to care about the failure ordering as explained in PR #67391. So we
// define these PatFrags that will be used to define cmpxchg pats below.
multiclass ternary_atomic_op_failure_ord {
  def NAME#_failure_monotonic : PatFrag<(ops node:$ptr, node:$cmp, node:$val),
      (!cast<SDPatternOperator>(NAME) node:$ptr, node:$cmp, node:$val), [{
    AtomicOrdering Ordering = cast<AtomicSDNode>(N)->getFailureOrdering();
    return Ordering == AtomicOrdering::Monotonic;
  }]>;
  def NAME#_failure_acquire : PatFrag<(ops node:$ptr, node:$cmp, node:$val),
      (!cast<SDPatternOperator>(NAME) node:$ptr, node:$cmp, node:$val), [{
    AtomicOrdering Ordering = cast<AtomicSDNode>(N)->getFailureOrdering();
    return Ordering == AtomicOrdering::Acquire;
  }]>;
  def NAME#_failure_release : PatFrag<(ops node:$ptr, node:$cmp, node:$val),
      (!cast<SDPatternOperator>(NAME) node:$ptr, node:$cmp, node:$val), [{
    AtomicOrdering Ordering = cast<AtomicSDNode>(N)->getFailureOrdering();
    return Ordering == AtomicOrdering::Release;
  }]>;
  def NAME#_failure_acq_rel : PatFrag<(ops node:$ptr, node:$cmp, node:$val),
      (!cast<SDPatternOperator>(NAME) node:$ptr, node:$cmp, node:$val), [{
    AtomicOrdering Ordering = cast<AtomicSDNode>(N)->getFailureOrdering();
    return Ordering == AtomicOrdering::AcquireRelease;
  }]>;
  def NAME#_failure_seq_cst : PatFrag<(ops node:$ptr, node:$cmp, node:$val),
      (!cast<SDPatternOperator>(NAME) node:$ptr, node:$cmp, node:$val), [{
    AtomicOrdering Ordering = cast<AtomicSDNode>(N)->getFailureOrdering();
    return Ordering == AtomicOrdering::SequentiallyConsistent;
  }]>;
}

defm atomic_cmp_swap_i32 : ternary_atomic_op_failure_ord;
defm atomic_cmp_swap_i64 : ternary_atomic_op_failure_ord;

let Predicates = [IsLA64] in {
def : AtomicPat<int_loongarch_masked_atomicrmw_xchg_i64,
                PseudoMaskedAtomicSwap32>;
def : Pat<(atomic_swap_i32 GPR:$addr, GPR:$incr),
          (AMSWAP__DB_W GPR:$incr, GPR:$addr)>;
def : Pat<(atomic_swap_i64 GPR:$addr, GPR:$incr),
          (AMSWAP__DB_D GPR:$incr, GPR:$addr)>;
def : Pat<(atomic_load_add_i64 GPR:$rj, GPR:$rk),
          (AMADD__DB_D GPR:$rk, GPR:$rj)>;
def : AtomicPat<int_loongarch_masked_atomicrmw_add_i64,
                PseudoMaskedAtomicLoadAdd32>;
def : Pat<(atomic_load_sub_i32 GPR:$rj, GPR:$rk),
          (AMADD__DB_W (SUB_W R0, GPR:$rk), GPR:$rj)>;
def : Pat<(atomic_load_sub_i64 GPR:$rj, GPR:$rk),
          (AMADD__DB_D (SUB_D R0, GPR:$rk), GPR:$rj)>;
def : AtomicPat<int_loongarch_masked_atomicrmw_sub_i64,
                PseudoMaskedAtomicLoadSub32>;
defm : PseudoBinPat<"atomic_load_nand_i64", PseudoAtomicLoadNand64>;
def : AtomicPat<int_loongarch_masked_atomicrmw_nand_i64,
                PseudoMaskedAtomicLoadNand32>;
def : Pat<(atomic_load_add_i32 GPR:$rj, GPR:$rk),
          (AMADD__DB_W GPR:$rk, GPR:$rj)>;
def : Pat<(atomic_load_and_i32 GPR:$rj, GPR:$rk),
          (AMAND__DB_W GPR:$rk, GPR:$rj)>;
def : Pat<(atomic_load_and_i64 GPR:$rj, GPR:$rk),
          (AMAND__DB_D GPR:$rk, GPR:$rj)>;
def : Pat<(atomic_load_or_i32 GPR:$rj, GPR:$rk),
          (AMOR__DB_W GPR:$rk, GPR:$rj)>;
def : Pat<(atomic_load_or_i64 GPR:$rj, GPR:$rk),
          (AMOR__DB_D GPR:$rk, GPR:$rj)>;
def : Pat<(atomic_load_xor_i32 GPR:$rj, GPR:$rk),
          (AMXOR__DB_W GPR:$rk, GPR:$rj)>;
def : Pat<(atomic_load_xor_i64 GPR:$rj, GPR:$rk),
          (AMXOR__DB_D GPR:$rk, GPR:$rj)>;

def : Pat<(atomic_load_umin_i32 GPR:$rj, GPR:$rk),
          (AMMIN__DB_WU GPR:$rk, GPR:$rj)>;
def : Pat<(atomic_load_umin_i64 GPR:$rj, GPR:$rk),
          (AMMIN__DB_DU GPR:$rk, GPR:$rj)>;
def : Pat<(atomic_load_umax_i32 GPR:$rj, GPR:$rk),
          (AMMAX__DB_WU GPR:$rk, GPR:$rj)>;
def : Pat<(atomic_load_umax_i64 GPR:$rj, GPR:$rk),
          (AMMAX__DB_DU GPR:$rk, GPR:$rj)>;

def : Pat<(atomic_load_min_i32 GPR:$rj, GPR:$rk),
          (AMMIN__DB_W GPR:$rk, GPR:$rj)>;
def : Pat<(atomic_load_min_i64 GPR:$rj, GPR:$rk),
          (AMMIN__DB_D GPR:$rk, GPR:$rj)>;
def : Pat<(atomic_load_max_i32 GPR:$rj, GPR:$rk),
          (AMMAX__DB_W GPR:$rk, GPR:$rj)>;
def : Pat<(atomic_load_max_i64 GPR:$rj, GPR:$rk),
          (AMMAX__DB_D GPR:$rk, GPR:$rj)>;

def : AtomicPat<int_loongarch_masked_atomicrmw_umax_i64,
                PseudoMaskedAtomicLoadUMax32>;
def : AtomicPat<int_loongarch_masked_atomicrmw_umin_i64,
                PseudoMaskedAtomicLoadUMin32>;

// Ordering constants must be kept in sync with the AtomicOrdering enum in
// AtomicOrdering.h.
multiclass PseudoCmpXchgPat<string Op, Pseudo CmpXchgInst,
                            ValueType vt = GRLenVT> {
  def : Pat<(vt (!cast<PatFrag>(Op#"_failure_monotonic") GPR:$addr, GPR:$cmp, GPR:$new)),
            (CmpXchgInst GPR:$addr, GPR:$cmp, GPR:$new, 2)>;
  def : Pat<(vt (!cast<PatFrag>(Op#"_failure_acquire") GPR:$addr, GPR:$cmp, GPR:$new)),
            (CmpXchgInst GPR:$addr, GPR:$cmp, GPR:$new, 4)>;
  def : Pat<(vt (!cast<PatFrag>(Op#"_failure_release") GPR:$addr, GPR:$cmp, GPR:$new)),
            (CmpXchgInst GPR:$addr, GPR:$cmp, GPR:$new, 5)>;
  def : Pat<(vt (!cast<PatFrag>(Op#"_failure_acq_rel") GPR:$addr, GPR:$cmp, GPR:$new)),
            (CmpXchgInst GPR:$addr, GPR:$cmp, GPR:$new, 6)>;
  def : Pat<(vt (!cast<PatFrag>(Op#"_failure_seq_cst") GPR:$addr, GPR:$cmp, GPR:$new)),
            (CmpXchgInst GPR:$addr, GPR:$cmp, GPR:$new, 7)>;
}

defm : PseudoCmpXchgPat<"atomic_cmp_swap_i32", PseudoCmpXchg32>;
defm : PseudoCmpXchgPat<"atomic_cmp_swap_i64", PseudoCmpXchg64, i64>;
def : Pat<(int_loongarch_masked_cmpxchg_i64
            GPR:$addr, GPR:$cmpval, GPR:$newval, GPR:$mask, timm:$fail_order),
          (PseudoMaskedCmpXchg32
            GPR:$addr, GPR:$cmpval, GPR:$newval, GPR:$mask, timm:$fail_order)>;

def : PseudoMaskedAMMinMaxPat<int_loongarch_masked_atomicrmw_max_i64,
                              PseudoMaskedAtomicLoadMax32>;
def : PseudoMaskedAMMinMaxPat<int_loongarch_masked_atomicrmw_min_i64,
                              PseudoMaskedAtomicLoadMin32>;
} // Predicates = [IsLA64]

defm : PseudoBinPat<"atomic_load_nand_i32", PseudoAtomicLoadNand32>;

let Predicates = [IsLA32] in {
def : AtomicPat<int_loongarch_masked_atomicrmw_xchg_i32,
                PseudoMaskedAtomicSwap32>;
defm : PseudoBinPat<"atomic_swap_i32", PseudoAtomicSwap32>;
def : AtomicPat<int_loongarch_masked_atomicrmw_add_i32,
                PseudoMaskedAtomicLoadAdd32>;
def : AtomicPat<int_loongarch_masked_atomicrmw_sub_i32,
                PseudoMaskedAtomicLoadSub32>;
def : AtomicPat<int_loongarch_masked_atomicrmw_nand_i32,
                PseudoMaskedAtomicLoadNand32>;
defm : PseudoBinPat<"atomic_load_add_i32", PseudoAtomicLoadAdd32>;
defm : PseudoBinPat<"atomic_load_sub_i32", PseudoAtomicLoadSub32>;
defm : PseudoBinPat<"atomic_load_and_i32", PseudoAtomicLoadAnd32>;
defm : PseudoBinPat<"atomic_load_or_i32", PseudoAtomicLoadOr32>;
defm : PseudoBinPat<"atomic_load_xor_i32", PseudoAtomicLoadXor32>;
} // Predicates = [IsLA32]

/// Intrinsics

def : Pat<(int_loongarch_cacop_d timm:$op, i64:$rj, timm:$imm12),
          (CACOP timm:$op, GPR:$rj, timm:$imm12)>;
def : Pat<(int_loongarch_cacop_w i32:$op, i32:$rj, i32:$imm12),
          (CACOP timm:$op, GPR:$rj, timm:$imm12)>;
def : Pat<(loongarch_dbar uimm15:$imm15), (DBAR uimm15:$imm15)>;
def : Pat<(loongarch_ibar uimm15:$imm15), (IBAR uimm15:$imm15)>;
def : Pat<(loongarch_break uimm15:$imm15), (BREAK uimm15:$imm15)>;
def : Pat<(loongarch_syscall uimm15:$imm15), (SYSCALL uimm15:$imm15)>;

let Predicates = [IsLA64] in {
// CRC Check Instructions
def : PatGprGpr<loongarch_crc_w_b_w, CRC_W_B_W>;
def : PatGprGpr<loongarch_crc_w_h_w, CRC_W_H_W>;
def : PatGprGpr<loongarch_crc_w_w_w, CRC_W_W_W>;
def : PatGprGpr<loongarch_crc_w_d_w, CRC_W_D_W>;
def : PatGprGpr<loongarch_crcc_w_b_w, CRCC_W_B_W>;
def : PatGprGpr<loongarch_crcc_w_h_w, CRCC_W_H_W>;
def : PatGprGpr<loongarch_crcc_w_w_w, CRCC_W_W_W>;
def : PatGprGpr<loongarch_crcc_w_d_w, CRCC_W_D_W>;
} // Predicates = [IsLA64]

/// Other pseudo-instructions

// Pessimistically assume the stack pointer will be clobbered
let Defs = [R3], Uses = [R3] in {
def ADJCALLSTACKDOWN : Pseudo<(outs), (ins i32imm:$amt1, i32imm:$amt2),
                              [(callseq_start timm:$amt1, timm:$amt2)]>;
def ADJCALLSTACKUP   : Pseudo<(outs), (ins i32imm:$amt1, i32imm:$amt2),
                              [(callseq_end timm:$amt1, timm:$amt2)]>;
} // Defs = [R3], Uses = [R3]

//===----------------------------------------------------------------------===//
// Assembler Pseudo Instructions
//===----------------------------------------------------------------------===//

def : InstAlias<"nop", (ANDI R0, R0, 0)>;
def : InstAlias<"move $dst, $src", (OR GPR:$dst, GPR:$src, R0)>;
// `ret` is supported since binutils commit 20f2e2686c79a5ac (version 2.40 and
// later).
def : InstAlias<"ret", (JIRL R0, R1, 0)>;
def : InstAlias<"jr $rj", (JIRL R0, GPR:$rj, 0)>;

// Branches implemented with alias.
// Always output the canonical mnemonic for the pseudo branch instructions.
// The GNU tools emit the canonical mnemonic for the branch pseudo instructions
// as well (e.g. "bgt" will be recognised by the assembler but never printed by
// objdump). Match this behaviour by setting a zero weight.
def : InstAlias<"bgt $rj, $rd, $imm16",
                (BLT GPR:$rd, GPR:$rj, simm16_lsl2_br:$imm16), 0>;
def : InstAlias<"bgtu $rj, $rd, $imm16",
                (BLTU GPR:$rd, GPR:$rj, simm16_lsl2_br:$imm16), 0>;
def : InstAlias<"ble $rj, $rd, $imm16",
                (BGE GPR:$rd, GPR:$rj, simm16_lsl2_br:$imm16), 0>;
def : InstAlias<"bleu $rj, $rd, $imm16",
                (BGEU GPR:$rd, GPR:$rj, simm16_lsl2_br:$imm16), 0>;
def : InstAlias<"bltz $rd, $imm16",
                (BLT GPR:$rd, R0, simm16_lsl2_br:$imm16), 0>;
def : InstAlias<"bgtz $rj, $imm16",
                (BLT R0, GPR:$rj, simm16_lsl2_br:$imm16), 0>;
def : InstAlias<"blez $rj, $imm16",
                (BGE R0, GPR:$rj, simm16_lsl2_br:$imm16), 0>;
def : InstAlias<"bgez $rd, $imm16",
                (BGE GPR:$rd, R0, simm16_lsl2_br:$imm16), 0>;

// Load immediate.
let hasSideEffects = 0, mayLoad = 0, mayStore = 0, isCodeGenOnly = 0,
    isAsmParserOnly = 1 in {
def PseudoLI_W : Pseudo<(outs GPR:$rd), (ins imm32:$imm), [],
                        "li.w", "$rd, $imm">;
def PseudoLI_D : Pseudo<(outs GPR:$rd), (ins imm64:$imm), [],
                        "li.d", "$rd, $imm">, Requires<[IsLA64]>;
}

//===----------------------------------------------------------------------===//
// Basic Floating-Point Instructions
//===----------------------------------------------------------------------===//

include "LoongArchFloat32InstrInfo.td"
include "LoongArchFloat64InstrInfo.td"

let Predicates = [HasBasicF], usesCustomInserter = 1 in {
  def WRFCSR : Pseudo<(outs), (ins uimm2:$fcsr, GPR:$src),
               [(loongarch_movgr2fcsr uimm2:$fcsr, GRLenVT:$src)]>;
  def RDFCSR : Pseudo<(outs GPR:$rd), (ins uimm2:$fcsr),
               [(set GPR:$rd, (loongarch_movfcsr2gr uimm2:$fcsr))]>;
}

//===----------------------------------------------------------------------===//
// Privilege Instructions
//===----------------------------------------------------------------------===//

// CSR Access Instructions
let hasSideEffects = 1 in
def CSRRD : FmtCSR<0x04000000, (outs GPR:$rd), (ins uimm14:$csr_num),
                   "$rd, $csr_num">;
let hasSideEffects = 1, Constraints = "$rd = $dst" in {
def CSRWR : FmtCSR<0x04000020, (outs GPR:$dst),
                   (ins GPR:$rd, uimm14:$csr_num), "$rd, $csr_num">;
def CSRXCHG : FmtCSRXCHG<0x04000000, (outs GPR:$dst),
                         (ins GPR:$rd, GPR:$rj, uimm14:$csr_num),
                         "$rd, $rj, $csr_num">;
} // hasSideEffects = 1, Constraints = "$rd = $dst"

// IOCSR Access Instructions
def IOCSRRD_B : IOCSRRD<0x06480000>;
def IOCSRRD_H : IOCSRRD<0x06480400>;
def IOCSRRD_W : IOCSRRD<0x06480800>;
def IOCSRWR_B : IOCSRWR<0x06481000>;
def IOCSRWR_H : IOCSRWR<0x06481400>;
def IOCSRWR_W : IOCSRWR<0x06481800>;
let Predicates = [IsLA64] in {
def IOCSRRD_D : IOCSRRD<0x06480c00>;
def IOCSRWR_D : IOCSRWR<0x06481c00>;
} // Predicates = [IsLA64]

// TLB Maintenance Instructions
let hasSideEffects = 1, mayLoad = 0, mayStore = 0 in {
def TLBSRCH  : FmtI32<0x06482800>;
def TLBRD    : FmtI32<0x06482c00>;
def TLBWR    : FmtI32<0x06483000>;
def TLBFILL  : FmtI32<0x06483400>;
def TLBCLR   : FmtI32<0x06482000>;
def TLBFLUSH : FmtI32<0x06482400>;
def INVTLB : FmtINVTLB<(outs), (ins GPR:$rk, GPR:$rj, uimm5:$op),
                       "$op, $rj, $rk">;
} // hasSideEffects = 1, mayLoad = 0, mayStore = 0

// Software Page Walking Instructions
def LDDIR : Fmt2RI8<0x06400000, (outs GPR:$rd),
                    (ins GPR:$rj, uimm8:$imm8), "$rd, $rj, $imm8">;
def LDPTE : FmtLDPTE<(outs), (ins GPR:$rj, uimm8:$seq), "$rj, $seq">;


// Other Miscellaneous Instructions
let hasSideEffects = 1, mayLoad = 0, mayStore = 0 in
def ERTN : FmtI32<0x06483800>;
def DBCL : MISC_I15<0x002a8000>;
def IDLE : MISC_I15<0x06488000>;

//===----------------------------------------------------------------------===//
// Privilege Intrinsics
//===----------------------------------------------------------------------===//

def : Pat<(loongarch_csrrd uimm14:$imm14), (CSRRD uimm14:$imm14)>;
def : Pat<(loongarch_csrwr GPR:$rd, uimm14:$imm14),
          (CSRWR GPR:$rd, uimm14:$imm14)>;
def : Pat<(loongarch_csrxchg GPR:$rd, GPR:$rj, uimm14:$imm14),
          (CSRXCHG GPR:$rd, GPR:$rj, uimm14:$imm14)>;

def : Pat<(loongarch_iocsrrd_b GPR:$rj), (IOCSRRD_B GPR:$rj)>;
def : Pat<(loongarch_iocsrrd_h GPR:$rj), (IOCSRRD_H GPR:$rj)>;
def : Pat<(loongarch_iocsrrd_w GPR:$rj), (IOCSRRD_W GPR:$rj)>;

def : Pat<(loongarch_iocsrwr_b GPR:$rd, GPR:$rj), (IOCSRWR_B GPR:$rd, GPR:$rj)>;
def : Pat<(loongarch_iocsrwr_h GPR:$rd, GPR:$rj), (IOCSRWR_H GPR:$rd, GPR:$rj)>;
def : Pat<(loongarch_iocsrwr_w GPR:$rd, GPR:$rj), (IOCSRWR_W GPR:$rd, GPR:$rj)>;

def : Pat<(loongarch_cpucfg GPR:$rj), (CPUCFG GPR:$rj)>;

let Predicates = [IsLA64] in {
def : Pat<(loongarch_iocsrrd_d GPR:$rj), (IOCSRRD_D GPR:$rj)>;
def : Pat<(loongarch_iocsrwr_d GPR:$rd, GPR:$rj), (IOCSRWR_D GPR:$rd, GPR:$rj)>;
def : Pat<(int_loongarch_asrtle_d GPR:$rj, GPR:$rk),
          (ASRTLE_D GPR:$rj, GPR:$rk)>;
def : Pat<(int_loongarch_asrtgt_d GPR:$rj, GPR:$rk),
          (ASRTGT_D GPR:$rj, GPR:$rk)>;
def : Pat<(int_loongarch_lddir_d GPR:$rj, timm:$imm8),
          (LDDIR GPR:$rj, timm:$imm8)>;
def : Pat<(int_loongarch_ldpte_d GPR:$rj, timm:$imm8),
          (LDPTE GPR:$rj, timm:$imm8)>;
} // Predicates = [IsLA64]

//===----------------------------------------------------------------------===//
// LSX Instructions
//===----------------------------------------------------------------------===//
include "LoongArchLSXInstrInfo.td"

//===----------------------------------------------------------------------===//
// LASX Instructions
//===----------------------------------------------------------------------===//
include "LoongArchLASXInstrInfo.td"

//===----------------------------------------------------------------------===//
// LVZ Instructions
//===----------------------------------------------------------------------===//
include "LoongArchLVZInstrInfo.td"

//===----------------------------------------------------------------------===//
// LBT Instructions
//===----------------------------------------------------------------------===//
include "LoongArchLBTInstrInfo.td"
