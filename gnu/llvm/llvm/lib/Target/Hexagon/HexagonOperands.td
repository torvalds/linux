//===--- HexagonOperands.td -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

def f32ImmOperand : AsmOperandClass { let Name = "f32Imm"; }
def f32Imm : Operand<f32> { let ParserMatchClass = f32ImmOperand; }
def f64ImmOperand : AsmOperandClass { let Name = "f64Imm"; }
def f64Imm : Operand<f64> { let ParserMatchClass = f64ImmOperand; }
def s8_0Imm64Pred  : PatLeaf<(i64 imm), [{ return isInt<8>(N->getSExtValue()); }]>;
def s9_0ImmOperand : AsmOperandClass { let Name = "s9_0Imm"; }
def s9_0Imm : Operand<i32> { let ParserMatchClass = s9_0ImmOperand; }
def s27_2ImmOperand : AsmOperandClass { let Name = "s27_2Imm"; let RenderMethod = "addSignedImmOperands"; }
def s27_2Imm : Operand<i32> { let ParserMatchClass = s27_2ImmOperand; }
def r32_0ImmPred  : PatLeaf<(i32 imm), [{
  int64_t v = (int64_t)N->getSExtValue();
  return isInt<32>(v);
}]>;
def u9_0ImmPred  : PatLeaf<(i32 imm), [{
  int64_t v = (int64_t)N->getSExtValue();
  return isUInt<9>(v);
}]>;

def u64_0ImmOperand : AsmOperandClass { let Name = "u64_0Imm"; let RenderMethod = "addImmOperands"; }
def u64_0Imm : Operand<i64> { let ParserMatchClass = u64_0ImmOperand; }
def n1ConstOperand : AsmOperandClass { let Name = "n1Const"; }
def n1Const : Operand<i32> { let ParserMatchClass = n1ConstOperand; }
def sgp10ConstOperand : AsmOperandClass { let Name = "sgp10Const"; }
def sgp10Const : Operand<i32> { let ParserMatchClass = sgp10ConstOperand; }

def bblabel : Operand<i32>;
def bbl     : SDNode<"ISD::BasicBlock", SDTPtrLeaf, [], "BasicBlockSDNode">;
