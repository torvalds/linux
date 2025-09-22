//===-- options_parser.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef GWP_ASAN_OPTIONAL_OPTIONS_PARSER_H_
#define GWP_ASAN_OPTIONAL_OPTIONS_PARSER_H_

#include "gwp_asan/optional/printf.h"
#include "gwp_asan/options.h"

namespace gwp_asan {
namespace options {
// Parse the options from the GWP_ASAN_OPTIONS environment variable.
void initOptions(Printf_t PrintfForWarnings = nullptr);
// Parse the options from the provided string.
void initOptions(const char *OptionsStr, Printf_t PrintfForWarnings = nullptr);
// Returns the initialised options. Call initOptions() prior to calling this
// function.
Options &getOptions();
} // namespace options
} // namespace gwp_asan

extern "C" {
__attribute__((weak)) const char *__gwp_asan_default_options();
}

#endif // GWP_ASAN_OPTIONAL_OPTIONS_PARSER_H_
