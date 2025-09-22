/*===-- targets.c - tool for testing libLLVM and llvm-c API ---------------===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This file implements the --targets command in llvm-c-test.                 *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#include "llvm-c/TargetMachine.h"
#include <stdio.h>

int llvm_targets_list(void) {
  LLVMTargetRef t;
  LLVMInitializeAllTargetInfos();
  LLVMInitializeAllTargets();

  for (t = LLVMGetFirstTarget(); t; t = LLVMGetNextTarget(t)) {
    printf("%s", LLVMGetTargetName(t));
    if (LLVMTargetHasJIT(t))
      printf(" (+jit)");
    printf("\n - %s\n", LLVMGetTargetDescription(t));
  }

  return 0;
}
