//===- PatternParser.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file Contains tools to parse MIR patterns from TableGen DAG elements.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_GLOBALISEL_PATTERNPARSER_H
#define LLVM_UTILS_GLOBALISEL_PATTERNPARSER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/SMLoc.h"
#include <memory>

namespace llvm {
class CodeGenTarget;
class DagInit;
class Init;
class Record;
class StringRef;
class StringInit;

namespace gi {
class InstructionPattern;
class Pattern;
class PatFrag;

/// Helper class to parse MIR Pattern lists.
///
/// e.g., `(match (G_FADD $x, $y, $z), (G_FNEG $y, $z))`
class PatternParser {
  const CodeGenTarget &CGT;
  ArrayRef<SMLoc> DiagLoc;

  mutable SmallPtrSet<const PatFrag *, 2> SeenPatFrags;

public:
  PatternParser(const CodeGenTarget &CGT, ArrayRef<SMLoc> DiagLoc)
      : CGT(CGT), DiagLoc(DiagLoc) {}

  /// Parses a list of patterns such as:
  ///   (Operator (Pattern1 ...), (Pattern2 ...))
  /// \param List         DagInit of the expected pattern list.
  /// \param ParseAction  Callback to handle a succesfully parsed pattern.
  /// \param Operator     The name of the operator, e.g. "match"
  /// \param AnonPatNamePrefix Prefix for anonymous pattern names.
  /// \return true on success, false on failure.
  bool
  parsePatternList(const DagInit &List,
                   function_ref<bool(std::unique_ptr<Pattern>)> ParseAction,
                   StringRef Operator, StringRef AnonPatNamePrefix);

  /// \returns all PatFrags encountered by this PatternParser.
  const auto &getSeenPatFrags() const { return SeenPatFrags; }

private:
  /// Parse any InstructionPattern from a TableGen Init.
  /// \param Arg Init to parse.
  /// \param PatName Name of the pattern that will be parsed.
  /// \return the parsed pattern on success, nullptr on failure.
  std::unique_ptr<Pattern> parseInstructionPattern(const Init &Arg,
                                                   StringRef PatName);

  /// Parse a WipOpcodeMatcher from a TableGen Init.
  /// \param Arg Init to parse.
  /// \param PatName Name of the pattern that will be parsed.
  /// \return the parsed pattern on success, nullptr on failure.
  std::unique_ptr<Pattern> parseWipMatchOpcodeMatcher(const Init &Arg,
                                                      StringRef PatName);

  /// Parses an Operand of an InstructionPattern from a TableGen Init.
  /// \param IP InstructionPattern for which we're parsing.
  /// \param OpInit Init to parse.
  /// \param OpName Name of the operand to parse.
  /// \return true on success, false on failure.
  bool parseInstructionPatternOperand(InstructionPattern &IP,
                                      const Init *OpInit,
                                      const StringInit *OpName);

  /// Parses a MIFlag for an InstructionPattern from a TableGen Init.
  /// \param IP InstructionPattern for which we're parsing.
  /// \param Op Init to parse.
  /// \return true on success, false on failure.
  bool parseInstructionPatternMIFlags(InstructionPattern &IP,
                                      const DagInit *Op);

  /// (Uncached) PatFrag parsing implementation.
  /// \param Def PatFrag def to parsee.
  /// \return the parsed PatFrag on success, nullptr on failure.
  std::unique_ptr<PatFrag> parsePatFragImpl(const Record *Def);

  /// Parses the in or out parameter list of a PatFrag.
  /// \param OpsList Init to parse.
  /// \param ParseAction Callback on successful parse, with the name of
  ///                     the parameter and its \ref PatFrag::ParamKind
  /// \return true on success, false on failure.
  bool
  parsePatFragParamList(const DagInit &OpsList,
                        function_ref<bool(StringRef, unsigned)> ParseAction);

  /// Cached PatFrag parser. This avoids duplicate work by keeping track of
  /// already-parsed PatFrags.
  /// \param Def PatFrag def to parsee.
  /// \return the parsed PatFrag on success, nullptr on failure.
  const PatFrag *parsePatFrag(const Record *Def);
};

} // namespace gi
} // namespace llvm

#endif
