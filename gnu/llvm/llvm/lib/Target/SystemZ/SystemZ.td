//===-- SystemZ.td - Describe the SystemZ target machine -----*- tblgen -*-===//
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
// SystemZ subtarget features
//===----------------------------------------------------------------------===//

include "SystemZFeatures.td"

//===----------------------------------------------------------------------===//
// SystemZ subtarget scheduling models
//===----------------------------------------------------------------------===//

include "SystemZSchedule.td"

//===----------------------------------------------------------------------===//
// SystemZ supported processors
//===----------------------------------------------------------------------===//

include "SystemZProcessors.td"

//===----------------------------------------------------------------------===//
// Register file description
//===----------------------------------------------------------------------===//

include "SystemZRegisterInfo.td"

//===----------------------------------------------------------------------===//
// Calling convention description
//===----------------------------------------------------------------------===//

include "SystemZCallingConv.td"

//===----------------------------------------------------------------------===//
// Instruction descriptions
//===----------------------------------------------------------------------===//

include "SystemZOperators.td"
include "SystemZOperands.td"
include "SystemZPatterns.td"
include "SystemZInstrFormats.td"
include "SystemZInstrInfo.td"
include "SystemZInstrVector.td"
include "SystemZInstrFP.td"
include "SystemZInstrHFP.td"
include "SystemZInstrDFP.td"
include "SystemZInstrSystem.td"

def SystemZInstrInfo : InstrInfo { let guessInstructionProperties = 0; }

//===----------------------------------------------------------------------===//
// Assembly parser
//===----------------------------------------------------------------------===//

def SystemZAsmParser : AsmParser {
  let ShouldEmitMatchRegisterName = 0;
}

def ATTAsmParserVariant : AsmParserVariant {
  int Variant = 0;

  // Variant name.
  string Name = "att";
}

def HLASMAsmParserVariant : AsmParserVariant {
  int Variant = 1;

  // Variant name.
  string Name = "hlasm";
}

//===----------------------------------------------------------------------===//
// Top-level target declaration
//===----------------------------------------------------------------------===//

def SystemZ : Target {
  let InstructionSet = SystemZInstrInfo;
  let AssemblyParsers = [SystemZAsmParser];
  let AssemblyParserVariants = [ATTAsmParserVariant, HLASMAsmParserVariant];
  let AllowRegisterRenaming = 1;
}
