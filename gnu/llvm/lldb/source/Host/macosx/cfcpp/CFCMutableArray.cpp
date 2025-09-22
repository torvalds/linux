//===-- CFCMutableArray.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CFCMutableArray.h"
#include "CFCString.h"

// CFCString constructor
CFCMutableArray::CFCMutableArray(CFMutableArrayRef s)
    : CFCReleaser<CFMutableArrayRef>(s) {}

// CFCMutableArray copy constructor
CFCMutableArray::CFCMutableArray(const CFCMutableArray &rhs) =
    default; // NOTE: this won't make a copy of the
             // array, just add a new reference to
             // it

// CFCMutableArray copy constructor
CFCMutableArray &CFCMutableArray::operator=(const CFCMutableArray &rhs) {
  if (this != &rhs)
    *this = rhs; // NOTE: this operator won't make a copy of the array, just add
                 // a new reference to it
  return *this;
}

// Destructor
CFCMutableArray::~CFCMutableArray() = default;

CFIndex CFCMutableArray::GetCount() const {
  CFMutableArrayRef array = get();
  if (array)
    return ::CFArrayGetCount(array);
  return 0;
}

CFIndex CFCMutableArray::GetCountOfValue(CFRange range,
                                         const void *value) const {
  CFMutableArrayRef array = get();
  if (array)
    return ::CFArrayGetCountOfValue(array, range, value);
  return 0;
}

CFIndex CFCMutableArray::GetCountOfValue(const void *value) const {
  CFMutableArrayRef array = get();
  if (array)
    return ::CFArrayGetCountOfValue(array, CFRangeMake(0, GetCount()), value);
  return 0;
}

const void *CFCMutableArray::GetValueAtIndex(CFIndex idx) const {
  CFMutableArrayRef array = get();
  if (array) {
    const CFIndex num_array_items = ::CFArrayGetCount(array);
    if (0 <= idx && idx < num_array_items) {
      return ::CFArrayGetValueAtIndex(array, idx);
    }
  }
  return NULL;
}

bool CFCMutableArray::SetValueAtIndex(CFIndex idx, const void *value) {
  CFMutableArrayRef array = get();
  if (array != NULL) {
    const CFIndex num_array_items = ::CFArrayGetCount(array);
    if (0 <= idx && idx < num_array_items) {
      ::CFArraySetValueAtIndex(array, idx, value);
      return true;
    }
  }
  return false;
}

bool CFCMutableArray::AppendValue(const void *value, bool can_create) {
  CFMutableArrayRef array = get();
  if (array == NULL) {
    if (!can_create)
      return false;
    array =
        ::CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
    reset(array);
  }
  if (array != NULL) {
    ::CFArrayAppendValue(array, value);
    return true;
  }
  return false;
}

bool CFCMutableArray::AppendCStringAsCFString(const char *s,
                                              CFStringEncoding encoding,
                                              bool can_create) {
  CFMutableArrayRef array = get();
  if (array == NULL) {
    if (!can_create)
      return false;
    array =
        ::CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
    reset(array);
  }
  if (array != NULL) {
    CFCString cf_str(s, encoding);
    ::CFArrayAppendValue(array, cf_str.get());
    return true;
  }
  return false;
}

bool CFCMutableArray::AppendFileSystemRepresentationAsCFString(
    const char *s, bool can_create) {
  CFMutableArrayRef array = get();
  if (array == NULL) {
    if (!can_create)
      return false;
    array =
        ::CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
    reset(array);
  }
  if (array != NULL) {
    CFCString cf_path;
    cf_path.SetFileSystemRepresentation(s);
    ::CFArrayAppendValue(array, cf_path.get());
    return true;
  }
  return false;
}
