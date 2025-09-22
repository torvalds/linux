//===- OutputSection.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "OutputSection.h"
#include "OutputSegment.h"

using namespace llvm;
using namespace lld;
using namespace lld::macho;

uint64_t OutputSection::getSegmentOffset() const { return addr - parent->addr; }

void OutputSection::assignAddressesToStartEndSymbols() {
  for (Defined *d : sectionStartSymbols)
    d->value = addr;
  for (Defined *d : sectionEndSymbols)
    d->value = addr + getSize();
}
