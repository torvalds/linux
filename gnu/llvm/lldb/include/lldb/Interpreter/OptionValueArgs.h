//===-- OptionValueArgs.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_OPTIONVALUEARGS_H
#define LLDB_INTERPRETER_OPTIONVALUEARGS_H

#include "lldb/Interpreter/OptionValueArray.h"

namespace lldb_private {

class OptionValueArgs : public Cloneable<OptionValueArgs, OptionValueArray> {
public:
  OptionValueArgs()
      : Cloneable(OptionValue::ConvertTypeToMask(OptionValue::eTypeString)) {}

  ~OptionValueArgs() override = default;

  size_t GetArgs(Args &args) const;

  Type GetType() const override { return eTypeArgs; }
};

} // namespace lldb_private

#endif // LLDB_INTERPRETER_OPTIONVALUEARGS_H
