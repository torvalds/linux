//===-- LTODisassembler.cpp - LTO Disassembler interface ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This function provides utility methods used by clients of libLTO that want
// to use the disassembler.
//
//===----------------------------------------------------------------------===//

#include "llvm-c/lto.h"
#include "llvm/Support/TargetSelect.h"

using namespace llvm;

void lto_initialize_disassembler() {
  // Initialize targets and assembly printers/parsers.
  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmParsers();
  llvm::InitializeAllDisassemblers();
}
