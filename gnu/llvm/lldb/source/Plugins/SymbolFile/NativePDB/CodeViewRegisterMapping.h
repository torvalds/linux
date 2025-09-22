//===-- CodeViewRegisterMapping.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_NATIVEPDB_CODEVIEWREGISTERMAPPING_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_NATIVEPDB_CODEVIEWREGISTERMAPPING_H

#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/TargetParser/Triple.h"

namespace lldb_private {
namespace npdb {

uint32_t GetLLDBRegisterNumber(llvm::Triple::ArchType arch_type,
                               llvm::codeview::RegisterId register_id);
uint32_t GetRegisterSize(llvm::codeview::RegisterId register_id);

} // namespace npdb
} // namespace lldb_private

#endif
