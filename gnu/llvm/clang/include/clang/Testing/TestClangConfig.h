//===--- TestClangConfig.h ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TESTING_TESTCLANGCONFIG_H
#define LLVM_CLANG_TESTING_TESTCLANGCONFIG_H

#include "clang/Testing/CommandLineArgs.h"
#include "llvm/Support/raw_ostream.h"
#include <string>
#include <vector>

namespace clang {

/// A Clang configuration for end-to-end tests that can be converted to
/// command line arguments for the driver.
///
/// The configuration is represented as typed, named values, making it easier
/// and safer to work with compared to an array of string command line flags.
struct TestClangConfig {
  TestLanguage Language;

  /// The argument of the `-target` command line flag.
  std::string Target;

  bool isC() const { return Language == Lang_C89 || Language == Lang_C99; }

  bool isC99OrLater() const { return Language == Lang_C99; }

  bool isCXX() const {
    return Language == Lang_CXX03 || Language == Lang_CXX11 ||
           Language == Lang_CXX14 || Language == Lang_CXX17 ||
           Language == Lang_CXX20 || Language == Lang_CXX23;
  }

  bool isCXX11OrLater() const {
    return Language == Lang_CXX11 || Language == Lang_CXX14 ||
           Language == Lang_CXX17 || Language == Lang_CXX20 ||
           Language == Lang_CXX23;
  }

  bool isCXX14OrLater() const {
    return Language == Lang_CXX14 || Language == Lang_CXX17 ||
           Language == Lang_CXX20 || Language == Lang_CXX23;
  }

  bool isCXX17OrLater() const {
    return Language == Lang_CXX17 || Language == Lang_CXX20 ||
           Language == Lang_CXX23;
  }

  bool isCXX20OrLater() const {
    return Language == Lang_CXX20 || Language == Lang_CXX23;
  }

  bool isCXX23OrLater() const { return Language == Lang_CXX23; }

  bool supportsCXXDynamicExceptionSpecification() const {
    return Language == Lang_CXX03 || Language == Lang_CXX11 ||
           Language == Lang_CXX14;
  }

  bool hasDelayedTemplateParsing() const {
    return Target == "x86_64-pc-win32-msvc";
  }

  std::vector<std::string> getCommandLineArgs() const {
    std::vector<std::string> Result = getCommandLineArgsForTesting(Language);
    Result.push_back("-target");
    Result.push_back(Target);
    return Result;
  }

  std::string toString() const {
    std::string Result;
    llvm::raw_string_ostream OS(Result);
    OS << "{ Language=" << Language << ", Target=" << Target << " }";
    return Result;
  }

  friend std::ostream &operator<<(std::ostream &OS,
                                  const TestClangConfig &ClangConfig) {
    return OS << ClangConfig.toString();
  }
};

} // end namespace clang

#endif
