//===- DataFlowSanitizer.h - dynamic data flow analysis -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_DATAFLOWSANITIZER_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_DATAFLOWSANITIZER_H

#include "llvm/IR/PassManager.h"
#include <string>
#include <vector>

namespace llvm {
class Module;

class DataFlowSanitizerPass : public PassInfoMixin<DataFlowSanitizerPass> {
private:
  std::vector<std::string> ABIListFiles;

public:
  DataFlowSanitizerPass(
      const std::vector<std::string> &ABIListFiles = std::vector<std::string>())
      : ABIListFiles(ABIListFiles) {}
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  static bool isRequired() { return true; }
};

} // namespace llvm

#endif
