//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AMDGPUArgumentUsageInfo.h"
#include "AMDGPU.h"
#include "AMDGPUTargetMachine.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "SIRegisterInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/NativeFormatting.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "amdgpu-argument-reg-usage-info"

INITIALIZE_PASS(AMDGPUArgumentUsageInfo, DEBUG_TYPE,
                "Argument Register Usage Information Storage", false, true)

void ArgDescriptor::print(raw_ostream &OS,
                          const TargetRegisterInfo *TRI) const {
  if (!isSet()) {
    OS << "<not set>\n";
    return;
  }

  if (isRegister())
    OS << "Reg " << printReg(getRegister(), TRI);
  else
    OS << "Stack offset " << getStackOffset();

  if (isMasked()) {
    OS << " & ";
    llvm::write_hex(OS, Mask, llvm::HexPrintStyle::PrefixLower);
  }

  OS << '\n';
}

char AMDGPUArgumentUsageInfo::ID = 0;

const AMDGPUFunctionArgInfo AMDGPUArgumentUsageInfo::ExternFunctionInfo{};

// Hardcoded registers from fixed function ABI
const AMDGPUFunctionArgInfo AMDGPUArgumentUsageInfo::FixedABIFunctionInfo
  = AMDGPUFunctionArgInfo::fixedABILayout();

bool AMDGPUArgumentUsageInfo::doInitialization(Module &M) {
  return false;
}

bool AMDGPUArgumentUsageInfo::doFinalization(Module &M) {
  ArgInfoMap.clear();
  return false;
}

// TODO: Print preload kernargs?
void AMDGPUArgumentUsageInfo::print(raw_ostream &OS, const Module *M) const {
  for (const auto &FI : ArgInfoMap) {
    OS << "Arguments for " << FI.first->getName() << '\n'
       << "  PrivateSegmentBuffer: " << FI.second.PrivateSegmentBuffer
       << "  DispatchPtr: " << FI.second.DispatchPtr
       << "  QueuePtr: " << FI.second.QueuePtr
       << "  KernargSegmentPtr: " << FI.second.KernargSegmentPtr
       << "  DispatchID: " << FI.second.DispatchID
       << "  FlatScratchInit: " << FI.second.FlatScratchInit
       << "  PrivateSegmentSize: " << FI.second.PrivateSegmentSize
       << "  WorkGroupIDX: " << FI.second.WorkGroupIDX
       << "  WorkGroupIDY: " << FI.second.WorkGroupIDY
       << "  WorkGroupIDZ: " << FI.second.WorkGroupIDZ
       << "  WorkGroupInfo: " << FI.second.WorkGroupInfo
       << "  LDSKernelId: " << FI.second.LDSKernelId
       << "  PrivateSegmentWaveByteOffset: "
          << FI.second.PrivateSegmentWaveByteOffset
       << "  ImplicitBufferPtr: " << FI.second.ImplicitBufferPtr
       << "  ImplicitArgPtr: " << FI.second.ImplicitArgPtr
       << "  WorkItemIDX " << FI.second.WorkItemIDX
       << "  WorkItemIDY " << FI.second.WorkItemIDY
       << "  WorkItemIDZ " << FI.second.WorkItemIDZ
       << '\n';
  }
}

std::tuple<const ArgDescriptor *, const TargetRegisterClass *, LLT>
AMDGPUFunctionArgInfo::getPreloadedValue(
    AMDGPUFunctionArgInfo::PreloadedValue Value) const {
  switch (Value) {
  case AMDGPUFunctionArgInfo::PRIVATE_SEGMENT_BUFFER: {
    return std::tuple(PrivateSegmentBuffer ? &PrivateSegmentBuffer : nullptr,
                      &AMDGPU::SGPR_128RegClass, LLT::fixed_vector(4, 32));
  }
  case AMDGPUFunctionArgInfo::IMPLICIT_BUFFER_PTR:
    return std::tuple(ImplicitBufferPtr ? &ImplicitBufferPtr : nullptr,
                      &AMDGPU::SGPR_64RegClass,
                      LLT::pointer(AMDGPUAS::CONSTANT_ADDRESS, 64));
  case AMDGPUFunctionArgInfo::WORKGROUP_ID_X:
    return std::tuple(WorkGroupIDX ? &WorkGroupIDX : nullptr,
                      &AMDGPU::SGPR_32RegClass, LLT::scalar(32));
  case AMDGPUFunctionArgInfo::WORKGROUP_ID_Y:
    return std::tuple(WorkGroupIDY ? &WorkGroupIDY : nullptr,
                      &AMDGPU::SGPR_32RegClass, LLT::scalar(32));
  case AMDGPUFunctionArgInfo::WORKGROUP_ID_Z:
    return std::tuple(WorkGroupIDZ ? &WorkGroupIDZ : nullptr,
                      &AMDGPU::SGPR_32RegClass, LLT::scalar(32));
  case AMDGPUFunctionArgInfo::LDS_KERNEL_ID:
    return std::tuple(LDSKernelId ? &LDSKernelId : nullptr,
                      &AMDGPU::SGPR_32RegClass, LLT::scalar(32));
  case AMDGPUFunctionArgInfo::PRIVATE_SEGMENT_WAVE_BYTE_OFFSET:
    return std::tuple(
        PrivateSegmentWaveByteOffset ? &PrivateSegmentWaveByteOffset : nullptr,
        &AMDGPU::SGPR_32RegClass, LLT::scalar(32));
  case AMDGPUFunctionArgInfo::PRIVATE_SEGMENT_SIZE:
    return {PrivateSegmentSize ? &PrivateSegmentSize : nullptr,
            &AMDGPU::SGPR_32RegClass, LLT::scalar(32)};
  case AMDGPUFunctionArgInfo::KERNARG_SEGMENT_PTR:
    return std::tuple(KernargSegmentPtr ? &KernargSegmentPtr : nullptr,
                      &AMDGPU::SGPR_64RegClass,
                      LLT::pointer(AMDGPUAS::CONSTANT_ADDRESS, 64));
  case AMDGPUFunctionArgInfo::IMPLICIT_ARG_PTR:
    return std::tuple(ImplicitArgPtr ? &ImplicitArgPtr : nullptr,
                      &AMDGPU::SGPR_64RegClass,
                      LLT::pointer(AMDGPUAS::CONSTANT_ADDRESS, 64));
  case AMDGPUFunctionArgInfo::DISPATCH_ID:
    return std::tuple(DispatchID ? &DispatchID : nullptr,
                      &AMDGPU::SGPR_64RegClass, LLT::scalar(64));
  case AMDGPUFunctionArgInfo::FLAT_SCRATCH_INIT:
    return std::tuple(FlatScratchInit ? &FlatScratchInit : nullptr,
                      &AMDGPU::SGPR_64RegClass, LLT::scalar(64));
  case AMDGPUFunctionArgInfo::DISPATCH_PTR:
    return std::tuple(DispatchPtr ? &DispatchPtr : nullptr,
                      &AMDGPU::SGPR_64RegClass,
                      LLT::pointer(AMDGPUAS::CONSTANT_ADDRESS, 64));
  case AMDGPUFunctionArgInfo::QUEUE_PTR:
    return std::tuple(QueuePtr ? &QueuePtr : nullptr, &AMDGPU::SGPR_64RegClass,
                      LLT::pointer(AMDGPUAS::CONSTANT_ADDRESS, 64));
  case AMDGPUFunctionArgInfo::WORKITEM_ID_X:
    return std::tuple(WorkItemIDX ? &WorkItemIDX : nullptr,
                      &AMDGPU::VGPR_32RegClass, LLT::scalar(32));
  case AMDGPUFunctionArgInfo::WORKITEM_ID_Y:
    return std::tuple(WorkItemIDY ? &WorkItemIDY : nullptr,
                      &AMDGPU::VGPR_32RegClass, LLT::scalar(32));
  case AMDGPUFunctionArgInfo::WORKITEM_ID_Z:
    return std::tuple(WorkItemIDZ ? &WorkItemIDZ : nullptr,
                      &AMDGPU::VGPR_32RegClass, LLT::scalar(32));
  }
  llvm_unreachable("unexpected preloaded value type");
}

AMDGPUFunctionArgInfo AMDGPUFunctionArgInfo::fixedABILayout() {
  AMDGPUFunctionArgInfo AI;
  AI.PrivateSegmentBuffer
    = ArgDescriptor::createRegister(AMDGPU::SGPR0_SGPR1_SGPR2_SGPR3);
  AI.DispatchPtr = ArgDescriptor::createRegister(AMDGPU::SGPR4_SGPR5);
  AI.QueuePtr = ArgDescriptor::createRegister(AMDGPU::SGPR6_SGPR7);

  // Do not pass kernarg segment pointer, only pass increment version in its
  // place.
  AI.ImplicitArgPtr = ArgDescriptor::createRegister(AMDGPU::SGPR8_SGPR9);
  AI.DispatchID = ArgDescriptor::createRegister(AMDGPU::SGPR10_SGPR11);

  // Skip FlatScratchInit/PrivateSegmentSize
  AI.WorkGroupIDX = ArgDescriptor::createRegister(AMDGPU::SGPR12);
  AI.WorkGroupIDY = ArgDescriptor::createRegister(AMDGPU::SGPR13);
  AI.WorkGroupIDZ = ArgDescriptor::createRegister(AMDGPU::SGPR14);
  AI.LDSKernelId = ArgDescriptor::createRegister(AMDGPU::SGPR15);

  const unsigned Mask = 0x3ff;
  AI.WorkItemIDX = ArgDescriptor::createRegister(AMDGPU::VGPR31, Mask);
  AI.WorkItemIDY = ArgDescriptor::createRegister(AMDGPU::VGPR31, Mask << 10);
  AI.WorkItemIDZ = ArgDescriptor::createRegister(AMDGPU::VGPR31, Mask << 20);
  return AI;
}

const AMDGPUFunctionArgInfo &
AMDGPUArgumentUsageInfo::lookupFuncArgInfo(const Function &F) const {
  auto I = ArgInfoMap.find(&F);
  if (I == ArgInfoMap.end())
    return FixedABIFunctionInfo;
  return I->second;
}
