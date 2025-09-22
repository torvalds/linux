//===-- llvm/Support/ManagedStatic.h - Static Global wrapper ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the ManagedStatic class and the llvm_shutdown() function.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_MANAGEDSTATIC_H
#define LLVM_SUPPORT_MANAGEDSTATIC_H

#include <atomic>
#include <cstddef>

namespace llvm {

/// object_creator - Helper method for ManagedStatic.
template <class C> struct object_creator {
  static void *call() { return new C(); }
};

/// object_deleter - Helper method for ManagedStatic.
///
template <typename T> struct object_deleter {
  static void call(void *Ptr) { delete (T *)Ptr; }
};
template <typename T, size_t N> struct object_deleter<T[N]> {
  static void call(void *Ptr) { delete[](T *)Ptr; }
};

// ManagedStatic must be initialized to zero, and it must *not* have a dynamic
// initializer because managed statics are often created while running other
// dynamic initializers. In standard C++11, the best way to accomplish this is
// with a constexpr default constructor. However, different versions of the
// Visual C++ compiler have had bugs where, even though the constructor may be
// constexpr, a dynamic initializer may be emitted depending on optimization
// settings. For the affected versions of MSVC, use the old linker
// initialization pattern of not providing a constructor and leaving the fields
// uninitialized. See http://llvm.org/PR41367 for details.
#if !defined(_MSC_VER) || (_MSC_VER >= 1925) || defined(__clang__)
#define LLVM_USE_CONSTEXPR_CTOR
#endif

/// ManagedStaticBase - Common base class for ManagedStatic instances.
class ManagedStaticBase {
protected:
#ifdef LLVM_USE_CONSTEXPR_CTOR
  mutable std::atomic<void *> Ptr{};
  mutable void (*DeleterFn)(void *) = nullptr;
  mutable const ManagedStaticBase *Next = nullptr;
#else
  // This should only be used as a static variable, which guarantees that this
  // will be zero initialized.
  mutable std::atomic<void *> Ptr;
  mutable void (*DeleterFn)(void *);
  mutable const ManagedStaticBase *Next;
#endif

  void RegisterManagedStatic(void *(*creator)(), void (*deleter)(void*)) const;

public:
#ifdef LLVM_USE_CONSTEXPR_CTOR
  constexpr ManagedStaticBase() = default;
#endif

  /// isConstructed - Return true if this object has not been created yet.
  bool isConstructed() const { return Ptr != nullptr; }

  void destroy() const;
};

/// ManagedStatic - This transparently changes the behavior of global statics to
/// be lazily constructed on demand (good for reducing startup times of dynamic
/// libraries that link in LLVM components) and for making destruction be
/// explicit through the llvm_shutdown() function call.
///
template <class C, class Creator = object_creator<C>,
          class Deleter = object_deleter<C>>
class ManagedStatic : public ManagedStaticBase {
public:
  // Accessors.
  C &operator*() {
    void *Tmp = Ptr.load(std::memory_order_acquire);
    if (!Tmp)
      RegisterManagedStatic(Creator::call, Deleter::call);

    return *static_cast<C *>(Ptr.load(std::memory_order_relaxed));
  }

  C *operator->() { return &**this; }

  const C &operator*() const {
    void *Tmp = Ptr.load(std::memory_order_acquire);
    if (!Tmp)
      RegisterManagedStatic(Creator::call, Deleter::call);

    return *static_cast<C *>(Ptr.load(std::memory_order_relaxed));
  }

  const C *operator->() const { return &**this; }

  // Extract the instance, leaving the ManagedStatic uninitialized. The
  // user is then responsible for the lifetime of the returned instance.
  C *claim() {
    return static_cast<C *>(Ptr.exchange(nullptr));
  }
};

/// llvm_shutdown - Deallocate and destroy all ManagedStatic variables.
void llvm_shutdown();

/// llvm_shutdown_obj - This is a simple helper class that calls
/// llvm_shutdown() when it is destroyed.
struct llvm_shutdown_obj {
  llvm_shutdown_obj() = default;
  ~llvm_shutdown_obj() { llvm_shutdown(); }
};

} // end namespace llvm

#endif // LLVM_SUPPORT_MANAGEDSTATIC_H
