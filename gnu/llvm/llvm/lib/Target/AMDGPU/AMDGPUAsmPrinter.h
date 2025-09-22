//===-- AMDGPUAsmPrinter.h - Print AMDGPU assembly code ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// AMDGPU Assembly printer class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_AMDGPUASMPRINTER_H
#define LLVM_LIB_TARGET_AMDGPU_AMDGPUASMPRINTER_H

#include "SIProgramInfo.h"
#include "llvm/CodeGen/AsmPrinter.h"

namespace llvm {

class AMDGPUMachineFunction;
struct AMDGPUResourceUsageAnalysis;
class AMDGPUTargetStreamer;
class MCCodeEmitter;
class MCOperand;

namespace AMDGPU {
struct MCKernelDescriptor;
struct AMDGPUMCKernelCodeT;
namespace HSAMD {
class MetadataStreamer;
}
} // namespace AMDGPU

class AMDGPUAsmPrinter final : public AsmPrinter {
private:
  unsigned CodeObjectVersion;
  void initializeTargetID(const Module &M);

  AMDGPUResourceUsageAnalysis *ResourceUsage;

  SIProgramInfo CurrentProgramInfo;

  std::unique_ptr<AMDGPU::HSAMD::MetadataStreamer> HSAMetadataStream;

  MCCodeEmitter *DumpCodeInstEmitter = nullptr;

  uint64_t getFunctionCodeSize(const MachineFunction &MF) const;

  void getSIProgramInfo(SIProgramInfo &Out, const MachineFunction &MF);
  void getAmdKernelCode(AMDGPU::AMDGPUMCKernelCodeT &Out,
                        const SIProgramInfo &KernelInfo,
                        const MachineFunction &MF) const;

  /// Emit register usage information so that the GPU driver
  /// can correctly setup the GPU state.
  void EmitProgramInfoSI(const MachineFunction &MF,
                         const SIProgramInfo &KernelInfo);
  void EmitPALMetadata(const MachineFunction &MF,
                       const SIProgramInfo &KernelInfo);
  void emitPALFunctionMetadata(const MachineFunction &MF);
  void emitCommonFunctionComments(uint32_t NumVGPR,
                                  std::optional<uint32_t> NumAGPR,
                                  uint32_t TotalNumVGPR, uint32_t NumSGPR,
                                  uint64_t ScratchSize, uint64_t CodeSize,
                                  const AMDGPUMachineFunction *MFI);
  void emitCommonFunctionComments(const MCExpr *NumVGPR, const MCExpr *NumAGPR,
                                  const MCExpr *TotalNumVGPR,
                                  const MCExpr *NumSGPR,
                                  const MCExpr *ScratchSize, uint64_t CodeSize,
                                  const AMDGPUMachineFunction *MFI);
  void emitResourceUsageRemarks(const MachineFunction &MF,
                                const SIProgramInfo &CurrentProgramInfo,
                                bool isModuleEntryFunction, bool hasMAIInsts);

  const MCExpr *getAmdhsaKernelCodeProperties(const MachineFunction &MF) const;

  AMDGPU::MCKernelDescriptor
  getAmdhsaKernelDescriptor(const MachineFunction &MF,
                            const SIProgramInfo &PI) const;

  void initTargetStreamer(Module &M);

  SmallString<128> getMCExprStr(const MCExpr *Value);

public:
  explicit AMDGPUAsmPrinter(TargetMachine &TM,
                            std::unique_ptr<MCStreamer> Streamer);

  StringRef getPassName() const override;

  const MCSubtargetInfo* getGlobalSTI() const;

  AMDGPUTargetStreamer* getTargetStreamer() const;

  bool doInitialization(Module &M) override;
  bool doFinalization(Module &M) override;
  bool runOnMachineFunction(MachineFunction &MF) override;

  /// Wrapper for MCInstLowering.lowerOperand() for the tblgen'erated
  /// pseudo lowering.
  bool lowerOperand(const MachineOperand &MO, MCOperand &MCOp) const;

  /// Lower the specified LLVM Constant to an MCExpr.
  /// The AsmPrinter::lowerConstantof does not know how to lower
  /// addrspacecast, therefore they should be lowered by this function.
  const MCExpr *lowerConstant(const Constant *CV) override;

  /// tblgen'erated driver function for lowering simple MI->MC pseudo
  /// instructions.
  bool emitPseudoExpansionLowering(MCStreamer &OutStreamer,
                                   const MachineInstr *MI);

  /// Implemented in AMDGPUMCInstLower.cpp
  void emitInstruction(const MachineInstr *MI) override;

  void emitFunctionBodyStart() override;

  void emitFunctionBodyEnd() override;

  void emitImplicitDef(const MachineInstr *MI) const override;

  void emitFunctionEntryLabel() override;

  void emitBasicBlockStart(const MachineBasicBlock &MBB) override;

  void emitGlobalVariable(const GlobalVariable *GV) override;

  void emitStartOfAsmFile(Module &M) override;

  void emitEndOfAsmFile(Module &M) override;

  bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                       const char *ExtraCode, raw_ostream &O) override;

protected:
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  std::vector<std::string> DisasmLines, HexLines;
  size_t DisasmLineMaxLen;
  bool IsTargetStreamerInitialized;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_AMDGPU_AMDGPUASMPRINTER_H
