//===-- llvm/Support/ThreadPool.h - A ThreadPool implementation -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a crude C++11 based thread pool.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_THREADPOOL_H
#define LLVM_SUPPORT_THREADPOOL_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/RWMutex.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/thread.h"

#include <future>

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

namespace llvm {

class ThreadPoolTaskGroup;

/// This defines the abstract base interface for a ThreadPool allowing
/// asynchronous parallel execution on a defined number of threads.
///
/// It is possible to reuse one thread pool for different groups of tasks
/// by grouping tasks using ThreadPoolTaskGroup. All tasks are processed using
/// the same queue, but it is possible to wait only for a specific group of
/// tasks to finish.
///
/// It is also possible for worker threads to submit new tasks and wait for
/// them. Note that this may result in a deadlock in cases such as when a task
/// (directly or indirectly) tries to wait for its own completion, or when all
/// available threads are used up by tasks waiting for a task that has no thread
/// left to run on (this includes waiting on the returned future). It should be
/// generally safe to wait() for a group as long as groups do not form a cycle.
class ThreadPoolInterface {
  /// The actual method to enqueue a task to be defined by the concrete
  /// implementation.
  virtual void asyncEnqueue(std::function<void()> Task,
                            ThreadPoolTaskGroup *Group) = 0;

public:
  /// Destroying the pool will drain the pending tasks and wait. The current
  /// thread may participate in the execution of the pending tasks.
  virtual ~ThreadPoolInterface();

  /// Blocking wait for all the threads to complete and the queue to be empty.
  /// It is an error to try to add new tasks while blocking on this call.
  /// Calling wait() from a task would deadlock waiting for itself.
  virtual void wait() = 0;

  /// Blocking wait for only all the threads in the given group to complete.
  /// It is possible to wait even inside a task, but waiting (directly or
  /// indirectly) on itself will deadlock. If called from a task running on a
  /// worker thread, the call may process pending tasks while waiting in order
  /// not to waste the thread.
  virtual void wait(ThreadPoolTaskGroup &Group) = 0;

  /// Returns the maximum number of worker this pool can eventually grow to.
  virtual unsigned getMaxConcurrency() const = 0;

  /// Asynchronous submission of a task to the pool. The returned future can be
  /// used to wait for the task to finish and is *non-blocking* on destruction.
  template <typename Function, typename... Args>
  auto async(Function &&F, Args &&...ArgList) {
    auto Task =
        std::bind(std::forward<Function>(F), std::forward<Args>(ArgList)...);
    return async(std::move(Task));
  }

  /// Overload, task will be in the given task group.
  template <typename Function, typename... Args>
  auto async(ThreadPoolTaskGroup &Group, Function &&F, Args &&...ArgList) {
    auto Task =
        std::bind(std::forward<Function>(F), std::forward<Args>(ArgList)...);
    return async(Group, std::move(Task));
  }

  /// Asynchronous submission of a task to the pool. The returned future can be
  /// used to wait for the task to finish and is *non-blocking* on destruction.
  template <typename Func>
  auto async(Func &&F) -> std::shared_future<decltype(F())> {
    return asyncImpl(std::function<decltype(F())()>(std::forward<Func>(F)),
                     nullptr);
  }

  template <typename Func>
  auto async(ThreadPoolTaskGroup &Group, Func &&F)
      -> std::shared_future<decltype(F())> {
    return asyncImpl(std::function<decltype(F())()>(std::forward<Func>(F)),
                     &Group);
  }

private:
  /// Asynchronous submission of a task to the pool. The returned future can be
  /// used to wait for the task to finish and is *non-blocking* on destruction.
  template <typename ResTy>
  std::shared_future<ResTy> asyncImpl(std::function<ResTy()> Task,
                                      ThreadPoolTaskGroup *Group) {
    auto Future = std::async(std::launch::deferred, std::move(Task)).share();
    asyncEnqueue([Future]() { Future.wait(); }, Group);
    return Future;
  }
};

#if LLVM_ENABLE_THREADS
/// A ThreadPool implementation using std::threads.
///
/// The pool keeps a vector of threads alive, waiting on a condition variable
/// for some work to become available.
class StdThreadPool : public ThreadPoolInterface {
public:
  /// Construct a pool using the hardware strategy \p S for mapping hardware
  /// execution resources (threads, cores, CPUs)
  /// Defaults to using the maximum execution resources in the system, but
  /// accounting for the affinity mask.
  StdThreadPool(ThreadPoolStrategy S = hardware_concurrency());

  /// Blocking destructor: the pool will wait for all the threads to complete.
  ~StdThreadPool() override;

  /// Blocking wait for all the threads to complete and the queue to be empty.
  /// It is an error to try to add new tasks while blocking on this call.
  /// Calling wait() from a task would deadlock waiting for itself.
  void wait() override;

  /// Blocking wait for only all the threads in the given group to complete.
  /// It is possible to wait even inside a task, but waiting (directly or
  /// indirectly) on itself will deadlock. If called from a task running on a
  /// worker thread, the call may process pending tasks while waiting in order
  /// not to waste the thread.
  void wait(ThreadPoolTaskGroup &Group) override;

  /// Returns the maximum number of worker threads in the pool, not the current
  /// number of threads!
  unsigned getMaxConcurrency() const override { return MaxThreadCount; }

  // TODO: Remove, misleading legacy name warning!
  LLVM_DEPRECATED("Use getMaxConcurrency instead", "getMaxConcurrency")
  unsigned getThreadCount() const { return MaxThreadCount; }

  /// Returns true if the current thread is a worker thread of this thread pool.
  bool isWorkerThread() const;

private:
  /// Returns true if all tasks in the given group have finished (nullptr means
  /// all tasks regardless of their group). QueueLock must be locked.
  bool workCompletedUnlocked(ThreadPoolTaskGroup *Group) const;

  /// Asynchronous submission of a task to the pool. The returned future can be
  /// used to wait for the task to finish and is *non-blocking* on destruction.
  void asyncEnqueue(std::function<void()> Task,
                    ThreadPoolTaskGroup *Group) override {
    int requestedThreads;
    {
      // Lock the queue and push the new task
      std::unique_lock<std::mutex> LockGuard(QueueLock);

      // Don't allow enqueueing after disabling the pool
      assert(EnableFlag && "Queuing a thread during ThreadPool destruction");
      Tasks.emplace_back(std::make_pair(std::move(Task), Group));
      requestedThreads = ActiveThreads + Tasks.size();
    }
    QueueCondition.notify_one();
    grow(requestedThreads);
  }

  /// Grow to ensure that we have at least `requested` Threads, but do not go
  /// over MaxThreadCount.
  void grow(int requested);

  void processTasks(ThreadPoolTaskGroup *WaitingForGroup);

  /// Threads in flight
  std::vector<llvm::thread> Threads;
  /// Lock protecting access to the Threads vector.
  mutable llvm::sys::RWMutex ThreadsLock;

  /// Tasks waiting for execution in the pool.
  std::deque<std::pair<std::function<void()>, ThreadPoolTaskGroup *>> Tasks;

  /// Locking and signaling for accessing the Tasks queue.
  std::mutex QueueLock;
  std::condition_variable QueueCondition;

  /// Signaling for job completion (all tasks or all tasks in a group).
  std::condition_variable CompletionCondition;

  /// Keep track of the number of thread actually busy
  unsigned ActiveThreads = 0;
  /// Number of threads active for tasks in the given group (only non-zero).
  DenseMap<ThreadPoolTaskGroup *, unsigned> ActiveGroups;

  /// Signal for the destruction of the pool, asking thread to exit.
  bool EnableFlag = true;

  const ThreadPoolStrategy Strategy;

  /// Maximum number of threads to potentially grow this pool to.
  const unsigned MaxThreadCount;
};
#endif // LLVM_ENABLE_THREADS

/// A non-threaded implementation.
class SingleThreadExecutor : public ThreadPoolInterface {
public:
  /// Construct a non-threaded pool, ignoring using the hardware strategy.
  SingleThreadExecutor(ThreadPoolStrategy ignored = {});

  /// Blocking destructor: the pool will first execute the pending tasks.
  ~SingleThreadExecutor() override;

  /// Blocking wait for all the tasks to execute first
  void wait() override;

  /// Blocking wait for only all the tasks in the given group to complete.
  void wait(ThreadPoolTaskGroup &Group) override;

  /// Returns always 1: there is no concurrency.
  unsigned getMaxConcurrency() const override { return 1; }

  // TODO: Remove, misleading legacy name warning!
  LLVM_DEPRECATED("Use getMaxConcurrency instead", "getMaxConcurrency")
  unsigned getThreadCount() const { return 1; }

  /// Returns true if the current thread is a worker thread of this thread pool.
  bool isWorkerThread() const;

private:
  /// Asynchronous submission of a task to the pool. The returned future can be
  /// used to wait for the task to finish and is *non-blocking* on destruction.
  void asyncEnqueue(std::function<void()> Task,
                    ThreadPoolTaskGroup *Group) override {
    Tasks.emplace_back(std::make_pair(std::move(Task), Group));
  }

  /// Tasks waiting for execution in the pool.
  std::deque<std::pair<std::function<void()>, ThreadPoolTaskGroup *>> Tasks;
};

#if LLVM_ENABLE_THREADS
using DefaultThreadPool = StdThreadPool;
#else
using DefaultThreadPool = SingleThreadExecutor;
#endif

/// A group of tasks to be run on a thread pool. Thread pool tasks in different
/// groups can run on the same threadpool but can be waited for separately.
/// It is even possible for tasks of one group to submit and wait for tasks
/// of another group, as long as this does not form a loop.
class ThreadPoolTaskGroup {
public:
  /// The ThreadPool argument is the thread pool to forward calls to.
  ThreadPoolTaskGroup(ThreadPoolInterface &Pool) : Pool(Pool) {}

  /// Blocking destructor: will wait for all the tasks in the group to complete
  /// by calling ThreadPool::wait().
  ~ThreadPoolTaskGroup() { wait(); }

  /// Calls ThreadPool::async() for this group.
  template <typename Function, typename... Args>
  inline auto async(Function &&F, Args &&...ArgList) {
    return Pool.async(*this, std::forward<Function>(F),
                      std::forward<Args>(ArgList)...);
  }

  /// Calls ThreadPool::wait() for this group.
  void wait() { Pool.wait(*this); }

private:
  ThreadPoolInterface &Pool;
};

} // namespace llvm

#endif // LLVM_SUPPORT_THREADPOOL_H
