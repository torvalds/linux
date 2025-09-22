//==- llvm/Support/Windows/COM.inc - Windows COM Implementation -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Windows portion of COM support.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//=== WARNING: Implementation here must contain only Windows code.
//===----------------------------------------------------------------------===//

#include <objbase.h>

namespace llvm {
namespace sys {

InitializeCOMRAII::InitializeCOMRAII(COMThreadingMode Threading,
                                     bool SpeedOverMemory) {
  DWORD Coinit = 0;
  if (Threading == COMThreadingMode::SingleThreaded)
    Coinit |= COINIT_APARTMENTTHREADED;
  else
    Coinit |= COINIT_MULTITHREADED;
  if (SpeedOverMemory)
    Coinit |= COINIT_SPEED_OVER_MEMORY;
  ::CoInitializeEx(nullptr, Coinit);
}

InitializeCOMRAII::~InitializeCOMRAII() { ::CoUninitialize(); }
} // namespace sys
} // namespace llvm
