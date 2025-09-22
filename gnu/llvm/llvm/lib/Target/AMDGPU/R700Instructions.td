//===-- R700Instructions.td - R700 Instruction defs  -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// TableGen definitions for instructions which are:
// - Available to R700 and newer VLIW4/VLIW5 GPUs
// - Available only on R700 family GPUs.
//
//===----------------------------------------------------------------------===//

def isR700 : Predicate<"Subtarget->getGeneration() == AMDGPUSubtarget::R700">;

let Predicates = [isR700] in {
  def SIN_r700 : SIN_Common<0x6E>;
  def COS_r700 : COS_Common<0x6F>;
}
