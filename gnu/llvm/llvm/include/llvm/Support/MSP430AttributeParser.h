//===-- MSP430AttributeParser.h - MSP430 Attribute Parser -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains support routines for parsing MSP430 ELF build attributes.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_MSP430ATTRIBUTEPARSER_H
#define LLVM_SUPPORT_MSP430ATTRIBUTEPARSER_H

#include "llvm/Support/ELFAttributeParser.h"
#include "llvm/Support/MSP430Attributes.h"

namespace llvm {
class MSP430AttributeParser : public ELFAttributeParser {
  struct DisplayHandler {
    MSP430Attrs::AttrType Attribute;
    Error (MSP430AttributeParser::*Routine)(MSP430Attrs::AttrType);
  };
  static const std::array<DisplayHandler, 4> DisplayRoutines;

  Error parseISA(MSP430Attrs::AttrType Tag);
  Error parseCodeModel(MSP430Attrs::AttrType Tag);
  Error parseDataModel(MSP430Attrs::AttrType Tag);
  Error parseEnumSize(MSP430Attrs::AttrType Tag);

  Error handler(uint64_t Tag, bool &Handled) override;

public:
  MSP430AttributeParser(ScopedPrinter *SW)
      : ELFAttributeParser(SW, MSP430Attrs::getMSP430AttributeTags(),
                           "mspabi") {}
  MSP430AttributeParser()
      : ELFAttributeParser(MSP430Attrs::getMSP430AttributeTags(), "mspabi") {}
};
} // namespace llvm

#endif
