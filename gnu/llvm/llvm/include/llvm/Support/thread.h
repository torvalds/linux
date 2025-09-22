//===-- llvm/Support/thread.h - Wrapper for <thread> ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header is a wrapper for <thread> that works around problems with the
// MSVC headers when exceptions are disabled. It also provides llvm::thread,
// which is either a typedef of std::thread or a replacement that calls the
// function synchronously depending on the value of LLVM_ENABLE_THREADS.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_THREAD_H
#define LLVM_SUPPORT_THREAD_H

#include "llvm/Config/llvm-config.h"
#include <optional>

#ifdef _WIN32
typedef unsigned long DWORD;
typedef void *PVOID;
typedef PVOID HANDLE;
#endif

#if LLVM_ENABLE_THREADS

#include <thread>

namespace llvm {

#if LLVM_ON_UNIX || _WIN32

/// LLVM thread following std::thread interface with added constructor to
/// specify stack size.
class thread {
  template <typename CalleeTuple> static void GenericThreadProxy(void *Ptr) {
    std::unique_ptr<CalleeTuple> Callee(static_cast<CalleeTuple *>(Ptr));
    std::apply(
        [](auto &&F, auto &&...Args) {
          std::forward<decltype(F)>(F)(std::forward<decltype(Args)>(Args)...);
        },
        *Callee);
  }

public:
#if LLVM_ON_UNIX
  using native_handle_type = pthread_t;
  using id = pthread_t;
  using start_routine_type = void *(*)(void *);

  template <typename CalleeTuple> static void *ThreadProxy(void *Ptr) {
    GenericThreadProxy<CalleeTuple>(Ptr);
    return nullptr;
  }
#elif _WIN32
  using native_handle_type = HANDLE;
  using id = DWORD;
  using start_routine_type = unsigned(__stdcall *)(void *);

  template <typename CalleeTuple>
  static unsigned __stdcall ThreadProxy(void *Ptr) {
    GenericThreadProxy<CalleeTuple>(Ptr);
    return 0;
  }
#endif

  static const std::optional<unsigned> DefaultStackSize;

  thread() : Thread(native_handle_type()) {}
  thread(thread &&Other) noexcept
      : Thread(std::exchange(Other.Thread, native_handle_type())) {}

  template <class Function, class... Args>
  explicit thread(Function &&f, Args &&...args)
      : thread(DefaultStackSize, f, args...) {}

  template <class Function, class... Args>
  explicit thread(std::optional<unsigned> StackSizeInBytes, Function &&f,
                  Args &&...args);
  thread(const thread &) = delete;

  ~thread() {
    if (joinable())
      std::terminate();
  }

  thread &operator=(thread &&Other) noexcept {
    if (joinable())
      std::terminate();
    Thread = std::exchange(Other.Thread, native_handle_type());
    return *this;
  }

  bool joinable() const noexcept { return Thread != native_handle_type(); }

  inline id get_id() const noexcept;

  native_handle_type native_handle() const noexcept { return Thread; }

  static unsigned hardware_concurrency() {
    return std::thread::hardware_concurrency();
  };

  inline void join();
  inline void detach();

  void swap(llvm::thread &Other) noexcept { std::swap(Thread, Other.Thread); }

private:
  native_handle_type Thread;
};

thread::native_handle_type
llvm_execute_on_thread_impl(thread::start_routine_type ThreadFunc, void *Arg,
                            std::optional<unsigned> StackSizeInBytes);
void llvm_thread_join_impl(thread::native_handle_type Thread);
void llvm_thread_detach_impl(thread::native_handle_type Thread);
thread::id llvm_thread_get_id_impl(thread::native_handle_type Thread);
thread::id llvm_thread_get_current_id_impl();

template <class Function, class... Args>
thread::thread(std::optional<unsigned> StackSizeInBytes, Function &&f,
               Args &&...args) {
  typedef std::tuple<std::decay_t<Function>, std::decay_t<Args>...> CalleeTuple;
  std::unique_ptr<CalleeTuple> Callee(
      new CalleeTuple(std::forward<Function>(f), std::forward<Args>(args)...));

  Thread = llvm_execute_on_thread_impl(ThreadProxy<CalleeTuple>, Callee.get(),
                                       StackSizeInBytes);
  if (Thread != native_handle_type())
    Callee.release();
}

thread::id thread::get_id() const noexcept {
  return llvm_thread_get_id_impl(Thread);
}

void thread::join() {
  llvm_thread_join_impl(Thread);
  Thread = native_handle_type();
}

void thread::detach() {
  llvm_thread_detach_impl(Thread);
  Thread = native_handle_type();
}

namespace this_thread {
inline thread::id get_id() { return llvm_thread_get_current_id_impl(); }
} // namespace this_thread

#else // !LLVM_ON_UNIX && !_WIN32

/// std::thread backed implementation of llvm::thread interface that ignores the
/// stack size request.
class thread {
public:
  using native_handle_type = std::thread::native_handle_type;
  using id = std::thread::id;

  thread() : Thread(std::thread()) {}
  thread(thread &&Other) noexcept
      : Thread(std::exchange(Other.Thread, std::thread())) {}

  template <class Function, class... Args>
  explicit thread(std::optional<unsigned> StackSizeInBytes, Function &&f,
                  Args &&...args)
      : Thread(std::forward<Function>(f), std::forward<Args>(args)...) {}

  template <class Function, class... Args>
  explicit thread(Function &&f, Args &&...args) : Thread(f, args...) {}

  thread(const thread &) = delete;

  ~thread() {}

  thread &operator=(thread &&Other) noexcept {
    Thread = std::exchange(Other.Thread, std::thread());
    return *this;
  }

  bool joinable() const noexcept { return Thread.joinable(); }

  id get_id() const noexcept { return Thread.get_id(); }

  native_handle_type native_handle() noexcept { return Thread.native_handle(); }

  static unsigned hardware_concurrency() {
    return std::thread::hardware_concurrency();
  };

  inline void join() { Thread.join(); }
  inline void detach() { Thread.detach(); }

  void swap(llvm::thread &Other) noexcept { std::swap(Thread, Other.Thread); }

private:
  std::thread Thread;
};

namespace this_thread {
  inline thread::id get_id() { return std::this_thread::get_id(); }
}

#endif // LLVM_ON_UNIX || _WIN32

} // namespace llvm

#else // !LLVM_ENABLE_THREADS

#include <utility>

namespace llvm {

struct thread {
  thread() {}
  thread(thread &&other) {}
  template <class Function, class... Args>
  explicit thread(std::optional<unsigned> StackSizeInBytes, Function &&f,
                  Args &&...args) {
    f(std::forward<Args>(args)...);
  }
  template <class Function, class... Args>
  explicit thread(Function &&f, Args &&...args) {
    f(std::forward<Args>(args)...);
  }
  thread(const thread &) = delete;

  void detach() {
    report_fatal_error("Detaching from a thread does not make sense with no "
                       "threading support");
  }
  void join() {}
  static unsigned hardware_concurrency() { return 1; };
};

} // namespace llvm

#endif // LLVM_ENABLE_THREADS

#endif // LLVM_SUPPORT_THREAD_H
