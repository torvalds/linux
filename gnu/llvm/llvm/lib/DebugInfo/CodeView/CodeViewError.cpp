//===- CodeViewError.cpp - Error extensions for CodeView --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/CodeViewError.h"
#include "llvm/Support/ErrorHandling.h"
#include <string>

using namespace llvm;
using namespace llvm::codeview;

namespace {
// FIXME: This class is only here to support the transition to llvm::Error. It
// will be removed once this transition is complete. Clients should prefer to
// deal with the Error value directly, rather than converting to error_code.
class CodeViewErrorCategory : public std::error_category {
public:
  const char *name() const noexcept override { return "llvm.codeview"; }
  std::string message(int Condition) const override {
    switch (static_cast<cv_error_code>(Condition)) {
    case cv_error_code::unspecified:
      return "An unknown CodeView error has occurred.";
    case cv_error_code::insufficient_buffer:
      return "The buffer is not large enough to read the requested number of "
             "bytes.";
    case cv_error_code::corrupt_record:
      return "The CodeView record is corrupted.";
    case cv_error_code::no_records:
      return "There are no records.";
    case cv_error_code::operation_unsupported:
      return "The requested operation is not supported.";
    case cv_error_code::unknown_member_record:
      return "The member record is of an unknown type.";
    }
    llvm_unreachable("Unrecognized cv_error_code");
  }
};
} // namespace

const std::error_category &llvm::codeview::CVErrorCategory() {
  static CodeViewErrorCategory CodeViewErrCategory;
  return CodeViewErrCategory;
}

char CodeViewError::ID;
