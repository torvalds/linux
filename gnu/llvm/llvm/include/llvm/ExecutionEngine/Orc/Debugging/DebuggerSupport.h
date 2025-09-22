//===-- DebugerSupport.h - Utils for enabling debugger support --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Utilities for enabling debugger support.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_DEBUGGERSUPPORT_H
#define LLVM_EXECUTIONENGINE_ORC_DEBUGGERSUPPORT_H

#include "llvm/Support/Error.h"

namespace llvm {
namespace orc {

class LLJIT;

Error enableDebuggerSupport(LLJIT &J);

} // namespace orc
} // namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_DEBUGGERSUPPORT_H
