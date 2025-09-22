//===-- RISCVInstrInfoM.td - RISC-V 'M' instructions -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the RISC-V instructions from the standard 'M', Integer
// Multiplication and Division instruction set extension.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// RISC-V specific DAG Nodes.
//===----------------------------------------------------------------------===//

def riscv_mulhsu : SDNode<"RISCVISD::MULHSU", SDTIntBinOp>;
def riscv_divw  : SDNode<"RISCVISD::DIVW",  SDT_RISCVIntBinOpW>;
def riscv_divuw : SDNode<"RISCVISD::DIVUW", SDT_RISCVIntBinOpW>;
def riscv_remuw : SDNode<"RISCVISD::REMUW", SDT_RISCVIntBinOpW>;

//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//

let Predicates = [HasStdExtZmmul] in {
def MUL     : ALU_rr<0b0000001, 0b000, "mul", Commutable=1>,
              Sched<[WriteIMul, ReadIMul, ReadIMul]>;
def MULH    : ALU_rr<0b0000001, 0b001, "mulh", Commutable=1>,
              Sched<[WriteIMul, ReadIMul, ReadIMul]>;
def MULHSU  : ALU_rr<0b0000001, 0b010, "mulhsu">,
              Sched<[WriteIMul, ReadIMul, ReadIMul]>;
def MULHU   : ALU_rr<0b0000001, 0b011, "mulhu", Commutable=1>,
              Sched<[WriteIMul, ReadIMul, ReadIMul]>;
} // Predicates = [HasStdExtZmmul]

let Predicates = [HasStdExtM] in {
def DIV     : ALU_rr<0b0000001, 0b100, "div">,
              Sched<[WriteIDiv, ReadIDiv, ReadIDiv]>;
def DIVU    : ALU_rr<0b0000001, 0b101, "divu">,
              Sched<[WriteIDiv, ReadIDiv, ReadIDiv]>;
def REM     : ALU_rr<0b0000001, 0b110, "rem">,
              Sched<[WriteIRem, ReadIRem, ReadIRem]>;
def REMU    : ALU_rr<0b0000001, 0b111, "remu">,
              Sched<[WriteIRem, ReadIRem, ReadIRem]>;
} // Predicates = [HasStdExtM]

let Predicates = [HasStdExtZmmul, IsRV64], IsSignExtendingOpW = 1 in {
def MULW    : ALUW_rr<0b0000001, 0b000, "mulw", Commutable=1>,
              Sched<[WriteIMul32, ReadIMul32, ReadIMul32]>;
} // Predicates = [HasStdExtZmmul, IsRV64]

let Predicates = [HasStdExtM, IsRV64], IsSignExtendingOpW = 1 in {
def DIVW    : ALUW_rr<0b0000001, 0b100, "divw">,
              Sched<[WriteIDiv32, ReadIDiv32, ReadIDiv32]>;
def DIVUW   : ALUW_rr<0b0000001, 0b101, "divuw">,
              Sched<[WriteIDiv32, ReadIDiv32, ReadIDiv32]>;
def REMW    : ALUW_rr<0b0000001, 0b110, "remw">,
              Sched<[WriteIRem32, ReadIRem32, ReadIRem32]>;
def REMUW   : ALUW_rr<0b0000001, 0b111, "remuw">,
              Sched<[WriteIRem32, ReadIRem32, ReadIRem32]>;
} // Predicates = [HasStdExtM, IsRV64]

//===----------------------------------------------------------------------===//
// Pseudo-instructions and codegen patterns
//===----------------------------------------------------------------------===//

let Predicates = [HasStdExtZmmul] in {
def : PatGprGpr<mul, MUL>;
def : PatGprGpr<mulhs, MULH>;
def : PatGprGpr<mulhu, MULHU>;
def : PatGprGpr<riscv_mulhsu, MULHSU>;
} // Predicates = [HasStdExtZmmul]

let Predicates = [HasStdExtM] in {
def : PatGprGpr<sdiv, DIV>;
def : PatGprGpr<udiv, DIVU>;
def : PatGprGpr<srem, REM>;
def : PatGprGpr<urem, REMU>;
} // Predicates = [HasStdExtM]

// Select W instructions if only the lower 32-bits of the result are used.
let Predicates = [HasStdExtZmmul, IsRV64] in
def : PatGprGpr<binop_allwusers<mul>, MULW>;

let Predicates = [HasStdExtM, IsRV64] in {
def : PatGprGpr<riscv_divw, DIVW>;
def : PatGprGpr<riscv_divuw, DIVUW>;
def : PatGprGpr<riscv_remuw, REMUW>;

// Handle the specific cases where using DIVU/REMU would be correct and result
// in fewer instructions than emitting DIVUW/REMUW then zero-extending the
// result.
def : Pat<(and (riscv_divuw (assertzexti32 GPR:$rs1),
                            (assertzexti32 GPR:$rs2)), 0xffffffff),
          (DIVU GPR:$rs1, GPR:$rs2)>;
def : Pat<(and (riscv_remuw (assertzexti32 GPR:$rs1),
                            (assertzexti32 GPR:$rs2)), 0xffffffff),
          (REMU GPR:$rs1, GPR:$rs2)>;

// Although the sexti32 operands may not have originated from an i32 srem,
// this pattern is safe as it is impossible for two sign extended inputs to
// produce a result where res[63:32]=0 and res[31]=1.
def : Pat<(srem (sexti32 (i64 GPR:$rs1)), (sexti32 (i64 GPR:$rs2))),
          (REMW GPR:$rs1, GPR:$rs2)>;
} // Predicates = [HasStdExtM, IsRV64]

let Predicates = [HasStdExtZmmul, IsRV64, NotHasStdExtZba] in {
// Special case for calculating the full 64-bit product of a 32x32 unsigned
// multiply where the inputs aren't known to be zero extended. We can shift the
// inputs left by 32 and use a MULHU. This saves two SRLIs needed to finish
// zeroing the upper 32 bits.
def : Pat<(i64 (mul (and GPR:$rs1, 0xffffffff), (and GPR:$rs2, 0xffffffff))),
          (MULHU (i64 (SLLI GPR:$rs1, 32)), (i64 (SLLI GPR:$rs2, 32)))>;
} // Predicates = [HasStdExtZmmul, IsRV64, NotHasStdExtZba]

//===----------------------------------------------------------------------===//
// Experimental RV64 i32 legalization patterns.
//===----------------------------------------------------------------------===//

let Predicates = [HasStdExtZmmul, IsRV64] in {
def : PatGprGpr<mul, MULW, i32, i32>;
}

let Predicates = [HasStdExtM, IsRV64] in {
def : PatGprGpr<sdiv, DIVW, i32, i32>;
def : PatGprGpr<udiv, DIVUW, i32, i32>;
def : PatGprGpr<srem, REMW, i32, i32>;
def : PatGprGpr<urem, REMUW, i32, i32>;
}
