//===-- vfabi-demangler-fuzzer.cpp - Fuzzer VFABI using lib/Fuzzer   ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Build tool to fuzz the demangler for the vector function ABI names.
//
//===----------------------------------------------------------------------===//

#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/VFABIDemangler.h"
#include "llvm/Support/SourceMgr.h"

using namespace llvm;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  LLVMContext Ctx;
  SMDiagnostic Err;
  const std::unique_ptr<Module> M =
      parseAssemblyString("declare i32 @foo(i32 )\n", Err, Ctx);
  const StringRef MangledName((const char *)Data, Size);
  // Make sure that whatever symbol the demangler is operating on is
  // present in the module (the signature is not important). This is
  // because `tryDemangleForVFABI` fails if the function is not
  // present. We need to make sure we can even invoke
  // `getOrInsertFunction` because such method asserts on strings with
  // zeroes.
  // TODO: What is this actually testing? That we don't crash?
  if (!MangledName.empty() && MangledName.find_first_of(0) == StringRef::npos) {
    FunctionType *FTy =
        FunctionType::get(Type::getVoidTy(M->getContext()), false);
    const auto Info = VFABI::tryDemangleForVFABI(MangledName, FTy);

    // Do not optimize away the return value. Inspired by
    // https://github.com/google/benchmark/blob/main/include/benchmark/benchmark.h#L307-L345
    asm volatile("" : : "r,m"(Info) : "memory");
  }

  return 0;
}
