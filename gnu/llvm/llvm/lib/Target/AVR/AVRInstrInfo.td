//===-- AVRInstrInfo.td - AVR Instruction defs -------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the AVR instructions in TableGen format.
//
//===----------------------------------------------------------------------===//

include "AVRInstrFormats.td"

//===----------------------------------------------------------------------===//
// AVR Type Profiles
//===----------------------------------------------------------------------===//

def SDT_AVRCallSeqStart : SDCallSeqStart<[SDTCisVT<0, i16>, SDTCisVT<1, i16>]>;
def SDT_AVRCallSeqEnd : SDCallSeqEnd<[SDTCisVT<0, i16>, SDTCisVT<1, i16>]>;
def SDT_AVRCall : SDTypeProfile<0, -1, [SDTCisVT<0, iPTR>]>;
def SDT_AVRWrapper : SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>, SDTCisPtrTy<0>]>;
def SDT_AVRBrcond
    : SDTypeProfile<0, 2, [SDTCisVT<0, OtherVT>, SDTCisVT<1, i8>]>;
def SDT_AVRCmp : SDTypeProfile<0, 2, [SDTCisSameAs<0, 1>]>;
def SDT_AVRTst : SDTypeProfile<0, 1, [SDTCisInt<0>]>;
def SDT_AVRSelectCC
    : SDTypeProfile<1, 3,
                    [SDTCisSameAs<0, 1>, SDTCisSameAs<1, 2>, SDTCisVT<3, i8>]>;

//===----------------------------------------------------------------------===//
// AVR Specific Node Definitions
//===----------------------------------------------------------------------===//

def AVRretglue : SDNode<"AVRISD::RET_GLUE", SDTNone,
                        [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
def AVRretiglue : SDNode<"AVRISD::RETI_GLUE", SDTNone,
                         [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;

def AVRcallseq_start : SDNode<"ISD::CALLSEQ_START", SDT_AVRCallSeqStart,
                              [SDNPHasChain, SDNPOutGlue]>;
def AVRcallseq_end : SDNode<"ISD::CALLSEQ_END", SDT_AVRCallSeqEnd,
                            [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

def AVRcall : SDNode<"AVRISD::CALL", SDT_AVRCall,
                     [SDNPHasChain, SDNPOutGlue, SDNPOptInGlue, SDNPVariadic]>;

def AVRWrapper : SDNode<"AVRISD::WRAPPER", SDT_AVRWrapper>;

def AVRbrcond
    : SDNode<"AVRISD::BRCOND", SDT_AVRBrcond, [SDNPHasChain, SDNPInGlue]>;
def AVRcmp : SDNode<"AVRISD::CMP", SDT_AVRCmp, [SDNPOutGlue]>;
def AVRcmpc : SDNode<"AVRISD::CMPC", SDT_AVRCmp, [SDNPInGlue, SDNPOutGlue]>;
def AVRtst : SDNode<"AVRISD::TST", SDT_AVRTst, [SDNPOutGlue]>;
def AVRselectcc : SDNode<"AVRISD::SELECT_CC", SDT_AVRSelectCC, [SDNPInGlue]>;

// Shift nodes.
def AVRlsl : SDNode<"AVRISD::LSL", SDTIntUnaryOp>;
def AVRlsr : SDNode<"AVRISD::LSR", SDTIntUnaryOp>;
def AVRrol : SDNode<"AVRISD::ROL", SDTIntUnaryOp>;
def AVRror : SDNode<"AVRISD::ROR", SDTIntUnaryOp>;
def AVRasr : SDNode<"AVRISD::ASR", SDTIntUnaryOp>;
def AVRlslhi : SDNode<"AVRISD::LSLHI", SDTIntUnaryOp>;
def AVRlsrlo : SDNode<"AVRISD::LSRLO", SDTIntUnaryOp>;
def AVRasrlo : SDNode<"AVRISD::ASRLO", SDTIntUnaryOp>;
def AVRlslbn : SDNode<"AVRISD::LSLBN", SDTIntBinOp>;
def AVRlsrbn : SDNode<"AVRISD::LSRBN", SDTIntBinOp>;
def AVRasrbn : SDNode<"AVRISD::ASRBN", SDTIntBinOp>;
def AVRlslwn : SDNode<"AVRISD::LSLWN", SDTIntBinOp>;
def AVRlsrwn : SDNode<"AVRISD::LSRWN", SDTIntBinOp>;
def AVRasrwn : SDNode<"AVRISD::ASRWN", SDTIntBinOp>;
def AVRlslw : SDNode<"AVRISD::LSLW", SDTIntShiftDOp>;
def AVRlsrw : SDNode<"AVRISD::LSRW", SDTIntShiftDOp>;
def AVRasrw : SDNode<"AVRISD::ASRW", SDTIntShiftDOp>;

// Pseudo shift nodes for non-constant shift amounts.
def AVRlslLoop : SDNode<"AVRISD::LSLLOOP", SDTIntShiftOp>;
def AVRlsrLoop : SDNode<"AVRISD::LSRLOOP", SDTIntShiftOp>;
def AVRrolLoop : SDNode<"AVRISD::ROLLOOP", SDTIntShiftOp>;
def AVRrorLoop : SDNode<"AVRISD::RORLOOP", SDTIntShiftOp>;
def AVRasrLoop : SDNode<"AVRISD::ASRLOOP", SDTIntShiftOp>;

// SWAP node.
def AVRSwap : SDNode<"AVRISD::SWAP", SDTIntUnaryOp>;

//===----------------------------------------------------------------------===//
// AVR Operands, Complex Patterns and Transformations Definitions.
//===----------------------------------------------------------------------===//

def imm8_neg_XFORM : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(-N->getAPIntValue(), SDLoc(N), MVT::i8);
}]>;

def imm16_neg_XFORM : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(-N->getAPIntValue(), SDLoc(N), MVT::i16);
}]>;

def imm0_63_neg : PatLeaf<(imm), [{
  int64_t val = -N->getSExtValue();
  return val >= 0 && val < 64;
}], imm16_neg_XFORM>;

def uimm6 : PatLeaf<(imm), [{ return isUInt<6>(N->getZExtValue()); }]>;

// imm_com8_XFORM - Return the complement of a imm_com8 value
def imm_com8_XFORM : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(
      ~((uint8_t) N->getZExtValue()), SDLoc(N), MVT::i8);
}]>;

// imm_com8 - Match an immediate that is a complement
// of a 8-bit immediate.
// Note: this pattern doesn't require an encoder method and such, as it's
// only used on aliases (Pat<> and InstAlias<>). The actual encoding
// is handled by the destination instructions, which use imm_com8.
def imm_com8_asmoperand : AsmOperandClass { let Name = "ImmCom8"; }
def imm_com8 : Operand<i8> { let ParserMatchClass = imm_com8_asmoperand; }

def ioaddr_XFORM : SDNodeXForm<imm, [{
  uint8_t offset = Subtarget->getIORegisterOffset();
  return CurDAG->getTargetConstant(
      uint8_t(N->getZExtValue()) - offset, SDLoc(N), MVT::i8);
}]>;

def iobitpos8_XFORM : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(
      Log2_32(uint8_t(N->getZExtValue())), SDLoc(N), MVT::i8);
}]>;

def iobitposn8_XFORM : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(
      Log2_32(uint8_t(~N->getZExtValue())), SDLoc(N), MVT::i8);
}]>;

def ioaddr8 : PatLeaf<(imm), [{
  uint8_t offset = Subtarget->getIORegisterOffset();
  uint64_t val = N->getZExtValue() - offset;
  return val < 0x40;
}], ioaddr_XFORM>;

def lowioaddr8 : PatLeaf<(imm), [{
  uint8_t offset = Subtarget->getIORegisterOffset();
  uint64_t val = N->getZExtValue() - offset;
  return val < 0x20;
}], ioaddr_XFORM>;

def ioaddr16 : PatLeaf<(imm), [{
  uint8_t offset = Subtarget->getIORegisterOffset();
  uint64_t val = N->getZExtValue() - offset;
  return val < 0x3f;
}], ioaddr_XFORM>;

def iobitpos8 : PatLeaf<(imm), [{
  return isPowerOf2_32(uint8_t(N->getZExtValue()));
}], iobitpos8_XFORM>;

def iobitposn8 : PatLeaf<(imm), [{
  return isPowerOf2_32(uint8_t(~N->getZExtValue()));
}], iobitposn8_XFORM>;

def MemriAsmOperand : AsmOperandClass {
  let Name = "Memri";
  let ParserMethod = "parseMemriOperand";
}

/// Address operand for `reg+imm` used by STD and LDD.
def memri : Operand<iPTR> {
  let MIOperandInfo = (ops PTRDISPREGS, i16imm);

  let PrintMethod = "printMemri";
  let EncoderMethod = "encodeMemri";
  let DecoderMethod = "decodeMemri";

  let ParserMatchClass = MemriAsmOperand;
}

// Address operand for `SP+imm` used by STD{W}SPQRr
def memspi : Operand<iPTR> {
  let MIOperandInfo = (ops GPRSP, i16imm);
  let PrintMethod = "printMemspi";
}

def relbrtarget_7 : Operand<OtherVT> {
  let PrintMethod = "printPCRelImm";
  let EncoderMethod = "encodeRelCondBrTarget<AVR::fixup_7_pcrel>";
}

def brtarget_13 : Operand<OtherVT> {
  let PrintMethod = "printPCRelImm";
  let EncoderMethod = "encodeRelCondBrTarget<AVR::fixup_13_pcrel>";
}

def rcalltarget_13 : Operand<i16> {
  let PrintMethod = "printPCRelImm";
  let EncoderMethod = "encodeRelCondBrTarget<AVR::fixup_13_pcrel>";
}

// The target of a 22 or 16-bit call/jmp instruction.
def call_target : Operand<iPTR> {
  let EncoderMethod = "encodeCallTarget";
  let DecoderMethod = "decodeCallTarget";
}

// A 16-bit address (which can lead to an R_AVR_16 relocation).
def imm16 : Operand<i16> { let EncoderMethod = "encodeImm<AVR::fixup_16, 2>"; }

// A 7-bit address (which can lead to an R_AVR_LDS_STS_16 relocation).
def imm7tiny : Operand<i16> {
  let EncoderMethod = "encodeImm<AVR::fixup_lds_sts_16, 0>";
}

/// A 6-bit immediate used in the ADIW/SBIW instructions.
def imm_arith6 : Operand<i16> {
  let EncoderMethod = "encodeImm<AVR::fixup_6_adiw, 0>";
}

/// An 8-bit immediate inside an instruction with the same format
/// as the `LDI` instruction (the `FRdK` format).
def imm_ldi8 : Operand<i8> {
  let EncoderMethod = "encodeImm<AVR::fixup_ldi, 0>";
}

/// A 5-bit port number used in SBIC and friends (the `FIOBIT` format).
def imm_port5 : Operand<i8> {
  let EncoderMethod = "encodeImm<AVR::fixup_port5, 0>";
}

/// A 6-bit port number used in the `IN` instruction and friends (the
/// `FIORdA` format.
def imm_port6 : Operand<i8> {
  let EncoderMethod = "encodeImm<AVR::fixup_port6, 0>";
}

// Addressing mode pattern reg+imm6
def addr : ComplexPattern<iPTR, 2, "SelectAddr", [], [SDNPWantRoot]>;

// AsmOperand class for a pointer register.
// Used with the LD/ST family of instructions.
// See FSTLD in AVRInstrFormats.td
def PtrRegAsmOperand : AsmOperandClass { let Name = "Reg"; }

// A special operand type for the LD/ST instructions.
// It converts the pointer register number into a two-bit field used in the
// instruction.
def LDSTPtrReg : Operand<i16> {
  let MIOperandInfo = (ops PTRREGS);
  let EncoderMethod = "encodeLDSTPtrReg";

  let ParserMatchClass = PtrRegAsmOperand;
}

// A special operand type for the LDD/STD instructions.
// It behaves identically to the LD/ST version, except restricts
// the pointer registers to Y and Z.
def LDDSTDPtrReg : Operand<i16> {
  let MIOperandInfo = (ops PTRDISPREGS);
  let EncoderMethod = "encodeLDSTPtrReg";

  let ParserMatchClass = PtrRegAsmOperand;
}

//===----------------------------------------------------------------------===//
// AVR predicates for subtarget features
//===----------------------------------------------------------------------===//

def HasSRAM : Predicate<"Subtarget->hasSRAM()">,
              AssemblerPredicate<(all_of FeatureSRAM)>;

def HasJMPCALL : Predicate<"Subtarget->hasJMPCALL()">,
                 AssemblerPredicate<(all_of FeatureJMPCALL)>;

def HasIJMPCALL : Predicate<"Subtarget->hasIJMPCALL()">,
                  AssemblerPredicate<(all_of FeatureIJMPCALL)>;

def HasEIJMPCALL : Predicate<"Subtarget->hasEIJMPCALL()">,
                   AssemblerPredicate<(all_of FeatureEIJMPCALL)>;

def HasADDSUBIW : Predicate<"Subtarget->hasADDSUBIW()">,
                  AssemblerPredicate<(all_of FeatureADDSUBIW)>;

def HasSmallStack : Predicate<"Subtarget->HasSmallStack()">,
                    AssemblerPredicate<(all_of FeatureSmallStack)>;

def HasMOVW : Predicate<"Subtarget->hasMOVW()">,
              AssemblerPredicate<(all_of FeatureMOVW)>;

def HasLPM : Predicate<"Subtarget->hasLPM()">,
             AssemblerPredicate<(all_of FeatureLPM)>;

def HasLPMX : Predicate<"Subtarget->hasLPMX()">,
              AssemblerPredicate<(all_of FeatureLPMX)>;

def HasELPM : Predicate<"Subtarget->hasELPM()">,
              AssemblerPredicate<(all_of FeatureELPM)>;

def HasELPMX : Predicate<"Subtarget->hasELPMX()">,
               AssemblerPredicate<(all_of FeatureELPMX)>;

def HasSPM : Predicate<"Subtarget->hasSPM()">,
             AssemblerPredicate<(all_of FeatureSPM)>;

def HasSPMX : Predicate<"Subtarget->hasSPMX()">,
              AssemblerPredicate<(all_of FeatureSPMX)>;

def HasDES : Predicate<"Subtarget->hasDES()">,
             AssemblerPredicate<(all_of FeatureDES)>;

def SupportsRMW : Predicate<"Subtarget->supportsRMW()">,
                  AssemblerPredicate<(all_of FeatureRMW)>;

def SupportsMultiplication : Predicate<"Subtarget->supportsMultiplication()">,
                             AssemblerPredicate<(all_of FeatureMultiplication)>;

def HasBREAK : Predicate<"Subtarget->hasBREAK()">,
               AssemblerPredicate<(all_of FeatureBREAK)>;

def HasTinyEncoding : Predicate<"Subtarget->hasTinyEncoding()">,
                      AssemblerPredicate<(all_of FeatureTinyEncoding)>;

def HasNonTinyEncoding : Predicate<"!Subtarget->hasTinyEncoding()">,
                         AssemblerPredicate<(any_of (not FeatureTinyEncoding))>;

// AVR specific condition code. These correspond to AVR_*_COND in
// AVRInstrInfo.td. They must be kept in synch.
def AVR_COND_EQ : PatLeaf<(i8 0)>;
def AVR_COND_NE : PatLeaf<(i8 1)>;
def AVR_COND_GE : PatLeaf<(i8 2)>;
def AVR_COND_LT : PatLeaf<(i8 3)>;
def AVR_COND_SH : PatLeaf<(i8 4)>;
def AVR_COND_LO : PatLeaf<(i8 5)>;
def AVR_COND_MI : PatLeaf<(i8 6)>;
def AVR_COND_PL : PatLeaf<(i8 7)>;

//===----------------------------------------------------------------------===//
//===----------------------------------------------------------------------===//
// AVR Instruction list
//===----------------------------------------------------------------------===//
//===----------------------------------------------------------------------===//

// ADJCALLSTACKDOWN/UP implicitly use/def SP because they may be expanded into
// a stack adjustment and the codegen must know that they may modify the stack
// pointer before prolog-epilog rewriting occurs.
// Pessimistically assume ADJCALLSTACKDOWN / ADJCALLSTACKUP will become
// sub / add which can clobber SREG.
let Defs = [SP, SREG], Uses = [SP] in {
  def ADJCALLSTACKDOWN : Pseudo<(outs), (ins i16imm:$amt, i16imm:$amt2),
                                "#ADJCALLSTACKDOWN",
                                [(AVRcallseq_start timm:$amt, timm:$amt2)]>;

  // R31R30 is used to update SP. It is normally free because it is a
  // call-clobbered register but it is necessary to set it as a def as the
  // register allocator might use it in rare cases (for rematerialization, it
  // seems). hasSideEffects needs to be set to true so this instruction isn't
  // considered dead.
  let Defs = [R31R30], hasSideEffects = 1 in def ADJCALLSTACKUP
      : Pseudo<(outs), (ins i16imm:$amt1, i16imm:$amt2),
               "#ADJCALLSTACKUP", [(AVRcallseq_end timm:$amt1, timm:$amt2)]>;
}

//===----------------------------------------------------------------------===//
// Addition
//===----------------------------------------------------------------------===//
let isCommutable = 1, Constraints = "$src = $rd", Defs = [SREG] in {
  // ADD Rd, Rr
  // Adds two 8-bit registers.
  def ADDRdRr : FRdRr<0b0000, 0b11, (outs GPR8:$rd),(ins GPR8:$src, GPR8:$rr),
                      "add\t$rd, $rr",
                      [(set i8:$rd, (add i8:$src, i8:$rr)), (implicit SREG)]>;

  // ADDW Rd+1:Rd, Rr+1:Rr
  // Pseudo instruction to add four 8-bit registers as two 16-bit values.
  //
  // Expands to:
  // add Rd,    Rr
  // adc Rd+1, Rr+1
  def ADDWRdRr : Pseudo<(outs DREGS:$rd), (ins DREGS:$src, DREGS:$rr),
                        "addw\t$rd, $rr",
                        [(set i16:$rd, (add i16:$src, i16:$rr)),
                         (implicit SREG)]>;

  // ADC Rd, Rr
  // Adds two 8-bit registers with carry.
  let Uses = [SREG] in
  def ADCRdRr : FRdRr<0b0001, 0b11, (outs GPR8:$rd), (ins GPR8:$src, GPR8:$rr),
                      "adc\t$rd, $rr",
                      [(set i8:$rd, (adde i8:$src, i8:$rr)), (implicit SREG)]>;

  // ADCW Rd+1:Rd, Rr+1:Rr
  // Pseudo instruction to add four 8-bit registers as two 16-bit values with
  // carry.
  //
  // Expands to:
  // adc Rd,   Rr
  // adc Rd+1, Rr+1
  let Uses = [SREG] in
  def ADCWRdRr : Pseudo<(outs DREGS:$rd), (ins DREGS:$src, DREGS:$rr),
                        "adcw\t$rd, $rr",
                        [(set i16:$rd, (adde i16:$src, i16:$rr)),
                         (implicit SREG)]>;

  // AIDW Rd, k
  // Adds an immediate 6-bit value K to Rd, placing the result in Rd.
  def ADIWRdK : FWRdK<0b0, (outs IWREGS:$rd), (ins IWREGS :$src, imm_arith6:$k),
                      "adiw\t$rd, $k",
                      [(set i16:$rd, (add i16:$src, uimm6:$k)),
                       (implicit SREG)]>,
                Requires<[HasADDSUBIW]>;
}

//===----------------------------------------------------------------------===//
// Subtraction
//===----------------------------------------------------------------------===//
let Constraints = "$rs = $rd", Defs = [SREG] in {
  // SUB Rd, Rr
  // Subtracts the 8-bit value of Rr from Rd and places the value in Rd.
  def SUBRdRr : FRdRr<0b0001, 0b10, (outs GPR8:$rd), (ins GPR8:$rs, GPR8:$rr),
                      "sub\t$rd, $rr",
                      [(set i8:$rd, (sub i8:$rs, i8:$rr)), (implicit SREG)]>;

  // SUBW Rd+1:Rd, Rr+1:Rr
  // Subtracts two 16-bit values and places the result into Rd.
  //
  // Expands to:
  // sub Rd,   Rr
  // sbc Rd+1, Rr+1
  def SUBWRdRr : Pseudo<(outs DREGS:$rd), (ins DREGS:$rs, DREGS:$rr),
                        "subw\t$rd, $rr",
                        [(set i16:$rd, (sub i16:$rs, i16:$rr)),
                         (implicit SREG)]>;

  def SUBIRdK : FRdK<0b0101, (outs LD8:$rd), (ins LD8:$rs, imm_ldi8:$k),
                     "subi\t$rd, $k",
                     [(set i8:$rd, (sub i8:$rs, imm:$k)), (implicit SREG)]>;

  // SUBIW Rd+1:Rd, K+1:K
  //
  // Expands to:
  // subi Rd,   K
  // sbci Rd+1, K+1
  def SUBIWRdK : Pseudo<(outs DLDREGS:$rd), (ins DLDREGS:$rs, i16imm:$rr),
                        "subiw\t$rd, $rr",
                        [(set i16:$rd, (sub i16:$rs, imm:$rr)),
                         (implicit SREG)]>;

  def SBIWRdK : FWRdK<0b1, (outs IWREGS:$rd), (ins IWREGS:$rs, imm_arith6:$k),
                      "sbiw\t$rd, $k",
                      [(set i16:$rd, (sub i16:$rs, uimm6:$k)),
                       (implicit SREG)]>,
                Requires<[HasADDSUBIW]>;

  // Subtract with carry operations which must read the carry flag in SREG.
  let Uses = [SREG] in {
    def SBCRdRr : FRdRr<0b0000, 0b10, (outs GPR8:$rd), (ins GPR8:$rs, GPR8:$rr),
                        "sbc\t$rd, $rr",
                        [(set i8:$rd, (sube i8:$rs, i8:$rr)), (implicit SREG)]>;

    // SBCW Rd+1:Rd, Rr+1:Rr
    //
    // Expands to:
    // sbc Rd,   Rr
    // sbc Rd+1, Rr+1
    def SBCWRdRr : Pseudo<(outs DREGS:$rd), (ins DREGS:$rs, DREGS:$rr),
                          "sbcw\t$rd, $rr",
                          [(set i16:$rd, (sube i16:$rs, i16:$rr)),
                           (implicit SREG)]>;

    def SBCIRdK : FRdK<0b0100, (outs LD8:$rd), (ins LD8:$rs, imm_ldi8:$k),
                       "sbci\t$rd, $k",
                       [(set i8:$rd, (sube i8:$rs, imm:$k)), (implicit SREG)]>;

    // SBCIW Rd+1:Rd, K+1:K
    // sbci Rd,   K
    // sbci Rd+1, K+1
    def SBCIWRdK : Pseudo<(outs DLDREGS:$rd), (ins DLDREGS:$rs, i16imm:$rr),
                          "sbciw\t$rd, $rr",
                          [(set i16:$rd, (sube i16:$rs, imm:$rr)),
                           (implicit SREG)]>;
  }
}

//===----------------------------------------------------------------------===//
// Increment and Decrement
//===----------------------------------------------------------------------===//
let Constraints = "$src = $rd", Defs = [SREG] in {
  def INCRd : FRd<0b1001, 0b0100011, (outs GPR8:$rd), (ins GPR8:$src),
                  "inc\t$rd",
                  [(set i8:$rd, (add i8:$src, 1)), (implicit SREG)]>;

  def DECRd : FRd<0b1001, 0b0101010, (outs GPR8:$rd), (ins GPR8:$src),
                  "dec\t$rd",
                  [(set i8:$rd, (add i8:$src, -1)), (implicit SREG)]>;
}

//===----------------------------------------------------------------------===//
// Multiplication
//===----------------------------------------------------------------------===//

let isCommutable = 1, Defs = [R1, R0, SREG] in {
  // MUL Rd, Rr
  // Multiplies Rd by Rr and places the result into R1:R0.
  let usesCustomInserter = 1 in {
    def MULRdRr : FRdRr<0b1001, 0b11, (outs), (ins GPR8:$rd, GPR8:$rr),
                        "mul\t$rd, $rr", []>,
                  Requires<[SupportsMultiplication]>;

    def MULSRdRr : FMUL2RdRr<0, (outs), (ins LD8:$rd, LD8:$rr),
                             "muls\t$rd, $rr", []>,
                   Requires<[SupportsMultiplication]>;
  }

  def MULSURdRr : FMUL2RdRr<1, (outs), (ins LD8lo:$rd, LD8lo:$rr),
                            "mulsu\t$rd, $rr", []>,
                  Requires<[SupportsMultiplication]>;

  def FMUL : FFMULRdRr<0b01, (outs), (ins LD8lo:$rd, LD8lo:$rr),
                       "fmul\t$rd, $rr", []>,
             Requires<[SupportsMultiplication]>;

  def FMULS : FFMULRdRr<0b10, (outs), (ins LD8lo:$rd, LD8lo:$rr),
                        "fmuls\t$rd, $rr", []>,
              Requires<[SupportsMultiplication]>;

  def FMULSU : FFMULRdRr<0b11, (outs), (ins LD8lo:$rd, LD8lo:$rr),
                         "fmulsu\t$rd, $rr", []>,
               Requires<[SupportsMultiplication]>;
}

let Defs =
    [R15, R14, R13, R12, R11, R10, R9, R8, R7, R6, R5, R4, R3, R2, R1, R0] in
def DESK : FDES<(outs), (ins i8imm:$k), "des\t$k", []>, Requires<[HasDES]>;

//===----------------------------------------------------------------------===//
// Logic
//===----------------------------------------------------------------------===//
let Constraints = "$src = $rd", Defs = [SREG] in {
  // Register-Register logic instructions (which have the
  // property of commutativity).
  let isCommutable = 1 in {
    def ANDRdRr : FRdRr<0b0010, 0b00, (outs GPR8:$rd),
                        (ins GPR8:$src, GPR8:$rr), "and\t$rd, $rr",
                        [(set i8:$rd, (and i8:$src, i8:$rr)), (implicit SREG)]>;

    // ANDW Rd+1:Rd, Rr+1:Rr
    //
    // Expands to:
    // and Rd,   Rr
    // and Rd+1, Rr+1
    def ANDWRdRr : Pseudo<(outs DREGS:$rd), (ins DREGS:$src, DREGS:$rr),
                          "andw\t$rd, $rr",
                          [(set i16:$rd, (and i16:$src, i16:$rr)),
                           (implicit SREG)]>;

    def ORRdRr : FRdRr<0b0010, 0b10, (outs GPR8:$rd), (ins GPR8:$src, GPR8:$rr),
                       "or\t$rd, $rr",
                       [(set i8:$rd, (or i8:$src, i8:$rr)), (implicit SREG)]>;

    // ORW Rd+1:Rd, Rr+1:Rr
    //
    // Expands to:
    // or Rd,   Rr
    // or Rd+1, Rr+1
    def ORWRdRr : Pseudo<(outs DREGS:$rd), (ins DREGS:$src, DREGS:$rr),
                         "orw\t$rd, $rr",
                         [(set i16:$rd, (or i16:$src, i16:$rr)),
                          (implicit SREG)]>;

    def EORRdRr : FRdRr<0b0010, 0b01, (outs GPR8:$rd),
                        (ins GPR8:$src, GPR8:$rr), "eor\t$rd, $rr",
                        [(set i8:$rd, (xor i8:$src, i8:$rr)), (implicit SREG)]>;

    // EORW Rd+1:Rd, Rr+1:Rr
    //
    // Expands to:
    // eor Rd,   Rr
    // eor Rd+1, Rr+1
    def EORWRdRr : Pseudo<(outs DREGS:$rd), (ins DREGS:$src, DREGS:$rr),
                          "eorw\t$rd, $rr",
                          [(set i16:$rd, (xor i16:$src, i16:$rr)),
                           (implicit SREG)]>;
  }

  def ANDIRdK : FRdK<0b0111, (outs LD8:$rd), (ins LD8:$src, imm_ldi8:$k),
                     "andi\t$rd, $k",
                     [(set i8:$rd, (and i8:$src, imm:$k)), (implicit SREG)]>;

  // ANDI Rd+1:Rd, K+1:K
  //
  // Expands to:
  // andi Rd,   K
  // andi Rd+1, K+1
  def ANDIWRdK : Pseudo<(outs DLDREGS:$rd), (ins DLDREGS:$src, i16imm:$k),
                        "andiw\t$rd, $k",
                        [(set i16:$rd, (and i16:$src, imm:$k)),
                         (implicit SREG)]>;

  def ORIRdK : FRdK<0b0110, (outs LD8:$rd), (ins LD8:$src, imm_ldi8:$k),
                    "ori\t$rd, $k",
                    [(set i8:$rd, (or i8:$src, imm:$k)), (implicit SREG)]>;

  // ORIW Rd+1:Rd, K+1,K
  //
  // Expands to:
  // ori Rd,   K
  // ori Rd+1, K+1
  def ORIWRdK : Pseudo<(outs DLDREGS:$rd), (ins DLDREGS:$src, i16imm:$rr),
                       "oriw\t$rd, $rr",
                       [(set i16:$rd, (or i16:$src, imm:$rr)),
                        (implicit SREG)]>;
}

//===----------------------------------------------------------------------===//
// One's/Two's Complement
//===----------------------------------------------------------------------===//
let Constraints = "$src = $rd", Defs = [SREG] in {
  def COMRd : FRd<0b1001, 0b0100000, (outs GPR8:$rd), (ins GPR8:$src),
                  "com\t$rd", [(set i8:$rd, (not i8:$src)), (implicit SREG)]>;

  // COMW Rd+1:Rd
  //
  // Expands to:
  // com Rd
  // com Rd+1
  def COMWRd : Pseudo<(outs DREGS:$rd), (ins DREGS:$src), "comw\t$rd",
                      [(set i16:$rd, (not i16:$src)), (implicit SREG)]>;

  def NEGRd : FRd<0b1001, 0b0100001, (outs GPR8:$rd), (ins GPR8:$src),
                  "neg\t$rd", [(set i8:$rd, (ineg i8:$src)), (implicit SREG)]>;

  // NEGW Rd+1:Rd
  //
  // Expands to:
  // neg Rd+1
  // neg Rd
  // sbc Rd+1, r1
  let hasSideEffects=0 in
  def NEGWRd : Pseudo<(outs DREGS:$rd), (ins DREGS:$src, GPR8:$zero),
                      "negw\t$rd", []>;
}

// TST Rd
// Test for zero of minus.
// This operation is identical to a `Rd AND Rd`.
def : InstAlias<"tst\t$rd", (ANDRdRr GPR8:$rd, GPR8:$rd)>;

// SBR Rd, K
//
// Mnemonic alias to 'ORI Rd, K'. Same bit pattern, same operands,
// same everything.
def : InstAlias<"sbr\t$rd, $k", (ORIRdK LD8:$rd, imm_ldi8:$k),
                /* Disable display, so we don't override ORI */ 0>;

//===----------------------------------------------------------------------===//
// Jump instructions
//===----------------------------------------------------------------------===//
let isBarrier = 1, isBranch = 1, isTerminator = 1 in {
  def RJMPk : FBRk<0, (outs), (ins brtarget_13:$k), "rjmp\t$k", [(br bb:$k)]>;

  let isIndirectBranch = 1, Uses = [R31R30] in
  def IJMP : F16<0b1001010000001001, (outs), (ins), "ijmp", []>,
             Requires<[HasIJMPCALL]>;

  let isIndirectBranch = 1, Uses = [R31R30] in
  def EIJMP : F16<0b1001010000011001, (outs), (ins), "eijmp", []>,
              Requires<[HasEIJMPCALL]>;

  def JMPk : F32BRk<0b110, (outs), (ins call_target:$k), "jmp\t$k", []>,
             Requires<[HasJMPCALL]>;
}

//===----------------------------------------------------------------------===//
// Call instructions
//===----------------------------------------------------------------------===//
let isCall = 1 in {
  // SP is marked as a use to prevent stack-pointer assignments that appear
  // immediately before calls from potentially appearing dead.
  let Uses = [SP] in
  def RCALLk : FBRk<1, (outs), (ins rcalltarget_13:$k), "rcall\t$k",
                    [(AVRcall imm:$k)]>;

  // SP is marked as a use to prevent stack-pointer assignments that appear
  // immediately before calls from potentially appearing dead.
  let Uses = [SP, R31R30] in
  def ICALL : F16<0b1001010100001001, (outs), (ins variable_ops), "icall", []>,
              Requires<[HasIJMPCALL]>;

  // SP is marked as a use to prevent stack-pointer assignments that appear
  // immediately before calls from potentially appearing dead.
  let Uses = [SP, R31R30] in
  def EICALL : F16<0b1001010100011001, (outs), (ins variable_ops), "eicall",
                   []>,
      Requires<[HasEIJMPCALL]>;

  // SP is marked as a use to prevent stack-pointer assignments that appear
  // immediately before calls from potentially appearing dead.
  //
  // TODO: the imm field can be either 16 or 22 bits in devices with more
  // than 64k of ROM, fix it once we support the largest devices.
  let Uses = [SP] in
  def CALLk : F32BRk<0b111, (outs), (ins call_target:$k), "call\t$k",
                     [(AVRcall imm:$k)]>,
              Requires<[HasJMPCALL]>;
}

//===----------------------------------------------------------------------===//
// Return instructions.
//===----------------------------------------------------------------------===//
let isTerminator = 1, isReturn = 1, isBarrier = 1 in {
  def RET : F16<0b1001010100001000, (outs), (ins), "ret", [(AVRretglue)]>;

  def RETI : F16<0b1001010100011000, (outs), (ins), "reti", [(AVRretiglue)]>;
}

//===----------------------------------------------------------------------===//
// Compare operations.
//===----------------------------------------------------------------------===//
let Defs = [SREG] in {
  // CPSE Rd, Rr
  // Compare Rd and Rr, skipping the next instruction if they are equal.
  let isBarrier = 1, isBranch = 1, isTerminator = 1 in
  def CPSE : FRdRr<0b0001, 0b00, (outs), (ins GPR8:$rd, GPR8:$rr),
                   "cpse\t$rd, $rr", []>;

  def CPRdRr : FRdRr<0b0001, 0b01, (outs), (ins GPR8:$rd, GPR8:$rr),
                     "cp\t$rd, $rr",
                     [(AVRcmp i8:$rd, i8:$rr), (implicit SREG)]>;

  // CPW Rd+1:Rd, Rr+1:Rr
  //
  // Expands to:
  // cp  Rd,   Rr
  // cpc Rd+1, Rr+1
  def CPWRdRr : Pseudo<(outs), (ins DREGS:$src, DREGS:$src2),
                       "cpw\t$src, $src2",
                       [(AVRcmp i16:$src, i16:$src2), (implicit SREG)]>;

  let Uses = [SREG] in
  def CPCRdRr : FRdRr<0b0000, 0b01, (outs), (ins GPR8:$rd, GPR8:$rr),
                      "cpc\t$rd, $rr",
                      [(AVRcmpc i8:$rd, i8:$rr), (implicit SREG)]>;

  // CPCW Rd+1:Rd. Rr+1:Rr
  //
  // Expands to:
  // cpc Rd,   Rr
  // cpc Rd+1, Rr+1
  let Uses = [SREG] in
  def CPCWRdRr : Pseudo<(outs), (ins DREGS:$src, DREGS:$src2),
                        "cpcw\t$src, $src2",
                        [(AVRcmpc i16:$src, i16:$src2), (implicit SREG)]>;

  // CPI Rd, K
  // Compares a register with an 8 bit immediate.
  def CPIRdK : FRdK<0b0011, (outs), (ins LD8:$rd, imm_ldi8:$k), "cpi\t$rd, $k",
                    [(AVRcmp i8:$rd, imm:$k), (implicit SREG)]>;
}

//===----------------------------------------------------------------------===//
// Register conditional skipping/branching operations.
//===----------------------------------------------------------------------===//
let isBranch = 1, isTerminator = 1 in {
  // Conditional skipping on GPR register bits, and
  // conditional skipping on IO register bits.
  let isBarrier = 1 in {
    def SBRCRrB : FRdB<0b10, (outs), (ins GPR8:$rd, i8imm:$b), "sbrc\t$rd, $b",
                       []>;

    def SBRSRrB : FRdB<0b11, (outs), (ins GPR8:$rd, i8imm:$b), "sbrs\t$rd, $b",
                       []>;

    def SBICAb : FIOBIT<0b01, (outs), (ins imm_port5:$addr, i8imm:$b),
                        "sbic\t$addr, $b", []>;

    def SBISAb : FIOBIT<0b11, (outs), (ins imm_port5:$addr, i8imm:$b),
                        "sbis\t$addr, $b", []>;
  }

  // Relative branches on status flag bits.
  let Uses = [SREG] in {
    // BRBS s, k
    // Branch if `s` flag in status register is set.
    def BRBSsk : FSK<0, (outs), (ins i8imm:$s, relbrtarget_7:$k),
                     "brbs\t$s, $k", []>;

    // BRBC s, k
    // Branch if `s` flag in status register is clear.
    def BRBCsk : FSK<1, (outs), (ins i8imm:$s, relbrtarget_7:$k),
                     "brbc\t$s, $k", []>;
  }
}

// BRCS k
// Branch if carry flag is set
def : InstAlias<"brcs\t$k", (BRBSsk 0, relbrtarget_7 : $k)>;

// BRCC k
// Branch if carry flag is clear
def : InstAlias<"brcc\t$k", (BRBCsk 0, relbrtarget_7 : $k)>;

// BRHS k
// Branch if half carry flag is set
def : InstAlias<"brhs\t$k", (BRBSsk 5, relbrtarget_7 : $k)>;

// BRHC k
// Branch if half carry flag is clear
def : InstAlias<"brhc\t$k", (BRBCsk 5, relbrtarget_7 : $k)>;

// BRTS k
// Branch if the T flag is set
def : InstAlias<"brts\t$k", (BRBSsk 6, relbrtarget_7 : $k)>;

// BRTC k
// Branch if the T flag is clear
def : InstAlias<"brtc\t$k", (BRBCsk 6, relbrtarget_7 : $k)>;

// BRVS k
// Branch if the overflow flag is set
def : InstAlias<"brvs\t$k", (BRBSsk 3, relbrtarget_7 : $k)>;

// BRVC k
// Branch if the overflow flag is clear
def : InstAlias<"brvc\t$k", (BRBCsk 3, relbrtarget_7 : $k)>;

// BRIE k
// Branch if the global interrupt flag is enabled
def : InstAlias<"brie\t$k", (BRBSsk 7, relbrtarget_7 : $k)>;

// BRID k
// Branch if the global interrupt flag is disabled
def : InstAlias<"brid\t$k", (BRBCsk 7, relbrtarget_7 : $k)>;

//===----------------------------------------------------------------------===//
// PC-relative conditional branches
//===----------------------------------------------------------------------===//
// Based on status register. We cannot simplify these into instruction aliases
// because we also need to be able to specify a pattern to match for ISel.
let isBranch = 1, isTerminator = 1, Uses = [SREG] in {
  def BREQk : FBRsk<0, 0b001, (outs), (ins relbrtarget_7:$k), "breq\t$k",
                    [(AVRbrcond bb:$k, AVR_COND_EQ)]>;

  def BRNEk : FBRsk<1, 0b001, (outs), (ins relbrtarget_7:$k), "brne\t$k",
                    [(AVRbrcond bb:$k, AVR_COND_NE)]>;

  def BRSHk : FBRsk<1, 0b000, (outs), (ins relbrtarget_7:$k), "brsh\t$k",
                    [(AVRbrcond bb:$k, AVR_COND_SH)]>;

  def BRLOk : FBRsk<0, 0b000, (outs), (ins relbrtarget_7:$k), "brlo\t$k",
                    [(AVRbrcond bb:$k, AVR_COND_LO)]>;

  def BRMIk : FBRsk<0, 0b010, (outs), (ins relbrtarget_7:$k), "brmi\t$k",
                    [(AVRbrcond bb:$k, AVR_COND_MI)]>;

  def BRPLk : FBRsk<1, 0b010, (outs), (ins relbrtarget_7:$k), "brpl\t$k",
                    [(AVRbrcond bb:$k, AVR_COND_PL)]>;

  def BRGEk : FBRsk<1, 0b100, (outs), (ins relbrtarget_7:$k), "brge\t$k",
                    [(AVRbrcond bb:$k, AVR_COND_GE)]>;

  def BRLTk : FBRsk<0, 0b100, (outs), (ins relbrtarget_7:$k), "brlt\t$k",
                    [(AVRbrcond bb:$k, AVR_COND_LT)]>;
}

//===----------------------------------------------------------------------===//
// Data transfer instructions
//===----------------------------------------------------------------------===//
// 8 and 16-bit register move instructions.
let hasSideEffects = 0 in {
  def MOVRdRr : FRdRr<0b0010, 0b11, (outs GPR8:$rd), (ins GPR8:$rr),
                      "mov\t$rd, $rr", []>;

  def MOVWRdRr : FMOVWRdRr<(outs DREGS:$rd), (ins DREGS:$rr), "movw\t$rd, $rr",
                           []>,
                 Requires<[HasMOVW]>;
}

// Load immediate values into registers.
let isReMaterializable = 1 in {
  def LDIRdK : FRdK<0b1110, (outs LD8:$rd), (ins imm_ldi8:$k), "ldi\t$rd, $k",
                    [(set i8:$rd, imm:$k)]>;

  // LDIW Rd+1:Rd, K+1:K
  //
  // Expands to:
  // ldi Rd,   K
  // ldi Rd+1, K+1
  def LDIWRdK : Pseudo<(outs DLDREGS:$dst), (ins i16imm:$src),
                       "ldiw\t$dst, $src", [(set i16:$dst, imm:$src)]>;
}

// Load from data space into register.
let canFoldAsLoad = 1, isReMaterializable = 1 in {
  def LDSRdK : F32DM<0b0, (outs GPR8:$rd), (ins imm16:$k), "lds\t$rd, $k",
                     [(set i8:$rd, (load imm:$k))]>,
               Requires<[HasSRAM, HasNonTinyEncoding]>;

  // Load from data space into register, which is only available on AVRTiny.
  def LDSRdKTiny : FLDSSTSTINY<0b0, (outs LD8:$rd), (ins imm7tiny:$k),
                               "lds\t$rd, $k", [(set i8:$rd, (load imm:$k))]>,
                   Requires<[HasSRAM, HasTinyEncoding]>;

  // LDSW Rd+1:Rd, K+1:K
  //
  // Expands to:
  // lds Rd,  (K+1:K)
  // lds Rd+1 (K+1:K) + 1
  def LDSWRdK : Pseudo<(outs DREGS:$dst), (ins i16imm:$src), "ldsw\t$dst, $src",
                       [(set i16:$dst, (load imm:$src))]>,
                Requires<[HasSRAM, HasNonTinyEncoding]>;
}

// Indirect loads.
let canFoldAsLoad = 1, isReMaterializable = 1 in {
  def LDRdPtr : FSTLD<0, 0b00, (outs GPR8:$reg), (ins LDSTPtrReg:$ptrreg),
                      "ld\t$reg, $ptrreg",
                      [(set GPR8:$reg, (load i16:$ptrreg))]>,
                Requires<[HasSRAM]>;

  // LDW Rd+1:Rd, P
  //
  // Expands to:
  //   ld  Rd,   P
  //   ldd Rd+1, P+1
  // On reduced tiny cores, this instruction expands to:
  //   ld    Rd,   P+
  //   ld    Rd+1, P+
  //   subiw P,    2
  let Constraints = "@earlyclobber $reg" in def LDWRdPtr
      : Pseudo<(outs DREGS:$reg), (ins PTRDISPREGS:$ptrreg),
                "ldw\t$reg, $ptrreg", [(set i16:$reg, (load i16:$ptrreg))]>,
      Requires<[HasSRAM]>;
}

// Indirect loads (with postincrement or predecrement).
let mayLoad = 1, hasSideEffects = 0,
    Constraints = "$ptrreg = $base_wb,@earlyclobber $reg" in {
  def LDRdPtrPi : FSTLD<0, 0b01,
                        (outs GPR8
                         : $reg, PTRREGS
                         : $base_wb),
                        (ins LDSTPtrReg
                         : $ptrreg),
                        "ld\t$reg, $ptrreg+", []>,
                  Requires<[HasSRAM]>;

  // LDW Rd+1:Rd, P+
  // Expands to:
  // ld Rd,   P+
  // ld Rd+1, P+
  def LDWRdPtrPi : Pseudo<(outs DREGS:$reg, PTRREGS:$base_wb),
                          (ins PTRREGS:$ptrreg), "ldw\t$reg, $ptrreg+", []>,
                   Requires<[HasSRAM]>;

  def LDRdPtrPd : FSTLD<0, 0b10, (outs GPR8:$reg, PTRREGS:$base_wb),
                        (ins LDSTPtrReg:$ptrreg), "ld\t$reg, -$ptrreg", []>,
                  Requires<[HasSRAM]>;

  // LDW Rd+1:Rd, -P
  //
  // Expands to:
  // ld Rd+1, -P
  // ld Rd,   -P
  def LDWRdPtrPd : Pseudo<(outs DREGS:$reg, PTRREGS:$base_wb),
                          (ins PTRREGS:$ptrreg), "ldw\t$reg, -$ptrreg", []>,
                   Requires<[HasSRAM]>;
}

// Load indirect with displacement operations.
let canFoldAsLoad = 1, isReMaterializable = 1 in {
  def LDDRdPtrQ : FSTDLDD<0, (outs GPR8:$reg), (ins memri:$memri),
                          "ldd\t$reg, $memri",
                          [(set i8:$reg, (load addr:$memri))]>,
                  Requires<[HasSRAM, HasNonTinyEncoding]>;

  // LDDW Rd+1:Rd, P+q
  //
  // Expands to:
  //   ldd Rd,   P+q
  //   ldd Rd+1, P+q+1
  // On reduced tiny cores, this instruction expands to:
  //   subiw P,    -q
  //   ld    Rd,   P+
  //   ld    Rd+1, P+
  //   subiw P,    q+2
  let Constraints = "@earlyclobber $dst" in
  def LDDWRdPtrQ : Pseudo<(outs DREGS:$dst), (ins memri:$memri),
                          "lddw\t$dst, $memri",
                          [(set i16:$dst, (load addr:$memri))]>,
                   Requires<[HasSRAM]>;

  // An identical pseudo instruction to LDDWRdPtrQ, expect restricted to the Y
  // register and without the @earlyclobber flag.
  //
  // Used to work around a bug caused by the register allocator not
  // being able to handle the expansion of a COPY into an machine instruction
  // that has an earlyclobber flag. This is because the register allocator will
  // try expand a copy from a register slot into an earlyclobber instruction.
  // Instructions that are earlyclobber need to be in a dedicated earlyclobber
  // slot.
  //
  // This pseudo instruction can be used pre-AVR pseudo expansion in order to
  // get a frame index load without directly using earlyclobber instructions.
  //
  // The pseudo expansion pass trivially expands this into LDDWRdPtrQ.
  //
  // This instruction may be removed once PR13375 is fixed.
  let mayLoad = 1, hasSideEffects = 0 in
  def LDDWRdYQ : Pseudo<(outs DREGS:$dst), (ins memri:$memri),
                        "lddw\t$dst, $memri", []>,
                 Requires<[HasSRAM]>;
}

class AtomicLoad<PatFrag Op, RegisterClass DRC, RegisterClass PTRRC>
    : Pseudo<(outs DRC:$rd), (ins PTRRC:$rr), "atomic_op",
             [(set DRC:$rd, (Op i16:$rr))]>;

class AtomicStore<PatFrag Op, RegisterClass DRC, RegisterClass PTRRC>
    : Pseudo<(outs), (ins PTRRC:$rd, DRC:$rr), "atomic_op",
             [(Op DRC:$rr, i16:$rd)]>;

class AtomicLoadOp<PatFrag Op, RegisterClass DRC, RegisterClass PTRRC>
    : Pseudo<(outs DRC:$rd), (ins PTRRC:$rr, DRC:$operand), "atomic_op",
             [(set DRC:$rd, (Op i16:$rr, DRC:$operand))]>;

// Atomic instructions
// ===================
//
// 8-bit operations can use any pointer register because
// they are expanded directly into an LD/ST instruction.
//
// 16-bit operations use 16-bit load/store postincrement instructions,
// which require PTRDISPREGS.

def AtomicLoad8 : AtomicLoad<atomic_load_8, GPR8, PTRREGS>;
def AtomicLoad16 : AtomicLoad<atomic_load_16, DREGS, PTRDISPREGS>;

def AtomicStore8 : AtomicStore<atomic_store_8, GPR8, PTRREGS>;
def AtomicStore16 : AtomicStore<atomic_store_16, DREGS, PTRDISPREGS>;

class AtomicLoadOp8<PatFrag Op> : AtomicLoadOp<Op, GPR8, PTRREGS>;
class AtomicLoadOp16<PatFrag Op> : AtomicLoadOp<Op, DREGS, PTRDISPREGS>;

let usesCustomInserter=1 in {
  def AtomicLoadAdd8 : AtomicLoadOp8<atomic_load_add_i8>;
  def AtomicLoadAdd16 : AtomicLoadOp16<atomic_load_add_i16>;
  def AtomicLoadSub8 : AtomicLoadOp8<atomic_load_sub_i8>;
  def AtomicLoadSub16 : AtomicLoadOp16<atomic_load_sub_i16>;
  def AtomicLoadAnd8 : AtomicLoadOp8<atomic_load_and_i8>;
  def AtomicLoadAnd16 : AtomicLoadOp16<atomic_load_and_i16>;
  def AtomicLoadOr8 : AtomicLoadOp8<atomic_load_or_i8>;
  def AtomicLoadOr16 : AtomicLoadOp16<atomic_load_or_i16>;
  def AtomicLoadXor8 : AtomicLoadOp8<atomic_load_xor_i8>;
  def AtomicLoadXor16 : AtomicLoadOp16<atomic_load_xor_i16>;
}

def AtomicFence
    : Pseudo<(outs), (ins), "atomic_fence", [(atomic_fence timm, timm)]>;

// Indirect store from register to data space.
def STSKRr : F32DM<0b1, (outs), (ins imm16:$k, GPR8:$rd), "sts\t$k, $rd",
                   [(store i8:$rd, imm:$k)]>,
             Requires<[HasSRAM, HasNonTinyEncoding]>;

// Store from register to data space, which is only available on AVRTiny.
def STSKRrTiny : FLDSSTSTINY<0b1, (outs), (ins imm7tiny:$k, LD8:$rd),
                             "sts\t$k, $rd", [(store i8:$rd, imm:$k)]>,
                 Requires<[HasSRAM, HasTinyEncoding]>;

// STSW K+1:K, Rr+1:Rr
//
// Expands to:
// sts Rr+1, (K+1:K) + 1
// sts Rr,   (K+1:K)
def STSWKRr : Pseudo<(outs), (ins i16imm:$dst, DREGS:$src),
                     "stsw\t$dst, $src", [(store i16:$src, imm:$dst)]>,
              Requires<[HasSRAM, HasNonTinyEncoding]>;

// Indirect stores.
// ST P, Rr
// Stores the value of Rr into the location addressed by pointer P.
def STPtrRr : FSTLD<1, 0b00, (outs), (ins LDSTPtrReg:$ptrreg, GPR8:$reg),
                    "st\t$ptrreg, $reg", [(store GPR8:$reg, i16:$ptrreg)]>,
              Requires<[HasSRAM]>;

// STW P, Rr+1:Rr
// Stores the value of Rr into the location addressed by pointer P.
//
// Expands to:
//   st P, Rr
//   std P+1, Rr+1
// On reduced tiny cores, this instruction expands to:
//   st    P+, Rr
//   st    P+, Rr+1
//   subiw P,  q+2
def STWPtrRr : Pseudo<(outs), (ins PTRDISPREGS:$ptrreg, DREGS:$reg),
                      "stw\t$ptrreg, $reg", [(store i16:$reg, i16:$ptrreg)]>,
               Requires<[HasSRAM]>;

// Indirect stores (with postincrement or predecrement).
let Constraints = "$ptrreg = $base_wb,@earlyclobber $base_wb" in {

  // ST P+, Rr
  // Stores the value of Rr into the location addressed by pointer P.
  // Post increments P.
  def STPtrPiRr : FSTLD<1, 0b01, (outs LDSTPtrReg:$base_wb),
                        (ins LDSTPtrReg:$ptrreg, GPR8:$reg, i8imm:$offs),
                        "st\t$ptrreg+, $reg",
                        [(set i16:$base_wb, (post_store GPR8:$reg, i16:$ptrreg,
                         imm:$offs))]>,
                  Requires<[HasSRAM]>;

  // STW P+, Rr+1:Rr
  // Stores the value of Rr into the location addressed by pointer P.
  // Post increments P.
  //
  // Expands to:
  // st P+, Rr
  // st P+, Rr+1
  def STWPtrPiRr : Pseudo<(outs PTRREGS:$base_wb),
                          (ins PTRREGS:$ptrreg, DREGS:$trh, i8imm:$offs),
                          "stw\t$ptrreg+, $trh",
                          [(set PTRREGS:$base_wb,
                           (post_store DREGS:$trh, PTRREGS:$ptrreg,
                            imm:$offs))]>,
                   Requires<[HasSRAM]>;

  // ST -P, Rr
  // Stores the value of Rr into the location addressed by pointer P.
  // Pre decrements P.
  def STPtrPdRr : FSTLD<1, 0b10, (outs LDSTPtrReg:$base_wb),
                        (ins LDSTPtrReg:$ptrreg, GPR8:$reg, i8imm:$offs),
                        "st\t-$ptrreg, $reg",
                        [(set i16: $base_wb,
                         (pre_store GPR8:$reg, i16:$ptrreg, imm:$offs))]>,
                  Requires<[HasSRAM]>;

  // STW -P, Rr+1:Rr
  // Stores the value of Rr into the location addressed by pointer P.
  // Pre decrements P.
  //
  // Expands to:
  // st -P, Rr+1
  // st -P, Rr
  def STWPtrPdRr : Pseudo<(outs PTRREGS:$base_wb),
                          (ins PTRREGS:$ptrreg, DREGS:$reg, i8imm:$offs),
                          "stw\t-$ptrreg, $reg",
                          [(set PTRREGS:$base_wb,
                           (pre_store i16:$reg, i16:$ptrreg, imm:$offs))]>,
                   Requires<[HasSRAM]>;
}

// Store indirect with displacement operations.
// STD P+q, Rr
// Stores the value of Rr into the location addressed by pointer P with a
// displacement of q. Does not modify P.
def STDPtrQRr : FSTDLDD<1, (outs), (ins memri:$memri, GPR8:$reg),
                        "std\t$memri, $reg", [(store i8:$reg, addr:$memri)]>,
                Requires<[HasSRAM, HasNonTinyEncoding]>;

// STDW P+q, Rr+1:Rr
// Stores the value of Rr into the location addressed by pointer P with a
// displacement of q. Does not modify P.
//
// Expands to:
//   std P+q,   Rr
//   std P+q+1, Rr+1
// On reduced tiny cores, this instruction expands to:
//   subiw P,  -q
//   st    P+, Rr
//   st    P+, Rr+1
//   subiw P,  q+2
def STDWPtrQRr : Pseudo<(outs), (ins memri:$memri, DREGS:$src),
                        "stdw\t$memri, $src", [(store i16:$src, addr:$memri)]>,
                 Requires<[HasSRAM]>;

// Load program memory operations.
let canFoldAsLoad = 1, isReMaterializable = 1, mayLoad = 1,
    hasSideEffects = 0 in {
  let Defs = [R0],
      Uses = [R31R30] in def LPM
      : F16<0b1001010111001000, (outs), (ins), "lpm", []>,
      Requires<[HasLPM]>;

  // These pseudo instructions are combination of the OUT and LPM instructions.
  let Defs = [R0] in {
    def LPMBRdZ : Pseudo<(outs GPR8:$dst), (ins ZREG:$z), "lpmb\t$dst, $z", []>,
                  Requires<[HasLPM]>;

    let Constraints = "@earlyclobber $dst" in
    def LPMWRdZ : Pseudo<(outs DREGS:$dst), (ins ZREG:$z), "lpmw\t$dst, $z", []>,
                  Requires<[HasLPM]>;
  }

  def LPMRdZ : FLPMX<0, 0,
                     (outs GPR8
                      : $rd),
                     (ins ZREG
                      : $z),
                     "lpm\t$rd, $z", []>,
               Requires<[HasLPMX]>;

  // Load program memory, while postincrementing the Z register.
  let Defs = [R31R30] in {
    def LPMRdZPi : FLPMX<0, 1,
                         (outs GPR8
                          : $rd),
                         (ins ZREG
                          : $z),
                         "lpm\t$rd, $z+", []>,
                   Requires<[HasLPMX]>;

    def LPMWRdZPi : Pseudo<(outs DREGS
                            : $dst),
                           (ins ZREG
                            : $z),
                           "lpmw\t$dst, $z+", []>,
                    Requires<[HasLPMX]>;
  }
}

// Extended load program memory operations.
let mayLoad = 1, hasSideEffects = 0 in {
  let Defs = [R0],
      Uses = [R31R30] in def ELPM
      : F16<0b1001010111011000, (outs), (ins), "elpm", []>,
      Requires<[HasELPM]>;

  def ELPMRdZ : FLPMX<1, 0, (outs GPR8:$rd), (ins ZREG:$z),
                      "elpm\t$rd, $z", []>,
                Requires<[HasELPMX]>;

  let Defs = [R31R30] in {
    def ELPMRdZPi : FLPMX<1, 1, (outs GPR8:$rd), (ins ZREG:$z),
                          "elpm\t$rd, $z+", []>,
                    Requires<[HasELPMX]>;
  }

  // These pseudo instructions are combination of the OUT and ELPM instructions.
  let Defs = [R0] in {
    def ELPMBRdZ : Pseudo<(outs GPR8:$dst), (ins ZREG:$z, LD8:$p),
                          "elpmb\t$dst, $z, $p", []>,
                   Requires<[HasELPM]>;

    let Constraints = "@earlyclobber $dst" in
    def ELPMWRdZ : Pseudo<(outs DREGS:$dst), (ins ZREG:$z, LD8:$p),
                          "elpmw\t$dst, $z, $p", []>,
                   Requires<[HasELPM]>;
  }

  // These pseudos are combination of the OUT and ELPM instructions.
  let Defs = [R31R30], hasSideEffects = 1 in {
    def ELPMBRdZPi : Pseudo<(outs GPR8:$dst), (ins ZREG:$z, LD8:$p),
                            "elpmb\t$dst, $z+, $p", []>,
                     Requires<[HasELPMX]>;

    def ELPMWRdZPi : Pseudo<(outs DREGS:$dst), (ins ZREG:$z, LD8:$p),
                            "elpmw\t$dst, $z+, $p", []>,
                     Requires<[HasELPMX]>;
  }
}

// Store program memory operations.
let Uses = [R1, R0] in {
  let Uses = [R31R30, R1, R0] in def SPM
      : F16<0b1001010111101000, (outs), (ins), "spm", []>,
      Requires<[HasSPM]>;

  let Defs = [R31R30] in def SPMZPi : F16<0b1001010111111000, (outs),
                                          (ins ZREG
                                           : $z),
                                          "spm $z+", []>,
      Requires<[HasSPMX]>;
}

// Read data from IO location operations.
let canFoldAsLoad = 1, isReMaterializable = 1 in {
  def INRdA : FIORdA<(outs GPR8
                      : $rd),
                     (ins imm_port6
                      : $A),
                     "in\t$rd, $A", [(set i8
                                         : $rd, (load ioaddr8
                                                  : $A))]>;

  def INWRdA : Pseudo<(outs DREGS
                       : $dst),
                      (ins imm_port6
                       : $src),
                      "inw\t$dst, $src", [(set i16
                                           : $dst, (load ioaddr16
                                                    : $src))]>;
}

// Write data to IO location operations.
def OUTARr : FIOARr<(outs),
                    (ins imm_port6
                     : $A, GPR8
                     : $rr),
                    "out\t$A, $rr", [(store i8
                                         : $rr, ioaddr8
                                         : $A)]>;

def OUTWARr : Pseudo<(outs),
                     (ins imm_port6
                      : $dst, DREGS
                      : $src),
                     "outw\t$dst, $src", [(store i16
                                           : $src, ioaddr16
                                           : $dst)]>;

// Stack push/pop operations.
let Defs = [SP], Uses = [SP], hasSideEffects = 0 in {
  // Stack push operations.
  let mayStore = 1 in {
    def PUSHRr : FRd<0b1001, 0b0011111, (outs),
                     (ins GPR8
                      : $rd),
                     "push\t$rd", []>,
                 Requires<[HasSRAM]>;

    def PUSHWRr : Pseudo<(outs),
                         (ins DREGS
                          : $reg),
                         "pushw\t$reg", []>,
                  Requires<[HasSRAM]>;
  }

  // Stack pop operations.
  let mayLoad = 1 in {
    def POPRd : FRd<0b1001, 0b0001111,
                    (outs GPR8
                     : $rd),
                    (ins), "pop\t$rd", []>,
                Requires<[HasSRAM]>;

    def POPWRd : Pseudo<(outs DREGS
                         : $reg),
                        (ins), "popw\t$reg", []>,
                 Requires<[HasSRAM]>;
  }
}

// Read-Write-Modify (RMW) instructions.
def XCHZRd : FZRd<0b100,
                  (outs GPR8
                   : $rd),
                  (ins ZREG
                   : $z),
                  "xch\t$z, $rd", []>,
             Requires<[SupportsRMW]>;

def LASZRd : FZRd<0b101,
                  (outs GPR8
                   : $rd),
                  (ins ZREG
                   : $z),
                  "las\t$z, $rd", []>,
             Requires<[SupportsRMW]>;

def LACZRd : FZRd<0b110,
                  (outs GPR8
                   : $rd),
                  (ins ZREG
                   : $z),
                  "lac\t$z, $rd", []>,
             Requires<[SupportsRMW]>;

def LATZRd : FZRd<0b111,
                  (outs GPR8
                   : $rd),
                  (ins ZREG
                   : $z),
                  "lat\t$z, $rd", []>,
             Requires<[SupportsRMW]>;

//===----------------------------------------------------------------------===//
// Bit and bit-test instructions
//===----------------------------------------------------------------------===//

// Bit shift/rotate operations.
let Constraints = "$src = $rd", Defs = [SREG] in {
  // 8-bit LSL is an alias of ADD Rd, Rd

  def LSLWRd : Pseudo<(outs DREGS
                       : $rd),
                      (ins DREGS
                       : $src),
                      "lslw\t$rd",
                      [(set i16
                        : $rd, (AVRlsl i16
                                : $src)),
                       (implicit SREG)]>;

  def LSLWHiRd : Pseudo<(outs DREGS:$rd), (ins DREGS:$src), "lslwhi\t$rd",
                        [(set i16:$rd, (AVRlslhi i16:$src)), (implicit SREG)]>;

  def LSLWNRd : Pseudo<(outs DLDREGS
                        : $rd),
                       (ins DREGS
                        : $src, imm16
                        : $bits),
                       "lslwn\t$rd, $bits", [
                         (set i16
                          : $rd, (AVRlslwn i16
                                  : $src, imm
                                  : $bits)),
                         (implicit SREG)
                       ]>;

  def LSLBNRd : Pseudo<(outs LD8
                        : $rd),
                       (ins GPR8
                        : $src, imm_ldi8
                        : $bits),
                       "lslbn\t$rd, $bits", [
                         (set i8
                          : $rd, (AVRlslbn i8
                                  : $src, imm
                                  : $bits)),
                         (implicit SREG)
                       ]>;

  def LSRRd
      : FRd<0b1001, 0b0100110,
            (outs GPR8
             : $rd),
            (ins GPR8
             : $src),
            "lsr\t$rd", [(set i8
                          : $rd, (AVRlsr i8
                                  : $src)),
                         (implicit SREG)]>;

  def LSRWRd : Pseudo<(outs DREGS
                       : $rd),
                      (ins DREGS
                       : $src),
                      "lsrw\t$rd",
                      [(set i16
                        : $rd, (AVRlsr i16
                                : $src)),
                       (implicit SREG)]>;

  def LSRWLoRd : Pseudo<(outs DREGS:$rd), (ins DREGS:$src), "lsrwlo\t$rd",
                        [(set i16:$rd, (AVRlsrlo i16:$src)), (implicit SREG)]>;

  def LSRWNRd : Pseudo<(outs DLDREGS
                        : $rd),
                       (ins DREGS
                        : $src, imm16
                        : $bits),
                       "lsrwn\t$rd, $bits", [
                         (set i16
                          : $rd, (AVRlsrwn i16
                                  : $src, imm
                                  : $bits)),
                         (implicit SREG)
                       ]>;

  def LSRBNRd : Pseudo<(outs LD8
                        : $rd),
                       (ins GPR8
                        : $src, imm_ldi8
                        : $bits),
                       "lsrbn\t$rd, $bits", [
                         (set i8
                          : $rd, (AVRlsrbn i8
                                  : $src, imm
                                  : $bits)),
                         (implicit SREG)
                       ]>;

  def ASRRd
      : FRd<0b1001, 0b0100101,
            (outs GPR8
             : $rd),
            (ins GPR8
             : $src),
            "asr\t$rd", [(set i8
                          : $rd, (AVRasr i8
                                  : $src)),
                         (implicit SREG)]>;

  def ASRWNRd : Pseudo<(outs DREGS
                        : $rd),
                       (ins DREGS
                        : $src, imm16
                        : $bits),
                       "asrwn\t$rd, $bits", [
                         (set i16
                          : $rd, (AVRasrwn i16
                                  : $src, imm
                                  : $bits)),
                         (implicit SREG)
                       ]>;

  def ASRBNRd : Pseudo<(outs LD8
                        : $rd),
                       (ins GPR8
                        : $src, imm_ldi8
                        : $bits),
                       "asrbn\t$rd, $bits", [
                         (set i8
                          : $rd, (AVRasrbn i8
                                  : $src, imm
                                  : $bits)),
                         (implicit SREG)
                       ]>;

  def ASRWRd : Pseudo<(outs DREGS
                       : $rd),
                      (ins DREGS
                       : $src),
                      "asrw\t$rd",
                      [(set i16
                        : $rd, (AVRasr i16
                                : $src)),
                       (implicit SREG)]>;

  def ASRWLoRd : Pseudo<(outs DREGS:$rd), (ins DREGS:$src), "asrwlo\t$rd",
                        [(set i16:$rd, (AVRasrlo i16:$src)), (implicit SREG)]>;
  let Uses = [R1] in
  def ROLBRdR1 : Pseudo<(outs GPR8:$rd),
                        (ins GPR8:$src),
                        "rolb\t$rd",
                        [(set i8:$rd, (AVRrol i8:$src)),
                        (implicit SREG)]>,
                 Requires<[HasNonTinyEncoding]>;

  let Uses = [R17] in
  def ROLBRdR17 : Pseudo<(outs GPR8:$rd),
                         (ins GPR8:$src),
                         "rolb\t$rd",
                         [(set i8:$rd, (AVRrol i8:$src)),
                         (implicit SREG)]>,
                  Requires<[HasTinyEncoding]>;

  def RORBRd : Pseudo<(outs GPR8
                       : $rd),
                      (ins GPR8
                       : $src),
                      "rorb\t$rd",
                      [(set i8
                        : $rd, (AVRror i8
                                : $src)),
                       (implicit SREG)]>;

  // Bit rotate operations.
  let Uses = [SREG] in {

    def ROLWRd
        : Pseudo<(outs DREGS
                  : $rd),
                 (ins DREGS
                  : $src),
                 "rolw\t$rd",
                 [(set i16
                   : $rd, (AVRrol i16
                           : $src)),
                  (implicit SREG)]>;

    def RORRd : FRd<0b1001, 0b0100111,
                    (outs GPR8
                     : $rd),
                    (ins GPR8
                     : $src),
                    "ror\t$rd", []>;

    def RORWRd
        : Pseudo<(outs DREGS
                  : $rd),
                 (ins DREGS
                  : $src),
                 "rorw\t$rd",
                 [(set i16
                   : $rd, (AVRror i16
                           : $src)),
                  (implicit SREG)]>;
  }
}

// SWAP Rd
// Swaps the high and low nibbles in a register.
let Constraints =
    "$src = $rd" in def SWAPRd : FRd<0b1001, 0b0100010,
                                     (outs GPR8
                                      : $rd),
                                     (ins GPR8
                                      : $src),
                                     "swap\t$rd", [(set i8
                                                    : $rd, (AVRSwap i8
                                                            : $src))]>;

// IO register bit set/clear operations.
//: TODO: add patterns when popcount(imm)==2 to be expanded with 2 sbi/cbi
// instead of in+ori+out which requires one more instr.
def SBIAb : FIOBIT<0b10, (outs),
                   (ins imm_port5
                    : $addr, i8imm
                    : $b),
                   "sbi\t$addr, $b", [(store(or(i8(load lowioaddr8
                                                     : $addr)),
                                               iobitpos8
                                               : $b),
                                         lowioaddr8
                                         : $addr)]>;

def CBIAb : FIOBIT<0b00, (outs),
                   (ins imm_port5
                    : $addr, i8imm
                    : $b),
                   "cbi\t$addr, $b", [(store(and(i8(load lowioaddr8
                                                      : $addr)),
                                               iobitposn8
                                               : $b),
                                         lowioaddr8
                                         : $addr)]>;

// Status register bit load/store operations.
let Defs = [SREG] in def BST : FRdB<0b01, (outs),
                                    (ins GPR8
                                     : $rd, i8imm
                                     : $b),
                                    "bst\t$rd, $b", []>;

let Constraints = "$src = $rd",
    Uses = [SREG] in def BLD : FRdB<0b00,
                                    (outs GPR8
                                     : $rd),
                                    (ins GPR8
                                     : $src, i8imm
                                     : $b),
                                    "bld\t$rd, $b", []>;

def CBR : InstAlias<"cbr\t$rd, $k", (ANDIRdK LD8 : $rd, imm_com8 : $k), 0>;

// CLR Rd
// Alias for EOR Rd, Rd
// -------------
// Clears all bits in a register.
def CLR : InstAlias<"clr\t$rd", (EORRdRr GPR8 : $rd, GPR8 : $rd)>;

// LSL Rd
// Alias for ADD Rd, Rd
// --------------
// Logical shift left one bit.
def LSL : InstAlias<"lsl\t$rd", (ADDRdRr GPR8 : $rd, GPR8 : $rd)>;

def ROL : InstAlias<"rol\t$rd", (ADCRdRr GPR8 : $rd, GPR8 : $rd)>;

// SER Rd
// Alias for LDI Rd, 0xff
// ---------
// Sets all bits in a register.
def : InstAlias<"ser\t$rd", (LDIRdK LD8 : $rd, 0xff), 0>;

let hasSideEffects=1 in {
  let Defs = [SREG] in def BSETs : FS<0,
                                      (outs),
                                      (ins i8imm:$s),
                                      "bset\t$s", []>;

  let Defs = [SREG] in def BCLRs : FS<1,
                                      (outs),
                                      (ins i8imm:$s),
                                      "bclr\t$s", []>;
}

// Set/clear aliases for the carry (C) status flag (bit 0).
def : InstAlias<"sec", (BSETs 0)>;
def : InstAlias<"clc", (BCLRs 0)>;

// Set/clear aliases for the zero (Z) status flag (bit 1).
def : InstAlias<"sez", (BSETs 1)>;
def : InstAlias<"clz", (BCLRs 1)>;

// Set/clear aliases for the negative (N) status flag (bit 2).
def : InstAlias<"sen", (BSETs 2)>;
def : InstAlias<"cln", (BCLRs 2)>;

// Set/clear aliases for the overflow (V) status flag (bit 3).
def : InstAlias<"sev", (BSETs 3)>;
def : InstAlias<"clv", (BCLRs 3)>;

// Set/clear aliases for the signed (S) status flag (bit 4).
def : InstAlias<"ses", (BSETs 4)>;
def : InstAlias<"cls", (BCLRs 4)>;

// Set/clear aliases for the half-carry (H) status flag (bit 5).
def : InstAlias<"seh", (BSETs 5)>;
def : InstAlias<"clh", (BCLRs 5)>;

// Set/clear aliases for the T status flag (bit 6).
def : InstAlias<"set", (BSETs 6)>;
def : InstAlias<"clt", (BCLRs 6)>;

// Set/clear aliases for the interrupt (I) status flag (bit 7).
def : InstAlias<"sei", (BSETs 7)>;
def : InstAlias<"cli", (BCLRs 7)>;

//===----------------------------------------------------------------------===//
// Special/Control instructions
//===----------------------------------------------------------------------===//

// BREAK
// Breakpoint instruction
// ---------
// <|1001|0101|1001|1000>
def BREAK : F16<0b1001010110011000, (outs), (ins), "break", []>,
            Requires<[HasBREAK]>;

// NOP
// No-operation instruction
// ---------
// <|0000|0000|0000|0000>
def NOP : F16<0b0000000000000000, (outs), (ins), "nop", []>;

// SLEEP
// Sleep instruction
// ---------
// <|1001|0101|1000|1000>
def SLEEP : F16<0b1001010110001000, (outs), (ins), "sleep", []>;

// WDR
// Watchdog reset
// ---------
// <|1001|0101|1010|1000>
def WDR : F16<0b1001010110101000, (outs), (ins), "wdr", []>;

//===----------------------------------------------------------------------===//
// Pseudo instructions for later expansion
//===----------------------------------------------------------------------===//

//: TODO: Optimize this for wider types AND optimize the following code
//       compile int foo(char a, char b, char c, char d) {return d+b;}
//       looks like a missed sext_inreg opportunity.
def SEXT
    : ExtensionPseudo<(outs DREGS
                       : $dst),
                      (ins GPR8
                       : $src),
                      "sext\t$dst, $src",
                      [(set i16
                        : $dst, (sext i8
                                 : $src)),
                       (implicit SREG)]>;

def ZEXT
    : ExtensionPseudo<(outs DREGS
                       : $dst),
                      (ins GPR8
                       : $src),
                      "zext\t$dst, $src",
                      [(set i16
                        : $dst, (zext i8
                                 : $src)),
                       (implicit SREG)]>;

// This pseudo gets expanded into a movw+adiw thus it clobbers SREG.
let Defs = [SREG],
    hasSideEffects = 0 in def FRMIDX : Pseudo<(outs DLDREGS
                                               : $dst),
                                              (ins DLDREGS
                                               : $src, i16imm
                                               : $src2),
                                              "frmidx\t$dst, $src, $src2", []>;

// This pseudo is either converted to a regular store or a push which clobbers
// SP.
def STDSPQRr : StorePseudo<(outs),
                           (ins memspi
                            : $dst, GPR8
                            : $src),
                           "stdstk\t$dst, $src", [(store i8
                                                   : $src, addr
                                                   : $dst)]>;

// This pseudo is either converted to a regular store or a push which clobbers
// SP.
def STDWSPQRr : StorePseudo<(outs),
                            (ins memspi
                             : $dst, DREGS
                             : $src),
                            "stdwstk\t$dst, $src", [(store i16
                                                     : $src, addr
                                                     : $dst)]>;

// SP read/write pseudos.
let hasSideEffects = 0 in {
  let Uses = [SP] in def SPREAD : Pseudo<(outs DREGS
                                          : $dst),
                                         (ins GPRSP
                                          : $src),
                                         "spread\t$dst, $src", []>;

  let Defs = [SP] in def SPWRITE : Pseudo<(outs GPRSP
                                           : $dst),
                                          (ins DREGS
                                           : $src),
                                          "spwrite\t$dst, $src", []>;
}

def Select8 : SelectPseudo<(outs GPR8
                            : $dst),
                           (ins GPR8
                            : $src, GPR8
                            : $src2, i8imm
                            : $cc),
                           "# Select8 PSEUDO", [(set i8
                                                 : $dst, (AVRselectcc i8
                                                          : $src, i8
                                                          : $src2, imm
                                                          : $cc))]>;

def Select16 : SelectPseudo<(outs DREGS
                             : $dst),
                            (ins DREGS
                             : $src, DREGS
                             : $src2, i8imm
                             : $cc),
                            "# Select16 PSEUDO", [(set i16
                                                   : $dst, (AVRselectcc i16
                                                            : $src, i16
                                                            : $src2, imm
                                                            : $cc))]>;

def Lsl8 : ShiftPseudo<(outs GPR8
                        : $dst),
                       (ins GPR8
                        : $src, GPR8
                        : $cnt),
                       "# Lsl8 PSEUDO", [(set i8
                                          : $dst, (AVRlslLoop i8
                                                   : $src, i8
                                                   : $cnt))]>;

def Lsl16 : ShiftPseudo<(outs DREGS
                         : $dst),
                        (ins DREGS
                         : $src, GPR8
                         : $cnt),
                        "# Lsl16 PSEUDO", [(set i16
                                            : $dst, (AVRlslLoop i16
                                                     : $src, i8
                                                     : $cnt))]>;

def Lsl32 : ShiftPseudo<(outs DREGS:$dstlo, DREGS:$dsthi),
                        (ins DREGS:$srclo, DREGS:$srchi, i8imm:$cnt),
                        "# Lsl32 PSEUDO",
                        [(set i16:$dstlo, i16:$dsthi, (AVRlslw i16:$srclo, i16:$srchi, i8:$cnt))]>;

def Lsr8 : ShiftPseudo<(outs GPR8
                        : $dst),
                       (ins GPR8
                        : $src, GPR8
                        : $cnt),
                       "# Lsr8 PSEUDO", [(set i8
                                          : $dst, (AVRlsrLoop i8
                                                   : $src, i8
                                                   : $cnt))]>;

def Lsr16 : ShiftPseudo<(outs DREGS
                         : $dst),
                        (ins DREGS
                         : $src, GPR8
                         : $cnt),
                        "# Lsr16 PSEUDO", [(set i16
                                            : $dst, (AVRlsrLoop i16
                                                     : $src, i8
                                                     : $cnt))]>;

def Lsr32 : ShiftPseudo<(outs DREGS:$dstlo, DREGS:$dsthi),
                        (ins DREGS:$srclo, DREGS:$srchi, i8imm:$cnt),
                        "# Lsr32 PSEUDO",
                        [(set i16:$dstlo, i16:$dsthi, (AVRlsrw i16:$srclo, i16:$srchi, i8:$cnt))]>;

def Rol8 : ShiftPseudo<(outs GPR8
                        : $dst),
                       (ins GPR8
                        : $src, GPR8
                        : $cnt),
                       "# Rol8 PSEUDO", [(set i8
                                          : $dst, (AVRrolLoop i8
                                                   : $src, i8
                                                   : $cnt))]>;

def Rol16 : ShiftPseudo<(outs DREGS
                         : $dst),
                        (ins DREGS
                         : $src, GPR8
                         : $cnt),
                        "# Rol16 PSEUDO", [(set i16
                                            : $dst, (AVRrolLoop i16
                                                     : $src, i8
                                                     : $cnt))]>;

def Ror8 : ShiftPseudo<(outs GPR8
                        : $dst),
                       (ins GPR8
                        : $src, GPR8
                        : $cnt),
                       "# Ror8 PSEUDO", [(set i8
                                          : $dst, (AVRrorLoop i8
                                                   : $src, i8
                                                   : $cnt))]>;

def Ror16 : ShiftPseudo<(outs DREGS
                         : $dst),
                        (ins DREGS
                         : $src, GPR8
                         : $cnt),
                        "# Ror16 PSEUDO", [(set i16
                                            : $dst, (AVRrorLoop i16
                                                     : $src, i8
                                                     : $cnt))]>;

def Asr8 : ShiftPseudo<(outs GPR8
                        : $dst),
                       (ins GPR8
                        : $src, GPR8
                        : $cnt),
                       "# Asr8 PSEUDO", [(set i8
                                          : $dst, (AVRasrLoop i8
                                                   : $src, i8
                                                   : $cnt))]>;

def Asr16 : ShiftPseudo<(outs DREGS
                         : $dst),
                        (ins DREGS
                         : $src, GPR8
                         : $cnt),
                        "# Asr16 PSEUDO", [(set i16
                                            : $dst, (AVRasrLoop i16
                                                     : $src, i8
                                                     : $cnt))]>;

def Asr32 : ShiftPseudo<(outs DREGS:$dstlo, DREGS:$dsthi),
                        (ins DREGS:$srclo, DREGS:$srchi, i8imm:$cnt),
                        "# Asr32 PSEUDO",
                        [(set i16:$dstlo, i16:$dsthi, (AVRasrw i16:$srclo, i16:$srchi, i8:$cnt))]>;

// lowered to a copy from the zero register.
let usesCustomInserter=1 in
def CopyZero : Pseudo<(outs GPR8:$rd), (ins), "clrz\t$rd", [(set i8:$rd, 0)]>;

//===----------------------------------------------------------------------===//
// Non-Instruction Patterns
//===----------------------------------------------------------------------===//

//: TODO: look in x86InstrCompiler.td for odd encoding trick related to
// add x, 128 -> sub x, -128. Clang is emitting an eor for this (ldi+eor)

// the add instruction always writes the carry flag
def : Pat<(addc i8 : $src, i8 : $src2), (ADDRdRr i8 : $src, i8 : $src2)>;
def : Pat<(addc DREGS
           : $src, DREGS
           : $src2),
          (ADDWRdRr DREGS
           : $src, DREGS
           : $src2)>;

// all sub instruction variants always writes the carry flag
def : Pat<(subc i8 : $src, i8 : $src2), (SUBRdRr i8 : $src, i8 : $src2)>;
def : Pat<(subc i16 : $src, i16 : $src2), (SUBWRdRr i16 : $src, i16 : $src2)>;
def : Pat<(subc i8 : $src, imm : $src2), (SUBIRdK i8 : $src, imm : $src2)>;
def : Pat<(subc i16 : $src, imm : $src2), (SUBIWRdK i16 : $src, imm : $src2)>;

// These patterns convert add (x, -imm) to sub (x, imm) since we dont have
// any add with imm instructions. Also take care of the adiw/sbiw instructions.
def : Pat<(add i16
           : $src1, imm0_63_neg
           : $src2),
          (SBIWRdK i16
           : $src1, (imm0_63_neg
                     : $src2))>,
          Requires<[HasADDSUBIW]>;
def : Pat<(add i16
           : $src1, imm
           : $src2),
          (SUBIWRdK i16
           : $src1, (imm16_neg_XFORM imm
                     : $src2))>;
def : Pat<(addc i16
           : $src1, imm
           : $src2),
          (SUBIWRdK i16
           : $src1, (imm16_neg_XFORM imm
                     : $src2))>;

def : Pat<(add i8
           : $src1, imm
           : $src2),
          (SUBIRdK i8
           : $src1, (imm8_neg_XFORM imm
                     : $src2))>;
def : Pat<(addc i8
           : $src1, imm
           : $src2),
          (SUBIRdK i8
           : $src1, (imm8_neg_XFORM imm
                     : $src2))>;
def : Pat<(adde i8
           : $src1, imm
           : $src2),
          (SBCIRdK i8
           : $src1, (imm8_neg_XFORM imm
                     : $src2))>;

// Emit NEGWRd with an extra zero register operand.
def : Pat<(ineg i16:$src),
          (NEGWRd i16:$src, (CopyZero))>;

// Calls.
let Predicates = [HasJMPCALL] in {
  def : Pat<(AVRcall(i16 tglobaladdr:$dst)), (CALLk tglobaladdr:$dst)>;
  def : Pat<(AVRcall(i16 texternalsym:$dst)), (CALLk texternalsym:$dst)>;
}
def : Pat<(AVRcall(i16 tglobaladdr:$dst)), (RCALLk tglobaladdr:$dst)>;
def : Pat<(AVRcall(i16 texternalsym:$dst)), (RCALLk texternalsym:$dst)>;

// `anyext`
def : Pat<(i16(anyext i8
               : $src)),
          (INSERT_SUBREG(i16(IMPLICIT_DEF)), i8
           : $src, sub_lo)>;

// `trunc`
def : Pat<(i8(trunc i16 : $src)), (EXTRACT_SUBREG i16 : $src, sub_lo)>;

// sext_inreg
def : Pat<(sext_inreg i16
           : $src, i8),
          (SEXT(i8(EXTRACT_SUBREG i16
                   : $src, sub_lo)))>;

// GlobalAddress
def : Pat<(i16(AVRWrapper tglobaladdr : $dst)), (LDIWRdK tglobaladdr : $dst)>;
def : Pat<(add i16
           : $src, (AVRWrapper tglobaladdr
                    : $src2)),
          (SUBIWRdK i16
           : $src, tglobaladdr
           : $src2)>;
def : Pat<(i8(load(AVRWrapper tglobaladdr:$dst))),
          (LDSRdK tglobaladdr:$dst)>,
          Requires<[HasSRAM, HasNonTinyEncoding]>;
def : Pat<(i8(load(AVRWrapper tglobaladdr:$dst))),
          (LDSRdKTiny tglobaladdr:$dst)>,
          Requires<[HasSRAM, HasTinyEncoding]>;
def : Pat<(i16(load(AVRWrapper tglobaladdr:$dst))),
          (LDSWRdK tglobaladdr:$dst)>,
          Requires<[HasSRAM, HasNonTinyEncoding]>;
def : Pat<(store i8:$src, (i16(AVRWrapper tglobaladdr:$dst))),
          (STSKRr tglobaladdr:$dst, i8:$src)>,
          Requires<[HasSRAM, HasNonTinyEncoding]>;
def : Pat<(store i8:$src, (i16(AVRWrapper tglobaladdr:$dst))),
          (STSKRrTiny tglobaladdr:$dst, i8:$src)>,
          Requires<[HasSRAM, HasTinyEncoding]>;
def : Pat<(store i16:$src, (i16(AVRWrapper tglobaladdr:$dst))),
          (STSWKRr tglobaladdr:$dst, i16:$src)>,
          Requires<[HasSRAM, HasNonTinyEncoding]>;

// BlockAddress
def : Pat<(i16(AVRWrapper tblockaddress
               : $dst)),
          (LDIWRdK tblockaddress
           : $dst)>;

def : Pat<(i8(trunc(AVRlsrwn DLDREGS
                    : $src, (i16 8)))),
          (EXTRACT_SUBREG DREGS
           : $src, sub_hi)>;

// :FIXME: DAGCombiner produces an shl node after legalization from these seq:
// BR_JT -> (mul x, 2) -> (shl x, 1)
def : Pat<(shl i16 : $src1, (i8 1)), (LSLWRd i16 : $src1)>;

// Lowering of 'tst' node to 'TST' instruction.
// TST is an alias of AND Rd, Rd.
def : Pat<(AVRtst i8 : $rd), (ANDRdRr GPR8 : $rd, GPR8 : $rd)>;

// Lowering of 'lsl' node to 'LSL' instruction.
// LSL is an alias of 'ADD Rd, Rd'
def : Pat<(AVRlsl i8 : $rd), (ADDRdRr GPR8 : $rd, GPR8 : $rd)>;
