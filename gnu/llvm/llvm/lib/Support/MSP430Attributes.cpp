//===-- MSP430Attributes.cpp - MSP430 Attributes --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/MSP430Attributes.h"

using namespace llvm;
using namespace llvm::MSP430Attrs;

static constexpr TagNameItem TagData[] = {{TagISA, "Tag_ISA"},
                                          {TagCodeModel, "Tag_Code_Model"},
                                          {TagDataModel, "Tag_Data_Model"},
                                          {TagEnumSize, "Tag_Enum_Size"}};

constexpr TagNameMap MSP430AttributeTags{TagData};
const TagNameMap &llvm::MSP430Attrs::getMSP430AttributeTags() {
  return MSP430AttributeTags;
}
