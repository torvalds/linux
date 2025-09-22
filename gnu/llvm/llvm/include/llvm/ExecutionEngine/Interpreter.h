//===-- Interpreter.h - Abstract Execution Engine Interface -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file forces the interpreter to link in on certain operating systems.
// (Windows).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_INTERPRETER_H
#define LLVM_EXECUTIONENGINE_INTERPRETER_H

#include "llvm/ExecutionEngine/ExecutionEngine.h"

extern "C" void LLVMLinkInInterpreter();

namespace {
  struct ForceInterpreterLinking {
    ForceInterpreterLinking() { LLVMLinkInInterpreter(); }
  } ForceInterpreterLinking;
}

#endif
