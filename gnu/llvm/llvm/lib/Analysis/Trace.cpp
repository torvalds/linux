//===- Trace.cpp - Implementation of Trace class --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class represents a single trace of LLVM basic blocks.  A trace is a
// single entry, multiple exit, region of code that is often hot.  Trace-based
// optimizations treat traces almost like they are a large, strange, basic
// block: because the trace path is assumed to be hot, optimizations for the
// fall-through path are made at the expense of the non-fall-through paths.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/Trace.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

Function *Trace::getFunction() const {
  return getEntryBasicBlock()->getParent();
}

Module *Trace::getModule() const {
  return getFunction()->getParent();
}

/// print - Write trace to output stream.
void Trace::print(raw_ostream &O) const {
  Function *F = getFunction();
  O << "; Trace from function " << F->getName() << ", blocks:\n";
  for (const_iterator i = begin(), e = end(); i != e; ++i) {
    O << "; ";
    (*i)->printAsOperand(O, true, getModule());
    O << "\n";
  }
  O << "; Trace parent function: \n" << *F;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
/// dump - Debugger convenience method; writes trace to standard error
/// output stream.
LLVM_DUMP_METHOD void Trace::dump() const {
  print(dbgs());
}
#endif
