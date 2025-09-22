//===-- ELFAttributes.h - ELF Attributes ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ELFATTRIBUTES_H
#define LLVM_SUPPORT_ELFATTRIBUTES_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include <optional>

namespace llvm {

struct TagNameItem {
  unsigned attr;
  StringRef tagName;
};

using TagNameMap = ArrayRef<TagNameItem>;

namespace ELFAttrs {

enum AttrType : unsigned { File = 1, Section = 2, Symbol = 3 };

StringRef attrTypeAsString(unsigned attr, TagNameMap tagNameMap,
                           bool hasTagPrefix = true);
std::optional<unsigned> attrTypeFromString(StringRef tag, TagNameMap tagNameMap);

// Magic numbers for ELF attributes.
enum AttrMagic { Format_Version = 0x41 };

} // namespace ELFAttrs
} // namespace llvm
#endif
