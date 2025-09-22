//===-- COFFDump.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_OBJDUMP_COFFDUMP_H
#define LLVM_TOOLS_LLVM_OBJDUMP_COFFDUMP_H

#include "llvm/ADT/SmallVector.h"

namespace llvm {

class Error;

namespace object {
class COFFObjectFile;
class COFFImportFile;
class RelocationRef;
} // namespace object

namespace objdump {
Error getCOFFRelocationValueString(const object::COFFObjectFile *Obj,
                                   const object::RelocationRef &Rel,
                                   llvm::SmallVectorImpl<char> &Result);

void printCOFFUnwindInfo(const object::COFFObjectFile *O);
void printCOFFFileHeader(const object::COFFObjectFile &Obj);
void printCOFFSymbolTable(const object::COFFImportFile &I);
void printCOFFSymbolTable(const object::COFFObjectFile &O);
} // namespace objdump
} // namespace llvm

#endif
