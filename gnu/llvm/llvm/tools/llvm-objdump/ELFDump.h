//===-- ELFDump.h - ELF-specific dumper -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_OBJDUMP_ELFDUMP_H
#define LLVM_TOOLS_LLVM_OBJDUMP_ELFDUMP_H

#include "llvm/ADT/SmallVector.h"

namespace llvm {

class Error;

namespace object {
class ELFObjectFileBase;
class ELFSectionRef;
class ObjectFile;
class RelocationRef;
} // namespace object

namespace objdump {

Error getELFRelocationValueString(const object::ELFObjectFileBase *Obj,
                                  const object::RelocationRef &Rel,
                                  llvm::SmallVectorImpl<char> &Result);
uint64_t getELFSectionLMA(const object::ELFSectionRef &Sec);

void printELFFileHeader(const object::ObjectFile *O);

} // namespace objdump
} // namespace llvm

#endif
