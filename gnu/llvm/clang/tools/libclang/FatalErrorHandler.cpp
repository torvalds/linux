/*===-- clang-c/FatalErrorHandler.cpp - Fatal Error Handling ------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#include "clang-c/FatalErrorHandler.h"
#include "llvm/Support/ErrorHandling.h"
#include <stdio.h>
#include <stdlib.h>

static void aborting_fatal_error_handler(void *, const char *reason,
                                         bool) {
  // Write the result out to stderr avoiding errs() because raw_ostreams can
  // call report_fatal_error.
  fprintf(stderr, "LIBCLANG FATAL ERROR: %s\n", reason);
  ::abort();
}

extern "C" {
void clang_install_aborting_llvm_fatal_error_handler(void) {
  llvm::remove_fatal_error_handler();
  llvm::install_fatal_error_handler(aborting_fatal_error_handler, nullptr);
}

void clang_uninstall_llvm_fatal_error_handler(void) {
  llvm::remove_fatal_error_handler();
}
}
