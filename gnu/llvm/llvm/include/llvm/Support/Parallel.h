//===- llvm/Support/Parallel.h - Parallel algorithms ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_PARALLEL_H
#define LLVM_SUPPORT_PARALLEL_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Threading.h"

#include <algorithm>
#include <condition_variable>
#include <functional>
#include <mutex>

namespace llvm {

namespace parallel {

// Strategy for the default executor used by the parallel routines provided by
// this file. It defaults to using all hardware threads and should be
// initialized before the first use of parallel routines.
extern ThreadPoolStrategy strategy;

#if LLVM_ENABLE_THREADS
#define GET_THREAD_INDEX_IMPL                                                  \
  if (parallel::strategy.ThreadsRequested == 1)                                \
    return 0;                                                                  \
  assert((threadIndex != UINT_MAX) &&                                          \
         "getThreadIndex() must be called from a thread created by "           \
         "ThreadPoolExecutor");                                                \
  return threadIndex;

#ifdef _WIN32
// Direct access to thread_local variables from a different DLL isn't
// possible with Windows Native TLS.
unsigned getThreadIndex();
#else
// Don't access this directly, use the getThreadIndex wrapper.
extern thread_local unsigned threadIndex;

inline unsigned getThreadIndex() { GET_THREAD_INDEX_IMPL; }
#endif

size_t getThreadCount();
#else
inline unsigned getThreadIndex() { return 0; }
inline size_t getThreadCount() { return 1; }
#endif

namespace detail {
class Latch {
  uint32_t Count;
  mutable std::mutex Mutex;
  mutable std::condition_variable Cond;

public:
  explicit Latch(uint32_t Count = 0) : Count(Count) {}
  ~Latch() {
    // Ensure at least that sync() was called.
    assert(Count == 0);
  }

  void inc() {
    std::lock_guard<std::mutex> lock(Mutex);
    ++Count;
  }

  void dec() {
    std::lock_guard<std::mutex> lock(Mutex);
    if (--Count == 0)
      Cond.notify_all();
  }

  void sync() const {
    std::unique_lock<std::mutex> lock(Mutex);
    Cond.wait(lock, [&] { return Count == 0; });
  }
};
} // namespace detail

class TaskGroup {
  detail::Latch L;
  bool Parallel;

public:
  TaskGroup();
  ~TaskGroup();

  // Spawn a task, but does not wait for it to finish.
  // Tasks marked with \p Sequential will be executed
  // exactly in the order which they were spawned.
  // Note: Sequential tasks may be executed on different
  // threads, but strictly in sequential order.
  void spawn(std::function<void()> f, bool Sequential = false);

  void sync() const { L.sync(); }

  bool isParallel() const { return Parallel; }
};

namespace detail {

#if LLVM_ENABLE_THREADS
const ptrdiff_t MinParallelSize = 1024;

/// Inclusive median.
template <class RandomAccessIterator, class Comparator>
RandomAccessIterator medianOf3(RandomAccessIterator Start,
                               RandomAccessIterator End,
                               const Comparator &Comp) {
  RandomAccessIterator Mid = Start + (std::distance(Start, End) / 2);
  return Comp(*Start, *(End - 1))
             ? (Comp(*Mid, *(End - 1)) ? (Comp(*Start, *Mid) ? Mid : Start)
                                       : End - 1)
             : (Comp(*Mid, *Start) ? (Comp(*(End - 1), *Mid) ? Mid : End - 1)
                                   : Start);
}

template <class RandomAccessIterator, class Comparator>
void parallel_quick_sort(RandomAccessIterator Start, RandomAccessIterator End,
                         const Comparator &Comp, TaskGroup &TG, size_t Depth) {
  // Do a sequential sort for small inputs.
  if (std::distance(Start, End) < detail::MinParallelSize || Depth == 0) {
    llvm::sort(Start, End, Comp);
    return;
  }

  // Partition.
  auto Pivot = medianOf3(Start, End, Comp);
  // Move Pivot to End.
  std::swap(*(End - 1), *Pivot);
  Pivot = std::partition(Start, End - 1, [&Comp, End](decltype(*Start) V) {
    return Comp(V, *(End - 1));
  });
  // Move Pivot to middle of partition.
  std::swap(*Pivot, *(End - 1));

  // Recurse.
  TG.spawn([=, &Comp, &TG] {
    parallel_quick_sort(Start, Pivot, Comp, TG, Depth - 1);
  });
  parallel_quick_sort(Pivot + 1, End, Comp, TG, Depth - 1);
}

template <class RandomAccessIterator, class Comparator>
void parallel_sort(RandomAccessIterator Start, RandomAccessIterator End,
                   const Comparator &Comp) {
  TaskGroup TG;
  parallel_quick_sort(Start, End, Comp, TG,
                      llvm::Log2_64(std::distance(Start, End)) + 1);
}

// TaskGroup has a relatively high overhead, so we want to reduce
// the number of spawn() calls. We'll create up to 1024 tasks here.
// (Note that 1024 is an arbitrary number. This code probably needs
// improving to take the number of available cores into account.)
enum { MaxTasksPerGroup = 1024 };

template <class IterTy, class ResultTy, class ReduceFuncTy,
          class TransformFuncTy>
ResultTy parallel_transform_reduce(IterTy Begin, IterTy End, ResultTy Init,
                                   ReduceFuncTy Reduce,
                                   TransformFuncTy Transform) {
  // Limit the number of tasks to MaxTasksPerGroup to limit job scheduling
  // overhead on large inputs.
  size_t NumInputs = std::distance(Begin, End);
  if (NumInputs == 0)
    return std::move(Init);
  size_t NumTasks = std::min(static_cast<size_t>(MaxTasksPerGroup), NumInputs);
  std::vector<ResultTy> Results(NumTasks, Init);
  {
    // Each task processes either TaskSize or TaskSize+1 inputs. Any inputs
    // remaining after dividing them equally amongst tasks are distributed as
    // one extra input over the first tasks.
    TaskGroup TG;
    size_t TaskSize = NumInputs / NumTasks;
    size_t RemainingInputs = NumInputs % NumTasks;
    IterTy TBegin = Begin;
    for (size_t TaskId = 0; TaskId < NumTasks; ++TaskId) {
      IterTy TEnd = TBegin + TaskSize + (TaskId < RemainingInputs ? 1 : 0);
      TG.spawn([=, &Transform, &Reduce, &Results] {
        // Reduce the result of transformation eagerly within each task.
        ResultTy R = Init;
        for (IterTy It = TBegin; It != TEnd; ++It)
          R = Reduce(R, Transform(*It));
        Results[TaskId] = R;
      });
      TBegin = TEnd;
    }
    assert(TBegin == End);
  }

  // Do a final reduction. There are at most 1024 tasks, so this only adds
  // constant single-threaded overhead for large inputs. Hopefully most
  // reductions are cheaper than the transformation.
  ResultTy FinalResult = std::move(Results.front());
  for (ResultTy &PartialResult :
       MutableArrayRef(Results.data() + 1, Results.size() - 1))
    FinalResult = Reduce(FinalResult, std::move(PartialResult));
  return std::move(FinalResult);
}

#endif

} // namespace detail
} // namespace parallel

template <class RandomAccessIterator,
          class Comparator = std::less<
              typename std::iterator_traits<RandomAccessIterator>::value_type>>
void parallelSort(RandomAccessIterator Start, RandomAccessIterator End,
                  const Comparator &Comp = Comparator()) {
#if LLVM_ENABLE_THREADS
  if (parallel::strategy.ThreadsRequested != 1) {
    parallel::detail::parallel_sort(Start, End, Comp);
    return;
  }
#endif
  llvm::sort(Start, End, Comp);
}

void parallelFor(size_t Begin, size_t End, function_ref<void(size_t)> Fn);

template <class IterTy, class FuncTy>
void parallelForEach(IterTy Begin, IterTy End, FuncTy Fn) {
  parallelFor(0, End - Begin, [&](size_t I) { Fn(Begin[I]); });
}

template <class IterTy, class ResultTy, class ReduceFuncTy,
          class TransformFuncTy>
ResultTy parallelTransformReduce(IterTy Begin, IterTy End, ResultTy Init,
                                 ReduceFuncTy Reduce,
                                 TransformFuncTy Transform) {
#if LLVM_ENABLE_THREADS
  if (parallel::strategy.ThreadsRequested != 1) {
    return parallel::detail::parallel_transform_reduce(Begin, End, Init, Reduce,
                                                       Transform);
  }
#endif
  for (IterTy I = Begin; I != End; ++I)
    Init = Reduce(std::move(Init), Transform(*I));
  return std::move(Init);
}

// Range wrappers.
template <class RangeTy,
          class Comparator = std::less<decltype(*std::begin(RangeTy()))>>
void parallelSort(RangeTy &&R, const Comparator &Comp = Comparator()) {
  parallelSort(std::begin(R), std::end(R), Comp);
}

template <class RangeTy, class FuncTy>
void parallelForEach(RangeTy &&R, FuncTy Fn) {
  parallelForEach(std::begin(R), std::end(R), Fn);
}

template <class RangeTy, class ResultTy, class ReduceFuncTy,
          class TransformFuncTy>
ResultTy parallelTransformReduce(RangeTy &&R, ResultTy Init,
                                 ReduceFuncTy Reduce,
                                 TransformFuncTy Transform) {
  return parallelTransformReduce(std::begin(R), std::end(R), Init, Reduce,
                                 Transform);
}

// Parallel for-each, but with error handling.
template <class RangeTy, class FuncTy>
Error parallelForEachError(RangeTy &&R, FuncTy Fn) {
  // The transform_reduce algorithm requires that the initial value be copyable.
  // Error objects are uncopyable. We only need to copy initial success values,
  // so work around this mismatch via the C API. The C API represents success
  // values with a null pointer. The joinErrors discards null values and joins
  // multiple errors into an ErrorList.
  return unwrap(parallelTransformReduce(
      std::begin(R), std::end(R), wrap(Error::success()),
      [](LLVMErrorRef Lhs, LLVMErrorRef Rhs) {
        return wrap(joinErrors(unwrap(Lhs), unwrap(Rhs)));
      },
      [&Fn](auto &&V) { return wrap(Fn(V)); }));
}

} // namespace llvm

#endif // LLVM_SUPPORT_PARALLEL_H
