//===- Error.h --------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_DWARFUTIL_ERROR_H
#define LLVM_TOOLS_LLVM_DWARFUTIL_ERROR_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"

namespace llvm {
namespace dwarfutil {

inline void error(Error Err, StringRef Prefix = "") {
  handleAllErrors(std::move(Err), [&](ErrorInfoBase &Info) {
    WithColor::error(errs(), Prefix) << Info.message() << '\n';
  });
  std::exit(EXIT_FAILURE);
}

inline void warning(const Twine &Message, StringRef Prefix = "") {
  WithColor::warning(errs(), Prefix) << Message << '\n';
}

inline void verbose(const Twine &Message, bool Verbose) {
  if (Verbose)
    outs() << Message << '\n';
}

} // end of namespace dwarfutil
} // end of namespace llvm

#endif // LLVM_TOOLS_LLVM_DWARFUTIL_ERROR_H
