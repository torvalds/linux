//===- LazyAtomicPointer.----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_LAZYATOMICPOINTER_H
#define LLVM_ADT_LAZYATOMICPOINTER_H

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/Support/Compiler.h"
#include <assert.h>
#include <atomic>

namespace llvm {

/// Atomic pointer that's lock-free, but that can coordinate concurrent writes
/// from a lazy generator. Should be reserved for cases where concurrent uses of
/// a generator for the same storage is unlikely.
///
/// The laziness comes in with \a loadOrGenerate(), which lazily calls the
/// provided generator ONLY when the value is currently \c nullptr. With
/// concurrent calls, only one generator is called and the rest see that value.
///
/// Most other APIs treat an in-flight \a loadOrGenerate() as if \c nullptr
/// were stored. APIs that are required to write a value will spin.
///
/// The underlying storage is \a std::atomic<uintptr_t>.
///
/// TODO: In C++20, use std::atomic<T>::wait() instead of spinning and call
/// std::atomic<T>::notify_all() in \a loadOrGenerate().
template <class T> class LazyAtomicPointer {
  static constexpr uintptr_t getNull() { return 0; }
  static constexpr uintptr_t getBusy() { return UINTPTR_MAX; }

  static T *makePointer(uintptr_t Value) {
    assert(Value != getBusy());
    return Value ? reinterpret_cast<T *>(Value) : nullptr;
  }
  static uintptr_t makeRaw(T *Value) {
    uintptr_t Raw = Value ? reinterpret_cast<uintptr_t>(Value) : getNull();
    assert(Raw != getBusy());
    return Raw;
  }

public:
  /// Store a value. Waits for concurrent \a loadOrGenerate() calls.
  void store(T *Value) { return (void)exchange(Value); }

  /// Set a value. Return the old value. Waits for concurrent \a
  /// loadOrGenerate() calls.
  T *exchange(T *Value) {
    // Note: the call to compare_exchange_weak() fails "spuriously" if the
    // current value is \a getBusy(), causing the loop to spin.
    T *Old = nullptr;
    while (!compare_exchange_weak(Old, Value)) {
    }
    return Old;
  }

  /// Compare-exchange. Returns \c false if there is a concurrent \a
  /// loadOrGenerate() call, setting \p ExistingValue to \c nullptr.
  bool compare_exchange_weak(T *&ExistingValue, T *NewValue) {
    uintptr_t RawExistingValue = makeRaw(ExistingValue);
    if (Storage.compare_exchange_weak(RawExistingValue, makeRaw(NewValue)))
      return true;

    /// Report the existing value as "None" if busy.
    if (RawExistingValue == getBusy())
      ExistingValue = nullptr;
    else
      ExistingValue = makePointer(RawExistingValue);
    return false;
  }

  /// Compare-exchange. Keeps trying if there is a concurrent
  /// \a loadOrGenerate() call.
  bool compare_exchange_strong(T *&ExistingValue, T *NewValue) {
    uintptr_t RawExistingValue = makeRaw(ExistingValue);
    const uintptr_t OriginalRawExistingValue = RawExistingValue;
    if (Storage.compare_exchange_strong(RawExistingValue, makeRaw(NewValue)))
      return true;

    /// Keep trying as long as it's busy.
    if (LLVM_UNLIKELY(RawExistingValue == getBusy())) {
      while (RawExistingValue == getBusy()) {
        RawExistingValue = OriginalRawExistingValue;
        if (Storage.compare_exchange_weak(RawExistingValue, makeRaw(NewValue)))
          return true;
      }
    }
    ExistingValue = makePointer(RawExistingValue);
    return false;
  }

  /// Return the current stored value. Returns \a None if there is a concurrent
  /// \a loadOrGenerate() in flight.
  T *load() const {
    uintptr_t RawValue = Storage.load();
    return RawValue == getBusy() ? nullptr : makePointer(RawValue);
  }

  /// Get the current value, or call \p Generator to generate a value.
  /// Guarantees that only one thread's \p Generator will run.
  ///
  /// \pre \p Generator doesn't return \c nullptr.
  T &loadOrGenerate(function_ref<T *()> Generator) {
    // Return existing value, if already set.
    uintptr_t Raw = Storage.load();
    if (Raw != getNull() && Raw != getBusy())
      return *makePointer(Raw);

    // Try to mark as busy, then generate and store a new value.
    if (LLVM_LIKELY(Raw == getNull() &&
                    Storage.compare_exchange_strong(Raw, getBusy()))) {
      Raw = makeRaw(Generator());
      assert(Raw != getNull() && "Expected non-null from generator");
      Storage.store(Raw);
      return *makePointer(Raw);
    }

    // Contended with another generator. Wait for it to complete.
    while (Raw == getBusy())
      Raw = Storage.load();
    assert(Raw != getNull() && "Expected non-null from competing generator");
    return *makePointer(Raw);
  }

  explicit operator bool() const { return load(); }
  operator T *() const { return load(); }

  T &operator*() const {
    T *P = load();
    assert(P && "Unexpected null dereference");
    return *P;
  }
  T *operator->() const { return &operator*(); }

  LazyAtomicPointer() : Storage(0) {}
  LazyAtomicPointer(std::nullptr_t) : Storage(0) {}
  LazyAtomicPointer(T *Value) : Storage(makeRaw(Value)) {}
  LazyAtomicPointer(const LazyAtomicPointer &RHS)
      : Storage(makeRaw(RHS.load())) {}

  LazyAtomicPointer &operator=(std::nullptr_t) {
    store(nullptr);
    return *this;
  }
  LazyAtomicPointer &operator=(T *RHS) {
    store(RHS);
    return *this;
  }
  LazyAtomicPointer &operator=(const LazyAtomicPointer &RHS) {
    store(RHS.load());
    return *this;
  }

private:
  std::atomic<uintptr_t> Storage;
};

} // end namespace llvm

#endif // LLVM_ADT_LAZYATOMICPOINTER_H
