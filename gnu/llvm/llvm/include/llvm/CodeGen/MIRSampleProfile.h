//===----- MIRSampleProfile.h: SampleFDO Support in MIR ---*- c++ -*-------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the supoorting functions for machine level Sample FDO
// loader. This is used in Flow Sensitive SampelFDO.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MIRSAMPLEPROFILE_H
#define LLVM_CODEGEN_MIRSAMPLEPROFILE_H

#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/Support/Discriminator.h"
#include <memory>
#include <string>

namespace llvm {
class AnalysisUsage;
class MachineBlockFrequencyInfo;
class MachineFunction;
class Module;

namespace vfs {
class FileSystem;
} // namespace vfs

using namespace sampleprof;

class MIRProfileLoader;
class MIRProfileLoaderPass : public MachineFunctionPass {
  MachineFunction *MF;
  std::string ProfileFileName;
  FSDiscriminatorPass P;
  unsigned LowBit;
  unsigned HighBit;

public:
  static char ID;
  /// FS bits will only use the '1' bits in the Mask.
  MIRProfileLoaderPass(std::string FileName = "",
                       std::string RemappingFileName = "",
                       FSDiscriminatorPass P = FSDiscriminatorPass::Pass1,
                       IntrusiveRefCntPtr<vfs::FileSystem> FS = nullptr);

  /// getMachineFunction - Return the last machine function computed.
  const MachineFunction *getMachineFunction() const { return MF; }

  StringRef getPassName() const override { return "SampleFDO loader in MIR"; }

private:
  void init(MachineFunction &MF);
  bool runOnMachineFunction(MachineFunction &) override;
  bool doInitialization(Module &M) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  std::unique_ptr<MIRProfileLoader> MIRSampleLoader;
  /// Hold the information of the basic block frequency.
  MachineBlockFrequencyInfo *MBFI;
};

} // namespace llvm

#endif // LLVM_CODEGEN_MIRSAMPLEPROFILE_H
