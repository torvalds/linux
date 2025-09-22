//===--- CrashRecoveryContext.h - Crash Recovery ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_CRASHRECOVERYCONTEXT_H
#define LLVM_SUPPORT_CRASHRECOVERYCONTEXT_H

#include "llvm/ADT/STLFunctionalExtras.h"

namespace llvm {
class CrashRecoveryContextCleanup;

/// Crash recovery helper object.
///
/// This class implements support for running operations in a safe context so
/// that crashes (memory errors, stack overflow, assertion violations) can be
/// detected and control restored to the crashing thread. Crash detection is
/// purely "best effort", the exact set of failures which can be recovered from
/// is platform dependent.
///
/// Clients make use of this code by first calling
/// CrashRecoveryContext::Enable(), and then executing unsafe operations via a
/// CrashRecoveryContext object. For example:
///
/// \code
///    void actual_work(void *);
///
///    void foo() {
///      CrashRecoveryContext CRC;
///
///      if (!CRC.RunSafely(actual_work, 0)) {
///         ... a crash was detected, report error to user ...
///      }
///
///      ... no crash was detected ...
///    }
/// \endcode
///
/// To assist recovery the class allows specifying set of actions that will be
/// executed in any case, whether crash occurs or not. These actions may be used
/// to reclaim resources in the case of crash.
class CrashRecoveryContext {
  void *Impl = nullptr;
  CrashRecoveryContextCleanup *head = nullptr;

public:
  CrashRecoveryContext();
  ~CrashRecoveryContext();

  /// Register cleanup handler, which is used when the recovery context is
  /// finished.
  /// The recovery context owns the handler.
  void registerCleanup(CrashRecoveryContextCleanup *cleanup);

  void unregisterCleanup(CrashRecoveryContextCleanup *cleanup);

  /// Enable crash recovery.
  static void Enable();

  /// Disable crash recovery.
  static void Disable();

  /// Return the active context, if the code is currently executing in a
  /// thread which is in a protected context.
  static CrashRecoveryContext *GetCurrent();

  /// Return true if the current thread is recovering from a crash.
  static bool isRecoveringFromCrash();

  /// Execute the provided callback function (with the given arguments) in
  /// a protected context.
  ///
  /// \return True if the function completed successfully, and false if the
  /// function crashed (or HandleCrash was called explicitly). Clients should
  /// make as little assumptions as possible about the program state when
  /// RunSafely has returned false.
  bool RunSafely(function_ref<void()> Fn);
  bool RunSafely(void (*Fn)(void*), void *UserData) {
    return RunSafely([&]() { Fn(UserData); });
  }

  /// Execute the provide callback function (with the given arguments) in
  /// a protected context which is run in another thread (optionally with a
  /// requested stack size).
  ///
  /// See RunSafely().
  ///
  /// On Darwin, if PRIO_DARWIN_BG is set on the calling thread, it will be
  /// propagated to the new thread as well.
  bool RunSafelyOnThread(function_ref<void()>, unsigned RequestedStackSize = 0);
  bool RunSafelyOnThread(void (*Fn)(void*), void *UserData,
                         unsigned RequestedStackSize = 0) {
    return RunSafelyOnThread([&]() { Fn(UserData); }, RequestedStackSize);
  }

  /// Explicitly trigger a crash recovery in the current process, and
  /// return failure from RunSafely(). This function does not return.
  [[noreturn]] void HandleExit(int RetCode);

  /// Return true if RetCode indicates that a signal or an exception occurred.
  static bool isCrash(int RetCode);

  /// Throw again a signal or an exception, after it was catched once by a
  /// CrashRecoveryContext.
  static bool throwIfCrash(int RetCode);

  /// In case of a crash, this is the crash identifier.
  int RetCode = 0;

  /// Selects whether handling of failures should be done in the same way as
  /// for regular crashes. When this is active, a crash would print the
  /// callstack, clean-up any temporary files and create a coredump/minidump.
  bool DumpStackAndCleanupOnFailure = false;
};

/// Abstract base class of cleanup handlers.
///
/// Derived classes override method recoverResources, which makes actual work on
/// resource recovery.
///
/// Cleanup handlers are stored in a double list, which is owned and managed by
/// a crash recovery context.
class CrashRecoveryContextCleanup {
protected:
  CrashRecoveryContext *context = nullptr;
  CrashRecoveryContextCleanup(CrashRecoveryContext *context)
      : context(context) {}

public:
  bool cleanupFired = false;

  virtual ~CrashRecoveryContextCleanup();
  virtual void recoverResources() = 0;

  CrashRecoveryContext *getContext() const {
    return context;
  }

private:
  friend class CrashRecoveryContext;
  CrashRecoveryContextCleanup *prev = nullptr, *next = nullptr;
};

/// Base class of cleanup handler that controls recovery of resources of the
/// given type.
///
/// \tparam Derived Class that uses this class as a base.
/// \tparam T Type of controlled resource.
///
/// This class serves as a base for its template parameter as implied by
/// Curiously Recurring Template Pattern.
///
/// This class factors out creation of a cleanup handler. The latter requires
/// knowledge of the current recovery context, which is provided by this class.
template<typename Derived, typename T>
class CrashRecoveryContextCleanupBase : public CrashRecoveryContextCleanup {
protected:
  T *resource;
  CrashRecoveryContextCleanupBase(CrashRecoveryContext *context, T *resource)
      : CrashRecoveryContextCleanup(context), resource(resource) {}

public:
  /// Creates cleanup handler.
  /// \param x Pointer to the resource recovered by this handler.
  /// \return New handler or null if the method was called outside a recovery
  ///         context.
  static Derived *create(T *x) {
    if (x) {
      if (CrashRecoveryContext *context = CrashRecoveryContext::GetCurrent())
        return new Derived(context, x);
    }
    return nullptr;
  }
};

/// Cleanup handler that reclaims resource by calling destructor on it.
template <typename T>
class CrashRecoveryContextDestructorCleanup : public
  CrashRecoveryContextCleanupBase<CrashRecoveryContextDestructorCleanup<T>, T> {
public:
  CrashRecoveryContextDestructorCleanup(CrashRecoveryContext *context,
                                        T *resource)
      : CrashRecoveryContextCleanupBase<
            CrashRecoveryContextDestructorCleanup<T>, T>(context, resource) {}

  void recoverResources() override {
    this->resource->~T();
  }
};

/// Cleanup handler that reclaims resource by calling 'delete' on it.
template <typename T>
class CrashRecoveryContextDeleteCleanup : public
  CrashRecoveryContextCleanupBase<CrashRecoveryContextDeleteCleanup<T>, T> {
public:
  CrashRecoveryContextDeleteCleanup(CrashRecoveryContext *context, T *resource)
    : CrashRecoveryContextCleanupBase<
        CrashRecoveryContextDeleteCleanup<T>, T>(context, resource) {}

  void recoverResources() override { delete this->resource; }
};

/// Cleanup handler that reclaims resource by calling its method 'Release'.
template <typename T>
class CrashRecoveryContextReleaseRefCleanup : public
  CrashRecoveryContextCleanupBase<CrashRecoveryContextReleaseRefCleanup<T>, T> {
public:
  CrashRecoveryContextReleaseRefCleanup(CrashRecoveryContext *context,
                                        T *resource)
    : CrashRecoveryContextCleanupBase<CrashRecoveryContextReleaseRefCleanup<T>,
          T>(context, resource) {}

  void recoverResources() override { this->resource->Release(); }
};

/// Helper class for managing resource cleanups.
///
/// \tparam T Type of resource been reclaimed.
/// \tparam Cleanup Class that defines how the resource is reclaimed.
///
/// Clients create objects of this type in the code executed in a crash recovery
/// context to ensure that the resource will be reclaimed even in the case of
/// crash. For example:
///
/// \code
///    void actual_work(void *) {
///      ...
///      std::unique_ptr<Resource> R(new Resource());
///      CrashRecoveryContextCleanupRegistrar D(R.get());
///      ...
///    }
///
///    void foo() {
///      CrashRecoveryContext CRC;
///
///      if (!CRC.RunSafely(actual_work, 0)) {
///         ... a crash was detected, report error to user ...
///      }
/// \endcode
///
/// If the code of `actual_work` in the example above does not crash, the
/// destructor of CrashRecoveryContextCleanupRegistrar removes cleanup code from
/// the current CrashRecoveryContext and the resource is reclaimed by the
/// destructor of std::unique_ptr. If crash happens, destructors are not called
/// and the resource is reclaimed by cleanup object registered in the recovery
/// context by the constructor of CrashRecoveryContextCleanupRegistrar.
template <typename T, typename Cleanup = CrashRecoveryContextDeleteCleanup<T> >
class CrashRecoveryContextCleanupRegistrar {
  CrashRecoveryContextCleanup *cleanup;

public:
  CrashRecoveryContextCleanupRegistrar(T *x)
    : cleanup(Cleanup::create(x)) {
    if (cleanup)
      cleanup->getContext()->registerCleanup(cleanup);
  }

  ~CrashRecoveryContextCleanupRegistrar() { unregister(); }

  void unregister() {
    if (cleanup && !cleanup->cleanupFired)
      cleanup->getContext()->unregisterCleanup(cleanup);
    cleanup = nullptr;
  }
};
} // end namespace llvm

#endif // LLVM_SUPPORT_CRASHRECOVERYCONTEXT_H
