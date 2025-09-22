//===- llvm/Support/ErrorOr.h - Error Smart Pointer -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
///
/// Provides ErrorOr<T> smart pointer.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ERROROR_H
#define LLVM_SUPPORT_ERROROR_H

#include "llvm/Support/AlignOf.h"
#include <cassert>
#include <system_error>
#include <type_traits>
#include <utility>

namespace llvm {

/// Represents either an error or a value T.
///
/// ErrorOr<T> is a pointer-like class that represents the result of an
/// operation. The result is either an error, or a value of type T. This is
/// designed to emulate the usage of returning a pointer where nullptr indicates
/// failure. However instead of just knowing that the operation failed, we also
/// have an error_code and optional user data that describes why it failed.
///
/// It is used like the following.
/// \code
///   ErrorOr<Buffer> getBuffer();
///
///   auto buffer = getBuffer();
///   if (error_code ec = buffer.getError())
///     return ec;
///   buffer->write("adena");
/// \endcode
///
///
/// Implicit conversion to bool returns true if there is a usable value. The
/// unary * and -> operators provide pointer like access to the value. Accessing
/// the value when there is an error has undefined behavior.
///
/// When T is a reference type the behavior is slightly different. The reference
/// is held in a std::reference_wrapper<std::remove_reference<T>::type>, and
/// there is special handling to make operator -> work as if T was not a
/// reference.
///
/// T cannot be a rvalue reference.
template<class T>
class ErrorOr {
  template <class OtherT> friend class ErrorOr;

  static constexpr bool isRef = std::is_reference_v<T>;

  using wrap = std::reference_wrapper<std::remove_reference_t<T>>;

public:
  using storage_type = std::conditional_t<isRef, wrap, T>;

private:
  using reference = std::remove_reference_t<T> &;
  using const_reference = const std::remove_reference_t<T> &;
  using pointer = std::remove_reference_t<T> *;
  using const_pointer = const std::remove_reference_t<T> *;

public:
  template <class E>
  ErrorOr(E ErrorCode,
          std::enable_if_t<std::is_error_code_enum<E>::value ||
                               std::is_error_condition_enum<E>::value,
                           void *> = nullptr)
      : HasError(true) {
    new (getErrorStorage()) std::error_code(make_error_code(ErrorCode));
  }

  ErrorOr(std::error_code EC) : HasError(true) {
    new (getErrorStorage()) std::error_code(EC);
  }

  template <class OtherT>
  ErrorOr(OtherT &&Val,
          std::enable_if_t<std::is_convertible_v<OtherT, T>> * = nullptr)
      : HasError(false) {
    new (getStorage()) storage_type(std::forward<OtherT>(Val));
  }

  ErrorOr(const ErrorOr &Other) {
    copyConstruct(Other);
  }

  template <class OtherT>
  ErrorOr(const ErrorOr<OtherT> &Other,
          std::enable_if_t<std::is_convertible_v<OtherT, T>> * = nullptr) {
    copyConstruct(Other);
  }

  template <class OtherT>
  explicit ErrorOr(
      const ErrorOr<OtherT> &Other,
      std::enable_if_t<!std::is_convertible_v<OtherT, const T &>> * = nullptr) {
    copyConstruct(Other);
  }

  ErrorOr(ErrorOr &&Other) {
    moveConstruct(std::move(Other));
  }

  template <class OtherT>
  ErrorOr(ErrorOr<OtherT> &&Other,
          std::enable_if_t<std::is_convertible_v<OtherT, T>> * = nullptr) {
    moveConstruct(std::move(Other));
  }

  // This might eventually need SFINAE but it's more complex than is_convertible
  // & I'm too lazy to write it right now.
  template <class OtherT>
  explicit ErrorOr(
      ErrorOr<OtherT> &&Other,
      std::enable_if_t<!std::is_convertible_v<OtherT, T>> * = nullptr) {
    moveConstruct(std::move(Other));
  }

  ErrorOr &operator=(const ErrorOr &Other) {
    copyAssign(Other);
    return *this;
  }

  ErrorOr &operator=(ErrorOr &&Other) {
    moveAssign(std::move(Other));
    return *this;
  }

  ~ErrorOr() {
    if (!HasError)
      getStorage()->~storage_type();
  }

  /// Return false if there is an error.
  explicit operator bool() const {
    return !HasError;
  }

  reference get() { return *getStorage(); }
  const_reference get() const { return const_cast<ErrorOr<T> *>(this)->get(); }

  std::error_code getError() const {
    return HasError ? *getErrorStorage() : std::error_code();
  }

  pointer operator ->() {
    return toPointer(getStorage());
  }

  const_pointer operator->() const { return toPointer(getStorage()); }

  reference operator *() {
    return *getStorage();
  }

  const_reference operator*() const { return *getStorage(); }

private:
  template <class OtherT>
  void copyConstruct(const ErrorOr<OtherT> &Other) {
    if (!Other.HasError) {
      // Get the other value.
      HasError = false;
      new (getStorage()) storage_type(*Other.getStorage());
    } else {
      // Get other's error.
      HasError = true;
      new (getErrorStorage()) std::error_code(Other.getError());
    }
  }

  template <class T1>
  static bool compareThisIfSameType(const T1 &a, const T1 &b) {
    return &a == &b;
  }

  template <class T1, class T2>
  static bool compareThisIfSameType(const T1 &a, const T2 &b) {
    return false;
  }

  template <class OtherT>
  void copyAssign(const ErrorOr<OtherT> &Other) {
    if (compareThisIfSameType(*this, Other))
      return;

    this->~ErrorOr();
    new (this) ErrorOr(Other);
  }

  template <class OtherT>
  void moveConstruct(ErrorOr<OtherT> &&Other) {
    if (!Other.HasError) {
      // Get the other value.
      HasError = false;
      new (getStorage()) storage_type(std::move(*Other.getStorage()));
    } else {
      // Get other's error.
      HasError = true;
      new (getErrorStorage()) std::error_code(Other.getError());
    }
  }

  template <class OtherT>
  void moveAssign(ErrorOr<OtherT> &&Other) {
    if (compareThisIfSameType(*this, Other))
      return;

    this->~ErrorOr();
    new (this) ErrorOr(std::move(Other));
  }

  pointer toPointer(pointer Val) {
    return Val;
  }

  const_pointer toPointer(const_pointer Val) const { return Val; }

  pointer toPointer(wrap *Val) {
    return &Val->get();
  }

  const_pointer toPointer(const wrap *Val) const { return &Val->get(); }

  storage_type *getStorage() {
    assert(!HasError && "Cannot get value when an error exists!");
    return reinterpret_cast<storage_type *>(&TStorage);
  }

  const storage_type *getStorage() const {
    assert(!HasError && "Cannot get value when an error exists!");
    return reinterpret_cast<const storage_type *>(&TStorage);
  }

  std::error_code *getErrorStorage() {
    assert(HasError && "Cannot get error when a value exists!");
    return reinterpret_cast<std::error_code *>(&ErrorStorage);
  }

  const std::error_code *getErrorStorage() const {
    return const_cast<ErrorOr<T> *>(this)->getErrorStorage();
  }

  union {
    AlignedCharArrayUnion<storage_type> TStorage;
    AlignedCharArrayUnion<std::error_code> ErrorStorage;
  };
  bool HasError : 1;
};

template <class T, class E>
std::enable_if_t<std::is_error_code_enum<E>::value ||
                     std::is_error_condition_enum<E>::value,
                 bool>
operator==(const ErrorOr<T> &Err, E Code) {
  return Err.getError() == Code;
}

} // end namespace llvm

#endif // LLVM_SUPPORT_ERROROR_H
