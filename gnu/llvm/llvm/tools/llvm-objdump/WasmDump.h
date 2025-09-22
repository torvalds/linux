//===-- WasmDump.h - wasm-specific dumper -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_OBJDUMP_WASMDUMP_H
#define LLVM_TOOLS_LLVM_OBJDUMP_WASMDUMP_H

#include "llvm/ADT/SmallVector.h"

namespace llvm {

class Error;

namespace object {
class WasmObjectFile;
class ObjectFile;
class RelocationRef;
} // namespace object

namespace objdump {

Error getWasmRelocationValueString(const object::WasmObjectFile *Obj,
                                   const object::RelocationRef &RelRef,
                                   llvm::SmallVectorImpl<char> &Result);

void printWasmFileHeader(const object::ObjectFile *O);

} // namespace objdump
} // namespace llvm

#endif
