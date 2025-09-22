//===-- sanitizer_redefine_builtins.h ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Redefine builtin functions to use internal versions. This is needed where
// compiler optimizations end up producing unwanted libcalls!
//
//===----------------------------------------------------------------------===//
#ifndef SANITIZER_COMMON_NO_REDEFINE_BUILTINS
#  ifndef SANITIZER_REDEFINE_BUILTINS_H
#    define SANITIZER_REDEFINE_BUILTINS_H

// The asm hack only works with GCC and Clang.
#    if !defined(_WIN32)

asm("memcpy = __sanitizer_internal_memcpy");
asm("memmove = __sanitizer_internal_memmove");
asm("memset = __sanitizer_internal_memset");

#      if defined(__cplusplus) && \
          !defined(SANITIZER_COMMON_REDEFINE_BUILTINS_IN_STD)

// The builtins should not be redefined in source files that make use of C++
// standard libraries, in particular where C++STL headers with inline functions
// are used. The redefinition in such cases would lead to ODR violations.
//
// Try to break the build in common cases where builtins shouldn't be redefined.
namespace std {
class Define_SANITIZER_COMMON_NO_REDEFINE_BUILTINS_in_cpp_file {
  Define_SANITIZER_COMMON_NO_REDEFINE_BUILTINS_in_cpp_file(
      const Define_SANITIZER_COMMON_NO_REDEFINE_BUILTINS_in_cpp_file&) = delete;
  Define_SANITIZER_COMMON_NO_REDEFINE_BUILTINS_in_cpp_file& operator=(
      const Define_SANITIZER_COMMON_NO_REDEFINE_BUILTINS_in_cpp_file&) = delete;
};
using array = Define_SANITIZER_COMMON_NO_REDEFINE_BUILTINS_in_cpp_file;
using atomic = Define_SANITIZER_COMMON_NO_REDEFINE_BUILTINS_in_cpp_file;
using function = Define_SANITIZER_COMMON_NO_REDEFINE_BUILTINS_in_cpp_file;
using map = Define_SANITIZER_COMMON_NO_REDEFINE_BUILTINS_in_cpp_file;
using set = Define_SANITIZER_COMMON_NO_REDEFINE_BUILTINS_in_cpp_file;
using shared_ptr = Define_SANITIZER_COMMON_NO_REDEFINE_BUILTINS_in_cpp_file;
using string = Define_SANITIZER_COMMON_NO_REDEFINE_BUILTINS_in_cpp_file;
using unique_ptr = Define_SANITIZER_COMMON_NO_REDEFINE_BUILTINS_in_cpp_file;
using unordered_map = Define_SANITIZER_COMMON_NO_REDEFINE_BUILTINS_in_cpp_file;
using unordered_set = Define_SANITIZER_COMMON_NO_REDEFINE_BUILTINS_in_cpp_file;
using vector = Define_SANITIZER_COMMON_NO_REDEFINE_BUILTINS_in_cpp_file;
}  // namespace std

#      endif  // __cpluplus
#    endif    // !_WIN32

#  endif  // SANITIZER_REDEFINE_BUILTINS_H
#endif    // SANITIZER_COMMON_NO_REDEFINE_BUILTINS
