//===- FormatVariadicDetails.h - Helpers for FormatVariadic.h ----*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_FORMATVARIADICDETAILS_H
#define LLVM_SUPPORT_FORMATVARIADICDETAILS_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/raw_ostream.h"

#include <type_traits>

namespace llvm {
template <typename T, typename Enable = void> struct format_provider {};
class Error;

namespace support {
namespace detail {
class format_adapter {
  virtual void anchor();

protected:
  virtual ~format_adapter() = default;

public:
  virtual void format(raw_ostream &S, StringRef Options) = 0;
};

template <typename T> class provider_format_adapter : public format_adapter {
  T Item;

public:
  explicit provider_format_adapter(T &&Item) : Item(std::forward<T>(Item)) {}

  void format(llvm::raw_ostream &S, StringRef Options) override {
    format_provider<std::decay_t<T>>::format(Item, S, Options);
  }
};

template <typename T>
class stream_operator_format_adapter : public format_adapter {
  T Item;

public:
  explicit stream_operator_format_adapter(T &&Item)
      : Item(std::forward<T>(Item)) {}

  void format(llvm::raw_ostream &S, StringRef) override { S << Item; }
};

template <typename T> class missing_format_adapter;

// Test if format_provider<T> is defined on T and contains a member function
// with the signature:
//   static void format(const T&, raw_stream &, StringRef);
//
template <class T> class has_FormatProvider {
public:
  using Decayed = std::decay_t<T>;
  typedef void (*Signature_format)(const Decayed &, llvm::raw_ostream &,
                                   StringRef);

  template <typename U>
  static char test(SameType<Signature_format, &U::format> *);

  template <typename U> static double test(...);

  static bool const value =
      (sizeof(test<llvm::format_provider<Decayed>>(nullptr)) == 1);
};

// Test if raw_ostream& << T -> raw_ostream& is findable via ADL.
template <class T> class has_StreamOperator {
public:
  using ConstRefT = const std::decay_t<T> &;

  template <typename U>
  static char test(std::enable_if_t<
                   std::is_same_v<decltype(std::declval<llvm::raw_ostream &>()
                                           << std::declval<U>()),
                                  llvm::raw_ostream &>,
                   int *>);

  template <typename U> static double test(...);

  static bool const value = (sizeof(test<ConstRefT>(nullptr)) == 1);
};

// Simple template that decides whether a type T should use the member-function
// based format() invocation.
template <typename T>
struct uses_format_member
    : public std::integral_constant<
          bool, std::is_base_of_v<format_adapter, std::remove_reference_t<T>>> {
};

// Simple template that decides whether a type T should use the format_provider
// based format() invocation.  The member function takes priority, so this test
// will only be true if there is not ALSO a format member.
template <typename T>
struct uses_format_provider
    : public std::integral_constant<
          bool, !uses_format_member<T>::value && has_FormatProvider<T>::value> {
};

// Simple template that decides whether a type T should use the operator<<
// based format() invocation.  This takes last priority.
template <typename T>
struct uses_stream_operator
    : public std::integral_constant<bool, !uses_format_member<T>::value &&
                                              !uses_format_provider<T>::value &&
                                              has_StreamOperator<T>::value> {};

// Simple template that decides whether a type T has neither a member-function
// nor format_provider based implementation that it can use.  Mostly used so
// that the compiler spits out a nice diagnostic when a type with no format
// implementation can be located.
template <typename T>
struct uses_missing_provider
    : public std::integral_constant<bool, !uses_format_member<T>::value &&
                                              !uses_format_provider<T>::value &&
                                              !uses_stream_operator<T>::value> {
};

template <typename T>
std::enable_if_t<uses_format_member<T>::value, T>
build_format_adapter(T &&Item) {
  return std::forward<T>(Item);
}

template <typename T>
std::enable_if_t<uses_format_provider<T>::value, provider_format_adapter<T>>
build_format_adapter(T &&Item) {
  return provider_format_adapter<T>(std::forward<T>(Item));
}

template <typename T>
std::enable_if_t<uses_stream_operator<T>::value,
                 stream_operator_format_adapter<T>>
build_format_adapter(T &&Item) {
  // If the caller passed an Error by value, then stream_operator_format_adapter
  // would be responsible for consuming it.
  // Make the caller opt into this by calling fmt_consume().
  static_assert(
      !std::is_same_v<llvm::Error, std::remove_cv_t<T>>,
      "llvm::Error-by-value must be wrapped in fmt_consume() for formatv");
  return stream_operator_format_adapter<T>(std::forward<T>(Item));
}

template <typename T>
std::enable_if_t<uses_missing_provider<T>::value, missing_format_adapter<T>>
build_format_adapter(T &&) {
  return missing_format_adapter<T>();
}
} // namespace detail
} // namespace support
} // namespace llvm

#endif
