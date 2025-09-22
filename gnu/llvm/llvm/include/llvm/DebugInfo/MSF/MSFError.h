//===- MSFError.h - Error extensions for MSF Files --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_MSF_MSFERROR_H
#define LLVM_DEBUGINFO_MSF_MSFERROR_H

#include "llvm/Support/Error.h"

namespace llvm {
namespace msf {
enum class msf_error_code {
  unspecified = 1,
  insufficient_buffer,
  not_writable,
  no_stream,
  invalid_format,
  block_in_use,
  size_overflow_4096,
  size_overflow_8192,
  size_overflow_16384,
  size_overflow_32768,
  stream_directory_overflow,
};
} // namespace msf
} // namespace llvm

namespace std {
template <>
struct is_error_code_enum<llvm::msf::msf_error_code> : std::true_type {};
} // namespace std

namespace llvm {
namespace msf {
const std::error_category &MSFErrCategory();

inline std::error_code make_error_code(msf_error_code E) {
  return std::error_code(static_cast<int>(E), MSFErrCategory());
}

/// Base class for errors originating when parsing raw PDB files
class MSFError : public ErrorInfo<MSFError, StringError> {
public:
  using ErrorInfo<MSFError, StringError>::ErrorInfo; // inherit constructors
  MSFError(const Twine &S) : ErrorInfo(S, msf_error_code::unspecified) {}

  bool isPageOverflow() const {
    switch (static_cast<msf_error_code>(convertToErrorCode().value())) {
    case msf_error_code::unspecified:
    case msf_error_code::insufficient_buffer:
    case msf_error_code::not_writable:
    case msf_error_code::no_stream:
    case msf_error_code::invalid_format:
    case msf_error_code::block_in_use:
      return false;
    case msf_error_code::size_overflow_4096:
    case msf_error_code::size_overflow_8192:
    case msf_error_code::size_overflow_16384:
    case msf_error_code::size_overflow_32768:
    case msf_error_code::stream_directory_overflow:
      return true;
    }
    llvm_unreachable("msf error code not implemented");
  }

  static char ID;
};
} // namespace msf
} // namespace llvm

#endif // LLVM_DEBUGINFO_MSF_MSFERROR_H
