//===- WrapperFunctionUtils.h - Utilities for wrapper functions -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A buffer for serialized results.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SHARED_WRAPPERFUNCTIONUTILS_H
#define LLVM_EXECUTIONENGINE_ORC_SHARED_WRAPPERFUNCTIONUTILS_H

#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/ExecutionEngine/Orc/Shared/SimplePackedSerialization.h"
#include "llvm/Support/Error.h"

#include <type_traits>

namespace llvm {
namespace orc {
namespace shared {

// Must be kept in-sync with compiler-rt/lib/orc/c-api.h.
union CWrapperFunctionResultDataUnion {
  char *ValuePtr;
  char Value[sizeof(ValuePtr)];
};

// Must be kept in-sync with compiler-rt/lib/orc/c-api.h.
typedef struct {
  CWrapperFunctionResultDataUnion Data;
  size_t Size;
} CWrapperFunctionResult;

/// C++ wrapper function result: Same as CWrapperFunctionResult but
/// auto-releases memory.
class WrapperFunctionResult {
public:
  /// Create a default WrapperFunctionResult.
  WrapperFunctionResult() { init(R); }

  /// Create a WrapperFunctionResult by taking ownership of a
  /// CWrapperFunctionResult.
  ///
  /// Warning: This should only be used by clients writing wrapper-function
  /// caller utilities (like TargetProcessControl).
  WrapperFunctionResult(CWrapperFunctionResult R) : R(R) {
    // Reset R.
    init(R);
  }

  WrapperFunctionResult(const WrapperFunctionResult &) = delete;
  WrapperFunctionResult &operator=(const WrapperFunctionResult &) = delete;

  WrapperFunctionResult(WrapperFunctionResult &&Other) {
    init(R);
    std::swap(R, Other.R);
  }

  WrapperFunctionResult &operator=(WrapperFunctionResult &&Other) {
    WrapperFunctionResult Tmp(std::move(Other));
    std::swap(R, Tmp.R);
    return *this;
  }

  ~WrapperFunctionResult() {
    if ((R.Size > sizeof(R.Data.Value)) ||
        (R.Size == 0 && R.Data.ValuePtr != nullptr))
      free(R.Data.ValuePtr);
  }

  /// Release ownership of the contained CWrapperFunctionResult.
  /// Warning: Do not use -- this method will be removed in the future. It only
  /// exists to temporarily support some code that will eventually be moved to
  /// the ORC runtime.
  CWrapperFunctionResult release() {
    CWrapperFunctionResult Tmp;
    init(Tmp);
    std::swap(R, Tmp);
    return Tmp;
  }

  /// Get a pointer to the data contained in this instance.
  char *data() {
    assert((R.Size != 0 || R.Data.ValuePtr == nullptr) &&
           "Cannot get data for out-of-band error value");
    return R.Size > sizeof(R.Data.Value) ? R.Data.ValuePtr : R.Data.Value;
  }

  /// Get a const pointer to the data contained in this instance.
  const char *data() const {
    assert((R.Size != 0 || R.Data.ValuePtr == nullptr) &&
           "Cannot get data for out-of-band error value");
    return R.Size > sizeof(R.Data.Value) ? R.Data.ValuePtr : R.Data.Value;
  }

  /// Returns the size of the data contained in this instance.
  size_t size() const {
    assert((R.Size != 0 || R.Data.ValuePtr == nullptr) &&
           "Cannot get data for out-of-band error value");
    return R.Size;
  }

  /// Returns true if this value is equivalent to a default-constructed
  /// WrapperFunctionResult.
  bool empty() const { return R.Size == 0 && R.Data.ValuePtr == nullptr; }

  /// Create a WrapperFunctionResult with the given size and return a pointer
  /// to the underlying memory.
  static WrapperFunctionResult allocate(size_t Size) {
    // Reset.
    WrapperFunctionResult WFR;
    WFR.R.Size = Size;
    if (WFR.R.Size > sizeof(WFR.R.Data.Value))
      WFR.R.Data.ValuePtr = (char *)malloc(WFR.R.Size);
    return WFR;
  }

  /// Copy from the given char range.
  static WrapperFunctionResult copyFrom(const char *Source, size_t Size) {
    auto WFR = allocate(Size);
    memcpy(WFR.data(), Source, Size);
    return WFR;
  }

  /// Copy from the given null-terminated string (includes the null-terminator).
  static WrapperFunctionResult copyFrom(const char *Source) {
    return copyFrom(Source, strlen(Source) + 1);
  }

  /// Copy from the given std::string (includes the null terminator).
  static WrapperFunctionResult copyFrom(const std::string &Source) {
    return copyFrom(Source.c_str());
  }

  /// Create an out-of-band error by copying the given string.
  static WrapperFunctionResult createOutOfBandError(const char *Msg) {
    // Reset.
    WrapperFunctionResult WFR;
    char *Tmp = (char *)malloc(strlen(Msg) + 1);
    strcpy(Tmp, Msg);
    WFR.R.Data.ValuePtr = Tmp;
    return WFR;
  }

  /// Create an out-of-band error by copying the given string.
  static WrapperFunctionResult createOutOfBandError(const std::string &Msg) {
    return createOutOfBandError(Msg.c_str());
  }

  /// If this value is an out-of-band error then this returns the error message,
  /// otherwise returns nullptr.
  const char *getOutOfBandError() const {
    return R.Size == 0 ? R.Data.ValuePtr : nullptr;
  }

private:
  static void init(CWrapperFunctionResult &R) {
    R.Data.ValuePtr = nullptr;
    R.Size = 0;
  }

  CWrapperFunctionResult R;
};

namespace detail {

template <typename SPSArgListT, typename... ArgTs>
WrapperFunctionResult
serializeViaSPSToWrapperFunctionResult(const ArgTs &...Args) {
  auto Result = WrapperFunctionResult::allocate(SPSArgListT::size(Args...));
  SPSOutputBuffer OB(Result.data(), Result.size());
  if (!SPSArgListT::serialize(OB, Args...))
    return WrapperFunctionResult::createOutOfBandError(
        "Error serializing arguments to blob in call");
  return Result;
}

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

template <typename WrapperFunctionImplT,
          template <typename> class ResultSerializer, typename... SPSTagTs>
class WrapperFunctionAsyncHandlerHelper
    : public WrapperFunctionAsyncHandlerHelper<
          decltype(&std::remove_reference_t<WrapperFunctionImplT>::operator()),
          ResultSerializer, SPSTagTs...> {};

template <typename RetT, typename SendResultT, typename... ArgTs,
          template <typename> class ResultSerializer, typename... SPSTagTs>
class WrapperFunctionAsyncHandlerHelper<RetT(SendResultT, ArgTs...),
                                        ResultSerializer, SPSTagTs...> {
public:
  using ArgTuple = std::tuple<std::decay_t<ArgTs>...>;
  using ArgIndices = std::make_index_sequence<std::tuple_size<ArgTuple>::value>;

  template <typename HandlerT, typename SendWrapperFunctionResultT>
  static void applyAsync(HandlerT &&H,
                         SendWrapperFunctionResultT &&SendWrapperFunctionResult,
                         const char *ArgData, size_t ArgSize) {
    ArgTuple Args;
    if (!deserialize(ArgData, ArgSize, Args, ArgIndices{})) {
      SendWrapperFunctionResult(WrapperFunctionResult::createOutOfBandError(
          "Could not deserialize arguments for wrapper function call"));
      return;
    }

    auto SendResult =
        [SendWFR = std::move(SendWrapperFunctionResult)](auto Result) mutable {
          using ResultT = decltype(Result);
          SendWFR(ResultSerializer<ResultT>::serialize(std::move(Result)));
        };

    callAsync(std::forward<HandlerT>(H), std::move(SendResult), std::move(Args),
              ArgIndices{});
  }

private:
  template <std::size_t... I>
  static bool deserialize(const char *ArgData, size_t ArgSize, ArgTuple &Args,
                          std::index_sequence<I...>) {
    SPSInputBuffer IB(ArgData, ArgSize);
    return SPSArgList<SPSTagTs...>::deserialize(IB, std::get<I>(Args)...);
  }

  template <typename HandlerT, typename SerializeAndSendResultT,
            typename ArgTupleT, std::size_t... I>
  static void callAsync(HandlerT &&H,
                        SerializeAndSendResultT &&SerializeAndSendResult,
                        ArgTupleT Args, std::index_sequence<I...>) {
    (void)Args; // Silence a buggy GCC warning.
    return std::forward<HandlerT>(H)(std::move(SerializeAndSendResult),
                                     std::move(std::get<I>(Args))...);
  }
};

// Map function pointers to function types.
template <typename RetT, typename... ArgTs,
          template <typename> class ResultSerializer, typename... SPSTagTs>
class WrapperFunctionAsyncHandlerHelper<RetT (*)(ArgTs...), ResultSerializer,
                                        SPSTagTs...>
    : public WrapperFunctionAsyncHandlerHelper<RetT(ArgTs...), ResultSerializer,
                                               SPSTagTs...> {};

// Map non-const member function types to function types.
template <typename ClassT, typename RetT, typename... ArgTs,
          template <typename> class ResultSerializer, typename... SPSTagTs>
class WrapperFunctionAsyncHandlerHelper<RetT (ClassT::*)(ArgTs...),
                                        ResultSerializer, SPSTagTs...>
    : public WrapperFunctionAsyncHandlerHelper<RetT(ArgTs...), ResultSerializer,
                                               SPSTagTs...> {};

// Map const member function types to function types.
template <typename ClassT, typename RetT, typename... ArgTs,
          template <typename> class ResultSerializer, typename... SPSTagTs>
class WrapperFunctionAsyncHandlerHelper<RetT (ClassT::*)(ArgTs...) const,
                                        ResultSerializer, SPSTagTs...>
    : public WrapperFunctionAsyncHandlerHelper<RetT(ArgTs...), ResultSerializer,
                                               SPSTagTs...> {};

template <typename SPSRetTagT, typename RetT> class ResultSerializer {
public:
  static WrapperFunctionResult serialize(RetT Result) {
    return serializeViaSPSToWrapperFunctionResult<SPSArgList<SPSRetTagT>>(
        Result);
  }
};

template <typename SPSRetTagT> class ResultSerializer<SPSRetTagT, Error> {
public:
  static WrapperFunctionResult serialize(Error Err) {
    return serializeViaSPSToWrapperFunctionResult<SPSArgList<SPSRetTagT>>(
        toSPSSerializable(std::move(Err)));
  }
};

template <typename SPSRetTagT>
class ResultSerializer<SPSRetTagT, ErrorSuccess> {
public:
  static WrapperFunctionResult serialize(ErrorSuccess Err) {
    return serializeViaSPSToWrapperFunctionResult<SPSArgList<SPSRetTagT>>(
        toSPSSerializable(std::move(Err)));
  }
};

template <typename SPSRetTagT, typename T>
class ResultSerializer<SPSRetTagT, Expected<T>> {
public:
  static WrapperFunctionResult serialize(Expected<T> E) {
    return serializeViaSPSToWrapperFunctionResult<SPSArgList<SPSRetTagT>>(
        toSPSSerializable(std::move(E)));
  }
};

template <typename SPSRetTagT, typename RetT> class ResultDeserializer {
public:
  static RetT makeValue() { return RetT(); }
  static void makeSafe(RetT &Result) {}

  static Error deserialize(RetT &Result, const char *ArgData, size_t ArgSize) {
    SPSInputBuffer IB(ArgData, ArgSize);
    if (!SPSArgList<SPSRetTagT>::deserialize(IB, Result))
      return make_error<StringError>(
          "Error deserializing return value from blob in call",
          inconvertibleErrorCode());
    return Error::success();
  }
};

template <> class ResultDeserializer<SPSError, Error> {
public:
  static Error makeValue() { return Error::success(); }
  static void makeSafe(Error &Err) { cantFail(std::move(Err)); }

  static Error deserialize(Error &Err, const char *ArgData, size_t ArgSize) {
    SPSInputBuffer IB(ArgData, ArgSize);
    SPSSerializableError BSE;
    if (!SPSArgList<SPSError>::deserialize(IB, BSE))
      return make_error<StringError>(
          "Error deserializing return value from blob in call",
          inconvertibleErrorCode());
    Err = fromSPSSerializable(std::move(BSE));
    return Error::success();
  }
};

template <typename SPSTagT, typename T>
class ResultDeserializer<SPSExpected<SPSTagT>, Expected<T>> {
public:
  static Expected<T> makeValue() { return T(); }
  static void makeSafe(Expected<T> &E) { cantFail(E.takeError()); }

  static Error deserialize(Expected<T> &E, const char *ArgData,
                           size_t ArgSize) {
    SPSInputBuffer IB(ArgData, ArgSize);
    SPSSerializableExpected<T> BSE;
    if (!SPSArgList<SPSExpected<SPSTagT>>::deserialize(IB, BSE))
      return make_error<StringError>(
          "Error deserializing return value from blob in call",
          inconvertibleErrorCode());
    E = fromSPSSerializable(std::move(BSE));
    return Error::success();
  }
};

template <typename SPSRetTagT, typename RetT> class AsyncCallResultHelper {
  // Did you forget to use Error / Expected in your handler?
};

} // end namespace detail

template <typename SPSSignature> class WrapperFunction;

template <typename SPSRetTagT, typename... SPSTagTs>
class WrapperFunction<SPSRetTagT(SPSTagTs...)> {
private:
  template <typename RetT>
  using ResultSerializer = detail::ResultSerializer<SPSRetTagT, RetT>;

public:
  /// Call a wrapper function. Caller should be callable as
  /// WrapperFunctionResult Fn(const char *ArgData, size_t ArgSize);
  template <typename CallerFn, typename RetT, typename... ArgTs>
  static Error call(const CallerFn &Caller, RetT &Result,
                    const ArgTs &...Args) {

    // RetT might be an Error or Expected value. Set the checked flag now:
    // we don't want the user to have to check the unused result if this
    // operation fails.
    detail::ResultDeserializer<SPSRetTagT, RetT>::makeSafe(Result);

    auto ArgBuffer =
        detail::serializeViaSPSToWrapperFunctionResult<SPSArgList<SPSTagTs...>>(
            Args...);
    if (const char *ErrMsg = ArgBuffer.getOutOfBandError())
      return make_error<StringError>(ErrMsg, inconvertibleErrorCode());

    WrapperFunctionResult ResultBuffer =
        Caller(ArgBuffer.data(), ArgBuffer.size());
    if (auto ErrMsg = ResultBuffer.getOutOfBandError())
      return make_error<StringError>(ErrMsg, inconvertibleErrorCode());

    return detail::ResultDeserializer<SPSRetTagT, RetT>::deserialize(
        Result, ResultBuffer.data(), ResultBuffer.size());
  }

  /// Call an async wrapper function.
  /// Caller should be callable as
  /// void Fn(unique_function<void(WrapperFunctionResult)> SendResult,
  ///         WrapperFunctionResult ArgBuffer);
  template <typename AsyncCallerFn, typename SendDeserializedResultFn,
            typename... ArgTs>
  static void callAsync(AsyncCallerFn &&Caller,
                        SendDeserializedResultFn &&SendDeserializedResult,
                        const ArgTs &...Args) {
    using RetT = typename std::tuple_element<
        1, typename detail::WrapperFunctionHandlerHelper<
               std::remove_reference_t<SendDeserializedResultFn>,
               ResultSerializer, SPSRetTagT>::ArgTuple>::type;

    auto ArgBuffer =
        detail::serializeViaSPSToWrapperFunctionResult<SPSArgList<SPSTagTs...>>(
            Args...);
    if (auto *ErrMsg = ArgBuffer.getOutOfBandError()) {
      SendDeserializedResult(
          make_error<StringError>(ErrMsg, inconvertibleErrorCode()),
          detail::ResultDeserializer<SPSRetTagT, RetT>::makeValue());
      return;
    }

    auto SendSerializedResult = [SDR = std::move(SendDeserializedResult)](
                                    WrapperFunctionResult R) mutable {
      RetT RetVal = detail::ResultDeserializer<SPSRetTagT, RetT>::makeValue();
      detail::ResultDeserializer<SPSRetTagT, RetT>::makeSafe(RetVal);

      if (auto *ErrMsg = R.getOutOfBandError()) {
        SDR(make_error<StringError>(ErrMsg, inconvertibleErrorCode()),
            std::move(RetVal));
        return;
      }

      SPSInputBuffer IB(R.data(), R.size());
      if (auto Err = detail::ResultDeserializer<SPSRetTagT, RetT>::deserialize(
              RetVal, R.data(), R.size())) {
        SDR(std::move(Err), std::move(RetVal));
        return;
      }

      SDR(Error::success(), std::move(RetVal));
    };

    Caller(std::move(SendSerializedResult), ArgBuffer.data(), ArgBuffer.size());
  }

  /// Handle a call to a wrapper function.
  template <typename HandlerT>
  static WrapperFunctionResult handle(const char *ArgData, size_t ArgSize,
                                      HandlerT &&Handler) {
    using WFHH =
        detail::WrapperFunctionHandlerHelper<std::remove_reference_t<HandlerT>,
                                             ResultSerializer, SPSTagTs...>;
    return WFHH::apply(std::forward<HandlerT>(Handler), ArgData, ArgSize);
  }

  /// Handle a call to an async wrapper function.
  template <typename HandlerT, typename SendResultT>
  static void handleAsync(const char *ArgData, size_t ArgSize,
                          HandlerT &&Handler, SendResultT &&SendResult) {
    using WFAHH = detail::WrapperFunctionAsyncHandlerHelper<
        std::remove_reference_t<HandlerT>, ResultSerializer, SPSTagTs...>;
    WFAHH::applyAsync(std::forward<HandlerT>(Handler),
                      std::forward<SendResultT>(SendResult), ArgData, ArgSize);
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
  template <typename CallerFn, typename... ArgTs>
  static Error call(const CallerFn &Caller, const ArgTs &...Args) {
    SPSEmpty BE;
    return WrapperFunction<SPSEmpty(SPSTagTs...)>::call(Caller, BE, Args...);
  }

  template <typename AsyncCallerFn, typename SendDeserializedResultFn,
            typename... ArgTs>
  static void callAsync(AsyncCallerFn &&Caller,
                        SendDeserializedResultFn &&SendDeserializedResult,
                        const ArgTs &...Args) {
    WrapperFunction<SPSEmpty(SPSTagTs...)>::callAsync(
        std::forward<AsyncCallerFn>(Caller),
        [SDR = std::move(SendDeserializedResult)](Error SerializeErr,
                                                  SPSEmpty E) mutable {
          SDR(std::move(SerializeErr));
        },
        Args...);
  }

  using WrapperFunction<SPSEmpty(SPSTagTs...)>::handle;
  using WrapperFunction<SPSEmpty(SPSTagTs...)>::handleAsync;
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
    return (ObjAddr.toPtr<ClassT*>()->*M)(std::forward<ArgTs>(Args)...);
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

/// Represents a serialized wrapper function call.
/// Serializing calls themselves allows us to batch them: We can make one
/// "run-wrapper-functions" utility and send it a list of calls to run.
///
/// The motivating use-case for this API is JITLink allocation actions, where
/// we want to run multiple functions to finalize linked memory without having
/// to make separate IPC calls for each one.
class WrapperFunctionCall {
public:
  using ArgDataBufferType = SmallVector<char, 24>;

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
                                   "AllocActionCall",
                                   inconvertibleErrorCode());
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
  shared::WrapperFunctionResult run() const {
    using FnTy =
        shared::CWrapperFunctionResult(const char *ArgData, size_t ArgSize);
    return shared::WrapperFunctionResult(
        FnAddr.toPtr<FnTy *>()(ArgData.data(), ArgData.size()));
  }

  /// Run call and deserialize result using SPS.
  template <typename SPSRetT, typename RetT>
  std::enable_if_t<!std::is_same<SPSRetT, void>::value, Error>
  runWithSPSRet(RetT &RetVal) const {
    auto WFR = run();
    if (const char *ErrMsg = WFR.getOutOfBandError())
      return make_error<StringError>(ErrMsg, inconvertibleErrorCode());
    shared::SPSInputBuffer IB(WFR.data(), WFR.size());
    if (!shared::SPSSerializationTraits<SPSRetT, RetT>::deserialize(IB, RetVal))
      return make_error<StringError>("Could not deserialize result from "
                                     "serialized wrapper function call",
                                     inconvertibleErrorCode());
    return Error::success();
  }

  /// Overload for SPS functions returning void.
  template <typename SPSRetT>
  std::enable_if_t<std::is_same<SPSRetT, void>::value, Error>
  runWithSPSRet() const {
    shared::SPSEmpty E;
    return runWithSPSRet<shared::SPSEmpty>(E);
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
  orc::ExecutorAddr FnAddr;
  ArgDataBufferType ArgData;
};

using SPSWrapperFunctionCall = SPSTuple<SPSExecutorAddr, SPSSequence<char>>;

template <>
class SPSSerializationTraits<SPSWrapperFunctionCall, WrapperFunctionCall> {
public:
  static size_t size(const WrapperFunctionCall &WFC) {
    return SPSWrapperFunctionCall::AsArgList::size(WFC.getCallee(),
                                                   WFC.getArgData());
  }

  static bool serialize(SPSOutputBuffer &OB, const WrapperFunctionCall &WFC) {
    return SPSWrapperFunctionCall::AsArgList::serialize(OB, WFC.getCallee(),
                                                        WFC.getArgData());
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

} // end namespace shared
} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_SHARED_WRAPPERFUNCTIONUTILS_H
