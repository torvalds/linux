//===- ObjCARCUtil.h - ObjC ARC Utility Functions ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file defines ARC utility functions which are used by various parts of
/// the compiler.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_OBJCARCUTIL_H
#define LLVM_ANALYSIS_OBJCARCUTIL_H

#include "llvm/Analysis/ObjCARCInstKind.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/LLVMContext.h"

namespace llvm {
namespace objcarc {

inline const char *getRVMarkerModuleFlagStr() {
  return "clang.arc.retainAutoreleasedReturnValueMarker";
}

inline bool hasAttachedCallOpBundle(const CallBase *CB) {
  // Ignore the bundle if the return type is void. Global optimization passes
  // can turn the called function's return type to void. That should happen only
  // if the call doesn't return and the call to @llvm.objc.clang.arc.noop.use
  // no longer consumes the function return or is deleted. In that case, it's
  // not necessary to emit the marker instruction or calls to the ARC runtime
  // functions.
  return !CB->getFunctionType()->getReturnType()->isVoidTy() &&
         CB->getOperandBundle(LLVMContext::OB_clang_arc_attachedcall)
             .has_value();
}

/// This function returns operand bundle clang_arc_attachedcall's argument,
/// which is the address of the ARC runtime function.
inline std::optional<Function *> getAttachedARCFunction(const CallBase *CB) {
  auto B = CB->getOperandBundle(LLVMContext::OB_clang_arc_attachedcall);
  if (!B)
    return std::nullopt;

  return cast<Function>(B->Inputs[0]);
}

/// Check whether the function is retainRV/unsafeClaimRV.
inline bool isRetainOrClaimRV(ARCInstKind Kind) {
  return Kind == ARCInstKind::RetainRV || Kind == ARCInstKind::UnsafeClaimRV;
}

/// This function returns the ARCInstKind of the function attached to operand
/// bundle clang_arc_attachedcall. It returns std::nullopt if the call doesn't
/// have the operand bundle or the operand is null. Otherwise it returns either
/// RetainRV or UnsafeClaimRV.
inline ARCInstKind getAttachedARCFunctionKind(const CallBase *CB) {
  std::optional<Function *> Fn = getAttachedARCFunction(CB);
  if (!Fn)
    return ARCInstKind::None;
  auto FnClass = GetFunctionClass(*Fn);
  assert(isRetainOrClaimRV(FnClass) && "unexpected ARC runtime function");
  return FnClass;
}

} // end namespace objcarc
} // end namespace llvm

#endif
