//===----------------------- CodeRegionGenerator.cpp ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines classes responsible for generating llvm-mca
/// CodeRegions from various types of input. llvm-mca only analyzes CodeRegions,
/// so the classes here provide the input-to-CodeRegions translation.
//
//===----------------------------------------------------------------------===//

#include "CodeRegionGenerator.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/SMLoc.h"
#include <memory>

namespace llvm {
namespace mca {

// This virtual dtor serves as the anchor for the CodeRegionGenerator class.
CodeRegionGenerator::~CodeRegionGenerator() {}

Expected<const CodeRegions &> AsmCodeRegionGenerator::parseCodeRegions(
    const std::unique_ptr<MCInstPrinter> &IP, bool SkipFailures) {
  MCTargetOptions Opts;
  Opts.PreserveAsmComments = false;
  CodeRegions &Regions = getRegions();
  MCStreamerWrapper *Str = getMCStreamer();

  // Need to initialize an MCTargetStreamer otherwise
  // certain asm directives will cause a segfault.
  // Using nulls() so that anything emitted by the MCTargetStreamer
  // doesn't show up in the llvm-mca output.
  raw_ostream &OSRef = nulls();
  formatted_raw_ostream FOSRef(OSRef);
  TheTarget.createAsmTargetStreamer(*Str, FOSRef, IP.get());

  // Create a MCAsmParser and setup the lexer to recognize llvm-mca ASM
  // comments.
  std::unique_ptr<MCAsmParser> Parser(
      createMCAsmParser(Regions.getSourceMgr(), Ctx, *Str, MAI));
  MCAsmLexer &Lexer = Parser->getLexer();
  MCACommentConsumer *CCP = getCommentConsumer();
  Lexer.setCommentConsumer(CCP);
  // Enable support for MASM literal numbers (example: 05h, 101b).
  Lexer.setLexMasmIntegers(true);

  std::unique_ptr<MCTargetAsmParser> TAP(
      TheTarget.createMCAsmParser(STI, *Parser, MCII, Opts));
  if (!TAP)
    return make_error<StringError>(
        "This target does not support assembly parsing.",
        inconvertibleErrorCode());
  Parser->setTargetParser(*TAP);
  // Parser->Run() confusingly returns true on errors, in which case the errors
  // were already shown to the user. SkipFailures implies continuing in the
  // presence of any kind of failure within the parser, in which case failing
  // input lines are not represented, but the rest of the input remains.
  if (Parser->Run(false) && !SkipFailures) {
    const char *Message = "Assembly input parsing had errors, use "
                          "-skip-unsupported-instructions=parse-failure "
                          "to drop failing lines from the input.";
    return make_error<StringError>(Message, inconvertibleErrorCode());
  }

  if (CCP->hadErr())
    return make_error<StringError>("There was an error parsing comments.",
                                   inconvertibleErrorCode());

  // Set the assembler dialect from the input. llvm-mca will use this as the
  // default dialect when printing reports.
  AssemblerDialect = Parser->getAssemblerDialect();
  return Regions;
}

void AnalysisRegionCommentConsumer::HandleComment(SMLoc Loc,
                                                  StringRef CommentText) {
  // Skip empty comments.
  StringRef Comment(CommentText);
  if (Comment.empty())
    return;

  // Skip spaces and tabs.
  unsigned Position = Comment.find_first_not_of(" \t");
  if (Position >= Comment.size())
    // We reached the end of the comment. Bail out.
    return;

  Comment = Comment.drop_front(Position);
  if (Comment.consume_front("LLVM-MCA-END")) {
    // Skip spaces and tabs.
    Position = Comment.find_first_not_of(" \t");
    if (Position < Comment.size())
      Comment = Comment.drop_front(Position);
    Regions.endRegion(Comment, Loc);
    return;
  }

  // Try to parse the LLVM-MCA-BEGIN comment.
  if (!Comment.consume_front("LLVM-MCA-BEGIN"))
    return;

  // Skip spaces and tabs.
  Position = Comment.find_first_not_of(" \t");
  if (Position < Comment.size())
    Comment = Comment.drop_front(Position);
  // Use the rest of the string as a descriptor for this code snippet.
  Regions.beginRegion(Comment, Loc);
}

void InstrumentRegionCommentConsumer::HandleComment(SMLoc Loc,
                                                    StringRef CommentText) {
  // Skip empty comments.
  StringRef Comment(CommentText);
  if (Comment.empty())
    return;

  // Skip spaces and tabs.
  unsigned Position = Comment.find_first_not_of(" \t");
  if (Position >= Comment.size())
    // We reached the end of the comment. Bail out.
    return;
  Comment = Comment.drop_front(Position);

  // Bail out if not an MCA style comment
  if (!Comment.consume_front("LLVM-MCA-"))
    return;

  // Skip AnalysisRegion comments
  if (Comment.consume_front("BEGIN") || Comment.consume_front("END"))
    return;

  if (IM.shouldIgnoreInstruments())
    return;

  auto [InstrumentKind, Data] = Comment.split(" ");

  // An error if not of the form LLVM-MCA-TARGET-KIND
  if (!IM.supportsInstrumentType(InstrumentKind)) {
    if (InstrumentKind.empty())
      SM.PrintMessage(
          Loc, llvm::SourceMgr::DK_Error,
          "No instrumentation kind was provided in LLVM-MCA comment");
    else
      SM.PrintMessage(Loc, llvm::SourceMgr::DK_Error,
                      "Unknown instrumentation type in LLVM-MCA comment: " +
                          InstrumentKind);
    FoundError = true;
    return;
  }

  UniqueInstrument I = IM.createInstrument(InstrumentKind, Data);
  if (!I) {
    if (Data.empty())
      SM.PrintMessage(Loc, llvm::SourceMgr::DK_Error,
                      "Failed to create " + InstrumentKind +
                          " instrument with no data");
    else
      SM.PrintMessage(Loc, llvm::SourceMgr::DK_Error,
                      "Failed to create " + InstrumentKind +
                          " instrument with data: " + Data);
    FoundError = true;
    return;
  }

  // End InstrumentType region if one is open
  if (Regions.isRegionActive(InstrumentKind))
    Regions.endRegion(InstrumentKind, Loc);
  // Start new instrumentation region
  Regions.beginRegion(InstrumentKind, Loc, std::move(I));
}

} // namespace mca
} // namespace llvm
