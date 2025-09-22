//===-- VE.td - Describe the VE Target Machine -------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Target-independent interfaces which we are implementing
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

//===----------------------------------------------------------------------===//
// VE Subtarget features.
//
def FeatureEnableVPU
  : SubtargetFeature<"vpu", "EnableVPU", "true",
                     "Enable the VPU">;

//===----------------------------------------------------------------------===//
// Register File, Calling Conv, Instruction Descriptions
//===----------------------------------------------------------------------===//

include "VERegisterInfo.td"
include "VECallingConv.td"
include "VEInstrInfo.td"

def VEInstrInfo : InstrInfo {}

def VEAsmParser : AsmParser {
  // Use both VE register name matcher to accept "S0~S63" register names
  // and default register matcher to accept other registeres.
  let AllowDuplicateRegisterNames = 1;
  let ShouldEmitMatchRegisterAltName = 1;
}

//===----------------------------------------------------------------------===//
// VE processors supported.
//===----------------------------------------------------------------------===//

class Proc<string Name, list<SubtargetFeature> Features>
 : Processor<Name, NoItineraries, Features>;

def : Proc<"generic", [FeatureEnableVPU]>;

//===----------------------------------------------------------------------===//
// Declare the target which we are implementing
//===----------------------------------------------------------------------===//

def VEAsmWriter : AsmWriter {
  string AsmWriterClassName  = "InstPrinter";
  int PassSubtarget = 1;
  int Variant = 0;
}

def VE : Target {
  // Pull in Instruction Info:
  let InstructionSet = VEInstrInfo;
  let AssemblyParsers = [VEAsmParser];
  let AssemblyWriters = [VEAsmWriter];
  let AllowRegisterRenaming = 1;
}
