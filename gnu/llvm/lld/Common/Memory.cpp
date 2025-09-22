//===- Memory.cpp ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lld/Common/Memory.h"
#include "lld/Common/CommonLinkerContext.h"

using namespace llvm;
using namespace lld;

SpecificAllocBase *
lld::SpecificAllocBase::getOrCreate(void *tag, size_t size, size_t align,
                                    SpecificAllocBase *(&creator)(void *)) {
  auto &instances = context().instances;
  auto &instance = instances[tag];
  if (instance == nullptr) {
    void *storage = context().bAlloc.Allocate(size, align);
    instance = creator(storage);
  }
  return instance;
}
