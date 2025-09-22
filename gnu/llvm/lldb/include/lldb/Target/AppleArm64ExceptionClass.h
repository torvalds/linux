//===-- AppleArm64ExceptionClass.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_APPLEARM64EXCEPTIONCLASS_H
#define LLDB_TARGET_APPLEARM64EXCEPTIONCLASS_H

#include <cstdint>

namespace lldb_private {

enum class AppleArm64ExceptionClass : unsigned {
#define APPLE_ARM64_EXCEPTION_CLASS(Name, Code) Name = Code,
#include "AppleArm64ExceptionClass.def"
};

/// Get the Apple ARM64 exception class encoded within \p esr.
inline AppleArm64ExceptionClass getAppleArm64ExceptionClass(uint32_t esr) {
  /*
   * Exception Syndrome Register
   *
   *  31  26 25 24               0
   * +------+--+------------------+
   * |  EC  |IL|       ISS        |
   * +------+--+------------------+
   *
   * EC  - Exception Class
   * IL  - Instruction Length
   * ISS - Instruction Specific Syndrome
   */
  return static_cast<AppleArm64ExceptionClass>(esr >> 26);
}

inline const char *toString(AppleArm64ExceptionClass EC) {
  switch (EC) {
#define APPLE_ARM64_EXCEPTION_CLASS(Name, Code)                                \
  case AppleArm64ExceptionClass::Name:                                         \
    return #Name;
#include "AppleArm64ExceptionClass.def"
  }
  return "Unknown Exception Class";
}

} // namespace lldb_private

#endif // LLDB_TARGET_APPLEARM64EXCEPTIONCLASS_H
