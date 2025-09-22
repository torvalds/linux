//===--- MacroBuilder.h - CPP Macro building utility ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines the clang::MacroBuilder utility class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_MACROBUILDER_H
#define LLVM_CLANG_BASIC_MACROBUILDER_H

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/raw_ostream.h"

namespace clang {

class MacroBuilder {
  raw_ostream &Out;
public:
  MacroBuilder(raw_ostream &Output) : Out(Output) {}

  /// Append a \#define line for macro of the form "\#define Name Value\n".
  void defineMacro(const Twine &Name, const Twine &Value = "1") {
    Out << "#define " << Name << ' ' << Value << '\n';
  }

  /// Append a \#undef line for Name.  Name should be of the form XXX
  /// and we emit "\#undef XXX".
  void undefineMacro(const Twine &Name) {
    Out << "#undef " << Name << '\n';
  }

  /// Directly append Str and a newline to the underlying buffer.
  void append(const Twine &Str) {
    Out << Str << '\n';
  }
};

}  // end namespace clang

#endif
