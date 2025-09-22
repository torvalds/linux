//===- TemplateBase.cpp - Common template AST class implementation --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements common classes used throughout C++ template
// representations.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/TemplateBase.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/DependenceFlags.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/TemplateName.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

using namespace clang;

/// Print a template integral argument value.
///
/// \param TemplArg the TemplateArgument instance to print.
///
/// \param Out the raw_ostream instance to use for printing.
///
/// \param Policy the printing policy for EnumConstantDecl printing.
///
/// \param IncludeType If set, ensure that the type of the expression printed
/// matches the type of the template argument.
static void printIntegral(const TemplateArgument &TemplArg, raw_ostream &Out,
                          const PrintingPolicy &Policy, bool IncludeType) {
  const Type *T = TemplArg.getIntegralType().getTypePtr();
  const llvm::APSInt &Val = TemplArg.getAsIntegral();

  if (Policy.UseEnumerators) {
    if (const EnumType *ET = T->getAs<EnumType>()) {
      for (const EnumConstantDecl *ECD : ET->getDecl()->enumerators()) {
        // In Sema::CheckTemplateArugment, enum template arguments value are
        // extended to the size of the integer underlying the enum type.  This
        // may create a size difference between the enum value and template
        // argument value, requiring isSameValue here instead of operator==.
        if (llvm::APSInt::isSameValue(ECD->getInitVal(), Val)) {
          ECD->printQualifiedName(Out, Policy);
          return;
        }
      }
    }
  }

  if (Policy.MSVCFormatting)
    IncludeType = false;

  if (T->isBooleanType()) {
    if (!Policy.MSVCFormatting)
      Out << (Val.getBoolValue() ? "true" : "false");
    else
      Out << Val;
  } else if (T->isCharType()) {
    if (IncludeType) {
      if (T->isSpecificBuiltinType(BuiltinType::SChar))
        Out << "(signed char)";
      else if (T->isSpecificBuiltinType(BuiltinType::UChar))
        Out << "(unsigned char)";
    }
    CharacterLiteral::print(Val.getZExtValue(), CharacterLiteralKind::Ascii,
                            Out);
  } else if (T->isAnyCharacterType() && !Policy.MSVCFormatting) {
    CharacterLiteralKind Kind;
    if (T->isWideCharType())
      Kind = CharacterLiteralKind::Wide;
    else if (T->isChar8Type())
      Kind = CharacterLiteralKind::UTF8;
    else if (T->isChar16Type())
      Kind = CharacterLiteralKind::UTF16;
    else if (T->isChar32Type())
      Kind = CharacterLiteralKind::UTF32;
    else
      Kind = CharacterLiteralKind::Ascii;
    CharacterLiteral::print(Val.getExtValue(), Kind, Out);
  } else if (IncludeType) {
    if (const auto *BT = T->getAs<BuiltinType>()) {
      switch (BT->getKind()) {
      case BuiltinType::ULongLong:
        Out << Val << "ULL";
        break;
      case BuiltinType::LongLong:
        Out << Val << "LL";
        break;
      case BuiltinType::ULong:
        Out << Val << "UL";
        break;
      case BuiltinType::Long:
        Out << Val << "L";
        break;
      case BuiltinType::UInt:
        Out << Val << "U";
        break;
      case BuiltinType::Int:
        Out << Val;
        break;
      default:
        Out << "(" << T->getCanonicalTypeInternal().getAsString(Policy) << ")"
            << Val;
        break;
      }
    } else
      Out << "(" << T->getCanonicalTypeInternal().getAsString(Policy) << ")"
          << Val;
  } else
    Out << Val;
}

static unsigned getArrayDepth(QualType type) {
  unsigned count = 0;
  while (const auto *arrayType = type->getAsArrayTypeUnsafe()) {
    count++;
    type = arrayType->getElementType();
  }
  return count;
}

static bool needsAmpersandOnTemplateArg(QualType paramType, QualType argType) {
  // Generally, if the parameter type is a pointer, we must be taking the
  // address of something and need a &.  However, if the argument is an array,
  // this could be implicit via array-to-pointer decay.
  if (!paramType->isPointerType())
    return paramType->isMemberPointerType();
  if (argType->isArrayType())
    return getArrayDepth(argType) == getArrayDepth(paramType->getPointeeType());
  return true;
}

//===----------------------------------------------------------------------===//
// TemplateArgument Implementation
//===----------------------------------------------------------------------===//

void TemplateArgument::initFromType(QualType T, bool IsNullPtr,
                                    bool IsDefaulted) {
  TypeOrValue.Kind = IsNullPtr ? NullPtr : Type;
  TypeOrValue.IsDefaulted = IsDefaulted;
  TypeOrValue.V = reinterpret_cast<uintptr_t>(T.getAsOpaquePtr());
}

void TemplateArgument::initFromDeclaration(ValueDecl *D, QualType QT,
                                           bool IsDefaulted) {
  assert(D && "Expected decl");
  DeclArg.Kind = Declaration;
  DeclArg.IsDefaulted = IsDefaulted;
  DeclArg.QT = QT.getAsOpaquePtr();
  DeclArg.D = D;
}

void TemplateArgument::initFromIntegral(const ASTContext &Ctx,
                                        const llvm::APSInt &Value,
                                        QualType Type, bool IsDefaulted) {
  Integer.Kind = Integral;
  Integer.IsDefaulted = IsDefaulted;
  // Copy the APSInt value into our decomposed form.
  Integer.BitWidth = Value.getBitWidth();
  Integer.IsUnsigned = Value.isUnsigned();
  // If the value is large, we have to get additional memory from the ASTContext
  unsigned NumWords = Value.getNumWords();
  if (NumWords > 1) {
    void *Mem = Ctx.Allocate(NumWords * sizeof(uint64_t));
    std::memcpy(Mem, Value.getRawData(), NumWords * sizeof(uint64_t));
    Integer.pVal = static_cast<uint64_t *>(Mem);
  } else {
    Integer.VAL = Value.getZExtValue();
  }

  Integer.Type = Type.getAsOpaquePtr();
}

void TemplateArgument::initFromStructural(const ASTContext &Ctx, QualType Type,
                                          const APValue &V, bool IsDefaulted) {
  Value.Kind = StructuralValue;
  Value.IsDefaulted = IsDefaulted;
  Value.Value = new (Ctx) APValue(V);
  Ctx.addDestruction(Value.Value);
  Value.Type = Type.getAsOpaquePtr();
}

TemplateArgument::TemplateArgument(const ASTContext &Ctx,
                                   const llvm::APSInt &Value, QualType Type,
                                   bool IsDefaulted) {
  initFromIntegral(Ctx, Value, Type, IsDefaulted);
}

static const ValueDecl *getAsSimpleValueDeclRef(const ASTContext &Ctx,
                                                QualType T, const APValue &V) {
  // Pointers to members are relatively easy.
  if (V.isMemberPointer() && V.getMemberPointerPath().empty())
    return V.getMemberPointerDecl();

  // We model class non-type template parameters as their template parameter
  // object declaration.
  if (V.isStruct() || V.isUnion()) {
    // Dependent types are not supposed to be described as
    // TemplateParamObjectDecls.
    if (T->isDependentType() || T->isInstantiationDependentType())
      return nullptr;
    return Ctx.getTemplateParamObjectDecl(T, V);
  }

  // Pointers and references with an empty path use the special 'Declaration'
  // representation.
  if (V.isLValue() && V.hasLValuePath() && V.getLValuePath().empty() &&
      !V.isLValueOnePastTheEnd())
    return V.getLValueBase().dyn_cast<const ValueDecl *>();

  // Everything else uses the 'structural' representation.
  return nullptr;
}

TemplateArgument::TemplateArgument(const ASTContext &Ctx, QualType Type,
                                   const APValue &V, bool IsDefaulted) {
  if (Type->isIntegralOrEnumerationType() && V.isInt())
    initFromIntegral(Ctx, V.getInt(), Type, IsDefaulted);
  else if ((V.isLValue() && V.isNullPointer()) ||
           (V.isMemberPointer() && !V.getMemberPointerDecl()))
    initFromType(Type, /*isNullPtr=*/true, IsDefaulted);
  else if (const ValueDecl *VD = getAsSimpleValueDeclRef(Ctx, Type, V))
    // FIXME: The Declaration form should expose a const ValueDecl*.
    initFromDeclaration(const_cast<ValueDecl *>(VD), Type, IsDefaulted);
  else
    initFromStructural(Ctx, Type, V, IsDefaulted);
}

TemplateArgument
TemplateArgument::CreatePackCopy(ASTContext &Context,
                                 ArrayRef<TemplateArgument> Args) {
  if (Args.empty())
    return getEmptyPack();

  return TemplateArgument(Args.copy(Context));
}

TemplateArgumentDependence TemplateArgument::getDependence() const {
  auto Deps = TemplateArgumentDependence::None;
  switch (getKind()) {
  case Null:
    llvm_unreachable("Should not have a NULL template argument");

  case Type:
    Deps = toTemplateArgumentDependence(getAsType()->getDependence());
    if (isa<PackExpansionType>(getAsType()))
      Deps |= TemplateArgumentDependence::Dependent;
    return Deps;

  case Template:
    return toTemplateArgumentDependence(getAsTemplate().getDependence());

  case TemplateExpansion:
    return TemplateArgumentDependence::Dependent |
           TemplateArgumentDependence::Instantiation;

  case Declaration: {
    auto *DC = dyn_cast<DeclContext>(getAsDecl());
    if (!DC)
      DC = getAsDecl()->getDeclContext();
    if (DC->isDependentContext())
      Deps = TemplateArgumentDependence::Dependent |
             TemplateArgumentDependence::Instantiation;
    return Deps;
  }

  case NullPtr:
  case Integral:
  case StructuralValue:
    return TemplateArgumentDependence::None;

  case Expression:
    Deps = toTemplateArgumentDependence(getAsExpr()->getDependence());
    if (isa<PackExpansionExpr>(getAsExpr()))
      Deps |= TemplateArgumentDependence::Dependent |
              TemplateArgumentDependence::Instantiation;
    return Deps;

  case Pack:
    for (const auto &P : pack_elements())
      Deps |= P.getDependence();
    return Deps;
  }
  llvm_unreachable("unhandled ArgKind");
}

bool TemplateArgument::isDependent() const {
  return getDependence() & TemplateArgumentDependence::Dependent;
}

bool TemplateArgument::isInstantiationDependent() const {
  return getDependence() & TemplateArgumentDependence::Instantiation;
}

bool TemplateArgument::isPackExpansion() const {
  switch (getKind()) {
  case Null:
  case Declaration:
  case Integral:
  case StructuralValue:
  case Pack:
  case Template:
  case NullPtr:
    return false;

  case TemplateExpansion:
    return true;

  case Type:
    return isa<PackExpansionType>(getAsType());

  case Expression:
    return isa<PackExpansionExpr>(getAsExpr());
  }

  llvm_unreachable("Invalid TemplateArgument Kind!");
}

bool TemplateArgument::containsUnexpandedParameterPack() const {
  return getDependence() & TemplateArgumentDependence::UnexpandedPack;
}

std::optional<unsigned> TemplateArgument::getNumTemplateExpansions() const {
  assert(getKind() == TemplateExpansion);
  if (TemplateArg.NumExpansions)
    return TemplateArg.NumExpansions - 1;

  return std::nullopt;
}

QualType TemplateArgument::getNonTypeTemplateArgumentType() const {
  switch (getKind()) {
  case TemplateArgument::Null:
  case TemplateArgument::Type:
  case TemplateArgument::Template:
  case TemplateArgument::TemplateExpansion:
  case TemplateArgument::Pack:
    return QualType();

  case TemplateArgument::Integral:
    return getIntegralType();

  case TemplateArgument::Expression:
    return getAsExpr()->getType();

  case TemplateArgument::Declaration:
    return getParamTypeForDecl();

  case TemplateArgument::NullPtr:
    return getNullPtrType();

  case TemplateArgument::StructuralValue:
    return getStructuralValueType();
  }

  llvm_unreachable("Invalid TemplateArgument Kind!");
}

void TemplateArgument::Profile(llvm::FoldingSetNodeID &ID,
                               const ASTContext &Context) const {
  ID.AddInteger(getKind());
  switch (getKind()) {
  case Null:
    break;

  case Type:
    getAsType().Profile(ID);
    break;

  case NullPtr:
    getNullPtrType().Profile(ID);
    break;

  case Declaration:
    getParamTypeForDecl().Profile(ID);
    ID.AddPointer(getAsDecl());
    break;

  case TemplateExpansion:
    ID.AddInteger(TemplateArg.NumExpansions);
    [[fallthrough]];
  case Template:
    ID.AddPointer(TemplateArg.Name);
    break;

  case Integral:
    getIntegralType().Profile(ID);
    getAsIntegral().Profile(ID);
    break;

  case StructuralValue:
    getStructuralValueType().Profile(ID);
    getAsStructuralValue().Profile(ID);
    break;

  case Expression:
    getAsExpr()->Profile(ID, Context, true);
    break;

  case Pack:
    ID.AddInteger(Args.NumArgs);
    for (unsigned I = 0; I != Args.NumArgs; ++I)
      Args.Args[I].Profile(ID, Context);
  }
}

bool TemplateArgument::structurallyEquals(const TemplateArgument &Other) const {
  if (getKind() != Other.getKind()) return false;

  switch (getKind()) {
  case Null:
  case Type:
  case Expression:
  case NullPtr:
    return TypeOrValue.V == Other.TypeOrValue.V;

  case Template:
  case TemplateExpansion:
    return TemplateArg.Name == Other.TemplateArg.Name &&
           TemplateArg.NumExpansions == Other.TemplateArg.NumExpansions;

  case Declaration:
    return getAsDecl() == Other.getAsDecl() &&
           getParamTypeForDecl() == Other.getParamTypeForDecl();

  case Integral:
    return getIntegralType() == Other.getIntegralType() &&
           getAsIntegral() == Other.getAsIntegral();

  case StructuralValue: {
    if (getStructuralValueType().getCanonicalType() !=
        Other.getStructuralValueType().getCanonicalType())
      return false;

    llvm::FoldingSetNodeID A, B;
    getAsStructuralValue().Profile(A);
    Other.getAsStructuralValue().Profile(B);
    return A == B;
  }

  case Pack:
    if (Args.NumArgs != Other.Args.NumArgs) return false;
    for (unsigned I = 0, E = Args.NumArgs; I != E; ++I)
      if (!Args.Args[I].structurallyEquals(Other.Args.Args[I]))
        return false;
    return true;
  }

  llvm_unreachable("Invalid TemplateArgument Kind!");
}

TemplateArgument TemplateArgument::getPackExpansionPattern() const {
  assert(isPackExpansion());

  switch (getKind()) {
  case Type:
    return getAsType()->castAs<PackExpansionType>()->getPattern();

  case Expression:
    return cast<PackExpansionExpr>(getAsExpr())->getPattern();

  case TemplateExpansion:
    return TemplateArgument(getAsTemplateOrTemplatePattern());

  case Declaration:
  case Integral:
  case StructuralValue:
  case Pack:
  case Null:
  case Template:
  case NullPtr:
    return TemplateArgument();
  }

  llvm_unreachable("Invalid TemplateArgument Kind!");
}

void TemplateArgument::print(const PrintingPolicy &Policy, raw_ostream &Out,
                             bool IncludeType) const {

  switch (getKind()) {
  case Null:
    Out << "(no value)";
    break;

  case Type: {
    PrintingPolicy SubPolicy(Policy);
    SubPolicy.SuppressStrongLifetime = true;
    getAsType().print(Out, SubPolicy);
    break;
  }

  case Declaration: {
    NamedDecl *ND = getAsDecl();
    if (getParamTypeForDecl()->isRecordType()) {
      if (auto *TPO = dyn_cast<TemplateParamObjectDecl>(ND)) {
        TPO->getType().getUnqualifiedType().print(Out, Policy);
        TPO->printAsInit(Out, Policy);
        break;
      }
    }
    if (auto *VD = dyn_cast<ValueDecl>(ND)) {
      if (needsAmpersandOnTemplateArg(getParamTypeForDecl(), VD->getType()))
        Out << "&";
    }
    ND->printQualifiedName(Out);
    break;
  }

  case StructuralValue:
    getAsStructuralValue().printPretty(Out, Policy, getStructuralValueType());
    break;

  case NullPtr:
    // FIXME: Include the type if it's not obvious from the context.
    Out << "nullptr";
    break;

  case Template: {
    getAsTemplate().print(Out, Policy);
    break;
  }

  case TemplateExpansion:
    getAsTemplateOrTemplatePattern().print(Out, Policy);
    Out << "...";
    break;

  case Integral:
    printIntegral(*this, Out, Policy, IncludeType);
    break;

  case Expression:
    getAsExpr()->printPretty(Out, nullptr, Policy);
    break;

  case Pack:
    Out << "<";
    bool First = true;
    for (const auto &P : pack_elements()) {
      if (First)
        First = false;
      else
        Out << ", ";

      P.print(Policy, Out, IncludeType);
    }
    Out << ">";
    break;
  }
}

//===----------------------------------------------------------------------===//
// TemplateArgumentLoc Implementation
//===----------------------------------------------------------------------===//

SourceRange TemplateArgumentLoc::getSourceRange() const {
  switch (Argument.getKind()) {
  case TemplateArgument::Expression:
    return getSourceExpression()->getSourceRange();

  case TemplateArgument::Declaration:
    return getSourceDeclExpression()->getSourceRange();

  case TemplateArgument::NullPtr:
    return getSourceNullPtrExpression()->getSourceRange();

  case TemplateArgument::Type:
    if (TypeSourceInfo *TSI = getTypeSourceInfo())
      return TSI->getTypeLoc().getSourceRange();
    else
      return SourceRange();

  case TemplateArgument::Template:
    if (getTemplateQualifierLoc())
      return SourceRange(getTemplateQualifierLoc().getBeginLoc(),
                         getTemplateNameLoc());
    return SourceRange(getTemplateNameLoc());

  case TemplateArgument::TemplateExpansion:
    if (getTemplateQualifierLoc())
      return SourceRange(getTemplateQualifierLoc().getBeginLoc(),
                         getTemplateEllipsisLoc());
    return SourceRange(getTemplateNameLoc(), getTemplateEllipsisLoc());

  case TemplateArgument::Integral:
    return getSourceIntegralExpression()->getSourceRange();

  case TemplateArgument::StructuralValue:
    return getSourceStructuralValueExpression()->getSourceRange();

  case TemplateArgument::Pack:
  case TemplateArgument::Null:
    return SourceRange();
  }

  llvm_unreachable("Invalid TemplateArgument Kind!");
}

template <typename T>
static const T &DiagTemplateArg(const T &DB, const TemplateArgument &Arg) {
  switch (Arg.getKind()) {
  case TemplateArgument::Null:
    // This is bad, but not as bad as crashing because of argument
    // count mismatches.
    return DB << "(null template argument)";

  case TemplateArgument::Type:
    return DB << Arg.getAsType();

  case TemplateArgument::Declaration:
    return DB << Arg.getAsDecl();

  case TemplateArgument::NullPtr:
    return DB << "nullptr";

  case TemplateArgument::Integral:
    return DB << toString(Arg.getAsIntegral(), 10);

  case TemplateArgument::StructuralValue: {
    // FIXME: We're guessing at LangOptions!
    SmallString<32> Str;
    llvm::raw_svector_ostream OS(Str);
    LangOptions LangOpts;
    LangOpts.CPlusPlus = true;
    PrintingPolicy Policy(LangOpts);
    Arg.getAsStructuralValue().printPretty(OS, Policy,
                                           Arg.getStructuralValueType());
    return DB << OS.str();
  }

  case TemplateArgument::Template:
    return DB << Arg.getAsTemplate();

  case TemplateArgument::TemplateExpansion:
    return DB << Arg.getAsTemplateOrTemplatePattern() << "...";

  case TemplateArgument::Expression: {
    // This shouldn't actually ever happen, so it's okay that we're
    // regurgitating an expression here.
    // FIXME: We're guessing at LangOptions!
    SmallString<32> Str;
    llvm::raw_svector_ostream OS(Str);
    LangOptions LangOpts;
    LangOpts.CPlusPlus = true;
    PrintingPolicy Policy(LangOpts);
    Arg.getAsExpr()->printPretty(OS, nullptr, Policy);
    return DB << OS.str();
  }

  case TemplateArgument::Pack: {
    // FIXME: We're guessing at LangOptions!
    SmallString<32> Str;
    llvm::raw_svector_ostream OS(Str);
    LangOptions LangOpts;
    LangOpts.CPlusPlus = true;
    PrintingPolicy Policy(LangOpts);
    Arg.print(Policy, OS, /*IncludeType*/ true);
    return DB << OS.str();
  }
  }

  llvm_unreachable("Invalid TemplateArgument Kind!");
}

const StreamingDiagnostic &clang::operator<<(const StreamingDiagnostic &DB,
                                             const TemplateArgument &Arg) {
  return DiagTemplateArg(DB, Arg);
}

clang::TemplateArgumentLocInfo::TemplateArgumentLocInfo(
    ASTContext &Ctx, NestedNameSpecifierLoc QualifierLoc,
    SourceLocation TemplateNameLoc, SourceLocation EllipsisLoc) {
  TemplateTemplateArgLocInfo *Template = new (Ctx) TemplateTemplateArgLocInfo;
  Template->Qualifier = QualifierLoc.getNestedNameSpecifier();
  Template->QualifierLocData = QualifierLoc.getOpaqueData();
  Template->TemplateNameLoc = TemplateNameLoc;
  Template->EllipsisLoc = EllipsisLoc;
  Pointer = Template;
}

const ASTTemplateArgumentListInfo *
ASTTemplateArgumentListInfo::Create(const ASTContext &C,
                                    const TemplateArgumentListInfo &List) {
  std::size_t size = totalSizeToAlloc<TemplateArgumentLoc>(List.size());
  void *Mem = C.Allocate(size, alignof(ASTTemplateArgumentListInfo));
  return new (Mem) ASTTemplateArgumentListInfo(List);
}

const ASTTemplateArgumentListInfo *
ASTTemplateArgumentListInfo::Create(const ASTContext &C,
                                    const ASTTemplateArgumentListInfo *List) {
  if (!List)
    return nullptr;
  std::size_t size =
      totalSizeToAlloc<TemplateArgumentLoc>(List->getNumTemplateArgs());
  void *Mem = C.Allocate(size, alignof(ASTTemplateArgumentListInfo));
  return new (Mem) ASTTemplateArgumentListInfo(List);
}

ASTTemplateArgumentListInfo::ASTTemplateArgumentListInfo(
    const TemplateArgumentListInfo &Info) {
  LAngleLoc = Info.getLAngleLoc();
  RAngleLoc = Info.getRAngleLoc();
  NumTemplateArgs = Info.size();

  TemplateArgumentLoc *ArgBuffer = getTrailingObjects<TemplateArgumentLoc>();
  for (unsigned i = 0; i != NumTemplateArgs; ++i)
    new (&ArgBuffer[i]) TemplateArgumentLoc(Info[i]);
}

ASTTemplateArgumentListInfo::ASTTemplateArgumentListInfo(
    const ASTTemplateArgumentListInfo *Info) {
  LAngleLoc = Info->getLAngleLoc();
  RAngleLoc = Info->getRAngleLoc();
  NumTemplateArgs = Info->getNumTemplateArgs();

  TemplateArgumentLoc *ArgBuffer = getTrailingObjects<TemplateArgumentLoc>();
  for (unsigned i = 0; i != NumTemplateArgs; ++i)
    new (&ArgBuffer[i]) TemplateArgumentLoc((*Info)[i]);
}

void ASTTemplateKWAndArgsInfo::initializeFrom(
    SourceLocation TemplateKWLoc, const TemplateArgumentListInfo &Info,
    TemplateArgumentLoc *OutArgArray) {
  this->TemplateKWLoc = TemplateKWLoc;
  LAngleLoc = Info.getLAngleLoc();
  RAngleLoc = Info.getRAngleLoc();
  NumTemplateArgs = Info.size();

  for (unsigned i = 0; i != NumTemplateArgs; ++i)
    new (&OutArgArray[i]) TemplateArgumentLoc(Info[i]);
}

void ASTTemplateKWAndArgsInfo::initializeFrom(SourceLocation TemplateKWLoc) {
  assert(TemplateKWLoc.isValid());
  LAngleLoc = SourceLocation();
  RAngleLoc = SourceLocation();
  this->TemplateKWLoc = TemplateKWLoc;
  NumTemplateArgs = 0;
}

void ASTTemplateKWAndArgsInfo::initializeFrom(
    SourceLocation TemplateKWLoc, const TemplateArgumentListInfo &Info,
    TemplateArgumentLoc *OutArgArray, TemplateArgumentDependence &Deps) {
  this->TemplateKWLoc = TemplateKWLoc;
  LAngleLoc = Info.getLAngleLoc();
  RAngleLoc = Info.getRAngleLoc();
  NumTemplateArgs = Info.size();

  for (unsigned i = 0; i != NumTemplateArgs; ++i) {
    Deps |= Info[i].getArgument().getDependence();

    new (&OutArgArray[i]) TemplateArgumentLoc(Info[i]);
  }
}

void ASTTemplateKWAndArgsInfo::copyInto(const TemplateArgumentLoc *ArgArray,
                                        TemplateArgumentListInfo &Info) const {
  Info.setLAngleLoc(LAngleLoc);
  Info.setRAngleLoc(RAngleLoc);
  for (unsigned I = 0; I != NumTemplateArgs; ++I)
    Info.addArgument(ArgArray[I]);
}
