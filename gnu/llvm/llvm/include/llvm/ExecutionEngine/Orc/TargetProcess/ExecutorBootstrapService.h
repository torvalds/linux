//===- ExecutorService.h - Provide bootstrap symbols to session -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Provides a service by supplying some set of bootstrap symbols.
//
// FIXME: The functionality in this file should be moved to the ORC runtime.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_EXECUTORBOOTSTRAPSERVICE_H
#define LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_EXECUTORBOOTSTRAPSERVICE_H

#include "llvm/ADT/StringMap.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"

namespace llvm {
namespace orc {

class ExecutorBootstrapService {
public:
  virtual ~ExecutorBootstrapService();

  virtual void
  addBootstrapSymbols(StringMap<ExecutorAddr> &BootstrapSymbols) = 0;
  virtual Error shutdown() = 0;
};

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_EXECUTORBOOTSTRAPSERVICE_H
