//===- llvm/TextAPI/TextAPIError.h - TAPI Error -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Define TAPI specific error codes.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TEXTAPI_TEXTAPIERROR_H
#define LLVM_TEXTAPI_TEXTAPIERROR_H

#include "llvm/Support/Error.h"

namespace llvm::MachO {
enum class TextAPIErrorCode {
  NoSuchArchitecture,
  EmptyResults,
  GenericFrontendError,
  InvalidInputFormat,
  UnsupportedTarget
};

class TextAPIError : public llvm::ErrorInfo<TextAPIError> {
public:
  static char ID;
  TextAPIErrorCode EC;
  std::string Msg;

  TextAPIError(TextAPIErrorCode EC) : EC(EC) {}
  TextAPIError(TextAPIErrorCode EC, std::string Msg)
      : EC(EC), Msg(std::move(Msg)) {}

  void log(raw_ostream &OS) const override;
  std::error_code convertToErrorCode() const override;
};

} // namespace llvm::MachO
#endif // LLVM_TEXTAPI_TEXTAPIERROR_H
