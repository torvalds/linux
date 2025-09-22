//===-- Version.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_VERSION_VERSION_H
#define LLDB_VERSION_VERSION_H

#include <string>

namespace lldb_private {

/// Retrieves a string representing the complete LLDB version, which includes
/// the lldb version number, as well as embedded compiler versions and the
/// vendor tag.
const char *GetVersion();

} // namespace lldb_private

#endif // LLDB_VERSION_VERSION_H
