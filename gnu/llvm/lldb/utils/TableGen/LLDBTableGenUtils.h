//===- LLDBTableGenUtils.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILS_TABLEGEN_LLDBTABLEGENUTILS_H
#define LLDB_UTILS_TABLEGEN_LLDBTABLEGENUTILS_H

#include "llvm/ADT/StringRef.h"
#include <map>
#include <string>
#include <vector>

namespace llvm {
class RecordKeeper;
class Record;
} // namespace llvm

namespace lldb_private {

/// Map of names to their associated records. This map also ensures that our
/// records are sorted in a deterministic way.
typedef std::map<std::string, std::vector<llvm::Record *>> RecordsByName;

/// Return records grouped by name.
RecordsByName getRecordsByName(std::vector<llvm::Record *> Records,
                               llvm::StringRef);

} // namespace lldb_private

#endif
