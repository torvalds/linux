//===-- RegisterTypeBuilderClang.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_PLUGINS_REGISTERTYPEBUILDER_REGISTERTYPEBUILDERCLANG_H
#define LLDB_PLUGINS_REGISTERTYPEBUILDER_REGISTERTYPEBUILDERCLANG_H

#include "lldb/Target/RegisterTypeBuilder.h"
#include "lldb/Target/Target.h"

namespace lldb_private {
class RegisterTypeBuilderClang : public RegisterTypeBuilder {
public:
  RegisterTypeBuilderClang(Target &target);

  static void Initialize();
  static void Terminate();
  static llvm::StringRef GetPluginNameStatic() {
    return "register-types-clang";
  }
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }
  static llvm::StringRef GetPluginDescriptionStatic() {
    return "Create register types using TypeSystemClang";
  }
  static lldb::RegisterTypeBuilderSP CreateInstance(Target &target);

  CompilerType GetRegisterType(const std::string &name,
                               const lldb_private::RegisterFlags &flags,
                               uint32_t byte_size) override;

private:
  Target &m_target;
};
} // namespace lldb_private

#endif // LLDB_PLUGINS_REGISTERTYPEBUILDER_REGISTERTYPEBUILDERCLANG_H
