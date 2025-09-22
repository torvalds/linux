//===-- OptionGroupArchitecture.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_OPTIONGROUPARCHITECTURE_H
#define LLDB_INTERPRETER_OPTIONGROUPARCHITECTURE_H

#include "lldb/Interpreter/Options.h"
#include "lldb/Utility/ArchSpec.h"

namespace lldb_private {

// OptionGroupArchitecture

class OptionGroupArchitecture : public OptionGroup {
public:
  OptionGroupArchitecture() = default;

  ~OptionGroupArchitecture() override = default;

  llvm::ArrayRef<OptionDefinition> GetDefinitions() override;

  Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_value,
                        ExecutionContext *execution_context) override;

  void OptionParsingStarting(ExecutionContext *execution_context) override;

  bool GetArchitecture(Platform *platform, ArchSpec &arch);

  bool ArchitectureWasSpecified() const { return !m_arch_str.empty(); }

  llvm::StringRef GetArchitectureName() const { return m_arch_str; }

protected:
  std::string m_arch_str; // Save the arch triple in case a platform is
                          // specified after the architecture
};

} // namespace lldb_private

#endif // LLDB_INTERPRETER_OPTIONGROUPARCHITECTURE_H
