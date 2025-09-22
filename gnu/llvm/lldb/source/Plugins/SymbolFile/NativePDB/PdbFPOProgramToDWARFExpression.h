//===-- PdbFPOProgramToDWARFExpression.h ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_NATIVEPDB_PDBFPOPROGRAMTODWARFEXPRESSION_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_NATIVEPDB_PDBFPOPROGRAMTODWARFEXPRESSION_H

#include "llvm/ADT/StringRef.h"
#include "llvm/TargetParser/Triple.h"

namespace lldb_private {
class Stream;

namespace npdb {
  
bool TranslateFPOProgramToDWARFExpression(llvm::StringRef program,
                                          llvm::StringRef register_name,
                                          llvm::Triple::ArchType arch_type,
                                          lldb_private::Stream &stream);

} // namespace npdb
} // namespace lldb_private

#endif
