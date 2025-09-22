//===-- UuidCompatibility.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Include this header for a definition of uuid_t compatible with Darwin's
// definition.

#ifndef utility_UUID_COMPATIBILITY_H
#define utility_UUID_COMPATIBILITY_H
// uuid_t is guaranteed to always be a 16-byte array
typedef unsigned char uuid_t[16];
#endif // utility_UUID_COMPATIBILITY_H
