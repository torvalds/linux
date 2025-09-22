//===-- source/Host/netbsd/HostNetBSD.cpp ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <cstdio>
#include <dlfcn.h>
#include <execinfo.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/types.h>

#include <climits>

#include <kvm.h>
#include <sys/exec.h>
#include <sys/ptrace.h>

#include "lldb/Host/FileSystem.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/NameMatches.h"
#include "lldb/Utility/ProcessInfo.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StreamString.h"

#include "llvm/Object/ELF.h"
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

static bool GetNetBSDProcessArgs(const ProcessInstanceInfoMatch *match_info_ptr,
                                 ProcessInstanceInfo &process_info) {
  if (!process_info.ProcessIDIsValid())
    return false;

  int pid = process_info.GetProcessID();

  int mib[4] = {CTL_KERN, KERN_PROC_ARGS, pid, KERN_PROC_ARGV};

  char arg_data[8192];
  size_t arg_data_size = sizeof(arg_data);
  if (::sysctl(mib, 4, arg_data, &arg_data_size, NULL, 0) != 0)
    return false;

  DataExtractor data(arg_data, arg_data_size, endian::InlHostByteOrder(),
                     sizeof(void *));
  lldb::offset_t offset = 0;
  const char *cstr;

  cstr = data.GetCStr(&offset);
  if (!cstr)
    return false;

  process_info.GetExecutableFile().SetFile(cstr,
                                           FileSpec::Style::native);

  if (!(match_info_ptr == NULL ||
        NameMatches(process_info.GetExecutableFile().GetFilename().GetCString(),
                    match_info_ptr->GetNameMatchType(),
                    match_info_ptr->GetProcessInfo().GetName())))
    return false;

  process_info.SetArg0(cstr);
  Args &proc_args = process_info.GetArguments();
  while (1) {
    const uint8_t *p = data.PeekData(offset, 1);
    while ((p != NULL) && (*p == '\0') && offset < arg_data_size) {
      ++offset;
      p = data.PeekData(offset, 1);
    }
    if (p == NULL || offset >= arg_data_size)
      break;

    cstr = data.GetCStr(&offset);
    if (!cstr)
      break;

    proc_args.AppendArgument(llvm::StringRef(cstr));
  }

  return true;
}

static bool GetNetBSDProcessCPUType(ProcessInstanceInfo &process_info) {
  Log *log = GetLog(LLDBLog::Host);

  if (process_info.ProcessIDIsValid()) {
    auto buffer_sp = FileSystem::Instance().CreateDataBuffer(
        process_info.GetExecutableFile(), 0x20, 0);
    if (buffer_sp) {
      uint8_t exe_class =
          llvm::object::getElfArchType(
              {reinterpret_cast<const char *>(buffer_sp->GetBytes()),
               size_t(buffer_sp->GetByteSize())})
              .first;

      switch (exe_class) {
      case llvm::ELF::ELFCLASS32:
        process_info.GetArchitecture() =
            HostInfo::GetArchitecture(HostInfo::eArchKind32);
        return true;
      case llvm::ELF::ELFCLASS64:
        process_info.GetArchitecture() =
            HostInfo::GetArchitecture(HostInfo::eArchKind64);
        return true;
      default:
        LLDB_LOG(log, "Unknown elf class ({0}) in file {1}", exe_class,
                 process_info.GetExecutableFile());
      }
    }
  }
  process_info.GetArchitecture().Clear();
  return false;
}

static bool GetNetBSDProcessUserAndGroup(ProcessInstanceInfo &process_info) {
  ::kvm_t *kdp;
  char errbuf[_POSIX2_LINE_MAX]; /* XXX: error string unused */

  struct ::kinfo_proc2 *proc_kinfo;
  const int pid = process_info.GetProcessID();
  int nproc;

  if (!process_info.ProcessIDIsValid())
    goto error;

  if ((kdp = ::kvm_openfiles(NULL, NULL, NULL, KVM_NO_FILES, errbuf)) == NULL)
    goto error;

  if ((proc_kinfo = ::kvm_getproc2(kdp, KERN_PROC_PID, pid,
                                   sizeof(struct ::kinfo_proc2), &nproc)) ==
      NULL) {
    ::kvm_close(kdp);
    goto error;
  }

  if (nproc < 1) {
    ::kvm_close(kdp); /* XXX: we don't check for error here */
    goto error;
  }

  process_info.SetParentProcessID(proc_kinfo->p_ppid);
  process_info.SetUserID(proc_kinfo->p_ruid);
  process_info.SetGroupID(proc_kinfo->p_rgid);
  process_info.SetEffectiveUserID(proc_kinfo->p_uid);
  process_info.SetEffectiveGroupID(proc_kinfo->p_gid);

  ::kvm_close(kdp); /* XXX: we don't check for error here */

  return true;

error:
  process_info.SetParentProcessID(LLDB_INVALID_PROCESS_ID);
  process_info.SetUserID(UINT32_MAX);
  process_info.SetGroupID(UINT32_MAX);
  process_info.SetEffectiveUserID(UINT32_MAX);
  process_info.SetEffectiveGroupID(UINT32_MAX);
  return false;
}

uint32_t Host::FindProcessesImpl(const ProcessInstanceInfoMatch &match_info,
                                 ProcessInstanceInfoList &process_infos) {
  const ::pid_t our_pid = ::getpid();
  const ::uid_t our_uid = ::getuid();

  const bool all_users =
      match_info.GetMatchAllUsers() ||
      // Special case, if lldb is being run as root we can attach to anything
      (our_uid == 0);

  kvm_t *kdp;
  char errbuf[_POSIX2_LINE_MAX]; /* XXX: error string unused */
  if ((kdp = ::kvm_openfiles(NULL, NULL, NULL, KVM_NO_FILES, errbuf)) == NULL)
    return 0;

  struct ::kinfo_proc2 *proc_kinfo;
  int nproc;
  if ((proc_kinfo = ::kvm_getproc2(kdp, KERN_PROC_ALL, 0,
                                   sizeof(struct ::kinfo_proc2), &nproc)) ==
      NULL) {
    ::kvm_close(kdp);
    return 0;
  }

  ProcessInstanceInfoMatch match_info_noname{match_info};
  match_info_noname.SetNameMatchType(NameMatch::Ignore);

  for (int i = 0; i < nproc; i++) {
    if (proc_kinfo[i].p_pid < 1)
      continue; /* not valid */
    /* Make sure the user is acceptable */
    if (!all_users && proc_kinfo[i].p_ruid != our_uid)
      continue;

    if (proc_kinfo[i].p_pid == our_pid ||  // Skip this process
        proc_kinfo[i].p_pid == 0 ||        // Skip kernel (kernel pid is 0)
        proc_kinfo[i].p_stat == LSZOMB ||  // Zombies are bad
        proc_kinfo[i].p_flag & P_TRACED || // Being debugged?
        proc_kinfo[i].p_flag & P_WEXIT)    // Working on exiting
      continue;

    // Every thread is a process in NetBSD, but all the threads of a single
    // process have the same pid. Do not store the process info in the result
    // list if a process with given identifier is already registered there.
    if (proc_kinfo[i].p_nlwps > 1) {
      bool already_registered = false;
      for (size_t pi = 0; pi < process_infos.size(); pi++) {
        if ((::pid_t)process_infos[pi].GetProcessID() == proc_kinfo[i].p_pid) {
          already_registered = true;
          break;
        }
      }

      if (already_registered)
        continue;
    }
    ProcessInstanceInfo process_info;
    process_info.SetProcessID(proc_kinfo[i].p_pid);
    process_info.SetParentProcessID(proc_kinfo[i].p_ppid);
    process_info.SetUserID(proc_kinfo[i].p_ruid);
    process_info.SetGroupID(proc_kinfo[i].p_rgid);
    process_info.SetEffectiveUserID(proc_kinfo[i].p_uid);
    process_info.SetEffectiveGroupID(proc_kinfo[i].p_gid);
    // Make sure our info matches before we go fetch the name and cpu type
    if (match_info_noname.Matches(process_info) &&
        GetNetBSDProcessArgs(&match_info, process_info)) {
      GetNetBSDProcessCPUType(process_info);
      if (match_info.Matches(process_info))
        process_infos.push_back(process_info);
    }
  }

  kvm_close(kdp); /* XXX: we don't check for error here */

  return process_infos.size();
}

bool Host::GetProcessInfo(lldb::pid_t pid, ProcessInstanceInfo &process_info) {
  process_info.SetProcessID(pid);

  if (GetNetBSDProcessArgs(NULL, process_info)) {
    GetNetBSDProcessCPUType(process_info);
    GetNetBSDProcessUserAndGroup(process_info);
    return true;
  }

  process_info.Clear();
  return false;
}

Status Host::ShellExpandArguments(ProcessLaunchInfo &launch_info) {
  return Status("unimplemented");
}
