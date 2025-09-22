//==- TLSVariableHoist.h ------ Remove Redundant TLS Loads -------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass identifies/eliminates Redundant TLS Loads if related option is set.
// For example:
// static __thread int x;
// int g();
// int f(int c) {
//   int *px = &x;
//   while (c--)
//     *px += g();
//   return *px;
// }
//
// will generate Redundant TLS Loads by compiling it with
// clang++ -fPIC -ftls-model=global-dynamic -O2 -S
//
// .LBB0_2:                                # %while.body
//                                         # =>This Inner Loop Header: Depth=1
//         callq   _Z1gv@PLT
//         movl    %eax, %ebp
//         leaq    _ZL1x@TLSLD(%rip), %rdi
//         callq   __tls_get_addr@PLT
//         addl    _ZL1x@DTPOFF(%rax), %ebp
//         movl    %ebp, _ZL1x@DTPOFF(%rax)
//         addl    $-1, %ebx
//         jne     .LBB0_2
//         jmp     .LBB0_3
// .LBB0_4:                                # %entry.while.end_crit_edge
//         leaq    _ZL1x@TLSLD(%rip), %rdi
//         callq   __tls_get_addr@PLT
//         movl    _ZL1x@DTPOFF(%rax), %ebp
//
// The Redundant TLS Loads will hurt the performance, especially in loops.
// So we try to eliminate/move them if required by customers, let it be:
//
// # %bb.0:                                # %entry
//         ...
//         movl    %edi, %ebx
//         leaq    _ZL1x@TLSLD(%rip), %rdi
//         callq   __tls_get_addr@PLT
//         leaq    _ZL1x@DTPOFF(%rax), %r14
//         testl   %ebx, %ebx
//         je      .LBB0_1
// .LBB0_2:                                # %while.body
//                                         # =>This Inner Loop Header: Depth=1
//         callq   _Z1gv@PLT
//         addl    (%r14), %eax
//         movl    %eax, (%r14)
//         addl    $-1, %ebx
//         jne     .LBB0_2
//         jmp     .LBB0_3
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_TLSVARIABLEHOIST_H
#define LLVM_TRANSFORMS_SCALAR_TLSVARIABLEHOIST_H

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

class BasicBlock;
class DominatorTree;
class Function;
class GlobalVariable;
class Instruction;

/// A private "module" namespace for types and utilities used by
/// TLSVariableHoist. These are implementation details and should
/// not be used by clients.
namespace tlshoist {

/// Keeps track of the user of a TLS variable and the operand index
/// where the variable is used.
struct TLSUser {
  Instruction *Inst;
  unsigned OpndIdx;

  TLSUser(Instruction *Inst, unsigned Idx) : Inst(Inst), OpndIdx(Idx) {}
};

/// Keeps track of a TLS variable candidate and its users.
struct TLSCandidate {
  SmallVector<TLSUser, 8> Users;

  /// Add the user to the use list and update the cost.
  void addUser(Instruction *Inst, unsigned Idx) {
    Users.push_back(TLSUser(Inst, Idx));
  }
};

} // end namespace tlshoist

class TLSVariableHoistPass : public PassInfoMixin<TLSVariableHoistPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  // Glue for old PM.
  bool runImpl(Function &F, DominatorTree &DT, LoopInfo &LI);

private:
  DominatorTree *DT;
  LoopInfo *LI;

  /// Keeps track of TLS variable candidates found in the function.
  using TLSCandMapType = MapVector<GlobalVariable *, tlshoist::TLSCandidate>;
  TLSCandMapType TLSCandMap;

  void collectTLSCandidates(Function &Fn);
  void collectTLSCandidate(Instruction *Inst);
  Instruction *getNearestLoopDomInst(BasicBlock *BB, Loop *L);
  Instruction *getDomInst(Instruction *I1, Instruction *I2);
  BasicBlock::iterator findInsertPos(Function &Fn, GlobalVariable *GV,
                                     BasicBlock *&PosBB);
  Instruction *genBitCastInst(Function &Fn, GlobalVariable *GV);
  bool tryReplaceTLSCandidates(Function &Fn);
  bool tryReplaceTLSCandidate(Function &Fn, GlobalVariable *GV);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_TLSVARIABLEHOIST_H
