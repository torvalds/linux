//===-- OMP.h - Core OpenMP definitions and declarations ---------- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the core set of OpenMP definitions and declarations.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_OPENMP_OMP_H
#define LLVM_FRONTEND_OPENMP_OMP_H

#include "llvm/Frontend/OpenMP/OMP.h.inc"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"

namespace llvm::omp {
ArrayRef<Directive> getLeafConstructs(Directive D);
ArrayRef<Directive> getLeafConstructsOrSelf(Directive D);

ArrayRef<Directive>
getLeafOrCompositeConstructs(Directive D, SmallVectorImpl<Directive> &Output);

Directive getCompoundConstruct(ArrayRef<Directive> Parts);

bool isLeafConstruct(Directive D);
bool isCompositeConstruct(Directive D);
bool isCombinedConstruct(Directive D);
} // namespace llvm::omp

#endif // LLVM_FRONTEND_OPENMP_OMP_H
