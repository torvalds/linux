//===- LLDBTableGenUtils.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LLDBTableGenUtils.h"
#include "llvm/TableGen/Record.h"

using namespace llvm;
using namespace lldb_private;

RecordsByName lldb_private::getRecordsByName(std::vector<Record *> Records,
                                             StringRef Name) {
  RecordsByName Result;
  for (Record *R : Records)
    Result[R->getValueAsString(Name).str()].push_back(R);
  return Result;
}
