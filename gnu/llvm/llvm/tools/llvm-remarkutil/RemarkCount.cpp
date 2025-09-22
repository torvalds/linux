//===- RemarkCount.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Count remarks using `instruction-count` for asm-printer remarks and
// `annotation-count` for annotation-remarks
//
//===----------------------------------------------------------------------===//
#include "RemarkUtilHelpers.h"
#include "RemarkUtilRegistry.h"

using namespace llvm;
using namespace remarks;
using namespace llvm::remarkutil;

static cl::SubCommand InstructionCount(
    "instruction-count",
    "Function instruction count information (requires asm-printer remarks)");
static cl::SubCommand
    AnnotationCount("annotation-count",
                    "Collect count information from annotation remarks (uses "
                    "AnnotationRemarksPass)");

namespace instructioncount {
INPUT_FORMAT_COMMAND_LINE_OPTIONS(InstructionCount)
INPUT_OUTPUT_COMMAND_LINE_OPTIONS(InstructionCount)
DEBUG_LOC_INFO_COMMAND_LINE_OPTIONS(InstructionCount)
} // namespace instructioncount

namespace annotationcount {
INPUT_FORMAT_COMMAND_LINE_OPTIONS(AnnotationCount)
static cl::opt<std::string> AnnotationTypeToCollect(
    "annotation-type", cl::desc("annotation-type remark to collect count for"),
    cl::sub(AnnotationCount));
INPUT_OUTPUT_COMMAND_LINE_OPTIONS(AnnotationCount)
DEBUG_LOC_INFO_COMMAND_LINE_OPTIONS(AnnotationCount)
} // namespace annotationcount

static bool shouldSkipRemark(bool UseDebugLoc, Remark &Remark) {
  return UseDebugLoc && !Remark.Loc.has_value();
}

namespace instructioncount {
/// Outputs all instruction count remarks in the file as a CSV.
/// \returns Error::success() on success, and an Error otherwise.
static Error tryInstructionCount() {
  // Create the output buffer.
  auto MaybeOF = getOutputFileWithFlags(OutputFileName,
                                        /*Flags = */ sys::fs::OF_TextWithCRLF);
  if (!MaybeOF)
    return MaybeOF.takeError();
  auto OF = std::move(*MaybeOF);
  // Create a parser for the user-specified input format.
  auto MaybeBuf = getInputMemoryBuffer(InputFileName);
  if (!MaybeBuf)
    return MaybeBuf.takeError();
  auto MaybeParser = createRemarkParser(InputFormat, (*MaybeBuf)->getBuffer());
  if (!MaybeParser)
    return MaybeParser.takeError();
  // Emit CSV header.
  if (UseDebugLoc)
    OF->os() << "Source,";
  OF->os() << "Function,InstructionCount\n";
  // Parse all remarks. Whenever we see an instruction count remark, output
  // the file name and the number of instructions.
  auto &Parser = **MaybeParser;
  auto MaybeRemark = Parser.next();
  for (; MaybeRemark; MaybeRemark = Parser.next()) {
    auto &Remark = **MaybeRemark;
    if (Remark.RemarkName != "InstructionCount")
      continue;
    if (shouldSkipRemark(UseDebugLoc, Remark))
      continue;
    auto *InstrCountArg = find_if(Remark.Args, [](const Argument &Arg) {
      return Arg.Key == "NumInstructions";
    });
    assert(InstrCountArg != Remark.Args.end() &&
           "Expected instruction count remarks to have a NumInstructions key?");
    if (UseDebugLoc) {
      std::string Loc = Remark.Loc->SourceFilePath.str() + ":" +
                        std::to_string(Remark.Loc->SourceLine) + +":" +
                        std::to_string(Remark.Loc->SourceColumn);
      OF->os() << Loc << ",";
    }
    OF->os() << Remark.FunctionName << "," << InstrCountArg->Val << "\n";
  }
  auto E = MaybeRemark.takeError();
  if (!E.isA<EndOfFileError>())
    return E;
  consumeError(std::move(E));
  OF->keep();
  return Error::success();
}
} // namespace instructioncount

namespace annotationcount {
static Error tryAnnotationCount() {
  // Create the output buffer.
  auto MaybeOF = getOutputFileWithFlags(OutputFileName,
                                        /*Flags = */ sys::fs::OF_TextWithCRLF);
  if (!MaybeOF)
    return MaybeOF.takeError();
  auto OF = std::move(*MaybeOF);
  // Create a parser for the user-specified input format.
  auto MaybeBuf = getInputMemoryBuffer(InputFileName);
  if (!MaybeBuf)
    return MaybeBuf.takeError();
  auto MaybeParser = createRemarkParser(InputFormat, (*MaybeBuf)->getBuffer());
  if (!MaybeParser)
    return MaybeParser.takeError();
  // Emit CSV header.
  if (UseDebugLoc)
    OF->os() << "Source,";
  OF->os() << "Function,Count\n";
  // Parse all remarks. When we see the specified remark collect the count
  // information.
  auto &Parser = **MaybeParser;
  auto MaybeRemark = Parser.next();
  for (; MaybeRemark; MaybeRemark = Parser.next()) {
    auto &Remark = **MaybeRemark;
    if (Remark.RemarkName != "AnnotationSummary")
      continue;
    if (shouldSkipRemark(UseDebugLoc, Remark))
      continue;
    auto *RemarkNameArg = find_if(Remark.Args, [](const Argument &Arg) {
      return Arg.Key == "type" && Arg.Val == AnnotationTypeToCollect;
    });
    if (RemarkNameArg == Remark.Args.end())
      continue;
    auto *CountArg = find_if(
        Remark.Args, [](const Argument &Arg) { return Arg.Key == "count"; });
    assert(CountArg != Remark.Args.end() &&
           "Expected annotation-type remark to have a count key?");
    if (UseDebugLoc) {
      std::string Loc = Remark.Loc->SourceFilePath.str() + ":" +
                        std::to_string(Remark.Loc->SourceLine) + +":" +
                        std::to_string(Remark.Loc->SourceColumn);
      OF->os() << Loc << ",";
    }
    OF->os() << Remark.FunctionName << "," << CountArg->Val << "\n";
  }
  auto E = MaybeRemark.takeError();
  if (!E.isA<EndOfFileError>())
    return E;
  consumeError(std::move(E));
  OF->keep();
  return Error::success();
}
} // namespace annotationcount

static CommandRegistration
    InstructionCountReg(&InstructionCount,
                        instructioncount::tryInstructionCount);
static CommandRegistration Yaml2Bitstream(&AnnotationCount,
                                          annotationcount::tryAnnotationCount);
