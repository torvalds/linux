//===- CommonLinkerContext.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lld/Common/CommonLinkerContext.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Memory.h"

#include "llvm/CodeGen/CommandFlags.h"

using namespace llvm;
using namespace lld;

// Reference to the current LLD instance. This is a temporary situation, until
// we pass this context everywhere by reference, or we make it a thread_local,
// as in https://reviews.llvm.org/D108850?id=370678 where each thread can be
// associated with a LLD instance. Only then will LLD be free of global
// state.
static CommonLinkerContext *lctx;

CommonLinkerContext::CommonLinkerContext() {
  lctx = this;
  // Fire off the static initializations in CGF's constructor.
  codegen::RegisterCodeGenFlags CGF;
}

CommonLinkerContext::~CommonLinkerContext() {
  assert(lctx);
  // Explicitly call the destructors since we created the objects with placement
  // new in SpecificAlloc::create().
  for (auto &it : instances)
    it.second->~SpecificAllocBase();
  lctx = nullptr;
}

CommonLinkerContext &lld::commonContext() {
  assert(lctx);
  return *lctx;
}

bool lld::hasContext() { return lctx != nullptr; }

void CommonLinkerContext::destroy() {
  if (lctx == nullptr)
    return;
  delete lctx;
}
