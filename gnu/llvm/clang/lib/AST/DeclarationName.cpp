//===- DeclarationName.cpp - Declaration names implementation -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the DeclarationName and DeclarationNameTable
// classes.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/DeclarationName.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/OpenMPClause.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/AST/TypeOrdering.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>

using namespace clang;

static int compareInt(unsigned A, unsigned B) {
  return (A < B ? -1 : (A > B ? 1 : 0));
}

int DeclarationName::compare(DeclarationName LHS, DeclarationName RHS) {
  if (LHS.getNameKind() != RHS.getNameKind())
    return (LHS.getNameKind() < RHS.getNameKind() ? -1 : 1);

  switch (LHS.getNameKind()) {
  case DeclarationName::Identifier: {
    IdentifierInfo *LII = LHS.castAsIdentifierInfo();
    IdentifierInfo *RII = RHS.castAsIdentifierInfo();
    if (!LII)
      return RII ? -1 : 0;
    if (!RII)
      return 1;

    return LII->getName().compare(RII->getName());
  }

  case DeclarationName::ObjCZeroArgSelector:
  case DeclarationName::ObjCOneArgSelector:
  case DeclarationName::ObjCMultiArgSelector: {
    Selector LHSSelector = LHS.getObjCSelector();
    Selector RHSSelector = RHS.getObjCSelector();
    // getNumArgs for ZeroArgSelector returns 0, but we still need to compare.
    if (LHS.getNameKind() == DeclarationName::ObjCZeroArgSelector &&
        RHS.getNameKind() == DeclarationName::ObjCZeroArgSelector) {
      return LHSSelector.getAsIdentifierInfo()->getName().compare(
          RHSSelector.getAsIdentifierInfo()->getName());
    }
    unsigned LN = LHSSelector.getNumArgs(), RN = RHSSelector.getNumArgs();
    for (unsigned I = 0, N = std::min(LN, RN); I != N; ++I) {
      if (int Compare = LHSSelector.getNameForSlot(I).compare(
              RHSSelector.getNameForSlot(I)))
        return Compare;
    }

    return compareInt(LN, RN);
  }

  case DeclarationName::CXXConstructorName:
  case DeclarationName::CXXDestructorName:
  case DeclarationName::CXXConversionFunctionName:
    if (QualTypeOrdering()(LHS.getCXXNameType(), RHS.getCXXNameType()))
      return -1;
    if (QualTypeOrdering()(RHS.getCXXNameType(), LHS.getCXXNameType()))
      return 1;
    return 0;

  case DeclarationName::CXXDeductionGuideName:
    // We never want to compare deduction guide names for templates from
    // different scopes, so just compare the template-name.
    return compare(LHS.getCXXDeductionGuideTemplate()->getDeclName(),
                   RHS.getCXXDeductionGuideTemplate()->getDeclName());

  case DeclarationName::CXXOperatorName:
    return compareInt(LHS.getCXXOverloadedOperator(),
                      RHS.getCXXOverloadedOperator());

  case DeclarationName::CXXLiteralOperatorName:
    return LHS.getCXXLiteralIdentifier()->getName().compare(
        RHS.getCXXLiteralIdentifier()->getName());

  case DeclarationName::CXXUsingDirective:
    return 0;
  }

  llvm_unreachable("Invalid DeclarationName Kind!");
}

static void printCXXConstructorDestructorName(QualType ClassType,
                                              raw_ostream &OS,
                                              PrintingPolicy Policy) {
  // We know we're printing C++ here. Ensure we print types properly.
  Policy.adjustForCPlusPlus();

  if (const RecordType *ClassRec = ClassType->getAs<RecordType>()) {
    ClassRec->getDecl()->printName(OS, Policy);
    return;
  }
  if (Policy.SuppressTemplateArgsInCXXConstructors) {
    if (auto *InjTy = ClassType->getAs<InjectedClassNameType>()) {
      InjTy->getDecl()->printName(OS, Policy);
      return;
    }
  }
  ClassType.print(OS, Policy);
}

void DeclarationName::print(raw_ostream &OS,
                            const PrintingPolicy &Policy) const {
  switch (getNameKind()) {
  case DeclarationName::Identifier:
    if (const IdentifierInfo *II = getAsIdentifierInfo()) {
      StringRef Name = II->getName();
      // If this is a mangled OpenMP variant name we strip off the mangling for
      // printing. It should not be visible to the user at all.
      if (II->isMangledOpenMPVariantName()) {
        std::pair<StringRef, StringRef> NameContextPair =
            Name.split(getOpenMPVariantManglingSeparatorStr());
        OS << NameContextPair.first << "["
           << OMPTraitInfo(NameContextPair.second) << "]";
      } else {
        OS << Name;
      }
    }
    return;

  case DeclarationName::ObjCZeroArgSelector:
  case DeclarationName::ObjCOneArgSelector:
  case DeclarationName::ObjCMultiArgSelector:
    getObjCSelector().print(OS);
    return;

  case DeclarationName::CXXConstructorName:
    return printCXXConstructorDestructorName(getCXXNameType(), OS, Policy);

  case DeclarationName::CXXDestructorName:
    OS << '~';
    return printCXXConstructorDestructorName(getCXXNameType(), OS, Policy);

  case DeclarationName::CXXDeductionGuideName:
    OS << "<deduction guide for ";
    getCXXDeductionGuideTemplate()->getDeclName().print(OS, Policy);
    OS << '>';
    return;

  case DeclarationName::CXXOperatorName: {
    const char *OpName = getOperatorSpelling(getCXXOverloadedOperator());
    assert(OpName && "not an overloaded operator");

    OS << "operator";
    if (OpName[0] >= 'a' && OpName[0] <= 'z')
      OS << ' ';
    OS << OpName;
    return;
  }

  case DeclarationName::CXXLiteralOperatorName:
    OS << "operator\"\"" << getCXXLiteralIdentifier()->getName();
    return;

  case DeclarationName::CXXConversionFunctionName: {
    OS << "operator ";
    QualType Type = getCXXNameType();
    if (const RecordType *Rec = Type->getAs<RecordType>()) {
      OS << *Rec->getDecl();
      return;
    }
    // We know we're printing C++ here, ensure we print 'bool' properly.
    PrintingPolicy CXXPolicy = Policy;
    CXXPolicy.adjustForCPlusPlus();
    Type.print(OS, CXXPolicy);
    return;
  }
  case DeclarationName::CXXUsingDirective:
    OS << "<using-directive>";
    return;
  }

  llvm_unreachable("Unexpected declaration name kind");
}

namespace clang {

raw_ostream &operator<<(raw_ostream &OS, DeclarationName N) {
  LangOptions LO;
  N.print(OS, PrintingPolicy(LO));
  return OS;
}

} // namespace clang

bool DeclarationName::isDependentName() const {
  QualType T = getCXXNameType();
  if (!T.isNull() && T->isDependentType())
    return true;

  // A class-scope deduction guide in a dependent context has a dependent name.
  auto *TD = getCXXDeductionGuideTemplate();
  if (TD && TD->getDeclContext()->isDependentContext())
    return true;

  return false;
}

std::string DeclarationName::getAsString() const {
  std::string Result;
  llvm::raw_string_ostream OS(Result);
  OS << *this;
  return Result;
}

void *DeclarationName::getFETokenInfoSlow() const {
  switch (getNameKind()) {
  case Identifier:
    llvm_unreachable("case Identifier already handled by getFETokenInfo!");
  case CXXConstructorName:
  case CXXDestructorName:
  case CXXConversionFunctionName:
    return castAsCXXSpecialNameExtra()->FETokenInfo;
  case CXXOperatorName:
    return castAsCXXOperatorIdName()->FETokenInfo;
  case CXXDeductionGuideName:
    return castAsCXXDeductionGuideNameExtra()->FETokenInfo;
  case CXXLiteralOperatorName:
    return castAsCXXLiteralOperatorIdName()->FETokenInfo;
  default:
    llvm_unreachable("DeclarationName has no FETokenInfo!");
  }
}

void DeclarationName::setFETokenInfoSlow(void *T) {
  switch (getNameKind()) {
  case Identifier:
    llvm_unreachable("case Identifier already handled by setFETokenInfo!");
  case CXXConstructorName:
  case CXXDestructorName:
  case CXXConversionFunctionName:
    castAsCXXSpecialNameExtra()->FETokenInfo = T;
    break;
  case CXXOperatorName:
    castAsCXXOperatorIdName()->FETokenInfo = T;
    break;
  case CXXDeductionGuideName:
    castAsCXXDeductionGuideNameExtra()->FETokenInfo = T;
    break;
  case CXXLiteralOperatorName:
    castAsCXXLiteralOperatorIdName()->FETokenInfo = T;
    break;
  default:
    llvm_unreachable("DeclarationName has no FETokenInfo!");
  }
}

LLVM_DUMP_METHOD void DeclarationName::dump() const {
  llvm::errs() << *this << '\n';
}

DeclarationNameTable::DeclarationNameTable(const ASTContext &C) : Ctx(C) {
  // Initialize the overloaded operator names.
  for (unsigned Op = 0; Op < NUM_OVERLOADED_OPERATORS; ++Op)
    CXXOperatorNames[Op].Kind = static_cast<OverloadedOperatorKind>(Op);
}

DeclarationName
DeclarationNameTable::getCXXDeductionGuideName(TemplateDecl *Template) {
  Template = cast<TemplateDecl>(Template->getCanonicalDecl());

  llvm::FoldingSetNodeID ID;
  ID.AddPointer(Template);

  void *InsertPos = nullptr;
  if (auto *Name = CXXDeductionGuideNames.FindNodeOrInsertPos(ID, InsertPos))
    return DeclarationName(Name);

  auto *Name = new (Ctx) detail::CXXDeductionGuideNameExtra(Template);
  CXXDeductionGuideNames.InsertNode(Name, InsertPos);
  return DeclarationName(Name);
}

DeclarationName DeclarationNameTable::getCXXConstructorName(CanQualType Ty) {
  // The type of constructors is unqualified.
  Ty = Ty.getUnqualifiedType();
  // Do we already have this C++ constructor name ?
  llvm::FoldingSetNodeID ID;
  ID.AddPointer(Ty.getAsOpaquePtr());
  void *InsertPos = nullptr;
  if (auto *Name = CXXConstructorNames.FindNodeOrInsertPos(ID, InsertPos))
    return {Name, DeclarationName::StoredCXXConstructorName};

  // We have to create it.
  auto *SpecialName = new (Ctx) detail::CXXSpecialNameExtra(Ty);
  CXXConstructorNames.InsertNode(SpecialName, InsertPos);
  return {SpecialName, DeclarationName::StoredCXXConstructorName};
}

DeclarationName DeclarationNameTable::getCXXDestructorName(CanQualType Ty) {
  // The type of destructors is unqualified.
  Ty = Ty.getUnqualifiedType();
  // Do we already have this C++ destructor name ?
  llvm::FoldingSetNodeID ID;
  ID.AddPointer(Ty.getAsOpaquePtr());
  void *InsertPos = nullptr;
  if (auto *Name = CXXDestructorNames.FindNodeOrInsertPos(ID, InsertPos))
    return {Name, DeclarationName::StoredCXXDestructorName};

  // We have to create it.
  auto *SpecialName = new (Ctx) detail::CXXSpecialNameExtra(Ty);
  CXXDestructorNames.InsertNode(SpecialName, InsertPos);
  return {SpecialName, DeclarationName::StoredCXXDestructorName};
}

DeclarationName
DeclarationNameTable::getCXXConversionFunctionName(CanQualType Ty) {
  // Do we already have this C++ conversion function name ?
  llvm::FoldingSetNodeID ID;
  ID.AddPointer(Ty.getAsOpaquePtr());
  void *InsertPos = nullptr;
  if (auto *Name =
          CXXConversionFunctionNames.FindNodeOrInsertPos(ID, InsertPos))
    return {Name, DeclarationName::StoredCXXConversionFunctionName};

  // We have to create it.
  auto *SpecialName = new (Ctx) detail::CXXSpecialNameExtra(Ty);
  CXXConversionFunctionNames.InsertNode(SpecialName, InsertPos);
  return {SpecialName, DeclarationName::StoredCXXConversionFunctionName};
}

DeclarationName
DeclarationNameTable::getCXXSpecialName(DeclarationName::NameKind Kind,
                                        CanQualType Ty) {
  switch (Kind) {
  case DeclarationName::CXXConstructorName:
    return getCXXConstructorName(Ty);
  case DeclarationName::CXXDestructorName:
    return getCXXDestructorName(Ty);
  case DeclarationName::CXXConversionFunctionName:
    return getCXXConversionFunctionName(Ty);
  default:
    llvm_unreachable("Invalid kind in getCXXSpecialName!");
  }
}

DeclarationName
DeclarationNameTable::getCXXLiteralOperatorName(const IdentifierInfo *II) {
  llvm::FoldingSetNodeID ID;
  ID.AddPointer(II);

  void *InsertPos = nullptr;
  if (auto *Name = CXXLiteralOperatorNames.FindNodeOrInsertPos(ID, InsertPos))
    return DeclarationName(Name);

  auto *LiteralName = new (Ctx) detail::CXXLiteralOperatorIdName(II);
  CXXLiteralOperatorNames.InsertNode(LiteralName, InsertPos);
  return DeclarationName(LiteralName);
}

DeclarationNameLoc::DeclarationNameLoc(DeclarationName Name) {
  switch (Name.getNameKind()) {
  case DeclarationName::Identifier:
  case DeclarationName::CXXDeductionGuideName:
    break;
  case DeclarationName::CXXConstructorName:
  case DeclarationName::CXXDestructorName:
  case DeclarationName::CXXConversionFunctionName:
    setNamedTypeLoc(nullptr);
    break;
  case DeclarationName::CXXOperatorName:
    setCXXOperatorNameRange(SourceRange());
    break;
  case DeclarationName::CXXLiteralOperatorName:
    setCXXLiteralOperatorNameLoc(SourceLocation());
    break;
  case DeclarationName::ObjCZeroArgSelector:
  case DeclarationName::ObjCOneArgSelector:
  case DeclarationName::ObjCMultiArgSelector:
    // FIXME: ?
    break;
  case DeclarationName::CXXUsingDirective:
    break;
  }
}

bool DeclarationNameInfo::containsUnexpandedParameterPack() const {
  switch (Name.getNameKind()) {
  case DeclarationName::Identifier:
  case DeclarationName::ObjCZeroArgSelector:
  case DeclarationName::ObjCOneArgSelector:
  case DeclarationName::ObjCMultiArgSelector:
  case DeclarationName::CXXOperatorName:
  case DeclarationName::CXXLiteralOperatorName:
  case DeclarationName::CXXUsingDirective:
  case DeclarationName::CXXDeductionGuideName:
    return false;

  case DeclarationName::CXXConstructorName:
  case DeclarationName::CXXDestructorName:
  case DeclarationName::CXXConversionFunctionName:
    if (TypeSourceInfo *TInfo = LocInfo.getNamedTypeInfo())
      return TInfo->getType()->containsUnexpandedParameterPack();

    return Name.getCXXNameType()->containsUnexpandedParameterPack();
  }
  llvm_unreachable("All name kinds handled.");
}

bool DeclarationNameInfo::isInstantiationDependent() const {
  switch (Name.getNameKind()) {
  case DeclarationName::Identifier:
  case DeclarationName::ObjCZeroArgSelector:
  case DeclarationName::ObjCOneArgSelector:
  case DeclarationName::ObjCMultiArgSelector:
  case DeclarationName::CXXOperatorName:
  case DeclarationName::CXXLiteralOperatorName:
  case DeclarationName::CXXUsingDirective:
  case DeclarationName::CXXDeductionGuideName:
    return false;

  case DeclarationName::CXXConstructorName:
  case DeclarationName::CXXDestructorName:
  case DeclarationName::CXXConversionFunctionName:
    if (TypeSourceInfo *TInfo = LocInfo.getNamedTypeInfo())
      return TInfo->getType()->isInstantiationDependentType();

    return Name.getCXXNameType()->isInstantiationDependentType();
  }
  llvm_unreachable("All name kinds handled.");
}

std::string DeclarationNameInfo::getAsString() const {
  std::string Result;
  llvm::raw_string_ostream OS(Result);
  OS << *this;
  return Result;
}

raw_ostream &clang::operator<<(raw_ostream &OS, DeclarationNameInfo DNInfo) {
  LangOptions LO;
  DNInfo.printName(OS, PrintingPolicy(LangOptions()));
  return OS;
}

void DeclarationNameInfo::printName(raw_ostream &OS, PrintingPolicy Policy) const {
  switch (Name.getNameKind()) {
  case DeclarationName::Identifier:
  case DeclarationName::ObjCZeroArgSelector:
  case DeclarationName::ObjCOneArgSelector:
  case DeclarationName::ObjCMultiArgSelector:
  case DeclarationName::CXXOperatorName:
  case DeclarationName::CXXLiteralOperatorName:
  case DeclarationName::CXXUsingDirective:
  case DeclarationName::CXXDeductionGuideName:
    Name.print(OS, Policy);
    return;

  case DeclarationName::CXXConstructorName:
  case DeclarationName::CXXDestructorName:
  case DeclarationName::CXXConversionFunctionName:
    if (TypeSourceInfo *TInfo = LocInfo.getNamedTypeInfo()) {
      if (Name.getNameKind() == DeclarationName::CXXDestructorName)
        OS << '~';
      else if (Name.getNameKind() == DeclarationName::CXXConversionFunctionName)
        OS << "operator ";
      LangOptions LO;
      Policy.adjustForCPlusPlus();
      Policy.SuppressScope = true;
      OS << TInfo->getType().getAsString(Policy);
    } else
      Name.print(OS, Policy);
    return;
  }
  llvm_unreachable("Unexpected declaration name kind");
}

SourceLocation DeclarationNameInfo::getEndLocPrivate() const {
  switch (Name.getNameKind()) {
  case DeclarationName::Identifier:
  case DeclarationName::CXXDeductionGuideName:
    return NameLoc;

  case DeclarationName::CXXOperatorName:
    return LocInfo.getCXXOperatorNameEndLoc();

  case DeclarationName::CXXLiteralOperatorName:
    return LocInfo.getCXXLiteralOperatorNameLoc();

  case DeclarationName::CXXConstructorName:
  case DeclarationName::CXXDestructorName:
  case DeclarationName::CXXConversionFunctionName:
    if (TypeSourceInfo *TInfo = LocInfo.getNamedTypeInfo())
      return TInfo->getTypeLoc().getEndLoc();
    else
      return NameLoc;

    // DNInfo work in progress: FIXME.
  case DeclarationName::ObjCZeroArgSelector:
  case DeclarationName::ObjCOneArgSelector:
  case DeclarationName::ObjCMultiArgSelector:
  case DeclarationName::CXXUsingDirective:
    return NameLoc;
  }
  llvm_unreachable("Unexpected declaration name kind");
}
