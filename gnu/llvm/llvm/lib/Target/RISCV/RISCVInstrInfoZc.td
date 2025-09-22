//===-- RISCVInstrInfoZc.td - RISC-V 'Zc*' instructions ----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// This file describes the RISC-V instructions from the 'Zc*' compressed
/// instruction extensions, version 1.0.3.
///
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Operand and SDNode transformation definitions.
//===----------------------------------------------------------------------===//

def uimm2_lsb0 : RISCVOp,
                 ImmLeaf<XLenVT, [{return isShiftedUInt<1, 1>(Imm);}]> {
  let ParserMatchClass = UImmAsmOperand<2, "Lsb0">;
  let EncoderMethod = "getImmOpValue";
  let DecoderMethod = "decodeUImmOperand<2>";
  let OperandType = "OPERAND_UIMM2_LSB0";
  let MCOperandPredicate = [{
    int64_t Imm;
    if (!MCOp.evaluateAsConstantImm(Imm))
      return false;
    return isShiftedUInt<1, 1>(Imm);
  }];
}

def uimm8ge32 : RISCVOp {
  let ParserMatchClass = UImmAsmOperand<8, "GE32">;
  let DecoderMethod = "decodeUImmOperand<8>";
  let OperandType = "OPERAND_UIMM8_GE32";
}

def RlistAsmOperand : AsmOperandClass {
  let Name = "Rlist";
  let ParserMethod = "parseReglist";
  let DiagnosticType = "InvalidRlist";
}

def StackAdjAsmOperand : AsmOperandClass {
  let Name = "StackAdj";
  let ParserMethod = "parseZcmpStackAdj";
  let DiagnosticType = "InvalidStackAdj";
  let PredicateMethod = "isSpimm";
  let RenderMethod = "addSpimmOperands";
}

def NegStackAdjAsmOperand : AsmOperandClass {
  let Name = "NegStackAdj";
  let ParserMethod = "parseZcmpNegStackAdj";
  let DiagnosticType = "InvalidStackAdj";
  let PredicateMethod = "isSpimm";
  let RenderMethod = "addSpimmOperands";
}

def rlist : Operand<OtherVT> {
   let ParserMatchClass = RlistAsmOperand;
   let PrintMethod = "printRlist";
   let DecoderMethod = "decodeZcmpRlist";
   let EncoderMethod = "getRlistOpValue";
   let MCOperandPredicate = [{
    int64_t Imm;
    if (!MCOp.evaluateAsConstantImm(Imm))
      return false;
    // 0~3 Reserved for EABI
    return isUInt<4>(Imm) && Imm >= 4;
  }];
}

def stackadj : RISCVOp<OtherVT> {
  let ParserMatchClass = StackAdjAsmOperand;
  let PrintMethod = "printStackAdj";
  let DecoderMethod = "decodeZcmpSpimm";
  let OperandType = "OPERAND_SPIMM";
  let MCOperandPredicate = [{
    int64_t Imm;
    if (!MCOp.evaluateAsConstantImm(Imm))
      return false;
    return isShiftedUInt<2, 4>(Imm);
  }];
}

def negstackadj : RISCVOp<OtherVT> {
  let ParserMatchClass = NegStackAdjAsmOperand;
  let PrintMethod = "printNegStackAdj";
  let DecoderMethod = "decodeZcmpSpimm";
  let OperandType = "OPERAND_SPIMM";
  let MCOperandPredicate = [{
    int64_t Imm;
    if (!MCOp.evaluateAsConstantImm(Imm))
      return false;
    return isShiftedUInt<2, 4>(Imm);
  }];
}

//===----------------------------------------------------------------------===//
// Instruction Class Templates
//===----------------------------------------------------------------------===//

let hasSideEffects = 0, mayLoad = 1, mayStore = 0 in
class CLoadB_ri<bits<6> funct6, string OpcodeStr>
    : RVInst16CLB<funct6, 0b00, (outs GPRC:$rd),
                  (ins GPRCMem:$rs1, uimm2:$imm),
                  OpcodeStr, "$rd, ${imm}(${rs1})"> {
  bits<2> imm;

  let Inst{6-5} = imm{0,1};
}

let hasSideEffects = 0, mayLoad = 1, mayStore = 0 in
class CLoadH_ri<bits<6> funct6, bit funct1, string OpcodeStr>
    : RVInst16CLH<funct6, funct1, 0b00, (outs GPRC:$rd),
                  (ins GPRCMem:$rs1, uimm2_lsb0:$imm),
                  OpcodeStr, "$rd, ${imm}(${rs1})"> {
  bits<2> imm;

  let Inst{5} = imm{1};
}

let hasSideEffects = 0, mayLoad = 0, mayStore = 1 in
class CStoreB_rri<bits<6> funct6, string OpcodeStr>
    : RVInst16CSB<funct6, 0b00, (outs),
                  (ins GPRC:$rs2, GPRCMem:$rs1, uimm2:$imm),
                  OpcodeStr, "$rs2, ${imm}(${rs1})"> {
  bits<2> imm;

  let Inst{6-5} = imm{0,1};
}

let hasSideEffects = 0, mayLoad = 0, mayStore = 1 in
class CStoreH_rri<bits<6> funct6, bit funct1, string OpcodeStr>
    : RVInst16CSH<funct6, funct1, 0b00, (outs),
                  (ins GPRC:$rs2, GPRCMem:$rs1, uimm2_lsb0:$imm),
                  OpcodeStr, "$rs2, ${imm}(${rs1})"> {
  bits<2> imm;

  let Inst{5} = imm{1};
}

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
class RVZcArith_r<bits<5> funct5, string OpcodeStr> :
  RVInst16CU<0b100111, funct5, 0b01, (outs GPRC:$rd_wb), (ins GPRC:$rd),
             OpcodeStr, "$rd"> {
  let Constraints = "$rd = $rd_wb";
}

class RVInstZcCPPP<bits<5> funct5, string opcodestr,
                   DAGOperand immtype = stackadj>
    : RVInst16<(outs), (ins rlist:$rlist, immtype:$stackadj),
               opcodestr, "$rlist, $stackadj", [], InstFormatOther> {
  bits<4> rlist;
  bits<16> stackadj;

  let Inst{1-0} = 0b10;
  let Inst{3-2} = stackadj{5-4};
  let Inst{7-4} = rlist;
  let Inst{12-8} = funct5;
  let Inst{15-13} = 0b101;
}

//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//

let Predicates = [HasStdExtZcb, HasStdExtZba, IsRV64] in
def C_ZEXT_W  : RVZcArith_r<0b11100 , "c.zext.w">,
                Sched<[WriteIALU32, ReadIALU32, ReadIALU32]>;

let Predicates = [HasStdExtZcb, HasStdExtZbb] in {
def C_ZEXT_H  : RVZcArith_r<0b11010 , "c.zext.h">,
                Sched<[WriteIALU, ReadIALU]>;
def C_SEXT_B  : RVZcArith_r<0b11001 , "c.sext.b">,
                Sched<[WriteIALU, ReadIALU]>;
def C_SEXT_H  : RVZcArith_r<0b11011 , "c.sext.h">,
                Sched<[WriteIALU, ReadIALU]>;
}

let Predicates = [HasStdExtZcb] in
def C_ZEXT_B  : RVZcArith_r<0b11000 , "c.zext.b">,
                Sched<[WriteIALU, ReadIALU]>;

let Predicates = [HasStdExtZcb, HasStdExtZmmul] in
def C_MUL     : CA_ALU<0b100111, 0b10, "c.mul", GPRC>,
                Sched<[WriteIMul, ReadIMul, ReadIMul]>;

let Predicates = [HasStdExtZcb] in {
def C_NOT : RVZcArith_r<0b11101 , "c.not">,
            Sched<[WriteIALU, ReadIALU]>;

def C_LBU : CLoadB_ri<0b100000, "c.lbu">,
            Sched<[WriteLDB, ReadMemBase]>;
def C_LHU : CLoadH_ri<0b100001, 0b0, "c.lhu">,
            Sched<[WriteLDH, ReadMemBase]>;
def C_LH  : CLoadH_ri<0b100001, 0b1, "c.lh">,
            Sched<[WriteLDH, ReadMemBase]>;

def C_SB : CStoreB_rri<0b100010, "c.sb">,
           Sched<[WriteSTB, ReadStoreData, ReadMemBase]>;
def C_SH : CStoreH_rri<0b100011, 0b0, "c.sh">,
           Sched<[WriteSTH, ReadStoreData, ReadMemBase]>;
}

// Zcmp
let DecoderNamespace = "RVZcmp", Predicates = [HasStdExtZcmp],
    hasSideEffects = 0, mayLoad = 0, mayStore = 0 in {
let Defs = [X10, X11] in
def CM_MVA01S : RVInst16CA<0b101011, 0b11, 0b10, (outs),
                            (ins SR07:$rs1, SR07:$rs2), "cm.mva01s", "$rs1, $rs2">,
                Sched<[WriteIALU, WriteIALU, ReadIALU, ReadIALU]>;

let Uses = [X10, X11] in
def CM_MVSA01 : RVInst16CA<0b101011, 0b01, 0b10, (outs SR07:$rs1, SR07:$rs2),
                            (ins), "cm.mvsa01", "$rs1, $rs2">,
                Sched<[WriteIALU, WriteIALU, ReadIALU, ReadIALU]>;
} // DecoderNamespace = "RVZcmp", Predicates = [HasStdExtZcmp]...

let DecoderNamespace = "RVZcmp", Predicates = [HasStdExtZcmp] in {
let hasSideEffects = 0, mayLoad = 0, mayStore = 1, Uses = [X2], Defs = [X2] in
def CM_PUSH : RVInstZcCPPP<0b11000, "cm.push", negstackadj>,
              Sched<[WriteIALU, ReadIALU, ReadStoreData, ReadStoreData,
                     ReadStoreData, ReadStoreData, ReadStoreData, ReadStoreData,
                     ReadStoreData, ReadStoreData, ReadStoreData, ReadStoreData,
                     ReadStoreData, ReadStoreData, ReadStoreData]>;

let hasSideEffects = 0, mayLoad = 1, mayStore = 0, isReturn = 1,
    Uses = [X2], Defs = [X2] in
def CM_POPRET : RVInstZcCPPP<0b11110, "cm.popret">,
                Sched<[WriteIALU, WriteLDW, WriteLDW, WriteLDW, WriteLDW,
                       WriteLDW, WriteLDW, WriteLDW, WriteLDW, WriteLDW,
                       WriteLDW, WriteLDW, WriteLDW, WriteLDW, ReadIALU]>;

let hasSideEffects = 0, mayLoad = 1, mayStore = 0, isReturn = 1,
    Uses = [X2], Defs = [X2, X10] in
def CM_POPRETZ : RVInstZcCPPP<0b11100, "cm.popretz">,
                 Sched<[WriteIALU, WriteIALU, WriteLDW, WriteLDW, WriteLDW,
                        WriteLDW, WriteLDW, WriteLDW, WriteLDW, WriteLDW,
                        WriteLDW, WriteLDW, WriteLDW, WriteLDW, WriteLDW,
                        ReadIALU]>;

let hasSideEffects = 0, mayLoad = 1, mayStore = 0,
    Uses = [X2], Defs = [X2] in
def CM_POP : RVInstZcCPPP<0b11010, "cm.pop">,
             Sched<[WriteIALU, WriteLDW, WriteLDW, WriteLDW, WriteLDW,
                    WriteLDW, WriteLDW, WriteLDW, WriteLDW, WriteLDW, WriteLDW,
                    WriteLDW, WriteLDW, WriteLDW, ReadIALU]>;
} // DecoderNamespace = "RVZcmp", Predicates = [HasStdExtZcmp]...

let DecoderNamespace = "RVZcmt", Predicates = [HasStdExtZcmt],
    hasSideEffects = 0, mayLoad = 0, mayStore = 0 in {
def CM_JT : RVInst16CJ<0b101, 0b10, (outs), (ins uimm5:$index),
                       "cm.jt", "$index">{
  bits<5> index;

  let Inst{12-7} = 0b000000;
  let Inst{6-2} = index;
}

let Defs = [X1] in
def CM_JALT : RVInst16CJ<0b101, 0b10, (outs), (ins uimm8ge32:$index),
                         "cm.jalt", "$index">{
  bits<8> index;

  let Inst{12-10} = 0b000;
  let Inst{9-2} = index;
}
} // DecoderNamespace = "RVZcmt", Predicates = [HasStdExtZcmt]...


let Predicates = [HasStdExtZcb, HasStdExtZmmul] in{
def : CompressPat<(MUL GPRC:$rs1, GPRC:$rs1, GPRC:$rs2),
                  (C_MUL GPRC:$rs1, GPRC:$rs2)>;
let isCompressOnly = true in
def : CompressPat<(MUL GPRC:$rs1, GPRC:$rs2, GPRC:$rs1),
                  (C_MUL GPRC:$rs1, GPRC:$rs2)>;
} // Predicates = [HasStdExtZcb, HasStdExtZmmul]

let Predicates = [HasStdExtZcb, HasStdExtZbb] in{
def : CompressPat<(SEXT_B GPRC:$rs1, GPRC:$rs1),
                  (C_SEXT_B GPRC:$rs1, GPRC:$rs1)>;
def : CompressPat<(SEXT_H GPRC:$rs1, GPRC:$rs1),
                  (C_SEXT_H GPRC:$rs1, GPRC:$rs1)>;
} // Predicates = [HasStdExtZcb, HasStdExtZbb]

let Predicates = [HasStdExtZcb, HasStdExtZbb] in{
def : CompressPat<(ZEXT_H_RV32 GPRC:$rs1, GPRC:$rs1),
                  (C_ZEXT_H GPRC:$rs1, GPRC:$rs1)>;
def : CompressPat<(ZEXT_H_RV64 GPRC:$rs1, GPRC:$rs1),
                  (C_ZEXT_H GPRC:$rs1, GPRC:$rs1)>;
} // Predicates = [HasStdExtZcb, HasStdExtZbb]

let Predicates = [HasStdExtZcb] in{
def : CompressPat<(ANDI GPRC:$rs1, GPRC:$rs1, 255),
                  (C_ZEXT_B GPRC:$rs1, GPRC:$rs1)>;
} // Predicates = [HasStdExtZcb]

let Predicates = [HasStdExtZcb, HasStdExtZba, IsRV64] in{
def : CompressPat<(ADD_UW GPRC:$rs1, GPRC:$rs1, X0),
                  (C_ZEXT_W GPRC:$rs1, GPRC:$rs1)>;
} // Predicates = [HasStdExtZcb, HasStdExtZba, IsRV64]

let Predicates = [HasStdExtZcb] in{
def : CompressPat<(XORI GPRC:$rs1, GPRC:$rs1, -1),
                  (C_NOT GPRC:$rs1, GPRC:$rs1)>;
}

let Predicates = [HasStdExtZcb] in{
def : CompressPat<(LBU GPRC:$rd, GPRCMem:$rs1, uimm2:$imm),
                  (C_LBU GPRC:$rd, GPRCMem:$rs1, uimm2:$imm)>;
def : CompressPat<(LHU GPRC:$rd, GPRCMem:$rs1, uimm2_lsb0:$imm),
                  (C_LHU GPRC:$rd, GPRCMem:$rs1, uimm2_lsb0:$imm)>;
def : CompressPat<(LH GPRC:$rd, GPRCMem:$rs1, uimm2_lsb0:$imm),
                  (C_LH GPRC:$rd, GPRCMem:$rs1, uimm2_lsb0:$imm)>;
def : CompressPat<(SB GPRC:$rs2, GPRCMem:$rs1, uimm2:$imm),
                  (C_SB GPRC:$rs2, GPRCMem:$rs1, uimm2:$imm)>;
def : CompressPat<(SH GPRC:$rs2, GPRCMem:$rs1, uimm2_lsb0:$imm),
                  (C_SH GPRC:$rs2, GPRCMem:$rs1, uimm2_lsb0:$imm)>;
}// Predicates = [HasStdExtZcb]


//===----------------------------------------------------------------------===//
// Pseudo Instructions
//===----------------------------------------------------------------------===//

let Predicates = [HasStdExtZcb] in {
def : InstAlias<"c.lbu $rd, (${rs1})",(C_LBU GPRC:$rd, GPRC:$rs1, 0), 0>;
def : InstAlias<"c.lhu $rd, (${rs1})",(C_LHU GPRC:$rd, GPRC:$rs1, 0), 0>;
def : InstAlias<"c.lh $rd, (${rs1})", (C_LH GPRC:$rd, GPRC:$rs1, 0), 0>;
def : InstAlias<"c.sb $rd, (${rs1})", (C_SB GPRC:$rd, GPRC:$rs1, 0), 0>;
def : InstAlias<"c.sh $rd, (${rs1})", (C_SH GPRC:$rd, GPRC:$rs1, 0), 0>;
}
