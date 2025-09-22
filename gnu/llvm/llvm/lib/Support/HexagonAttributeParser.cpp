//===-- HexagonAttributeParser.cpp - Hexagon Attribute Parser -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/HexagonAttributeParser.h"

using namespace llvm;

const HexagonAttributeParser::DisplayHandler
    HexagonAttributeParser::DisplayRoutines[] = {
        {
            HexagonAttrs::ARCH,
            &ELFAttributeParser::integerAttribute,
        },
        {
            HexagonAttrs::HVXARCH,
            &ELFAttributeParser::integerAttribute,
        },
        {
            HexagonAttrs::HVXIEEEFP,
            &ELFAttributeParser::integerAttribute,
        },
        {
            HexagonAttrs::HVXQFLOAT,
            &ELFAttributeParser::integerAttribute,
        },
        {
            HexagonAttrs::ZREG,
            &ELFAttributeParser::integerAttribute,
        },
        {
            HexagonAttrs::AUDIO,
            &ELFAttributeParser::integerAttribute,
        },
        {
            HexagonAttrs::CABAC,
            &ELFAttributeParser::integerAttribute,
        }};

Error HexagonAttributeParser::handler(uint64_t Tag, bool &Handled) {
  Handled = false;
  for (const auto &R : DisplayRoutines) {
    if (uint64_t(R.Attribute) == Tag) {
      if (Error E = (this->*R.Routine)(Tag))
        return E;
      Handled = true;
      break;
    }
  }
  return Error::success();
}
