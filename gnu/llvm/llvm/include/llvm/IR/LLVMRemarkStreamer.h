//===- llvm/IR/LLVMRemarkStreamer.h - Streamer for LLVM remarks--*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the conversion between IR Diagnostics and
// serializable remarks::Remark objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_LLVMREMARKSTREAMER_H
#define LLVM_IR_LLVMREMARKSTREAMER_H

#include "llvm/Remarks/Remark.h"
#include "llvm/Support/Error.h"
#include <memory>
#include <optional>
#include <string>

namespace llvm {

class DiagnosticInfoOptimizationBase;
class LLVMContext;
class ToolOutputFile;
namespace remarks {
class RemarkStreamer;
}

/// Streamer for LLVM remarks which has logic for dealing with DiagnosticInfo
/// objects.
class LLVMRemarkStreamer {
  remarks::RemarkStreamer &RS;
  /// Convert diagnostics into remark objects.
  /// The lifetime of the members of the result is bound to the lifetime of
  /// the LLVM diagnostics.
  remarks::Remark toRemark(const DiagnosticInfoOptimizationBase &Diag) const;

public:
  LLVMRemarkStreamer(remarks::RemarkStreamer &RS) : RS(RS) {}
  /// Emit a diagnostic through the streamer.
  void emit(const DiagnosticInfoOptimizationBase &Diag);
};

template <typename ThisError>
struct LLVMRemarkSetupErrorInfo : public ErrorInfo<ThisError> {
  std::string Msg;
  std::error_code EC;

  LLVMRemarkSetupErrorInfo(Error E) {
    handleAllErrors(std::move(E), [&](const ErrorInfoBase &EIB) {
      Msg = EIB.message();
      EC = EIB.convertToErrorCode();
    });
  }

  void log(raw_ostream &OS) const override { OS << Msg; }
  std::error_code convertToErrorCode() const override { return EC; }
};

struct LLVMRemarkSetupFileError
    : LLVMRemarkSetupErrorInfo<LLVMRemarkSetupFileError> {
  static char ID;
  using LLVMRemarkSetupErrorInfo<
      LLVMRemarkSetupFileError>::LLVMRemarkSetupErrorInfo;
};

struct LLVMRemarkSetupPatternError
    : LLVMRemarkSetupErrorInfo<LLVMRemarkSetupPatternError> {
  static char ID;
  using LLVMRemarkSetupErrorInfo<
      LLVMRemarkSetupPatternError>::LLVMRemarkSetupErrorInfo;
};

struct LLVMRemarkSetupFormatError
    : LLVMRemarkSetupErrorInfo<LLVMRemarkSetupFormatError> {
  static char ID;
  using LLVMRemarkSetupErrorInfo<
      LLVMRemarkSetupFormatError>::LLVMRemarkSetupErrorInfo;
};

/// Setup optimization remarks that output to a file.
Expected<std::unique_ptr<ToolOutputFile>>
setupLLVMOptimizationRemarks(LLVMContext &Context, StringRef RemarksFilename,
                             StringRef RemarksPasses, StringRef RemarksFormat,
                             bool RemarksWithHotness,
                             std::optional<uint64_t> RemarksHotnessThreshold = 0);

/// Setup optimization remarks that output directly to a raw_ostream.
/// \p OS is managed by the caller and should be open for writing as long as \p
/// Context is streaming remarks to it.
Error setupLLVMOptimizationRemarks(
    LLVMContext &Context, raw_ostream &OS, StringRef RemarksPasses,
    StringRef RemarksFormat, bool RemarksWithHotness,
    std::optional<uint64_t> RemarksHotnessThreshold = 0);

} // end namespace llvm

#endif // LLVM_IR_LLVMREMARKSTREAMER_H
