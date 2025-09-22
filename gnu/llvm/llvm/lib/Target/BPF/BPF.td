//===-- BPF.td - Describe the BPF Target Machine -----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

include "BPFRegisterInfo.td"
include "BPFCallingConv.td"
include "BPFInstrInfo.td"
include "GISel/BPFRegisterBanks.td"

def BPFInstrInfo : InstrInfo;

class Proc<string Name, list<SubtargetFeature> Features>
 : Processor<Name, NoItineraries, Features>;

def DummyFeature : SubtargetFeature<"dummy", "isDummyMode",
                                    "true", "unused feature">;

def ALU32 : SubtargetFeature<"alu32", "HasAlu32", "true",
                             "Enable ALU32 instructions">;

def DwarfRIS: SubtargetFeature<"dwarfris", "UseDwarfRIS", "true",
                               "Disable MCAsmInfo DwarfUsesRelocationsAcrossSections">;

def : Proc<"generic", []>;
def : Proc<"v1", []>;
def : Proc<"v2", []>;
def : Proc<"v3", [ALU32]>;
def : Proc<"v4", [ALU32]>;
def : Proc<"probe", []>;

def BPFInstPrinter : AsmWriter {
  string AsmWriterClassName  = "InstPrinter";
  bit isMCAsmWriter = 1;
}

def BPFAsmParser : AsmParser {
  bit HasMnemonicFirst = 0;
}

def BPFAsmParserVariant : AsmParserVariant {
  int Variant = 0;
  string Name = "BPF";
  string BreakCharacters = ".";
  string TokenizingCharacters = "#()[]=:.<>!+*%/";
}

def BPF : Target {
  let InstructionSet = BPFInstrInfo;
  let AssemblyWriters = [BPFInstPrinter];
  let AssemblyParsers = [BPFAsmParser];
  let AssemblyParserVariants = [BPFAsmParserVariant];
}
