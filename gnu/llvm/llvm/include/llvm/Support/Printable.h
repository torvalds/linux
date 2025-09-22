//===--- Printable.h - Print function helpers -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Printable struct.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_PRINTABLE_H
#define LLVM_SUPPORT_PRINTABLE_H

#include <functional>
#include <utility>

namespace llvm {

class raw_ostream;

/// Simple wrapper around std::function<void(raw_ostream&)>.
/// This class is useful to construct print helpers for raw_ostream.
///
/// Example:
///     Printable printRegister(unsigned Register) {
///       return Printable([Register](raw_ostream &OS) {
///         OS << getRegisterName(Register);
///       });
///     }
///     ... OS << printRegister(Register); ...
///
/// Implementation note: Ideally this would just be a typedef, but doing so
/// leads to operator << being ambiguous as function has matching constructors
/// in some STL versions. I have seen the problem on gcc 4.6 libstdc++ and
/// microsoft STL.
class Printable {
public:
  std::function<void(raw_ostream &OS)> Print;
  Printable(std::function<void(raw_ostream &OS)> Print)
      : Print(std::move(Print)) {}
};

inline raw_ostream &operator<<(raw_ostream &OS, const Printable &P) {
  P.Print(OS);
  return OS;
}

} // namespace llvm

#endif
