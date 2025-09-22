//===- SVals.h - Abstract Values for Static Analysis ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines SVal, Loc, and NonLoc, classes that represent
//  abstract r-values for use with path-sensitive value tracking.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_SVALS_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_SVALS_H

#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/Basic/LLVM.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymExpr.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/ImmutableList.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Casting.h"
#include <cassert>
#include <cstdint>
#include <optional>
#include <utility>

//==------------------------------------------------------------------------==//
//  Base SVal types.
//==------------------------------------------------------------------------==//

namespace clang {

class CXXBaseSpecifier;
class FunctionDecl;
class LabelDecl;

namespace ento {

class CompoundValData;
class LazyCompoundValData;
class MemRegion;
class PointerToMemberData;
class SValBuilder;
class TypedValueRegion;

/// SVal - This represents a symbolic expression, which can be either
///  an L-value or an R-value.
///
class SVal {
public:
  enum SValKind : unsigned char {
#define BASIC_SVAL(Id, Parent) Id##Kind,
#define LOC_SVAL(Id, Parent) Loc##Id##Kind,
#define NONLOC_SVAL(Id, Parent) NonLoc##Id##Kind,
#define SVAL_RANGE(Id, First, Last)                                            \
  BEGIN_##Id = Id##First##Kind, END_##Id = Id##Last##Kind,
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.def"
  };

protected:
  const void *Data = nullptr;
  SValKind Kind = UndefinedValKind;

  explicit SVal(SValKind Kind, const void *Data = nullptr)
      : Data(Data), Kind(Kind) {}

  template <typename T> const T *castDataAs() const {
    return static_cast<const T *>(Data);
  }

public:
  explicit SVal() = default;

  /// Convert to the specified SVal type, asserting that this SVal is of
  /// the desired type.
  template <typename T> T castAs() const { return llvm::cast<T>(*this); }

  /// Convert to the specified SVal type, returning std::nullopt if this SVal is
  /// not of the desired type.
  template <typename T> std::optional<T> getAs() const {
    return llvm::dyn_cast<T>(*this);
  }

  SValKind getKind() const { return Kind; }

  // This method is required for using SVal in a FoldingSetNode.  It
  // extracts a unique signature for this SVal object.
  void Profile(llvm::FoldingSetNodeID &ID) const {
    ID.AddPointer(Data);
    ID.AddInteger(llvm::to_underlying(getKind()));
  }

  bool operator==(SVal R) const { return Kind == R.Kind && Data == R.Data; }
  bool operator!=(SVal R) const { return !(*this == R); }

  bool isUnknown() const { return getKind() == UnknownValKind; }

  bool isUndef() const { return getKind() == UndefinedValKind; }

  bool isUnknownOrUndef() const { return isUnknown() || isUndef(); }

  bool isValid() const { return !isUnknownOrUndef(); }

  bool isConstant() const;

  bool isConstant(int I) const;

  bool isZeroConstant() const;

  /// getAsFunctionDecl - If this SVal is a MemRegionVal and wraps a
  /// CodeTextRegion wrapping a FunctionDecl, return that FunctionDecl.
  /// Otherwise return 0.
  const FunctionDecl *getAsFunctionDecl() const;

  /// If this SVal is a location and wraps a symbol, return that
  ///  SymbolRef. Otherwise return 0.
  ///
  /// Casts are ignored during lookup.
  /// \param IncludeBaseRegions The boolean that controls whether the search
  /// should continue to the base regions if the region is not symbolic.
  SymbolRef getAsLocSymbol(bool IncludeBaseRegions = false) const;

  /// Get the symbol in the SVal or its base region.
  SymbolRef getLocSymbolInBase() const;

  /// If this SVal wraps a symbol return that SymbolRef.
  /// Otherwise, return 0.
  ///
  /// Casts are ignored during lookup.
  /// \param IncludeBaseRegions The boolean that controls whether the search
  /// should continue to the base regions if the region is not symbolic.
  SymbolRef getAsSymbol(bool IncludeBaseRegions = false) const;

  /// If this SVal is loc::ConcreteInt or nonloc::ConcreteInt,
  /// return a pointer to APSInt which is held in it.
  /// Otherwise, return nullptr.
  const llvm::APSInt *getAsInteger() const;

  const MemRegion *getAsRegion() const;

  /// printJson - Pretty-prints in JSON format.
  void printJson(raw_ostream &Out, bool AddQuotes) const;

  void dumpToStream(raw_ostream &OS) const;
  void dump() const;

  llvm::iterator_range<SymExpr::symbol_iterator> symbols() const {
    if (const SymExpr *SE = getAsSymbol(/*IncludeBaseRegions=*/true))
      return SE->symbols();
    SymExpr::symbol_iterator end{};
    return llvm::make_range(end, end);
  }

  /// Try to get a reasonable type for the given value.
  ///
  /// \returns The best approximation of the value type or Null.
  /// In theory, all symbolic values should be typed, but this function
  /// is still a WIP and might have a few blind spots.
  ///
  /// \note This function should not be used when the user has access to the
  /// bound expression AST node as well, since AST always has exact types.
  ///
  /// \note Loc values are interpreted as pointer rvalues for the purposes of
  /// this method.
  QualType getType(const ASTContext &) const;
};

inline raw_ostream &operator<<(raw_ostream &os, clang::ento::SVal V) {
  V.dumpToStream(os);
  return os;
}

namespace nonloc {
/// Sub-kinds for NonLoc values.
#define NONLOC_SVAL(Id, Parent)                                                \
  inline constexpr auto Id##Kind = SVal::SValKind::NonLoc##Id##Kind;
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.def"
} // namespace nonloc

namespace loc {
/// Sub-kinds for Loc values.
#define LOC_SVAL(Id, Parent)                                                   \
  inline constexpr auto Id##Kind = SVal::SValKind::Loc##Id##Kind;
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.def"
} // namespace loc

class UndefinedVal : public SVal {
public:
  UndefinedVal() : SVal(UndefinedValKind) {}
  static bool classof(SVal V) { return V.getKind() == UndefinedValKind; }
};

class DefinedOrUnknownSVal : public SVal {
public:
  // We want calling these methods to be a compiler error since they are
  // tautologically false.
  bool isUndef() const = delete;
  bool isValid() const = delete;

  static bool classof(SVal V) { return !V.isUndef(); }

protected:
  explicit DefinedOrUnknownSVal(SValKind Kind, const void *Data = nullptr)
      : SVal(Kind, Data) {}
};

class UnknownVal : public DefinedOrUnknownSVal {
public:
  explicit UnknownVal() : DefinedOrUnknownSVal(UnknownValKind) {}

  static bool classof(SVal V) { return V.getKind() == UnknownValKind; }
};

class DefinedSVal : public DefinedOrUnknownSVal {
public:
  // We want calling these methods to be a compiler error since they are
  // tautologically true/false.
  bool isUnknown() const = delete;
  bool isUnknownOrUndef() const = delete;
  bool isValid() const = delete;

  static bool classof(SVal V) { return !V.isUnknownOrUndef(); }

protected:
  explicit DefinedSVal(SValKind Kind, const void *Data)
      : DefinedOrUnknownSVal(Kind, Data) {}
};

class NonLoc : public DefinedSVal {
protected:
  NonLoc(SValKind Kind, const void *Data) : DefinedSVal(Kind, Data) {}

public:
  void dumpToStream(raw_ostream &Out) const;

  static bool isCompoundType(QualType T) {
    return T->isArrayType() || T->isRecordType() ||
           T->isAnyComplexType() || T->isVectorType();
  }

  static bool classof(SVal V) {
    return BEGIN_NonLoc <= V.getKind() && V.getKind() <= END_NonLoc;
  }
};

class Loc : public DefinedSVal {
protected:
  Loc(SValKind Kind, const void *Data) : DefinedSVal(Kind, Data) {}

public:
  void dumpToStream(raw_ostream &Out) const;

  static bool isLocType(QualType T) {
    return T->isAnyPointerType() || T->isBlockPointerType() ||
           T->isReferenceType() || T->isNullPtrType();
  }

  static bool classof(SVal V) {
    return BEGIN_Loc <= V.getKind() && V.getKind() <= END_Loc;
  }
};

//==------------------------------------------------------------------------==//
//  Subclasses of NonLoc.
//==------------------------------------------------------------------------==//

namespace nonloc {

/// Represents symbolic expression that isn't a location.
class SymbolVal : public NonLoc {
public:
  SymbolVal() = delete;
  explicit SymbolVal(SymbolRef Sym) : NonLoc(SymbolValKind, Sym) {
    assert(Sym);
    assert(!Loc::isLocType(Sym->getType()));
  }

  LLVM_ATTRIBUTE_RETURNS_NONNULL
  SymbolRef getSymbol() const {
    return (const SymExpr *) Data;
  }

  bool isExpression() const {
    return !isa<SymbolData>(getSymbol());
  }

  static bool classof(SVal V) { return V.getKind() == SymbolValKind; }
};

/// Value representing integer constant.
class ConcreteInt : public NonLoc {
public:
  explicit ConcreteInt(const llvm::APSInt &V) : NonLoc(ConcreteIntKind, &V) {}

  const llvm::APSInt &getValue() const { return *castDataAs<llvm::APSInt>(); }

  static bool classof(SVal V) { return V.getKind() == ConcreteIntKind; }
};

class LocAsInteger : public NonLoc {
  friend class ento::SValBuilder;

  explicit LocAsInteger(const std::pair<SVal, uintptr_t> &data)
      : NonLoc(LocAsIntegerKind, &data) {
    // We do not need to represent loc::ConcreteInt as LocAsInteger,
    // as it'd collapse into a nonloc::ConcreteInt instead.
    [[maybe_unused]] SValKind K = data.first.getKind();
    assert(K == loc::MemRegionValKind || K == loc::GotoLabelKind);
  }

public:
  Loc getLoc() const {
    return castDataAs<std::pair<SVal, uintptr_t>>()->first.castAs<Loc>();
  }

  unsigned getNumBits() const {
    return castDataAs<std::pair<SVal, uintptr_t>>()->second;
  }

  static bool classof(SVal V) { return V.getKind() == LocAsIntegerKind; }
};

class CompoundVal : public NonLoc {
  friend class ento::SValBuilder;

  explicit CompoundVal(const CompoundValData *D) : NonLoc(CompoundValKind, D) {
    assert(D);
  }

public:
  LLVM_ATTRIBUTE_RETURNS_NONNULL
  const CompoundValData* getValue() const {
    return castDataAs<CompoundValData>();
  }

  using iterator = llvm::ImmutableList<SVal>::iterator;
  iterator begin() const;
  iterator end() const;

  static bool classof(SVal V) { return V.getKind() == CompoundValKind; }
};

class LazyCompoundVal : public NonLoc {
  friend class ento::SValBuilder;

  explicit LazyCompoundVal(const LazyCompoundValData *D)
      : NonLoc(LazyCompoundValKind, D) {
    assert(D);
  }

public:
  LLVM_ATTRIBUTE_RETURNS_NONNULL
  const LazyCompoundValData *getCVData() const {
    return castDataAs<LazyCompoundValData>();
  }

  /// It might return null.
  const void *getStore() const;

  LLVM_ATTRIBUTE_RETURNS_NONNULL
  const TypedValueRegion *getRegion() const;

  static bool classof(SVal V) { return V.getKind() == LazyCompoundValKind; }
};

/// Value representing pointer-to-member.
///
/// This value is qualified as NonLoc because neither loading nor storing
/// operations are applied to it. Instead, the analyzer uses the L-value coming
/// from pointer-to-member applied to an object.
/// This SVal is represented by a NamedDecl which can be a member function
/// pointer or a member data pointer and an optional list of CXXBaseSpecifiers.
/// This list is required to accumulate the pointer-to-member cast history to
/// figure out the correct subobject field. In particular, implicit casts grow
/// this list and explicit casts like static_cast shrink this list.
class PointerToMember : public NonLoc {
  friend class ento::SValBuilder;

public:
  using PTMDataType =
      llvm::PointerUnion<const NamedDecl *, const PointerToMemberData *>;

  const PTMDataType getPTMData() const {
    return PTMDataType::getFromOpaqueValue(const_cast<void *>(Data));
  }

  bool isNullMemberPointer() const;

  const NamedDecl *getDecl() const;

  template<typename AdjustedDecl>
  const AdjustedDecl *getDeclAs() const {
    return dyn_cast_or_null<AdjustedDecl>(getDecl());
  }

  using iterator = llvm::ImmutableList<const CXXBaseSpecifier *>::iterator;

  iterator begin() const;
  iterator end() const;

  static bool classof(SVal V) { return V.getKind() == PointerToMemberKind; }

private:
  explicit PointerToMember(const PTMDataType D)
      : NonLoc(PointerToMemberKind, D.getOpaqueValue()) {}
};

} // namespace nonloc

//==------------------------------------------------------------------------==//
//  Subclasses of Loc.
//==------------------------------------------------------------------------==//

namespace loc {

class GotoLabel : public Loc {
public:
  explicit GotoLabel(const LabelDecl *Label) : Loc(GotoLabelKind, Label) {
    assert(Label);
  }

  const LabelDecl *getLabel() const { return castDataAs<LabelDecl>(); }

  static bool classof(SVal V) { return V.getKind() == GotoLabelKind; }
};

class MemRegionVal : public Loc {
public:
  explicit MemRegionVal(const MemRegion *r) : Loc(MemRegionValKind, r) {
    assert(r);
  }

  /// Get the underlining region.
  LLVM_ATTRIBUTE_RETURNS_NONNULL
  const MemRegion *getRegion() const { return castDataAs<MemRegion>(); }

  /// Get the underlining region and strip casts.
  LLVM_ATTRIBUTE_RETURNS_NONNULL
  const MemRegion* stripCasts(bool StripBaseCasts = true) const;

  template <typename REGION>
  const REGION* getRegionAs() const {
    return dyn_cast<REGION>(getRegion());
  }

  bool operator==(const MemRegionVal &R) const {
    return getRegion() == R.getRegion();
  }

  bool operator!=(const MemRegionVal &R) const {
    return getRegion() != R.getRegion();
  }

  static bool classof(SVal V) { return V.getKind() == MemRegionValKind; }
};

class ConcreteInt : public Loc {
public:
  explicit ConcreteInt(const llvm::APSInt &V) : Loc(ConcreteIntKind, &V) {}

  const llvm::APSInt &getValue() const { return *castDataAs<llvm::APSInt>(); }

  static bool classof(SVal V) { return V.getKind() == ConcreteIntKind; }
};

} // namespace loc
} // namespace ento
} // namespace clang

namespace llvm {
template <typename To, typename From>
struct CastInfo<
    To, From,
    std::enable_if_t<std::is_base_of<::clang::ento::SVal, From>::value>>
    : public CastIsPossible<To, ::clang::ento::SVal> {
  using Self = CastInfo<
      To, From,
      std::enable_if_t<std::is_base_of<::clang::ento::SVal, From>::value>>;
  static bool isPossible(const From &V) {
    return To::classof(*static_cast<const ::clang::ento::SVal *>(&V));
  }
  static std::optional<To> castFailed() { return std::optional<To>{}; }
  static To doCast(const From &f) {
    return *static_cast<const To *>(cast<::clang::ento::SVal>(&f));
  }
  static std::optional<To> doCastIfPossible(const From &f) {
    if (!Self::isPossible(f))
      return Self::castFailed();
    return doCast(f);
  }
};
} // namespace llvm

#endif // LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_SVALS_H
