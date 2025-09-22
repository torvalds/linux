//===- JITSymbol.h - JIT symbol abstraction ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Abstraction for target process addresses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_JITSYMBOL_H
#define LLVM_EXECUTIONENGINE_JITSYMBOL_H

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>

#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/FunctionExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

namespace llvm {

class GlobalValue;
class GlobalValueSummary;

namespace object {

class SymbolRef;

} // end namespace object

/// Represents an address in the target process's address space.
using JITTargetAddress = uint64_t;

/// Convert a JITTargetAddress to a pointer.
///
/// Note: This is a raw cast of the address bit pattern to the given pointer
/// type. When casting to a function pointer in order to execute JIT'd code
/// jitTargetAddressToFunction should be preferred, as it will also perform
/// pointer signing on targets that require it.
template <typename T> T jitTargetAddressToPointer(JITTargetAddress Addr) {
  static_assert(std::is_pointer<T>::value, "T must be a pointer type");
  uintptr_t IntPtr = static_cast<uintptr_t>(Addr);
  assert(IntPtr == Addr && "JITTargetAddress value out of range for uintptr_t");
  return reinterpret_cast<T>(IntPtr);
}

/// Convert a JITTargetAddress to a callable function pointer.
///
/// Casts the given address to a callable function pointer. This operation
/// will perform pointer signing for platforms that require it (e.g. arm64e).
template <typename T> T jitTargetAddressToFunction(JITTargetAddress Addr) {
  static_assert(std::is_pointer<T>::value &&
                    std::is_function<std::remove_pointer_t<T>>::value,
                "T must be a function pointer type");
  return jitTargetAddressToPointer<T>(Addr);
}

/// Convert a pointer to a JITTargetAddress.
template <typename T> JITTargetAddress pointerToJITTargetAddress(T *Ptr) {
  return static_cast<JITTargetAddress>(reinterpret_cast<uintptr_t>(Ptr));
}

/// Flags for symbols in the JIT.
class JITSymbolFlags {
public:
  using UnderlyingType = uint8_t;
  using TargetFlagsType = uint8_t;

  enum FlagNames : UnderlyingType {
    None = 0,
    HasError = 1U << 0,
    Weak = 1U << 1,
    Common = 1U << 2,
    Absolute = 1U << 3,
    Exported = 1U << 4,
    Callable = 1U << 5,
    MaterializationSideEffectsOnly = 1U << 6,
    LLVM_MARK_AS_BITMASK_ENUM( // LargestValue =
        MaterializationSideEffectsOnly)
  };

  /// Default-construct a JITSymbolFlags instance.
  JITSymbolFlags() = default;

  /// Construct a JITSymbolFlags instance from the given flags.
  JITSymbolFlags(FlagNames Flags) : Flags(Flags) {}

  /// Construct a JITSymbolFlags instance from the given flags and target
  ///        flags.
  JITSymbolFlags(FlagNames Flags, TargetFlagsType TargetFlags)
      : TargetFlags(TargetFlags), Flags(Flags) {}

  /// Implicitly convert to bool. Returns true if any flag is set.
  explicit operator bool() const { return Flags != None || TargetFlags != 0; }

  /// Compare for equality.
  bool operator==(const JITSymbolFlags &RHS) const {
    return Flags == RHS.Flags && TargetFlags == RHS.TargetFlags;
  }

  /// Bitwise AND-assignment for FlagNames.
  JITSymbolFlags &operator&=(const FlagNames &RHS) {
    Flags &= RHS;
    return *this;
  }

  /// Bitwise OR-assignment for FlagNames.
  JITSymbolFlags &operator|=(const FlagNames &RHS) {
    Flags |= RHS;
    return *this;
  }

  /// Return true if there was an error retrieving this symbol.
  bool hasError() const {
    return (Flags & HasError) == HasError;
  }

  /// Returns true if the Weak flag is set.
  bool isWeak() const {
    return (Flags & Weak) == Weak;
  }

  /// Returns true if the Common flag is set.
  bool isCommon() const {
    return (Flags & Common) == Common;
  }

  /// Returns true if the symbol isn't weak or common.
  bool isStrong() const {
    return !isWeak() && !isCommon();
  }

  /// Returns true if the Exported flag is set.
  bool isExported() const {
    return (Flags & Exported) == Exported;
  }

  /// Returns true if the given symbol is known to be callable.
  bool isCallable() const { return (Flags & Callable) == Callable; }

  /// Returns true if this symbol is a materialization-side-effects-only
  /// symbol. Such symbols do not have a real address. They exist to trigger
  /// and support synchronization of materialization side effects, e.g. for
  /// collecting initialization information. These symbols will vanish from
  /// the symbol table immediately upon reaching the ready state, and will
  /// appear to queries as if they were never defined (except that query
  /// callback execution will be delayed until they reach the ready state).
  /// MaterializationSideEffectOnly symbols should only be queried using the
  /// SymbolLookupFlags::WeaklyReferencedSymbol flag (see
  /// llvm/include/llvm/ExecutionEngine/Orc/Core.h).
  bool hasMaterializationSideEffectsOnly() const {
    return (Flags & MaterializationSideEffectsOnly) ==
           MaterializationSideEffectsOnly;
  }

  /// Get the underlying flags value as an integer.
  UnderlyingType getRawFlagsValue() const {
    return static_cast<UnderlyingType>(Flags);
  }

  /// Return a reference to the target-specific flags.
  TargetFlagsType& getTargetFlags() { return TargetFlags; }

  /// Return a reference to the target-specific flags.
  const TargetFlagsType& getTargetFlags() const { return TargetFlags; }

  /// Construct a JITSymbolFlags value based on the flags of the given global
  /// value.
  static JITSymbolFlags fromGlobalValue(const GlobalValue &GV);

  /// Construct a JITSymbolFlags value based on the flags of the given global
  /// value summary.
  static JITSymbolFlags fromSummary(GlobalValueSummary *S);

  /// Construct a JITSymbolFlags value based on the flags of the given libobject
  /// symbol.
  static Expected<JITSymbolFlags>
  fromObjectSymbol(const object::SymbolRef &Symbol);

private:
  TargetFlagsType TargetFlags = 0;
  FlagNames Flags = None;
};

inline JITSymbolFlags operator&(const JITSymbolFlags &LHS,
                                const JITSymbolFlags::FlagNames &RHS) {
  JITSymbolFlags Tmp = LHS;
  Tmp &= RHS;
  return Tmp;
}

inline JITSymbolFlags operator|(const JITSymbolFlags &LHS,
                                const JITSymbolFlags::FlagNames &RHS) {
  JITSymbolFlags Tmp = LHS;
  Tmp |= RHS;
  return Tmp;
}

/// ARM-specific JIT symbol flags.
/// FIXME: This should be moved into a target-specific header.
class ARMJITSymbolFlags {
public:
  ARMJITSymbolFlags() = default;

  enum FlagNames {
    None = 0,
    Thumb = 1 << 0
  };

  operator JITSymbolFlags::TargetFlagsType&() { return Flags; }

  static ARMJITSymbolFlags fromObjectSymbol(const object::SymbolRef &Symbol);

private:
  JITSymbolFlags::TargetFlagsType Flags = 0;
};

/// Represents a symbol that has been evaluated to an address already.
class JITEvaluatedSymbol {
public:
  JITEvaluatedSymbol() = default;

  /// Create a 'null' symbol.
  JITEvaluatedSymbol(std::nullptr_t) {}

  /// Create a symbol for the given address and flags.
  JITEvaluatedSymbol(JITTargetAddress Address, JITSymbolFlags Flags)
      : Address(Address), Flags(Flags) {}

  /// Create a symbol from the given pointer with the given flags.
  template <typename T>
  static JITEvaluatedSymbol
  fromPointer(T *P, JITSymbolFlags Flags = JITSymbolFlags::Exported) {
    return JITEvaluatedSymbol(pointerToJITTargetAddress(P), Flags);
  }

  /// An evaluated symbol converts to 'true' if its address is non-zero.
  explicit operator bool() const { return Address != 0; }

  /// Return the address of this symbol.
  JITTargetAddress getAddress() const { return Address; }

  /// Return the flags for this symbol.
  JITSymbolFlags getFlags() const { return Flags; }

  /// Set the flags for this symbol.
  void setFlags(JITSymbolFlags Flags) { this->Flags = std::move(Flags); }

private:
  JITTargetAddress Address = 0;
  JITSymbolFlags Flags;
};

/// Represents a symbol in the JIT.
class JITSymbol {
public:
  using GetAddressFtor = unique_function<Expected<JITTargetAddress>()>;

  /// Create a 'null' symbol, used to represent a "symbol not found"
  ///        result from a successful (non-erroneous) lookup.
  JITSymbol(std::nullptr_t)
      : CachedAddr(0) {}

  /// Create a JITSymbol representing an error in the symbol lookup
  ///        process (e.g. a network failure during a remote lookup).
  JITSymbol(Error Err)
    : Err(std::move(Err)), Flags(JITSymbolFlags::HasError) {}

  /// Create a symbol for a definition with a known address.
  JITSymbol(JITTargetAddress Addr, JITSymbolFlags Flags)
      : CachedAddr(Addr), Flags(Flags) {}

  /// Construct a JITSymbol from a JITEvaluatedSymbol.
  JITSymbol(JITEvaluatedSymbol Sym)
      : CachedAddr(Sym.getAddress()), Flags(Sym.getFlags()) {}

  /// Create a symbol for a definition that doesn't have a known address
  ///        yet.
  /// @param GetAddress A functor to materialize a definition (fixing the
  ///        address) on demand.
  ///
  ///   This constructor allows a JIT layer to provide a reference to a symbol
  /// definition without actually materializing the definition up front. The
  /// user can materialize the definition at any time by calling the getAddress
  /// method.
  JITSymbol(GetAddressFtor GetAddress, JITSymbolFlags Flags)
      : GetAddress(std::move(GetAddress)), CachedAddr(0), Flags(Flags) {}

  JITSymbol(const JITSymbol&) = delete;
  JITSymbol& operator=(const JITSymbol&) = delete;

  JITSymbol(JITSymbol &&Other)
    : GetAddress(std::move(Other.GetAddress)), Flags(std::move(Other.Flags)) {
    if (Flags.hasError())
      Err = std::move(Other.Err);
    else
      CachedAddr = std::move(Other.CachedAddr);
  }

  JITSymbol& operator=(JITSymbol &&Other) {
    GetAddress = std::move(Other.GetAddress);
    Flags = std::move(Other.Flags);
    if (Flags.hasError())
      Err = std::move(Other.Err);
    else
      CachedAddr = std::move(Other.CachedAddr);
    return *this;
  }

  ~JITSymbol() {
    if (Flags.hasError())
      Err.~Error();
    else
      CachedAddr.~JITTargetAddress();
  }

  /// Returns true if the symbol exists, false otherwise.
  explicit operator bool() const {
    return !Flags.hasError() && (CachedAddr || GetAddress);
  }

  /// Move the error field value out of this JITSymbol.
  Error takeError() {
    if (Flags.hasError())
      return std::move(Err);
    return Error::success();
  }

  /// Get the address of the symbol in the target address space. Returns
  ///        '0' if the symbol does not exist.
  Expected<JITTargetAddress> getAddress() {
    assert(!Flags.hasError() && "getAddress called on error value");
    if (GetAddress) {
      if (auto CachedAddrOrErr = GetAddress()) {
        GetAddress = nullptr;
        CachedAddr = *CachedAddrOrErr;
        assert(CachedAddr && "Symbol could not be materialized.");
      } else
        return CachedAddrOrErr.takeError();
    }
    return CachedAddr;
  }

  JITSymbolFlags getFlags() const { return Flags; }

private:
  GetAddressFtor GetAddress;
  union {
    JITTargetAddress CachedAddr;
    Error Err;
  };
  JITSymbolFlags Flags;
};

/// Symbol resolution interface.
///
/// Allows symbol flags and addresses to be looked up by name.
/// Symbol queries are done in bulk (i.e. you request resolution of a set of
/// symbols, rather than a single one) to reduce IPC overhead in the case of
/// remote JITing, and expose opportunities for parallel compilation.
class JITSymbolResolver {
public:
  using LookupSet = std::set<StringRef>;
  using LookupResult = std::map<StringRef, JITEvaluatedSymbol>;
  using OnResolvedFunction = unique_function<void(Expected<LookupResult>)>;

  virtual ~JITSymbolResolver() = default;

  /// Returns the fully resolved address and flags for each of the given
  ///        symbols.
  ///
  /// This method will return an error if any of the given symbols can not be
  /// resolved, or if the resolution process itself triggers an error.
  virtual void lookup(const LookupSet &Symbols,
                      OnResolvedFunction OnResolved) = 0;

  /// Returns the subset of the given symbols that should be materialized by
  /// the caller. Only weak/common symbols should be looked up, as strong
  /// definitions are implicitly always part of the caller's responsibility.
  virtual Expected<LookupSet>
  getResponsibilitySet(const LookupSet &Symbols) = 0;

  /// Specify if this resolver can return valid symbols with zero value.
  virtual bool allowsZeroSymbols() { return false; }

private:
  virtual void anchor();
};

/// Legacy symbol resolution interface.
class LegacyJITSymbolResolver : public JITSymbolResolver {
public:
  /// Performs lookup by, for each symbol, first calling
  ///        findSymbolInLogicalDylib and if that fails calling
  ///        findSymbol.
  void lookup(const LookupSet &Symbols, OnResolvedFunction OnResolved) final;

  /// Performs flags lookup by calling findSymbolInLogicalDylib and
  ///        returning the flags value for that symbol.
  Expected<LookupSet> getResponsibilitySet(const LookupSet &Symbols) final;

  /// This method returns the address of the specified symbol if it exists
  /// within the logical dynamic library represented by this JITSymbolResolver.
  /// Unlike findSymbol, queries through this interface should return addresses
  /// for hidden symbols.
  ///
  /// This is of particular importance for the Orc JIT APIs, which support lazy
  /// compilation by breaking up modules: Each of those broken out modules
  /// must be able to resolve hidden symbols provided by the others. Clients
  /// writing memory managers for MCJIT can usually ignore this method.
  ///
  /// This method will be queried by RuntimeDyld when checking for previous
  /// definitions of common symbols.
  virtual JITSymbol findSymbolInLogicalDylib(const std::string &Name) = 0;

  /// This method returns the address of the specified function or variable.
  /// It is used to resolve symbols during module linking.
  ///
  /// If the returned symbol's address is equal to ~0ULL then RuntimeDyld will
  /// skip all relocations for that symbol, and the client will be responsible
  /// for handling them manually.
  virtual JITSymbol findSymbol(const std::string &Name) = 0;

private:
  void anchor() override;
};

} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_JITSYMBOL_H
