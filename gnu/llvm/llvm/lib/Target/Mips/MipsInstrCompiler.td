//===- MipsInstrCompiler.td - Compiler Pseudos and Patterns -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the various pseudo instructions used by the compiler,
// as well as Pat patterns used during instruction selection.
//
//===----------------------------------------------------------------------===//


def shiftMask_32 : PatFrag<(ops node:$lhs), (and node:$lhs, imm), [{
  return isUnneededShiftMask(N, 5);
}]>;

def shiftMask_64 : PatFrag<(ops node:$src0), (and node:$src0, imm), [{
  return isUnneededShiftMask(N, 6);
}]>;

foreach width = [32, 64] in {
defvar shiftMask = !cast<SDPatternOperator>("shiftMask_"#width);
def mshl_#width : PatFrags<(ops node:$src0, node:$src1),
  [(shl node:$src0, node:$src1), (shl node:$src0, (shiftMask node:$src1))]>;

def msrl_#width : PatFrags<(ops node:$src0, node:$src1),
  [(srl node:$src0, node:$src1), (srl node:$src0, (shiftMask node:$src1))]>;

def msra_#width : PatFrags<(ops node:$src0, node:$src1),
  [(sra node:$src0, node:$src1), (sra node:$src0, (shiftMask node:$src1))]>;
}
