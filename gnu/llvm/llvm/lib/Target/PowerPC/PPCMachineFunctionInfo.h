//===-- PPCMachineFunctionInfo.h - Private data used for PowerPC --*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the PowerPC specific subclass of MachineFunctionInfo.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_POWERPC_PPCMACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_POWERPC_PPCMACHINEFUNCTIONINFO_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/TargetCallingConv.h"

namespace llvm {

/// PPCFunctionInfo - This class is derived from MachineFunction private
/// PowerPC target-specific information for each MachineFunction.
class PPCFunctionInfo : public MachineFunctionInfo {
public:
  enum ParamType {
    FixedType,
    ShortFloatingPoint,
    LongFloatingPoint,
    VectorChar,
    VectorShort,
    VectorInt,
    VectorFloat
  };

private:
  virtual void anchor();

  /// FramePointerSaveIndex - Frame index of where the old frame pointer is
  /// stored.  Also used as an anchor for instructions that need to be altered
  /// when using frame pointers (dyna_add, dyna_sub.)
  int FramePointerSaveIndex = 0;

  /// ReturnAddrSaveIndex - Frame index of where the return address is stored.
  ///
  int ReturnAddrSaveIndex = 0;

  /// Frame index where the old base pointer is stored.
  int BasePointerSaveIndex = 0;

  /// Frame index where the old PIC base pointer is stored.
  int PICBasePointerSaveIndex = 0;

  /// Frame index where the ROP Protection Hash is stored.
  int ROPProtectionHashSaveIndex = 0;

  /// MustSaveLR - Indicates whether LR is defined (or clobbered) in the current
  /// function.  This is only valid after the initial scan of the function by
  /// PEI.
  bool MustSaveLR = false;

  /// MustSaveTOC - Indicates that the TOC save needs to be performed in the
  /// prologue of the function. This is typically the case when there are
  /// indirect calls in the function and it is more profitable to save the
  /// TOC pointer in the prologue than in the block(s) containing the call(s).
  bool MustSaveTOC = false;

  /// Do we have to disable shrink-wrapping? This has to be set if we emit any
  /// instructions that clobber LR in the entry block because discovering this
  /// in PEI is too late (happens after shrink-wrapping);
  bool ShrinkWrapDisabled = false;

  /// Does this function have any stack spills.
  bool HasSpills = false;

  /// Does this function spill using instructions with only r+r (not r+i)
  /// forms.
  bool HasNonRISpills = false;

  /// SpillsCR - Indicates whether CR is spilled in the current function.
  bool SpillsCR = false;

  /// DisableNonVolatileCR - Indicates whether non-volatile CR fields would be
  /// disabled.
  bool DisableNonVolatileCR = false;

  /// LRStoreRequired - The bool indicates whether there is some explicit use of
  /// the LR/LR8 stack slot that is not obvious from scanning the code.  This
  /// requires that the code generator produce a store of LR to the stack on
  /// entry, even though LR may otherwise apparently not be used.
  bool LRStoreRequired = false;

  /// This function makes use of the PPC64 ELF TOC base pointer (register r2).
  bool UsesTOCBasePtr = false;

  /// MinReservedArea - This is the frame size that is at least reserved in a
  /// potential caller (parameter+linkage area).
  unsigned MinReservedArea = 0;

  /// TailCallSPDelta - Stack pointer delta used when tail calling. Maximum
  /// amount the stack pointer is adjusted to make the frame bigger for tail
  /// calls. Used for creating an area before the register spill area.
  int TailCallSPDelta = 0;

  /// HasFastCall - Does this function contain a fast call. Used to determine
  /// how the caller's stack pointer should be calculated (epilog/dynamicalloc).
  bool HasFastCall = false;

  /// VarArgsFrameIndex - FrameIndex for start of varargs area.
  int VarArgsFrameIndex = 0;

  /// VarArgsStackOffset - StackOffset for start of stack
  /// arguments.

  int VarArgsStackOffset = 0;

  /// VarArgsNumGPR - Index of the first unused integer
  /// register for parameter passing.
  unsigned VarArgsNumGPR = 0;

  /// VarArgsNumFPR - Index of the first unused double
  /// register for parameter passing.
  unsigned VarArgsNumFPR = 0;

  /// FixedParmsNum - The number of fixed parameters.
  unsigned FixedParmsNum = 0;

  /// FloatingParmsNum - The number of floating parameters.
  unsigned FloatingParmsNum = 0;

  /// VectorParmsNum - The number of vector parameters.
  unsigned VectorParmsNum = 0;

  /// ParamtersType - Store all the parameter's type that are saved on
  /// registers.
  SmallVector<ParamType, 32> ParamtersType;

  /// CRSpillFrameIndex - FrameIndex for CR spill slot for 32-bit SVR4.
  int CRSpillFrameIndex = 0;

  /// If any of CR[2-4] need to be saved in the prologue and restored in the
  /// epilogue then they are added to this array. This is used for the
  /// 64-bit SVR4 ABI.
  SmallVector<Register, 3> MustSaveCRs;

  /// Whether this uses the PIC Base register or not.
  bool UsesPICBase = false;

  /// We keep track attributes for each live-in virtual registers
  /// to use SExt/ZExt flags in later optimization.
  std::vector<std::pair<Register, ISD::ArgFlagsTy>> LiveInAttrs;

  /// Flags for aix-shared-lib-tls-model-opt, will be lazily initialized for
  /// each function.
  bool AIXFuncUseTLSIEForLD = false;
  bool AIXFuncTLSModelOptInitDone = false;

public:
  explicit PPCFunctionInfo(const Function &F, const TargetSubtargetInfo *STI);

  MachineFunctionInfo *
  clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
        const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
      const override;

  int getFramePointerSaveIndex() const { return FramePointerSaveIndex; }
  void setFramePointerSaveIndex(int Idx) { FramePointerSaveIndex = Idx; }

  int getReturnAddrSaveIndex() const { return ReturnAddrSaveIndex; }
  void setReturnAddrSaveIndex(int idx) { ReturnAddrSaveIndex = idx; }

  int getBasePointerSaveIndex() const { return BasePointerSaveIndex; }
  void setBasePointerSaveIndex(int Idx) { BasePointerSaveIndex = Idx; }

  int getPICBasePointerSaveIndex() const { return PICBasePointerSaveIndex; }
  void setPICBasePointerSaveIndex(int Idx) { PICBasePointerSaveIndex = Idx; }

  int getROPProtectionHashSaveIndex() const {
    return ROPProtectionHashSaveIndex;
  }
  void setROPProtectionHashSaveIndex(int Idx) {
    ROPProtectionHashSaveIndex = Idx;
  }

  unsigned getMinReservedArea() const { return MinReservedArea; }
  void setMinReservedArea(unsigned size) { MinReservedArea = size; }

  int getTailCallSPDelta() const { return TailCallSPDelta; }
  void setTailCallSPDelta(int size) { TailCallSPDelta = size; }

  /// MustSaveLR - This is set when the prolog/epilog inserter does its initial
  /// scan of the function. It is true if the LR/LR8 register is ever explicitly
  /// defined/clobbered in the machine function (e.g. by calls and movpctolr,
  /// which is used in PIC generation), or if the LR stack slot is explicitly
  /// referenced by builtin_return_address.
  void setMustSaveLR(bool U) { MustSaveLR = U; }
  bool mustSaveLR() const    { return MustSaveLR; }

  void setMustSaveTOC(bool U) { MustSaveTOC = U; }
  bool mustSaveTOC() const    { return MustSaveTOC; }

  /// We certainly don't want to shrink wrap functions if we've emitted a
  /// MovePCtoLR8 as that has to go into the entry, so the prologue definitely
  /// has to go into the entry block.
  void setShrinkWrapDisabled(bool U) { ShrinkWrapDisabled = U; }
  bool shrinkWrapDisabled() const { return ShrinkWrapDisabled; }

  void setHasSpills()      { HasSpills = true; }
  bool hasSpills() const   { return HasSpills; }

  void setHasNonRISpills()    { HasNonRISpills = true; }
  bool hasNonRISpills() const { return HasNonRISpills; }

  void setSpillsCR()       { SpillsCR = true; }
  bool isCRSpilled() const { return SpillsCR; }

  void setDisableNonVolatileCR() { DisableNonVolatileCR = true; }
  bool isNonVolatileCRDisabled() const { return DisableNonVolatileCR; }

  void setLRStoreRequired() { LRStoreRequired = true; }
  bool isLRStoreRequired() const { return LRStoreRequired; }

  void setUsesTOCBasePtr()    { UsesTOCBasePtr = true; }
  bool usesTOCBasePtr() const { return UsesTOCBasePtr; }

  void setHasFastCall() { HasFastCall = true; }
  bool hasFastCall() const { return HasFastCall;}

  void setAIXFuncTLSModelOptInitDone() { AIXFuncTLSModelOptInitDone = true; }
  bool isAIXFuncTLSModelOptInitDone() const {
    return AIXFuncTLSModelOptInitDone;
  }
  void setAIXFuncUseTLSIEForLD() { AIXFuncUseTLSIEForLD = true; }
  bool isAIXFuncUseTLSIEForLD() const { return AIXFuncUseTLSIEForLD; }

  int getVarArgsFrameIndex() const { return VarArgsFrameIndex; }
  void setVarArgsFrameIndex(int Index) { VarArgsFrameIndex = Index; }

  int getVarArgsStackOffset() const { return VarArgsStackOffset; }
  void setVarArgsStackOffset(int Offset) { VarArgsStackOffset = Offset; }

  unsigned getVarArgsNumGPR() const { return VarArgsNumGPR; }
  void setVarArgsNumGPR(unsigned Num) { VarArgsNumGPR = Num; }

  unsigned getFixedParmsNum() const { return FixedParmsNum; }
  unsigned getFloatingPointParmsNum() const { return FloatingParmsNum; }
  unsigned getVectorParmsNum() const { return VectorParmsNum; }
  bool hasVectorParms() const { return VectorParmsNum != 0; }

  uint32_t getParmsType() const;

  uint32_t getVecExtParmsType() const;

  void appendParameterType(ParamType Type);

  unsigned getVarArgsNumFPR() const { return VarArgsNumFPR; }
  void setVarArgsNumFPR(unsigned Num) { VarArgsNumFPR = Num; }

  /// This function associates attributes for each live-in virtual register.
  void addLiveInAttr(Register VReg, ISD::ArgFlagsTy Flags) {
    LiveInAttrs.push_back(std::make_pair(VReg, Flags));
  }

  /// This function returns true if the specified vreg is
  /// a live-in register and sign-extended.
  bool isLiveInSExt(Register VReg) const;

  /// This function returns true if the specified vreg is
  /// a live-in register and zero-extended.
  bool isLiveInZExt(Register VReg) const;

  int getCRSpillFrameIndex() const { return CRSpillFrameIndex; }
  void setCRSpillFrameIndex(int idx) { CRSpillFrameIndex = idx; }

  const SmallVectorImpl<Register> &
    getMustSaveCRs() const { return MustSaveCRs; }
  void addMustSaveCR(Register Reg) { MustSaveCRs.push_back(Reg); }

  void setUsesPICBase(bool uses) { UsesPICBase = uses; }
  bool usesPICBase() const { return UsesPICBase; }

  MCSymbol *getPICOffsetSymbol(MachineFunction &MF) const;

  MCSymbol *getGlobalEPSymbol(MachineFunction &MF) const;
  MCSymbol *getLocalEPSymbol(MachineFunction &MF) const;
  MCSymbol *getTOCOffsetSymbol(MachineFunction &MF) const;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_POWERPC_PPCMACHINEFUNCTIONINFO_H
