//===-- WindowsMiniDump.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_OBJECTFILE_PECOFF_WINDOWSMINIDUMP_H
#define LLDB_SOURCE_PLUGINS_OBJECTFILE_PECOFF_WINDOWSMINIDUMP_H

#include "lldb/Target/Process.h"

namespace lldb_private {

bool SaveMiniDump(const lldb::ProcessSP &process_sp,
                  const SaveCoreOptions &core_options,
                  lldb_private::Status &error);

} // namespace lldb_private

#endif
