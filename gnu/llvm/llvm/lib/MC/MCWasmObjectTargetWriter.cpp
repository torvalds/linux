//===-- MCWasmObjectTargetWriter.cpp - Wasm Target Writer Subclass --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCWasmObjectWriter.h"

using namespace llvm;

MCWasmObjectTargetWriter::MCWasmObjectTargetWriter(bool Is64Bit,
                                                   bool IsEmscripten)
    : Is64Bit(Is64Bit), IsEmscripten(IsEmscripten) {}

// Pin the vtable to this object file
MCWasmObjectTargetWriter::~MCWasmObjectTargetWriter() = default;
