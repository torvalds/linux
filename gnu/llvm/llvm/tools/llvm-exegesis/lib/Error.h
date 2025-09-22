//===-- Error.h -------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_EXEGESIS_ERROR_H
#define LLVM_TOOLS_LLVM_EXEGESIS_ERROR_H

#include "llvm/ADT/Twine.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace exegesis {

// A class representing failures that happened within llvm-exegesis, they are
// used to report informations to the user.
class Failure : public StringError {
public:
  Failure(const Twine &S) : StringError(S, inconvertibleErrorCode()) {}
};

// A class representing failures that happened during clustering calculations.
class ClusteringError : public ErrorInfo<ClusteringError> {
public:
  static char ID;
  ClusteringError(const Twine &S) : Msg(S.str()) {}

  void log(raw_ostream &OS) const override;

  std::error_code convertToErrorCode() const override;

private:
  std::string Msg;
};

// A class representing a non-descript snippet execution failure. This class
// is designed to sub-classed into more specific failures that contain
// additional data about the specific error that they represent. Instead of
// halting the program, the errors are reported in the output.
class SnippetExecutionFailure : public ErrorInfo<SnippetExecutionFailure> {
public:
  static char ID;

  std::error_code convertToErrorCode() const override;
};

// A class representing specifically segmentation faults that happen during
// snippet execution.
class SnippetSegmentationFault : public SnippetExecutionFailure {
public:
  static char ID;
  SnippetSegmentationFault(intptr_t SegFaultAddress)
      : Address(SegFaultAddress){};

  intptr_t getAddress() { return Address; }

  void log(raw_ostream &OS) const override;

private:
  intptr_t Address;
};

// A class representing all other non-specific failures that happen during
// snippet execution.
class SnippetSignal : public SnippetExecutionFailure {
public:
  static char ID;
  SnippetSignal(int Signal) : SignalNumber(Signal){};

  void log(raw_ostream &OS) const override;

private:
  int SignalNumber;
};

} // namespace exegesis
} // namespace llvm

#endif
