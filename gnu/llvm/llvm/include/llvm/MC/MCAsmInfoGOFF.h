//===- MCAsmInfoGOFF.h - GOFF Asm Info Fields -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines certain target specific asm properties for GOFF (z/OS)
/// based targets.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCASMINFOGOFF_H
#define LLVM_MC_MCASMINFOGOFF_H

#include "llvm/MC/MCAsmInfo.h"

namespace llvm {
class MCAsmInfoGOFF : public MCAsmInfo {
  virtual void anchor();

protected:
  MCAsmInfoGOFF();
};
} // end namespace llvm

#endif // LLVM_MC_MCASMINFOGOFF_H
