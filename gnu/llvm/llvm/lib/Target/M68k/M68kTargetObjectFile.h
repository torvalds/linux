//===-- M68kELFTargetObjectFile.h - M68k Object Info ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains declarations for M68k ELF object file lowering.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_M68K_M68KTARGETOBJECTFILE_H
#define LLVM_LIB_TARGET_M68K_M68KTARGETOBJECTFILE_H

#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"

namespace llvm {
class M68kTargetMachine;
class M68kELFTargetObjectFile : public TargetLoweringObjectFileELF {
  const M68kTargetMachine *TM;
  MCSection *SmallDataSection;
  MCSection *SmallBSSSection;

public:
  void Initialize(MCContext &Ctx, const TargetMachine &TM) override;
};
} // end namespace llvm

#endif // LLVM_LIB_TARGET_M68K_M68KTARGETOBJECTFILE_H
