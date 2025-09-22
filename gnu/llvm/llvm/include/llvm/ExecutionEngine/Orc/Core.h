//===------ Core.h -- Core ORC APIs (Layer, JITDylib, etc.) -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Contains core ORC APIs.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_CORE_H
#define LLVM_EXECUTIONENGINE_ORC_CORE_H

#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/FunctionExtras.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ExecutionEngine/JITLink/JITLinkDylib.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorSymbolDef.h"
#include "llvm/ExecutionEngine/Orc/Shared/WrapperFunctionUtils.h"
#include "llvm/ExecutionEngine/Orc/TaskDispatch.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ExtensibleRTTI.h"

#include <atomic>
#include <deque>
#include <future>
#include <memory>
#include <vector>

namespace llvm {
namespace orc {

// Forward declare some classes.
class AsynchronousSymbolQuery;
class ExecutionSession;
class MaterializationUnit;
class MaterializationResponsibility;
class JITDylib;
class ResourceTracker;
class InProgressLookupState;

enum class SymbolState : uint8_t;

using ResourceTrackerSP = IntrusiveRefCntPtr<ResourceTracker>;
using JITDylibSP = IntrusiveRefCntPtr<JITDylib>;

using ResourceKey = uintptr_t;

/// API to remove / transfer ownership of JIT resources.
class ResourceTracker : public ThreadSafeRefCountedBase<ResourceTracker> {
private:
  friend class ExecutionSession;
  friend class JITDylib;
  friend class MaterializationResponsibility;

public:
  ResourceTracker(const ResourceTracker &) = delete;
  ResourceTracker &operator=(const ResourceTracker &) = delete;
  ResourceTracker(ResourceTracker &&) = delete;
  ResourceTracker &operator=(ResourceTracker &&) = delete;

  ~ResourceTracker();

  /// Return the JITDylib targeted by this tracker.
  JITDylib &getJITDylib() const {
    return *reinterpret_cast<JITDylib *>(JDAndFlag.load() &
                                         ~static_cast<uintptr_t>(1));
  }

  /// Runs the given callback under the session lock, passing in the associated
  /// ResourceKey. This is the safe way to associate resources with trackers.
  template <typename Func> Error withResourceKeyDo(Func &&F);

  /// Remove all resources associated with this key.
  Error remove();

  /// Transfer all resources associated with this key to the given
  /// tracker, which must target the same JITDylib as this one.
  void transferTo(ResourceTracker &DstRT);

  /// Return true if this tracker has become defunct.
  bool isDefunct() const { return JDAndFlag.load() & 0x1; }

  /// Returns the key associated with this tracker.
  /// This method should not be used except for debug logging: there is no
  /// guarantee that the returned value will remain valid.
  ResourceKey getKeyUnsafe() const { return reinterpret_cast<uintptr_t>(this); }

private:
  ResourceTracker(JITDylibSP JD);

  void makeDefunct();

  std::atomic_uintptr_t JDAndFlag;
};

/// Listens for ResourceTracker operations.
class ResourceManager {
public:
  virtual ~ResourceManager();
  virtual Error handleRemoveResources(JITDylib &JD, ResourceKey K) = 0;
  virtual void handleTransferResources(JITDylib &JD, ResourceKey DstK,
                                       ResourceKey SrcK) = 0;
};

/// A set of symbol names (represented by SymbolStringPtrs for
//         efficiency).
using SymbolNameSet = DenseSet<SymbolStringPtr>;

/// A vector of symbol names.
using SymbolNameVector = std::vector<SymbolStringPtr>;

/// A map from symbol names (as SymbolStringPtrs) to JITSymbols
/// (address/flags pairs).
using SymbolMap = DenseMap<SymbolStringPtr, ExecutorSymbolDef>;

/// A map from symbol names (as SymbolStringPtrs) to JITSymbolFlags.
using SymbolFlagsMap = DenseMap<SymbolStringPtr, JITSymbolFlags>;

/// A map from JITDylibs to sets of symbols.
using SymbolDependenceMap = DenseMap<JITDylib *, SymbolNameSet>;

/// Lookup flags that apply to each dylib in the search order for a lookup.
///
/// If MatchHiddenSymbolsOnly is used (the default) for a given dylib, then
/// only symbols in that Dylib's interface will be searched. If
/// MatchHiddenSymbols is used then symbols with hidden visibility will match
/// as well.
enum class JITDylibLookupFlags { MatchExportedSymbolsOnly, MatchAllSymbols };

/// Lookup flags that apply to each symbol in a lookup.
///
/// If RequiredSymbol is used (the default) for a given symbol then that symbol
/// must be found during the lookup or the lookup will fail returning a
/// SymbolNotFound error. If WeaklyReferencedSymbol is used and the given
/// symbol is not found then the query will continue, and no result for the
/// missing symbol will be present in the result (assuming the rest of the
/// lookup succeeds).
enum class SymbolLookupFlags { RequiredSymbol, WeaklyReferencedSymbol };

/// Describes the kind of lookup being performed. The lookup kind is passed to
/// symbol generators (if they're invoked) to help them determine what
/// definitions to generate.
///
/// Static -- Lookup is being performed as-if at static link time (e.g.
///           generators representing static archives should pull in new
///           definitions).
///
/// DLSym -- Lookup is being performed as-if at runtime (e.g. generators
///          representing static archives should not pull in new definitions).
enum class LookupKind { Static, DLSym };

/// A list of (JITDylib*, JITDylibLookupFlags) pairs to be used as a search
/// order during symbol lookup.
using JITDylibSearchOrder =
    std::vector<std::pair<JITDylib *, JITDylibLookupFlags>>;

/// Convenience function for creating a search order from an ArrayRef of
/// JITDylib*, all with the same flags.
inline JITDylibSearchOrder makeJITDylibSearchOrder(
    ArrayRef<JITDylib *> JDs,
    JITDylibLookupFlags Flags = JITDylibLookupFlags::MatchExportedSymbolsOnly) {
  JITDylibSearchOrder O;
  O.reserve(JDs.size());
  for (auto *JD : JDs)
    O.push_back(std::make_pair(JD, Flags));
  return O;
}

/// A set of symbols to look up, each associated with a SymbolLookupFlags
/// value.
///
/// This class is backed by a vector and optimized for fast insertion,
/// deletion and iteration. It does not guarantee a stable order between
/// operations, and will not automatically detect duplicate elements (they
/// can be manually checked by calling the validate method).
class SymbolLookupSet {
public:
  using value_type = std::pair<SymbolStringPtr, SymbolLookupFlags>;
  using UnderlyingVector = std::vector<value_type>;
  using iterator = UnderlyingVector::iterator;
  using const_iterator = UnderlyingVector::const_iterator;

  SymbolLookupSet() = default;

  explicit SymbolLookupSet(
      SymbolStringPtr Name,
      SymbolLookupFlags Flags = SymbolLookupFlags::RequiredSymbol) {
    add(std::move(Name), Flags);
  }

  /// Construct a SymbolLookupSet from an initializer list of SymbolStringPtrs.
  explicit SymbolLookupSet(
      std::initializer_list<SymbolStringPtr> Names,
      SymbolLookupFlags Flags = SymbolLookupFlags::RequiredSymbol) {
    Symbols.reserve(Names.size());
    for (const auto &Name : Names)
      add(std::move(Name), Flags);
  }

  /// Construct a SymbolLookupSet from a SymbolNameSet with the given
  /// Flags used for each value.
  explicit SymbolLookupSet(
      const SymbolNameSet &Names,
      SymbolLookupFlags Flags = SymbolLookupFlags::RequiredSymbol) {
    Symbols.reserve(Names.size());
    for (const auto &Name : Names)
      add(Name, Flags);
  }

  /// Construct a SymbolLookupSet from a vector of symbols with the given Flags
  /// used for each value.
  /// If the ArrayRef contains duplicates it is up to the client to remove these
  /// before using this instance for lookup.
  explicit SymbolLookupSet(
      ArrayRef<SymbolStringPtr> Names,
      SymbolLookupFlags Flags = SymbolLookupFlags::RequiredSymbol) {
    Symbols.reserve(Names.size());
    for (const auto &Name : Names)
      add(Name, Flags);
  }

  /// Construct a SymbolLookupSet from DenseMap keys.
  template <typename ValT>
  static SymbolLookupSet
  fromMapKeys(const DenseMap<SymbolStringPtr, ValT> &M,
              SymbolLookupFlags Flags = SymbolLookupFlags::RequiredSymbol) {
    SymbolLookupSet Result;
    Result.Symbols.reserve(M.size());
    for (const auto &[Name, Val] : M)
      Result.add(Name, Flags);
    return Result;
  }

  /// Add an element to the set. The client is responsible for checking that
  /// duplicates are not added.
  SymbolLookupSet &
  add(SymbolStringPtr Name,
      SymbolLookupFlags Flags = SymbolLookupFlags::RequiredSymbol) {
    Symbols.push_back(std::make_pair(std::move(Name), Flags));
    return *this;
  }

  /// Quickly append one lookup set to another.
  SymbolLookupSet &append(SymbolLookupSet Other) {
    Symbols.reserve(Symbols.size() + Other.size());
    for (auto &KV : Other)
      Symbols.push_back(std::move(KV));
    return *this;
  }

  bool empty() const { return Symbols.empty(); }
  UnderlyingVector::size_type size() const { return Symbols.size(); }
  iterator begin() { return Symbols.begin(); }
  iterator end() { return Symbols.end(); }
  const_iterator begin() const { return Symbols.begin(); }
  const_iterator end() const { return Symbols.end(); }

  /// Removes the Ith element of the vector, replacing it with the last element.
  void remove(UnderlyingVector::size_type I) {
    std::swap(Symbols[I], Symbols.back());
    Symbols.pop_back();
  }

  /// Removes the element pointed to by the given iterator. This iterator and
  /// all subsequent ones (including end()) are invalidated.
  void remove(iterator I) { remove(I - begin()); }

  /// Removes all elements matching the given predicate, which must be callable
  /// as bool(const SymbolStringPtr &, SymbolLookupFlags Flags).
  template <typename PredFn> void remove_if(PredFn &&Pred) {
    UnderlyingVector::size_type I = 0;
    while (I != Symbols.size()) {
      const auto &Name = Symbols[I].first;
      auto Flags = Symbols[I].second;
      if (Pred(Name, Flags))
        remove(I);
      else
        ++I;
    }
  }

  /// Loop over the elements of this SymbolLookupSet, applying the Body function
  /// to each one. Body must be callable as
  /// bool(const SymbolStringPtr &, SymbolLookupFlags).
  /// If Body returns true then the element just passed in is removed from the
  /// set. If Body returns false then the element is retained.
  template <typename BodyFn>
  auto forEachWithRemoval(BodyFn &&Body) -> std::enable_if_t<
      std::is_same<decltype(Body(std::declval<const SymbolStringPtr &>(),
                                 std::declval<SymbolLookupFlags>())),
                   bool>::value> {
    UnderlyingVector::size_type I = 0;
    while (I != Symbols.size()) {
      const auto &Name = Symbols[I].first;
      auto Flags = Symbols[I].second;
      if (Body(Name, Flags))
        remove(I);
      else
        ++I;
    }
  }

  /// Loop over the elements of this SymbolLookupSet, applying the Body function
  /// to each one. Body must be callable as
  /// Expected<bool>(const SymbolStringPtr &, SymbolLookupFlags).
  /// If Body returns a failure value, the loop exits immediately. If Body
  /// returns true then the element just passed in is removed from the set. If
  /// Body returns false then the element is retained.
  template <typename BodyFn>
  auto forEachWithRemoval(BodyFn &&Body) -> std::enable_if_t<
      std::is_same<decltype(Body(std::declval<const SymbolStringPtr &>(),
                                 std::declval<SymbolLookupFlags>())),
                   Expected<bool>>::value,
      Error> {
    UnderlyingVector::size_type I = 0;
    while (I != Symbols.size()) {
      const auto &Name = Symbols[I].first;
      auto Flags = Symbols[I].second;
      auto Remove = Body(Name, Flags);
      if (!Remove)
        return Remove.takeError();
      if (*Remove)
        remove(I);
      else
        ++I;
    }
    return Error::success();
  }

  /// Construct a SymbolNameVector from this instance by dropping the Flags
  /// values.
  SymbolNameVector getSymbolNames() const {
    SymbolNameVector Names;
    Names.reserve(Symbols.size());
    for (const auto &KV : Symbols)
      Names.push_back(KV.first);
    return Names;
  }

  /// Sort the lookup set by pointer value. This sort is fast but sensitive to
  /// allocation order and so should not be used where a consistent order is
  /// required.
  void sortByAddress() { llvm::sort(Symbols, llvm::less_first()); }

  /// Sort the lookup set lexicographically. This sort is slow but the order
  /// is unaffected by allocation order.
  void sortByName() {
    llvm::sort(Symbols, [](const value_type &LHS, const value_type &RHS) {
      return *LHS.first < *RHS.first;
    });
  }

  /// Remove any duplicate elements. If a SymbolLookupSet is not duplicate-free
  /// by construction, this method can be used to turn it into a proper set.
  void removeDuplicates() {
    sortByAddress();
    auto LastI = llvm::unique(Symbols);
    Symbols.erase(LastI, Symbols.end());
  }

#ifndef NDEBUG
  /// Returns true if this set contains any duplicates. This should only be used
  /// in assertions.
  bool containsDuplicates() {
    if (Symbols.size() < 2)
      return false;
    sortByAddress();
    for (UnderlyingVector::size_type I = 1; I != Symbols.size(); ++I)
      if (Symbols[I].first == Symbols[I - 1].first)
        return true;
    return false;
  }
#endif

private:
  UnderlyingVector Symbols;
};

struct SymbolAliasMapEntry {
  SymbolAliasMapEntry() = default;
  SymbolAliasMapEntry(SymbolStringPtr Aliasee, JITSymbolFlags AliasFlags)
      : Aliasee(std::move(Aliasee)), AliasFlags(AliasFlags) {}

  SymbolStringPtr Aliasee;
  JITSymbolFlags AliasFlags;
};

/// A map of Symbols to (Symbol, Flags) pairs.
using SymbolAliasMap = DenseMap<SymbolStringPtr, SymbolAliasMapEntry>;

/// Callback to notify client that symbols have been resolved.
using SymbolsResolvedCallback = unique_function<void(Expected<SymbolMap>)>;

/// Callback to register the dependencies for a given query.
using RegisterDependenciesFunction =
    std::function<void(const SymbolDependenceMap &)>;

/// This can be used as the value for a RegisterDependenciesFunction if there
/// are no dependants to register with.
extern RegisterDependenciesFunction NoDependenciesToRegister;

class ResourceTrackerDefunct : public ErrorInfo<ResourceTrackerDefunct> {
public:
  static char ID;

  ResourceTrackerDefunct(ResourceTrackerSP RT);
  std::error_code convertToErrorCode() const override;
  void log(raw_ostream &OS) const override;

private:
  ResourceTrackerSP RT;
};

/// Used to notify a JITDylib that the given set of symbols failed to
/// materialize.
class FailedToMaterialize : public ErrorInfo<FailedToMaterialize> {
public:
  static char ID;

  FailedToMaterialize(std::shared_ptr<SymbolStringPool> SSP,
                      std::shared_ptr<SymbolDependenceMap> Symbols);
  ~FailedToMaterialize();
  std::error_code convertToErrorCode() const override;
  void log(raw_ostream &OS) const override;
  const SymbolDependenceMap &getSymbols() const { return *Symbols; }

private:
  std::shared_ptr<SymbolStringPool> SSP;
  std::shared_ptr<SymbolDependenceMap> Symbols;
};

/// Used to report failure due to unsatisfiable symbol dependencies.
class UnsatisfiedSymbolDependencies
    : public ErrorInfo<UnsatisfiedSymbolDependencies> {
public:
  static char ID;

  UnsatisfiedSymbolDependencies(std::shared_ptr<SymbolStringPool> SSP,
                                JITDylibSP JD, SymbolNameSet FailedSymbols,
                                SymbolDependenceMap BadDeps,
                                std::string Explanation);
  std::error_code convertToErrorCode() const override;
  void log(raw_ostream &OS) const override;

private:
  std::shared_ptr<SymbolStringPool> SSP;
  JITDylibSP JD;
  SymbolNameSet FailedSymbols;
  SymbolDependenceMap BadDeps;
  std::string Explanation;
};

/// Used to notify clients when symbols can not be found during a lookup.
class SymbolsNotFound : public ErrorInfo<SymbolsNotFound> {
public:
  static char ID;

  SymbolsNotFound(std::shared_ptr<SymbolStringPool> SSP, SymbolNameSet Symbols);
  SymbolsNotFound(std::shared_ptr<SymbolStringPool> SSP,
                  SymbolNameVector Symbols);
  std::error_code convertToErrorCode() const override;
  void log(raw_ostream &OS) const override;
  std::shared_ptr<SymbolStringPool> getSymbolStringPool() { return SSP; }
  const SymbolNameVector &getSymbols() const { return Symbols; }

private:
  std::shared_ptr<SymbolStringPool> SSP;
  SymbolNameVector Symbols;
};

/// Used to notify clients that a set of symbols could not be removed.
class SymbolsCouldNotBeRemoved : public ErrorInfo<SymbolsCouldNotBeRemoved> {
public:
  static char ID;

  SymbolsCouldNotBeRemoved(std::shared_ptr<SymbolStringPool> SSP,
                           SymbolNameSet Symbols);
  std::error_code convertToErrorCode() const override;
  void log(raw_ostream &OS) const override;
  std::shared_ptr<SymbolStringPool> getSymbolStringPool() { return SSP; }
  const SymbolNameSet &getSymbols() const { return Symbols; }

private:
  std::shared_ptr<SymbolStringPool> SSP;
  SymbolNameSet Symbols;
};

/// Errors of this type should be returned if a module fails to include
/// definitions that are claimed by the module's associated
/// MaterializationResponsibility. If this error is returned it is indicative of
/// a broken transformation / compiler / object cache.
class MissingSymbolDefinitions : public ErrorInfo<MissingSymbolDefinitions> {
public:
  static char ID;

  MissingSymbolDefinitions(std::shared_ptr<SymbolStringPool> SSP,
                           std::string ModuleName, SymbolNameVector Symbols)
      : SSP(std::move(SSP)), ModuleName(std::move(ModuleName)),
        Symbols(std::move(Symbols)) {}
  std::error_code convertToErrorCode() const override;
  void log(raw_ostream &OS) const override;
  std::shared_ptr<SymbolStringPool> getSymbolStringPool() { return SSP; }
  const std::string &getModuleName() const { return ModuleName; }
  const SymbolNameVector &getSymbols() const { return Symbols; }
private:
  std::shared_ptr<SymbolStringPool> SSP;
  std::string ModuleName;
  SymbolNameVector Symbols;
};

/// Errors of this type should be returned if a module contains definitions for
/// symbols that are not claimed by the module's associated
/// MaterializationResponsibility. If this error is returned it is indicative of
/// a broken transformation / compiler / object cache.
class UnexpectedSymbolDefinitions : public ErrorInfo<UnexpectedSymbolDefinitions> {
public:
  static char ID;

  UnexpectedSymbolDefinitions(std::shared_ptr<SymbolStringPool> SSP,
                              std::string ModuleName, SymbolNameVector Symbols)
      : SSP(std::move(SSP)), ModuleName(std::move(ModuleName)),
        Symbols(std::move(Symbols)) {}
  std::error_code convertToErrorCode() const override;
  void log(raw_ostream &OS) const override;
  std::shared_ptr<SymbolStringPool> getSymbolStringPool() { return SSP; }
  const std::string &getModuleName() const { return ModuleName; }
  const SymbolNameVector &getSymbols() const { return Symbols; }
private:
  std::shared_ptr<SymbolStringPool> SSP;
  std::string ModuleName;
  SymbolNameVector Symbols;
};

/// A set of symbols and the their dependencies. Used to describe dependencies
/// for the MaterializationResponsibility::notifyEmitted operation.
struct SymbolDependenceGroup {
  SymbolNameSet Symbols;
  SymbolDependenceMap Dependencies;
};

/// Tracks responsibility for materialization, and mediates interactions between
/// MaterializationUnits and JDs.
///
/// An instance of this class is passed to MaterializationUnits when their
/// materialize method is called. It allows MaterializationUnits to resolve and
/// emit symbols, or abandon materialization by notifying any unmaterialized
/// symbols of an error.
class MaterializationResponsibility {
  friend class ExecutionSession;
  friend class JITDylib;

public:
  MaterializationResponsibility(MaterializationResponsibility &&) = delete;
  MaterializationResponsibility &
  operator=(MaterializationResponsibility &&) = delete;

  /// Destruct a MaterializationResponsibility instance. In debug mode
  ///        this asserts that all symbols being tracked have been either
  ///        emitted or notified of an error.
  ~MaterializationResponsibility();

  /// Runs the given callback under the session lock, passing in the associated
  /// ResourceKey. This is the safe way to associate resources with trackers.
  template <typename Func> Error withResourceKeyDo(Func &&F) const {
    return RT->withResourceKeyDo(std::forward<Func>(F));
  }

  /// Returns the target JITDylib that these symbols are being materialized
  ///        into.
  JITDylib &getTargetJITDylib() const { return JD; }

  /// Returns the ExecutionSession for this instance.
  ExecutionSession &getExecutionSession() const;

  /// Returns the symbol flags map for this responsibility instance.
  /// Note: The returned flags may have transient flags (Lazy, Materializing)
  /// set. These should be stripped with JITSymbolFlags::stripTransientFlags
  /// before using.
  const SymbolFlagsMap &getSymbols() const { return SymbolFlags; }

  /// Returns the initialization pseudo-symbol, if any. This symbol will also
  /// be present in the SymbolFlagsMap for this MaterializationResponsibility
  /// object.
  const SymbolStringPtr &getInitializerSymbol() const { return InitSymbol; }

  /// Returns the names of any symbols covered by this
  /// MaterializationResponsibility object that have queries pending. This
  /// information can be used to return responsibility for unrequested symbols
  /// back to the JITDylib via the delegate method.
  SymbolNameSet getRequestedSymbols() const;

  /// Notifies the target JITDylib that the given symbols have been resolved.
  /// This will update the given symbols' addresses in the JITDylib, and notify
  /// any pending queries on the given symbols of their resolution. The given
  /// symbols must be ones covered by this MaterializationResponsibility
  /// instance. Individual calls to this method may resolve a subset of the
  /// symbols, but all symbols must have been resolved prior to calling emit.
  ///
  /// This method will return an error if any symbols being resolved have been
  /// moved to the error state due to the failure of a dependency. If this
  /// method returns an error then clients should log it and call
  /// failMaterialize. If no dependencies have been registered for the
  /// symbols covered by this MaterializationResponsibility then this method
  /// is guaranteed to return Error::success() and can be wrapped with cantFail.
  Error notifyResolved(const SymbolMap &Symbols);

  /// Notifies the target JITDylib (and any pending queries on that JITDylib)
  /// that all symbols covered by this MaterializationResponsibility instance
  /// have been emitted.
  ///
  /// The DepGroups array describes the dependencies of symbols being emitted on
  /// symbols that are outside this MaterializationResponsibility object. Each
  /// group consists of a pair of a set of symbols and a SymbolDependenceMap
  /// that describes the dependencies for the symbols in the first set. The
  /// elements of DepGroups must be non-overlapping (no symbol should appear in
  /// more than one of hte symbol sets), but do not have to be exhaustive. Any
  /// symbol in this MaterializationResponsibility object that is not covered
  /// by an entry will be treated as having no dependencies.
  ///
  /// This method will return an error if any symbols being resolved have been
  /// moved to the error state due to the failure of a dependency. If this
  /// method returns an error then clients should log it and call
  /// failMaterialize. If no dependencies have been registered for the
  /// symbols covered by this MaterializationResponsibility then this method
  /// is guaranteed to return Error::success() and can be wrapped with cantFail.
  Error notifyEmitted(ArrayRef<SymbolDependenceGroup> DepGroups);

  /// Attempt to claim responsibility for new definitions. This method can be
  /// used to claim responsibility for symbols that are added to a
  /// materialization unit during the compilation process (e.g. literal pool
  /// symbols). Symbol linkage rules are the same as for symbols that are
  /// defined up front: duplicate strong definitions will result in errors.
  /// Duplicate weak definitions will be discarded (in which case they will
  /// not be added to this responsibility instance).
  ///
  ///   This method can be used by materialization units that want to add
  /// additional symbols at materialization time (e.g. stubs, compile
  /// callbacks, metadata).
  Error defineMaterializing(SymbolFlagsMap SymbolFlags);

  /// Notify all not-yet-emitted covered by this MaterializationResponsibility
  /// instance that an error has occurred.
  /// This will remove all symbols covered by this MaterializationResponsibility
  /// from the target JITDylib, and send an error to any queries waiting on
  /// these symbols.
  void failMaterialization();

  /// Transfers responsibility to the given MaterializationUnit for all
  /// symbols defined by that MaterializationUnit. This allows
  /// materializers to break up work based on run-time information (e.g.
  /// by introspecting which symbols have actually been looked up and
  /// materializing only those).
  Error replace(std::unique_ptr<MaterializationUnit> MU);

  /// Delegates responsibility for the given symbols to the returned
  /// materialization responsibility. Useful for breaking up work between
  /// threads, or different kinds of materialization processes.
  Expected<std::unique_ptr<MaterializationResponsibility>>
  delegate(const SymbolNameSet &Symbols);

private:
  /// Create a MaterializationResponsibility for the given JITDylib and
  ///        initial symbols.
  MaterializationResponsibility(ResourceTrackerSP RT,
                                SymbolFlagsMap SymbolFlags,
                                SymbolStringPtr InitSymbol)
      : JD(RT->getJITDylib()), RT(std::move(RT)),
        SymbolFlags(std::move(SymbolFlags)), InitSymbol(std::move(InitSymbol)) {
    assert(!this->SymbolFlags.empty() && "Materializing nothing?");
  }

  JITDylib &JD;
  ResourceTrackerSP RT;
  SymbolFlagsMap SymbolFlags;
  SymbolStringPtr InitSymbol;
};

/// A MaterializationUnit represents a set of symbol definitions that can
///        be materialized as a group, or individually discarded (when
///        overriding definitions are encountered).
///
/// MaterializationUnits are used when providing lazy definitions of symbols to
/// JITDylibs. The JITDylib will call materialize when the address of a symbol
/// is requested via the lookup method. The JITDylib will call discard if a
/// stronger definition is added or already present.
class MaterializationUnit {
  friend class ExecutionSession;
  friend class JITDylib;

public:
  static char ID;

  struct Interface {
    Interface() = default;
    Interface(SymbolFlagsMap InitalSymbolFlags, SymbolStringPtr InitSymbol)
        : SymbolFlags(std::move(InitalSymbolFlags)),
          InitSymbol(std::move(InitSymbol)) {
      assert((!this->InitSymbol || this->SymbolFlags.count(this->InitSymbol)) &&
             "If set, InitSymbol should appear in InitialSymbolFlags map");
    }

    SymbolFlagsMap SymbolFlags;
    SymbolStringPtr InitSymbol;
  };

  MaterializationUnit(Interface I)
      : SymbolFlags(std::move(I.SymbolFlags)),
        InitSymbol(std::move(I.InitSymbol)) {}
  virtual ~MaterializationUnit() = default;

  /// Return the name of this materialization unit. Useful for debugging
  /// output.
  virtual StringRef getName() const = 0;

  /// Return the set of symbols that this source provides.
  const SymbolFlagsMap &getSymbols() const { return SymbolFlags; }

  /// Returns the initialization symbol for this MaterializationUnit (if any).
  const SymbolStringPtr &getInitializerSymbol() const { return InitSymbol; }

  /// Implementations of this method should materialize all symbols
  ///        in the materialzation unit, except for those that have been
  ///        previously discarded.
  virtual void
  materialize(std::unique_ptr<MaterializationResponsibility> R) = 0;

  /// Called by JITDylibs to notify MaterializationUnits that the given symbol
  /// has been overridden.
  void doDiscard(const JITDylib &JD, const SymbolStringPtr &Name) {
    SymbolFlags.erase(Name);
    if (InitSymbol == Name) {
      DEBUG_WITH_TYPE("orc", {
        dbgs() << "In " << getName() << ": discarding init symbol \""
               << *Name << "\"\n";
      });
      InitSymbol = nullptr;
    }
    discard(JD, std::move(Name));
  }

protected:
  SymbolFlagsMap SymbolFlags;
  SymbolStringPtr InitSymbol;

private:
  virtual void anchor();

  /// Implementations of this method should discard the given symbol
  ///        from the source (e.g. if the source is an LLVM IR Module and the
  ///        symbol is a function, delete the function body or mark it available
  ///        externally).
  virtual void discard(const JITDylib &JD, const SymbolStringPtr &Name) = 0;
};

/// A MaterializationUnit implementation for pre-existing absolute symbols.
///
/// All symbols will be resolved and marked ready as soon as the unit is
/// materialized.
class AbsoluteSymbolsMaterializationUnit : public MaterializationUnit {
public:
  AbsoluteSymbolsMaterializationUnit(SymbolMap Symbols);

  StringRef getName() const override;

private:
  void materialize(std::unique_ptr<MaterializationResponsibility> R) override;
  void discard(const JITDylib &JD, const SymbolStringPtr &Name) override;
  static MaterializationUnit::Interface extractFlags(const SymbolMap &Symbols);

  SymbolMap Symbols;
};

/// Create an AbsoluteSymbolsMaterializationUnit with the given symbols.
/// Useful for inserting absolute symbols into a JITDylib. E.g.:
/// \code{.cpp}
///   JITDylib &JD = ...;
///   SymbolStringPtr Foo = ...;
///   ExecutorSymbolDef FooSym = ...;
///   if (auto Err = JD.define(absoluteSymbols({{Foo, FooSym}})))
///     return Err;
/// \endcode
///
inline std::unique_ptr<AbsoluteSymbolsMaterializationUnit>
absoluteSymbols(SymbolMap Symbols) {
  return std::make_unique<AbsoluteSymbolsMaterializationUnit>(
      std::move(Symbols));
}

/// A materialization unit for symbol aliases. Allows existing symbols to be
/// aliased with alternate flags.
class ReExportsMaterializationUnit : public MaterializationUnit {
public:
  /// SourceJD is allowed to be nullptr, in which case the source JITDylib is
  /// taken to be whatever JITDylib these definitions are materialized in (and
  /// MatchNonExported has no effect). This is useful for defining aliases
  /// within a JITDylib.
  ///
  /// Note: Care must be taken that no sets of aliases form a cycle, as such
  ///       a cycle will result in a deadlock when any symbol in the cycle is
  ///       resolved.
  ReExportsMaterializationUnit(JITDylib *SourceJD,
                               JITDylibLookupFlags SourceJDLookupFlags,
                               SymbolAliasMap Aliases);

  StringRef getName() const override;

private:
  void materialize(std::unique_ptr<MaterializationResponsibility> R) override;
  void discard(const JITDylib &JD, const SymbolStringPtr &Name) override;
  static MaterializationUnit::Interface
  extractFlags(const SymbolAliasMap &Aliases);

  JITDylib *SourceJD = nullptr;
  JITDylibLookupFlags SourceJDLookupFlags;
  SymbolAliasMap Aliases;
};

/// Create a ReExportsMaterializationUnit with the given aliases.
/// Useful for defining symbol aliases.: E.g., given a JITDylib JD containing
/// symbols "foo" and "bar", we can define aliases "baz" (for "foo") and "qux"
/// (for "bar") with: \code{.cpp}
///   SymbolStringPtr Baz = ...;
///   SymbolStringPtr Qux = ...;
///   if (auto Err = JD.define(symbolAliases({
///       {Baz, { Foo, JITSymbolFlags::Exported }},
///       {Qux, { Bar, JITSymbolFlags::Weak }}}))
///     return Err;
/// \endcode
inline std::unique_ptr<ReExportsMaterializationUnit>
symbolAliases(SymbolAliasMap Aliases) {
  return std::make_unique<ReExportsMaterializationUnit>(
      nullptr, JITDylibLookupFlags::MatchAllSymbols, std::move(Aliases));
}

/// Create a materialization unit for re-exporting symbols from another JITDylib
/// with alternative names/flags.
/// SourceJD will be searched using the given JITDylibLookupFlags.
inline std::unique_ptr<ReExportsMaterializationUnit>
reexports(JITDylib &SourceJD, SymbolAliasMap Aliases,
          JITDylibLookupFlags SourceJDLookupFlags =
              JITDylibLookupFlags::MatchExportedSymbolsOnly) {
  return std::make_unique<ReExportsMaterializationUnit>(
      &SourceJD, SourceJDLookupFlags, std::move(Aliases));
}

/// Build a SymbolAliasMap for the common case where you want to re-export
/// symbols from another JITDylib with the same linkage/flags.
Expected<SymbolAliasMap>
buildSimpleReexportsAliasMap(JITDylib &SourceJD, const SymbolNameSet &Symbols);

/// Represents the state that a symbol has reached during materialization.
enum class SymbolState : uint8_t {
  Invalid,       /// No symbol should be in this state.
  NeverSearched, /// Added to the symbol table, never queried.
  Materializing, /// Queried, materialization begun.
  Resolved,      /// Assigned address, still materializing.
  Emitted,       /// Emitted to memory, but waiting on transitive dependencies.
  Ready = 0x3f   /// Ready and safe for clients to access.
};

/// A symbol query that returns results via a callback when results are
///        ready.
///
/// makes a callback when all symbols are available.
class AsynchronousSymbolQuery {
  friend class ExecutionSession;
  friend class InProgressFullLookupState;
  friend class JITDylib;
  friend class JITSymbolResolverAdapter;
  friend class MaterializationResponsibility;

public:
  /// Create a query for the given symbols. The NotifyComplete
  /// callback will be called once all queried symbols reach the given
  /// minimum state.
  AsynchronousSymbolQuery(const SymbolLookupSet &Symbols,
                          SymbolState RequiredState,
                          SymbolsResolvedCallback NotifyComplete);

  /// Notify the query that a requested symbol has reached the required state.
  void notifySymbolMetRequiredState(const SymbolStringPtr &Name,
                                    ExecutorSymbolDef Sym);

  /// Returns true if all symbols covered by this query have been
  ///        resolved.
  bool isComplete() const { return OutstandingSymbolsCount == 0; }


private:
  void handleComplete(ExecutionSession &ES);

  SymbolState getRequiredState() { return RequiredState; }

  void addQueryDependence(JITDylib &JD, SymbolStringPtr Name);

  void removeQueryDependence(JITDylib &JD, const SymbolStringPtr &Name);

  void dropSymbol(const SymbolStringPtr &Name);

  void handleFailed(Error Err);

  void detach();

  SymbolsResolvedCallback NotifyComplete;
  SymbolDependenceMap QueryRegistrations;
  SymbolMap ResolvedSymbols;
  size_t OutstandingSymbolsCount;
  SymbolState RequiredState;
};

/// Wraps state for a lookup-in-progress.
/// DefinitionGenerators can optionally take ownership of a LookupState object
/// to suspend a lookup-in-progress while they search for definitions.
class LookupState {
  friend class OrcV2CAPIHelper;
  friend class ExecutionSession;

public:
  LookupState();
  LookupState(LookupState &&);
  LookupState &operator=(LookupState &&);
  ~LookupState();

  /// Continue the lookup. This can be called by DefinitionGenerators
  /// to re-start a captured query-application operation.
  void continueLookup(Error Err);

private:
  LookupState(std::unique_ptr<InProgressLookupState> IPLS);

  // For C API.
  void reset(InProgressLookupState *IPLS);

  std::unique_ptr<InProgressLookupState> IPLS;
};

/// Definition generators can be attached to JITDylibs to generate new
/// definitions for otherwise unresolved symbols during lookup.
class DefinitionGenerator {
  friend class ExecutionSession;

public:
  virtual ~DefinitionGenerator();

  /// DefinitionGenerators should override this method to insert new
  /// definitions into the parent JITDylib. K specifies the kind of this
  /// lookup. JD specifies the target JITDylib being searched, and
  /// JDLookupFlags specifies whether the search should match against
  /// hidden symbols. Finally, Symbols describes the set of unresolved
  /// symbols and their associated lookup flags.
  virtual Error tryToGenerate(LookupState &LS, LookupKind K, JITDylib &JD,
                              JITDylibLookupFlags JDLookupFlags,
                              const SymbolLookupSet &LookupSet) = 0;

private:
  std::mutex M;
  bool InUse = false;
  std::deque<LookupState> PendingLookups;
};

/// Represents a JIT'd dynamic library.
///
/// This class aims to mimic the behavior of a regular dylib or shared object,
/// but without requiring the contained program representations to be compiled
/// up-front. The JITDylib's content is defined by adding MaterializationUnits,
/// and contained MaterializationUnits will typically rely on the JITDylib's
/// links-against order to resolve external references (similar to a regular
/// dylib).
///
/// The JITDylib object is a thin wrapper that references state held by the
/// ExecutionSession. JITDylibs can be removed, clearing this underlying state
/// and leaving the JITDylib object in a defunct state. In this state the
/// JITDylib's name is guaranteed to remain accessible. If the ExecutionSession
/// is still alive then other operations are callable but will return an Error
/// or null result (depending on the API). It is illegal to call any operation
/// other than getName on a JITDylib after the ExecutionSession has been torn
/// down.
///
/// JITDylibs cannot be moved or copied. Their address is stable, and useful as
/// a key in some JIT data structures.
class JITDylib : public ThreadSafeRefCountedBase<JITDylib>,
                 public jitlink::JITLinkDylib {
  friend class AsynchronousSymbolQuery;
  friend class ExecutionSession;
  friend class Platform;
  friend class MaterializationResponsibility;
public:

  JITDylib(const JITDylib &) = delete;
  JITDylib &operator=(const JITDylib &) = delete;
  JITDylib(JITDylib &&) = delete;
  JITDylib &operator=(JITDylib &&) = delete;
  ~JITDylib();

  /// Get a reference to the ExecutionSession for this JITDylib.
  ///
  /// It is legal to call this method on a defunct JITDylib, however the result
  /// will only usable if the ExecutionSession is still alive. If this JITDylib
  /// is held by an error that may have torn down the JIT then the result
  /// should not be used.
  ExecutionSession &getExecutionSession() const { return ES; }

  /// Dump current JITDylib state to OS.
  ///
  /// It is legal to call this method on a defunct JITDylib.
  void dump(raw_ostream &OS);

  /// Calls remove on all trackers currently associated with this JITDylib.
  /// Does not run static deinits.
  ///
  /// Note that removal happens outside the session lock, so new code may be
  /// added concurrently while the clear is underway, and the newly added
  /// code will *not* be cleared. Adding new code concurrently with a clear
  /// is usually a bug and should be avoided.
  ///
  /// It is illegal to call this method on a defunct JITDylib and the client
  /// is responsible for ensuring that they do not do so.
  Error clear();

  /// Get the default resource tracker for this JITDylib.
  ///
  /// It is illegal to call this method on a defunct JITDylib and the client
  /// is responsible for ensuring that they do not do so.
  ResourceTrackerSP getDefaultResourceTracker();

  /// Create a resource tracker for this JITDylib.
  ///
  /// It is illegal to call this method on a defunct JITDylib and the client
  /// is responsible for ensuring that they do not do so.
  ResourceTrackerSP createResourceTracker();

  /// Adds a definition generator to this JITDylib and returns a referenece to
  /// it.
  ///
  /// When JITDylibs are searched during lookup, if no existing definition of
  /// a symbol is found, then any generators that have been added are run (in
  /// the order that they were added) to potentially generate a definition.
  ///
  /// It is illegal to call this method on a defunct JITDylib and the client
  /// is responsible for ensuring that they do not do so.
  template <typename GeneratorT>
  GeneratorT &addGenerator(std::unique_ptr<GeneratorT> DefGenerator);

  /// Remove a definition generator from this JITDylib.
  ///
  /// The given generator must exist in this JITDylib's generators list (i.e.
  /// have been added and not yet removed).
  ///
  /// It is illegal to call this method on a defunct JITDylib and the client
  /// is responsible for ensuring that they do not do so.
  void removeGenerator(DefinitionGenerator &G);

  /// Set the link order to be used when fixing up definitions in JITDylib.
  /// This will replace the previous link order, and apply to any symbol
  /// resolutions made for definitions in this JITDylib after the call to
  /// setLinkOrder (even if the definition itself was added before the
  /// call).
  ///
  /// If LinkAgainstThisJITDylibFirst is true (the default) then this JITDylib
  /// will add itself to the beginning of the LinkOrder (Clients should not
  /// put this JITDylib in the list in this case, to avoid redundant lookups).
  ///
  /// If LinkAgainstThisJITDylibFirst is false then the link order will be used
  /// as-is. The primary motivation for this feature is to support deliberate
  /// shadowing of symbols in this JITDylib by a facade JITDylib. For example,
  /// the facade may resolve function names to stubs, and the stubs may compile
  /// lazily by looking up symbols in this dylib. Adding the facade dylib
  /// as the first in the link order (instead of this dylib) ensures that
  /// definitions within this dylib resolve to the lazy-compiling stubs,
  /// rather than immediately materializing the definitions in this dylib.
  ///
  /// It is illegal to call this method on a defunct JITDylib and the client
  /// is responsible for ensuring that they do not do so.
  void setLinkOrder(JITDylibSearchOrder NewSearchOrder,
                    bool LinkAgainstThisJITDylibFirst = true);

  /// Append the given JITDylibSearchOrder to the link order for this
  /// JITDylib (discarding any elements already present in this JITDylib's
  /// link order).
  void addToLinkOrder(const JITDylibSearchOrder &NewLinks);

  /// Add the given JITDylib to the link order for definitions in this
  /// JITDylib.
  ///
  /// It is illegal to call this method on a defunct JITDylib and the client
  /// is responsible for ensuring that they do not do so.
  void addToLinkOrder(JITDylib &JD,
                      JITDylibLookupFlags JDLookupFlags =
                          JITDylibLookupFlags::MatchExportedSymbolsOnly);

  /// Replace OldJD with NewJD in the link order if OldJD is present.
  /// Otherwise this operation is a no-op.
  ///
  /// It is illegal to call this method on a defunct JITDylib and the client
  /// is responsible for ensuring that they do not do so.
  void replaceInLinkOrder(JITDylib &OldJD, JITDylib &NewJD,
                          JITDylibLookupFlags JDLookupFlags =
                              JITDylibLookupFlags::MatchExportedSymbolsOnly);

  /// Remove the given JITDylib from the link order for this JITDylib if it is
  /// present. Otherwise this operation is a no-op.
  ///
  /// It is illegal to call this method on a defunct JITDylib and the client
  /// is responsible for ensuring that they do not do so.
  void removeFromLinkOrder(JITDylib &JD);

  /// Do something with the link order (run under the session lock).
  ///
  /// It is illegal to call this method on a defunct JITDylib and the client
  /// is responsible for ensuring that they do not do so.
  template <typename Func>
  auto withLinkOrderDo(Func &&F)
      -> decltype(F(std::declval<const JITDylibSearchOrder &>()));

  /// Define all symbols provided by the materialization unit to be part of this
  /// JITDylib.
  ///
  /// If RT is not specified then the default resource tracker will be used.
  ///
  /// This overload always takes ownership of the MaterializationUnit. If any
  /// errors occur, the MaterializationUnit consumed.
  ///
  /// It is illegal to call this method on a defunct JITDylib and the client
  /// is responsible for ensuring that they do not do so.
  template <typename MaterializationUnitType>
  Error define(std::unique_ptr<MaterializationUnitType> &&MU,
               ResourceTrackerSP RT = nullptr);

  /// Define all symbols provided by the materialization unit to be part of this
  /// JITDylib.
  ///
  /// This overload only takes ownership of the MaterializationUnit no error is
  /// generated. If an error occurs, ownership remains with the caller. This
  /// may allow the caller to modify the MaterializationUnit to correct the
  /// issue, then re-call define.
  ///
  /// It is illegal to call this method on a defunct JITDylib and the client
  /// is responsible for ensuring that they do not do so.
  template <typename MaterializationUnitType>
  Error define(std::unique_ptr<MaterializationUnitType> &MU,
               ResourceTrackerSP RT = nullptr);

  /// Tries to remove the given symbols.
  ///
  /// If any symbols are not defined in this JITDylib this method will return
  /// a SymbolsNotFound error covering the missing symbols.
  ///
  /// If all symbols are found but some symbols are in the process of being
  /// materialized this method will return a SymbolsCouldNotBeRemoved error.
  ///
  /// On success, all symbols are removed. On failure, the JITDylib state is
  /// left unmodified (no symbols are removed).
  ///
  /// It is illegal to call this method on a defunct JITDylib and the client
  /// is responsible for ensuring that they do not do so.
  Error remove(const SymbolNameSet &Names);

  /// Returns the given JITDylibs and all of their transitive dependencies in
  /// DFS order (based on linkage relationships). Each JITDylib will appear
  /// only once.
  ///
  /// If any JITDylib in the order is defunct then this method will return an
  /// error, otherwise returns the order.
  static Expected<std::vector<JITDylibSP>>
  getDFSLinkOrder(ArrayRef<JITDylibSP> JDs);

  /// Returns the given JITDylibs and all of their transitive dependencies in
  /// reverse DFS order (based on linkage relationships). Each JITDylib will
  /// appear only once.
  ///
  /// If any JITDylib in the order is defunct then this method will return an
  /// error, otherwise returns the order.
  static Expected<std::vector<JITDylibSP>>
  getReverseDFSLinkOrder(ArrayRef<JITDylibSP> JDs);

  /// Return this JITDylib and its transitive dependencies in DFS order
  /// based on linkage relationships.
  ///
  /// If any JITDylib in the order is defunct then this method will return an
  /// error, otherwise returns the order.
  Expected<std::vector<JITDylibSP>> getDFSLinkOrder();

  /// Rteurn this JITDylib and its transitive dependencies in reverse DFS order
  /// based on linkage relationships.
  ///
  /// If any JITDylib in the order is defunct then this method will return an
  /// error, otherwise returns the order.
  Expected<std::vector<JITDylibSP>> getReverseDFSLinkOrder();

private:
  using AsynchronousSymbolQuerySet =
    std::set<std::shared_ptr<AsynchronousSymbolQuery>>;

  using AsynchronousSymbolQueryList =
      std::vector<std::shared_ptr<AsynchronousSymbolQuery>>;

  struct UnmaterializedInfo {
    UnmaterializedInfo(std::unique_ptr<MaterializationUnit> MU,
                       ResourceTracker *RT)
        : MU(std::move(MU)), RT(RT) {}

    std::unique_ptr<MaterializationUnit> MU;
    ResourceTracker *RT;
  };

  using UnmaterializedInfosMap =
      DenseMap<SymbolStringPtr, std::shared_ptr<UnmaterializedInfo>>;

  using UnmaterializedInfosList =
      std::vector<std::shared_ptr<UnmaterializedInfo>>;

  struct EmissionDepUnit {
    EmissionDepUnit(JITDylib &JD) : JD(&JD) {}

    JITDylib *JD = nullptr;
    DenseMap<NonOwningSymbolStringPtr, JITSymbolFlags> Symbols;
    DenseMap<JITDylib *, DenseSet<NonOwningSymbolStringPtr>> Dependencies;
  };

  struct EmissionDepUnitInfo {
    std::shared_ptr<EmissionDepUnit> EDU;
    DenseSet<EmissionDepUnit *> IntraEmitUsers;
    DenseMap<JITDylib *, DenseSet<NonOwningSymbolStringPtr>> NewDeps;
  };

  // Information about not-yet-ready symbol.
  // * DefiningEDU will point to the EmissionDepUnit that defines the symbol.
  // * DependantEDUs will hold pointers to any EmissionDepUnits currently
  //   waiting on this symbol.
  // * Pending queries holds any not-yet-completed queries that include this
  //   symbol.
  struct MaterializingInfo {
    friend class ExecutionSession;

    std::shared_ptr<EmissionDepUnit> DefiningEDU;
    DenseSet<EmissionDepUnit *> DependantEDUs;

    void addQuery(std::shared_ptr<AsynchronousSymbolQuery> Q);
    void removeQuery(const AsynchronousSymbolQuery &Q);
    AsynchronousSymbolQueryList takeQueriesMeeting(SymbolState RequiredState);
    AsynchronousSymbolQueryList takeAllPendingQueries() {
      return std::move(PendingQueries);
    }
    bool hasQueriesPending() const { return !PendingQueries.empty(); }
    const AsynchronousSymbolQueryList &pendingQueries() const {
      return PendingQueries;
    }
  private:
    AsynchronousSymbolQueryList PendingQueries;
  };

  using MaterializingInfosMap = DenseMap<SymbolStringPtr, MaterializingInfo>;

  class SymbolTableEntry {
  public:
    SymbolTableEntry() = default;
    SymbolTableEntry(JITSymbolFlags Flags)
        : Flags(Flags), State(static_cast<uint8_t>(SymbolState::NeverSearched)),
          MaterializerAttached(false) {}

    ExecutorAddr getAddress() const { return Addr; }
    JITSymbolFlags getFlags() const { return Flags; }
    SymbolState getState() const { return static_cast<SymbolState>(State); }

    bool hasMaterializerAttached() const { return MaterializerAttached; }

    void setAddress(ExecutorAddr Addr) { this->Addr = Addr; }
    void setFlags(JITSymbolFlags Flags) { this->Flags = Flags; }
    void setState(SymbolState State) {
      assert(static_cast<uint8_t>(State) < (1 << 6) &&
             "State does not fit in bitfield");
      this->State = static_cast<uint8_t>(State);
    }

    void setMaterializerAttached(bool MaterializerAttached) {
      this->MaterializerAttached = MaterializerAttached;
    }

    ExecutorSymbolDef getSymbol() const { return {Addr, Flags}; }

  private:
    ExecutorAddr Addr;
    JITSymbolFlags Flags;
    uint8_t State : 7;
    uint8_t MaterializerAttached : 1;
  };

  using SymbolTable = DenseMap<SymbolStringPtr, SymbolTableEntry>;

  JITDylib(ExecutionSession &ES, std::string Name);

  std::pair<AsynchronousSymbolQuerySet, std::shared_ptr<SymbolDependenceMap>>
  IL_removeTracker(ResourceTracker &RT);

  void transferTracker(ResourceTracker &DstRT, ResourceTracker &SrcRT);

  Error defineImpl(MaterializationUnit &MU);

  void installMaterializationUnit(std::unique_ptr<MaterializationUnit> MU,
                                  ResourceTracker &RT);

  void detachQueryHelper(AsynchronousSymbolQuery &Q,
                         const SymbolNameSet &QuerySymbols);

  void transferEmittedNodeDependencies(MaterializingInfo &DependantMI,
                                       const SymbolStringPtr &DependantName,
                                       MaterializingInfo &EmittedMI);

  Expected<SymbolFlagsMap>
  defineMaterializing(MaterializationResponsibility &FromMR,
                      SymbolFlagsMap SymbolFlags);

  Error replace(MaterializationResponsibility &FromMR,
                std::unique_ptr<MaterializationUnit> MU);

  Expected<std::unique_ptr<MaterializationResponsibility>>
  delegate(MaterializationResponsibility &FromMR, SymbolFlagsMap SymbolFlags,
           SymbolStringPtr InitSymbol);

  SymbolNameSet getRequestedSymbols(const SymbolFlagsMap &SymbolFlags) const;

  void addDependencies(const SymbolStringPtr &Name,
                       const SymbolDependenceMap &Dependants);

  Error resolve(MaterializationResponsibility &MR, const SymbolMap &Resolved);

  void unlinkMaterializationResponsibility(MaterializationResponsibility &MR);

  /// Attempt to reduce memory usage from empty \c UnmaterializedInfos and
  /// \c MaterializingInfos tables.
  void shrinkMaterializationInfoMemory();

  ExecutionSession &ES;
  enum { Open, Closing, Closed } State = Open;
  std::mutex GeneratorsMutex;
  SymbolTable Symbols;
  UnmaterializedInfosMap UnmaterializedInfos;
  MaterializingInfosMap MaterializingInfos;
  std::vector<std::shared_ptr<DefinitionGenerator>> DefGenerators;
  JITDylibSearchOrder LinkOrder;
  ResourceTrackerSP DefaultTracker;

  // Map trackers to sets of symbols tracked.
  DenseMap<ResourceTracker *, SymbolNameVector> TrackerSymbols;
  DenseMap<ResourceTracker *, DenseSet<MaterializationResponsibility *>>
      TrackerMRs;
};

/// Platforms set up standard symbols and mediate interactions between dynamic
/// initializers (e.g. C++ static constructors) and ExecutionSession state.
/// Note that Platforms do not automatically run initializers: clients are still
/// responsible for doing this.
class Platform {
public:
  virtual ~Platform();

  /// This method will be called outside the session lock each time a JITDylib
  /// is created (unless it is created with EmptyJITDylib set) to allow the
  /// Platform to install any JITDylib specific standard symbols (e.g
  /// __dso_handle).
  virtual Error setupJITDylib(JITDylib &JD) = 0;

  /// This method will be called outside the session lock each time a JITDylib
  /// is removed to allow the Platform to remove any JITDylib-specific data.
  virtual Error teardownJITDylib(JITDylib &JD) = 0;

  /// This method will be called under the ExecutionSession lock each time a
  /// MaterializationUnit is added to a JITDylib.
  virtual Error notifyAdding(ResourceTracker &RT,
                             const MaterializationUnit &MU) = 0;

  /// This method will be called under the ExecutionSession lock when a
  /// ResourceTracker is removed.
  virtual Error notifyRemoving(ResourceTracker &RT) = 0;

  /// A utility function for looking up initializer symbols. Performs a blocking
  /// lookup for the given symbols in each of the given JITDylibs.
  ///
  /// Note: This function is deprecated and will be removed in the near future.
  static Expected<DenseMap<JITDylib *, SymbolMap>>
  lookupInitSymbols(ExecutionSession &ES,
                    const DenseMap<JITDylib *, SymbolLookupSet> &InitSyms);

  /// Performs an async lookup for the given symbols in each of the given
  /// JITDylibs, calling the given handler once all lookups have completed.
  static void
  lookupInitSymbolsAsync(unique_function<void(Error)> OnComplete,
                         ExecutionSession &ES,
                         const DenseMap<JITDylib *, SymbolLookupSet> &InitSyms);
};

/// A materialization task.
class MaterializationTask : public RTTIExtends<MaterializationTask, Task> {
public:
  static char ID;

  MaterializationTask(std::unique_ptr<MaterializationUnit> MU,
                      std::unique_ptr<MaterializationResponsibility> MR)
      : MU(std::move(MU)), MR(std::move(MR)) {}
  void printDescription(raw_ostream &OS) override;
  void run() override;

private:
  std::unique_ptr<MaterializationUnit> MU;
  std::unique_ptr<MaterializationResponsibility> MR;
};

/// Lookups are usually run on the current thread, but in some cases they may
/// be run as tasks, e.g. if the lookup has been continued from a suspended
/// state.
class LookupTask : public RTTIExtends<LookupTask, Task> {
public:
  static char ID;

  LookupTask(LookupState LS) : LS(std::move(LS)) {}
  void printDescription(raw_ostream &OS) override;
  void run() override;

private:
  LookupState LS;
};

/// An ExecutionSession represents a running JIT program.
class ExecutionSession {
  friend class InProgressLookupFlagsState;
  friend class InProgressFullLookupState;
  friend class JITDylib;
  friend class LookupState;
  friend class MaterializationResponsibility;
  friend class ResourceTracker;

public:
  /// For reporting errors.
  using ErrorReporter = unique_function<void(Error)>;

  /// Send a result to the remote.
  using SendResultFunction = unique_function<void(shared::WrapperFunctionResult)>;

  /// An asynchronous wrapper-function callable from the executor via
  /// jit-dispatch.
  using JITDispatchHandlerFunction = unique_function<void(
      SendResultFunction SendResult,
      const char *ArgData, size_t ArgSize)>;

  /// A map associating tag names with asynchronous wrapper function
  /// implementations in the JIT.
  using JITDispatchHandlerAssociationMap =
      DenseMap<SymbolStringPtr, JITDispatchHandlerFunction>;

  /// Construct an ExecutionSession with the given ExecutorProcessControl
  /// object.
  ExecutionSession(std::unique_ptr<ExecutorProcessControl> EPC);

  /// Destroy an ExecutionSession. Verifies that endSession was called prior to
  /// destruction.
  ~ExecutionSession();

  /// End the session. Closes all JITDylibs and disconnects from the
  /// executor. Clients must call this method before destroying the session.
  Error endSession();

  /// Get the ExecutorProcessControl object associated with this
  /// ExecutionSession.
  ExecutorProcessControl &getExecutorProcessControl() { return *EPC; }

  /// Return the triple for the executor.
  const Triple &getTargetTriple() const { return EPC->getTargetTriple(); }

  // Return the page size for the executor.
  size_t getPageSize() const { return EPC->getPageSize(); }

  /// Get the SymbolStringPool for this instance.
  std::shared_ptr<SymbolStringPool> getSymbolStringPool() {
    return EPC->getSymbolStringPool();
  }

  /// Add a symbol name to the SymbolStringPool and return a pointer to it.
  SymbolStringPtr intern(StringRef SymName) { return EPC->intern(SymName); }

  /// Set the Platform for this ExecutionSession.
  void setPlatform(std::unique_ptr<Platform> P) { this->P = std::move(P); }

  /// Get the Platform for this session.
  /// Will return null if no Platform has been set for this ExecutionSession.
  Platform *getPlatform() { return P.get(); }

  /// Run the given lambda with the session mutex locked.
  template <typename Func> decltype(auto) runSessionLocked(Func &&F) {
    std::lock_guard<std::recursive_mutex> Lock(SessionMutex);
    return F();
  }

  /// Register the given ResourceManager with this ExecutionSession.
  /// Managers will be notified of events in reverse order of registration.
  void registerResourceManager(ResourceManager &RM);

  /// Deregister the given ResourceManager with this ExecutionSession.
  /// Manager must have been previously registered.
  void deregisterResourceManager(ResourceManager &RM);

  /// Return a pointer to the "name" JITDylib.
  /// Ownership of JITDylib remains within Execution Session
  JITDylib *getJITDylibByName(StringRef Name);

  /// Add a new bare JITDylib to this ExecutionSession.
  ///
  /// The JITDylib Name is required to be unique. Clients should verify that
  /// names are not being re-used (E.g. by calling getJITDylibByName) if names
  /// are based on user input.
  ///
  /// This call does not install any library code or symbols into the newly
  /// created JITDylib. The client is responsible for all configuration.
  JITDylib &createBareJITDylib(std::string Name);

  /// Add a new JITDylib to this ExecutionSession.
  ///
  /// The JITDylib Name is required to be unique. Clients should verify that
  /// names are not being re-used (e.g. by calling getJITDylibByName) if names
  /// are based on user input.
  ///
  /// If a Platform is attached then Platform::setupJITDylib will be called to
  /// install standard platform symbols (e.g. standard library interposes).
  /// If no Platform is attached this call is equivalent to createBareJITDylib.
  Expected<JITDylib &> createJITDylib(std::string Name);

  /// Removes the given JITDylibs from the ExecutionSession.
  ///
  /// This method clears all resources held for the JITDylibs, puts them in the
  /// closed state, and clears all references to them that are held by the
  /// ExecutionSession or other JITDylibs. No further code can be added to the
  /// removed JITDylibs, and the JITDylib objects will be freed once any
  /// remaining JITDylibSPs pointing to them are destroyed.
  ///
  /// This method does *not* run static destructors for code contained in the
  /// JITDylibs, and each JITDylib can only be removed once.
  ///
  /// JITDylibs will be removed in the order given. Teardown is usually
  /// independent for each JITDylib, but not always. In particular, where the
  /// ORC runtime is used it is expected that teardown off all JITDylibs will
  /// depend on it, so the JITDylib containing the ORC runtime must be removed
  /// last. If the client has introduced any other dependencies they should be
  /// accounted for in the removal order too.
  Error removeJITDylibs(std::vector<JITDylibSP> JDsToRemove);

  /// Calls removeJTIDylibs on the gives JITDylib.
  Error removeJITDylib(JITDylib &JD) {
    return removeJITDylibs(std::vector<JITDylibSP>({&JD}));
  }

  /// Set the error reporter function.
  ExecutionSession &setErrorReporter(ErrorReporter ReportError) {
    this->ReportError = std::move(ReportError);
    return *this;
  }

  /// Report a error for this execution session.
  ///
  /// Unhandled errors can be sent here to log them.
  void reportError(Error Err) { ReportError(std::move(Err)); }

  /// Search the given JITDylibs to find the flags associated with each of the
  /// given symbols.
  void lookupFlags(LookupKind K, JITDylibSearchOrder SearchOrder,
                   SymbolLookupSet Symbols,
                   unique_function<void(Expected<SymbolFlagsMap>)> OnComplete);

  /// Blocking version of lookupFlags.
  Expected<SymbolFlagsMap> lookupFlags(LookupKind K,
                                       JITDylibSearchOrder SearchOrder,
                                       SymbolLookupSet Symbols);

  /// Search the given JITDylibs for the given symbols.
  ///
  /// SearchOrder lists the JITDylibs to search. For each dylib, the associated
  /// boolean indicates whether the search should match against non-exported
  /// (hidden visibility) symbols in that dylib (true means match against
  /// non-exported symbols, false means do not match).
  ///
  /// The NotifyComplete callback will be called once all requested symbols
  /// reach the required state.
  ///
  /// If all symbols are found, the RegisterDependencies function will be called
  /// while the session lock is held. This gives clients a chance to register
  /// dependencies for on the queried symbols for any symbols they are
  /// materializing (if a MaterializationResponsibility instance is present,
  /// this can be implemented by calling
  /// MaterializationResponsibility::addDependencies). If there are no
  /// dependenant symbols for this query (e.g. it is being made by a top level
  /// client to get an address to call) then the value NoDependenciesToRegister
  /// can be used.
  void lookup(LookupKind K, const JITDylibSearchOrder &SearchOrder,
              SymbolLookupSet Symbols, SymbolState RequiredState,
              SymbolsResolvedCallback NotifyComplete,
              RegisterDependenciesFunction RegisterDependencies);

  /// Blocking version of lookup above. Returns the resolved symbol map.
  /// If WaitUntilReady is true (the default), will not return until all
  /// requested symbols are ready (or an error occurs). If WaitUntilReady is
  /// false, will return as soon as all requested symbols are resolved,
  /// or an error occurs. If WaitUntilReady is false and an error occurs
  /// after resolution, the function will return a success value, but the
  /// error will be reported via reportErrors.
  Expected<SymbolMap> lookup(const JITDylibSearchOrder &SearchOrder,
                             SymbolLookupSet Symbols,
                             LookupKind K = LookupKind::Static,
                             SymbolState RequiredState = SymbolState::Ready,
                             RegisterDependenciesFunction RegisterDependencies =
                                 NoDependenciesToRegister);

  /// Convenience version of blocking lookup.
  /// Searches each of the JITDylibs in the search order in turn for the given
  /// symbol.
  Expected<ExecutorSymbolDef>
  lookup(const JITDylibSearchOrder &SearchOrder, SymbolStringPtr Symbol,
         SymbolState RequiredState = SymbolState::Ready);

  /// Convenience version of blocking lookup.
  /// Searches each of the JITDylibs in the search order in turn for the given
  /// symbol. The search will not find non-exported symbols.
  Expected<ExecutorSymbolDef>
  lookup(ArrayRef<JITDylib *> SearchOrder, SymbolStringPtr Symbol,
         SymbolState RequiredState = SymbolState::Ready);

  /// Convenience version of blocking lookup.
  /// Searches each of the JITDylibs in the search order in turn for the given
  /// symbol. The search will not find non-exported symbols.
  Expected<ExecutorSymbolDef>
  lookup(ArrayRef<JITDylib *> SearchOrder, StringRef Symbol,
         SymbolState RequiredState = SymbolState::Ready);

  /// Materialize the given unit.
  void dispatchTask(std::unique_ptr<Task> T) {
    assert(T && "T must be non-null");
    DEBUG_WITH_TYPE("orc", dumpDispatchInfo(*T));
    EPC->getDispatcher().dispatch(std::move(T));
  }

  /// Run a wrapper function in the executor.
  ///
  /// The wrapper function should be callable as:
  ///
  /// \code{.cpp}
  ///   CWrapperFunctionResult fn(uint8_t *Data, uint64_t Size);
  /// \endcode{.cpp}
  ///
  /// The given OnComplete function will be called to return the result.
  template <typename... ArgTs>
  void callWrapperAsync(ArgTs &&... Args) {
    EPC->callWrapperAsync(std::forward<ArgTs>(Args)...);
  }

  /// Run a wrapper function in the executor. The wrapper function should be
  /// callable as:
  ///
  /// \code{.cpp}
  ///   CWrapperFunctionResult fn(uint8_t *Data, uint64_t Size);
  /// \endcode{.cpp}
  shared::WrapperFunctionResult callWrapper(ExecutorAddr WrapperFnAddr,
                                            ArrayRef<char> ArgBuffer) {
    return EPC->callWrapper(WrapperFnAddr, ArgBuffer);
  }

  /// Run a wrapper function using SPS to serialize the arguments and
  /// deserialize the results.
  template <typename SPSSignature, typename SendResultT, typename... ArgTs>
  void callSPSWrapperAsync(ExecutorAddr WrapperFnAddr, SendResultT &&SendResult,
                           const ArgTs &...Args) {
    EPC->callSPSWrapperAsync<SPSSignature, SendResultT, ArgTs...>(
        WrapperFnAddr, std::forward<SendResultT>(SendResult), Args...);
  }

  /// Run a wrapper function using SPS to serialize the arguments and
  /// deserialize the results.
  ///
  /// If SPSSignature is a non-void function signature then the second argument
  /// (the first in the Args list) should be a reference to a return value.
  template <typename SPSSignature, typename... WrapperCallArgTs>
  Error callSPSWrapper(ExecutorAddr WrapperFnAddr,
                       WrapperCallArgTs &&...WrapperCallArgs) {
    return EPC->callSPSWrapper<SPSSignature, WrapperCallArgTs...>(
        WrapperFnAddr, std::forward<WrapperCallArgTs>(WrapperCallArgs)...);
  }

  /// Wrap a handler that takes concrete argument types (and a sender for a
  /// concrete return type) to produce an AsyncHandlerWrapperFunction. Uses SPS
  /// to unpack the arguments and pack the result.
  ///
  /// This function is intended to support easy construction of
  /// AsyncHandlerWrapperFunctions that can be associated with a tag
  /// (using registerJITDispatchHandler) and called from the executor.
  template <typename SPSSignature, typename HandlerT>
  static JITDispatchHandlerFunction wrapAsyncWithSPS(HandlerT &&H) {
    return [H = std::forward<HandlerT>(H)](
               SendResultFunction SendResult,
               const char *ArgData, size_t ArgSize) mutable {
      shared::WrapperFunction<SPSSignature>::handleAsync(ArgData, ArgSize, H,
                                                         std::move(SendResult));
    };
  }

  /// Wrap a class method that takes concrete argument types (and a sender for
  /// a concrete return type) to produce an AsyncHandlerWrapperFunction. Uses
  /// SPS to unpack the arguments and pack the result.
  ///
  /// This function is intended to support easy construction of
  /// AsyncHandlerWrapperFunctions that can be associated with a tag
  /// (using registerJITDispatchHandler) and called from the executor.
  template <typename SPSSignature, typename ClassT, typename... MethodArgTs>
  static JITDispatchHandlerFunction
  wrapAsyncWithSPS(ClassT *Instance, void (ClassT::*Method)(MethodArgTs...)) {
    return wrapAsyncWithSPS<SPSSignature>(
        [Instance, Method](MethodArgTs &&...MethodArgs) {
          (Instance->*Method)(std::forward<MethodArgTs>(MethodArgs)...);
        });
  }

  /// For each tag symbol name, associate the corresponding
  /// AsyncHandlerWrapperFunction with the address of that symbol. The
  /// handler becomes callable from the executor using the ORC runtime
  /// __orc_rt_jit_dispatch function and the given tag.
  ///
  /// Tag symbols will be looked up in JD using LookupKind::Static,
  /// JITDylibLookupFlags::MatchAllSymbols (hidden tags will be found), and
  /// LookupFlags::WeaklyReferencedSymbol. Missing tag definitions will not
  /// cause an error, the handler will simply be dropped.
  Error registerJITDispatchHandlers(JITDylib &JD,
                                    JITDispatchHandlerAssociationMap WFs);

  /// Run a registered jit-side wrapper function.
  /// This should be called by the ExecutorProcessControl instance in response
  /// to incoming jit-dispatch requests from the executor.
  void runJITDispatchHandler(SendResultFunction SendResult,
                             ExecutorAddr HandlerFnTagAddr,
                             ArrayRef<char> ArgBuffer);

  /// Dump the state of all the JITDylibs in this session.
  void dump(raw_ostream &OS);

  /// Check the internal consistency of ExecutionSession data structures.
#ifdef EXPENSIVE_CHECKS
  bool verifySessionState(Twine Phase);
#endif

private:
  static void logErrorsToStdErr(Error Err) {
    logAllUnhandledErrors(std::move(Err), errs(), "JIT session error: ");
  }

  void dispatchOutstandingMUs();

  static std::unique_ptr<MaterializationResponsibility>
  createMaterializationResponsibility(ResourceTracker &RT,
                                      SymbolFlagsMap Symbols,
                                      SymbolStringPtr InitSymbol) {
    auto &JD = RT.getJITDylib();
    std::unique_ptr<MaterializationResponsibility> MR(
        new MaterializationResponsibility(&RT, std::move(Symbols),
                                          std::move(InitSymbol)));
    JD.TrackerMRs[&RT].insert(MR.get());
    return MR;
  }

  Error removeResourceTracker(ResourceTracker &RT);
  void transferResourceTracker(ResourceTracker &DstRT, ResourceTracker &SrcRT);
  void destroyResourceTracker(ResourceTracker &RT);

  // State machine functions for query application..

  /// IL_updateCandidatesFor is called to remove already-defined symbols that
  /// match a given query from the set of candidate symbols to generate
  /// definitions for (no need to generate a definition if one already exists).
  Error IL_updateCandidatesFor(JITDylib &JD, JITDylibLookupFlags JDLookupFlags,
                               SymbolLookupSet &Candidates,
                               SymbolLookupSet *NonCandidates);

  /// Handle resumption of a lookup after entering a generator.
  void OL_resumeLookupAfterGeneration(InProgressLookupState &IPLS);

  /// OL_applyQueryPhase1 is an optionally re-startable loop for triggering
  /// definition generation. It is called when a lookup is performed, and again
  /// each time that LookupState::continueLookup is called.
  void OL_applyQueryPhase1(std::unique_ptr<InProgressLookupState> IPLS,
                           Error Err);

  /// OL_completeLookup is run once phase 1 successfully completes for a lookup
  /// call. It attempts to attach the symbol to all symbol table entries and
  /// collect all MaterializationUnits to dispatch. If this method fails then
  /// all MaterializationUnits will be left un-materialized.
  void OL_completeLookup(std::unique_ptr<InProgressLookupState> IPLS,
                         std::shared_ptr<AsynchronousSymbolQuery> Q,
                         RegisterDependenciesFunction RegisterDependencies);

  /// OL_completeLookupFlags is run once phase 1 successfully completes for a
  /// lookupFlags call.
  void OL_completeLookupFlags(
      std::unique_ptr<InProgressLookupState> IPLS,
      unique_function<void(Expected<SymbolFlagsMap>)> OnComplete);

  // State machine functions for MaterializationResponsibility.
  void OL_destroyMaterializationResponsibility(
      MaterializationResponsibility &MR);
  SymbolNameSet OL_getRequestedSymbols(const MaterializationResponsibility &MR);
  Error OL_notifyResolved(MaterializationResponsibility &MR,
                          const SymbolMap &Symbols);

  using EDUInfosMap =
      DenseMap<JITDylib::EmissionDepUnit *, JITDylib::EmissionDepUnitInfo>;

  template <typename HandleNewDepFn>
  void propagateExtraEmitDeps(std::deque<JITDylib::EmissionDepUnit *> Worklist,
                              EDUInfosMap &EDUInfos,
                              HandleNewDepFn HandleNewDep);
  EDUInfosMap simplifyDepGroups(MaterializationResponsibility &MR,
                                ArrayRef<SymbolDependenceGroup> EmittedDeps);
  void IL_makeEDUReady(std::shared_ptr<JITDylib::EmissionDepUnit> EDU,
                       JITDylib::AsynchronousSymbolQuerySet &Queries);
  void IL_makeEDUEmitted(std::shared_ptr<JITDylib::EmissionDepUnit> EDU,
                         JITDylib::AsynchronousSymbolQuerySet &Queries);
  bool IL_removeEDUDependence(JITDylib::EmissionDepUnit &EDU, JITDylib &DepJD,
                              NonOwningSymbolStringPtr DepSym,
                              EDUInfosMap &EDUInfos);

  static Error makeJDClosedError(JITDylib::EmissionDepUnit &EDU,
                                 JITDylib &ClosedJD);
  static Error makeUnsatisfiedDepsError(JITDylib::EmissionDepUnit &EDU,
                                        JITDylib &BadJD, SymbolNameSet BadDeps);

  Expected<JITDylib::AsynchronousSymbolQuerySet>
  IL_emit(MaterializationResponsibility &MR, EDUInfosMap EDUInfos);
  Error OL_notifyEmitted(MaterializationResponsibility &MR,
                         ArrayRef<SymbolDependenceGroup> EmittedDeps);

  Error OL_defineMaterializing(MaterializationResponsibility &MR,
                               SymbolFlagsMap SymbolFlags);

  std::pair<JITDylib::AsynchronousSymbolQuerySet,
            std::shared_ptr<SymbolDependenceMap>>
  IL_failSymbols(JITDylib &JD, const SymbolNameVector &SymbolsToFail);
  void OL_notifyFailed(MaterializationResponsibility &MR);
  Error OL_replace(MaterializationResponsibility &MR,
                   std::unique_ptr<MaterializationUnit> MU);
  Expected<std::unique_ptr<MaterializationResponsibility>>
  OL_delegate(MaterializationResponsibility &MR, const SymbolNameSet &Symbols);

#ifndef NDEBUG
  void dumpDispatchInfo(Task &T);
#endif // NDEBUG

  mutable std::recursive_mutex SessionMutex;
  bool SessionOpen = true;
  std::unique_ptr<ExecutorProcessControl> EPC;
  std::unique_ptr<Platform> P;
  ErrorReporter ReportError = logErrorsToStdErr;

  std::vector<ResourceManager *> ResourceManagers;

  std::vector<JITDylibSP> JDs;

  // FIXME: Remove this (and runOutstandingMUs) once the linking layer works
  //        with callbacks from asynchronous queries.
  mutable std::recursive_mutex OutstandingMUsMutex;
  std::vector<std::pair<std::unique_ptr<MaterializationUnit>,
                        std::unique_ptr<MaterializationResponsibility>>>
      OutstandingMUs;

  mutable std::mutex JITDispatchHandlersMutex;
  DenseMap<ExecutorAddr, std::shared_ptr<JITDispatchHandlerFunction>>
      JITDispatchHandlers;
};

template <typename Func> Error ResourceTracker::withResourceKeyDo(Func &&F) {
  return getJITDylib().getExecutionSession().runSessionLocked([&]() -> Error {
    if (isDefunct())
      return make_error<ResourceTrackerDefunct>(this);
    F(getKeyUnsafe());
    return Error::success();
  });
}

inline ExecutionSession &
MaterializationResponsibility::getExecutionSession() const {
  return JD.getExecutionSession();
}

template <typename GeneratorT>
GeneratorT &JITDylib::addGenerator(std::unique_ptr<GeneratorT> DefGenerator) {
  auto &G = *DefGenerator;
  ES.runSessionLocked([&] {
    assert(State == Open && "Cannot add generator to closed JITDylib");
    DefGenerators.push_back(std::move(DefGenerator));
  });
  return G;
}

template <typename Func>
auto JITDylib::withLinkOrderDo(Func &&F)
    -> decltype(F(std::declval<const JITDylibSearchOrder &>())) {
  assert(State == Open && "Cannot use link order of closed JITDylib");
  return ES.runSessionLocked([&]() { return F(LinkOrder); });
}

template <typename MaterializationUnitType>
Error JITDylib::define(std::unique_ptr<MaterializationUnitType> &&MU,
                       ResourceTrackerSP RT) {
  assert(MU && "Can not define with a null MU");

  if (MU->getSymbols().empty()) {
    // Empty MUs are allowable but pathological, so issue a warning.
    DEBUG_WITH_TYPE("orc", {
      dbgs() << "Warning: Discarding empty MU " << MU->getName() << " for "
             << getName() << "\n";
    });
    return Error::success();
  } else
    DEBUG_WITH_TYPE("orc", {
      dbgs() << "Defining MU " << MU->getName() << " for " << getName()
             << " (tracker: ";
      if (RT == getDefaultResourceTracker())
        dbgs() << "default)";
      else if (RT)
        dbgs() << RT.get() << ")\n";
      else
        dbgs() << "0x0, default will be used)\n";
    });

  return ES.runSessionLocked([&, this]() -> Error {
    assert(State == Open && "JD is defunct");

    if (auto Err = defineImpl(*MU))
      return Err;

    if (!RT)
      RT = getDefaultResourceTracker();

    if (auto *P = ES.getPlatform()) {
      if (auto Err = P->notifyAdding(*RT, *MU))
        return Err;
    }

    installMaterializationUnit(std::move(MU), *RT);
    return Error::success();
  });
}

template <typename MaterializationUnitType>
Error JITDylib::define(std::unique_ptr<MaterializationUnitType> &MU,
                       ResourceTrackerSP RT) {
  assert(MU && "Can not define with a null MU");

  if (MU->getSymbols().empty()) {
    // Empty MUs are allowable but pathological, so issue a warning.
    DEBUG_WITH_TYPE("orc", {
      dbgs() << "Warning: Discarding empty MU " << MU->getName() << getName()
             << "\n";
    });
    return Error::success();
  } else
    DEBUG_WITH_TYPE("orc", {
      dbgs() << "Defining MU " << MU->getName() << " for " << getName()
             << " (tracker: ";
      if (RT == getDefaultResourceTracker())
        dbgs() << "default)";
      else if (RT)
        dbgs() << RT.get() << ")\n";
      else
        dbgs() << "0x0, default will be used)\n";
    });

  return ES.runSessionLocked([&, this]() -> Error {
    assert(State == Open && "JD is defunct");

    if (auto Err = defineImpl(*MU))
      return Err;

    if (!RT)
      RT = getDefaultResourceTracker();

    if (auto *P = ES.getPlatform()) {
      if (auto Err = P->notifyAdding(*RT, *MU))
        return Err;
    }

    installMaterializationUnit(std::move(MU), *RT);
    return Error::success();
  });
}

/// ReexportsGenerator can be used with JITDylib::addGenerator to automatically
/// re-export a subset of the source JITDylib's symbols in the target.
class ReexportsGenerator : public DefinitionGenerator {
public:
  using SymbolPredicate = std::function<bool(SymbolStringPtr)>;

  /// Create a reexports generator. If an Allow predicate is passed, only
  /// symbols for which the predicate returns true will be reexported. If no
  /// Allow predicate is passed, all symbols will be exported.
  ReexportsGenerator(JITDylib &SourceJD,
                     JITDylibLookupFlags SourceJDLookupFlags,
                     SymbolPredicate Allow = SymbolPredicate());

  Error tryToGenerate(LookupState &LS, LookupKind K, JITDylib &JD,
                      JITDylibLookupFlags JDLookupFlags,
                      const SymbolLookupSet &LookupSet) override;

private:
  JITDylib &SourceJD;
  JITDylibLookupFlags SourceJDLookupFlags;
  SymbolPredicate Allow;
};

// --------------- IMPLEMENTATION --------------
// Implementations for inline functions/methods.
// ---------------------------------------------

inline MaterializationResponsibility::~MaterializationResponsibility() {
  getExecutionSession().OL_destroyMaterializationResponsibility(*this);
}

inline SymbolNameSet MaterializationResponsibility::getRequestedSymbols() const {
  return getExecutionSession().OL_getRequestedSymbols(*this);
}

inline Error MaterializationResponsibility::notifyResolved(
    const SymbolMap &Symbols) {
  return getExecutionSession().OL_notifyResolved(*this, Symbols);
}

inline Error MaterializationResponsibility::notifyEmitted(
    ArrayRef<SymbolDependenceGroup> EmittedDeps) {
  return getExecutionSession().OL_notifyEmitted(*this, EmittedDeps);
}

inline Error MaterializationResponsibility::defineMaterializing(
    SymbolFlagsMap SymbolFlags) {
  return getExecutionSession().OL_defineMaterializing(*this,
                                                      std::move(SymbolFlags));
}

inline void MaterializationResponsibility::failMaterialization() {
  getExecutionSession().OL_notifyFailed(*this);
}

inline Error MaterializationResponsibility::replace(
    std::unique_ptr<MaterializationUnit> MU) {
  return getExecutionSession().OL_replace(*this, std::move(MU));
}

inline Expected<std::unique_ptr<MaterializationResponsibility>>
MaterializationResponsibility::delegate(const SymbolNameSet &Symbols) {
  return getExecutionSession().OL_delegate(*this, Symbols);
}

} // End namespace orc
} // End namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_CORE_H
