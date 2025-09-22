//===-- Instrumentation.h ---------------------------------------*- C++ -*-===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_INSTRUMENTATION_H
#define LLDB_UTILITY_INSTRUMENTATION_H

#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Log.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ErrorHandling.h"

#include <map>
#include <thread>
#include <type_traits>

namespace lldb_private {
namespace instrumentation {

template <typename T, std::enable_if_t<std::is_fundamental<T>::value, int> = 0>
inline void stringify_append(llvm::raw_string_ostream &ss, const T &t) {
  ss << t;
}

template <typename T, std::enable_if_t<!std::is_fundamental<T>::value, int> = 0>
inline void stringify_append(llvm::raw_string_ostream &ss, const T &t) {
  ss << &t;
}

template <typename T>
inline void stringify_append(llvm::raw_string_ostream &ss, T *t) {
  ss << reinterpret_cast<void *>(t);
}

template <typename T>
inline void stringify_append(llvm::raw_string_ostream &ss, const T *t) {
  ss << reinterpret_cast<const void *>(t);
}

template <>
inline void stringify_append<char>(llvm::raw_string_ostream &ss,
                                   const char *t) {
  ss << '\"' << t << '\"';
}

template <>
inline void stringify_append<std::nullptr_t>(llvm::raw_string_ostream &ss,
                                             const std::nullptr_t &t) {
  ss << "\"nullptr\"";
}

template <typename Head>
inline void stringify_helper(llvm::raw_string_ostream &ss, const Head &head) {
  stringify_append(ss, head);
}

template <typename Head, typename... Tail>
inline void stringify_helper(llvm::raw_string_ostream &ss, const Head &head,
                             const Tail &...tail) {
  stringify_append(ss, head);
  ss << ", ";
  stringify_helper(ss, tail...);
}

template <typename... Ts> inline std::string stringify_args(const Ts &...ts) {
  std::string buffer;
  llvm::raw_string_ostream ss(buffer);
  stringify_helper(ss, ts...);
  return ss.str();
}

/// RAII object for instrumenting LLDB API functions.
class Instrumenter {
public:
  Instrumenter(llvm::StringRef pretty_func, std::string &&pretty_args = {});
  ~Instrumenter();

private:
  void UpdateBoundary();

  llvm::StringRef m_pretty_func;

  /// Whether this function call was the one crossing the API boundary.
  bool m_local_boundary = false;
};
} // namespace instrumentation
} // namespace lldb_private

#define LLDB_INSTRUMENT()                                                      \
  lldb_private::instrumentation::Instrumenter _instr(LLVM_PRETTY_FUNCTION);

#define LLDB_INSTRUMENT_VA(...)                                                \
  lldb_private::instrumentation::Instrumenter _instr(                          \
      LLVM_PRETTY_FUNCTION,                                                    \
      lldb_private::instrumentation::stringify_args(__VA_ARGS__));

#endif // LLDB_UTILITY_INSTRUMENTATION_H
