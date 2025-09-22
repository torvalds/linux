//===-- MCTargetAsmParser.cpp - Target Assembly Parser --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCRegister.h"

using namespace llvm;

MCTargetAsmParser::MCTargetAsmParser(MCTargetOptions const &MCOptions,
                                     const MCSubtargetInfo &STI,
                                     const MCInstrInfo &MII)
    : MCOptions(MCOptions), STI(&STI), MII(MII) {}

MCTargetAsmParser::~MCTargetAsmParser() = default;

MCSubtargetInfo &MCTargetAsmParser::copySTI() {
  MCSubtargetInfo &STICopy = getContext().getSubtargetCopy(getSTI());
  STI = &STICopy;
  return STICopy;
}

const MCSubtargetInfo &MCTargetAsmParser::getSTI() const {
  return *STI;
}

ParseStatus MCTargetAsmParser::parseDirective(AsmToken DirectiveID) {
  SMLoc StartTokLoc = getTok().getLoc();
  // Delegate to ParseDirective by default for transition period. Once the
  // transition is over, this method should just return NoMatch.
  bool Res = ParseDirective(DirectiveID);

  // Some targets erroneously report success after emitting an error.
  if (getParser().hasPendingError())
    return ParseStatus::Failure;

  // ParseDirective returns true if there was an error or if the directive is
  // not target-specific. Disambiguate the two cases by comparing position of
  // the lexer before and after calling the method: if no tokens were consumed,
  // there was no match, otherwise there was a failure.
  if (!Res)
    return ParseStatus::Success;
  if (getTok().getLoc() != StartTokLoc)
    return ParseStatus::Failure;
  return ParseStatus::NoMatch;
}

bool MCTargetAsmParser::areEqualRegs(const MCParsedAsmOperand &Op1,
                                     const MCParsedAsmOperand &Op2) const {
  return Op1.isReg() && Op2.isReg() && Op1.getReg() == Op2.getReg();
}
