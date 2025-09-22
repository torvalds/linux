//===-- CFUtils.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 3/5/07.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_DEBUGSERVER_SOURCE_MACOSX_CFUTILS_H
#define LLDB_TOOLS_DEBUGSERVER_SOURCE_MACOSX_CFUTILS_H

#include <CoreFoundation/CoreFoundation.h>

// Templatized CF helper class that can own any CF pointer and will
// call CFRelease() on any valid pointer it owns unless that pointer is
// explicitly released using the release() member function.
template <class T> class CFReleaser {
public:
  // Type names for the value
  typedef T element_type;

  // Constructors and destructors
  CFReleaser(T ptr = NULL) : _ptr(ptr) {}
  CFReleaser(const CFReleaser &copy) : _ptr(copy.get()) {
    if (get())
      ::CFRetain(get());
  }
  virtual ~CFReleaser() { reset(); }

  // Assignments
  CFReleaser &operator=(const CFReleaser<T> &copy) {
    if (copy != *this) {
      // Replace our owned pointer with the new one
      reset(copy.get());
      // Retain the current pointer that we own
      if (get())
        ::CFRetain(get());
    }
  }
  // Get the address of the contained type
  T *ptr_address() { return &_ptr; }

  // Access the pointer itself
  const T get() const { return _ptr; }
  T get() { return _ptr; }

  // Set a new value for the pointer and CFRelease our old
  // value if we had a valid one.
  void reset(T ptr = NULL) {
    if (ptr != _ptr) {
      if (_ptr != NULL)
        ::CFRelease(_ptr);
      _ptr = ptr;
    }
  }

  // Release ownership without calling CFRelease
  T release() {
    T tmp = _ptr;
    _ptr = NULL;
    return tmp;
  }

private:
  element_type _ptr;
};

#endif // LLDB_TOOLS_DEBUGSERVER_SOURCE_MACOSX_CFUTILS_H
