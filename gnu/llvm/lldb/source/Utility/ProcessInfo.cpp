//===-- ProcessInfo.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/ProcessInfo.h"

#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/ScriptedMetadata.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/UserIDResolver.h"
#include "llvm/ADT/SmallString.h"

#include <climits>
#include <optional>

using namespace lldb;
using namespace lldb_private;

ProcessInfo::ProcessInfo()
    : m_executable(), m_arguments(), m_environment(), m_arch(), m_listener_sp(),
      m_hijack_listener_sp(), m_shadow_listener_sp() {}

ProcessInfo::ProcessInfo(const char *name, const ArchSpec &arch,
                         lldb::pid_t pid)
    : m_executable(name), m_arguments(), m_environment(), m_arch(arch),
      m_pid(pid), m_listener_sp(), m_hijack_listener_sp(),
      m_shadow_listener_sp() {}

void ProcessInfo::Clear() {
  m_executable.Clear();
  m_arguments.Clear();
  m_environment.clear();
  m_uid = UINT32_MAX;
  m_gid = UINT32_MAX;
  m_arch.Clear();
  m_pid = LLDB_INVALID_PROCESS_ID;
  m_scripted_metadata_sp.reset();
}

const char *ProcessInfo::GetName() const {
  return m_executable.GetFilename().GetCString();
}

llvm::StringRef ProcessInfo::GetNameAsStringRef() const {
  return m_executable.GetFilename().GetStringRef();
}

void ProcessInfo::Dump(Stream &s, Platform *platform) const {
  s << "Executable: " << GetName() << "\n";
  s << "Triple: ";
  m_arch.DumpTriple(s.AsRawOstream());
  s << "\n";

  s << "Arguments:\n";
  m_arguments.Dump(s);

  s.Format("Environment:\n{0}", m_environment);
}

void ProcessInfo::SetExecutableFile(const FileSpec &exe_file,
                                    bool add_exe_file_as_first_arg) {
  if (exe_file) {
    m_executable = exe_file;
    if (add_exe_file_as_first_arg) {
      llvm::SmallString<128> filename;
      exe_file.GetPath(filename);
      if (!filename.empty())
        m_arguments.InsertArgumentAtIndex(0, filename);
    }
  } else {
    m_executable.Clear();
  }
}

llvm::StringRef ProcessInfo::GetArg0() const { return m_arg0; }

void ProcessInfo::SetArg0(llvm::StringRef arg) { m_arg0 = std::string(arg); }

void ProcessInfo::SetArguments(char const **argv,
                               bool first_arg_is_executable) {
  m_arguments.SetArguments(argv);

  // Is the first argument the executable?
  if (first_arg_is_executable) {
    const char *first_arg = m_arguments.GetArgumentAtIndex(0);
    if (first_arg) {
      // Yes the first argument is an executable, set it as the executable in
      // the launch options. Don't resolve the file path as the path could be a
      // remote platform path
      m_executable.SetFile(first_arg, FileSpec::Style::native);
    }
  }
}

void ProcessInfo::SetArguments(const Args &args, bool first_arg_is_executable) {
  // Copy all arguments
  m_arguments = args;

  // Is the first argument the executable?
  if (first_arg_is_executable) {
    const char *first_arg = m_arguments.GetArgumentAtIndex(0);
    if (first_arg) {
      // Yes the first argument is an executable, set it as the executable in
      // the launch options. Don't resolve the file path as the path could be a
      // remote platform path
      m_executable.SetFile(first_arg, FileSpec::Style::native);
    }
  }
}

bool ProcessInfo::IsScriptedProcess() const {
  return m_scripted_metadata_sp && *m_scripted_metadata_sp;
}

void ProcessInstanceInfo::Dump(Stream &s, UserIDResolver &resolver) const {
  if (m_pid != LLDB_INVALID_PROCESS_ID)
    s.Printf("    pid = %" PRIu64 "\n", m_pid);

  if (ParentProcessIDIsValid())
    s.Printf(" parent = %" PRIu64 "\n", GetParentProcessID());

  if (m_executable) {
    s.Printf("   name = %s\n", m_executable.GetFilename().GetCString());
    s.PutCString("   file = ");
    m_executable.Dump(s.AsRawOstream());
    s.EOL();
  }
  const uint32_t argc = m_arguments.GetArgumentCount();
  if (argc > 0) {
    for (uint32_t i = 0; i < argc; i++) {
      const char *arg = m_arguments.GetArgumentAtIndex(i);
      if (i < 10)
        s.Printf(" arg[%u] = %s\n", i, arg);
      else
        s.Printf("arg[%u] = %s\n", i, arg);
    }
  }

  s.Format("{0}", m_environment);

  if (m_arch.IsValid()) {
    s.Printf("   arch = ");
    m_arch.DumpTriple(s.AsRawOstream());
    s.EOL();
  }

  if (UserIDIsValid()) {
    s.Format("    uid = {0,-5} ({1})\n", GetUserID(),
             resolver.GetUserName(GetUserID()).value_or(""));
  }
  if (GroupIDIsValid()) {
    s.Format("    gid = {0,-5} ({1})\n", GetGroupID(),
             resolver.GetGroupName(GetGroupID()).value_or(""));
  }
  if (EffectiveUserIDIsValid()) {
    s.Format("   euid = {0,-5} ({1})\n", GetEffectiveUserID(),
             resolver.GetUserName(GetEffectiveUserID()).value_or(""));
  }
  if (EffectiveGroupIDIsValid()) {
    s.Format("   egid = {0,-5} ({1})\n", GetEffectiveGroupID(),
             resolver.GetGroupName(GetEffectiveGroupID()).value_or(""));
  }
}

void ProcessInstanceInfo::DumpTableHeader(Stream &s, bool show_args,
                                          bool verbose) {
  const char *label;
  if (show_args || verbose)
    label = "ARGUMENTS";
  else
    label = "NAME";

  if (verbose) {
    s.Printf("PID    PARENT USER       GROUP      EFF USER   EFF GROUP  TRIPLE "
             "                        %s\n",
             label);
    s.PutCString(
        "====== ====== ========== ========== ========== ========== "
        "============================== ============================\n");
  } else {
    s.Printf("PID    PARENT USER       TRIPLE                         %s\n",
             label);
    s.PutCString("====== ====== ========== ============================== "
                 "============================\n");
  }
}

void ProcessInstanceInfo::DumpAsTableRow(Stream &s, UserIDResolver &resolver,
                                         bool show_args, bool verbose) const {
  if (m_pid != LLDB_INVALID_PROCESS_ID) {
    s.Printf("%-6" PRIu64 " %-6" PRIu64 " ", m_pid,
             (ParentProcessIDIsValid()) ? GetParentProcessID() : 0);

    StreamString arch_strm;
    if (m_arch.IsValid())
      m_arch.DumpTriple(arch_strm.AsRawOstream());

    auto print = [&](bool (ProcessInstanceInfo::*isValid)() const,
                     uint32_t (ProcessInstanceInfo::*getID)() const,
                     std::optional<llvm::StringRef> (UserIDResolver::*getName)(
                         UserIDResolver::id_t id)) {
      const char *format = "{0,-10} ";
      if (!(this->*isValid)()) {
        s.Format(format, "");
        return;
      }
      uint32_t id = (this->*getID)();
      if (auto name = (resolver.*getName)(id))
        s.Format(format, *name);
      else
        s.Format(format, id);
    };
    if (verbose) {
      print(&ProcessInstanceInfo::UserIDIsValid,
            &ProcessInstanceInfo::GetUserID, &UserIDResolver::GetUserName);
      print(&ProcessInstanceInfo::GroupIDIsValid,
            &ProcessInstanceInfo::GetGroupID, &UserIDResolver::GetGroupName);
      print(&ProcessInstanceInfo::EffectiveUserIDIsValid,
            &ProcessInstanceInfo::GetEffectiveUserID,
            &UserIDResolver::GetUserName);
      print(&ProcessInstanceInfo::EffectiveGroupIDIsValid,
            &ProcessInstanceInfo::GetEffectiveGroupID,
            &UserIDResolver::GetGroupName);

      s.Printf("%-30s ", arch_strm.GetData());
    } else {
      print(&ProcessInstanceInfo::EffectiveUserIDIsValid,
            &ProcessInstanceInfo::GetEffectiveUserID,
            &UserIDResolver::GetUserName);
      s.Printf("%-30s ", arch_strm.GetData());
    }

    if (verbose || show_args) {
      s.PutCString(m_arg0);
      const uint32_t argc = m_arguments.GetArgumentCount();
      for (uint32_t i = 0; i < argc; i++) {
        s.PutChar(' ');
        s.PutCString(m_arguments.GetArgumentAtIndex(i));
      }
    } else {
      s.PutCString(GetName());
    }

    s.EOL();
  }
}

bool ProcessInstanceInfoMatch::ArchitectureMatches(
    const ArchSpec &arch_spec) const {
  return !m_match_info.GetArchitecture().IsValid() ||
         m_match_info.GetArchitecture().IsCompatibleMatch(arch_spec);
}

bool ProcessInstanceInfoMatch::NameMatches(const char *process_name) const {
  if (m_name_match_type == NameMatch::Ignore)
    return true;
  const char *match_name = m_match_info.GetName();
  if (!match_name)
    return true;

  return lldb_private::NameMatches(process_name, m_name_match_type, match_name);
}

bool ProcessInstanceInfoMatch::ProcessIDsMatch(
    const ProcessInstanceInfo &proc_info) const {
  if (m_match_info.ProcessIDIsValid() &&
      m_match_info.GetProcessID() != proc_info.GetProcessID())
    return false;

  if (m_match_info.ParentProcessIDIsValid() &&
      m_match_info.GetParentProcessID() != proc_info.GetParentProcessID())
    return false;
  return true;
}

bool ProcessInstanceInfoMatch::UserIDsMatch(
    const ProcessInstanceInfo &proc_info) const {
  if (m_match_info.UserIDIsValid() &&
      m_match_info.GetUserID() != proc_info.GetUserID())
    return false;

  if (m_match_info.GroupIDIsValid() &&
      m_match_info.GetGroupID() != proc_info.GetGroupID())
    return false;

  if (m_match_info.EffectiveUserIDIsValid() &&
      m_match_info.GetEffectiveUserID() != proc_info.GetEffectiveUserID())
    return false;

  if (m_match_info.EffectiveGroupIDIsValid() &&
      m_match_info.GetEffectiveGroupID() != proc_info.GetEffectiveGroupID())
    return false;
  return true;
}
bool ProcessInstanceInfoMatch::Matches(
    const ProcessInstanceInfo &proc_info) const {
  return ArchitectureMatches(proc_info.GetArchitecture()) &&
         ProcessIDsMatch(proc_info) && UserIDsMatch(proc_info) &&
         NameMatches(proc_info.GetName());
}

bool ProcessInstanceInfoMatch::MatchAllProcesses() const {
  if (m_name_match_type != NameMatch::Ignore)
    return false;

  if (m_match_info.ProcessIDIsValid())
    return false;

  if (m_match_info.ParentProcessIDIsValid())
    return false;

  if (m_match_info.UserIDIsValid())
    return false;

  if (m_match_info.GroupIDIsValid())
    return false;

  if (m_match_info.EffectiveUserIDIsValid())
    return false;

  if (m_match_info.EffectiveGroupIDIsValid())
    return false;

  if (m_match_info.GetArchitecture().IsValid())
    return false;

  if (m_match_all_users)
    return false;

  return true;
}

void ProcessInstanceInfoMatch::Clear() {
  m_match_info.Clear();
  m_name_match_type = NameMatch::Ignore;
  m_match_all_users = false;
}
