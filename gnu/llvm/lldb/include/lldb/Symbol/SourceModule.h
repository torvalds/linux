//===-- SourceModule.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SYMBOL_SOURCEMODULE_H
#define LLDB_SYMBOL_SOURCEMODULE_H

#include "lldb/Utility/ConstString.h"
#include <vector>

namespace lldb_private {

/// Information needed to import a source-language module.
struct SourceModule {
  /// Something like "Module.Submodule".
  std::vector<ConstString> path;
  ConstString search_path;
  ConstString sysroot;
};

} // namespace lldb_private

#endif
