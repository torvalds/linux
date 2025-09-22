//===-- HexagonAttributes.h - Qualcomm Hexagon Attributes -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_HEXAGONATTRIBUTES_H
#define LLVM_SUPPORT_HEXAGONATTRIBUTES_H

#include "llvm/Support/ELFAttributes.h"

namespace llvm {
namespace HexagonAttrs {

const TagNameMap &getHexagonAttributeTags();

enum AttrType : unsigned {
  ARCH = 4,
  HVXARCH = 5,
  HVXIEEEFP = 6,
  HVXQFLOAT = 7,
  ZREG = 8,
  AUDIO = 9,
  CABAC = 10
};

} // namespace HexagonAttrs
} // namespace llvm

#endif
