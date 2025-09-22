//===--------- Definition of the AddressSanitizer class ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the AddressSanitizer class which is a port of the legacy
// AddressSanitizer pass to use the new PassManager infrastructure.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_ADDRESSSANITIZER_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_ADDRESSSANITIZER_H

#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Instrumentation/AddressSanitizerOptions.h"

namespace llvm {
class Module;
class raw_ostream;

struct AddressSanitizerOptions {
  bool CompileKernel = false;
  bool Recover = false;
  bool UseAfterScope = false;
  AsanDetectStackUseAfterReturnMode UseAfterReturn =
      AsanDetectStackUseAfterReturnMode::Runtime;
  int InstrumentationWithCallsThreshold = 7000;
  uint32_t MaxInlinePoisoningSize = 64;
  bool InsertVersionCheck = true;
};

/// Public interface to the address sanitizer module pass for instrumenting code
/// to check for various memory errors.
///
/// This adds 'asan.module_ctor' to 'llvm.global_ctors'. This pass may also
/// run intependently of the function address sanitizer.
class AddressSanitizerPass : public PassInfoMixin<AddressSanitizerPass> {
public:
  AddressSanitizerPass(const AddressSanitizerOptions &Options,
                       bool UseGlobalGC = true, bool UseOdrIndicator = true,
                       AsanDtorKind DestructorKind = AsanDtorKind::Global,
                       AsanCtorKind ConstructorKind = AsanCtorKind::Global);
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  void printPipeline(raw_ostream &OS,
                     function_ref<StringRef(StringRef)> MapClassName2PassName);
  static bool isRequired() { return true; }

private:
  AddressSanitizerOptions Options;
  bool UseGlobalGC;
  bool UseOdrIndicator;
  AsanDtorKind DestructorKind;
  AsanCtorKind ConstructorKind;
};

struct ASanAccessInfo {
  const int32_t Packed;
  const uint8_t AccessSizeIndex;
  const bool IsWrite;
  const bool CompileKernel;

  explicit ASanAccessInfo(int32_t Packed);
  ASanAccessInfo(bool IsWrite, bool CompileKernel, uint8_t AccessSizeIndex);
};

} // namespace llvm

#endif
