//===-- SystemInitializer.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INITIALIZATION_SYSTEMINITIALIZER_H
#define LLDB_INITIALIZATION_SYSTEMINITIALIZER_H

#include "llvm/Support/Error.h"

#include <string>

namespace lldb_private {

class SystemInitializer {
public:
  SystemInitializer();
  virtual ~SystemInitializer();

  virtual llvm::Error Initialize() = 0;
  virtual void Terminate() = 0;
};
}

#endif
