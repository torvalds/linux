//===- IRPrintingPasses.h - Passes to print out IR constructs ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file contains an interface for creating legacy passes to print out IR
/// in various granularities.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_IRPRINTINGPASSES_H
#define LLVM_IR_IRPRINTINGPASSES_H

#include <string>

namespace llvm {
class raw_ostream;
class StringRef;
class FunctionPass;
class ModulePass;
class Pass;

/// Create and return a pass that writes the module to the specified
/// \c raw_ostream.
ModulePass *createPrintModulePass(raw_ostream &OS,
                                  const std::string &Banner = "",
                                  bool ShouldPreserveUseListOrder = false);

/// Create and return a pass that prints functions to the specified
/// \c raw_ostream as they are processed.
FunctionPass *createPrintFunctionPass(raw_ostream &OS,
                                      const std::string &Banner = "");

/// Print out a name of an LLVM value without any prefixes.
///
/// The name is surrounded with ""'s and escaped if it has any special or
/// non-printable characters in it.
void printLLVMNameWithoutPrefix(raw_ostream &OS, StringRef Name);

/// Return true if a pass is for IR printing.
bool isIRPrintingPass(Pass *P);

} // namespace llvm

#endif
