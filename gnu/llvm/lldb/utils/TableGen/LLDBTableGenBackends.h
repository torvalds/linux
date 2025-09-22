//===- LLDBTableGenBackends.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations for all of the LLDB TableGen
// backends. A "TableGen backend" is just a function.
//
// See "$LLVM_ROOT/utils/TableGen/TableGenBackends.h" for more info.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILS_TABLEGEN_LLDBTABLEGENBACKENDS_H
#define LLDB_UTILS_TABLEGEN_LLDBTABLEGENBACKENDS_H

#include "llvm/ADT/StringRef.h"

namespace llvm {
class raw_ostream;
class RecordKeeper;
class Record;
} // namespace llvm

using llvm::raw_ostream;
using llvm::RecordKeeper;

namespace lldb_private {

void EmitOptionDefs(RecordKeeper &RK, raw_ostream &OS);
void EmitPropertyDefs(RecordKeeper &RK, raw_ostream &OS);
void EmitPropertyEnumDefs(RecordKeeper &RK, raw_ostream &OS);
int EmitSBAPIDWARFEnum(int argc, char **argv);

} // namespace lldb_private

#endif
