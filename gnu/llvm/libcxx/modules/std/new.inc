// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
  // [alloc.errors], storage allocation errors
  using std::bad_alloc;
  using std::bad_array_new_length;

  using std::destroying_delete;
  using std::destroying_delete_t;

  // global operator new control
  using std::align_val_t;

  using std::nothrow;
  using std::nothrow_t;

  using std::get_new_handler;
  using std::new_handler;
  using std::set_new_handler;

  // [ptr.launder], pointer optimization barrier
  using std::launder;
#if 0
  // [hardware.interference], hardware interference size
  using std::hardware_constructive_interference_size;
  using std::hardware_destructive_interference_size;
#endif
} // namespace std

export {
  using ::operator new;
  using ::operator delete;
  using ::operator new[];
  using ::operator delete[];
} // export
