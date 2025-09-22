//===-- OptionGroupUUID.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_OPTIONGROUPUUID_H
#define LLDB_INTERPRETER_OPTIONGROUPUUID_H

#include "lldb/Interpreter/OptionValueUUID.h"
#include "lldb/Interpreter/Options.h"

namespace lldb_private {

// OptionGroupUUID

class OptionGroupUUID : public OptionGroup {
public:
  OptionGroupUUID() = default;

  ~OptionGroupUUID() override = default;

  llvm::ArrayRef<OptionDefinition> GetDefinitions() override;

  Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_value,
                        ExecutionContext *execution_context) override;

  void OptionParsingStarting(ExecutionContext *execution_context) override;

  const OptionValueUUID &GetOptionValue() const { return m_uuid; }

protected:
  OptionValueUUID m_uuid;
};

} // namespace lldb_private

#endif // LLDB_INTERPRETER_OPTIONGROUPUUID_H
