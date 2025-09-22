//===- DIAError.h - Error extensions for PDB DIA implementation -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_DIA_DIAERROR_H
#define LLVM_DEBUGINFO_PDB_DIA_DIAERROR_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace pdb {
enum class dia_error_code {
  unspecified = 1,
  could_not_create_impl,
  invalid_file_format,
  invalid_parameter,
  already_loaded,
  debug_info_mismatch,
};
} // namespace pdb
} // namespace llvm

namespace std {
template <>
struct is_error_code_enum<llvm::pdb::dia_error_code> : std::true_type {};
} // namespace std

namespace llvm {
namespace pdb {
const std::error_category &DIAErrCategory();

inline std::error_code make_error_code(dia_error_code E) {
  return std::error_code(static_cast<int>(E), DIAErrCategory());
}

/// Base class for errors originating in DIA SDK, e.g. COM calls
class DIAError : public ErrorInfo<DIAError, StringError> {
public:
  using ErrorInfo<DIAError, StringError>::ErrorInfo;
  DIAError(const Twine &S) : ErrorInfo(S, dia_error_code::unspecified) {}
  static char ID;
};
} // namespace pdb
} // namespace llvm
#endif
