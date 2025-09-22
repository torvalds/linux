//=-- Hexagon.h - Top-level interface for Hexagon representation --*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in the LLVM
// Hexagon back-end.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_HEXAGON_H
#define LLVM_LIB_TARGET_HEXAGON_HEXAGON_H

namespace llvm {
  class HexagonTargetMachine;
  class ImmutablePass;
  class PassRegistry;

  /// Creates a Hexagon-specific Target Transformation Info pass.
  ImmutablePass *createHexagonTargetTransformInfoPass(const HexagonTargetMachine *TM);

  void initializeHexagonDAGToDAGISelLegacyPass(PassRegistry &);
} // end namespace llvm;

#endif
