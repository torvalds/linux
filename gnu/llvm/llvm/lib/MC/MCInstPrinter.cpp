//===- MCInstPrinter.cpp - Convert an MCInst to target assembly syntax ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCInstPrinter.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <cinttypes>
#include <cstdint>

using namespace llvm;

void llvm::dumpBytes(ArrayRef<uint8_t> bytes, raw_ostream &OS) {
  static const char hex_rep[] = "0123456789abcdef";
  bool First = true;
  for (char i: bytes) {
    if (First)
      First = false;
    else
      OS << ' ';
    OS << hex_rep[(i & 0xF0) >> 4];
    OS << hex_rep[i & 0xF];
  }
}

MCInstPrinter::~MCInstPrinter() = default;

/// getOpcodeName - Return the name of the specified opcode enum (e.g.
/// "MOV32ri") or empty if we can't resolve it.
StringRef MCInstPrinter::getOpcodeName(unsigned Opcode) const {
  return MII.getName(Opcode);
}

void MCInstPrinter::printRegName(raw_ostream &OS, MCRegister Reg) const {
  llvm_unreachable("Target should implement this");
}

void MCInstPrinter::printAnnotation(raw_ostream &OS, StringRef Annot) {
  if (!Annot.empty()) {
    if (CommentStream) {
      (*CommentStream) << Annot;
      // By definition (see MCInstPrinter.h), CommentStream must end with
      // a newline after each comment.
      if (Annot.back() != '\n')
        (*CommentStream) << '\n';
    } else
      OS << " " << MAI.getCommentString() << " " << Annot;
  }
}

static bool matchAliasCondition(const MCInst &MI, const MCSubtargetInfo *STI,
                                const MCRegisterInfo &MRI, unsigned &OpIdx,
                                const AliasMatchingData &M,
                                const AliasPatternCond &C,
                                bool &OrPredicateResult) {
  // Feature tests are special, they don't consume operands.
  if (C.Kind == AliasPatternCond::K_Feature)
    return STI->getFeatureBits().test(C.Value);
  if (C.Kind == AliasPatternCond::K_NegFeature)
    return !STI->getFeatureBits().test(C.Value);
  // For feature tests where just one feature is required in a list, set the
  // predicate result bit to whether the expression will return true, and only
  // return the real result at the end of list marker.
  if (C.Kind == AliasPatternCond::K_OrFeature) {
    OrPredicateResult |= STI->getFeatureBits().test(C.Value);
    return true;
  }
  if (C.Kind == AliasPatternCond::K_OrNegFeature) {
    OrPredicateResult |= !(STI->getFeatureBits().test(C.Value));
    return true;
  }
  if (C.Kind == AliasPatternCond::K_EndOrFeatures) {
    bool Res = OrPredicateResult;
    OrPredicateResult = false;
    return Res;
  }

  // Get and consume an operand.
  const MCOperand &Opnd = MI.getOperand(OpIdx);
  ++OpIdx;

  // Check the specific condition for the operand.
  switch (C.Kind) {
  case AliasPatternCond::K_Imm:
    // Operand must be a specific immediate.
    return Opnd.isImm() && Opnd.getImm() == int32_t(C.Value);
  case AliasPatternCond::K_Reg:
    // Operand must be a specific register.
    return Opnd.isReg() && Opnd.getReg() == C.Value;
  case AliasPatternCond::K_TiedReg:
    // Operand must match the register of another operand.
    return Opnd.isReg() && Opnd.getReg() == MI.getOperand(C.Value).getReg();
  case AliasPatternCond::K_RegClass:
    // Operand must be a register in this class. Value is a register class id.
    return Opnd.isReg() && MRI.getRegClass(C.Value).contains(Opnd.getReg());
  case AliasPatternCond::K_Custom:
    // Operand must match some custom criteria.
    return M.ValidateMCOperand(Opnd, *STI, C.Value);
  case AliasPatternCond::K_Ignore:
    // Operand can be anything.
    return true;
  case AliasPatternCond::K_Feature:
  case AliasPatternCond::K_NegFeature:
  case AliasPatternCond::K_OrFeature:
  case AliasPatternCond::K_OrNegFeature:
  case AliasPatternCond::K_EndOrFeatures:
    llvm_unreachable("handled earlier");
  }
  llvm_unreachable("invalid kind");
}

const char *MCInstPrinter::matchAliasPatterns(const MCInst *MI,
                                              const MCSubtargetInfo *STI,
                                              const AliasMatchingData &M) {
  // Binary search by opcode. Return false if there are no aliases for this
  // opcode.
  auto It = lower_bound(M.OpToPatterns, MI->getOpcode(),
                        [](const PatternsForOpcode &L, unsigned Opcode) {
                          return L.Opcode < Opcode;
                        });
  if (It == M.OpToPatterns.end() || It->Opcode != MI->getOpcode())
    return nullptr;

  // Try all patterns for this opcode.
  uint32_t AsmStrOffset = ~0U;
  ArrayRef<AliasPattern> Patterns =
      M.Patterns.slice(It->PatternStart, It->NumPatterns);
  for (const AliasPattern &P : Patterns) {
    // Check operand count first.
    if (MI->getNumOperands() != P.NumOperands)
      return nullptr;

    // Test all conditions for this pattern.
    ArrayRef<AliasPatternCond> Conds =
        M.PatternConds.slice(P.AliasCondStart, P.NumConds);
    unsigned OpIdx = 0;
    bool OrPredicateResult = false;
    if (llvm::all_of(Conds, [&](const AliasPatternCond &C) {
          return matchAliasCondition(*MI, STI, MRI, OpIdx, M, C,
                                     OrPredicateResult);
        })) {
      // If all conditions matched, use this asm string.
      AsmStrOffset = P.AsmStrOffset;
      break;
    }
  }

  // If no alias matched, don't print an alias.
  if (AsmStrOffset == ~0U)
    return nullptr;

  // Go to offset AsmStrOffset and use the null terminated string there. The
  // offset should point to the beginning of an alias string, so it should
  // either be zero or be preceded by a null byte.
  assert(AsmStrOffset < M.AsmStrings.size() &&
         (AsmStrOffset == 0 || M.AsmStrings[AsmStrOffset - 1] == '\0') &&
         "bad asm string offset");
  return M.AsmStrings.data() + AsmStrOffset;
}

// For asm-style hex (e.g. 0ffh) the first digit always has to be a number.
static bool needsLeadingZero(uint64_t Value)
{
  while (Value)
  {
    uint64_t digit = (Value >> 60) & 0xf;
    if (digit != 0)
      return (digit >= 0xa);
    Value <<= 4;
  }
  return false;
}

format_object<int64_t> MCInstPrinter::formatDec(int64_t Value) const {
  return format("%" PRId64, Value);
}

format_object<int64_t> MCInstPrinter::formatHex(int64_t Value) const {
  switch (PrintHexStyle) {
  case HexStyle::C:
    if (Value < 0) {
      if (Value == std::numeric_limits<int64_t>::min())
        return format<int64_t>("-0x8000000000000000", Value);
      return format("-0x%" PRIx64, -Value);
    }
    return format("0x%" PRIx64, Value);
  case HexStyle::Asm:
    if (Value < 0) {
      if (Value == std::numeric_limits<int64_t>::min())
        return format<int64_t>("-8000000000000000h", Value);
      if (needsLeadingZero(-(uint64_t)(Value)))
        return format("-0%" PRIx64 "h", -Value);
      return format("-%" PRIx64 "h", -Value);
    }
    if (needsLeadingZero((uint64_t)(Value)))
      return format("0%" PRIx64 "h", Value);
    return format("%" PRIx64 "h", Value);
  }
  llvm_unreachable("unsupported print style");
}

format_object<uint64_t> MCInstPrinter::formatHex(uint64_t Value) const {
  switch(PrintHexStyle) {
  case HexStyle::C:
     return format("0x%" PRIx64, Value);
  case HexStyle::Asm:
    if (needsLeadingZero(Value))
      return format("0%" PRIx64 "h", Value);
    else
      return format("%" PRIx64 "h", Value);
  }
  llvm_unreachable("unsupported print style");
}

MCInstPrinter::WithMarkup MCInstPrinter::markup(raw_ostream &OS,
                                                Markup S) const {
  return WithMarkup(OS, S, getUseMarkup(), getUseColor());
}

MCInstPrinter::WithMarkup::WithMarkup(raw_ostream &OS, Markup M,
                                      bool EnableMarkup, bool EnableColor)
    : OS(OS), EnableMarkup(EnableMarkup), EnableColor(EnableColor) {
  if (EnableColor) {
    switch (M) {
    case Markup::Immediate:
      OS.changeColor(raw_ostream::RED);
      break;
    case Markup::Register:
      OS.changeColor(raw_ostream::CYAN);
      break;
    case Markup::Target:
      OS.changeColor(raw_ostream::YELLOW);
      break;
    case Markup::Memory:
      OS.changeColor(raw_ostream::GREEN);
      break;
    }
  }

  if (EnableMarkup) {
    switch (M) {
    case Markup::Immediate:
      OS << "<imm:";
      break;
    case Markup::Register:
      OS << "<reg:";
      break;
    case Markup::Target:
      OS << "<target:";
      break;
    case Markup::Memory:
      OS << "<mem:";
      break;
    }
  }
}

MCInstPrinter::WithMarkup::~WithMarkup() {
  if (EnableMarkup)
    OS << '>';
  if (EnableColor)
    OS.resetColor();
}
