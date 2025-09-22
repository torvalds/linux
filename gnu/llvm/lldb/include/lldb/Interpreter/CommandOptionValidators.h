//===-- CommandOptionValidators.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_COMMANDOPTIONVALIDATORS_H
#define LLDB_INTERPRETER_COMMANDOPTIONVALIDATORS_H

#include "lldb/lldb-private-types.h"

namespace lldb_private {

class Platform;
class ExecutionContext;

class PosixPlatformCommandOptionValidator : public OptionValidator {
  bool IsValid(Platform &platform,
               const ExecutionContext &target) const override;
  const char *ShortConditionString() const override;
  const char *LongConditionString() const override;
};

} // namespace lldb_private

#endif // LLDB_INTERPRETER_COMMANDOPTIONVALIDATORS_H
