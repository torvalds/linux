//===--- ELFAttributeParser.cpp - ELF Attribute Parser --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/ELFAttributeParser.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/ScopedPrinter.h"

using namespace llvm;
using namespace llvm::ELFAttrs;

static constexpr EnumEntry<unsigned> tagNames[] = {
    {"Tag_File", ELFAttrs::File},
    {"Tag_Section", ELFAttrs::Section},
    {"Tag_Symbol", ELFAttrs::Symbol},
};

Error ELFAttributeParser::parseStringAttribute(const char *name, unsigned tag,
                                               ArrayRef<const char *> strings) {
  uint64_t value = de.getULEB128(cursor);
  if (value >= strings.size()) {
    printAttribute(tag, value, "");
    return createStringError(errc::invalid_argument,
                             "unknown " + Twine(name) +
                                 " value: " + Twine(value));
  }
  printAttribute(tag, value, strings[value]);
  return Error::success();
}

Error ELFAttributeParser::integerAttribute(unsigned tag) {
  StringRef tagName =
      ELFAttrs::attrTypeAsString(tag, tagToStringMap, /*hasTagPrefix=*/false);
  uint64_t value = de.getULEB128(cursor);
  attributes.insert(std::make_pair(tag, value));

  if (sw) {
    DictScope scope(*sw, "Attribute");
    sw->printNumber("Tag", tag);
    if (!tagName.empty())
      sw->printString("TagName", tagName);
    sw->printNumber("Value", value);
  }
  return Error::success();
}

Error ELFAttributeParser::stringAttribute(unsigned tag) {
  StringRef tagName =
      ELFAttrs::attrTypeAsString(tag, tagToStringMap, /*hasTagPrefix=*/false);
  StringRef desc = de.getCStrRef(cursor);
  setAttributeString(tag, desc);

  if (sw) {
    DictScope scope(*sw, "Attribute");
    sw->printNumber("Tag", tag);
    if (!tagName.empty())
      sw->printString("TagName", tagName);
    sw->printString("Value", desc);
  }
  return Error::success();
}

void ELFAttributeParser::printAttribute(unsigned tag, unsigned value,
                                        StringRef valueDesc) {
  attributes.insert(std::make_pair(tag, value));

  if (sw) {
    StringRef tagName = ELFAttrs::attrTypeAsString(tag, tagToStringMap,
                                                   /*hasTagPrefix=*/false);
    DictScope as(*sw, "Attribute");
    sw->printNumber("Tag", tag);
    sw->printNumber("Value", value);
    if (!tagName.empty())
      sw->printString("TagName", tagName);
    if (!valueDesc.empty())
      sw->printString("Description", valueDesc);
  }
}

void ELFAttributeParser::parseIndexList(SmallVectorImpl<uint8_t> &indexList) {
  for (;;) {
    uint64_t value = de.getULEB128(cursor);
    if (!cursor || !value)
      break;
    indexList.push_back(value);
  }
}

Error ELFAttributeParser::parseAttributeList(uint32_t length) {
  uint64_t pos;
  uint64_t end = cursor.tell() + length;
  while ((pos = cursor.tell()) < end) {
    uint64_t tag = de.getULEB128(cursor);
    bool handled;
    if (Error e = handler(tag, handled))
      return e;

    if (!handled) {
      if (tag < 32) {
        return createStringError(errc::invalid_argument,
                                 "invalid tag 0x" + Twine::utohexstr(tag) +
                                     " at offset 0x" + Twine::utohexstr(pos));
      }

      if (tag % 2 == 0) {
        if (Error e = integerAttribute(tag))
          return e;
      } else {
        if (Error e = stringAttribute(tag))
          return e;
      }
    }
  }
  return Error::success();
}

Error ELFAttributeParser::parseSubsection(uint32_t length) {
  uint64_t end = cursor.tell() - sizeof(length) + length;
  StringRef vendorName = de.getCStrRef(cursor);
  if (sw) {
    sw->printNumber("SectionLength", length);
    sw->printString("Vendor", vendorName);
  }

  // Handle a subsection with an unrecognized vendor-name by skipping
  // over it to the next subsection. ADDENDA32 in the Arm ABI defines
  // that vendor attribute sections must not affect compatibility, so
  // this should always be safe.
  if (vendorName.lower() != vendor) {
    cursor.seek(end);
    return Error::success();
  }

  while (cursor.tell() < end) {
    /// Tag_File | Tag_Section | Tag_Symbol   uleb128:byte-size
    uint8_t tag = de.getU8(cursor);
    uint32_t size = de.getU32(cursor);
    if (!cursor)
      return cursor.takeError();

    if (sw) {
      sw->printEnum("Tag", tag, ArrayRef(tagNames));
      sw->printNumber("Size", size);
    }
    if (size < 5)
      return createStringError(errc::invalid_argument,
                               "invalid attribute size " + Twine(size) +
                                   " at offset 0x" +
                                   Twine::utohexstr(cursor.tell() - 5));

    StringRef scopeName, indexName;
    SmallVector<uint8_t, 8> indices;
    switch (tag) {
    case ELFAttrs::File:
      scopeName = "FileAttributes";
      break;
    case ELFAttrs::Section:
      scopeName = "SectionAttributes";
      indexName = "Sections";
      parseIndexList(indices);
      break;
    case ELFAttrs::Symbol:
      scopeName = "SymbolAttributes";
      indexName = "Symbols";
      parseIndexList(indices);
      break;
    default:
      return createStringError(errc::invalid_argument,
                               "unrecognized tag 0x" + Twine::utohexstr(tag) +
                                   " at offset 0x" +
                                   Twine::utohexstr(cursor.tell() - 5));
    }

    if (sw) {
      DictScope scope(*sw, scopeName);
      if (!indices.empty())
        sw->printList(indexName, indices);
      if (Error e = parseAttributeList(size - 5))
        return e;
    } else if (Error e = parseAttributeList(size - 5))
      return e;
  }
  return Error::success();
}

Error ELFAttributeParser::parse(ArrayRef<uint8_t> section,
                                llvm::endianness endian) {
  unsigned sectionNumber = 0;
  de = DataExtractor(section, endian == llvm::endianness::little, 0);

  // For early returns, we have more specific errors, consume the Error in
  // cursor.
  struct ClearCursorError {
    DataExtractor::Cursor &cursor;
    ~ClearCursorError() { consumeError(cursor.takeError()); }
  } clear{cursor};

  // Unrecognized format-version.
  uint8_t formatVersion = de.getU8(cursor);
  if (formatVersion != ELFAttrs::Format_Version)
    return createStringError(errc::invalid_argument,
                             "unrecognized format-version: 0x" +
                                 utohexstr(formatVersion));

  while (!de.eof(cursor)) {
    uint32_t sectionLength = de.getU32(cursor);
    if (!cursor)
      return cursor.takeError();

    if (sw) {
      sw->startLine() << "Section " << ++sectionNumber << " {\n";
      sw->indent();
    }

    if (sectionLength < 4 || cursor.tell() - 4 + sectionLength > section.size())
      return createStringError(errc::invalid_argument,
                               "invalid section length " +
                                   Twine(sectionLength) + " at offset 0x" +
                                   utohexstr(cursor.tell() - 4));

    if (Error e = parseSubsection(sectionLength))
      return e;
    if (sw) {
      sw->unindent();
      sw->startLine() << "}\n";
    }
  }

  return cursor.takeError();
}
