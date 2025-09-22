//===-- error_test.cpp --sssssssss-----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of the ORC runtime.
//
// Note:
//   This unit test was adapted from
//   llvm/unittests/Support/ExtensibleRTTITest.cpp
//
//===----------------------------------------------------------------------===//

#include "error.h"
#include "gtest/gtest.h"

using namespace __orc_rt;

namespace {

class CustomError : public RTTIExtends<CustomError, ErrorInfoBase> {
public:
  CustomError(int V1) : V1(V1) {}
  std::string toString() const override {
    return "CustomError V1 = " + std::to_string(V1);
  }
  int getV1() const { return V1; }

protected:
  int V1;
};

class CustomSubError : public RTTIExtends<CustomSubError, CustomError> {
public:
  CustomSubError(int V1, std::string V2)
      : RTTIExtends<CustomSubError, CustomError>(V1), V2(std::move(V2)) {}
  std::string toString() const override {
    return "CustomSubError V1 = " + std::to_string(V1) + ", " + V2;
  }
  const std::string &getV2() const { return V2; }

protected:
  std::string V2;
};

} // end anonymous namespace

// Test that a checked success value doesn't cause any issues.
TEST(Error, CheckedSuccess) {
  Error E = Error::success();
  EXPECT_FALSE(E) << "Unexpected error while testing Error 'Success'";
}

// Check that a consumed success value doesn't cause any issues.
TEST(Error, ConsumeSuccess) { consumeError(Error::success()); }

TEST(Error, ConsumeError) {
  Error E = make_error<CustomError>(42);
  if (E) {
    consumeError(std::move(E));
  } else
    ADD_FAILURE() << "Error failure value should convert to true";
}

// Test that unchecked success values cause an abort.
TEST(Error, UncheckedSuccess) {
  EXPECT_DEATH({ Error E = Error::success(); },
               "Error must be checked prior to destruction")
      << "Unchecked Error Succes value did not cause abort()";
}

// Test that a checked but unhandled error causes an abort.
TEST(Error, CheckedButUnhandledError) {
  auto DropUnhandledError = []() {
    Error E = make_error<CustomError>(42);
    (void)!E;
  };
  EXPECT_DEATH(DropUnhandledError(),
               "Error must be checked prior to destruction")
      << "Unhandled Error failure value did not cause an abort()";
}

// Test that error_cast works as expected.
TEST(Error, BasicErrorCast) {
  {
    // Check casting base error value to base error type.
    auto E = make_error<CustomError>(42);
    if (auto CSE = error_cast<CustomSubError>(E)) {
      ADD_FAILURE() << "Derived cast incorrectly matched base error";
    } else if (auto CE = error_cast<CustomError>(E)) {
      EXPECT_EQ(CE->getV1(), 42) << "Unexpected wrapped value";
    } else
      ADD_FAILURE() << "Unexpected error value";
  }

  {
    // Check casting derived error value to base error type.
    auto E = make_error<CustomSubError>(42, "foo");
    if (auto CE = error_cast<CustomError>(E)) {
      EXPECT_EQ(CE->getV1(), 42) << "Unexpected wrapped value";
    } else
      ADD_FAILURE() << "Unexpected error value";
  }

  {
    // Check casting derived error value to derived error type.
    auto E = make_error<CustomSubError>(42, "foo");
    if (auto CSE = error_cast<CustomSubError>(E)) {
      EXPECT_EQ(CSE->getV1(), 42) << "Unexpected wrapped value";
      EXPECT_EQ(CSE->getV2(), "foo") << "Unexpected wrapped value";
    } else
      ADD_FAILURE() << "Unexpected error value";
  }
}

// ErrorAsOutParameter tester.
static void errAsOutParamHelper(Error &Err) {
  ErrorAsOutParameter ErrAsOutParam(&Err);
  // Verify that checked flag is raised - assignment should not crash.
  Err = Error::success();
  // Raise the checked bit manually - caller should still have to test the
  // error.
  (void)!!Err;
}

// Test that ErrorAsOutParameter sets the checked flag on construction.
TEST(Error, ErrorAsOutParameterChecked) {
  Error E = Error::success();
  errAsOutParamHelper(E);
  (void)!!E;
}

// Test that ErrorAsOutParameter clears the checked flag on destruction.
TEST(Error, ErrorAsOutParameterUnchecked) {
  EXPECT_DEATH(
      {
        Error E = Error::success();
        errAsOutParamHelper(E);
      },
      "Error must be checked prior to destruction")
      << "ErrorAsOutParameter did not clear the checked flag on destruction.";
}

// Check 'Error::isA<T>' method handling.
TEST(Error, IsAHandling) {
  // Check 'isA' handling.
  Error E = make_error<CustomError>(42);
  Error F = make_error<CustomSubError>(42, "foo");
  Error G = Error::success();

  EXPECT_TRUE(E.isA<CustomError>());
  EXPECT_FALSE(E.isA<CustomSubError>());
  EXPECT_TRUE(F.isA<CustomError>());
  EXPECT_TRUE(F.isA<CustomSubError>());
  EXPECT_FALSE(G.isA<CustomError>());

  consumeError(std::move(E));
  consumeError(std::move(F));
  consumeError(std::move(G));
}

TEST(Error, StringError) {
  auto E = make_error<StringError>("foo");
  if (auto SE = error_cast<StringError>(E)) {
    EXPECT_EQ(SE->toString(), "foo") << "Unexpected StringError value";
  } else
    ADD_FAILURE() << "Expected StringError value";
}

// Test Checked Expected<T> in success mode.
TEST(Error, CheckedExpectedInSuccessMode) {
  Expected<int> A = 7;
  EXPECT_TRUE(!!A) << "Expected with non-error value doesn't convert to 'true'";
  // Access is safe in second test, since we checked the error in the first.
  EXPECT_EQ(*A, 7) << "Incorrect Expected non-error value";
}

// Test Expected with reference type.
TEST(Error, ExpectedWithReferenceType) {
  int A = 7;
  Expected<int &> B = A;
  // 'Check' B.
  (void)!!B;
  int &C = *B;
  EXPECT_EQ(&A, &C) << "Expected failed to propagate reference";
}

// Test Unchecked Expected<T> in success mode.
// We expect this to blow up the same way Error would.
// Test runs in debug mode only.
TEST(Error, UncheckedExpectedInSuccessModeDestruction) {
  EXPECT_DEATH({ Expected<int> A = 7; },
               "Expected<T> must be checked before access or destruction.")
      << "Unchecekd Expected<T> success value did not cause an abort().";
}

// Test Unchecked Expected<T> in success mode.
// We expect this to blow up the same way Error would.
// Test runs in debug mode only.
TEST(Error, UncheckedExpectedInSuccessModeAccess) {
  EXPECT_DEATH(
      {
        Expected<int> A = 7;
        *A;
      },
      "Expected<T> must be checked before access or destruction.")
      << "Unchecekd Expected<T> success value did not cause an abort().";
}

// Test Unchecked Expected<T> in success mode.
// We expect this to blow up the same way Error would.
// Test runs in debug mode only.
TEST(Error, UncheckedExpectedInSuccessModeAssignment) {
  EXPECT_DEATH(
      {
        Expected<int> A = 7;
        A = 7;
      },
      "Expected<T> must be checked before access or destruction.")
      << "Unchecekd Expected<T> success value did not cause an abort().";
}

// Test Expected<T> in failure mode.
TEST(Error, ExpectedInFailureMode) {
  Expected<int> A = make_error<CustomError>(42);
  EXPECT_FALSE(!!A) << "Expected with error value doesn't convert to 'false'";
  Error E = A.takeError();
  EXPECT_TRUE(E.isA<CustomError>()) << "Incorrect Expected error value";
  consumeError(std::move(E));
}

// Check that an Expected instance with an error value doesn't allow access to
// operator*.
// Test runs in debug mode only.
TEST(Error, AccessExpectedInFailureMode) {
  Expected<int> A = make_error<CustomError>(42);
  EXPECT_DEATH(*A, "Expected<T> must be checked before access or destruction.")
      << "Incorrect Expected error value";
  consumeError(A.takeError());
}

// Check that an Expected instance with an error triggers an abort if
// unhandled.
// Test runs in debug mode only.
TEST(Error, UnhandledExpectedInFailureMode) {
  EXPECT_DEATH({ Expected<int> A = make_error<CustomError>(42); },
               "Expected<T> must be checked before access or destruction.")
      << "Unchecked Expected<T> failure value did not cause an abort()";
}

// Test covariance of Expected.
TEST(Error, ExpectedCovariance) {
  class B {};
  class D : public B {};

  Expected<B *> A1(Expected<D *>(nullptr));
  // Check A1 by converting to bool before assigning to it.
  (void)!!A1;
  A1 = Expected<D *>(nullptr);
  // Check A1 again before destruction.
  (void)!!A1;

  Expected<std::unique_ptr<B>> A2(Expected<std::unique_ptr<D>>(nullptr));
  // Check A2 by converting to bool before assigning to it.
  (void)!!A2;
  A2 = Expected<std::unique_ptr<D>>(nullptr);
  // Check A2 again before destruction.
  (void)!!A2;
}

// Test that the ExitOnError utility works as expected.
TEST(Error, CantFailSuccess) {
  cantFail(Error::success());

  int X = cantFail(Expected<int>(42));
  EXPECT_EQ(X, 42) << "Expected value modified by cantFail";

  int Dummy = 42;
  int &Y = cantFail(Expected<int &>(Dummy));
  EXPECT_EQ(&Dummy, &Y) << "Reference mangled by cantFail";
}

// Test that cantFail results in a crash if you pass it a failure value.
TEST(Error, CantFailDeath) {
  EXPECT_DEATH(cantFail(make_error<StringError>("foo")),
               "cantFail called on failure value")
      << "cantFail(Error) did not cause an abort for failure value";

  EXPECT_DEATH(cantFail(Expected<int>(make_error<StringError>("foo"))),
               "cantFail called on failure value")
      << "cantFail(Expected<int>) did not cause an abort for failure value";
}
