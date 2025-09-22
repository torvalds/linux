//===-- X86MachineFunctionInfo.h - X86 machine function info ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares X86-specific per-machine-function information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_X86MACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_X86_X86MACHINEFUNCTIONINFO_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MIRYamlMapping.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/Support/YAMLTraits.h"
#include <set>

namespace llvm {

enum AMXProgModelEnum { None = 0, DirectReg = 1, ManagedRA = 2 };

class X86MachineFunctionInfo;

namespace yaml {
template <> struct ScalarEnumerationTraits<AMXProgModelEnum> {
  static void enumeration(IO &YamlIO, AMXProgModelEnum &Value) {
    YamlIO.enumCase(Value, "None", AMXProgModelEnum::None);
    YamlIO.enumCase(Value, "DirectReg", AMXProgModelEnum::DirectReg);
    YamlIO.enumCase(Value, "ManagedRA", AMXProgModelEnum::ManagedRA);
  }
};

struct X86MachineFunctionInfo final : public yaml::MachineFunctionInfo {
  AMXProgModelEnum AMXProgModel;

  X86MachineFunctionInfo() = default;
  X86MachineFunctionInfo(const llvm::X86MachineFunctionInfo &MFI);

  void mappingImpl(yaml::IO &YamlIO) override;
  ~X86MachineFunctionInfo() = default;
};

template <> struct MappingTraits<X86MachineFunctionInfo> {
  static void mapping(IO &YamlIO, X86MachineFunctionInfo &MFI) {
    YamlIO.mapOptional("amxProgModel", MFI.AMXProgModel);
  }
};
} // end namespace yaml

/// X86MachineFunctionInfo - This class is derived from MachineFunction and
/// contains private X86 target-specific information for each MachineFunction.
class X86MachineFunctionInfo : public MachineFunctionInfo {
  virtual void anchor();

  /// ForceFramePointer - True if the function is required to use of frame
  /// pointer for reasons other than it containing dynamic allocation or
  /// that FP eliminatation is turned off. For example, Cygwin main function
  /// contains stack pointer re-alignment code which requires FP.
  bool ForceFramePointer = false;

  /// RestoreBasePointerOffset - Non-zero if the function has base pointer
  /// and makes call to llvm.eh.sjlj.setjmp. When non-zero, the value is a
  /// displacement from the frame pointer to a slot where the base pointer
  /// is stashed.
  signed char RestoreBasePointerOffset = 0;

  /// WinEHXMMSlotInfo - Slot information of XMM registers in the stack frame
  /// in bytes.
  DenseMap<int, unsigned> WinEHXMMSlotInfo;

  /// CalleeSavedFrameSize - Size of the callee-saved register portion of the
  /// stack frame in bytes.
  unsigned CalleeSavedFrameSize = 0;

  // SaveArgSize - Number of register arguments saved on the stack
  unsigned SaveArgSize = 0;

  /// BytesToPopOnReturn - Number of bytes function pops on return (in addition
  /// to the space used by the return address).
  /// Used on windows platform for stdcall & fastcall name decoration
  unsigned BytesToPopOnReturn = 0;

  /// ReturnAddrIndex - FrameIndex for return slot.
  int ReturnAddrIndex = 0;

  /// FrameIndex for return slot.
  int FrameAddrIndex = 0;

  /// TailCallReturnAddrDelta - The number of bytes by which return address
  /// stack slot is moved as the result of tail call optimization.
  int TailCallReturnAddrDelta = 0;

  /// SRetReturnReg - Some subtargets require that sret lowering includes
  /// returning the value of the returned struct in a register. This field
  /// holds the virtual register into which the sret argument is passed.
  Register SRetReturnReg;

  /// GlobalBaseReg - keeps track of the virtual register initialized for
  /// use as the global base register. This is used for PIC in some PIC
  /// relocation models.
  Register GlobalBaseReg;

  /// VarArgsFrameIndex - FrameIndex for start of varargs area.
  int VarArgsFrameIndex = 0;
  /// RegSaveFrameIndex - X86-64 vararg func register save area.
  int RegSaveFrameIndex = 0;
  /// VarArgsGPOffset - X86-64 vararg func int reg offset.
  unsigned VarArgsGPOffset = 0;
  /// VarArgsFPOffset - X86-64 vararg func fp reg offset.
  unsigned VarArgsFPOffset = 0;
  /// ArgumentStackSize - The number of bytes on stack consumed by the arguments
  /// being passed on the stack.
  unsigned ArgumentStackSize = 0;
  /// NumLocalDynamics - Number of local-dynamic TLS accesses.
  unsigned NumLocalDynamics = 0;
  /// HasPushSequences - Keeps track of whether this function uses sequences
  /// of pushes to pass function parameters.
  bool HasPushSequences = false;

  /// True if the function recovers from an SEH exception, and therefore needs
  /// to spill and restore the frame pointer.
  bool HasSEHFramePtrSave = false;

  /// The frame index of a stack object containing the original frame pointer
  /// used to address arguments in a function using a base pointer.
  int SEHFramePtrSaveIndex = 0;

  /// The AMX programing model used in the function.
  AMXProgModelEnum AMXProgModel = AMXProgModelEnum::None;

  /// True if this function has a subset of CSRs that is handled explicitly via
  /// copies.
  bool IsSplitCSR = false;

  /// True if this function uses the red zone.
  bool UsesRedZone = false;

  /// True if this function has DYN_ALLOCA instructions.
  bool HasDynAlloca = false;

  /// True if this function has any preallocated calls.
  bool HasPreallocatedCall = false;

  /// Whether this function has an extended frame record [Ctx, RBP, Return
  /// addr]. If so, bit 60 of the in-memory frame pointer will be 1 to enable
  /// other tools to detect the extended record.
  bool HasSwiftAsyncContext = false;

  /// Ajust stack for push2/pop2
  bool PadForPush2Pop2 = false;

  /// Candidate registers for push2/pop2
  std::set<Register> CandidatesForPush2Pop2;

  /// True if this function has CFI directives that adjust the CFA.
  /// This is used to determine if we should direct the debugger to use
  /// the CFA instead of the stack pointer.
  bool HasCFIAdjustCfa = false;

  MachineInstr *StackPtrSaveMI = nullptr;

  std::optional<int> SwiftAsyncContextFrameIdx;

  // Preallocated fields are only used during isel.
  // FIXME: Can we find somewhere else to store these?
  DenseMap<const Value *, size_t> PreallocatedIds;
  SmallVector<size_t, 0> PreallocatedStackSizes;
  SmallVector<SmallVector<size_t, 4>, 0> PreallocatedArgOffsets;

private:
  /// ForwardedMustTailRegParms - A list of virtual and physical registers
  /// that must be forwarded to every musttail call.
  SmallVector<ForwardedRegister, 1> ForwardedMustTailRegParms;

public:
  X86MachineFunctionInfo() = default;
  X86MachineFunctionInfo(const Function &F, const TargetSubtargetInfo *STI) {}

  X86MachineFunctionInfo(const X86MachineFunctionInfo &) = default;

  MachineFunctionInfo *
  clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
        const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
      const override;

  void initializeBaseYamlFields(const yaml::X86MachineFunctionInfo &YamlMFI);

  bool getForceFramePointer() const { return ForceFramePointer;}
  void setForceFramePointer(bool forceFP) { ForceFramePointer = forceFP; }

  bool getHasPushSequences() const { return HasPushSequences; }
  void setHasPushSequences(bool HasPush) { HasPushSequences = HasPush; }

  bool getRestoreBasePointer() const { return RestoreBasePointerOffset!=0; }
  void setRestoreBasePointer(const MachineFunction *MF);
  void setRestoreBasePointer(unsigned CalleeSavedFrameSize) {
    RestoreBasePointerOffset = -CalleeSavedFrameSize;
  }
  int getRestoreBasePointerOffset() const {return RestoreBasePointerOffset; }

  DenseMap<int, unsigned>& getWinEHXMMSlotInfo() { return WinEHXMMSlotInfo; }
  const DenseMap<int, unsigned>& getWinEHXMMSlotInfo() const {
    return WinEHXMMSlotInfo; }

  unsigned getCalleeSavedFrameSize() const {
    return CalleeSavedFrameSize + 8 * padForPush2Pop2();
  }
  void setCalleeSavedFrameSize(unsigned bytes) { CalleeSavedFrameSize = bytes; }

  unsigned getSaveArgSize() const { return SaveArgSize; }
  void setSaveArgSize(unsigned bytes) { SaveArgSize = bytes; }

  unsigned getBytesToPopOnReturn() const { return BytesToPopOnReturn; }
  void setBytesToPopOnReturn (unsigned bytes) { BytesToPopOnReturn = bytes;}

  int getRAIndex() const { return ReturnAddrIndex; }
  void setRAIndex(int Index) { ReturnAddrIndex = Index; }

  int getFAIndex() const { return FrameAddrIndex; }
  void setFAIndex(int Index) { FrameAddrIndex = Index; }

  int getTCReturnAddrDelta() const { return TailCallReturnAddrDelta; }
  void setTCReturnAddrDelta(int delta) {TailCallReturnAddrDelta = delta;}

  Register getSRetReturnReg() const { return SRetReturnReg; }
  void setSRetReturnReg(Register Reg) { SRetReturnReg = Reg; }

  Register getGlobalBaseReg() const { return GlobalBaseReg; }
  void setGlobalBaseReg(Register Reg) { GlobalBaseReg = Reg; }

  int getVarArgsFrameIndex() const { return VarArgsFrameIndex; }
  void setVarArgsFrameIndex(int Idx) { VarArgsFrameIndex = Idx; }

  int getRegSaveFrameIndex() const { return RegSaveFrameIndex; }
  void setRegSaveFrameIndex(int Idx) { RegSaveFrameIndex = Idx; }

  unsigned getVarArgsGPOffset() const { return VarArgsGPOffset; }
  void setVarArgsGPOffset(unsigned Offset) { VarArgsGPOffset = Offset; }

  unsigned getVarArgsFPOffset() const { return VarArgsFPOffset; }
  void setVarArgsFPOffset(unsigned Offset) { VarArgsFPOffset = Offset; }

  unsigned getArgumentStackSize() const { return ArgumentStackSize; }
  void setArgumentStackSize(unsigned size) { ArgumentStackSize = size; }

  unsigned getNumLocalDynamicTLSAccesses() const { return NumLocalDynamics; }
  void incNumLocalDynamicTLSAccesses() { ++NumLocalDynamics; }

  bool getHasSEHFramePtrSave() const { return HasSEHFramePtrSave; }
  void setHasSEHFramePtrSave(bool V) { HasSEHFramePtrSave = V; }

  int getSEHFramePtrSaveIndex() const { return SEHFramePtrSaveIndex; }
  void setSEHFramePtrSaveIndex(int Index) { SEHFramePtrSaveIndex = Index; }

  AMXProgModelEnum getAMXProgModel() const { return AMXProgModel; }
  void setAMXProgModel(AMXProgModelEnum Model) {
    assert((AMXProgModel == AMXProgModelEnum::None || AMXProgModel == Model) &&
           "mixed model is not supported");
    AMXProgModel = Model;
  }

  SmallVectorImpl<ForwardedRegister> &getForwardedMustTailRegParms() {
    return ForwardedMustTailRegParms;
  }

  bool isSplitCSR() const { return IsSplitCSR; }
  void setIsSplitCSR(bool s) { IsSplitCSR = s; }

  bool getUsesRedZone() const { return UsesRedZone; }
  void setUsesRedZone(bool V) { UsesRedZone = V; }

  bool hasDynAlloca() const { return HasDynAlloca; }
  void setHasDynAlloca(bool v) { HasDynAlloca = v; }

  bool hasPreallocatedCall() const { return HasPreallocatedCall; }
  void setHasPreallocatedCall(bool v) { HasPreallocatedCall = v; }

  bool hasSwiftAsyncContext() const { return HasSwiftAsyncContext; }
  void setHasSwiftAsyncContext(bool v) { HasSwiftAsyncContext = v; }

  bool padForPush2Pop2() const { return PadForPush2Pop2; }
  void setPadForPush2Pop2(bool V) { PadForPush2Pop2 = V; }

  bool isCandidateForPush2Pop2(Register Reg) const {
    return CandidatesForPush2Pop2.find(Reg) != CandidatesForPush2Pop2.end();
  }
  void addCandidateForPush2Pop2(Register Reg) {
    CandidatesForPush2Pop2.insert(Reg);
  }
  size_t getNumCandidatesForPush2Pop2() const {
    return CandidatesForPush2Pop2.size();
  }

  bool hasCFIAdjustCfa() const { return HasCFIAdjustCfa; }
  void setHasCFIAdjustCfa(bool v) { HasCFIAdjustCfa = v; }

  void setStackPtrSaveMI(MachineInstr *MI) { StackPtrSaveMI = MI; }
  MachineInstr *getStackPtrSaveMI() const { return StackPtrSaveMI; }

  std::optional<int> getSwiftAsyncContextFrameIdx() const {
    return SwiftAsyncContextFrameIdx;
  }
  void setSwiftAsyncContextFrameIdx(int v) { SwiftAsyncContextFrameIdx = v; }

  size_t getPreallocatedIdForCallSite(const Value *CS) {
    auto Insert = PreallocatedIds.insert({CS, PreallocatedIds.size()});
    if (Insert.second) {
      PreallocatedStackSizes.push_back(0);
      PreallocatedArgOffsets.emplace_back();
    }
    return Insert.first->second;
  }

  void setPreallocatedStackSize(size_t Id, size_t StackSize) {
    PreallocatedStackSizes[Id] = StackSize;
  }

  size_t getPreallocatedStackSize(const size_t Id) {
    assert(PreallocatedStackSizes[Id] != 0 && "stack size not set");
    return PreallocatedStackSizes[Id];
  }

  void setPreallocatedArgOffsets(size_t Id, ArrayRef<size_t> AO) {
    PreallocatedArgOffsets[Id].assign(AO.begin(), AO.end());
  }

  ArrayRef<size_t> getPreallocatedArgOffsets(const size_t Id) {
    assert(!PreallocatedArgOffsets[Id].empty() && "arg offsets not set");
    return PreallocatedArgOffsets[Id];
  }
};

} // End llvm namespace

#endif
