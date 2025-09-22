//===- Testing/Support/SupportHelpers.h -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TESTING_SUPPORT_SUPPORTHELPERS_H
#define LLVM_TESTING_SUPPORT_SUPPORTHELPERS_H

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_os_ostream.h"
#include "gmock/gmock-matchers.h"
#include "gtest/gtest-printers.h"

#include <optional>
#include <string>

namespace llvm {
namespace detail {
struct ErrorHolder {
  std::vector<std::shared_ptr<ErrorInfoBase>> Infos;

  bool Success() const { return Infos.empty(); }
};

template <typename T> struct ExpectedHolder : public ErrorHolder {
  ExpectedHolder(ErrorHolder Err, Expected<T> &Exp)
      : ErrorHolder(std::move(Err)), Exp(Exp) {}

  Expected<T> &Exp;
};

inline void PrintTo(const ErrorHolder &Err, std::ostream *Out) {
  raw_os_ostream OS(*Out);
  OS << (Err.Success() ? "succeeded" : "failed");
  if (!Err.Success()) {
    const char *Delim = "  (";
    for (const auto &Info : Err.Infos) {
      OS << Delim;
      Delim = "; ";
      Info->log(OS);
    }
    OS << ")";
  }
}

template <typename T>
void PrintTo(const ExpectedHolder<T> &Item, std::ostream *Out) {
  if (Item.Success()) {
    *Out << "succeeded with value " << ::testing::PrintToString(*Item.Exp);
  } else {
    PrintTo(static_cast<const ErrorHolder &>(Item), Out);
  }
}

template <class InnerMatcher> class ValueIsMatcher {
public:
  explicit ValueIsMatcher(InnerMatcher ValueMatcher)
      : ValueMatcher(ValueMatcher) {}

  template <class T>
  operator ::testing::Matcher<const std::optional<T> &>() const {
    return ::testing::MakeMatcher(
        new Impl<T>(::testing::SafeMatcherCast<T>(ValueMatcher)));
  }

  template <class T, class O = std::optional<T>>
  class Impl : public ::testing::MatcherInterface<const O &> {
  public:
    explicit Impl(const ::testing::Matcher<T> &ValueMatcher)
        : ValueMatcher(ValueMatcher) {}

    bool MatchAndExplain(const O &Input,
                         testing::MatchResultListener *L) const override {
      return Input && ValueMatcher.MatchAndExplain(*Input, L);
    }

    void DescribeTo(std::ostream *OS) const override {
      *OS << "has a value that ";
      ValueMatcher.DescribeTo(OS);
    }
    void DescribeNegationTo(std::ostream *OS) const override {
      *OS << "does not have a value that ";
      ValueMatcher.DescribeTo(OS);
    }

  private:
    testing::Matcher<T> ValueMatcher;
  };

private:
  InnerMatcher ValueMatcher;
};
} // namespace detail

/// Matches an std::optional<T> with a value that conforms to an inner matcher.
/// To match std::nullopt you could use Eq(std::nullopt).
template <class InnerMatcher>
detail::ValueIsMatcher<InnerMatcher> ValueIs(const InnerMatcher &ValueMatcher) {
  return detail::ValueIsMatcher<InnerMatcher>(ValueMatcher);
}
namespace unittest {

SmallString<128> getInputFileDirectory(const char *Argv0);

/// A RAII object that creates a temporary directory upon initialization and
/// removes it upon destruction.
class TempDir {
  SmallString<128> Path;

public:
  /// Creates a managed temporary directory.
  ///
  /// @param Name The name of the directory to create.
  /// @param Unique If true, the directory will be created using
  ///               llvm::sys::fs::createUniqueDirectory.
  explicit TempDir(StringRef Name, bool Unique = false) {
    std::error_code EC;
    if (Unique) {
      EC = llvm::sys::fs::createUniqueDirectory(Name, Path);
      if (!EC) {
        // Resolve any symlinks in the new directory.
        std::string UnresolvedPath(Path.str());
        EC = llvm::sys::fs::real_path(UnresolvedPath, Path);
      }
    } else {
      Path = Name;
      EC = llvm::sys::fs::create_directory(Path);
    }
    if (EC)
      Path.clear();
    EXPECT_FALSE(EC) << EC.message();
  }

  ~TempDir() {
    if (!Path.empty()) {
      EXPECT_FALSE(llvm::sys::fs::remove_directories(Path.str()));
    }
  }

  TempDir(const TempDir &) = delete;
  TempDir &operator=(const TempDir &) = delete;

  TempDir(TempDir &&) = default;
  TempDir &operator=(TempDir &&) = default;

  /// The path to the temporary directory.
  StringRef path() const { return Path; }

  /// The null-terminated C string pointing to the path.
  const char *c_str() { return Path.c_str(); }

  /// Creates a new path by appending the argument to the path of the managed
  /// directory using the native path separator.
  SmallString<128> path(StringRef component) const {
    SmallString<128> Result(Path);
    SmallString<128> ComponentToAppend(component);
    llvm::sys::path::native(ComponentToAppend);
    llvm::sys::path::append(Result, Twine(ComponentToAppend));
    return Result;
  }
};

/// A RAII object that creates a link upon initialization and
/// removes it upon destruction.
///
/// The link may be a soft or a hard link, depending on the platform.
class TempLink {
  SmallString<128> Path;

public:
  /// Creates a managed link at path Link pointing to Target.
  TempLink(StringRef Target, StringRef Link) {
    Path = Link;
    std::error_code EC = sys::fs::create_link(Target, Link);
    if (EC)
      Path.clear();
    EXPECT_FALSE(EC);
  }
  ~TempLink() {
    if (!Path.empty()) {
      EXPECT_FALSE(llvm::sys::fs::remove(Path.str()));
    }
  }

  TempLink(const TempLink &) = delete;
  TempLink &operator=(const TempLink &) = delete;

  TempLink(TempLink &&) = default;
  TempLink &operator=(TempLink &&) = default;

  /// The path to the link.
  StringRef path() const { return Path; }
};

/// A RAII object that creates a file upon initialization and
/// removes it upon destruction.
class TempFile {
  SmallString<128> Path;

public:
  /// Creates a managed file.
  ///
  /// @param Name The name of the file to create.
  /// @param Contents The string to write to the file.
  /// @param Unique If true, the file will be created using
  ///               llvm::sys::fs::createTemporaryFile.
  TempFile(StringRef Name, StringRef Suffix = "", StringRef Contents = "",
           bool Unique = false) {
    std::error_code EC;
    int fd;
    if (Unique) {
      EC = llvm::sys::fs::createTemporaryFile(Name, Suffix, fd, Path);
    } else {
      Path = Name;
      if (!Suffix.empty()) {
        Path.append(".");
        Path.append(Suffix);
      }
      EC = llvm::sys::fs::openFileForWrite(Path, fd);
    }
    EXPECT_FALSE(EC);
    raw_fd_ostream OS(fd, /*shouldClose*/ true);
    OS << Contents;
    OS.flush();
    EXPECT_FALSE(OS.error());
    if (EC || OS.error())
      Path.clear();
  }
  ~TempFile() {
    if (!Path.empty()) {
      EXPECT_FALSE(llvm::sys::fs::remove(Path.str()));
    }
  }

  TempFile(const TempFile &) = delete;
  TempFile &operator=(const TempFile &) = delete;

  TempFile(TempFile &&) = default;
  TempFile &operator=(TempFile &&) = default;

  /// The path to the file.
  StringRef path() const { return Path; }
};

} // namespace unittest
} // namespace llvm

#endif
