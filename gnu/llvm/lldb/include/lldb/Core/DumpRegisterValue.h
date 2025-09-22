//===-- DumpRegisterValue.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_DUMPREGISTERVALUE_H
#define LLDB_CORE_DUMPREGISTERVALUE_H

#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include <cstdint>

namespace lldb_private {

class ExecutionContextScope;
class RegisterValue;
struct RegisterInfo;
class Stream;

// The default value of 0 for reg_name_right_align_at means no alignment at
// all.
// Set print_flags to true to print register fields if they are available.
// If you do so, target_sp must be non-null for it to work.
void DumpRegisterValue(const RegisterValue &reg_val, Stream &s,
                       const RegisterInfo &reg_info, bool prefix_with_name,
                       bool prefix_with_alt_name, lldb::Format format,
                       uint32_t reg_name_right_align_at = 0,
                       ExecutionContextScope *exe_scope = nullptr,
                       bool print_flags = false,
                       lldb::TargetSP target_sp = nullptr);

} // namespace lldb_private

#endif // LLDB_CORE_DUMPREGISTERVALUE_H
