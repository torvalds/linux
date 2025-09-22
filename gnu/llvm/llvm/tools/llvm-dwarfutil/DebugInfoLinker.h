//===- DebugInfoLinker.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_DWARFUTIL_DEBUGINFOLINKER_H
#define LLVM_TOOLS_LLVM_DWARFUTIL_DEBUGINFOLINKER_H

#include "Options.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ObjectFile.h"

namespace llvm {
namespace dwarfutil {

inline bool isDebugSection(StringRef SecName) {
  return SecName.starts_with(".debug") || SecName.starts_with(".zdebug") ||
         SecName == ".gdb_index";
}

Error linkDebugInfo(object::ObjectFile &file, const Options &Options,
                    raw_pwrite_stream &OutStream);

} // end of namespace dwarfutil
} // end of namespace llvm

#endif // LLVM_TOOLS_LLVM_DWARFUTIL_DEBUGINFOLINKER_H
