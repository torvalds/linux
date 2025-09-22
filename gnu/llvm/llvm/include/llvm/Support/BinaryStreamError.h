//===- BinaryStreamError.h - Error extensions for Binary Streams *- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_BINARYSTREAMERROR_H
#define LLVM_SUPPORT_BINARYSTREAMERROR_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <string>

namespace llvm {
enum class stream_error_code {
  unspecified,
  stream_too_short,
  invalid_array_size,
  invalid_offset,
  filesystem_error
};

/// Base class for errors originating when parsing raw PDB files
class BinaryStreamError : public ErrorInfo<BinaryStreamError> {
public:
  static char ID;
  explicit BinaryStreamError(stream_error_code C);
  explicit BinaryStreamError(StringRef Context);
  BinaryStreamError(stream_error_code C, StringRef Context);

  void log(raw_ostream &OS) const override;
  std::error_code convertToErrorCode() const override;

  StringRef getErrorMessage() const;

  stream_error_code getErrorCode() const { return Code; }

private:
  std::string ErrMsg;
  stream_error_code Code;
};
} // namespace llvm

#endif // LLVM_SUPPORT_BINARYSTREAMERROR_H
