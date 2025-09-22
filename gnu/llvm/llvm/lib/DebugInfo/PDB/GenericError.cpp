//===- Error.cpp - system_error extensions for PDB --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/GenericError.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;
using namespace llvm::pdb;

namespace {
// FIXME: This class is only here to support the transition to llvm::Error. It
// will be removed once this transition is complete. Clients should prefer to
// deal with the Error value directly, rather than converting to error_code.
class PDBErrorCategory : public std::error_category {
public:
  const char *name() const noexcept override { return "llvm.pdb"; }
  std::string message(int Condition) const override {
    switch (static_cast<pdb_error_code>(Condition)) {
    case pdb_error_code::unspecified:
      return "An unknown error has occurred.";
    case pdb_error_code::dia_sdk_not_present:
      return "LLVM was not compiled with support for DIA. This usually means "
             "that you are not using MSVC, or your Visual Studio "
             "installation is corrupt.";
    case pdb_error_code::dia_failed_loading:
      return "DIA is only supported when using MSVC.";
    case pdb_error_code::invalid_utf8_path:
      return "The PDB file path is an invalid UTF8 sequence.";
    case pdb_error_code::signature_out_of_date:
      return "The signature does not match; the file(s) might be out of date.";
    case pdb_error_code::no_matching_pch:
      return "No matching precompiled header could be located.";
    }
    llvm_unreachable("Unrecognized generic_error_code");
  }
};
} // namespace

const std::error_category &llvm::pdb::PDBErrCategory() {
  static PDBErrorCategory PDBCategory;
  return PDBCategory;
}

char PDBError::ID;
