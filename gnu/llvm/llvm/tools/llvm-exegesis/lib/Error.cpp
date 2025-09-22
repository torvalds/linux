//===-- Error.cpp -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Error.h"

#ifdef LLVM_ON_UNIX
#include "llvm/Support/SystemZ/zOSSupport.h"
#include <string.h>
#endif // LLVM_ON_UNIX

namespace llvm {
namespace exegesis {

char ClusteringError::ID;

void ClusteringError::log(raw_ostream &OS) const { OS << Msg; }

std::error_code ClusteringError::convertToErrorCode() const {
  return inconvertibleErrorCode();
}

char SnippetExecutionFailure::ID;

std::error_code SnippetExecutionFailure::convertToErrorCode() const {
  return inconvertibleErrorCode();
}

char SnippetSegmentationFault::ID;

void SnippetSegmentationFault::log(raw_ostream &OS) const {
  OS << "The snippet encountered a segmentation fault at address "
     << Twine::utohexstr(Address);
}

char SnippetSignal::ID;

void SnippetSignal::log(raw_ostream &OS) const {
  OS << "snippet crashed while running";
#ifdef LLVM_ON_UNIX
  OS << ": " << strsignal(SignalNumber);
#else
  (void)SignalNumber;
#endif // LLVM_ON_UNIX
}

} // namespace exegesis
} // namespace llvm
