//===-- sanitizer_stackdepotbase.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation of a mapping from arbitrary values to unique 32-bit
// identifiers.
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_STACKDEPOTBASE_H
#define SANITIZER_STACKDEPOTBASE_H

#include <stdio.h>

#include "sanitizer_atomic.h"
#include "sanitizer_flat_map.h"
#include "sanitizer_internal_defs.h"
#include "sanitizer_mutex.h"

namespace __sanitizer {

template <class Node, int kReservedBits, int kTabSizeLog>
class StackDepotBase {
  static constexpr u32 kIdSizeLog =
      sizeof(u32) * 8 - Max(kReservedBits, 1 /* At least 1 bit for locking. */);
  static constexpr u32 kNodesSize1Log = kIdSizeLog / 2;
  static constexpr u32 kNodesSize2Log = kIdSizeLog - kNodesSize1Log;
  static constexpr int kTabSize = 1 << kTabSizeLog;  // Hash table size.
  static constexpr u32 kUnlockMask = (1ull << kIdSizeLog) - 1;
  static constexpr u32 kLockMask = ~kUnlockMask;

 public:
  typedef typename Node::args_type args_type;
  typedef typename Node::handle_type handle_type;
  typedef typename Node::hash_type hash_type;

  static constexpr u64 kNodesSize1 = 1ull << kNodesSize1Log;
  static constexpr u64 kNodesSize2 = 1ull << kNodesSize2Log;

  // Maps stack trace to an unique id.
  u32 Put(args_type args, bool *inserted = nullptr);
  // Retrieves a stored stack trace by the id.
  args_type Get(u32 id);

  StackDepotStats GetStats() const {
    return {
        atomic_load_relaxed(&n_uniq_ids),
        nodes.MemoryUsage() + Node::allocated(),
    };
  }

  void LockBeforeFork();
  void UnlockAfterFork(bool fork_child);
  void PrintAll();

  void TestOnlyUnmap() {
    nodes.TestOnlyUnmap();
    internal_memset(this, 0, sizeof(*this));
  }

 private:
  friend Node;
  u32 find(u32 s, args_type args, hash_type hash) const;
  static u32 lock(atomic_uint32_t *p);
  static void unlock(atomic_uint32_t *p, u32 s);
  atomic_uint32_t tab[kTabSize];  // Hash table of Node's.

  atomic_uint32_t n_uniq_ids;

  TwoLevelMap<Node, kNodesSize1, kNodesSize2> nodes;

  friend class StackDepotReverseMap;
};

template <class Node, int kReservedBits, int kTabSizeLog>
u32 StackDepotBase<Node, kReservedBits, kTabSizeLog>::find(
    u32 s, args_type args, hash_type hash) const {
  // Searches linked list s for the stack, returns its id.
  for (; s;) {
    const Node &node = nodes[s];
    if (node.eq(hash, args))
      return s;
    s = node.link;
  }
  return 0;
}

template <class Node, int kReservedBits, int kTabSizeLog>
u32 StackDepotBase<Node, kReservedBits, kTabSizeLog>::lock(atomic_uint32_t *p) {
  // Uses the pointer lsb as mutex.
  for (int i = 0;; i++) {
    u32 cmp = atomic_load(p, memory_order_relaxed);
    if ((cmp & kLockMask) == 0 &&
        atomic_compare_exchange_weak(p, &cmp, cmp | kLockMask,
                                     memory_order_acquire))
      return cmp;
    if (i < 10)
      proc_yield(10);
    else
      internal_sched_yield();
  }
}

template <class Node, int kReservedBits, int kTabSizeLog>
void StackDepotBase<Node, kReservedBits, kTabSizeLog>::unlock(
    atomic_uint32_t *p, u32 s) {
  DCHECK_EQ(s & kLockMask, 0);
  atomic_store(p, s, memory_order_release);
}

template <class Node, int kReservedBits, int kTabSizeLog>
u32 StackDepotBase<Node, kReservedBits, kTabSizeLog>::Put(args_type args,
                                                          bool *inserted) {
  if (inserted)
    *inserted = false;
  if (!LIKELY(Node::is_valid(args)))
    return 0;
  hash_type h = Node::hash(args);
  atomic_uint32_t *p = &tab[h % kTabSize];
  u32 v = atomic_load(p, memory_order_consume);
  u32 s = v & kUnlockMask;
  // First, try to find the existing stack.
  u32 node = find(s, args, h);
  if (LIKELY(node))
    return node;

  // If failed, lock, retry and insert new.
  u32 s2 = lock(p);
  if (s2 != s) {
    node = find(s2, args, h);
    if (node) {
      unlock(p, s2);
      return node;
    }
  }
  s = atomic_fetch_add(&n_uniq_ids, 1, memory_order_relaxed) + 1;
  CHECK_EQ(s & kUnlockMask, s);
  CHECK_EQ(s & (((u32)-1) >> kReservedBits), s);
  Node &new_node = nodes[s];
  new_node.store(s, args, h);
  new_node.link = s2;
  unlock(p, s);
  if (inserted) *inserted = true;
  return s;
}

template <class Node, int kReservedBits, int kTabSizeLog>
typename StackDepotBase<Node, kReservedBits, kTabSizeLog>::args_type
StackDepotBase<Node, kReservedBits, kTabSizeLog>::Get(u32 id) {
  if (id == 0)
    return args_type();
  CHECK_EQ(id & (((u32)-1) >> kReservedBits), id);
  if (!nodes.contains(id))
    return args_type();
  const Node &node = nodes[id];
  return node.load(id);
}

template <class Node, int kReservedBits, int kTabSizeLog>
void StackDepotBase<Node, kReservedBits, kTabSizeLog>::LockBeforeFork() {
  // Do not lock hash table. It's very expensive, but it's not rely needed. The
  // parent process will neither lock nor unlock. Child process risks to be
  // deadlocked on already locked buckets. To avoid deadlock we will unlock
  // every locked buckets in `UnlockAfterFork`. This may affect consistency of
  // the hash table, but the only issue is a few items inserted by parent
  // process will be not found by child, and the child may insert them again,
  // wasting some space in `stackStore`.

  // We still need to lock nodes.
  nodes.Lock();
}

template <class Node, int kReservedBits, int kTabSizeLog>
void StackDepotBase<Node, kReservedBits, kTabSizeLog>::UnlockAfterFork(
    bool fork_child) {
  nodes.Unlock();

  // Only unlock in child process to avoid deadlock. See `LockBeforeFork`.
  if (!fork_child)
    return;

  for (int i = 0; i < kTabSize; ++i) {
    atomic_uint32_t *p = &tab[i];
    uptr s = atomic_load(p, memory_order_relaxed);
    if (s & kLockMask)
      unlock(p, s & kUnlockMask);
  }
}

template <class Node, int kReservedBits, int kTabSizeLog>
void StackDepotBase<Node, kReservedBits, kTabSizeLog>::PrintAll() {
  for (int i = 0; i < kTabSize; ++i) {
    atomic_uint32_t *p = &tab[i];
    u32 s = atomic_load(p, memory_order_consume) & kUnlockMask;
    for (; s;) {
      const Node &node = nodes[s];
      Printf("Stack for id %u:\n", s);
      node.load(s).Print();
      s = node.link;
    }
  }
}

} // namespace __sanitizer

#endif // SANITIZER_STACKDEPOTBASE_H
