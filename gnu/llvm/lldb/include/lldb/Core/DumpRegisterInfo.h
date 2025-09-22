//===-- DumpRegisterInfo.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_DUMPREGISTERINFO_H
#define LLDB_CORE_DUMPREGISTERINFO_H

#include <stdint.h>
#include <utility>
#include <vector>

namespace lldb_private {

class Stream;
class RegisterContext;
struct RegisterInfo;
class RegisterFlags;

void DumpRegisterInfo(Stream &strm, RegisterContext &ctx,
                      const RegisterInfo &info, uint32_t terminal_width);

// For testing only. Use DumpRegisterInfo instead.
void DoDumpRegisterInfo(
    Stream &strm, const char *name, const char *alt_name, uint32_t byte_size,
    const std::vector<const char *> &invalidates,
    const std::vector<const char *> &read_from,
    const std::vector<std::pair<const char *, uint32_t>> &in_sets,
    const RegisterFlags *flags_type, uint32_t terminal_width);

} // namespace lldb_private

#endif // LLDB_CORE_DUMPREGISTERINFO_H
