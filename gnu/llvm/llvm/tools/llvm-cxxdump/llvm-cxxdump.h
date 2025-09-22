//===-- llvm-cxxdump.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_CXXDUMP_LLVM_CXXDUMP_H
#define LLVM_TOOLS_LLVM_CXXDUMP_LLVM_CXXDUMP_H

#include "llvm/Support/CommandLine.h"
#include <string>

namespace opts {
extern llvm::cl::list<std::string> InputFilenames;
} // namespace opts

#define LLVM_CXXDUMP_ENUM_ENT(ns, enum)                                        \
  { #enum, ns::enum }

#endif
