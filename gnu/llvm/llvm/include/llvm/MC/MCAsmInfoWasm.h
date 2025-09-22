//===-- llvm/MC/MCAsmInfoWasm.h - Wasm Asm info -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCASMINFOWASM_H
#define LLVM_MC_MCASMINFOWASM_H

#include "llvm/MC/MCAsmInfo.h"

namespace llvm {
class MCAsmInfoWasm : public MCAsmInfo {
  virtual void anchor();

protected:
  MCAsmInfoWasm();
};
} // namespace llvm

#endif
