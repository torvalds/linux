//===-- llvm/Remarks/RemarkFormat.h - The format of remarks -----*- C++/-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines utilities to deal with the format of remarks.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_REMARKS_REMARKFORMAT_H
#define LLVM_REMARKS_REMARKFORMAT_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace remarks {

constexpr StringLiteral Magic("REMARKS");

/// The format used for serializing/deserializing remarks.
enum class Format { Unknown, YAML, YAMLStrTab, Bitstream };

/// Parse and validate a string for the remark format.
Expected<Format> parseFormat(StringRef FormatStr);

/// Parse and validate a magic number to a remark format.
Expected<Format> magicToFormat(StringRef Magic);

} // end namespace remarks
} // end namespace llvm

#endif // LLVM_REMARKS_REMARKFORMAT_H
