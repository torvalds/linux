//===----- ExampleModules.h - IR modules for LLJIT examples -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Example modules for LLJIT examples
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXAMPLES_ORCV2EXAMPLES_EXAMPLEMODULES_H
#define LLVM_EXAMPLES_ORCV2EXAMPLES_EXAMPLEMODULES_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/SourceMgr.h"

const llvm::StringRef Add1Example =
    R"(
  define i32 @add1(i32 %x) {
  entry:
    %r = add nsw i32 %x, 1
    ret i32 %r
  }
)";

inline llvm::Error createSMDiagnosticError(llvm::SMDiagnostic &Diag) {
  using namespace llvm;
  std::string Msg;
  {
    raw_string_ostream OS(Msg);
    Diag.print("", OS);
  }
  return make_error<StringError>(std::move(Msg), inconvertibleErrorCode());
}

inline llvm::Expected<llvm::orc::ThreadSafeModule>
parseExampleModule(llvm::StringRef Source, llvm::StringRef Name) {
  using namespace llvm;
  auto Ctx = std::make_unique<LLVMContext>();
  SMDiagnostic Err;
  if (auto M = parseIR(MemoryBufferRef(Source, Name), Err, *Ctx))
    return orc::ThreadSafeModule(std::move(M), std::move(Ctx));

  return createSMDiagnosticError(Err);
}

inline llvm::Expected<llvm::orc::ThreadSafeModule>
parseExampleModuleFromFile(llvm::StringRef FileName) {
  using namespace llvm;
  auto Ctx = std::make_unique<LLVMContext>();
  SMDiagnostic Err;

  if (auto M = parseIRFile(FileName, Err, *Ctx))
    return orc::ThreadSafeModule(std::move(M), std::move(Ctx));

  return createSMDiagnosticError(Err);
}

#endif // LLVM_EXAMPLES_ORCV2EXAMPLES_EXAMPLEMODULES_H
