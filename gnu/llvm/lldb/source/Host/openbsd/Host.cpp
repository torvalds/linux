//===-- source/Host/openbsd/Host.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <sys/types.h>

#include <sys/signal.h>
#include <sys/exec.h>
#include <sys/proc.h>
#include <sys/ptrace.h>
#include <sys/sysctl.h>
#include <sys/user.h>

#include <cstdio>

#include "lldb/Host/Host.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/NameMatches.h"
#include "lldb/Utility/ProcessInfo.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StreamString.h"

#include "llvm/TargetParser/Host.h"

extern "C" {
extern char **environ;
}

using namespace lldb;
using namespace lldb_private;

namespace lldb_private {
class ProcessLaunchInfo;
}

Environment Host::GetEnvironment() { return Environment(environ); }

static bool
GetOpenBSDProcessArgs(const ProcessInstanceInfoMatch *match_info_ptr,
                      ProcessInstanceInfo &process_info) {
  if (!process_info.ProcessIDIsValid())
    return false;

  int pid = process_info.GetProcessID();

  int mib[4] = {CTL_KERN, KERN_PROC_ARGS, pid, KERN_PROC_ARGV};
  size_t kern_proc_args_size = 0;

  // On OpenBSD, this will just fill ARG_MAX all the time
  if (::sysctl(mib, 4, NULL, &kern_proc_args_size, NULL, 0) == -1)
    return false;

  std::string arg_data(kern_proc_args_size, 0);

  if (::sysctl(mib, 4, (void *)arg_data.data(), &kern_proc_args_size, NULL, 0) == -1)
    return false;

  arg_data.resize(kern_proc_args_size);

  // arg_data is a NULL terminated list of pointers, where the pointers
  // point within arg_data to the location of the arg string
  DataExtractor data(arg_data.data(), arg_data.length(), endian::InlHostByteOrder(), sizeof(void *));

  lldb::offset_t offset = 0;
  lldb::offset_t arg_offset = 0;
  uint64_t arg_addr = 0;
  const char *cstr;

  arg_addr = data.GetAddress(&offset);
  arg_offset = arg_addr - (uint64_t)arg_data.data();
  cstr = data.GetCStr(&arg_offset);

  if (!cstr)
    return false;

  process_info.GetExecutableFile().SetFile(cstr, FileSpec::Style::native);

  if (match_info_ptr != NULL &&
      !NameMatches(process_info.GetExecutableFile().GetFilename().GetCString(),
                  match_info_ptr->GetNameMatchType(),
                  match_info_ptr->GetProcessInfo().GetName()))
  {
    return false;
  }

  Args &proc_args = process_info.GetArguments();

  while (1) {
    arg_addr = data.GetAddress(&offset);
    if (arg_addr == 0)
      break;
    arg_offset = arg_addr - (uint64_t)arg_data.data();
    cstr = data.GetCStr(&arg_offset);
    proc_args.AppendArgument(cstr);
  }
  return true;
}

static bool GetOpenBSDProcessCPUType(ProcessInstanceInfo &process_info) {
  if (process_info.ProcessIDIsValid()) {
    process_info.GetArchitecture() =
        HostInfo::GetArchitecture(HostInfo::eArchKindDefault);
    return true;
  }
  process_info.GetArchitecture().Clear();
  return false;
}

static bool GetOpenBSDProcessUserAndGroup(ProcessInstanceInfo &process_info) {

  if (process_info.ProcessIDIsValid()) {
    struct kinfo_proc proc_kinfo = {};
    size_t proc_kinfo_size = sizeof(proc_kinfo);
    int mib[6] = {CTL_KERN, KERN_PROC, KERN_PROC_PID,
                  (int)process_info.GetProcessID(),
                  sizeof(proc_kinfo), 1};

    if (::sysctl(mib, 6, &proc_kinfo, &proc_kinfo_size, NULL, 0) == 0) {
      if (proc_kinfo_size > 0) {
        process_info.SetParentProcessID(proc_kinfo.p_ppid);
        process_info.SetUserID(proc_kinfo.p_ruid);
        process_info.SetGroupID(proc_kinfo.p_rgid);
        process_info.SetEffectiveUserID(proc_kinfo.p_uid);
        process_info.SetEffectiveGroupID(proc_kinfo.p_gid);
        return true;
      }
    }
  }
  process_info.SetParentProcessID(LLDB_INVALID_PROCESS_ID);
  process_info.SetUserID(UINT32_MAX);
  process_info.SetGroupID(UINT32_MAX);
  process_info.SetEffectiveUserID(UINT32_MAX);
  process_info.SetEffectiveGroupID(UINT32_MAX);
  return false;
}

uint32_t Host::FindProcessesImpl(const ProcessInstanceInfoMatch &match_info,
                                 ProcessInstanceInfoList &process_infos) {
  std::vector<struct kinfo_proc> kinfos;

  int mib[6] = {CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0, sizeof(struct kinfo_proc), 0};

  size_t pid_data_size = 0;
  if (::sysctl(mib, 6, NULL, &pid_data_size, NULL, 0) != 0)
    return 0;

  // Add a few extra in case a few more show up
  const size_t estimated_pid_count =
      (pid_data_size / sizeof(struct kinfo_proc)) + 10;

  kinfos.resize(estimated_pid_count);
  mib[5] = estimated_pid_count;
  pid_data_size = kinfos.size() * sizeof(struct kinfo_proc);

  if (::sysctl(mib, 6, &kinfos[0], &pid_data_size, NULL, 0) != 0)
    return 0;

  const size_t actual_pid_count = (pid_data_size / sizeof(struct kinfo_proc));

  bool all_users = match_info.GetMatchAllUsers();
  const ::pid_t our_pid = getpid();
  const uid_t our_uid = getuid();
  for (size_t i = 0; i < actual_pid_count; i++) {
    const struct kinfo_proc &kinfo = kinfos[i];
    const bool kinfo_user_matches = (all_users || (kinfo.p_ruid == our_uid) ||
                                     // Special case, if lldb is being run as
                                     // root we can attach to anything.
                                     (our_uid == 0));

    if (kinfo_user_matches == false || // Make sure the user is acceptable
        kinfo.p_pid == our_pid ||     // Skip this process
        kinfo.p_pid == 0 ||           // Skip kernel (kernel pid is zero)
        kinfo.p_stat == SZOMB ||      // Zombies are bad, they like brains...
        kinfo.p_psflags & PS_TRACED || // Being debugged?
        kinfo.p_flag & P_WEXIT)       // Working on exiting
      continue;

    ProcessInstanceInfo process_info;
    process_info.SetProcessID(kinfo.p_pid);
    process_info.SetParentProcessID(kinfo.p_ppid);
    process_info.SetUserID(kinfo.p_ruid);
    process_info.SetGroupID(kinfo.p_rgid);
    process_info.SetEffectiveUserID(kinfo.p_svuid);
    process_info.SetEffectiveGroupID(kinfo.p_svgid);

    // Make sure our info matches before we go fetch the name and cpu type
    if (match_info.Matches(process_info) &&
        GetOpenBSDProcessArgs(&match_info, process_info)) {
      GetOpenBSDProcessCPUType(process_info);
      if (match_info.Matches(process_info))
        process_infos.push_back(process_info);
    }
  }

  return process_infos.size();
}

bool Host::GetProcessInfo(lldb::pid_t pid, ProcessInstanceInfo &process_info) {
  process_info.SetProcessID(pid);

  if (GetOpenBSDProcessArgs(NULL, process_info)) {
    // should use libprocstat instead of going right into sysctl?
    GetOpenBSDProcessCPUType(process_info);
    GetOpenBSDProcessUserAndGroup(process_info);
    return true;
  }

  process_info.Clear();
  return false;
}

Status Host::ShellExpandArguments(ProcessLaunchInfo &launch_info) {
  return Status("unimplemented");
}
