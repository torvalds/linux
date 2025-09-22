//===- RemarkUtilHelpers.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Helpers for remark utilites
//
//===----------------------------------------------------------------------===//
#include "RemarkUtilHelpers.h"

namespace llvm {
namespace remarks {
/// \returns A MemoryBuffer for the input file on success, and an Error
/// otherwise.
Expected<std::unique_ptr<MemoryBuffer>>
getInputMemoryBuffer(StringRef InputFileName) {
  auto MaybeBuf = MemoryBuffer::getFileOrSTDIN(InputFileName);
  if (auto ErrorCode = MaybeBuf.getError())
    return createStringError(ErrorCode,
                             Twine("Cannot open file '" + InputFileName +
                                   "': " + ErrorCode.message()));
  return std::move(*MaybeBuf);
}

/// \returns A ToolOutputFile which can be used for outputting the results of
/// some tool mode.
/// \p OutputFileName is the desired destination.
/// \p Flags controls whether or not the file is opened for writing in text
/// mode, as a binary, etc. See sys::fs::OpenFlags for more detail.
Expected<std::unique_ptr<ToolOutputFile>>
getOutputFileWithFlags(StringRef OutputFileName, sys::fs::OpenFlags Flags) {
  if (OutputFileName == "")
    OutputFileName = "-";
  std::error_code ErrorCode;
  auto OF = std::make_unique<ToolOutputFile>(OutputFileName, ErrorCode, Flags);
  if (ErrorCode)
    return errorCodeToError(ErrorCode);
  return std::move(OF);
}

/// \returns A ToolOutputFile which can be used for writing remarks on success,
/// and an Error otherwise.
/// \p OutputFileName is the desired destination.
/// \p OutputFormat
Expected<std::unique_ptr<ToolOutputFile>>
getOutputFileForRemarks(StringRef OutputFileName, Format OutputFormat) {
  assert((OutputFormat == Format::YAML || OutputFormat == Format::Bitstream) &&
         "Expected one of YAML or Bitstream!");
  return getOutputFileWithFlags(OutputFileName, OutputFormat == Format::YAML
                                                    ? sys::fs::OF_TextWithCRLF
                                                    : sys::fs::OF_None);
}
} // namespace remarks
} // namespace llvm
