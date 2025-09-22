//===-- sanitizer_report_decorator.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Tags to decorate the sanitizer reports.
// Currently supported tags:
//   * None.
//   * ANSI color sequences.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_REPORT_DECORATOR_H
#define SANITIZER_REPORT_DECORATOR_H

#include "sanitizer_common.h"

namespace __sanitizer {
class SanitizerCommonDecorator {
  // FIXME: This is not portable. It assumes the special strings are printed to
  // stdout, which is not the case on Windows (see SetConsoleTextAttribute()).
 public:
  SanitizerCommonDecorator() : ansi_(ColorizeReports()) {}
  const char *Bold() const { return ansi_ ? "\033[1m" : ""; }
  const char *Default() const { return ansi_ ? "\033[1m\033[0m"  : ""; }
  const char *Warning() const { return Red(); }
  const char *Error() const { return Red(); }
  const char *MemoryByte() const { return Magenta(); }

 protected:
  const char *Black()   const { return ansi_ ? "\033[1m\033[30m" : ""; }
  const char *Red()     const { return ansi_ ? "\033[1m\033[31m" : ""; }
  const char *Green()   const { return ansi_ ? "\033[1m\033[32m" : ""; }
  const char *Yellow()  const { return ansi_ ? "\033[1m\033[33m" : ""; }
  const char *Blue()    const { return ansi_ ? "\033[1m\033[34m" : ""; }
  const char *Magenta() const { return ansi_ ? "\033[1m\033[35m" : ""; }
  const char *Cyan()    const { return ansi_ ? "\033[1m\033[36m" : ""; }
  const char *White()   const { return ansi_ ? "\033[1m\033[37m" : ""; }
 private:
  bool ansi_;
};

}  // namespace __sanitizer

#endif  // SANITIZER_REPORT_DECORATOR_H
