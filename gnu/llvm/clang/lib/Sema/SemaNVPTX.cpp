//===------ SemaNVPTX.cpp ------- NVPTX target-specific routines ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis functions specific to NVPTX.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/SemaNVPTX.h"
#include "clang/Basic/TargetBuiltins.h"
#include "clang/Sema/Sema.h"

namespace clang {

SemaNVPTX::SemaNVPTX(Sema &S) : SemaBase(S) {}

bool SemaNVPTX::CheckNVPTXBuiltinFunctionCall(const TargetInfo &TI,
                                              unsigned BuiltinID,
                                              CallExpr *TheCall) {
  switch (BuiltinID) {
  case NVPTX::BI__nvvm_cp_async_ca_shared_global_4:
  case NVPTX::BI__nvvm_cp_async_ca_shared_global_8:
  case NVPTX::BI__nvvm_cp_async_ca_shared_global_16:
  case NVPTX::BI__nvvm_cp_async_cg_shared_global_16:
    return SemaRef.checkArgCountAtMost(TheCall, 3);
  }

  return false;
}

} // namespace clang
