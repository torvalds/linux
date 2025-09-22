//- DirectX.td - Describe the DirectX Target Machine ----------*- tablegen -*-//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This is a target description file for the DirectX target
///
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Target-independent interfaces which we are implementing
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"
include "DXILStubs.td"

//===----------------------------------------------------------------------===//
// DirectX Subtarget features.
//===----------------------------------------------------------------------===//

def DirectXInstrInfo : InstrInfo;

//===----------------------------------------------------------------------===//
// DirectX Processors supported.
//===----------------------------------------------------------------------===//

def : ProcessorModel<"generic", NoSchedModel, []>;


//===----------------------------------------------------------------------===//
// Target Declaration
//===----------------------------------------------------------------------===//

def DirectXAsmParser : AsmParser {
  // The physical register names are not in the binary format or asm text
  let ShouldEmitMatchRegisterName = 0;
}

def DirectXAsmWriter : AsmWriter {
  string AsmWriterClassName  = "InstPrinter";
  int PassSubtarget = 0;
  int Variant = 0;
  bit isMCAsmWriter = 1;
}

def DirectX : Target {
  let InstructionSet = DirectXInstrInfo;
  let AssemblyParsers  = [DirectXAsmParser];
  let AssemblyWriters = [DirectXAsmWriter];
}
