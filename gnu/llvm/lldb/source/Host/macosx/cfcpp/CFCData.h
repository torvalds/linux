//===-- CFCData.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_HOST_MACOSX_CFCPP_CFCDATA_H
#define LLDB_SOURCE_HOST_MACOSX_CFCPP_CFCDATA_H

#include "CFCReleaser.h"

class CFCData : public CFCReleaser<CFDataRef> {
public:
  // Constructors and Destructors
  CFCData(CFDataRef data = NULL);
  CFCData(const CFCData &rhs);
  CFCData &operator=(const CFCData &rhs);
  ~CFCData() override;

  CFDataRef Serialize(CFPropertyListRef plist, CFPropertyListFormat format);
  const uint8_t *GetBytePtr() const;
  CFIndex GetLength() const;

protected:
  // Classes that inherit from CFCData can see and modify these
};

#endif // LLDB_SOURCE_HOST_MACOSX_CFCPP_CFCDATA_H
