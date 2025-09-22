//===-- X86ISelDAGToDAG.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_X86ISELDAGTODAG_H
#define LLVM_LIB_TARGET_X86_X86ISELDAGTODAG_H

#include "llvm/CodeGen/SelectionDAGISel.h"

namespace llvm {

class X86TargetMachine;

class X86ISelDAGToDAGPass : public SelectionDAGISelPass {
public:
  X86ISelDAGToDAGPass(X86TargetMachine &TM);
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_X86_X86ISELDAGTODAG_H
