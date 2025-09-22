//===- llvm/Support/Error.h - Recoverable error handling --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines an API used to report recoverable errors.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ERROR_H
#define LLVM_SUPPORT_ERROR_H

#include "llvm-c/Error.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Config/abi-breaking.h"
#include "llvm/Support/AlignOf.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace llvm {

class ErrorSuccess;

/// Base class for error info classes. Do not extend this directly: Extend
/// the ErrorInfo template subclass instead.
class ErrorInfoBase {
public:
  virtual ~ErrorInfoBase() = default;

  /// Print an error message to an output stream.
  virtual void log(raw_ostream &OS) const = 0;

  /// Return the error message as a string.
  virtual std::string message() const {
    std::string Msg;
    raw_string_ostream OS(Msg);
    log(OS);
    return Msg;
  }

  /// Convert this error to a std::error_code.
  ///
  /// This is a temporary crutch to enable interaction with code still
  /// using std::error_code. It will be removed in the future.
  virtual std::error_code convertToErrorCode() const = 0;

  // Returns the class ID for this type.
  static const void *classID() { return &ID; }

  // Returns the class ID for the dynamic type of this ErrorInfoBase instance.
  virtual const void *dynamicClassID() const = 0;

  // Check whether this instance is a subclass of the class identified by
  // ClassID.
  virtual bool isA(const void *const ClassID) const {
    return ClassID == classID();
  }

  // Check whether this instance is a subclass of ErrorInfoT.
  template <typename ErrorInfoT> bool isA() const {
    return isA(ErrorInfoT::classID());
  }

private:
  virtual void anchor();

  static char ID;
};

/// Lightweight error class with error context and mandatory checking.
///
/// Instances of this class wrap a ErrorInfoBase pointer. Failure states
/// are represented by setting the pointer to a ErrorInfoBase subclass
/// instance containing information describing the failure. Success is
/// represented by a null pointer value.
///
/// Instances of Error also contains a 'Checked' flag, which must be set
/// before the destructor is called, otherwise the destructor will trigger a
/// runtime error. This enforces at runtime the requirement that all Error
/// instances be checked or returned to the caller.
///
/// There are two ways to set the checked flag, depending on what state the
/// Error instance is in. For Error instances indicating success, it
/// is sufficient to invoke the boolean conversion operator. E.g.:
///
///   @code{.cpp}
///   Error foo(<...>);
///
///   if (auto E = foo(<...>))
///     return E; // <- Return E if it is in the error state.
///   // We have verified that E was in the success state. It can now be safely
///   // destroyed.
///   @endcode
///
/// A success value *can not* be dropped. For example, just calling 'foo(<...>)'
/// without testing the return value will raise a runtime error, even if foo
/// returns success.
///
/// For Error instances representing failure, you must use either the
/// handleErrors or handleAllErrors function with a typed handler. E.g.:
///
///   @code{.cpp}
///   class MyErrorInfo : public ErrorInfo<MyErrorInfo> {
///     // Custom error info.
///   };
///
///   Error foo(<...>) { return make_error<MyErrorInfo>(...); }
///
///   auto E = foo(<...>); // <- foo returns failure with MyErrorInfo.
///   auto NewE =
///     handleErrors(std::move(E),
///       [](const MyErrorInfo &M) {
///         // Deal with the error.
///       },
///       [](std::unique_ptr<OtherError> M) -> Error {
///         if (canHandle(*M)) {
///           // handle error.
///           return Error::success();
///         }
///         // Couldn't handle this error instance. Pass it up the stack.
///         return Error(std::move(M));
///     });
///   // Note - The error passed to handleErrors will be marked as checked. If
///   // there is no matched handler, a new error with the same payload is
///   // created and returned.
///   // The handlers take the error checked by handleErrors as an argument,
///   // which can be used to retrieve more information. If a new error is
///   // created by a handler, it will be passed back to the caller of
///   // handleErrors and needs to be checked or return up to the stack.
///   // Otherwise, the passed-in error is considered consumed.
///   @endcode
///
/// The handleAllErrors function is identical to handleErrors, except
/// that it has a void return type, and requires all errors to be handled and
/// no new errors be returned. It prevents errors (assuming they can all be
/// handled) from having to be bubbled all the way to the top-level.
///
/// *All* Error instances must be checked before destruction, even if
/// they're moved-assigned or constructed from Success values that have already
/// been checked. This enforces checking through all levels of the call stack.
class [[nodiscard]] Error {
  // ErrorList needs to be able to yank ErrorInfoBase pointers out of Errors
  // to add to the error list. It can't rely on handleErrors for this, since
  // handleErrors does not support ErrorList handlers.
  friend class ErrorList;

  // handleErrors needs to be able to set the Checked flag.
  template <typename... HandlerTs>
  friend Error handleErrors(Error E, HandlerTs &&... Handlers);
  // visitErrors needs direct access to the payload.
  template <typename HandlerT>
  friend void visitErrors(const Error &E, HandlerT H);

  // Expected<T> needs to be able to steal the payload when constructed from an
  // error.
  template <typename T> friend class Expected;

  // wrap needs to be able to steal the payload.
  friend LLVMErrorRef wrap(Error);

protected:
  /// Create a success value. Prefer using 'Error::success()' for readability
  Error() {
    setPtr(nullptr);
    setChecked(false);
  }

public:
  /// Create a success value.
  static ErrorSuccess success();

  // Errors are not copy-constructable.
  Error(const Error &Other) = delete;

  /// Move-construct an error value. The newly constructed error is considered
  /// unchecked, even if the source error had been checked. The original error
  /// becomes a checked Success value, regardless of its original state.
  Error(Error &&Other) {
    setChecked(true);
    *this = std::move(Other);
  }

  /// Create an error value. Prefer using the 'make_error' function, but
  /// this constructor can be useful when "re-throwing" errors from handlers.
  Error(std::unique_ptr<ErrorInfoBase> Payload) {
    setPtr(Payload.release());
    setChecked(false);
  }

  // Errors are not copy-assignable.
  Error &operator=(const Error &Other) = delete;

  /// Move-assign an error value. The current error must represent success, you
  /// you cannot overwrite an unhandled error. The current error is then
  /// considered unchecked. The source error becomes a checked success value,
  /// regardless of its original state.
  Error &operator=(Error &&Other) {
    // Don't allow overwriting of unchecked values.
    assertIsChecked();
    setPtr(Other.getPtr());

    // This Error is unchecked, even if the source error was checked.
    setChecked(false);

    // Null out Other's payload and set its checked bit.
    Other.setPtr(nullptr);
    Other.setChecked(true);

    return *this;
  }

  /// Destroy a Error. Fails with a call to abort() if the error is
  /// unchecked.
  ~Error() {
    assertIsChecked();
    delete getPtr();
  }

  /// Bool conversion. Returns true if this Error is in a failure state,
  /// and false if it is in an accept state. If the error is in a Success state
  /// it will be considered checked.
  explicit operator bool() {
    setChecked(getPtr() == nullptr);
    return getPtr() != nullptr;
  }

  /// Check whether one error is a subclass of another.
  template <typename ErrT> bool isA() const {
    return getPtr() && getPtr()->isA(ErrT::classID());
  }

  /// Returns the dynamic class id of this error, or null if this is a success
  /// value.
  const void* dynamicClassID() const {
    if (!getPtr())
      return nullptr;
    return getPtr()->dynamicClassID();
  }

private:
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  // assertIsChecked() happens very frequently, but under normal circumstances
  // is supposed to be a no-op.  So we want it to be inlined, but having a bunch
  // of debug prints can cause the function to be too large for inlining.  So
  // it's important that we define this function out of line so that it can't be
  // inlined.
  [[noreturn]] void fatalUncheckedError() const;
#endif

  void assertIsChecked() {
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    if (LLVM_UNLIKELY(!getChecked() || getPtr()))
      fatalUncheckedError();
#endif
  }

  ErrorInfoBase *getPtr() const {
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    return reinterpret_cast<ErrorInfoBase*>(
             reinterpret_cast<uintptr_t>(Payload) &
             ~static_cast<uintptr_t>(0x1));
#else
    return Payload;
#endif
  }

  void setPtr(ErrorInfoBase *EI) {
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    Payload = reinterpret_cast<ErrorInfoBase*>(
                (reinterpret_cast<uintptr_t>(EI) &
                 ~static_cast<uintptr_t>(0x1)) |
                (reinterpret_cast<uintptr_t>(Payload) & 0x1));
#else
    Payload = EI;
#endif
  }

  bool getChecked() const {
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    return (reinterpret_cast<uintptr_t>(Payload) & 0x1) == 0;
#else
    return true;
#endif
  }

  void setChecked(bool V) {
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    Payload = reinterpret_cast<ErrorInfoBase*>(
                (reinterpret_cast<uintptr_t>(Payload) &
                  ~static_cast<uintptr_t>(0x1)) |
                  (V ? 0 : 1));
#endif
  }

  std::unique_ptr<ErrorInfoBase> takePayload() {
    std::unique_ptr<ErrorInfoBase> Tmp(getPtr());
    setPtr(nullptr);
    setChecked(true);
    return Tmp;
  }

  friend raw_ostream &operator<<(raw_ostream &OS, const Error &E) {
    if (auto *P = E.getPtr())
      P->log(OS);
    else
      OS << "success";
    return OS;
  }

  ErrorInfoBase *Payload = nullptr;
};

/// Subclass of Error for the sole purpose of identifying the success path in
/// the type system. This allows to catch invalid conversion to Expected<T> at
/// compile time.
class ErrorSuccess final : public Error {};

inline ErrorSuccess Error::success() { return ErrorSuccess(); }

/// Make a Error instance representing failure using the given error info
/// type.
template <typename ErrT, typename... ArgTs> Error make_error(ArgTs &&... Args) {
  return Error(std::make_unique<ErrT>(std::forward<ArgTs>(Args)...));
}

/// Base class for user error types. Users should declare their error types
/// like:
///
/// class MyError : public ErrorInfo<MyError> {
///   ....
/// };
///
/// This class provides an implementation of the ErrorInfoBase::kind
/// method, which is used by the Error RTTI system.
template <typename ThisErrT, typename ParentErrT = ErrorInfoBase>
class ErrorInfo : public ParentErrT {
public:
  using ParentErrT::ParentErrT; // inherit constructors

  static const void *classID() { return &ThisErrT::ID; }

  const void *dynamicClassID() const override { return &ThisErrT::ID; }

  bool isA(const void *const ClassID) const override {
    return ClassID == classID() || ParentErrT::isA(ClassID);
  }
};

/// Special ErrorInfo subclass representing a list of ErrorInfos.
/// Instances of this class are constructed by joinError.
class ErrorList final : public ErrorInfo<ErrorList> {
  // handleErrors needs to be able to iterate the payload list of an
  // ErrorList.
  template <typename... HandlerTs>
  friend Error handleErrors(Error E, HandlerTs &&... Handlers);
  // visitErrors needs to be able to iterate the payload list of an
  // ErrorList.
  template <typename HandlerT>
  friend void visitErrors(const Error &E, HandlerT H);

  // joinErrors is implemented in terms of join.
  friend Error joinErrors(Error, Error);

public:
  void log(raw_ostream &OS) const override {
    OS << "Multiple errors:\n";
    for (const auto &ErrPayload : Payloads) {
      ErrPayload->log(OS);
      OS << "\n";
    }
  }

  std::error_code convertToErrorCode() const override;

  // Used by ErrorInfo::classID.
  static char ID;

private:
  ErrorList(std::unique_ptr<ErrorInfoBase> Payload1,
            std::unique_ptr<ErrorInfoBase> Payload2) {
    assert(!Payload1->isA<ErrorList>() && !Payload2->isA<ErrorList>() &&
           "ErrorList constructor payloads should be singleton errors");
    Payloads.push_back(std::move(Payload1));
    Payloads.push_back(std::move(Payload2));
  }

  static Error join(Error E1, Error E2) {
    if (!E1)
      return E2;
    if (!E2)
      return E1;
    if (E1.isA<ErrorList>()) {
      auto &E1List = static_cast<ErrorList &>(*E1.getPtr());
      if (E2.isA<ErrorList>()) {
        auto E2Payload = E2.takePayload();
        auto &E2List = static_cast<ErrorList &>(*E2Payload);
        for (auto &Payload : E2List.Payloads)
          E1List.Payloads.push_back(std::move(Payload));
      } else
        E1List.Payloads.push_back(E2.takePayload());

      return E1;
    }
    if (E2.isA<ErrorList>()) {
      auto &E2List = static_cast<ErrorList &>(*E2.getPtr());
      E2List.Payloads.insert(E2List.Payloads.begin(), E1.takePayload());
      return E2;
    }
    return Error(std::unique_ptr<ErrorList>(
        new ErrorList(E1.takePayload(), E2.takePayload())));
  }

  std::vector<std::unique_ptr<ErrorInfoBase>> Payloads;
};

/// Concatenate errors. The resulting Error is unchecked, and contains the
/// ErrorInfo(s), if any, contained in E1, followed by the
/// ErrorInfo(s), if any, contained in E2.
inline Error joinErrors(Error E1, Error E2) {
  return ErrorList::join(std::move(E1), std::move(E2));
}

/// Tagged union holding either a T or a Error.
///
/// This class parallels ErrorOr, but replaces error_code with Error. Since
/// Error cannot be copied, this class replaces getError() with
/// takeError(). It also adds an bool errorIsA<ErrT>() method for testing the
/// error class type.
///
/// Example usage of 'Expected<T>' as a function return type:
///
///   @code{.cpp}
///     Expected<int> myDivide(int A, int B) {
///       if (B == 0) {
///         // return an Error
///         return createStringError(inconvertibleErrorCode(),
///                                  "B must not be zero!");
///       }
///       // return an integer
///       return A / B;
///     }
///   @endcode
///
///   Checking the results of to a function returning 'Expected<T>':
///   @code{.cpp}
///     if (auto E = Result.takeError()) {
///       // We must consume the error. Typically one of:
///       // - return the error to our caller
///       // - toString(), when logging
///       // - consumeError(), to silently swallow the error
///       // - handleErrors(), to distinguish error types
///       errs() << "Problem with division " << toString(std::move(E)) << "\n";
///       return;
///     }
///     // use the result
///     outs() << "The answer is " << *Result << "\n";
///   @endcode
///
///  For unit-testing a function returning an 'Expected<T>', see the
///  'EXPECT_THAT_EXPECTED' macros in llvm/Testing/Support/Error.h

template <class T> class [[nodiscard]] Expected {
  template <class T1> friend class ExpectedAsOutParameter;
  template <class OtherT> friend class Expected;

  static constexpr bool isRef = std::is_reference_v<T>;

  using wrap = std::reference_wrapper<std::remove_reference_t<T>>;

  using error_type = std::unique_ptr<ErrorInfoBase>;

public:
  using storage_type = std::conditional_t<isRef, wrap, T>;
  using value_type = T;

private:
  using reference = std::remove_reference_t<T> &;
  using const_reference = const std::remove_reference_t<T> &;
  using pointer = std::remove_reference_t<T> *;
  using const_pointer = const std::remove_reference_t<T> *;

public:
  /// Create an Expected<T> error value from the given Error.
  Expected(Error &&Err)
      : HasError(true)
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
        // Expected is unchecked upon construction in Debug builds.
        , Unchecked(true)
#endif
  {
    assert(Err && "Cannot create Expected<T> from Error success value.");
    new (getErrorStorage()) error_type(Err.takePayload());
  }

  /// Forbid to convert from Error::success() implicitly, this avoids having
  /// Expected<T> foo() { return Error::success(); } which compiles otherwise
  /// but triggers the assertion above.
  Expected(ErrorSuccess) = delete;

  /// Create an Expected<T> success value from the given OtherT value, which
  /// must be convertible to T.
  template <typename OtherT>
  Expected(OtherT &&Val,
           std::enable_if_t<std::is_convertible_v<OtherT, T>> * = nullptr)
      : HasError(false)
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
        // Expected is unchecked upon construction in Debug builds.
        ,
        Unchecked(true)
#endif
  {
    new (getStorage()) storage_type(std::forward<OtherT>(Val));
  }

  /// Move construct an Expected<T> value.
  Expected(Expected &&Other) { moveConstruct(std::move(Other)); }

  /// Move construct an Expected<T> value from an Expected<OtherT>, where OtherT
  /// must be convertible to T.
  template <class OtherT>
  Expected(Expected<OtherT> &&Other,
           std::enable_if_t<std::is_convertible_v<OtherT, T>> * = nullptr) {
    moveConstruct(std::move(Other));
  }

  /// Move construct an Expected<T> value from an Expected<OtherT>, where OtherT
  /// isn't convertible to T.
  template <class OtherT>
  explicit Expected(
      Expected<OtherT> &&Other,
      std::enable_if_t<!std::is_convertible_v<OtherT, T>> * = nullptr) {
    moveConstruct(std::move(Other));
  }

  /// Move-assign from another Expected<T>.
  Expected &operator=(Expected &&Other) {
    moveAssign(std::move(Other));
    return *this;
  }

  /// Destroy an Expected<T>.
  ~Expected() {
    assertIsChecked();
    if (!HasError)
      getStorage()->~storage_type();
    else
      getErrorStorage()->~error_type();
  }

  /// Return false if there is an error.
  explicit operator bool() {
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    Unchecked = HasError;
#endif
    return !HasError;
  }

  /// Returns a reference to the stored T value.
  reference get() {
    assertIsChecked();
    return *getStorage();
  }

  /// Returns a const reference to the stored T value.
  const_reference get() const {
    assertIsChecked();
    return const_cast<Expected<T> *>(this)->get();
  }

  /// Returns \a takeError() after moving the held T (if any) into \p V.
  template <class OtherT>
  Error moveInto(
      OtherT &Value,
      std::enable_if_t<std::is_assignable_v<OtherT &, T &&>> * = nullptr) && {
    if (*this)
      Value = std::move(get());
    return takeError();
  }

  /// Check that this Expected<T> is an error of type ErrT.
  template <typename ErrT> bool errorIsA() const {
    return HasError && (*getErrorStorage())->template isA<ErrT>();
  }

  /// Take ownership of the stored error.
  /// After calling this the Expected<T> is in an indeterminate state that can
  /// only be safely destructed. No further calls (beside the destructor) should
  /// be made on the Expected<T> value.
  Error takeError() {
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    Unchecked = false;
#endif
    return HasError ? Error(std::move(*getErrorStorage())) : Error::success();
  }

  /// Returns a pointer to the stored T value.
  pointer operator->() {
    assertIsChecked();
    return toPointer(getStorage());
  }

  /// Returns a const pointer to the stored T value.
  const_pointer operator->() const {
    assertIsChecked();
    return toPointer(getStorage());
  }

  /// Returns a reference to the stored T value.
  reference operator*() {
    assertIsChecked();
    return *getStorage();
  }

  /// Returns a const reference to the stored T value.
  const_reference operator*() const {
    assertIsChecked();
    return *getStorage();
  }

private:
  template <class T1>
  static bool compareThisIfSameType(const T1 &a, const T1 &b) {
    return &a == &b;
  }

  template <class T1, class T2>
  static bool compareThisIfSameType(const T1 &, const T2 &) {
    return false;
  }

  template <class OtherT> void moveConstruct(Expected<OtherT> &&Other) {
    HasError = Other.HasError;
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    Unchecked = true;
    Other.Unchecked = false;
#endif

    if (!HasError)
      new (getStorage()) storage_type(std::move(*Other.getStorage()));
    else
      new (getErrorStorage()) error_type(std::move(*Other.getErrorStorage()));
  }

  template <class OtherT> void moveAssign(Expected<OtherT> &&Other) {
    assertIsChecked();

    if (compareThisIfSameType(*this, Other))
      return;

    this->~Expected();
    new (this) Expected(std::move(Other));
  }

  pointer toPointer(pointer Val) { return Val; }

  const_pointer toPointer(const_pointer Val) const { return Val; }

  pointer toPointer(wrap *Val) { return &Val->get(); }

  const_pointer toPointer(const wrap *Val) const { return &Val->get(); }

  storage_type *getStorage() {
    assert(!HasError && "Cannot get value when an error exists!");
    return reinterpret_cast<storage_type *>(&TStorage);
  }

  const storage_type *getStorage() const {
    assert(!HasError && "Cannot get value when an error exists!");
    return reinterpret_cast<const storage_type *>(&TStorage);
  }

  error_type *getErrorStorage() {
    assert(HasError && "Cannot get error when a value exists!");
    return reinterpret_cast<error_type *>(&ErrorStorage);
  }

  const error_type *getErrorStorage() const {
    assert(HasError && "Cannot get error when a value exists!");
    return reinterpret_cast<const error_type *>(&ErrorStorage);
  }

  // Used by ExpectedAsOutParameter to reset the checked flag.
  void setUnchecked() {
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    Unchecked = true;
#endif
  }

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  [[noreturn]] LLVM_ATTRIBUTE_NOINLINE void fatalUncheckedExpected() const {
    dbgs() << "Expected<T> must be checked before access or destruction.\n";
    if (HasError) {
      dbgs() << "Unchecked Expected<T> contained error:\n";
      (*getErrorStorage())->log(dbgs());
    } else
      dbgs() << "Expected<T> value was in success state. (Note: Expected<T> "
                "values in success mode must still be checked prior to being "
                "destroyed).\n";
    abort();
  }
#endif

  void assertIsChecked() const {
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    if (LLVM_UNLIKELY(Unchecked))
      fatalUncheckedExpected();
#endif
  }

  union {
    AlignedCharArrayUnion<storage_type> TStorage;
    AlignedCharArrayUnion<error_type> ErrorStorage;
  };
  bool HasError : 1;
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  bool Unchecked : 1;
#endif
};

/// Report a serious error, calling any installed error handler. See
/// ErrorHandling.h.
[[noreturn]] void report_fatal_error(Error Err, bool gen_crash_diag = true);

/// Report a fatal error if Err is a failure value.
///
/// This function can be used to wrap calls to fallible functions ONLY when it
/// is known that the Error will always be a success value. E.g.
///
///   @code{.cpp}
///   // foo only attempts the fallible operation if DoFallibleOperation is
///   // true. If DoFallibleOperation is false then foo always returns
///   // Error::success().
///   Error foo(bool DoFallibleOperation);
///
///   cantFail(foo(false));
///   @endcode
inline void cantFail(Error Err, const char *Msg = nullptr) {
  if (Err) {
    if (!Msg)
      Msg = "Failure value returned from cantFail wrapped call";
#ifndef NDEBUG
    std::string Str;
    raw_string_ostream OS(Str);
    OS << Msg << "\n" << Err;
    Msg = Str.c_str();
#endif
    llvm_unreachable(Msg);
  }
}

/// Report a fatal error if ValOrErr is a failure value, otherwise unwraps and
/// returns the contained value.
///
/// This function can be used to wrap calls to fallible functions ONLY when it
/// is known that the Error will always be a success value. E.g.
///
///   @code{.cpp}
///   // foo only attempts the fallible operation if DoFallibleOperation is
///   // true. If DoFallibleOperation is false then foo always returns an int.
///   Expected<int> foo(bool DoFallibleOperation);
///
///   int X = cantFail(foo(false));
///   @endcode
template <typename T>
T cantFail(Expected<T> ValOrErr, const char *Msg = nullptr) {
  if (ValOrErr)
    return std::move(*ValOrErr);
  else {
    if (!Msg)
      Msg = "Failure value returned from cantFail wrapped call";
#ifndef NDEBUG
    std::string Str;
    raw_string_ostream OS(Str);
    auto E = ValOrErr.takeError();
    OS << Msg << "\n" << E;
    Msg = Str.c_str();
#endif
    llvm_unreachable(Msg);
  }
}

/// Report a fatal error if ValOrErr is a failure value, otherwise unwraps and
/// returns the contained reference.
///
/// This function can be used to wrap calls to fallible functions ONLY when it
/// is known that the Error will always be a success value. E.g.
///
///   @code{.cpp}
///   // foo only attempts the fallible operation if DoFallibleOperation is
///   // true. If DoFallibleOperation is false then foo always returns a Bar&.
///   Expected<Bar&> foo(bool DoFallibleOperation);
///
///   Bar &X = cantFail(foo(false));
///   @endcode
template <typename T>
T& cantFail(Expected<T&> ValOrErr, const char *Msg = nullptr) {
  if (ValOrErr)
    return *ValOrErr;
  else {
    if (!Msg)
      Msg = "Failure value returned from cantFail wrapped call";
#ifndef NDEBUG
    std::string Str;
    raw_string_ostream OS(Str);
    auto E = ValOrErr.takeError();
    OS << Msg << "\n" << E;
    Msg = Str.c_str();
#endif
    llvm_unreachable(Msg);
  }
}

/// Helper for testing applicability of, and applying, handlers for
/// ErrorInfo types.
template <typename HandlerT>
class ErrorHandlerTraits
    : public ErrorHandlerTraits<
          decltype(&std::remove_reference_t<HandlerT>::operator())> {};

// Specialization functions of the form 'Error (const ErrT&)'.
template <typename ErrT> class ErrorHandlerTraits<Error (&)(ErrT &)> {
public:
  static bool appliesTo(const ErrorInfoBase &E) {
    return E.template isA<ErrT>();
  }

  template <typename HandlerT>
  static Error apply(HandlerT &&H, std::unique_ptr<ErrorInfoBase> E) {
    assert(appliesTo(*E) && "Applying incorrect handler");
    return H(static_cast<ErrT &>(*E));
  }
};

// Specialization functions of the form 'void (const ErrT&)'.
template <typename ErrT> class ErrorHandlerTraits<void (&)(ErrT &)> {
public:
  static bool appliesTo(const ErrorInfoBase &E) {
    return E.template isA<ErrT>();
  }

  template <typename HandlerT>
  static Error apply(HandlerT &&H, std::unique_ptr<ErrorInfoBase> E) {
    assert(appliesTo(*E) && "Applying incorrect handler");
    H(static_cast<ErrT &>(*E));
    return Error::success();
  }
};

/// Specialization for functions of the form 'Error (std::unique_ptr<ErrT>)'.
template <typename ErrT>
class ErrorHandlerTraits<Error (&)(std::unique_ptr<ErrT>)> {
public:
  static bool appliesTo(const ErrorInfoBase &E) {
    return E.template isA<ErrT>();
  }

  template <typename HandlerT>
  static Error apply(HandlerT &&H, std::unique_ptr<ErrorInfoBase> E) {
    assert(appliesTo(*E) && "Applying incorrect handler");
    std::unique_ptr<ErrT> SubE(static_cast<ErrT *>(E.release()));
    return H(std::move(SubE));
  }
};

/// Specialization for functions of the form 'void (std::unique_ptr<ErrT>)'.
template <typename ErrT>
class ErrorHandlerTraits<void (&)(std::unique_ptr<ErrT>)> {
public:
  static bool appliesTo(const ErrorInfoBase &E) {
    return E.template isA<ErrT>();
  }

  template <typename HandlerT>
  static Error apply(HandlerT &&H, std::unique_ptr<ErrorInfoBase> E) {
    assert(appliesTo(*E) && "Applying incorrect handler");
    std::unique_ptr<ErrT> SubE(static_cast<ErrT *>(E.release()));
    H(std::move(SubE));
    return Error::success();
  }
};

// Specialization for member functions of the form 'RetT (const ErrT&)'.
template <typename C, typename RetT, typename ErrT>
class ErrorHandlerTraits<RetT (C::*)(ErrT &)>
    : public ErrorHandlerTraits<RetT (&)(ErrT &)> {};

// Specialization for member functions of the form 'RetT (const ErrT&) const'.
template <typename C, typename RetT, typename ErrT>
class ErrorHandlerTraits<RetT (C::*)(ErrT &) const>
    : public ErrorHandlerTraits<RetT (&)(ErrT &)> {};

// Specialization for member functions of the form 'RetT (const ErrT&)'.
template <typename C, typename RetT, typename ErrT>
class ErrorHandlerTraits<RetT (C::*)(const ErrT &)>
    : public ErrorHandlerTraits<RetT (&)(ErrT &)> {};

// Specialization for member functions of the form 'RetT (const ErrT&) const'.
template <typename C, typename RetT, typename ErrT>
class ErrorHandlerTraits<RetT (C::*)(const ErrT &) const>
    : public ErrorHandlerTraits<RetT (&)(ErrT &)> {};

/// Specialization for member functions of the form
/// 'RetT (std::unique_ptr<ErrT>)'.
template <typename C, typename RetT, typename ErrT>
class ErrorHandlerTraits<RetT (C::*)(std::unique_ptr<ErrT>)>
    : public ErrorHandlerTraits<RetT (&)(std::unique_ptr<ErrT>)> {};

/// Specialization for member functions of the form
/// 'RetT (std::unique_ptr<ErrT>) const'.
template <typename C, typename RetT, typename ErrT>
class ErrorHandlerTraits<RetT (C::*)(std::unique_ptr<ErrT>) const>
    : public ErrorHandlerTraits<RetT (&)(std::unique_ptr<ErrT>)> {};

inline Error handleErrorImpl(std::unique_ptr<ErrorInfoBase> Payload) {
  return Error(std::move(Payload));
}

template <typename HandlerT, typename... HandlerTs>
Error handleErrorImpl(std::unique_ptr<ErrorInfoBase> Payload,
                      HandlerT &&Handler, HandlerTs &&... Handlers) {
  if (ErrorHandlerTraits<HandlerT>::appliesTo(*Payload))
    return ErrorHandlerTraits<HandlerT>::apply(std::forward<HandlerT>(Handler),
                                               std::move(Payload));
  return handleErrorImpl(std::move(Payload),
                         std::forward<HandlerTs>(Handlers)...);
}

/// Pass the ErrorInfo(s) contained in E to their respective handlers. Any
/// unhandled errors (or Errors returned by handlers) are re-concatenated and
/// returned.
/// Because this function returns an error, its result must also be checked
/// or returned. If you intend to handle all errors use handleAllErrors
/// (which returns void, and will abort() on unhandled errors) instead.
template <typename... HandlerTs>
Error handleErrors(Error E, HandlerTs &&... Hs) {
  if (!E)
    return Error::success();

  std::unique_ptr<ErrorInfoBase> Payload = E.takePayload();

  if (Payload->isA<ErrorList>()) {
    ErrorList &List = static_cast<ErrorList &>(*Payload);
    Error R;
    for (auto &P : List.Payloads)
      R = ErrorList::join(
          std::move(R),
          handleErrorImpl(std::move(P), std::forward<HandlerTs>(Hs)...));
    return R;
  }

  return handleErrorImpl(std::move(Payload), std::forward<HandlerTs>(Hs)...);
}

/// Behaves the same as handleErrors, except that by contract all errors
/// *must* be handled by the given handlers (i.e. there must be no remaining
/// errors after running the handlers, or llvm_unreachable is called).
template <typename... HandlerTs>
void handleAllErrors(Error E, HandlerTs &&... Handlers) {
  cantFail(handleErrors(std::move(E), std::forward<HandlerTs>(Handlers)...));
}

/// Check that E is a non-error, then drop it.
/// If E is an error, llvm_unreachable will be called.
inline void handleAllErrors(Error E) {
  cantFail(std::move(E));
}

/// Visit all the ErrorInfo(s) contained in E by passing them to the respective
/// handler, without consuming the error.
template <typename HandlerT> void visitErrors(const Error &E, HandlerT H) {
  const ErrorInfoBase *Payload = E.getPtr();
  if (!Payload)
    return;

  if (Payload->isA<ErrorList>()) {
    const ErrorList &List = static_cast<const ErrorList &>(*Payload);
    for (const auto &P : List.Payloads)
      H(*P);
    return;
  }

  return H(*Payload);
}

/// Handle any errors (if present) in an Expected<T>, then try a recovery path.
///
/// If the incoming value is a success value it is returned unmodified. If it
/// is a failure value then it the contained error is passed to handleErrors.
/// If handleErrors is able to handle the error then the RecoveryPath functor
/// is called to supply the final result. If handleErrors is not able to
/// handle all errors then the unhandled errors are returned.
///
/// This utility enables the follow pattern:
///
///   @code{.cpp}
///   enum FooStrategy { Aggressive, Conservative };
///   Expected<Foo> foo(FooStrategy S);
///
///   auto ResultOrErr =
///     handleExpected(
///       foo(Aggressive),
///       []() { return foo(Conservative); },
///       [](AggressiveStrategyError&) {
///         // Implicitly conusme this - we'll recover by using a conservative
///         // strategy.
///       });
///
///   @endcode
template <typename T, typename RecoveryFtor, typename... HandlerTs>
Expected<T> handleExpected(Expected<T> ValOrErr, RecoveryFtor &&RecoveryPath,
                           HandlerTs &&... Handlers) {
  if (ValOrErr)
    return ValOrErr;

  if (auto Err = handleErrors(ValOrErr.takeError(),
                              std::forward<HandlerTs>(Handlers)...))
    return std::move(Err);

  return RecoveryPath();
}

/// Log all errors (if any) in E to OS. If there are any errors, ErrorBanner
/// will be printed before the first one is logged. A newline will be printed
/// after each error.
///
/// This function is compatible with the helpers from Support/WithColor.h. You
/// can pass any of them as the OS. Please consider using them instead of
/// including 'error: ' in the ErrorBanner.
///
/// This is useful in the base level of your program to allow clean termination
/// (allowing clean deallocation of resources, etc.), while reporting error
/// information to the user.
void logAllUnhandledErrors(Error E, raw_ostream &OS, Twine ErrorBanner = {});

/// Write all error messages (if any) in E to a string. The newline character
/// is used to separate error messages.
std::string toString(Error E);

/// Like toString(), but does not consume the error. This can be used to print
/// a warning while retaining the original error object.
std::string toStringWithoutConsuming(const Error &E);

/// Consume a Error without doing anything. This method should be used
/// only where an error can be considered a reasonable and expected return
/// value.
///
/// Uses of this method are potentially indicative of design problems: If it's
/// legitimate to do nothing while processing an "error", the error-producer
/// might be more clearly refactored to return an std::optional<T>.
inline void consumeError(Error Err) {
  handleAllErrors(std::move(Err), [](const ErrorInfoBase &) {});
}

/// Convert an Expected to an Optional without doing anything. This method
/// should be used only where an error can be considered a reasonable and
/// expected return value.
///
/// Uses of this method are potentially indicative of problems: perhaps the
/// error should be propagated further, or the error-producer should just
/// return an Optional in the first place.
template <typename T> std::optional<T> expectedToOptional(Expected<T> &&E) {
  if (E)
    return std::move(*E);
  consumeError(E.takeError());
  return std::nullopt;
}

template <typename T> std::optional<T> expectedToStdOptional(Expected<T> &&E) {
  if (E)
    return std::move(*E);
  consumeError(E.takeError());
  return std::nullopt;
}

/// Helper for converting an Error to a bool.
///
/// This method returns true if Err is in an error state, or false if it is
/// in a success state.  Puts Err in a checked state in both cases (unlike
/// Error::operator bool(), which only does this for success states).
inline bool errorToBool(Error Err) {
  bool IsError = static_cast<bool>(Err);
  if (IsError)
    consumeError(std::move(Err));
  return IsError;
}

/// Helper for Errors used as out-parameters.
///
/// This helper is for use with the Error-as-out-parameter idiom, where an error
/// is passed to a function or method by reference, rather than being returned.
/// In such cases it is helpful to set the checked bit on entry to the function
/// so that the error can be written to (unchecked Errors abort on assignment)
/// and clear the checked bit on exit so that clients cannot accidentally forget
/// to check the result. This helper performs these actions automatically using
/// RAII:
///
///   @code{.cpp}
///   Result foo(Error &Err) {
///     ErrorAsOutParameter ErrAsOutParam(&Err); // 'Checked' flag set
///     // <body of foo>
///     // <- 'Checked' flag auto-cleared when ErrAsOutParam is destructed.
///   }
///   @endcode
///
/// ErrorAsOutParameter takes an Error* rather than Error& so that it can be
/// used with optional Errors (Error pointers that are allowed to be null). If
/// ErrorAsOutParameter took an Error reference, an instance would have to be
/// created inside every condition that verified that Error was non-null. By
/// taking an Error pointer we can just create one instance at the top of the
/// function.
class ErrorAsOutParameter {
public:
  ErrorAsOutParameter(Error *Err) : Err(Err) {
    // Raise the checked bit if Err is success.
    if (Err)
      (void)!!*Err;
  }

  ~ErrorAsOutParameter() {
    // Clear the checked bit.
    if (Err && !*Err)
      *Err = Error::success();
  }

private:
  Error *Err;
};

/// Helper for Expected<T>s used as out-parameters.
///
/// See ErrorAsOutParameter.
template <typename T>
class ExpectedAsOutParameter {
public:
  ExpectedAsOutParameter(Expected<T> *ValOrErr)
    : ValOrErr(ValOrErr) {
    if (ValOrErr)
      (void)!!*ValOrErr;
  }

  ~ExpectedAsOutParameter() {
    if (ValOrErr)
      ValOrErr->setUnchecked();
  }

private:
  Expected<T> *ValOrErr;
};

/// This class wraps a std::error_code in a Error.
///
/// This is useful if you're writing an interface that returns a Error
/// (or Expected) and you want to call code that still returns
/// std::error_codes.
class ECError : public ErrorInfo<ECError> {
  friend Error errorCodeToError(std::error_code);

  void anchor() override;

public:
  void setErrorCode(std::error_code EC) { this->EC = EC; }
  std::error_code convertToErrorCode() const override { return EC; }
  void log(raw_ostream &OS) const override { OS << EC.message(); }

  // Used by ErrorInfo::classID.
  static char ID;

protected:
  ECError() = default;
  ECError(std::error_code EC) : EC(EC) {}

  std::error_code EC;
};

/// The value returned by this function can be returned from convertToErrorCode
/// for Error values where no sensible translation to std::error_code exists.
/// It should only be used in this situation, and should never be used where a
/// sensible conversion to std::error_code is available, as attempts to convert
/// to/from this error will result in a fatal error. (i.e. it is a programmatic
/// error to try to convert such a value).
std::error_code inconvertibleErrorCode();

/// Helper for converting an std::error_code to a Error.
Error errorCodeToError(std::error_code EC);

/// Helper for converting an ECError to a std::error_code.
///
/// This method requires that Err be Error() or an ECError, otherwise it
/// will trigger a call to abort().
std::error_code errorToErrorCode(Error Err);

/// Helper to get errno as an std::error_code.
///
/// errno should always be represented using the generic category as that's what
/// both libc++ and libstdc++ do. On POSIX systems you can also represent them
/// using the system category, however this makes them compare differently for
/// values outside of those used by `std::errc` if one is generic and the other
/// is system.
///
/// See the libc++ and libstdc++ implementations of `default_error_condition` on
/// the system category for more details on what the difference is.
inline std::error_code errnoAsErrorCode() {
  return std::error_code(errno, std::generic_category());
}

/// Convert an ErrorOr<T> to an Expected<T>.
template <typename T> Expected<T> errorOrToExpected(ErrorOr<T> &&EO) {
  if (auto EC = EO.getError())
    return errorCodeToError(EC);
  return std::move(*EO);
}

/// Convert an Expected<T> to an ErrorOr<T>.
template <typename T> ErrorOr<T> expectedToErrorOr(Expected<T> &&E) {
  if (auto Err = E.takeError())
    return errorToErrorCode(std::move(Err));
  return std::move(*E);
}

/// This class wraps a string in an Error.
///
/// StringError is useful in cases where the client is not expected to be able
/// to consume the specific error message programmatically (for example, if the
/// error message is to be presented to the user).
///
/// StringError can also be used when additional information is to be printed
/// along with a error_code message. Depending on the constructor called, this
/// class can either display:
///    1. the error_code message (ECError behavior)
///    2. a string
///    3. the error_code message and a string
///
/// These behaviors are useful when subtyping is required; for example, when a
/// specific library needs an explicit error type. In the example below,
/// PDBError is derived from StringError:
///
///   @code{.cpp}
///   Expected<int> foo() {
///      return llvm::make_error<PDBError>(pdb_error_code::dia_failed_loading,
///                                        "Additional information");
///   }
///   @endcode
///
class StringError : public ErrorInfo<StringError> {
public:
  static char ID;

  StringError(std::string &&S, std::error_code EC, bool PrintMsgOnly);
  /// Prints EC + S and converts to EC.
  StringError(std::error_code EC, const Twine &S = Twine());
  /// Prints S and converts to EC.
  StringError(const Twine &S, std::error_code EC);

  void log(raw_ostream &OS) const override;
  std::error_code convertToErrorCode() const override;

  const std::string &getMessage() const { return Msg; }

private:
  std::string Msg;
  std::error_code EC;
  const bool PrintMsgOnly = false;
};

/// Create formatted StringError object.
template <typename... Ts>
inline Error createStringError(std::error_code EC, char const *Fmt,
                               const Ts &... Vals) {
  std::string Buffer;
  raw_string_ostream(Buffer) << format(Fmt, Vals...);
  return make_error<StringError>(Buffer, EC);
}

Error createStringError(std::string &&Msg, std::error_code EC);

inline Error createStringError(std::error_code EC, const char *S) {
  return createStringError(std::string(S), EC);
}

inline Error createStringError(std::error_code EC, const Twine &S) {
  return createStringError(S.str(), EC);
}

/// Create a StringError with an inconvertible error code.
inline Error createStringError(const Twine &S) {
  return createStringError(llvm::inconvertibleErrorCode(), S);
}

template <typename... Ts>
inline Error createStringError(char const *Fmt, const Ts &...Vals) {
  return createStringError(llvm::inconvertibleErrorCode(), Fmt, Vals...);
}

template <typename... Ts>
inline Error createStringError(std::errc EC, char const *Fmt,
                               const Ts &... Vals) {
  return createStringError(std::make_error_code(EC), Fmt, Vals...);
}

/// This class wraps a filename and another Error.
///
/// In some cases, an error needs to live along a 'source' name, in order to
/// show more detailed information to the user.
class FileError final : public ErrorInfo<FileError> {

  friend Error createFileError(const Twine &, Error);
  friend Error createFileError(const Twine &, size_t, Error);

public:
  void log(raw_ostream &OS) const override {
    assert(Err && "Trying to log after takeError().");
    OS << "'" << FileName << "': ";
    if (Line)
      OS << "line " << *Line << ": ";
    Err->log(OS);
  }

  std::string messageWithoutFileInfo() const {
    std::string Msg;
    raw_string_ostream OS(Msg);
    Err->log(OS);
    return Msg;
  }

  StringRef getFileName() const { return FileName; }

  Error takeError() { return Error(std::move(Err)); }

  std::error_code convertToErrorCode() const override;

  // Used by ErrorInfo::classID.
  static char ID;

private:
  FileError(const Twine &F, std::optional<size_t> LineNum,
            std::unique_ptr<ErrorInfoBase> E) {
    assert(E && "Cannot create FileError from Error success value.");
    FileName = F.str();
    Err = std::move(E);
    Line = std::move(LineNum);
  }

  static Error build(const Twine &F, std::optional<size_t> Line, Error E) {
    std::unique_ptr<ErrorInfoBase> Payload;
    handleAllErrors(std::move(E),
                    [&](std::unique_ptr<ErrorInfoBase> EIB) -> Error {
                      Payload = std::move(EIB);
                      return Error::success();
                    });
    return Error(
        std::unique_ptr<FileError>(new FileError(F, Line, std::move(Payload))));
  }

  std::string FileName;
  std::optional<size_t> Line;
  std::unique_ptr<ErrorInfoBase> Err;
};

/// Concatenate a source file path and/or name with an Error. The resulting
/// Error is unchecked.
inline Error createFileError(const Twine &F, Error E) {
  return FileError::build(F, std::optional<size_t>(), std::move(E));
}

/// Concatenate a source file path and/or name with line number and an Error.
/// The resulting Error is unchecked.
inline Error createFileError(const Twine &F, size_t Line, Error E) {
  return FileError::build(F, std::optional<size_t>(Line), std::move(E));
}

/// Concatenate a source file path and/or name with a std::error_code 
/// to form an Error object.
inline Error createFileError(const Twine &F, std::error_code EC) {
  return createFileError(F, errorCodeToError(EC));
}

/// Concatenate a source file path and/or name with line number and
/// std::error_code to form an Error object.
inline Error createFileError(const Twine &F, size_t Line, std::error_code EC) {
  return createFileError(F, Line, errorCodeToError(EC));
}

Error createFileError(const Twine &F, ErrorSuccess) = delete;

/// Helper for check-and-exit error handling.
///
/// For tool use only. NOT FOR USE IN LIBRARY CODE.
///
class ExitOnError {
public:
  /// Create an error on exit helper.
  ExitOnError(std::string Banner = "", int DefaultErrorExitCode = 1)
      : Banner(std::move(Banner)),
        GetExitCode([=](const Error &) { return DefaultErrorExitCode; }) {}

  /// Set the banner string for any errors caught by operator().
  void setBanner(std::string Banner) { this->Banner = std::move(Banner); }

  /// Set the exit-code mapper function.
  void setExitCodeMapper(std::function<int(const Error &)> GetExitCode) {
    this->GetExitCode = std::move(GetExitCode);
  }

  /// Check Err. If it's in a failure state log the error(s) and exit.
  void operator()(Error Err) const { checkError(std::move(Err)); }

  /// Check E. If it's in a success state then return the contained value. If
  /// it's in a failure state log the error(s) and exit.
  template <typename T> T operator()(Expected<T> &&E) const {
    checkError(E.takeError());
    return std::move(*E);
  }

  /// Check E. If it's in a success state then return the contained reference. If
  /// it's in a failure state log the error(s) and exit.
  template <typename T> T& operator()(Expected<T&> &&E) const {
    checkError(E.takeError());
    return *E;
  }

private:
  void checkError(Error Err) const {
    if (Err) {
      int ExitCode = GetExitCode(Err);
      logAllUnhandledErrors(std::move(Err), errs(), Banner);
      exit(ExitCode);
    }
  }

  std::string Banner;
  std::function<int(const Error &)> GetExitCode;
};

/// Conversion from Error to LLVMErrorRef for C error bindings.
inline LLVMErrorRef wrap(Error Err) {
  return reinterpret_cast<LLVMErrorRef>(Err.takePayload().release());
}

/// Conversion from LLVMErrorRef to Error for C error bindings.
inline Error unwrap(LLVMErrorRef ErrRef) {
  return Error(std::unique_ptr<ErrorInfoBase>(
      reinterpret_cast<ErrorInfoBase *>(ErrRef)));
}

} // end namespace llvm

#endif // LLVM_SUPPORT_ERROR_H
