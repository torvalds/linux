//===-- sanitizer/nsan_interface.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Public interface for nsan.
//
//===----------------------------------------------------------------------===//
#ifndef SANITIZER_NSAN_INTERFACE_H
#define SANITIZER_NSAN_INTERFACE_H

#include <sanitizer/common_interface_defs.h>

#ifdef __cplusplus
extern "C" {
#endif

/// User-provided default option settings.
///
/// You can provide your own implementation of this function to return a string
/// containing NSan runtime options (for example,
/// <c>verbosity=1:halt_on_error=0</c>).
///
/// \returns Default options string.
const char *__nsan_default_options(void);

// Dumps nsan shadow data for a block of `size_bytes` bytes of application
// memory at location `addr`.
//
// Each line contains application address, shadow types, then values.
// Unknown types are shown as `__`, while known values are shown as
// `f`, `d`, `l` for float, double, and long double respectively. Position is
// shown as a single hex digit. The shadow value itself appears on the line that
// contains the first byte of the value.
// FIXME: Show both shadow and application value.
//
// Example: `__nsan_dump_shadow_mem(addr, 32, 8, 0)` might print:
//
//  0x0add7359:  __ f0 f1 f2 f3 __ __ __   (42.000)
//  0x0add7361:  __ d1 d2 d3 d4 d5 d6 d7
//  0x0add7369:  d8 f0 f1 f2 f3 __ __ f2   (-1.000) (12.5)
//  0x0add7371:  f3 __ __ __ __ __ __ __
//
// This means that there is:
//   - a shadow double for the float at address 0x0add7360, with value 42;
//   - a shadow float128 for the double at address 0x0add7362, with value -1;
//   - a shadow double for the float at address 0x0add736a, with value 12.5;
// There was also a shadow double for the float at address 0x0add736e, but bytes
// f0 and f1 were overwritten by one or several stores, so that the shadow value
// is no longer valid.
// The argument `reserved` can be any value. Its true value is provided by the
// instrumentation.
void __nsan_dump_shadow_mem(const char *addr, size_t size_bytes,
                            size_t bytes_per_line, size_t reserved);

// Explicitly dumps a value.
// FIXME: vector versions ?
void __nsan_dump_float(float value);
void __nsan_dump_double(double value);
void __nsan_dump_longdouble(long double value);

// Explicitly checks a value.
// FIXME: vector versions ?
void __nsan_check_float(float value);
void __nsan_check_double(double value);
void __nsan_check_longdouble(long double value);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // SANITIZER_NSAN_INTERFACE_H
