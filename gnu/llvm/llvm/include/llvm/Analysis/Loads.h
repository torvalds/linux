//===- Loads.h - Local load analysis --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares simple local analyses for load instructions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_LOADS_H
#define LLVM_ANALYSIS_LOADS_H

#include "llvm/IR/BasicBlock.h"
#include "llvm/Support/CommandLine.h"

namespace llvm {

class BatchAAResults;
class AssumptionCache;
class DataLayout;
class DominatorTree;
class Instruction;
class LoadInst;
class Loop;
class MemoryLocation;
class ScalarEvolution;
class TargetLibraryInfo;

/// Return true if this is always a dereferenceable pointer. If the context
/// instruction is specified perform context-sensitive analysis and return true
/// if the pointer is dereferenceable at the specified instruction.
bool isDereferenceablePointer(const Value *V, Type *Ty, const DataLayout &DL,
                              const Instruction *CtxI = nullptr,
                              AssumptionCache *AC = nullptr,
                              const DominatorTree *DT = nullptr,
                              const TargetLibraryInfo *TLI = nullptr);

/// Returns true if V is always a dereferenceable pointer with alignment
/// greater or equal than requested. If the context instruction is specified
/// performs context-sensitive analysis and returns true if the pointer is
/// dereferenceable at the specified instruction.
bool isDereferenceableAndAlignedPointer(const Value *V, Type *Ty,
                                        Align Alignment, const DataLayout &DL,
                                        const Instruction *CtxI = nullptr,
                                        AssumptionCache *AC = nullptr,
                                        const DominatorTree *DT = nullptr,
                                        const TargetLibraryInfo *TLI = nullptr);

/// Returns true if V is always dereferenceable for Size byte with alignment
/// greater or equal than requested. If the context instruction is specified
/// performs context-sensitive analysis and returns true if the pointer is
/// dereferenceable at the specified instruction.
bool isDereferenceableAndAlignedPointer(const Value *V, Align Alignment,
                                        const APInt &Size, const DataLayout &DL,
                                        const Instruction *CtxI = nullptr,
                                        AssumptionCache *AC = nullptr,
                                        const DominatorTree *DT = nullptr,
                                        const TargetLibraryInfo *TLI = nullptr);

/// Return true if we know that executing a load from this value cannot trap.
///
/// If DT and ScanFrom are specified this method performs context-sensitive
/// analysis and returns true if it is safe to load immediately before ScanFrom.
///
/// If it is not obviously safe to load from the specified pointer, we do a
/// quick local scan of the basic block containing ScanFrom, to determine if
/// the address is already accessed.
bool isSafeToLoadUnconditionally(Value *V, Align Alignment, const APInt &Size,
                                 const DataLayout &DL,
                                 Instruction *ScanFrom = nullptr,
                                 AssumptionCache *AC = nullptr,
                                 const DominatorTree *DT = nullptr,
                                 const TargetLibraryInfo *TLI = nullptr);

/// Return true if we can prove that the given load (which is assumed to be
/// within the specified loop) would access only dereferenceable memory, and
/// be properly aligned on every iteration of the specified loop regardless of
/// its placement within the loop. (i.e. does not require predication beyond
/// that required by the header itself and could be hoisted into the header
/// if desired.)  This is more powerful than the variants above when the
/// address loaded from is analyzeable by SCEV.
bool isDereferenceableAndAlignedInLoop(LoadInst *LI, Loop *L,
                                       ScalarEvolution &SE, DominatorTree &DT,
                                       AssumptionCache *AC = nullptr);

/// Return true if the loop \p L cannot fault on any iteration and only
/// contains read-only memory accesses.
bool isDereferenceableReadOnlyLoop(Loop *L, ScalarEvolution *SE,
                                   DominatorTree *DT, AssumptionCache *AC);

/// Return true if we know that executing a load from this value cannot trap.
///
/// If DT and ScanFrom are specified this method performs context-sensitive
/// analysis and returns true if it is safe to load immediately before ScanFrom.
///
/// If it is not obviously safe to load from the specified pointer, we do a
/// quick local scan of the basic block containing ScanFrom, to determine if
/// the address is already accessed.
bool isSafeToLoadUnconditionally(Value *V, Type *Ty, Align Alignment,
                                 const DataLayout &DL,
                                 Instruction *ScanFrom = nullptr,
                                 AssumptionCache *AC = nullptr,
                                 const DominatorTree *DT = nullptr,
                                 const TargetLibraryInfo *TLI = nullptr);

/// The default number of maximum instructions to scan in the block, used by
/// FindAvailableLoadedValue().
extern cl::opt<unsigned> DefMaxInstsToScan;

/// Scan backwards to see if we have the value of the given load available
/// locally within a small number of instructions.
///
/// You can use this function to scan across multiple blocks: after you call
/// this function, if ScanFrom points at the beginning of the block, it's safe
/// to continue scanning the predecessors.
///
/// Note that performing load CSE requires special care to make sure the
/// metadata is set appropriately.  In particular, aliasing metadata needs
/// to be merged.  (This doesn't matter for store-to-load forwarding because
/// the only relevant load gets deleted.)
///
/// \param Load The load we want to replace.
/// \param ScanBB The basic block to scan.
/// \param [in,out] ScanFrom The location to start scanning from. When this
/// function returns, it points at the last instruction scanned.
/// \param MaxInstsToScan The maximum number of instructions to scan. If this
/// is zero, the whole block will be scanned.
/// \param AA Optional pointer to alias analysis, to make the scan more
/// precise.
/// \param [out] IsLoadCSE Whether the returned value is a load from the same
/// location in memory, as opposed to the value operand of a store.
///
/// \returns The found value, or nullptr if no value is found.
Value *FindAvailableLoadedValue(LoadInst *Load, BasicBlock *ScanBB,
                                BasicBlock::iterator &ScanFrom,
                                unsigned MaxInstsToScan = DefMaxInstsToScan,
                                BatchAAResults *AA = nullptr,
                                bool *IsLoadCSE = nullptr,
                                unsigned *NumScanedInst = nullptr);

/// This overload provides a more efficient implementation of
/// FindAvailableLoadedValue() for the case where we are not interested in
/// finding the closest clobbering instruction if no available load is found.
/// This overload cannot be used to scan across multiple blocks.
Value *FindAvailableLoadedValue(LoadInst *Load, BatchAAResults &AA,
                                bool *IsLoadCSE,
                                unsigned MaxInstsToScan = DefMaxInstsToScan);

/// Scan backwards to see if we have the value of the given pointer available
/// locally within a small number of instructions.
///
/// You can use this function to scan across multiple blocks: after you call
/// this function, if ScanFrom points at the beginning of the block, it's safe
/// to continue scanning the predecessors.
///
/// \param Loc The location we want the load and store to originate from.
/// \param AccessTy The access type of the pointer.
/// \param AtLeastAtomic Are we looking for at-least an atomic load/store ? In
/// case it is false, we can return an atomic or non-atomic load or store. In
/// case it is true, we need to return an atomic load or store.
/// \param ScanBB The basic block to scan.
/// \param [in,out] ScanFrom The location to start scanning from. When this
/// function returns, it points at the last instruction scanned.
/// \param MaxInstsToScan The maximum number of instructions to scan. If this
/// is zero, the whole block will be scanned.
/// \param AA Optional pointer to alias analysis, to make the scan more
/// precise.
/// \param [out] IsLoadCSE Whether the returned value is a load from the same
/// location in memory, as opposed to the value operand of a store.
///
/// \returns The found value, or nullptr if no value is found.
Value *findAvailablePtrLoadStore(const MemoryLocation &Loc, Type *AccessTy,
                                 bool AtLeastAtomic, BasicBlock *ScanBB,
                                 BasicBlock::iterator &ScanFrom,
                                 unsigned MaxInstsToScan, BatchAAResults *AA,
                                 bool *IsLoadCSE, unsigned *NumScanedInst);

/// Returns true if a pointer value \p From can be replaced with another pointer
/// value \To if they are deemed equal through some means (e.g. information from
/// conditions).
/// NOTE: The current implementation allows replacement in Icmp and PtrToInt
/// instructions, as well as when we are replacing with a null pointer.
/// Additionally it also allows replacement of pointers when both pointers have
/// the same underlying object.
bool canReplacePointersIfEqual(const Value *From, const Value *To,
                               const DataLayout &DL);
bool canReplacePointersInUseIfEqual(const Use &U, const Value *To,
                                    const DataLayout &DL);
}

#endif
