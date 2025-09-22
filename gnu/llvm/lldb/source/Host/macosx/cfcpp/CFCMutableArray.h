//===-- CFCMutableArray.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_HOST_MACOSX_CFCPP_CFCMUTABLEARRAY_H
#define LLDB_SOURCE_HOST_MACOSX_CFCPP_CFCMUTABLEARRAY_H

#include "CFCReleaser.h"

class CFCMutableArray : public CFCReleaser<CFMutableArrayRef> {
public:
  // Constructors and Destructors
  CFCMutableArray(CFMutableArrayRef array = NULL);
  CFCMutableArray(const CFCMutableArray &rhs); // This will copy the array
                                               // contents into a new array
  CFCMutableArray &operator=(const CFCMutableArray &rhs); // This will re-use
                                                          // the same array and
                                                          // just bump the ref
                                                          // count
  ~CFCMutableArray() override;

  CFIndex GetCount() const;
  CFIndex GetCountOfValue(const void *value) const;
  CFIndex GetCountOfValue(CFRange range, const void *value) const;
  const void *GetValueAtIndex(CFIndex idx) const;
  bool SetValueAtIndex(CFIndex idx, const void *value);
  bool AppendValue(const void *value,
                   bool can_create = true); // Appends value and optionally
                                            // creates a CFCMutableArray if this
                                            // class doesn't contain one
  bool
  AppendCStringAsCFString(const char *cstr,
                          CFStringEncoding encoding = kCFStringEncodingUTF8,
                          bool can_create = true);
  bool AppendFileSystemRepresentationAsCFString(const char *s,
                                                bool can_create = true);
};

#endif // LLDB_SOURCE_HOST_MACOSX_CFCPP_CFCMUTABLEARRAY_H
