//===-- sanitizer_stoptheworld_mac.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// See sanitizer_stoptheworld.h for details.
//
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"

#if SANITIZER_APPLE && (defined(__x86_64__) || defined(__aarch64__) || \
                      defined(__i386))

#include <mach/mach.h>
#include <mach/thread_info.h>
#include <pthread.h>

#include "sanitizer_stoptheworld.h"

namespace __sanitizer {
typedef struct {
  tid_t tid;
  thread_t thread;
} SuspendedThreadInfo;

class SuspendedThreadsListMac final : public SuspendedThreadsList {
 public:
  SuspendedThreadsListMac() = default;

  tid_t GetThreadID(uptr index) const override;
  thread_t GetThread(uptr index) const;
  uptr ThreadCount() const override;
  bool ContainsThread(thread_t thread) const;
  void Append(thread_t thread);

  PtraceRegistersStatus GetRegistersAndSP(uptr index,
                                          InternalMmapVector<uptr> *buffer,
                                          uptr *sp) const override;

 private:
  InternalMmapVector<SuspendedThreadInfo> threads_;
};

struct RunThreadArgs {
  StopTheWorldCallback callback;
  void *argument;
};

void *RunThread(void *arg) {
  struct RunThreadArgs *run_args = (struct RunThreadArgs *)arg;
  SuspendedThreadsListMac suspended_threads_list;

  thread_array_t threads;
  mach_msg_type_number_t num_threads;
  kern_return_t err = task_threads(mach_task_self(), &threads, &num_threads);
  if (err != KERN_SUCCESS) {
    VReport(1, "Failed to get threads for task (errno %d).\n", err);
    return nullptr;
  }

  thread_t thread_self = mach_thread_self();
  for (unsigned int i = 0; i < num_threads; ++i) {
    if (threads[i] == thread_self) continue;

    thread_suspend(threads[i]);
    suspended_threads_list.Append(threads[i]);
  }

  run_args->callback(suspended_threads_list, run_args->argument);

  uptr num_suspended = suspended_threads_list.ThreadCount();
  for (unsigned int i = 0; i < num_suspended; ++i) {
    thread_resume(suspended_threads_list.GetThread(i));
  }
  return nullptr;
}

void StopTheWorld(StopTheWorldCallback callback, void *argument) {
  struct RunThreadArgs arg = {callback, argument};
  pthread_t run_thread = (pthread_t)internal_start_thread(RunThread, &arg);
  internal_join_thread(run_thread);
}

#if defined(__x86_64__)
typedef x86_thread_state64_t regs_struct;
#define regs_flavor x86_THREAD_STATE64

#define SP_REG __rsp

#elif defined(__aarch64__)
typedef arm_thread_state64_t regs_struct;
#define regs_flavor ARM_THREAD_STATE64

# if __DARWIN_UNIX03
#  define SP_REG __sp
# else
#  define SP_REG sp
# endif

#elif defined(__i386)
typedef x86_thread_state32_t regs_struct;
#define regs_flavor x86_THREAD_STATE32

#define SP_REG __esp

#else
#error "Unsupported architecture"
#endif

tid_t SuspendedThreadsListMac::GetThreadID(uptr index) const {
  CHECK_LT(index, threads_.size());
  return threads_[index].tid;
}

thread_t SuspendedThreadsListMac::GetThread(uptr index) const {
  CHECK_LT(index, threads_.size());
  return threads_[index].thread;
}

uptr SuspendedThreadsListMac::ThreadCount() const {
  return threads_.size();
}

bool SuspendedThreadsListMac::ContainsThread(thread_t thread) const {
  for (uptr i = 0; i < threads_.size(); i++) {
    if (threads_[i].thread == thread) return true;
  }
  return false;
}

void SuspendedThreadsListMac::Append(thread_t thread) {
  thread_identifier_info_data_t info;
  mach_msg_type_number_t info_count = THREAD_IDENTIFIER_INFO_COUNT;
  kern_return_t err = thread_info(thread, THREAD_IDENTIFIER_INFO,
                                  (thread_info_t)&info, &info_count);
  if (err != KERN_SUCCESS) {
    VReport(1, "Error - unable to get thread ident for a thread\n");
    return;
  }
  threads_.push_back({info.thread_id, thread});
}

PtraceRegistersStatus SuspendedThreadsListMac::GetRegistersAndSP(
    uptr index, InternalMmapVector<uptr> *buffer, uptr *sp) const {
  thread_t thread = GetThread(index);
  regs_struct regs;
  int err;
  mach_msg_type_number_t reg_count = sizeof(regs) / sizeof(natural_t);
  err = thread_get_state(thread, regs_flavor, (thread_state_t)&regs,
                         &reg_count);
  if (err != KERN_SUCCESS) {
    VReport(1, "Error - unable to get registers for a thread\n");
    // MIG_ARRAY_TOO_LARGE, means that the state is too large, but it's
    // still safe to proceed.
    return err == MIG_ARRAY_TOO_LARGE ? REGISTERS_UNAVAILABLE
                                      : REGISTERS_UNAVAILABLE_FATAL;
  }

  buffer->resize(RoundUpTo(sizeof(regs), sizeof(uptr)) / sizeof(uptr));
  internal_memcpy(buffer->data(), &regs, sizeof(regs));
#if defined(__aarch64__) && defined(arm_thread_state64_get_sp)
  *sp = arm_thread_state64_get_sp(regs);
#else
  *sp = regs.SP_REG;
#endif

  // On x86_64 and aarch64, we must account for the stack redzone, which is 128
  // bytes.
  if (SANITIZER_WORDSIZE == 64) *sp -= 128;

  return REGISTERS_AVAILABLE;
}

} // namespace __sanitizer

#endif  // SANITIZER_APPLE && (defined(__x86_64__) || defined(__aarch64__)) ||
        //                   defined(__i386))
