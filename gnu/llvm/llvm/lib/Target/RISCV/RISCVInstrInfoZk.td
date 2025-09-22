//===- RISCVInstrInfoZk.td - RISC-V 'Zk' instructions ------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the RISC-V instructions from the standard 'Zk',
// Scalar Cryptography Instructions extension, version 1.0.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Operand and SDNode transformation definitions.
//===----------------------------------------------------------------------===//

def riscv_sha256sig0 : SDNode<"RISCVISD::SHA256SIG0", SDTIntUnaryOp>;
def riscv_sha256sig1 : SDNode<"RISCVISD::SHA256SIG1", SDTIntUnaryOp>;
def riscv_sha256sum0 : SDNode<"RISCVISD::SHA256SUM0", SDTIntUnaryOp>;
def riscv_sha256sum1 : SDNode<"RISCVISD::SHA256SUM1", SDTIntUnaryOp>;

def SDT_RISCVZkByteSelect : SDTypeProfile<1, 3, [SDTCisVT<0, XLenVT>,
                                                 SDTCisVT<1, XLenVT>,
                                                 SDTCisVT<2, XLenVT>,
                                                 SDTCisVT<3, i32>]>;
def riscv_sm4ks : SDNode<"RISCVISD::SM4KS", SDT_RISCVZkByteSelect>;
def riscv_sm4ed : SDNode<"RISCVISD::SM4ED", SDT_RISCVZkByteSelect>;

def riscv_sm3p0 : SDNode<"RISCVISD::SM3P0", SDTIntUnaryOp>;
def riscv_sm3p1 : SDNode<"RISCVISD::SM3P1", SDTIntUnaryOp>;

def RnumArg : AsmOperandClass {
  let Name = "RnumArg";
  let RenderMethod = "addImmOperands";
  let DiagnosticType = "InvalidRnumArg";
}

def rnum : RISCVOp<i32>, TImmLeaf<i32, [{return (Imm >= 0 && Imm <= 10);}]> {
  let ParserMatchClass = RnumArg;
  let EncoderMethod = "getImmOpValue";
  let DecoderMethod = "decodeUImmOperand<4>";
  let OperandType = "OPERAND_RVKRNUM";
}

def byteselect : RISCVOp<i32>, TImmLeaf<i32, [{return isUInt<2>(Imm);}]> {
  let ParserMatchClass = UImmAsmOperand<2>;
  let DecoderMethod = "decodeUImmOperand<2>";
  let OperandType = "OPERAND_UIMM2";
}

//===----------------------------------------------------------------------===//
// Instruction class templates
//===----------------------------------------------------------------------===//
let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
class RVKUnary<bits<12> imm12, bits<3> funct3, string opcodestr>
    : RVInstIUnary<imm12, funct3, OPC_OP_IMM, (outs GPR:$rd), (ins GPR:$rs1),
                   opcodestr, "$rd, $rs1">;

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
class RVKByteSelect<bits<5> funct5, string opcodestr>
    : RVInstR<{0b00, funct5}, 0b000, OPC_OP, (outs GPR:$rd),
              (ins GPR:$rs1, GPR:$rs2, byteselect:$bs),
              opcodestr, "$rd, $rs1, $rs2, $bs">{
  bits<2> bs;
  let Inst{31-30} = bs;
}

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
class RVKUnary_rnum<bits<7> funct7, bits<3> funct3, string opcodestr>
    : RVInstIBase<funct3, OPC_OP_IMM, (outs GPR:$rd),
                  (ins GPR:$rs1, rnum:$rnum), opcodestr, "$rd, $rs1, $rnum"> {
  bits<4> rnum;
  let Inst{31-25} = funct7;
  let Inst{24} = 0b1;
  let Inst{23-20} = rnum;
}

//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//
let Predicates = [HasStdExtZknd, IsRV32] in {
def AES32DSI  : RVKByteSelect<0b10101, "aes32dsi">;
def AES32DSMI : RVKByteSelect<0b10111, "aes32dsmi">;
} // Predicates = [HasStdExtZknd, IsRV32]

let Predicates = [HasStdExtZknd, IsRV64] in {
def AES64DS  : ALU_rr<0b0011101, 0b000, "aes64ds">;
def AES64DSM : ALU_rr<0b0011111, 0b000, "aes64dsm">;

def AES64IM  : RVKUnary<0b001100000000, 0b001, "aes64im">;
} // Predicates = [HasStdExtZknd, IsRV64]

let Predicates = [HasStdExtZkndOrZkne, IsRV64] in {
def AES64KS2  : ALU_rr<0b0111111, 0b000, "aes64ks2">;

def AES64KS1I : RVKUnary_rnum<0b0011000, 0b001, "aes64ks1i">;
} // Predicates = [HasStdExtZkndOrZkne, IsRV64]

let Predicates = [HasStdExtZkne, IsRV32] in {
def AES32ESI  : RVKByteSelect<0b10001, "aes32esi">;
def AES32ESMI : RVKByteSelect<0b10011, "aes32esmi">;
} // Predicates = [HasStdExtZkne, IsRV32]

let Predicates = [HasStdExtZkne, IsRV64] in {
def AES64ES   : ALU_rr<0b0011001, 0b000, "aes64es">;
def AES64ESM  : ALU_rr<0b0011011, 0b000, "aes64esm">;
} // Predicates = [HasStdExtZkne, IsRV64]

let Predicates = [HasStdExtZknh], IsSignExtendingOpW = 1 in {
def SHA256SIG0 : RVKUnary<0b000100000010, 0b001, "sha256sig0">;
def SHA256SIG1 : RVKUnary<0b000100000011, 0b001, "sha256sig1">;
def SHA256SUM0 : RVKUnary<0b000100000000, 0b001, "sha256sum0">;
def SHA256SUM1 : RVKUnary<0b000100000001, 0b001, "sha256sum1">;
} // Predicates = [HasStdExtZknh]

let Predicates = [HasStdExtZknh, IsRV32] in {
def SHA512SIG0H : ALU_rr<0b0101110, 0b000, "sha512sig0h">;
def SHA512SIG0L : ALU_rr<0b0101010, 0b000, "sha512sig0l">;
def SHA512SIG1H : ALU_rr<0b0101111, 0b000, "sha512sig1h">;
def SHA512SIG1L : ALU_rr<0b0101011, 0b000, "sha512sig1l">;
def SHA512SUM0R : ALU_rr<0b0101000, 0b000, "sha512sum0r">;
def SHA512SUM1R : ALU_rr<0b0101001, 0b000, "sha512sum1r">;
} // [HasStdExtZknh, IsRV32]

let Predicates = [HasStdExtZknh, IsRV64] in {
def SHA512SIG0 : RVKUnary<0b000100000110, 0b001, "sha512sig0">;
def SHA512SIG1 : RVKUnary<0b000100000111, 0b001, "sha512sig1">;
def SHA512SUM0 : RVKUnary<0b000100000100, 0b001, "sha512sum0">;
def SHA512SUM1 : RVKUnary<0b000100000101, 0b001, "sha512sum1">;
} // Predicates = [HasStdExtZknh, IsRV64]

let Predicates = [HasStdExtZksed], IsSignExtendingOpW = 1 in {
def SM4ED : RVKByteSelect<0b11000, "sm4ed">;
def SM4KS : RVKByteSelect<0b11010, "sm4ks">;
} // Predicates = [HasStdExtZksed]

let Predicates = [HasStdExtZksh], IsSignExtendingOpW = 1 in {
def SM3P0 : RVKUnary<0b000100001000, 0b001, "sm3p0">;
def SM3P1 : RVKUnary<0b000100001001, 0b001, "sm3p1">;
} // Predicates = [HasStdExtZksh]

//===----------------------------------------------------------------------===//
// Codegen patterns
//===----------------------------------------------------------------------===//

class PatGprGprByteSelect<SDPatternOperator OpNode, RVInst Inst>
    : Pat<(XLenVT (OpNode (XLenVT GPR:$rs1), (XLenVT GPR:$rs2), byteselect:$imm)),
          (Inst GPR:$rs1, GPR:$rs2, byteselect:$imm)>;

// Zknd
let Predicates = [HasStdExtZknd, IsRV32] in {
def : PatGprGprByteSelect<int_riscv_aes32dsi, AES32DSI>;
def : PatGprGprByteSelect<int_riscv_aes32dsmi, AES32DSMI>;
} // Predicates = [HasStdExtZknd, IsRV32]

let Predicates = [HasStdExtZknd, IsRV64] in {
def : PatGprGpr<int_riscv_aes64ds, AES64DS>;
def : PatGprGpr<int_riscv_aes64dsm, AES64DSM>;
def : PatGpr<int_riscv_aes64im, AES64IM>;
} // Predicates = [HasStdExtZknd, IsRV64]

let Predicates = [HasStdExtZkndOrZkne, IsRV64] in {
def : PatGprGpr<int_riscv_aes64ks2, AES64KS2>;
def : Pat<(int_riscv_aes64ks1i GPR:$rs1, rnum:$rnum),
          (AES64KS1I GPR:$rs1, rnum:$rnum)>;
} // Predicates = [HasStdExtZkndOrZkne, IsRV64]

// Zkne
let Predicates = [HasStdExtZkne, IsRV32] in {
def : PatGprGprByteSelect<int_riscv_aes32esi, AES32ESI>;
def : PatGprGprByteSelect<int_riscv_aes32esmi, AES32ESMI>;
} // Predicates = [HasStdExtZkne, IsRV32]

let Predicates = [HasStdExtZkne, IsRV64] in {
def : PatGprGpr<int_riscv_aes64es, AES64ES>;
def : PatGprGpr<int_riscv_aes64esm, AES64ESM>;
} // Predicates = [HasStdExtZkne, IsRV64]

// Zknh
let Predicates = [HasStdExtZknh] in {
def : PatGpr<riscv_sha256sig0, SHA256SIG0>;
def : PatGpr<riscv_sha256sig1, SHA256SIG1>;
def : PatGpr<riscv_sha256sum0, SHA256SUM0>;
def : PatGpr<riscv_sha256sum1, SHA256SUM1>;
} // Predicates = [HasStdExtZknh]

let Predicates = [HasStdExtZknh, IsRV32] in {
def : PatGprGpr<int_riscv_sha512sig0l, SHA512SIG0L>;
def : PatGprGpr<int_riscv_sha512sig0h, SHA512SIG0H>;
def : PatGprGpr<int_riscv_sha512sig1l, SHA512SIG1L>;
def : PatGprGpr<int_riscv_sha512sig1h, SHA512SIG1H>;
def : PatGprGpr<int_riscv_sha512sum0r, SHA512SUM0R>;
def : PatGprGpr<int_riscv_sha512sum1r, SHA512SUM1R>;
} // Predicates = [HasStdExtZknh, IsRV32]

let Predicates = [HasStdExtZknh, IsRV64] in {
def : PatGpr<int_riscv_sha512sig0, SHA512SIG0>;
def : PatGpr<int_riscv_sha512sig1, SHA512SIG1>;
def : PatGpr<int_riscv_sha512sum0, SHA512SUM0>;
def : PatGpr<int_riscv_sha512sum1, SHA512SUM1>;
} // Predicates = [HasStdExtZknh, IsRV64]

// Zksed
let Predicates = [HasStdExtZksed] in {
def : PatGprGprByteSelect<riscv_sm4ks, SM4KS>;
def : PatGprGprByteSelect<riscv_sm4ed, SM4ED>;
} // Predicates = [HasStdExtZksed]

// Zksh
let Predicates = [HasStdExtZksh] in {
def : PatGpr<riscv_sm3p0, SM3P0>;
def : PatGpr<riscv_sm3p1, SM3P1>;
} // Predicates = [HasStdExtZksh]
