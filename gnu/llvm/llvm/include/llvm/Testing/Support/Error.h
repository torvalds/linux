//===- llvm/Testing/Support/Error.h ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TESTING_SUPPORT_ERROR_H
#define LLVM_TESTING_SUPPORT_ERROR_H

#include "llvm/Support/Error.h"
#include "llvm/Testing/Support/SupportHelpers.h"

#include "gmock/gmock.h"
#include <ostream>

namespace llvm {
namespace detail {
ErrorHolder TakeError(Error Err);

template <typename T> ExpectedHolder<T> TakeExpected(Expected<T> &Exp) {
  return {TakeError(Exp.takeError()), Exp};
}

template <typename T> ExpectedHolder<T> TakeExpected(Expected<T> &&Exp) {
  return TakeExpected(Exp);
}

template <typename T>
class ValueMatchesMono
    : public testing::MatcherInterface<const ExpectedHolder<T> &> {
public:
  explicit ValueMatchesMono(const testing::Matcher<T> &Matcher)
      : Matcher(Matcher) {}

  bool MatchAndExplain(const ExpectedHolder<T> &Holder,
                       testing::MatchResultListener *listener) const override {
    if (!Holder.Success())
      return false;

    bool result = Matcher.MatchAndExplain(*Holder.Exp, listener);

    if (result || !listener->IsInterested())
      return result;
    *listener << "(";
    Matcher.DescribeNegationTo(listener->stream());
    *listener << ")";
    return result;
  }

  void DescribeTo(std::ostream *OS) const override {
    *OS << "succeeded with value (";
    Matcher.DescribeTo(OS);
    *OS << ")";
  }

  void DescribeNegationTo(std::ostream *OS) const override {
    *OS << "did not succeed or value (";
    Matcher.DescribeNegationTo(OS);
    *OS << ")";
  }

private:
  testing::Matcher<T> Matcher;
};

template<typename M>
class ValueMatchesPoly {
public:
  explicit ValueMatchesPoly(const M &Matcher) : Matcher(Matcher) {}

  template <typename T>
  operator testing::Matcher<const ExpectedHolder<T> &>() const {
    return MakeMatcher(
        new ValueMatchesMono<T>(testing::SafeMatcherCast<T>(Matcher)));
  }

private:
  M Matcher;
};

template <typename InfoT>
class ErrorMatchesMono : public testing::MatcherInterface<const ErrorHolder &> {
public:
  explicit ErrorMatchesMono(std::optional<testing::Matcher<InfoT &>> Matcher)
      : Matcher(std::move(Matcher)) {}

  bool MatchAndExplain(const ErrorHolder &Holder,
                       testing::MatchResultListener *listener) const override {
    if (Holder.Success())
      return false;

    if (Holder.Infos.size() > 1) {
      *listener << "multiple errors";
      return false;
    }

    auto &Info = *Holder.Infos[0];
    if (!Info.isA<InfoT>()) {
      *listener << "Error was not of given type";
      return false;
    }

    if (!Matcher)
      return true;

    return Matcher->MatchAndExplain(static_cast<InfoT &>(Info), listener);
  }

  void DescribeTo(std::ostream *OS) const override {
    *OS << "failed with Error of given type";
    if (Matcher) {
      *OS << " and the error ";
      Matcher->DescribeTo(OS);
    }
  }

  void DescribeNegationTo(std::ostream *OS) const override {
    *OS << "succeeded or did not fail with the error of given type";
    if (Matcher) {
      *OS << " or the error ";
      Matcher->DescribeNegationTo(OS);
    }
  }

private:
  std::optional<testing::Matcher<InfoT &>> Matcher;
};

class ErrorMessageMatches
    : public testing::MatcherInterface<const ErrorHolder &> {
public:
  explicit ErrorMessageMatches(
      testing::Matcher<std::vector<std::string>> Matcher)
      : Matcher(std::move(Matcher)) {}

  bool MatchAndExplain(const ErrorHolder &Holder,
                       testing::MatchResultListener *listener) const override {
    std::vector<std::string> Messages;
    Messages.reserve(Holder.Infos.size());
    for (const std::shared_ptr<ErrorInfoBase> &Info : Holder.Infos)
      Messages.push_back(Info->message());

    return Matcher.MatchAndExplain(Messages, listener);
  }

  void DescribeTo(std::ostream *OS) const override {
    *OS << "failed with Error whose message ";
    Matcher.DescribeTo(OS);
  }

  void DescribeNegationTo(std::ostream *OS) const override {
    *OS << "failed with an Error whose message ";
    Matcher.DescribeNegationTo(OS);
  }

private:
  testing::Matcher<std::vector<std::string>> Matcher;
};
} // namespace detail

#define EXPECT_THAT_ERROR(Err, Matcher)                                        \
  EXPECT_THAT(llvm::detail::TakeError(Err), Matcher)
#define ASSERT_THAT_ERROR(Err, Matcher)                                        \
  ASSERT_THAT(llvm::detail::TakeError(Err), Matcher)

/// Helper macro for checking the result of an 'Expected<T>'
///
///   @code{.cpp}
///     // function to be tested
///     Expected<int> myDivide(int A, int B);
///
///     TEST(myDivideTests, GoodAndBad) {
///       // test good case
///       // if you only care about success or failure:
///       EXPECT_THAT_EXPECTED(myDivide(10, 5), Succeeded());
///       // if you also care about the value:
///       EXPECT_THAT_EXPECTED(myDivide(10, 5), HasValue(2));
///
///       // test the error case
///       EXPECT_THAT_EXPECTED(myDivide(10, 0), Failed());
///       // also check the error message
///       EXPECT_THAT_EXPECTED(myDivide(10, 0),
///           FailedWithMessage("B must not be zero!"));
///     }
///   @endcode

#define EXPECT_THAT_EXPECTED(Err, Matcher)                                     \
  EXPECT_THAT(llvm::detail::TakeExpected(Err), Matcher)
#define ASSERT_THAT_EXPECTED(Err, Matcher)                                     \
  ASSERT_THAT(llvm::detail::TakeExpected(Err), Matcher)

MATCHER(Succeeded, "") { return arg.Success(); }
MATCHER(Failed, "") { return !arg.Success(); }

template <typename InfoT>
testing::Matcher<const detail::ErrorHolder &> Failed() {
  return MakeMatcher(new detail::ErrorMatchesMono<InfoT>(std::nullopt));
}

template <typename InfoT, typename M>
testing::Matcher<const detail::ErrorHolder &> Failed(M Matcher) {
  return MakeMatcher(new detail::ErrorMatchesMono<InfoT>(
      testing::SafeMatcherCast<InfoT &>(Matcher)));
}

template <typename... M>
testing::Matcher<const detail::ErrorHolder &> FailedWithMessage(M... Matcher) {
  static_assert(sizeof...(M) > 0);
  return MakeMatcher(
      new detail::ErrorMessageMatches(testing::ElementsAre(Matcher...)));
}

template <typename M>
testing::Matcher<const detail::ErrorHolder &> FailedWithMessageArray(M Matcher) {
  return MakeMatcher(new detail::ErrorMessageMatches(Matcher));
}

template <typename M>
detail::ValueMatchesPoly<M> HasValue(M Matcher) {
  return detail::ValueMatchesPoly<M>(Matcher);
}

} // namespace llvm

#endif
