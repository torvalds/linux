//===------ SemaBPF.cpp ---------- BPF target-specific routines -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis functions specific to BPF.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/SemaBPF.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Type.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Basic/TargetBuiltins.h"
#include "clang/Sema/ParsedAttr.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/APSInt.h"
#include <optional>

namespace clang {

SemaBPF::SemaBPF(Sema &S) : SemaBase(S) {}

static bool isValidPreserveFieldInfoArg(Expr *Arg) {
  if (Arg->getType()->getAsPlaceholderType())
    return false;

  // The first argument needs to be a record field access.
  // If it is an array element access, we delay decision
  // to BPF backend to check whether the access is a
  // field access or not.
  return (Arg->IgnoreParens()->getObjectKind() == OK_BitField ||
          isa<MemberExpr>(Arg->IgnoreParens()) ||
          isa<ArraySubscriptExpr>(Arg->IgnoreParens()));
}

static bool isValidPreserveTypeInfoArg(Expr *Arg) {
  QualType ArgType = Arg->getType();
  if (ArgType->getAsPlaceholderType())
    return false;

  // for TYPE_EXISTENCE/TYPE_MATCH/TYPE_SIZEOF reloc type
  // format:
  //   1. __builtin_preserve_type_info(*(<type> *)0, flag);
  //   2. <type> var;
  //      __builtin_preserve_type_info(var, flag);
  if (!isa<DeclRefExpr>(Arg->IgnoreParens()) &&
      !isa<UnaryOperator>(Arg->IgnoreParens()))
    return false;

  // Typedef type.
  if (ArgType->getAs<TypedefType>())
    return true;

  // Record type or Enum type.
  const Type *Ty = ArgType->getUnqualifiedDesugaredType();
  if (const auto *RT = Ty->getAs<RecordType>()) {
    if (!RT->getDecl()->getDeclName().isEmpty())
      return true;
  } else if (const auto *ET = Ty->getAs<EnumType>()) {
    if (!ET->getDecl()->getDeclName().isEmpty())
      return true;
  }

  return false;
}

static bool isValidPreserveEnumValueArg(Expr *Arg) {
  QualType ArgType = Arg->getType();
  if (ArgType->getAsPlaceholderType())
    return false;

  // for ENUM_VALUE_EXISTENCE/ENUM_VALUE reloc type
  // format:
  //   __builtin_preserve_enum_value(*(<enum_type> *)<enum_value>,
  //                                 flag);
  const auto *UO = dyn_cast<UnaryOperator>(Arg->IgnoreParens());
  if (!UO)
    return false;

  const auto *CE = dyn_cast<CStyleCastExpr>(UO->getSubExpr());
  if (!CE)
    return false;
  if (CE->getCastKind() != CK_IntegralToPointer &&
      CE->getCastKind() != CK_NullToPointer)
    return false;

  // The integer must be from an EnumConstantDecl.
  const auto *DR = dyn_cast<DeclRefExpr>(CE->getSubExpr());
  if (!DR)
    return false;

  const EnumConstantDecl *Enumerator =
      dyn_cast<EnumConstantDecl>(DR->getDecl());
  if (!Enumerator)
    return false;

  // The type must be EnumType.
  const Type *Ty = ArgType->getUnqualifiedDesugaredType();
  const auto *ET = Ty->getAs<EnumType>();
  if (!ET)
    return false;

  // The enum value must be supported.
  return llvm::is_contained(ET->getDecl()->enumerators(), Enumerator);
}

bool SemaBPF::CheckBPFBuiltinFunctionCall(unsigned BuiltinID,
                                          CallExpr *TheCall) {
  assert((BuiltinID == BPF::BI__builtin_preserve_field_info ||
          BuiltinID == BPF::BI__builtin_btf_type_id ||
          BuiltinID == BPF::BI__builtin_preserve_type_info ||
          BuiltinID == BPF::BI__builtin_preserve_enum_value) &&
         "unexpected BPF builtin");
  ASTContext &Context = getASTContext();
  if (SemaRef.checkArgCount(TheCall, 2))
    return true;

  // The second argument needs to be a constant int
  Expr *Arg = TheCall->getArg(1);
  std::optional<llvm::APSInt> Value = Arg->getIntegerConstantExpr(Context);
  diag::kind kind;
  if (!Value) {
    if (BuiltinID == BPF::BI__builtin_preserve_field_info)
      kind = diag::err_preserve_field_info_not_const;
    else if (BuiltinID == BPF::BI__builtin_btf_type_id)
      kind = diag::err_btf_type_id_not_const;
    else if (BuiltinID == BPF::BI__builtin_preserve_type_info)
      kind = diag::err_preserve_type_info_not_const;
    else
      kind = diag::err_preserve_enum_value_not_const;
    Diag(Arg->getBeginLoc(), kind) << 2 << Arg->getSourceRange();
    return true;
  }

  // The first argument
  Arg = TheCall->getArg(0);
  bool InvalidArg = false;
  bool ReturnUnsignedInt = true;
  if (BuiltinID == BPF::BI__builtin_preserve_field_info) {
    if (!isValidPreserveFieldInfoArg(Arg)) {
      InvalidArg = true;
      kind = diag::err_preserve_field_info_not_field;
    }
  } else if (BuiltinID == BPF::BI__builtin_preserve_type_info) {
    if (!isValidPreserveTypeInfoArg(Arg)) {
      InvalidArg = true;
      kind = diag::err_preserve_type_info_invalid;
    }
  } else if (BuiltinID == BPF::BI__builtin_preserve_enum_value) {
    if (!isValidPreserveEnumValueArg(Arg)) {
      InvalidArg = true;
      kind = diag::err_preserve_enum_value_invalid;
    }
    ReturnUnsignedInt = false;
  } else if (BuiltinID == BPF::BI__builtin_btf_type_id) {
    ReturnUnsignedInt = false;
  }

  if (InvalidArg) {
    Diag(Arg->getBeginLoc(), kind) << 1 << Arg->getSourceRange();
    return true;
  }

  if (ReturnUnsignedInt)
    TheCall->setType(Context.UnsignedIntTy);
  else
    TheCall->setType(Context.UnsignedLongTy);
  return false;
}

void SemaBPF::handlePreserveAIRecord(RecordDecl *RD) {
  // Add preserve_access_index attribute to all fields and inner records.
  for (auto *D : RD->decls()) {
    if (D->hasAttr<BPFPreserveAccessIndexAttr>())
      continue;

    D->addAttr(BPFPreserveAccessIndexAttr::CreateImplicit(getASTContext()));
    if (auto *Rec = dyn_cast<RecordDecl>(D))
      handlePreserveAIRecord(Rec);
  }
}

void SemaBPF::handlePreserveAccessIndexAttr(Decl *D, const ParsedAttr &AL) {
  auto *Rec = cast<RecordDecl>(D);
  handlePreserveAIRecord(Rec);
  Rec->addAttr(::new (getASTContext())
                   BPFPreserveAccessIndexAttr(getASTContext(), AL));
}

} // namespace clang
