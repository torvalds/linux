//===- AnnotationRemarks.cpp - Emit remarks for !annotation MD --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// \file
// This file defines AnnotationRemarksPass for the new pass manager.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_ANNOTATIONREMARKS_H
#define LLVM_TRANSFORMS_SCALAR_ANNOTATIONREMARKS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

struct AnnotationRemarksPass : public PassInfoMixin<AnnotationRemarksPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  static bool isRequired() { return true; }
};
} // namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_ANNOTATIONREMARKS_H
