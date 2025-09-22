//===--- llvm/CodeGen/WasmEHFuncInfo.h --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Data structures for Wasm exception handling schemes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_WASMEHFUNCINFO_H
#define LLVM_CODEGEN_WASMEHFUNCINFO_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallPtrSet.h"

namespace llvm {

class BasicBlock;
class Function;
class MachineBasicBlock;

namespace WebAssembly {
enum Tag { CPP_EXCEPTION = 0, C_LONGJMP = 1 };
}

using BBOrMBB = PointerUnion<const BasicBlock *, MachineBasicBlock *>;

struct WasmEHFuncInfo {
  // When there is an entry <A, B>, if an exception is not caught by A, it
  // should next unwind to the EH pad B.
  DenseMap<BBOrMBB, BBOrMBB> SrcToUnwindDest;
  DenseMap<BBOrMBB, SmallPtrSet<BBOrMBB, 4>> UnwindDestToSrcs; // reverse map

  // Helper functions
  const BasicBlock *getUnwindDest(const BasicBlock *BB) const {
    assert(hasUnwindDest(BB));
    return cast<const BasicBlock *>(SrcToUnwindDest.lookup(BB));
  }
  SmallPtrSet<const BasicBlock *, 4> getUnwindSrcs(const BasicBlock *BB) const {
    assert(hasUnwindSrcs(BB));
    const auto &Set = UnwindDestToSrcs.lookup(BB);
    SmallPtrSet<const BasicBlock *, 4> Ret;
    for (const auto P : Set)
      Ret.insert(cast<const BasicBlock *>(P));
    return Ret;
  }
  void setUnwindDest(const BasicBlock *BB, const BasicBlock *Dest) {
    SrcToUnwindDest[BB] = Dest;
    UnwindDestToSrcs[Dest].insert(BB);
  }
  bool hasUnwindDest(const BasicBlock *BB) const {
    return SrcToUnwindDest.count(BB);
  }
  bool hasUnwindSrcs(const BasicBlock *BB) const {
    return UnwindDestToSrcs.count(BB);
  }

  MachineBasicBlock *getUnwindDest(MachineBasicBlock *MBB) const {
    assert(hasUnwindDest(MBB));
    return cast<MachineBasicBlock *>(SrcToUnwindDest.lookup(MBB));
  }
  SmallPtrSet<MachineBasicBlock *, 4>
  getUnwindSrcs(MachineBasicBlock *MBB) const {
    assert(hasUnwindSrcs(MBB));
    const auto &Set = UnwindDestToSrcs.lookup(MBB);
    SmallPtrSet<MachineBasicBlock *, 4> Ret;
    for (const auto P : Set)
      Ret.insert(cast<MachineBasicBlock *>(P));
    return Ret;
  }
  void setUnwindDest(MachineBasicBlock *MBB, MachineBasicBlock *Dest) {
    SrcToUnwindDest[MBB] = Dest;
    UnwindDestToSrcs[Dest].insert(MBB);
  }
  bool hasUnwindDest(MachineBasicBlock *MBB) const {
    return SrcToUnwindDest.count(MBB);
  }
  bool hasUnwindSrcs(MachineBasicBlock *MBB) const {
    return UnwindDestToSrcs.count(MBB);
  }
};

// Analyze the IR in the given function to build WasmEHFuncInfo.
void calculateWasmEHInfo(const Function *F, WasmEHFuncInfo &EHInfo);

} // namespace llvm

#endif // LLVM_CODEGEN_WASMEHFUNCINFO_H
