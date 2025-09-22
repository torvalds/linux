//===-- ErrorMessages.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_ERROR_MESSAGES_H
#define LLDB_UTILITY_ERROR_MESSAGES_H

#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include <string>

namespace lldb_private {

/// Produce a human-readable rendition of an ExpressionResults value.
std::string toString(lldb::ExpressionResults e);

} // namespace lldb_private
#endif
