//===-- StringConvert.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_DEBUGSERVER_SOURCE_STRINGCONVERT_H
#define LLDB_TOOLS_DEBUGSERVER_SOURCE_STRINGCONVERT_H

#include <cstdint>

namespace StringConvert {

int64_t ToSInt64(const char *s, int64_t fail_value = 0, int base = 0,
                 bool *success_ptr = nullptr);

uint64_t ToUInt64(const char *s, uint64_t fail_value = 0, int base = 0,
                  bool *success_ptr = nullptr);

double ToDouble(const char *s, double fail_value = 0.0,
                bool *success_ptr = nullptr);

} // namespace StringConvert

#endif
