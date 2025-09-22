//===- Store.h - Interface for maps from Locations to Values ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defined the types Store and StoreManager.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_STORE_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_STORE_H

#include "clang/AST/Type.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/MemRegion.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState_Fwd.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SValBuilder.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/StoreRef.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymExpr.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>

namespace clang {

class ASTContext;
class CastExpr;
class CompoundLiteralExpr;
class CXXBasePath;
class Decl;
class Expr;
class LocationContext;
class ObjCIvarDecl;
class StackFrameContext;

namespace ento {

class CallEvent;
class ProgramStateManager;
class ScanReachableSymbols;
class SymbolReaper;

using InvalidatedSymbols = llvm::DenseSet<SymbolRef>;

class StoreManager {
protected:
  SValBuilder &svalBuilder;
  ProgramStateManager &StateMgr;

  /// MRMgr - Manages region objects associated with this StoreManager.
  MemRegionManager &MRMgr;
  ASTContext &Ctx;

  StoreManager(ProgramStateManager &stateMgr);

public:
  virtual ~StoreManager() = default;

  /// Return the value bound to specified location in a given state.
  /// \param[in] store The store in which to make the lookup.
  /// \param[in] loc The symbolic memory location.
  /// \param[in] T An optional type that provides a hint indicating the
  ///   expected type of the returned value.  This is used if the value is
  ///   lazily computed.
  /// \return The value bound to the location \c loc.
  virtual SVal getBinding(Store store, Loc loc, QualType T = QualType()) = 0;

  /// Return the default value bound to a region in a given store. The default
  /// binding is the value of sub-regions that were not initialized separately
  /// from their base region. For example, if the structure is zero-initialized
  /// upon construction, this method retrieves the concrete zero value, even if
  /// some or all fields were later overwritten manually. Default binding may be
  /// an unknown, undefined, concrete, or symbolic value.
  /// \param[in] store The store in which to make the lookup.
  /// \param[in] R The region to find the default binding for.
  /// \return The default value bound to the region in the store, if a default
  /// binding exists.
  virtual std::optional<SVal> getDefaultBinding(Store store,
                                                const MemRegion *R) = 0;

  /// Return the default value bound to a LazyCompoundVal. The default binding
  /// is used to represent the value of any fields or elements within the
  /// structure represented by the LazyCompoundVal which were not initialized
  /// explicitly separately from the whole structure. Default binding may be an
  /// unknown, undefined, concrete, or symbolic value.
  /// \param[in] lcv The lazy compound value.
  /// \return The default value bound to the LazyCompoundVal \c lcv, if a
  /// default binding exists.
  std::optional<SVal> getDefaultBinding(nonloc::LazyCompoundVal lcv) {
    return getDefaultBinding(lcv.getStore(), lcv.getRegion());
  }

  /// Return a store with the specified value bound to the given location.
  /// \param[in] store The store in which to make the binding.
  /// \param[in] loc The symbolic memory location.
  /// \param[in] val The value to bind to location \c loc.
  /// \return A StoreRef object that contains the same
  ///   bindings as \c store with the addition of having the value specified
  ///   by \c val bound to the location given for \c loc.
  virtual StoreRef Bind(Store store, Loc loc, SVal val) = 0;

  /// Return a store with the specified value bound to all sub-regions of the
  /// region. The region must not have previous bindings. If you need to
  /// invalidate existing bindings, consider invalidateRegions().
  virtual StoreRef BindDefaultInitial(Store store, const MemRegion *R,
                                      SVal V) = 0;

  /// Return a store with in which all values within the given region are
  /// reset to zero. This method is allowed to overwrite previous bindings.
  virtual StoreRef BindDefaultZero(Store store, const MemRegion *R) = 0;

  /// Create a new store with the specified binding removed.
  /// \param ST the original store, that is the basis for the new store.
  /// \param L the location whose binding should be removed.
  virtual StoreRef killBinding(Store ST, Loc L) = 0;

  /// getInitialStore - Returns the initial "empty" store representing the
  ///  value bindings upon entry to an analyzed function.
  virtual StoreRef getInitialStore(const LocationContext *InitLoc) = 0;

  /// getRegionManager - Returns the internal RegionManager object that is
  ///  used to query and manipulate MemRegion objects.
  MemRegionManager& getRegionManager() { return MRMgr; }

  SValBuilder& getSValBuilder() { return svalBuilder; }

  virtual Loc getLValueVar(const VarDecl *VD, const LocationContext *LC) {
    return svalBuilder.makeLoc(MRMgr.getVarRegion(VD, LC));
  }

  Loc getLValueCompoundLiteral(const CompoundLiteralExpr *CL,
                               const LocationContext *LC) {
    return loc::MemRegionVal(MRMgr.getCompoundLiteralRegion(CL, LC));
  }

  virtual SVal getLValueIvar(const ObjCIvarDecl *decl, SVal base);

  virtual SVal getLValueField(const FieldDecl *D, SVal Base) {
    return getLValueFieldOrIvar(D, Base);
  }

  virtual SVal getLValueElement(QualType elementType, NonLoc offset, SVal Base);

  /// ArrayToPointer - Used by ExprEngine::VistCast to handle implicit
  ///  conversions between arrays and pointers.
  virtual SVal ArrayToPointer(Loc Array, QualType ElementTy) = 0;

  /// Evaluates a chain of derived-to-base casts through the path specified in
  /// \p Cast.
  SVal evalDerivedToBase(SVal Derived, const CastExpr *Cast);

  /// Evaluates a chain of derived-to-base casts through the specified path.
  SVal evalDerivedToBase(SVal Derived, const CXXBasePath &CastPath);

  /// Evaluates a derived-to-base cast through a single level of derivation.
  SVal evalDerivedToBase(SVal Derived, QualType DerivedPtrType,
                         bool IsVirtual);

  /// Attempts to do a down cast. Used to model BaseToDerived and C++
  ///        dynamic_cast.
  /// The callback may result in the following 3 scenarios:
  ///  - Successful cast (ex: derived is subclass of base).
  ///  - Failed cast (ex: derived is definitely not a subclass of base).
  ///    The distinction of this case from the next one is necessary to model
  ///    dynamic_cast.
  ///  - We don't know (base is a symbolic region and we don't have
  ///    enough info to determine if the cast will succeed at run time).
  /// The function returns an optional with SVal representing the derived class
  /// in case of a successful cast and `std::nullopt` otherwise.
  std::optional<SVal> evalBaseToDerived(SVal Base, QualType DerivedPtrType);

  const ElementRegion *GetElementZeroRegion(const SubRegion *R, QualType T);

  /// castRegion - Used by ExprEngine::VisitCast to handle casts from
  ///  a MemRegion* to a specific location type.  'R' is the region being
  ///  casted and 'CastToTy' the result type of the cast.
  std::optional<const MemRegion *> castRegion(const MemRegion *region,
                                              QualType CastToTy);

  virtual StoreRef removeDeadBindings(Store store, const StackFrameContext *LCtx,
                                      SymbolReaper &SymReaper) = 0;

  virtual bool includedInBindings(Store store,
                                  const MemRegion *region) const = 0;

  /// If the StoreManager supports it, increment the reference count of
  /// the specified Store object.
  virtual void incrementReferenceCount(Store store) {}

  /// If the StoreManager supports it, decrement the reference count of
  /// the specified Store object.  If the reference count hits 0, the memory
  /// associated with the object is recycled.
  virtual void decrementReferenceCount(Store store) {}

  using InvalidatedRegions = SmallVector<const MemRegion *, 8>;

  /// invalidateRegions - Clears out the specified regions from the store,
  ///  marking their values as unknown. Depending on the store, this may also
  ///  invalidate additional regions that may have changed based on accessing
  ///  the given regions. Optionally, invalidates non-static globals as well.
  /// \param[in] store The initial store
  /// \param[in] Values The values to invalidate.
  /// \param[in] E The current statement being evaluated. Used to conjure
  ///   symbols to mark the values of invalidated regions.
  /// \param[in] Count The current block count. Used to conjure
  ///   symbols to mark the values of invalidated regions.
  /// \param[in] Call The call expression which will be used to determine which
  ///   globals should get invalidated.
  /// \param[in,out] IS A set to fill with any symbols that are no longer
  ///   accessible. Pass \c NULL if this information will not be used.
  /// \param[in] ITraits Information about invalidation for a particular
  ///   region/symbol.
  /// \param[in,out] InvalidatedTopLevel A vector to fill with regions
  ////  explicitly being invalidated. Pass \c NULL if this
  ///   information will not be used.
  /// \param[in,out] Invalidated A vector to fill with any regions being
  ///   invalidated. This should include any regions explicitly invalidated
  ///   even if they do not currently have bindings. Pass \c NULL if this
  ///   information will not be used.
  virtual StoreRef invalidateRegions(
      Store store, ArrayRef<SVal> Values, const Expr *Ex, unsigned Count,
      const LocationContext *LCtx, const CallEvent *Call,
      InvalidatedSymbols &IS, RegionAndSymbolInvalidationTraits &ITraits,
      InvalidatedRegions *TopLevelRegions, InvalidatedRegions *Invalidated) = 0;

  /// enterStackFrame - Let the StoreManager to do something when execution
  /// engine is about to execute into a callee.
  StoreRef enterStackFrame(Store store,
                           const CallEvent &Call,
                           const StackFrameContext *CalleeCtx);

  /// Finds the transitive closure of symbols within the given region.
  ///
  /// Returns false if the visitor aborted the scan.
  virtual bool scanReachableSymbols(Store S, const MemRegion *R,
                                    ScanReachableSymbols &Visitor) = 0;

  virtual void printJson(raw_ostream &Out, Store S, const char *NL,
                         unsigned int Space, bool IsDot) const = 0;

  class BindingsHandler {
  public:
    virtual ~BindingsHandler();

    /// \return whether the iteration should continue.
    virtual bool HandleBinding(StoreManager& SMgr, Store store,
                               const MemRegion *region, SVal val) = 0;
  };

  class FindUniqueBinding : public BindingsHandler {
    SymbolRef Sym;
    const MemRegion* Binding = nullptr;
    bool First = true;

  public:
    FindUniqueBinding(SymbolRef sym) : Sym(sym) {}

    explicit operator bool() { return First && Binding; }

    bool HandleBinding(StoreManager& SMgr, Store store, const MemRegion* R,
                       SVal val) override;
    const MemRegion *getRegion() { return Binding; }
  };

  /// iterBindings - Iterate over the bindings in the Store.
  virtual void iterBindings(Store store, BindingsHandler& f) = 0;

protected:
  const ElementRegion *MakeElementRegion(const SubRegion *baseRegion,
                                         QualType pointeeTy,
                                         uint64_t index = 0);

private:
  SVal getLValueFieldOrIvar(const Decl *decl, SVal base);
};

inline StoreRef::StoreRef(Store store, StoreManager & smgr)
    : store(store), mgr(smgr) {
  if (store)
    mgr.incrementReferenceCount(store);
}

inline StoreRef::StoreRef(const StoreRef &sr)
    : store(sr.store), mgr(sr.mgr)
{
  if (store)
    mgr.incrementReferenceCount(store);
}

inline StoreRef::~StoreRef() {
  if (store)
    mgr.decrementReferenceCount(store);
}

inline StoreRef &StoreRef::operator=(StoreRef const &newStore) {
  assert(&newStore.mgr == &mgr);
  if (store != newStore.store) {
    mgr.incrementReferenceCount(newStore.store);
    mgr.decrementReferenceCount(store);
    store = newStore.getStore();
  }
  return *this;
}

// FIXME: Do we need to pass ProgramStateManager anymore?
std::unique_ptr<StoreManager>
CreateRegionStoreManager(ProgramStateManager &StMgr);

} // namespace ento

} // namespace clang

#endif // LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_STORE_H
