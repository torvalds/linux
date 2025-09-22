//===-- TargetExecutionUtils.h - Utils for execution in target --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Utilities for execution in the target process.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_TARGETEXECUTIONUTILS_H
#define LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_TARGETEXECUTIONUTILS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include <string>

namespace llvm {
namespace orc {

/// Run a main function, returning the result.
///
/// If the optional ProgramName argument is given then it will be inserted
/// before the strings in Args as the first argument to the called function.
///
/// It is legal to have an empty argument list and no program name, however
/// many main functions will expect a name argument at least, and will fail
/// if none is provided.
int runAsMain(int (*Main)(int, char *[]), ArrayRef<std::string> Args,
              std::optional<StringRef> ProgramName = std::nullopt);

int runAsVoidFunction(int (*Func)(void));
int runAsIntFunction(int (*Func)(int), int Arg);

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_TARGETEXECUTIONUTILS_H
