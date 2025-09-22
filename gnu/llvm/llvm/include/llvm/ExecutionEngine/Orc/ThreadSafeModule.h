//===----------- ThreadSafeModule.h -- Layer interfaces ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Thread safe wrappers and utilities for Module and LLVMContext.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_THREADSAFEMODULE_H
#define LLVM_EXECUTIONENGINE_ORC_THREADSAFEMODULE_H

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Compiler.h"

#include <functional>
#include <memory>
#include <mutex>

namespace llvm {
namespace orc {

/// An LLVMContext together with an associated mutex that can be used to lock
/// the context to prevent concurrent access by other threads.
class ThreadSafeContext {
private:
  struct State {
    State(std::unique_ptr<LLVMContext> Ctx) : Ctx(std::move(Ctx)) {}

    std::unique_ptr<LLVMContext> Ctx;
    std::recursive_mutex Mutex;
  };

public:
  // RAII based lock for ThreadSafeContext.
  class [[nodiscard]] Lock {
  public:
    Lock(std::shared_ptr<State> S) : S(std::move(S)), L(this->S->Mutex) {}

  private:
    std::shared_ptr<State> S;
    std::unique_lock<std::recursive_mutex> L;
  };

  /// Construct a null context.
  ThreadSafeContext() = default;

  /// Construct a ThreadSafeContext from the given LLVMContext.
  ThreadSafeContext(std::unique_ptr<LLVMContext> NewCtx)
      : S(std::make_shared<State>(std::move(NewCtx))) {
    assert(S->Ctx != nullptr &&
           "Can not construct a ThreadSafeContext from a nullptr");
  }

  /// Returns a pointer to the LLVMContext that was used to construct this
  /// instance, or null if the instance was default constructed.
  LLVMContext *getContext() { return S ? S->Ctx.get() : nullptr; }

  /// Returns a pointer to the LLVMContext that was used to construct this
  /// instance, or null if the instance was default constructed.
  const LLVMContext *getContext() const { return S ? S->Ctx.get() : nullptr; }

  Lock getLock() const {
    assert(S && "Can not lock an empty ThreadSafeContext");
    return Lock(S);
  }

private:
  std::shared_ptr<State> S;
};

/// An LLVM Module together with a shared ThreadSafeContext.
class ThreadSafeModule {
public:
  /// Default construct a ThreadSafeModule. This results in a null module and
  /// null context.
  ThreadSafeModule() = default;

  ThreadSafeModule(ThreadSafeModule &&Other) = default;

  ThreadSafeModule &operator=(ThreadSafeModule &&Other) {
    // We have to explicitly define this move operator to copy the fields in
    // reverse order (i.e. module first) to ensure the dependencies are
    // protected: The old module that is being overwritten must be destroyed
    // *before* the context that it depends on.
    // We also need to lock the context to make sure the module tear-down
    // does not overlap any other work on the context.
    if (M) {
      auto L = TSCtx.getLock();
      M = nullptr;
    }
    M = std::move(Other.M);
    TSCtx = std::move(Other.TSCtx);
    return *this;
  }

  /// Construct a ThreadSafeModule from a unique_ptr<Module> and a
  /// unique_ptr<LLVMContext>. This creates a new ThreadSafeContext from the
  /// given context.
  ThreadSafeModule(std::unique_ptr<Module> M, std::unique_ptr<LLVMContext> Ctx)
      : M(std::move(M)), TSCtx(std::move(Ctx)) {}

  /// Construct a ThreadSafeModule from a unique_ptr<Module> and an
  /// existing ThreadSafeContext.
  ThreadSafeModule(std::unique_ptr<Module> M, ThreadSafeContext TSCtx)
      : M(std::move(M)), TSCtx(std::move(TSCtx)) {}

  ~ThreadSafeModule() {
    // We need to lock the context while we destruct the module.
    if (M) {
      auto L = TSCtx.getLock();
      M = nullptr;
    }
  }

  /// Boolean conversion: This ThreadSafeModule will evaluate to true if it
  /// wraps a non-null module.
  explicit operator bool() const {
    if (M) {
      assert(TSCtx.getContext() &&
             "Non-null module must have non-null context");
      return true;
    }
    return false;
  }

  /// Locks the associated ThreadSafeContext and calls the given function
  /// on the contained Module.
  template <typename Func> decltype(auto) withModuleDo(Func &&F) {
    assert(M && "Can not call on null module");
    auto Lock = TSCtx.getLock();
    return F(*M);
  }

  /// Locks the associated ThreadSafeContext and calls the given function
  /// on the contained Module.
  template <typename Func> decltype(auto) withModuleDo(Func &&F) const {
    assert(M && "Can not call on null module");
    auto Lock = TSCtx.getLock();
    return F(*M);
  }

  /// Locks the associated ThreadSafeContext and calls the given function,
  /// passing the contained std::unique_ptr<Module>. The given function should
  /// consume the Module.
  template <typename Func> decltype(auto) consumingModuleDo(Func &&F) {
    auto Lock = TSCtx.getLock();
    return F(std::move(M));
  }

  /// Get a raw pointer to the contained module without locking the context.
  Module *getModuleUnlocked() { return M.get(); }

  /// Get a raw pointer to the contained module without locking the context.
  const Module *getModuleUnlocked() const { return M.get(); }

  /// Returns the context for this ThreadSafeModule.
  ThreadSafeContext getContext() const { return TSCtx; }

private:
  std::unique_ptr<Module> M;
  ThreadSafeContext TSCtx;
};

using GVPredicate = std::function<bool(const GlobalValue &)>;
using GVModifier = std::function<void(GlobalValue &)>;

/// Clones the given module on to a new context.
ThreadSafeModule
cloneToNewContext(const ThreadSafeModule &TSMW,
                  GVPredicate ShouldCloneDef = GVPredicate(),
                  GVModifier UpdateClonedDefSource = GVModifier());

} // End namespace orc
} // End namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_THREADSAFEMODULE_H
