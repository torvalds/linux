/*===-- module.c - tool for testing libLLVM and llvm-c API ----------------===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This file implements the --module-dump, --module-list-functions and        *|
|* --module-list-globals commands in llvm-c-test.                             *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#include "llvm-c-test.h"
#include "llvm-c/BitReader.h"
#include <stdio.h>
#include <stdlib.h>

static void diagnosticHandler(LLVMDiagnosticInfoRef DI, void *C) {
  char *CErr = LLVMGetDiagInfoDescription(DI);
  fprintf(stderr, "Error with new bitcode parser: %s\n", CErr);
  LLVMDisposeMessage(CErr);
  exit(1);
}

LLVMModuleRef llvm_load_module(bool Lazy, bool New) {
  LLVMMemoryBufferRef MB;
  LLVMModuleRef M;
  char *msg = NULL;

  if (LLVMCreateMemoryBufferWithSTDIN(&MB, &msg)) {
    fprintf(stderr, "Error reading file: %s\n", msg);
    exit(1);
  }

  LLVMBool Ret;
  if (New) {
    LLVMContextRef C = LLVMGetGlobalContext();
    LLVMContextSetDiagnosticHandler(C, diagnosticHandler, NULL);
    if (Lazy)
      Ret = LLVMGetBitcodeModule2(MB, &M);
    else
      Ret = LLVMParseBitcode2(MB, &M);
  } else {
    if (Lazy)
      Ret = LLVMGetBitcodeModule(MB, &M, &msg);
    else
      Ret = LLVMParseBitcode(MB, &M, &msg);
  }

  if (Ret) {
    fprintf(stderr, "Error parsing bitcode: %s\n", msg);
    LLVMDisposeMemoryBuffer(MB);
    exit(1);
  }

  if (!Lazy)
    LLVMDisposeMemoryBuffer(MB);

  return M;
}

int llvm_module_dump(bool Lazy, bool New) {
  LLVMModuleRef M = llvm_load_module(Lazy, New);

  char *irstr = LLVMPrintModuleToString(M);
  puts(irstr);
  LLVMDisposeMessage(irstr);

  LLVMDisposeModule(M);

  return 0;
}

int llvm_module_list_functions(void) {
  LLVMModuleRef M = llvm_load_module(false, false);
  LLVMValueRef f;

  f = LLVMGetFirstFunction(M);
  while (f) {
    if (LLVMIsDeclaration(f)) {
      printf("FunctionDeclaration: %s\n", LLVMGetValueName(f));
    } else {
      LLVMBasicBlockRef bb;
      LLVMValueRef isn;
      unsigned nisn = 0;
      unsigned nbb = 0;

      printf("FunctionDefinition: %s [#bb=%u]\n", LLVMGetValueName(f),
             LLVMCountBasicBlocks(f));

      for (bb = LLVMGetFirstBasicBlock(f); bb;
           bb = LLVMGetNextBasicBlock(bb)) {
        nbb++;
        for (isn = LLVMGetFirstInstruction(bb); isn;
             isn = LLVMGetNextInstruction(isn)) {
          nisn++;
          if (LLVMIsACallInst(isn)) {
            LLVMValueRef callee =
                LLVMGetOperand(isn, LLVMGetNumOperands(isn) - 1);
            printf(" calls: %s\n", LLVMGetValueName(callee));
          }
        }
      }
      printf(" #isn: %u\n", nisn);
      printf(" #bb: %u\n\n", nbb);
    }
    f = LLVMGetNextFunction(f);
  }

  LLVMDisposeModule(M);

  return 0;
}

int llvm_module_list_globals(void) {
  LLVMModuleRef M = llvm_load_module(false, false);
  LLVMValueRef g;

  g = LLVMGetFirstGlobal(M);
  while (g) {
    LLVMTypeRef T = LLVMTypeOf(g);
    char *s = LLVMPrintTypeToString(T);

    printf("Global%s: %s %s\n",
           LLVMIsDeclaration(g) ? "Declaration" : "Definition",
           LLVMGetValueName(g), s);

    LLVMDisposeMessage(s);

    g = LLVMGetNextGlobal(g);
  }

  LLVMDisposeModule(M);

  return 0;
}
