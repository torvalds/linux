//===- Reproduce.h - Utilities for creating reproducers ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_COMMON_REPRODUCE_H
#define LLD_COMMON_REPRODUCE_H

#include "lld/Common/LLVM.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace opt { class Arg; }
}

namespace lld {

// Makes a given pathname an absolute path first, and then remove
// beginning /. For example, "../foo.o" is converted to "home/john/foo.o",
// assuming that the current directory is "/home/john/bar".
std::string relativeToRoot(StringRef path);

// Quote a given string if it contains a space character.
std::string quote(StringRef s);

// Returns the string form of the given argument.
std::string toString(const llvm::opt::Arg &arg);
}

#endif
