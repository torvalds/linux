//===------ SemaWasm.cpp ---- WebAssembly target-specific routines --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis functions specific to WebAssembly.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/SemaWasm.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Type.h"
#include "clang/Basic/AddressSpaces.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Basic/TargetBuiltins.h"
#include "clang/Sema/Attr.h"
#include "clang/Sema/Sema.h"

namespace clang {

SemaWasm::SemaWasm(Sema &S) : SemaBase(S) {}

/// Checks the argument at the given index is a WebAssembly table and if it
/// is, sets ElTy to the element type.
static bool CheckWasmBuiltinArgIsTable(Sema &S, CallExpr *E, unsigned ArgIndex,
                                       QualType &ElTy) {
  Expr *ArgExpr = E->getArg(ArgIndex);
  const auto *ATy = dyn_cast<ArrayType>(ArgExpr->getType());
  if (!ATy || !ATy->getElementType().isWebAssemblyReferenceType()) {
    return S.Diag(ArgExpr->getBeginLoc(),
                  diag::err_wasm_builtin_arg_must_be_table_type)
           << ArgIndex + 1 << ArgExpr->getSourceRange();
  }
  ElTy = ATy->getElementType();
  return false;
}

/// Checks the argument at the given index is an integer.
static bool CheckWasmBuiltinArgIsInteger(Sema &S, CallExpr *E,
                                         unsigned ArgIndex) {
  Expr *ArgExpr = E->getArg(ArgIndex);
  if (!ArgExpr->getType()->isIntegerType()) {
    return S.Diag(ArgExpr->getBeginLoc(),
                  diag::err_wasm_builtin_arg_must_be_integer_type)
           << ArgIndex + 1 << ArgExpr->getSourceRange();
  }
  return false;
}

bool SemaWasm::BuiltinWasmRefNullExtern(CallExpr *TheCall) {
  if (TheCall->getNumArgs() != 0)
    return true;

  TheCall->setType(getASTContext().getWebAssemblyExternrefType());

  return false;
}

bool SemaWasm::BuiltinWasmRefNullFunc(CallExpr *TheCall) {
  ASTContext &Context = getASTContext();
  if (TheCall->getNumArgs() != 0) {
    Diag(TheCall->getBeginLoc(), diag::err_typecheck_call_too_many_args)
        << 0 /*function call*/ << /*expected*/ 0 << TheCall->getNumArgs()
        << /*is non object*/ 0;
    return true;
  }

  // This custom type checking code ensures that the nodes are as expected
  // in order to later on generate the necessary builtin.
  QualType Pointee = Context.getFunctionType(Context.VoidTy, {}, {});
  QualType Type = Context.getPointerType(Pointee);
  Pointee = Context.getAddrSpaceQualType(Pointee, LangAS::wasm_funcref);
  Type = Context.getAttributedType(attr::WebAssemblyFuncref, Type,
                                   Context.getPointerType(Pointee));
  TheCall->setType(Type);

  return false;
}

/// Check that the first argument is a WebAssembly table, and the second
/// is an index to use as index into the table.
bool SemaWasm::BuiltinWasmTableGet(CallExpr *TheCall) {
  if (SemaRef.checkArgCount(TheCall, 2))
    return true;

  QualType ElTy;
  if (CheckWasmBuiltinArgIsTable(SemaRef, TheCall, 0, ElTy))
    return true;

  if (CheckWasmBuiltinArgIsInteger(SemaRef, TheCall, 1))
    return true;

  // If all is well, we set the type of TheCall to be the type of the
  // element of the table.
  // i.e. a table.get on an externref table has type externref,
  // or whatever the type of the table element is.
  TheCall->setType(ElTy);

  return false;
}

/// Check that the first argumnet is a WebAssembly table, the second is
/// an index to use as index into the table and the third is the reference
/// type to set into the table.
bool SemaWasm::BuiltinWasmTableSet(CallExpr *TheCall) {
  if (SemaRef.checkArgCount(TheCall, 3))
    return true;

  QualType ElTy;
  if (CheckWasmBuiltinArgIsTable(SemaRef, TheCall, 0, ElTy))
    return true;

  if (CheckWasmBuiltinArgIsInteger(SemaRef, TheCall, 1))
    return true;

  if (!getASTContext().hasSameType(ElTy, TheCall->getArg(2)->getType()))
    return true;

  return false;
}

/// Check that the argument is a WebAssembly table.
bool SemaWasm::BuiltinWasmTableSize(CallExpr *TheCall) {
  if (SemaRef.checkArgCount(TheCall, 1))
    return true;

  QualType ElTy;
  if (CheckWasmBuiltinArgIsTable(SemaRef, TheCall, 0, ElTy))
    return true;

  return false;
}

/// Check that the first argument is a WebAssembly table, the second is the
/// value to use for new elements (of a type matching the table type), the
/// third value is an integer.
bool SemaWasm::BuiltinWasmTableGrow(CallExpr *TheCall) {
  if (SemaRef.checkArgCount(TheCall, 3))
    return true;

  QualType ElTy;
  if (CheckWasmBuiltinArgIsTable(SemaRef, TheCall, 0, ElTy))
    return true;

  Expr *NewElemArg = TheCall->getArg(1);
  if (!getASTContext().hasSameType(ElTy, NewElemArg->getType())) {
    return Diag(NewElemArg->getBeginLoc(),
                diag::err_wasm_builtin_arg_must_match_table_element_type)
           << 2 << 1 << NewElemArg->getSourceRange();
  }

  if (CheckWasmBuiltinArgIsInteger(SemaRef, TheCall, 2))
    return true;

  return false;
}

/// Check that the first argument is a WebAssembly table, the second is an
/// integer, the third is the value to use to fill the table (of a type
/// matching the table type), and the fourth is an integer.
bool SemaWasm::BuiltinWasmTableFill(CallExpr *TheCall) {
  if (SemaRef.checkArgCount(TheCall, 4))
    return true;

  QualType ElTy;
  if (CheckWasmBuiltinArgIsTable(SemaRef, TheCall, 0, ElTy))
    return true;

  if (CheckWasmBuiltinArgIsInteger(SemaRef, TheCall, 1))
    return true;

  Expr *NewElemArg = TheCall->getArg(2);
  if (!getASTContext().hasSameType(ElTy, NewElemArg->getType())) {
    return Diag(NewElemArg->getBeginLoc(),
                diag::err_wasm_builtin_arg_must_match_table_element_type)
           << 3 << 1 << NewElemArg->getSourceRange();
  }

  if (CheckWasmBuiltinArgIsInteger(SemaRef, TheCall, 3))
    return true;

  return false;
}

/// Check that the first argument is a WebAssembly table, the second is also a
/// WebAssembly table (of the same element type), and the third to fifth
/// arguments are integers.
bool SemaWasm::BuiltinWasmTableCopy(CallExpr *TheCall) {
  if (SemaRef.checkArgCount(TheCall, 5))
    return true;

  QualType XElTy;
  if (CheckWasmBuiltinArgIsTable(SemaRef, TheCall, 0, XElTy))
    return true;

  QualType YElTy;
  if (CheckWasmBuiltinArgIsTable(SemaRef, TheCall, 1, YElTy))
    return true;

  Expr *TableYArg = TheCall->getArg(1);
  if (!getASTContext().hasSameType(XElTy, YElTy)) {
    return Diag(TableYArg->getBeginLoc(),
                diag::err_wasm_builtin_arg_must_match_table_element_type)
           << 2 << 1 << TableYArg->getSourceRange();
  }

  for (int I = 2; I <= 4; I++) {
    if (CheckWasmBuiltinArgIsInteger(SemaRef, TheCall, I))
      return true;
  }

  return false;
}

bool SemaWasm::CheckWebAssemblyBuiltinFunctionCall(const TargetInfo &TI,
                                                   unsigned BuiltinID,
                                                   CallExpr *TheCall) {
  switch (BuiltinID) {
  case WebAssembly::BI__builtin_wasm_ref_null_extern:
    return BuiltinWasmRefNullExtern(TheCall);
  case WebAssembly::BI__builtin_wasm_ref_null_func:
    return BuiltinWasmRefNullFunc(TheCall);
  case WebAssembly::BI__builtin_wasm_table_get:
    return BuiltinWasmTableGet(TheCall);
  case WebAssembly::BI__builtin_wasm_table_set:
    return BuiltinWasmTableSet(TheCall);
  case WebAssembly::BI__builtin_wasm_table_size:
    return BuiltinWasmTableSize(TheCall);
  case WebAssembly::BI__builtin_wasm_table_grow:
    return BuiltinWasmTableGrow(TheCall);
  case WebAssembly::BI__builtin_wasm_table_fill:
    return BuiltinWasmTableFill(TheCall);
  case WebAssembly::BI__builtin_wasm_table_copy:
    return BuiltinWasmTableCopy(TheCall);
  }

  return false;
}

WebAssemblyImportModuleAttr *
SemaWasm::mergeImportModuleAttr(Decl *D,
                                const WebAssemblyImportModuleAttr &AL) {
  auto *FD = cast<FunctionDecl>(D);

  if (const auto *ExistingAttr = FD->getAttr<WebAssemblyImportModuleAttr>()) {
    if (ExistingAttr->getImportModule() == AL.getImportModule())
      return nullptr;
    Diag(ExistingAttr->getLocation(), diag::warn_mismatched_import)
        << 0 << ExistingAttr->getImportModule() << AL.getImportModule();
    Diag(AL.getLoc(), diag::note_previous_attribute);
    return nullptr;
  }
  if (FD->hasBody()) {
    Diag(AL.getLoc(), diag::warn_import_on_definition) << 0;
    return nullptr;
  }
  return ::new (getASTContext())
      WebAssemblyImportModuleAttr(getASTContext(), AL, AL.getImportModule());
}

WebAssemblyImportNameAttr *
SemaWasm::mergeImportNameAttr(Decl *D, const WebAssemblyImportNameAttr &AL) {
  auto *FD = cast<FunctionDecl>(D);

  if (const auto *ExistingAttr = FD->getAttr<WebAssemblyImportNameAttr>()) {
    if (ExistingAttr->getImportName() == AL.getImportName())
      return nullptr;
    Diag(ExistingAttr->getLocation(), diag::warn_mismatched_import)
        << 1 << ExistingAttr->getImportName() << AL.getImportName();
    Diag(AL.getLoc(), diag::note_previous_attribute);
    return nullptr;
  }
  if (FD->hasBody()) {
    Diag(AL.getLoc(), diag::warn_import_on_definition) << 1;
    return nullptr;
  }
  return ::new (getASTContext())
      WebAssemblyImportNameAttr(getASTContext(), AL, AL.getImportName());
}

void SemaWasm::handleWebAssemblyImportModuleAttr(Decl *D,
                                                 const ParsedAttr &AL) {
  auto *FD = cast<FunctionDecl>(D);

  StringRef Str;
  SourceLocation ArgLoc;
  if (!SemaRef.checkStringLiteralArgumentAttr(AL, 0, Str, &ArgLoc))
    return;
  if (FD->hasBody()) {
    Diag(AL.getLoc(), diag::warn_import_on_definition) << 0;
    return;
  }

  FD->addAttr(::new (getASTContext())
                  WebAssemblyImportModuleAttr(getASTContext(), AL, Str));
}

void SemaWasm::handleWebAssemblyImportNameAttr(Decl *D, const ParsedAttr &AL) {
  auto *FD = cast<FunctionDecl>(D);

  StringRef Str;
  SourceLocation ArgLoc;
  if (!SemaRef.checkStringLiteralArgumentAttr(AL, 0, Str, &ArgLoc))
    return;
  if (FD->hasBody()) {
    Diag(AL.getLoc(), diag::warn_import_on_definition) << 1;
    return;
  }

  FD->addAttr(::new (getASTContext())
                  WebAssemblyImportNameAttr(getASTContext(), AL, Str));
}

void SemaWasm::handleWebAssemblyExportNameAttr(Decl *D, const ParsedAttr &AL) {
  ASTContext &Context = getASTContext();
  if (!isFuncOrMethodForAttrSubject(D)) {
    Diag(D->getLocation(), diag::warn_attribute_wrong_decl_type)
        << AL << AL.isRegularKeywordAttribute() << ExpectedFunction;
    return;
  }

  auto *FD = cast<FunctionDecl>(D);
  if (FD->isThisDeclarationADefinition()) {
    Diag(D->getLocation(), diag::err_alias_is_definition) << FD << 0;
    return;
  }

  StringRef Str;
  SourceLocation ArgLoc;
  if (!SemaRef.checkStringLiteralArgumentAttr(AL, 0, Str, &ArgLoc))
    return;

  D->addAttr(::new (Context) WebAssemblyExportNameAttr(Context, AL, Str));
  D->addAttr(UsedAttr::CreateImplicit(Context));
}

} // namespace clang
