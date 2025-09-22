//===- MapFile.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_WASM_MAPFILE_H
#define LLD_WASM_MAPFILE_H

#include "llvm/ADT/ArrayRef.h"

namespace lld::wasm {
class OutputSection;
void writeMapFile(llvm::ArrayRef<OutputSection *> outputSections);
} // namespace lld::wasm

#endif
