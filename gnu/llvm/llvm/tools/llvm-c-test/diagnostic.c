//===-- diagnostic.cpp - tool for testing libLLVM and llvm-c API ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the --test-diagnostic-handler command in llvm-c-test.
//
// This command uses the C API to read a module with a custom diagnostic
// handler set to test the diagnostic handler functionality.
//
//===----------------------------------------------------------------------===//

#include "llvm-c-test.h"
#include "llvm-c/BitReader.h"
#include "llvm-c/Core.h"

#include <stdio.h>

static void diagnosticHandler(LLVMDiagnosticInfoRef DI, void *C) {
  fprintf(stderr, "Executing diagnostic handler\n");

  fprintf(stderr, "Diagnostic severity is of type ");
  switch (LLVMGetDiagInfoSeverity(DI)) {
  case LLVMDSError:
    fprintf(stderr, "error");
    break;
  case LLVMDSWarning:
    fprintf(stderr, "warning");
    break;
  case LLVMDSRemark:
    fprintf(stderr, "remark");
    break;
  case LLVMDSNote:
    fprintf(stderr, "note");
    break;
  }
  fprintf(stderr, "\n");

  (*(int *)C) = 1;
}

static int handlerCalled = 0;

int llvm_test_diagnostic_handler(void) {
  LLVMContextRef C = LLVMGetGlobalContext();
  LLVMContextSetDiagnosticHandler(C, diagnosticHandler, &handlerCalled);

  if (LLVMContextGetDiagnosticHandler(C) != diagnosticHandler) {
    fprintf(stderr, "LLVMContext{Set,Get}DiagnosticHandler failed\n");
    return 1;
  }

  int *DC = (int *)LLVMContextGetDiagnosticContext(C);
  if (DC != &handlerCalled || *DC) {
    fprintf(stderr, "LLVMContextGetDiagnosticContext failed\n");
    return 1;
  }

  LLVMMemoryBufferRef MB;
  char *msg = NULL;
  if (LLVMCreateMemoryBufferWithSTDIN(&MB, &msg)) {
    fprintf(stderr, "Error reading file: %s\n", msg);
    LLVMDisposeMessage(msg);
    return 1;
  }


  LLVMModuleRef M;
  int Ret = LLVMGetBitcodeModule2(MB, &M);
  if (Ret)
    LLVMDisposeMemoryBuffer(MB);

  if (handlerCalled) {
    fprintf(stderr, "Diagnostic handler was called while loading module\n");
  } else {
    fprintf(stderr, "Diagnostic handler was not called while loading module\n");
  }

  return 0;
}
