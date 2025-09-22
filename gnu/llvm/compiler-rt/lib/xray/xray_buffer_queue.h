//===-- xray_buffer_queue.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a dynamic runtime instrumentation system.
//
// Defines the interface for a buffer queue implementation.
//
//===----------------------------------------------------------------------===//
#ifndef XRAY_BUFFER_QUEUE_H
#define XRAY_BUFFER_QUEUE_H

#include "sanitizer_common/sanitizer_atomic.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_mutex.h"
#include "xray_defs.h"
#include <cstddef>
#include <cstdint>

namespace __xray {

/// BufferQueue implements a circular queue of fixed sized buffers (much like a
/// freelist) but is concerned with making it quick to initialise, finalise, and
/// get from or return buffers to the queue. This is one key component of the
/// "flight data recorder" (FDR) mode to support ongoing XRay function call
/// trace collection.
class BufferQueue {
public:
  /// ControlBlock represents the memory layout of how we interpret the backing
  /// store for all buffers and extents managed by a BufferQueue instance. The
  /// ControlBlock has the reference count as the first member, sized according
  /// to platform-specific cache-line size. We never use the Buffer member of
  /// the union, which is only there for compiler-supported alignment and
  /// sizing.
  ///
  /// This ensures that the `Data` member will be placed at least kCacheLineSize
  /// bytes from the beginning of the structure.
  struct ControlBlock {
    union {
      atomic_uint64_t RefCount;
      char Buffer[kCacheLineSize];
    };

    /// We need to make this size 1, to conform to the C++ rules for array data
    /// members. Typically, we want to subtract this 1 byte for sizing
    /// information.
    char Data[1];
  };

  struct Buffer {
    atomic_uint64_t *Extents = nullptr;
    uint64_t Generation{0};
    void *Data = nullptr;
    size_t Size = 0;

  private:
    friend class BufferQueue;
    ControlBlock *BackingStore = nullptr;
    ControlBlock *ExtentsBackingStore = nullptr;
    size_t Count = 0;
  };

  struct BufferRep {
    // The managed buffer.
    Buffer Buff;

    // This is true if the buffer has been returned to the available queue, and
    // is considered "used" by another thread.
    bool Used = false;
  };

private:
  // This models a ForwardIterator. |T| Must be either a `Buffer` or `const
  // Buffer`. Note that we only advance to the "used" buffers, when
  // incrementing, so that at dereference we're always at a valid point.
  template <class T> class Iterator {
  public:
    BufferRep *Buffers = nullptr;
    size_t Offset = 0;
    size_t Max = 0;

    Iterator &operator++() {
      DCHECK_NE(Offset, Max);
      do {
        ++Offset;
      } while (Offset != Max && !Buffers[Offset].Used);
      return *this;
    }

    Iterator operator++(int) {
      Iterator C = *this;
      ++(*this);
      return C;
    }

    T &operator*() const { return Buffers[Offset].Buff; }

    T *operator->() const { return &(Buffers[Offset].Buff); }

    Iterator(BufferRep *Root, size_t O, size_t M) XRAY_NEVER_INSTRUMENT
        : Buffers(Root),
          Offset(O),
          Max(M) {
      // We want to advance to the first Offset where the 'Used' property is
      // true, or to the end of the list/queue.
      while (Offset != Max && !Buffers[Offset].Used) {
        ++Offset;
      }
    }

    Iterator() = default;
    Iterator(const Iterator &) = default;
    Iterator(Iterator &&) = default;
    Iterator &operator=(const Iterator &) = default;
    Iterator &operator=(Iterator &&) = default;
    ~Iterator() = default;

    template <class V>
    friend bool operator==(const Iterator &L, const Iterator<V> &R) {
      DCHECK_EQ(L.Max, R.Max);
      return L.Buffers == R.Buffers && L.Offset == R.Offset;
    }

    template <class V>
    friend bool operator!=(const Iterator &L, const Iterator<V> &R) {
      return !(L == R);
    }
  };

  // Size of each individual Buffer.
  size_t BufferSize;

  // Amount of pre-allocated buffers.
  size_t BufferCount;

  SpinMutex Mutex;
  atomic_uint8_t Finalizing;

  // The collocated ControlBlock and buffer storage.
  ControlBlock *BackingStore;

  // The collocated ControlBlock and extents storage.
  ControlBlock *ExtentsBackingStore;

  // A dynamically allocated array of BufferRep instances.
  BufferRep *Buffers;

  // Pointer to the next buffer to be handed out.
  BufferRep *Next;

  // Pointer to the entry in the array where the next released buffer will be
  // placed.
  BufferRep *First;

  // Count of buffers that have been handed out through 'getBuffer'.
  size_t LiveBuffers;

  // We use a generation number to identify buffers and which generation they're
  // associated with.
  atomic_uint64_t Generation;

  /// Releases references to the buffers backed by the current buffer queue.
  void cleanupBuffers();

public:
  enum class ErrorCode : unsigned {
    Ok,
    NotEnoughMemory,
    QueueFinalizing,
    UnrecognizedBuffer,
    AlreadyFinalized,
    AlreadyInitialized,
  };

  static const char *getErrorString(ErrorCode E) {
    switch (E) {
    case ErrorCode::Ok:
      return "(none)";
    case ErrorCode::NotEnoughMemory:
      return "no available buffers in the queue";
    case ErrorCode::QueueFinalizing:
      return "queue already finalizing";
    case ErrorCode::UnrecognizedBuffer:
      return "buffer being returned not owned by buffer queue";
    case ErrorCode::AlreadyFinalized:
      return "queue already finalized";
    case ErrorCode::AlreadyInitialized:
      return "queue already initialized";
    }
    return "unknown error";
  }

  /// Initialise a queue of size |N| with buffers of size |B|. We report success
  /// through |Success|.
  BufferQueue(size_t B, size_t N, bool &Success);

  /// Updates |Buf| to contain the pointer to an appropriate buffer. Returns an
  /// error in case there are no available buffers to return when we will run
  /// over the upper bound for the total buffers.
  ///
  /// Requirements:
  ///   - BufferQueue is not finalising.
  ///
  /// Returns:
  ///   - ErrorCode::NotEnoughMemory on exceeding MaxSize.
  ///   - ErrorCode::Ok when we find a Buffer.
  ///   - ErrorCode::QueueFinalizing or ErrorCode::AlreadyFinalized on
  ///     a finalizing/finalized BufferQueue.
  ErrorCode getBuffer(Buffer &Buf);

  /// Updates |Buf| to point to nullptr, with size 0.
  ///
  /// Returns:
  ///   - ErrorCode::Ok when we successfully release the buffer.
  ///   - ErrorCode::UnrecognizedBuffer for when this BufferQueue does not own
  ///     the buffer being released.
  ErrorCode releaseBuffer(Buffer &Buf);

  /// Initializes the buffer queue, starting a new generation. We can re-set the
  /// size of buffers with |BS| along with the buffer count with |BC|.
  ///
  /// Returns:
  ///   - ErrorCode::Ok when we successfully initialize the buffer. This
  ///   requires that the buffer queue is previously finalized.
  ///   - ErrorCode::AlreadyInitialized when the buffer queue is not finalized.
  ErrorCode init(size_t BS, size_t BC);

  bool finalizing() const {
    return atomic_load(&Finalizing, memory_order_acquire);
  }

  uint64_t generation() const {
    return atomic_load(&Generation, memory_order_acquire);
  }

  /// Returns the configured size of the buffers in the buffer queue.
  size_t ConfiguredBufferSize() const { return BufferSize; }

  /// Sets the state of the BufferQueue to finalizing, which ensures that:
  ///
  ///   - All subsequent attempts to retrieve a Buffer will fail.
  ///   - All releaseBuffer operations will not fail.
  ///
  /// After a call to finalize succeeds, all subsequent calls to finalize will
  /// fail with ErrorCode::QueueFinalizing.
  ErrorCode finalize();

  /// Applies the provided function F to each Buffer in the queue, only if the
  /// Buffer is marked 'used' (i.e. has been the result of getBuffer(...) and a
  /// releaseBuffer(...) operation).
  template <class F> void apply(F Fn) XRAY_NEVER_INSTRUMENT {
    SpinMutexLock G(&Mutex);
    for (auto I = begin(), E = end(); I != E; ++I)
      Fn(*I);
  }

  using const_iterator = Iterator<const Buffer>;
  using iterator = Iterator<Buffer>;

  /// Provides iterator access to the raw Buffer instances.
  iterator begin() const { return iterator(Buffers, 0, BufferCount); }
  const_iterator cbegin() const {
    return const_iterator(Buffers, 0, BufferCount);
  }
  iterator end() const { return iterator(Buffers, BufferCount, BufferCount); }
  const_iterator cend() const {
    return const_iterator(Buffers, BufferCount, BufferCount);
  }

  // Cleans up allocated buffers.
  ~BufferQueue();
};

} // namespace __xray

#endif // XRAY_BUFFER_QUEUE_H
