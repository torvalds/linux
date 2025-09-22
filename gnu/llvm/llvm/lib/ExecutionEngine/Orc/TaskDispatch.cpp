//===------------ TaskDispatch.cpp - ORC task dispatch utils --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/TaskDispatch.h"
#include "llvm/ExecutionEngine/Orc/Core.h"

namespace llvm {
namespace orc {

char Task::ID = 0;
char GenericNamedTask::ID = 0;
const char *GenericNamedTask::DefaultDescription = "Generic Task";

void Task::anchor() {}
TaskDispatcher::~TaskDispatcher() = default;

void InPlaceTaskDispatcher::dispatch(std::unique_ptr<Task> T) { T->run(); }

void InPlaceTaskDispatcher::shutdown() {}

#if LLVM_ENABLE_THREADS
void DynamicThreadPoolTaskDispatcher::dispatch(std::unique_ptr<Task> T) {
  bool IsMaterializationTask = isa<MaterializationTask>(*T);

  {
    std::lock_guard<std::mutex> Lock(DispatchMutex);

    if (IsMaterializationTask) {

      // If this is a materialization task and there are too many running
      // already then queue this one up and return early.
      if (MaxMaterializationThreads &&
          NumMaterializationThreads == *MaxMaterializationThreads) {
        MaterializationTaskQueue.push_back(std::move(T));
        return;
      }

      // Otherwise record that we have a materialization task running.
      ++NumMaterializationThreads;
    }

    ++Outstanding;
  }

  std::thread([this, T = std::move(T), IsMaterializationTask]() mutable {
    while (true) {

      // Run the task.
      T->run();

      std::lock_guard<std::mutex> Lock(DispatchMutex);
      if (!MaterializationTaskQueue.empty()) {
        // If there are any materialization tasks running then steal that work.
        T = std::move(MaterializationTaskQueue.front());
        MaterializationTaskQueue.pop_front();
        if (!IsMaterializationTask) {
          ++NumMaterializationThreads;
          IsMaterializationTask = true;
        }
      } else {
        // Otherwise decrement work counters.
        if (IsMaterializationTask)
          --NumMaterializationThreads;
        --Outstanding;
        OutstandingCV.notify_all();
        return;
      }
    }
  }).detach();
}

void DynamicThreadPoolTaskDispatcher::shutdown() {
  std::unique_lock<std::mutex> Lock(DispatchMutex);
  Running = false;
  OutstandingCV.wait(Lock, [this]() { return Outstanding == 0; });
}
#endif

} // namespace orc
} // namespace llvm
