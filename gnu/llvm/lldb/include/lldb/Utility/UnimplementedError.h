//===-- UnimplementedError.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_UNIMPLEMENTEDERROR_H
#define LLDB_UTILITY_UNIMPLEMENTEDERROR_H

#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"

namespace lldb_private {
class UnimplementedError : public llvm::ErrorInfo<UnimplementedError> {
public:
  static char ID;

  void log(llvm::raw_ostream &OS) const override { OS << "Not implemented"; }

  std::error_code convertToErrorCode() const override {
    return llvm::errc::not_supported;
  };
};
} // namespace lldb_private

#endif // LLDB_UTILITY_UNIMPLEMENTEDERROR_H
