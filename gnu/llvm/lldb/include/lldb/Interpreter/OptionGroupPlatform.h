//===-- OptionGroupPlatform.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_OPTIONGROUPPLATFORM_H
#define LLDB_INTERPRETER_OPTIONGROUPPLATFORM_H

#include "lldb/Interpreter/Options.h"
#include "lldb/Utility/ConstString.h"
#include "llvm/Support/VersionTuple.h"

namespace lldb_private {

// PlatformOptionGroup
//
// Make platform options available to any commands that need the settings.
class OptionGroupPlatform : public OptionGroup {
public:
  OptionGroupPlatform(bool include_platform_option)
      : m_include_platform_option(include_platform_option) {}

  ~OptionGroupPlatform() override = default;

  llvm::ArrayRef<OptionDefinition> GetDefinitions() override;

  Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_value,
                        ExecutionContext *execution_context) override;
  Status SetOptionValue(uint32_t, const char *, ExecutionContext *) = delete;

  void OptionParsingStarting(ExecutionContext *execution_context) override;

  lldb::PlatformSP CreatePlatformWithOptions(CommandInterpreter &interpreter,
                                             const ArchSpec &arch,
                                             bool make_selected, Status &error,
                                             ArchSpec &platform_arch) const;

  bool PlatformWasSpecified() const { return !m_platform_name.empty(); }

  void SetPlatformName(const char *platform_name) {
    if (platform_name && platform_name[0])
      m_platform_name.assign(platform_name);
    else
      m_platform_name.clear();
  }

  const std::string &GetSDKRootDirectory() const { return m_sdk_sysroot; }

  void SetSDKRootDirectory(std::string sdk_root_directory) {
    m_sdk_sysroot = std::move(sdk_root_directory);
  }

  const std::string &GetSDKBuild() const { return m_sdk_build; }

  void SetSDKBuild(std::string sdk_build) {
    m_sdk_build = std::move(sdk_build);
  }

  bool PlatformMatches(const lldb::PlatformSP &platform_sp) const;

protected:
  std::string m_platform_name;
  std::string m_sdk_sysroot;
  std::string m_sdk_build;
  llvm::VersionTuple m_os_version;
  bool m_include_platform_option;
};

} // namespace lldb_private

#endif // LLDB_INTERPRETER_OPTIONGROUPPLATFORM_H
