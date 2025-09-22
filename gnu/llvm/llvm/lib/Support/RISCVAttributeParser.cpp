//===-- RISCVAttributeParser.cpp - RISCV Attribute Parser -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/RISCVAttributeParser.h"
#include "llvm/ADT/StringExtras.h"

using namespace llvm;

const RISCVAttributeParser::DisplayHandler
    RISCVAttributeParser::displayRoutines[] = {
        {
            RISCVAttrs::ARCH,
            &ELFAttributeParser::stringAttribute,
        },
        {
            RISCVAttrs::PRIV_SPEC,
            &ELFAttributeParser::integerAttribute,
        },
        {
            RISCVAttrs::PRIV_SPEC_MINOR,
            &ELFAttributeParser::integerAttribute,
        },
        {
            RISCVAttrs::PRIV_SPEC_REVISION,
            &ELFAttributeParser::integerAttribute,
        },
        {
            RISCVAttrs::STACK_ALIGN,
            &RISCVAttributeParser::stackAlign,
        },
        {
            RISCVAttrs::UNALIGNED_ACCESS,
            &RISCVAttributeParser::unalignedAccess,
        },
        {
            RISCVAttrs::ATOMIC_ABI,
            &RISCVAttributeParser::atomicAbi,
        },
};

Error RISCVAttributeParser::atomicAbi(unsigned Tag) {
  uint64_t Value = de.getULEB128(cursor);
  printAttribute(Tag, Value, "Atomic ABI is " + utostr(Value));
  return Error::success();
}

Error RISCVAttributeParser::unalignedAccess(unsigned tag) {
  static const char *strings[] = {"No unaligned access", "Unaligned access"};
  return parseStringAttribute("Unaligned_access", tag, ArrayRef(strings));
}

Error RISCVAttributeParser::stackAlign(unsigned tag) {
  uint64_t value = de.getULEB128(cursor);
  std::string description =
      "Stack alignment is " + utostr(value) + std::string("-bytes");
  printAttribute(tag, value, description);
  return Error::success();
}

Error RISCVAttributeParser::handler(uint64_t tag, bool &handled) {
  handled = false;
  for (const auto &AH : displayRoutines) {
    if (uint64_t(AH.attribute) == tag) {
      if (Error e = (this->*AH.routine)(tag))
        return e;
      handled = true;
      break;
    }
  }

  return Error::success();
}
