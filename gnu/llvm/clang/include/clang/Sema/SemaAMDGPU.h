//===----- SemaAMDGPU.h --- AMDGPU target-specific routines ---*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file declares semantic analysis functions specific to AMDGPU.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_SEMAAMDGPU_H
#define LLVM_CLANG_SEMA_SEMAAMDGPU_H

#include "clang/AST/Attr.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/Expr.h"
#include "clang/Basic/AttributeCommonInfo.h"
#include "clang/Sema/ParsedAttr.h"
#include "clang/Sema/SemaBase.h"

namespace clang {
class SemaAMDGPU : public SemaBase {
public:
  SemaAMDGPU(Sema &S);

  bool CheckAMDGCNBuiltinFunctionCall(unsigned BuiltinID, CallExpr *TheCall);

  /// Create an AMDGPUWavesPerEUAttr attribute.
  AMDGPUFlatWorkGroupSizeAttr *
  CreateAMDGPUFlatWorkGroupSizeAttr(const AttributeCommonInfo &CI, Expr *Min,
                                    Expr *Max);

  /// addAMDGPUFlatWorkGroupSizeAttr - Adds an amdgpu_flat_work_group_size
  /// attribute to a particular declaration.
  void addAMDGPUFlatWorkGroupSizeAttr(Decl *D, const AttributeCommonInfo &CI,
                                      Expr *Min, Expr *Max);

  /// Create an AMDGPUWavesPerEUAttr attribute.
  AMDGPUWavesPerEUAttr *
  CreateAMDGPUWavesPerEUAttr(const AttributeCommonInfo &CI, Expr *Min,
                             Expr *Max);

  /// addAMDGPUWavePersEUAttr - Adds an amdgpu_waves_per_eu attribute to a
  /// particular declaration.
  void addAMDGPUWavesPerEUAttr(Decl *D, const AttributeCommonInfo &CI,
                               Expr *Min, Expr *Max);

  /// Create an AMDGPUMaxNumWorkGroupsAttr attribute.
  AMDGPUMaxNumWorkGroupsAttr *
  CreateAMDGPUMaxNumWorkGroupsAttr(const AttributeCommonInfo &CI, Expr *XExpr,
                                   Expr *YExpr, Expr *ZExpr);

  /// addAMDGPUMaxNumWorkGroupsAttr - Adds an amdgpu_max_num_work_groups
  /// attribute to a particular declaration.
  void addAMDGPUMaxNumWorkGroupsAttr(Decl *D, const AttributeCommonInfo &CI,
                                     Expr *XExpr, Expr *YExpr, Expr *ZExpr);

  void handleAMDGPUWavesPerEUAttr(Decl *D, const ParsedAttr &AL);
  void handleAMDGPUNumSGPRAttr(Decl *D, const ParsedAttr &AL);
  void handleAMDGPUNumVGPRAttr(Decl *D, const ParsedAttr &AL);
  void handleAMDGPUMaxNumWorkGroupsAttr(Decl *D, const ParsedAttr &AL);
  void handleAMDGPUFlatWorkGroupSizeAttr(Decl *D, const ParsedAttr &AL);
};
} // namespace clang

#endif // LLVM_CLANG_SEMA_SEMAAMDGPU_H
