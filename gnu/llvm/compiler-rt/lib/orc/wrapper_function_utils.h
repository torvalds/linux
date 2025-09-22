//===-- wrapper_function_utils.h - Utilities for wrapper funcs --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of the ORC runtime support library.
//
//===----------------------------------------------------------------------===//

#ifndef ORC_RT_WRAPPER_FUNCTION_UTILS_H
#define ORC_RT_WRAPPER_FUNCTION_UTILS_H

#include "orc_rt/c_api.h"
#include "common.h"
#include "error.h"
#include "executor_address.h"
#include "simple_packed_serialization.h"
#include <type_traits>

namespace __orc_rt {

/// C++ wrapper function result: Same as CWrapperFunctionResult but
/// auto-releases memory.
class WrapperFunctionResult {
public:
  /// Create a default WrapperFunctionResult.
  WrapperFunctionResult() { orc_rt_CWrapperFunctionResultInit(&R); }

  /// Create a WrapperFunctionResult from a CWrapperFunctionResult. This
  /// instance takes ownership of the result object and will automatically
  /// call dispose on the result upon destruction.
  WrapperFunctionResult(orc_rt_CWrapperFunctionResult R) : R(R) {}

  WrapperFunctionResult(const WrapperFunctionResult &) = delete;
  WrapperFunctionResult &operator=(const WrapperFunctionResult &) = delete;

  WrapperFunctionResult(WrapperFunctionResult &&Other) {
    orc_rt_CWrapperFunctionResultInit(&R);
    std::swap(R, Other.R);
  }

  WrapperFunctionResult &operator=(WrapperFunctionResult &&Other) {
    orc_rt_CWrapperFunctionResult Tmp;
    orc_rt_CWrapperFunctionResultInit(&Tmp);
    std::swap(Tmp, Other.R);
    std::swap(R, Tmp);
    return *this;
  }

  ~WrapperFunctionResult() { orc_rt_DisposeCWrapperFunctionResult(&R); }

  /// Relinquish ownership of and return the
  /// orc_rt_CWrapperFunctionResult.
  orc_rt_CWrapperFunctionResult release() {
    orc_rt_CWrapperFunctionResult Tmp;
    orc_rt_CWrapperFunctionResultInit(&Tmp);
    std::swap(R, Tmp);
    return Tmp;
  }

  /// Get a pointer to the data contained in this instance.
  char *data() { return orc_rt_CWrapperFunctionResultData(&R); }

  /// Returns the size of the data contained in this instance.
  size_t size() const { return orc_rt_CWrapperFunctionResultSize(&R); }

  /// Returns true if this value is equivalent to a default-constructed
  /// WrapperFunctionResult.
  bool empty() const { return orc_rt_CWrapperFunctionResultEmpty(&R); }

  /// Create a WrapperFunctionResult with the given size and return a pointer
  /// to the underlying memory.
  static WrapperFunctionResult allocate(size_t Size) {
    WrapperFunctionResult R;
    R.R = orc_rt_CWrapperFunctionResultAllocate(Size);
    return R;
  }

  /// Copy from the given char range.
  static WrapperFunctionResult copyFrom(const char *Source, size_t Size) {
    return orc_rt_CreateCWrapperFunctionResultFromRange(Source, Size);
  }

  /// Copy from the given null-terminated string (includes the null-terminator).
  static WrapperFunctionResult copyFrom(const char *Source) {
    return orc_rt_CreateCWrapperFunctionResultFromString(Source);
  }

  /// Copy from the given std::string (includes the null terminator).
  static WrapperFunctionResult copyFrom(const std::string &Source) {
    return copyFrom(Source.c_str());
  }

  /// Create an out-of-band error by copying the given string.
  static WrapperFunctionResult createOutOfBandError(const char *Msg) {
    return orc_rt_CreateCWrapperFunctionResultFromOutOfBandError(Msg);
  }

  /// Create an out-of-band error by copying the given string.
  static WrapperFunctionResult createOutOfBandError(const std::string &Msg) {
    return createOutOfBandError(Msg.c_str());
  }

  template <typename SPSArgListT, typename... ArgTs>
  static WrapperFunctionResult fromSPSArgs(const ArgTs &...Args) {
    auto Result = allocate(SPSArgListT::size(Args...));
    SPSOutputBuffer OB(Result.data(), Result.size());
    if (!SPSArgListT::serialize(OB, Args...))
      return createOutOfBandError(
          "Error serializing arguments to blob in call");
    return Result;
  }

  /// If this value is an out-of-band error then this returns the error message,
  /// otherwise returns nullptr.
  const char *getOutOfBandError() const {
    return orc_rt_CWrapperFunctionResultGetOutOfBandError(&R);
  }

private:
  orc_rt_CWrapperFunctionResult R;
};

namespace detail {

template <typename RetT> class WrapperFunctionHandlerCaller {
public:
  template <typename HandlerT, typename ArgTupleT, std::size_t... I>
  static decltype(auto) call(HandlerT &&H, ArgTupleT &Args,
                             std::index_sequence<I...>) {
    return std::forward<HandlerT>(H)(std::get<I>(Args)...);
  }
};

template <> class WrapperFunctionHandlerCaller<void> {
public:
  template <typename HandlerT, typename ArgTupleT, std::size_t... I>
  static SPSEmpty call(HandlerT &&H, ArgTupleT &Args,
                       std::index_sequence<I...>) {
    std::forward<HandlerT>(H)(std::get<I>(Args)...);
    return SPSEmpty();
  }
};

template <typename WrapperFunctionImplT,
          template <typename> class ResultSerializer, typename... SPSTagTs>
class WrapperFunctionHandlerHelper
    : public WrapperFunctionHandlerHelper<
          decltype(&std::remove_reference_t<WrapperFunctionImplT>::operator()),
          ResultSerializer, SPSTagTs...> {};

template <typename RetT, typename... ArgTs,
          template <typename> class ResultSerializer, typename... SPSTagTs>
class WrapperFunctionHandlerHelper<RetT(ArgTs...), ResultSerializer,
                                   SPSTagTs...> {
public:
  using ArgTuple = std::tuple<std::decay_t<ArgTs>...>;
  using ArgIndices = std::make_index_sequence<std::tuple_size<ArgTuple>::value>;

  template <typename HandlerT>
  static WrapperFunctionResult apply(HandlerT &&H, const char *ArgData,
                                     size_t ArgSize) {
    ArgTuple Args;
    if (!deserialize(ArgData, ArgSize, Args, ArgIndices{}))
      return WrapperFunctionResult::createOutOfBandError(
          "Could not deserialize arguments for wrapper function call");

    auto HandlerResult = WrapperFunctionHandlerCaller<RetT>::call(
        std::forward<HandlerT>(H), Args, ArgIndices{});

    return ResultSerializer<decltype(HandlerResult)>::serialize(
        std::move(HandlerResult));
  }

private:
  template <std::size_t... I>
  static bool deserialize(const char *ArgData, size_t ArgSize, ArgTuple &Args,
                          std::index_sequence<I...>) {
    SPSInputBuffer IB(ArgData, ArgSize);
    return SPSArgList<SPSTagTs...>::deserialize(IB, std::get<I>(Args)...);
  }
};

// Map function pointers to function types.
template <typename RetT, typename... ArgTs,
          template <typename> class ResultSerializer, typename... SPSTagTs>
class WrapperFunctionHandlerHelper<RetT (*)(ArgTs...), ResultSerializer,
                                   SPSTagTs...>
    : public WrapperFunctionHandlerHelper<RetT(ArgTs...), ResultSerializer,
                                          SPSTagTs...> {};

// Map non-const member function types to function types.
template <typename ClassT, typename RetT, typename... ArgTs,
          template <typename> class ResultSerializer, typename... SPSTagTs>
class WrapperFunctionHandlerHelper<RetT (ClassT::*)(ArgTs...), ResultSerializer,
                                   SPSTagTs...>
    : public WrapperFunctionHandlerHelper<RetT(ArgTs...), ResultSerializer,
                                          SPSTagTs...> {};

// Map const member function types to function types.
template <typename ClassT, typename RetT, typename... ArgTs,
          template <typename> class ResultSerializer, typename... SPSTagTs>
class WrapperFunctionHandlerHelper<RetT (ClassT::*)(ArgTs...) const,
                                   ResultSerializer, SPSTagTs...>
    : public WrapperFunctionHandlerHelper<RetT(ArgTs...), ResultSerializer,
                                          SPSTagTs...> {};

template <typename SPSRetTagT, typename RetT> class ResultSerializer {
public:
  static WrapperFunctionResult serialize(RetT Result) {
    return WrapperFunctionResult::fromSPSArgs<SPSArgList<SPSRetTagT>>(Result);
  }
};

template <typename SPSRetTagT> class ResultSerializer<SPSRetTagT, Error> {
public:
  static WrapperFunctionResult serialize(Error Err) {
    return WrapperFunctionResult::fromSPSArgs<SPSArgList<SPSRetTagT>>(
        toSPSSerializable(std::move(Err)));
  }
};

template <typename SPSRetTagT, typename T>
class ResultSerializer<SPSRetTagT, Expected<T>> {
public:
  static WrapperFunctionResult serialize(Expected<T> E) {
    return WrapperFunctionResult::fromSPSArgs<SPSArgList<SPSRetTagT>>(
        toSPSSerializable(std::move(E)));
  }
};

template <typename SPSRetTagT, typename RetT> class ResultDeserializer {
public:
  static void makeSafe(RetT &Result) {}

  static Error deserialize(RetT &Result, const char *ArgData, size_t ArgSize) {
    SPSInputBuffer IB(ArgData, ArgSize);
    if (!SPSArgList<SPSRetTagT>::deserialize(IB, Result))
      return make_error<StringError>(
          "Error deserializing return value from blob in call");
    return Error::success();
  }
};

template <> class ResultDeserializer<SPSError, Error> {
public:
  static void makeSafe(Error &Err) { cantFail(std::move(Err)); }

  static Error deserialize(Error &Err, const char *ArgData, size_t ArgSize) {
    SPSInputBuffer IB(ArgData, ArgSize);
    SPSSerializableError BSE;
    if (!SPSArgList<SPSError>::deserialize(IB, BSE))
      return make_error<StringError>(
          "Error deserializing return value from blob in call");
    Err = fromSPSSerializable(std::move(BSE));
    return Error::success();
  }
};

template <typename SPSTagT, typename T>
class ResultDeserializer<SPSExpected<SPSTagT>, Expected<T>> {
public:
  static void makeSafe(Expected<T> &E) { cantFail(E.takeError()); }

  static Error deserialize(Expected<T> &E, const char *ArgData,
                           size_t ArgSize) {
    SPSInputBuffer IB(ArgData, ArgSize);
    SPSSerializableExpected<T> BSE;
    if (!SPSArgList<SPSExpected<SPSTagT>>::deserialize(IB, BSE))
      return make_error<StringError>(
          "Error deserializing return value from blob in call");
    E = fromSPSSerializable(std::move(BSE));
    return Error::success();
  }
};

} // end namespace detail

template <typename SPSSignature> class WrapperFunction;

template <typename SPSRetTagT, typename... SPSTagTs>
class WrapperFunction<SPSRetTagT(SPSTagTs...)> {
private:
  template <typename RetT>
  using ResultSerializer = detail::ResultSerializer<SPSRetTagT, RetT>;

public:
  template <typename RetT, typename... ArgTs>
  static Error call(const void *FnTag, RetT &Result, const ArgTs &...Args) {

    // RetT might be an Error or Expected value. Set the checked flag now:
    // we don't want the user to have to check the unused result if this
    // operation fails.
    detail::ResultDeserializer<SPSRetTagT, RetT>::makeSafe(Result);

    // Since the functions cannot be zero/unresolved on Windows, the following
    // reference taking would always be non-zero, thus generating a compiler
    // warning otherwise.
#if !defined(_WIN32)
    if (ORC_RT_UNLIKELY(!&__orc_rt_jit_dispatch_ctx))
      return make_error<StringError>("__orc_rt_jit_dispatch_ctx not set");
    if (ORC_RT_UNLIKELY(!&__orc_rt_jit_dispatch))
      return make_error<StringError>("__orc_rt_jit_dispatch not set");
#endif
    auto ArgBuffer =
        WrapperFunctionResult::fromSPSArgs<SPSArgList<SPSTagTs...>>(Args...);
    if (const char *ErrMsg = ArgBuffer.getOutOfBandError())
      return make_error<StringError>(ErrMsg);

    WrapperFunctionResult ResultBuffer = __orc_rt_jit_dispatch(
        &__orc_rt_jit_dispatch_ctx, FnTag, ArgBuffer.data(), ArgBuffer.size());
    if (auto ErrMsg = ResultBuffer.getOutOfBandError())
      return make_error<StringError>(ErrMsg);

    return detail::ResultDeserializer<SPSRetTagT, RetT>::deserialize(
        Result, ResultBuffer.data(), ResultBuffer.size());
  }

  template <typename HandlerT>
  static WrapperFunctionResult handle(const char *ArgData, size_t ArgSize,
                                      HandlerT &&Handler) {
    using WFHH =
        detail::WrapperFunctionHandlerHelper<std::remove_reference_t<HandlerT>,
                                             ResultSerializer, SPSTagTs...>;
    return WFHH::apply(std::forward<HandlerT>(Handler), ArgData, ArgSize);
  }

private:
  template <typename T> static const T &makeSerializable(const T &Value) {
    return Value;
  }

  static detail::SPSSerializableError makeSerializable(Error Err) {
    return detail::toSPSSerializable(std::move(Err));
  }

  template <typename T>
  static detail::SPSSerializableExpected<T> makeSerializable(Expected<T> E) {
    return detail::toSPSSerializable(std::move(E));
  }
};

template <typename... SPSTagTs>
class WrapperFunction<void(SPSTagTs...)>
    : private WrapperFunction<SPSEmpty(SPSTagTs...)> {
public:
  template <typename... ArgTs>
  static Error call(const void *FnTag, const ArgTs &...Args) {
    SPSEmpty BE;
    return WrapperFunction<SPSEmpty(SPSTagTs...)>::call(FnTag, BE, Args...);
  }

  using WrapperFunction<SPSEmpty(SPSTagTs...)>::handle;
};

/// A function object that takes an ExecutorAddr as its first argument,
/// casts that address to a ClassT*, then calls the given method on that
/// pointer passing in the remaining function arguments. This utility
/// removes some of the boilerplate from writing wrappers for method calls.
///
///   @code{.cpp}
///   class MyClass {
///   public:
///     void myMethod(uint32_t, bool) { ... }
///   };
///
///   // SPS Method signature -- note MyClass object address as first argument.
///   using SPSMyMethodWrapperSignature =
///     SPSTuple<SPSExecutorAddr, uint32_t, bool>;
///
///   WrapperFunctionResult
///   myMethodCallWrapper(const char *ArgData, size_t ArgSize) {
///     return WrapperFunction<SPSMyMethodWrapperSignature>::handle(
///        ArgData, ArgSize, makeMethodWrapperHandler(&MyClass::myMethod));
///   }
///   @endcode
///
template <typename RetT, typename ClassT, typename... ArgTs>
class MethodWrapperHandler {
public:
  using MethodT = RetT (ClassT::*)(ArgTs...);
  MethodWrapperHandler(MethodT M) : M(M) {}
  RetT operator()(ExecutorAddr ObjAddr, ArgTs &...Args) {
    return (ObjAddr.toPtr<ClassT *>()->*M)(std::forward<ArgTs>(Args)...);
  }

private:
  MethodT M;
};

/// Create a MethodWrapperHandler object from the given method pointer.
template <typename RetT, typename ClassT, typename... ArgTs>
MethodWrapperHandler<RetT, ClassT, ArgTs...>
makeMethodWrapperHandler(RetT (ClassT::*Method)(ArgTs...)) {
  return MethodWrapperHandler<RetT, ClassT, ArgTs...>(Method);
}

/// Represents a call to a wrapper function.
class WrapperFunctionCall {
public:
  // FIXME: Switch to a SmallVector<char, 24> once ORC runtime has a
  // smallvector.
  using ArgDataBufferType = std::vector<char>;

  /// Create a WrapperFunctionCall using the given SPS serializer to serialize
  /// the arguments.
  template <typename SPSSerializer, typename... ArgTs>
  static Expected<WrapperFunctionCall> Create(ExecutorAddr FnAddr,
                                              const ArgTs &...Args) {
    ArgDataBufferType ArgData;
    ArgData.resize(SPSSerializer::size(Args...));
    SPSOutputBuffer OB(ArgData.empty() ? nullptr : ArgData.data(),
                       ArgData.size());
    if (SPSSerializer::serialize(OB, Args...))
      return WrapperFunctionCall(FnAddr, std::move(ArgData));
    return make_error<StringError>("Cannot serialize arguments for "
                                   "AllocActionCall");
  }

  WrapperFunctionCall() = default;

  /// Create a WrapperFunctionCall from a target function and arg buffer.
  WrapperFunctionCall(ExecutorAddr FnAddr, ArgDataBufferType ArgData)
      : FnAddr(FnAddr), ArgData(std::move(ArgData)) {}

  /// Returns the address to be called.
  const ExecutorAddr &getCallee() const { return FnAddr; }

  /// Returns the argument data.
  const ArgDataBufferType &getArgData() const { return ArgData; }

  /// WrapperFunctionCalls convert to true if the callee is non-null.
  explicit operator bool() const { return !!FnAddr; }

  /// Run call returning raw WrapperFunctionResult.
  WrapperFunctionResult run() const {
    using FnTy =
        orc_rt_CWrapperFunctionResult(const char *ArgData, size_t ArgSize);
    return WrapperFunctionResult(
        FnAddr.toPtr<FnTy *>()(ArgData.data(), ArgData.size()));
  }

  /// Run call and deserialize result using SPS.
  template <typename SPSRetT, typename RetT>
  std::enable_if_t<!std::is_same<SPSRetT, void>::value, Error>
  runWithSPSRet(RetT &RetVal) const {
    auto WFR = run();
    if (const char *ErrMsg = WFR.getOutOfBandError())
      return make_error<StringError>(ErrMsg);
    SPSInputBuffer IB(WFR.data(), WFR.size());
    if (!SPSSerializationTraits<SPSRetT, RetT>::deserialize(IB, RetVal))
      return make_error<StringError>("Could not deserialize result from "
                                     "serialized wrapper function call");
    return Error::success();
  }

  /// Overload for SPS functions returning void.
  template <typename SPSRetT>
  std::enable_if_t<std::is_same<SPSRetT, void>::value, Error>
  runWithSPSRet() const {
    SPSEmpty E;
    return runWithSPSRet<SPSEmpty>(E);
  }

  /// Run call and deserialize an SPSError result. SPSError returns and
  /// deserialization failures are merged into the returned error.
  Error runWithSPSRetErrorMerged() const {
    detail::SPSSerializableError RetErr;
    if (auto Err = runWithSPSRet<SPSError>(RetErr))
      return Err;
    return detail::fromSPSSerializable(std::move(RetErr));
  }

private:
  ExecutorAddr FnAddr;
  std::vector<char> ArgData;
};

using SPSWrapperFunctionCall = SPSTuple<SPSExecutorAddr, SPSSequence<char>>;

template <>
class SPSSerializationTraits<SPSWrapperFunctionCall, WrapperFunctionCall> {
public:
  static size_t size(const WrapperFunctionCall &WFC) {
    return SPSArgList<SPSExecutorAddr, SPSSequence<char>>::size(
        WFC.getCallee(), WFC.getArgData());
  }

  static bool serialize(SPSOutputBuffer &OB, const WrapperFunctionCall &WFC) {
    return SPSArgList<SPSExecutorAddr, SPSSequence<char>>::serialize(
        OB, WFC.getCallee(), WFC.getArgData());
  }

  static bool deserialize(SPSInputBuffer &IB, WrapperFunctionCall &WFC) {
    ExecutorAddr FnAddr;
    WrapperFunctionCall::ArgDataBufferType ArgData;
    if (!SPSWrapperFunctionCall::AsArgList::deserialize(IB, FnAddr, ArgData))
      return false;
    WFC = WrapperFunctionCall(FnAddr, std::move(ArgData));
    return true;
  }
};

} // end namespace __orc_rt

#endif // ORC_RT_WRAPPER_FUNCTION_UTILS_H
