//===- OptSpecifier.h - Option Specifiers -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OPTION_OPTSPECIFIER_H
#define LLVM_OPTION_OPTSPECIFIER_H

namespace llvm {
namespace opt {

class Option;

/// OptSpecifier - Wrapper class for abstracting references to option IDs.
class OptSpecifier {
  unsigned ID = 0;

public:
  OptSpecifier() = default;
  explicit OptSpecifier(bool) = delete;
  /*implicit*/ OptSpecifier(unsigned ID) : ID(ID) {}
  /*implicit*/ OptSpecifier(const Option *Opt);

  bool isValid() const { return ID != 0; }

  unsigned getID() const { return ID; }

  bool operator==(OptSpecifier Opt) const { return ID == Opt.getID(); }
  bool operator!=(OptSpecifier Opt) const { return !(*this == Opt); }
};

} // end namespace opt
} // end namespace llvm

#endif // LLVM_OPTION_OPTSPECIFIER_H
