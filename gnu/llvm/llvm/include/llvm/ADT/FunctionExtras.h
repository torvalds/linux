//===- FunctionExtras.h - Function type erasure utilities -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file provides a collection of function (or more generally, callable)
/// type erasure utilities supplementing those provided by the standard library
/// in `<function>`.
///
/// It provides `unique_function`, which works like `std::function` but supports
/// move-only callable objects and const-qualification.
///
/// Future plans:
/// - Add a `function` that provides ref-qualified support, which doesn't work
///   with `std::function`.
/// - Provide support for specifying multiple signatures to type erase callable
///   objects with an overload set, such as those produced by generic lambdas.
/// - Expand to include a copyable utility that directly replaces std::function
///   but brings the above improvements.
///
/// Note that LLVM's utilities are greatly simplified by not supporting
/// allocators.
///
/// If the standard library ever begins to provide comparable facilities we can
/// consider switching to those.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_FUNCTIONEXTRAS_H
#define LLVM_ADT_FUNCTIONEXTRAS_H

#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MemAlloc.h"
#include "llvm/Support/type_traits.h"
#include <cstring>
#include <memory>
#include <type_traits>

namespace llvm {

/// unique_function is a type-erasing functor similar to std::function.
///
/// It can hold move-only function objects, like lambdas capturing unique_ptrs.
/// Accordingly, it is movable but not copyable.
///
/// It supports const-qualification:
/// - unique_function<int() const> has a const operator().
///   It can only hold functions which themselves have a const operator().
/// - unique_function<int()> has a non-const operator().
///   It can hold functions with a non-const operator(), like mutable lambdas.
template <typename FunctionT> class unique_function;

namespace detail {

template <typename T>
using EnableIfTrivial =
    std::enable_if_t<std::is_trivially_move_constructible<T>::value &&
                     std::is_trivially_destructible<T>::value>;
template <typename CallableT, typename ThisT>
using EnableUnlessSameType =
    std::enable_if_t<!std::is_same<remove_cvref_t<CallableT>, ThisT>::value>;
template <typename CallableT, typename Ret, typename... Params>
using EnableIfCallable = std::enable_if_t<std::disjunction<
    std::is_void<Ret>,
    std::is_same<decltype(std::declval<CallableT>()(std::declval<Params>()...)),
                 Ret>,
    std::is_same<const decltype(std::declval<CallableT>()(
                     std::declval<Params>()...)),
                 Ret>,
    std::is_convertible<decltype(std::declval<CallableT>()(
                            std::declval<Params>()...)),
                        Ret>>::value>;

template <typename ReturnT, typename... ParamTs> class UniqueFunctionBase {
protected:
  static constexpr size_t InlineStorageSize = sizeof(void *) * 3;

  template <typename T, class = void>
  struct IsSizeLessThanThresholdT : std::false_type {};

  template <typename T>
  struct IsSizeLessThanThresholdT<
      T, std::enable_if_t<sizeof(T) <= 2 * sizeof(void *)>> : std::true_type {};

  // Provide a type function to map parameters that won't observe extra copies
  // or moves and which are small enough to likely pass in register to values
  // and all other types to l-value reference types. We use this to compute the
  // types used in our erased call utility to minimize copies and moves unless
  // doing so would force things unnecessarily into memory.
  //
  // The heuristic used is related to common ABI register passing conventions.
  // It doesn't have to be exact though, and in one way it is more strict
  // because we want to still be able to observe either moves *or* copies.
  template <typename T> struct AdjustedParamTBase {
    static_assert(!std::is_reference<T>::value,
                  "references should be handled by template specialization");
    using type =
        std::conditional_t<std::is_trivially_copy_constructible<T>::value &&
                               std::is_trivially_move_constructible<T>::value &&
                               IsSizeLessThanThresholdT<T>::value,
                           T, T &>;
  };

  // This specialization ensures that 'AdjustedParam<V<T>&>' or
  // 'AdjustedParam<V<T>&&>' does not trigger a compile-time error when 'T' is
  // an incomplete type and V a templated type.
  template <typename T> struct AdjustedParamTBase<T &> { using type = T &; };
  template <typename T> struct AdjustedParamTBase<T &&> { using type = T &; };

  template <typename T>
  using AdjustedParamT = typename AdjustedParamTBase<T>::type;

  // The type of the erased function pointer we use as a callback to dispatch to
  // the stored callable when it is trivial to move and destroy.
  using CallPtrT = ReturnT (*)(void *CallableAddr,
                               AdjustedParamT<ParamTs>... Params);
  using MovePtrT = void (*)(void *LHSCallableAddr, void *RHSCallableAddr);
  using DestroyPtrT = void (*)(void *CallableAddr);

  /// A struct to hold a single trivial callback with sufficient alignment for
  /// our bitpacking.
  struct alignas(8) TrivialCallback {
    CallPtrT CallPtr;
  };

  /// A struct we use to aggregate three callbacks when we need full set of
  /// operations.
  struct alignas(8) NonTrivialCallbacks {
    CallPtrT CallPtr;
    MovePtrT MovePtr;
    DestroyPtrT DestroyPtr;
  };

  // Create a pointer union between either a pointer to a static trivial call
  // pointer in a struct or a pointer to a static struct of the call, move, and
  // destroy pointers.
  using CallbackPointerUnionT =
      PointerUnion<TrivialCallback *, NonTrivialCallbacks *>;

  // The main storage buffer. This will either have a pointer to out-of-line
  // storage or an inline buffer storing the callable.
  union StorageUnionT {
    // For out-of-line storage we keep a pointer to the underlying storage and
    // the size. This is enough to deallocate the memory.
    struct OutOfLineStorageT {
      void *StoragePtr;
      size_t Size;
      size_t Alignment;
    } OutOfLineStorage;
    static_assert(
        sizeof(OutOfLineStorageT) <= InlineStorageSize,
        "Should always use all of the out-of-line storage for inline storage!");

    // For in-line storage, we just provide an aligned character buffer. We
    // provide three pointers worth of storage here.
    // This is mutable as an inlined `const unique_function<void() const>` may
    // still modify its own mutable members.
    alignas(void *) mutable std::byte InlineStorage[InlineStorageSize];
  } StorageUnion;

  // A compressed pointer to either our dispatching callback or our table of
  // dispatching callbacks and the flag for whether the callable itself is
  // stored inline or not.
  PointerIntPair<CallbackPointerUnionT, 1, bool> CallbackAndInlineFlag;

  bool isInlineStorage() const { return CallbackAndInlineFlag.getInt(); }

  bool isTrivialCallback() const {
    return isa<TrivialCallback *>(CallbackAndInlineFlag.getPointer());
  }

  CallPtrT getTrivialCallback() const {
    return cast<TrivialCallback *>(CallbackAndInlineFlag.getPointer())->CallPtr;
  }

  NonTrivialCallbacks *getNonTrivialCallbacks() const {
    return cast<NonTrivialCallbacks *>(CallbackAndInlineFlag.getPointer());
  }

  CallPtrT getCallPtr() const {
    return isTrivialCallback() ? getTrivialCallback()
                               : getNonTrivialCallbacks()->CallPtr;
  }

  // These three functions are only const in the narrow sense. They return
  // mutable pointers to function state.
  // This allows unique_function<T const>::operator() to be const, even if the
  // underlying functor may be internally mutable.
  //
  // const callers must ensure they're only used in const-correct ways.
  void *getCalleePtr() const {
    return isInlineStorage() ? getInlineStorage() : getOutOfLineStorage();
  }
  void *getInlineStorage() const { return &StorageUnion.InlineStorage; }
  void *getOutOfLineStorage() const {
    return StorageUnion.OutOfLineStorage.StoragePtr;
  }

  size_t getOutOfLineStorageSize() const {
    return StorageUnion.OutOfLineStorage.Size;
  }
  size_t getOutOfLineStorageAlignment() const {
    return StorageUnion.OutOfLineStorage.Alignment;
  }

  void setOutOfLineStorage(void *Ptr, size_t Size, size_t Alignment) {
    StorageUnion.OutOfLineStorage = {Ptr, Size, Alignment};
  }

  template <typename CalledAsT>
  static ReturnT CallImpl(void *CallableAddr,
                          AdjustedParamT<ParamTs>... Params) {
    auto &Func = *reinterpret_cast<CalledAsT *>(CallableAddr);
    return Func(std::forward<ParamTs>(Params)...);
  }

  template <typename CallableT>
  static void MoveImpl(void *LHSCallableAddr, void *RHSCallableAddr) noexcept {
    new (LHSCallableAddr)
        CallableT(std::move(*reinterpret_cast<CallableT *>(RHSCallableAddr)));
  }

  template <typename CallableT>
  static void DestroyImpl(void *CallableAddr) noexcept {
    reinterpret_cast<CallableT *>(CallableAddr)->~CallableT();
  }

  // The pointers to call/move/destroy functions are determined for each
  // callable type (and called-as type, which determines the overload chosen).
  // (definitions are out-of-line).

  // By default, we need an object that contains all the different
  // type erased behaviors needed. Create a static instance of the struct type
  // here and each instance will contain a pointer to it.
  // Wrap in a struct to avoid https://gcc.gnu.org/PR71954
  template <typename CallableT, typename CalledAs, typename Enable = void>
  struct CallbacksHolder {
    static NonTrivialCallbacks Callbacks;
  };
  // See if we can create a trivial callback. We need the callable to be
  // trivially moved and trivially destroyed so that we don't have to store
  // type erased callbacks for those operations.
  template <typename CallableT, typename CalledAs>
  struct CallbacksHolder<CallableT, CalledAs, EnableIfTrivial<CallableT>> {
    static TrivialCallback Callbacks;
  };

  // A simple tag type so the call-as type to be passed to the constructor.
  template <typename T> struct CalledAs {};

  // Essentially the "main" unique_function constructor, but subclasses
  // provide the qualified type to be used for the call.
  // (We always store a T, even if the call will use a pointer to const T).
  template <typename CallableT, typename CalledAsT>
  UniqueFunctionBase(CallableT Callable, CalledAs<CalledAsT>) {
    bool IsInlineStorage = true;
    void *CallableAddr = getInlineStorage();
    if (sizeof(CallableT) > InlineStorageSize ||
        alignof(CallableT) > alignof(decltype(StorageUnion.InlineStorage))) {
      IsInlineStorage = false;
      // Allocate out-of-line storage. FIXME: Use an explicit alignment
      // parameter in C++17 mode.
      auto Size = sizeof(CallableT);
      auto Alignment = alignof(CallableT);
      CallableAddr = allocate_buffer(Size, Alignment);
      setOutOfLineStorage(CallableAddr, Size, Alignment);
    }

    // Now move into the storage.
    new (CallableAddr) CallableT(std::move(Callable));
    CallbackAndInlineFlag.setPointerAndInt(
        &CallbacksHolder<CallableT, CalledAsT>::Callbacks, IsInlineStorage);
  }

  ~UniqueFunctionBase() {
    if (!CallbackAndInlineFlag.getPointer())
      return;

    // Cache this value so we don't re-check it after type-erased operations.
    bool IsInlineStorage = isInlineStorage();

    if (!isTrivialCallback())
      getNonTrivialCallbacks()->DestroyPtr(
          IsInlineStorage ? getInlineStorage() : getOutOfLineStorage());

    if (!IsInlineStorage)
      deallocate_buffer(getOutOfLineStorage(), getOutOfLineStorageSize(),
                        getOutOfLineStorageAlignment());
  }

  UniqueFunctionBase(UniqueFunctionBase &&RHS) noexcept {
    // Copy the callback and inline flag.
    CallbackAndInlineFlag = RHS.CallbackAndInlineFlag;

    // If the RHS is empty, just copying the above is sufficient.
    if (!RHS)
      return;

    if (!isInlineStorage()) {
      // The out-of-line case is easiest to move.
      StorageUnion.OutOfLineStorage = RHS.StorageUnion.OutOfLineStorage;
    } else if (isTrivialCallback()) {
      // Move is trivial, just memcpy the bytes across.
      memcpy(getInlineStorage(), RHS.getInlineStorage(), InlineStorageSize);
    } else {
      // Non-trivial move, so dispatch to a type-erased implementation.
      getNonTrivialCallbacks()->MovePtr(getInlineStorage(),
                                        RHS.getInlineStorage());
    }

    // Clear the old callback and inline flag to get back to as-if-null.
    RHS.CallbackAndInlineFlag = {};

#if !defined(NDEBUG) && !LLVM_ADDRESS_SANITIZER_BUILD
    // In debug builds without ASan, we also scribble across the rest of the
    // storage. Scribbling under AddressSanitizer (ASan) is disabled to prevent
    // overwriting poisoned objects (e.g., annotated short strings).
    memset(RHS.getInlineStorage(), 0xAD, InlineStorageSize);
#endif
  }

  UniqueFunctionBase &operator=(UniqueFunctionBase &&RHS) noexcept {
    if (this == &RHS)
      return *this;

    // Because we don't try to provide any exception safety guarantees we can
    // implement move assignment very simply by first destroying the current
    // object and then move-constructing over top of it.
    this->~UniqueFunctionBase();
    new (this) UniqueFunctionBase(std::move(RHS));
    return *this;
  }

  UniqueFunctionBase() = default;

public:
  explicit operator bool() const {
    return (bool)CallbackAndInlineFlag.getPointer();
  }
};

template <typename R, typename... P>
template <typename CallableT, typename CalledAsT, typename Enable>
typename UniqueFunctionBase<R, P...>::NonTrivialCallbacks UniqueFunctionBase<
    R, P...>::CallbacksHolder<CallableT, CalledAsT, Enable>::Callbacks = {
    &CallImpl<CalledAsT>, &MoveImpl<CallableT>, &DestroyImpl<CallableT>};

template <typename R, typename... P>
template <typename CallableT, typename CalledAsT>
typename UniqueFunctionBase<R, P...>::TrivialCallback
    UniqueFunctionBase<R, P...>::CallbacksHolder<
        CallableT, CalledAsT, EnableIfTrivial<CallableT>>::Callbacks{
        &CallImpl<CalledAsT>};

} // namespace detail

template <typename R, typename... P>
class unique_function<R(P...)> : public detail::UniqueFunctionBase<R, P...> {
  using Base = detail::UniqueFunctionBase<R, P...>;

public:
  unique_function() = default;
  unique_function(std::nullptr_t) {}
  unique_function(unique_function &&) = default;
  unique_function(const unique_function &) = delete;
  unique_function &operator=(unique_function &&) = default;
  unique_function &operator=(const unique_function &) = delete;

  template <typename CallableT>
  unique_function(
      CallableT Callable,
      detail::EnableUnlessSameType<CallableT, unique_function> * = nullptr,
      detail::EnableIfCallable<CallableT, R, P...> * = nullptr)
      : Base(std::forward<CallableT>(Callable),
             typename Base::template CalledAs<CallableT>{}) {}

  R operator()(P... Params) {
    return this->getCallPtr()(this->getCalleePtr(), Params...);
  }
};

template <typename R, typename... P>
class unique_function<R(P...) const>
    : public detail::UniqueFunctionBase<R, P...> {
  using Base = detail::UniqueFunctionBase<R, P...>;

public:
  unique_function() = default;
  unique_function(std::nullptr_t) {}
  unique_function(unique_function &&) = default;
  unique_function(const unique_function &) = delete;
  unique_function &operator=(unique_function &&) = default;
  unique_function &operator=(const unique_function &) = delete;

  template <typename CallableT>
  unique_function(
      CallableT Callable,
      detail::EnableUnlessSameType<CallableT, unique_function> * = nullptr,
      detail::EnableIfCallable<const CallableT, R, P...> * = nullptr)
      : Base(std::forward<CallableT>(Callable),
             typename Base::template CalledAs<const CallableT>{}) {}

  R operator()(P... Params) const {
    return this->getCallPtr()(this->getCalleePtr(), Params...);
  }
};

} // end namespace llvm

#endif // LLVM_ADT_FUNCTIONEXTRAS_H
