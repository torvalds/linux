//===-libremarks.cpp - LLVM Remarks Shared Library ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Provide a library to work with remark diagnostics.
//
//===----------------------------------------------------------------------===//

#include "llvm-c/Remarks.h"

extern uint32_t LLVMRemarkVersion(void) {
  return REMARKS_API_VERSION;
}
