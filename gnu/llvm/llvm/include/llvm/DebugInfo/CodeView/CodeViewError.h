//===- CodeViewError.h - Error extensions for CodeView ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_CODEVIEWERROR_H
#define LLVM_DEBUGINFO_CODEVIEW_CODEVIEWERROR_H

#include "llvm/Support/Error.h"

namespace llvm {
namespace codeview {
enum class cv_error_code {
  unspecified = 1,
  insufficient_buffer,
  operation_unsupported,
  corrupt_record,
  no_records,
  unknown_member_record,
};
} // namespace codeview
} // namespace llvm

namespace std {
template <>
struct is_error_code_enum<llvm::codeview::cv_error_code> : std::true_type {};
} // namespace std

namespace llvm {
namespace codeview {
const std::error_category &CVErrorCategory();

inline std::error_code make_error_code(cv_error_code E) {
  return std::error_code(static_cast<int>(E), CVErrorCategory());
}

/// Base class for errors originating when parsing raw PDB files
class CodeViewError : public ErrorInfo<CodeViewError, StringError> {
public:
  using ErrorInfo<CodeViewError,
                  StringError>::ErrorInfo; // inherit constructors
  CodeViewError(const Twine &S) : ErrorInfo(S, cv_error_code::unspecified) {}
  static char ID;
};

} // namespace codeview
} // namespace llvm

#endif
