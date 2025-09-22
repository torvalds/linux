//===--- AArch64Subtarget.h - Define Subtarget for the AArch64 -*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the AArch64 specific subclass of TargetSubtarget.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_AARCH64SUBTARGET_H
#define LLVM_LIB_TARGET_AARCH64_AARCH64SUBTARGET_H

#include "AArch64FrameLowering.h"
#include "AArch64ISelLowering.h"
#include "AArch64InstrInfo.h"
#include "AArch64PointerAuth.h"
#include "AArch64RegisterInfo.h"
#include "AArch64SelectionDAGInfo.h"
#include "llvm/CodeGen/GlobalISel/CallLowering.h"
#include "llvm/CodeGen/GlobalISel/InlineAsmLowering.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelector.h"
#include "llvm/CodeGen/GlobalISel/LegalizerInfo.h"
#include "llvm/CodeGen/RegisterBankInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DataLayout.h"

#define GET_SUBTARGETINFO_HEADER
#include "AArch64GenSubtargetInfo.inc"

namespace llvm {
class GlobalValue;
class StringRef;
class Triple;

class AArch64Subtarget final : public AArch64GenSubtargetInfo {
public:
  enum ARMProcFamilyEnum : uint8_t {
    Others,
#define ARM_PROCESSOR_FAMILY(ENUM) ENUM,
#include "llvm/TargetParser/AArch64TargetParserDef.inc"
#undef ARM_PROCESSOR_FAMILY
  };

protected:
  /// ARMProcFamily - ARM processor family: Cortex-A53, Cortex-A57, and others.
  ARMProcFamilyEnum ARMProcFamily = Others;

  // Enable 64-bit vectorization in SLP.
  unsigned MinVectorRegisterBitWidth = 64;

// Bool members corresponding to the SubtargetFeatures defined in tablegen
#define GET_SUBTARGETINFO_MACRO(ATTRIBUTE, DEFAULT, GETTER)                    \
  bool ATTRIBUTE = DEFAULT;
#include "AArch64GenSubtargetInfo.inc"

  uint8_t MaxInterleaveFactor = 2;
  uint8_t VectorInsertExtractBaseCost = 2;
  uint16_t CacheLineSize = 0;
  uint16_t PrefetchDistance = 0;
  uint16_t MinPrefetchStride = 1;
  unsigned MaxPrefetchIterationsAhead = UINT_MAX;
  Align PrefFunctionAlignment;
  Align PrefLoopAlignment;
  unsigned MaxBytesForLoopAlignment = 0;
  unsigned MinimumJumpTableEntries = 4;
  unsigned MaxJumpTableSize = 0;

  // ReserveXRegister[i] - X#i is not available as a general purpose register.
  BitVector ReserveXRegister;

  // ReserveXRegisterForRA[i] - X#i is not available for register allocator.
  BitVector ReserveXRegisterForRA;

  // CustomCallUsedXRegister[i] - X#i call saved.
  BitVector CustomCallSavedXRegs;

  bool IsLittle;

  bool IsStreaming;
  bool IsStreamingCompatible;
  unsigned MinSVEVectorSizeInBits;
  unsigned MaxSVEVectorSizeInBits;
  unsigned VScaleForTuning = 2;
  TailFoldingOpts DefaultSVETFOpts = TailFoldingOpts::Disabled;

  /// TargetTriple - What processor and OS we're targeting.
  Triple TargetTriple;

  AArch64FrameLowering FrameLowering;
  AArch64InstrInfo InstrInfo;
  AArch64SelectionDAGInfo TSInfo;
  AArch64TargetLowering TLInfo;

  /// GlobalISel related APIs.
  std::unique_ptr<CallLowering> CallLoweringInfo;
  std::unique_ptr<InlineAsmLowering> InlineAsmLoweringInfo;
  std::unique_ptr<InstructionSelector> InstSelector;
  std::unique_ptr<LegalizerInfo> Legalizer;
  std::unique_ptr<RegisterBankInfo> RegBankInfo;

private:
  /// initializeSubtargetDependencies - Initializes using CPUString and the
  /// passed in feature string so that we can use initializer lists for
  /// subtarget initialization.
  AArch64Subtarget &initializeSubtargetDependencies(StringRef FS,
                                                    StringRef CPUString,
                                                    StringRef TuneCPUString,
                                                    bool HasMinSize);

  /// Initialize properties based on the selected processor family.
  void initializeProperties(bool HasMinSize);

public:
  /// This constructor initializes the data members to match that
  /// of the specified triple.
  AArch64Subtarget(const Triple &TT, StringRef CPU, StringRef TuneCPU,
                   StringRef FS, const TargetMachine &TM, bool LittleEndian,
                   unsigned MinSVEVectorSizeInBitsOverride = 0,
                   unsigned MaxSVEVectorSizeInBitsOverride = 0,
                   bool IsStreaming = false, bool IsStreamingCompatible = false,
                   bool HasMinSize = false);

// Getters for SubtargetFeatures defined in tablegen
#define GET_SUBTARGETINFO_MACRO(ATTRIBUTE, DEFAULT, GETTER)                    \
  bool GETTER() const { return ATTRIBUTE; }
#include "AArch64GenSubtargetInfo.inc"

  const AArch64SelectionDAGInfo *getSelectionDAGInfo() const override {
    return &TSInfo;
  }
  const AArch64FrameLowering *getFrameLowering() const override {
    return &FrameLowering;
  }
  const AArch64TargetLowering *getTargetLowering() const override {
    return &TLInfo;
  }
  const AArch64InstrInfo *getInstrInfo() const override { return &InstrInfo; }
  const AArch64RegisterInfo *getRegisterInfo() const override {
    return &getInstrInfo()->getRegisterInfo();
  }
  const CallLowering *getCallLowering() const override;
  const InlineAsmLowering *getInlineAsmLowering() const override;
  InstructionSelector *getInstructionSelector() const override;
  const LegalizerInfo *getLegalizerInfo() const override;
  const RegisterBankInfo *getRegBankInfo() const override;
  const Triple &getTargetTriple() const { return TargetTriple; }
  bool enableMachineScheduler() const override { return true; }
  bool enablePostRAScheduler() const override { return usePostRAScheduler(); }

  bool enableMachinePipeliner() const override;
  bool useDFAforSMS() const override { return false; }

  /// Returns ARM processor family.
  /// Avoid this function! CPU specifics should be kept local to this class
  /// and preferably modeled with SubtargetFeatures or properties in
  /// initializeProperties().
  ARMProcFamilyEnum getProcFamily() const {
    return ARMProcFamily;
  }

  bool isXRaySupported() const override { return true; }

  /// Returns true if the function has a streaming body.
  bool isStreaming() const { return IsStreaming; }

  /// Returns true if the function has a streaming-compatible body.
  bool isStreamingCompatible() const { return IsStreamingCompatible; }

  /// Returns true if the target has NEON and the function at runtime is known
  /// to have NEON enabled (e.g. the function is known not to be in streaming-SVE
  /// mode, which disables NEON instructions).
  bool isNeonAvailable() const {
    return hasNEON() &&
           (hasSMEFA64() || (!isStreaming() && !isStreamingCompatible()));
  }

  /// Returns true if the target has SVE and can use the full range of SVE
  /// instructions, for example because it knows the function is known not to be
  /// in streaming-SVE mode or when the target has FEAT_FA64 enabled.
  bool isSVEAvailable() const {
    return hasSVE() &&
           (hasSMEFA64() || (!isStreaming() && !isStreamingCompatible()));
  }

  /// Returns true if the target has access to either the full range of SVE instructions,
  /// or the streaming-compatible subset of SVE instructions.
  bool isSVEorStreamingSVEAvailable() const {
    return hasSVE() || (hasSME() && isStreaming());
  }

  unsigned getMinVectorRegisterBitWidth() const {
    // Don't assume any minimum vector size when PSTATE.SM may not be 0, because
    // we don't yet support streaming-compatible codegen support that we trust
    // is safe for functions that may be executed in streaming-SVE mode.
    // By returning '0' here, we disable vectorization.
    if (!isSVEAvailable() && !isNeonAvailable())
      return 0;
    return MinVectorRegisterBitWidth;
  }

  bool isXRegisterReserved(size_t i) const { return ReserveXRegister[i]; }
  bool isXRegisterReservedForRA(size_t i) const { return ReserveXRegisterForRA[i]; }
  unsigned getNumXRegisterReserved() const {
    BitVector AllReservedX(AArch64::GPR64commonRegClass.getNumRegs());
    AllReservedX |= ReserveXRegister;
    AllReservedX |= ReserveXRegisterForRA;
    return AllReservedX.count();
  }
  bool isLRReservedForRA() const { return ReserveLRForRA; }
  bool isXRegCustomCalleeSaved(size_t i) const {
    return CustomCallSavedXRegs[i];
  }
  bool hasCustomCallingConv() const { return CustomCallSavedXRegs.any(); }

  /// Return true if the CPU supports any kind of instruction fusion.
  bool hasFusion() const {
    return hasArithmeticBccFusion() || hasArithmeticCbzFusion() ||
           hasFuseAES() || hasFuseArithmeticLogic() || hasFuseCCSelect() ||
           hasFuseAdrpAdd() || hasFuseLiterals();
  }

  unsigned getMaxInterleaveFactor() const { return MaxInterleaveFactor; }
  unsigned getVectorInsertExtractBaseCost() const;
  unsigned getCacheLineSize() const override { return CacheLineSize; }
  unsigned getPrefetchDistance() const override { return PrefetchDistance; }
  unsigned getMinPrefetchStride(unsigned NumMemAccesses,
                                unsigned NumStridedMemAccesses,
                                unsigned NumPrefetches,
                                bool HasCall) const override {
    return MinPrefetchStride;
  }
  unsigned getMaxPrefetchIterationsAhead() const override {
    return MaxPrefetchIterationsAhead;
  }
  Align getPrefFunctionAlignment() const {
    return PrefFunctionAlignment;
  }
  Align getPrefLoopAlignment() const { return PrefLoopAlignment; }

  unsigned getMaxBytesForLoopAlignment() const {
    return MaxBytesForLoopAlignment;
  }

  unsigned getMaximumJumpTableSize() const { return MaxJumpTableSize; }
  unsigned getMinimumJumpTableEntries() const {
    return MinimumJumpTableEntries;
  }

  /// CPU has TBI (top byte of addresses is ignored during HW address
  /// translation) and OS enables it.
  bool supportsAddressTopByteIgnored() const;

  bool isLittleEndian() const { return IsLittle; }

  bool isTargetDarwin() const { return TargetTriple.isOSDarwin(); }
  bool isTargetIOS() const { return TargetTriple.isiOS(); }
  bool isTargetLinux() const { return TargetTriple.isOSLinux(); }
  bool isTargetOpenBSD() const { return TargetTriple.isOSOpenBSD(); }
  bool isTargetWindows() const { return TargetTriple.isOSWindows(); }
  bool isTargetAndroid() const { return TargetTriple.isAndroid(); }
  bool isTargetFuchsia() const { return TargetTriple.isOSFuchsia(); }
  bool isWindowsArm64EC() const { return TargetTriple.isWindowsArm64EC(); }

  bool isTargetCOFF() const { return TargetTriple.isOSBinFormatCOFF(); }
  bool isTargetELF() const { return TargetTriple.isOSBinFormatELF(); }
  bool isTargetMachO() const { return TargetTriple.isOSBinFormatMachO(); }

  bool isTargetILP32() const {
    return TargetTriple.isArch32Bit() ||
           TargetTriple.getEnvironment() == Triple::GNUILP32;
  }

  bool useAA() const override;

  bool addrSinkUsingGEPs() const override {
    // Keeping GEPs inbounds is important for exploiting AArch64
    // addressing-modes in ILP32 mode.
    return useAA() || isTargetILP32();
  }

  bool useSmallAddressing() const {
    switch (TLInfo.getTargetMachine().getCodeModel()) {
      case CodeModel::Kernel:
        // Kernel is currently allowed only for Fuchsia targets,
        // where it is the same as Small for almost all purposes.
      case CodeModel::Small:
        return true;
      default:
        return false;
    }
  }

  /// ParseSubtargetFeatures - Parses features string setting specified
  /// subtarget options.  Definition of function is auto generated by tblgen.
  void ParseSubtargetFeatures(StringRef CPU, StringRef TuneCPU, StringRef FS);

  /// ClassifyGlobalReference - Find the target operand flags that describe
  /// how a global value should be referenced for the current subtarget.
  unsigned ClassifyGlobalReference(const GlobalValue *GV,
                                   const TargetMachine &TM) const;

  unsigned classifyGlobalFunctionReference(const GlobalValue *GV,
                                           const TargetMachine &TM) const;

  /// This function is design to compatible with the function def in other
  /// targets and escape build error about the virtual function def in base
  /// class TargetSubtargetInfo. Updeate me if AArch64 target need to use it.
  unsigned char
  classifyGlobalFunctionReference(const GlobalValue *GV) const override {
    return 0;
  }

  void overrideSchedPolicy(MachineSchedPolicy &Policy,
                           unsigned NumRegionInstrs) const override;
  void adjustSchedDependency(SUnit *Def, int DefOpIdx, SUnit *Use, int UseOpIdx,
                             SDep &Dep,
                             const TargetSchedModel *SchedModel) const override;

  bool enableEarlyIfConversion() const override;

  std::unique_ptr<PBQPRAConstraint> getCustomPBQPConstraints() const override;

  bool isCallingConvWin64(CallingConv::ID CC, bool IsVarArg) const {
    switch (CC) {
    case CallingConv::C:
    case CallingConv::Fast:
    case CallingConv::Swift:
    case CallingConv::SwiftTail:
      return isTargetWindows();
    case CallingConv::PreserveNone:
      return IsVarArg && isTargetWindows();
    case CallingConv::Win64:
      return true;
    default:
      return false;
    }
  }

  /// Return whether FrameLowering should always set the "extended frame
  /// present" bit in FP, or set it based on a symbol in the runtime.
  bool swiftAsyncContextIsDynamicallySet() const {
    // Older OS versions (particularly system unwinders) are confused by the
    // Swift extended frame, so when building code that might be run on them we
    // must dynamically query the concurrency library to determine whether
    // extended frames should be flagged as present.
    const Triple &TT = getTargetTriple();

    unsigned Major = TT.getOSVersion().getMajor();
    switch(TT.getOS()) {
    default:
      return false;
    case Triple::IOS:
    case Triple::TvOS:
      return Major < 15;
    case Triple::WatchOS:
      return Major < 8;
    case Triple::MacOSX:
    case Triple::Darwin:
      return Major < 12;
    }
  }

  void mirFileLoaded(MachineFunction &MF) const override;

  // Return the known range for the bit length of SVE data registers. A value
  // of 0 means nothing is known about that particular limit beyong what's
  // implied by the architecture.
  unsigned getMaxSVEVectorSizeInBits() const {
    assert(isSVEorStreamingSVEAvailable() &&
           "Tried to get SVE vector length without SVE support!");
    return MaxSVEVectorSizeInBits;
  }

  unsigned getMinSVEVectorSizeInBits() const {
    assert(isSVEorStreamingSVEAvailable() &&
           "Tried to get SVE vector length without SVE support!");
    return MinSVEVectorSizeInBits;
  }

  bool useSVEForFixedLengthVectors() const {
    if (!isSVEorStreamingSVEAvailable())
      return false;

    // Prefer NEON unless larger SVE registers are available.
    return !isNeonAvailable() || getMinSVEVectorSizeInBits() >= 256;
  }

  bool useSVEForFixedLengthVectors(EVT VT) const {
    if (!useSVEForFixedLengthVectors() || !VT.isFixedLengthVector())
      return false;
    return VT.getFixedSizeInBits() > AArch64::SVEBitsPerBlock ||
           !isNeonAvailable();
  }

  unsigned getVScaleForTuning() const { return VScaleForTuning; }

  TailFoldingOpts getSVETailFoldingDefaultOpts() const {
    return DefaultSVETFOpts;
  }

  const char* getChkStkName() const {
    if (isWindowsArm64EC())
      return "#__chkstk_arm64ec";
    return "__chkstk";
  }

  const char* getSecurityCheckCookieName() const {
    if (isWindowsArm64EC())
      return "#__security_check_cookie_arm64ec";
    return "__security_check_cookie";
  }

  /// Choose a method of checking LR before performing a tail call.
  AArch64PAuth::AuthCheckMethod
  getAuthenticatedLRCheckMethod(const MachineFunction &MF) const;

  /// Compute the integer discriminator for a given BlockAddress constant, if
  /// blockaddress signing is enabled, or std::nullopt otherwise.
  /// Blockaddress signing is controlled by the function attribute
  /// "ptrauth-indirect-gotos" on the parent function.
  /// Note that this assumes the discriminator is independent of the indirect
  /// goto branch site itself, i.e., it's the same for all BlockAddresses in
  /// a function.
  std::optional<uint16_t>
  getPtrAuthBlockAddressDiscriminatorIfEnabled(const Function &ParentFn) const;

  const PseudoSourceValue *getAddressCheckPSV() const {
    return AddressCheckPSV.get();
  }

private:
  /// Pseudo value representing memory load performed to check an address.
  ///
  /// This load operation is solely used for its side-effects: if the address
  /// is not mapped (or not readable), it triggers CPU exception, otherwise
  /// execution proceeds and the value is not used.
  class AddressCheckPseudoSourceValue : public PseudoSourceValue {
  public:
    AddressCheckPseudoSourceValue(const TargetMachine &TM)
        : PseudoSourceValue(TargetCustom, TM) {}

    bool isConstant(const MachineFrameInfo *) const override { return false; }
    bool isAliased(const MachineFrameInfo *) const override { return true; }
    bool mayAlias(const MachineFrameInfo *) const override { return true; }
    void printCustom(raw_ostream &OS) const override { OS << "AddressCheck"; }
  };

  std::unique_ptr<AddressCheckPseudoSourceValue> AddressCheckPSV;
};
} // End llvm namespace

#endif
