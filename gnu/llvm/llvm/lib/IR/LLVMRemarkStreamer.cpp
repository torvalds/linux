//===- llvm/IR/LLVMRemarkStreamer.cpp - Remark Streamer -*- C++ ---------*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation of the conversion between IR
// Diagnostics and serializable remarks::Remark objects.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/LLVMRemarkStreamer.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/Remarks/RemarkStreamer.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ToolOutputFile.h"
#include <optional>

using namespace llvm;

/// DiagnosticKind -> remarks::Type
static remarks::Type toRemarkType(enum DiagnosticKind Kind) {
  switch (Kind) {
  default:
    return remarks::Type::Unknown;
  case DK_OptimizationRemark:
  case DK_MachineOptimizationRemark:
    return remarks::Type::Passed;
  case DK_OptimizationRemarkMissed:
  case DK_MachineOptimizationRemarkMissed:
    return remarks::Type::Missed;
  case DK_OptimizationRemarkAnalysis:
  case DK_MachineOptimizationRemarkAnalysis:
    return remarks::Type::Analysis;
  case DK_OptimizationRemarkAnalysisFPCommute:
    return remarks::Type::AnalysisFPCommute;
  case DK_OptimizationRemarkAnalysisAliasing:
    return remarks::Type::AnalysisAliasing;
  case DK_OptimizationFailure:
    return remarks::Type::Failure;
  }
}

/// DiagnosticLocation -> remarks::RemarkLocation.
static std::optional<remarks::RemarkLocation>
toRemarkLocation(const DiagnosticLocation &DL) {
  if (!DL.isValid())
    return std::nullopt;
  StringRef File = DL.getRelativePath();
  unsigned Line = DL.getLine();
  unsigned Col = DL.getColumn();
  return remarks::RemarkLocation{File, Line, Col};
}

/// LLVM Diagnostic -> Remark
remarks::Remark
LLVMRemarkStreamer::toRemark(const DiagnosticInfoOptimizationBase &Diag) const {
  remarks::Remark R; // The result.
  R.RemarkType = toRemarkType(static_cast<DiagnosticKind>(Diag.getKind()));
  R.PassName = Diag.getPassName();
  R.RemarkName = Diag.getRemarkName();
  R.FunctionName =
      GlobalValue::dropLLVMManglingEscape(Diag.getFunction().getName());
  R.Loc = toRemarkLocation(Diag.getLocation());
  R.Hotness = Diag.getHotness();

  for (const DiagnosticInfoOptimizationBase::Argument &Arg : Diag.getArgs()) {
    R.Args.emplace_back();
    R.Args.back().Key = Arg.Key;
    R.Args.back().Val = Arg.Val;
    R.Args.back().Loc = toRemarkLocation(Arg.Loc);
  }

  return R;
}

void LLVMRemarkStreamer::emit(const DiagnosticInfoOptimizationBase &Diag) {
  if (!RS.matchesFilter(Diag.getPassName()))
      return;

  // First, convert the diagnostic to a remark.
  remarks::Remark R = toRemark(Diag);
  // Then, emit the remark through the serializer.
  RS.getSerializer().emit(R);
}

char LLVMRemarkSetupFileError::ID = 0;
char LLVMRemarkSetupPatternError::ID = 0;
char LLVMRemarkSetupFormatError::ID = 0;

Expected<std::unique_ptr<ToolOutputFile>> llvm::setupLLVMOptimizationRemarks(
    LLVMContext &Context, StringRef RemarksFilename, StringRef RemarksPasses,
    StringRef RemarksFormat, bool RemarksWithHotness,
    std::optional<uint64_t> RemarksHotnessThreshold) {
  if (RemarksWithHotness || RemarksHotnessThreshold.value_or(1))
      Context.setDiagnosticsHotnessRequested(true);

  Context.setDiagnosticsHotnessThreshold(RemarksHotnessThreshold);

  if (RemarksFilename.empty())
    return nullptr;

  Expected<remarks::Format> Format = remarks::parseFormat(RemarksFormat);
  if (Error E = Format.takeError())
    return make_error<LLVMRemarkSetupFormatError>(std::move(E));

  std::error_code EC;
  auto Flags = *Format == remarks::Format::YAML ? sys::fs::OF_TextWithCRLF
                                                : sys::fs::OF_None;
  auto RemarksFile =
      std::make_unique<ToolOutputFile>(RemarksFilename, EC, Flags);
  // We don't use llvm::FileError here because some diagnostics want the file
  // name separately.
  if (EC)
    return make_error<LLVMRemarkSetupFileError>(errorCodeToError(EC));

  Expected<std::unique_ptr<remarks::RemarkSerializer>> RemarkSerializer =
      remarks::createRemarkSerializer(
          *Format, remarks::SerializerMode::Separate, RemarksFile->os());
  if (Error E = RemarkSerializer.takeError())
    return make_error<LLVMRemarkSetupFormatError>(std::move(E));

  // Create the main remark streamer.
  Context.setMainRemarkStreamer(std::make_unique<remarks::RemarkStreamer>(
      std::move(*RemarkSerializer), RemarksFilename));

  // Create LLVM's optimization remarks streamer.
  Context.setLLVMRemarkStreamer(
      std::make_unique<LLVMRemarkStreamer>(*Context.getMainRemarkStreamer()));

  if (!RemarksPasses.empty())
    if (Error E = Context.getMainRemarkStreamer()->setFilter(RemarksPasses))
      return make_error<LLVMRemarkSetupPatternError>(std::move(E));

  return std::move(RemarksFile);
}

Error llvm::setupLLVMOptimizationRemarks(
    LLVMContext &Context, raw_ostream &OS, StringRef RemarksPasses,
    StringRef RemarksFormat, bool RemarksWithHotness,
    std::optional<uint64_t> RemarksHotnessThreshold) {
  if (RemarksWithHotness || RemarksHotnessThreshold.value_or(1))
    Context.setDiagnosticsHotnessRequested(true);

  Context.setDiagnosticsHotnessThreshold(RemarksHotnessThreshold);

  Expected<remarks::Format> Format = remarks::parseFormat(RemarksFormat);
  if (Error E = Format.takeError())
    return make_error<LLVMRemarkSetupFormatError>(std::move(E));

  Expected<std::unique_ptr<remarks::RemarkSerializer>> RemarkSerializer =
      remarks::createRemarkSerializer(*Format,
                                      remarks::SerializerMode::Separate, OS);
  if (Error E = RemarkSerializer.takeError())
    return make_error<LLVMRemarkSetupFormatError>(std::move(E));

  // Create the main remark streamer.
  Context.setMainRemarkStreamer(
      std::make_unique<remarks::RemarkStreamer>(std::move(*RemarkSerializer)));

  // Create LLVM's optimization remarks streamer.
  Context.setLLVMRemarkStreamer(
      std::make_unique<LLVMRemarkStreamer>(*Context.getMainRemarkStreamer()));

  if (!RemarksPasses.empty())
    if (Error E = Context.getMainRemarkStreamer()->setFilter(RemarksPasses))
      return make_error<LLVMRemarkSetupPatternError>(std::move(E));

  return Error::success();
}
