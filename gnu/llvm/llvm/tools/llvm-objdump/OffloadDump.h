//===-- OffloadDump.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_OBJDUMP_OFFLOADDUMP_H
#define LLVM_TOOLS_LLVM_OBJDUMP_OFFLOADDUMP_H

#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/OffloadBinary.h"

namespace llvm {

void dumpOffloadSections(const object::OffloadBinary &OB);
void dumpOffloadBinary(const object::ObjectFile &O);

} // namespace llvm

#endif
