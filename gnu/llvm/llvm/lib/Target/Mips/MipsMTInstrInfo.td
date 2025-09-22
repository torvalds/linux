//===-- MipsMTInstrInfo.td - Mips MT Instruction Infos -----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the MIPS MT ASE as defined by MD00378 1.12.
//
// TODO: Add support for the microMIPS encodings for the MT ASE and add the
//       instruction mappings.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// MIPS MT Instruction Encodings
//===----------------------------------------------------------------------===//

class DMT_ENC : COP0_MFMC0_MT<FIELD5_1_DMT_EMT, FIELD5_2_DMT_EMT,
                              OPCODE_SC_D>;

class EMT_ENC : COP0_MFMC0_MT<FIELD5_1_DMT_EMT, FIELD5_2_DMT_EMT,
                              OPCODE_SC_E>;

class DVPE_ENC : COP0_MFMC0_MT<FIELD5_1_2_DVPE_EVPE, FIELD5_1_2_DVPE_EVPE,
                               OPCODE_SC_D>;

class EVPE_ENC : COP0_MFMC0_MT<FIELD5_1_2_DVPE_EVPE, FIELD5_1_2_DVPE_EVPE,
                               OPCODE_SC_E>;

class FORK_ENC : SPECIAL3_MT_FORK;

class YIELD_ENC : SPECIAL3_MT_YIELD;

class MFTR_ENC : COP0_MFTTR_MT<FIELD5_MFTR>;

class MTTR_ENC : COP0_MFTTR_MT<FIELD5_MTTR>;

//===----------------------------------------------------------------------===//
// MIPS MT Instruction Descriptions
//===----------------------------------------------------------------------===//

class MT_1R_DESC_BASE<string instr_asm, InstrItinClass Itin = NoItinerary> {
  dag OutOperandList = (outs GPR32Opnd:$rt);
  dag InOperandList = (ins);
  string AsmString = !strconcat(instr_asm, "\t$rt");
  list<dag> Pattern = [];
  InstrItinClass Itinerary = Itin;
}

class MFTR_DESC {
  dag OutOperandList = (outs GPR32Opnd:$rd);
  dag InOperandList = (ins GPR32Opnd:$rt, uimm1:$u, uimm3:$sel, uimm1:$h);
  string AsmString = "mftr\t$rd, $rt, $u, $sel, $h";
  list<dag> Pattern = [];
  InstrItinClass Itinerary = II_MFTR;
}

class MTTR_DESC {
  dag OutOperandList = (outs GPR32Opnd:$rd);
  dag InOperandList = (ins GPR32Opnd:$rt, uimm1:$u, uimm3:$sel, uimm1:$h);
  string AsmString = "mttr\t$rt, $rd, $u, $sel, $h";
  list<dag> Pattern = [];
  InstrItinClass Itinerary = II_MTTR;
}

class FORK_DESC {
  dag OutOperandList = (outs GPR32Opnd:$rs, GPR32Opnd:$rd);
  dag InOperandList = (ins GPR32Opnd:$rt);
  string AsmString = "fork\t$rd, $rs, $rt";
  list<dag> Pattern = [];
  InstrItinClass Itinerary = II_FORK;
}

class YIELD_DESC {
  dag OutOperandList = (outs GPR32Opnd:$rd);
  dag InOperandList = (ins GPR32Opnd:$rs);
  string AsmString = "yield\t$rd, $rs";
  list<dag> Pattern = [];
  InstrItinClass Itinerary = II_YIELD;
}

class DMT_DESC : MT_1R_DESC_BASE<"dmt", II_DMT>;

class EMT_DESC : MT_1R_DESC_BASE<"emt", II_EMT>;

class DVPE_DESC : MT_1R_DESC_BASE<"dvpe", II_DVPE>;

class EVPE_DESC : MT_1R_DESC_BASE<"evpe", II_EVPE>;

//===----------------------------------------------------------------------===//
// MIPS MT Instruction Definitions
//===----------------------------------------------------------------------===//
let hasSideEffects = 1, isNotDuplicable = 1,
    AdditionalPredicates = [NotInMicroMips] in {
  def DMT : DMT_ENC, DMT_DESC, ASE_MT;

  def EMT : EMT_ENC, EMT_DESC, ASE_MT;

  def DVPE : DVPE_ENC, DVPE_DESC, ASE_MT;

  def EVPE : EVPE_ENC, EVPE_DESC, ASE_MT;

  def FORK : FORK_ENC, FORK_DESC, ASE_MT;

  def YIELD : YIELD_ENC, YIELD_DESC, ASE_MT;

  def MFTR : MFTR_ENC, MFTR_DESC, ASE_MT;

  def MTTR : MTTR_ENC, MTTR_DESC, ASE_MT;
}

//===----------------------------------------------------------------------===//
// MIPS MT Pseudo Instructions - used to support mtfr & mttr aliases.
//===----------------------------------------------------------------------===//
def MFTC0 : MipsAsmPseudoInst<(outs GPR32Opnd:$rd), (ins COP0Opnd:$rt,
                                                        uimm3:$sel),
                              "mftc0 $rd, $rt, $sel">, ASE_MT;

def MFTGPR : MipsAsmPseudoInst<(outs GPR32Opnd:$rd), (ins GPR32Opnd:$rt,
                                                          uimm3:$sel),
                               "mftgpr $rd, $rt">, ASE_MT;

def MFTLO : MipsAsmPseudoInst<(outs GPR32Opnd:$rt), (ins ACC64DSPOpnd:$ac),
                              "mftlo $rt, $ac">, ASE_MT;

def MFTHI : MipsAsmPseudoInst<(outs GPR32Opnd:$rt), (ins ACC64DSPOpnd:$ac),
                              "mfthi $rt, $ac">, ASE_MT;

def MFTACX : MipsAsmPseudoInst<(outs GPR32Opnd:$rt), (ins ACC64DSPOpnd:$ac),
                               "mftacx $rt, $ac">, ASE_MT;

def MFTDSP : MipsAsmPseudoInst<(outs GPR32Opnd:$rt), (ins),
                               "mftdsp $rt">, ASE_MT;

def MFTC1 : MipsAsmPseudoInst<(outs GPR32Opnd:$rt), (ins FGR32Opnd:$ft),
                              "mftc1 $rt, $ft">, ASE_MT;

def MFTHC1 : MipsAsmPseudoInst<(outs GPR32Opnd:$rt), (ins FGR32Opnd:$ft),
                               "mfthc1 $rt, $ft">, ASE_MT;

def CFTC1 : MipsAsmPseudoInst<(outs GPR32Opnd:$rt), (ins FGRCCOpnd:$ft),
                              "cftc1 $rt, $ft">, ASE_MT;


def MTTC0 : MipsAsmPseudoInst<(outs COP0Opnd:$rd), (ins GPR32Opnd:$rt,
                                                        uimm3:$sel),
                              "mttc0 $rt, $rd, $sel">, ASE_MT;

def MTTGPR : MipsAsmPseudoInst<(outs GPR32Opnd:$rt), (ins GPR32Opnd:$rd),
                               "mttgpr $rd, $rt">, ASE_MT;

def MTTLO : MipsAsmPseudoInst<(outs ACC64DSPOpnd:$ac), (ins GPR32Opnd:$rt),
                              "mttlo $rt, $ac">, ASE_MT;

def MTTHI : MipsAsmPseudoInst<(outs ACC64DSPOpnd:$ac), (ins GPR32Opnd:$rt),
                              "mtthi $rt, $ac">, ASE_MT;

def MTTACX : MipsAsmPseudoInst<(outs ACC64DSPOpnd:$ac), (ins GPR32Opnd:$rt),
                               "mttacx $rt, $ac">, ASE_MT;

def MTTDSP : MipsAsmPseudoInst<(outs), (ins GPR32Opnd:$rt),
                               "mttdsp $rt">, ASE_MT;

def MTTC1 : MipsAsmPseudoInst<(outs FGR32Opnd:$ft), (ins GPR32Opnd:$rt),
                              "mttc1 $rt, $ft">, ASE_MT;

def MTTHC1 : MipsAsmPseudoInst<(outs FGR32Opnd:$ft), (ins GPR32Opnd:$rt),
                               "mtthc1 $rt, $ft">, ASE_MT;

def CTTC1 : MipsAsmPseudoInst<(outs FGRCCOpnd:$ft), (ins GPR32Opnd:$rt),
                              "cttc1 $rt, $ft">, ASE_MT;

//===----------------------------------------------------------------------===//
// MIPS MT Instruction Definitions
//===----------------------------------------------------------------------===//

let AdditionalPredicates = [NotInMicroMips] in {
  def : MipsInstAlias<"dmt", (DMT ZERO), 1>, ASE_MT;

  def : MipsInstAlias<"emt", (EMT ZERO), 1>, ASE_MT;

  def : MipsInstAlias<"dvpe", (DVPE ZERO), 1>, ASE_MT;

  def : MipsInstAlias<"evpe", (EVPE ZERO), 1>, ASE_MT;

  def : MipsInstAlias<"yield $rs", (YIELD ZERO, GPR32Opnd:$rs), 1>, ASE_MT;

  def : MipsInstAlias<"mftc0 $rd, $rt", (MFTC0 GPR32Opnd:$rd, COP0Opnd:$rt, 0),
                      1>, ASE_MT;

  def : MipsInstAlias<"mftlo $rt", (MFTLO GPR32Opnd:$rt, AC0), 1>, ASE_MT;

  def : MipsInstAlias<"mfthi $rt", (MFTHI GPR32Opnd:$rt, AC0), 1>, ASE_MT;

  def : MipsInstAlias<"mftacx $rt", (MFTACX GPR32Opnd:$rt, AC0), 1>, ASE_MT;

  def : MipsInstAlias<"mttc0 $rd, $rt", (MTTC0 COP0Opnd:$rt, GPR32Opnd:$rd, 0),
                      1>, ASE_MT;

  def : MipsInstAlias<"mttlo $rt", (MTTLO AC0, GPR32Opnd:$rt), 1>, ASE_MT;

  def : MipsInstAlias<"mtthi $rt", (MTTHI AC0, GPR32Opnd:$rt), 1>, ASE_MT;

  def : MipsInstAlias<"mttacx $rt", (MTTACX AC0, GPR32Opnd:$rt), 1>, ASE_MT;
}
