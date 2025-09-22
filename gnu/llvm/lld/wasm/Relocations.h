//===- Relocations.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_WASM_RELOCATIONS_H
#define LLD_WASM_RELOCATIONS_H

namespace lld::wasm {

class InputChunk;

void scanRelocations(InputChunk *chunk);

} // namespace lld::wasm

#endif
