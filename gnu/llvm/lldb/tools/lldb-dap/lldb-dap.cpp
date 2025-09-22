//===-- lldb-dap.cpp -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DAP.h"
#include "Watchpoint.h"
#include "lldb/API/SBMemoryRegionInfo.h"

#include <cassert>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <sys/stat.h>
#include <sys/types.h>
#if defined(_WIN32)
// We need to #define NOMINMAX in order to skip `min()` and `max()` macro
// definitions that conflict with other system headers.
// We also need to #undef GetObject (which is defined to GetObjectW) because
// the JSON code we use also has methods named `GetObject()` and we conflict
// against these.
#define NOMINMAX
#include <windows.h>
#undef GetObject
#include <io.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#if defined(__linux__)
#include <sys/prctl.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

#include "lldb/API/SBStream.h"
#include "lldb/Host/Config.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/Errno.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/raw_ostream.h"

#include "JSONUtils.h"
#include "LLDBUtils.h"
#include "OutputRedirector.h"

#if defined(_WIN32)
#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif
typedef int socklen_t;
#endif

using namespace lldb_dap;

namespace {
using namespace llvm::opt;

enum ID {
  OPT_INVALID = 0, // This is not an option ID.
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "Options.inc"
#undef OPTION
};

#define PREFIX(NAME, VALUE)                                                    \
  static constexpr llvm::StringLiteral NAME##_init[] = VALUE;                  \
  static constexpr llvm::ArrayRef<llvm::StringLiteral> NAME(                   \
      NAME##_init, std::size(NAME##_init) - 1);
#include "Options.inc"
#undef PREFIX

static constexpr llvm::opt::OptTable::Info InfoTable[] = {
#define OPTION(...) LLVM_CONSTRUCT_OPT_INFO(__VA_ARGS__),
#include "Options.inc"
#undef OPTION
};
class LLDBDAPOptTable : public llvm::opt::GenericOptTable {
public:
  LLDBDAPOptTable() : llvm::opt::GenericOptTable(InfoTable, true) {}
};

typedef void (*RequestCallback)(const llvm::json::Object &command);

enum LaunchMethod { Launch, Attach, AttachForSuspendedLaunch };

/// Prints a welcome message on the editor if the preprocessor variable
/// LLDB_DAP_WELCOME_MESSAGE is defined.
static void PrintWelcomeMessage() {
#ifdef LLDB_DAP_WELCOME_MESSAGE
  g_dap.SendOutput(OutputType::Console, LLDB_DAP_WELCOME_MESSAGE);
#endif
}

lldb::SBValueList *GetTopLevelScope(int64_t variablesReference) {
  switch (variablesReference) {
  case VARREF_LOCALS:
    return &g_dap.variables.locals;
  case VARREF_GLOBALS:
    return &g_dap.variables.globals;
  case VARREF_REGS:
    return &g_dap.variables.registers;
  default:
    return nullptr;
  }
}

SOCKET AcceptConnection(int portno) {
  // Accept a socket connection from any host on "portno".
  SOCKET newsockfd = -1;
  struct sockaddr_in serv_addr, cli_addr;
  SOCKET sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    if (g_dap.log)
      *g_dap.log << "error: opening socket (" << strerror(errno) << ")"
                 << std::endl;
  } else {
    memset((char *)&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    // serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    serv_addr.sin_port = htons(portno);
    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
      if (g_dap.log)
        *g_dap.log << "error: binding socket (" << strerror(errno) << ")"
                   << std::endl;
    } else {
      listen(sockfd, 5);
      socklen_t clilen = sizeof(cli_addr);
      newsockfd =
          llvm::sys::RetryAfterSignal(static_cast<SOCKET>(-1), accept, sockfd,
                                      (struct sockaddr *)&cli_addr, &clilen);
      if (newsockfd < 0)
        if (g_dap.log)
          *g_dap.log << "error: accept (" << strerror(errno) << ")"
                     << std::endl;
    }
#if defined(_WIN32)
    closesocket(sockfd);
#else
    close(sockfd);
#endif
  }
  return newsockfd;
}

std::vector<const char *> MakeArgv(const llvm::ArrayRef<std::string> &strs) {
  // Create and return an array of "const char *", one for each C string in
  // "strs" and terminate the list with a NULL. This can be used for argument
  // vectors (argv) or environment vectors (envp) like those passed to the
  // "main" function in C programs.
  std::vector<const char *> argv;
  for (const auto &s : strs)
    argv.push_back(s.c_str());
  argv.push_back(nullptr);
  return argv;
}

// Send a "exited" event to indicate the process has exited.
void SendProcessExitedEvent(lldb::SBProcess &process) {
  llvm::json::Object event(CreateEventObject("exited"));
  llvm::json::Object body;
  body.try_emplace("exitCode", (int64_t)process.GetExitStatus());
  event.try_emplace("body", std::move(body));
  g_dap.SendJSON(llvm::json::Value(std::move(event)));
}

void SendThreadExitedEvent(lldb::tid_t tid) {
  llvm::json::Object event(CreateEventObject("thread"));
  llvm::json::Object body;
  body.try_emplace("reason", "exited");
  body.try_emplace("threadId", (int64_t)tid);
  event.try_emplace("body", std::move(body));
  g_dap.SendJSON(llvm::json::Value(std::move(event)));
}

// Send a "continued" event to indicate the process is in the running state.
void SendContinuedEvent() {
  lldb::SBProcess process = g_dap.target.GetProcess();
  if (!process.IsValid()) {
    return;
  }

  // If the focus thread is not set then we haven't reported any thread status
  // to the client, so nothing to report.
  if (!g_dap.configuration_done_sent ||
      g_dap.focus_tid == LLDB_INVALID_THREAD_ID) {
    return;
  }

  llvm::json::Object event(CreateEventObject("continued"));
  llvm::json::Object body;
  body.try_emplace("threadId", (int64_t)g_dap.focus_tid);
  body.try_emplace("allThreadsContinued", true);
  event.try_emplace("body", std::move(body));
  g_dap.SendJSON(llvm::json::Value(std::move(event)));
}

// Send a "terminated" event to indicate the process is done being
// debugged.
void SendTerminatedEvent() {
  // Prevent races if the process exits while we're being asked to disconnect.
  llvm::call_once(g_dap.terminated_event_flag, [&] {
    g_dap.RunTerminateCommands();
    // Send a "terminated" event
    llvm::json::Object event(CreateTerminatedEventObject());
    g_dap.SendJSON(llvm::json::Value(std::move(event)));
  });
}

// Send a thread stopped event for all threads as long as the process
// is stopped.
void SendThreadStoppedEvent() {
  lldb::SBProcess process = g_dap.target.GetProcess();
  if (process.IsValid()) {
    auto state = process.GetState();
    if (state == lldb::eStateStopped) {
      llvm::DenseSet<lldb::tid_t> old_thread_ids;
      old_thread_ids.swap(g_dap.thread_ids);
      uint32_t stop_id = process.GetStopID();
      const uint32_t num_threads = process.GetNumThreads();

      // First make a pass through the threads to see if the focused thread
      // has a stop reason. In case the focus thread doesn't have a stop
      // reason, remember the first thread that has a stop reason so we can
      // set it as the focus thread if below if needed.
      lldb::tid_t first_tid_with_reason = LLDB_INVALID_THREAD_ID;
      uint32_t num_threads_with_reason = 0;
      bool focus_thread_exists = false;
      for (uint32_t thread_idx = 0; thread_idx < num_threads; ++thread_idx) {
        lldb::SBThread thread = process.GetThreadAtIndex(thread_idx);
        const lldb::tid_t tid = thread.GetThreadID();
        const bool has_reason = ThreadHasStopReason(thread);
        // If the focus thread doesn't have a stop reason, clear the thread ID
        if (tid == g_dap.focus_tid) {
          focus_thread_exists = true;
          if (!has_reason)
            g_dap.focus_tid = LLDB_INVALID_THREAD_ID;
        }
        if (has_reason) {
          ++num_threads_with_reason;
          if (first_tid_with_reason == LLDB_INVALID_THREAD_ID)
            first_tid_with_reason = tid;
        }
      }

      // We will have cleared g_dap.focus_tid if the focus thread doesn't have
      // a stop reason, so if it was cleared, or wasn't set, or doesn't exist,
      // then set the focus thread to the first thread with a stop reason.
      if (!focus_thread_exists || g_dap.focus_tid == LLDB_INVALID_THREAD_ID)
        g_dap.focus_tid = first_tid_with_reason;

      // If no threads stopped with a reason, then report the first one so
      // we at least let the UI know we stopped.
      if (num_threads_with_reason == 0) {
        lldb::SBThread thread = process.GetThreadAtIndex(0);
        g_dap.focus_tid = thread.GetThreadID();
        g_dap.SendJSON(CreateThreadStopped(thread, stop_id));
      } else {
        for (uint32_t thread_idx = 0; thread_idx < num_threads; ++thread_idx) {
          lldb::SBThread thread = process.GetThreadAtIndex(thread_idx);
          g_dap.thread_ids.insert(thread.GetThreadID());
          if (ThreadHasStopReason(thread)) {
            g_dap.SendJSON(CreateThreadStopped(thread, stop_id));
          }
        }
      }

      for (auto tid : old_thread_ids) {
        auto end = g_dap.thread_ids.end();
        auto pos = g_dap.thread_ids.find(tid);
        if (pos == end)
          SendThreadExitedEvent(tid);
      }
    } else {
      if (g_dap.log)
        *g_dap.log << "error: SendThreadStoppedEvent() when process"
                      " isn't stopped ("
                   << lldb::SBDebugger::StateAsCString(state) << ')'
                   << std::endl;
    }
  } else {
    if (g_dap.log)
      *g_dap.log << "error: SendThreadStoppedEvent() invalid process"
                 << std::endl;
  }
  g_dap.RunStopCommands();
}

// "ProcessEvent": {
//   "allOf": [
//     { "$ref": "#/definitions/Event" },
//     {
//       "type": "object",
//       "description": "Event message for 'process' event type. The event
//                       indicates that the debugger has begun debugging a
//                       new process. Either one that it has launched, or one
//                       that it has attached to.",
//       "properties": {
//         "event": {
//           "type": "string",
//           "enum": [ "process" ]
//         },
//         "body": {
//           "type": "object",
//           "properties": {
//             "name": {
//               "type": "string",
//               "description": "The logical name of the process. This is
//                               usually the full path to process's executable
//                               file. Example: /home/myproj/program.js."
//             },
//             "systemProcessId": {
//               "type": "integer",
//               "description": "The system process id of the debugged process.
//                               This property will be missing for non-system
//                               processes."
//             },
//             "isLocalProcess": {
//               "type": "boolean",
//               "description": "If true, the process is running on the same
//                               computer as the debug adapter."
//             },
//             "startMethod": {
//               "type": "string",
//               "enum": [ "launch", "attach", "attachForSuspendedLaunch" ],
//               "description": "Describes how the debug engine started
//                               debugging this process.",
//               "enumDescriptions": [
//                 "Process was launched under the debugger.",
//                 "Debugger attached to an existing process.",
//                 "A project launcher component has launched a new process in
//                  a suspended state and then asked the debugger to attach."
//               ]
//             }
//           },
//           "required": [ "name" ]
//         }
//       },
//       "required": [ "event", "body" ]
//     }
//   ]
// }
void SendProcessEvent(LaunchMethod launch_method) {
  lldb::SBFileSpec exe_fspec = g_dap.target.GetExecutable();
  char exe_path[PATH_MAX];
  exe_fspec.GetPath(exe_path, sizeof(exe_path));
  llvm::json::Object event(CreateEventObject("process"));
  llvm::json::Object body;
  EmplaceSafeString(body, "name", std::string(exe_path));
  const auto pid = g_dap.target.GetProcess().GetProcessID();
  body.try_emplace("systemProcessId", (int64_t)pid);
  body.try_emplace("isLocalProcess", true);
  const char *startMethod = nullptr;
  switch (launch_method) {
  case Launch:
    startMethod = "launch";
    break;
  case Attach:
    startMethod = "attach";
    break;
  case AttachForSuspendedLaunch:
    startMethod = "attachForSuspendedLaunch";
    break;
  }
  body.try_emplace("startMethod", startMethod);
  event.try_emplace("body", std::move(body));
  g_dap.SendJSON(llvm::json::Value(std::move(event)));
}

// Grab any STDOUT and STDERR from the process and send it up to VS Code
// via an "output" event to the "stdout" and "stderr" categories.
void SendStdOutStdErr(lldb::SBProcess &process) {
  char buffer[1024];
  size_t count;
  while ((count = process.GetSTDOUT(buffer, sizeof(buffer))) > 0)
    g_dap.SendOutput(OutputType::Stdout, llvm::StringRef(buffer, count));
  while ((count = process.GetSTDERR(buffer, sizeof(buffer))) > 0)
    g_dap.SendOutput(OutputType::Stderr, llvm::StringRef(buffer, count));
}

void ProgressEventThreadFunction() {
  lldb::SBListener listener("lldb-dap.progress.listener");
  g_dap.debugger.GetBroadcaster().AddListener(
      listener, lldb::SBDebugger::eBroadcastBitProgress);
  g_dap.broadcaster.AddListener(listener, eBroadcastBitStopProgressThread);
  lldb::SBEvent event;
  bool done = false;
  while (!done) {
    if (listener.WaitForEvent(1, event)) {
      const auto event_mask = event.GetType();
      if (event.BroadcasterMatchesRef(g_dap.broadcaster)) {
        if (event_mask & eBroadcastBitStopProgressThread) {
          done = true;
        }
      } else {
        uint64_t progress_id = 0;
        uint64_t completed = 0;
        uint64_t total = 0;
        bool is_debugger_specific = false;
        const char *message = lldb::SBDebugger::GetProgressFromEvent(
            event, progress_id, completed, total, is_debugger_specific);
        if (message)
          g_dap.SendProgressEvent(progress_id, message, completed, total);
      }
    }
  }
}

// All events from the debugger, target, process, thread and frames are
// received in this function that runs in its own thread. We are using a
// "FILE *" to output packets back to VS Code and they have mutexes in them
// them prevent multiple threads from writing simultaneously so no locking
// is required.
void EventThreadFunction() {
  lldb::SBEvent event;
  lldb::SBListener listener = g_dap.debugger.GetListener();
  bool done = false;
  while (!done) {
    if (listener.WaitForEvent(1, event)) {
      const auto event_mask = event.GetType();
      if (lldb::SBProcess::EventIsProcessEvent(event)) {
        lldb::SBProcess process = lldb::SBProcess::GetProcessFromEvent(event);
        if (event_mask & lldb::SBProcess::eBroadcastBitStateChanged) {
          auto state = lldb::SBProcess::GetStateFromEvent(event);
          switch (state) {
          case lldb::eStateInvalid:
            // Not a state event
            break;
          case lldb::eStateUnloaded:
            break;
          case lldb::eStateConnected:
            break;
          case lldb::eStateAttaching:
            break;
          case lldb::eStateLaunching:
            break;
          case lldb::eStateStepping:
            break;
          case lldb::eStateCrashed:
            break;
          case lldb::eStateDetached:
            break;
          case lldb::eStateSuspended:
            break;
          case lldb::eStateStopped:
            // We launch and attach in synchronous mode then the first stop
            // event will not be delivered. If we use "launchCommands" during a
            // launch or "attachCommands" during an attach we might some process
            // stop events which we do not want to send an event for. We will
            // manually send a stopped event in request_configurationDone(...)
            // so don't send any before then.
            if (g_dap.configuration_done_sent) {
              // Only report a stopped event if the process was not
              // automatically restarted.
              if (!lldb::SBProcess::GetRestartedFromEvent(event)) {
                SendStdOutStdErr(process);
                SendThreadStoppedEvent();
              }
            }
            break;
          case lldb::eStateRunning:
            g_dap.WillContinue();
            SendContinuedEvent();
            break;
          case lldb::eStateExited:
            lldb::SBStream stream;
            process.GetStatus(stream);
            g_dap.SendOutput(OutputType::Console, stream.GetData());

            // When restarting, we can get an "exited" event for the process we
            // just killed with the old PID, or even with no PID. In that case
            // we don't have to terminate the session.
            if (process.GetProcessID() == LLDB_INVALID_PROCESS_ID ||
                process.GetProcessID() == g_dap.restarting_process_id) {
              g_dap.restarting_process_id = LLDB_INVALID_PROCESS_ID;
            } else {
              // Run any exit LLDB commands the user specified in the
              // launch.json
              g_dap.RunExitCommands();
              SendProcessExitedEvent(process);
              SendTerminatedEvent();
              done = true;
            }
            break;
          }
        } else if ((event_mask & lldb::SBProcess::eBroadcastBitSTDOUT) ||
                   (event_mask & lldb::SBProcess::eBroadcastBitSTDERR)) {
          SendStdOutStdErr(process);
        }
      } else if (lldb::SBBreakpoint::EventIsBreakpointEvent(event)) {
        if (event_mask & lldb::SBTarget::eBroadcastBitBreakpointChanged) {
          auto event_type =
              lldb::SBBreakpoint::GetBreakpointEventTypeFromEvent(event);
          auto bp =
              Breakpoint(lldb::SBBreakpoint::GetBreakpointFromEvent(event));
          // If the breakpoint was originated from the IDE, it will have the
          // BreakpointBase::GetBreakpointLabel() label attached. Regardless
          // of wether the locations were added or removed, the breakpoint
          // ins't going away, so we the reason is always "changed".
          if ((event_type & lldb::eBreakpointEventTypeLocationsAdded ||
               event_type & lldb::eBreakpointEventTypeLocationsRemoved) &&
              bp.MatchesName(BreakpointBase::GetBreakpointLabel())) {
            auto bp_event = CreateEventObject("breakpoint");
            llvm::json::Object body;
            // As VSCode already knows the path of this breakpoint, we don't
            // need to send it back as part of a "changed" event. This
            // prevent us from sending to VSCode paths that should be source
            // mapped. Note that CreateBreakpoint doesn't apply source mapping.
            // Besides, the current implementation of VSCode ignores the
            // "source" element of breakpoint events.
            llvm::json::Value source_bp = CreateBreakpoint(&bp);
            source_bp.getAsObject()->erase("source");

            body.try_emplace("breakpoint", source_bp);
            body.try_emplace("reason", "changed");
            bp_event.try_emplace("body", std::move(body));
            g_dap.SendJSON(llvm::json::Value(std::move(bp_event)));
          }
        }
      } else if (event.BroadcasterMatchesRef(g_dap.broadcaster)) {
        if (event_mask & eBroadcastBitStopEventThread) {
          done = true;
        }
      }
    }
  }
}

lldb::SBValue FindVariable(uint64_t variablesReference, llvm::StringRef name) {
  lldb::SBValue variable;
  if (lldb::SBValueList *top_scope = GetTopLevelScope(variablesReference)) {
    bool is_duplicated_variable_name = name.contains(" @");
    // variablesReference is one of our scopes, not an actual variable it is
    // asking for a variable in locals or globals or registers
    int64_t end_idx = top_scope->GetSize();
    // Searching backward so that we choose the variable in closest scope
    // among variables of the same name.
    for (int64_t i = end_idx - 1; i >= 0; --i) {
      lldb::SBValue curr_variable = top_scope->GetValueAtIndex(i);
      std::string variable_name = CreateUniqueVariableNameForDisplay(
          curr_variable, is_duplicated_variable_name);
      if (variable_name == name) {
        variable = curr_variable;
        break;
      }
    }
  } else {
    // This is not under the globals or locals scope, so there are no duplicated
    // names.

    // We have a named item within an actual variable so we need to find it
    // withing the container variable by name.
    lldb::SBValue container = g_dap.variables.GetVariable(variablesReference);
    variable = container.GetChildMemberWithName(name.data());
    if (!variable.IsValid()) {
      if (name.starts_with("[")) {
        llvm::StringRef index_str(name.drop_front(1));
        uint64_t index = 0;
        if (!index_str.consumeInteger(0, index)) {
          if (index_str == "]")
            variable = container.GetChildAtIndex(index);
        }
      }
    }
  }
  return variable;
}

// Both attach and launch take a either a sourcePath or sourceMap
// argument (or neither), from which we need to set the target.source-map.
void SetSourceMapFromArguments(const llvm::json::Object &arguments) {
  const char *sourceMapHelp =
      "source must be be an array of two-element arrays, "
      "each containing a source and replacement path string.\n";

  std::string sourceMapCommand;
  llvm::raw_string_ostream strm(sourceMapCommand);
  strm << "settings set target.source-map ";
  auto sourcePath = GetString(arguments, "sourcePath");

  // sourceMap is the new, more general form of sourcePath and overrides it.
  auto sourceMap = arguments.getArray("sourceMap");
  if (sourceMap) {
    for (const auto &value : *sourceMap) {
      auto mapping = value.getAsArray();
      if (mapping == nullptr || mapping->size() != 2 ||
          (*mapping)[0].kind() != llvm::json::Value::String ||
          (*mapping)[1].kind() != llvm::json::Value::String) {
        g_dap.SendOutput(OutputType::Console, llvm::StringRef(sourceMapHelp));
        return;
      }
      auto mapFrom = GetAsString((*mapping)[0]);
      auto mapTo = GetAsString((*mapping)[1]);
      strm << "\"" << mapFrom << "\" \"" << mapTo << "\" ";
    }
  } else {
    if (ObjectContainsKey(arguments, "sourceMap")) {
      g_dap.SendOutput(OutputType::Console, llvm::StringRef(sourceMapHelp));
      return;
    }
    if (sourcePath.empty())
      return;
    // Do any source remapping needed before we create our targets
    strm << "\".\" \"" << sourcePath << "\"";
  }
  strm.flush();
  if (!sourceMapCommand.empty()) {
    g_dap.RunLLDBCommands("Setting source map:", {sourceMapCommand});
  }
}

// "AttachRequest": {
//   "allOf": [ { "$ref": "#/definitions/Request" }, {
//     "type": "object",
//     "description": "Attach request; value of command field is 'attach'.",
//     "properties": {
//       "command": {
//         "type": "string",
//         "enum": [ "attach" ]
//       },
//       "arguments": {
//         "$ref": "#/definitions/AttachRequestArguments"
//       }
//     },
//     "required": [ "command", "arguments" ]
//   }]
// },
// "AttachRequestArguments": {
//   "type": "object",
//   "description": "Arguments for 'attach' request.\nThe attach request has no
//   standardized attributes."
// },
// "AttachResponse": {
//   "allOf": [ { "$ref": "#/definitions/Response" }, {
//     "type": "object",
//     "description": "Response to 'attach' request. This is just an
//     acknowledgement, so no body field is required."
//   }]
// }
void request_attach(const llvm::json::Object &request) {
  g_dap.is_attach = true;
  g_dap.last_launch_or_attach_request = request;
  llvm::json::Object response;
  lldb::SBError error;
  FillResponse(request, response);
  lldb::SBAttachInfo attach_info;
  const int invalid_port = 0;
  auto arguments = request.getObject("arguments");
  const lldb::pid_t pid =
      GetUnsigned(arguments, "pid", LLDB_INVALID_PROCESS_ID);
  const auto gdb_remote_port =
      GetUnsigned(arguments, "gdb-remote-port", invalid_port);
  const auto gdb_remote_hostname =
      GetString(arguments, "gdb-remote-hostname", "localhost");
  if (pid != LLDB_INVALID_PROCESS_ID)
    attach_info.SetProcessID(pid);
  const auto wait_for = GetBoolean(arguments, "waitFor", false);
  attach_info.SetWaitForLaunch(wait_for, false /*async*/);
  g_dap.init_commands = GetStrings(arguments, "initCommands");
  g_dap.pre_run_commands = GetStrings(arguments, "preRunCommands");
  g_dap.stop_commands = GetStrings(arguments, "stopCommands");
  g_dap.exit_commands = GetStrings(arguments, "exitCommands");
  g_dap.terminate_commands = GetStrings(arguments, "terminateCommands");
  auto attachCommands = GetStrings(arguments, "attachCommands");
  llvm::StringRef core_file = GetString(arguments, "coreFile");
  const uint64_t timeout_seconds = GetUnsigned(arguments, "timeout", 30);
  g_dap.stop_at_entry =
      core_file.empty() ? GetBoolean(arguments, "stopOnEntry", false) : true;
  g_dap.post_run_commands = GetStrings(arguments, "postRunCommands");
  const llvm::StringRef debuggerRoot = GetString(arguments, "debuggerRoot");
  g_dap.enable_auto_variable_summaries =
      GetBoolean(arguments, "enableAutoVariableSummaries", false);
  g_dap.enable_synthetic_child_debugging =
      GetBoolean(arguments, "enableSyntheticChildDebugging", false);
  g_dap.command_escape_prefix =
      GetString(arguments, "commandEscapePrefix", "`");
  g_dap.SetFrameFormat(GetString(arguments, "customFrameFormat"));
  g_dap.SetThreadFormat(GetString(arguments, "customThreadFormat"));

  PrintWelcomeMessage();

  // This is a hack for loading DWARF in .o files on Mac where the .o files
  // in the debug map of the main executable have relative paths which require
  // the lldb-dap binary to have its working directory set to that relative
  // root for the .o files in order to be able to load debug info.
  if (!debuggerRoot.empty())
    llvm::sys::fs::set_current_path(debuggerRoot);

  // Run any initialize LLDB commands the user specified in the launch.json
  if (llvm::Error err = g_dap.RunInitCommands()) {
    response["success"] = false;
    EmplaceSafeString(response, "message", llvm::toString(std::move(err)));
    g_dap.SendJSON(llvm::json::Value(std::move(response)));
    return;
  }

  SetSourceMapFromArguments(*arguments);

  lldb::SBError status;
  g_dap.SetTarget(g_dap.CreateTargetFromArguments(*arguments, status));
  if (status.Fail()) {
    response["success"] = llvm::json::Value(false);
    EmplaceSafeString(response, "message", status.GetCString());
    g_dap.SendJSON(llvm::json::Value(std::move(response)));
    return;
  }

  // Run any pre run LLDB commands the user specified in the launch.json
  if (llvm::Error err = g_dap.RunPreRunCommands()) {
    response["success"] = false;
    EmplaceSafeString(response, "message", llvm::toString(std::move(err)));
    g_dap.SendJSON(llvm::json::Value(std::move(response)));
    return;
  }

  if ((pid == LLDB_INVALID_PROCESS_ID || gdb_remote_port == invalid_port) &&
      wait_for) {
    char attach_msg[256];
    auto attach_msg_len = snprintf(attach_msg, sizeof(attach_msg),
                                   "Waiting to attach to \"%s\"...",
                                   g_dap.target.GetExecutable().GetFilename());
    g_dap.SendOutput(OutputType::Console,
                     llvm::StringRef(attach_msg, attach_msg_len));
  }
  if (attachCommands.empty()) {
    // No "attachCommands", just attach normally.
    // Disable async events so the attach will be successful when we return from
    // the launch call and the launch will happen synchronously
    g_dap.debugger.SetAsync(false);
    if (core_file.empty()) {
      if ((pid != LLDB_INVALID_PROCESS_ID) &&
          (gdb_remote_port != invalid_port)) {
        // If both pid and port numbers are specified.
        error.SetErrorString("The user can't specify both pid and port");
      } else if (gdb_remote_port != invalid_port) {
        // If port is specified and pid is not.
        lldb::SBListener listener = g_dap.debugger.GetListener();

        // If the user hasn't provided the hostname property, default localhost
        // being used.
        std::string connect_url =
            llvm::formatv("connect://{0}:", gdb_remote_hostname);
        connect_url += std::to_string(gdb_remote_port);
        g_dap.target.ConnectRemote(listener, connect_url.c_str(), "gdb-remote",
                                   error);
      } else {
        // Attach by process name or id.
        g_dap.target.Attach(attach_info, error);
      }
    } else
      g_dap.target.LoadCore(core_file.data(), error);
    // Reenable async events
    g_dap.debugger.SetAsync(true);
  } else {
    // We have "attachCommands" that are a set of commands that are expected
    // to execute the commands after which a process should be created. If there
    // is no valid process after running these commands, we have failed.
    if (llvm::Error err = g_dap.RunAttachCommands(attachCommands)) {
      response["success"] = false;
      EmplaceSafeString(response, "message", llvm::toString(std::move(err)));
      g_dap.SendJSON(llvm::json::Value(std::move(response)));
      return;
    }
    // The custom commands might have created a new target so we should use the
    // selected target after these commands are run.
    g_dap.target = g_dap.debugger.GetSelectedTarget();

    // Make sure the process is attached and stopped before proceeding as the
    // the launch commands are not run using the synchronous mode.
    error = g_dap.WaitForProcessToStop(timeout_seconds);
  }

  if (error.Success() && core_file.empty()) {
    auto attached_pid = g_dap.target.GetProcess().GetProcessID();
    if (attached_pid == LLDB_INVALID_PROCESS_ID) {
      if (attachCommands.empty())
        error.SetErrorString("failed to attach to a process");
      else
        error.SetErrorString("attachCommands failed to attach to a process");
    }
  }

  if (error.Fail()) {
    response["success"] = llvm::json::Value(false);
    EmplaceSafeString(response, "message", std::string(error.GetCString()));
  } else {
    g_dap.RunPostRunCommands();
  }

  g_dap.SendJSON(llvm::json::Value(std::move(response)));
  if (error.Success()) {
    SendProcessEvent(Attach);
    g_dap.SendJSON(CreateEventObject("initialized"));
  }
}

// "ContinueRequest": {
//   "allOf": [ { "$ref": "#/definitions/Request" }, {
//     "type": "object",
//     "description": "Continue request; value of command field is 'continue'.
//                     The request starts the debuggee to run again.",
//     "properties": {
//       "command": {
//         "type": "string",
//         "enum": [ "continue" ]
//       },
//       "arguments": {
//         "$ref": "#/definitions/ContinueArguments"
//       }
//     },
//     "required": [ "command", "arguments"  ]
//   }]
// },
// "ContinueArguments": {
//   "type": "object",
//   "description": "Arguments for 'continue' request.",
//   "properties": {
//     "threadId": {
//       "type": "integer",
//       "description": "Continue execution for the specified thread (if
//                       possible). If the backend cannot continue on a single
//                       thread but will continue on all threads, it should
//                       set the allThreadsContinued attribute in the response
//                       to true."
//     }
//   },
//   "required": [ "threadId" ]
// },
// "ContinueResponse": {
//   "allOf": [ { "$ref": "#/definitions/Response" }, {
//     "type": "object",
//     "description": "Response to 'continue' request.",
//     "properties": {
//       "body": {
//         "type": "object",
//         "properties": {
//           "allThreadsContinued": {
//             "type": "boolean",
//             "description": "If true, the continue request has ignored the
//                             specified thread and continued all threads
//                             instead. If this attribute is missing a value
//                             of 'true' is assumed for backward
//                             compatibility."
//           }
//         }
//       }
//     },
//     "required": [ "body" ]
//   }]
// }
void request_continue(const llvm::json::Object &request) {
  llvm::json::Object response;
  FillResponse(request, response);
  lldb::SBProcess process = g_dap.target.GetProcess();
  lldb::SBError error = process.Continue();
  llvm::json::Object body;
  body.try_emplace("allThreadsContinued", true);
  response.try_emplace("body", std::move(body));
  g_dap.SendJSON(llvm::json::Value(std::move(response)));
}

// "ConfigurationDoneRequest": {
//   "allOf": [ { "$ref": "#/definitions/Request" }, {
//             "type": "object",
//             "description": "ConfigurationDone request; value of command field
//             is 'configurationDone'.\nThe client of the debug protocol must
//             send this request at the end of the sequence of configuration
//             requests (which was started by the InitializedEvent).",
//             "properties": {
//             "command": {
//             "type": "string",
//             "enum": [ "configurationDone" ]
//             },
//             "arguments": {
//             "$ref": "#/definitions/ConfigurationDoneArguments"
//             }
//             },
//             "required": [ "command" ]
//             }]
// },
// "ConfigurationDoneArguments": {
//   "type": "object",
//   "description": "Arguments for 'configurationDone' request.\nThe
//   configurationDone request has no standardized attributes."
// },
// "ConfigurationDoneResponse": {
//   "allOf": [ { "$ref": "#/definitions/Response" }, {
//             "type": "object",
//             "description": "Response to 'configurationDone' request. This is
//             just an acknowledgement, so no body field is required."
//             }]
// },
void request_configurationDone(const llvm::json::Object &request) {
  llvm::json::Object response;
  FillResponse(request, response);
  g_dap.SendJSON(llvm::json::Value(std::move(response)));
  g_dap.configuration_done_sent = true;
  if (g_dap.stop_at_entry)
    SendThreadStoppedEvent();
  else
    g_dap.target.GetProcess().Continue();
}

// "DisconnectRequest": {
//   "allOf": [ { "$ref": "#/definitions/Request" }, {
//     "type": "object",
//     "description": "Disconnect request; value of command field is
//                     'disconnect'.",
//     "properties": {
//       "command": {
//         "type": "string",
//         "enum": [ "disconnect" ]
//       },
//       "arguments": {
//         "$ref": "#/definitions/DisconnectArguments"
//       }
//     },
//     "required": [ "command" ]
//   }]
// },
// "DisconnectArguments": {
//   "type": "object",
//   "description": "Arguments for 'disconnect' request.",
//   "properties": {
//     "terminateDebuggee": {
//       "type": "boolean",
//       "description": "Indicates whether the debuggee should be terminated
//                       when the debugger is disconnected. If unspecified,
//                       the debug adapter is free to do whatever it thinks
//                       is best. A client can only rely on this attribute
//                       being properly honored if a debug adapter returns
//                       true for the 'supportTerminateDebuggee' capability."
//     },
//     "restart": {
//       "type": "boolean",
//       "description": "Indicates whether the debuggee should be restart
//                       the process."
//     }
//   }
// },
// "DisconnectResponse": {
//   "allOf": [ { "$ref": "#/definitions/Response" }, {
//     "type": "object",
//     "description": "Response to 'disconnect' request. This is just an
//                     acknowledgement, so no body field is required."
//   }]
// }
void request_disconnect(const llvm::json::Object &request) {
  llvm::json::Object response;
  FillResponse(request, response);
  auto arguments = request.getObject("arguments");

  bool defaultTerminateDebuggee = g_dap.is_attach ? false : true;
  bool terminateDebuggee =
      GetBoolean(arguments, "terminateDebuggee", defaultTerminateDebuggee);
  lldb::SBProcess process = g_dap.target.GetProcess();
  auto state = process.GetState();
  switch (state) {
  case lldb::eStateInvalid:
  case lldb::eStateUnloaded:
  case lldb::eStateDetached:
  case lldb::eStateExited:
    break;
  case lldb::eStateConnected:
  case lldb::eStateAttaching:
  case lldb::eStateLaunching:
  case lldb::eStateStepping:
  case lldb::eStateCrashed:
  case lldb::eStateSuspended:
  case lldb::eStateStopped:
  case lldb::eStateRunning:
    g_dap.debugger.SetAsync(false);
    lldb::SBError error = terminateDebuggee ? process.Kill() : process.Detach();
    if (!error.Success())
      EmplaceSafeString(response, "error", error.GetCString());
    g_dap.debugger.SetAsync(true);
    break;
  }
  SendTerminatedEvent();
  g_dap.SendJSON(llvm::json::Value(std::move(response)));
  if (g_dap.event_thread.joinable()) {
    g_dap.broadcaster.BroadcastEventByType(eBroadcastBitStopEventThread);
    g_dap.event_thread.join();
  }
  if (g_dap.progress_event_thread.joinable()) {
    g_dap.broadcaster.BroadcastEventByType(eBroadcastBitStopProgressThread);
    g_dap.progress_event_thread.join();
  }
  g_dap.disconnecting = true;
}

void request_exceptionInfo(const llvm::json::Object &request) {
  llvm::json::Object response;
  FillResponse(request, response);
  auto arguments = request.getObject("arguments");
  llvm::json::Object body;
  lldb::SBThread thread = g_dap.GetLLDBThread(*arguments);
  if (thread.IsValid()) {
    auto stopReason = thread.GetStopReason();
    if (stopReason == lldb::eStopReasonSignal)
      body.try_emplace("exceptionId", "signal");
    else if (stopReason == lldb::eStopReasonBreakpoint) {
      ExceptionBreakpoint *exc_bp = g_dap.GetExceptionBPFromStopReason(thread);
      if (exc_bp) {
        EmplaceSafeString(body, "exceptionId", exc_bp->filter);
        EmplaceSafeString(body, "description", exc_bp->label);
      } else {
        body.try_emplace("exceptionId", "exception");
      }
    } else {
      body.try_emplace("exceptionId", "exception");
    }
    if (!ObjectContainsKey(body, "description")) {
      char description[1024];
      if (thread.GetStopDescription(description, sizeof(description))) {
        EmplaceSafeString(body, "description", std::string(description));
      }
    }
    body.try_emplace("breakMode", "always");
    // auto excInfoCount = thread.GetStopReasonDataCount();
    // for (auto i=0; i<excInfoCount; ++i) {
    //   uint64_t exc_data = thread.GetStopReasonDataAtIndex(i);
    // }
  } else {
    response["success"] = llvm::json::Value(false);
  }
  response.try_emplace("body", std::move(body));
  g_dap.SendJSON(llvm::json::Value(std::move(response)));
}

// "CompletionsRequest": {
//   "allOf": [ { "$ref": "#/definitions/Request" }, {
//     "type": "object",
//     "description": "Returns a list of possible completions for a given caret
//     position and text.\nThe CompletionsRequest may only be called if the
//     'supportsCompletionsRequest' capability exists and is true.",
//     "properties": {
//       "command": {
//         "type": "string",
//         "enum": [ "completions" ]
//       },
//       "arguments": {
//         "$ref": "#/definitions/CompletionsArguments"
//       }
//     },
//     "required": [ "command", "arguments"  ]
//   }]
// },
// "CompletionsArguments": {
//   "type": "object",
//   "description": "Arguments for 'completions' request.",
//   "properties": {
//     "frameId": {
//       "type": "integer",
//       "description": "Returns completions in the scope of this stack frame.
//       If not specified, the completions are returned for the global scope."
//     },
//     "text": {
//       "type": "string",
//       "description": "One or more source lines. Typically this is the text a
//       user has typed into the debug console before he asked for completion."
//     },
//     "column": {
//       "type": "integer",
//       "description": "The character position for which to determine the
//       completion proposals."
//     },
//     "line": {
//       "type": "integer",
//       "description": "An optional line for which to determine the completion
//       proposals. If missing the first line of the text is assumed."
//     }
//   },
//   "required": [ "text", "column" ]
// },
// "CompletionsResponse": {
//   "allOf": [ { "$ref": "#/definitions/Response" }, {
//     "type": "object",
//     "description": "Response to 'completions' request.",
//     "properties": {
//       "body": {
//         "type": "object",
//         "properties": {
//           "targets": {
//             "type": "array",
//             "items": {
//               "$ref": "#/definitions/CompletionItem"
//             },
//             "description": "The possible completions for ."
//           }
//         },
//         "required": [ "targets" ]
//       }
//     },
//     "required": [ "body" ]
//   }]
// },
// "CompletionItem": {
//   "type": "object",
//   "description": "CompletionItems are the suggestions returned from the
//   CompletionsRequest.", "properties": {
//     "label": {
//       "type": "string",
//       "description": "The label of this completion item. By default this is
//       also the text that is inserted when selecting this completion."
//     },
//     "text": {
//       "type": "string",
//       "description": "If text is not falsy then it is inserted instead of the
//       label."
//     },
//     "sortText": {
//       "type": "string",
//       "description": "A string that should be used when comparing this item
//       with other items. When `falsy` the label is used."
//     },
//     "type": {
//       "$ref": "#/definitions/CompletionItemType",
//       "description": "The item's type. Typically the client uses this
//       information to render the item in the UI with an icon."
//     },
//     "start": {
//       "type": "integer",
//       "description": "This value determines the location (in the
//       CompletionsRequest's 'text' attribute) where the completion text is
//       added.\nIf missing the text is added at the location specified by the
//       CompletionsRequest's 'column' attribute."
//     },
//     "length": {
//       "type": "integer",
//       "description": "This value determines how many characters are
//       overwritten by the completion text.\nIf missing the value 0 is assumed
//       which results in the completion text being inserted."
//     }
//   },
//   "required": [ "label" ]
// },
// "CompletionItemType": {
//   "type": "string",
//   "description": "Some predefined types for the CompletionItem. Please note
//   that not all clients have specific icons for all of them.", "enum": [
//   "method", "function", "constructor", "field", "variable", "class",
//   "interface", "module", "property", "unit", "value", "enum", "keyword",
//   "snippet", "text", "color", "file", "reference", "customcolor" ]
// }
void request_completions(const llvm::json::Object &request) {
  llvm::json::Object response;
  FillResponse(request, response);
  llvm::json::Object body;
  auto arguments = request.getObject("arguments");

  // If we have a frame, try to set the context for variable completions.
  lldb::SBFrame frame = g_dap.GetLLDBFrame(*arguments);
  if (frame.IsValid()) {
    frame.GetThread().GetProcess().SetSelectedThread(frame.GetThread());
    frame.GetThread().SetSelectedFrame(frame.GetFrameID());
  }

  std::string text = GetString(arguments, "text").str();
  auto original_column = GetSigned(arguments, "column", text.size());
  auto original_line = GetSigned(arguments, "line", 1);
  auto offset = original_column - 1;
  if (original_line > 1) {
    llvm::SmallVector<::llvm::StringRef, 2> lines;
    llvm::StringRef(text).split(lines, '\n');
    for (int i = 0; i < original_line - 1; i++) {
      offset += lines[i].size();
    }
  }
  llvm::json::Array targets;

  if (!text.empty() &&
      llvm::StringRef(text).starts_with(g_dap.command_escape_prefix)) {
    text = text.substr(g_dap.command_escape_prefix.size());
  }

  // While the user is typing then we likely have an incomplete input and cannot
  // reliably determine the precise intent (command vs variable), try completing
  // the text as both a command and variable expression, if applicable.
  const std::string expr_prefix = "expression -- ";
  std::array<std::tuple<ReplMode, std::string, uint64_t>, 2> exprs = {
      {std::make_tuple(ReplMode::Command, text, offset),
       std::make_tuple(ReplMode::Variable, expr_prefix + text,
                       offset + expr_prefix.size())}};
  for (const auto &[mode, line, cursor] : exprs) {
    if (g_dap.repl_mode != ReplMode::Auto && g_dap.repl_mode != mode)
      continue;

    lldb::SBStringList matches;
    lldb::SBStringList descriptions;
    if (!g_dap.debugger.GetCommandInterpreter()
             .HandleCompletionWithDescriptions(line.c_str(), cursor, 0, 100,
                                               matches, descriptions))
      continue;

    // The first element is the common substring after the cursor position for
    // all the matches. The rest of the elements are the matches so ignore the
    // first result.
    for (size_t i = 1; i < matches.GetSize(); i++) {
      std::string match = matches.GetStringAtIndex(i);
      std::string description = descriptions.GetStringAtIndex(i);

      llvm::json::Object item;
      llvm::StringRef match_ref = match;
      for (llvm::StringRef commit_point : {".", "->"}) {
        if (match_ref.contains(commit_point)) {
          match_ref = match_ref.rsplit(commit_point).second;
        }
      }
      EmplaceSafeString(item, "text", match_ref);

      if (description.empty())
        EmplaceSafeString(item, "label", match);
      else
        EmplaceSafeString(item, "label", match + " -- " + description);

      targets.emplace_back(std::move(item));
    }
  }

  body.try_emplace("targets", std::move(targets));
  response.try_emplace("body", std::move(body));
  g_dap.SendJSON(llvm::json::Value(std::move(response)));
}

//  "EvaluateRequest": {
//    "allOf": [ { "$ref": "#/definitions/Request" }, {
//      "type": "object",
//      "description": "Evaluate request; value of command field is 'evaluate'.
//                      Evaluates the given expression in the context of the
//                      top most stack frame. The expression has access to any
//                      variables and arguments that are in scope.",
//      "properties": {
//        "command": {
//          "type": "string",
//          "enum": [ "evaluate" ]
//        },
//        "arguments": {
//          "$ref": "#/definitions/EvaluateArguments"
//        }
//      },
//      "required": [ "command", "arguments"  ]
//    }]
//  },
//  "EvaluateArguments": {
//    "type": "object",
//    "description": "Arguments for 'evaluate' request.",
//    "properties": {
//      "expression": {
//        "type": "string",
//        "description": "The expression to evaluate."
//      },
//      "frameId": {
//        "type": "integer",
//        "description": "Evaluate the expression in the scope of this stack
//                        frame. If not specified, the expression is evaluated
//                        in the global scope."
//      },
//      "context": {
//        "type": "string",
//        "_enum": [ "watch", "repl", "hover" ],
//        "enumDescriptions": [
//          "evaluate is run in a watch.",
//          "evaluate is run from REPL console.",
//          "evaluate is run from a data hover."
//        ],
//        "description": "The context in which the evaluate request is run."
//      },
//      "format": {
//        "$ref": "#/definitions/ValueFormat",
//        "description": "Specifies details on how to format the Evaluate
//                        result."
//      }
//    },
//    "required": [ "expression" ]
//  },
//  "EvaluateResponse": {
//    "allOf": [ { "$ref": "#/definitions/Response" }, {
//      "type": "object",
//      "description": "Response to 'evaluate' request.",
//      "properties": {
//        "body": {
//          "type": "object",
//          "properties": {
//            "result": {
//              "type": "string",
//              "description": "The result of the evaluate request."
//            },
//            "type": {
//              "type": "string",
//              "description": "The optional type of the evaluate result."
//            },
//            "presentationHint": {
//              "$ref": "#/definitions/VariablePresentationHint",
//              "description": "Properties of a evaluate result that can be
//                              used to determine how to render the result in
//                              the UI."
//            },
//            "variablesReference": {
//              "type": "number",
//              "description": "If variablesReference is > 0, the evaluate
//                              result is structured and its children can be
//                              retrieved by passing variablesReference to the
//                              VariablesRequest."
//            },
//            "namedVariables": {
//              "type": "number",
//              "description": "The number of named child variables. The
//                              client can use this optional information to
//                              present the variables in a paged UI and fetch
//                              them in chunks."
//            },
//            "indexedVariables": {
//              "type": "number",
//              "description": "The number of indexed child variables. The
//                              client can use this optional information to
//                              present the variables in a paged UI and fetch
//                              them in chunks."
//            }
//          },
//          "required": [ "result", "variablesReference" ]
//        }
//      },
//      "required": [ "body" ]
//    }]
//  }
void request_evaluate(const llvm::json::Object &request) {
  llvm::json::Object response;
  FillResponse(request, response);
  llvm::json::Object body;
  auto arguments = request.getObject("arguments");
  lldb::SBFrame frame = g_dap.GetLLDBFrame(*arguments);
  std::string expression = GetString(arguments, "expression").str();
  llvm::StringRef context = GetString(arguments, "context");

  if (context == "repl" && g_dap.DetectExpressionContext(frame, expression) ==
                               ExpressionContext::Command) {
    // If we're evaluating a command relative to the current frame, set the
    // focus_tid to the current frame for any thread related events.
    if (frame.IsValid()) {
      g_dap.focus_tid = frame.GetThread().GetThreadID();
    }
    auto result =
        RunLLDBCommandsVerbatim(llvm::StringRef(), {std::string(expression)});
    EmplaceSafeString(body, "result", result);
    body.try_emplace("variablesReference", (int64_t)0);
  } else {
    // Always try to get the answer from the local variables if possible. If
    // this fails, then if the context is not "hover", actually evaluate an
    // expression using the expression parser.
    //
    // "frame variable" is more reliable than the expression parser in
    // many cases and it is faster.
    lldb::SBValue value = frame.GetValueForVariablePath(
        expression.data(), lldb::eDynamicDontRunTarget);

    // Freeze dry the value in case users expand it later in the debug console
    if (value.GetError().Success() && context == "repl")
      value = value.Persist();

    if (value.GetError().Fail() && context != "hover")
      value = frame.EvaluateExpression(expression.data());

    if (value.GetError().Fail()) {
      response["success"] = llvm::json::Value(false);
      // This error object must live until we're done with the pointer returned
      // by GetCString().
      lldb::SBError error = value.GetError();
      const char *error_cstr = error.GetCString();
      if (error_cstr && error_cstr[0])
        EmplaceSafeString(response, "message", std::string(error_cstr));
      else
        EmplaceSafeString(response, "message", "evaluate failed");
    } else {
      VariableDescription desc(value);
      EmplaceSafeString(body, "result", desc.GetResult(context));
      EmplaceSafeString(body, "type", desc.display_type_name);
      if (value.MightHaveChildren()) {
        auto variableReference = g_dap.variables.InsertExpandableVariable(
            value, /*is_permanent=*/context == "repl");
        body.try_emplace("variablesReference", variableReference);
      } else {
        body.try_emplace("variablesReference", (int64_t)0);
      }
    }
  }
  response.try_emplace("body", std::move(body));
  g_dap.SendJSON(llvm::json::Value(std::move(response)));
}

// "compileUnitsRequest": {
//   "allOf": [ { "$ref": "#/definitions/Request" }, {
//     "type": "object",
//     "description": "Compile Unit request; value of command field is
//                     'compileUnits'.",
//     "properties": {
//       "command": {
//         "type": "string",
//         "enum": [ "compileUnits" ]
//       },
//       "arguments": {
//         "$ref": "#/definitions/compileUnitRequestArguments"
//       }
//     },
//     "required": [ "command", "arguments" ]
//   }]
// },
// "compileUnitsRequestArguments": {
//   "type": "object",
//   "description": "Arguments for 'compileUnits' request.",
//   "properties": {
//     "moduleId": {
//       "type": "string",
//       "description": "The ID of the module."
//     }
//   },
//   "required": [ "moduleId" ]
// },
// "compileUnitsResponse": {
//   "allOf": [ { "$ref": "#/definitions/Response" }, {
//     "type": "object",
//     "description": "Response to 'compileUnits' request.",
//     "properties": {
//       "body": {
//         "description": "Response to 'compileUnits' request. Array of
//                         paths of compile units."
//       }
//     }
//   }]
// }
void request_compileUnits(const llvm::json::Object &request) {
  llvm::json::Object response;
  FillResponse(request, response);
  llvm::json::Object body;
  llvm::json::Array units;
  auto arguments = request.getObject("arguments");
  std::string module_id = std::string(GetString(arguments, "moduleId"));
  int num_modules = g_dap.target.GetNumModules();
  for (int i = 0; i < num_modules; i++) {
    auto curr_module = g_dap.target.GetModuleAtIndex(i);
    if (module_id == curr_module.GetUUIDString()) {
      int num_units = curr_module.GetNumCompileUnits();
      for (int j = 0; j < num_units; j++) {
        auto curr_unit = curr_module.GetCompileUnitAtIndex(j);
        units.emplace_back(CreateCompileUnit(curr_unit));
      }
      body.try_emplace("compileUnits", std::move(units));
      break;
    }
  }
  response.try_emplace("body", std::move(body));
  g_dap.SendJSON(llvm::json::Value(std::move(response)));
}

// "modulesRequest": {
//   "allOf": [ { "$ref": "#/definitions/Request" }, {
//     "type": "object",
//     "description": "Modules request; value of command field is
//                     'modules'.",
//     "properties": {
//       "command": {
//         "type": "string",
//         "enum": [ "modules" ]
//       },
//     },
//     "required": [ "command" ]
//   }]
// },
// "modulesResponse": {
//   "allOf": [ { "$ref": "#/definitions/Response" }, {
//     "type": "object",
//     "description": "Response to 'modules' request.",
//     "properties": {
//       "body": {
//         "description": "Response to 'modules' request. Array of
//                         module objects."
//       }
//     }
//   }]
// }
void request_modules(const llvm::json::Object &request) {
  llvm::json::Object response;
  FillResponse(request, response);

  llvm::json::Array modules;
  for (size_t i = 0; i < g_dap.target.GetNumModules(); i++) {
    lldb::SBModule module = g_dap.target.GetModuleAtIndex(i);
    modules.emplace_back(CreateModule(module));
  }

  llvm::json::Object body;
  body.try_emplace("modules", std::move(modules));
  response.try_emplace("body", std::move(body));
  g_dap.SendJSON(llvm::json::Value(std::move(response)));
}

// "InitializeRequest": {
//   "allOf": [ { "$ref": "#/definitions/Request" }, {
//     "type": "object",
//     "description": "Initialize request; value of command field is
//                     'initialize'.",
//     "properties": {
//       "command": {
//         "type": "string",
//         "enum": [ "initialize" ]
//       },
//       "arguments": {
//         "$ref": "#/definitions/InitializeRequestArguments"
//       }
//     },
//     "required": [ "command", "arguments" ]
//   }]
// },
// "InitializeRequestArguments": {
//   "type": "object",
//   "description": "Arguments for 'initialize' request.",
//   "properties": {
//     "clientID": {
//       "type": "string",
//       "description": "The ID of the (frontend) client using this adapter."
//     },
//     "adapterID": {
//       "type": "string",
//       "description": "The ID of the debug adapter."
//     },
//     "locale": {
//       "type": "string",
//       "description": "The ISO-639 locale of the (frontend) client using
//                       this adapter, e.g. en-US or de-CH."
//     },
//     "linesStartAt1": {
//       "type": "boolean",
//       "description": "If true all line numbers are 1-based (default)."
//     },
//     "columnsStartAt1": {
//       "type": "boolean",
//       "description": "If true all column numbers are 1-based (default)."
//     },
//     "pathFormat": {
//       "type": "string",
//       "_enum": [ "path", "uri" ],
//       "description": "Determines in what format paths are specified. The
//                       default is 'path', which is the native format."
//     },
//     "supportsVariableType": {
//       "type": "boolean",
//       "description": "Client supports the optional type attribute for
//                       variables."
//     },
//     "supportsVariablePaging": {
//       "type": "boolean",
//       "description": "Client supports the paging of variables."
//     },
//     "supportsRunInTerminalRequest": {
//       "type": "boolean",
//       "description": "Client supports the runInTerminal request."
//     }
//   },
//   "required": [ "adapterID" ]
// },
// "InitializeResponse": {
//   "allOf": [ { "$ref": "#/definitions/Response" }, {
//     "type": "object",
//     "description": "Response to 'initialize' request.",
//     "properties": {
//       "body": {
//         "$ref": "#/definitions/Capabilities",
//         "description": "The capabilities of this debug adapter."
//       }
//     }
//   }]
// }
void request_initialize(const llvm::json::Object &request) {
  llvm::json::Object response;
  FillResponse(request, response);
  llvm::json::Object body;

  auto log_cb = [](const char *buf, void *baton) -> void {
    g_dap.SendOutput(OutputType::Console, llvm::StringRef{buf});
  };

  auto arguments = request.getObject("arguments");
  // sourceInitFile option is not from formal DAP specification. It is only
  // used by unit tests to prevent sourcing .lldbinit files from environment
  // which may affect the outcome of tests.
  bool source_init_file = GetBoolean(arguments, "sourceInitFile", true);

  g_dap.debugger = lldb::SBDebugger::Create(source_init_file, log_cb, nullptr);
  if (llvm::Error err = g_dap.RunPreInitCommands()) {
    response["success"] = false;
    EmplaceSafeString(response, "message", llvm::toString(std::move(err)));
    g_dap.SendJSON(llvm::json::Value(std::move(response)));
    return;
  }

  g_dap.PopulateExceptionBreakpoints();
  auto cmd = g_dap.debugger.GetCommandInterpreter().AddMultiwordCommand(
      "lldb-dap", "Commands for managing lldb-dap.");
  if (GetBoolean(arguments, "supportsStartDebuggingRequest", false)) {
    cmd.AddCommand(
        "startDebugging", &g_dap.start_debugging_request_handler,
        "Sends a startDebugging request from the debug adapter to the client "
        "to start a child debug session of the same type as the caller.");
  }
  cmd.AddCommand(
      "repl-mode", &g_dap.repl_mode_request_handler,
      "Get or set the repl behavior of lldb-dap evaluation requests.");

  g_dap.progress_event_thread = std::thread(ProgressEventThreadFunction);

  // Start our event thread so we can receive events from the debugger, target,
  // process and more.
  g_dap.event_thread = std::thread(EventThreadFunction);

  // The debug adapter supports the configurationDoneRequest.
  body.try_emplace("supportsConfigurationDoneRequest", true);
  // The debug adapter supports function breakpoints.
  body.try_emplace("supportsFunctionBreakpoints", true);
  // The debug adapter supports conditional breakpoints.
  body.try_emplace("supportsConditionalBreakpoints", true);
  // The debug adapter supports breakpoints that break execution after a
  // specified number of hits.
  body.try_emplace("supportsHitConditionalBreakpoints", true);
  // The debug adapter supports a (side effect free) evaluate request for
  // data hovers.
  body.try_emplace("supportsEvaluateForHovers", true);
  // Available filters or options for the setExceptionBreakpoints request.
  llvm::json::Array filters;
  for (const auto &exc_bp : *g_dap.exception_breakpoints) {
    filters.emplace_back(CreateExceptionBreakpointFilter(exc_bp));
  }
  body.try_emplace("exceptionBreakpointFilters", std::move(filters));
  // The debug adapter supports launching a debugee in intergrated VSCode
  // terminal.
  body.try_emplace("supportsRunInTerminalRequest", true);
  // The debug adapter supports stepping back via the stepBack and
  // reverseContinue requests.
  body.try_emplace("supportsStepBack", false);
  // The debug adapter supports setting a variable to a value.
  body.try_emplace("supportsSetVariable", true);
  // The debug adapter supports restarting a frame.
  body.try_emplace("supportsRestartFrame", false);
  // The debug adapter supports the gotoTargetsRequest.
  body.try_emplace("supportsGotoTargetsRequest", false);
  // The debug adapter supports the stepInTargetsRequest.
  body.try_emplace("supportsStepInTargetsRequest", true);
  // The debug adapter supports the completions request.
  body.try_emplace("supportsCompletionsRequest", true);
  // The debug adapter supports the disassembly request.
  body.try_emplace("supportsDisassembleRequest", true);

  llvm::json::Array completion_characters;
  completion_characters.emplace_back(".");
  completion_characters.emplace_back(" ");
  completion_characters.emplace_back("\t");
  body.try_emplace("completionTriggerCharacters",
                   std::move(completion_characters));

  // The debug adapter supports the modules request.
  body.try_emplace("supportsModulesRequest", true);
  // The set of additional module information exposed by the debug adapter.
  //   body.try_emplace("additionalModuleColumns"] = ColumnDescriptor
  // Checksum algorithms supported by the debug adapter.
  //   body.try_emplace("supportedChecksumAlgorithms"] = ChecksumAlgorithm
  // The debug adapter supports the RestartRequest. In this case a client
  // should not implement 'restart' by terminating and relaunching the adapter
  // but by calling the RestartRequest.
  body.try_emplace("supportsRestartRequest", true);
  // The debug adapter supports 'exceptionOptions' on the
  // setExceptionBreakpoints request.
  body.try_emplace("supportsExceptionOptions", true);
  // The debug adapter supports a 'format' attribute on the stackTraceRequest,
  // variablesRequest, and evaluateRequest.
  body.try_emplace("supportsValueFormattingOptions", true);
  // The debug adapter supports the exceptionInfo request.
  body.try_emplace("supportsExceptionInfoRequest", true);
  // The debug adapter supports the 'terminateDebuggee' attribute on the
  // 'disconnect' request.
  body.try_emplace("supportTerminateDebuggee", true);
  // The debug adapter supports the delayed loading of parts of the stack,
  // which requires that both the 'startFrame' and 'levels' arguments and the
  // 'totalFrames' result of the 'StackTrace' request are supported.
  body.try_emplace("supportsDelayedStackTraceLoading", true);
  // The debug adapter supports the 'loadedSources' request.
  body.try_emplace("supportsLoadedSourcesRequest", false);
  // The debug adapter supports sending progress reporting events.
  body.try_emplace("supportsProgressReporting", true);
  // The debug adapter supports 'logMessage' in breakpoint.
  body.try_emplace("supportsLogPoints", true);
  // The debug adapter supports data watchpoints.
  body.try_emplace("supportsDataBreakpoints", true);

  // Put in non-DAP specification lldb specific information.
  llvm::json::Object lldb_json;
  lldb_json.try_emplace("version", g_dap.debugger.GetVersionString());
  body.try_emplace("__lldb", std::move(lldb_json));

  response.try_emplace("body", std::move(body));
  g_dap.SendJSON(llvm::json::Value(std::move(response)));
}

llvm::Error request_runInTerminal(const llvm::json::Object &launch_request,
                                  const uint64_t timeout_seconds) {
  g_dap.is_attach = true;
  lldb::SBAttachInfo attach_info;

  llvm::Expected<std::shared_ptr<FifoFile>> comm_file_or_err =
      CreateRunInTerminalCommFile();
  if (!comm_file_or_err)
    return comm_file_or_err.takeError();
  FifoFile &comm_file = *comm_file_or_err.get();

  RunInTerminalDebugAdapterCommChannel comm_channel(comm_file.m_path);

  lldb::pid_t debugger_pid = LLDB_INVALID_PROCESS_ID;
#if !defined(_WIN32)
  debugger_pid = getpid();
#endif
  llvm::json::Object reverse_request = CreateRunInTerminalReverseRequest(
      launch_request, g_dap.debug_adaptor_path, comm_file.m_path, debugger_pid);
  g_dap.SendReverseRequest("runInTerminal", std::move(reverse_request),
                           [](llvm::Expected<llvm::json::Value> value) {
                             if (!value) {
                               llvm::Error err = value.takeError();
                               llvm::errs()
                                   << "runInTerminal request failed: "
                                   << llvm::toString(std::move(err)) << "\n";
                             }
                           });

  if (llvm::Expected<lldb::pid_t> pid = comm_channel.GetLauncherPid())
    attach_info.SetProcessID(*pid);
  else
    return pid.takeError();

  g_dap.debugger.SetAsync(false);
  lldb::SBError error;
  g_dap.target.Attach(attach_info, error);

  if (error.Fail())
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "Failed to attach to the target process. %s",
                                   comm_channel.GetLauncherError().c_str());
  // This will notify the runInTerminal launcher that we attached.
  // We have to make this async, as the function won't return until the launcher
  // resumes and reads the data.
  std::future<lldb::SBError> did_attach_message_success =
      comm_channel.NotifyDidAttach();

  // We just attached to the runInTerminal launcher, which was waiting to be
  // attached. We now resume it, so it can receive the didAttach notification
  // and then perform the exec. Upon continuing, the debugger will stop the
  // process right in the middle of the exec. To the user, what we are doing is
  // transparent, as they will only be able to see the process since the exec,
  // completely unaware of the preparatory work.
  g_dap.target.GetProcess().Continue();

  // Now that the actual target is just starting (i.e. exec was just invoked),
  // we return the debugger to its async state.
  g_dap.debugger.SetAsync(true);

  // If sending the notification failed, the launcher should be dead by now and
  // the async didAttach notification should have an error message, so we
  // return it. Otherwise, everything was a success.
  did_attach_message_success.wait();
  error = did_attach_message_success.get();
  if (error.Success())
    return llvm::Error::success();
  return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                 error.GetCString());
}

// Takes a LaunchRequest object and launches the process, also handling
// runInTerminal if applicable. It doesn't do any of the additional
// initialization and bookkeeping stuff that is needed for `request_launch`.
// This way we can reuse the process launching logic for RestartRequest too.
lldb::SBError LaunchProcess(const llvm::json::Object &request) {
  lldb::SBError error;
  auto arguments = request.getObject("arguments");
  auto launchCommands = GetStrings(arguments, "launchCommands");

  // Instantiate a launch info instance for the target.
  auto launch_info = g_dap.target.GetLaunchInfo();

  // Grab the current working directory if there is one and set it in the
  // launch info.
  const auto cwd = GetString(arguments, "cwd");
  if (!cwd.empty())
    launch_info.SetWorkingDirectory(cwd.data());

  // Extract any extra arguments and append them to our program arguments for
  // when we launch
  auto args = GetStrings(arguments, "args");
  if (!args.empty())
    launch_info.SetArguments(MakeArgv(args).data(), true);

  // Pass any environment variables along that the user specified.
  auto envs = GetStrings(arguments, "env");
  if (!envs.empty())
    launch_info.SetEnvironmentEntries(MakeArgv(envs).data(), true);

  auto flags = launch_info.GetLaunchFlags();

  if (GetBoolean(arguments, "disableASLR", true))
    flags |= lldb::eLaunchFlagDisableASLR;
  if (GetBoolean(arguments, "disableSTDIO", false))
    flags |= lldb::eLaunchFlagDisableSTDIO;
  if (GetBoolean(arguments, "shellExpandArguments", false))
    flags |= lldb::eLaunchFlagShellExpandArguments;
  const bool detachOnError = GetBoolean(arguments, "detachOnError", false);
  launch_info.SetDetachOnError(detachOnError);
  launch_info.SetLaunchFlags(flags | lldb::eLaunchFlagDebug |
                             lldb::eLaunchFlagStopAtEntry);
  const uint64_t timeout_seconds = GetUnsigned(arguments, "timeout", 30);

  if (GetBoolean(arguments, "runInTerminal", false)) {
    if (llvm::Error err = request_runInTerminal(request, timeout_seconds))
      error.SetErrorString(llvm::toString(std::move(err)).c_str());
  } else if (launchCommands.empty()) {
    // Disable async events so the launch will be successful when we return from
    // the launch call and the launch will happen synchronously
    g_dap.debugger.SetAsync(false);
    g_dap.target.Launch(launch_info, error);
    g_dap.debugger.SetAsync(true);
  } else {
    // Set the launch info so that run commands can access the configured
    // launch details.
    g_dap.target.SetLaunchInfo(launch_info);
    if (llvm::Error err = g_dap.RunLaunchCommands(launchCommands)) {
      error.SetErrorString(llvm::toString(std::move(err)).c_str());
      return error;
    }
    // The custom commands might have created a new target so we should use the
    // selected target after these commands are run.
    g_dap.target = g_dap.debugger.GetSelectedTarget();
    // Make sure the process is launched and stopped at the entry point before
    // proceeding as the launch commands are not run using the synchronous
    // mode.
    error = g_dap.WaitForProcessToStop(timeout_seconds);
  }
  return error;
}

// "LaunchRequest": {
//   "allOf": [ { "$ref": "#/definitions/Request" }, {
//     "type": "object",
//     "description": "Launch request; value of command field is 'launch'.",
//     "properties": {
//       "command": {
//         "type": "string",
//         "enum": [ "launch" ]
//       },
//       "arguments": {
//         "$ref": "#/definitions/LaunchRequestArguments"
//       }
//     },
//     "required": [ "command", "arguments"  ]
//   }]
// },
// "LaunchRequestArguments": {
//   "type": "object",
//   "description": "Arguments for 'launch' request.",
//   "properties": {
//     "noDebug": {
//       "type": "boolean",
//       "description": "If noDebug is true the launch request should launch
//                       the program without enabling debugging."
//     }
//   }
// },
// "LaunchResponse": {
//   "allOf": [ { "$ref": "#/definitions/Response" }, {
//     "type": "object",
//     "description": "Response to 'launch' request. This is just an
//                     acknowledgement, so no body field is required."
//   }]
// }
void request_launch(const llvm::json::Object &request) {
  g_dap.is_attach = false;
  g_dap.last_launch_or_attach_request = request;
  llvm::json::Object response;
  FillResponse(request, response);
  auto arguments = request.getObject("arguments");
  g_dap.init_commands = GetStrings(arguments, "initCommands");
  g_dap.pre_run_commands = GetStrings(arguments, "preRunCommands");
  g_dap.stop_commands = GetStrings(arguments, "stopCommands");
  g_dap.exit_commands = GetStrings(arguments, "exitCommands");
  g_dap.terminate_commands = GetStrings(arguments, "terminateCommands");
  g_dap.post_run_commands = GetStrings(arguments, "postRunCommands");
  g_dap.stop_at_entry = GetBoolean(arguments, "stopOnEntry", false);
  const llvm::StringRef debuggerRoot = GetString(arguments, "debuggerRoot");
  g_dap.enable_auto_variable_summaries =
      GetBoolean(arguments, "enableAutoVariableSummaries", false);
  g_dap.enable_synthetic_child_debugging =
      GetBoolean(arguments, "enableSyntheticChildDebugging", false);
  g_dap.command_escape_prefix =
      GetString(arguments, "commandEscapePrefix", "`");
  g_dap.SetFrameFormat(GetString(arguments, "customFrameFormat"));
  g_dap.SetThreadFormat(GetString(arguments, "customThreadFormat"));

  PrintWelcomeMessage();

  // This is a hack for loading DWARF in .o files on Mac where the .o files
  // in the debug map of the main executable have relative paths which
  // require the lldb-dap binary to have its working directory set to that
  // relative root for the .o files in order to be able to load debug info.
  if (!debuggerRoot.empty())
    llvm::sys::fs::set_current_path(debuggerRoot);

  // Run any initialize LLDB commands the user specified in the launch.json.
  // This is run before target is created, so commands can't do anything with
  // the targets - preRunCommands are run with the target.
  if (llvm::Error err = g_dap.RunInitCommands()) {
    response["success"] = false;
    EmplaceSafeString(response, "message", llvm::toString(std::move(err)));
    g_dap.SendJSON(llvm::json::Value(std::move(response)));
    return;
  }

  SetSourceMapFromArguments(*arguments);

  lldb::SBError status;
  g_dap.SetTarget(g_dap.CreateTargetFromArguments(*arguments, status));
  if (status.Fail()) {
    response["success"] = llvm::json::Value(false);
    EmplaceSafeString(response, "message", status.GetCString());
    g_dap.SendJSON(llvm::json::Value(std::move(response)));
    return;
  }

  // Run any pre run LLDB commands the user specified in the launch.json
  if (llvm::Error err = g_dap.RunPreRunCommands()) {
    response["success"] = false;
    EmplaceSafeString(response, "message", llvm::toString(std::move(err)));
    g_dap.SendJSON(llvm::json::Value(std::move(response)));
    return;
  }

  status = LaunchProcess(request);

  if (status.Fail()) {
    response["success"] = llvm::json::Value(false);
    EmplaceSafeString(response, "message", std::string(status.GetCString()));
  } else {
    g_dap.RunPostRunCommands();
  }

  g_dap.SendJSON(llvm::json::Value(std::move(response)));

  if (!status.Fail()) {
    if (g_dap.is_attach)
      SendProcessEvent(Attach); // this happens when doing runInTerminal
    else
      SendProcessEvent(Launch);
  }
  g_dap.SendJSON(CreateEventObject("initialized"));
}

// "NextRequest": {
//   "allOf": [ { "$ref": "#/definitions/Request" }, {
//     "type": "object",
//     "description": "Next request; value of command field is 'next'. The
//                     request starts the debuggee to run again for one step.
//                     The debug adapter first sends the NextResponse and then
//                     a StoppedEvent (event type 'step') after the step has
//                     completed.",
//     "properties": {
//       "command": {
//         "type": "string",
//         "enum": [ "next" ]
//       },
//       "arguments": {
//         "$ref": "#/definitions/NextArguments"
//       }
//     },
//     "required": [ "command", "arguments"  ]
//   }]
// },
// "NextArguments": {
//   "type": "object",
//   "description": "Arguments for 'next' request.",
//   "properties": {
//     "threadId": {
//       "type": "integer",
//       "description": "Execute 'next' for this thread."
//     }
//   },
//   "required": [ "threadId" ]
// },
// "NextResponse": {
//   "allOf": [ { "$ref": "#/definitions/Response" }, {
//     "type": "object",
//     "description": "Response to 'next' request. This is just an
//                     acknowledgement, so no body field is required."
//   }]
// }
void request_next(const llvm::json::Object &request) {
  llvm::json::Object response;
  FillResponse(request, response);
  auto arguments = request.getObject("arguments");
  lldb::SBThread thread = g_dap.GetLLDBThread(*arguments);
  if (thread.IsValid()) {
    // Remember the thread ID that caused the resume so we can set the
    // "threadCausedFocus" boolean value in the "stopped" events.
    g_dap.focus_tid = thread.GetThreadID();
    thread.StepOver();
  } else {
    response["success"] = llvm::json::Value(false);
  }
  g_dap.SendJSON(llvm::json::Value(std::move(response)));
}

// "PauseRequest": {
//   "allOf": [ { "$ref": "#/definitions/Request" }, {
//     "type": "object",
//     "description": "Pause request; value of command field is 'pause'. The
//     request suspenses the debuggee. The debug adapter first sends the
//     PauseResponse and then a StoppedEvent (event type 'pause') after the
//     thread has been paused successfully.", "properties": {
//       "command": {
//         "type": "string",
//         "enum": [ "pause" ]
//       },
//       "arguments": {
//         "$ref": "#/definitions/PauseArguments"
//       }
//     },
//     "required": [ "command", "arguments"  ]
//   }]
// },
// "PauseArguments": {
//   "type": "object",
//   "description": "Arguments for 'pause' request.",
//   "properties": {
//     "threadId": {
//       "type": "integer",
//       "description": "Pause execution for this thread."
//     }
//   },
//   "required": [ "threadId" ]
// },
// "PauseResponse": {
//   "allOf": [ { "$ref": "#/definitions/Response" }, {
//     "type": "object",
//     "description": "Response to 'pause' request. This is just an
//     acknowledgement, so no body field is required."
//   }]
// }
void request_pause(const llvm::json::Object &request) {
  llvm::json::Object response;
  FillResponse(request, response);
  lldb::SBProcess process = g_dap.target.GetProcess();
  lldb::SBError error = process.Stop();
  g_dap.SendJSON(llvm::json::Value(std::move(response)));
}

// "RestartRequest": {
//   "allOf": [ { "$ref": "#/definitions/Request" }, {
//     "type": "object",
//     "description": "Restarts a debug session. Clients should only call this
//     request if the corresponding capability `supportsRestartRequest` is
//     true.\nIf the capability is missing or has the value false, a typical
//     client emulates `restart` by terminating the debug adapter first and then
//     launching it anew.",
//     "properties": {
//       "command": {
//         "type": "string",
//         "enum": [ "restart" ]
//       },
//       "arguments": {
//         "$ref": "#/definitions/RestartArguments"
//       }
//     },
//     "required": [ "command" ]
//   }]
// },
// "RestartArguments": {
//   "type": "object",
//   "description": "Arguments for `restart` request.",
//   "properties": {
//     "arguments": {
//       "oneOf": [
//         { "$ref": "#/definitions/LaunchRequestArguments" },
//         { "$ref": "#/definitions/AttachRequestArguments" }
//       ],
//       "description": "The latest version of the `launch` or `attach`
//       configuration."
//     }
//   }
// },
// "RestartResponse": {
//   "allOf": [ { "$ref": "#/definitions/Response" }, {
//     "type": "object",
//     "description": "Response to `restart` request. This is just an
//     acknowledgement, so no body field is required."
//   }]
// },
void request_restart(const llvm::json::Object &request) {
  llvm::json::Object response;
  FillResponse(request, response);
  if (!g_dap.last_launch_or_attach_request) {
    response["success"] = llvm::json::Value(false);
    EmplaceSafeString(response, "message",
                      "Restart request received but no process was launched.");
    g_dap.SendJSON(llvm::json::Value(std::move(response)));
    return;
  }
  // Check if we were in a "launch" session or an "attach" session.
  //
  // Restarting is not well defined when we started the session by attaching to
  // an existing process, because we don't know how the process was started, so
  // we don't support it.
  //
  // Note that when using runInTerminal we're technically attached, but it's an
  // implementation detail. The adapter *did* launch the process in response to
  // a "launch" command, so we can still stop it and re-run it. This is why we
  // don't just check `g_dap.is_attach`.
  if (GetString(*g_dap.last_launch_or_attach_request, "command") == "attach") {
    response["success"] = llvm::json::Value(false);
    EmplaceSafeString(response, "message",
                      "Restarting an \"attach\" session is not supported.");
    g_dap.SendJSON(llvm::json::Value(std::move(response)));
    return;
  }

  // The optional `arguments` field in RestartRequest can contain an updated
  // version of the launch arguments. If there's one, use it.
  auto restart_arguments = request.getObject("arguments");
  if (restart_arguments) {
    auto launch_request_arguments = restart_arguments->getObject("arguments");
    if (launch_request_arguments) {
      (*g_dap.last_launch_or_attach_request)["arguments"] =
          llvm::json::Value(llvm::json::Object(*launch_request_arguments));
    }
  }

  // Keep track of the old PID so when we get a "process exited" event from the
  // killed process we can detect it and not shut down the whole session.
  lldb::SBProcess process = g_dap.target.GetProcess();
  g_dap.restarting_process_id = process.GetProcessID();

  // Stop the current process if necessary. The logic here is similar to
  // CommandObjectProcessLaunchOrAttach::StopProcessIfNecessary, except that
  // we don't ask the user for confirmation.
  g_dap.debugger.SetAsync(false);
  if (process.IsValid()) {
    lldb::StateType state = process.GetState();
    if (state != lldb::eStateConnected) {
      process.Kill();
    }
    // Clear the list of thread ids to avoid sending "thread exited" events
    // for threads of the process we are terminating.
    g_dap.thread_ids.clear();
  }
  g_dap.debugger.SetAsync(true);
  LaunchProcess(*g_dap.last_launch_or_attach_request);

  // This is normally done after receiving a "configuration done" request.
  // Because we're restarting, configuration has already happened so we can
  // continue the process right away.
  if (g_dap.stop_at_entry) {
    SendThreadStoppedEvent();
  } else {
    g_dap.target.GetProcess().Continue();
  }

  g_dap.SendJSON(llvm::json::Value(std::move(response)));
}

// "ScopesRequest": {
//   "allOf": [ { "$ref": "#/definitions/Request" }, {
//     "type": "object",
//     "description": "Scopes request; value of command field is 'scopes'. The
//     request returns the variable scopes for a given stackframe ID.",
//     "properties": {
//       "command": {
//         "type": "string",
//         "enum": [ "scopes" ]
//       },
//       "arguments": {
//         "$ref": "#/definitions/ScopesArguments"
//       }
//     },
//     "required": [ "command", "arguments"  ]
//   }]
// },
// "ScopesArguments": {
//   "type": "object",
//   "description": "Arguments for 'scopes' request.",
//   "properties": {
//     "frameId": {
//       "type": "integer",
//       "description": "Retrieve the scopes for this stackframe."
//     }
//   },
//   "required": [ "frameId" ]
// },
// "ScopesResponse": {
//   "allOf": [ { "$ref": "#/definitions/Response" }, {
//     "type": "object",
//     "description": "Response to 'scopes' request.",
//     "properties": {
//       "body": {
//         "type": "object",
//         "properties": {
//           "scopes": {
//             "type": "array",
//             "items": {
//               "$ref": "#/definitions/Scope"
//             },
//             "description": "The scopes of the stackframe. If the array has
//             length zero, there are no scopes available."
//           }
//         },
//         "required": [ "scopes" ]
//       }
//     },
//     "required": [ "body" ]
//   }]
// }
void request_scopes(const llvm::json::Object &request) {
  llvm::json::Object response;
  FillResponse(request, response);
  llvm::json::Object body;
  auto arguments = request.getObject("arguments");
  lldb::SBFrame frame = g_dap.GetLLDBFrame(*arguments);
  // As the user selects different stack frames in the GUI, a "scopes" request
  // will be sent to the DAP. This is the only way we know that the user has
  // selected a frame in a thread. There are no other notifications that are
  // sent and VS code doesn't allow multiple frames to show variables
  // concurrently. If we select the thread and frame as the "scopes" requests
  // are sent, this allows users to type commands in the debugger console
  // with a backtick character to run lldb commands and these lldb commands
  // will now have the right context selected as they are run. If the user
  // types "`bt" into the debugger console and we had another thread selected
  // in the LLDB library, we would show the wrong thing to the user. If the
  // users switches threads with a lldb command like "`thread select 14", the
  // GUI will not update as there are no "event" notification packets that
  // allow us to change the currently selected thread or frame in the GUI that
  // I am aware of.
  if (frame.IsValid()) {
    frame.GetThread().GetProcess().SetSelectedThread(frame.GetThread());
    frame.GetThread().SetSelectedFrame(frame.GetFrameID());
  }

  g_dap.variables.locals = frame.GetVariables(/*arguments=*/true,
                                              /*locals=*/true,
                                              /*statics=*/false,
                                              /*in_scope_only=*/true);
  g_dap.variables.globals = frame.GetVariables(/*arguments=*/false,
                                               /*locals=*/false,
                                               /*statics=*/true,
                                               /*in_scope_only=*/true);
  g_dap.variables.registers = frame.GetRegisters();
  body.try_emplace("scopes", g_dap.CreateTopLevelScopes());
  response.try_emplace("body", std::move(body));
  g_dap.SendJSON(llvm::json::Value(std::move(response)));
}

// "SetBreakpointsRequest": {
//   "allOf": [ { "$ref": "#/definitions/Request" }, {
//     "type": "object",
//     "description": "SetBreakpoints request; value of command field is
//     'setBreakpoints'. Sets multiple breakpoints for a single source and
//     clears all previous breakpoints in that source. To clear all breakpoint
//     for a source, specify an empty array. When a breakpoint is hit, a
//     StoppedEvent (event type 'breakpoint') is generated.", "properties": {
//       "command": {
//         "type": "string",
//         "enum": [ "setBreakpoints" ]
//       },
//       "arguments": {
//         "$ref": "#/definitions/SetBreakpointsArguments"
//       }
//     },
//     "required": [ "command", "arguments"  ]
//   }]
// },
// "SetBreakpointsArguments": {
//   "type": "object",
//   "description": "Arguments for 'setBreakpoints' request.",
//   "properties": {
//     "source": {
//       "$ref": "#/definitions/Source",
//       "description": "The source location of the breakpoints; either
//       source.path or source.reference must be specified."
//     },
//     "breakpoints": {
//       "type": "array",
//       "items": {
//         "$ref": "#/definitions/SourceBreakpoint"
//       },
//       "description": "The code locations of the breakpoints."
//     },
//     "lines": {
//       "type": "array",
//       "items": {
//         "type": "integer"
//       },
//       "description": "Deprecated: The code locations of the breakpoints."
//     },
//     "sourceModified": {
//       "type": "boolean",
//       "description": "A value of true indicates that the underlying source
//       has been modified which results in new breakpoint locations."
//     }
//   },
//   "required": [ "source" ]
// },
// "SetBreakpointsResponse": {
//   "allOf": [ { "$ref": "#/definitions/Response" }, {
//     "type": "object",
//     "description": "Response to 'setBreakpoints' request. Returned is
//     information about each breakpoint created by this request. This includes
//     the actual code location and whether the breakpoint could be verified.
//     The breakpoints returned are in the same order as the elements of the
//     'breakpoints' (or the deprecated 'lines') in the
//     SetBreakpointsArguments.", "properties": {
//       "body": {
//         "type": "object",
//         "properties": {
//           "breakpoints": {
//             "type": "array",
//             "items": {
//               "$ref": "#/definitions/Breakpoint"
//             },
//             "description": "Information about the breakpoints. The array
//             elements are in the same order as the elements of the
//             'breakpoints' (or the deprecated 'lines') in the
//             SetBreakpointsArguments."
//           }
//         },
//         "required": [ "breakpoints" ]
//       }
//     },
//     "required": [ "body" ]
//   }]
// },
// "SourceBreakpoint": {
//   "type": "object",
//   "description": "Properties of a breakpoint or logpoint passed to the
//   setBreakpoints request.", "properties": {
//     "line": {
//       "type": "integer",
//       "description": "The source line of the breakpoint or logpoint."
//     },
//     "column": {
//       "type": "integer",
//       "description": "An optional source column of the breakpoint."
//     },
//     "condition": {
//       "type": "string",
//       "description": "An optional expression for conditional breakpoints."
//     },
//     "hitCondition": {
//       "type": "string",
//       "description": "An optional expression that controls how many hits of
//       the breakpoint are ignored. The backend is expected to interpret the
//       expression as needed."
//     },
//     "logMessage": {
//       "type": "string",
//       "description": "If this attribute exists and is non-empty, the backend
//       must not 'break' (stop) but log the message instead. Expressions within
//       {} are interpolated."
//     }
//   },
//   "required": [ "line" ]
// }
void request_setBreakpoints(const llvm::json::Object &request) {
  llvm::json::Object response;
  lldb::SBError error;
  FillResponse(request, response);
  auto arguments = request.getObject("arguments");
  auto source = arguments->getObject("source");
  const auto path = GetString(source, "path");
  auto breakpoints = arguments->getArray("breakpoints");
  llvm::json::Array response_breakpoints;

  // Decode the source breakpoint infos for this "setBreakpoints" request
  SourceBreakpointMap request_bps;
  // "breakpoints" may be unset, in which case we treat it the same as being set
  // to an empty array.
  if (breakpoints) {
    for (const auto &bp : *breakpoints) {
      auto bp_obj = bp.getAsObject();
      if (bp_obj) {
        SourceBreakpoint src_bp(*bp_obj);
        request_bps[src_bp.line] = src_bp;

        // We check if this breakpoint already exists to update it
        auto existing_source_bps = g_dap.source_breakpoints.find(path);
        if (existing_source_bps != g_dap.source_breakpoints.end()) {
          const auto &existing_bp =
              existing_source_bps->second.find(src_bp.line);
          if (existing_bp != existing_source_bps->second.end()) {
            existing_bp->second.UpdateBreakpoint(src_bp);
            AppendBreakpoint(&existing_bp->second, response_breakpoints, path,
                             src_bp.line);
            continue;
          }
        }
        // At this point the breakpoint is new
        g_dap.source_breakpoints[path][src_bp.line] = src_bp;
        SourceBreakpoint &new_bp = g_dap.source_breakpoints[path][src_bp.line];
        new_bp.SetBreakpoint(path.data());
        AppendBreakpoint(&new_bp, response_breakpoints, path, new_bp.line);
      }
    }
  }

  // Delete any breakpoints in this source file that aren't in the
  // request_bps set. There is no call to remove breakpoints other than
  // calling this function with a smaller or empty "breakpoints" list.
  auto old_src_bp_pos = g_dap.source_breakpoints.find(path);
  if (old_src_bp_pos != g_dap.source_breakpoints.end()) {
    for (auto &old_bp : old_src_bp_pos->second) {
      auto request_pos = request_bps.find(old_bp.first);
      if (request_pos == request_bps.end()) {
        // This breakpoint no longer exists in this source file, delete it
        g_dap.target.BreakpointDelete(old_bp.second.bp.GetID());
        old_src_bp_pos->second.erase(old_bp.first);
      }
    }
  }

  llvm::json::Object body;
  body.try_emplace("breakpoints", std::move(response_breakpoints));
  response.try_emplace("body", std::move(body));
  g_dap.SendJSON(llvm::json::Value(std::move(response)));
}

// "SetExceptionBreakpointsRequest": {
//   "allOf": [ { "$ref": "#/definitions/Request" }, {
//     "type": "object",
//     "description": "SetExceptionBreakpoints request; value of command field
//     is 'setExceptionBreakpoints'. The request configures the debuggers
//     response to thrown exceptions. If an exception is configured to break, a
//     StoppedEvent is fired (event type 'exception').", "properties": {
//       "command": {
//         "type": "string",
//         "enum": [ "setExceptionBreakpoints" ]
//       },
//       "arguments": {
//         "$ref": "#/definitions/SetExceptionBreakpointsArguments"
//       }
//     },
//     "required": [ "command", "arguments"  ]
//   }]
// },
// "SetExceptionBreakpointsArguments": {
//   "type": "object",
//   "description": "Arguments for 'setExceptionBreakpoints' request.",
//   "properties": {
//     "filters": {
//       "type": "array",
//       "items": {
//         "type": "string"
//       },
//       "description": "IDs of checked exception options. The set of IDs is
//       returned via the 'exceptionBreakpointFilters' capability."
//     },
//     "exceptionOptions": {
//       "type": "array",
//       "items": {
//         "$ref": "#/definitions/ExceptionOptions"
//       },
//       "description": "Configuration options for selected exceptions."
//     }
//   },
//   "required": [ "filters" ]
// },
// "SetExceptionBreakpointsResponse": {
//   "allOf": [ { "$ref": "#/definitions/Response" }, {
//     "type": "object",
//     "description": "Response to 'setExceptionBreakpoints' request. This is
//     just an acknowledgement, so no body field is required."
//   }]
// }
void request_setExceptionBreakpoints(const llvm::json::Object &request) {
  llvm::json::Object response;
  lldb::SBError error;
  FillResponse(request, response);
  auto arguments = request.getObject("arguments");
  auto filters = arguments->getArray("filters");
  // Keep a list of any exception breakpoint filter names that weren't set
  // so we can clear any exception breakpoints if needed.
  std::set<std::string> unset_filters;
  for (const auto &bp : *g_dap.exception_breakpoints)
    unset_filters.insert(bp.filter);

  for (const auto &value : *filters) {
    const auto filter = GetAsString(value);
    auto exc_bp = g_dap.GetExceptionBreakpoint(std::string(filter));
    if (exc_bp) {
      exc_bp->SetBreakpoint();
      unset_filters.erase(std::string(filter));
    }
  }
  for (const auto &filter : unset_filters) {
    auto exc_bp = g_dap.GetExceptionBreakpoint(filter);
    if (exc_bp)
      exc_bp->ClearBreakpoint();
  }
  g_dap.SendJSON(llvm::json::Value(std::move(response)));
}

// "SetFunctionBreakpointsRequest": {
//   "allOf": [ { "$ref": "#/definitions/Request" }, {
//     "type": "object",
//     "description": "SetFunctionBreakpoints request; value of command field is
//     'setFunctionBreakpoints'. Sets multiple function breakpoints and clears
//     all previous function breakpoints. To clear all function breakpoint,
//     specify an empty array. When a function breakpoint is hit, a StoppedEvent
//     (event type 'function breakpoint') is generated.", "properties": {
//       "command": {
//         "type": "string",
//         "enum": [ "setFunctionBreakpoints" ]
//       },
//       "arguments": {
//         "$ref": "#/definitions/SetFunctionBreakpointsArguments"
//       }
//     },
//     "required": [ "command", "arguments"  ]
//   }]
// },
// "SetFunctionBreakpointsArguments": {
//   "type": "object",
//   "description": "Arguments for 'setFunctionBreakpoints' request.",
//   "properties": {
//     "breakpoints": {
//       "type": "array",
//       "items": {
//         "$ref": "#/definitions/FunctionBreakpoint"
//       },
//       "description": "The function names of the breakpoints."
//     }
//   },
//   "required": [ "breakpoints" ]
// },
// "FunctionBreakpoint": {
//   "type": "object",
//   "description": "Properties of a breakpoint passed to the
//   setFunctionBreakpoints request.", "properties": {
//     "name": {
//       "type": "string",
//       "description": "The name of the function."
//     },
//     "condition": {
//       "type": "string",
//       "description": "An optional expression for conditional breakpoints."
//     },
//     "hitCondition": {
//       "type": "string",
//       "description": "An optional expression that controls how many hits of
//       the breakpoint are ignored. The backend is expected to interpret the
//       expression as needed."
//     }
//   },
//   "required": [ "name" ]
// },
// "SetFunctionBreakpointsResponse": {
//   "allOf": [ { "$ref": "#/definitions/Response" }, {
//     "type": "object",
//     "description": "Response to 'setFunctionBreakpoints' request. Returned is
//     information about each breakpoint created by this request.",
//     "properties": {
//       "body": {
//         "type": "object",
//         "properties": {
//           "breakpoints": {
//             "type": "array",
//             "items": {
//               "$ref": "#/definitions/Breakpoint"
//             },
//             "description": "Information about the breakpoints. The array
//             elements correspond to the elements of the 'breakpoints' array."
//           }
//         },
//         "required": [ "breakpoints" ]
//       }
//     },
//     "required": [ "body" ]
//   }]
// }
void request_setFunctionBreakpoints(const llvm::json::Object &request) {
  llvm::json::Object response;
  lldb::SBError error;
  FillResponse(request, response);
  auto arguments = request.getObject("arguments");
  auto breakpoints = arguments->getArray("breakpoints");
  FunctionBreakpointMap request_bps;
  llvm::json::Array response_breakpoints;
  for (const auto &value : *breakpoints) {
    auto bp_obj = value.getAsObject();
    if (bp_obj == nullptr)
      continue;
    FunctionBreakpoint func_bp(*bp_obj);
    request_bps[func_bp.functionName] = std::move(func_bp);
  }

  std::vector<llvm::StringRef> remove_names;
  // Disable any function breakpoints that aren't in the request_bps.
  // There is no call to remove function breakpoints other than calling this
  // function with a smaller or empty "breakpoints" list.
  for (auto &pair : g_dap.function_breakpoints) {
    auto request_pos = request_bps.find(pair.first());
    if (request_pos == request_bps.end()) {
      // This function breakpoint no longer exists delete it from LLDB
      g_dap.target.BreakpointDelete(pair.second.bp.GetID());
      remove_names.push_back(pair.first());
    } else {
      // Update the existing breakpoint as any setting withing the function
      // breakpoint might have changed.
      pair.second.UpdateBreakpoint(request_pos->second);
      // Remove this breakpoint from the request breakpoints since we have
      // handled it here and we don't need to set a new breakpoint below.
      request_bps.erase(request_pos);
      // Add this breakpoint info to the response
      AppendBreakpoint(&pair.second, response_breakpoints);
    }
  }
  // Remove any breakpoints that are no longer in our list
  for (const auto &name : remove_names)
    g_dap.function_breakpoints.erase(name);

  // Any breakpoints that are left in "request_bps" are breakpoints that
  // need to be set.
  for (auto &pair : request_bps) {
    // Add this breakpoint info to the response
    g_dap.function_breakpoints[pair.first()] = std::move(pair.second);
    FunctionBreakpoint &new_bp = g_dap.function_breakpoints[pair.first()];
    new_bp.SetBreakpoint();
    AppendBreakpoint(&new_bp, response_breakpoints);
  }

  llvm::json::Object body;
  body.try_emplace("breakpoints", std::move(response_breakpoints));
  response.try_emplace("body", std::move(body));
  g_dap.SendJSON(llvm::json::Value(std::move(response)));
}

// "DataBreakpointInfoRequest": {
//   "allOf": [ { "$ref": "#/definitions/Request" }, {
//     "type": "object",
//     "description": "Obtains information on a possible data breakpoint that
//     could be set on an expression or variable.\nClients should only call this
//     request if the corresponding capability `supportsDataBreakpoints` is
//     true.", "properties": {
//       "command": {
//         "type": "string",
//         "enum": [ "dataBreakpointInfo" ]
//       },
//       "arguments": {
//         "$ref": "#/definitions/DataBreakpointInfoArguments"
//       }
//     },
//     "required": [ "command", "arguments"  ]
//   }]
// },
// "DataBreakpointInfoArguments": {
//   "type": "object",
//   "description": "Arguments for `dataBreakpointInfo` request.",
//   "properties": {
//     "variablesReference": {
//       "type": "integer",
//       "description": "Reference to the variable container if the data
//       breakpoint is requested for a child of the container. The
//       `variablesReference` must have been obtained in the current suspended
//       state. See 'Lifetime of Object References' in the Overview section for
//       details."
//     },
//     "name": {
//       "type": "string",
//       "description": "The name of the variable's child to obtain data
//       breakpoint information for.\nIf `variablesReference` isn't specified,
//       this can be an expression."
//     },
//     "frameId": {
//       "type": "integer",
//       "description": "When `name` is an expression, evaluate it in the scope
//       of this stack frame. If not specified, the expression is evaluated in
//       the global scope. When `variablesReference` is specified, this property
//       has no effect."
//     }
//   },
//   "required": [ "name" ]
// },
// "DataBreakpointInfoResponse": {
//   "allOf": [ { "$ref": "#/definitions/Response" }, {
//     "type": "object",
//     "description": "Response to `dataBreakpointInfo` request.",
//     "properties": {
//       "body": {
//         "type": "object",
//         "properties": {
//           "dataId": {
//             "type": [ "string", "null" ],
//             "description": "An identifier for the data on which a data
//             breakpoint can be registered with the `setDataBreakpoints`
//             request or null if no data breakpoint is available. If a
//             `variablesReference` or `frameId` is passed, the `dataId` is
//             valid in the current suspended state, otherwise it's valid
//             indefinitely. See 'Lifetime of Object References' in the Overview
//             section for details. Breakpoints set using the `dataId` in the
//             `setDataBreakpoints` request may outlive the lifetime of the
//             associated `dataId`."
//           },
//           "description": {
//             "type": "string",
//             "description": "UI string that describes on what data the
//             breakpoint is set on or why a data breakpoint is not available."
//           },
//           "accessTypes": {
//             "type": "array",
//             "items": {
//               "$ref": "#/definitions/DataBreakpointAccessType"
//             },
//             "description": "Attribute lists the available access types for a
//             potential data breakpoint. A UI client could surface this
//             information."
//           },
//           "canPersist": {
//             "type": "boolean",
//             "description": "Attribute indicates that a potential data
//             breakpoint could be persisted across sessions."
//           }
//         },
//         "required": [ "dataId", "description" ]
//       }
//     },
//     "required": [ "body" ]
//   }]
// }
void request_dataBreakpointInfo(const llvm::json::Object &request) {
  llvm::json::Object response;
  FillResponse(request, response);
  llvm::json::Object body;
  lldb::SBError error;
  llvm::json::Array accessTypes{"read", "write", "readWrite"};
  const auto *arguments = request.getObject("arguments");
  const auto variablesReference =
      GetUnsigned(arguments, "variablesReference", 0);
  llvm::StringRef name = GetString(arguments, "name");
  lldb::SBFrame frame = g_dap.GetLLDBFrame(*arguments);
  lldb::SBValue variable = FindVariable(variablesReference, name);
  std::string addr, size;

  if (variable.IsValid()) {
    lldb::addr_t load_addr = variable.GetLoadAddress();
    size_t byte_size = variable.GetByteSize();
    if (load_addr == LLDB_INVALID_ADDRESS) {
      body.try_emplace("dataId", nullptr);
      body.try_emplace("description",
                       "does not exist in memory, its location is " +
                           std::string(variable.GetLocation()));
    } else if (byte_size == 0) {
      body.try_emplace("dataId", nullptr);
      body.try_emplace("description", "variable size is 0");
    } else {
      addr = llvm::utohexstr(load_addr);
      size = llvm::utostr(byte_size);
    }
  } else if (variablesReference == 0 && frame.IsValid()) {
    lldb::SBValue value = frame.EvaluateExpression(name.data());
    if (value.GetError().Fail()) {
      lldb::SBError error = value.GetError();
      const char *error_cstr = error.GetCString();
      body.try_emplace("dataId", nullptr);
      body.try_emplace("description", error_cstr && error_cstr[0]
                                          ? std::string(error_cstr)
                                          : "evaluation failed");
    } else {
      uint64_t load_addr = value.GetValueAsUnsigned();
      lldb::SBData data = value.GetPointeeData();
      if (data.IsValid()) {
        size = llvm::utostr(data.GetByteSize());
        addr = llvm::utohexstr(load_addr);
        lldb::SBMemoryRegionInfo region;
        lldb::SBError err =
            g_dap.target.GetProcess().GetMemoryRegionInfo(load_addr, region);
        // Only lldb-server supports "qMemoryRegionInfo". So, don't fail this
        // request if SBProcess::GetMemoryRegionInfo returns error.
        if (err.Success()) {
          if (!(region.IsReadable() || region.IsWritable())) {
            body.try_emplace("dataId", nullptr);
            body.try_emplace("description",
                             "memory region for address " + addr +
                                 " has no read or write permissions");
          }
        }
      } else {
        body.try_emplace("dataId", nullptr);
        body.try_emplace("description",
                         "unable to get byte size for expression: " +
                             name.str());
      }
    }
  } else {
    body.try_emplace("dataId", nullptr);
    body.try_emplace("description", "variable not found: " + name.str());
  }

  if (!body.getObject("dataId")) {
    body.try_emplace("dataId", addr + "/" + size);
    body.try_emplace("accessTypes", std::move(accessTypes));
    body.try_emplace("description",
                     size + " bytes at " + addr + " " + name.str());
  }
  response.try_emplace("body", std::move(body));
  g_dap.SendJSON(llvm::json::Value(std::move(response)));
}

// "SetDataBreakpointsRequest": {
//   "allOf": [ { "$ref": "#/definitions/Request" }, {
//     "type": "object",
//     "description": "Replaces all existing data breakpoints with new data
//     breakpoints.\nTo clear all data breakpoints, specify an empty
//     array.\nWhen a data breakpoint is hit, a `stopped` event (with reason
//     `data breakpoint`) is generated.\nClients should only call this request
//     if the corresponding capability `supportsDataBreakpoints` is true.",
//     "properties": {
//       "command": {
//         "type": "string",
//         "enum": [ "setDataBreakpoints" ]
//       },
//       "arguments": {
//         "$ref": "#/definitions/SetDataBreakpointsArguments"
//       }
//     },
//     "required": [ "command", "arguments"  ]
//   }]
// },
// "SetDataBreakpointsArguments": {
//   "type": "object",
//   "description": "Arguments for `setDataBreakpoints` request.",
//   "properties": {
//     "breakpoints": {
//       "type": "array",
//       "items": {
//         "$ref": "#/definitions/DataBreakpoint"
//       },
//       "description": "The contents of this array replaces all existing data
//       breakpoints. An empty array clears all data breakpoints."
//     }
//   },
//   "required": [ "breakpoints" ]
// },
// "SetDataBreakpointsResponse": {
//   "allOf": [ { "$ref": "#/definitions/Response" }, {
//     "type": "object",
//     "description": "Response to `setDataBreakpoints` request.\nReturned is
//     information about each breakpoint created by this request.",
//     "properties": {
//       "body": {
//         "type": "object",
//         "properties": {
//           "breakpoints": {
//             "type": "array",
//             "items": {
//               "$ref": "#/definitions/Breakpoint"
//             },
//             "description": "Information about the data breakpoints. The array
//             elements correspond to the elements of the input argument
//             `breakpoints` array."
//           }
//         },
//         "required": [ "breakpoints" ]
//       }
//     },
//     "required": [ "body" ]
//   }]
// }
void request_setDataBreakpoints(const llvm::json::Object &request) {
  llvm::json::Object response;
  lldb::SBError error;
  FillResponse(request, response);
  const auto *arguments = request.getObject("arguments");
  const auto *breakpoints = arguments->getArray("breakpoints");
  llvm::json::Array response_breakpoints;
  g_dap.target.DeleteAllWatchpoints();
  std::vector<Watchpoint> watchpoints;
  if (breakpoints) {
    for (const auto &bp : *breakpoints) {
      const auto *bp_obj = bp.getAsObject();
      if (bp_obj) {
        Watchpoint wp(*bp_obj);
        watchpoints.push_back(wp);
      }
    }
  }
  // If two watchpoints start at the same address, the latter overwrite the
  // former. So, we only enable those at first-seen addresses when iterating
  // backward.
  std::set<lldb::addr_t> addresses;
  for (auto iter = watchpoints.rbegin(); iter != watchpoints.rend(); ++iter) {
    if (addresses.count(iter->addr) == 0) {
      iter->SetWatchpoint();
      addresses.insert(iter->addr);
    }
  }
  for (auto wp : watchpoints)
    AppendBreakpoint(&wp, response_breakpoints);

  llvm::json::Object body;
  body.try_emplace("breakpoints", std::move(response_breakpoints));
  response.try_emplace("body", std::move(body));
  g_dap.SendJSON(llvm::json::Value(std::move(response)));
}

// "SourceRequest": {
//   "allOf": [ { "$ref": "#/definitions/Request" }, {
//     "type": "object",
//     "description": "Source request; value of command field is 'source'. The
//     request retrieves the source code for a given source reference.",
//     "properties": {
//       "command": {
//         "type": "string",
//         "enum": [ "source" ]
//       },
//       "arguments": {
//         "$ref": "#/definitions/SourceArguments"
//       }
//     },
//     "required": [ "command", "arguments"  ]
//   }]
// },
// "SourceArguments": {
//   "type": "object",
//   "description": "Arguments for 'source' request.",
//   "properties": {
//     "source": {
//       "$ref": "#/definitions/Source",
//       "description": "Specifies the source content to load. Either
//       source.path or source.sourceReference must be specified."
//     },
//     "sourceReference": {
//       "type": "integer",
//       "description": "The reference to the source. This is the same as
//       source.sourceReference. This is provided for backward compatibility
//       since old backends do not understand the 'source' attribute."
//     }
//   },
//   "required": [ "sourceReference" ]
// },
// "SourceResponse": {
//   "allOf": [ { "$ref": "#/definitions/Response" }, {
//     "type": "object",
//     "description": "Response to 'source' request.",
//     "properties": {
//       "body": {
//         "type": "object",
//         "properties": {
//           "content": {
//             "type": "string",
//             "description": "Content of the source reference."
//           },
//           "mimeType": {
//             "type": "string",
//             "description": "Optional content type (mime type) of the source."
//           }
//         },
//         "required": [ "content" ]
//       }
//     },
//     "required": [ "body" ]
//   }]
// }
void request_source(const llvm::json::Object &request) {
  llvm::json::Object response;
  FillResponse(request, response);
  llvm::json::Object body{{"content", ""}};
  response.try_emplace("body", std::move(body));
  g_dap.SendJSON(llvm::json::Value(std::move(response)));
}

// "StackTraceRequest": {
//   "allOf": [ { "$ref": "#/definitions/Request" }, {
//     "type": "object",
//     "description": "StackTrace request; value of command field is
//     'stackTrace'. The request returns a stacktrace from the current execution
//     state.", "properties": {
//       "command": {
//         "type": "string",
//         "enum": [ "stackTrace" ]
//       },
//       "arguments": {
//         "$ref": "#/definitions/StackTraceArguments"
//       }
//     },
//     "required": [ "command", "arguments"  ]
//   }]
// },
// "StackTraceArguments": {
//   "type": "object",
//   "description": "Arguments for 'stackTrace' request.",
//   "properties": {
//     "threadId": {
//       "type": "integer",
//       "description": "Retrieve the stacktrace for this thread."
//     },
//     "startFrame": {
//       "type": "integer",
//       "description": "The index of the first frame to return; if omitted
//       frames start at 0."
//     },
//     "levels": {
//       "type": "integer",
//       "description": "The maximum number of frames to return. If levels is
//       not specified or 0, all frames are returned."
//     },
//     "format": {
//       "$ref": "#/definitions/StackFrameFormat",
//       "description": "Specifies details on how to format the stack frames."
//     }
//  },
//   "required": [ "threadId" ]
// },
// "StackTraceResponse": {
//   "allOf": [ { "$ref": "#/definitions/Response" }, {
//     "type": "object",
//     "description": "Response to 'stackTrace' request.",
//     "properties": {
//       "body": {
//         "type": "object",
//         "properties": {
//           "stackFrames": {
//             "type": "array",
//             "items": {
//               "$ref": "#/definitions/StackFrame"
//             },
//             "description": "The frames of the stackframe. If the array has
//             length zero, there are no stackframes available. This means that
//             there is no location information available."
//           },
//           "totalFrames": {
//             "type": "integer",
//             "description": "The total number of frames available."
//           }
//         },
//         "required": [ "stackFrames" ]
//       }
//     },
//     "required": [ "body" ]
//   }]
// }
void request_stackTrace(const llvm::json::Object &request) {
  llvm::json::Object response;
  FillResponse(request, response);
  lldb::SBError error;
  auto arguments = request.getObject("arguments");
  lldb::SBThread thread = g_dap.GetLLDBThread(*arguments);
  llvm::json::Array stackFrames;
  llvm::json::Object body;

  if (thread.IsValid()) {
    const auto startFrame = GetUnsigned(arguments, "startFrame", 0);
    const auto levels = GetUnsigned(arguments, "levels", 0);
    const auto endFrame = (levels == 0) ? INT64_MAX : (startFrame + levels);
    auto totalFrames = thread.GetNumFrames();

    // This will always return an invalid thread when
    // libBacktraceRecording.dylib is not loaded or if there is no extended
    // backtrace.
    lldb::SBThread queue_backtrace_thread =
        thread.GetExtendedBacktraceThread("libdispatch");
    if (queue_backtrace_thread.IsValid()) {
      // One extra frame as a label to mark the enqueued thread.
      totalFrames += queue_backtrace_thread.GetNumFrames() + 1;
    }

    // This will always return an invalid thread when there is no exception in
    // the current thread.
    lldb::SBThread exception_backtrace_thread =
        thread.GetCurrentExceptionBacktrace();
    if (exception_backtrace_thread.IsValid()) {
      // One extra frame as a label to mark the exception thread.
      totalFrames += exception_backtrace_thread.GetNumFrames() + 1;
    }

    for (uint32_t i = startFrame; i < endFrame; ++i) {
      lldb::SBFrame frame;
      std::string prefix;
      if (i < thread.GetNumFrames()) {
        frame = thread.GetFrameAtIndex(i);
      } else if (queue_backtrace_thread.IsValid() &&
                 i < (thread.GetNumFrames() +
                      queue_backtrace_thread.GetNumFrames() + 1)) {
        if (i == thread.GetNumFrames()) {
          const uint32_t thread_idx =
              queue_backtrace_thread.GetExtendedBacktraceOriginatingIndexID();
          const char *queue_name = queue_backtrace_thread.GetQueueName();
          auto name = llvm::formatv("Enqueued from {0} (Thread {1})",
                                    queue_name, thread_idx);
          stackFrames.emplace_back(
              llvm::json::Object{{"id", thread.GetThreadID() + 1},
                                 {"name", name},
                                 {"presentationHint", "label"}});
          continue;
        }
        frame = queue_backtrace_thread.GetFrameAtIndex(
            i - thread.GetNumFrames() - 1);
      } else if (exception_backtrace_thread.IsValid()) {
        if (i == thread.GetNumFrames() +
                     (queue_backtrace_thread.IsValid()
                          ? queue_backtrace_thread.GetNumFrames() + 1
                          : 0)) {
          stackFrames.emplace_back(
              llvm::json::Object{{"id", thread.GetThreadID() + 2},
                                 {"name", "Original Exception Backtrace"},
                                 {"presentationHint", "label"}});
          continue;
        }

        frame = exception_backtrace_thread.GetFrameAtIndex(
            i - thread.GetNumFrames() -
            (queue_backtrace_thread.IsValid()
                 ? queue_backtrace_thread.GetNumFrames() + 1
                 : 0));
      }
      if (!frame.IsValid())
        break;
      stackFrames.emplace_back(CreateStackFrame(frame));
    }

    body.try_emplace("totalFrames", totalFrames);
  }
  body.try_emplace("stackFrames", std::move(stackFrames));
  response.try_emplace("body", std::move(body));
  g_dap.SendJSON(llvm::json::Value(std::move(response)));
}

// "StepInRequest": {
//   "allOf": [ { "$ref": "#/definitions/Request" }, {
//     "type": "object",
//     "description": "StepIn request; value of command field is 'stepIn'. The
//     request starts the debuggee to step into a function/method if possible.
//     If it cannot step into a target, 'stepIn' behaves like 'next'. The debug
//     adapter first sends the StepInResponse and then a StoppedEvent (event
//     type 'step') after the step has completed. If there are multiple
//     function/method calls (or other targets) on the source line, the optional
//     argument 'targetId' can be used to control into which target the 'stepIn'
//     should occur. The list of possible targets for a given source line can be
//     retrieved via the 'stepInTargets' request.", "properties": {
//       "command": {
//         "type": "string",
//         "enum": [ "stepIn" ]
//       },
//       "arguments": {
//         "$ref": "#/definitions/StepInArguments"
//       }
//     },
//     "required": [ "command", "arguments"  ]
//   }]
// },
// "StepInArguments": {
//   "type": "object",
//   "description": "Arguments for 'stepIn' request.",
//   "properties": {
//     "threadId": {
//       "type": "integer",
//       "description": "Execute 'stepIn' for this thread."
//     },
//     "targetId": {
//       "type": "integer",
//       "description": "Optional id of the target to step into."
//     }
//   },
//   "required": [ "threadId" ]
// },
// "StepInResponse": {
//   "allOf": [ { "$ref": "#/definitions/Response" }, {
//     "type": "object",
//     "description": "Response to 'stepIn' request. This is just an
//     acknowledgement, so no body field is required."
//   }]
// }
void request_stepIn(const llvm::json::Object &request) {
  llvm::json::Object response;
  FillResponse(request, response);
  auto arguments = request.getObject("arguments");

  std::string step_in_target;
  uint64_t target_id = GetUnsigned(arguments, "targetId", 0);
  auto it = g_dap.step_in_targets.find(target_id);
  if (it != g_dap.step_in_targets.end())
    step_in_target = it->second;

  const bool single_thread = GetBoolean(arguments, "singleThread", false);
  lldb::RunMode run_mode =
      single_thread ? lldb::eOnlyThisThread : lldb::eOnlyDuringStepping;
  lldb::SBThread thread = g_dap.GetLLDBThread(*arguments);
  if (thread.IsValid()) {
    // Remember the thread ID that caused the resume so we can set the
    // "threadCausedFocus" boolean value in the "stopped" events.
    g_dap.focus_tid = thread.GetThreadID();
    thread.StepInto(step_in_target.c_str(), run_mode);
  } else {
    response["success"] = llvm::json::Value(false);
  }
  g_dap.SendJSON(llvm::json::Value(std::move(response)));
}

// "StepInTargetsRequest": {
//   "allOf": [ { "$ref": "#/definitions/Request" }, {
//     "type": "object",
//     "description": "This request retrieves the possible step-in targets for
//     the specified stack frame.\nThese targets can be used in the `stepIn`
//     request.\nClients should only call this request if the corresponding
//     capability `supportsStepInTargetsRequest` is true.", "properties": {
//       "command": {
//         "type": "string",
//         "enum": [ "stepInTargets" ]
//       },
//       "arguments": {
//         "$ref": "#/definitions/StepInTargetsArguments"
//       }
//     },
//     "required": [ "command", "arguments"  ]
//   }]
// },
// "StepInTargetsArguments": {
//   "type": "object",
//   "description": "Arguments for `stepInTargets` request.",
//   "properties": {
//     "frameId": {
//       "type": "integer",
//       "description": "The stack frame for which to retrieve the possible
//       step-in targets."
//     }
//   },
//   "required": [ "frameId" ]
// },
// "StepInTargetsResponse": {
//   "allOf": [ { "$ref": "#/definitions/Response" }, {
//     "type": "object",
//     "description": "Response to `stepInTargets` request.",
//     "properties": {
//       "body": {
//         "type": "object",
//         "properties": {
//           "targets": {
//             "type": "array",
//             "items": {
//               "$ref": "#/definitions/StepInTarget"
//             },
//             "description": "The possible step-in targets of the specified
//             source location."
//           }
//         },
//         "required": [ "targets" ]
//       }
//     },
//     "required": [ "body" ]
//   }]
// }
void request_stepInTargets(const llvm::json::Object &request) {
  llvm::json::Object response;
  FillResponse(request, response);
  auto arguments = request.getObject("arguments");

  g_dap.step_in_targets.clear();
  lldb::SBFrame frame = g_dap.GetLLDBFrame(*arguments);
  if (frame.IsValid()) {
    lldb::SBAddress pc_addr = frame.GetPCAddress();
    lldb::SBAddress line_end_addr =
        pc_addr.GetLineEntry().GetSameLineContiguousAddressRangeEnd(true);
    lldb::SBInstructionList insts = g_dap.target.ReadInstructions(
        pc_addr, line_end_addr, /*flavor_string=*/nullptr);

    if (!insts.IsValid()) {
      response["success"] = false;
      response["message"] = "Failed to get instructions for frame.";
      g_dap.SendJSON(llvm::json::Value(std::move(response)));
      return;
    }

    llvm::json::Array step_in_targets;
    const auto num_insts = insts.GetSize();
    for (size_t i = 0; i < num_insts; ++i) {
      lldb::SBInstruction inst = insts.GetInstructionAtIndex(i);
      if (!inst.IsValid())
        break;

      lldb::addr_t inst_addr = inst.GetAddress().GetLoadAddress(g_dap.target);

      // Note: currently only x86/x64 supports flow kind.
      lldb::InstructionControlFlowKind flow_kind =
          inst.GetControlFlowKind(g_dap.target);
      if (flow_kind == lldb::eInstructionControlFlowKindCall) {
        // Use call site instruction address as id which is easy to debug.
        llvm::json::Object step_in_target;
        step_in_target["id"] = inst_addr;

        llvm::StringRef call_operand_name = inst.GetOperands(g_dap.target);
        lldb::addr_t call_target_addr;
        if (call_operand_name.getAsInteger(0, call_target_addr))
          continue;

        lldb::SBAddress call_target_load_addr =
            g_dap.target.ResolveLoadAddress(call_target_addr);
        if (!call_target_load_addr.IsValid())
          continue;

        // The existing ThreadPlanStepInRange only accept step in target
        // function with debug info.
        lldb::SBSymbolContext sc = g_dap.target.ResolveSymbolContextForAddress(
            call_target_load_addr, lldb::eSymbolContextFunction);

        // The existing ThreadPlanStepInRange only accept step in target
        // function with debug info.
        std::string step_in_target_name;
        if (sc.IsValid() && sc.GetFunction().IsValid())
          step_in_target_name = sc.GetFunction().GetDisplayName();

        // Skip call sites if we fail to resolve its symbol name.
        if (step_in_target_name.empty())
          continue;

        g_dap.step_in_targets.try_emplace(inst_addr, step_in_target_name);
        step_in_target.try_emplace("label", step_in_target_name);
        step_in_targets.emplace_back(std::move(step_in_target));
      }
    }
    llvm::json::Object body;
    body.try_emplace("targets", std::move(step_in_targets));
    response.try_emplace("body", std::move(body));
  } else {
    response["success"] = llvm::json::Value(false);
    response["message"] = "Failed to get frame for input frameId.";
  }
  g_dap.SendJSON(llvm::json::Value(std::move(response)));
}

// "StepOutRequest": {
//   "allOf": [ { "$ref": "#/definitions/Request" }, {
//     "type": "object",
//     "description": "StepOut request; value of command field is 'stepOut'. The
//     request starts the debuggee to run again for one step. The debug adapter
//     first sends the StepOutResponse and then a StoppedEvent (event type
//     'step') after the step has completed.", "properties": {
//       "command": {
//         "type": "string",
//         "enum": [ "stepOut" ]
//       },
//       "arguments": {
//         "$ref": "#/definitions/StepOutArguments"
//       }
//     },
//     "required": [ "command", "arguments"  ]
//   }]
// },
// "StepOutArguments": {
//   "type": "object",
//   "description": "Arguments for 'stepOut' request.",
//   "properties": {
//     "threadId": {
//       "type": "integer",
//       "description": "Execute 'stepOut' for this thread."
//     }
//   },
//   "required": [ "threadId" ]
// },
// "StepOutResponse": {
//   "allOf": [ { "$ref": "#/definitions/Response" }, {
//     "type": "object",
//     "description": "Response to 'stepOut' request. This is just an
//     acknowledgement, so no body field is required."
//   }]
// }
void request_stepOut(const llvm::json::Object &request) {
  llvm::json::Object response;
  FillResponse(request, response);
  auto arguments = request.getObject("arguments");
  lldb::SBThread thread = g_dap.GetLLDBThread(*arguments);
  if (thread.IsValid()) {
    // Remember the thread ID that caused the resume so we can set the
    // "threadCausedFocus" boolean value in the "stopped" events.
    g_dap.focus_tid = thread.GetThreadID();
    thread.StepOut();
  } else {
    response["success"] = llvm::json::Value(false);
  }
  g_dap.SendJSON(llvm::json::Value(std::move(response)));
}

// "ThreadsRequest": {
//   "allOf": [ { "$ref": "#/definitions/Request" }, {
//     "type": "object",
//     "description": "Thread request; value of command field is 'threads'. The
//     request retrieves a list of all threads.", "properties": {
//       "command": {
//         "type": "string",
//         "enum": [ "threads" ]
//       }
//     },
//     "required": [ "command" ]
//   }]
// },
// "ThreadsResponse": {
//   "allOf": [ { "$ref": "#/definitions/Response" }, {
//     "type": "object",
//     "description": "Response to 'threads' request.",
//     "properties": {
//       "body": {
//         "type": "object",
//         "properties": {
//           "threads": {
//             "type": "array",
//             "items": {
//               "$ref": "#/definitions/Thread"
//             },
//             "description": "All threads."
//           }
//         },
//         "required": [ "threads" ]
//       }
//     },
//     "required": [ "body" ]
//   }]
// }
void request_threads(const llvm::json::Object &request) {

  lldb::SBProcess process = g_dap.target.GetProcess();
  llvm::json::Object response;
  FillResponse(request, response);

  const uint32_t num_threads = process.GetNumThreads();
  llvm::json::Array threads;
  for (uint32_t thread_idx = 0; thread_idx < num_threads; ++thread_idx) {
    lldb::SBThread thread = process.GetThreadAtIndex(thread_idx);
    threads.emplace_back(CreateThread(thread));
  }
  if (threads.size() == 0) {
    response["success"] = llvm::json::Value(false);
  }
  llvm::json::Object body;
  body.try_emplace("threads", std::move(threads));
  response.try_emplace("body", std::move(body));
  g_dap.SendJSON(llvm::json::Value(std::move(response)));
}

// "SetVariableRequest": {
//   "allOf": [ { "$ref": "#/definitions/Request" }, {
//     "type": "object",
//     "description": "setVariable request; value of command field is
//     'setVariable'. Set the variable with the given name in the variable
//     container to a new value.", "properties": {
//       "command": {
//         "type": "string",
//         "enum": [ "setVariable" ]
//       },
//       "arguments": {
//         "$ref": "#/definitions/SetVariableArguments"
//       }
//     },
//     "required": [ "command", "arguments"  ]
//   }]
// },
// "SetVariableArguments": {
//   "type": "object",
//   "description": "Arguments for 'setVariable' request.",
//   "properties": {
//     "variablesReference": {
//       "type": "integer",
//       "description": "The reference of the variable container."
//     },
//     "name": {
//       "type": "string",
//       "description": "The name of the variable."
//     },
//     "value": {
//       "type": "string",
//       "description": "The value of the variable."
//     },
//     "format": {
//       "$ref": "#/definitions/ValueFormat",
//       "description": "Specifies details on how to format the response value."
//     }
//   },
//   "required": [ "variablesReference", "name", "value" ]
// },
// "SetVariableResponse": {
//   "allOf": [ { "$ref": "#/definitions/Response" }, {
//     "type": "object",
//     "description": "Response to 'setVariable' request.",
//     "properties": {
//       "body": {
//         "type": "object",
//         "properties": {
//           "value": {
//             "type": "string",
//             "description": "The new value of the variable."
//           },
//           "type": {
//             "type": "string",
//             "description": "The type of the new value. Typically shown in the
//             UI when hovering over the value."
//           },
//           "variablesReference": {
//             "type": "number",
//             "description": "If variablesReference is > 0, the new value is
//             structured and its children can be retrieved by passing
//             variablesReference to the VariablesRequest."
//           },
//           "namedVariables": {
//             "type": "number",
//             "description": "The number of named child variables. The client
//             can use this optional information to present the variables in a
//             paged UI and fetch them in chunks."
//           },
//           "indexedVariables": {
//             "type": "number",
//             "description": "The number of indexed child variables. The client
//             can use this optional information to present the variables in a
//             paged UI and fetch them in chunks."
//           }
//         },
//         "required": [ "value" ]
//       }
//     },
//     "required": [ "body" ]
//   }]
// }
void request_setVariable(const llvm::json::Object &request) {
  llvm::json::Object response;
  FillResponse(request, response);
  llvm::json::Array variables;
  llvm::json::Object body;
  auto arguments = request.getObject("arguments");
  // This is a reference to the containing variable/scope
  const auto variablesReference =
      GetUnsigned(arguments, "variablesReference", 0);
  llvm::StringRef name = GetString(arguments, "name");

  const auto value = GetString(arguments, "value");
  // Set success to false just in case we don't find the variable by name
  response.try_emplace("success", false);

  lldb::SBValue variable;
  int64_t newVariablesReference = 0;

  // The "id" is the unique integer ID that is unique within the enclosing
  // variablesReference. It is optionally added to any "interface Variable"
  // objects to uniquely identify a variable within an enclosing
  // variablesReference. It helps to disambiguate between two variables that
  // have the same name within the same scope since the "setVariables" request
  // only specifies the variable reference of the enclosing scope/variable, and
  // the name of the variable. We could have two shadowed variables with the
  // same name in "Locals" or "Globals". In our case the "id" absolute index
  // of the variable within the g_dap.variables list.
  const auto id_value = GetUnsigned(arguments, "id", UINT64_MAX);
  if (id_value != UINT64_MAX) {
    variable = g_dap.variables.GetVariable(id_value);
  } else {
    variable = FindVariable(variablesReference, name);
  }

  if (variable.IsValid()) {
    lldb::SBError error;
    bool success = variable.SetValueFromCString(value.data(), error);
    if (success) {
      VariableDescription desc(variable);
      EmplaceSafeString(body, "result", desc.display_value);
      EmplaceSafeString(body, "type", desc.display_type_name);

      // We don't know the index of the variable in our g_dap.variables
      // so always insert a new one to get its variablesReference.
      // is_permanent is false because debug console does not support
      // setVariable request.
      if (variable.MightHaveChildren())
        newVariablesReference = g_dap.variables.InsertExpandableVariable(
            variable, /*is_permanent=*/false);

      body.try_emplace("variablesReference", newVariablesReference);
    } else {
      EmplaceSafeString(body, "message", std::string(error.GetCString()));
    }
    response["success"] = llvm::json::Value(success);
  } else {
    response["success"] = llvm::json::Value(false);
  }

  response.try_emplace("body", std::move(body));
  g_dap.SendJSON(llvm::json::Value(std::move(response)));
}

// "VariablesRequest": {
//   "allOf": [ { "$ref": "#/definitions/Request" }, {
//     "type": "object",
//     "description": "Variables request; value of command field is 'variables'.
//     Retrieves all child variables for the given variable reference. An
//     optional filter can be used to limit the fetched children to either named
//     or indexed children.", "properties": {
//       "command": {
//         "type": "string",
//         "enum": [ "variables" ]
//       },
//       "arguments": {
//         "$ref": "#/definitions/VariablesArguments"
//       }
//     },
//     "required": [ "command", "arguments"  ]
//   }]
// },
// "VariablesArguments": {
//   "type": "object",
//   "description": "Arguments for 'variables' request.",
//   "properties": {
//     "variablesReference": {
//       "type": "integer",
//       "description": "The Variable reference."
//     },
//     "filter": {
//       "type": "string",
//       "enum": [ "indexed", "named" ],
//       "description": "Optional filter to limit the child variables to either
//       named or indexed. If ommited, both types are fetched."
//     },
//     "start": {
//       "type": "integer",
//       "description": "The index of the first variable to return; if omitted
//       children start at 0."
//     },
//     "count": {
//       "type": "integer",
//       "description": "The number of variables to return. If count is missing
//       or 0, all variables are returned."
//     },
//     "format": {
//       "$ref": "#/definitions/ValueFormat",
//       "description": "Specifies details on how to format the Variable
//       values."
//     }
//   },
//   "required": [ "variablesReference" ]
// },
// "VariablesResponse": {
//   "allOf": [ { "$ref": "#/definitions/Response" }, {
//     "type": "object",
//     "description": "Response to 'variables' request.",
//     "properties": {
//       "body": {
//         "type": "object",
//         "properties": {
//           "variables": {
//             "type": "array",
//             "items": {
//               "$ref": "#/definitions/Variable"
//             },
//             "description": "All (or a range) of variables for the given
//             variable reference."
//           }
//         },
//         "required": [ "variables" ]
//       }
//     },
//     "required": [ "body" ]
//   }]
// }
void request_variables(const llvm::json::Object &request) {
  llvm::json::Object response;
  FillResponse(request, response);
  llvm::json::Array variables;
  auto arguments = request.getObject("arguments");
  const auto variablesReference =
      GetUnsigned(arguments, "variablesReference", 0);
  const int64_t start = GetSigned(arguments, "start", 0);
  const int64_t count = GetSigned(arguments, "count", 0);
  bool hex = false;
  auto format = arguments->getObject("format");
  if (format)
    hex = GetBoolean(format, "hex", false);

  if (lldb::SBValueList *top_scope = GetTopLevelScope(variablesReference)) {
    // variablesReference is one of our scopes, not an actual variable it is
    // asking for the list of args, locals or globals.
    int64_t start_idx = 0;
    int64_t num_children = 0;

    if (variablesReference == VARREF_REGS) {
      // Change the default format of any pointer sized registers in the first
      // register set to be the lldb::eFormatAddressInfo so we show the pointer
      // and resolve what the pointer resolves to. Only change the format if the
      // format was set to the default format or if it was hex as some registers
      // have formats set for them.
      const uint32_t addr_size = g_dap.target.GetProcess().GetAddressByteSize();
      lldb::SBValue reg_set = g_dap.variables.registers.GetValueAtIndex(0);
      const uint32_t num_regs = reg_set.GetNumChildren();
      for (uint32_t reg_idx = 0; reg_idx < num_regs; ++reg_idx) {
        lldb::SBValue reg = reg_set.GetChildAtIndex(reg_idx);
        const lldb::Format format = reg.GetFormat();
        if (format == lldb::eFormatDefault || format == lldb::eFormatHex) {
          if (reg.GetByteSize() == addr_size)
            reg.SetFormat(lldb::eFormatAddressInfo);
        }
      }
    }

    num_children = top_scope->GetSize();
    if (num_children == 0 && variablesReference == VARREF_LOCALS) {
      // Check for an error in the SBValueList that might explain why we don't
      // have locals. If we have an error display it as the sole value in the
      // the locals.

      // "error" owns the error string so we must keep it alive as long as we
      // want to use the returns "const char *"
      lldb::SBError error = top_scope->GetError();
      const char *var_err = error.GetCString();
      if (var_err) {
        // Create a fake variable named "error" to explain why variables were
        // not available. This new error will help let users know when there was
        // a problem that kept variables from being available for display and
        // allow users to fix this issue instead of seeing no variables. The
        // errors are only set when there is a problem that the user could
        // fix, so no error will show up when you have no debug info, only when
        // we do have debug info and something that is fixable can be done.
        llvm::json::Object object;
        EmplaceSafeString(object, "name", "<error>");
        EmplaceSafeString(object, "type", "const char *");
        EmplaceSafeString(object, "value", var_err);
        object.try_emplace("variablesReference", (int64_t)0);
        variables.emplace_back(std::move(object));
      }
    }
    const int64_t end_idx = start_idx + ((count == 0) ? num_children : count);

    // We first find out which variable names are duplicated
    std::map<std::string, int> variable_name_counts;
    for (auto i = start_idx; i < end_idx; ++i) {
      lldb::SBValue variable = top_scope->GetValueAtIndex(i);
      if (!variable.IsValid())
        break;
      variable_name_counts[GetNonNullVariableName(variable)]++;
    }

    // Now we construct the result with unique display variable names
    for (auto i = start_idx; i < end_idx; ++i) {
      lldb::SBValue variable = top_scope->GetValueAtIndex(i);

      if (!variable.IsValid())
        break;

      int64_t var_ref = 0;
      if (variable.MightHaveChildren() || variable.IsSynthetic()) {
        var_ref = g_dap.variables.InsertExpandableVariable(
            variable, /*is_permanent=*/false);
      }
      variables.emplace_back(CreateVariable(
          variable, var_ref, var_ref != 0 ? var_ref : UINT64_MAX, hex,
          variable_name_counts[GetNonNullVariableName(variable)] > 1));
    }
  } else {
    // We are expanding a variable that has children, so we will return its
    // children.
    lldb::SBValue variable = g_dap.variables.GetVariable(variablesReference);
    if (variable.IsValid()) {
      auto addChild = [&](lldb::SBValue child,
                          std::optional<std::string> custom_name = {}) {
        if (!child.IsValid())
          return;
        if (child.MightHaveChildren()) {
          auto is_permanent =
              g_dap.variables.IsPermanentVariableReference(variablesReference);
          auto childVariablesReferences =
              g_dap.variables.InsertExpandableVariable(child, is_permanent);
          variables.emplace_back(CreateVariable(
              child, childVariablesReferences, childVariablesReferences, hex,
              /*is_name_duplicated=*/false, custom_name));
        } else {
          variables.emplace_back(CreateVariable(child, 0, INT64_MAX, hex,
                                                /*is_name_duplicated=*/false,
                                                custom_name));
        }
      };
      const int64_t num_children = variable.GetNumChildren();
      int64_t end_idx = start + ((count == 0) ? num_children : count);
      int64_t i = start;
      for (; i < end_idx && i < num_children; ++i)
        addChild(variable.GetChildAtIndex(i));

      // If we haven't filled the count quota from the request, we insert a new
      // "[raw]" child that can be used to inspect the raw version of a
      // synthetic member. That eliminates the need for the user to go to the
      // debug console and type `frame var <variable> to get these values.
      if (g_dap.enable_synthetic_child_debugging && variable.IsSynthetic() &&
          i == num_children)
        addChild(variable.GetNonSyntheticValue(), "[raw]");
    }
  }
  llvm::json::Object body;
  body.try_emplace("variables", std::move(variables));
  response.try_emplace("body", std::move(body));
  g_dap.SendJSON(llvm::json::Value(std::move(response)));
}

// "DisassembleRequest": {
//   "allOf": [ { "$ref": "#/definitions/Request" }, {
//     "type": "object",
//     "description": "Disassembles code stored at the provided
//     location.\nClients should only call this request if the corresponding
//     capability `supportsDisassembleRequest` is true.", "properties": {
//       "command": {
//         "type": "string",
//         "enum": [ "disassemble" ]
//       },
//       "arguments": {
//         "$ref": "#/definitions/DisassembleArguments"
//       }
//     },
//     "required": [ "command", "arguments" ]
//   }]
// },
// "DisassembleArguments": {
//   "type": "object",
//   "description": "Arguments for `disassemble` request.",
//   "properties": {
//     "memoryReference": {
//       "type": "string",
//       "description": "Memory reference to the base location containing the
//       instructions to disassemble."
//     },
//     "offset": {
//       "type": "integer",
//       "description": "Offset (in bytes) to be applied to the reference
//       location before disassembling. Can be negative."
//     },
//     "instructionOffset": {
//       "type": "integer",
//       "description": "Offset (in instructions) to be applied after the byte
//       offset (if any) before disassembling. Can be negative."
//     },
//     "instructionCount": {
//       "type": "integer",
//       "description": "Number of instructions to disassemble starting at the
//       specified location and offset.\nAn adapter must return exactly this
//       number of instructions - any unavailable instructions should be
//       replaced with an implementation-defined 'invalid instruction' value."
//     },
//     "resolveSymbols": {
//       "type": "boolean",
//       "description": "If true, the adapter should attempt to resolve memory
//       addresses and other values to symbolic names."
//     }
//   },
//   "required": [ "memoryReference", "instructionCount" ]
// },
// "DisassembleResponse": {
//   "allOf": [ { "$ref": "#/definitions/Response" }, {
//     "type": "object",
//     "description": "Response to `disassemble` request.",
//     "properties": {
//       "body": {
//         "type": "object",
//         "properties": {
//           "instructions": {
//             "type": "array",
//             "items": {
//               "$ref": "#/definitions/DisassembledInstruction"
//             },
//             "description": "The list of disassembled instructions."
//           }
//         },
//         "required": [ "instructions" ]
//       }
//     }
//   }]
// }
void request_disassemble(const llvm::json::Object &request) {
  llvm::json::Object response;
  FillResponse(request, response);
  auto arguments = request.getObject("arguments");

  auto memoryReference = GetString(arguments, "memoryReference");
  lldb::addr_t addr_ptr;
  if (memoryReference.consumeInteger(0, addr_ptr)) {
    response["success"] = false;
    response["message"] =
        "Malformed memory reference: " + memoryReference.str();
    g_dap.SendJSON(llvm::json::Value(std::move(response)));
    return;
  }

  addr_ptr += GetSigned(arguments, "instructionOffset", 0);
  lldb::SBAddress addr(addr_ptr, g_dap.target);
  if (!addr.IsValid()) {
    response["success"] = false;
    response["message"] = "Memory reference not found in the current binary.";
    g_dap.SendJSON(llvm::json::Value(std::move(response)));
    return;
  }

  const auto inst_count = GetUnsigned(arguments, "instructionCount", 0);
  lldb::SBInstructionList insts =
      g_dap.target.ReadInstructions(addr, inst_count);

  if (!insts.IsValid()) {
    response["success"] = false;
    response["message"] = "Failed to find instructions for memory address.";
    g_dap.SendJSON(llvm::json::Value(std::move(response)));
    return;
  }

  const bool resolveSymbols = GetBoolean(arguments, "resolveSymbols", false);
  llvm::json::Array instructions;
  const auto num_insts = insts.GetSize();
  for (size_t i = 0; i < num_insts; ++i) {
    lldb::SBInstruction inst = insts.GetInstructionAtIndex(i);
    auto addr = inst.GetAddress();
    const auto inst_addr = addr.GetLoadAddress(g_dap.target);
    const char *m = inst.GetMnemonic(g_dap.target);
    const char *o = inst.GetOperands(g_dap.target);
    const char *c = inst.GetComment(g_dap.target);
    auto d = inst.GetData(g_dap.target);

    std::string bytes;
    llvm::raw_string_ostream sb(bytes);
    for (unsigned i = 0; i < inst.GetByteSize(); i++) {
      lldb::SBError error;
      uint8_t b = d.GetUnsignedInt8(error, i);
      if (error.Success()) {
        sb << llvm::format("%2.2x ", b);
      }
    }
    sb.flush();

    llvm::json::Object disassembled_inst{
        {"address", "0x" + llvm::utohexstr(inst_addr)},
        {"instructionBytes",
         bytes.size() > 0 ? bytes.substr(0, bytes.size() - 1) : ""},
    };

    std::string instruction;
    llvm::raw_string_ostream si(instruction);

    lldb::SBSymbol symbol = addr.GetSymbol();
    // Only add the symbol on the first line of the function.
    if (symbol.IsValid() && symbol.GetStartAddress() == addr) {
      // If we have a valid symbol, append it as a label prefix for the first
      // instruction. This is so you can see the start of a function/callsite
      // in the assembly, at the moment VS Code (1.80) does not visualize the
      // symbol associated with the assembly instruction.
      si << (symbol.GetMangledName() != nullptr ? symbol.GetMangledName()
                                                : symbol.GetName())
         << ": ";

      if (resolveSymbols) {
        disassembled_inst.try_emplace("symbol", symbol.GetDisplayName());
      }
    }

    si << llvm::formatv("{0,7} {1,12}", m, o);
    if (c && c[0]) {
      si << " ; " << c;
    }
    si.flush();

    disassembled_inst.try_emplace("instruction", instruction);

    auto line_entry = addr.GetLineEntry();
    // If the line number is 0 then the entry represents a compiler generated
    // location.
    if (line_entry.GetStartAddress() == addr && line_entry.IsValid() &&
        line_entry.GetFileSpec().IsValid() && line_entry.GetLine() != 0) {
      auto source = CreateSource(line_entry);
      disassembled_inst.try_emplace("location", source);

      const auto line = line_entry.GetLine();
      if (line && line != LLDB_INVALID_LINE_NUMBER) {
        disassembled_inst.try_emplace("line", line);
      }
      const auto column = line_entry.GetColumn();
      if (column && column != LLDB_INVALID_COLUMN_NUMBER) {
        disassembled_inst.try_emplace("column", column);
      }

      auto end_line_entry = line_entry.GetEndAddress().GetLineEntry();
      if (end_line_entry.IsValid() &&
          end_line_entry.GetFileSpec() == line_entry.GetFileSpec()) {
        const auto end_line = end_line_entry.GetLine();
        if (end_line && end_line != LLDB_INVALID_LINE_NUMBER &&
            end_line != line) {
          disassembled_inst.try_emplace("endLine", end_line);

          const auto end_column = end_line_entry.GetColumn();
          if (end_column && end_column != LLDB_INVALID_COLUMN_NUMBER &&
              end_column != column) {
            disassembled_inst.try_emplace("endColumn", end_column - 1);
          }
        }
      }
    }

    instructions.emplace_back(std::move(disassembled_inst));
  }

  llvm::json::Object body;
  body.try_emplace("instructions", std::move(instructions));
  response.try_emplace("body", std::move(body));
  g_dap.SendJSON(llvm::json::Value(std::move(response)));
}
// A request used in testing to get the details on all breakpoints that are
// currently set in the target. This helps us to test "setBreakpoints" and
// "setFunctionBreakpoints" requests to verify we have the correct set of
// breakpoints currently set in LLDB.
void request__testGetTargetBreakpoints(const llvm::json::Object &request) {
  llvm::json::Object response;
  FillResponse(request, response);
  llvm::json::Array response_breakpoints;
  for (uint32_t i = 0; g_dap.target.GetBreakpointAtIndex(i).IsValid(); ++i) {
    auto bp = Breakpoint(g_dap.target.GetBreakpointAtIndex(i));
    AppendBreakpoint(&bp, response_breakpoints);
  }
  llvm::json::Object body;
  body.try_emplace("breakpoints", std::move(response_breakpoints));
  response.try_emplace("body", std::move(body));
  g_dap.SendJSON(llvm::json::Value(std::move(response)));
}

void RegisterRequestCallbacks() {
  g_dap.RegisterRequestCallback("attach", request_attach);
  g_dap.RegisterRequestCallback("completions", request_completions);
  g_dap.RegisterRequestCallback("continue", request_continue);
  g_dap.RegisterRequestCallback("configurationDone", request_configurationDone);
  g_dap.RegisterRequestCallback("disconnect", request_disconnect);
  g_dap.RegisterRequestCallback("evaluate", request_evaluate);
  g_dap.RegisterRequestCallback("exceptionInfo", request_exceptionInfo);
  g_dap.RegisterRequestCallback("initialize", request_initialize);
  g_dap.RegisterRequestCallback("launch", request_launch);
  g_dap.RegisterRequestCallback("next", request_next);
  g_dap.RegisterRequestCallback("pause", request_pause);
  g_dap.RegisterRequestCallback("restart", request_restart);
  g_dap.RegisterRequestCallback("scopes", request_scopes);
  g_dap.RegisterRequestCallback("setBreakpoints", request_setBreakpoints);
  g_dap.RegisterRequestCallback("setExceptionBreakpoints",
                                request_setExceptionBreakpoints);
  g_dap.RegisterRequestCallback("setFunctionBreakpoints",
                                request_setFunctionBreakpoints);
  g_dap.RegisterRequestCallback("dataBreakpointInfo",
                                request_dataBreakpointInfo);
  g_dap.RegisterRequestCallback("setDataBreakpoints",
                                request_setDataBreakpoints);
  g_dap.RegisterRequestCallback("setVariable", request_setVariable);
  g_dap.RegisterRequestCallback("source", request_source);
  g_dap.RegisterRequestCallback("stackTrace", request_stackTrace);
  g_dap.RegisterRequestCallback("stepIn", request_stepIn);
  g_dap.RegisterRequestCallback("stepInTargets", request_stepInTargets);
  g_dap.RegisterRequestCallback("stepOut", request_stepOut);
  g_dap.RegisterRequestCallback("threads", request_threads);
  g_dap.RegisterRequestCallback("variables", request_variables);
  g_dap.RegisterRequestCallback("disassemble", request_disassemble);
  // Custom requests
  g_dap.RegisterRequestCallback("compileUnits", request_compileUnits);
  g_dap.RegisterRequestCallback("modules", request_modules);
  // Testing requests
  g_dap.RegisterRequestCallback("_testGetTargetBreakpoints",
                                request__testGetTargetBreakpoints);
}

} // anonymous namespace

static void printHelp(LLDBDAPOptTable &table, llvm::StringRef tool_name) {
  std::string usage_str = tool_name.str() + " options";
  table.printHelp(llvm::outs(), usage_str.c_str(), "LLDB DAP", false);

  std::string examples = R"___(
EXAMPLES:
  The debug adapter can be started in two modes.

  Running lldb-dap without any arguments will start communicating with the
  parent over stdio. Passing a port number causes lldb-dap to start listening
  for connections on that port.

    lldb-dap -p <port>

  Passing --wait-for-debugger will pause the process at startup and wait for a
  debugger to attach to the process.

    lldb-dap -g
)___";
  llvm::outs() << examples;
}

// If --launch-target is provided, this instance of lldb-dap becomes a
// runInTerminal launcher. It will ultimately launch the program specified in
// the --launch-target argument, which is the original program the user wanted
// to debug. This is done in such a way that the actual debug adaptor can
// place breakpoints at the beginning of the program.
//
// The launcher will communicate with the debug adaptor using a fifo file in the
// directory specified in the --comm-file argument.
//
// Regarding the actual flow, this launcher will first notify the debug adaptor
// of its pid. Then, the launcher will be in a pending state waiting to be
// attached by the adaptor.
//
// Once attached and resumed, the launcher will exec and become the program
// specified by --launch-target, which is the original target the
// user wanted to run.
//
// In case of errors launching the target, a suitable error message will be
// emitted to the debug adaptor.
void LaunchRunInTerminalTarget(llvm::opt::Arg &target_arg,
                               llvm::StringRef comm_file,
                               lldb::pid_t debugger_pid, char *argv[]) {
#if defined(_WIN32)
  llvm::errs() << "runInTerminal is only supported on POSIX systems\n";
  exit(EXIT_FAILURE);
#else

  // On Linux with the Yama security module enabled, a process can only attach
  // to its descendants by default. In the runInTerminal case the target
  // process is launched by the client so we need to allow tracing explicitly.
#if defined(__linux__)
  if (debugger_pid != LLDB_INVALID_PROCESS_ID)
    (void)prctl(PR_SET_PTRACER, debugger_pid, 0, 0, 0);
#endif

  RunInTerminalLauncherCommChannel comm_channel(comm_file);
  if (llvm::Error err = comm_channel.NotifyPid()) {
    llvm::errs() << llvm::toString(std::move(err)) << "\n";
    exit(EXIT_FAILURE);
  }

  // We will wait to be attached with a timeout. We don't wait indefinitely
  // using a signal to prevent being paused forever.

  // This env var should be used only for tests.
  const char *timeout_env_var = getenv("LLDB_DAP_RIT_TIMEOUT_IN_MS");
  int timeout_in_ms =
      timeout_env_var != nullptr ? atoi(timeout_env_var) : 20000;
  if (llvm::Error err = comm_channel.WaitUntilDebugAdaptorAttaches(
          std::chrono::milliseconds(timeout_in_ms))) {
    llvm::errs() << llvm::toString(std::move(err)) << "\n";
    exit(EXIT_FAILURE);
  }

  const char *target = target_arg.getValue();
  execvp(target, argv);

  std::string error = std::strerror(errno);
  comm_channel.NotifyError(error);
  llvm::errs() << error << "\n";
  exit(EXIT_FAILURE);
#endif
}

/// used only by TestVSCode_redirection_to_console.py
void redirection_test() {
  printf("stdout message\n");
  fprintf(stderr, "stderr message\n");
  fflush(stdout);
  fflush(stderr);
}

/// Redirect stdout and stderr fo the IDE's console output.
///
/// Errors in this operation will be printed to the log file and the IDE's
/// console output as well.
///
/// \return
///     A fd pointing to the original stdout.
int SetupStdoutStderrRedirection() {
  int stdoutfd = fileno(stdout);
  int new_stdout_fd = dup(stdoutfd);
  auto output_callback_stderr = [](llvm::StringRef data) {
    g_dap.SendOutput(OutputType::Stderr, data);
  };
  auto output_callback_stdout = [](llvm::StringRef data) {
    g_dap.SendOutput(OutputType::Stdout, data);
  };
  if (llvm::Error err = RedirectFd(stdoutfd, output_callback_stdout)) {
    std::string error_message = llvm::toString(std::move(err));
    if (g_dap.log)
      *g_dap.log << error_message << std::endl;
    output_callback_stderr(error_message);
  }
  if (llvm::Error err = RedirectFd(fileno(stderr), output_callback_stderr)) {
    std::string error_message = llvm::toString(std::move(err));
    if (g_dap.log)
      *g_dap.log << error_message << std::endl;
    output_callback_stderr(error_message);
  }

  /// used only by TestVSCode_redirection_to_console.py
  if (getenv("LLDB_DAP_TEST_STDOUT_STDERR_REDIRECTION") != nullptr)
    redirection_test();
  return new_stdout_fd;
}

int main(int argc, char *argv[]) {
  llvm::InitLLVM IL(argc, argv, /*InstallPipeSignalExitHandler=*/false);
#if !defined(__APPLE__)
  llvm::setBugReportMsg("PLEASE submit a bug report to " LLDB_BUG_REPORT_URL
                        " and include the crash backtrace.\n");
#else
  llvm::setBugReportMsg("PLEASE submit a bug report to " LLDB_BUG_REPORT_URL
                        " and include the crash report from "
                        "~/Library/Logs/DiagnosticReports/.\n");
#endif

  llvm::SmallString<256> program_path(argv[0]);
  llvm::sys::fs::make_absolute(program_path);
  g_dap.debug_adaptor_path = program_path.str().str();

  LLDBDAPOptTable T;
  unsigned MAI, MAC;
  llvm::ArrayRef<const char *> ArgsArr = llvm::ArrayRef(argv + 1, argc);
  llvm::opt::InputArgList input_args = T.ParseArgs(ArgsArr, MAI, MAC);

  if (input_args.hasArg(OPT_help)) {
    printHelp(T, llvm::sys::path::filename(argv[0]));
    return EXIT_SUCCESS;
  }

  if (input_args.hasArg(OPT_repl_mode)) {
    llvm::opt::Arg *repl_mode = input_args.getLastArg(OPT_repl_mode);
    llvm::StringRef repl_mode_value = repl_mode->getValue();
    if (repl_mode_value == "auto") {
      g_dap.repl_mode = ReplMode::Auto;
    } else if (repl_mode_value == "variable") {
      g_dap.repl_mode = ReplMode::Variable;
    } else if (repl_mode_value == "command") {
      g_dap.repl_mode = ReplMode::Command;
    } else {
      llvm::errs()
          << "'" << repl_mode_value
          << "' is not a valid option, use 'variable', 'command' or 'auto'.\n";
      return EXIT_FAILURE;
    }
  }

  if (llvm::opt::Arg *target_arg = input_args.getLastArg(OPT_launch_target)) {
    if (llvm::opt::Arg *comm_file = input_args.getLastArg(OPT_comm_file)) {
      lldb::pid_t pid = LLDB_INVALID_PROCESS_ID;
      llvm::opt::Arg *debugger_pid = input_args.getLastArg(OPT_debugger_pid);
      if (debugger_pid) {
        llvm::StringRef debugger_pid_value = debugger_pid->getValue();
        if (debugger_pid_value.getAsInteger(10, pid)) {
          llvm::errs() << "'" << debugger_pid_value
                       << "' is not a valid "
                          "PID\n";
          return EXIT_FAILURE;
        }
      }
      int target_args_pos = argc;
      for (int i = 0; i < argc; i++)
        if (strcmp(argv[i], "--launch-target") == 0) {
          target_args_pos = i + 1;
          break;
        }
      LaunchRunInTerminalTarget(*target_arg, comm_file->getValue(), pid,
                                argv + target_args_pos);
    } else {
      llvm::errs() << "\"--launch-target\" requires \"--comm-file\" to be "
                      "specified\n";
      return EXIT_FAILURE;
    }
  }

  // stdout/stderr redirection to the IDE's console
  int new_stdout_fd = SetupStdoutStderrRedirection();

  // Initialize LLDB first before we do anything.
  lldb::SBDebugger::Initialize();

  // Terminate the debugger before the C++ destructor chain kicks in.
  auto terminate_debugger =
      llvm::make_scope_exit([] { lldb::SBDebugger::Terminate(); });

  RegisterRequestCallbacks();

  int portno = -1;

  if (auto *arg = input_args.getLastArg(OPT_port)) {
    auto optarg = arg->getValue();
    char *remainder;
    portno = strtol(optarg, &remainder, 0);
    if (remainder == optarg || *remainder != '\0') {
      fprintf(stderr, "'%s' is not a valid port number.\n", optarg);
      return EXIT_FAILURE;
    }
  }

#if !defined(_WIN32)
  if (input_args.hasArg(OPT_wait_for_debugger)) {
    printf("Paused waiting for debugger to attach (pid = %i)...\n", getpid());
    pause();
  }
#endif
  if (portno != -1) {
    printf("Listening on port %i...\n", portno);
    SOCKET socket_fd = AcceptConnection(portno);
    if (socket_fd >= 0) {
      g_dap.input.descriptor = StreamDescriptor::from_socket(socket_fd, true);
      g_dap.output.descriptor = StreamDescriptor::from_socket(socket_fd, false);
    } else {
      return EXIT_FAILURE;
    }
  } else {
    g_dap.input.descriptor = StreamDescriptor::from_file(fileno(stdin), false);
    g_dap.output.descriptor = StreamDescriptor::from_file(new_stdout_fd, false);
  }

  for (const std::string &arg :
       input_args.getAllArgValues(OPT_pre_init_command)) {
    g_dap.pre_init_commands.push_back(arg);
  }

  bool CleanExit = true;
  if (auto Err = g_dap.Loop()) {
    if (g_dap.log)
      *g_dap.log << "Transport Error: " << llvm::toString(std::move(Err))
                 << "\n";
    CleanExit = false;
  }

  return CleanExit ? EXIT_SUCCESS : EXIT_FAILURE;
}
