//===- Lanai.td - Describe the Lanai Target Machine --------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Target-independent interfaces which we are implementing
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

//===----------------------------------------------------------------------===//
// Register File, Calling Conv, Instruction Descriptions
//===----------------------------------------------------------------------===//

include "LanaiSchedule.td"
include "LanaiRegisterInfo.td"
include "LanaiCallingConv.td"
include "LanaiInstrInfo.td"

def LanaiInstrInfo : InstrInfo;

//===----------------------------------------------------------------------===//
// Lanai processors supported.
//===----------------------------------------------------------------------===//

def : ProcessorModel<"generic", LanaiSchedModel, []>;
def : ProcessorModel<"v11", LanaiSchedModel, []>;

def LanaiInstPrinter : AsmWriter {
  string AsmWriterClassName  = "InstPrinter";
  bit isMCAsmWriter = 1;
}

//===----------------------------------------------------------------------===//
// Declare the target which we are implementing
//===----------------------------------------------------------------------===//

def Lanai : Target {
  // Pull in Instruction Info:
  let InstructionSet = LanaiInstrInfo;
  let AssemblyWriters = [LanaiInstPrinter];
}
