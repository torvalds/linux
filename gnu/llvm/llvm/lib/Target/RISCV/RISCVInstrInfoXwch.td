//===-- RISCVInstrInfoXwch.td ------------------------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the vendor extension(s) defined by WCH.
//
//===----------------------------------------------------------------------===//

class QKStackInst<bits<2> funct2, dag outs, dag ins,
                  string opcodestr, string argstr>
    : RVInst16<outs, ins, opcodestr, argstr, [], InstFormatOther> {
  bits<3> rd_rs2;

  let Inst{15-11} = 0b10000;
  let Inst{6-5} = funct2;
  let Inst{4-2} = rd_rs2;
  let Inst{1-0} = 0b00;
}

//===----------------------------------------------------------------------===//
// Operand definitions.
//===----------------------------------------------------------------------===//

def uimm4_with_predicate : RISCVUImmLeafOp<4> {
  let MCOperandPredicate = [{
    int64_t Imm;
    if (!MCOp.evaluateAsConstantImm(Imm))
      return false;
    return isUInt<4>(Imm);
  }];
}

def uimm5_with_predicate : RISCVUImmLeafOp<5> {
  let MCOperandPredicate = [{
    int64_t Imm;
    if (!MCOp.evaluateAsConstantImm(Imm))
      return false;
    return isUInt<5>(Imm);
  }];
}

// A 5-bit unsigned immediate where the least significant bit is zero.
def uimm5_lsb0 : RISCVOp,
                 ImmLeaf<XLenVT, [{return isShiftedUInt<4, 1>(Imm);}]> {
  let ParserMatchClass = UImmAsmOperand<5, "Lsb0">;
  let EncoderMethod = "getImmOpValue";
  let DecoderMethod = "decodeUImmOperand<5>";
  let OperandType = "OPERAND_UIMM5_LSB0";
  let MCOperandPredicate = [{
    int64_t Imm;
    if (!MCOp.evaluateAsConstantImm(Imm))
      return false;
    return isShiftedUInt<4, 1>(Imm);
  }];
}

// A 6-bit unsigned immediate where the least significant bit is zero.
def uimm6_lsb0 : RISCVOp,
                 ImmLeaf<XLenVT, [{return isShiftedUInt<5, 1>(Imm);}]> {
  let ParserMatchClass = UImmAsmOperand<6, "Lsb0">;
  let EncoderMethod = "getImmOpValue";
  let DecoderMethod = "decodeUImmOperand<6>";
  let OperandType = "OPERAND_UIMM6_LSB0";
  let MCOperandPredicate = [{
    int64_t Imm;
    if (!MCOp.evaluateAsConstantImm(Imm))
      return false;
    return isShiftedUInt<5, 1>(Imm);
  }];
}

//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//
let Predicates = [HasVendorXwchc], DecoderNamespace = "Xwchc" in {

let hasSideEffects = 0, mayLoad = 1, mayStore = 0 in
def QK_C_LBU : RVInst16CL<0b001, 0b00, (outs GPRC:$rd),
                          (ins GPRCMem:$rs1, uimm5_with_predicate:$imm),
                          "qk.c.lbu", "$rd, ${imm}(${rs1})">,
               Sched<[WriteLDB, ReadMemBase]> {
  bits<5> imm;
  let Inst{12} = imm{0};
  let Inst{11-10} = imm{4-3};
  let Inst{6-5} = imm{2-1};
}
let hasSideEffects = 0, mayLoad = 0, mayStore = 1 in
def QK_C_SB : RVInst16CS<0b101, 0b00, (outs),
                         (ins GPRC:$rs2, GPRCMem:$rs1,
                              uimm5_with_predicate:$imm),
                         "qk.c.sb", "$rs2, ${imm}(${rs1})">,
              Sched<[WriteSTB, ReadStoreData, ReadMemBase]> {
  bits<5> imm;
  let Inst{12} = imm{0};
  let Inst{11-10} = imm{4-3};
  let Inst{6-5} = imm{2-1};
}

let hasSideEffects = 0, mayLoad = 1, mayStore = 0 in
def QK_C_LHU : RVInst16CL<0b001, 0b10, (outs GPRC:$rd),
                          (ins GPRCMem:$rs1, uimm6_lsb0:$imm),
                          "qk.c.lhu", "$rd, ${imm}(${rs1})">,
               Sched<[WriteLDH, ReadMemBase]> {
  bits<6> imm;
  let Inst{12-10} = imm{5-3};
  let Inst{6-5} = imm{2-1};
}
let hasSideEffects = 0, mayLoad = 0, mayStore = 1 in
def QK_C_SH : RVInst16CS<0b101, 0b10, (outs),
                         (ins GPRC:$rs2, GPRCMem:$rs1, uimm6_lsb0:$imm),
                         "qk.c.sh", "$rs2, ${imm}(${rs1})">,
              Sched<[WriteSTH, ReadStoreData, ReadMemBase]> {
  bits<6> imm;
  let Inst{12-10} = imm{5-3};
  let Inst{6-5} = imm{2-1};
}

let hasSideEffects = 0, mayLoad = 1, mayStore = 0 in
def QK_C_LBUSP : QKStackInst<0b00, (outs GPRC:$rd_rs2),
                             (ins SPMem:$rs1, uimm4_with_predicate:$imm),
                             "qk.c.lbusp", "$rd_rs2, ${imm}(${rs1})">,
                 Sched<[WriteLDB, ReadMemBase]> {
  bits<4> imm;
  let Inst{10-7} = imm;
}
let hasSideEffects = 0, mayLoad = 0, mayStore = 1 in
def QK_C_SBSP : QKStackInst<0b10, (outs),
                            (ins GPRC:$rd_rs2, SPMem:$rs1,
                                 uimm4_with_predicate:$imm),
                            "qk.c.sbsp", "$rd_rs2, ${imm}(${rs1})">,
                Sched<[WriteSTB, ReadStoreData, ReadMemBase]> {
  bits<4> imm;
  let Inst{10-7} = imm;
}

let hasSideEffects = 0, mayLoad = 1, mayStore = 0 in
def QK_C_LHUSP : QKStackInst<0b01, (outs GPRC:$rd_rs2),
                             (ins SPMem:$rs1, uimm5_lsb0:$imm),
                             "qk.c.lhusp", "$rd_rs2, ${imm}(${rs1})">,
                 Sched<[WriteLDH, ReadMemBase]> {
  bits<5> imm;
  let Inst{10-8} = imm{3-1};
  let Inst{7} = imm{4};
}
let hasSideEffects = 0, mayLoad = 0, mayStore = 1 in
def QK_C_SHSP : QKStackInst<0b11, (outs),
                            (ins GPRC:$rd_rs2, SPMem:$rs1, uimm5_lsb0:$imm),
                            "qk.c.shsp", "$rd_rs2, ${imm}(${rs1})">,
                Sched<[WriteSTH, ReadStoreData, ReadMemBase]> {
  bits<5> imm;
  let Inst{10-8} = imm{3-1};
  let Inst{7} = imm{4};
}

} // Predicates = [HasVendorXwchc], DecoderNamespace = "Xwchc"

//===----------------------------------------------------------------------===//
// Assembler Pseudo Instructions
//===----------------------------------------------------------------------===//

let EmitPriority = 0 in {
let Predicates = [HasVendorXwchc] in {
def : InstAlias<"qk.c.lbu $rd, (${rs1})", (QK_C_LBU GPRC:$rd, GPRCMem:$rs1, 0)>;
def : InstAlias<"qk.c.sb $rs2, (${rs1})", (QK_C_SB GPRC:$rs2, GPRCMem:$rs1, 0)>;
def : InstAlias<"qk.c.lhu $rd, (${rs1})", (QK_C_LHU GPRC:$rd, GPRCMem:$rs1, 0)>;
def : InstAlias<"qk.c.sh $rs2, (${rs1})", (QK_C_SH GPRC:$rs2, GPRCMem:$rs1, 0)>;
def : InstAlias<"qk.c.lbusp $rd, (${rs1})", (QK_C_LBUSP GPRC:$rd, SPMem:$rs1, 0)>;
def : InstAlias<"qk.c.sbsp $rs2, (${rs1})", (QK_C_SBSP GPRC:$rs2, SPMem:$rs1, 0)>;
def : InstAlias<"qk.c.lhusp $rd, (${rs1})", (QK_C_LHUSP GPRC:$rd, SPMem:$rs1, 0)>;
def : InstAlias<"qk.c.shsp $rs2, (${rs1})", (QK_C_SHSP GPRC:$rs2, SPMem:$rs1, 0)>;
}
}

//===----------------------------------------------------------------------===/
// Compress Instruction tablegen backend.
//===----------------------------------------------------------------------===//

let Predicates = [HasVendorXwchc] in {
def : CompressPat<(LBU GPRC:$rd, GPRCMem:$rs1, uimm5_with_predicate:$imm),
                  (QK_C_LBU GPRC:$rd, GPRCMem:$rs1, uimm5_with_predicate:$imm)>;
def : CompressPat<(SB GPRC:$rs2, GPRCMem:$rs1, uimm5_with_predicate:$imm),
                  (QK_C_SB GPRC:$rs2, GPRCMem:$rs1, uimm5_with_predicate:$imm)>;
def : CompressPat<(LHU GPRC:$rd, GPRCMem:$rs1, uimm6_lsb0:$imm),
                  (QK_C_LHU GPRC:$rd, GPRCMem:$rs1, uimm6_lsb0:$imm)>;
def : CompressPat<(SH GPRC:$rs2, GPRCMem:$rs1, uimm6_lsb0:$imm),
                  (QK_C_SH GPRC:$rs2, GPRCMem:$rs1, uimm6_lsb0:$imm)>;
def : CompressPat<(LBU GPRC:$rd, SPMem:$rs1,   uimm4_with_predicate:$imm),
                  (QK_C_LBUSP GPRC:$rd, SPMem:$rs1, uimm4_with_predicate:$imm)>;
def : CompressPat<(SB GPRC:$rs2, SPMem:$rs1,   uimm4_with_predicate:$imm),
                  (QK_C_SBSP GPRC:$rs2, SPMem:$rs1, uimm4_with_predicate:$imm)>;
def : CompressPat<(LHU GPRC:$rd, SPMem:$rs1,   uimm5_lsb0:$imm),
                  (QK_C_LHUSP GPRC:$rd, SPMem:$rs1, uimm5_lsb0:$imm)>;
def : CompressPat<(SH GPRC:$rs2, SPMem:$rs1,   uimm5_lsb0:$imm),
                  (QK_C_SHSP GPRC:$rs2, SPMem:$rs1, uimm5_lsb0:$imm)>;
}
