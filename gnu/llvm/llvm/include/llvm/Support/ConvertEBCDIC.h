//===--- ConvertEBCDIC.h - UTF8/EBCDIC CharSet Conversion -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file provides utility functions for converting between EBCDIC-1047 and
/// UTF-8.
///
///
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <system_error>

namespace llvm {
namespace ConverterEBCDIC {
std::error_code convertToEBCDIC(StringRef Source,
                                SmallVectorImpl<char> &Result);

void convertToUTF8(StringRef Source, SmallVectorImpl<char> &Result);

} // namespace ConverterEBCDIC
} // namespace llvm
