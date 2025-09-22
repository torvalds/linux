//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <__config>
#include <memory>

// Support for garbage collection was removed in C++23 by https://wg21.link/P2186R2. Libc++ implements
// that removal as an extension in all Standard versions. However, we still define the functions that
// were once part of the library's ABI for backwards compatibility.

_LIBCPP_BEGIN_NAMESPACE_STD

_LIBCPP_EXPORTED_FROM_ABI void declare_reachable(void*) {}
_LIBCPP_EXPORTED_FROM_ABI void declare_no_pointers(char*, size_t) {}
_LIBCPP_EXPORTED_FROM_ABI void undeclare_no_pointers(char*, size_t) {}
_LIBCPP_EXPORTED_FROM_ABI void* __undeclare_reachable(void* p) { return p; }

_LIBCPP_END_NAMESPACE_STD
