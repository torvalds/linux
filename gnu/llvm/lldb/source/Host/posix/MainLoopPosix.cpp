//===-- MainLoopPosix.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/posix/MainLoopPosix.h"
#include "lldb/Host/Config.h"
#include "lldb/Host/PosixApi.h"
#include "lldb/Utility/Status.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Errno.h"
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <csignal>
#include <ctime>
#include <vector>

// Multiplexing is implemented using kqueue on systems that support it (BSD
// variants including OSX). On linux we use ppoll, while android uses pselect
// (ppoll is present but not implemented properly). On windows we use WSApoll
// (which does not support signals).

#if HAVE_SYS_EVENT_H
#include <sys/event.h>
#elif defined(__ANDROID__)
#include <sys/syscall.h>
#else
#include <poll.h>
#endif

using namespace lldb;
using namespace lldb_private;

static sig_atomic_t g_signal_flags[NSIG];

static void SignalHandler(int signo, siginfo_t *info, void *) {
  assert(signo < NSIG);
  g_signal_flags[signo] = 1;
}

class MainLoopPosix::RunImpl {
public:
  RunImpl(MainLoopPosix &loop);
  ~RunImpl() = default;

  Status Poll();
  void ProcessEvents();

private:
  MainLoopPosix &loop;

#if HAVE_SYS_EVENT_H
  std::vector<struct kevent> in_events;
  struct kevent out_events[4];
  int num_events = -1;

#else
#ifdef __ANDROID__
  fd_set read_fd_set;
#else
  std::vector<struct pollfd> read_fds;
#endif

  sigset_t get_sigmask();
#endif
};

#if HAVE_SYS_EVENT_H
MainLoopPosix::RunImpl::RunImpl(MainLoopPosix &loop) : loop(loop) {
  in_events.reserve(loop.m_read_fds.size());
}

Status MainLoopPosix::RunImpl::Poll() {
  in_events.resize(loop.m_read_fds.size());
  unsigned i = 0;
  for (auto &fd : loop.m_read_fds)
    EV_SET(&in_events[i++], fd.first, EVFILT_READ, EV_ADD, 0, 0, 0);

  num_events = kevent(loop.m_kqueue, in_events.data(), in_events.size(),
                      out_events, std::size(out_events), nullptr);

  if (num_events < 0) {
    if (errno == EINTR) {
      // in case of EINTR, let the main loop run one iteration
      // we need to zero num_events to avoid assertions failing
      num_events = 0;
    } else
      return Status(errno, eErrorTypePOSIX);
  }
  return Status();
}

void MainLoopPosix::RunImpl::ProcessEvents() {
  assert(num_events >= 0);
  for (int i = 0; i < num_events; ++i) {
    if (loop.m_terminate_request)
      return;
    switch (out_events[i].filter) {
    case EVFILT_READ:
      loop.ProcessReadObject(out_events[i].ident);
      break;
    case EVFILT_SIGNAL:
      loop.ProcessSignal(out_events[i].ident);
      break;
    default:
      llvm_unreachable("Unknown event");
    }
  }
}
#else
MainLoopPosix::RunImpl::RunImpl(MainLoopPosix &loop) : loop(loop) {
#ifndef __ANDROID__
  read_fds.reserve(loop.m_read_fds.size());
#endif
}

sigset_t MainLoopPosix::RunImpl::get_sigmask() {
  sigset_t sigmask;
  int ret = pthread_sigmask(SIG_SETMASK, nullptr, &sigmask);
  assert(ret == 0);
  UNUSED_IF_ASSERT_DISABLED(ret);

  for (const auto &sig : loop.m_signals)
    sigdelset(&sigmask, sig.first);
  return sigmask;
}

#ifdef __ANDROID__
Status MainLoopPosix::RunImpl::Poll() {
  // ppoll(2) is not supported on older all android versions. Also, older
  // versions android (API <= 19) implemented pselect in a non-atomic way, as a
  // combination of pthread_sigmask and select. This is not sufficient for us,
  // as we rely on the atomicity to correctly implement signal polling, so we
  // call the underlying syscall ourselves.

  FD_ZERO(&read_fd_set);
  int nfds = 0;
  for (const auto &fd : loop.m_read_fds) {
    FD_SET(fd.first, &read_fd_set);
    nfds = std::max(nfds, fd.first + 1);
  }

  union {
    sigset_t set;
    uint64_t pad;
  } kernel_sigset;
  memset(&kernel_sigset, 0, sizeof(kernel_sigset));
  kernel_sigset.set = get_sigmask();

  struct {
    void *sigset_ptr;
    size_t sigset_len;
  } extra_data = {&kernel_sigset, sizeof(kernel_sigset)};
  if (syscall(__NR_pselect6, nfds, &read_fd_set, nullptr, nullptr, nullptr,
              &extra_data) == -1) {
    if (errno != EINTR)
      return Status(errno, eErrorTypePOSIX);
    else
      FD_ZERO(&read_fd_set);
  }

  return Status();
}
#else
Status MainLoopPosix::RunImpl::Poll() {
  read_fds.clear();

  sigset_t sigmask = get_sigmask();

  for (const auto &fd : loop.m_read_fds) {
    struct pollfd pfd;
    pfd.fd = fd.first;
    pfd.events = POLLIN;
    pfd.revents = 0;
    read_fds.push_back(pfd);
  }

  if (ppoll(read_fds.data(), read_fds.size(), nullptr, &sigmask) == -1 &&
      errno != EINTR)
    return Status(errno, eErrorTypePOSIX);

  return Status();
}
#endif

void MainLoopPosix::RunImpl::ProcessEvents() {
#ifdef __ANDROID__
  // Collect first all readable file descriptors into a separate vector and
  // then iterate over it to invoke callbacks. Iterating directly over
  // loop.m_read_fds is not possible because the callbacks can modify the
  // container which could invalidate the iterator.
  std::vector<IOObject::WaitableHandle> fds;
  for (const auto &fd : loop.m_read_fds)
    if (FD_ISSET(fd.first, &read_fd_set))
      fds.push_back(fd.first);

  for (const auto &handle : fds) {
#else
  for (const auto &fd : read_fds) {
    if ((fd.revents & (POLLIN | POLLHUP)) == 0)
      continue;
    IOObject::WaitableHandle handle = fd.fd;
#endif
    if (loop.m_terminate_request)
      return;

    loop.ProcessReadObject(handle);
  }

  std::vector<int> signals;
  for (const auto &entry : loop.m_signals)
    if (g_signal_flags[entry.first] != 0)
      signals.push_back(entry.first);

  for (const auto &signal : signals) {
    if (loop.m_terminate_request)
      return;
    g_signal_flags[signal] = 0;
    loop.ProcessSignal(signal);
  }
}
#endif

MainLoopPosix::MainLoopPosix() : m_triggering(false) {
  Status error = m_trigger_pipe.CreateNew(/*child_process_inherit=*/false);
  assert(error.Success());
  const int trigger_pipe_fd = m_trigger_pipe.GetReadFileDescriptor();
  m_read_fds.insert({trigger_pipe_fd, [trigger_pipe_fd](MainLoopBase &loop) {
                       char c;
                       ssize_t bytes_read = llvm::sys::RetryAfterSignal(
                           -1, ::read, trigger_pipe_fd, &c, 1);
                       assert(bytes_read == 1);
                       UNUSED_IF_ASSERT_DISABLED(bytes_read);
                       // NB: This implicitly causes another loop iteration
                       // and therefore the execution of pending callbacks.
                     }});
#if HAVE_SYS_EVENT_H
  m_kqueue = kqueue();
  assert(m_kqueue >= 0);
#endif
}

MainLoopPosix::~MainLoopPosix() {
#if HAVE_SYS_EVENT_H
  close(m_kqueue);
#endif
  m_read_fds.erase(m_trigger_pipe.GetReadFileDescriptor());
  m_trigger_pipe.Close();
  assert(m_read_fds.size() == 0); 
  assert(m_signals.size() == 0);
}

MainLoopPosix::ReadHandleUP
MainLoopPosix::RegisterReadObject(const IOObjectSP &object_sp,
                                 const Callback &callback, Status &error) {
  if (!object_sp || !object_sp->IsValid()) {
    error.SetErrorString("IO object is not valid.");
    return nullptr;
  }

  const bool inserted =
      m_read_fds.insert({object_sp->GetWaitableHandle(), callback}).second;
  if (!inserted) {
    error.SetErrorStringWithFormat("File descriptor %d already monitored.",
                                   object_sp->GetWaitableHandle());
    return nullptr;
  }

  return CreateReadHandle(object_sp);
}

// We shall block the signal, then install the signal handler. The signal will
// be unblocked in the Run() function to check for signal delivery.
MainLoopPosix::SignalHandleUP
MainLoopPosix::RegisterSignal(int signo, const Callback &callback,
                              Status &error) {
  auto signal_it = m_signals.find(signo);
  if (signal_it != m_signals.end()) {
    auto callback_it = signal_it->second.callbacks.insert(
        signal_it->second.callbacks.end(), callback);
    return SignalHandleUP(new SignalHandle(*this, signo, callback_it));
  }

  SignalInfo info;
  info.callbacks.push_back(callback);
  struct sigaction new_action;
  new_action.sa_sigaction = &SignalHandler;
  new_action.sa_flags = SA_SIGINFO;
  sigemptyset(&new_action.sa_mask);
  sigaddset(&new_action.sa_mask, signo);
  sigset_t old_set;

  g_signal_flags[signo] = 0;

  // Even if using kqueue, the signal handler will still be invoked, so it's
  // important to replace it with our "benign" handler.
  int ret = sigaction(signo, &new_action, &info.old_action);
  UNUSED_IF_ASSERT_DISABLED(ret);
  assert(ret == 0 && "sigaction failed");

#if HAVE_SYS_EVENT_H
  struct kevent ev;
  EV_SET(&ev, signo, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
  ret = kevent(m_kqueue, &ev, 1, nullptr, 0, nullptr);
  assert(ret == 0);
#endif

  // If we're using kqueue, the signal needs to be unblocked in order to
  // receive it. If using pselect/ppoll, we need to block it, and later unblock
  // it as a part of the system call.
  ret = pthread_sigmask(HAVE_SYS_EVENT_H ? SIG_UNBLOCK : SIG_BLOCK,
                        &new_action.sa_mask, &old_set);
  assert(ret == 0 && "pthread_sigmask failed");
  info.was_blocked = sigismember(&old_set, signo);
  auto insert_ret = m_signals.insert({signo, info});

  return SignalHandleUP(new SignalHandle(
      *this, signo, insert_ret.first->second.callbacks.begin()));
}

void MainLoopPosix::UnregisterReadObject(IOObject::WaitableHandle handle) {
  bool erased = m_read_fds.erase(handle);
  UNUSED_IF_ASSERT_DISABLED(erased);
  assert(erased);
}

void MainLoopPosix::UnregisterSignal(
    int signo, std::list<Callback>::iterator callback_it) {
  auto it = m_signals.find(signo);
  assert(it != m_signals.end());

  it->second.callbacks.erase(callback_it);
  // Do not remove the signal handler unless all callbacks have been erased.
  if (!it->second.callbacks.empty())
    return;

  sigaction(signo, &it->second.old_action, nullptr);

  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, signo);
  int ret = pthread_sigmask(it->second.was_blocked ? SIG_BLOCK : SIG_UNBLOCK,
                            &set, nullptr);
  assert(ret == 0);
  UNUSED_IF_ASSERT_DISABLED(ret);

#if HAVE_SYS_EVENT_H
  struct kevent ev;
  EV_SET(&ev, signo, EVFILT_SIGNAL, EV_DELETE, 0, 0, 0);
  ret = kevent(m_kqueue, &ev, 1, nullptr, 0, nullptr);
  assert(ret == 0);
#endif

  m_signals.erase(it);
}

Status MainLoopPosix::Run() {
  m_terminate_request = false;

  Status error;
  RunImpl impl(*this);

  // run until termination or until we run out of things to listen to
  // (m_read_fds will always contain m_trigger_pipe fd, so check for > 1)
  while (!m_terminate_request &&
         (m_read_fds.size() > 1 || !m_signals.empty())) {
    error = impl.Poll();
    if (error.Fail())
      return error;

    impl.ProcessEvents();

    m_triggering = false;
    ProcessPendingCallbacks();
  }
  return Status();
}

void MainLoopPosix::ProcessReadObject(IOObject::WaitableHandle handle) {
  auto it = m_read_fds.find(handle);
  if (it != m_read_fds.end())
    it->second(*this); // Do the work
}

void MainLoopPosix::ProcessSignal(int signo) {
  auto it = m_signals.find(signo);
  if (it != m_signals.end()) {
    // The callback may actually register/unregister signal handlers,
    // so we need to create a copy first.
    llvm::SmallVector<Callback, 4> callbacks_to_run{
        it->second.callbacks.begin(), it->second.callbacks.end()};
    for (auto &x : callbacks_to_run)
      x(*this); // Do the work
  }
}

void MainLoopPosix::TriggerPendingCallbacks() {
  if (m_triggering.exchange(true))
    return;

  char c = '.';
  size_t bytes_written;
  Status error = m_trigger_pipe.Write(&c, 1, bytes_written);
  assert(error.Success());
  UNUSED_IF_ASSERT_DISABLED(error);
  assert(bytes_written == 1);
}
