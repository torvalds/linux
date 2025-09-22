//== RegionStore.cpp - Field-sensitive store model --------------*- C++ -*--==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a basic region store model. In this model, we do have field
// sensitivity. But we assume nothing about the heap shape. So recursive data
// structures are largely ignored. Basically we do 1-limiting analysis.
// Parameter pointers are assumed with no aliasing. Pointee objects of
// parameters are created lazily.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/Attr.h"
#include "clang/AST/CharUnits.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Analysis/Analyses/LiveVariables.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Basic/JsonSupport.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExprEngine.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/MemRegion.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "llvm/ADT/ImmutableMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>
#include <utility>

using namespace clang;
using namespace ento;

//===----------------------------------------------------------------------===//
// Representation of binding keys.
//===----------------------------------------------------------------------===//

namespace {
class BindingKey {
public:
  enum Kind { Default = 0x0, Direct = 0x1 };
private:
  enum { Symbolic = 0x2 };

  llvm::PointerIntPair<const MemRegion *, 2> P;
  uint64_t Data;

  /// Create a key for a binding to region \p r, which has a symbolic offset
  /// from region \p Base.
  explicit BindingKey(const SubRegion *r, const SubRegion *Base, Kind k)
    : P(r, k | Symbolic), Data(reinterpret_cast<uintptr_t>(Base)) {
    assert(r && Base && "Must have known regions.");
    assert(getConcreteOffsetRegion() == Base && "Failed to store base region");
  }

  /// Create a key for a binding at \p offset from base region \p r.
  explicit BindingKey(const MemRegion *r, uint64_t offset, Kind k)
    : P(r, k), Data(offset) {
    assert(r && "Must have known regions.");
    assert(getOffset() == offset && "Failed to store offset");
    assert((r == r->getBaseRegion() ||
            isa<ObjCIvarRegion, CXXDerivedObjectRegion>(r)) &&
           "Not a base");
  }
public:

  bool isDirect() const { return P.getInt() & Direct; }
  bool hasSymbolicOffset() const { return P.getInt() & Symbolic; }

  const MemRegion *getRegion() const { return P.getPointer(); }
  uint64_t getOffset() const {
    assert(!hasSymbolicOffset());
    return Data;
  }

  const SubRegion *getConcreteOffsetRegion() const {
    assert(hasSymbolicOffset());
    return reinterpret_cast<const SubRegion *>(static_cast<uintptr_t>(Data));
  }

  const MemRegion *getBaseRegion() const {
    if (hasSymbolicOffset())
      return getConcreteOffsetRegion()->getBaseRegion();
    return getRegion()->getBaseRegion();
  }

  void Profile(llvm::FoldingSetNodeID& ID) const {
    ID.AddPointer(P.getOpaqueValue());
    ID.AddInteger(Data);
  }

  static BindingKey Make(const MemRegion *R, Kind k);

  bool operator<(const BindingKey &X) const {
    if (P.getOpaqueValue() < X.P.getOpaqueValue())
      return true;
    if (P.getOpaqueValue() > X.P.getOpaqueValue())
      return false;
    return Data < X.Data;
  }

  bool operator==(const BindingKey &X) const {
    return P.getOpaqueValue() == X.P.getOpaqueValue() &&
           Data == X.Data;
  }

  LLVM_DUMP_METHOD void dump() const;
};
} // end anonymous namespace

BindingKey BindingKey::Make(const MemRegion *R, Kind k) {
  const RegionOffset &RO = R->getAsOffset();
  if (RO.hasSymbolicOffset())
    return BindingKey(cast<SubRegion>(R), cast<SubRegion>(RO.getRegion()), k);

  return BindingKey(RO.getRegion(), RO.getOffset(), k);
}

namespace llvm {
static inline raw_ostream &operator<<(raw_ostream &Out, BindingKey K) {
  Out << "\"kind\": \"" << (K.isDirect() ? "Direct" : "Default")
      << "\", \"offset\": ";

  if (!K.hasSymbolicOffset())
    Out << K.getOffset();
  else
    Out << "null";

  return Out;
}

} // namespace llvm

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void BindingKey::dump() const { llvm::errs() << *this; }
#endif

//===----------------------------------------------------------------------===//
// Actual Store type.
//===----------------------------------------------------------------------===//

typedef llvm::ImmutableMap<BindingKey, SVal>    ClusterBindings;
typedef llvm::ImmutableMapRef<BindingKey, SVal> ClusterBindingsRef;
typedef std::pair<BindingKey, SVal> BindingPair;

typedef llvm::ImmutableMap<const MemRegion *, ClusterBindings>
        RegionBindings;

namespace {
class RegionBindingsRef : public llvm::ImmutableMapRef<const MemRegion *,
                                 ClusterBindings> {
  ClusterBindings::Factory *CBFactory;

  // This flag indicates whether the current bindings are within the analysis
  // that has started from main(). It affects how we perform loads from
  // global variables that have initializers: if we have observed the
  // program execution from the start and we know that these variables
  // have not been overwritten yet, we can be sure that their initializers
  // are still relevant. This flag never gets changed when the bindings are
  // updated, so it could potentially be moved into RegionStoreManager
  // (as if it's the same bindings but a different loading procedure)
  // however that would have made the manager needlessly stateful.
  bool IsMainAnalysis;

public:
  typedef llvm::ImmutableMapRef<const MemRegion *, ClusterBindings>
          ParentTy;

  RegionBindingsRef(ClusterBindings::Factory &CBFactory,
                    const RegionBindings::TreeTy *T,
                    RegionBindings::TreeTy::Factory *F,
                    bool IsMainAnalysis)
      : llvm::ImmutableMapRef<const MemRegion *, ClusterBindings>(T, F),
        CBFactory(&CBFactory), IsMainAnalysis(IsMainAnalysis) {}

  RegionBindingsRef(const ParentTy &P,
                    ClusterBindings::Factory &CBFactory,
                    bool IsMainAnalysis)
      : llvm::ImmutableMapRef<const MemRegion *, ClusterBindings>(P),
        CBFactory(&CBFactory), IsMainAnalysis(IsMainAnalysis) {}

  RegionBindingsRef add(key_type_ref K, data_type_ref D) const {
    return RegionBindingsRef(static_cast<const ParentTy *>(this)->add(K, D),
                             *CBFactory, IsMainAnalysis);
  }

  RegionBindingsRef remove(key_type_ref K) const {
    return RegionBindingsRef(static_cast<const ParentTy *>(this)->remove(K),
                             *CBFactory, IsMainAnalysis);
  }

  RegionBindingsRef addBinding(BindingKey K, SVal V) const;

  RegionBindingsRef addBinding(const MemRegion *R,
                               BindingKey::Kind k, SVal V) const;

  const SVal *lookup(BindingKey K) const;
  const SVal *lookup(const MemRegion *R, BindingKey::Kind k) const;
  using llvm::ImmutableMapRef<const MemRegion *, ClusterBindings>::lookup;

  RegionBindingsRef removeBinding(BindingKey K);

  RegionBindingsRef removeBinding(const MemRegion *R,
                                  BindingKey::Kind k);

  RegionBindingsRef removeBinding(const MemRegion *R) {
    return removeBinding(R, BindingKey::Direct).
           removeBinding(R, BindingKey::Default);
  }

  std::optional<SVal> getDirectBinding(const MemRegion *R) const;

  /// getDefaultBinding - Returns an SVal* representing an optional default
  ///  binding associated with a region and its subregions.
  std::optional<SVal> getDefaultBinding(const MemRegion *R) const;

  /// Return the internal tree as a Store.
  Store asStore() const {
    llvm::PointerIntPair<Store, 1, bool> Ptr = {
        asImmutableMap().getRootWithoutRetain(), IsMainAnalysis};
    return reinterpret_cast<Store>(Ptr.getOpaqueValue());
  }

  bool isMainAnalysis() const {
    return IsMainAnalysis;
  }

  void printJson(raw_ostream &Out, const char *NL = "\n",
                 unsigned int Space = 0, bool IsDot = false) const {
    for (iterator I = begin(), E = end(); I != E; ++I) {
      // TODO: We might need a .printJson for I.getKey() as well.
      Indent(Out, Space, IsDot)
          << "{ \"cluster\": \"" << I.getKey() << "\", \"pointer\": \""
          << (const void *)I.getKey() << "\", \"items\": [" << NL;

      ++Space;
      const ClusterBindings &CB = I.getData();
      for (ClusterBindings::iterator CI = CB.begin(), CE = CB.end(); CI != CE;
           ++CI) {
        Indent(Out, Space, IsDot) << "{ " << CI.getKey() << ", \"value\": ";
        CI.getData().printJson(Out, /*AddQuotes=*/true);
        Out << " }";
        if (std::next(CI) != CE)
          Out << ',';
        Out << NL;
      }

      --Space;
      Indent(Out, Space, IsDot) << "]}";
      if (std::next(I) != E)
        Out << ',';
      Out << NL;
    }
  }

  LLVM_DUMP_METHOD void dump() const { printJson(llvm::errs()); }
};
} // end anonymous namespace

typedef const RegionBindingsRef& RegionBindingsConstRef;

std::optional<SVal>
RegionBindingsRef::getDirectBinding(const MemRegion *R) const {
  const SVal *V = lookup(R, BindingKey::Direct);
  return V ? std::optional<SVal>(*V) : std::nullopt;
}

std::optional<SVal>
RegionBindingsRef::getDefaultBinding(const MemRegion *R) const {
  const SVal *V = lookup(R, BindingKey::Default);
  return V ? std::optional<SVal>(*V) : std::nullopt;
}

RegionBindingsRef RegionBindingsRef::addBinding(BindingKey K, SVal V) const {
  const MemRegion *Base = K.getBaseRegion();

  const ClusterBindings *ExistingCluster = lookup(Base);
  ClusterBindings Cluster =
      (ExistingCluster ? *ExistingCluster : CBFactory->getEmptyMap());

  ClusterBindings NewCluster = CBFactory->add(Cluster, K, V);
  return add(Base, NewCluster);
}


RegionBindingsRef RegionBindingsRef::addBinding(const MemRegion *R,
                                                BindingKey::Kind k,
                                                SVal V) const {
  return addBinding(BindingKey::Make(R, k), V);
}

const SVal *RegionBindingsRef::lookup(BindingKey K) const {
  const ClusterBindings *Cluster = lookup(K.getBaseRegion());
  if (!Cluster)
    return nullptr;
  return Cluster->lookup(K);
}

const SVal *RegionBindingsRef::lookup(const MemRegion *R,
                                      BindingKey::Kind k) const {
  return lookup(BindingKey::Make(R, k));
}

RegionBindingsRef RegionBindingsRef::removeBinding(BindingKey K) {
  const MemRegion *Base = K.getBaseRegion();
  const ClusterBindings *Cluster = lookup(Base);
  if (!Cluster)
    return *this;

  ClusterBindings NewCluster = CBFactory->remove(*Cluster, K);
  if (NewCluster.isEmpty())
    return remove(Base);
  return add(Base, NewCluster);
}

RegionBindingsRef RegionBindingsRef::removeBinding(const MemRegion *R,
                                                BindingKey::Kind k){
  return removeBinding(BindingKey::Make(R, k));
}

//===----------------------------------------------------------------------===//
// Main RegionStore logic.
//===----------------------------------------------------------------------===//

namespace {
class InvalidateRegionsWorker;

class RegionStoreManager : public StoreManager {
public:
  RegionBindings::Factory RBFactory;
  mutable ClusterBindings::Factory CBFactory;

  typedef std::vector<SVal> SValListTy;
private:
  typedef llvm::DenseMap<const LazyCompoundValData *,
                         SValListTy> LazyBindingsMapTy;
  LazyBindingsMapTy LazyBindingsMap;

  /// The largest number of fields a struct can have and still be
  /// considered "small".
  ///
  /// This is currently used to decide whether or not it is worth "forcing" a
  /// LazyCompoundVal on bind.
  ///
  /// This is controlled by 'region-store-small-struct-limit' option.
  /// To disable all small-struct-dependent behavior, set the option to "0".
  unsigned SmallStructLimit;

  /// The largest number of element an array can have and still be
  /// considered "small".
  ///
  /// This is currently used to decide whether or not it is worth "forcing" a
  /// LazyCompoundVal on bind.
  ///
  /// This is controlled by 'region-store-small-struct-limit' option.
  /// To disable all small-struct-dependent behavior, set the option to "0".
  unsigned SmallArrayLimit;

  /// A helper used to populate the work list with the given set of
  /// regions.
  void populateWorkList(InvalidateRegionsWorker &W,
                        ArrayRef<SVal> Values,
                        InvalidatedRegions *TopLevelRegions);

public:
  RegionStoreManager(ProgramStateManager &mgr)
      : StoreManager(mgr), RBFactory(mgr.getAllocator()),
        CBFactory(mgr.getAllocator()), SmallStructLimit(0), SmallArrayLimit(0) {
    ExprEngine &Eng = StateMgr.getOwningEngine();
    AnalyzerOptions &Options = Eng.getAnalysisManager().options;
    SmallStructLimit = Options.RegionStoreSmallStructLimit;
    SmallArrayLimit = Options.RegionStoreSmallArrayLimit;
  }

  /// setImplicitDefaultValue - Set the default binding for the provided
  ///  MemRegion to the value implicitly defined for compound literals when
  ///  the value is not specified.
  RegionBindingsRef setImplicitDefaultValue(RegionBindingsConstRef B,
                                            const MemRegion *R, QualType T);

  /// ArrayToPointer - Emulates the "decay" of an array to a pointer
  ///  type.  'Array' represents the lvalue of the array being decayed
  ///  to a pointer, and the returned SVal represents the decayed
  ///  version of that lvalue (i.e., a pointer to the first element of
  ///  the array).  This is called by ExprEngine when evaluating
  ///  casts from arrays to pointers.
  SVal ArrayToPointer(Loc Array, QualType ElementTy) override;

  /// Creates the Store that correctly represents memory contents before
  /// the beginning of the analysis of the given top-level stack frame.
  StoreRef getInitialStore(const LocationContext *InitLoc) override {
    bool IsMainAnalysis = false;
    if (const auto *FD = dyn_cast<FunctionDecl>(InitLoc->getDecl()))
      IsMainAnalysis = FD->isMain() && !Ctx.getLangOpts().CPlusPlus;
    return StoreRef(RegionBindingsRef(
        RegionBindingsRef::ParentTy(RBFactory.getEmptyMap(), RBFactory),
        CBFactory, IsMainAnalysis).asStore(), *this);
  }

  //===-------------------------------------------------------------------===//
  // Binding values to regions.
  //===-------------------------------------------------------------------===//
  RegionBindingsRef invalidateGlobalRegion(MemRegion::Kind K,
                                           const Expr *Ex,
                                           unsigned Count,
                                           const LocationContext *LCtx,
                                           RegionBindingsRef B,
                                           InvalidatedRegions *Invalidated);

  StoreRef invalidateRegions(Store store,
                             ArrayRef<SVal> Values,
                             const Expr *E, unsigned Count,
                             const LocationContext *LCtx,
                             const CallEvent *Call,
                             InvalidatedSymbols &IS,
                             RegionAndSymbolInvalidationTraits &ITraits,
                             InvalidatedRegions *Invalidated,
                             InvalidatedRegions *InvalidatedTopLevel) override;

  bool scanReachableSymbols(Store S, const MemRegion *R,
                            ScanReachableSymbols &Callbacks) override;

  RegionBindingsRef removeSubRegionBindings(RegionBindingsConstRef B,
                                            const SubRegion *R);
  std::optional<SVal>
  getConstantValFromConstArrayInitializer(RegionBindingsConstRef B,
                                          const ElementRegion *R);
  std::optional<SVal>
  getSValFromInitListExpr(const InitListExpr *ILE,
                          const SmallVector<uint64_t, 2> &ConcreteOffsets,
                          QualType ElemT);
  SVal getSValFromStringLiteral(const StringLiteral *SL, uint64_t Offset,
                                QualType ElemT);

public: // Part of public interface to class.

  StoreRef Bind(Store store, Loc LV, SVal V) override {
    return StoreRef(bind(getRegionBindings(store), LV, V).asStore(), *this);
  }

  RegionBindingsRef bind(RegionBindingsConstRef B, Loc LV, SVal V);

  // BindDefaultInitial is only used to initialize a region with
  // a default value.
  StoreRef BindDefaultInitial(Store store, const MemRegion *R,
                              SVal V) override {
    RegionBindingsRef B = getRegionBindings(store);
    // Use other APIs when you have to wipe the region that was initialized
    // earlier.
    assert(!(B.getDefaultBinding(R) || B.getDirectBinding(R)) &&
           "Double initialization!");
    B = B.addBinding(BindingKey::Make(R, BindingKey::Default), V);
    return StoreRef(B.asImmutableMap().getRootWithoutRetain(), *this);
  }

  // BindDefaultZero is used for zeroing constructors that may accidentally
  // overwrite existing bindings.
  StoreRef BindDefaultZero(Store store, const MemRegion *R) override {
    // FIXME: The offsets of empty bases can be tricky because of
    // of the so called "empty base class optimization".
    // If a base class has been optimized out
    // we should not try to create a binding, otherwise we should.
    // Unfortunately, at the moment ASTRecordLayout doesn't expose
    // the actual sizes of the empty bases
    // and trying to infer them from offsets/alignments
    // seems to be error-prone and non-trivial because of the trailing padding.
    // As a temporary mitigation we don't create bindings for empty bases.
    if (const auto *BR = dyn_cast<CXXBaseObjectRegion>(R))
      if (BR->getDecl()->isEmpty())
        return StoreRef(store, *this);

    RegionBindingsRef B = getRegionBindings(store);
    SVal V = svalBuilder.makeZeroVal(Ctx.CharTy);
    B = removeSubRegionBindings(B, cast<SubRegion>(R));
    B = B.addBinding(BindingKey::Make(R, BindingKey::Default), V);
    return StoreRef(B.asImmutableMap().getRootWithoutRetain(), *this);
  }

  /// Attempt to extract the fields of \p LCV and bind them to the struct region
  /// \p R.
  ///
  /// This path is used when it seems advantageous to "force" loading the values
  /// within a LazyCompoundVal to bind memberwise to the struct region, rather
  /// than using a Default binding at the base of the entire region. This is a
  /// heuristic attempting to avoid building long chains of LazyCompoundVals.
  ///
  /// \returns The updated store bindings, or \c std::nullopt if binding
  ///          non-lazily would be too expensive.
  std::optional<RegionBindingsRef>
  tryBindSmallStruct(RegionBindingsConstRef B, const TypedValueRegion *R,
                     const RecordDecl *RD, nonloc::LazyCompoundVal LCV);

  /// BindStruct - Bind a compound value to a structure.
  RegionBindingsRef bindStruct(RegionBindingsConstRef B,
                               const TypedValueRegion* R, SVal V);

  /// BindVector - Bind a compound value to a vector.
  RegionBindingsRef bindVector(RegionBindingsConstRef B,
                               const TypedValueRegion* R, SVal V);

  std::optional<RegionBindingsRef>
  tryBindSmallArray(RegionBindingsConstRef B, const TypedValueRegion *R,
                    const ArrayType *AT, nonloc::LazyCompoundVal LCV);

  RegionBindingsRef bindArray(RegionBindingsConstRef B,
                              const TypedValueRegion* R,
                              SVal V);

  /// Clears out all bindings in the given region and assigns a new value
  /// as a Default binding.
  RegionBindingsRef bindAggregate(RegionBindingsConstRef B,
                                  const TypedRegion *R,
                                  SVal DefaultVal);

  /// Create a new store with the specified binding removed.
  /// \param ST the original store, that is the basis for the new store.
  /// \param L the location whose binding should be removed.
  StoreRef killBinding(Store ST, Loc L) override;

  void incrementReferenceCount(Store store) override {
    getRegionBindings(store).manualRetain();
  }

  /// If the StoreManager supports it, decrement the reference count of
  /// the specified Store object.  If the reference count hits 0, the memory
  /// associated with the object is recycled.
  void decrementReferenceCount(Store store) override {
    getRegionBindings(store).manualRelease();
  }

  bool includedInBindings(Store store, const MemRegion *region) const override;

  /// Return the value bound to specified location in a given state.
  ///
  /// The high level logic for this method is this:
  /// getBinding (L)
  ///   if L has binding
  ///     return L's binding
  ///   else if L is in killset
  ///     return unknown
  ///   else
  ///     if L is on stack or heap
  ///       return undefined
  ///     else
  ///       return symbolic
  SVal getBinding(Store S, Loc L, QualType T) override {
    return getBinding(getRegionBindings(S), L, T);
  }

  std::optional<SVal> getDefaultBinding(Store S, const MemRegion *R) override {
    RegionBindingsRef B = getRegionBindings(S);
    // Default bindings are always applied over a base region so look up the
    // base region's default binding, otherwise the lookup will fail when R
    // is at an offset from R->getBaseRegion().
    return B.getDefaultBinding(R->getBaseRegion());
  }

  SVal getBinding(RegionBindingsConstRef B, Loc L, QualType T = QualType());

  SVal getBindingForElement(RegionBindingsConstRef B, const ElementRegion *R);

  SVal getBindingForField(RegionBindingsConstRef B, const FieldRegion *R);

  SVal getBindingForObjCIvar(RegionBindingsConstRef B, const ObjCIvarRegion *R);

  SVal getBindingForVar(RegionBindingsConstRef B, const VarRegion *R);

  SVal getBindingForLazySymbol(const TypedValueRegion *R);

  SVal getBindingForFieldOrElementCommon(RegionBindingsConstRef B,
                                         const TypedValueRegion *R,
                                         QualType Ty);

  SVal getLazyBinding(const SubRegion *LazyBindingRegion,
                      RegionBindingsRef LazyBinding);

  /// Get bindings for the values in a struct and return a CompoundVal, used
  /// when doing struct copy:
  /// struct s x, y;
  /// x = y;
  /// y's value is retrieved by this method.
  SVal getBindingForStruct(RegionBindingsConstRef B, const TypedValueRegion *R);
  SVal getBindingForArray(RegionBindingsConstRef B, const TypedValueRegion *R);
  NonLoc createLazyBinding(RegionBindingsConstRef B, const TypedValueRegion *R);

  /// Used to lazily generate derived symbols for bindings that are defined
  /// implicitly by default bindings in a super region.
  ///
  /// Note that callers may need to specially handle LazyCompoundVals, which
  /// are returned as is in case the caller needs to treat them differently.
  std::optional<SVal>
  getBindingForDerivedDefaultValue(RegionBindingsConstRef B,
                                   const MemRegion *superR,
                                   const TypedValueRegion *R, QualType Ty);

  /// Get the state and region whose binding this region \p R corresponds to.
  ///
  /// If there is no lazy binding for \p R, the returned value will have a null
  /// \c second. Note that a null pointer can represents a valid Store.
  std::pair<Store, const SubRegion *>
  findLazyBinding(RegionBindingsConstRef B, const SubRegion *R,
                  const SubRegion *originalRegion);

  /// Returns the cached set of interesting SVals contained within a lazy
  /// binding.
  ///
  /// The precise value of "interesting" is determined for the purposes of
  /// RegionStore's internal analysis. It must always contain all regions and
  /// symbols, but may omit constants and other kinds of SVal.
  ///
  /// In contrast to compound values, LazyCompoundVals are also added
  /// to the 'interesting values' list in addition to the child interesting
  /// values.
  const SValListTy &getInterestingValues(nonloc::LazyCompoundVal LCV);

  //===------------------------------------------------------------------===//
  // State pruning.
  //===------------------------------------------------------------------===//

  /// removeDeadBindings - Scans the RegionStore of 'state' for dead values.
  ///  It returns a new Store with these values removed.
  StoreRef removeDeadBindings(Store store, const StackFrameContext *LCtx,
                              SymbolReaper& SymReaper) override;

  //===------------------------------------------------------------------===//
  // Utility methods.
  //===------------------------------------------------------------------===//

  RegionBindingsRef getRegionBindings(Store store) const {
    llvm::PointerIntPair<Store, 1, bool> Ptr;
    Ptr.setFromOpaqueValue(const_cast<void *>(store));
    return RegionBindingsRef(
        CBFactory,
        static_cast<const RegionBindings::TreeTy *>(Ptr.getPointer()),
        RBFactory.getTreeFactory(),
        Ptr.getInt());
  }

  void printJson(raw_ostream &Out, Store S, const char *NL = "\n",
                 unsigned int Space = 0, bool IsDot = false) const override;

  void iterBindings(Store store, BindingsHandler& f) override {
    RegionBindingsRef B = getRegionBindings(store);
    for (const auto &[Region, Cluster] : B) {
      for (const auto &[Key, Value] : Cluster) {
        if (!Key.isDirect())
          continue;
        if (const SubRegion *R = dyn_cast<SubRegion>(Key.getRegion())) {
          // FIXME: Possibly incorporate the offset?
          if (!f.HandleBinding(*this, store, R, Value))
            return;
        }
      }
    }
  }
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// RegionStore creation.
//===----------------------------------------------------------------------===//

std::unique_ptr<StoreManager>
ento::CreateRegionStoreManager(ProgramStateManager &StMgr) {
  return std::make_unique<RegionStoreManager>(StMgr);
}

//===----------------------------------------------------------------------===//
// Region Cluster analysis.
//===----------------------------------------------------------------------===//

namespace {
/// Used to determine which global regions are automatically included in the
/// initial worklist of a ClusterAnalysis.
enum GlobalsFilterKind {
  /// Don't include any global regions.
  GFK_None,
  /// Only include system globals.
  GFK_SystemOnly,
  /// Include all global regions.
  GFK_All
};

template <typename DERIVED>
class ClusterAnalysis  {
protected:
  typedef llvm::DenseMap<const MemRegion *, const ClusterBindings *> ClusterMap;
  typedef const MemRegion * WorkListElement;
  typedef SmallVector<WorkListElement, 10> WorkList;

  llvm::SmallPtrSet<const ClusterBindings *, 16> Visited;

  WorkList WL;

  RegionStoreManager &RM;
  ASTContext &Ctx;
  SValBuilder &svalBuilder;

  RegionBindingsRef B;


protected:
  const ClusterBindings *getCluster(const MemRegion *R) {
    return B.lookup(R);
  }

  /// Returns true if all clusters in the given memspace should be initially
  /// included in the cluster analysis. Subclasses may provide their
  /// own implementation.
  bool includeEntireMemorySpace(const MemRegion *Base) {
    return false;
  }

public:
  ClusterAnalysis(RegionStoreManager &rm, ProgramStateManager &StateMgr,
                  RegionBindingsRef b)
      : RM(rm), Ctx(StateMgr.getContext()),
        svalBuilder(StateMgr.getSValBuilder()), B(std::move(b)) {}

  RegionBindingsRef getRegionBindings() const { return B; }

  bool isVisited(const MemRegion *R) {
    return Visited.count(getCluster(R));
  }

  void GenerateClusters() {
    // Scan the entire set of bindings and record the region clusters.
    for (RegionBindingsRef::iterator RI = B.begin(), RE = B.end();
         RI != RE; ++RI){
      const MemRegion *Base = RI.getKey();

      const ClusterBindings &Cluster = RI.getData();
      assert(!Cluster.isEmpty() && "Empty clusters should be removed");
      static_cast<DERIVED*>(this)->VisitAddedToCluster(Base, Cluster);

      // If the base's memspace should be entirely invalidated, add the cluster
      // to the workspace up front.
      if (static_cast<DERIVED*>(this)->includeEntireMemorySpace(Base))
        AddToWorkList(WorkListElement(Base), &Cluster);
    }
  }

  bool AddToWorkList(WorkListElement E, const ClusterBindings *C) {
    if (C && !Visited.insert(C).second)
      return false;
    WL.push_back(E);
    return true;
  }

  bool AddToWorkList(const MemRegion *R) {
    return static_cast<DERIVED*>(this)->AddToWorkList(R);
  }

  void RunWorkList() {
    while (!WL.empty()) {
      WorkListElement E = WL.pop_back_val();
      const MemRegion *BaseR = E;

      static_cast<DERIVED*>(this)->VisitCluster(BaseR, getCluster(BaseR));
    }
  }

  void VisitAddedToCluster(const MemRegion *baseR, const ClusterBindings &C) {}
  void VisitCluster(const MemRegion *baseR, const ClusterBindings *C) {}

  void VisitCluster(const MemRegion *BaseR, const ClusterBindings *C,
                    bool Flag) {
    static_cast<DERIVED*>(this)->VisitCluster(BaseR, C);
  }
};
}

//===----------------------------------------------------------------------===//
// Binding invalidation.
//===----------------------------------------------------------------------===//

bool RegionStoreManager::scanReachableSymbols(Store S, const MemRegion *R,
                                              ScanReachableSymbols &Callbacks) {
  assert(R == R->getBaseRegion() && "Should only be called for base regions");
  RegionBindingsRef B = getRegionBindings(S);
  const ClusterBindings *Cluster = B.lookup(R);

  if (!Cluster)
    return true;

  for (ClusterBindings::iterator RI = Cluster->begin(), RE = Cluster->end();
       RI != RE; ++RI) {
    if (!Callbacks.scan(RI.getData()))
      return false;
  }

  return true;
}

static inline bool isUnionField(const FieldRegion *FR) {
  return FR->getDecl()->getParent()->isUnion();
}

typedef SmallVector<const FieldDecl *, 8> FieldVector;

static void getSymbolicOffsetFields(BindingKey K, FieldVector &Fields) {
  assert(K.hasSymbolicOffset() && "Not implemented for concrete offset keys");

  const MemRegion *Base = K.getConcreteOffsetRegion();
  const MemRegion *R = K.getRegion();

  while (R != Base) {
    if (const FieldRegion *FR = dyn_cast<FieldRegion>(R))
      if (!isUnionField(FR))
        Fields.push_back(FR->getDecl());

    R = cast<SubRegion>(R)->getSuperRegion();
  }
}

static bool isCompatibleWithFields(BindingKey K, const FieldVector &Fields) {
  assert(K.hasSymbolicOffset() && "Not implemented for concrete offset keys");

  if (Fields.empty())
    return true;

  FieldVector FieldsInBindingKey;
  getSymbolicOffsetFields(K, FieldsInBindingKey);

  ptrdiff_t Delta = FieldsInBindingKey.size() - Fields.size();
  if (Delta >= 0)
    return std::equal(FieldsInBindingKey.begin() + Delta,
                      FieldsInBindingKey.end(),
                      Fields.begin());
  else
    return std::equal(FieldsInBindingKey.begin(), FieldsInBindingKey.end(),
                      Fields.begin() - Delta);
}

/// Collects all bindings in \p Cluster that may refer to bindings within
/// \p Top.
///
/// Each binding is a pair whose \c first is the key (a BindingKey) and whose
/// \c second is the value (an SVal).
///
/// The \p IncludeAllDefaultBindings parameter specifies whether to include
/// default bindings that may extend beyond \p Top itself, e.g. if \p Top is
/// an aggregate within a larger aggregate with a default binding.
static void
collectSubRegionBindings(SmallVectorImpl<BindingPair> &Bindings,
                         SValBuilder &SVB, const ClusterBindings &Cluster,
                         const SubRegion *Top, BindingKey TopKey,
                         bool IncludeAllDefaultBindings) {
  FieldVector FieldsInSymbolicSubregions;
  if (TopKey.hasSymbolicOffset()) {
    getSymbolicOffsetFields(TopKey, FieldsInSymbolicSubregions);
    Top = TopKey.getConcreteOffsetRegion();
    TopKey = BindingKey::Make(Top, BindingKey::Default);
  }

  // Find the length (in bits) of the region being invalidated.
  uint64_t Length = UINT64_MAX;
  SVal Extent = Top->getMemRegionManager().getStaticSize(Top, SVB);
  if (std::optional<nonloc::ConcreteInt> ExtentCI =
          Extent.getAs<nonloc::ConcreteInt>()) {
    const llvm::APSInt &ExtentInt = ExtentCI->getValue();
    assert(ExtentInt.isNonNegative() || ExtentInt.isUnsigned());
    // Extents are in bytes but region offsets are in bits. Be careful!
    Length = ExtentInt.getLimitedValue() * SVB.getContext().getCharWidth();
  } else if (const FieldRegion *FR = dyn_cast<FieldRegion>(Top)) {
    if (FR->getDecl()->isBitField())
      Length = FR->getDecl()->getBitWidthValue(SVB.getContext());
  }

  for (const auto &StoreEntry : Cluster) {
    BindingKey NextKey = StoreEntry.first;
    if (NextKey.getRegion() == TopKey.getRegion()) {
      // FIXME: This doesn't catch the case where we're really invalidating a
      // region with a symbolic offset. Example:
      //      R: points[i].y
      //   Next: points[0].x

      if (NextKey.getOffset() > TopKey.getOffset() &&
          NextKey.getOffset() - TopKey.getOffset() < Length) {
        // Case 1: The next binding is inside the region we're invalidating.
        // Include it.
        Bindings.push_back(StoreEntry);

      } else if (NextKey.getOffset() == TopKey.getOffset()) {
        // Case 2: The next binding is at the same offset as the region we're
        // invalidating. In this case, we need to leave default bindings alone,
        // since they may be providing a default value for a regions beyond what
        // we're invalidating.
        // FIXME: This is probably incorrect; consider invalidating an outer
        // struct whose first field is bound to a LazyCompoundVal.
        if (IncludeAllDefaultBindings || NextKey.isDirect())
          Bindings.push_back(StoreEntry);
      }

    } else if (NextKey.hasSymbolicOffset()) {
      const MemRegion *Base = NextKey.getConcreteOffsetRegion();
      if (Top->isSubRegionOf(Base) && Top != Base) {
        // Case 3: The next key is symbolic and we just changed something within
        // its concrete region. We don't know if the binding is still valid, so
        // we'll be conservative and include it.
        if (IncludeAllDefaultBindings || NextKey.isDirect())
          if (isCompatibleWithFields(NextKey, FieldsInSymbolicSubregions))
            Bindings.push_back(StoreEntry);
      } else if (const SubRegion *BaseSR = dyn_cast<SubRegion>(Base)) {
        // Case 4: The next key is symbolic, but we changed a known
        // super-region. In this case the binding is certainly included.
        if (BaseSR->isSubRegionOf(Top))
          if (isCompatibleWithFields(NextKey, FieldsInSymbolicSubregions))
            Bindings.push_back(StoreEntry);
      }
    }
  }
}

static void
collectSubRegionBindings(SmallVectorImpl<BindingPair> &Bindings,
                         SValBuilder &SVB, const ClusterBindings &Cluster,
                         const SubRegion *Top, bool IncludeAllDefaultBindings) {
  collectSubRegionBindings(Bindings, SVB, Cluster, Top,
                           BindingKey::Make(Top, BindingKey::Default),
                           IncludeAllDefaultBindings);
}

RegionBindingsRef
RegionStoreManager::removeSubRegionBindings(RegionBindingsConstRef B,
                                            const SubRegion *Top) {
  BindingKey TopKey = BindingKey::Make(Top, BindingKey::Default);
  const MemRegion *ClusterHead = TopKey.getBaseRegion();

  if (Top == ClusterHead) {
    // We can remove an entire cluster's bindings all in one go.
    return B.remove(Top);
  }

  const ClusterBindings *Cluster = B.lookup(ClusterHead);
  if (!Cluster) {
    // If we're invalidating a region with a symbolic offset, we need to make
    // sure we don't treat the base region as uninitialized anymore.
    if (TopKey.hasSymbolicOffset()) {
      const SubRegion *Concrete = TopKey.getConcreteOffsetRegion();
      return B.addBinding(Concrete, BindingKey::Default, UnknownVal());
    }
    return B;
  }

  SmallVector<BindingPair, 32> Bindings;
  collectSubRegionBindings(Bindings, svalBuilder, *Cluster, Top, TopKey,
                           /*IncludeAllDefaultBindings=*/false);

  ClusterBindingsRef Result(*Cluster, CBFactory);
  for (BindingKey Key : llvm::make_first_range(Bindings))
    Result = Result.remove(Key);

  // If we're invalidating a region with a symbolic offset, we need to make sure
  // we don't treat the base region as uninitialized anymore.
  // FIXME: This isn't very precise; see the example in
  // collectSubRegionBindings.
  if (TopKey.hasSymbolicOffset()) {
    const SubRegion *Concrete = TopKey.getConcreteOffsetRegion();
    Result = Result.add(BindingKey::Make(Concrete, BindingKey::Default),
                        UnknownVal());
  }

  if (Result.isEmpty())
    return B.remove(ClusterHead);
  return B.add(ClusterHead, Result.asImmutableMap());
}

namespace {
class InvalidateRegionsWorker : public ClusterAnalysis<InvalidateRegionsWorker>
{
  const Expr *Ex;
  unsigned Count;
  const LocationContext *LCtx;
  InvalidatedSymbols &IS;
  RegionAndSymbolInvalidationTraits &ITraits;
  StoreManager::InvalidatedRegions *Regions;
  GlobalsFilterKind GlobalsFilter;
public:
  InvalidateRegionsWorker(RegionStoreManager &rm,
                          ProgramStateManager &stateMgr,
                          RegionBindingsRef b,
                          const Expr *ex, unsigned count,
                          const LocationContext *lctx,
                          InvalidatedSymbols &is,
                          RegionAndSymbolInvalidationTraits &ITraitsIn,
                          StoreManager::InvalidatedRegions *r,
                          GlobalsFilterKind GFK)
     : ClusterAnalysis<InvalidateRegionsWorker>(rm, stateMgr, b),
       Ex(ex), Count(count), LCtx(lctx), IS(is), ITraits(ITraitsIn), Regions(r),
       GlobalsFilter(GFK) {}

  void VisitCluster(const MemRegion *baseR, const ClusterBindings *C);
  void VisitBinding(SVal V);

  using ClusterAnalysis::AddToWorkList;

  bool AddToWorkList(const MemRegion *R);

  /// Returns true if all clusters in the memory space for \p Base should be
  /// be invalidated.
  bool includeEntireMemorySpace(const MemRegion *Base);

  /// Returns true if the memory space of the given region is one of the global
  /// regions specially included at the start of invalidation.
  bool isInitiallyIncludedGlobalRegion(const MemRegion *R);
};
}

bool InvalidateRegionsWorker::AddToWorkList(const MemRegion *R) {
  bool doNotInvalidateSuperRegion = ITraits.hasTrait(
      R, RegionAndSymbolInvalidationTraits::TK_DoNotInvalidateSuperRegion);
  const MemRegion *BaseR = doNotInvalidateSuperRegion ? R : R->getBaseRegion();
  return AddToWorkList(WorkListElement(BaseR), getCluster(BaseR));
}

void InvalidateRegionsWorker::VisitBinding(SVal V) {
  // A symbol?  Mark it touched by the invalidation.
  if (SymbolRef Sym = V.getAsSymbol())
    IS.insert(Sym);

  if (const MemRegion *R = V.getAsRegion()) {
    AddToWorkList(R);
    return;
  }

  // Is it a LazyCompoundVal?  All references get invalidated as well.
  if (std::optional<nonloc::LazyCompoundVal> LCS =
          V.getAs<nonloc::LazyCompoundVal>()) {

    // `getInterestingValues()` returns SVals contained within LazyCompoundVals,
    // so there is no need to visit them.
    for (SVal V : RM.getInterestingValues(*LCS))
      if (!isa<nonloc::LazyCompoundVal>(V))
        VisitBinding(V);

    return;
  }
}

void InvalidateRegionsWorker::VisitCluster(const MemRegion *baseR,
                                           const ClusterBindings *C) {

  bool PreserveRegionsContents =
      ITraits.hasTrait(baseR,
                       RegionAndSymbolInvalidationTraits::TK_PreserveContents);

  if (C) {
    for (SVal Val : llvm::make_second_range(*C))
      VisitBinding(Val);

    // Invalidate regions contents.
    if (!PreserveRegionsContents)
      B = B.remove(baseR);
  }

  if (const auto *TO = dyn_cast<TypedValueRegion>(baseR)) {
    if (const auto *RD = TO->getValueType()->getAsCXXRecordDecl()) {

      // Lambdas can affect all static local variables without explicitly
      // capturing those.
      // We invalidate all static locals referenced inside the lambda body.
      if (RD->isLambda() && RD->getLambdaCallOperator()->getBody()) {
        using namespace ast_matchers;

        const char *DeclBind = "DeclBind";
        StatementMatcher RefToStatic = stmt(hasDescendant(declRefExpr(
              to(varDecl(hasStaticStorageDuration()).bind(DeclBind)))));
        auto Matches =
            match(RefToStatic, *RD->getLambdaCallOperator()->getBody(),
                  RD->getASTContext());

        for (BoundNodes &Match : Matches) {
          auto *VD = Match.getNodeAs<VarDecl>(DeclBind);
          const VarRegion *ToInvalidate =
              RM.getRegionManager().getVarRegion(VD, LCtx);
          AddToWorkList(ToInvalidate);
        }
      }
    }
  }

  // BlockDataRegion?  If so, invalidate captured variables that are passed
  // by reference.
  if (const BlockDataRegion *BR = dyn_cast<BlockDataRegion>(baseR)) {
    for (auto Var : BR->referenced_vars()) {
      const VarRegion *VR = Var.getCapturedRegion();
      const VarDecl *VD = VR->getDecl();
      if (VD->hasAttr<BlocksAttr>() || !VD->hasLocalStorage()) {
        AddToWorkList(VR);
      }
      else if (Loc::isLocType(VR->getValueType())) {
        // Map the current bindings to a Store to retrieve the value
        // of the binding.  If that binding itself is a region, we should
        // invalidate that region.  This is because a block may capture
        // a pointer value, but the thing pointed by that pointer may
        // get invalidated.
        SVal V = RM.getBinding(B, loc::MemRegionVal(VR));
        if (std::optional<Loc> L = V.getAs<Loc>()) {
          if (const MemRegion *LR = L->getAsRegion())
            AddToWorkList(LR);
        }
      }
    }
    return;
  }

  // Symbolic region?
  if (const SymbolicRegion *SR = dyn_cast<SymbolicRegion>(baseR))
    IS.insert(SR->getSymbol());

  // Nothing else should be done in the case when we preserve regions context.
  if (PreserveRegionsContents)
    return;

  // Otherwise, we have a normal data region. Record that we touched the region.
  if (Regions)
    Regions->push_back(baseR);

  if (isa<AllocaRegion, SymbolicRegion>(baseR)) {
    // Invalidate the region by setting its default value to
    // conjured symbol. The type of the symbol is irrelevant.
    DefinedOrUnknownSVal V =
      svalBuilder.conjureSymbolVal(baseR, Ex, LCtx, Ctx.IntTy, Count);
    B = B.addBinding(baseR, BindingKey::Default, V);
    return;
  }

  if (!baseR->isBoundable())
    return;

  const TypedValueRegion *TR = cast<TypedValueRegion>(baseR);
  QualType T = TR->getValueType();

  if (isInitiallyIncludedGlobalRegion(baseR)) {
    // If the region is a global and we are invalidating all globals,
    // erasing the entry is good enough.  This causes all globals to be lazily
    // symbolicated from the same base symbol.
    return;
  }

  if (T->isRecordType()) {
    // Invalidate the region by setting its default value to
    // conjured symbol. The type of the symbol is irrelevant.
    DefinedOrUnknownSVal V = svalBuilder.conjureSymbolVal(baseR, Ex, LCtx,
                                                          Ctx.IntTy, Count);
    B = B.addBinding(baseR, BindingKey::Default, V);
    return;
  }

  if (const ArrayType *AT = Ctx.getAsArrayType(T)) {
    bool doNotInvalidateSuperRegion = ITraits.hasTrait(
        baseR,
        RegionAndSymbolInvalidationTraits::TK_DoNotInvalidateSuperRegion);

    if (doNotInvalidateSuperRegion) {
      // We are not doing blank invalidation of the whole array region so we
      // have to manually invalidate each elements.
      std::optional<uint64_t> NumElements;

      // Compute lower and upper offsets for region within array.
      if (const ConstantArrayType *CAT = dyn_cast<ConstantArrayType>(AT))
        NumElements = CAT->getZExtSize();
      if (!NumElements) // We are not dealing with a constant size array
        goto conjure_default;
      QualType ElementTy = AT->getElementType();
      uint64_t ElemSize = Ctx.getTypeSize(ElementTy);
      const RegionOffset &RO = baseR->getAsOffset();
      const MemRegion *SuperR = baseR->getBaseRegion();
      if (RO.hasSymbolicOffset()) {
        // If base region has a symbolic offset,
        // we revert to invalidating the super region.
        if (SuperR)
          AddToWorkList(SuperR);
        goto conjure_default;
      }

      uint64_t LowerOffset = RO.getOffset();
      uint64_t UpperOffset = LowerOffset + *NumElements * ElemSize;
      bool UpperOverflow = UpperOffset < LowerOffset;

      // Invalidate regions which are within array boundaries,
      // or have a symbolic offset.
      if (!SuperR)
        goto conjure_default;

      const ClusterBindings *C = B.lookup(SuperR);
      if (!C)
        goto conjure_default;

      for (const auto &[BK, V] : *C) {
        std::optional<uint64_t> ROffset =
            BK.hasSymbolicOffset() ? std::optional<uint64_t>() : BK.getOffset();

        // Check offset is not symbolic and within array's boundaries.
        // Handles arrays of 0 elements and of 0-sized elements as well.
        if (!ROffset ||
            ((*ROffset >= LowerOffset && *ROffset < UpperOffset) ||
             (UpperOverflow &&
              (*ROffset >= LowerOffset || *ROffset < UpperOffset)) ||
             (LowerOffset == UpperOffset && *ROffset == LowerOffset))) {
          B = B.removeBinding(BK);
          // Bound symbolic regions need to be invalidated for dead symbol
          // detection.
          const MemRegion *R = V.getAsRegion();
          if (isa_and_nonnull<SymbolicRegion>(R))
            VisitBinding(V);
        }
      }
    }
  conjure_default:
      // Set the default value of the array to conjured symbol.
    DefinedOrUnknownSVal V =
    svalBuilder.conjureSymbolVal(baseR, Ex, LCtx,
                                     AT->getElementType(), Count);
    B = B.addBinding(baseR, BindingKey::Default, V);
    return;
  }

  DefinedOrUnknownSVal V = svalBuilder.conjureSymbolVal(baseR, Ex, LCtx,
                                                        T,Count);
  assert(SymbolManager::canSymbolicate(T) || V.isUnknown());
  B = B.addBinding(baseR, BindingKey::Direct, V);
}

bool InvalidateRegionsWorker::isInitiallyIncludedGlobalRegion(
    const MemRegion *R) {
  switch (GlobalsFilter) {
  case GFK_None:
    return false;
  case GFK_SystemOnly:
    return isa<GlobalSystemSpaceRegion>(R->getMemorySpace());
  case GFK_All:
    return isa<NonStaticGlobalSpaceRegion>(R->getMemorySpace());
  }

  llvm_unreachable("unknown globals filter");
}

bool InvalidateRegionsWorker::includeEntireMemorySpace(const MemRegion *Base) {
  if (isInitiallyIncludedGlobalRegion(Base))
    return true;

  const MemSpaceRegion *MemSpace = Base->getMemorySpace();
  return ITraits.hasTrait(MemSpace,
                          RegionAndSymbolInvalidationTraits::TK_EntireMemSpace);
}

RegionBindingsRef
RegionStoreManager::invalidateGlobalRegion(MemRegion::Kind K,
                                           const Expr *Ex,
                                           unsigned Count,
                                           const LocationContext *LCtx,
                                           RegionBindingsRef B,
                                           InvalidatedRegions *Invalidated) {
  // Bind the globals memory space to a new symbol that we will use to derive
  // the bindings for all globals.
  const GlobalsSpaceRegion *GS = MRMgr.getGlobalsRegion(K);
  SVal V = svalBuilder.conjureSymbolVal(/* symbolTag = */ (const void*) GS, Ex, LCtx,
                                        /* type does not matter */ Ctx.IntTy,
                                        Count);

  B = B.removeBinding(GS)
       .addBinding(BindingKey::Make(GS, BindingKey::Default), V);

  // Even if there are no bindings in the global scope, we still need to
  // record that we touched it.
  if (Invalidated)
    Invalidated->push_back(GS);

  return B;
}

void RegionStoreManager::populateWorkList(InvalidateRegionsWorker &W,
                                          ArrayRef<SVal> Values,
                                          InvalidatedRegions *TopLevelRegions) {
  for (SVal V : Values) {
    if (auto LCS = V.getAs<nonloc::LazyCompoundVal>()) {
      for (SVal S : getInterestingValues(*LCS))
        if (const MemRegion *R = S.getAsRegion())
          W.AddToWorkList(R);

      continue;
    }

    if (const MemRegion *R = V.getAsRegion()) {
      if (TopLevelRegions)
        TopLevelRegions->push_back(R);
      W.AddToWorkList(R);
      continue;
    }
  }
}

StoreRef
RegionStoreManager::invalidateRegions(Store store,
                                     ArrayRef<SVal> Values,
                                     const Expr *Ex, unsigned Count,
                                     const LocationContext *LCtx,
                                     const CallEvent *Call,
                                     InvalidatedSymbols &IS,
                                     RegionAndSymbolInvalidationTraits &ITraits,
                                     InvalidatedRegions *TopLevelRegions,
                                     InvalidatedRegions *Invalidated) {
  GlobalsFilterKind GlobalsFilter;
  if (Call) {
    if (Call->isInSystemHeader())
      GlobalsFilter = GFK_SystemOnly;
    else
      GlobalsFilter = GFK_All;
  } else {
    GlobalsFilter = GFK_None;
  }

  RegionBindingsRef B = getRegionBindings(store);
  InvalidateRegionsWorker W(*this, StateMgr, B, Ex, Count, LCtx, IS, ITraits,
                            Invalidated, GlobalsFilter);

  // Scan the bindings and generate the clusters.
  W.GenerateClusters();

  // Add the regions to the worklist.
  populateWorkList(W, Values, TopLevelRegions);

  W.RunWorkList();

  // Return the new bindings.
  B = W.getRegionBindings();

  // For calls, determine which global regions should be invalidated and
  // invalidate them. (Note that function-static and immutable globals are never
  // invalidated by this.)
  // TODO: This could possibly be more precise with modules.
  switch (GlobalsFilter) {
  case GFK_All:
    B = invalidateGlobalRegion(MemRegion::GlobalInternalSpaceRegionKind,
                               Ex, Count, LCtx, B, Invalidated);
    [[fallthrough]];
  case GFK_SystemOnly:
    B = invalidateGlobalRegion(MemRegion::GlobalSystemSpaceRegionKind,
                               Ex, Count, LCtx, B, Invalidated);
    [[fallthrough]];
  case GFK_None:
    break;
  }

  return StoreRef(B.asStore(), *this);
}

//===----------------------------------------------------------------------===//
// Location and region casting.
//===----------------------------------------------------------------------===//

/// ArrayToPointer - Emulates the "decay" of an array to a pointer
///  type.  'Array' represents the lvalue of the array being decayed
///  to a pointer, and the returned SVal represents the decayed
///  version of that lvalue (i.e., a pointer to the first element of
///  the array).  This is called by ExprEngine when evaluating casts
///  from arrays to pointers.
SVal RegionStoreManager::ArrayToPointer(Loc Array, QualType T) {
  if (isa<loc::ConcreteInt>(Array))
    return Array;

  if (!isa<loc::MemRegionVal>(Array))
    return UnknownVal();

  const SubRegion *R =
      cast<SubRegion>(Array.castAs<loc::MemRegionVal>().getRegion());
  NonLoc ZeroIdx = svalBuilder.makeZeroArrayIndex();
  return loc::MemRegionVal(MRMgr.getElementRegion(T, ZeroIdx, R, Ctx));
}

//===----------------------------------------------------------------------===//
// Loading values from regions.
//===----------------------------------------------------------------------===//

SVal RegionStoreManager::getBinding(RegionBindingsConstRef B, Loc L, QualType T) {
  assert(!isa<UnknownVal>(L) && "location unknown");
  assert(!isa<UndefinedVal>(L) && "location undefined");

  // For access to concrete addresses, return UnknownVal.  Checks
  // for null dereferences (and similar errors) are done by checkers, not
  // the Store.
  // FIXME: We can consider lazily symbolicating such memory, but we really
  // should defer this when we can reason easily about symbolicating arrays
  // of bytes.
  if (L.getAs<loc::ConcreteInt>()) {
    return UnknownVal();
  }
  if (!L.getAs<loc::MemRegionVal>()) {
    return UnknownVal();
  }

  const MemRegion *MR = L.castAs<loc::MemRegionVal>().getRegion();

  if (isa<BlockDataRegion>(MR)) {
    return UnknownVal();
  }

  // Auto-detect the binding type.
  if (T.isNull()) {
    if (const auto *TVR = dyn_cast<TypedValueRegion>(MR))
      T = TVR->getValueType();
    else if (const auto *TR = dyn_cast<TypedRegion>(MR))
      T = TR->getLocationType()->getPointeeType();
    else if (const auto *SR = dyn_cast<SymbolicRegion>(MR))
      T = SR->getPointeeStaticType();
  }
  assert(!T.isNull() && "Unable to auto-detect binding type!");
  assert(!T->isVoidType() && "Attempting to dereference a void pointer!");

  if (!isa<TypedValueRegion>(MR))
    MR = GetElementZeroRegion(cast<SubRegion>(MR), T);

  // FIXME: Perhaps this method should just take a 'const MemRegion*' argument
  //  instead of 'Loc', and have the other Loc cases handled at a higher level.
  const TypedValueRegion *R = cast<TypedValueRegion>(MR);
  QualType RTy = R->getValueType();

  // FIXME: we do not yet model the parts of a complex type, so treat the
  // whole thing as "unknown".
  if (RTy->isAnyComplexType())
    return UnknownVal();

  // FIXME: We should eventually handle funny addressing.  e.g.:
  //
  //   int x = ...;
  //   int *p = &x;
  //   char *q = (char*) p;
  //   char c = *q;  // returns the first byte of 'x'.
  //
  // Such funny addressing will occur due to layering of regions.
  if (RTy->isStructureOrClassType())
    return getBindingForStruct(B, R);

  // FIXME: Handle unions.
  if (RTy->isUnionType())
    return createLazyBinding(B, R);

  if (RTy->isArrayType()) {
    if (RTy->isConstantArrayType())
      return getBindingForArray(B, R);
    else
      return UnknownVal();
  }

  // FIXME: handle Vector types.
  if (RTy->isVectorType())
    return UnknownVal();

  if (const FieldRegion* FR = dyn_cast<FieldRegion>(R))
    return svalBuilder.evalCast(getBindingForField(B, FR), T, QualType{});

  if (const ElementRegion* ER = dyn_cast<ElementRegion>(R)) {
    // FIXME: Here we actually perform an implicit conversion from the loaded
    // value to the element type.  Eventually we want to compose these values
    // more intelligently.  For example, an 'element' can encompass multiple
    // bound regions (e.g., several bound bytes), or could be a subset of
    // a larger value.
    return svalBuilder.evalCast(getBindingForElement(B, ER), T, QualType{});
  }

  if (const ObjCIvarRegion *IVR = dyn_cast<ObjCIvarRegion>(R)) {
    // FIXME: Here we actually perform an implicit conversion from the loaded
    // value to the ivar type.  What we should model is stores to ivars
    // that blow past the extent of the ivar.  If the address of the ivar is
    // reinterpretted, it is possible we stored a different value that could
    // fit within the ivar.  Either we need to cast these when storing them
    // or reinterpret them lazily (as we do here).
    return svalBuilder.evalCast(getBindingForObjCIvar(B, IVR), T, QualType{});
  }

  if (const VarRegion *VR = dyn_cast<VarRegion>(R)) {
    // FIXME: Here we actually perform an implicit conversion from the loaded
    // value to the variable type.  What we should model is stores to variables
    // that blow past the extent of the variable.  If the address of the
    // variable is reinterpretted, it is possible we stored a different value
    // that could fit within the variable.  Either we need to cast these when
    // storing them or reinterpret them lazily (as we do here).
    return svalBuilder.evalCast(getBindingForVar(B, VR), T, QualType{});
  }

  const SVal *V = B.lookup(R, BindingKey::Direct);

  // Check if the region has a binding.
  if (V)
    return *V;

  // The location does not have a bound value.  This means that it has
  // the value it had upon its creation and/or entry to the analyzed
  // function/method.  These are either symbolic values or 'undefined'.
  if (R->hasStackNonParametersStorage()) {
    // All stack variables are considered to have undefined values
    // upon creation.  All heap allocated blocks are considered to
    // have undefined values as well unless they are explicitly bound
    // to specific values.
    return UndefinedVal();
  }

  // All other values are symbolic.
  return svalBuilder.getRegionValueSymbolVal(R);
}

static QualType getUnderlyingType(const SubRegion *R) {
  QualType RegionTy;
  if (const TypedValueRegion *TVR = dyn_cast<TypedValueRegion>(R))
    RegionTy = TVR->getValueType();

  if (const SymbolicRegion *SR = dyn_cast<SymbolicRegion>(R))
    RegionTy = SR->getSymbol()->getType();

  return RegionTy;
}

/// Checks to see if store \p B has a lazy binding for region \p R.
///
/// If \p AllowSubregionBindings is \c false, a lazy binding will be rejected
/// if there are additional bindings within \p R.
///
/// Note that unlike RegionStoreManager::findLazyBinding, this will not search
/// for lazy bindings for super-regions of \p R.
static std::optional<nonloc::LazyCompoundVal>
getExistingLazyBinding(SValBuilder &SVB, RegionBindingsConstRef B,
                       const SubRegion *R, bool AllowSubregionBindings) {
  std::optional<SVal> V = B.getDefaultBinding(R);
  if (!V)
    return std::nullopt;

  std::optional<nonloc::LazyCompoundVal> LCV =
      V->getAs<nonloc::LazyCompoundVal>();
  if (!LCV)
    return std::nullopt;

  // If the LCV is for a subregion, the types might not match, and we shouldn't
  // reuse the binding.
  QualType RegionTy = getUnderlyingType(R);
  if (!RegionTy.isNull() &&
      !RegionTy->isVoidPointerType()) {
    QualType SourceRegionTy = LCV->getRegion()->getValueType();
    if (!SVB.getContext().hasSameUnqualifiedType(RegionTy, SourceRegionTy))
      return std::nullopt;
  }

  if (!AllowSubregionBindings) {
    // If there are any other bindings within this region, we shouldn't reuse
    // the top-level binding.
    SmallVector<BindingPair, 16> Bindings;
    collectSubRegionBindings(Bindings, SVB, *B.lookup(R->getBaseRegion()), R,
                             /*IncludeAllDefaultBindings=*/true);
    if (Bindings.size() > 1)
      return std::nullopt;
  }

  return *LCV;
}

std::pair<Store, const SubRegion *>
RegionStoreManager::findLazyBinding(RegionBindingsConstRef B,
                                   const SubRegion *R,
                                   const SubRegion *originalRegion) {
  if (originalRegion != R) {
    if (std::optional<nonloc::LazyCompoundVal> V =
            getExistingLazyBinding(svalBuilder, B, R, true))
      return std::make_pair(V->getStore(), V->getRegion());
  }

  typedef std::pair<Store, const SubRegion *> StoreRegionPair;
  StoreRegionPair Result = StoreRegionPair();

  if (const ElementRegion *ER = dyn_cast<ElementRegion>(R)) {
    Result = findLazyBinding(B, cast<SubRegion>(ER->getSuperRegion()),
                             originalRegion);

    if (Result.second)
      Result.second = MRMgr.getElementRegionWithSuper(ER, Result.second);

  } else if (const FieldRegion *FR = dyn_cast<FieldRegion>(R)) {
    Result = findLazyBinding(B, cast<SubRegion>(FR->getSuperRegion()),
                                       originalRegion);

    if (Result.second)
      Result.second = MRMgr.getFieldRegionWithSuper(FR, Result.second);

  } else if (const CXXBaseObjectRegion *BaseReg =
               dyn_cast<CXXBaseObjectRegion>(R)) {
    // C++ base object region is another kind of region that we should blast
    // through to look for lazy compound value. It is like a field region.
    Result = findLazyBinding(B, cast<SubRegion>(BaseReg->getSuperRegion()),
                             originalRegion);

    if (Result.second)
      Result.second = MRMgr.getCXXBaseObjectRegionWithSuper(BaseReg,
                                                            Result.second);
  }

  return Result;
}

/// This is a helper function for `getConstantValFromConstArrayInitializer`.
///
/// Return an array of extents of the declared array type.
///
/// E.g. for `int x[1][2][3];` returns { 1, 2, 3 }.
static SmallVector<uint64_t, 2>
getConstantArrayExtents(const ConstantArrayType *CAT) {
  assert(CAT && "ConstantArrayType should not be null");
  CAT = cast<ConstantArrayType>(CAT->getCanonicalTypeInternal());
  SmallVector<uint64_t, 2> Extents;
  do {
    Extents.push_back(CAT->getZExtSize());
  } while ((CAT = dyn_cast<ConstantArrayType>(CAT->getElementType())));
  return Extents;
}

/// This is a helper function for `getConstantValFromConstArrayInitializer`.
///
/// Return an array of offsets from nested ElementRegions and a root base
/// region. The array is never empty and a base region is never null.
///
/// E.g. for `Element{Element{Element{VarRegion},1},2},3}` returns { 3, 2, 1 }.
/// This represents an access through indirection: `arr[1][2][3];`
///
/// \param ER The given (possibly nested) ElementRegion.
///
/// \note The result array is in the reverse order of indirection expression:
/// arr[1][2][3] -> { 3, 2, 1 }. This helps to provide complexity O(n), where n
/// is a number of indirections. It may not affect performance in real-life
/// code, though.
static std::pair<SmallVector<SVal, 2>, const MemRegion *>
getElementRegionOffsetsWithBase(const ElementRegion *ER) {
  assert(ER && "ConstantArrayType should not be null");
  const MemRegion *Base;
  SmallVector<SVal, 2> SValOffsets;
  do {
    SValOffsets.push_back(ER->getIndex());
    Base = ER->getSuperRegion();
    ER = dyn_cast<ElementRegion>(Base);
  } while (ER);
  return {SValOffsets, Base};
}

/// This is a helper function for `getConstantValFromConstArrayInitializer`.
///
/// Convert array of offsets from `SVal` to `uint64_t` in consideration of
/// respective array extents.
/// \param SrcOffsets [in]   The array of offsets of type `SVal` in reversed
///   order (expectedly received from `getElementRegionOffsetsWithBase`).
/// \param ArrayExtents [in] The array of extents.
/// \param DstOffsets [out]  The array of offsets of type `uint64_t`.
/// \returns:
/// - `std::nullopt` for successful convertion.
/// - `UndefinedVal` or `UnknownVal` otherwise. It's expected that this SVal
///   will be returned as a suitable value of the access operation.
///   which should be returned as a correct
///
/// \example:
///   const int arr[10][20][30] = {}; // ArrayExtents { 10, 20, 30 }
///   int x1 = arr[4][5][6]; // SrcOffsets { NonLoc(6), NonLoc(5), NonLoc(4) }
///                          // DstOffsets { 4, 5, 6 }
///                          // returns std::nullopt
///   int x2 = arr[42][5][-6]; // returns UndefinedVal
///   int x3 = arr[4][5][x2];  // returns UnknownVal
static std::optional<SVal>
convertOffsetsFromSvalToUnsigneds(const SmallVector<SVal, 2> &SrcOffsets,
                                  const SmallVector<uint64_t, 2> ArrayExtents,
                                  SmallVector<uint64_t, 2> &DstOffsets) {
  // Check offsets for being out of bounds.
  // C++20 [expr.add] 7.6.6.4 (excerpt):
  //   If P points to an array element i of an array object x with n
  //   elements, where i < 0 or i > n, the behavior is undefined.
  //   Dereferencing is not allowed on the "one past the last
  //   element", when i == n.
  // Example:
  //  const int arr[3][2] = {{1, 2}, {3, 4}};
  //  arr[0][0];  // 1
  //  arr[0][1];  // 2
  //  arr[0][2];  // UB
  //  arr[1][0];  // 3
  //  arr[1][1];  // 4
  //  arr[1][-1]; // UB
  //  arr[2][0];  // 0
  //  arr[2][1];  // 0
  //  arr[-2][0]; // UB
  DstOffsets.resize(SrcOffsets.size());
  auto ExtentIt = ArrayExtents.begin();
  auto OffsetIt = DstOffsets.begin();
  // Reverse `SValOffsets` to make it consistent with `ArrayExtents`.
  for (SVal V : llvm::reverse(SrcOffsets)) {
    if (auto CI = V.getAs<nonloc::ConcreteInt>()) {
      // When offset is out of array's bounds, result is UB.
      const llvm::APSInt &Offset = CI->getValue();
      if (Offset.isNegative() || Offset.uge(*(ExtentIt++)))
        return UndefinedVal();
      // Store index in a reversive order.
      *(OffsetIt++) = Offset.getZExtValue();
      continue;
    }
    // Symbolic index presented. Return Unknown value.
    // FIXME: We also need to take ElementRegions with symbolic indexes into
    // account.
    return UnknownVal();
  }
  return std::nullopt;
}

std::optional<SVal> RegionStoreManager::getConstantValFromConstArrayInitializer(
    RegionBindingsConstRef B, const ElementRegion *R) {
  assert(R && "ElementRegion should not be null");

  // Treat an n-dimensional array.
  SmallVector<SVal, 2> SValOffsets;
  const MemRegion *Base;
  std::tie(SValOffsets, Base) = getElementRegionOffsetsWithBase(R);
  const VarRegion *VR = dyn_cast<VarRegion>(Base);
  if (!VR)
    return std::nullopt;

  assert(!SValOffsets.empty() && "getElementRegionOffsets guarantees the "
                                 "offsets vector is not empty.");

  // Check if the containing array has an initialized value that we can trust.
  // We can trust a const value or a value of a global initializer in main().
  const VarDecl *VD = VR->getDecl();
  if (!VD->getType().isConstQualified() &&
      !R->getElementType().isConstQualified() &&
      (!B.isMainAnalysis() || !VD->hasGlobalStorage()))
    return std::nullopt;

  // Array's declaration should have `ConstantArrayType` type, because only this
  // type contains an array extent. It may happen that array type can be of
  // `IncompleteArrayType` type. To get the declaration of `ConstantArrayType`
  // type, we should find the declaration in the redeclarations chain that has
  // the initialization expression.
  // NOTE: `getAnyInitializer` has an out-parameter, which returns a new `VD`
  // from which an initializer is obtained. We replace current `VD` with the new
  // `VD`. If the return value of the function is null than `VD` won't be
  // replaced.
  const Expr *Init = VD->getAnyInitializer(VD);
  // NOTE: If `Init` is non-null, then a new `VD` is non-null for sure. So check
  // `Init` for null only and don't worry about the replaced `VD`.
  if (!Init)
    return std::nullopt;

  // Array's declaration should have ConstantArrayType type, because only this
  // type contains an array extent.
  const ConstantArrayType *CAT = Ctx.getAsConstantArrayType(VD->getType());
  if (!CAT)
    return std::nullopt;

  // Get array extents.
  SmallVector<uint64_t, 2> Extents = getConstantArrayExtents(CAT);

  // The number of offsets should equal to the numbers of extents,
  // otherwise wrong type punning occurred. For instance:
  //  int arr[1][2][3];
  //  auto ptr = (int(*)[42])arr;
  //  auto x = ptr[4][2]; // UB
  // FIXME: Should return UndefinedVal.
  if (SValOffsets.size() != Extents.size())
    return std::nullopt;

  SmallVector<uint64_t, 2> ConcreteOffsets;
  if (std::optional<SVal> V = convertOffsetsFromSvalToUnsigneds(
          SValOffsets, Extents, ConcreteOffsets))
    return *V;

  // Handle InitListExpr.
  // Example:
  //   const char arr[4][2] = { { 1, 2 }, { 3 }, 4, 5 };
  if (const auto *ILE = dyn_cast<InitListExpr>(Init))
    return getSValFromInitListExpr(ILE, ConcreteOffsets, R->getElementType());

  // Handle StringLiteral.
  // Example:
  //   const char arr[] = "abc";
  if (const auto *SL = dyn_cast<StringLiteral>(Init))
    return getSValFromStringLiteral(SL, ConcreteOffsets.front(),
                                    R->getElementType());

  // FIXME: Handle CompoundLiteralExpr.

  return std::nullopt;
}

/// Returns an SVal, if possible, for the specified position of an
/// initialization list.
///
/// \param ILE The given initialization list.
/// \param Offsets The array of unsigned offsets. E.g. for the expression
///  `int x = arr[1][2][3];` an array should be { 1, 2, 3 }.
/// \param ElemT The type of the result SVal expression.
/// \return Optional SVal for the particular position in the initialization
///   list. E.g. for the list `{{1, 2},[3, 4],{5, 6}, {}}` offsets:
///   - {1, 1} returns SVal{4}, because it's the second position in the second
///     sublist;
///   - {3, 0} returns SVal{0}, because there's no explicit value at this
///     position in the sublist.
///
/// NOTE: Inorder to get a valid SVal, a caller shall guarantee valid offsets
/// for the given initialization list. Otherwise SVal can be an equivalent to 0
/// or lead to assertion.
std::optional<SVal> RegionStoreManager::getSValFromInitListExpr(
    const InitListExpr *ILE, const SmallVector<uint64_t, 2> &Offsets,
    QualType ElemT) {
  assert(ILE && "InitListExpr should not be null");

  for (uint64_t Offset : Offsets) {
    // C++20 [dcl.init.string] 9.4.2.1:
    //   An array of ordinary character type [...] can be initialized by [...]
    //   an appropriately-typed string-literal enclosed in braces.
    // Example:
    //   const char arr[] = { "abc" };
    if (ILE->isStringLiteralInit())
      if (const auto *SL = dyn_cast<StringLiteral>(ILE->getInit(0)))
        return getSValFromStringLiteral(SL, Offset, ElemT);

    // C++20 [expr.add] 9.4.17.5 (excerpt):
    //   i-th array element is value-initialized for each k < i ≤ n,
    //   where k is an expression-list size and n is an array extent.
    if (Offset >= ILE->getNumInits())
      return svalBuilder.makeZeroVal(ElemT);

    const Expr *E = ILE->getInit(Offset);
    const auto *IL = dyn_cast<InitListExpr>(E);
    if (!IL)
      // Return a constant value, if it is presented.
      // FIXME: Support other SVals.
      return svalBuilder.getConstantVal(E);

    // Go to the nested initializer list.
    ILE = IL;
  }

  assert(ILE);

  // FIXME: Unhandeled InitListExpr sub-expression, possibly constructing an
  //        enum?
  return std::nullopt;
}

/// Returns an SVal, if possible, for the specified position in a string
/// literal.
///
/// \param SL The given string literal.
/// \param Offset The unsigned offset. E.g. for the expression
///   `char x = str[42];` an offset should be 42.
///   E.g. for the string "abc" offset:
///   - 1 returns SVal{b}, because it's the second position in the string.
///   - 42 returns SVal{0}, because there's no explicit value at this
///     position in the string.
/// \param ElemT The type of the result SVal expression.
///
/// NOTE: We return `0` for every offset >= the literal length for array
/// declarations, like:
///   const char str[42] = "123"; // Literal length is 4.
///   char c = str[41];           // Offset is 41.
/// FIXME: Nevertheless, we can't do the same for pointer declaraions, like:
///   const char * const str = "123"; // Literal length is 4.
///   char c = str[41];               // Offset is 41. Returns `0`, but Undef
///                                   // expected.
/// It should be properly handled before reaching this point.
/// The main problem is that we can't distinguish between these declarations,
/// because in case of array we can get the Decl from VarRegion, but in case
/// of pointer the region is a StringRegion, which doesn't contain a Decl.
/// Possible solution could be passing an array extent along with the offset.
SVal RegionStoreManager::getSValFromStringLiteral(const StringLiteral *SL,
                                                  uint64_t Offset,
                                                  QualType ElemT) {
  assert(SL && "StringLiteral should not be null");
  // C++20 [dcl.init.string] 9.4.2.3:
  //   If there are fewer initializers than there are array elements, each
  //   element not explicitly initialized shall be zero-initialized [dcl.init].
  uint32_t Code = (Offset >= SL->getLength()) ? 0 : SL->getCodeUnit(Offset);
  return svalBuilder.makeIntVal(Code, ElemT);
}

static std::optional<SVal> getDerivedSymbolForBinding(
    RegionBindingsConstRef B, const TypedValueRegion *BaseRegion,
    const TypedValueRegion *SubReg, const ASTContext &Ctx, SValBuilder &SVB) {
  assert(BaseRegion);
  QualType BaseTy = BaseRegion->getValueType();
  QualType Ty = SubReg->getValueType();
  if (BaseTy->isScalarType() && Ty->isScalarType()) {
    if (Ctx.getTypeSizeInChars(BaseTy) >= Ctx.getTypeSizeInChars(Ty)) {
      if (const std::optional<SVal> &ParentValue =
              B.getDirectBinding(BaseRegion)) {
        if (SymbolRef ParentValueAsSym = ParentValue->getAsSymbol())
          return SVB.getDerivedRegionValueSymbolVal(ParentValueAsSym, SubReg);

        if (ParentValue->isUndef())
          return UndefinedVal();

        // Other cases: give up.  We are indexing into a larger object
        // that has some value, but we don't know how to handle that yet.
        return UnknownVal();
      }
    }
  }
  return std::nullopt;
}

SVal RegionStoreManager::getBindingForElement(RegionBindingsConstRef B,
                                              const ElementRegion* R) {
  // Check if the region has a binding.
  if (const std::optional<SVal> &V = B.getDirectBinding(R))
    return *V;

  const MemRegion* superR = R->getSuperRegion();

  // Check if the region is an element region of a string literal.
  if (const StringRegion *StrR = dyn_cast<StringRegion>(superR)) {
    // FIXME: Handle loads from strings where the literal is treated as
    // an integer, e.g., *((unsigned int*)"hello"). Such loads are UB according
    // to C++20 7.2.1.11 [basic.lval].
    QualType T = Ctx.getAsArrayType(StrR->getValueType())->getElementType();
    if (!Ctx.hasSameUnqualifiedType(T, R->getElementType()))
      return UnknownVal();
    if (const auto CI = R->getIndex().getAs<nonloc::ConcreteInt>()) {
      const llvm::APSInt &Idx = CI->getValue();
      if (Idx < 0)
        return UndefinedVal();
      const StringLiteral *SL = StrR->getStringLiteral();
      return getSValFromStringLiteral(SL, Idx.getZExtValue(), T);
    }
  } else if (isa<ElementRegion, VarRegion>(superR)) {
    if (std::optional<SVal> V = getConstantValFromConstArrayInitializer(B, R))
      return *V;
  }

  // Check for loads from a code text region.  For such loads, just give up.
  if (isa<CodeTextRegion>(superR))
    return UnknownVal();

  // Handle the case where we are indexing into a larger scalar object.
  // For example, this handles:
  //   int x = ...
  //   char *y = &x;
  //   return *y;
  // FIXME: This is a hack, and doesn't do anything really intelligent yet.
  const RegionRawOffset &O = R->getAsArrayOffset();

  // If we cannot reason about the offset, return an unknown value.
  if (!O.getRegion())
    return UnknownVal();

  if (const TypedValueRegion *baseR = dyn_cast<TypedValueRegion>(O.getRegion()))
    if (auto V = getDerivedSymbolForBinding(B, baseR, R, Ctx, svalBuilder))
      return *V;

  return getBindingForFieldOrElementCommon(B, R, R->getElementType());
}

SVal RegionStoreManager::getBindingForField(RegionBindingsConstRef B,
                                            const FieldRegion* R) {

  // Check if the region has a binding.
  if (const std::optional<SVal> &V = B.getDirectBinding(R))
    return *V;

  // If the containing record was initialized, try to get its constant value.
  const FieldDecl *FD = R->getDecl();
  QualType Ty = FD->getType();
  const MemRegion* superR = R->getSuperRegion();
  if (const auto *VR = dyn_cast<VarRegion>(superR)) {
    const VarDecl *VD = VR->getDecl();
    QualType RecordVarTy = VD->getType();
    unsigned Index = FD->getFieldIndex();
    // Either the record variable or the field has an initializer that we can
    // trust. We trust initializers of constants and, additionally, respect
    // initializers of globals when analyzing main().
    if (RecordVarTy.isConstQualified() || Ty.isConstQualified() ||
        (B.isMainAnalysis() && VD->hasGlobalStorage()))
      if (const Expr *Init = VD->getAnyInitializer())
        if (const auto *InitList = dyn_cast<InitListExpr>(Init)) {
          if (Index < InitList->getNumInits()) {
            if (const Expr *FieldInit = InitList->getInit(Index))
              if (std::optional<SVal> V = svalBuilder.getConstantVal(FieldInit))
                return *V;
          } else {
            return svalBuilder.makeZeroVal(Ty);
          }
        }
  }

  // Handle the case where we are accessing into a larger scalar object.
  // For example, this handles:
  //   struct header {
  //     unsigned a : 1;
  //     unsigned b : 1;
  //   };
  //   struct parse_t {
  //     unsigned bits0 : 1;
  //     unsigned bits2 : 2; // <-- header
  //     unsigned bits4 : 4;
  //   };
  //   int parse(parse_t *p) {
  //     unsigned copy = p->bits2;
  //     header *bits = (header *)&copy;
  //     return bits->b;  <-- here
  //   }
  if (const auto *Base = dyn_cast<TypedValueRegion>(R->getBaseRegion()))
    if (auto V = getDerivedSymbolForBinding(B, Base, R, Ctx, svalBuilder))
      return *V;

  return getBindingForFieldOrElementCommon(B, R, Ty);
}

std::optional<SVal> RegionStoreManager::getBindingForDerivedDefaultValue(
    RegionBindingsConstRef B, const MemRegion *superR,
    const TypedValueRegion *R, QualType Ty) {

  if (const std::optional<SVal> &D = B.getDefaultBinding(superR)) {
    SVal val = *D;
    if (SymbolRef parentSym = val.getAsSymbol())
      return svalBuilder.getDerivedRegionValueSymbolVal(parentSym, R);

    if (val.isZeroConstant())
      return svalBuilder.makeZeroVal(Ty);

    if (val.isUnknownOrUndef())
      return val;

    // Lazy bindings are usually handled through getExistingLazyBinding().
    // We should unify these two code paths at some point.
    if (isa<nonloc::LazyCompoundVal, nonloc::CompoundVal>(val))
      return val;

    llvm_unreachable("Unknown default value");
  }

  return std::nullopt;
}

SVal RegionStoreManager::getLazyBinding(const SubRegion *LazyBindingRegion,
                                        RegionBindingsRef LazyBinding) {
  SVal Result;
  if (const ElementRegion *ER = dyn_cast<ElementRegion>(LazyBindingRegion))
    Result = getBindingForElement(LazyBinding, ER);
  else
    Result = getBindingForField(LazyBinding,
                                cast<FieldRegion>(LazyBindingRegion));

  // FIXME: This is a hack to deal with RegionStore's inability to distinguish a
  // default value for /part/ of an aggregate from a default value for the
  // /entire/ aggregate. The most common case of this is when struct Outer
  // has as its first member a struct Inner, which is copied in from a stack
  // variable. In this case, even if the Outer's default value is symbolic, 0,
  // or unknown, it gets overridden by the Inner's default value of undefined.
  //
  // This is a general problem -- if the Inner is zero-initialized, the Outer
  // will now look zero-initialized. The proper way to solve this is with a
  // new version of RegionStore that tracks the extent of a binding as well
  // as the offset.
  //
  // This hack only takes care of the undefined case because that can very
  // quickly result in a warning.
  if (Result.isUndef())
    Result = UnknownVal();

  return Result;
}

SVal
RegionStoreManager::getBindingForFieldOrElementCommon(RegionBindingsConstRef B,
                                                      const TypedValueRegion *R,
                                                      QualType Ty) {

  // At this point we have already checked in either getBindingForElement or
  // getBindingForField if 'R' has a direct binding.

  // Lazy binding?
  Store lazyBindingStore = nullptr;
  const SubRegion *lazyBindingRegion = nullptr;
  std::tie(lazyBindingStore, lazyBindingRegion) = findLazyBinding(B, R, R);
  if (lazyBindingRegion)
    return getLazyBinding(lazyBindingRegion,
                          getRegionBindings(lazyBindingStore));

  // Record whether or not we see a symbolic index.  That can completely
  // be out of scope of our lookup.
  bool hasSymbolicIndex = false;

  // FIXME: This is a hack to deal with RegionStore's inability to distinguish a
  // default value for /part/ of an aggregate from a default value for the
  // /entire/ aggregate. The most common case of this is when struct Outer
  // has as its first member a struct Inner, which is copied in from a stack
  // variable. In this case, even if the Outer's default value is symbolic, 0,
  // or unknown, it gets overridden by the Inner's default value of undefined.
  //
  // This is a general problem -- if the Inner is zero-initialized, the Outer
  // will now look zero-initialized. The proper way to solve this is with a
  // new version of RegionStore that tracks the extent of a binding as well
  // as the offset.
  //
  // This hack only takes care of the undefined case because that can very
  // quickly result in a warning.
  bool hasPartialLazyBinding = false;

  const SubRegion *SR = R;
  while (SR) {
    const MemRegion *Base = SR->getSuperRegion();
    if (std::optional<SVal> D =
            getBindingForDerivedDefaultValue(B, Base, R, Ty)) {
      if (D->getAs<nonloc::LazyCompoundVal>()) {
        hasPartialLazyBinding = true;
        break;
      }

      return *D;
    }

    if (const ElementRegion *ER = dyn_cast<ElementRegion>(Base)) {
      NonLoc index = ER->getIndex();
      if (!index.isConstant())
        hasSymbolicIndex = true;
    }

    // If our super region is a field or element itself, walk up the region
    // hierarchy to see if there is a default value installed in an ancestor.
    SR = dyn_cast<SubRegion>(Base);
  }

  if (R->hasStackNonParametersStorage()) {
    if (isa<ElementRegion>(R)) {
      // Currently we don't reason specially about Clang-style vectors.  Check
      // if superR is a vector and if so return Unknown.
      if (const TypedValueRegion *typedSuperR =
            dyn_cast<TypedValueRegion>(R->getSuperRegion())) {
        if (typedSuperR->getValueType()->isVectorType())
          return UnknownVal();
      }
    }

    // FIXME: We also need to take ElementRegions with symbolic indexes into
    // account.  This case handles both directly accessing an ElementRegion
    // with a symbolic offset, but also fields within an element with
    // a symbolic offset.
    if (hasSymbolicIndex)
      return UnknownVal();

    // Additionally allow introspection of a block's internal layout.
    // Try to get direct binding if all other attempts failed thus far.
    // Else, return UndefinedVal()
    if (!hasPartialLazyBinding && !isa<BlockDataRegion>(R->getBaseRegion())) {
      if (const std::optional<SVal> &V = B.getDefaultBinding(R))
        return *V;
      return UndefinedVal();
    }
  }

  // All other values are symbolic.
  return svalBuilder.getRegionValueSymbolVal(R);
}

SVal RegionStoreManager::getBindingForObjCIvar(RegionBindingsConstRef B,
                                               const ObjCIvarRegion* R) {
  // Check if the region has a binding.
  if (const std::optional<SVal> &V = B.getDirectBinding(R))
    return *V;

  const MemRegion *superR = R->getSuperRegion();

  // Check if the super region has a default binding.
  if (const std::optional<SVal> &V = B.getDefaultBinding(superR)) {
    if (SymbolRef parentSym = V->getAsSymbol())
      return svalBuilder.getDerivedRegionValueSymbolVal(parentSym, R);

    // Other cases: give up.
    return UnknownVal();
  }

  return getBindingForLazySymbol(R);
}

SVal RegionStoreManager::getBindingForVar(RegionBindingsConstRef B,
                                          const VarRegion *R) {

  // Check if the region has a binding.
  if (std::optional<SVal> V = B.getDirectBinding(R))
    return *V;

  if (std::optional<SVal> V = B.getDefaultBinding(R))
    return *V;

  // Lazily derive a value for the VarRegion.
  const VarDecl *VD = R->getDecl();
  const MemSpaceRegion *MS = R->getMemorySpace();

  // Arguments are always symbolic.
  if (isa<StackArgumentsSpaceRegion>(MS))
    return svalBuilder.getRegionValueSymbolVal(R);

  // Is 'VD' declared constant?  If so, retrieve the constant value.
  if (VD->getType().isConstQualified()) {
    if (const Expr *Init = VD->getAnyInitializer()) {
      if (std::optional<SVal> V = svalBuilder.getConstantVal(Init))
        return *V;

      // If the variable is const qualified and has an initializer but
      // we couldn't evaluate initializer to a value, treat the value as
      // unknown.
      return UnknownVal();
    }
  }

  // This must come after the check for constants because closure-captured
  // constant variables may appear in UnknownSpaceRegion.
  if (isa<UnknownSpaceRegion>(MS))
    return svalBuilder.getRegionValueSymbolVal(R);

  if (isa<GlobalsSpaceRegion>(MS)) {
    QualType T = VD->getType();

    // If we're in main(), then global initializers have not become stale yet.
    if (B.isMainAnalysis())
      if (const Expr *Init = VD->getAnyInitializer())
        if (std::optional<SVal> V = svalBuilder.getConstantVal(Init))
          return *V;

    // Function-scoped static variables are default-initialized to 0; if they
    // have an initializer, it would have been processed by now.
    // FIXME: This is only true when we're starting analysis from main().
    // We're losing a lot of coverage here.
    if (isa<StaticGlobalSpaceRegion>(MS))
      return svalBuilder.makeZeroVal(T);

    if (std::optional<SVal> V = getBindingForDerivedDefaultValue(B, MS, R, T)) {
      assert(!V->getAs<nonloc::LazyCompoundVal>());
      return *V;
    }

    return svalBuilder.getRegionValueSymbolVal(R);
  }

  return UndefinedVal();
}

SVal RegionStoreManager::getBindingForLazySymbol(const TypedValueRegion *R) {
  // All other values are symbolic.
  return svalBuilder.getRegionValueSymbolVal(R);
}

const RegionStoreManager::SValListTy &
RegionStoreManager::getInterestingValues(nonloc::LazyCompoundVal LCV) {
  // First, check the cache.
  LazyBindingsMapTy::iterator I = LazyBindingsMap.find(LCV.getCVData());
  if (I != LazyBindingsMap.end())
    return I->second;

  // If we don't have a list of values cached, start constructing it.
  SValListTy List;

  const SubRegion *LazyR = LCV.getRegion();
  RegionBindingsRef B = getRegionBindings(LCV.getStore());

  // If this region had /no/ bindings at the time, there are no interesting
  // values to return.
  const ClusterBindings *Cluster = B.lookup(LazyR->getBaseRegion());
  if (!Cluster)
    return (LazyBindingsMap[LCV.getCVData()] = std::move(List));

  SmallVector<BindingPair, 32> Bindings;
  collectSubRegionBindings(Bindings, svalBuilder, *Cluster, LazyR,
                           /*IncludeAllDefaultBindings=*/true);
  for (SVal V : llvm::make_second_range(Bindings)) {
    if (V.isUnknownOrUndef() || V.isConstant())
      continue;

    if (auto InnerLCV = V.getAs<nonloc::LazyCompoundVal>()) {
      const SValListTy &InnerList = getInterestingValues(*InnerLCV);
      List.insert(List.end(), InnerList.begin(), InnerList.end());
    }

    List.push_back(V);
  }

  return (LazyBindingsMap[LCV.getCVData()] = std::move(List));
}

NonLoc RegionStoreManager::createLazyBinding(RegionBindingsConstRef B,
                                             const TypedValueRegion *R) {
  if (std::optional<nonloc::LazyCompoundVal> V =
          getExistingLazyBinding(svalBuilder, B, R, false))
    return *V;

  return svalBuilder.makeLazyCompoundVal(StoreRef(B.asStore(), *this), R);
}

static bool isRecordEmpty(const RecordDecl *RD) {
  if (!RD->field_empty())
    return false;
  if (const CXXRecordDecl *CRD = dyn_cast<CXXRecordDecl>(RD))
    return CRD->getNumBases() == 0;
  return true;
}

SVal RegionStoreManager::getBindingForStruct(RegionBindingsConstRef B,
                                             const TypedValueRegion *R) {
  const RecordDecl *RD = R->getValueType()->castAs<RecordType>()->getDecl();
  if (!RD->getDefinition() || isRecordEmpty(RD))
    return UnknownVal();

  return createLazyBinding(B, R);
}

SVal RegionStoreManager::getBindingForArray(RegionBindingsConstRef B,
                                            const TypedValueRegion *R) {
  assert(Ctx.getAsConstantArrayType(R->getValueType()) &&
         "Only constant array types can have compound bindings.");

  return createLazyBinding(B, R);
}

bool RegionStoreManager::includedInBindings(Store store,
                                            const MemRegion *region) const {
  RegionBindingsRef B = getRegionBindings(store);
  region = region->getBaseRegion();

  // Quick path: if the base is the head of a cluster, the region is live.
  if (B.lookup(region))
    return true;

  // Slow path: if the region is the VALUE of any binding, it is live.
  for (RegionBindingsRef::iterator RI = B.begin(), RE = B.end(); RI != RE; ++RI) {
    const ClusterBindings &Cluster = RI.getData();
    for (ClusterBindings::iterator CI = Cluster.begin(), CE = Cluster.end();
         CI != CE; ++CI) {
      SVal D = CI.getData();
      if (const MemRegion *R = D.getAsRegion())
        if (R->getBaseRegion() == region)
          return true;
    }
  }

  return false;
}

//===----------------------------------------------------------------------===//
// Binding values to regions.
//===----------------------------------------------------------------------===//

StoreRef RegionStoreManager::killBinding(Store ST, Loc L) {
  if (std::optional<loc::MemRegionVal> LV = L.getAs<loc::MemRegionVal>())
    if (const MemRegion* R = LV->getRegion())
      return StoreRef(getRegionBindings(ST).removeBinding(R)
                                           .asImmutableMap()
                                           .getRootWithoutRetain(),
                      *this);

  return StoreRef(ST, *this);
}

RegionBindingsRef
RegionStoreManager::bind(RegionBindingsConstRef B, Loc L, SVal V) {
  // We only care about region locations.
  auto MemRegVal = L.getAs<loc::MemRegionVal>();
  if (!MemRegVal)
    return B;

  const MemRegion *R = MemRegVal->getRegion();

  // Check if the region is a struct region.
  if (const TypedValueRegion* TR = dyn_cast<TypedValueRegion>(R)) {
    QualType Ty = TR->getValueType();
    if (Ty->isArrayType())
      return bindArray(B, TR, V);
    if (Ty->isStructureOrClassType())
      return bindStruct(B, TR, V);
    if (Ty->isVectorType())
      return bindVector(B, TR, V);
    if (Ty->isUnionType())
      return bindAggregate(B, TR, V);
  }

  // Binding directly to a symbolic region should be treated as binding
  // to element 0.
  if (const SymbolicRegion *SR = dyn_cast<SymbolicRegion>(R))
    R = GetElementZeroRegion(SR, SR->getPointeeStaticType());

  assert((!isa<CXXThisRegion>(R) || !B.lookup(R)) &&
         "'this' pointer is not an l-value and is not assignable");

  // Clear out bindings that may overlap with this binding.
  RegionBindingsRef NewB = removeSubRegionBindings(B, cast<SubRegion>(R));

  // LazyCompoundVals should be always bound as 'default' bindings.
  auto KeyKind = isa<nonloc::LazyCompoundVal>(V) ? BindingKey::Default
                                                 : BindingKey::Direct;
  return NewB.addBinding(BindingKey::Make(R, KeyKind), V);
}

RegionBindingsRef
RegionStoreManager::setImplicitDefaultValue(RegionBindingsConstRef B,
                                            const MemRegion *R,
                                            QualType T) {
  SVal V;

  if (Loc::isLocType(T))
    V = svalBuilder.makeNullWithType(T);
  else if (T->isIntegralOrEnumerationType())
    V = svalBuilder.makeZeroVal(T);
  else if (T->isStructureOrClassType() || T->isArrayType()) {
    // Set the default value to a zero constant when it is a structure
    // or array.  The type doesn't really matter.
    V = svalBuilder.makeZeroVal(Ctx.IntTy);
  }
  else {
    // We can't represent values of this type, but we still need to set a value
    // to record that the region has been initialized.
    // If this assertion ever fires, a new case should be added above -- we
    // should know how to default-initialize any value we can symbolicate.
    assert(!SymbolManager::canSymbolicate(T) && "This type is representable");
    V = UnknownVal();
  }

  return B.addBinding(R, BindingKey::Default, V);
}

std::optional<RegionBindingsRef> RegionStoreManager::tryBindSmallArray(
    RegionBindingsConstRef B, const TypedValueRegion *R, const ArrayType *AT,
    nonloc::LazyCompoundVal LCV) {

  auto CAT = dyn_cast<ConstantArrayType>(AT);

  // If we don't know the size, create a lazyCompoundVal instead.
  if (!CAT)
    return std::nullopt;

  QualType Ty = CAT->getElementType();
  if (!(Ty->isScalarType() || Ty->isReferenceType()))
    return std::nullopt;

  // If the array is too big, create a LCV instead.
  uint64_t ArrSize = CAT->getLimitedSize();
  if (ArrSize > SmallArrayLimit)
    return std::nullopt;

  RegionBindingsRef NewB = B;

  for (uint64_t i = 0; i < ArrSize; ++i) {
    auto Idx = svalBuilder.makeArrayIndex(i);
    const ElementRegion *SrcER =
        MRMgr.getElementRegion(Ty, Idx, LCV.getRegion(), Ctx);
    SVal V = getBindingForElement(getRegionBindings(LCV.getStore()), SrcER);

    const ElementRegion *DstER = MRMgr.getElementRegion(Ty, Idx, R, Ctx);
    NewB = bind(NewB, loc::MemRegionVal(DstER), V);
  }

  return NewB;
}

RegionBindingsRef
RegionStoreManager::bindArray(RegionBindingsConstRef B,
                              const TypedValueRegion* R,
                              SVal Init) {

  const ArrayType *AT =cast<ArrayType>(Ctx.getCanonicalType(R->getValueType()));
  QualType ElementTy = AT->getElementType();
  std::optional<uint64_t> Size;

  if (const ConstantArrayType* CAT = dyn_cast<ConstantArrayType>(AT))
    Size = CAT->getZExtSize();

  // Check if the init expr is a literal. If so, bind the rvalue instead.
  // FIXME: It's not responsibility of the Store to transform this lvalue
  // to rvalue. ExprEngine or maybe even CFG should do this before binding.
  if (std::optional<loc::MemRegionVal> MRV = Init.getAs<loc::MemRegionVal>()) {
    SVal V = getBinding(B.asStore(), *MRV, R->getValueType());
    return bindAggregate(B, R, V);
  }

  // Handle lazy compound values.
  if (std::optional<nonloc::LazyCompoundVal> LCV =
          Init.getAs<nonloc::LazyCompoundVal>()) {
    if (std::optional<RegionBindingsRef> NewB =
            tryBindSmallArray(B, R, AT, *LCV))
      return *NewB;

    return bindAggregate(B, R, Init);
  }

  if (Init.isUnknown())
    return bindAggregate(B, R, UnknownVal());

  // Remaining case: explicit compound values.
  const nonloc::CompoundVal& CV = Init.castAs<nonloc::CompoundVal>();
  nonloc::CompoundVal::iterator VI = CV.begin(), VE = CV.end();
  uint64_t i = 0;

  RegionBindingsRef NewB(B);

  for (; Size ? i < *Size : true; ++i, ++VI) {
    // The init list might be shorter than the array length.
    if (VI == VE)
      break;

    NonLoc Idx = svalBuilder.makeArrayIndex(i);
    const ElementRegion *ER = MRMgr.getElementRegion(ElementTy, Idx, R, Ctx);

    if (ElementTy->isStructureOrClassType())
      NewB = bindStruct(NewB, ER, *VI);
    else if (ElementTy->isArrayType())
      NewB = bindArray(NewB, ER, *VI);
    else
      NewB = bind(NewB, loc::MemRegionVal(ER), *VI);
  }

  // If the init list is shorter than the array length (or the array has
  // variable length), set the array default value. Values that are already set
  // are not overwritten.
  if (!Size || i < *Size)
    NewB = setImplicitDefaultValue(NewB, R, ElementTy);

  return NewB;
}

RegionBindingsRef RegionStoreManager::bindVector(RegionBindingsConstRef B,
                                                 const TypedValueRegion* R,
                                                 SVal V) {
  QualType T = R->getValueType();
  const VectorType *VT = T->castAs<VectorType>(); // Use castAs for typedefs.

  // Handle lazy compound values and symbolic values.
  if (isa<nonloc::LazyCompoundVal, nonloc::SymbolVal>(V))
    return bindAggregate(B, R, V);

  // We may get non-CompoundVal accidentally due to imprecise cast logic or
  // that we are binding symbolic struct value. Kill the field values, and if
  // the value is symbolic go and bind it as a "default" binding.
  if (!isa<nonloc::CompoundVal>(V)) {
    return bindAggregate(B, R, UnknownVal());
  }

  QualType ElemType = VT->getElementType();
  nonloc::CompoundVal CV = V.castAs<nonloc::CompoundVal>();
  nonloc::CompoundVal::iterator VI = CV.begin(), VE = CV.end();
  unsigned index = 0, numElements = VT->getNumElements();
  RegionBindingsRef NewB(B);

  for ( ; index != numElements ; ++index) {
    if (VI == VE)
      break;

    NonLoc Idx = svalBuilder.makeArrayIndex(index);
    const ElementRegion *ER = MRMgr.getElementRegion(ElemType, Idx, R, Ctx);

    if (ElemType->isArrayType())
      NewB = bindArray(NewB, ER, *VI);
    else if (ElemType->isStructureOrClassType())
      NewB = bindStruct(NewB, ER, *VI);
    else
      NewB = bind(NewB, loc::MemRegionVal(ER), *VI);
  }
  return NewB;
}

std::optional<RegionBindingsRef> RegionStoreManager::tryBindSmallStruct(
    RegionBindingsConstRef B, const TypedValueRegion *R, const RecordDecl *RD,
    nonloc::LazyCompoundVal LCV) {
  FieldVector Fields;

  if (const CXXRecordDecl *Class = dyn_cast<CXXRecordDecl>(RD))
    if (Class->getNumBases() != 0 || Class->getNumVBases() != 0)
      return std::nullopt;

  for (const auto *FD : RD->fields()) {
    if (FD->isUnnamedBitField())
      continue;

    // If there are too many fields, or if any of the fields are aggregates,
    // just use the LCV as a default binding.
    if (Fields.size() == SmallStructLimit)
      return std::nullopt;

    QualType Ty = FD->getType();

    // Zero length arrays are basically no-ops, so we also ignore them here.
    if (Ty->isConstantArrayType() &&
        Ctx.getConstantArrayElementCount(Ctx.getAsConstantArrayType(Ty)) == 0)
      continue;

    if (!(Ty->isScalarType() || Ty->isReferenceType()))
      return std::nullopt;

    Fields.push_back(FD);
  }

  RegionBindingsRef NewB = B;

  for (const FieldDecl *Field : Fields) {
    const FieldRegion *SourceFR = MRMgr.getFieldRegion(Field, LCV.getRegion());
    SVal V = getBindingForField(getRegionBindings(LCV.getStore()), SourceFR);

    const FieldRegion *DestFR = MRMgr.getFieldRegion(Field, R);
    NewB = bind(NewB, loc::MemRegionVal(DestFR), V);
  }

  return NewB;
}

RegionBindingsRef RegionStoreManager::bindStruct(RegionBindingsConstRef B,
                                                 const TypedValueRegion *R,
                                                 SVal V) {
  QualType T = R->getValueType();
  assert(T->isStructureOrClassType());

  const RecordType* RT = T->castAs<RecordType>();
  const RecordDecl *RD = RT->getDecl();

  if (!RD->isCompleteDefinition())
    return B;

  // Handle lazy compound values and symbolic values.
  if (std::optional<nonloc::LazyCompoundVal> LCV =
          V.getAs<nonloc::LazyCompoundVal>()) {
    if (std::optional<RegionBindingsRef> NewB =
            tryBindSmallStruct(B, R, RD, *LCV))
      return *NewB;
    return bindAggregate(B, R, V);
  }
  if (isa<nonloc::SymbolVal>(V))
    return bindAggregate(B, R, V);

  // We may get non-CompoundVal accidentally due to imprecise cast logic or
  // that we are binding symbolic struct value. Kill the field values, and if
  // the value is symbolic go and bind it as a "default" binding.
  if (V.isUnknown() || !isa<nonloc::CompoundVal>(V))
    return bindAggregate(B, R, UnknownVal());

  // The raw CompoundVal is essentially a symbolic InitListExpr: an (immutable)
  // list of other values. It appears pretty much only when there's an actual
  // initializer list expression in the program, and the analyzer tries to
  // unwrap it as soon as possible.
  // This code is where such unwrap happens: when the compound value is put into
  // the object that it was supposed to initialize (it's an *initializer* list,
  // after all), instead of binding the whole value to the whole object, we bind
  // sub-values to sub-objects. Sub-values may themselves be compound values,
  // and in this case the procedure becomes recursive.
  // FIXME: The annoying part about compound values is that they don't carry
  // any sort of information about which value corresponds to which sub-object.
  // It's simply a list of values in the middle of nowhere; we expect to match
  // them to sub-objects, essentially, "by index": first value binds to
  // the first field, second value binds to the second field, etc.
  // It would have been much safer to organize non-lazy compound values as
  // a mapping from fields/bases to values.
  const nonloc::CompoundVal& CV = V.castAs<nonloc::CompoundVal>();
  nonloc::CompoundVal::iterator VI = CV.begin(), VE = CV.end();

  RegionBindingsRef NewB(B);

  // In C++17 aggregates may have base classes, handle those as well.
  // They appear before fields in the initializer list / compound value.
  if (const auto *CRD = dyn_cast<CXXRecordDecl>(RD)) {
    // If the object was constructed with a constructor, its value is a
    // LazyCompoundVal. If it's a raw CompoundVal, it means that we're
    // performing aggregate initialization. The only exception from this
    // rule is sending an Objective-C++ message that returns a C++ object
    // to a nil receiver; in this case the semantics is to return a
    // zero-initialized object even if it's a C++ object that doesn't have
    // this sort of constructor; the CompoundVal is empty in this case.
    assert((CRD->isAggregate() || (Ctx.getLangOpts().ObjC && VI == VE)) &&
           "Non-aggregates are constructed with a constructor!");

    for (const auto &B : CRD->bases()) {
      // (Multiple inheritance is fine though.)
      assert(!B.isVirtual() && "Aggregates cannot have virtual base classes!");

      if (VI == VE)
        break;

      QualType BTy = B.getType();
      assert(BTy->isStructureOrClassType() && "Base classes must be classes!");

      const CXXRecordDecl *BRD = BTy->getAsCXXRecordDecl();
      assert(BRD && "Base classes must be C++ classes!");

      const CXXBaseObjectRegion *BR =
          MRMgr.getCXXBaseObjectRegion(BRD, R, /*IsVirtual=*/false);

      NewB = bindStruct(NewB, BR, *VI);

      ++VI;
    }
  }

  RecordDecl::field_iterator FI, FE;

  for (FI = RD->field_begin(), FE = RD->field_end(); FI != FE; ++FI) {

    if (VI == VE)
      break;

    // Skip any unnamed bitfields to stay in sync with the initializers.
    if (FI->isUnnamedBitField())
      continue;

    QualType FTy = FI->getType();
    const FieldRegion* FR = MRMgr.getFieldRegion(*FI, R);

    if (FTy->isArrayType())
      NewB = bindArray(NewB, FR, *VI);
    else if (FTy->isStructureOrClassType())
      NewB = bindStruct(NewB, FR, *VI);
    else
      NewB = bind(NewB, loc::MemRegionVal(FR), *VI);
    ++VI;
  }

  // There may be fewer values in the initialize list than the fields of struct.
  if (FI != FE) {
    NewB = NewB.addBinding(R, BindingKey::Default,
                           svalBuilder.makeIntVal(0, false));
  }

  return NewB;
}

RegionBindingsRef
RegionStoreManager::bindAggregate(RegionBindingsConstRef B,
                                  const TypedRegion *R,
                                  SVal Val) {
  // Remove the old bindings, using 'R' as the root of all regions
  // we will invalidate. Then add the new binding.
  return removeSubRegionBindings(B, R).addBinding(R, BindingKey::Default, Val);
}

//===----------------------------------------------------------------------===//
// State pruning.
//===----------------------------------------------------------------------===//

namespace {
class RemoveDeadBindingsWorker
    : public ClusterAnalysis<RemoveDeadBindingsWorker> {
  SmallVector<const SymbolicRegion *, 12> Postponed;
  SymbolReaper &SymReaper;
  const StackFrameContext *CurrentLCtx;

public:
  RemoveDeadBindingsWorker(RegionStoreManager &rm,
                           ProgramStateManager &stateMgr,
                           RegionBindingsRef b, SymbolReaper &symReaper,
                           const StackFrameContext *LCtx)
    : ClusterAnalysis<RemoveDeadBindingsWorker>(rm, stateMgr, b),
      SymReaper(symReaper), CurrentLCtx(LCtx) {}

  // Called by ClusterAnalysis.
  void VisitAddedToCluster(const MemRegion *baseR, const ClusterBindings &C);
  void VisitCluster(const MemRegion *baseR, const ClusterBindings *C);
  using ClusterAnalysis<RemoveDeadBindingsWorker>::VisitCluster;

  using ClusterAnalysis::AddToWorkList;

  bool AddToWorkList(const MemRegion *R);

  bool UpdatePostponed();
  void VisitBinding(SVal V);
};
}

bool RemoveDeadBindingsWorker::AddToWorkList(const MemRegion *R) {
  const MemRegion *BaseR = R->getBaseRegion();
  return AddToWorkList(WorkListElement(BaseR), getCluster(BaseR));
}

void RemoveDeadBindingsWorker::VisitAddedToCluster(const MemRegion *baseR,
                                                   const ClusterBindings &C) {

  if (const VarRegion *VR = dyn_cast<VarRegion>(baseR)) {
    if (SymReaper.isLive(VR))
      AddToWorkList(baseR, &C);

    return;
  }

  if (const SymbolicRegion *SR = dyn_cast<SymbolicRegion>(baseR)) {
    if (SymReaper.isLive(SR->getSymbol()))
      AddToWorkList(SR, &C);
    else
      Postponed.push_back(SR);

    return;
  }

  if (isa<NonStaticGlobalSpaceRegion>(baseR)) {
    AddToWorkList(baseR, &C);
    return;
  }

  // CXXThisRegion in the current or parent location context is live.
  if (const CXXThisRegion *TR = dyn_cast<CXXThisRegion>(baseR)) {
    const auto *StackReg =
        cast<StackArgumentsSpaceRegion>(TR->getSuperRegion());
    const StackFrameContext *RegCtx = StackReg->getStackFrame();
    if (CurrentLCtx &&
        (RegCtx == CurrentLCtx || RegCtx->isParentOf(CurrentLCtx)))
      AddToWorkList(TR, &C);
  }
}

void RemoveDeadBindingsWorker::VisitCluster(const MemRegion *baseR,
                                            const ClusterBindings *C) {
  if (!C)
    return;

  // Mark the symbol for any SymbolicRegion with live bindings as live itself.
  // This means we should continue to track that symbol.
  if (const SymbolicRegion *SymR = dyn_cast<SymbolicRegion>(baseR))
    SymReaper.markLive(SymR->getSymbol());

  for (const auto &[Key, Val] : *C) {
    // Element index of a binding key is live.
    SymReaper.markElementIndicesLive(Key.getRegion());

    VisitBinding(Val);
  }
}

void RemoveDeadBindingsWorker::VisitBinding(SVal V) {
  // Is it a LazyCompoundVal? All referenced regions are live as well.
  // The LazyCompoundVal itself is not live but should be readable.
  if (auto LCS = V.getAs<nonloc::LazyCompoundVal>()) {
    SymReaper.markLazilyCopied(LCS->getRegion());

    for (SVal V : RM.getInterestingValues(*LCS)) {
      if (auto DepLCS = V.getAs<nonloc::LazyCompoundVal>())
        SymReaper.markLazilyCopied(DepLCS->getRegion());
      else
        VisitBinding(V);
    }

    return;
  }

  // If V is a region, then add it to the worklist.
  if (const MemRegion *R = V.getAsRegion()) {
    AddToWorkList(R);
    SymReaper.markLive(R);

    // All regions captured by a block are also live.
    if (const BlockDataRegion *BR = dyn_cast<BlockDataRegion>(R)) {
      for (auto Var : BR->referenced_vars())
        AddToWorkList(Var.getCapturedRegion());
    }
  }


  // Update the set of live symbols.
  for (SymbolRef Sym : V.symbols())
    SymReaper.markLive(Sym);
}

bool RemoveDeadBindingsWorker::UpdatePostponed() {
  // See if any postponed SymbolicRegions are actually live now, after
  // having done a scan.
  bool Changed = false;

  for (const SymbolicRegion *SR : Postponed) {
    if (SymReaper.isLive(SR->getSymbol())) {
      Changed |= AddToWorkList(SR);
      SR = nullptr;
    }
  }

  return Changed;
}

StoreRef RegionStoreManager::removeDeadBindings(Store store,
                                                const StackFrameContext *LCtx,
                                                SymbolReaper& SymReaper) {
  RegionBindingsRef B = getRegionBindings(store);
  RemoveDeadBindingsWorker W(*this, StateMgr, B, SymReaper, LCtx);
  W.GenerateClusters();

  // Enqueue the region roots onto the worklist.
  for (const MemRegion *Reg : SymReaper.regions()) {
    W.AddToWorkList(Reg);
  }

  do W.RunWorkList(); while (W.UpdatePostponed());

  // We have now scanned the store, marking reachable regions and symbols
  // as live.  We now remove all the regions that are dead from the store
  // as well as update DSymbols with the set symbols that are now dead.
  for (const MemRegion *Base : llvm::make_first_range(B)) {
    // If the cluster has been visited, we know the region has been marked.
    // Otherwise, remove the dead entry.
    if (!W.isVisited(Base))
      B = B.remove(Base);
  }

  return StoreRef(B.asStore(), *this);
}

//===----------------------------------------------------------------------===//
// Utility methods.
//===----------------------------------------------------------------------===//

void RegionStoreManager::printJson(raw_ostream &Out, Store S, const char *NL,
                                   unsigned int Space, bool IsDot) const {
  RegionBindingsRef Bindings = getRegionBindings(S);

  Indent(Out, Space, IsDot) << "\"store\": ";

  if (Bindings.isEmpty()) {
    Out << "null," << NL;
    return;
  }

  Out << "{ \"pointer\": \"" << Bindings.asStore() << "\", \"items\": [" << NL;
  Bindings.printJson(Out, NL, Space + 1, IsDot);
  Indent(Out, Space, IsDot) << "]}," << NL;
}
