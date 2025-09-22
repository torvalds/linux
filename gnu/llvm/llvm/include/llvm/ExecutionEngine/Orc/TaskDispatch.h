//===--------- TaskDispatch.h - ORC task dispatch utils ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Task and TaskDispatch classes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_TASKDISPATCH_H
#define LLVM_EXECUTIONENGINE_ORC_TASKDISPATCH_H

#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ExtensibleRTTI.h"
#include "llvm/Support/raw_ostream.h"

#include <cassert>
#include <string>

#if LLVM_ENABLE_THREADS
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#endif

namespace llvm {
namespace orc {

/// Represents an abstract task for ORC to run.
class Task : public RTTIExtends<Task, RTTIRoot> {
public:
  static char ID;

  virtual ~Task() = default;

  /// Description of the task to be performed. Used for logging.
  virtual void printDescription(raw_ostream &OS) = 0;

  /// Run the task.
  virtual void run() = 0;

private:
  void anchor() override;
};

/// Base class for generic tasks.
class GenericNamedTask : public RTTIExtends<GenericNamedTask, Task> {
public:
  static char ID;
  static const char *DefaultDescription;
};

/// Generic task implementation.
template <typename FnT> class GenericNamedTaskImpl : public GenericNamedTask {
public:
  GenericNamedTaskImpl(FnT &&Fn, std::string DescBuffer)
      : Fn(std::forward<FnT>(Fn)), Desc(DescBuffer.c_str()),
        DescBuffer(std::move(DescBuffer)) {}
  GenericNamedTaskImpl(FnT &&Fn, const char *Desc)
      : Fn(std::forward<FnT>(Fn)), Desc(Desc) {
    assert(Desc && "Description cannot be null");
  }
  void printDescription(raw_ostream &OS) override { OS << Desc; }
  void run() override { Fn(); }

private:
  FnT Fn;
  const char *Desc;
  std::string DescBuffer;
};

/// Create a generic named task from a std::string description.
template <typename FnT>
std::unique_ptr<GenericNamedTask> makeGenericNamedTask(FnT &&Fn,
                                                       std::string Desc) {
  return std::make_unique<GenericNamedTaskImpl<FnT>>(std::forward<FnT>(Fn),
                                                     std::move(Desc));
}

/// Create a generic named task from a const char * description.
template <typename FnT>
std::unique_ptr<GenericNamedTask>
makeGenericNamedTask(FnT &&Fn, const char *Desc = nullptr) {
  if (!Desc)
    Desc = GenericNamedTask::DefaultDescription;
  return std::make_unique<GenericNamedTaskImpl<FnT>>(std::forward<FnT>(Fn),
                                                     Desc);
}

/// Abstract base for classes that dispatch ORC Tasks.
class TaskDispatcher {
public:
  virtual ~TaskDispatcher();

  /// Run the given task.
  virtual void dispatch(std::unique_ptr<Task> T) = 0;

  /// Called by ExecutionSession. Waits until all tasks have completed.
  virtual void shutdown() = 0;
};

/// Runs all tasks on the current thread.
class InPlaceTaskDispatcher : public TaskDispatcher {
public:
  void dispatch(std::unique_ptr<Task> T) override;
  void shutdown() override;
};

#if LLVM_ENABLE_THREADS

class DynamicThreadPoolTaskDispatcher : public TaskDispatcher {
public:
  DynamicThreadPoolTaskDispatcher(
      std::optional<size_t> MaxMaterializationThreads)
      : MaxMaterializationThreads(MaxMaterializationThreads) {}
  void dispatch(std::unique_ptr<Task> T) override;
  void shutdown() override;
private:
  std::mutex DispatchMutex;
  bool Running = true;
  size_t Outstanding = 0;
  std::condition_variable OutstandingCV;

  std::optional<size_t> MaxMaterializationThreads;
  size_t NumMaterializationThreads = 0;
  std::deque<std::unique_ptr<Task>> MaterializationTaskQueue;
};

#endif // LLVM_ENABLE_THREADS

} // End namespace orc
} // End namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_TASKDISPATCH_H
