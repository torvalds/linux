//===-- RISCVInstrInfoZicbo.td - RISC-V CMO instructions ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the RISC-V instructions from the standard Base Cache
// Management Operation ISA Extensions document (Zicbom, Zicboz, and Zicbop).
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Operand definitions.
//===----------------------------------------------------------------------===//

// A 12-bit signed immediate where the least significant five bits are zero.
def simm12_lsb00000 : RISCVOp,
                      ImmLeaf<XLenVT, [{return isShiftedInt<7, 5>(Imm);}]> {
  let ParserMatchClass = SImmAsmOperand<12, "Lsb00000">;
  let EncoderMethod = "getImmOpValue";
  let DecoderMethod = "decodeSImmOperand<12>";
  let MCOperandPredicate = [{
    int64_t Imm;
    if (MCOp.evaluateAsConstantImm(Imm))
      return isShiftedInt<7, 5>(Imm);
    return MCOp.isBareSymbolRef();
  }];
  let OperandType = "OPERAND_SIMM12_LSB00000";
}

//===----------------------------------------------------------------------===//
// Instruction Class Templates
//===----------------------------------------------------------------------===//
let hasSideEffects = 0, mayLoad = 0, mayStore = 1 in
class CBO_r<bits<12> optype, string opcodestr>
    : RVInstI<0b010, OPC_MISC_MEM, (outs), (ins GPRMemZeroOffset:$rs1),
              opcodestr, "$rs1"> {
  let imm12 = optype;
  let rd = 0b00000;
}

let hasSideEffects = 0, mayLoad = 1, mayStore = 1 in
class Prefetch_ri<bits<5> optype, string opcodestr>
    : RVInstS<0b110, OPC_OP_IMM, (outs), (ins GPR:$rs1, simm12_lsb00000:$imm12),
              opcodestr, "${imm12}(${rs1})"> {
  let Inst{11-7} = 0b00000;
  let rs2 = optype;
}

//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//

let Predicates = [HasStdExtZicbom] in {
def CBO_CLEAN : CBO_r<0b000000000001, "cbo.clean">, Sched<[]>;
def CBO_FLUSH : CBO_r<0b000000000010, "cbo.flush">, Sched<[]>;
def CBO_INVAL : CBO_r<0b000000000000, "cbo.inval">, Sched<[]>;
} // Predicates = [HasStdExtZicbom]

let Predicates = [HasStdExtZicboz] in {
def CBO_ZERO : CBO_r<0b000000000100, "cbo.zero">, Sched<[]>;
} // Predicates = [HasStdExtZicboz]

let Predicates = [HasStdExtZicbop] in {
def PREFETCH_I : Prefetch_ri<0b00000, "prefetch.i">, Sched<[]>;
def PREFETCH_R : Prefetch_ri<0b00001, "prefetch.r">, Sched<[]>;
def PREFETCH_W : Prefetch_ri<0b00011, "prefetch.w">, Sched<[]>;
} // Predicates = [HasStdExtZicbop]

//===----------------------------------------------------------------------===//
// Patterns
//===----------------------------------------------------------------------===//

def AddrRegImmLsb00000 : ComplexPattern<iPTR, 2, "SelectAddrRegImmLsb00000">;

let Predicates = [HasStdExtZicbop] in {
  def : Pat<(prefetch (AddrRegImmLsb00000 (XLenVT GPR:$rs1), simm12_lsb00000:$imm12),
                      timm, timm, (i32 0)),
            (PREFETCH_I GPR:$rs1, simm12_lsb00000:$imm12)>;
  def : Pat<(prefetch (AddrRegImmLsb00000 (XLenVT GPR:$rs1), simm12_lsb00000:$imm12),
                      (i32 0), timm, (i32 1)),
            (PREFETCH_R GPR:$rs1, simm12_lsb00000:$imm12)>;
  def : Pat<(prefetch (AddrRegImmLsb00000 (XLenVT GPR:$rs1), simm12_lsb00000:$imm12),
                      (i32 1), timm, (i32 1)),
            (PREFETCH_W GPR:$rs1, simm12_lsb00000:$imm12)>;
}
