//===- GCStrategy.cpp - Garbage Collector Description ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the policy object GCStrategy which describes the
// behavior of a given garbage collector.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/GCStrategy.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/BuiltinGCs.h"

using namespace llvm;

LLVM_INSTANTIATE_REGISTRY(GCRegistry)

GCStrategy::GCStrategy() = default;

std::unique_ptr<GCStrategy> llvm::getGCStrategy(const StringRef Name) {
  for (auto &S : GCRegistry::entries())
    if (S.getName() == Name)
      return S.instantiate();

  // We need to link all the builtin GCs when LLVM is used as a static library.
  // The linker will quite happily remove the static constructors that register
  // the builtin GCs if we don't use a function from that object. This function
  // does nothing but we need to make sure it is (or at least could be, even
  // with all optimisations enabled) called *somewhere*, and this is a good
  // place to do that: if the GC strategies are being used then this function
  // obviously can't be removed by the linker, and here it won't affect
  // performance, since there's about to be a fatal error anyway.
  llvm::linkAllBuiltinGCs();

  if (GCRegistry::begin() == GCRegistry::end()) {
    // In normal operation, the registry should not be empty.  There should
    // be the builtin GCs if nothing else.  The most likely scenario here is
    // that we got here without running the initializers used by the Registry
    // itself and it's registration mechanism.
    const std::string error =
        std::string("unsupported GC: ") + Name.str() +
        " (did you remember to link and initialize the library?)";
    report_fatal_error(Twine(error));
  } else
    report_fatal_error(Twine(std::string("unsupported GC: ") + Name.str()));
}
