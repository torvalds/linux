//===- DeclarationName.h - Representation of declaration names --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the DeclarationName and DeclarationNameTable classes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_DECLARATIONNAME_H
#define LLVM_CLANG_AST_DECLARATIONNAME_H

#include "clang/AST/Type.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/PartialDiagnostic.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/type_traits.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>

namespace clang {

class ASTContext;
template <typename> class CanQual;
class DeclarationName;
class DeclarationNameTable;
struct PrintingPolicy;
class TemplateDecl;
class TypeSourceInfo;

using CanQualType = CanQual<Type>;

namespace detail {

/// CXXSpecialNameExtra records the type associated with one of the "special"
/// kinds of declaration names in C++, e.g., constructors, destructors, and
/// conversion functions. Note that CXXSpecialName is used for C++ constructor,
/// destructor and conversion functions, but the actual kind is not stored in
/// CXXSpecialName. Instead we use three different FoldingSet<CXXSpecialName>
/// in DeclarationNameTable.
class alignas(IdentifierInfoAlignment) CXXSpecialNameExtra
    : public llvm::FoldingSetNode {
  friend class clang::DeclarationName;
  friend class clang::DeclarationNameTable;

  /// The type associated with this declaration name.
  QualType Type;

  /// Extra information associated with this declaration name that
  /// can be used by the front end. All bits are really needed
  /// so it is not possible to stash something in the low order bits.
  void *FETokenInfo;

  CXXSpecialNameExtra(QualType QT) : Type(QT), FETokenInfo(nullptr) {}

public:
  void Profile(llvm::FoldingSetNodeID &ID) {
    ID.AddPointer(Type.getAsOpaquePtr());
  }
};

/// Contains extra information for the name of a C++ deduction guide.
class alignas(IdentifierInfoAlignment) CXXDeductionGuideNameExtra
    : public detail::DeclarationNameExtra,
      public llvm::FoldingSetNode {
  friend class clang::DeclarationName;
  friend class clang::DeclarationNameTable;

  /// The template named by the deduction guide.
  TemplateDecl *Template;

  /// Extra information associated with this operator name that
  /// can be used by the front end. All bits are really needed
  /// so it is not possible to stash something in the low order bits.
  void *FETokenInfo;

  CXXDeductionGuideNameExtra(TemplateDecl *TD)
      : DeclarationNameExtra(CXXDeductionGuideName), Template(TD),
        FETokenInfo(nullptr) {}

public:
  void Profile(llvm::FoldingSetNodeID &ID) { ID.AddPointer(Template); }
};

/// Contains extra information for the name of an overloaded operator
/// in C++, such as "operator+. This do not includes literal or conversion
/// operators. For literal operators see CXXLiteralOperatorIdName and for
/// conversion operators see CXXSpecialNameExtra.
class alignas(IdentifierInfoAlignment) CXXOperatorIdName {
  friend class clang::DeclarationName;
  friend class clang::DeclarationNameTable;

  /// The kind of this operator.
  OverloadedOperatorKind Kind = OO_None;

  /// Extra information associated with this operator name that
  /// can be used by the front end. All bits are really needed
  /// so it is not possible to stash something in the low order bits.
  void *FETokenInfo = nullptr;
};

/// Contains the actual identifier that makes up the
/// name of a C++ literal operator.
class alignas(IdentifierInfoAlignment) CXXLiteralOperatorIdName
    : public detail::DeclarationNameExtra,
      public llvm::FoldingSetNode {
  friend class clang::DeclarationName;
  friend class clang::DeclarationNameTable;

  const IdentifierInfo *ID;

  /// Extra information associated with this operator name that
  /// can be used by the front end. All bits are really needed
  /// so it is not possible to stash something in the low order bits.
  void *FETokenInfo;

  CXXLiteralOperatorIdName(const IdentifierInfo *II)
      : DeclarationNameExtra(CXXLiteralOperatorName), ID(II),
        FETokenInfo(nullptr) {}

public:
  void Profile(llvm::FoldingSetNodeID &FSID) { FSID.AddPointer(ID); }
};

} // namespace detail

/// The name of a declaration. In the common case, this just stores
/// an IdentifierInfo pointer to a normal name. However, it also provides
/// encodings for Objective-C selectors (optimizing zero- and one-argument
/// selectors, which make up 78% percent of all selectors in Cocoa.h),
/// special C++ names for constructors, destructors, and conversion functions,
/// and C++ overloaded operators.
class DeclarationName {
  friend class DeclarationNameTable;
  friend class NamedDecl;

  /// StoredNameKind represent the kind of name that is actually stored in the
  /// upper bits of the Ptr field. This is only used internally.
  ///
  /// NameKind, StoredNameKind, and DeclarationNameExtra::ExtraKind
  /// must satisfy the following properties. These properties enable
  /// efficient conversion between the various kinds.
  ///
  /// * The first seven enumerators of StoredNameKind must have the same
  ///   numerical value as the first seven enumerators of NameKind.
  ///   This enable efficient conversion between the two enumerations
  ///   in the usual case.
  ///
  /// * The enumerations values of DeclarationNameExtra::ExtraKind must start
  ///   at zero, and correspond to the numerical value of the first non-inline
  ///   enumeration values of NameKind minus an offset. This makes conversion
  ///   between DeclarationNameExtra::ExtraKind and NameKind possible with
  ///   a single addition/substraction.
  ///
  /// * The enumeration values of Selector::IdentifierInfoFlag must correspond
  ///   to the relevant enumeration values of StoredNameKind.
  ///   More specifically:
  ///    * ZeroArg == StoredObjCZeroArgSelector,
  ///    * OneArg == StoredObjCOneArgSelector,
  ///    * MultiArg == StoredDeclarationNameExtra
  ///
  /// * PtrMask must mask the low 3 bits of Ptr.
  enum StoredNameKind {
    StoredIdentifier = 0,
    StoredObjCZeroArgSelector = Selector::ZeroArg,
    StoredObjCOneArgSelector = Selector::OneArg,
    StoredCXXConstructorName = 3,
    StoredCXXDestructorName = 4,
    StoredCXXConversionFunctionName = 5,
    StoredCXXOperatorName = 6,
    StoredDeclarationNameExtra = Selector::MultiArg,
    PtrMask = 7,
    UncommonNameKindOffset = 8
  };

  static_assert(alignof(IdentifierInfo) >= 8 &&
                    alignof(detail::DeclarationNameExtra) >= 8 &&
                    alignof(detail::CXXSpecialNameExtra) >= 8 &&
                    alignof(detail::CXXOperatorIdName) >= 8 &&
                    alignof(detail::CXXDeductionGuideNameExtra) >= 8 &&
                    alignof(detail::CXXLiteralOperatorIdName) >= 8,
                "The various classes that DeclarationName::Ptr can point to"
                " must be at least aligned to 8 bytes!");

  static_assert(
      std::is_same<std::underlying_type_t<StoredNameKind>,
                   std::underlying_type_t<
                       detail::DeclarationNameExtra::ExtraKind>>::value,
      "The various enums used to compute values for NameKind should "
      "all have the same underlying type");

public:
  /// The kind of the name stored in this DeclarationName.
  /// The first 7 enumeration values are stored inline and correspond
  /// to frequently used kinds. The rest is stored in DeclarationNameExtra
  /// and correspond to infrequently used kinds.
  enum NameKind {
    Identifier = StoredIdentifier,
    ObjCZeroArgSelector = StoredObjCZeroArgSelector,
    ObjCOneArgSelector = StoredObjCOneArgSelector,
    CXXConstructorName = StoredCXXConstructorName,
    CXXDestructorName = StoredCXXDestructorName,
    CXXConversionFunctionName = StoredCXXConversionFunctionName,
    CXXOperatorName = StoredCXXOperatorName,
    CXXDeductionGuideName = llvm::addEnumValues(
        UncommonNameKindOffset,
        detail::DeclarationNameExtra::CXXDeductionGuideName),
    CXXLiteralOperatorName = llvm::addEnumValues(
        UncommonNameKindOffset,
        detail::DeclarationNameExtra::CXXLiteralOperatorName),
    CXXUsingDirective =
        llvm::addEnumValues(UncommonNameKindOffset,
                            detail::DeclarationNameExtra::CXXUsingDirective),
    ObjCMultiArgSelector =
        llvm::addEnumValues(UncommonNameKindOffset,
                            detail::DeclarationNameExtra::ObjCMultiArgSelector),
  };

private:
  /// The lowest three bits of Ptr are used to express what kind of name
  /// we're actually storing, using the values of StoredNameKind. Depending
  /// on the kind of name this is, the upper bits of Ptr may have one
  /// of several different meanings:
  ///
  ///   StoredIdentifier - The name is a normal identifier, and Ptr is
  ///   a normal IdentifierInfo pointer.
  ///
  ///   StoredObjCZeroArgSelector - The name is an Objective-C
  ///   selector with zero arguments, and Ptr is an IdentifierInfo
  ///   pointer pointing to the selector name.
  ///
  ///   StoredObjCOneArgSelector - The name is an Objective-C selector
  ///   with one argument, and Ptr is an IdentifierInfo pointer
  ///   pointing to the selector name.
  ///
  ///   StoredCXXConstructorName - The name of a C++ constructor,
  ///   Ptr points to a CXXSpecialNameExtra.
  ///
  ///   StoredCXXDestructorName - The name of a C++ destructor,
  ///   Ptr points to a CXXSpecialNameExtra.
  ///
  ///   StoredCXXConversionFunctionName - The name of a C++ conversion function,
  ///   Ptr points to a CXXSpecialNameExtra.
  ///
  ///   StoredCXXOperatorName - The name of an overloaded C++ operator,
  ///   Ptr points to a CXXOperatorIdName.
  ///
  ///   StoredDeclarationNameExtra - Ptr is actually a pointer to a
  ///   DeclarationNameExtra structure, whose first value will tell us
  ///   whether this is an Objective-C selector, C++ deduction guide,
  ///   C++ literal operator, or C++ using directive.
  uintptr_t Ptr = 0;

  StoredNameKind getStoredNameKind() const {
    return static_cast<StoredNameKind>(Ptr & PtrMask);
  }

  void *getPtr() const { return reinterpret_cast<void *>(Ptr & ~PtrMask); }

  void setPtrAndKind(const void *P, StoredNameKind Kind) {
    uintptr_t PAsInteger = reinterpret_cast<uintptr_t>(P);
    assert((Kind & ~PtrMask) == 0 &&
           "Invalid StoredNameKind in setPtrAndKind!");
    assert((PAsInteger & PtrMask) == 0 &&
           "Improperly aligned pointer in setPtrAndKind!");
    Ptr = PAsInteger | Kind;
  }

  /// Construct a declaration name from a DeclarationNameExtra.
  DeclarationName(detail::DeclarationNameExtra *Name) {
    setPtrAndKind(Name, StoredDeclarationNameExtra);
  }

  /// Construct a declaration name from a CXXSpecialNameExtra.
  DeclarationName(detail::CXXSpecialNameExtra *Name,
                  StoredNameKind StoredKind) {
    assert((StoredKind == StoredCXXConstructorName ||
           StoredKind == StoredCXXDestructorName ||
           StoredKind == StoredCXXConversionFunctionName) &&
               "Invalid StoredNameKind when constructing a DeclarationName"
               " from a CXXSpecialNameExtra!");
    setPtrAndKind(Name, StoredKind);
  }

  /// Construct a DeclarationName from a CXXOperatorIdName.
  DeclarationName(detail::CXXOperatorIdName *Name) {
    setPtrAndKind(Name, StoredCXXOperatorName);
  }

  /// Assert that the stored pointer points to an IdentifierInfo and return it.
  IdentifierInfo *castAsIdentifierInfo() const {
    assert((getStoredNameKind() == StoredIdentifier) &&
           "DeclarationName does not store an IdentifierInfo!");
    return static_cast<IdentifierInfo *>(getPtr());
  }

  /// Assert that the stored pointer points to a DeclarationNameExtra
  /// and return it.
  detail::DeclarationNameExtra *castAsExtra() const {
    assert((getStoredNameKind() == StoredDeclarationNameExtra) &&
           "DeclarationName does not store an Extra structure!");
    return static_cast<detail::DeclarationNameExtra *>(getPtr());
  }

  /// Assert that the stored pointer points to a CXXSpecialNameExtra
  /// and return it.
  detail::CXXSpecialNameExtra *castAsCXXSpecialNameExtra() const {
    assert((getStoredNameKind() == StoredCXXConstructorName ||
           getStoredNameKind() == StoredCXXDestructorName ||
           getStoredNameKind() == StoredCXXConversionFunctionName) &&
               "DeclarationName does not store a CXXSpecialNameExtra!");
    return static_cast<detail::CXXSpecialNameExtra *>(getPtr());
  }

  /// Assert that the stored pointer points to a CXXOperatorIdName
  /// and return it.
  detail::CXXOperatorIdName *castAsCXXOperatorIdName() const {
    assert((getStoredNameKind() == StoredCXXOperatorName) &&
           "DeclarationName does not store a CXXOperatorIdName!");
    return static_cast<detail::CXXOperatorIdName *>(getPtr());
  }

  /// Assert that the stored pointer points to a CXXDeductionGuideNameExtra
  /// and return it.
  detail::CXXDeductionGuideNameExtra *castAsCXXDeductionGuideNameExtra() const {
    assert(getNameKind() == CXXDeductionGuideName &&
           "DeclarationName does not store a CXXDeductionGuideNameExtra!");
    return static_cast<detail::CXXDeductionGuideNameExtra *>(getPtr());
  }

  /// Assert that the stored pointer points to a CXXLiteralOperatorIdName
  /// and return it.
  detail::CXXLiteralOperatorIdName *castAsCXXLiteralOperatorIdName() const {
    assert(getNameKind() == CXXLiteralOperatorName &&
           "DeclarationName does not store a CXXLiteralOperatorIdName!");
    return static_cast<detail::CXXLiteralOperatorIdName *>(getPtr());
  }

  /// Get and set the FETokenInfo in the less common cases where the
  /// declaration name do not point to an identifier.
  void *getFETokenInfoSlow() const;
  void setFETokenInfoSlow(void *T);

public:
  /// Construct an empty declaration name.
  DeclarationName() { setPtrAndKind(nullptr, StoredIdentifier); }

  /// Construct a declaration name from an IdentifierInfo *.
  DeclarationName(const IdentifierInfo *II) {
    setPtrAndKind(II, StoredIdentifier);
  }

  /// Construct a declaration name from an Objective-C selector.
  DeclarationName(Selector Sel)
      : Ptr(reinterpret_cast<uintptr_t>(Sel.InfoPtr.getOpaqueValue())) {}

  /// Returns the name for all C++ using-directives.
  static DeclarationName getUsingDirectiveName() {
    // Single instance of DeclarationNameExtra for using-directive
    static detail::DeclarationNameExtra UDirExtra(
        detail::DeclarationNameExtra::CXXUsingDirective);
    return DeclarationName(&UDirExtra);
  }

  /// Evaluates true when this declaration name is non-empty.
  explicit operator bool() const {
    return getPtr() || (getStoredNameKind() != StoredIdentifier);
  }

  /// Evaluates true when this declaration name is empty.
  bool isEmpty() const { return !*this; }

  /// Predicate functions for querying what type of name this is.
  bool isIdentifier() const { return getStoredNameKind() == StoredIdentifier; }
  bool isObjCZeroArgSelector() const {
    return getStoredNameKind() == StoredObjCZeroArgSelector;
  }
  bool isObjCOneArgSelector() const {
    return getStoredNameKind() == StoredObjCOneArgSelector;
  }

  /// Determine what kind of name this is.
  NameKind getNameKind() const {
    // We rely on the fact that the first 7 NameKind and StoredNameKind
    // have the same numerical value. This makes the usual case efficient.
    StoredNameKind StoredKind = getStoredNameKind();
    if (StoredKind != StoredDeclarationNameExtra)
      return static_cast<NameKind>(StoredKind);
    // We have to consult DeclarationNameExtra. We rely on the fact that the
    // enumeration values of ExtraKind correspond to the enumeration values of
    // NameKind minus an offset of UncommonNameKindOffset.
    unsigned ExtraKind = castAsExtra()->getKind();
    return static_cast<NameKind>(UncommonNameKindOffset + ExtraKind);
  }

  /// Determines whether the name itself is dependent, e.g., because it
  /// involves a C++ type that is itself dependent.
  ///
  /// Note that this does not capture all of the notions of "dependent name",
  /// because an identifier can be a dependent name if it is used as the
  /// callee in a call expression with dependent arguments.
  bool isDependentName() const;

  /// Retrieve the human-readable string for this name.
  std::string getAsString() const;

  /// Retrieve the IdentifierInfo * stored in this declaration name,
  /// or null if this declaration name isn't a simple identifier.
  IdentifierInfo *getAsIdentifierInfo() const {
    if (isIdentifier())
      return castAsIdentifierInfo();
    return nullptr;
  }

  /// Get the representation of this declaration name as an opaque integer.
  uintptr_t getAsOpaqueInteger() const { return Ptr; }

  /// Get the representation of this declaration name as an opaque pointer.
  void *getAsOpaquePtr() const { return reinterpret_cast<void *>(Ptr); }

  /// Get a declaration name from an opaque pointer returned by getAsOpaquePtr.
  static DeclarationName getFromOpaquePtr(void *P) {
    DeclarationName N;
    N.Ptr = reinterpret_cast<uintptr_t>(P);
    return N;
  }

  /// Get a declaration name from an opaque integer
  /// returned by getAsOpaqueInteger.
  static DeclarationName getFromOpaqueInteger(uintptr_t P) {
    DeclarationName N;
    N.Ptr = P;
    return N;
  }

  /// If this name is one of the C++ names (of a constructor, destructor,
  /// or conversion function), return the type associated with that name.
  QualType getCXXNameType() const {
    if (getStoredNameKind() == StoredCXXConstructorName ||
        getStoredNameKind() == StoredCXXDestructorName ||
        getStoredNameKind() == StoredCXXConversionFunctionName) {
      assert(getPtr() && "getCXXNameType on a null DeclarationName!");
      return castAsCXXSpecialNameExtra()->Type;
    }
    return QualType();
  }

  /// If this name is the name of a C++ deduction guide, return the
  /// template associated with that name.
  TemplateDecl *getCXXDeductionGuideTemplate() const {
    if (getNameKind() == CXXDeductionGuideName) {
      assert(getPtr() &&
             "getCXXDeductionGuideTemplate on a null DeclarationName!");
      return castAsCXXDeductionGuideNameExtra()->Template;
    }
    return nullptr;
  }

  /// If this name is the name of an overloadable operator in C++
  /// (e.g., @c operator+), retrieve the kind of overloaded operator.
  OverloadedOperatorKind getCXXOverloadedOperator() const {
    if (getStoredNameKind() == StoredCXXOperatorName) {
      assert(getPtr() && "getCXXOverloadedOperator on a null DeclarationName!");
      return castAsCXXOperatorIdName()->Kind;
    }
    return OO_None;
  }

  /// If this name is the name of a literal operator,
  /// retrieve the identifier associated with it.
  const IdentifierInfo *getCXXLiteralIdentifier() const {
    if (getNameKind() == CXXLiteralOperatorName) {
      assert(getPtr() && "getCXXLiteralIdentifier on a null DeclarationName!");
      return castAsCXXLiteralOperatorIdName()->ID;
    }
    return nullptr;
  }

  /// Get the Objective-C selector stored in this declaration name.
  Selector getObjCSelector() const {
    assert((getNameKind() == ObjCZeroArgSelector ||
            getNameKind() == ObjCOneArgSelector ||
            getNameKind() == ObjCMultiArgSelector || !getPtr()) &&
           "Not a selector!");
    return Selector(Ptr);
  }

  /// Get and set FETokenInfo. The language front-end is allowed to associate
  /// arbitrary metadata with some kinds of declaration names, including normal
  /// identifiers and C++ constructors, destructors, and conversion functions.
  void *getFETokenInfo() const {
    assert(getPtr() && "getFETokenInfo on an empty DeclarationName!");
    if (getStoredNameKind() == StoredIdentifier)
      return castAsIdentifierInfo()->getFETokenInfo();
    return getFETokenInfoSlow();
  }

  void setFETokenInfo(void *T) {
    assert(getPtr() && "setFETokenInfo on an empty DeclarationName!");
    if (getStoredNameKind() == StoredIdentifier)
      castAsIdentifierInfo()->setFETokenInfo(T);
    else
      setFETokenInfoSlow(T);
  }

  /// Determine whether the specified names are identical.
  friend bool operator==(DeclarationName LHS, DeclarationName RHS) {
    return LHS.Ptr == RHS.Ptr;
  }

  /// Determine whether the specified names are different.
  friend bool operator!=(DeclarationName LHS, DeclarationName RHS) {
    return LHS.Ptr != RHS.Ptr;
  }

  static DeclarationName getEmptyMarker() {
    DeclarationName Name;
    Name.Ptr = uintptr_t(-1);
    return Name;
  }

  static DeclarationName getTombstoneMarker() {
    DeclarationName Name;
    Name.Ptr = uintptr_t(-2);
    return Name;
  }

  static int compare(DeclarationName LHS, DeclarationName RHS);

  void print(raw_ostream &OS, const PrintingPolicy &Policy) const;

  void dump() const;
};

raw_ostream &operator<<(raw_ostream &OS, DeclarationName N);

/// Ordering on two declaration names. If both names are identifiers,
/// this provides a lexicographical ordering.
inline bool operator<(DeclarationName LHS, DeclarationName RHS) {
  return DeclarationName::compare(LHS, RHS) < 0;
}

/// Ordering on two declaration names. If both names are identifiers,
/// this provides a lexicographical ordering.
inline bool operator>(DeclarationName LHS, DeclarationName RHS) {
  return DeclarationName::compare(LHS, RHS) > 0;
}

/// Ordering on two declaration names. If both names are identifiers,
/// this provides a lexicographical ordering.
inline bool operator<=(DeclarationName LHS, DeclarationName RHS) {
  return DeclarationName::compare(LHS, RHS) <= 0;
}

/// Ordering on two declaration names. If both names are identifiers,
/// this provides a lexicographical ordering.
inline bool operator>=(DeclarationName LHS, DeclarationName RHS) {
  return DeclarationName::compare(LHS, RHS) >= 0;
}

/// DeclarationNameTable is used to store and retrieve DeclarationName
/// instances for the various kinds of declaration names, e.g., normal
/// identifiers, C++ constructor names, etc. This class contains
/// uniqued versions of each of the C++ special names, which can be
/// retrieved using its member functions (e.g., getCXXConstructorName).
class DeclarationNameTable {
  /// Used to allocate elements in the FoldingSets below.
  const ASTContext &Ctx;

  /// Manage the uniqued CXXSpecialNameExtra representing C++ constructors.
  /// getCXXConstructorName and getCXXSpecialName can be used to obtain
  /// a DeclarationName from the corresponding type of the constructor.
  llvm::FoldingSet<detail::CXXSpecialNameExtra> CXXConstructorNames;

  /// Manage the uniqued CXXSpecialNameExtra representing C++ destructors.
  /// getCXXDestructorName and getCXXSpecialName can be used to obtain
  /// a DeclarationName from the corresponding type of the destructor.
  llvm::FoldingSet<detail::CXXSpecialNameExtra> CXXDestructorNames;

  /// Manage the uniqued CXXSpecialNameExtra representing C++ conversion
  /// functions. getCXXConversionFunctionName and getCXXSpecialName can be
  /// used to obtain a DeclarationName from the corresponding type of the
  /// conversion function.
  llvm::FoldingSet<detail::CXXSpecialNameExtra> CXXConversionFunctionNames;

  /// Manage the uniqued CXXOperatorIdName, which contain extra information
  /// for the name of overloaded C++ operators. getCXXOperatorName
  /// can be used to obtain a DeclarationName from the operator kind.
  detail::CXXOperatorIdName CXXOperatorNames[NUM_OVERLOADED_OPERATORS];

  /// Manage the uniqued CXXLiteralOperatorIdName, which contain extra
  /// information for the name of C++ literal operators.
  /// getCXXLiteralOperatorName can be used to obtain a DeclarationName
  /// from the corresponding IdentifierInfo.
  llvm::FoldingSet<detail::CXXLiteralOperatorIdName> CXXLiteralOperatorNames;

  /// Manage the uniqued CXXDeductionGuideNameExtra, which contain
  /// extra information for the name of a C++ deduction guide.
  /// getCXXDeductionGuideName can be used to obtain a DeclarationName
  /// from the corresponding template declaration.
  llvm::FoldingSet<detail::CXXDeductionGuideNameExtra> CXXDeductionGuideNames;

public:
  DeclarationNameTable(const ASTContext &C);
  DeclarationNameTable(const DeclarationNameTable &) = delete;
  DeclarationNameTable &operator=(const DeclarationNameTable &) = delete;
  DeclarationNameTable(DeclarationNameTable &&) = delete;
  DeclarationNameTable &operator=(DeclarationNameTable &&) = delete;
  ~DeclarationNameTable() = default;

  /// Create a declaration name that is a simple identifier.
  DeclarationName getIdentifier(const IdentifierInfo *ID) {
    return DeclarationName(ID);
  }

  /// Returns the name of a C++ constructor for the given Type.
  DeclarationName getCXXConstructorName(CanQualType Ty);

  /// Returns the name of a C++ destructor for the given Type.
  DeclarationName getCXXDestructorName(CanQualType Ty);

  /// Returns the name of a C++ deduction guide for the given template.
  DeclarationName getCXXDeductionGuideName(TemplateDecl *TD);

  /// Returns the name of a C++ conversion function for the given Type.
  DeclarationName getCXXConversionFunctionName(CanQualType Ty);

  /// Returns a declaration name for special kind of C++ name,
  /// e.g., for a constructor, destructor, or conversion function.
  /// Kind must be one of:
  ///   * DeclarationName::CXXConstructorName,
  ///   * DeclarationName::CXXDestructorName or
  ///   * DeclarationName::CXXConversionFunctionName
  DeclarationName getCXXSpecialName(DeclarationName::NameKind Kind,
                                    CanQualType Ty);

  /// Get the name of the overloadable C++ operator corresponding to Op.
  DeclarationName getCXXOperatorName(OverloadedOperatorKind Op) {
    return DeclarationName(&CXXOperatorNames[Op]);
  }

  /// Get the name of the literal operator function with II as the identifier.
  DeclarationName getCXXLiteralOperatorName(const IdentifierInfo *II);
};

/// DeclarationNameLoc - Additional source/type location info
/// for a declaration name. Needs a DeclarationName in order
/// to be interpreted correctly.
class DeclarationNameLoc {
  // The source location for identifier stored elsewhere.
  // struct {} Identifier;

  // Type info for constructors, destructors and conversion functions.
  // Locations (if any) for the tilde (destructor) or operator keyword
  // (conversion) are stored elsewhere.
  struct NT {
    TypeSourceInfo *TInfo;
  };

  // The location (if any) of the operator keyword is stored elsewhere.
  struct CXXOpName {
    SourceLocation::UIntTy BeginOpNameLoc;
    SourceLocation::UIntTy EndOpNameLoc;
  };

  // The location (if any) of the operator keyword is stored elsewhere.
  struct CXXLitOpName {
    SourceLocation::UIntTy OpNameLoc;
  };

  // struct {} CXXUsingDirective;
  // struct {} ObjCZeroArgSelector;
  // struct {} ObjCOneArgSelector;
  // struct {} ObjCMultiArgSelector;
  union {
    struct NT NamedType;
    struct CXXOpName CXXOperatorName;
    struct CXXLitOpName CXXLiteralOperatorName;
  };

  void setNamedTypeLoc(TypeSourceInfo *TInfo) { NamedType.TInfo = TInfo; }

  void setCXXOperatorNameRange(SourceRange Range) {
    CXXOperatorName.BeginOpNameLoc = Range.getBegin().getRawEncoding();
    CXXOperatorName.EndOpNameLoc = Range.getEnd().getRawEncoding();
  }

  void setCXXLiteralOperatorNameLoc(SourceLocation Loc) {
    CXXLiteralOperatorName.OpNameLoc = Loc.getRawEncoding();
  }

public:
  DeclarationNameLoc(DeclarationName Name);
  // FIXME: this should go away once all DNLocs are properly initialized.
  DeclarationNameLoc() { memset((void*) this, 0, sizeof(*this)); }

  /// Returns the source type info. Assumes that the object stores location
  /// information of a constructor, destructor or conversion operator.
  TypeSourceInfo *getNamedTypeInfo() const { return NamedType.TInfo; }

  /// Return the beginning location of the getCXXOperatorNameRange() range.
  SourceLocation getCXXOperatorNameBeginLoc() const {
    return SourceLocation::getFromRawEncoding(CXXOperatorName.BeginOpNameLoc);
  }

  /// Return the end location of the getCXXOperatorNameRange() range.
  SourceLocation getCXXOperatorNameEndLoc() const {
    return SourceLocation::getFromRawEncoding(CXXOperatorName.EndOpNameLoc);
  }

  /// Return the range of the operator name (without the operator keyword).
  /// Assumes that the object stores location information of a (non-literal)
  /// operator.
  SourceRange getCXXOperatorNameRange() const {
    return SourceRange(getCXXOperatorNameBeginLoc(),
                       getCXXOperatorNameEndLoc());
  }

  /// Return the location of the literal operator name (without the operator
  /// keyword). Assumes that the object stores location information of a literal
  /// operator.
  SourceLocation getCXXLiteralOperatorNameLoc() const {
    return SourceLocation::getFromRawEncoding(CXXLiteralOperatorName.OpNameLoc);
  }

  /// Construct location information for a constructor, destructor or conversion
  /// operator.
  static DeclarationNameLoc makeNamedTypeLoc(TypeSourceInfo *TInfo) {
    DeclarationNameLoc DNL;
    DNL.setNamedTypeLoc(TInfo);
    return DNL;
  }

  /// Construct location information for a non-literal C++ operator.
  static DeclarationNameLoc makeCXXOperatorNameLoc(SourceLocation BeginLoc,
                                                   SourceLocation EndLoc) {
    return makeCXXOperatorNameLoc(SourceRange(BeginLoc, EndLoc));
  }

  /// Construct location information for a non-literal C++ operator.
  static DeclarationNameLoc makeCXXOperatorNameLoc(SourceRange Range) {
    DeclarationNameLoc DNL;
    DNL.setCXXOperatorNameRange(Range);
    return DNL;
  }

  /// Construct location information for a literal C++ operator.
  static DeclarationNameLoc makeCXXLiteralOperatorNameLoc(SourceLocation Loc) {
    DeclarationNameLoc DNL;
    DNL.setCXXLiteralOperatorNameLoc(Loc);
    return DNL;
  }
};

/// DeclarationNameInfo - A collector data type for bundling together
/// a DeclarationName and the corresponding source/type location info.
struct DeclarationNameInfo {
private:
  /// Name - The declaration name, also encoding name kind.
  DeclarationName Name;

  /// Loc - The main source location for the declaration name.
  SourceLocation NameLoc;

  /// Info - Further source/type location info for special kinds of names.
  DeclarationNameLoc LocInfo;

public:
  // FIXME: remove it.
  DeclarationNameInfo() = default;

  DeclarationNameInfo(DeclarationName Name, SourceLocation NameLoc)
      : Name(Name), NameLoc(NameLoc), LocInfo(Name) {}

  DeclarationNameInfo(DeclarationName Name, SourceLocation NameLoc,
                      DeclarationNameLoc LocInfo)
      : Name(Name), NameLoc(NameLoc), LocInfo(LocInfo) {}

  /// getName - Returns the embedded declaration name.
  DeclarationName getName() const { return Name; }

  /// setName - Sets the embedded declaration name.
  void setName(DeclarationName N) { Name = N; }

  /// getLoc - Returns the main location of the declaration name.
  SourceLocation getLoc() const { return NameLoc; }

  /// setLoc - Sets the main location of the declaration name.
  void setLoc(SourceLocation L) { NameLoc = L; }

  const DeclarationNameLoc &getInfo() const { return LocInfo; }
  void setInfo(const DeclarationNameLoc &Info) { LocInfo = Info; }

  /// getNamedTypeInfo - Returns the source type info associated to
  /// the name. Assumes it is a constructor, destructor or conversion.
  TypeSourceInfo *getNamedTypeInfo() const {
    if (Name.getNameKind() != DeclarationName::CXXConstructorName &&
        Name.getNameKind() != DeclarationName::CXXDestructorName &&
        Name.getNameKind() != DeclarationName::CXXConversionFunctionName)
      return nullptr;
    return LocInfo.getNamedTypeInfo();
  }

  /// setNamedTypeInfo - Sets the source type info associated to
  /// the name. Assumes it is a constructor, destructor or conversion.
  void setNamedTypeInfo(TypeSourceInfo *TInfo) {
    assert(Name.getNameKind() == DeclarationName::CXXConstructorName ||
           Name.getNameKind() == DeclarationName::CXXDestructorName ||
           Name.getNameKind() == DeclarationName::CXXConversionFunctionName);
    LocInfo = DeclarationNameLoc::makeNamedTypeLoc(TInfo);
  }

  /// getCXXOperatorNameRange - Gets the range of the operator name
  /// (without the operator keyword). Assumes it is a (non-literal) operator.
  SourceRange getCXXOperatorNameRange() const {
    if (Name.getNameKind() != DeclarationName::CXXOperatorName)
      return SourceRange();
    return LocInfo.getCXXOperatorNameRange();
  }

  /// setCXXOperatorNameRange - Sets the range of the operator name
  /// (without the operator keyword). Assumes it is a C++ operator.
  void setCXXOperatorNameRange(SourceRange R) {
    assert(Name.getNameKind() == DeclarationName::CXXOperatorName);
    LocInfo = DeclarationNameLoc::makeCXXOperatorNameLoc(R);
  }

  /// getCXXLiteralOperatorNameLoc - Returns the location of the literal
  /// operator name (not the operator keyword).
  /// Assumes it is a literal operator.
  SourceLocation getCXXLiteralOperatorNameLoc() const {
    if (Name.getNameKind() != DeclarationName::CXXLiteralOperatorName)
      return SourceLocation();
    return LocInfo.getCXXLiteralOperatorNameLoc();
  }

  /// setCXXLiteralOperatorNameLoc - Sets the location of the literal
  /// operator name (not the operator keyword).
  /// Assumes it is a literal operator.
  void setCXXLiteralOperatorNameLoc(SourceLocation Loc) {
    assert(Name.getNameKind() == DeclarationName::CXXLiteralOperatorName);
    LocInfo = DeclarationNameLoc::makeCXXLiteralOperatorNameLoc(Loc);
  }

  /// Determine whether this name involves a template parameter.
  bool isInstantiationDependent() const;

  /// Determine whether this name contains an unexpanded
  /// parameter pack.
  bool containsUnexpandedParameterPack() const;

  /// getAsString - Retrieve the human-readable string for this name.
  std::string getAsString() const;

  /// printName - Print the human-readable name to a stream.
  void printName(raw_ostream &OS, PrintingPolicy Policy) const;

  /// getBeginLoc - Retrieve the location of the first token.
  SourceLocation getBeginLoc() const { return NameLoc; }

  /// getSourceRange - The range of the declaration name.
  SourceRange getSourceRange() const LLVM_READONLY {
    return SourceRange(getBeginLoc(), getEndLoc());
  }

  SourceLocation getEndLoc() const LLVM_READONLY {
    SourceLocation EndLoc = getEndLocPrivate();
    return EndLoc.isValid() ? EndLoc : getBeginLoc();
  }

private:
  SourceLocation getEndLocPrivate() const;
};

/// Insertion operator for partial diagnostics.  This allows binding
/// DeclarationName's into a partial diagnostic with <<.
inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &PD,
                                             DeclarationName N) {
  PD.AddTaggedVal(N.getAsOpaqueInteger(),
                  DiagnosticsEngine::ak_declarationname);
  return PD;
}

raw_ostream &operator<<(raw_ostream &OS, DeclarationNameInfo DNInfo);

} // namespace clang

namespace llvm {

/// Define DenseMapInfo so that DeclarationNames can be used as keys
/// in DenseMap and DenseSets.
template<>
struct DenseMapInfo<clang::DeclarationName> {
  static inline clang::DeclarationName getEmptyKey() {
    return clang::DeclarationName::getEmptyMarker();
  }

  static inline clang::DeclarationName getTombstoneKey() {
    return clang::DeclarationName::getTombstoneMarker();
  }

  static unsigned getHashValue(clang::DeclarationName Name) {
    return DenseMapInfo<void*>::getHashValue(Name.getAsOpaquePtr());
  }

  static inline bool
  isEqual(clang::DeclarationName LHS, clang::DeclarationName RHS) {
    return LHS == RHS;
  }
};

template <> struct PointerLikeTypeTraits<clang::DeclarationName> {
  static inline void *getAsVoidPointer(clang::DeclarationName P) {
    return P.getAsOpaquePtr();
  }
  static inline clang::DeclarationName getFromVoidPointer(void *P) {
    return clang::DeclarationName::getFromOpaquePtr(P);
  }
  static constexpr int NumLowBitsAvailable = 0;
};

} // namespace llvm

// The definition of AssumedTemplateStorage is factored out of TemplateName to
// resolve a cyclic dependency between it and DeclarationName (via Type).
namespace clang {

/// A structure for storing the information associated with a name that has
/// been assumed to be a template name (despite finding no TemplateDecls).
class AssumedTemplateStorage : public UncommonTemplateNameStorage {
  friend class ASTContext;

  AssumedTemplateStorage(DeclarationName Name)
      : UncommonTemplateNameStorage(Assumed, 0, 0), Name(Name) {}
  DeclarationName Name;

public:
  /// Get the name of the template.
  DeclarationName getDeclName() const { return Name; }
};

} // namespace clang

#endif // LLVM_CLANG_AST_DECLARATIONNAME_H
