/*===-- clang-c/Rewrite.h - C CXRewriter   --------------------------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*/

#ifndef LLVM_CLANG_C_REWRITE_H
#define LLVM_CLANG_C_REWRITE_H

#include "clang-c/CXString.h"
#include "clang-c/ExternC.h"
#include "clang-c/Index.h"
#include "clang-c/Platform.h"

LLVM_CLANG_C_EXTERN_C_BEGIN

typedef void *CXRewriter;

/**
 * Create CXRewriter.
 */
CINDEX_LINKAGE CXRewriter clang_CXRewriter_create(CXTranslationUnit TU);

/**
 * Insert the specified string at the specified location in the original buffer.
 */
CINDEX_LINKAGE void clang_CXRewriter_insertTextBefore(CXRewriter Rew, CXSourceLocation Loc,
                                           const char *Insert);

/**
 * Replace the specified range of characters in the input with the specified
 * replacement.
 */
CINDEX_LINKAGE void clang_CXRewriter_replaceText(CXRewriter Rew, CXSourceRange ToBeReplaced,
                                      const char *Replacement);

/**
 * Remove the specified range.
 */
CINDEX_LINKAGE void clang_CXRewriter_removeText(CXRewriter Rew, CXSourceRange ToBeRemoved);

/**
 * Save all changed files to disk.
 * Returns 1 if any files were not saved successfully, returns 0 otherwise.
 */
CINDEX_LINKAGE int clang_CXRewriter_overwriteChangedFiles(CXRewriter Rew);

/**
 * Write out rewritten version of the main file to stdout.
 */
CINDEX_LINKAGE void clang_CXRewriter_writeMainFileToStdOut(CXRewriter Rew);

/**
 * Free the given CXRewriter.
 */
CINDEX_LINKAGE void clang_CXRewriter_dispose(CXRewriter Rew);

LLVM_CLANG_C_EXTERN_C_END

#endif
