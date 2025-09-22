//===- LogicalResult.h - Utilities for handling success/failure -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_LOGICALRESULT_H
#define LLVM_SUPPORT_LOGICALRESULT_H

#include <cassert>
#include <optional>

namespace llvm {
/// This class represents an efficient way to signal success or failure. It
/// should be preferred over the use of `bool` when appropriate, as it avoids
/// all of the ambiguity that arises in interpreting a boolean result. This
/// class is marked as NODISCARD to ensure that the result is processed. Users
/// may explicitly discard a result by using `(void)`, e.g.
/// `(void)functionThatReturnsALogicalResult();`. Given the intended nature of
/// this class, it generally shouldn't be used as the result of functions that
/// very frequently have the result ignored. This class is intended to be used
/// in conjunction with the utility functions below.
struct [[nodiscard]] LogicalResult {
public:
  /// If isSuccess is true a `success` result is generated, otherwise a
  /// 'failure' result is generated.
  static LogicalResult success(bool IsSuccess = true) {
    return LogicalResult(IsSuccess);
  }

  /// If isFailure is true a `failure` result is generated, otherwise a
  /// 'success' result is generated.
  static LogicalResult failure(bool IsFailure = true) {
    return LogicalResult(!IsFailure);
  }

  /// Returns true if the provided LogicalResult corresponds to a success value.
  constexpr bool succeeded() const { return IsSuccess; }

  /// Returns true if the provided LogicalResult corresponds to a failure value.
  constexpr bool failed() const { return !IsSuccess; }

private:
  LogicalResult(bool IsSuccess) : IsSuccess(IsSuccess) {}

  /// Boolean indicating if this is a success result, if false this is a
  /// failure result.
  bool IsSuccess;
};

/// Utility function to generate a LogicalResult. If isSuccess is true a
/// `success` result is generated, otherwise a 'failure' result is generated.
inline LogicalResult success(bool IsSuccess = true) {
  return LogicalResult::success(IsSuccess);
}

/// Utility function to generate a LogicalResult. If isFailure is true a
/// `failure` result is generated, otherwise a 'success' result is generated.
inline LogicalResult failure(bool IsFailure = true) {
  return LogicalResult::failure(IsFailure);
}

/// Utility function that returns true if the provided LogicalResult corresponds
/// to a success value.
inline bool succeeded(LogicalResult Result) { return Result.succeeded(); }

/// Utility function that returns true if the provided LogicalResult corresponds
/// to a failure value.
inline bool failed(LogicalResult Result) { return Result.failed(); }

/// This class provides support for representing a failure result, or a valid
/// value of type `T`. This allows for integrating with LogicalResult, while
/// also providing a value on the success path.
template <typename T> class [[nodiscard]] FailureOr : public std::optional<T> {
public:
  /// Allow constructing from a LogicalResult. The result *must* be a failure.
  /// Success results should use a proper instance of type `T`.
  FailureOr(LogicalResult Result) {
    assert(failed(Result) &&
           "success should be constructed with an instance of 'T'");
  }
  FailureOr() : FailureOr(failure()) {}
  FailureOr(T &&Y) : std::optional<T>(std::forward<T>(Y)) {}
  FailureOr(const T &Y) : std::optional<T>(Y) {}
  template <typename U,
            std::enable_if_t<std::is_constructible<T, U>::value> * = nullptr>
  FailureOr(const FailureOr<U> &Other)
      : std::optional<T>(failed(Other) ? std::optional<T>()
                                       : std::optional<T>(*Other)) {}

  operator LogicalResult() const { return success(has_value()); }

private:
  /// Hide the bool conversion as it easily creates confusion.
  using std::optional<T>::operator bool;
  using std::optional<T>::has_value;
};

/// Wrap a value on the success path in a FailureOr of the same value type.
template <typename T,
          typename = std::enable_if_t<!std::is_convertible_v<T, bool>>>
inline auto success(T &&Y) {
  return FailureOr<std::decay_t<T>>(std::forward<T>(Y));
}

/// This class represents success/failure for parsing-like operations that find
/// it important to chain together failable operations with `||`.  This is an
/// extended version of `LogicalResult` that allows for explicit conversion to
/// bool.
///
/// This class should not be used for general error handling cases - we prefer
/// to keep the logic explicit with the `succeeded`/`failed` predicates.
/// However, traditional monadic-style parsing logic can sometimes get
/// swallowed up in boilerplate without this, so we provide this for narrow
/// cases where it is important.
///
class [[nodiscard]] ParseResult : public LogicalResult {
public:
  ParseResult(LogicalResult Result = success()) : LogicalResult(Result) {}

  /// Failure is true in a boolean context.
  constexpr explicit operator bool() const { return failed(); }
};
} // namespace llvm

#endif // LLVM_SUPPORT_LOGICALRESULT_H
