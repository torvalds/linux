// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
#ifndef _LIBCPP_HAS_NO_THREADS
  // [thread.sema.cnt], class template counting_semaphore
  using std::counting_semaphore;

  using std::binary_semaphore;
#endif // _LIBCPP_HAS_NO_THREADS
} // namespace std
