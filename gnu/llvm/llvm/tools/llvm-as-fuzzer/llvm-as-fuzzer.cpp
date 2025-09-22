//===-- llvm-as-fuzzer.cpp - Fuzzer for llvm-as using lib/Fuzzer ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Build tool to fuzz the LLVM assembler (llvm-as) using
// lib/Fuzzer. The main reason for using this tool is that it is much
// faster than using afl-fuzz, since it is run in-process.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringRef.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

#include <csetjmp>

using namespace llvm;

static jmp_buf JmpBuf;

namespace {

void MyFatalErrorHandler(void *user_data, const char *reason,
                         bool gen_crash_diag) {
  // Don't bother printing reason, just return to the test function,
  // since a fatal error represents a successful parse (i.e. it correctly
  // terminated with an error message to the user).
  longjmp(JmpBuf, 1);
}

static bool InstalledHandler = false;

} // end of anonymous namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {

  // Allocate space for locals before setjmp so that memory can be collected
  // if parse exits prematurely (via longjmp).
  StringRef Input((const char *)Data, Size);
  // Note: We need to create a buffer to add a null terminator to the
  // end of the input string. The parser assumes that the string
  // parsed is always null terminated.
  std::unique_ptr<MemoryBuffer> MemBuf = MemoryBuffer::getMemBufferCopy(Input);
  SMDiagnostic Err;
  LLVMContext Context;
  std::unique_ptr<Module> M;

  if (setjmp(JmpBuf))
    // If reached, we have returned with non-zero status, so exit.
    return 0;

  // TODO(kschimpf) Write a main to do this initialization.
  if (!InstalledHandler) {
    llvm::install_fatal_error_handler(::MyFatalErrorHandler, nullptr);
    InstalledHandler = true;
  }

  M = parseAssembly(MemBuf->getMemBufferRef(), Err, Context);

  if (!M.get())
    return 0;

  if (verifyModule(*M.get(), &errs()))
    report_fatal_error("Broken module");
  return 0;
}
