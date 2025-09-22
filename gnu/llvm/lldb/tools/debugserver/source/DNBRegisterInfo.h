//===-- DNBRegisterInfo.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 8/3/07.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_DEBUGSERVER_SOURCE_DNBREGISTERINFO_H
#define LLDB_TOOLS_DEBUGSERVER_SOURCE_DNBREGISTERINFO_H

#include "DNBDefs.h"
#include <cstdint>
#include <cstdio>

struct DNBRegisterValueClass : public DNBRegisterValue {
  DNBRegisterValueClass(const DNBRegisterInfo *regInfo = NULL);
  void Clear();
  void Dump(const char *pre, const char *post) const;
  bool IsValid() const;
};

#endif
