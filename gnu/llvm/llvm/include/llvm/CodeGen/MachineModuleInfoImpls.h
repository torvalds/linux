//===- llvm/CodeGen/MachineModuleInfoImpls.h --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines object-file format specific implementations of
// MachineModuleInfoImpl.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEMODULEINFOIMPLS_H
#define LLVM_CODEGEN_MACHINEMODULEINFOIMPLS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include <cassert>

namespace llvm {

class MCSymbol;

/// MachineModuleInfoMachO - This is a MachineModuleInfoImpl implementation
/// for MachO targets.
class MachineModuleInfoMachO : public MachineModuleInfoImpl {
  /// GVStubs - Darwin '$non_lazy_ptr' stubs.  The key is something like
  /// "Lfoo$non_lazy_ptr", the value is something like "_foo". The extra bit
  /// is true if this GV is external.
  DenseMap<MCSymbol *, StubValueTy> GVStubs;

  /// ThreadLocalGVStubs - Darwin '$non_lazy_ptr' stubs.  The key is something
  /// like "Lfoo$non_lazy_ptr", the value is something like "_foo". The extra
  /// bit is true if this GV is external.
  DenseMap<MCSymbol *, StubValueTy> ThreadLocalGVStubs;

  /// Darwin '$auth_ptr' stubs.  The key is the stub symbol, like
  /// "Lfoo$auth_ptr$ib$12".  The value is the MCExpr representing that
  /// signed pointer, something like "_foo@AUTH(ib, 12)".
  DenseMap<MCSymbol *, const MCExpr *> AuthPtrStubs;

  virtual void anchor(); // Out of line virtual method.

public:
  MachineModuleInfoMachO(const MachineModuleInfo &) {}

  StubValueTy &getGVStubEntry(MCSymbol *Sym) {
    assert(Sym && "Key cannot be null");
    return GVStubs[Sym];
  }

  StubValueTy &getThreadLocalGVStubEntry(MCSymbol *Sym) {
    assert(Sym && "Key cannot be null");
    return ThreadLocalGVStubs[Sym];
  }

  const MCExpr *&getAuthPtrStubEntry(MCSymbol *Sym) {
    assert(Sym && "Key cannot be null");
    return AuthPtrStubs[Sym];
  }

  /// Accessor methods to return the set of stubs in sorted order.
  SymbolListTy GetGVStubList() { return getSortedStubs(GVStubs); }
  SymbolListTy GetThreadLocalGVStubList() {
    return getSortedStubs(ThreadLocalGVStubs);
  }

  ExprStubListTy getAuthGVStubList() {
    return getSortedExprStubs(AuthPtrStubs);
  }
};

/// MachineModuleInfoELF - This is a MachineModuleInfoImpl implementation
/// for ELF targets.
class MachineModuleInfoELF : public MachineModuleInfoImpl {
  /// GVStubs - These stubs are used to materialize global addresses in PIC
  /// mode.
  DenseMap<MCSymbol *, StubValueTy> GVStubs;

  /// AuthPtrStubs - These stubs are used to materialize signed addresses for
  /// extern_weak symbols.
  DenseMap<MCSymbol *, const MCExpr *> AuthPtrStubs;

  virtual void anchor(); // Out of line virtual method.

public:
  MachineModuleInfoELF(const MachineModuleInfo &) {}

  StubValueTy &getGVStubEntry(MCSymbol *Sym) {
    assert(Sym && "Key cannot be null");
    return GVStubs[Sym];
  }

  const MCExpr *&getAuthPtrStubEntry(MCSymbol *Sym) {
    assert(Sym && "Key cannot be null");
    return AuthPtrStubs[Sym];
  }

  /// Accessor methods to return the set of stubs in sorted order.

  SymbolListTy GetGVStubList() { return getSortedStubs(GVStubs); }

  ExprStubListTy getAuthGVStubList() {
    return getSortedExprStubs(AuthPtrStubs);
  }
};

/// MachineModuleInfoCOFF - This is a MachineModuleInfoImpl implementation
/// for COFF targets.
class MachineModuleInfoCOFF : public MachineModuleInfoImpl {
  /// GVStubs - These stubs are used to materialize global addresses in PIC
  /// mode.
  DenseMap<MCSymbol *, StubValueTy> GVStubs;

  virtual void anchor(); // Out of line virtual method.

public:
  MachineModuleInfoCOFF(const MachineModuleInfo &) {}

  StubValueTy &getGVStubEntry(MCSymbol *Sym) {
    assert(Sym && "Key cannot be null");
    return GVStubs[Sym];
  }

  /// Accessor methods to return the set of stubs in sorted order.

  SymbolListTy GetGVStubList() { return getSortedStubs(GVStubs); }
};

/// MachineModuleInfoWasm - This is a MachineModuleInfoImpl implementation
/// for Wasm targets.
class MachineModuleInfoWasm : public MachineModuleInfoImpl {
  virtual void anchor(); // Out of line virtual method.

public:
  MachineModuleInfoWasm(const MachineModuleInfo &) {}

  SetVector<StringRef> MachineSymbolsUsed;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINEMODULEINFOIMPLS_H
