//===- ELF AttributeParser.h - ELF Attribute Parser -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ELFATTRIBUTEPARSER_H
#define LLVM_SUPPORT_ELFATTRIBUTEPARSER_H

#include "ELFAttributes.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/Error.h"

#include <optional>
#include <unordered_map>

namespace llvm {
class StringRef;
class ScopedPrinter;

class ELFAttributeParser {
  StringRef vendor;
  std::unordered_map<unsigned, unsigned> attributes;
  std::unordered_map<unsigned, StringRef> attributesStr;

  virtual Error handler(uint64_t tag, bool &handled) = 0;

protected:
  ScopedPrinter *sw;
  TagNameMap tagToStringMap;
  DataExtractor de{ArrayRef<uint8_t>{}, true, 0};
  DataExtractor::Cursor cursor{0};

  void printAttribute(unsigned tag, unsigned value, StringRef valueDesc);

  Error parseStringAttribute(const char *name, unsigned tag,
                             ArrayRef<const char *> strings);
  Error parseAttributeList(uint32_t length);
  void parseIndexList(SmallVectorImpl<uint8_t> &indexList);
  Error parseSubsection(uint32_t length);

  void setAttributeString(unsigned tag, StringRef value) {
    attributesStr.emplace(tag, value);
  }

public:
  virtual ~ELFAttributeParser() { static_cast<void>(!cursor.takeError()); }
  Error integerAttribute(unsigned tag);
  Error stringAttribute(unsigned tag);

  ELFAttributeParser(ScopedPrinter *sw, TagNameMap tagNameMap, StringRef vendor)
      : vendor(vendor), sw(sw), tagToStringMap(tagNameMap) {}

  ELFAttributeParser(TagNameMap tagNameMap, StringRef vendor)
      : vendor(vendor), sw(nullptr), tagToStringMap(tagNameMap) {}

  Error parse(ArrayRef<uint8_t> section, llvm::endianness endian);

  std::optional<unsigned> getAttributeValue(unsigned tag) const {
    auto I = attributes.find(tag);
    if (I == attributes.end())
      return std::nullopt;
    return I->second;
  }
  std::optional<StringRef> getAttributeString(unsigned tag) const {
    auto I = attributesStr.find(tag);
    if (I == attributesStr.end())
      return std::nullopt;
    return I->second;
  }
};

} // namespace llvm
#endif
