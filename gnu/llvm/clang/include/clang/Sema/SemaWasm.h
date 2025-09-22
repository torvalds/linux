//===----- SemaWasm.h ------ Wasm target-specific routines ----*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file declares semantic analysis functions specific to Wasm.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_SEMAWASM_H
#define LLVM_CLANG_SEMA_SEMAWASM_H

#include "clang/AST/Attr.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/Expr.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Sema/ParsedAttr.h"
#include "clang/Sema/SemaBase.h"

namespace clang {
class SemaWasm : public SemaBase {
public:
  SemaWasm(Sema &S);

  bool CheckWebAssemblyBuiltinFunctionCall(const TargetInfo &TI,
                                           unsigned BuiltinID,
                                           CallExpr *TheCall);

  bool BuiltinWasmRefNullExtern(CallExpr *TheCall);
  bool BuiltinWasmRefNullFunc(CallExpr *TheCall);
  bool BuiltinWasmTableGet(CallExpr *TheCall);
  bool BuiltinWasmTableSet(CallExpr *TheCall);
  bool BuiltinWasmTableSize(CallExpr *TheCall);
  bool BuiltinWasmTableGrow(CallExpr *TheCall);
  bool BuiltinWasmTableFill(CallExpr *TheCall);
  bool BuiltinWasmTableCopy(CallExpr *TheCall);

  WebAssemblyImportNameAttr *
  mergeImportNameAttr(Decl *D, const WebAssemblyImportNameAttr &AL);
  WebAssemblyImportModuleAttr *
  mergeImportModuleAttr(Decl *D, const WebAssemblyImportModuleAttr &AL);

  void handleWebAssemblyExportNameAttr(Decl *D, const ParsedAttr &AL);
  void handleWebAssemblyImportModuleAttr(Decl *D, const ParsedAttr &AL);
  void handleWebAssemblyImportNameAttr(Decl *D, const ParsedAttr &AL);
};
} // namespace clang

#endif // LLVM_CLANG_SEMA_SEMAWASM_H
