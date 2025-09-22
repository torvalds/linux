//===- CommonLinkerContext.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Entry point for all global state in lldCommon. The objective is for LLD to be
// used "as a library" in a thread-safe manner.
//
// Instead of program-wide globals or function-local statics, we prefer
// aggregating all "global" states into a heap-based structure
// (CommonLinkerContext). This also achieves deterministic initialization &
// shutdown for all "global" states.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_COMMON_COMMONLINKINGCONTEXT_H
#define LLD_COMMON_COMMONLINKINGCONTEXT_H

#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Memory.h"
#include "llvm/Support/StringSaver.h"

namespace llvm {
class raw_ostream;
} // namespace llvm

namespace lld {
struct SpecificAllocBase;
class CommonLinkerContext {
public:
  CommonLinkerContext();
  virtual ~CommonLinkerContext();

  static void destroy();

  llvm::BumpPtrAllocator bAlloc;
  llvm::StringSaver saver{bAlloc};
  llvm::DenseMap<void *, SpecificAllocBase *> instances;

  ErrorHandler e;
};

// Retrieve the global state. Currently only one state can exist per process,
// but in the future we plan on supporting an arbitrary number of LLD instances
// in a single process.
CommonLinkerContext &commonContext();

template <typename T = CommonLinkerContext> T &context() {
  return static_cast<T &>(commonContext());
}

bool hasContext();

inline llvm::StringSaver &saver() { return context().saver; }
inline llvm::BumpPtrAllocator &bAlloc() { return context().bAlloc; }
} // namespace lld

#endif
