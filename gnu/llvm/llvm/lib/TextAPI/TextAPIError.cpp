//===- TextAPIError.cpp - Tapi Error ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implements TAPI Error.
///
//===----------------------------------------------------------------------===//

#include "llvm/TextAPI/TextAPIError.h"

using namespace llvm;
using namespace llvm::MachO;

char TextAPIError::ID = 0;

void TextAPIError::log(raw_ostream &OS) const {
  switch (EC) {
  case TextAPIErrorCode::NoSuchArchitecture:
    OS << "no such architecture";
    break;
  case TextAPIErrorCode::InvalidInputFormat:
    OS << "invalid input format";
    break;
  default:
    llvm_unreachable("unhandled TextAPIErrorCode");
  }
  if (!Msg.empty())
    OS << ": " << Msg;
  OS << "\n";
}

std::error_code TextAPIError::convertToErrorCode() const {
  llvm_unreachable("convertToErrorCode is not supported.");
}
