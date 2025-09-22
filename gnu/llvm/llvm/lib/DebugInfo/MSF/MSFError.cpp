//===- MSFError.cpp - Error extensions for MSF files ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/MSF/MSFError.h"
#include "llvm/Support/ErrorHandling.h"
#include <string>

using namespace llvm;
using namespace llvm::msf;

namespace {
// FIXME: This class is only here to support the transition to llvm::Error. It
// will be removed once this transition is complete. Clients should prefer to
// deal with the Error value directly, rather than converting to error_code.
class MSFErrorCategory : public std::error_category {
public:
  const char *name() const noexcept override { return "llvm.msf"; }
  std::string message(int Condition) const override {
    switch (static_cast<msf_error_code>(Condition)) {
    case msf_error_code::unspecified:
      return "An unknown error has occurred.";
    case msf_error_code::insufficient_buffer:
      return "The buffer is not large enough to read the requested number of "
             "bytes.";
    case msf_error_code::size_overflow_4096:
      return "Output data is larger than 4 GiB.";
    case msf_error_code::size_overflow_8192:
      return "Output data is larger than 8 GiB.";
    case msf_error_code::size_overflow_16384:
      return "Output data is larger than 16 GiB.";
    case msf_error_code::size_overflow_32768:
      return "Output data is larger than 32 GiB.";
    case msf_error_code::not_writable:
      return "The specified stream is not writable.";
    case msf_error_code::no_stream:
      return "The specified stream does not exist.";
    case msf_error_code::invalid_format:
      return "The data is in an unexpected format.";
    case msf_error_code::block_in_use:
      return "The block is already in use.";
    case msf_error_code::stream_directory_overflow:
      return "PDB stream directory too large.";
    }
    llvm_unreachable("Unrecognized msf_error_code");
  }
};
} // namespace

const std::error_category &llvm::msf::MSFErrCategory() {
  static MSFErrorCategory MSFCategory;
  return MSFCategory;
}

char MSFError::ID;
