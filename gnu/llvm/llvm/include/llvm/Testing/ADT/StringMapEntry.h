//===- llvm/Testing/ADT/StringMapEntry.h ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TESTING_ADT_STRINGMAPENTRY_H_
#define LLVM_TESTING_ADT_STRINGMAPENTRY_H_

#include "llvm/ADT/StringMapEntry.h"
#include "gmock/gmock.h"
#include <ostream>
#include <type_traits>

namespace llvm {
namespace detail {

template <typename T, typename = std::void_t<>>
struct CanOutputToOStream : std::false_type {};

template <typename T>
struct CanOutputToOStream<T, std::void_t<decltype(std::declval<std::ostream &>()
                                                  << std::declval<T>())>>
    : std::true_type {};

} // namespace detail

/// Support for printing to std::ostream, for use with e.g. producing more
/// useful error messages with Google Test.
template <typename T>
std::ostream &operator<<(std::ostream &OS, const StringMapEntry<T> &E) {
  OS << "{\"" << E.getKey().data() << "\": ";
  if constexpr (detail::CanOutputToOStream<decltype(E.getValue())>::value) {
    OS << E.getValue();
  } else {
    OS << "non-printable value";
  }
  return OS << "}";
}

namespace detail {

template <typename StringMapEntryT>
class StringMapEntryMatcherImpl
    : public testing::MatcherInterface<StringMapEntryT> {
public:
  using ValueT = typename std::remove_reference_t<StringMapEntryT>::ValueType;

  template <typename KeyMatcherT, typename ValueMatcherT>
  StringMapEntryMatcherImpl(KeyMatcherT KeyMatcherArg,
                            ValueMatcherT ValueMatcherArg)
      : KeyMatcher(
            testing::SafeMatcherCast<const std::string &>(KeyMatcherArg)),
        ValueMatcher(
            testing::SafeMatcherCast<const ValueT &>(ValueMatcherArg)) {}

  void DescribeTo(std::ostream *OS) const override {
    *OS << "has a string key that ";
    KeyMatcher.DescribeTo(OS);
    *OS << ", and has a value that ";
    ValueMatcher.DescribeTo(OS);
  }

  void DescribeNegationTo(std::ostream *OS) const override {
    *OS << "has a string key that ";
    KeyMatcher.DescribeNegationTo(OS);
    *OS << ", or has a value that ";
    ValueMatcher.DescribeNegationTo(OS);
  }

  bool
  MatchAndExplain(StringMapEntryT Entry,
                  testing::MatchResultListener *ResultListener) const override {
    testing::StringMatchResultListener KeyListener;
    if (!KeyMatcher.MatchAndExplain(Entry.getKey().data(), &KeyListener)) {
      *ResultListener << ("which has a string key " +
                          (KeyListener.str().empty() ? "that doesn't match"
                                                     : KeyListener.str()));
      return false;
    }
    testing::StringMatchResultListener ValueListener;
    if (!ValueMatcher.MatchAndExplain(Entry.getValue(), &ValueListener)) {
      *ResultListener << ("which has a value " + (ValueListener.str().empty()
                                                      ? "that doesn't match"
                                                      : ValueListener.str()));
      return false;
    }
    *ResultListener << "which is a match";
    return true;
  }

private:
  const testing::Matcher<const std::string &> KeyMatcher;
  const testing::Matcher<const ValueT &> ValueMatcher;
};

template <typename KeyMatcherT, typename ValueMatcherT>
class StringMapEntryMatcher {
public:
  StringMapEntryMatcher(KeyMatcherT KMArg, ValueMatcherT VMArg)
      : KM(std::move(KMArg)), VM(std::move(VMArg)) {}

  template <typename StringMapEntryT>
  operator testing::Matcher<StringMapEntryT>() const { // NOLINT
    return testing::Matcher<StringMapEntryT>(
        new StringMapEntryMatcherImpl<const StringMapEntryT &>(KM, VM));
  }

private:
  const KeyMatcherT KM;
  const ValueMatcherT VM;
};

} // namespace detail

/// Returns a gMock matcher that matches a `StringMapEntry` whose string key
/// matches `KeyMatcher`, and whose value matches `ValueMatcher`.
template <typename KeyMatcherT, typename ValueMatcherT>
detail::StringMapEntryMatcher<KeyMatcherT, ValueMatcherT>
IsStringMapEntry(KeyMatcherT KM, ValueMatcherT VM) {
  return detail::StringMapEntryMatcher<KeyMatcherT, ValueMatcherT>(
      std::move(KM), std::move(VM));
}

} // namespace llvm

#endif
