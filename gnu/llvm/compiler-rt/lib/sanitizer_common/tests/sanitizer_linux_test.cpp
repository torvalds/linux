//===-- sanitizer_linux_test.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Tests for sanitizer_linux.h
//
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"
#if SANITIZER_LINUX

#include "sanitizer_common/sanitizer_linux.h"

#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_file.h"
#include "gtest/gtest.h"

#include <pthread.h>
#include <sched.h>
#include <stdlib.h>

#include <algorithm>
#include <vector>

namespace __sanitizer {

struct TidReporterArgument {
  TidReporterArgument() {
    pthread_mutex_init(&terminate_thread_mutex, NULL);
    pthread_mutex_init(&tid_reported_mutex, NULL);
    pthread_cond_init(&terminate_thread_cond, NULL);
    pthread_cond_init(&tid_reported_cond, NULL);
    terminate_thread = false;
  }

  ~TidReporterArgument() {
    pthread_mutex_destroy(&terminate_thread_mutex);
    pthread_mutex_destroy(&tid_reported_mutex);
    pthread_cond_destroy(&terminate_thread_cond);
    pthread_cond_destroy(&tid_reported_cond);
  }

  tid_t reported_tid;
  // For signaling to spawned threads that they should terminate.
  pthread_cond_t terminate_thread_cond;
  pthread_mutex_t terminate_thread_mutex;
  bool terminate_thread;
  // For signaling to main thread that a child thread has reported its tid.
  pthread_cond_t tid_reported_cond;
  pthread_mutex_t tid_reported_mutex;

 private:
  // Disallow evil constructors
  TidReporterArgument(const TidReporterArgument &);
  void operator=(const TidReporterArgument &);
};

class ThreadListerTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    pthread_t pthread_id;
    tid_t tid;
    for (uptr i = 0; i < kThreadCount; i++) {
      SpawnTidReporter(&pthread_id, &tid);
      pthread_ids_.push_back(pthread_id);
      tids_.push_back(tid);
    }
  }

  virtual void TearDown() {
    pthread_mutex_lock(&thread_arg.terminate_thread_mutex);
    thread_arg.terminate_thread = true;
    pthread_cond_broadcast(&thread_arg.terminate_thread_cond);
    pthread_mutex_unlock(&thread_arg.terminate_thread_mutex);
    for (uptr i = 0; i < pthread_ids_.size(); i++)
      pthread_join(pthread_ids_[i], NULL);
  }

  void SpawnTidReporter(pthread_t *pthread_id, tid_t *tid);

  static const uptr kThreadCount = 20;

  std::vector<pthread_t> pthread_ids_;
  std::vector<tid_t> tids_;

  TidReporterArgument thread_arg;
};

// Writes its TID once to reported_tid and waits until signaled to terminate.
void *TidReporterThread(void *argument) {
  TidReporterArgument *arg = reinterpret_cast<TidReporterArgument *>(argument);
  pthread_mutex_lock(&arg->tid_reported_mutex);
  arg->reported_tid = GetTid();
  pthread_cond_broadcast(&arg->tid_reported_cond);
  pthread_mutex_unlock(&arg->tid_reported_mutex);

  pthread_mutex_lock(&arg->terminate_thread_mutex);
  while (!arg->terminate_thread)
    pthread_cond_wait(&arg->terminate_thread_cond,
                      &arg->terminate_thread_mutex);
  pthread_mutex_unlock(&arg->terminate_thread_mutex);
  return NULL;
}

void ThreadListerTest::SpawnTidReporter(pthread_t *pthread_id, tid_t *tid) {
  pthread_mutex_lock(&thread_arg.tid_reported_mutex);
  thread_arg.reported_tid = -1;
  ASSERT_EQ(0, pthread_create(pthread_id, NULL,
                              TidReporterThread,
                              &thread_arg));
  while (thread_arg.reported_tid == (tid_t)(-1))
    pthread_cond_wait(&thread_arg.tid_reported_cond,
                      &thread_arg.tid_reported_mutex);
  pthread_mutex_unlock(&thread_arg.tid_reported_mutex);
  *tid = thread_arg.reported_tid;
}

static std::vector<tid_t> ReadTidsToVector(ThreadLister *thread_lister) {
  std::vector<tid_t> listed_tids;
  InternalMmapVector<tid_t> threads(128);
  EXPECT_TRUE(thread_lister->ListThreads(&threads));
  return std::vector<tid_t>(threads.begin(), threads.end());
}

static bool Includes(std::vector<tid_t> first, std::vector<tid_t> second) {
  std::sort(first.begin(), first.end());
  std::sort(second.begin(), second.end());
  return std::includes(first.begin(), first.end(),
                       second.begin(), second.end());
}

static bool HasElement(const std::vector<tid_t> &vector, tid_t element) {
  return std::find(vector.begin(), vector.end(), element) != vector.end();
}

// ThreadLister's output should include the current thread's TID and the TID of
// every thread we spawned.
TEST_F(ThreadListerTest, ThreadListerSeesAllSpawnedThreads) {
  tid_t self_tid = GetTid();
  ThreadLister thread_lister(getpid());
  std::vector<tid_t> listed_tids = ReadTidsToVector(&thread_lister);
  ASSERT_TRUE(HasElement(listed_tids, self_tid));
  ASSERT_TRUE(Includes(listed_tids, tids_));
}

TEST_F(ThreadListerTest, DoNotForgetThreads) {
  ThreadLister thread_lister(getpid());

  // Run the loop body twice, because ThreadLister might behave differently if
  // called on a freshly created object.
  for (uptr i = 0; i < 2; i++) {
    std::vector<tid_t> listed_tids = ReadTidsToVector(&thread_lister);
    ASSERT_TRUE(Includes(listed_tids, tids_));
  }
}

// If new threads have spawned during ThreadLister object's lifetime, calling
// relisting should cause ThreadLister to recognize their existence.
TEST_F(ThreadListerTest, NewThreads) {
  ThreadLister thread_lister(getpid());
  std::vector<tid_t> threads_before_extra = ReadTidsToVector(&thread_lister);

  pthread_t extra_pthread_id;
  tid_t extra_tid;
  SpawnTidReporter(&extra_pthread_id, &extra_tid);
  // Register the new thread so it gets terminated in TearDown().
  pthread_ids_.push_back(extra_pthread_id);

  // It would be very bizarre if the new TID had been listed before we even
  // spawned that thread, but it would also cause a false success in this test,
  // so better check for that.
  ASSERT_FALSE(HasElement(threads_before_extra, extra_tid));

  std::vector<tid_t> threads_after_extra = ReadTidsToVector(&thread_lister);
  ASSERT_TRUE(HasElement(threads_after_extra, extra_tid));
}

TEST(SanitizerCommon, SetEnvTest) {
  const char kEnvName[] = "ENV_FOO";
  SetEnv(kEnvName, "value");
  EXPECT_STREQ("value", getenv(kEnvName));
  unsetenv(kEnvName);
  EXPECT_EQ(0, getenv(kEnvName));
}

#if (defined(__x86_64__) || defined(__i386__)) && !SANITIZER_ANDROID
// libpthread puts the thread descriptor at the end of stack space.
void *thread_descriptor_size_test_func(void *arg) {
  uptr descr_addr = (uptr)pthread_self();
  pthread_attr_t attr;
  pthread_getattr_np(pthread_self(), &attr);
  void *stackaddr;
  size_t stacksize;
  pthread_attr_getstack(&attr, &stackaddr, &stacksize);
  return (void *)((uptr)stackaddr + stacksize - descr_addr);
}

TEST(SanitizerLinux, ThreadDescriptorSize) {
  pthread_t tid;
  void *result;
  ASSERT_EQ(0, pthread_create(&tid, 0, thread_descriptor_size_test_func, 0));
  ASSERT_EQ(0, pthread_join(tid, &result));
  EXPECT_EQ((uptr)result, ThreadDescriptorSize());
}
#endif

TEST(SanitizerCommon, LibraryNameIs) {
  EXPECT_FALSE(LibraryNameIs("", ""));

  char full_name[256];
  const char *paths[] = { "", "/", "/path/to/" };
  const char *suffixes[] = { "", "-linux", ".1.2", "-linux.1.2" };
  const char *base_names[] = { "lib", "lib.0", "lib-i386" };
  const char *wrong_names[] = { "", "lib.9", "lib-x86_64" };
  for (uptr i = 0; i < ARRAY_SIZE(paths); i++)
    for (uptr j = 0; j < ARRAY_SIZE(suffixes); j++) {
      for (uptr k = 0; k < ARRAY_SIZE(base_names); k++) {
        internal_snprintf(full_name, ARRAY_SIZE(full_name), "%s%s%s.so",
                          paths[i], base_names[k], suffixes[j]);
        EXPECT_TRUE(LibraryNameIs(full_name, base_names[k]))
            << "Full name " << full_name
            << " doesn't match base name " << base_names[k];
        for (uptr m = 0; m < ARRAY_SIZE(wrong_names); m++)
          EXPECT_FALSE(LibraryNameIs(full_name, wrong_names[m]))
            << "Full name " << full_name
            << " matches base name " << wrong_names[m];
      }
    }
}

#if defined(__mips64)
// Effectively, this is a test for ThreadDescriptorSize() which is used to
// compute ThreadSelf().
TEST(SanitizerLinux, ThreadSelfTest) {
  ASSERT_EQ(pthread_self(), ThreadSelf());
}
#endif

TEST(SanitizerCommon, StartSubprocessTest) {
  int pipe_fds[2];
  ASSERT_EQ(0, pipe(pipe_fds));
#if SANITIZER_ANDROID
  const char *shell = "/system/bin/sh";
#else
  const char *shell = "/bin/sh";
#endif
  const char *argv[] = {shell, "-c", "echo -n 'hello'", (char *)NULL};
  int pid = StartSubprocess(shell, argv, GetEnviron(),
                            /* stdin */ kInvalidFd, /* stdout */ pipe_fds[1]);
  ASSERT_GT(pid, 0);

  // wait for process to finish.
  while (IsProcessRunning(pid)) {
  }
  ASSERT_FALSE(IsProcessRunning(pid));

  char buffer[256];
  {
    char *ptr = buffer;
    uptr bytes_read;
    while (ReadFromFile(pipe_fds[0], ptr, 256, &bytes_read)) {
      if (!bytes_read) {
        break;
      }
      ptr += bytes_read;
    }
    ASSERT_EQ(5, ptr - buffer);
    *ptr = 0;
  }
  ASSERT_EQ(0, strcmp(buffer, "hello")) << "Buffer: " << buffer;
  internal_close(pipe_fds[0]);
}

}  // namespace __sanitizer

#endif  // SANITIZER_LINUX
