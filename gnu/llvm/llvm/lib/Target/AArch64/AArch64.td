//=- AArch64.td - Describe the AArch64 Target Machine --------*- tablegen -*-=//
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
// Target-independent interfaces which we are implementing.
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

//===----------------------------------------------------------------------===//
// Subtarget features.
//===----------------------------------------------------------------------===//
include "AArch64Features.td"
include "AArch64FMV.td"

//===----------------------------------------------------------------------===//
// Register File Description
//===----------------------------------------------------------------------===//

include "AArch64RegisterInfo.td"
include "AArch64RegisterBanks.td"
include "AArch64CallingConvention.td"

//===----------------------------------------------------------------------===//
// Instruction Descriptions
//===----------------------------------------------------------------------===//

include "AArch64Schedule.td"
include "AArch64InstrInfo.td"
include "AArch64SchedPredicates.td"
include "AArch64SchedPredExynos.td"
include "AArch64SchedPredNeoverse.td"
include "AArch64Combine.td"

def AArch64InstrInfo : InstrInfo;

//===----------------------------------------------------------------------===//
// Named operands for MRS/MSR/TLBI/...
//===----------------------------------------------------------------------===//

include "AArch64SystemOperands.td"

//===----------------------------------------------------------------------===//
// AArch64 Processors supported.
//

//===----------------------------------------------------------------------===//
// Unsupported features to disable for scheduling models
//===----------------------------------------------------------------------===//

class AArch64Unsupported { list<Predicate> F; }

let F = [HasSVE2p1, HasSVE2p1_or_HasSME2, HasSVE2p1_or_HasSME2p1] in
def SVE2p1Unsupported : AArch64Unsupported;

def SVE2Unsupported : AArch64Unsupported {
  let F = !listconcat([HasSVE2, HasSVE2orSME, HasSVE2orSME2, HasSSVE_FP8FMA, HasSMEF8F16,
                       HasSMEF8F32, HasSVE2AES, HasSVE2SHA3, HasSVE2SM4, HasSVE2BitPerm],
                       SVE2p1Unsupported.F);
}

def SVEUnsupported : AArch64Unsupported {
  let F = !listconcat([HasSVE, HasSVEorSME],
                      SVE2Unsupported.F);
}

let F = [HasSME2p1, HasSVE2p1_or_HasSME2p1] in
def SME2p1Unsupported : AArch64Unsupported;

def SME2Unsupported : AArch64Unsupported {
  let F = !listconcat([HasSME2, HasSVE2orSME2, HasSVE2p1_or_HasSME2, HasSSVE_FP8FMA,
                      HasSMEF8F16, HasSMEF8F32, HasSMEF16F16orSMEF8F16],
                      SME2p1Unsupported.F);
}

def SMEUnsupported : AArch64Unsupported {
  let F = !listconcat([HasSME, HasSMEI16I64, HasSMEF16F16, HasSMEF64F64, HasSMEFA64],
                      SME2Unsupported.F);
}

def MTEUnsupported : AArch64Unsupported {
  let F = [HasMTE];
}

let F = [HasPAuth, HasPAuthLR] in
def PAUnsupported : AArch64Unsupported;

include "AArch64SchedA53.td"
include "AArch64SchedA55.td"
include "AArch64SchedA510.td"
include "AArch64SchedA57.td"
include "AArch64SchedCyclone.td"
include "AArch64SchedFalkor.td"
include "AArch64SchedKryo.td"
include "AArch64SchedExynosM3.td"
include "AArch64SchedExynosM4.td"
include "AArch64SchedExynosM5.td"
include "AArch64SchedThunderX.td"
include "AArch64SchedThunderX2T99.td"
include "AArch64SchedA64FX.td"
include "AArch64SchedThunderX3T110.td"
include "AArch64SchedTSV110.td"
include "AArch64SchedAmpere1.td"
include "AArch64SchedAmpere1B.td"
include "AArch64SchedNeoverseN1.td"
include "AArch64SchedNeoverseN2.td"
include "AArch64SchedNeoverseV1.td"
include "AArch64SchedNeoverseV2.td"
include "AArch64SchedOryon.td"

include "AArch64Processors.td"

//===----------------------------------------------------------------------===//
// Assembly parser
//===----------------------------------------------------------------------===//

def GenericAsmParserVariant : AsmParserVariant {
  int Variant = 0;
  string Name = "generic";
  string BreakCharacters = ".";
  string TokenizingCharacters = "[]*!/";
}

def AppleAsmParserVariant : AsmParserVariant {
  int Variant = 1;
  string Name = "apple-neon";
  string BreakCharacters = ".";
  string TokenizingCharacters = "[]*!/";
}

//===----------------------------------------------------------------------===//
// Assembly printer
//===----------------------------------------------------------------------===//
// AArch64 Uses the MC printer for asm output, so make sure the TableGen
// AsmWriter bits get associated with the correct class.
def GenericAsmWriter : AsmWriter {
  string AsmWriterClassName  = "InstPrinter";
  int PassSubtarget = 1;
  int Variant = 0;
  bit isMCAsmWriter = 1;
}

def AppleAsmWriter : AsmWriter {
  let AsmWriterClassName = "AppleInstPrinter";
  int PassSubtarget = 1;
  int Variant = 1;
  int isMCAsmWriter = 1;
}

//===----------------------------------------------------------------------===//
// Target Declaration
//===----------------------------------------------------------------------===//

def AArch64 : Target {
  let InstructionSet = AArch64InstrInfo;
  let AssemblyParserVariants = [GenericAsmParserVariant, AppleAsmParserVariant];
  let AssemblyWriters = [GenericAsmWriter, AppleAsmWriter];
  let AllowRegisterRenaming = 1;
}

//===----------------------------------------------------------------------===//
// Pfm Counters
//===----------------------------------------------------------------------===//

include "AArch64PfmCounters.td"
