//===-- OptionGroupPlatform.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionGroupPlatform.h"

#include "lldb/Host/OptionParser.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Target/Platform.h"

using namespace lldb;
using namespace lldb_private;

PlatformSP OptionGroupPlatform::CreatePlatformWithOptions(
    CommandInterpreter &interpreter, const ArchSpec &arch, bool make_selected,
    Status &error, ArchSpec &platform_arch) const {
  PlatformList &platforms = interpreter.GetDebugger().GetPlatformList();

  PlatformSP platform_sp;

  if (!m_platform_name.empty()) {
    platform_sp = platforms.Create(m_platform_name);
    if (!platform_sp) {
      error.SetErrorStringWithFormatv(
          "unable to find a plug-in for the platform named \"{0}\"",
          m_platform_name);
    }
    if (platform_sp) {
      if (platform_arch.IsValid() &&
          !platform_sp->IsCompatibleArchitecture(
              arch, {}, ArchSpec::CompatibleMatch, &platform_arch)) {
        error.SetErrorStringWithFormatv("platform '{0}' doesn't support '{1}'",
                                        platform_sp->GetPluginName(),
                                        arch.GetTriple().getTriple());
        platform_sp.reset();
        return platform_sp;
      }
    }
  } else if (arch.IsValid()) {
    platform_sp = platforms.GetOrCreate(arch, {}, &platform_arch, error);
  }

  if (platform_sp) {
    if (make_selected)
      platforms.SetSelectedPlatform(platform_sp);
    if (!m_os_version.empty())
      platform_sp->SetOSVersion(m_os_version);

    if (!m_sdk_sysroot.empty())
      platform_sp->SetSDKRootDirectory(m_sdk_sysroot);

    if (!m_sdk_build.empty())
      platform_sp->SetSDKBuild(m_sdk_build);
  }

  return platform_sp;
}

void OptionGroupPlatform::OptionParsingStarting(
    ExecutionContext *execution_context) {
  m_platform_name.clear();
  m_sdk_sysroot.clear();
  m_sdk_build.clear();
  m_os_version = llvm::VersionTuple();
}

static constexpr OptionDefinition g_option_table[] = {
    {LLDB_OPT_SET_ALL, false, "platform", 'p', OptionParser::eRequiredArgument,
     nullptr, {}, 0, eArgTypePlatform, "Specify name of the platform to "
                                       "use for this target, creating the "
                                       "platform if necessary."},
    {LLDB_OPT_SET_ALL, false, "version", 'v', OptionParser::eRequiredArgument,
     nullptr, {}, 0, eArgTypeNone,
     "Specify the initial SDK version to use prior to connecting."},
    {LLDB_OPT_SET_ALL, false, "build", 'b', OptionParser::eRequiredArgument,
     nullptr, {}, 0, eArgTypeNone,
     "Specify the initial SDK build number."},
    {LLDB_OPT_SET_ALL, false, "sysroot", 'S', OptionParser::eRequiredArgument,
     nullptr, {}, 0, eArgTypeFilename, "Specify the SDK root directory "
                                       "that contains a root of all "
                                       "remote system files."}};

llvm::ArrayRef<OptionDefinition> OptionGroupPlatform::GetDefinitions() {
  llvm::ArrayRef<OptionDefinition> result(g_option_table);
  if (m_include_platform_option)
    return result;
  return result.drop_front();
}

Status
OptionGroupPlatform::SetOptionValue(uint32_t option_idx,
                                    llvm::StringRef option_arg,
                                    ExecutionContext *execution_context) {
  Status error;
  if (!m_include_platform_option)
    ++option_idx;

  const int short_option = g_option_table[option_idx].short_option;

  switch (short_option) {
  case 'p':
    m_platform_name.assign(option_arg.str());
    break;

  case 'v':
    if (m_os_version.tryParse(option_arg))
      error.SetErrorStringWithFormatv("invalid version string '{0}'",
                                      option_arg);
    break;

  case 'b':
    m_sdk_build.assign(option_arg.str());
    break;

  case 'S':
    m_sdk_sysroot.assign(option_arg.str());
    break;

  default:
    llvm_unreachable("Unimplemented option");
  }
  return error;
}

bool OptionGroupPlatform::PlatformMatches(
    const lldb::PlatformSP &platform_sp) const {
  if (!platform_sp)
    return false;

  if (!m_platform_name.empty() && platform_sp->GetName() != m_platform_name)
    return false;

  if (!m_sdk_build.empty() && platform_sp->GetSDKBuild() != m_sdk_build)
    return false;

  if (!m_sdk_sysroot.empty() &&
      platform_sp->GetSDKRootDirectory() != m_sdk_sysroot)
    return false;

  if (!m_os_version.empty() && platform_sp->GetOSVersion() != m_os_version)
    return false;

  return true;
}
