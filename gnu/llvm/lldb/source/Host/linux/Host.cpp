//===-- source/Host/linux/Host.cpp ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <optional>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "llvm/ADT/StringSwitch.h"
#include "llvm/Object/ELF.h"
#include "llvm/Support/ScopedPrinter.h"

#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/ProcessInfo.h"
#include "lldb/Utility/Status.h"

#include "lldb/Host/FileSystem.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Host/linux/Host.h"
#include "lldb/Host/linux/Support.h"
#include "lldb/Utility/DataExtractor.h"

using namespace lldb;
using namespace lldb_private;

namespace {

enum class ProcessState {
  Unknown,
  Dead,
  DiskSleep,
  Idle,
  Paging,
  Parked,
  Running,
  Sleeping,
  TracedOrStopped,
  Zombie,
};

constexpr int task_comm_len = 16;

struct StatFields {
  ::pid_t pid = LLDB_INVALID_PROCESS_ID;
  char comm[task_comm_len];
  char state;
  ::pid_t ppid = LLDB_INVALID_PROCESS_ID;
  ::pid_t pgrp = LLDB_INVALID_PROCESS_ID;
  ::pid_t session = LLDB_INVALID_PROCESS_ID;
  int tty_nr;
  int tpgid;
  unsigned flags;
  long unsigned minflt;
  long unsigned cminflt;
  long unsigned majflt;
  long unsigned cmajflt;
  long unsigned utime;
  long unsigned stime;
  long cutime;
  long cstime;
  // In proc_pid_stat(5) this field is specified as priority
  // but documented as realtime priority. To keep with the adopted
  // nomenclature in ProcessInstanceInfo, we adopt the documented
  // naming here.
  long realtime_priority;
  long priority;
  // .... other things. We don't need them below
};
}

namespace lldb_private {
class ProcessLaunchInfo;
}

static bool GetStatusInfo(::pid_t Pid, ProcessInstanceInfo &ProcessInfo,
                          ProcessState &State, ::pid_t &TracerPid,
                          ::pid_t &Tgid) {
  Log *log = GetLog(LLDBLog::Host);

  auto BufferOrError = getProcFile(Pid, "stat");
  if (!BufferOrError)
    return false;

  llvm::StringRef Rest = BufferOrError.get()->getBuffer();
  if (Rest.empty())
    return false;
  StatFields stat_fields;
  if (sscanf(
          Rest.data(),
          "%d %s %c %d %d %d %d %d %u %lu %lu %lu %lu %lu %lu %ld %ld %ld %ld",
          &stat_fields.pid, stat_fields.comm, &stat_fields.state,
          &stat_fields.ppid, &stat_fields.pgrp, &stat_fields.session,
          &stat_fields.tty_nr, &stat_fields.tpgid, &stat_fields.flags,
          &stat_fields.minflt, &stat_fields.cminflt, &stat_fields.majflt,
          &stat_fields.cmajflt, &stat_fields.utime, &stat_fields.stime,
          &stat_fields.cutime, &stat_fields.cstime,
          &stat_fields.realtime_priority, &stat_fields.priority) < 0) {
    return false;
  }

  auto convert = [sc_clk_ticks = sysconf(_SC_CLK_TCK)](auto time_in_ticks) {
    ProcessInstanceInfo::timespec ts;
    if (sc_clk_ticks <= 0) {
      return ts;
    }
    ts.tv_sec = time_in_ticks / sc_clk_ticks;
    double remainder =
        (static_cast<double>(time_in_ticks) / sc_clk_ticks) - ts.tv_sec;
    ts.tv_usec =
        std::chrono::microseconds{std::lround(1e+6 * remainder)}.count();
    return ts;
  };

  // Priority (nice) values run from 19 to -20 inclusive (in linux). In the
  // prpsinfo struct pr_nice is a char.
  auto priority_value = static_cast<int8_t>(
      (stat_fields.priority < 0 ? 0x80 : 0x00) | (stat_fields.priority & 0x7f));

  ProcessInfo.SetParentProcessID(stat_fields.ppid);
  ProcessInfo.SetProcessGroupID(stat_fields.pgrp);
  ProcessInfo.SetProcessSessionID(stat_fields.session);
  ProcessInfo.SetUserTime(convert(stat_fields.utime));
  ProcessInfo.SetSystemTime(convert(stat_fields.stime));
  ProcessInfo.SetCumulativeUserTime(convert(stat_fields.cutime));
  ProcessInfo.SetCumulativeSystemTime(convert(stat_fields.cstime));
  ProcessInfo.SetPriorityValue(priority_value);
  switch (stat_fields.state) {
  case 'R':
    State = ProcessState::Running;
    break;
  case 'S':
    State = ProcessState::Sleeping;
    break;
  case 'D':
    State = ProcessState::DiskSleep;
    break;
  case 'Z':
    State = ProcessState::Zombie;
    break;
  case 'X':
    State = ProcessState::Dead;
    break;
  case 'P':
    State = ProcessState::Parked;
    break;
  case 'W':
    State = ProcessState::Paging;
    break;
  case 'I':
    State = ProcessState::Idle;
    break;
  case 'T': // Stopped on a signal or (before Linux 2.6.33) trace stopped
    [[fallthrough]];
  case 't':
    State = ProcessState::TracedOrStopped;
    break;
  default:
    State = ProcessState::Unknown;
    break;
  }
  ProcessInfo.SetIsZombie(State == ProcessState::Zombie);

  if (State == ProcessState::Unknown) {
    LLDB_LOG(log, "Unknown process state {0}", stat_fields.state);
  }

  BufferOrError = getProcFile(Pid, "status");
  if (!BufferOrError)
    return false;

  Rest = BufferOrError.get()->getBuffer();
  if (Rest.empty())
    return false;

  while (!Rest.empty()) {
    llvm::StringRef Line;
    std::tie(Line, Rest) = Rest.split('\n');

    if (Line.consume_front("Gid:")) {
      // Real, effective, saved set, and file system GIDs. Read the first two.
      Line = Line.ltrim();
      uint32_t RGid, EGid;
      Line.consumeInteger(10, RGid);
      Line = Line.ltrim();
      Line.consumeInteger(10, EGid);

      ProcessInfo.SetGroupID(RGid);
      ProcessInfo.SetEffectiveGroupID(EGid);
    } else if (Line.consume_front("Uid:")) {
      // Real, effective, saved set, and file system UIDs. Read the first two.
      Line = Line.ltrim();
      uint32_t RUid, EUid;
      Line.consumeInteger(10, RUid);
      Line = Line.ltrim();
      Line.consumeInteger(10, EUid);

      ProcessInfo.SetUserID(RUid);
      ProcessInfo.SetEffectiveUserID(EUid);
    } else if (Line.consume_front("TracerPid:")) {
      Line = Line.ltrim();
      Line.consumeInteger(10, TracerPid);
    } else if (Line.consume_front("Tgid:")) {
      Line = Line.ltrim();
      Line.consumeInteger(10, Tgid);
    }
  }
  return true;
}

static bool IsDirNumeric(const char *dname) {
  for (; *dname; dname++) {
    if (!isdigit(*dname))
      return false;
  }
  return true;
}

static ArchSpec GetELFProcessCPUType(llvm::StringRef exe_path) {
  Log *log = GetLog(LLDBLog::Host);

  auto buffer_sp = FileSystem::Instance().CreateDataBuffer(exe_path, 0x20, 0);
  if (!buffer_sp)
    return ArchSpec();

  uint8_t exe_class =
      llvm::object::getElfArchType(
          {reinterpret_cast<const char *>(buffer_sp->GetBytes()),
           size_t(buffer_sp->GetByteSize())})
          .first;

  switch (exe_class) {
  case llvm::ELF::ELFCLASS32:
    return HostInfo::GetArchitecture(HostInfo::eArchKind32);
  case llvm::ELF::ELFCLASS64:
    return HostInfo::GetArchitecture(HostInfo::eArchKind64);
  default:
    LLDB_LOG(log, "Unknown elf class ({0}) in file {1}", exe_class, exe_path);
    return ArchSpec();
  }
}

static void GetProcessArgs(::pid_t pid, ProcessInstanceInfo &process_info) {
  auto BufferOrError = getProcFile(pid, "cmdline");
  if (!BufferOrError)
    return;
  std::unique_ptr<llvm::MemoryBuffer> Cmdline = std::move(*BufferOrError);

  llvm::StringRef Arg0, Rest;
  std::tie(Arg0, Rest) = Cmdline->getBuffer().split('\0');
  process_info.SetArg0(Arg0);
  while (!Rest.empty()) {
    llvm::StringRef Arg;
    std::tie(Arg, Rest) = Rest.split('\0');
    process_info.GetArguments().AppendArgument(Arg);
  }
}

static void GetExePathAndArch(::pid_t pid, ProcessInstanceInfo &process_info) {
  Log *log = GetLog(LLDBLog::Process);
  std::string ExePath(PATH_MAX, '\0');

  // We can't use getProcFile here because proc/[pid]/exe is a symbolic link.
  llvm::SmallString<64> ProcExe;
  (llvm::Twine("/proc/") + llvm::Twine(pid) + "/exe").toVector(ProcExe);

  ssize_t len = readlink(ProcExe.c_str(), &ExePath[0], PATH_MAX);
  if (len > 0) {
    ExePath.resize(len);
  } else {
    LLDB_LOG(log, "failed to read link exe link for {0}: {1}", pid,
             Status(errno, eErrorTypePOSIX));
    ExePath.resize(0);
  }
  // If the binary has been deleted, the link name has " (deleted)" appended.
  // Remove if there.
  llvm::StringRef PathRef = ExePath;
  PathRef.consume_back(" (deleted)");

  if (!PathRef.empty()) {
    process_info.GetExecutableFile().SetFile(PathRef, FileSpec::Style::native);
    process_info.SetArchitecture(GetELFProcessCPUType(PathRef));
  }
}

static void GetProcessEnviron(::pid_t pid, ProcessInstanceInfo &process_info) {
  // Get the process environment.
  auto BufferOrError = getProcFile(pid, "environ");
  if (!BufferOrError)
    return;

  std::unique_ptr<llvm::MemoryBuffer> Environ = std::move(*BufferOrError);
  llvm::StringRef Rest = Environ->getBuffer();
  while (!Rest.empty()) {
    llvm::StringRef Var;
    std::tie(Var, Rest) = Rest.split('\0');
    process_info.GetEnvironment().insert(Var);
  }
}

static bool GetProcessAndStatInfo(::pid_t pid,
                                  ProcessInstanceInfo &process_info,
                                  ProcessState &State, ::pid_t &tracerpid) {
  ::pid_t tgid;
  tracerpid = 0;
  process_info.Clear();

  process_info.SetProcessID(pid);

  GetExePathAndArch(pid, process_info);
  GetProcessArgs(pid, process_info);
  GetProcessEnviron(pid, process_info);

  // Get User and Group IDs and get tracer pid.
  if (!GetStatusInfo(pid, process_info, State, tracerpid, tgid))
    return false;

  return true;
}

uint32_t Host::FindProcessesImpl(const ProcessInstanceInfoMatch &match_info,
                                 ProcessInstanceInfoList &process_infos) {
  static const char procdir[] = "/proc/";

  DIR *dirproc = opendir(procdir);
  if (dirproc) {
    struct dirent *direntry = nullptr;
    const uid_t our_uid = getuid();
    const lldb::pid_t our_pid = getpid();
    bool all_users = match_info.GetMatchAllUsers();

    while ((direntry = readdir(dirproc)) != nullptr) {
      if (direntry->d_type != DT_DIR || !IsDirNumeric(direntry->d_name))
        continue;

      lldb::pid_t pid = atoi(direntry->d_name);

      // Skip this process.
      if (pid == our_pid)
        continue;

      ::pid_t tracerpid;
      ProcessState State;
      ProcessInstanceInfo process_info;

      if (!GetProcessAndStatInfo(pid, process_info, State, tracerpid))
        continue;

      // Skip if process is being debugged.
      if (tracerpid != 0)
        continue;

      if (State == ProcessState::Zombie)
        continue;

      // Check for user match if we're not matching all users and not running
      // as root.
      if (!all_users && (our_uid != 0) && (process_info.GetUserID() != our_uid))
        continue;

      if (match_info.Matches(process_info)) {
        process_infos.push_back(process_info);
      }
    }

    closedir(dirproc);
  }

  return process_infos.size();
}

bool Host::FindProcessThreads(const lldb::pid_t pid, TidMap &tids_to_attach) {
  bool tids_changed = false;
  static const char procdir[] = "/proc/";
  static const char taskdir[] = "/task/";
  std::string process_task_dir = procdir + llvm::to_string(pid) + taskdir;
  DIR *dirproc = opendir(process_task_dir.c_str());

  if (dirproc) {
    struct dirent *direntry = nullptr;
    while ((direntry = readdir(dirproc)) != nullptr) {
      if (direntry->d_type != DT_DIR || !IsDirNumeric(direntry->d_name))
        continue;

      lldb::tid_t tid = atoi(direntry->d_name);
      TidMap::iterator it = tids_to_attach.find(tid);
      if (it == tids_to_attach.end()) {
        tids_to_attach.insert(TidPair(tid, false));
        tids_changed = true;
      }
    }
    closedir(dirproc);
  }

  return tids_changed;
}

bool Host::GetProcessInfo(lldb::pid_t pid, ProcessInstanceInfo &process_info) {
  ::pid_t tracerpid;
  ProcessState State;
  return GetProcessAndStatInfo(pid, process_info, State, tracerpid);
}

Environment Host::GetEnvironment() { return Environment(environ); }

Status Host::ShellExpandArguments(ProcessLaunchInfo &launch_info) {
  return Status("unimplemented");
}

std::optional<lldb::pid_t> lldb_private::getPIDForTID(lldb::pid_t tid) {
  ::pid_t tracerpid, tgid = LLDB_INVALID_PROCESS_ID;
  ProcessInstanceInfo process_info;
  ProcessState state;

  if (!GetStatusInfo(tid, process_info, state, tracerpid, tgid) ||
      tgid == LLDB_INVALID_PROCESS_ID)
    return std::nullopt;
  return tgid;
}
