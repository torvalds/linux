//===-- CFCMutableSet.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CFCMutableSet.h"


// CFCString constructor
CFCMutableSet::CFCMutableSet(CFMutableSetRef s)
    : CFCReleaser<CFMutableSetRef>(s) {}

// CFCMutableSet copy constructor
CFCMutableSet::CFCMutableSet(const CFCMutableSet &rhs) = default;

// CFCMutableSet copy constructor
const CFCMutableSet &CFCMutableSet::operator=(const CFCMutableSet &rhs) {
  if (this != &rhs)
    *this = rhs;
  return *this;
}

// Destructor
CFCMutableSet::~CFCMutableSet() = default;

CFIndex CFCMutableSet::GetCount() const {
  CFMutableSetRef set = get();
  if (set)
    return ::CFSetGetCount(set);
  return 0;
}

CFIndex CFCMutableSet::GetCountOfValue(const void *value) const {
  CFMutableSetRef set = get();
  if (set)
    return ::CFSetGetCountOfValue(set, value);
  return 0;
}

const void *CFCMutableSet::GetValue(const void *value) const {
  CFMutableSetRef set = get();
  if (set)
    return ::CFSetGetValue(set, value);
  return NULL;
}

const void *CFCMutableSet::AddValue(const void *value, bool can_create) {
  CFMutableSetRef set = get();
  if (set == NULL) {
    if (!can_create)
      return NULL;
    set = ::CFSetCreateMutable(kCFAllocatorDefault, 0, &kCFTypeSetCallBacks);
    reset(set);
  }
  if (set != NULL) {
    ::CFSetAddValue(set, value);
    return value;
  }
  return NULL;
}

void CFCMutableSet::RemoveValue(const void *value) {
  CFMutableSetRef set = get();
  if (set)
    ::CFSetRemoveValue(set, value);
}

void CFCMutableSet::RemoveAllValues() {
  CFMutableSetRef set = get();
  if (set)
    ::CFSetRemoveAllValues(set);
}
