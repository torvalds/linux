//===-- ARMMachineFunctionInfo.h - ARM machine function info ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares ARM-specific per-machine-function information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_ARMMACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_ARM_ARMMACHINEFUNCTIONINFO_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/CodeGen/MIRYamlMapping.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/Support/ErrorHandling.h"
#include <utility>

namespace llvm {

namespace yaml {
struct ARMFunctionInfo;
} // end namespace yaml

class ARMSubtarget;

/// ARMFunctionInfo - This class is derived from MachineFunctionInfo and
/// contains private ARM-specific information for each MachineFunction.
class ARMFunctionInfo : public MachineFunctionInfo {
  virtual void anchor();

  /// isThumb - True if this function is compiled under Thumb mode.
  /// Used to initialized Align, so must precede it.
  bool isThumb = false;

  /// hasThumb2 - True if the target architecture supports Thumb2. Do not use
  /// to determine if function is compiled under Thumb mode, for that use
  /// 'isThumb'.
  bool hasThumb2 = false;

  /// ArgsRegSaveSize - Size of the register save area for vararg functions or
  /// those making guaranteed tail calls that need more stack argument space
  /// than is provided by this functions incoming parameters.
  ///
  unsigned ArgRegsSaveSize = 0;

  /// ReturnRegsCount - Number of registers used up in the return.
  unsigned ReturnRegsCount = 0;

  /// HasStackFrame - True if this function has a stack frame. Set by
  /// determineCalleeSaves().
  bool HasStackFrame = false;

  /// RestoreSPFromFP - True if epilogue should restore SP from FP. Set by
  /// emitPrologue.
  bool RestoreSPFromFP = false;

  /// LRSpilled - True if the LR register has been for spilled for
  /// any reason, so it's legal to emit an ARM::tBfar (i.e. "bl").
  bool LRSpilled = false;

  /// FramePtrSpillOffset - If HasStackFrame, this records the frame pointer
  /// spill stack offset.
  unsigned FramePtrSpillOffset = 0;

  /// GPRCS1Offset, GPRCS2Offset, DPRCSOffset - Starting offset of callee saved
  /// register spills areas. For Mac OS X:
  ///
  /// GPR callee-saved (1) : r4, r5, r6, r7, lr
  /// --------------------------------------------
  /// GPR callee-saved (2) : r8, r10, r11
  /// --------------------------------------------
  /// DPR callee-saved : d8 - d15
  ///
  /// Also see AlignedDPRCSRegs below. Not all D-regs need to go in area 3.
  /// Some may be spilled after the stack has been realigned.
  unsigned GPRCS1Offset = 0;
  unsigned GPRCS2Offset = 0;
  unsigned DPRCSOffset = 0;

  /// GPRCS1Size, GPRCS2Size, DPRCSSize - Sizes of callee saved register spills
  /// areas.
  unsigned FPCXTSaveSize = 0;
  unsigned FRSaveSize = 0;
  unsigned GPRCS1Size = 0;
  unsigned GPRCS2Size = 0;
  unsigned DPRCSAlignGapSize = 0;
  unsigned DPRCSSize = 0;

  /// NumAlignedDPRCS2Regs - The number of callee-saved DPRs that are saved in
  /// the aligned portion of the stack frame.  This is always a contiguous
  /// sequence of D-registers starting from d8.
  ///
  /// We do not keep track of the frame indices used for these registers - they
  /// behave like any other frame index in the aligned stack frame.  These
  /// registers also aren't included in DPRCSSize above.
  unsigned NumAlignedDPRCS2Regs = 0;

  unsigned PICLabelUId = 0;

  /// VarArgsFrameIndex - FrameIndex for start of varargs area.
  int VarArgsFrameIndex = 0;

  /// HasITBlocks - True if IT blocks have been inserted.
  bool HasITBlocks = false;

  // Security Extensions
  bool IsCmseNSEntry;
  bool IsCmseNSCall;

  /// CPEClones - Track constant pool entries clones created by Constant Island
  /// pass.
  DenseMap<unsigned, unsigned> CPEClones;

  /// ArgumentStackSize - amount of bytes on stack consumed by the arguments
  /// being passed on the stack
  unsigned ArgumentStackSize = 0;

  /// ArgumentStackToRestore - amount of bytes on stack consumed that we must
  /// restore on return.
  unsigned ArgumentStackToRestore = 0;

  /// CoalescedWeights - mapping of basic blocks to the rolling counter of
  /// coalesced weights.
  DenseMap<const MachineBasicBlock*, unsigned> CoalescedWeights;

  /// True if this function has a subset of CSRs that is handled explicitly via
  /// copies.
  bool IsSplitCSR = false;

  /// Globals that have had their storage promoted into the constant pool.
  SmallPtrSet<const GlobalVariable*,2> PromotedGlobals;

  /// The amount the literal pool has been increasedby due to promoted globals.
  int PromotedGlobalsIncrease = 0;

  /// True if r0 will be preserved by a call to this function (e.g. C++
  /// con/destructors).
  bool PreservesR0 = false;

  /// True if the function should sign its return address.
  bool SignReturnAddress = false;

  /// True if the fucntion should sign its return address, even if LR is not
  /// saved.
  bool SignReturnAddressAll = false;

  /// True if BTI instructions should be placed at potential indirect jump
  /// destinations.
  bool BranchTargetEnforcement = false;

public:
  ARMFunctionInfo() = default;

  explicit ARMFunctionInfo(const Function &F, const ARMSubtarget *STI);

  MachineFunctionInfo *
  clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
        const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
      const override;

  bool isThumbFunction() const { return isThumb; }
  bool isThumb1OnlyFunction() const { return isThumb && !hasThumb2; }
  bool isThumb2Function() const { return isThumb && hasThumb2; }

  bool isCmseNSEntryFunction() const { return IsCmseNSEntry; }
  bool isCmseNSCallFunction() const { return IsCmseNSCall; }

  unsigned getArgRegsSaveSize() const { return ArgRegsSaveSize; }
  void setArgRegsSaveSize(unsigned s) { ArgRegsSaveSize = s; }

  unsigned getReturnRegsCount() const { return ReturnRegsCount; }
  void setReturnRegsCount(unsigned s) { ReturnRegsCount = s; }

  bool hasStackFrame() const { return HasStackFrame; }
  void setHasStackFrame(bool s) { HasStackFrame = s; }

  bool shouldRestoreSPFromFP() const { return RestoreSPFromFP; }
  void setShouldRestoreSPFromFP(bool s) { RestoreSPFromFP = s; }

  bool isLRSpilled() const { return LRSpilled; }
  void setLRIsSpilled(bool s) { LRSpilled = s; }

  unsigned getFramePtrSpillOffset() const { return FramePtrSpillOffset; }
  void setFramePtrSpillOffset(unsigned o) { FramePtrSpillOffset = o; }

  unsigned getNumAlignedDPRCS2Regs() const { return NumAlignedDPRCS2Regs; }
  void setNumAlignedDPRCS2Regs(unsigned n) { NumAlignedDPRCS2Regs = n; }

  unsigned getGPRCalleeSavedArea1Offset() const { return GPRCS1Offset; }
  unsigned getGPRCalleeSavedArea2Offset() const { return GPRCS2Offset; }
  unsigned getDPRCalleeSavedAreaOffset()  const { return DPRCSOffset; }

  void setGPRCalleeSavedArea1Offset(unsigned o) { GPRCS1Offset = o; }
  void setGPRCalleeSavedArea2Offset(unsigned o) { GPRCS2Offset = o; }
  void setDPRCalleeSavedAreaOffset(unsigned o)  { DPRCSOffset = o; }

  unsigned getFPCXTSaveAreaSize() const       { return FPCXTSaveSize; }
  unsigned getFrameRecordSavedAreaSize() const { return FRSaveSize; }
  unsigned getGPRCalleeSavedArea1Size() const { return GPRCS1Size; }
  unsigned getGPRCalleeSavedArea2Size() const { return GPRCS2Size; }
  unsigned getDPRCalleeSavedGapSize() const   { return DPRCSAlignGapSize; }
  unsigned getDPRCalleeSavedAreaSize()  const { return DPRCSSize; }

  void setFPCXTSaveAreaSize(unsigned s)       { FPCXTSaveSize = s; }
  void setFrameRecordSavedAreaSize(unsigned s) { FRSaveSize = s; }
  void setGPRCalleeSavedArea1Size(unsigned s) { GPRCS1Size = s; }
  void setGPRCalleeSavedArea2Size(unsigned s) { GPRCS2Size = s; }
  void setDPRCalleeSavedGapSize(unsigned s)   { DPRCSAlignGapSize = s; }
  void setDPRCalleeSavedAreaSize(unsigned s)  { DPRCSSize = s; }

  unsigned getArgumentStackSize() const { return ArgumentStackSize; }
  void setArgumentStackSize(unsigned size) { ArgumentStackSize = size; }

  unsigned getArgumentStackToRestore() const { return ArgumentStackToRestore; }
  void setArgumentStackToRestore(unsigned v) { ArgumentStackToRestore = v; }

  void initPICLabelUId(unsigned UId) {
    PICLabelUId = UId;
  }

  unsigned getNumPICLabels() const {
    return PICLabelUId;
  }

  unsigned createPICLabelUId() {
    return PICLabelUId++;
  }

  int getVarArgsFrameIndex() const { return VarArgsFrameIndex; }
  void setVarArgsFrameIndex(int Index) { VarArgsFrameIndex = Index; }

  bool hasITBlocks() const { return HasITBlocks; }
  void setHasITBlocks(bool h) { HasITBlocks = h; }

  bool isSplitCSR() const { return IsSplitCSR; }
  void setIsSplitCSR(bool s) { IsSplitCSR = s; }

  void recordCPEClone(unsigned CPIdx, unsigned CPCloneIdx) {
    if (!CPEClones.insert(std::make_pair(CPCloneIdx, CPIdx)).second)
      llvm_unreachable("Duplicate entries!");
  }

  unsigned getOriginalCPIdx(unsigned CloneIdx) const {
    DenseMap<unsigned, unsigned>::const_iterator I = CPEClones.find(CloneIdx);
    if (I != CPEClones.end())
      return I->second;
    else
      return -1U;
  }

  DenseMap<const MachineBasicBlock*, unsigned>::iterator getCoalescedWeight(
                                                  MachineBasicBlock* MBB) {
    auto It = CoalescedWeights.find(MBB);
    if (It == CoalescedWeights.end()) {
      It = CoalescedWeights.insert(std::make_pair(MBB, 0)).first;
    }
    return It;
  }

  /// Indicate to the backend that \c GV has had its storage changed to inside
  /// a constant pool. This means it no longer needs to be emitted as a
  /// global variable.
  void markGlobalAsPromotedToConstantPool(const GlobalVariable *GV) {
    PromotedGlobals.insert(GV);
  }
  SmallPtrSet<const GlobalVariable*, 2>& getGlobalsPromotedToConstantPool() {
    return PromotedGlobals;
  }
  int getPromotedConstpoolIncrease() const {
    return PromotedGlobalsIncrease;
  }
  void setPromotedConstpoolIncrease(int Sz) {
    PromotedGlobalsIncrease = Sz;
  }

  DenseMap<unsigned, unsigned> EHPrologueRemappedRegs;
  DenseMap<unsigned, unsigned> EHPrologueOffsetInRegs;

  void setPreservesR0() { PreservesR0 = true; }
  bool getPreservesR0() const { return PreservesR0; }

  bool shouldSignReturnAddress() const {
    return shouldSignReturnAddress(LRSpilled);
  }

  bool shouldSignReturnAddress(bool SpillsLR) const {
    if (!SignReturnAddress)
      return false;
    if (SignReturnAddressAll)
      return true;
    return SpillsLR;
  }

  bool branchTargetEnforcement() const { return BranchTargetEnforcement; }

  void initializeBaseYamlFields(const yaml::ARMFunctionInfo &YamlMFI);
};

namespace yaml {
struct ARMFunctionInfo final : public yaml::MachineFunctionInfo {
  bool LRSpilled;

  ARMFunctionInfo() = default;
  ARMFunctionInfo(const llvm::ARMFunctionInfo &MFI);

  void mappingImpl(yaml::IO &YamlIO) override;
  ~ARMFunctionInfo() = default;
};

template <> struct MappingTraits<ARMFunctionInfo> {
  static void mapping(IO &YamlIO, ARMFunctionInfo &MFI) {
    YamlIO.mapOptional("isLRSpilled", MFI.LRSpilled);
  }
};

} // end namespace yaml

} // end namespace llvm

#endif // LLVM_LIB_TARGET_ARM_ARMMACHINEFUNCTIONINFO_H
