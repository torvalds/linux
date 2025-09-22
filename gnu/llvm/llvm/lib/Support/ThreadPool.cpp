//==-- llvm/Support/ThreadPool.cpp - A ThreadPool implementation -*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a crude C++11 based thread pool.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/ThreadPool.h"

#include "llvm/Config/llvm-config.h"

#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

ThreadPoolInterface::~ThreadPoolInterface() = default;

// A note on thread groups: Tasks are by default in no group (represented
// by nullptr ThreadPoolTaskGroup pointer in the Tasks queue) and functionality
// here normally works on all tasks regardless of their group (functions
// in that case receive nullptr ThreadPoolTaskGroup pointer as argument).
// A task in a group has a pointer to that ThreadPoolTaskGroup in the Tasks
// queue, and functions called to work only on tasks from one group take that
// pointer.

#if LLVM_ENABLE_THREADS

StdThreadPool::StdThreadPool(ThreadPoolStrategy S)
    : Strategy(S), MaxThreadCount(S.compute_thread_count()) {}

void StdThreadPool::grow(int requested) {
  llvm::sys::ScopedWriter LockGuard(ThreadsLock);
  if (Threads.size() >= MaxThreadCount)
    return; // Already hit the max thread pool size.
  int newThreadCount = std::min<int>(requested, MaxThreadCount);
  while (static_cast<int>(Threads.size()) < newThreadCount) {
    int ThreadID = Threads.size();
    Threads.emplace_back([this, ThreadID] {
      set_thread_name(formatv("llvm-worker-{0}", ThreadID));
      Strategy.apply_thread_strategy(ThreadID);
      processTasks(nullptr);
    });
  }
}

#ifndef NDEBUG
// The group of the tasks run by the current thread.
static LLVM_THREAD_LOCAL std::vector<ThreadPoolTaskGroup *>
    *CurrentThreadTaskGroups = nullptr;
#endif

// WaitingForGroup == nullptr means all tasks regardless of their group.
void StdThreadPool::processTasks(ThreadPoolTaskGroup *WaitingForGroup) {
  while (true) {
    std::function<void()> Task;
    ThreadPoolTaskGroup *GroupOfTask;
    {
      std::unique_lock<std::mutex> LockGuard(QueueLock);
      bool workCompletedForGroup = false; // Result of workCompletedUnlocked()
      // Wait for tasks to be pushed in the queue
      QueueCondition.wait(LockGuard, [&] {
        return !EnableFlag || !Tasks.empty() ||
               (WaitingForGroup != nullptr &&
                (workCompletedForGroup =
                     workCompletedUnlocked(WaitingForGroup)));
      });
      // Exit condition
      if (!EnableFlag && Tasks.empty())
        return;
      if (WaitingForGroup != nullptr && workCompletedForGroup)
        return;
      // Yeah, we have a task, grab it and release the lock on the queue

      // We first need to signal that we are active before popping the queue
      // in order for wait() to properly detect that even if the queue is
      // empty, there is still a task in flight.
      ++ActiveThreads;
      Task = std::move(Tasks.front().first);
      GroupOfTask = Tasks.front().second;
      // Need to count active threads in each group separately, ActiveThreads
      // would never be 0 if waiting for another group inside a wait.
      if (GroupOfTask != nullptr)
        ++ActiveGroups[GroupOfTask]; // Increment or set to 1 if new item
      Tasks.pop_front();
    }
#ifndef NDEBUG
    if (CurrentThreadTaskGroups == nullptr)
      CurrentThreadTaskGroups = new std::vector<ThreadPoolTaskGroup *>;
    CurrentThreadTaskGroups->push_back(GroupOfTask);
#endif

    // Run the task we just grabbed
    Task();

#ifndef NDEBUG
    CurrentThreadTaskGroups->pop_back();
    if (CurrentThreadTaskGroups->empty()) {
      delete CurrentThreadTaskGroups;
      CurrentThreadTaskGroups = nullptr;
    }
#endif

    bool Notify;
    bool NotifyGroup;
    {
      // Adjust `ActiveThreads`, in case someone waits on StdThreadPool::wait()
      std::lock_guard<std::mutex> LockGuard(QueueLock);
      --ActiveThreads;
      if (GroupOfTask != nullptr) {
        auto A = ActiveGroups.find(GroupOfTask);
        if (--(A->second) == 0)
          ActiveGroups.erase(A);
      }
      Notify = workCompletedUnlocked(GroupOfTask);
      NotifyGroup = GroupOfTask != nullptr && Notify;
    }
    // Notify task completion if this is the last active thread, in case
    // someone waits on StdThreadPool::wait().
    if (Notify)
      CompletionCondition.notify_all();
    // If this was a task in a group, notify also threads waiting for tasks
    // in this function on QueueCondition, to make a recursive wait() return
    // after the group it's been waiting for has finished.
    if (NotifyGroup)
      QueueCondition.notify_all();
  }
}

bool StdThreadPool::workCompletedUnlocked(ThreadPoolTaskGroup *Group) const {
  if (Group == nullptr)
    return !ActiveThreads && Tasks.empty();
  return ActiveGroups.count(Group) == 0 &&
         !llvm::any_of(Tasks,
                       [Group](const auto &T) { return T.second == Group; });
}

void StdThreadPool::wait() {
  assert(!isWorkerThread()); // Would deadlock waiting for itself.
  // Wait for all threads to complete and the queue to be empty
  std::unique_lock<std::mutex> LockGuard(QueueLock);
  CompletionCondition.wait(LockGuard,
                           [&] { return workCompletedUnlocked(nullptr); });
}

void StdThreadPool::wait(ThreadPoolTaskGroup &Group) {
  // Wait for all threads in the group to complete.
  if (!isWorkerThread()) {
    std::unique_lock<std::mutex> LockGuard(QueueLock);
    CompletionCondition.wait(LockGuard,
                             [&] { return workCompletedUnlocked(&Group); });
    return;
  }
  // Make sure to not deadlock waiting for oneself.
  assert(CurrentThreadTaskGroups == nullptr ||
         !llvm::is_contained(*CurrentThreadTaskGroups, &Group));
  // Handle the case of recursive call from another task in a different group,
  // in which case process tasks while waiting to keep the thread busy and avoid
  // possible deadlock.
  processTasks(&Group);
}

bool StdThreadPool::isWorkerThread() const {
  llvm::sys::ScopedReader LockGuard(ThreadsLock);
  llvm::thread::id CurrentThreadId = llvm::this_thread::get_id();
  for (const llvm::thread &Thread : Threads)
    if (CurrentThreadId == Thread.get_id())
      return true;
  return false;
}

// The destructor joins all threads, waiting for completion.
StdThreadPool::~StdThreadPool() {
  {
    std::unique_lock<std::mutex> LockGuard(QueueLock);
    EnableFlag = false;
  }
  QueueCondition.notify_all();
  llvm::sys::ScopedReader LockGuard(ThreadsLock);
  for (auto &Worker : Threads)
    Worker.join();
}

#endif // LLVM_ENABLE_THREADS Disabled

// No threads are launched, issue a warning if ThreadCount is not 0
SingleThreadExecutor::SingleThreadExecutor(ThreadPoolStrategy S) {
  int ThreadCount = S.compute_thread_count();
  if (ThreadCount != 1) {
    errs() << "Warning: request a ThreadPool with " << ThreadCount
           << " threads, but LLVM_ENABLE_THREADS has been turned off\n";
  }
}

void SingleThreadExecutor::wait() {
  // Sequential implementation running the tasks
  while (!Tasks.empty()) {
    auto Task = std::move(Tasks.front().first);
    Tasks.pop_front();
    Task();
  }
}

void SingleThreadExecutor::wait(ThreadPoolTaskGroup &) {
  // Simply wait for all, this works even if recursive (the running task
  // is already removed from the queue).
  wait();
}

bool SingleThreadExecutor::isWorkerThread() const {
  report_fatal_error("LLVM compiled without multithreading");
}

SingleThreadExecutor::~SingleThreadExecutor() { wait(); }
