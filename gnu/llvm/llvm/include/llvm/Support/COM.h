//===- llvm/Support/COM.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// Provides a library for accessing COM functionality of the Host OS.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_COM_H
#define LLVM_SUPPORT_COM_H

namespace llvm {
namespace sys {

enum class COMThreadingMode { SingleThreaded, MultiThreaded };

class InitializeCOMRAII {
public:
  explicit InitializeCOMRAII(COMThreadingMode Threading,
                             bool SpeedOverMemory = false);
  ~InitializeCOMRAII();

private:
  InitializeCOMRAII(const InitializeCOMRAII &) = delete;
  void operator=(const InitializeCOMRAII &) = delete;
};
}
}

#endif
