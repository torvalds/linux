//===--- SPIRVMetadata.cpp ---- IR Metadata Parsing Funcs -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains functions needed for parsing LLVM IR metadata relevant
// to the SPIR-V target.
//
//===----------------------------------------------------------------------===//

#include "SPIRVMetadata.h"

using namespace llvm;

static MDString *getOCLKernelArgAttribute(const Function &F, unsigned ArgIdx,
                                          const StringRef AttributeName) {
  assert(
      F.getCallingConv() == CallingConv::SPIR_KERNEL &&
      "Kernel attributes are attached/belong only to OpenCL kernel functions");

  // Lookup the argument attribute in metadata attached to the kernel function.
  MDNode *Node = F.getMetadata(AttributeName);
  if (Node && ArgIdx < Node->getNumOperands())
    return cast<MDString>(Node->getOperand(ArgIdx));

  // Sometimes metadata containing kernel attributes is not attached to the
  // function, but can be found in the named module-level metadata instead.
  // For example:
  //   !opencl.kernels = !{!0}
  //   !0 = !{void ()* @someKernelFunction, !1, ...}
  //   !1 = !{!"kernel_arg_addr_space", ...}
  // In this case the actual index of searched argument attribute is ArgIdx + 1,
  // since the first metadata node operand is occupied by attribute name
  // ("kernel_arg_addr_space" in the example above).
  unsigned MDArgIdx = ArgIdx + 1;
  NamedMDNode *OpenCLKernelsMD =
      F.getParent()->getNamedMetadata("opencl.kernels");
  if (!OpenCLKernelsMD || OpenCLKernelsMD->getNumOperands() == 0)
    return nullptr;

  // KernelToMDNodeList contains kernel function declarations followed by
  // corresponding MDNodes for each attribute. Search only MDNodes "belonging"
  // to the currently lowered kernel function.
  MDNode *KernelToMDNodeList = OpenCLKernelsMD->getOperand(0);
  bool FoundLoweredKernelFunction = false;
  for (const MDOperand &Operand : KernelToMDNodeList->operands()) {
    ValueAsMetadata *MaybeValue = dyn_cast<ValueAsMetadata>(Operand);
    if (MaybeValue &&
        dyn_cast<Function>(MaybeValue->getValue())->getName() == F.getName()) {
      FoundLoweredKernelFunction = true;
      continue;
    }
    if (MaybeValue && FoundLoweredKernelFunction)
      return nullptr;

    MDNode *MaybeNode = dyn_cast<MDNode>(Operand);
    if (FoundLoweredKernelFunction && MaybeNode &&
        cast<MDString>(MaybeNode->getOperand(0))->getString() ==
            AttributeName &&
        MDArgIdx < MaybeNode->getNumOperands())
      return cast<MDString>(MaybeNode->getOperand(MDArgIdx));
  }
  return nullptr;
}

namespace llvm {

MDString *getOCLKernelArgAccessQual(const Function &F, unsigned ArgIdx) {
  assert(
      F.getCallingConv() == CallingConv::SPIR_KERNEL &&
      "Kernel attributes are attached/belong only to OpenCL kernel functions");
  return getOCLKernelArgAttribute(F, ArgIdx, "kernel_arg_access_qual");
}

MDString *getOCLKernelArgTypeQual(const Function &F, unsigned ArgIdx) {
  assert(
      F.getCallingConv() == CallingConv::SPIR_KERNEL &&
      "Kernel attributes are attached/belong only to OpenCL kernel functions");
  return getOCLKernelArgAttribute(F, ArgIdx, "kernel_arg_type_qual");
}

} // namespace llvm
