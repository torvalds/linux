//===- Target/DirectX/PointerTypeAnalysis.h - PointerType analysis --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Analysis pass to assign types to opaque pointers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGET_DIRECTX_POINTERTYPEANALYSIS_H
#define LLVM_TARGET_DIRECTX_POINTERTYPEANALYSIS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/TypedPointerType.h"
#include "llvm/IR/Value.h"

namespace llvm {

namespace dxil {

// Store the underlying type and the number of pointer indirections
using PointerTypeMap = DenseMap<const Value *, Type *>;

/// An analysis to compute the \c PointerTypes for pointers in a \c Module.
/// Since this analysis is only run during codegen and the new pass manager
/// doesn't support codegen passes, this is wrtten as a function in a namespace.
/// It is very simple to transform it into a proper analysis pass.
/// This code relies on typed pointers existing as LLVM types, but could be
/// migrated to a custom Type if PointerType loses typed support.
namespace PointerTypeAnalysis {

/// Compute the \c PointerTypeMap for the module \c M.
PointerTypeMap run(const Module &M);
} // namespace PointerTypeAnalysis

} // namespace dxil

} // namespace llvm

#endif // LLVM_TARGET_DIRECTX_POINTERTYPEANALYSIS_H
