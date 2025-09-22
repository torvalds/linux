//===- CXString.h - Routines for manipulating CXStrings -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines routines for manipulating CXStrings.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_LIBCLANG_INDEX_INTERNAL_H
#define LLVM_CLANG_TOOLS_LIBCLANG_INDEX_INTERNAL_H

#include "clang-c/Index.h"

#ifndef __has_feature
#define __has_feature(x) 0
#endif

#if __has_feature(blocks)

#define INVOKE_BLOCK2(block, arg1, arg2) block(arg1, arg2)

#else
// If we are compiled with a compiler that doesn't have native blocks support,
// define and call the block manually. 

#define INVOKE_BLOCK2(block, arg1, arg2) block->invoke(block, arg1, arg2)

typedef struct _CXCursorAndRangeVisitorBlock {
  void *isa;
  int flags;
  int reserved;
  enum CXVisitorResult (*invoke)(_CXCursorAndRangeVisitorBlock *,
                                 CXCursor, CXSourceRange);
} *CXCursorAndRangeVisitorBlock;

#endif // !__has_feature(blocks)

/// The result of comparing two source ranges.
enum RangeComparisonResult {
  /// Either the ranges overlap or one of the ranges is invalid.
  RangeOverlap,

  /// The first range ends before the second range starts.
  RangeBefore,

  /// The first range starts after the second range ends.
  RangeAfter
};

#endif
