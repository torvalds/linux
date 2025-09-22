//===-- CFCMutableSet.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_HOST_MACOSX_CFCPP_CFCMUTABLESET_H
#define LLDB_SOURCE_HOST_MACOSX_CFCPP_CFCMUTABLESET_H

#include "CFCReleaser.h"

class CFCMutableSet : public CFCReleaser<CFMutableSetRef> {
public:
  // Constructors and Destructors
  CFCMutableSet(CFMutableSetRef s = NULL);
  CFCMutableSet(const CFCMutableSet &rhs);
  ~CFCMutableSet() override;

  // Operators
  const CFCMutableSet &operator=(const CFCMutableSet &rhs);

  CFIndex GetCount() const;
  CFIndex GetCountOfValue(const void *value) const;
  const void *GetValue(const void *value) const;
  const void *AddValue(const void *value, bool can_create);
  void RemoveValue(const void *value);
  void RemoveAllValues();

protected:
  // Classes that inherit from CFCMutableSet can see and modify these

private:
  // For CFCMutableSet only
};

#endif // LLDB_SOURCE_HOST_MACOSX_CFCPP_CFCMUTABLESET_H
