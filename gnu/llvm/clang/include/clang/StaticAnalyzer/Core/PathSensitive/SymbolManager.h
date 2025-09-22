//===- SymbolManager.h - Management of Symbolic Values ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines SymbolManager, a class that manages symbolic values
//  created for use by ExprEngine and related classes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_SYMBOLMANAGER_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_SYMBOLMANAGER_H

#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Basic/LLVM.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/MemRegion.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/StoreRef.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymExpr.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Allocator.h"
#include <cassert>

namespace clang {

class ASTContext;
class Stmt;

namespace ento {

class BasicValueFactory;
class StoreManager;

///A symbol representing the value stored at a MemRegion.
class SymbolRegionValue : public SymbolData {
  const TypedValueRegion *R;

public:
  SymbolRegionValue(SymbolID sym, const TypedValueRegion *r)
      : SymbolData(SymbolRegionValueKind, sym), R(r) {
    assert(r);
    assert(isValidTypeForSymbol(r->getValueType()));
  }

  LLVM_ATTRIBUTE_RETURNS_NONNULL
  const TypedValueRegion* getRegion() const { return R; }

  static void Profile(llvm::FoldingSetNodeID& profile, const TypedValueRegion* R) {
    profile.AddInteger((unsigned) SymbolRegionValueKind);
    profile.AddPointer(R);
  }

  void Profile(llvm::FoldingSetNodeID& profile) override {
    Profile(profile, R);
  }

  StringRef getKindStr() const override;

  void dumpToStream(raw_ostream &os) const override;
  const MemRegion *getOriginRegion() const override { return getRegion(); }

  QualType getType() const override;

  // Implement isa<T> support.
  static bool classof(const SymExpr *SE) {
    return SE->getKind() == SymbolRegionValueKind;
  }
};

/// A symbol representing the result of an expression in the case when we do
/// not know anything about what the expression is.
class SymbolConjured : public SymbolData {
  const Stmt *S;
  QualType T;
  unsigned Count;
  const LocationContext *LCtx;
  const void *SymbolTag;

public:
  SymbolConjured(SymbolID sym, const Stmt *s, const LocationContext *lctx,
                 QualType t, unsigned count, const void *symbolTag)
      : SymbolData(SymbolConjuredKind, sym), S(s), T(t), Count(count),
        LCtx(lctx), SymbolTag(symbolTag) {
    // FIXME: 's' might be a nullptr if we're conducting invalidation
    // that was caused by a destructor call on a temporary object,
    // which has no statement associated with it.
    // Due to this, we might be creating the same invalidation symbol for
    // two different invalidation passes (for two different temporaries).
    assert(lctx);
    assert(isValidTypeForSymbol(t));
  }

  /// It might return null.
  const Stmt *getStmt() const { return S; }
  unsigned getCount() const { return Count; }
  /// It might return null.
  const void *getTag() const { return SymbolTag; }

  QualType getType() const override;

  StringRef getKindStr() const override;

  void dumpToStream(raw_ostream &os) const override;

  static void Profile(llvm::FoldingSetNodeID& profile, const Stmt *S,
                      QualType T, unsigned Count, const LocationContext *LCtx,
                      const void *SymbolTag) {
    profile.AddInteger((unsigned) SymbolConjuredKind);
    profile.AddPointer(S);
    profile.AddPointer(LCtx);
    profile.Add(T);
    profile.AddInteger(Count);
    profile.AddPointer(SymbolTag);
  }

  void Profile(llvm::FoldingSetNodeID& profile) override {
    Profile(profile, S, T, Count, LCtx, SymbolTag);
  }

  // Implement isa<T> support.
  static bool classof(const SymExpr *SE) {
    return SE->getKind() == SymbolConjuredKind;
  }
};

/// A symbol representing the value of a MemRegion whose parent region has
/// symbolic value.
class SymbolDerived : public SymbolData {
  SymbolRef parentSymbol;
  const TypedValueRegion *R;

public:
  SymbolDerived(SymbolID sym, SymbolRef parent, const TypedValueRegion *r)
      : SymbolData(SymbolDerivedKind, sym), parentSymbol(parent), R(r) {
    assert(parent);
    assert(r);
    assert(isValidTypeForSymbol(r->getValueType()));
  }

  LLVM_ATTRIBUTE_RETURNS_NONNULL
  SymbolRef getParentSymbol() const { return parentSymbol; }
  LLVM_ATTRIBUTE_RETURNS_NONNULL
  const TypedValueRegion *getRegion() const { return R; }

  QualType getType() const override;

  StringRef getKindStr() const override;

  void dumpToStream(raw_ostream &os) const override;
  const MemRegion *getOriginRegion() const override { return getRegion(); }

  static void Profile(llvm::FoldingSetNodeID& profile, SymbolRef parent,
                      const TypedValueRegion *r) {
    profile.AddInteger((unsigned) SymbolDerivedKind);
    profile.AddPointer(r);
    profile.AddPointer(parent);
  }

  void Profile(llvm::FoldingSetNodeID& profile) override {
    Profile(profile, parentSymbol, R);
  }

  // Implement isa<T> support.
  static bool classof(const SymExpr *SE) {
    return SE->getKind() == SymbolDerivedKind;
  }
};

/// SymbolExtent - Represents the extent (size in bytes) of a bounded region.
///  Clients should not ask the SymbolManager for a region's extent. Always use
///  SubRegion::getExtent instead -- the value returned may not be a symbol.
class SymbolExtent : public SymbolData {
  const SubRegion *R;

public:
  SymbolExtent(SymbolID sym, const SubRegion *r)
      : SymbolData(SymbolExtentKind, sym), R(r) {
    assert(r);
  }

  LLVM_ATTRIBUTE_RETURNS_NONNULL
  const SubRegion *getRegion() const { return R; }

  QualType getType() const override;

  StringRef getKindStr() const override;

  void dumpToStream(raw_ostream &os) const override;

  static void Profile(llvm::FoldingSetNodeID& profile, const SubRegion *R) {
    profile.AddInteger((unsigned) SymbolExtentKind);
    profile.AddPointer(R);
  }

  void Profile(llvm::FoldingSetNodeID& profile) override {
    Profile(profile, R);
  }

  // Implement isa<T> support.
  static bool classof(const SymExpr *SE) {
    return SE->getKind() == SymbolExtentKind;
  }
};

/// SymbolMetadata - Represents path-dependent metadata about a specific region.
///  Metadata symbols remain live as long as they are marked as in use before
///  dead-symbol sweeping AND their associated regions are still alive.
///  Intended for use by checkers.
class SymbolMetadata : public SymbolData {
  const MemRegion* R;
  const Stmt *S;
  QualType T;
  const LocationContext *LCtx;
  unsigned Count;
  const void *Tag;

public:
  SymbolMetadata(SymbolID sym, const MemRegion* r, const Stmt *s, QualType t,
                 const LocationContext *LCtx, unsigned count, const void *tag)
      : SymbolData(SymbolMetadataKind, sym), R(r), S(s), T(t), LCtx(LCtx),
        Count(count), Tag(tag) {
      assert(r);
      assert(s);
      assert(isValidTypeForSymbol(t));
      assert(LCtx);
      assert(tag);
    }

    LLVM_ATTRIBUTE_RETURNS_NONNULL
    const MemRegion *getRegion() const { return R; }

    LLVM_ATTRIBUTE_RETURNS_NONNULL
    const Stmt *getStmt() const { return S; }

    LLVM_ATTRIBUTE_RETURNS_NONNULL
    const LocationContext *getLocationContext() const { return LCtx; }

    unsigned getCount() const { return Count; }

    LLVM_ATTRIBUTE_RETURNS_NONNULL
    const void *getTag() const { return Tag; }

    QualType getType() const override;

    StringRef getKindStr() const override;

    void dumpToStream(raw_ostream &os) const override;

    static void Profile(llvm::FoldingSetNodeID &profile, const MemRegion *R,
                        const Stmt *S, QualType T, const LocationContext *LCtx,
                        unsigned Count, const void *Tag) {
      profile.AddInteger((unsigned)SymbolMetadataKind);
      profile.AddPointer(R);
      profile.AddPointer(S);
      profile.Add(T);
      profile.AddPointer(LCtx);
      profile.AddInteger(Count);
      profile.AddPointer(Tag);
    }

  void Profile(llvm::FoldingSetNodeID& profile) override {
    Profile(profile, R, S, T, LCtx, Count, Tag);
  }

  // Implement isa<T> support.
  static bool classof(const SymExpr *SE) {
    return SE->getKind() == SymbolMetadataKind;
  }
};

/// Represents a cast expression.
class SymbolCast : public SymExpr {
  const SymExpr *Operand;

  /// Type of the operand.
  QualType FromTy;

  /// The type of the result.
  QualType ToTy;

public:
  SymbolCast(const SymExpr *In, QualType From, QualType To)
      : SymExpr(SymbolCastKind), Operand(In), FromTy(From), ToTy(To) {
    assert(In);
    assert(isValidTypeForSymbol(From));
    // FIXME: GenericTaintChecker creates symbols of void type.
    // Otherwise, 'To' should also be a valid type.
  }

  unsigned computeComplexity() const override {
    if (Complexity == 0)
      Complexity = 1 + Operand->computeComplexity();
    return Complexity;
  }

  QualType getType() const override { return ToTy; }

  LLVM_ATTRIBUTE_RETURNS_NONNULL
  const SymExpr *getOperand() const { return Operand; }

  void dumpToStream(raw_ostream &os) const override;

  static void Profile(llvm::FoldingSetNodeID& ID,
                      const SymExpr *In, QualType From, QualType To) {
    ID.AddInteger((unsigned) SymbolCastKind);
    ID.AddPointer(In);
    ID.Add(From);
    ID.Add(To);
  }

  void Profile(llvm::FoldingSetNodeID& ID) override {
    Profile(ID, Operand, FromTy, ToTy);
  }

  // Implement isa<T> support.
  static bool classof(const SymExpr *SE) {
    return SE->getKind() == SymbolCastKind;
  }
};

/// Represents a symbolic expression involving a unary operator.
class UnarySymExpr : public SymExpr {
  const SymExpr *Operand;
  UnaryOperator::Opcode Op;
  QualType T;

public:
  UnarySymExpr(const SymExpr *In, UnaryOperator::Opcode Op, QualType T)
      : SymExpr(UnarySymExprKind), Operand(In), Op(Op), T(T) {
    // Note, some unary operators are modeled as a binary operator. E.g. ++x is
    // modeled as x + 1.
    assert((Op == UO_Minus || Op == UO_Not) && "non-supported unary expression");
    // Unary expressions are results of arithmetic. Pointer arithmetic is not
    // handled by unary expressions, but it is instead handled by applying
    // sub-regions to regions.
    assert(isValidTypeForSymbol(T) && "non-valid type for unary symbol");
    assert(!Loc::isLocType(T) && "unary symbol should be nonloc");
  }

  unsigned computeComplexity() const override {
    if (Complexity == 0)
      Complexity = 1 + Operand->computeComplexity();
    return Complexity;
  }

  const SymExpr *getOperand() const { return Operand; }
  UnaryOperator::Opcode getOpcode() const { return Op; }
  QualType getType() const override { return T; }

  void dumpToStream(raw_ostream &os) const override;

  static void Profile(llvm::FoldingSetNodeID &ID, const SymExpr *In,
                      UnaryOperator::Opcode Op, QualType T) {
    ID.AddInteger((unsigned)UnarySymExprKind);
    ID.AddPointer(In);
    ID.AddInteger(Op);
    ID.Add(T);
  }

  void Profile(llvm::FoldingSetNodeID &ID) override {
    Profile(ID, Operand, Op, T);
  }

  // Implement isa<T> support.
  static bool classof(const SymExpr *SE) {
    return SE->getKind() == UnarySymExprKind;
  }
};

/// Represents a symbolic expression involving a binary operator
class BinarySymExpr : public SymExpr {
  BinaryOperator::Opcode Op;
  QualType T;

protected:
  BinarySymExpr(Kind k, BinaryOperator::Opcode op, QualType t)
      : SymExpr(k), Op(op), T(t) {
    assert(classof(this));
    // Binary expressions are results of arithmetic. Pointer arithmetic is not
    // handled by binary expressions, but it is instead handled by applying
    // sub-regions to regions.
    assert(isValidTypeForSymbol(t) && !Loc::isLocType(t));
  }

public:
  // FIXME: We probably need to make this out-of-line to avoid redundant
  // generation of virtual functions.
  QualType getType() const override { return T; }

  BinaryOperator::Opcode getOpcode() const { return Op; }

  // Implement isa<T> support.
  static bool classof(const SymExpr *SE) {
    Kind k = SE->getKind();
    return k >= BEGIN_BINARYSYMEXPRS && k <= END_BINARYSYMEXPRS;
  }

protected:
  static unsigned computeOperandComplexity(const SymExpr *Value) {
    return Value->computeComplexity();
  }
  static unsigned computeOperandComplexity(const llvm::APSInt &Value) {
    return 1;
  }

  static const llvm::APSInt *getPointer(const llvm::APSInt &Value) {
    return &Value;
  }
  static const SymExpr *getPointer(const SymExpr *Value) { return Value; }

  static void dumpToStreamImpl(raw_ostream &os, const SymExpr *Value);
  static void dumpToStreamImpl(raw_ostream &os, const llvm::APSInt &Value);
  static void dumpToStreamImpl(raw_ostream &os, BinaryOperator::Opcode op);
};

/// Template implementation for all binary symbolic expressions
template <class LHSTYPE, class RHSTYPE, SymExpr::Kind ClassKind>
class BinarySymExprImpl : public BinarySymExpr {
  LHSTYPE LHS;
  RHSTYPE RHS;

public:
  BinarySymExprImpl(LHSTYPE lhs, BinaryOperator::Opcode op, RHSTYPE rhs,
                    QualType t)
      : BinarySymExpr(ClassKind, op, t), LHS(lhs), RHS(rhs) {
    assert(getPointer(lhs));
    assert(getPointer(rhs));
  }

  void dumpToStream(raw_ostream &os) const override {
    dumpToStreamImpl(os, LHS);
    dumpToStreamImpl(os, getOpcode());
    dumpToStreamImpl(os, RHS);
  }

  LHSTYPE getLHS() const { return LHS; }
  RHSTYPE getRHS() const { return RHS; }

  unsigned computeComplexity() const override {
    if (Complexity == 0)
      Complexity =
          computeOperandComplexity(RHS) + computeOperandComplexity(LHS);
    return Complexity;
  }

  static void Profile(llvm::FoldingSetNodeID &ID, LHSTYPE lhs,
                      BinaryOperator::Opcode op, RHSTYPE rhs, QualType t) {
    ID.AddInteger((unsigned)ClassKind);
    ID.AddPointer(getPointer(lhs));
    ID.AddInteger(op);
    ID.AddPointer(getPointer(rhs));
    ID.Add(t);
  }

  void Profile(llvm::FoldingSetNodeID &ID) override {
    Profile(ID, LHS, getOpcode(), RHS, getType());
  }

  // Implement isa<T> support.
  static bool classof(const SymExpr *SE) { return SE->getKind() == ClassKind; }
};

/// Represents a symbolic expression like 'x' + 3.
using SymIntExpr = BinarySymExprImpl<const SymExpr *, const llvm::APSInt &,
                                     SymExpr::Kind::SymIntExprKind>;

/// Represents a symbolic expression like 3 - 'x'.
using IntSymExpr = BinarySymExprImpl<const llvm::APSInt &, const SymExpr *,
                                     SymExpr::Kind::IntSymExprKind>;

/// Represents a symbolic expression like 'x' + 'y'.
using SymSymExpr = BinarySymExprImpl<const SymExpr *, const SymExpr *,
                                     SymExpr::Kind::SymSymExprKind>;

class SymbolManager {
  using DataSetTy = llvm::FoldingSet<SymExpr>;
  using SymbolDependTy =
      llvm::DenseMap<SymbolRef, std::unique_ptr<SymbolRefSmallVectorTy>>;

  DataSetTy DataSet;

  /// Stores the extra dependencies between symbols: the data should be kept
  /// alive as long as the key is live.
  SymbolDependTy SymbolDependencies;

  unsigned SymbolCounter = 0;
  llvm::BumpPtrAllocator& BPAlloc;
  BasicValueFactory &BV;
  ASTContext &Ctx;

public:
  SymbolManager(ASTContext &ctx, BasicValueFactory &bv,
                llvm::BumpPtrAllocator& bpalloc)
      : SymbolDependencies(16), BPAlloc(bpalloc), BV(bv), Ctx(ctx) {}

  static bool canSymbolicate(QualType T);

  /// Make a unique symbol for MemRegion R according to its kind.
  const SymbolRegionValue* getRegionValueSymbol(const TypedValueRegion* R);

  const SymbolConjured* conjureSymbol(const Stmt *E,
                                      const LocationContext *LCtx,
                                      QualType T,
                                      unsigned VisitCount,
                                      const void *SymbolTag = nullptr);

  const SymbolConjured* conjureSymbol(const Expr *E,
                                      const LocationContext *LCtx,
                                      unsigned VisitCount,
                                      const void *SymbolTag = nullptr) {
    return conjureSymbol(E, LCtx, E->getType(), VisitCount, SymbolTag);
  }

  const SymbolDerived *getDerivedSymbol(SymbolRef parentSymbol,
                                        const TypedValueRegion *R);

  const SymbolExtent *getExtentSymbol(const SubRegion *R);

  /// Creates a metadata symbol associated with a specific region.
  ///
  /// VisitCount can be used to differentiate regions corresponding to
  /// different loop iterations, thus, making the symbol path-dependent.
  const SymbolMetadata *getMetadataSymbol(const MemRegion *R, const Stmt *S,
                                          QualType T,
                                          const LocationContext *LCtx,
                                          unsigned VisitCount,
                                          const void *SymbolTag = nullptr);

  const SymbolCast* getCastSymbol(const SymExpr *Operand,
                                  QualType From, QualType To);

  const SymIntExpr *getSymIntExpr(const SymExpr *lhs, BinaryOperator::Opcode op,
                                  const llvm::APSInt& rhs, QualType t);

  const SymIntExpr *getSymIntExpr(const SymExpr &lhs, BinaryOperator::Opcode op,
                                  const llvm::APSInt& rhs, QualType t) {
    return getSymIntExpr(&lhs, op, rhs, t);
  }

  const IntSymExpr *getIntSymExpr(const llvm::APSInt& lhs,
                                  BinaryOperator::Opcode op,
                                  const SymExpr *rhs, QualType t);

  const SymSymExpr *getSymSymExpr(const SymExpr *lhs, BinaryOperator::Opcode op,
                                  const SymExpr *rhs, QualType t);

  const UnarySymExpr *getUnarySymExpr(const SymExpr *operand,
                                      UnaryOperator::Opcode op, QualType t);

  QualType getType(const SymExpr *SE) const {
    return SE->getType();
  }

  /// Add artificial symbol dependency.
  ///
  /// The dependent symbol should stay alive as long as the primary is alive.
  void addSymbolDependency(const SymbolRef Primary, const SymbolRef Dependent);

  const SymbolRefSmallVectorTy *getDependentSymbols(const SymbolRef Primary);

  ASTContext &getContext() { return Ctx; }
  BasicValueFactory &getBasicVals() { return BV; }
};

/// A class responsible for cleaning up unused symbols.
class SymbolReaper {
  enum SymbolStatus {
    NotProcessed,
    HaveMarkedDependents
  };

  using SymbolSetTy = llvm::DenseSet<SymbolRef>;
  using SymbolMapTy = llvm::DenseMap<SymbolRef, SymbolStatus>;
  using RegionSetTy = llvm::DenseSet<const MemRegion *>;

  SymbolMapTy TheLiving;
  SymbolSetTy MetadataInUse;

  RegionSetTy LiveRegionRoots;
  // The lazily copied regions are locations for which a program
  // can access the value stored at that location, but not its address.
  // These regions are constructed as a set of regions referred to by
  // lazyCompoundVal.
  RegionSetTy LazilyCopiedRegionRoots;

  const StackFrameContext *LCtx;
  const Stmt *Loc;
  SymbolManager& SymMgr;
  StoreRef reapedStore;
  llvm::DenseMap<const MemRegion *, unsigned> includedRegionCache;

public:
  /// Construct a reaper object, which removes everything which is not
  /// live before we execute statement s in the given location context.
  ///
  /// If the statement is NULL, everything is this and parent contexts is
  /// considered live.
  /// If the stack frame context is NULL, everything on stack is considered
  /// dead.
  SymbolReaper(const StackFrameContext *Ctx, const Stmt *s,
               SymbolManager &symmgr, StoreManager &storeMgr)
      : LCtx(Ctx), Loc(s), SymMgr(symmgr), reapedStore(nullptr, storeMgr) {}

  /// It might return null.
  const LocationContext *getLocationContext() const { return LCtx; }

  bool isLive(SymbolRef sym);
  bool isLiveRegion(const MemRegion *region);
  bool isLive(const Expr *ExprVal, const LocationContext *LCtx) const;
  bool isLive(const VarRegion *VR, bool includeStoreBindings = false) const;

  /// Unconditionally marks a symbol as live.
  ///
  /// This should never be
  /// used by checkers, only by the state infrastructure such as the store and
  /// environment. Checkers should instead use metadata symbols and markInUse.
  void markLive(SymbolRef sym);

  /// Marks a symbol as important to a checker.
  ///
  /// For metadata symbols,
  /// this will keep the symbol alive as long as its associated region is also
  /// live. For other symbols, this has no effect; checkers are not permitted
  /// to influence the life of other symbols. This should be used before any
  /// symbol marking has occurred, i.e. in the MarkLiveSymbols callback.
  void markInUse(SymbolRef sym);

  llvm::iterator_range<RegionSetTy::const_iterator> regions() const {
    return LiveRegionRoots;
  }

  /// Returns whether or not a symbol has been confirmed dead.
  ///
  /// This should only be called once all marking of dead symbols has completed.
  /// (For checkers, this means only in the checkDeadSymbols callback.)
  bool isDead(SymbolRef sym) {
    return !isLive(sym);
  }

  void markLive(const MemRegion *region);
  void markLazilyCopied(const MemRegion *region);
  void markElementIndicesLive(const MemRegion *region);

  /// Set to the value of the symbolic store after
  /// StoreManager::removeDeadBindings has been called.
  void setReapedStore(StoreRef st) { reapedStore = st; }

private:
  bool isLazilyCopiedRegion(const MemRegion *region) const;
  // A readable region is a region that live or lazily copied.
  // Any symbols that refer to values in regions are alive if the region
  // is readable.
  bool isReadableRegion(const MemRegion *region);

  /// Mark the symbols dependent on the input symbol as live.
  void markDependentsLive(SymbolRef sym);
};

class SymbolVisitor {
protected:
  ~SymbolVisitor() = default;

public:
  SymbolVisitor() = default;
  SymbolVisitor(const SymbolVisitor &) = default;
  SymbolVisitor(SymbolVisitor &&) {}

  // The copy and move assignment operator is defined as deleted pending further
  // motivation.
  SymbolVisitor &operator=(const SymbolVisitor &) = delete;
  SymbolVisitor &operator=(SymbolVisitor &&) = delete;

  /// A visitor method invoked by ProgramStateManager::scanReachableSymbols.
  ///
  /// The method returns \c true if symbols should continue be scanned and \c
  /// false otherwise.
  virtual bool VisitSymbol(SymbolRef sym) = 0;
  virtual bool VisitMemRegion(const MemRegion *) { return true; }
};

} // namespace ento

} // namespace clang

#endif // LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_SYMBOLMANAGER_H
