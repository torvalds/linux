//== llvm/CodeGen/GlobalISel/LoadStoreOpt.h - LoadStoreOpt -------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// This is an optimization pass for GlobalISel generic memory operations.
/// Specifically, it focuses on merging stores and loads to consecutive
/// addresses.
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_LOADSTOREOPT_H
#define LLVM_CODEGEN_GLOBALISEL_LOADSTOREOPT_H

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {
// Forward declarations.
class AnalysisUsage;
class GStore;
class LegalizerInfo;
class MachineBasicBlock;
class MachineInstr;
class TargetLowering;
struct LegalityQuery;
class MachineRegisterInfo;
namespace GISelAddressing {
/// Helper struct to store a base, index and offset that forms an address
class BaseIndexOffset {
private:
  Register BaseReg;
  Register IndexReg;
  std::optional<int64_t> Offset;

public:
  BaseIndexOffset() = default;
  Register getBase() { return BaseReg; }
  Register getBase() const { return BaseReg; }
  Register getIndex() { return IndexReg; }
  Register getIndex() const { return IndexReg; }
  void setBase(Register NewBase) { BaseReg = NewBase; }
  void setIndex(Register NewIndex) { IndexReg = NewIndex; }
  void setOffset(std::optional<int64_t> NewOff) { Offset = NewOff; }
  bool hasValidOffset() const { return Offset.has_value(); }
  int64_t getOffset() const { return *Offset; }
};

/// Returns a BaseIndexOffset which describes the pointer in \p Ptr.
BaseIndexOffset getPointerInfo(Register Ptr, MachineRegisterInfo &MRI);

/// Compute whether or not a memory access at \p MI1 aliases with an access at
/// \p MI2 \returns true if either alias/no-alias is known. Sets \p IsAlias
/// accordingly.
bool aliasIsKnownForLoadStore(const MachineInstr &MI1, const MachineInstr &MI2,
                              bool &IsAlias, MachineRegisterInfo &MRI);

/// Returns true if the instruction \p MI may alias \p Other.
/// This function uses multiple strategies to detect aliasing, whereas
/// aliasIsKnownForLoadStore just looks at the addresses of load/stores and is
/// tries to reason about base/index/offsets.
bool instMayAlias(const MachineInstr &MI, const MachineInstr &Other,
                  MachineRegisterInfo &MRI, AliasAnalysis *AA);
} // namespace GISelAddressing

using namespace GISelAddressing;

class LoadStoreOpt : public MachineFunctionPass {
public:
  static char ID;

private:
  /// An input function to decide if the pass should run or not
  /// on the given MachineFunction.
  std::function<bool(const MachineFunction &)> DoNotRunPass;

  MachineRegisterInfo *MRI = nullptr;
  const TargetLowering *TLI = nullptr;
  MachineFunction *MF = nullptr;
  AliasAnalysis *AA = nullptr;
  const LegalizerInfo *LI = nullptr;

  MachineIRBuilder Builder;

  /// Initialize the field members using \p MF.
  void init(MachineFunction &MF);

  class StoreMergeCandidate {
  public:
    // The base pointer used as the base for all stores in this candidate.
    Register BasePtr;
    // Our algorithm is very simple at the moment. We assume that in instruction
    // order stores are writing to incremeneting consecutive addresses. So when
    // we walk the block in reverse order, the next eligible store must write to
    // an offset one store width lower than CurrentLowestOffset.
    int64_t CurrentLowestOffset;
    SmallVector<GStore *> Stores;
    // A vector of MachineInstr/unsigned pairs to denote potential aliases that
    // need to be checked before the candidate is considered safe to merge. The
    // unsigned value is an index into the Stores vector. The indexed store is
    // the highest-indexed store that has already been checked to not have an
    // alias with the instruction. We record this so we don't have to repeat
    // alias checks that have been already done, only those with stores added
    // after the potential alias is recorded.
    SmallVector<std::pair<MachineInstr *, unsigned>> PotentialAliases;

    void addPotentialAlias(MachineInstr &MI);

    /// Reset this candidate back to an empty one.
    void reset() {
      Stores.clear();
      PotentialAliases.clear();
      CurrentLowestOffset = 0;
      BasePtr = Register();
    }
  };

  bool isLegalOrBeforeLegalizer(const LegalityQuery &Query,
                                MachineFunction &MF) const;
  /// If the given store is valid to be a member of the candidate, add it and
  /// return true. Otherwise, returns false.
  bool addStoreToCandidate(GStore &MI, StoreMergeCandidate &C);
  /// Returns true if the instruction \p MI would potentially alias with any
  /// stores in the candidate \p C.
  bool operationAliasesWithCandidate(MachineInstr &MI, StoreMergeCandidate &C);
  /// Merges the stores in the given vector into a wide store.
  /// \p returns true if at least some of the stores were merged.
  /// This may decide not to merge stores if heuristics predict it will not be
  /// worth it.
  bool mergeStores(SmallVectorImpl<GStore *> &StoresToMerge);
  /// Perform a merge of all the stores in \p Stores into a single store.
  /// Erases the old stores from the block when finished.
  /// \returns true if merging was done. It may fail to perform a merge if
  /// there are issues with materializing legal wide values.
  bool doSingleStoreMerge(SmallVectorImpl<GStore *> &Stores);
  bool processMergeCandidate(StoreMergeCandidate &C);
  bool mergeBlockStores(MachineBasicBlock &MBB);
  bool mergeFunctionStores(MachineFunction &MF);

  bool mergeTruncStore(GStore &StoreMI,
                       SmallPtrSetImpl<GStore *> &DeletedStores);
  bool mergeTruncStoresBlock(MachineBasicBlock &MBB);

  /// Initialize some target-specific data structures for the store merging
  /// optimization. \p AddrSpace indicates which address space to use when
  /// probing the legalizer info for legal stores.
  void initializeStoreMergeTargetInfo(unsigned AddrSpace = 0);
  /// A map between address space numbers and a bitvector of supported stores
  /// sizes. Each bit in the bitvector represents whether a store size of
  /// that bit's value is legal. E.g. if bit 64 is set, then 64 bit scalar
  /// stores are legal.
  DenseMap<unsigned, BitVector> LegalStoreSizes;
  bool IsPreLegalizer = false;
  /// Contains instructions to be erased at the end of a block scan.
  SmallSet<MachineInstr *, 16> InstsToErase;

public:
  LoadStoreOpt();
  LoadStoreOpt(std::function<bool(const MachineFunction &)>);

  StringRef getPassName() const override { return "LoadStoreOpt"; }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties()
        .set(MachineFunctionProperties::Property::IsSSA);
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  bool runOnMachineFunction(MachineFunction &MF) override;
};

} // End namespace llvm.

#endif
