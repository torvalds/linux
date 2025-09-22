//===- FunctionLoweringInfo.h - Lower functions from LLVM IR ---*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements routines for translating functions from LLVM IR into
// Machine IR.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_FUNCTIONLOWERINGINFO_H
#define LLVM_CODEGEN_FUNCTIONLOWERINGINFO_H

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/IndexedMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/KnownBits.h"
#include <cassert>
#include <utility>
#include <vector>

namespace llvm {

class Argument;
class BasicBlock;
class BranchProbabilityInfo;
class DbgDeclareInst;
class Function;
class Instruction;
class MachineFunction;
class MachineInstr;
class MachineRegisterInfo;
class MVT;
class SelectionDAG;
class TargetLowering;

template <typename T> class GenericSSAContext;
using SSAContext = GenericSSAContext<Function>;
template <typename T> class GenericUniformityInfo;
using UniformityInfo = GenericUniformityInfo<SSAContext>;

//===--------------------------------------------------------------------===//
/// FunctionLoweringInfo - This contains information that is global to a
/// function that is used when lowering a region of the function.
///
class FunctionLoweringInfo {
public:
  const Function *Fn;
  MachineFunction *MF;
  const TargetLowering *TLI;
  MachineRegisterInfo *RegInfo;
  BranchProbabilityInfo *BPI;
  const UniformityInfo *UA;
  /// CanLowerReturn - true iff the function's return value can be lowered to
  /// registers.
  bool CanLowerReturn;

  /// True if part of the CSRs will be handled via explicit copies.
  bool SplitCSR;

  /// DemoteRegister - if CanLowerReturn is false, DemoteRegister is a vreg
  /// allocated to hold a pointer to the hidden sret parameter.
  Register DemoteRegister;

  /// MBBMap - A mapping from LLVM basic blocks to their machine code entry.
  DenseMap<const BasicBlock*, MachineBasicBlock *> MBBMap;

  /// ValueMap - Since we emit code for the function a basic block at a time,
  /// we must remember which virtual registers hold the values for
  /// cross-basic-block values.
  DenseMap<const Value *, Register> ValueMap;

  /// VirtReg2Value map is needed by the Divergence Analysis driven
  /// instruction selection. It is reverted ValueMap. It is computed
  /// in lazy style - on demand. It is used to get the Value corresponding
  /// to the live in virtual register and is called from the
  /// TargetLowerinInfo::isSDNodeSourceOfDivergence.
  DenseMap<Register, const Value*> VirtReg2Value;

  /// This method is called from TargetLowerinInfo::isSDNodeSourceOfDivergence
  /// to get the Value corresponding to the live-in virtual register.
  const Value *getValueFromVirtualReg(Register Vreg);

  /// Track virtual registers created for exception pointers.
  DenseMap<const Value *, Register> CatchPadExceptionPointers;

  /// Helper object to track which of three possible relocation mechanisms are
  /// used for a particular value being relocated over a statepoint.
  struct StatepointRelocationRecord {
    enum RelocType {
      // Value did not need to be relocated and can be used directly.
      NoRelocate,
      // Value was spilled to stack and needs filled at the gc.relocate.
      Spill,
      // Value was lowered to tied def and gc.relocate should be replaced with
      // copy from vreg.
      VReg,
      // Value was lowered to tied def and gc.relocate should be replaced with
      // SDValue kept in StatepointLoweringInfo structure. This valid for local
      // relocates only.
      SDValueNode,
    } type = NoRelocate;
    // Payload contains either frame index of the stack slot in which the value
    // was spilled, or virtual register which contains the re-definition.
    union payload_t {
      payload_t() : FI(-1) {}
      int FI;
      Register Reg;
    } payload;
  };

  /// Keep track of each value which was relocated and the strategy used to
  /// relocate that value.  This information is required when visiting
  /// gc.relocates which may appear in following blocks.
  using StatepointSpillMapTy =
    DenseMap<const Value *, StatepointRelocationRecord>;
  DenseMap<const Instruction *, StatepointSpillMapTy> StatepointRelocationMaps;

  /// StaticAllocaMap - Keep track of frame indices for fixed sized allocas in
  /// the entry block.  This allows the allocas to be efficiently referenced
  /// anywhere in the function.
  DenseMap<const AllocaInst*, int> StaticAllocaMap;

  /// ByValArgFrameIndexMap - Keep track of frame indices for byval arguments.
  DenseMap<const Argument*, int> ByValArgFrameIndexMap;

  /// ArgDbgValues - A list of DBG_VALUE instructions created during isel for
  /// function arguments that are inserted after scheduling is completed.
  SmallVector<MachineInstr*, 8> ArgDbgValues;

  /// Bitvector with a bit set if corresponding argument is described in
  /// ArgDbgValues. Using arg numbers according to Argument numbering.
  BitVector DescribedArgs;

  /// RegFixups - Registers which need to be replaced after isel is done.
  DenseMap<Register, Register> RegFixups;

  DenseSet<Register> RegsWithFixups;

  /// StatepointStackSlots - A list of temporary stack slots (frame indices)
  /// used to spill values at a statepoint.  We store them here to enable
  /// reuse of the same stack slots across different statepoints in different
  /// basic blocks.
  SmallVector<unsigned, 50> StatepointStackSlots;

  /// MBB - The current block.
  MachineBasicBlock *MBB;

  /// MBB - The current insert position inside the current block.
  MachineBasicBlock::iterator InsertPt;

  struct LiveOutInfo {
    unsigned NumSignBits : 31;
    unsigned IsValid : 1;
    KnownBits Known = 1;

    LiveOutInfo() : NumSignBits(0), IsValid(true) {}
  };

  /// Record the preferred extend type (ISD::SIGN_EXTEND or ISD::ZERO_EXTEND)
  /// for a value.
  DenseMap<const Value *, ISD::NodeType> PreferredExtendType;

  /// VisitedBBs - The set of basic blocks visited thus far by instruction
  /// selection.
  SmallPtrSet<const BasicBlock*, 4> VisitedBBs;

  /// PHINodesToUpdate - A list of phi instructions whose operand list will
  /// be updated after processing the current basic block.
  /// TODO: This isn't per-function state, it's per-basic-block state. But
  /// there's no other convenient place for it to live right now.
  std::vector<std::pair<MachineInstr*, unsigned> > PHINodesToUpdate;
  unsigned OrigNumPHINodesToUpdate;

  /// If the current MBB is a landing pad, the exception pointer and exception
  /// selector registers are copied into these virtual registers by
  /// SelectionDAGISel::PrepareEHLandingPad().
  unsigned ExceptionPointerVirtReg, ExceptionSelectorVirtReg;

  /// Collection of dbg.declare instructions handled after argument
  /// lowering and before ISel proper.
  SmallPtrSet<const DbgDeclareInst *, 8> PreprocessedDbgDeclares;
  SmallPtrSet<const DbgVariableRecord *, 8> PreprocessedDVRDeclares;

  /// set - Initialize this FunctionLoweringInfo with the given Function
  /// and its associated MachineFunction.
  ///
  void set(const Function &Fn, MachineFunction &MF, SelectionDAG *DAG);

  /// clear - Clear out all the function-specific state. This returns this
  /// FunctionLoweringInfo to an empty state, ready to be used for a
  /// different function.
  void clear();

  /// isExportedInst - Return true if the specified value is an instruction
  /// exported from its block.
  bool isExportedInst(const Value *V) const {
    return ValueMap.count(V);
  }

  Register CreateReg(MVT VT, bool isDivergent = false);

  Register CreateRegs(const Value *V);

  Register CreateRegs(Type *Ty, bool isDivergent = false);

  Register InitializeRegForValue(const Value *V);

  /// GetLiveOutRegInfo - Gets LiveOutInfo for a register, returning NULL if the
  /// register is a PHI destination and the PHI's LiveOutInfo is not valid.
  const LiveOutInfo *GetLiveOutRegInfo(Register Reg) {
    if (!LiveOutRegInfo.inBounds(Reg))
      return nullptr;

    const LiveOutInfo *LOI = &LiveOutRegInfo[Reg];
    if (!LOI->IsValid)
      return nullptr;

    return LOI;
  }

  /// GetLiveOutRegInfo - Gets LiveOutInfo for a register, returning NULL if the
  /// register is a PHI destination and the PHI's LiveOutInfo is not valid. If
  /// the register's LiveOutInfo is for a smaller bit width, it is extended to
  /// the larger bit width by zero extension. The bit width must be no smaller
  /// than the LiveOutInfo's existing bit width.
  const LiveOutInfo *GetLiveOutRegInfo(Register Reg, unsigned BitWidth);

  /// AddLiveOutRegInfo - Adds LiveOutInfo for a register.
  void AddLiveOutRegInfo(Register Reg, unsigned NumSignBits,
                         const KnownBits &Known) {
    // Only install this information if it tells us something.
    if (NumSignBits == 1 && Known.isUnknown())
      return;

    LiveOutRegInfo.grow(Reg);
    LiveOutInfo &LOI = LiveOutRegInfo[Reg];
    LOI.NumSignBits = NumSignBits;
    LOI.Known.One = Known.One;
    LOI.Known.Zero = Known.Zero;
  }

  /// ComputePHILiveOutRegInfo - Compute LiveOutInfo for a PHI's destination
  /// register based on the LiveOutInfo of its operands.
  void ComputePHILiveOutRegInfo(const PHINode*);

  /// InvalidatePHILiveOutRegInfo - Invalidates a PHI's LiveOutInfo, to be
  /// called when a block is visited before all of its predecessors.
  void InvalidatePHILiveOutRegInfo(const PHINode *PN) {
    // PHIs with no uses have no ValueMap entry.
    DenseMap<const Value*, Register>::const_iterator It = ValueMap.find(PN);
    if (It == ValueMap.end())
      return;

    Register Reg = It->second;
    if (Reg == 0)
      return;

    LiveOutRegInfo.grow(Reg);
    LiveOutRegInfo[Reg].IsValid = false;
  }

  /// setArgumentFrameIndex - Record frame index for the byval
  /// argument.
  void setArgumentFrameIndex(const Argument *A, int FI);

  /// getArgumentFrameIndex - Get frame index for the byval argument.
  int getArgumentFrameIndex(const Argument *A);

  Register getCatchPadExceptionPointerVReg(const Value *CPI,
                                           const TargetRegisterClass *RC);

private:
  /// LiveOutRegInfo - Information about live out vregs.
  IndexedMap<LiveOutInfo, VirtReg2IndexFunctor> LiveOutRegInfo;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_FUNCTIONLOWERINGINFO_H
