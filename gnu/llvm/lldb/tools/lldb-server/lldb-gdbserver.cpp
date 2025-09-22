//===-- lldb-gdbserver.cpp --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef _WIN32
#include <csignal>
#include <unistd.h>
#endif

#include "LLDBServerUtilities.h"
#include "Plugins/Process/gdb-remote/GDBRemoteCommunicationServerLLGS.h"
#include "Plugins/Process/gdb-remote/ProcessGDBRemoteLog.h"
#include "lldb/Host/Config.h"
#include "lldb/Host/ConnectionFileDescriptor.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/Pipe.h"
#include "lldb/Host/Socket.h"
#include "lldb/Host/common/NativeProcessProtocol.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Status.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/Errno.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/WithColor.h"

#if defined(__linux__)
#include "Plugins/Process/Linux/NativeProcessLinux.h"
#elif defined(__FreeBSD__)
#include "Plugins/Process/FreeBSD/NativeProcessFreeBSD.h"
#elif defined(__NetBSD__)
#include "Plugins/Process/NetBSD/NativeProcessNetBSD.h"
#elif defined(__OpenBSD__)
#include "Plugins/Process/OpenBSD/NativeProcessOpenBSD.h"
#elif defined(_WIN32)
#include "Plugins/Process/Windows/Common/NativeProcessWindows.h"
#endif

#ifndef LLGS_PROGRAM_NAME
#define LLGS_PROGRAM_NAME "lldb-server"
#endif

#ifndef LLGS_VERSION_STR
#define LLGS_VERSION_STR "local_build"
#endif

using namespace llvm;
using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::lldb_server;
using namespace lldb_private::process_gdb_remote;

namespace {
#if defined(__linux__)
typedef process_linux::NativeProcessLinux::Manager NativeProcessManager;
#elif defined(__FreeBSD__)
typedef process_freebsd::NativeProcessFreeBSD::Manager NativeProcessManager;
#elif defined(__NetBSD__)
typedef process_netbsd::NativeProcessNetBSD::Manager NativeProcessManager;
#elif defined(__OpenBSD__)
typedef process_openbsd::NativeProcessOpenBSD::Manager NativeProcessManager;
#elif defined(_WIN32)
typedef NativeProcessWindows::Manager NativeProcessManager;
#else
// Dummy implementation to make sure the code compiles
class NativeProcessManager : public NativeProcessProtocol::Manager {
public:
  NativeProcessManager(MainLoop &mainloop)
      : NativeProcessProtocol::Manager(mainloop) {}

  llvm::Expected<std::unique_ptr<NativeProcessProtocol>>
  Launch(ProcessLaunchInfo &launch_info,
         NativeProcessProtocol::NativeDelegate &native_delegate) override {
    llvm_unreachable("Not implemented");
  }
  llvm::Expected<std::unique_ptr<NativeProcessProtocol>>
  Attach(lldb::pid_t pid,
         NativeProcessProtocol::NativeDelegate &native_delegate) override {
    llvm_unreachable("Not implemented");
  }
};
#endif
}

#ifndef _WIN32
// Watch for signals
static int g_sighup_received_count = 0;

static void sighup_handler(MainLoopBase &mainloop) {
  ++g_sighup_received_count;

  Log *log = GetLog(LLDBLog::Process);
  LLDB_LOGF(log, "lldb-server:%s swallowing SIGHUP (receive count=%d)",
            __FUNCTION__, g_sighup_received_count);

  if (g_sighup_received_count >= 2)
    mainloop.RequestTermination();
}
#endif // #ifndef _WIN32

void handle_attach_to_pid(GDBRemoteCommunicationServerLLGS &gdb_server,
                          lldb::pid_t pid) {
  Status error = gdb_server.AttachToProcess(pid);
  if (error.Fail()) {
    fprintf(stderr, "error: failed to attach to pid %" PRIu64 ": %s\n", pid,
            error.AsCString());
    exit(1);
  }
}

void handle_attach_to_process_name(GDBRemoteCommunicationServerLLGS &gdb_server,
                                   const std::string &process_name) {
  // FIXME implement.
}

void handle_attach(GDBRemoteCommunicationServerLLGS &gdb_server,
                   const std::string &attach_target) {
  assert(!attach_target.empty() && "attach_target cannot be empty");

  // First check if the attach_target is convertible to a long. If so, we'll use
  // it as a pid.
  char *end_p = nullptr;
  const long int pid = strtol(attach_target.c_str(), &end_p, 10);

  // We'll call it a match if the entire argument is consumed.
  if (end_p &&
      static_cast<size_t>(end_p - attach_target.c_str()) ==
          attach_target.size())
    handle_attach_to_pid(gdb_server, static_cast<lldb::pid_t>(pid));
  else
    handle_attach_to_process_name(gdb_server, attach_target);
}

void handle_launch(GDBRemoteCommunicationServerLLGS &gdb_server,
                   llvm::ArrayRef<llvm::StringRef> Arguments) {
  ProcessLaunchInfo info;
  info.GetFlags().Set(eLaunchFlagStopAtEntry | eLaunchFlagDebug |
                      eLaunchFlagDisableASLR);
  info.SetArguments(Args(Arguments), true);

  llvm::SmallString<64> cwd;
  if (std::error_code ec = llvm::sys::fs::current_path(cwd)) {
    llvm::errs() << "Error getting current directory: " << ec.message() << "\n";
    exit(1);
  }
  FileSpec cwd_spec(cwd);
  FileSystem::Instance().Resolve(cwd_spec);
  info.SetWorkingDirectory(cwd_spec);
  info.GetEnvironment() = Host::GetEnvironment();

  gdb_server.SetLaunchInfo(info);

  Status error = gdb_server.LaunchProcess();
  if (error.Fail()) {
    llvm::errs() << llvm::formatv("error: failed to launch '{0}': {1}\n",
                                  Arguments[0], error);
    exit(1);
  }
}

Status writeSocketIdToPipe(Pipe &port_pipe, llvm::StringRef socket_id) {
  size_t bytes_written = 0;
  // Write the port number as a C string with the NULL terminator.
  return port_pipe.Write(socket_id.data(), socket_id.size() + 1, bytes_written);
}

Status writeSocketIdToPipe(const char *const named_pipe_path,
                           llvm::StringRef socket_id) {
  Pipe port_name_pipe;
  // Wait for 10 seconds for pipe to be opened.
  auto error = port_name_pipe.OpenAsWriterWithTimeout(named_pipe_path, false,
                                                      std::chrono::seconds{10});
  if (error.Fail())
    return error;
  return writeSocketIdToPipe(port_name_pipe, socket_id);
}

Status writeSocketIdToPipe(lldb::pipe_t unnamed_pipe,
                           llvm::StringRef socket_id) {
  Pipe port_pipe{LLDB_INVALID_PIPE, unnamed_pipe};
  return writeSocketIdToPipe(port_pipe, socket_id);
}

void ConnectToRemote(MainLoop &mainloop,
                     GDBRemoteCommunicationServerLLGS &gdb_server,
                     bool reverse_connect, llvm::StringRef host_and_port,
                     const char *const progname, const char *const subcommand,
                     const char *const named_pipe_path, pipe_t unnamed_pipe,
                     int connection_fd) {
  Status error;

  std::unique_ptr<Connection> connection_up;
  std::string url;

  if (connection_fd != -1) {
    url = llvm::formatv("fd://{0}", connection_fd).str();

    // Create the connection.
#if LLDB_ENABLE_POSIX && !defined _WIN32
    ::fcntl(connection_fd, F_SETFD, FD_CLOEXEC);
#endif
  } else if (!host_and_port.empty()) {
    llvm::Expected<std::string> url_exp =
        LLGSArgToURL(host_and_port, reverse_connect);
    if (!url_exp) {
      llvm::errs() << llvm::formatv("error: invalid host:port or URL '{0}': "
                                    "{1}\n",
                                    host_and_port,
                                    llvm::toString(url_exp.takeError()));
      exit(-1);
    }

    url = std::move(url_exp.get());
  }

  if (!url.empty()) {
    // Create the connection or server.
    std::unique_ptr<ConnectionFileDescriptor> conn_fd_up{
        new ConnectionFileDescriptor};
    auto connection_result = conn_fd_up->Connect(
        url,
        [named_pipe_path, unnamed_pipe](llvm::StringRef socket_id) {
          // If we have a named pipe to write the socket id back to, do that
          // now.
          if (named_pipe_path && named_pipe_path[0]) {
            Status error = writeSocketIdToPipe(named_pipe_path, socket_id);
            if (error.Fail())
              llvm::errs() << llvm::formatv(
                  "failed to write to the named pipe '{0}': {1}\n",
                  named_pipe_path, error.AsCString());
          }
          // If we have an unnamed pipe to write the socket id back to, do
          // that now.
          else if (unnamed_pipe != LLDB_INVALID_PIPE) {
            Status error = writeSocketIdToPipe(unnamed_pipe, socket_id);
            if (error.Fail())
              llvm::errs() << llvm::formatv(
                  "failed to write to the unnamed pipe: {0}\n", error);
          }
        },
        &error);

    if (error.Fail()) {
      llvm::errs() << llvm::formatv(
          "error: failed to connect to client at '{0}': {1}\n", url, error);
      exit(-1);
    }
    if (connection_result != eConnectionStatusSuccess) {
      llvm::errs() << llvm::formatv(
          "error: failed to connect to client at '{0}' "
          "(connection status: {1})\n",
          url, static_cast<int>(connection_result));
      exit(-1);
    }
    connection_up = std::move(conn_fd_up);
  }
  error = gdb_server.InitializeConnection(std::move(connection_up));
  if (error.Fail()) {
    llvm::errs() << llvm::formatv("failed to initialize connection\n", error);
    exit(-1);
  }
  llvm::outs() << "Connection established.\n";
}

namespace {
using namespace llvm::opt;

enum ID {
  OPT_INVALID = 0, // This is not an option ID.
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "LLGSOptions.inc"
#undef OPTION
};

#define PREFIX(NAME, VALUE)                                                    \
  constexpr llvm::StringLiteral NAME##_init[] = VALUE;                         \
  constexpr llvm::ArrayRef<llvm::StringLiteral> NAME(                          \
      NAME##_init, std::size(NAME##_init) - 1);
#include "LLGSOptions.inc"
#undef PREFIX

static constexpr opt::OptTable::Info InfoTable[] = {
#define OPTION(...) LLVM_CONSTRUCT_OPT_INFO(__VA_ARGS__),
#include "LLGSOptions.inc"
#undef OPTION
};

class LLGSOptTable : public opt::GenericOptTable {
public:
  LLGSOptTable() : opt::GenericOptTable(InfoTable) {}

  void PrintHelp(llvm::StringRef Name) {
    std::string Usage =
        (Name + " [options] [[host]:port] [[--] program args...]").str();
    OptTable::printHelp(llvm::outs(), Usage.c_str(), "lldb-server");
    llvm::outs() << R"(
DESCRIPTION
  lldb-server connects to the LLDB client, which drives the debugging session.
  If no connection options are given, the [host]:port argument must be present
  and will denote the address that lldb-server will listen on. [host] defaults
  to "localhost" if empty. Port can be zero, in which case the port number will
  be chosen dynamically and written to destinations given by --named-pipe and
  --pipe arguments.

  If no target is selected at startup, lldb-server can be directed by the LLDB
  client to launch or attach to a process.

)";
  }
};
} // namespace

int main_gdbserver(int argc, char *argv[]) {
  Status error;
  MainLoop mainloop;
#ifndef _WIN32
  // Setup signal handlers first thing.
  signal(SIGPIPE, SIG_IGN);
  MainLoop::SignalHandleUP sighup_handle =
      mainloop.RegisterSignal(SIGHUP, sighup_handler, error);
#endif

  const char *progname = argv[0];
  const char *subcommand = argv[1];
  std::string attach_target;
  std::string named_pipe_path;
  std::string log_file;
  StringRef
      log_channels; // e.g. "lldb process threads:gdb-remote default:linux all"
  lldb::pipe_t unnamed_pipe = LLDB_INVALID_PIPE;
  bool reverse_connect = false;
  int connection_fd = -1;

  // ProcessLaunchInfo launch_info;
  ProcessAttachInfo attach_info;

  LLGSOptTable Opts;
  llvm::BumpPtrAllocator Alloc;
  llvm::StringSaver Saver(Alloc);
  bool HasError = false;
  opt::InputArgList Args = Opts.parseArgs(argc - 1, argv + 1, OPT_UNKNOWN,
                                          Saver, [&](llvm::StringRef Msg) {
                                            WithColor::error() << Msg << "\n";
                                            HasError = true;
                                          });
  std::string Name =
      (llvm::sys::path::filename(argv[0]) + " g[dbserver]").str();
  std::string HelpText =
      "Use '" + Name + " --help' for a complete list of options.\n";
  if (HasError) {
    llvm::errs() << HelpText;
    return 1;
  }

  if (Args.hasArg(OPT_help)) {
    Opts.PrintHelp(Name);
    return 0;
  }

#ifndef _WIN32
  if (Args.hasArg(OPT_setsid)) {
    // Put llgs into a new session. Terminals group processes
    // into sessions and when a special terminal key sequences
    // (like control+c) are typed they can cause signals to go out to
    // all processes in a session. Using this --setsid (-S) option
    // will cause debugserver to run in its own sessions and be free
    // from such issues.
    //
    // This is useful when llgs is spawned from a command
    // line application that uses llgs to do the debugging,
    // yet that application doesn't want llgs receiving the
    // signals sent to the session (i.e. dying when anyone hits ^C).
    {
      const ::pid_t new_sid = setsid();
      if (new_sid == -1) {
        WithColor::warning()
            << llvm::formatv("failed to set new session id for {0} ({1})\n",
                             LLGS_PROGRAM_NAME, llvm::sys::StrError());
      }
    }
  }
#endif

  log_file = Args.getLastArgValue(OPT_log_file).str();
  log_channels = Args.getLastArgValue(OPT_log_channels);
  named_pipe_path = Args.getLastArgValue(OPT_named_pipe).str();
  reverse_connect = Args.hasArg(OPT_reverse_connect);
  attach_target = Args.getLastArgValue(OPT_attach).str();
  if (Args.hasArg(OPT_pipe)) {
    uint64_t Arg;
    if (!llvm::to_integer(Args.getLastArgValue(OPT_pipe), Arg)) {
      WithColor::error() << "invalid '--pipe' argument\n" << HelpText;
      return 1;
    }
    unnamed_pipe = (pipe_t)Arg;
  }
  if (Args.hasArg(OPT_fd)) {
    if (!llvm::to_integer(Args.getLastArgValue(OPT_fd), connection_fd)) {
      WithColor::error() << "invalid '--fd' argument\n" << HelpText;
      return 1;
    }
  }

  if (!LLDBServerUtilities::SetupLogging(
          log_file, log_channels,
          LLDB_LOG_OPTION_PREPEND_TIMESTAMP |
              LLDB_LOG_OPTION_PREPEND_FILE_FUNCTION))
    return -1;

  std::vector<llvm::StringRef> Inputs;
  for (opt::Arg *Arg : Args.filtered(OPT_INPUT))
    Inputs.push_back(Arg->getValue());
  if (opt::Arg *Arg = Args.getLastArg(OPT_REM)) {
    for (const char *Val : Arg->getValues())
      Inputs.push_back(Val);
  }
  if (Inputs.empty() && connection_fd == -1) {
    WithColor::error() << "no connection arguments\n" << HelpText;
    return 1;
  }

  NativeProcessManager manager(mainloop);
  GDBRemoteCommunicationServerLLGS gdb_server(mainloop, manager);

  llvm::StringRef host_and_port;
  if (!Inputs.empty()) {
    host_and_port = Inputs.front();
    Inputs.erase(Inputs.begin());
  }

  // Any arguments left over are for the program that we need to launch. If
  // there
  // are no arguments, then the GDB server will start up and wait for an 'A'
  // packet
  // to launch a program, or a vAttach packet to attach to an existing process,
  // unless
  // explicitly asked to attach with the --attach={pid|program_name} form.
  if (!attach_target.empty())
    handle_attach(gdb_server, attach_target);
  else if (!Inputs.empty())
    handle_launch(gdb_server, Inputs);

  // Print version info.
  printf("%s-%s\n", LLGS_PROGRAM_NAME, LLGS_VERSION_STR);

  ConnectToRemote(mainloop, gdb_server, reverse_connect, host_and_port,
                  progname, subcommand, named_pipe_path.c_str(),
                  unnamed_pipe, connection_fd);

  if (!gdb_server.IsConnected()) {
    fprintf(stderr, "no connection information provided, unable to run\n");
    return 1;
  }

  Status ret = mainloop.Run();
  if (ret.Fail()) {
    fprintf(stderr, "lldb-server terminating due to error: %s\n",
            ret.AsCString());
    return 1;
  }
  fprintf(stderr, "lldb-server exiting...\n");

  return 0;
}
