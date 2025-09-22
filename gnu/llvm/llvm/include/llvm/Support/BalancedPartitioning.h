//===- BalancedPartitioning.h ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements BalancedPartitioning, a recursive balanced graph
// partitioning algorithm.
//
// The algorithm is used to find an ordering of FunctionNodes while optimizing
// a specified objective. The algorithm uses recursive bisection; it starts
// with a collection of unordered FunctionNodes and tries to split them into
// two sets (buckets) of equal cardinality. Each bisection step is comprised of
// iterations that greedily swap the FunctionNodes between the two buckets while
// there is an improvement of the objective. Once the process converges, the
// problem is divided into two sub-problems of half the size, which are
// recursively applied for the two buckets. The final ordering of the
// FunctionNodes is obtained by concatenating the two (recursively computed)
// orderings.
//
// In order to speed up the computation, we limit the depth of the recursive
// tree by a specified constant (SplitDepth) and apply at most a constant
// number of greedy iterations per split (IterationsPerSplit). The worst-case
// time complexity of the implementation is bounded by O(M*log^2 N), where
// N is the number of FunctionNodes and M is the number of
// FunctionNode-UtilityNode edges; (assuming that any collection of D
// FunctionNodes contains O(D) UtilityNodes). Notice that the two different
// recursive sub-problems are independent and thus can be efficiently processed
// in parallel.
//
// Reference:
//   * Optimizing Function Layout for Mobile Applications,
//     https://arxiv.org/abs/2211.09285
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_BALANCED_PARTITIONING_H
#define LLVM_SUPPORT_BALANCED_PARTITIONING_H

#include "raw_ostream.h"
#include "llvm/ADT/ArrayRef.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <random>
#include <vector>

namespace llvm {

class ThreadPoolInterface;
/// A function with a set of utility nodes where it is beneficial to order two
/// functions close together if they have similar utility nodes
class BPFunctionNode {
  friend class BalancedPartitioning;

public:
  using IDT = uint64_t;
  using UtilityNodeT = uint32_t;

  /// \param UtilityNodes the set of utility nodes (must be unique'd)
  BPFunctionNode(IDT Id, ArrayRef<UtilityNodeT> UtilityNodes)
      : Id(Id), UtilityNodes(UtilityNodes) {}

  /// The ID of this node
  IDT Id;

  void dump(raw_ostream &OS) const;

protected:
  /// The list of utility nodes associated with this node
  SmallVector<UtilityNodeT, 4> UtilityNodes;
  /// The bucket assigned by balanced partitioning
  std::optional<unsigned> Bucket;
  /// The index of the input order of the FunctionNodes
  uint64_t InputOrderIndex = 0;

  friend class BPFunctionNodeTest_Basic_Test;
  friend class BalancedPartitioningTest_Basic_Test;
  friend class BalancedPartitioningTest_Large_Test;
};

/// Algorithm parameters; default values are tuned on real-world binaries
struct BalancedPartitioningConfig {
  /// The depth of the recursive bisection
  unsigned SplitDepth = 18;
  /// The maximum number of bp iterations per split
  unsigned IterationsPerSplit = 40;
  /// The probability for a vertex to skip a move from its current bucket to
  /// another bucket; it often helps to escape from a local optima
  float SkipProbability = 0.1f;
  /// Recursive subtasks up to the given depth are added to the queue and
  /// distributed among threads by ThreadPool; all subsequent calls are executed
  /// on the same thread
  unsigned TaskSplitDepth = 9;
};

class BalancedPartitioning {
public:
  BalancedPartitioning(const BalancedPartitioningConfig &Config);

  /// Run recursive graph partitioning that optimizes a given objective.
  void run(std::vector<BPFunctionNode> &Nodes) const;

private:
  struct UtilitySignature;
  using SignaturesT = SmallVector<UtilitySignature, 4>;
  using FunctionNodeRange =
      iterator_range<std::vector<BPFunctionNode>::iterator>;

  /// A special ThreadPool that allows for spawning new tasks after blocking on
  /// wait(). BalancedPartitioning recursively spawns new threads inside other
  /// threads, so we need to track how many active threads that could spawn more
  /// threads.
  struct BPThreadPool {
    ThreadPoolInterface &TheThreadPool;
    std::mutex mtx;
    std::condition_variable cv;
    /// The number of threads that could spawn more threads
    std::atomic<int> NumActiveThreads = 0;
    /// Only true when all threads are down spawning new threads
    bool IsFinishedSpawning = false;
    /// Asynchronous submission of the task to the pool
    template <typename Func> void async(Func &&F);
    /// Blocking wait for all threads to complete. Unlike ThreadPool, it is
    /// acceptable for other threads to add more tasks while blocking on this
    /// call.
    void wait();
    BPThreadPool(ThreadPoolInterface &TheThreadPool)
        : TheThreadPool(TheThreadPool) {}
  };

  /// Run a recursive bisection of a given list of FunctionNodes
  /// \param RecDepth the current depth of recursion
  /// \param RootBucket the initial bucket of the dataVertices
  /// \param Offset the assigned buckets are the range [Offset, Offset +
  /// Nodes.size()]
  void bisect(const FunctionNodeRange Nodes, unsigned RecDepth,
              unsigned RootBucket, unsigned Offset,
              std::optional<BPThreadPool> &TP) const;

  /// Run bisection iterations
  void runIterations(const FunctionNodeRange Nodes, unsigned LeftBucket,
                     unsigned RightBucket, std::mt19937 &RNG) const;

  /// Run a bisection iteration to improve the optimization goal
  /// \returns the total number of moved FunctionNodes
  unsigned runIteration(const FunctionNodeRange Nodes, unsigned LeftBucket,
                        unsigned RightBucket, SignaturesT &Signatures,
                        std::mt19937 &RNG) const;

  /// Try to move \p N from one bucket to another
  /// \returns true iff \p N is moved
  bool moveFunctionNode(BPFunctionNode &N, unsigned LeftBucket,
                        unsigned RightBucket, SignaturesT &Signatures,
                        std::mt19937 &RNG) const;

  /// Split all the FunctionNodes into 2 buckets, StartBucket and StartBucket +
  /// 1 The method is used for an initial assignment before a bisection step
  void split(const FunctionNodeRange Nodes, unsigned StartBucket) const;

  /// The cost of the uniform log-gap cost, assuming a utility node has \p X
  /// FunctionNodes in the left bucket and \p Y FunctionNodes in the right one.
  float logCost(unsigned X, unsigned Y) const;

  float log2Cached(unsigned i) const;

  const BalancedPartitioningConfig &Config;

  /// Precomputed values of log2(x). Table size is small enough to fit in cache.
  static constexpr unsigned LOG_CACHE_SIZE = 16384;
  float Log2Cache[LOG_CACHE_SIZE];

  /// The signature of a particular utility node used for the bisection step,
  /// i.e., the number of \p FunctionNodes in each of the two buckets
  struct UtilitySignature {
    /// The number of \p FunctionNodes in the left bucket
    unsigned LeftCount = 0;
    /// The number of \p FunctionNodes in the right bucket
    unsigned RightCount = 0;
    /// The cached gain of moving a \p FunctionNode from the left bucket to the
    /// right bucket
    float CachedGainLR;
    /// The cached gain of moving a \p FunctionNode from the right bucket to the
    /// left bucket
    float CachedGainRL;
    /// Whether \p CachedGainLR and \p CachedGainRL are valid
    bool CachedGainIsValid = false;
  };

protected:
  /// Compute the move gain for uniform log-gap cost
  static float moveGain(const BPFunctionNode &N, bool FromLeftToRight,
                        const SignaturesT &Signatures);
  friend class BalancedPartitioningTest_MoveGain_Test;
};

} // end namespace llvm

#endif // LLVM_SUPPORT_BALANCED_PARTITIONING_H
