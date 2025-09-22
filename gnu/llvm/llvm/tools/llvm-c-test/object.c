/*===-- object.c - tool for testing libLLVM and llvm-c API ----------------===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This file implements the --object-list-sections and --object-list-symbols  *|
|* commands in llvm-c-test.                                                   *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#include "llvm-c-test.h"
#include "llvm-c/Object.h"
#include <stdio.h>
#include <stdlib.h>

int llvm_object_list_sections(void) {
  LLVMMemoryBufferRef MB;
  LLVMBinaryRef O;
  LLVMSectionIteratorRef sect;

  char *outBufferErr = NULL;
  if (LLVMCreateMemoryBufferWithSTDIN(&MB, &outBufferErr)) {
    fprintf(stderr, "Error reading file: %s\n", outBufferErr);
    free(outBufferErr);
    exit(1);
  }

  char *outBinaryErr = NULL;
  O = LLVMCreateBinary(MB, LLVMGetGlobalContext(), &outBinaryErr);
  if (!O || outBinaryErr) {
    fprintf(stderr, "Error reading object: %s\n", outBinaryErr);
    free(outBinaryErr);
    exit(1);
  }

  sect = LLVMObjectFileCopySectionIterator(O);
  while (sect && !LLVMObjectFileIsSectionIteratorAtEnd(O, sect)) {
    printf("'%s': @0x%08" PRIx64 " +%" PRIu64 "\n", LLVMGetSectionName(sect),
           LLVMGetSectionAddress(sect), LLVMGetSectionSize(sect));

    LLVMMoveToNextSection(sect);
  }

  LLVMDisposeSectionIterator(sect);

  LLVMDisposeBinary(O);

  LLVMDisposeMemoryBuffer(MB);

  return 0;
}

int llvm_object_list_symbols(void) {
  LLVMMemoryBufferRef MB;
  LLVMBinaryRef O;
  LLVMSectionIteratorRef sect;
  LLVMSymbolIteratorRef sym;

  char *outBufferErr = NULL;
  if (LLVMCreateMemoryBufferWithSTDIN(&MB, &outBufferErr)) {
    fprintf(stderr, "Error reading file: %s\n", outBufferErr);
    free(outBufferErr);
    exit(1);
  }

  char *outBinaryErr = NULL;
  O = LLVMCreateBinary(MB, LLVMGetGlobalContext(), &outBinaryErr);
  if (!O || outBinaryErr) {
    fprintf(stderr, "Error reading object: %s\n", outBinaryErr);
    free(outBinaryErr);
    exit(1);
  }

  sect = LLVMObjectFileCopySectionIterator(O);
  sym = LLVMObjectFileCopySymbolIterator(O);
  while (sect && sym && !LLVMObjectFileIsSymbolIteratorAtEnd(O, sym)) {

    LLVMMoveToContainingSection(sect, sym);
    printf("%s @0x%08" PRIx64 " +%" PRIu64 " (%s)\n", LLVMGetSymbolName(sym),
           LLVMGetSymbolAddress(sym), LLVMGetSymbolSize(sym),
           LLVMGetSectionName(sect));

    LLVMMoveToNextSymbol(sym);
  }

  LLVMDisposeSymbolIterator(sym);

  LLVMDisposeBinary(O);

  LLVMDisposeMemoryBuffer(MB);

  return 0;
}
