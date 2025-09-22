//===--- InputInfo.h - Input Source & Type Information ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_DRIVER_INPUTINFO_H
#define LLVM_CLANG_DRIVER_INPUTINFO_H

#include "clang/Driver/Action.h"
#include "clang/Driver/Types.h"
#include "llvm/Option/Arg.h"
#include <cassert>
#include <string>

namespace clang {
namespace driver {

/// InputInfo - Wrapper for information about an input source.
class InputInfo {
  // FIXME: The distinction between filenames and inputarg here is
  // gross; we should probably drop the idea of a "linker
  // input". Doing so means tweaking pipelining to still create link
  // steps when it sees linker inputs (but not treat them as
  // arguments), and making sure that arguments get rendered
  // correctly.
  enum Class {
    Nothing,
    Filename,
    InputArg,
    Pipe
  };

  union {
    const char *Filename;
    const llvm::opt::Arg *InputArg;
  } Data;
  Class Kind;
  const Action* Act;
  types::ID Type;
  const char *BaseInput;

  static types::ID GetActionType(const Action *A) {
    return A != nullptr ? A->getType() : types::TY_Nothing;
  }

public:
  InputInfo() : InputInfo(nullptr, nullptr) {}
  InputInfo(const Action *A, const char *_BaseInput)
      : Kind(Nothing), Act(A), Type(GetActionType(A)), BaseInput(_BaseInput) {}

  InputInfo(types::ID _Type, const char *_Filename, const char *_BaseInput)
      : Kind(Filename), Act(nullptr), Type(_Type), BaseInput(_BaseInput) {
    Data.Filename = _Filename;
  }
  InputInfo(const Action *A, const char *_Filename, const char *_BaseInput)
      : Kind(Filename), Act(A), Type(GetActionType(A)), BaseInput(_BaseInput) {
    Data.Filename = _Filename;
  }

  InputInfo(types::ID _Type, const llvm::opt::Arg *_InputArg,
            const char *_BaseInput)
      : Kind(InputArg), Act(nullptr), Type(_Type), BaseInput(_BaseInput) {
    Data.InputArg = _InputArg;
  }
  InputInfo(const Action *A, const llvm::opt::Arg *_InputArg,
            const char *_BaseInput)
      : Kind(InputArg), Act(A), Type(GetActionType(A)), BaseInput(_BaseInput) {
    Data.InputArg = _InputArg;
  }

  bool isNothing() const { return Kind == Nothing; }
  bool isFilename() const { return Kind == Filename; }
  bool isInputArg() const { return Kind == InputArg; }
  types::ID getType() const { return Type; }
  const char *getBaseInput() const { return BaseInput; }
  /// The action for which this InputInfo was created.  May be null.
  const Action *getAction() const { return Act; }
  void setAction(const Action *A) { Act = A; }

  const char *getFilename() const {
    assert(isFilename() && "Invalid accessor.");
    return Data.Filename;
  }
  const llvm::opt::Arg &getInputArg() const {
    assert(isInputArg() && "Invalid accessor.");
    return *Data.InputArg;
  }

  /// getAsString - Return a string name for this input, for
  /// debugging.
  std::string getAsString() const {
    if (isFilename())
      return std::string("\"") + getFilename() + '"';
    else if (isInputArg())
      return "(input arg)";
    else
      return "(nothing)";
  }
};

} // end namespace driver
} // end namespace clang

#endif
