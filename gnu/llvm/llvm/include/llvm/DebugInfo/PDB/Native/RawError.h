//===- RawError.h - Error extensions for raw PDB implementation -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_RAWERROR_H
#define LLVM_DEBUGINFO_PDB_NATIVE_RAWERROR_H

#include "llvm/Support/Error.h"

namespace llvm {
namespace pdb {
enum class raw_error_code {
  unspecified = 1,
  feature_unsupported,
  invalid_format,
  corrupt_file,
  insufficient_buffer,
  no_stream,
  index_out_of_bounds,
  invalid_block_address,
  duplicate_entry,
  no_entry,
  not_writable,
  stream_too_long,
  invalid_tpi_hash,
};
} // namespace pdb
} // namespace llvm

namespace std {
template <>
struct is_error_code_enum<llvm::pdb::raw_error_code> : std::true_type {};
} // namespace std

namespace llvm {
namespace pdb {
const std::error_category &RawErrCategory();

inline std::error_code make_error_code(raw_error_code E) {
  return std::error_code(static_cast<int>(E), RawErrCategory());
}

/// Base class for errors originating when parsing raw PDB files
class RawError : public ErrorInfo<RawError, StringError> {
public:
  using ErrorInfo<RawError, StringError>::ErrorInfo; // inherit constructors
  RawError(const Twine &S) : ErrorInfo(S, raw_error_code::unspecified) {}
  static char ID;
};
} // namespace pdb
} // namespace llvm
#endif
