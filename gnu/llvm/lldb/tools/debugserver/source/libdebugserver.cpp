//===-- libdebugserver.cpp --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <cerrno>
#include <getopt.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/types.h>

#include <memory>

#include "DNB.h"
#include "DNBLog.h"
#include "DNBTimer.h"
#include "PseudoTerminal.h"
#include "RNBContext.h"
#include "RNBRemote.h"
#include "RNBServices.h"
#include "RNBSocket.h"
#include "SysSignal.h"

// Run loop modes which determine which run loop function will be called
enum RNBRunLoopMode {
  eRNBRunLoopModeInvalid = 0,
  eRNBRunLoopModeGetStartModeFromRemoteProtocol,
  eRNBRunLoopModeInferiorExecuting,
  eRNBRunLoopModeExit
};

// Global Variables
RNBRemoteSP g_remoteSP;
int g_disable_aslr = 0;
int g_isatty = 0;

#define RNBLogSTDOUT(fmt, ...)                                                 \
  do {                                                                         \
    if (g_isatty) {                                                            \
      fprintf(stdout, fmt, ##__VA_ARGS__);                                     \
    } else {                                                                   \
      _DNBLog(0, fmt, ##__VA_ARGS__);                                          \
    }                                                                          \
  } while (0)
#define RNBLogSTDERR(fmt, ...)                                                 \
  do {                                                                         \
    if (g_isatty) {                                                            \
      fprintf(stderr, fmt, ##__VA_ARGS__);                                     \
    } else {                                                                   \
      _DNBLog(0, fmt, ##__VA_ARGS__);                                          \
    }                                                                          \
  } while (0)

// Get our program path and arguments from the remote connection.
// We will need to start up the remote connection without a PID, get the
// arguments, wait for the new process to finish launching and hit its
// entry point,  and then return the run loop mode that should come next.
RNBRunLoopMode RNBRunLoopGetStartModeFromRemote(RNBRemoteSP &remoteSP) {
  std::string packet;

  if (remoteSP.get() != NULL) {
    RNBRemote *remote = remoteSP.get();
    RNBContext &ctx = remote->Context();
    uint32_t event_mask = RNBContext::event_read_packet_available;

    // Spin waiting to get the A packet.
    while (true) {
      DNBLogThreadedIf(LOG_RNB_MAX,
                       "%s ctx.Events().WaitForSetEvents( 0x%08x ) ...",
                       __FUNCTION__, event_mask);
      nub_event_t set_events = ctx.Events().WaitForSetEvents(event_mask);
      DNBLogThreadedIf(LOG_RNB_MAX,
                       "%s ctx.Events().WaitForSetEvents( 0x%08x ) => 0x%08x",
                       __FUNCTION__, event_mask, set_events);

      if (set_events & RNBContext::event_read_packet_available) {
        rnb_err_t err = rnb_err;
        RNBRemote::PacketEnum type;

        err = remote->HandleReceivedPacket(&type);

        // check if we tried to attach to a process
        if (type == RNBRemote::vattach || type == RNBRemote::vattachwait) {
          if (err == rnb_success)
            return eRNBRunLoopModeInferiorExecuting;
          else {
            RNBLogSTDERR("error: attach failed.");
            return eRNBRunLoopModeExit;
          }
        }

        if (err == rnb_success) {
          DNBLogThreadedIf(LOG_RNB_MINIMAL, "%s Got success...", __FUNCTION__);
          continue;
        } else if (err == rnb_not_connected) {
          RNBLogSTDERR("error: connection lost.");
          return eRNBRunLoopModeExit;
        } else {
          // a catch all for any other gdb remote packets that failed
          DNBLogThreadedIf(LOG_RNB_MINIMAL, "%s Error getting packet.",
                           __FUNCTION__);
          continue;
        }

        DNBLogThreadedIf(LOG_RNB_MINIMAL, "#### %s", __FUNCTION__);
      } else {
        DNBLogThreadedIf(LOG_RNB_MINIMAL,
                         "%s Connection closed before getting \"A\" packet.",
                         __FUNCTION__);
        return eRNBRunLoopModeExit;
      }
    }
  }
  return eRNBRunLoopModeExit;
}

// Watch for signals:
// SIGINT: so we can halt our inferior. (disabled for now)
// SIGPIPE: in case our child process dies
nub_process_t g_pid;
int g_sigpipe_received = 0;
void signal_handler(int signo) {
  DNBLogThreadedIf(LOG_RNB_MINIMAL, "%s (%s)", __FUNCTION__,
                   SysSignal::Name(signo));

  switch (signo) {
  //  case SIGINT:
  //      DNBProcessKill (g_pid, signo);
  //      break;

  case SIGPIPE:
    g_sigpipe_received = 1;
    break;
  }
}

// Return the new run loop mode based off of the current process state
RNBRunLoopMode HandleProcessStateChange(RNBRemoteSP &remote, bool initialize) {
  RNBContext &ctx = remote->Context();
  nub_process_t pid = ctx.ProcessID();

  if (pid == INVALID_NUB_PROCESS) {
    DNBLogThreadedIf(LOG_RNB_MINIMAL, "#### %s error: pid invalid, exiting...",
                     __FUNCTION__);
    return eRNBRunLoopModeExit;
  }
  nub_state_t pid_state = DNBProcessGetState(pid);

  DNBLogThreadedIf(LOG_RNB_MINIMAL,
                   "%s (&remote, initialize=%i)  pid_state = %s", __FUNCTION__,
                   (int)initialize, DNBStateAsString(pid_state));

  switch (pid_state) {
  case eStateInvalid:
  case eStateUnloaded:
    // Something bad happened
    return eRNBRunLoopModeExit;
    break;

  case eStateAttaching:
  case eStateLaunching:
    return eRNBRunLoopModeInferiorExecuting;

  case eStateSuspended:
  case eStateCrashed:
  case eStateStopped:
    if (!initialize) {
      // Compare the last stop count to our current notion of a stop count
      // to make sure we don't notify more than once for a given stop.
      nub_size_t prev_pid_stop_count = ctx.GetProcessStopCount();
      bool pid_stop_count_changed =
          ctx.SetProcessStopCount(DNBProcessGetStopCount(pid));
      if (pid_stop_count_changed) {
        remote->FlushSTDIO();

        if (ctx.GetProcessStopCount() == 1) {
          DNBLogThreadedIf(
              LOG_RNB_MINIMAL, "%s (&remote, initialize=%i)  pid_state = %s "
                               "pid_stop_count %zu (old %zu)) Notify??? no, "
                               "first stop...",
              __FUNCTION__, (int)initialize, DNBStateAsString(pid_state),
              ctx.GetProcessStopCount(), prev_pid_stop_count);
        } else {

          DNBLogThreadedIf(
              LOG_RNB_MINIMAL, "%s (&remote, initialize=%i)  pid_state = %s "
                               "pid_stop_count %zu (old %zu)) Notify??? YES!!!",
              __FUNCTION__, (int)initialize, DNBStateAsString(pid_state),
              ctx.GetProcessStopCount(), prev_pid_stop_count);
          remote->NotifyThatProcessStopped();
        }
      } else {
        DNBLogThreadedIf(LOG_RNB_MINIMAL, "%s (&remote, initialize=%i)  "
                                          "pid_state = %s pid_stop_count %zu "
                                          "(old %zu)) Notify??? skipping...",
                         __FUNCTION__, (int)initialize,
                         DNBStateAsString(pid_state), ctx.GetProcessStopCount(),
                         prev_pid_stop_count);
      }
    }
    return eRNBRunLoopModeInferiorExecuting;

  case eStateStepping:
  case eStateRunning:
    return eRNBRunLoopModeInferiorExecuting;

  case eStateExited:
    remote->HandlePacket_last_signal(NULL);
    return eRNBRunLoopModeExit;
  case eStateDetached:
    return eRNBRunLoopModeExit;
  }

  // Catch all...
  return eRNBRunLoopModeExit;
}
// This function handles the case where our inferior program is stopped and
// we are waiting for gdb remote protocol packets. When a packet occurs that
// makes the inferior run, we need to leave this function with a new state
// as the return code.
RNBRunLoopMode RNBRunLoopInferiorExecuting(RNBRemoteSP &remote) {
  DNBLogThreadedIf(LOG_RNB_MINIMAL, "#### %s", __FUNCTION__);
  RNBContext &ctx = remote->Context();

  // Init our mode and set 'is_running' based on the current process state
  RNBRunLoopMode mode = HandleProcessStateChange(remote, true);

  while (ctx.ProcessID() != INVALID_NUB_PROCESS) {

    std::string set_events_str;
    uint32_t event_mask = ctx.NormalEventBits();

    if (!ctx.ProcessStateRunning()) {
      // Clear the stdio bits if we are not running so we don't send any async
      // packets
      event_mask &= ~RNBContext::event_proc_stdio_available;
    }

    // We want to make sure we consume all process state changes and have
    // whomever is notifying us to wait for us to reset the event bit before
    // continuing.
    // ctx.Events().SetResetAckMask (RNBContext::event_proc_state_changed);

    DNBLogThreadedIf(LOG_RNB_EVENTS,
                     "%s ctx.Events().WaitForSetEvents(0x%08x) ...",
                     __FUNCTION__, event_mask);
    nub_event_t set_events = ctx.Events().WaitForSetEvents(event_mask);
    DNBLogThreadedIf(LOG_RNB_EVENTS,
                     "%s ctx.Events().WaitForSetEvents(0x%08x) => 0x%08x (%s)",
                     __FUNCTION__, event_mask, set_events,
                     ctx.EventsAsString(set_events, set_events_str));

    if (set_events) {
      if ((set_events & RNBContext::event_proc_thread_exiting) ||
          (set_events & RNBContext::event_proc_stdio_available)) {
        remote->FlushSTDIO();
      }

      if (set_events & RNBContext::event_read_packet_available) {
        // handleReceivedPacket will take care of resetting the
        // event_read_packet_available events when there are no more...
        set_events ^= RNBContext::event_read_packet_available;

        if (ctx.ProcessStateRunning()) {
          if (remote->HandleAsyncPacket() == rnb_not_connected) {
            // TODO: connect again? Exit?
          }
        } else {
          if (remote->HandleReceivedPacket() == rnb_not_connected) {
            // TODO: connect again? Exit?
          }
        }
      }

      if (set_events & RNBContext::event_proc_state_changed) {
        mode = HandleProcessStateChange(remote, false);
        ctx.Events().ResetEvents(RNBContext::event_proc_state_changed);
        set_events ^= RNBContext::event_proc_state_changed;
      }

      if (set_events & RNBContext::event_proc_thread_exiting) {
        mode = eRNBRunLoopModeExit;
      }

      if (set_events & RNBContext::event_read_thread_exiting) {
        // Out remote packet receiving thread exited, exit for now.
        if (ctx.HasValidProcessID()) {
          // TODO: We should add code that will leave the current process
          // in its current state and listen for another connection...
          if (ctx.ProcessStateRunning()) {
            DNBProcessKill(ctx.ProcessID());
          }
        }
        mode = eRNBRunLoopModeExit;
      }
    }

    // Reset all event bits that weren't reset for now...
    if (set_events != 0)
      ctx.Events().ResetEvents(set_events);

    if (mode != eRNBRunLoopModeInferiorExecuting)
      break;
  }

  return mode;
}

void ASLLogCallback(void *baton, uint32_t flags, const char *format,
                    va_list args) {
#if 0
	vprintf(format, args);
#endif
}

extern "C" int debug_server_main(int fd) {
#if 1
  g_isatty = 0;
#else
  g_isatty = ::isatty(STDIN_FILENO);

  DNBLogSetDebug(1);
  DNBLogSetVerbose(1);
  DNBLogSetLogMask(-1);
  DNBLogSetLogCallback(ASLLogCallback, NULL);
#endif

  signal(SIGPIPE, signal_handler);

  g_remoteSP = std::make_shared<RNBRemote>();

  RNBRemote *remote = g_remoteSP.get();
  if (remote == NULL) {
    RNBLogSTDERR("error: failed to create a remote connection class\n");
    return -1;
  }

  RNBRunLoopMode mode = eRNBRunLoopModeGetStartModeFromRemoteProtocol;

  while (mode != eRNBRunLoopModeExit) {
    switch (mode) {
    case eRNBRunLoopModeGetStartModeFromRemoteProtocol:
      if (g_remoteSP->Comm().useFD(fd) == rnb_success) {
        RNBLogSTDOUT("Starting remote data thread.\n");
        g_remoteSP->StartReadRemoteDataThread();

        RNBLogSTDOUT("Waiting for start mode from remote.\n");
        mode = RNBRunLoopGetStartModeFromRemote(g_remoteSP);
      } else {
        mode = eRNBRunLoopModeExit;
      }
      break;

    case eRNBRunLoopModeInferiorExecuting:
      mode = RNBRunLoopInferiorExecuting(g_remoteSP);
      break;

    default:
      mode = eRNBRunLoopModeExit;
      break;

    case eRNBRunLoopModeExit:
      break;
    }
  }

  g_remoteSP->StopReadRemoteDataThread();
  g_remoteSP->Context().SetProcessID(INVALID_NUB_PROCESS);

  return 0;
}
