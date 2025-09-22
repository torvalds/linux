//===-- VASPrintf.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_VASPRINTF_H
#define LLDB_UTILITY_VASPRINTF_H

#include "llvm/ADT/SmallVector.h"

#include <cstdarg>

namespace lldb_private {
bool VASprintf(llvm::SmallVectorImpl<char> &buf, const char *fmt, va_list args);
}

#endif // LLDB_UTILITY_VASPRINTF_H
