//===---- X86InstrRAOINT.td -------------------------------*- tablegen -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the instructions that make up the Intel RAO-INT
// instruction set.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// RAO-INT instructions

def SDTRAOBinaryArith : SDTypeProfile<0, 2, [SDTCisPtrTy<0>, SDTCisInt<1>]>;

def X86rao_add  : SDNode<"X86ISD::AADD", SDTRAOBinaryArith,
                         [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;
def X86rao_or   : SDNode<"X86ISD::AOR",  SDTRAOBinaryArith,
                         [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;
def X86rao_xor  : SDNode<"X86ISD::AXOR", SDTRAOBinaryArith,
                         [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;
def X86rao_and  : SDNode<"X86ISD::AAND", SDTRAOBinaryArith,
                         [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;

multiclass RaoInt<string m, string suffix = ""> {
  let Pattern = [(!cast<SDNode>("X86rao_" # m) addr:$src1, GR32:$src2)] in
    def 32mr#suffix : BinOpMR_M<0xfc, "a" # m, Xi32>;
  let Pattern = [(!cast<SDNode>("X86rao_" # m) addr:$src1, GR64:$src2)] in
    def 64mr#suffix : BinOpMR_M<0xfc, "a" # m, Xi64>;
}

let Predicates = [HasRAOINT, NoEGPR] in {
  defm AADD : RaoInt<"add">, T8;
  defm AAND : RaoInt<"and">, T8, PD;
  defm AOR  : RaoInt<"or" >, T8, XD;
  defm AXOR : RaoInt<"xor">, T8, XS;
}

let Predicates = [HasRAOINT, HasEGPR, In64BitMode] in {
  defm AADD : RaoInt<"add", "_EVEX">, EVEX, T_MAP4;
  defm AAND : RaoInt<"and", "_EVEX">, EVEX, T_MAP4, PD;
  defm AOR  : RaoInt<"or",  "_EVEX">, EVEX, T_MAP4, XD;
  defm AXOR : RaoInt<"xor", "_EVEX">, EVEX, T_MAP4, XS;
}
