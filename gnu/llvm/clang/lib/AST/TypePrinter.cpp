//===- TypePrinter.cpp - Pretty-Print Clang Types -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This contains code to print types from Clang's type system.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/TemplateName.h"
#include "clang/AST/TextNodeDumper.h"
#include "clang/AST/Type.h"
#include "clang/Basic/AddressSpaces.h"
#include "clang/Basic/ExceptionSpecificationType.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Specifiers.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <string>

using namespace clang;

namespace {

/// RAII object that enables printing of the ARC __strong lifetime
/// qualifier.
class IncludeStrongLifetimeRAII {
  PrintingPolicy &Policy;
  bool Old;

public:
  explicit IncludeStrongLifetimeRAII(PrintingPolicy &Policy)
      : Policy(Policy), Old(Policy.SuppressStrongLifetime) {
    if (!Policy.SuppressLifetimeQualifiers)
      Policy.SuppressStrongLifetime = false;
  }

  ~IncludeStrongLifetimeRAII() { Policy.SuppressStrongLifetime = Old; }
};

class ParamPolicyRAII {
  PrintingPolicy &Policy;
  bool Old;

public:
  explicit ParamPolicyRAII(PrintingPolicy &Policy)
      : Policy(Policy), Old(Policy.SuppressSpecifiers) {
    Policy.SuppressSpecifiers = false;
  }

  ~ParamPolicyRAII() { Policy.SuppressSpecifiers = Old; }
};

class DefaultTemplateArgsPolicyRAII {
  PrintingPolicy &Policy;
  bool Old;

public:
  explicit DefaultTemplateArgsPolicyRAII(PrintingPolicy &Policy)
      : Policy(Policy), Old(Policy.SuppressDefaultTemplateArgs) {
    Policy.SuppressDefaultTemplateArgs = false;
  }

  ~DefaultTemplateArgsPolicyRAII() { Policy.SuppressDefaultTemplateArgs = Old; }
};

class ElaboratedTypePolicyRAII {
  PrintingPolicy &Policy;
  bool SuppressTagKeyword;
  bool SuppressScope;

public:
  explicit ElaboratedTypePolicyRAII(PrintingPolicy &Policy) : Policy(Policy) {
    SuppressTagKeyword = Policy.SuppressTagKeyword;
    SuppressScope = Policy.SuppressScope;
    Policy.SuppressTagKeyword = true;
    Policy.SuppressScope = true;
  }

  ~ElaboratedTypePolicyRAII() {
    Policy.SuppressTagKeyword = SuppressTagKeyword;
    Policy.SuppressScope = SuppressScope;
  }
};

class TypePrinter {
  PrintingPolicy Policy;
  unsigned Indentation;
  bool HasEmptyPlaceHolder = false;
  bool InsideCCAttribute = false;

public:
  explicit TypePrinter(const PrintingPolicy &Policy, unsigned Indentation = 0)
      : Policy(Policy), Indentation(Indentation) {}

  void print(const Type *ty, Qualifiers qs, raw_ostream &OS,
             StringRef PlaceHolder);
  void print(QualType T, raw_ostream &OS, StringRef PlaceHolder);

  static bool canPrefixQualifiers(const Type *T, bool &NeedARCStrongQualifier);
  void spaceBeforePlaceHolder(raw_ostream &OS);
  void printTypeSpec(NamedDecl *D, raw_ostream &OS);
  void printTemplateId(const TemplateSpecializationType *T, raw_ostream &OS,
                       bool FullyQualify);

  void printBefore(QualType T, raw_ostream &OS);
  void printAfter(QualType T, raw_ostream &OS);
  void AppendScope(DeclContext *DC, raw_ostream &OS,
                   DeclarationName NameInScope);
  void printTag(TagDecl *T, raw_ostream &OS);
  void printFunctionAfter(const FunctionType::ExtInfo &Info, raw_ostream &OS);
#define ABSTRACT_TYPE(CLASS, PARENT)
#define TYPE(CLASS, PARENT)                                                    \
  void print##CLASS##Before(const CLASS##Type *T, raw_ostream &OS);            \
  void print##CLASS##After(const CLASS##Type *T, raw_ostream &OS);
#include "clang/AST/TypeNodes.inc"

private:
  void printBefore(const Type *ty, Qualifiers qs, raw_ostream &OS);
  void printAfter(const Type *ty, Qualifiers qs, raw_ostream &OS);
};

} // namespace

static void AppendTypeQualList(raw_ostream &OS, unsigned TypeQuals,
                               bool HasRestrictKeyword) {
  bool appendSpace = false;
  if (TypeQuals & Qualifiers::Const) {
    OS << "const";
    appendSpace = true;
  }
  if (TypeQuals & Qualifiers::Volatile) {
    if (appendSpace) OS << ' ';
    OS << "volatile";
    appendSpace = true;
  }
  if (TypeQuals & Qualifiers::Restrict) {
    if (appendSpace) OS << ' ';
    if (HasRestrictKeyword) {
      OS << "restrict";
    } else {
      OS << "__restrict";
    }
  }
}

void TypePrinter::spaceBeforePlaceHolder(raw_ostream &OS) {
  if (!HasEmptyPlaceHolder)
    OS << ' ';
}

static SplitQualType splitAccordingToPolicy(QualType QT,
                                            const PrintingPolicy &Policy) {
  if (Policy.PrintCanonicalTypes)
    QT = QT.getCanonicalType();
  return QT.split();
}

void TypePrinter::print(QualType t, raw_ostream &OS, StringRef PlaceHolder) {
  SplitQualType split = splitAccordingToPolicy(t, Policy);
  print(split.Ty, split.Quals, OS, PlaceHolder);
}

void TypePrinter::print(const Type *T, Qualifiers Quals, raw_ostream &OS,
                        StringRef PlaceHolder) {
  if (!T) {
    OS << "NULL TYPE";
    return;
  }

  SaveAndRestore PHVal(HasEmptyPlaceHolder, PlaceHolder.empty());

  printBefore(T, Quals, OS);
  OS << PlaceHolder;
  printAfter(T, Quals, OS);
}

bool TypePrinter::canPrefixQualifiers(const Type *T,
                                      bool &NeedARCStrongQualifier) {
  // CanPrefixQualifiers - We prefer to print type qualifiers before the type,
  // so that we get "const int" instead of "int const", but we can't do this if
  // the type is complex.  For example if the type is "int*", we *must* print
  // "int * const", printing "const int *" is different.  Only do this when the
  // type expands to a simple string.
  bool CanPrefixQualifiers = false;
  NeedARCStrongQualifier = false;
  const Type *UnderlyingType = T;
  if (const auto *AT = dyn_cast<AutoType>(T))
    UnderlyingType = AT->desugar().getTypePtr();
  if (const auto *Subst = dyn_cast<SubstTemplateTypeParmType>(T))
    UnderlyingType = Subst->getReplacementType().getTypePtr();
  Type::TypeClass TC = UnderlyingType->getTypeClass();

  switch (TC) {
    case Type::Auto:
    case Type::Builtin:
    case Type::Complex:
    case Type::UnresolvedUsing:
    case Type::Using:
    case Type::Typedef:
    case Type::TypeOfExpr:
    case Type::TypeOf:
    case Type::Decltype:
    case Type::UnaryTransform:
    case Type::Record:
    case Type::Enum:
    case Type::Elaborated:
    case Type::TemplateTypeParm:
    case Type::SubstTemplateTypeParmPack:
    case Type::DeducedTemplateSpecialization:
    case Type::TemplateSpecialization:
    case Type::InjectedClassName:
    case Type::DependentName:
    case Type::DependentTemplateSpecialization:
    case Type::ObjCObject:
    case Type::ObjCTypeParam:
    case Type::ObjCInterface:
    case Type::Atomic:
    case Type::Pipe:
    case Type::BitInt:
    case Type::DependentBitInt:
    case Type::BTFTagAttributed:
      CanPrefixQualifiers = true;
      break;

    case Type::ObjCObjectPointer:
      CanPrefixQualifiers = T->isObjCIdType() || T->isObjCClassType() ||
        T->isObjCQualifiedIdType() || T->isObjCQualifiedClassType();
      break;

    case Type::VariableArray:
    case Type::DependentSizedArray:
      NeedARCStrongQualifier = true;
      [[fallthrough]];

    case Type::ConstantArray:
    case Type::IncompleteArray:
      return canPrefixQualifiers(
          cast<ArrayType>(UnderlyingType)->getElementType().getTypePtr(),
          NeedARCStrongQualifier);

    case Type::Adjusted:
    case Type::Decayed:
    case Type::ArrayParameter:
    case Type::Pointer:
    case Type::BlockPointer:
    case Type::LValueReference:
    case Type::RValueReference:
    case Type::MemberPointer:
    case Type::DependentAddressSpace:
    case Type::DependentVector:
    case Type::DependentSizedExtVector:
    case Type::Vector:
    case Type::ExtVector:
    case Type::ConstantMatrix:
    case Type::DependentSizedMatrix:
    case Type::FunctionProto:
    case Type::FunctionNoProto:
    case Type::Paren:
    case Type::PackExpansion:
    case Type::SubstTemplateTypeParm:
    case Type::MacroQualified:
    case Type::CountAttributed:
      CanPrefixQualifiers = false;
      break;

    case Type::Attributed: {
      // We still want to print the address_space before the type if it is an
      // address_space attribute.
      const auto *AttrTy = cast<AttributedType>(UnderlyingType);
      CanPrefixQualifiers = AttrTy->getAttrKind() == attr::AddressSpace;
      break;
    }
    case Type::PackIndexing: {
      return canPrefixQualifiers(
          cast<PackIndexingType>(UnderlyingType)->getPattern().getTypePtr(),
          NeedARCStrongQualifier);
    }
  }

  return CanPrefixQualifiers;
}

void TypePrinter::printBefore(QualType T, raw_ostream &OS) {
  SplitQualType Split = splitAccordingToPolicy(T, Policy);

  // If we have cv1 T, where T is substituted for cv2 U, only print cv1 - cv2
  // at this level.
  Qualifiers Quals = Split.Quals;
  if (const auto *Subst = dyn_cast<SubstTemplateTypeParmType>(Split.Ty))
    Quals -= QualType(Subst, 0).getQualifiers();

  printBefore(Split.Ty, Quals, OS);
}

/// Prints the part of the type string before an identifier, e.g. for
/// "int foo[10]" it prints "int ".
void TypePrinter::printBefore(const Type *T,Qualifiers Quals, raw_ostream &OS) {
  if (Policy.SuppressSpecifiers && T->isSpecifierType())
    return;

  SaveAndRestore PrevPHIsEmpty(HasEmptyPlaceHolder);

  // Print qualifiers as appropriate.

  bool CanPrefixQualifiers = false;
  bool NeedARCStrongQualifier = false;
  CanPrefixQualifiers = canPrefixQualifiers(T, NeedARCStrongQualifier);

  if (CanPrefixQualifiers && !Quals.empty()) {
    if (NeedARCStrongQualifier) {
      IncludeStrongLifetimeRAII Strong(Policy);
      Quals.print(OS, Policy, /*appendSpaceIfNonEmpty=*/true);
    } else {
      Quals.print(OS, Policy, /*appendSpaceIfNonEmpty=*/true);
    }
  }

  bool hasAfterQuals = false;
  if (!CanPrefixQualifiers && !Quals.empty()) {
    hasAfterQuals = !Quals.isEmptyWhenPrinted(Policy);
    if (hasAfterQuals)
      HasEmptyPlaceHolder = false;
  }

  switch (T->getTypeClass()) {
#define ABSTRACT_TYPE(CLASS, PARENT)
#define TYPE(CLASS, PARENT) case Type::CLASS: \
    print##CLASS##Before(cast<CLASS##Type>(T), OS); \
    break;
#include "clang/AST/TypeNodes.inc"
  }

  if (hasAfterQuals) {
    if (NeedARCStrongQualifier) {
      IncludeStrongLifetimeRAII Strong(Policy);
      Quals.print(OS, Policy, /*appendSpaceIfNonEmpty=*/!PrevPHIsEmpty.get());
    } else {
      Quals.print(OS, Policy, /*appendSpaceIfNonEmpty=*/!PrevPHIsEmpty.get());
    }
  }
}

void TypePrinter::printAfter(QualType t, raw_ostream &OS) {
  SplitQualType split = splitAccordingToPolicy(t, Policy);
  printAfter(split.Ty, split.Quals, OS);
}

/// Prints the part of the type string after an identifier, e.g. for
/// "int foo[10]" it prints "[10]".
void TypePrinter::printAfter(const Type *T, Qualifiers Quals, raw_ostream &OS) {
  switch (T->getTypeClass()) {
#define ABSTRACT_TYPE(CLASS, PARENT)
#define TYPE(CLASS, PARENT) case Type::CLASS: \
    print##CLASS##After(cast<CLASS##Type>(T), OS); \
    break;
#include "clang/AST/TypeNodes.inc"
  }
}

void TypePrinter::printBuiltinBefore(const BuiltinType *T, raw_ostream &OS) {
  OS << T->getName(Policy);
  spaceBeforePlaceHolder(OS);
}

void TypePrinter::printBuiltinAfter(const BuiltinType *T, raw_ostream &OS) {}

void TypePrinter::printComplexBefore(const ComplexType *T, raw_ostream &OS) {
  OS << "_Complex ";
  printBefore(T->getElementType(), OS);
}

void TypePrinter::printComplexAfter(const ComplexType *T, raw_ostream &OS) {
  printAfter(T->getElementType(), OS);
}

void TypePrinter::printPointerBefore(const PointerType *T, raw_ostream &OS) {
  IncludeStrongLifetimeRAII Strong(Policy);
  SaveAndRestore NonEmptyPH(HasEmptyPlaceHolder, false);
  printBefore(T->getPointeeType(), OS);
  // Handle things like 'int (*A)[4];' correctly.
  // FIXME: this should include vectors, but vectors use attributes I guess.
  if (isa<ArrayType>(T->getPointeeType()))
    OS << '(';
  OS << '*';
}

void TypePrinter::printPointerAfter(const PointerType *T, raw_ostream &OS) {
  IncludeStrongLifetimeRAII Strong(Policy);
  SaveAndRestore NonEmptyPH(HasEmptyPlaceHolder, false);
  // Handle things like 'int (*A)[4];' correctly.
  // FIXME: this should include vectors, but vectors use attributes I guess.
  if (isa<ArrayType>(T->getPointeeType()))
    OS << ')';
  printAfter(T->getPointeeType(), OS);
}

void TypePrinter::printBlockPointerBefore(const BlockPointerType *T,
                                          raw_ostream &OS) {
  SaveAndRestore NonEmptyPH(HasEmptyPlaceHolder, false);
  printBefore(T->getPointeeType(), OS);
  OS << '^';
}

void TypePrinter::printBlockPointerAfter(const BlockPointerType *T,
                                          raw_ostream &OS) {
  SaveAndRestore NonEmptyPH(HasEmptyPlaceHolder, false);
  printAfter(T->getPointeeType(), OS);
}

// When printing a reference, the referenced type might also be a reference.
// If so, we want to skip that before printing the inner type.
static QualType skipTopLevelReferences(QualType T) {
  if (auto *Ref = T->getAs<ReferenceType>())
    return skipTopLevelReferences(Ref->getPointeeTypeAsWritten());
  return T;
}

void TypePrinter::printLValueReferenceBefore(const LValueReferenceType *T,
                                             raw_ostream &OS) {
  IncludeStrongLifetimeRAII Strong(Policy);
  SaveAndRestore NonEmptyPH(HasEmptyPlaceHolder, false);
  QualType Inner = skipTopLevelReferences(T->getPointeeTypeAsWritten());
  printBefore(Inner, OS);
  // Handle things like 'int (&A)[4];' correctly.
  // FIXME: this should include vectors, but vectors use attributes I guess.
  if (isa<ArrayType>(Inner))
    OS << '(';
  OS << '&';
}

void TypePrinter::printLValueReferenceAfter(const LValueReferenceType *T,
                                            raw_ostream &OS) {
  IncludeStrongLifetimeRAII Strong(Policy);
  SaveAndRestore NonEmptyPH(HasEmptyPlaceHolder, false);
  QualType Inner = skipTopLevelReferences(T->getPointeeTypeAsWritten());
  // Handle things like 'int (&A)[4];' correctly.
  // FIXME: this should include vectors, but vectors use attributes I guess.
  if (isa<ArrayType>(Inner))
    OS << ')';
  printAfter(Inner, OS);
}

void TypePrinter::printRValueReferenceBefore(const RValueReferenceType *T,
                                             raw_ostream &OS) {
  IncludeStrongLifetimeRAII Strong(Policy);
  SaveAndRestore NonEmptyPH(HasEmptyPlaceHolder, false);
  QualType Inner = skipTopLevelReferences(T->getPointeeTypeAsWritten());
  printBefore(Inner, OS);
  // Handle things like 'int (&&A)[4];' correctly.
  // FIXME: this should include vectors, but vectors use attributes I guess.
  if (isa<ArrayType>(Inner))
    OS << '(';
  OS << "&&";
}

void TypePrinter::printRValueReferenceAfter(const RValueReferenceType *T,
                                            raw_ostream &OS) {
  IncludeStrongLifetimeRAII Strong(Policy);
  SaveAndRestore NonEmptyPH(HasEmptyPlaceHolder, false);
  QualType Inner = skipTopLevelReferences(T->getPointeeTypeAsWritten());
  // Handle things like 'int (&&A)[4];' correctly.
  // FIXME: this should include vectors, but vectors use attributes I guess.
  if (isa<ArrayType>(Inner))
    OS << ')';
  printAfter(Inner, OS);
}

void TypePrinter::printMemberPointerBefore(const MemberPointerType *T,
                                           raw_ostream &OS) {
  IncludeStrongLifetimeRAII Strong(Policy);
  SaveAndRestore NonEmptyPH(HasEmptyPlaceHolder, false);
  printBefore(T->getPointeeType(), OS);
  // Handle things like 'int (Cls::*A)[4];' correctly.
  // FIXME: this should include vectors, but vectors use attributes I guess.
  if (isa<ArrayType>(T->getPointeeType()))
    OS << '(';

  PrintingPolicy InnerPolicy(Policy);
  InnerPolicy.IncludeTagDefinition = false;
  TypePrinter(InnerPolicy).print(QualType(T->getClass(), 0), OS, StringRef());

  OS << "::*";
}

void TypePrinter::printMemberPointerAfter(const MemberPointerType *T,
                                          raw_ostream &OS) {
  IncludeStrongLifetimeRAII Strong(Policy);
  SaveAndRestore NonEmptyPH(HasEmptyPlaceHolder, false);
  // Handle things like 'int (Cls::*A)[4];' correctly.
  // FIXME: this should include vectors, but vectors use attributes I guess.
  if (isa<ArrayType>(T->getPointeeType()))
    OS << ')';
  printAfter(T->getPointeeType(), OS);
}

void TypePrinter::printConstantArrayBefore(const ConstantArrayType *T,
                                           raw_ostream &OS) {
  IncludeStrongLifetimeRAII Strong(Policy);
  printBefore(T->getElementType(), OS);
}

void TypePrinter::printConstantArrayAfter(const ConstantArrayType *T,
                                          raw_ostream &OS) {
  OS << '[';
  if (T->getIndexTypeQualifiers().hasQualifiers()) {
    AppendTypeQualList(OS, T->getIndexTypeCVRQualifiers(),
                       Policy.Restrict);
    OS << ' ';
  }

  if (T->getSizeModifier() == ArraySizeModifier::Static)
    OS << "static ";

  OS << T->getZExtSize() << ']';
  printAfter(T->getElementType(), OS);
}

void TypePrinter::printIncompleteArrayBefore(const IncompleteArrayType *T,
                                             raw_ostream &OS) {
  IncludeStrongLifetimeRAII Strong(Policy);
  printBefore(T->getElementType(), OS);
}

void TypePrinter::printIncompleteArrayAfter(const IncompleteArrayType *T,
                                            raw_ostream &OS) {
  OS << "[]";
  printAfter(T->getElementType(), OS);
}

void TypePrinter::printVariableArrayBefore(const VariableArrayType *T,
                                           raw_ostream &OS) {
  IncludeStrongLifetimeRAII Strong(Policy);
  printBefore(T->getElementType(), OS);
}

void TypePrinter::printVariableArrayAfter(const VariableArrayType *T,
                                          raw_ostream &OS) {
  OS << '[';
  if (T->getIndexTypeQualifiers().hasQualifiers()) {
    AppendTypeQualList(OS, T->getIndexTypeCVRQualifiers(), Policy.Restrict);
    OS << ' ';
  }

  if (T->getSizeModifier() == ArraySizeModifier::Static)
    OS << "static ";
  else if (T->getSizeModifier() == ArraySizeModifier::Star)
    OS << '*';

  if (T->getSizeExpr())
    T->getSizeExpr()->printPretty(OS, nullptr, Policy);
  OS << ']';

  printAfter(T->getElementType(), OS);
}

void TypePrinter::printAdjustedBefore(const AdjustedType *T, raw_ostream &OS) {
  // Print the adjusted representation, otherwise the adjustment will be
  // invisible.
  printBefore(T->getAdjustedType(), OS);
}

void TypePrinter::printAdjustedAfter(const AdjustedType *T, raw_ostream &OS) {
  printAfter(T->getAdjustedType(), OS);
}

void TypePrinter::printDecayedBefore(const DecayedType *T, raw_ostream &OS) {
  // Print as though it's a pointer.
  printAdjustedBefore(T, OS);
}

void TypePrinter::printArrayParameterAfter(const ArrayParameterType *T,
                                           raw_ostream &OS) {
  printConstantArrayAfter(T, OS);
}

void TypePrinter::printArrayParameterBefore(const ArrayParameterType *T,
                                            raw_ostream &OS) {
  printConstantArrayBefore(T, OS);
}

void TypePrinter::printDecayedAfter(const DecayedType *T, raw_ostream &OS) {
  printAdjustedAfter(T, OS);
}

void TypePrinter::printDependentSizedArrayBefore(
                                               const DependentSizedArrayType *T,
                                               raw_ostream &OS) {
  IncludeStrongLifetimeRAII Strong(Policy);
  printBefore(T->getElementType(), OS);
}

void TypePrinter::printDependentSizedArrayAfter(
                                               const DependentSizedArrayType *T,
                                               raw_ostream &OS) {
  OS << '[';
  if (T->getSizeExpr())
    T->getSizeExpr()->printPretty(OS, nullptr, Policy);
  OS << ']';
  printAfter(T->getElementType(), OS);
}

void TypePrinter::printDependentAddressSpaceBefore(
    const DependentAddressSpaceType *T, raw_ostream &OS) {
  printBefore(T->getPointeeType(), OS);
}

void TypePrinter::printDependentAddressSpaceAfter(
    const DependentAddressSpaceType *T, raw_ostream &OS) {
  OS << " __attribute__((address_space(";
  if (T->getAddrSpaceExpr())
    T->getAddrSpaceExpr()->printPretty(OS, nullptr, Policy);
  OS << ")))";
  printAfter(T->getPointeeType(), OS);
}

void TypePrinter::printDependentSizedExtVectorBefore(
                                          const DependentSizedExtVectorType *T,
                                          raw_ostream &OS) {
  if (Policy.UseHLSLTypes)
    OS << "vector<";
  printBefore(T->getElementType(), OS);
}

void TypePrinter::printDependentSizedExtVectorAfter(
                                          const DependentSizedExtVectorType *T,
                                          raw_ostream &OS) {
  if (Policy.UseHLSLTypes) {
    OS << ", ";
    if (T->getSizeExpr())
      T->getSizeExpr()->printPretty(OS, nullptr, Policy);
    OS << ">";
  } else {
    OS << " __attribute__((ext_vector_type(";
    if (T->getSizeExpr())
      T->getSizeExpr()->printPretty(OS, nullptr, Policy);
    OS << ")))";
  }
  printAfter(T->getElementType(), OS);
}

void TypePrinter::printVectorBefore(const VectorType *T, raw_ostream &OS) {
  switch (T->getVectorKind()) {
  case VectorKind::AltiVecPixel:
    OS << "__vector __pixel ";
    break;
  case VectorKind::AltiVecBool:
    OS << "__vector __bool ";
    printBefore(T->getElementType(), OS);
    break;
  case VectorKind::AltiVecVector:
    OS << "__vector ";
    printBefore(T->getElementType(), OS);
    break;
  case VectorKind::Neon:
    OS << "__attribute__((neon_vector_type("
       << T->getNumElements() << "))) ";
    printBefore(T->getElementType(), OS);
    break;
  case VectorKind::NeonPoly:
    OS << "__attribute__((neon_polyvector_type(" <<
          T->getNumElements() << "))) ";
    printBefore(T->getElementType(), OS);
    break;
  case VectorKind::Generic: {
    // FIXME: We prefer to print the size directly here, but have no way
    // to get the size of the type.
    OS << "__attribute__((__vector_size__("
       << T->getNumElements()
       << " * sizeof(";
    print(T->getElementType(), OS, StringRef());
    OS << ")))) ";
    printBefore(T->getElementType(), OS);
    break;
  }
  case VectorKind::SveFixedLengthData:
  case VectorKind::SveFixedLengthPredicate:
    // FIXME: We prefer to print the size directly here, but have no way
    // to get the size of the type.
    OS << "__attribute__((__arm_sve_vector_bits__(";

    if (T->getVectorKind() == VectorKind::SveFixedLengthPredicate)
      // Predicates take a bit per byte of the vector size, multiply by 8 to
      // get the number of bits passed to the attribute.
      OS << T->getNumElements() * 8;
    else
      OS << T->getNumElements();

    OS << " * sizeof(";
    print(T->getElementType(), OS, StringRef());
    // Multiply by 8 for the number of bits.
    OS << ") * 8))) ";
    printBefore(T->getElementType(), OS);
    break;
  case VectorKind::RVVFixedLengthData:
  case VectorKind::RVVFixedLengthMask:
    // FIXME: We prefer to print the size directly here, but have no way
    // to get the size of the type.
    OS << "__attribute__((__riscv_rvv_vector_bits__(";

    OS << T->getNumElements();

    OS << " * sizeof(";
    print(T->getElementType(), OS, StringRef());
    // Multiply by 8 for the number of bits.
    OS << ") * 8))) ";
    printBefore(T->getElementType(), OS);
    break;
  }
}

void TypePrinter::printVectorAfter(const VectorType *T, raw_ostream &OS) {
  printAfter(T->getElementType(), OS);
}

void TypePrinter::printDependentVectorBefore(
    const DependentVectorType *T, raw_ostream &OS) {
  switch (T->getVectorKind()) {
  case VectorKind::AltiVecPixel:
    OS << "__vector __pixel ";
    break;
  case VectorKind::AltiVecBool:
    OS << "__vector __bool ";
    printBefore(T->getElementType(), OS);
    break;
  case VectorKind::AltiVecVector:
    OS << "__vector ";
    printBefore(T->getElementType(), OS);
    break;
  case VectorKind::Neon:
    OS << "__attribute__((neon_vector_type(";
    if (T->getSizeExpr())
      T->getSizeExpr()->printPretty(OS, nullptr, Policy);
    OS << "))) ";
    printBefore(T->getElementType(), OS);
    break;
  case VectorKind::NeonPoly:
    OS << "__attribute__((neon_polyvector_type(";
    if (T->getSizeExpr())
      T->getSizeExpr()->printPretty(OS, nullptr, Policy);
    OS << "))) ";
    printBefore(T->getElementType(), OS);
    break;
  case VectorKind::Generic: {
    // FIXME: We prefer to print the size directly here, but have no way
    // to get the size of the type.
    OS << "__attribute__((__vector_size__(";
    if (T->getSizeExpr())
      T->getSizeExpr()->printPretty(OS, nullptr, Policy);
    OS << " * sizeof(";
    print(T->getElementType(), OS, StringRef());
    OS << ")))) ";
    printBefore(T->getElementType(), OS);
    break;
  }
  case VectorKind::SveFixedLengthData:
  case VectorKind::SveFixedLengthPredicate:
    // FIXME: We prefer to print the size directly here, but have no way
    // to get the size of the type.
    OS << "__attribute__((__arm_sve_vector_bits__(";
    if (T->getSizeExpr()) {
      T->getSizeExpr()->printPretty(OS, nullptr, Policy);
      if (T->getVectorKind() == VectorKind::SveFixedLengthPredicate)
        // Predicates take a bit per byte of the vector size, multiply by 8 to
        // get the number of bits passed to the attribute.
        OS << " * 8";
      OS << " * sizeof(";
      print(T->getElementType(), OS, StringRef());
      // Multiply by 8 for the number of bits.
      OS << ") * 8";
    }
    OS << "))) ";
    printBefore(T->getElementType(), OS);
    break;
  case VectorKind::RVVFixedLengthData:
  case VectorKind::RVVFixedLengthMask:
    // FIXME: We prefer to print the size directly here, but have no way
    // to get the size of the type.
    OS << "__attribute__((__riscv_rvv_vector_bits__(";
    if (T->getSizeExpr()) {
      T->getSizeExpr()->printPretty(OS, nullptr, Policy);
      OS << " * sizeof(";
      print(T->getElementType(), OS, StringRef());
      // Multiply by 8 for the number of bits.
      OS << ") * 8";
    }
    OS << "))) ";
    printBefore(T->getElementType(), OS);
    break;
  }
}

void TypePrinter::printDependentVectorAfter(
    const DependentVectorType *T, raw_ostream &OS) {
  printAfter(T->getElementType(), OS);
}

void TypePrinter::printExtVectorBefore(const ExtVectorType *T,
                                       raw_ostream &OS) {
  if (Policy.UseHLSLTypes)
    OS << "vector<";
  printBefore(T->getElementType(), OS);
}

void TypePrinter::printExtVectorAfter(const ExtVectorType *T, raw_ostream &OS) {
  printAfter(T->getElementType(), OS);

  if (Policy.UseHLSLTypes) {
    OS << ", ";
    OS << T->getNumElements();
    OS << ">";
  } else {
    OS << " __attribute__((ext_vector_type(";
    OS << T->getNumElements();
    OS << ")))";
  }
}

void TypePrinter::printConstantMatrixBefore(const ConstantMatrixType *T,
                                            raw_ostream &OS) {
  printBefore(T->getElementType(), OS);
  OS << " __attribute__((matrix_type(";
  OS << T->getNumRows() << ", " << T->getNumColumns();
  OS << ")))";
}

void TypePrinter::printConstantMatrixAfter(const ConstantMatrixType *T,
                                           raw_ostream &OS) {
  printAfter(T->getElementType(), OS);
}

void TypePrinter::printDependentSizedMatrixBefore(
    const DependentSizedMatrixType *T, raw_ostream &OS) {
  printBefore(T->getElementType(), OS);
  OS << " __attribute__((matrix_type(";
  if (T->getRowExpr()) {
    T->getRowExpr()->printPretty(OS, nullptr, Policy);
  }
  OS << ", ";
  if (T->getColumnExpr()) {
    T->getColumnExpr()->printPretty(OS, nullptr, Policy);
  }
  OS << ")))";
}

void TypePrinter::printDependentSizedMatrixAfter(
    const DependentSizedMatrixType *T, raw_ostream &OS) {
  printAfter(T->getElementType(), OS);
}

void
FunctionProtoType::printExceptionSpecification(raw_ostream &OS,
                                               const PrintingPolicy &Policy)
                                                                         const {
  if (hasDynamicExceptionSpec()) {
    OS << " throw(";
    if (getExceptionSpecType() == EST_MSAny)
      OS << "...";
    else
      for (unsigned I = 0, N = getNumExceptions(); I != N; ++I) {
        if (I)
          OS << ", ";

        OS << getExceptionType(I).stream(Policy);
      }
    OS << ')';
  } else if (EST_NoThrow == getExceptionSpecType()) {
    OS << " __attribute__((nothrow))";
  } else if (isNoexceptExceptionSpec(getExceptionSpecType())) {
    OS << " noexcept";
    // FIXME:Is it useful to print out the expression for a non-dependent
    // noexcept specification?
    if (isComputedNoexcept(getExceptionSpecType())) {
      OS << '(';
      if (getNoexceptExpr())
        getNoexceptExpr()->printPretty(OS, nullptr, Policy);
      OS << ')';
    }
  }
}

void TypePrinter::printFunctionProtoBefore(const FunctionProtoType *T,
                                           raw_ostream &OS) {
  if (T->hasTrailingReturn()) {
    OS << "auto ";
    if (!HasEmptyPlaceHolder)
      OS << '(';
  } else {
    // If needed for precedence reasons, wrap the inner part in grouping parens.
    SaveAndRestore PrevPHIsEmpty(HasEmptyPlaceHolder, false);
    printBefore(T->getReturnType(), OS);
    if (!PrevPHIsEmpty.get())
      OS << '(';
  }
}

StringRef clang::getParameterABISpelling(ParameterABI ABI) {
  switch (ABI) {
  case ParameterABI::Ordinary:
    llvm_unreachable("asking for spelling of ordinary parameter ABI");
  case ParameterABI::SwiftContext:
    return "swift_context";
  case ParameterABI::SwiftAsyncContext:
    return "swift_async_context";
  case ParameterABI::SwiftErrorResult:
    return "swift_error_result";
  case ParameterABI::SwiftIndirectResult:
    return "swift_indirect_result";
  }
  llvm_unreachable("bad parameter ABI kind");
}

void TypePrinter::printFunctionProtoAfter(const FunctionProtoType *T,
                                          raw_ostream &OS) {
  // If needed for precedence reasons, wrap the inner part in grouping parens.
  if (!HasEmptyPlaceHolder)
    OS << ')';
  SaveAndRestore NonEmptyPH(HasEmptyPlaceHolder, false);

  OS << '(';
  {
    ParamPolicyRAII ParamPolicy(Policy);
    for (unsigned i = 0, e = T->getNumParams(); i != e; ++i) {
      if (i) OS << ", ";

      auto EPI = T->getExtParameterInfo(i);
      if (EPI.isConsumed()) OS << "__attribute__((ns_consumed)) ";
      if (EPI.isNoEscape())
        OS << "__attribute__((noescape)) ";
      auto ABI = EPI.getABI();
      if (ABI != ParameterABI::Ordinary)
        OS << "__attribute__((" << getParameterABISpelling(ABI) << ")) ";

      print(T->getParamType(i), OS, StringRef());
    }
  }

  if (T->isVariadic()) {
    if (T->getNumParams())
      OS << ", ";
    OS << "...";
  } else if (T->getNumParams() == 0 && Policy.UseVoidForZeroParams) {
    // Do not emit int() if we have a proto, emit 'int(void)'.
    OS << "void";
  }

  OS << ')';

  FunctionType::ExtInfo Info = T->getExtInfo();
  unsigned SMEBits = T->getAArch64SMEAttributes();

  if (SMEBits & FunctionType::SME_PStateSMCompatibleMask)
    OS << " __arm_streaming_compatible";
  if (SMEBits & FunctionType::SME_PStateSMEnabledMask)
    OS << " __arm_streaming";
  if (FunctionType::getArmZAState(SMEBits) == FunctionType::ARM_Preserves)
    OS << " __arm_preserves(\"za\")";
  if (FunctionType::getArmZAState(SMEBits) == FunctionType::ARM_In)
    OS << " __arm_in(\"za\")";
  if (FunctionType::getArmZAState(SMEBits) == FunctionType::ARM_Out)
    OS << " __arm_out(\"za\")";
  if (FunctionType::getArmZAState(SMEBits) == FunctionType::ARM_InOut)
    OS << " __arm_inout(\"za\")";
  if (FunctionType::getArmZT0State(SMEBits) == FunctionType::ARM_Preserves)
    OS << " __arm_preserves(\"zt0\")";
  if (FunctionType::getArmZT0State(SMEBits) == FunctionType::ARM_In)
    OS << " __arm_in(\"zt0\")";
  if (FunctionType::getArmZT0State(SMEBits) == FunctionType::ARM_Out)
    OS << " __arm_out(\"zt0\")";
  if (FunctionType::getArmZT0State(SMEBits) == FunctionType::ARM_InOut)
    OS << " __arm_inout(\"zt0\")";

  printFunctionAfter(Info, OS);

  if (!T->getMethodQuals().empty())
    OS << " " << T->getMethodQuals().getAsString();

  switch (T->getRefQualifier()) {
  case RQ_None:
    break;

  case RQ_LValue:
    OS << " &";
    break;

  case RQ_RValue:
    OS << " &&";
    break;
  }
  T->printExceptionSpecification(OS, Policy);

  const FunctionEffectsRef FX = T->getFunctionEffects();
  for (const auto &CFE : FX) {
    OS << " __attribute__((" << CFE.Effect.name();
    if (const Expr *E = CFE.Cond.getCondition()) {
      OS << '(';
      E->printPretty(OS, nullptr, Policy);
      OS << ')';
    }
    OS << "))";
  }

  if (T->hasTrailingReturn()) {
    OS << " -> ";
    print(T->getReturnType(), OS, StringRef());
  } else
    printAfter(T->getReturnType(), OS);
}

void TypePrinter::printFunctionAfter(const FunctionType::ExtInfo &Info,
                                     raw_ostream &OS) {
  if (!InsideCCAttribute) {
    switch (Info.getCC()) {
    case CC_C:
      // The C calling convention is the default on the vast majority of platforms
      // we support.  If the user wrote it explicitly, it will usually be printed
      // while traversing the AttributedType.  If the type has been desugared, let
      // the canonical spelling be the implicit calling convention.
      // FIXME: It would be better to be explicit in certain contexts, such as a
      // cdecl function typedef used to declare a member function with the
      // Microsoft C++ ABI.
      break;
    case CC_X86StdCall:
      OS << " __attribute__((stdcall))";
      break;
    case CC_X86FastCall:
      OS << " __attribute__((fastcall))";
      break;
    case CC_X86ThisCall:
      OS << " __attribute__((thiscall))";
      break;
    case CC_X86VectorCall:
      OS << " __attribute__((vectorcall))";
      break;
    case CC_X86Pascal:
      OS << " __attribute__((pascal))";
      break;
    case CC_AAPCS:
      OS << " __attribute__((pcs(\"aapcs\")))";
      break;
    case CC_AAPCS_VFP:
      OS << " __attribute__((pcs(\"aapcs-vfp\")))";
      break;
    case CC_AArch64VectorCall:
      OS << "__attribute__((aarch64_vector_pcs))";
      break;
    case CC_AArch64SVEPCS:
      OS << "__attribute__((aarch64_sve_pcs))";
      break;
    case CC_AMDGPUKernelCall:
      OS << "__attribute__((amdgpu_kernel))";
      break;
    case CC_IntelOclBicc:
      OS << " __attribute__((intel_ocl_bicc))";
      break;
    case CC_Win64:
      OS << " __attribute__((ms_abi))";
      break;
    case CC_X86_64SysV:
      OS << " __attribute__((sysv_abi))";
      break;
    case CC_X86RegCall:
      OS << " __attribute__((regcall))";
      break;
    case CC_SpirFunction:
    case CC_OpenCLKernel:
      // Do nothing. These CCs are not available as attributes.
      break;
    case CC_Swift:
      OS << " __attribute__((swiftcall))";
      break;
    case CC_SwiftAsync:
      OS << "__attribute__((swiftasynccall))";
      break;
    case CC_PreserveMost:
      OS << " __attribute__((preserve_most))";
      break;
    case CC_PreserveAll:
      OS << " __attribute__((preserve_all))";
      break;
    case CC_M68kRTD:
      OS << " __attribute__((m68k_rtd))";
      break;
    case CC_PreserveNone:
      OS << " __attribute__((preserve_none))";
      break;
    case CC_RISCVVectorCall:
      OS << "__attribute__((riscv_vector_cc))";
      break;
    }
  }

  if (Info.getNoReturn())
    OS << " __attribute__((noreturn))";
  if (Info.getCmseNSCall())
    OS << " __attribute__((cmse_nonsecure_call))";
  if (Info.getProducesResult())
    OS << " __attribute__((ns_returns_retained))";
  if (Info.getRegParm())
    OS << " __attribute__((regparm ("
       << Info.getRegParm() << ")))";
  if (Info.getNoCallerSavedRegs())
    OS << " __attribute__((no_caller_saved_registers))";
  if (Info.getNoCfCheck())
    OS << " __attribute__((nocf_check))";
}

void TypePrinter::printFunctionNoProtoBefore(const FunctionNoProtoType *T,
                                             raw_ostream &OS) {
  // If needed for precedence reasons, wrap the inner part in grouping parens.
  SaveAndRestore PrevPHIsEmpty(HasEmptyPlaceHolder, false);
  printBefore(T->getReturnType(), OS);
  if (!PrevPHIsEmpty.get())
    OS << '(';
}

void TypePrinter::printFunctionNoProtoAfter(const FunctionNoProtoType *T,
                                            raw_ostream &OS) {
  // If needed for precedence reasons, wrap the inner part in grouping parens.
  if (!HasEmptyPlaceHolder)
    OS << ')';
  SaveAndRestore NonEmptyPH(HasEmptyPlaceHolder, false);

  OS << "()";
  printFunctionAfter(T->getExtInfo(), OS);
  printAfter(T->getReturnType(), OS);
}

void TypePrinter::printTypeSpec(NamedDecl *D, raw_ostream &OS) {

  // Compute the full nested-name-specifier for this type.
  // In C, this will always be empty except when the type
  // being printed is anonymous within other Record.
  if (!Policy.SuppressScope)
    AppendScope(D->getDeclContext(), OS, D->getDeclName());

  IdentifierInfo *II = D->getIdentifier();
  OS << II->getName();
  spaceBeforePlaceHolder(OS);
}

void TypePrinter::printUnresolvedUsingBefore(const UnresolvedUsingType *T,
                                             raw_ostream &OS) {
  printTypeSpec(T->getDecl(), OS);
}

void TypePrinter::printUnresolvedUsingAfter(const UnresolvedUsingType *T,
                                            raw_ostream &OS) {}

void TypePrinter::printUsingBefore(const UsingType *T, raw_ostream &OS) {
  // After `namespace b { using a::X }`, is the type X within B a::X or b::X?
  //
  // - b::X is more formally correct given the UsingType model
  // - b::X makes sense if "re-exporting" a symbol in a new namespace
  // - a::X makes sense if "importing" a symbol for convenience
  //
  // The "importing" use seems much more common, so we print a::X.
  // This could be a policy option, but the right choice seems to rest more
  // with the intent of the code than the caller.
  printTypeSpec(T->getFoundDecl()->getUnderlyingDecl(), OS);
}

void TypePrinter::printUsingAfter(const UsingType *T, raw_ostream &OS) {}

void TypePrinter::printTypedefBefore(const TypedefType *T, raw_ostream &OS) {
  printTypeSpec(T->getDecl(), OS);
}

void TypePrinter::printMacroQualifiedBefore(const MacroQualifiedType *T,
                                            raw_ostream &OS) {
  StringRef MacroName = T->getMacroIdentifier()->getName();
  OS << MacroName << " ";

  // Since this type is meant to print the macro instead of the whole attribute,
  // we trim any attributes and go directly to the original modified type.
  printBefore(T->getModifiedType(), OS);
}

void TypePrinter::printMacroQualifiedAfter(const MacroQualifiedType *T,
                                           raw_ostream &OS) {
  printAfter(T->getModifiedType(), OS);
}

void TypePrinter::printTypedefAfter(const TypedefType *T, raw_ostream &OS) {}

void TypePrinter::printTypeOfExprBefore(const TypeOfExprType *T,
                                        raw_ostream &OS) {
  OS << (T->getKind() == TypeOfKind::Unqualified ? "typeof_unqual "
                                                 : "typeof ");
  if (T->getUnderlyingExpr())
    T->getUnderlyingExpr()->printPretty(OS, nullptr, Policy);
  spaceBeforePlaceHolder(OS);
}

void TypePrinter::printTypeOfExprAfter(const TypeOfExprType *T,
                                       raw_ostream &OS) {}

void TypePrinter::printTypeOfBefore(const TypeOfType *T, raw_ostream &OS) {
  OS << (T->getKind() == TypeOfKind::Unqualified ? "typeof_unqual("
                                                 : "typeof(");
  print(T->getUnmodifiedType(), OS, StringRef());
  OS << ')';
  spaceBeforePlaceHolder(OS);
}

void TypePrinter::printTypeOfAfter(const TypeOfType *T, raw_ostream &OS) {}

void TypePrinter::printDecltypeBefore(const DecltypeType *T, raw_ostream &OS) {
  OS << "decltype(";
  if (T->getUnderlyingExpr())
    T->getUnderlyingExpr()->printPretty(OS, nullptr, Policy);
  OS << ')';
  spaceBeforePlaceHolder(OS);
}

void TypePrinter::printPackIndexingBefore(const PackIndexingType *T,
                                          raw_ostream &OS) {
  if (T->hasSelectedType()) {
    OS << T->getSelectedType();
  } else {
    OS << T->getPattern() << "...[";
    T->getIndexExpr()->printPretty(OS, nullptr, Policy);
    OS << "]";
  }
  spaceBeforePlaceHolder(OS);
}

void TypePrinter::printPackIndexingAfter(const PackIndexingType *T,
                                         raw_ostream &OS) {}

void TypePrinter::printDecltypeAfter(const DecltypeType *T, raw_ostream &OS) {}

void TypePrinter::printUnaryTransformBefore(const UnaryTransformType *T,
                                            raw_ostream &OS) {
  IncludeStrongLifetimeRAII Strong(Policy);

  static llvm::DenseMap<int, const char *> Transformation = {{
#define TRANSFORM_TYPE_TRAIT_DEF(Enum, Trait)                                  \
  {UnaryTransformType::Enum, "__" #Trait},
#include "clang/Basic/TransformTypeTraits.def"
  }};
  OS << Transformation[T->getUTTKind()] << '(';
  print(T->getBaseType(), OS, StringRef());
  OS << ')';
  spaceBeforePlaceHolder(OS);
}

void TypePrinter::printUnaryTransformAfter(const UnaryTransformType *T,
                                           raw_ostream &OS) {}

void TypePrinter::printAutoBefore(const AutoType *T, raw_ostream &OS) {
  // If the type has been deduced, do not print 'auto'.
  if (!T->getDeducedType().isNull()) {
    printBefore(T->getDeducedType(), OS);
  } else {
    if (T->isConstrained()) {
      // FIXME: Track a TypeConstraint as type sugar, so that we can print the
      // type as it was written.
      T->getTypeConstraintConcept()->getDeclName().print(OS, Policy);
      auto Args = T->getTypeConstraintArguments();
      if (!Args.empty())
        printTemplateArgumentList(
            OS, Args, Policy,
            T->getTypeConstraintConcept()->getTemplateParameters());
      OS << ' ';
    }
    switch (T->getKeyword()) {
    case AutoTypeKeyword::Auto: OS << "auto"; break;
    case AutoTypeKeyword::DecltypeAuto: OS << "decltype(auto)"; break;
    case AutoTypeKeyword::GNUAutoType: OS << "__auto_type"; break;
    }
    spaceBeforePlaceHolder(OS);
  }
}

void TypePrinter::printAutoAfter(const AutoType *T, raw_ostream &OS) {
  // If the type has been deduced, do not print 'auto'.
  if (!T->getDeducedType().isNull())
    printAfter(T->getDeducedType(), OS);
}

void TypePrinter::printDeducedTemplateSpecializationBefore(
    const DeducedTemplateSpecializationType *T, raw_ostream &OS) {
  // If the type has been deduced, print the deduced type.
  if (!T->getDeducedType().isNull()) {
    printBefore(T->getDeducedType(), OS);
  } else {
    IncludeStrongLifetimeRAII Strong(Policy);
    T->getTemplateName().print(OS, Policy);
    spaceBeforePlaceHolder(OS);
  }
}

void TypePrinter::printDeducedTemplateSpecializationAfter(
    const DeducedTemplateSpecializationType *T, raw_ostream &OS) {
  // If the type has been deduced, print the deduced type.
  if (!T->getDeducedType().isNull())
    printAfter(T->getDeducedType(), OS);
}

void TypePrinter::printAtomicBefore(const AtomicType *T, raw_ostream &OS) {
  IncludeStrongLifetimeRAII Strong(Policy);

  OS << "_Atomic(";
  print(T->getValueType(), OS, StringRef());
  OS << ')';
  spaceBeforePlaceHolder(OS);
}

void TypePrinter::printAtomicAfter(const AtomicType *T, raw_ostream &OS) {}

void TypePrinter::printPipeBefore(const PipeType *T, raw_ostream &OS) {
  IncludeStrongLifetimeRAII Strong(Policy);

  if (T->isReadOnly())
    OS << "read_only ";
  else
    OS << "write_only ";
  OS << "pipe ";
  print(T->getElementType(), OS, StringRef());
  spaceBeforePlaceHolder(OS);
}

void TypePrinter::printPipeAfter(const PipeType *T, raw_ostream &OS) {}

void TypePrinter::printBitIntBefore(const BitIntType *T, raw_ostream &OS) {
  if (T->isUnsigned())
    OS << "unsigned ";
  OS << "_BitInt(" << T->getNumBits() << ")";
  spaceBeforePlaceHolder(OS);
}

void TypePrinter::printBitIntAfter(const BitIntType *T, raw_ostream &OS) {}

void TypePrinter::printDependentBitIntBefore(const DependentBitIntType *T,
                                             raw_ostream &OS) {
  if (T->isUnsigned())
    OS << "unsigned ";
  OS << "_BitInt(";
  T->getNumBitsExpr()->printPretty(OS, nullptr, Policy);
  OS << ")";
  spaceBeforePlaceHolder(OS);
}

void TypePrinter::printDependentBitIntAfter(const DependentBitIntType *T,
                                            raw_ostream &OS) {}

/// Appends the given scope to the end of a string.
void TypePrinter::AppendScope(DeclContext *DC, raw_ostream &OS,
                              DeclarationName NameInScope) {
  if (DC->isTranslationUnit())
    return;

  // FIXME: Consider replacing this with NamedDecl::printNestedNameSpecifier,
  // which can also print names for function and method scopes.
  if (DC->isFunctionOrMethod())
    return;

  if (Policy.Callbacks && Policy.Callbacks->isScopeVisible(DC))
    return;

  if (const auto *NS = dyn_cast<NamespaceDecl>(DC)) {
    if (Policy.SuppressUnwrittenScope && NS->isAnonymousNamespace())
      return AppendScope(DC->getParent(), OS, NameInScope);

    // Only suppress an inline namespace if the name has the same lookup
    // results in the enclosing namespace.
    if (Policy.SuppressInlineNamespace && NS->isInline() && NameInScope &&
        NS->isRedundantInlineQualifierFor(NameInScope))
      return AppendScope(DC->getParent(), OS, NameInScope);

    AppendScope(DC->getParent(), OS, NS->getDeclName());
    if (NS->getIdentifier())
      OS << NS->getName() << "::";
    else
      OS << "(anonymous namespace)::";
  } else if (const auto *Spec = dyn_cast<ClassTemplateSpecializationDecl>(DC)) {
    AppendScope(DC->getParent(), OS, Spec->getDeclName());
    IncludeStrongLifetimeRAII Strong(Policy);
    OS << Spec->getIdentifier()->getName();
    const TemplateArgumentList &TemplateArgs = Spec->getTemplateArgs();
    printTemplateArgumentList(
        OS, TemplateArgs.asArray(), Policy,
        Spec->getSpecializedTemplate()->getTemplateParameters());
    OS << "::";
  } else if (const auto *Tag = dyn_cast<TagDecl>(DC)) {
    AppendScope(DC->getParent(), OS, Tag->getDeclName());
    if (TypedefNameDecl *Typedef = Tag->getTypedefNameForAnonDecl())
      OS << Typedef->getIdentifier()->getName() << "::";
    else if (Tag->getIdentifier())
      OS << Tag->getIdentifier()->getName() << "::";
    else
      return;
  } else {
    AppendScope(DC->getParent(), OS, NameInScope);
  }
}

void TypePrinter::printTag(TagDecl *D, raw_ostream &OS) {
  if (Policy.IncludeTagDefinition) {
    PrintingPolicy SubPolicy = Policy;
    SubPolicy.IncludeTagDefinition = false;
    D->print(OS, SubPolicy, Indentation);
    spaceBeforePlaceHolder(OS);
    return;
  }

  bool HasKindDecoration = false;

  // We don't print tags unless this is an elaborated type.
  // In C, we just assume every RecordType is an elaborated type.
  if (!Policy.SuppressTagKeyword && !D->getTypedefNameForAnonDecl()) {
    HasKindDecoration = true;
    OS << D->getKindName();
    OS << ' ';
  }

  // Compute the full nested-name-specifier for this type.
  // In C, this will always be empty except when the type
  // being printed is anonymous within other Record.
  if (!Policy.SuppressScope)
    AppendScope(D->getDeclContext(), OS, D->getDeclName());

  if (const IdentifierInfo *II = D->getIdentifier())
    OS << II->getName();
  else if (TypedefNameDecl *Typedef = D->getTypedefNameForAnonDecl()) {
    assert(Typedef->getIdentifier() && "Typedef without identifier?");
    OS << Typedef->getIdentifier()->getName();
  } else {
    // Make an unambiguous representation for anonymous types, e.g.
    //   (anonymous enum at /usr/include/string.h:120:9)
    OS << (Policy.MSVCFormatting ? '`' : '(');

    if (isa<CXXRecordDecl>(D) && cast<CXXRecordDecl>(D)->isLambda()) {
      OS << "lambda";
      HasKindDecoration = true;
    } else if ((isa<RecordDecl>(D) && cast<RecordDecl>(D)->isAnonymousStructOrUnion())) {
      OS << "anonymous";
    } else {
      OS << "unnamed";
    }

    if (Policy.AnonymousTagLocations) {
      // Suppress the redundant tag keyword if we just printed one.
      // We don't have to worry about ElaboratedTypes here because you can't
      // refer to an anonymous type with one.
      if (!HasKindDecoration)
        OS << " " << D->getKindName();

      PresumedLoc PLoc = D->getASTContext().getSourceManager().getPresumedLoc(
          D->getLocation());
      if (PLoc.isValid()) {
        OS << " at ";
        StringRef File = PLoc.getFilename();
        llvm::SmallString<1024> WrittenFile(File);
        if (auto *Callbacks = Policy.Callbacks)
          WrittenFile = Callbacks->remapPath(File);
        // Fix inconsistent path separator created by
        // clang::DirectoryLookup::LookupFile when the file path is relative
        // path.
        llvm::sys::path::Style Style =
            llvm::sys::path::is_absolute(WrittenFile)
                ? llvm::sys::path::Style::native
                : (Policy.MSVCFormatting
                       ? llvm::sys::path::Style::windows_backslash
                       : llvm::sys::path::Style::posix);
        llvm::sys::path::native(WrittenFile, Style);
        OS << WrittenFile << ':' << PLoc.getLine() << ':' << PLoc.getColumn();
      }
    }

    OS << (Policy.MSVCFormatting ? '\'' : ')');
  }

  // If this is a class template specialization, print the template
  // arguments.
  if (auto *S = dyn_cast<ClassTemplateSpecializationDecl>(D)) {
    const TemplateParameterList *TParams =
        S->getSpecializedTemplate()->getTemplateParameters();
    const ASTTemplateArgumentListInfo *TArgAsWritten =
        S->getTemplateArgsAsWritten();
    IncludeStrongLifetimeRAII Strong(Policy);
    if (TArgAsWritten && !Policy.PrintCanonicalTypes)
      printTemplateArgumentList(OS, TArgAsWritten->arguments(), Policy,
                                TParams);
    else
      printTemplateArgumentList(OS, S->getTemplateArgs().asArray(), Policy,
                                TParams);
  }

  spaceBeforePlaceHolder(OS);
}

void TypePrinter::printRecordBefore(const RecordType *T, raw_ostream &OS) {
  // Print the preferred name if we have one for this type.
  if (Policy.UsePreferredNames) {
    for (const auto *PNA : T->getDecl()->specific_attrs<PreferredNameAttr>()) {
      if (!declaresSameEntity(PNA->getTypedefType()->getAsCXXRecordDecl(),
                              T->getDecl()))
        continue;
      // Find the outermost typedef or alias template.
      QualType T = PNA->getTypedefType();
      while (true) {
        if (auto *TT = dyn_cast<TypedefType>(T))
          return printTypeSpec(TT->getDecl(), OS);
        if (auto *TST = dyn_cast<TemplateSpecializationType>(T))
          return printTemplateId(TST, OS, /*FullyQualify=*/true);
        T = T->getLocallyUnqualifiedSingleStepDesugaredType();
      }
    }
  }

  printTag(T->getDecl(), OS);
}

void TypePrinter::printRecordAfter(const RecordType *T, raw_ostream &OS) {}

void TypePrinter::printEnumBefore(const EnumType *T, raw_ostream &OS) {
  printTag(T->getDecl(), OS);
}

void TypePrinter::printEnumAfter(const EnumType *T, raw_ostream &OS) {}

void TypePrinter::printTemplateTypeParmBefore(const TemplateTypeParmType *T,
                                              raw_ostream &OS) {
  TemplateTypeParmDecl *D = T->getDecl();
  if (D && D->isImplicit()) {
    if (auto *TC = D->getTypeConstraint()) {
      TC->print(OS, Policy);
      OS << ' ';
    }
    OS << "auto";
  } else if (IdentifierInfo *Id = T->getIdentifier())
    OS << (Policy.CleanUglifiedParameters ? Id->deuglifiedName()
                                          : Id->getName());
  else
    OS << "type-parameter-" << T->getDepth() << '-' << T->getIndex();

  spaceBeforePlaceHolder(OS);
}

void TypePrinter::printTemplateTypeParmAfter(const TemplateTypeParmType *T,
                                             raw_ostream &OS) {}

void TypePrinter::printSubstTemplateTypeParmBefore(
                                             const SubstTemplateTypeParmType *T,
                                             raw_ostream &OS) {
  IncludeStrongLifetimeRAII Strong(Policy);
  printBefore(T->getReplacementType(), OS);
}

void TypePrinter::printSubstTemplateTypeParmAfter(
                                             const SubstTemplateTypeParmType *T,
                                             raw_ostream &OS) {
  IncludeStrongLifetimeRAII Strong(Policy);
  printAfter(T->getReplacementType(), OS);
}

void TypePrinter::printSubstTemplateTypeParmPackBefore(
                                        const SubstTemplateTypeParmPackType *T,
                                        raw_ostream &OS) {
  IncludeStrongLifetimeRAII Strong(Policy);
  if (const TemplateTypeParmDecl *D = T->getReplacedParameter()) {
    if (D && D->isImplicit()) {
      if (auto *TC = D->getTypeConstraint()) {
        TC->print(OS, Policy);
        OS << ' ';
      }
      OS << "auto";
    } else if (IdentifierInfo *Id = D->getIdentifier())
      OS << (Policy.CleanUglifiedParameters ? Id->deuglifiedName()
                                            : Id->getName());
    else
      OS << "type-parameter-" << D->getDepth() << '-' << D->getIndex();

    spaceBeforePlaceHolder(OS);
  }
}

void TypePrinter::printSubstTemplateTypeParmPackAfter(
                                        const SubstTemplateTypeParmPackType *T,
                                        raw_ostream &OS) {
  IncludeStrongLifetimeRAII Strong(Policy);
}

void TypePrinter::printTemplateId(const TemplateSpecializationType *T,
                                  raw_ostream &OS, bool FullyQualify) {
  IncludeStrongLifetimeRAII Strong(Policy);

  TemplateDecl *TD = T->getTemplateName().getAsTemplateDecl();
  // FIXME: Null TD never exercised in test suite.
  if (FullyQualify && TD) {
    if (!Policy.SuppressScope)
      AppendScope(TD->getDeclContext(), OS, TD->getDeclName());

    OS << TD->getName();
  } else {
    T->getTemplateName().print(OS, Policy, TemplateName::Qualified::None);
  }

  DefaultTemplateArgsPolicyRAII TemplateArgs(Policy);
  const TemplateParameterList *TPL = TD ? TD->getTemplateParameters() : nullptr;
  printTemplateArgumentList(OS, T->template_arguments(), Policy, TPL);
  spaceBeforePlaceHolder(OS);
}

void TypePrinter::printTemplateSpecializationBefore(
                                            const TemplateSpecializationType *T,
                                            raw_ostream &OS) {
  printTemplateId(T, OS, Policy.FullyQualifiedName);
}

void TypePrinter::printTemplateSpecializationAfter(
                                            const TemplateSpecializationType *T,
                                            raw_ostream &OS) {}

void TypePrinter::printInjectedClassNameBefore(const InjectedClassNameType *T,
                                               raw_ostream &OS) {
  if (Policy.PrintInjectedClassNameWithArguments)
    return printTemplateSpecializationBefore(T->getInjectedTST(), OS);

  IncludeStrongLifetimeRAII Strong(Policy);
  T->getTemplateName().print(OS, Policy);
  spaceBeforePlaceHolder(OS);
}

void TypePrinter::printInjectedClassNameAfter(const InjectedClassNameType *T,
                                               raw_ostream &OS) {}

void TypePrinter::printElaboratedBefore(const ElaboratedType *T,
                                        raw_ostream &OS) {
  if (Policy.IncludeTagDefinition && T->getOwnedTagDecl()) {
    TagDecl *OwnedTagDecl = T->getOwnedTagDecl();
    assert(OwnedTagDecl->getTypeForDecl() == T->getNamedType().getTypePtr() &&
           "OwnedTagDecl expected to be a declaration for the type");
    PrintingPolicy SubPolicy = Policy;
    SubPolicy.IncludeTagDefinition = false;
    OwnedTagDecl->print(OS, SubPolicy, Indentation);
    spaceBeforePlaceHolder(OS);
    return;
  }

  if (Policy.SuppressElaboration) {
    printBefore(T->getNamedType(), OS);
    return;
  }

  // The tag definition will take care of these.
  if (!Policy.IncludeTagDefinition)
  {
    OS << TypeWithKeyword::getKeywordName(T->getKeyword());
    if (T->getKeyword() != ElaboratedTypeKeyword::None)
      OS << " ";
    NestedNameSpecifier *Qualifier = T->getQualifier();
    if (!Policy.SuppressTagKeyword && Policy.SuppressScope &&
        !Policy.SuppressUnwrittenScope) {
      bool OldTagKeyword = Policy.SuppressTagKeyword;
      bool OldSupressScope = Policy.SuppressScope;
      Policy.SuppressTagKeyword = true;
      Policy.SuppressScope = false;
      printBefore(T->getNamedType(), OS);
      Policy.SuppressTagKeyword = OldTagKeyword;
      Policy.SuppressScope = OldSupressScope;
      return;
    }
    if (Qualifier)
      Qualifier->print(OS, Policy);
  }

  ElaboratedTypePolicyRAII PolicyRAII(Policy);
  printBefore(T->getNamedType(), OS);
}

void TypePrinter::printElaboratedAfter(const ElaboratedType *T,
                                        raw_ostream &OS) {
  if (Policy.IncludeTagDefinition && T->getOwnedTagDecl())
    return;

  if (Policy.SuppressElaboration) {
    printAfter(T->getNamedType(), OS);
    return;
  }

  ElaboratedTypePolicyRAII PolicyRAII(Policy);
  printAfter(T->getNamedType(), OS);
}

void TypePrinter::printParenBefore(const ParenType *T, raw_ostream &OS) {
  if (!HasEmptyPlaceHolder && !isa<FunctionType>(T->getInnerType())) {
    printBefore(T->getInnerType(), OS);
    OS << '(';
  } else
    printBefore(T->getInnerType(), OS);
}

void TypePrinter::printParenAfter(const ParenType *T, raw_ostream &OS) {
  if (!HasEmptyPlaceHolder && !isa<FunctionType>(T->getInnerType())) {
    OS << ')';
    printAfter(T->getInnerType(), OS);
  } else
    printAfter(T->getInnerType(), OS);
}

void TypePrinter::printDependentNameBefore(const DependentNameType *T,
                                           raw_ostream &OS) {
  OS << TypeWithKeyword::getKeywordName(T->getKeyword());
  if (T->getKeyword() != ElaboratedTypeKeyword::None)
    OS << " ";

  T->getQualifier()->print(OS, Policy);

  OS << T->getIdentifier()->getName();
  spaceBeforePlaceHolder(OS);
}

void TypePrinter::printDependentNameAfter(const DependentNameType *T,
                                          raw_ostream &OS) {}

void TypePrinter::printDependentTemplateSpecializationBefore(
        const DependentTemplateSpecializationType *T, raw_ostream &OS) {
  IncludeStrongLifetimeRAII Strong(Policy);

  OS << TypeWithKeyword::getKeywordName(T->getKeyword());
  if (T->getKeyword() != ElaboratedTypeKeyword::None)
    OS << " ";

  if (T->getQualifier())
    T->getQualifier()->print(OS, Policy);
  OS << "template " << T->getIdentifier()->getName();
  printTemplateArgumentList(OS, T->template_arguments(), Policy);
  spaceBeforePlaceHolder(OS);
}

void TypePrinter::printDependentTemplateSpecializationAfter(
        const DependentTemplateSpecializationType *T, raw_ostream &OS) {}

void TypePrinter::printPackExpansionBefore(const PackExpansionType *T,
                                           raw_ostream &OS) {
  printBefore(T->getPattern(), OS);
}

void TypePrinter::printPackExpansionAfter(const PackExpansionType *T,
                                          raw_ostream &OS) {
  printAfter(T->getPattern(), OS);
  OS << "...";
}

static void printCountAttributedImpl(const CountAttributedType *T,
                                     raw_ostream &OS,
                                     const PrintingPolicy &Policy) {
  OS << ' ';
  if (T->isCountInBytes() && T->isOrNull())
    OS << "__sized_by_or_null(";
  else if (T->isCountInBytes())
    OS << "__sized_by(";
  else if (T->isOrNull())
    OS << "__counted_by_or_null(";
  else
    OS << "__counted_by(";
  if (T->getCountExpr())
    T->getCountExpr()->printPretty(OS, nullptr, Policy);
  OS << ')';
}

void TypePrinter::printCountAttributedBefore(const CountAttributedType *T,
                                             raw_ostream &OS) {
  printBefore(T->desugar(), OS);
  if (!T->isArrayType())
    printCountAttributedImpl(T, OS, Policy);
}

void TypePrinter::printCountAttributedAfter(const CountAttributedType *T,
                                            raw_ostream &OS) {
  printAfter(T->desugar(), OS);
  if (T->isArrayType())
    printCountAttributedImpl(T, OS, Policy);
}

void TypePrinter::printAttributedBefore(const AttributedType *T,
                                        raw_ostream &OS) {
  // FIXME: Generate this with TableGen.

  // Prefer the macro forms of the GC and ownership qualifiers.
  if (T->getAttrKind() == attr::ObjCGC ||
      T->getAttrKind() == attr::ObjCOwnership)
    return printBefore(T->getEquivalentType(), OS);

  if (T->getAttrKind() == attr::ObjCKindOf)
    OS << "__kindof ";

  if (T->getAttrKind() == attr::AddressSpace)
    printBefore(T->getEquivalentType(), OS);
  else
    printBefore(T->getModifiedType(), OS);

  if (T->isMSTypeSpec()) {
    switch (T->getAttrKind()) {
    default: return;
    case attr::Ptr32: OS << " __ptr32"; break;
    case attr::Ptr64: OS << " __ptr64"; break;
    case attr::SPtr: OS << " __sptr"; break;
    case attr::UPtr: OS << " __uptr"; break;
    }
    spaceBeforePlaceHolder(OS);
  }

  if (T->isWebAssemblyFuncrefSpec())
    OS << "__funcref";

  // Print nullability type specifiers.
  if (T->getImmediateNullability()) {
    if (T->getAttrKind() == attr::TypeNonNull)
      OS << " _Nonnull";
    else if (T->getAttrKind() == attr::TypeNullable)
      OS << " _Nullable";
    else if (T->getAttrKind() == attr::TypeNullUnspecified)
      OS << " _Null_unspecified";
    else if (T->getAttrKind() == attr::TypeNullableResult)
      OS << " _Nullable_result";
    else
      llvm_unreachable("unhandled nullability");
    spaceBeforePlaceHolder(OS);
  }
}

void TypePrinter::printAttributedAfter(const AttributedType *T,
                                       raw_ostream &OS) {
  // FIXME: Generate this with TableGen.

  // Prefer the macro forms of the GC and ownership qualifiers.
  if (T->getAttrKind() == attr::ObjCGC ||
      T->getAttrKind() == attr::ObjCOwnership)
    return printAfter(T->getEquivalentType(), OS);

  // If this is a calling convention attribute, don't print the implicit CC from
  // the modified type.
  SaveAndRestore MaybeSuppressCC(InsideCCAttribute, T->isCallingConv());

  printAfter(T->getModifiedType(), OS);

  // Some attributes are printed as qualifiers before the type, so we have
  // nothing left to do.
  if (T->getAttrKind() == attr::ObjCKindOf || T->isMSTypeSpec() ||
      T->getImmediateNullability() || T->isWebAssemblyFuncrefSpec())
    return;

  // Don't print the inert __unsafe_unretained attribute at all.
  if (T->getAttrKind() == attr::ObjCInertUnsafeUnretained)
    return;

  // Don't print ns_returns_retained unless it had an effect.
  if (T->getAttrKind() == attr::NSReturnsRetained &&
      !T->getEquivalentType()->castAs<FunctionType>()
                             ->getExtInfo().getProducesResult())
    return;

  if (T->getAttrKind() == attr::LifetimeBound) {
    OS << " [[clang::lifetimebound]]";
    return;
  }

  // The printing of the address_space attribute is handled by the qualifier
  // since it is still stored in the qualifier. Return early to prevent printing
  // this twice.
  if (T->getAttrKind() == attr::AddressSpace)
    return;

  if (T->getAttrKind() == attr::AnnotateType) {
    // FIXME: Print the attribute arguments once we have a way to retrieve these
    // here. For the meantime, we just print `[[clang::annotate_type(...)]]`
    // without the arguments so that we know at least that we had _some_
    // annotation on the type.
    OS << " [[clang::annotate_type(...)]]";
    return;
  }

  if (T->getAttrKind() == attr::ArmStreaming) {
    OS << "__arm_streaming";
    return;
  }
  if (T->getAttrKind() == attr::ArmStreamingCompatible) {
    OS << "__arm_streaming_compatible";
    return;
  }

  OS << " __attribute__((";
  switch (T->getAttrKind()) {
#define TYPE_ATTR(NAME)
#define DECL_OR_TYPE_ATTR(NAME)
#define ATTR(NAME) case attr::NAME:
#include "clang/Basic/AttrList.inc"
    llvm_unreachable("non-type attribute attached to type");

  case attr::BTFTypeTag:
    llvm_unreachable("BTFTypeTag attribute handled separately");

  case attr::OpenCLPrivateAddressSpace:
  case attr::OpenCLGlobalAddressSpace:
  case attr::OpenCLGlobalDeviceAddressSpace:
  case attr::OpenCLGlobalHostAddressSpace:
  case attr::OpenCLLocalAddressSpace:
  case attr::OpenCLConstantAddressSpace:
  case attr::OpenCLGenericAddressSpace:
  case attr::HLSLGroupSharedAddressSpace:
    // FIXME: Update printAttributedBefore to print these once we generate
    // AttributedType nodes for them.
    break;

  case attr::CountedBy:
  case attr::CountedByOrNull:
  case attr::SizedBy:
  case attr::SizedByOrNull:
  case attr::LifetimeBound:
  case attr::TypeNonNull:
  case attr::TypeNullable:
  case attr::TypeNullableResult:
  case attr::TypeNullUnspecified:
  case attr::ObjCGC:
  case attr::ObjCInertUnsafeUnretained:
  case attr::ObjCKindOf:
  case attr::ObjCOwnership:
  case attr::Ptr32:
  case attr::Ptr64:
  case attr::SPtr:
  case attr::UPtr:
  case attr::AddressSpace:
  case attr::CmseNSCall:
  case attr::AnnotateType:
  case attr::WebAssemblyFuncref:
  case attr::ArmStreaming:
  case attr::ArmStreamingCompatible:
  case attr::ArmIn:
  case attr::ArmOut:
  case attr::ArmInOut:
  case attr::ArmPreserves:
  case attr::NonBlocking:
  case attr::NonAllocating:
  case attr::Blocking:
  case attr::Allocating:
    llvm_unreachable("This attribute should have been handled already");

  case attr::NSReturnsRetained:
    OS << "ns_returns_retained";
    break;

  // FIXME: When Sema learns to form this AttributedType, avoid printing the
  // attribute again in printFunctionProtoAfter.
  case attr::AnyX86NoCfCheck: OS << "nocf_check"; break;
  case attr::CDecl: OS << "cdecl"; break;
  case attr::FastCall: OS << "fastcall"; break;
  case attr::StdCall: OS << "stdcall"; break;
  case attr::ThisCall: OS << "thiscall"; break;
  case attr::SwiftCall: OS << "swiftcall"; break;
  case attr::SwiftAsyncCall: OS << "swiftasynccall"; break;
  case attr::VectorCall: OS << "vectorcall"; break;
  case attr::Pascal: OS << "pascal"; break;
  case attr::MSABI: OS << "ms_abi"; break;
  case attr::SysVABI: OS << "sysv_abi"; break;
  case attr::RegCall: OS << "regcall"; break;
  case attr::Pcs: {
    OS << "pcs(";
   QualType t = T->getEquivalentType();
   while (!t->isFunctionType())
     t = t->getPointeeType();
   OS << (t->castAs<FunctionType>()->getCallConv() == CC_AAPCS ?
         "\"aapcs\"" : "\"aapcs-vfp\"");
   OS << ')';
   break;
  }
  case attr::AArch64VectorPcs: OS << "aarch64_vector_pcs"; break;
  case attr::AArch64SVEPcs: OS << "aarch64_sve_pcs"; break;
  case attr::AMDGPUKernelCall: OS << "amdgpu_kernel"; break;
  case attr::IntelOclBicc: OS << "inteloclbicc"; break;
  case attr::PreserveMost:
    OS << "preserve_most";
    break;

  case attr::PreserveAll:
    OS << "preserve_all";
    break;
  case attr::M68kRTD:
    OS << "m68k_rtd";
    break;
  case attr::PreserveNone:
    OS << "preserve_none";
    break;
  case attr::RISCVVectorCC:
    OS << "riscv_vector_cc";
    break;
  case attr::NoDeref:
    OS << "noderef";
    break;
  case attr::AcquireHandle:
    OS << "acquire_handle";
    break;
  case attr::ArmMveStrictPolymorphism:
    OS << "__clang_arm_mve_strict_polymorphism";
    break;

  // Nothing to print for this attribute.
  case attr::HLSLParamModifier:
    break;
  }
  OS << "))";
}

void TypePrinter::printBTFTagAttributedBefore(const BTFTagAttributedType *T,
                                              raw_ostream &OS) {
  printBefore(T->getWrappedType(), OS);
  OS << " __attribute__((btf_type_tag(\"" << T->getAttr()->getBTFTypeTag() << "\")))";
}

void TypePrinter::printBTFTagAttributedAfter(const BTFTagAttributedType *T,
                                             raw_ostream &OS) {
  printAfter(T->getWrappedType(), OS);
}

void TypePrinter::printObjCInterfaceBefore(const ObjCInterfaceType *T,
                                           raw_ostream &OS) {
  OS << T->getDecl()->getName();
  spaceBeforePlaceHolder(OS);
}

void TypePrinter::printObjCInterfaceAfter(const ObjCInterfaceType *T,
                                          raw_ostream &OS) {}

void TypePrinter::printObjCTypeParamBefore(const ObjCTypeParamType *T,
                                          raw_ostream &OS) {
  OS << T->getDecl()->getName();
  if (!T->qual_empty()) {
    bool isFirst = true;
    OS << '<';
    for (const auto *I : T->quals()) {
      if (isFirst)
        isFirst = false;
      else
        OS << ',';
      OS << I->getName();
    }
    OS << '>';
  }

  spaceBeforePlaceHolder(OS);
}

void TypePrinter::printObjCTypeParamAfter(const ObjCTypeParamType *T,
                                          raw_ostream &OS) {}

void TypePrinter::printObjCObjectBefore(const ObjCObjectType *T,
                                        raw_ostream &OS) {
  if (T->qual_empty() && T->isUnspecializedAsWritten() &&
      !T->isKindOfTypeAsWritten())
    return printBefore(T->getBaseType(), OS);

  if (T->isKindOfTypeAsWritten())
    OS << "__kindof ";

  print(T->getBaseType(), OS, StringRef());

  if (T->isSpecializedAsWritten()) {
    bool isFirst = true;
    OS << '<';
    for (auto typeArg : T->getTypeArgsAsWritten()) {
      if (isFirst)
        isFirst = false;
      else
        OS << ",";

      print(typeArg, OS, StringRef());
    }
    OS << '>';
  }

  if (!T->qual_empty()) {
    bool isFirst = true;
    OS << '<';
    for (const auto *I : T->quals()) {
      if (isFirst)
        isFirst = false;
      else
        OS << ',';
      OS << I->getName();
    }
    OS << '>';
  }

  spaceBeforePlaceHolder(OS);
}

void TypePrinter::printObjCObjectAfter(const ObjCObjectType *T,
                                        raw_ostream &OS) {
  if (T->qual_empty() && T->isUnspecializedAsWritten() &&
      !T->isKindOfTypeAsWritten())
    return printAfter(T->getBaseType(), OS);
}

void TypePrinter::printObjCObjectPointerBefore(const ObjCObjectPointerType *T,
                                               raw_ostream &OS) {
  printBefore(T->getPointeeType(), OS);

  // If we need to print the pointer, print it now.
  if (!T->isObjCIdType() && !T->isObjCQualifiedIdType() &&
      !T->isObjCClassType() && !T->isObjCQualifiedClassType()) {
    if (HasEmptyPlaceHolder)
      OS << ' ';
    OS << '*';
  }
}

void TypePrinter::printObjCObjectPointerAfter(const ObjCObjectPointerType *T,
                                              raw_ostream &OS) {}

static
const TemplateArgument &getArgument(const TemplateArgument &A) { return A; }

static const TemplateArgument &getArgument(const TemplateArgumentLoc &A) {
  return A.getArgument();
}

static void printArgument(const TemplateArgument &A, const PrintingPolicy &PP,
                          llvm::raw_ostream &OS, bool IncludeType) {
  A.print(PP, OS, IncludeType);
}

static void printArgument(const TemplateArgumentLoc &A,
                          const PrintingPolicy &PP, llvm::raw_ostream &OS,
                          bool IncludeType) {
  const TemplateArgument::ArgKind &Kind = A.getArgument().getKind();
  if (Kind == TemplateArgument::ArgKind::Type)
    return A.getTypeSourceInfo()->getType().print(OS, PP);
  return A.getArgument().print(PP, OS, IncludeType);
}

static bool isSubstitutedTemplateArgument(ASTContext &Ctx, TemplateArgument Arg,
                                          TemplateArgument Pattern,
                                          ArrayRef<TemplateArgument> Args,
                                          unsigned Depth);

static bool isSubstitutedType(ASTContext &Ctx, QualType T, QualType Pattern,
                              ArrayRef<TemplateArgument> Args, unsigned Depth) {
  if (Ctx.hasSameType(T, Pattern))
    return true;

  // A type parameter matches its argument.
  if (auto *TTPT = Pattern->getAs<TemplateTypeParmType>()) {
    if (TTPT->getDepth() == Depth && TTPT->getIndex() < Args.size() &&
        Args[TTPT->getIndex()].getKind() == TemplateArgument::Type) {
      QualType SubstArg = Ctx.getQualifiedType(
          Args[TTPT->getIndex()].getAsType(), Pattern.getQualifiers());
      return Ctx.hasSameType(SubstArg, T);
    }
    return false;
  }

  // FIXME: Recurse into array types.

  // All other cases will need the types to be identically qualified.
  Qualifiers TQual, PatQual;
  T = Ctx.getUnqualifiedArrayType(T, TQual);
  Pattern = Ctx.getUnqualifiedArrayType(Pattern, PatQual);
  if (TQual != PatQual)
    return false;

  // Recurse into pointer-like types.
  {
    QualType TPointee = T->getPointeeType();
    QualType PPointee = Pattern->getPointeeType();
    if (!TPointee.isNull() && !PPointee.isNull())
      return T->getTypeClass() == Pattern->getTypeClass() &&
             isSubstitutedType(Ctx, TPointee, PPointee, Args, Depth);
  }

  // Recurse into template specialization types.
  if (auto *PTST =
          Pattern.getCanonicalType()->getAs<TemplateSpecializationType>()) {
    TemplateName Template;
    ArrayRef<TemplateArgument> TemplateArgs;
    if (auto *TTST = T->getAs<TemplateSpecializationType>()) {
      Template = TTST->getTemplateName();
      TemplateArgs = TTST->template_arguments();
    } else if (auto *CTSD = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
                   T->getAsCXXRecordDecl())) {
      Template = TemplateName(CTSD->getSpecializedTemplate());
      TemplateArgs = CTSD->getTemplateArgs().asArray();
    } else {
      return false;
    }

    if (!isSubstitutedTemplateArgument(Ctx, Template, PTST->getTemplateName(),
                                       Args, Depth))
      return false;
    if (TemplateArgs.size() != PTST->template_arguments().size())
      return false;
    for (unsigned I = 0, N = TemplateArgs.size(); I != N; ++I)
      if (!isSubstitutedTemplateArgument(
              Ctx, TemplateArgs[I], PTST->template_arguments()[I], Args, Depth))
        return false;
    return true;
  }

  // FIXME: Handle more cases.
  return false;
}

/// Evaluates the expression template argument 'Pattern' and returns true
/// if 'Arg' evaluates to the same result.
static bool templateArgumentExpressionsEqual(ASTContext const &Ctx,
                                             TemplateArgument const &Pattern,
                                             TemplateArgument const &Arg) {
  if (Pattern.getKind() != TemplateArgument::Expression)
    return false;

  // Can't evaluate value-dependent expressions so bail early
  Expr const *pattern_expr = Pattern.getAsExpr();
  if (pattern_expr->isValueDependent() ||
      !pattern_expr->isIntegerConstantExpr(Ctx))
    return false;

  if (Arg.getKind() == TemplateArgument::Integral)
    return llvm::APSInt::isSameValue(pattern_expr->EvaluateKnownConstInt(Ctx),
                                     Arg.getAsIntegral());

  if (Arg.getKind() == TemplateArgument::Expression) {
    Expr const *args_expr = Arg.getAsExpr();
    if (args_expr->isValueDependent() || !args_expr->isIntegerConstantExpr(Ctx))
      return false;

    return llvm::APSInt::isSameValue(args_expr->EvaluateKnownConstInt(Ctx),
                                     pattern_expr->EvaluateKnownConstInt(Ctx));
  }

  return false;
}

static bool isSubstitutedTemplateArgument(ASTContext &Ctx, TemplateArgument Arg,
                                          TemplateArgument Pattern,
                                          ArrayRef<TemplateArgument> Args,
                                          unsigned Depth) {
  Arg = Ctx.getCanonicalTemplateArgument(Arg);
  Pattern = Ctx.getCanonicalTemplateArgument(Pattern);
  if (Arg.structurallyEquals(Pattern))
    return true;

  if (Pattern.getKind() == TemplateArgument::Expression) {
    if (auto *DRE =
            dyn_cast<DeclRefExpr>(Pattern.getAsExpr()->IgnoreParenImpCasts())) {
      if (auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(DRE->getDecl()))
        return NTTP->getDepth() == Depth && Args.size() > NTTP->getIndex() &&
               Args[NTTP->getIndex()].structurallyEquals(Arg);
    }
  }

  if (templateArgumentExpressionsEqual(Ctx, Pattern, Arg))
    return true;

  if (Arg.getKind() != Pattern.getKind())
    return false;

  if (Arg.getKind() == TemplateArgument::Type)
    return isSubstitutedType(Ctx, Arg.getAsType(), Pattern.getAsType(), Args,
                             Depth);

  if (Arg.getKind() == TemplateArgument::Template) {
    TemplateDecl *PatTD = Pattern.getAsTemplate().getAsTemplateDecl();
    if (auto *TTPD = dyn_cast_or_null<TemplateTemplateParmDecl>(PatTD))
      return TTPD->getDepth() == Depth && Args.size() > TTPD->getIndex() &&
             Ctx.getCanonicalTemplateArgument(Args[TTPD->getIndex()])
                 .structurallyEquals(Arg);
  }

  // FIXME: Handle more cases.
  return false;
}

bool clang::isSubstitutedDefaultArgument(ASTContext &Ctx, TemplateArgument Arg,
                                         const NamedDecl *Param,
                                         ArrayRef<TemplateArgument> Args,
                                         unsigned Depth) {
  // An empty pack is equivalent to not providing a pack argument.
  if (Arg.getKind() == TemplateArgument::Pack && Arg.pack_size() == 0)
    return true;

  if (auto *TTPD = dyn_cast<TemplateTypeParmDecl>(Param)) {
    return TTPD->hasDefaultArgument() &&
           isSubstitutedTemplateArgument(
               Ctx, Arg, TTPD->getDefaultArgument().getArgument(), Args, Depth);
  } else if (auto *TTPD = dyn_cast<TemplateTemplateParmDecl>(Param)) {
    return TTPD->hasDefaultArgument() &&
           isSubstitutedTemplateArgument(
               Ctx, Arg, TTPD->getDefaultArgument().getArgument(), Args, Depth);
  } else if (auto *NTTPD = dyn_cast<NonTypeTemplateParmDecl>(Param)) {
    return NTTPD->hasDefaultArgument() &&
           isSubstitutedTemplateArgument(
               Ctx, Arg, NTTPD->getDefaultArgument().getArgument(), Args,
               Depth);
  }
  return false;
}

template <typename TA>
static void
printTo(raw_ostream &OS, ArrayRef<TA> Args, const PrintingPolicy &Policy,
        const TemplateParameterList *TPL, bool IsPack, unsigned ParmIndex) {
  // Drop trailing template arguments that match default arguments.
  if (TPL && Policy.SuppressDefaultTemplateArgs &&
      !Policy.PrintCanonicalTypes && !Args.empty() && !IsPack &&
      Args.size() <= TPL->size()) {
    llvm::SmallVector<TemplateArgument, 8> OrigArgs;
    for (const TA &A : Args)
      OrigArgs.push_back(getArgument(A));
    while (!Args.empty() && getArgument(Args.back()).getIsDefaulted())
      Args = Args.drop_back();
  }

  const char *Comma = Policy.MSVCFormatting ? "," : ", ";
  if (!IsPack)
    OS << '<';

  bool NeedSpace = false;
  bool FirstArg = true;
  for (const auto &Arg : Args) {
    // Print the argument into a string.
    SmallString<128> Buf;
    llvm::raw_svector_ostream ArgOS(Buf);
    const TemplateArgument &Argument = getArgument(Arg);
    if (Argument.getKind() == TemplateArgument::Pack) {
      if (Argument.pack_size() && !FirstArg)
        OS << Comma;
      printTo(ArgOS, Argument.getPackAsArray(), Policy, TPL,
              /*IsPack*/ true, ParmIndex);
    } else {
      if (!FirstArg)
        OS << Comma;
      // Tries to print the argument with location info if exists.
      printArgument(Arg, Policy, ArgOS,
                    TemplateParameterList::shouldIncludeTypeForArgument(
                        Policy, TPL, ParmIndex));
    }
    StringRef ArgString = ArgOS.str();

    // If this is the first argument and its string representation
    // begins with the global scope specifier ('::foo'), add a space
    // to avoid printing the diagraph '<:'.
    if (FirstArg && ArgString.starts_with(":"))
      OS << ' ';

    OS << ArgString;

    // If the last character of our string is '>', add another space to
    // keep the two '>''s separate tokens.
    if (!ArgString.empty()) {
      NeedSpace = Policy.SplitTemplateClosers && ArgString.back() == '>';
      FirstArg = false;
    }

    // Use same template parameter for all elements of Pack
    if (!IsPack)
      ParmIndex++;
  }

  if (!IsPack) {
    if (NeedSpace)
      OS << ' ';
    OS << '>';
  }
}

void clang::printTemplateArgumentList(raw_ostream &OS,
                                      const TemplateArgumentListInfo &Args,
                                      const PrintingPolicy &Policy,
                                      const TemplateParameterList *TPL) {
  printTemplateArgumentList(OS, Args.arguments(), Policy, TPL);
}

void clang::printTemplateArgumentList(raw_ostream &OS,
                                      ArrayRef<TemplateArgument> Args,
                                      const PrintingPolicy &Policy,
                                      const TemplateParameterList *TPL) {
  printTo(OS, Args, Policy, TPL, /*isPack*/ false, /*parmIndex*/ 0);
}

void clang::printTemplateArgumentList(raw_ostream &OS,
                                      ArrayRef<TemplateArgumentLoc> Args,
                                      const PrintingPolicy &Policy,
                                      const TemplateParameterList *TPL) {
  printTo(OS, Args, Policy, TPL, /*isPack*/ false, /*parmIndex*/ 0);
}

std::string Qualifiers::getAsString() const {
  LangOptions LO;
  return getAsString(PrintingPolicy(LO));
}

// Appends qualifiers to the given string, separated by spaces.  Will
// prefix a space if the string is non-empty.  Will not append a final
// space.
std::string Qualifiers::getAsString(const PrintingPolicy &Policy) const {
  SmallString<64> Buf;
  llvm::raw_svector_ostream StrOS(Buf);
  print(StrOS, Policy);
  return std::string(StrOS.str());
}

bool Qualifiers::isEmptyWhenPrinted(const PrintingPolicy &Policy) const {
  if (getCVRQualifiers())
    return false;

  if (getAddressSpace() != LangAS::Default)
    return false;

  if (getObjCGCAttr())
    return false;

  if (Qualifiers::ObjCLifetime lifetime = getObjCLifetime())
    if (!(lifetime == Qualifiers::OCL_Strong && Policy.SuppressStrongLifetime))
      return false;

  return true;
}

std::string Qualifiers::getAddrSpaceAsString(LangAS AS) {
  switch (AS) {
  case LangAS::Default:
    return "";
  case LangAS::opencl_global:
  case LangAS::sycl_global:
    return "__global";
  case LangAS::opencl_local:
  case LangAS::sycl_local:
    return "__local";
  case LangAS::opencl_private:
  case LangAS::sycl_private:
    return "__private";
  case LangAS::opencl_constant:
    return "__constant";
  case LangAS::opencl_generic:
    return "__generic";
  case LangAS::opencl_global_device:
  case LangAS::sycl_global_device:
    return "__global_device";
  case LangAS::opencl_global_host:
  case LangAS::sycl_global_host:
    return "__global_host";
  case LangAS::cuda_device:
    return "__device__";
  case LangAS::cuda_constant:
    return "__constant__";
  case LangAS::cuda_shared:
    return "__shared__";
  case LangAS::ptr32_sptr:
    return "__sptr __ptr32";
  case LangAS::ptr32_uptr:
    return "__uptr __ptr32";
  case LangAS::ptr64:
    return "__ptr64";
  case LangAS::wasm_funcref:
    return "__funcref";
  case LangAS::hlsl_groupshared:
    return "groupshared";
  default:
    return std::to_string(toTargetAddressSpace(AS));
  }
}

// Appends qualifiers to the given string, separated by spaces.  Will
// prefix a space if the string is non-empty.  Will not append a final
// space.
void Qualifiers::print(raw_ostream &OS, const PrintingPolicy& Policy,
                       bool appendSpaceIfNonEmpty) const {
  bool addSpace = false;

  unsigned quals = getCVRQualifiers();
  if (quals) {
    AppendTypeQualList(OS, quals, Policy.Restrict);
    addSpace = true;
  }
  if (hasUnaligned()) {
    if (addSpace)
      OS << ' ';
    OS << "__unaligned";
    addSpace = true;
  }
  auto ASStr = getAddrSpaceAsString(getAddressSpace());
  if (!ASStr.empty()) {
    if (addSpace)
      OS << ' ';
    addSpace = true;
    // Wrap target address space into an attribute syntax
    if (isTargetAddressSpace(getAddressSpace()))
      OS << "__attribute__((address_space(" << ASStr << ")))";
    else
      OS << ASStr;
  }

  if (Qualifiers::GC gc = getObjCGCAttr()) {
    if (addSpace)
      OS << ' ';
    addSpace = true;
    if (gc == Qualifiers::Weak)
      OS << "__weak";
    else
      OS << "__strong";
  }
  if (Qualifiers::ObjCLifetime lifetime = getObjCLifetime()) {
    if (!(lifetime == Qualifiers::OCL_Strong && Policy.SuppressStrongLifetime)){
      if (addSpace)
        OS << ' ';
      addSpace = true;
    }

    switch (lifetime) {
    case Qualifiers::OCL_None: llvm_unreachable("none but true");
    case Qualifiers::OCL_ExplicitNone: OS << "__unsafe_unretained"; break;
    case Qualifiers::OCL_Strong:
      if (!Policy.SuppressStrongLifetime)
        OS << "__strong";
      break;

    case Qualifiers::OCL_Weak: OS << "__weak"; break;
    case Qualifiers::OCL_Autoreleasing: OS << "__autoreleasing"; break;
    }
  }

  if (appendSpaceIfNonEmpty && addSpace)
    OS << ' ';
}

std::string QualType::getAsString() const {
  return getAsString(split(), LangOptions());
}

std::string QualType::getAsString(const PrintingPolicy &Policy) const {
  std::string S;
  getAsStringInternal(S, Policy);
  return S;
}

std::string QualType::getAsString(const Type *ty, Qualifiers qs,
                                  const PrintingPolicy &Policy) {
  std::string buffer;
  getAsStringInternal(ty, qs, buffer, Policy);
  return buffer;
}

void QualType::print(raw_ostream &OS, const PrintingPolicy &Policy,
                     const Twine &PlaceHolder, unsigned Indentation) const {
  print(splitAccordingToPolicy(*this, Policy), OS, Policy, PlaceHolder,
        Indentation);
}

void QualType::print(const Type *ty, Qualifiers qs,
                     raw_ostream &OS, const PrintingPolicy &policy,
                     const Twine &PlaceHolder, unsigned Indentation) {
  SmallString<128> PHBuf;
  StringRef PH = PlaceHolder.toStringRef(PHBuf);

  TypePrinter(policy, Indentation).print(ty, qs, OS, PH);
}

void QualType::getAsStringInternal(std::string &Str,
                                   const PrintingPolicy &Policy) const {
  return getAsStringInternal(splitAccordingToPolicy(*this, Policy), Str,
                             Policy);
}

void QualType::getAsStringInternal(const Type *ty, Qualifiers qs,
                                   std::string &buffer,
                                   const PrintingPolicy &policy) {
  SmallString<256> Buf;
  llvm::raw_svector_ostream StrOS(Buf);
  TypePrinter(policy).print(ty, qs, StrOS, buffer);
  std::string str = std::string(StrOS.str());
  buffer.swap(str);
}

raw_ostream &clang::operator<<(raw_ostream &OS, QualType QT) {
  SplitQualType S = QT.split();
  TypePrinter(LangOptions()).print(S.Ty, S.Quals, OS, /*PlaceHolder=*/"");
  return OS;
}
