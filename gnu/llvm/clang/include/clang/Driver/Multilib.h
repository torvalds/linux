//===- Multilib.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_DRIVER_MULTILIB_H
#define LLVM_CLANG_DRIVER_MULTILIB_H

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/SourceMgr.h"
#include <cassert>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace clang {
namespace driver {

/// This corresponds to a single GCC Multilib, or a segment of one controlled
/// by a command line flag.
/// See also MultilibBuilder for building a multilib by mutating it
/// incrementally.
class Multilib {
public:
  using flags_list = std::vector<std::string>;

private:
  std::string GCCSuffix;
  std::string OSSuffix;
  std::string IncludeSuffix;
  flags_list Flags;

  // Optionally, a multilib can be assigned a string tag indicating that it's
  // part of a group of mutually exclusive possibilities. If two or more
  // multilibs have the same non-empty value of ExclusiveGroup, then only the
  // last matching one of them will be selected.
  //
  // Setting this to the empty string is a special case, indicating that the
  // directory is not mutually exclusive with anything else.
  std::string ExclusiveGroup;

public:
  /// GCCSuffix, OSSuffix & IncludeSuffix will be appended directly to the
  /// sysroot string so they must either be empty or begin with a '/' character.
  /// This is enforced with an assert in the constructor.
  Multilib(StringRef GCCSuffix = {}, StringRef OSSuffix = {},
           StringRef IncludeSuffix = {}, const flags_list &Flags = flags_list(),
           StringRef ExclusiveGroup = {});

  /// Get the detected GCC installation path suffix for the multi-arch
  /// target variant. Always starts with a '/', unless empty
  const std::string &gccSuffix() const { return GCCSuffix; }

  /// Get the detected os path suffix for the multi-arch
  /// target variant. Always starts with a '/', unless empty
  const std::string &osSuffix() const { return OSSuffix; }

  /// Get the include directory suffix. Always starts with a '/', unless
  /// empty
  const std::string &includeSuffix() const { return IncludeSuffix; }

  /// Get the flags that indicate or contraindicate this multilib's use
  /// All elements begin with either '-' or '!'
  const flags_list &flags() const { return Flags; }

  /// Get the exclusive group label.
  const std::string &exclusiveGroup() const { return ExclusiveGroup; }

  LLVM_DUMP_METHOD void dump() const;
  /// print summary of the Multilib
  void print(raw_ostream &OS) const;

  /// Check whether the default is selected
  bool isDefault() const
  { return GCCSuffix.empty() && OSSuffix.empty() && IncludeSuffix.empty(); }

  bool operator==(const Multilib &Other) const;
};

raw_ostream &operator<<(raw_ostream &OS, const Multilib &M);

/// See also MultilibSetBuilder for combining multilibs into a set.
class MultilibSet {
public:
  using multilib_list = std::vector<Multilib>;
  using const_iterator = multilib_list::const_iterator;
  using IncludeDirsFunc =
      std::function<std::vector<std::string>(const Multilib &M)>;
  using FilterCallback = llvm::function_ref<bool(const Multilib &)>;

  /// Uses regular expressions to simplify flags used for multilib selection.
  /// For example, we may wish both -mfloat-abi=soft and -mfloat-abi=softfp to
  /// be treated as -mfloat-abi=soft.
  struct FlagMatcher {
    std::string Match;
    std::vector<std::string> Flags;
  };

private:
  multilib_list Multilibs;
  std::vector<FlagMatcher> FlagMatchers;
  IncludeDirsFunc IncludeCallback;
  IncludeDirsFunc FilePathsCallback;

public:
  MultilibSet() = default;
  MultilibSet(multilib_list &&Multilibs,
              std::vector<FlagMatcher> &&FlagMatchers = {})
      : Multilibs(Multilibs), FlagMatchers(FlagMatchers) {}

  const multilib_list &getMultilibs() { return Multilibs; }

  /// Filter out some subset of the Multilibs using a user defined callback
  MultilibSet &FilterOut(FilterCallback F);

  /// Add a completed Multilib to the set
  void push_back(const Multilib &M);

  const_iterator begin() const { return Multilibs.begin(); }
  const_iterator end() const { return Multilibs.end(); }

  /// Select compatible variants, \returns false if none are compatible
  bool select(const Multilib::flags_list &Flags,
              llvm::SmallVectorImpl<Multilib> &) const;

  unsigned size() const { return Multilibs.size(); }

  /// Get the given flags plus flags found by matching them against the
  /// FlagMatchers and choosing the Flags of each accordingly. The select method
  /// calls this method so in most cases it's not necessary to call it directly.
  llvm::StringSet<> expandFlags(const Multilib::flags_list &) const;

  LLVM_DUMP_METHOD void dump() const;
  void print(raw_ostream &OS) const;

  MultilibSet &setIncludeDirsCallback(IncludeDirsFunc F) {
    IncludeCallback = std::move(F);
    return *this;
  }

  const IncludeDirsFunc &includeDirsCallback() const { return IncludeCallback; }

  MultilibSet &setFilePathsCallback(IncludeDirsFunc F) {
    FilePathsCallback = std::move(F);
    return *this;
  }

  const IncludeDirsFunc &filePathsCallback() const { return FilePathsCallback; }

  static llvm::ErrorOr<MultilibSet>
  parseYaml(llvm::MemoryBufferRef, llvm::SourceMgr::DiagHandlerTy = nullptr,
            void *DiagHandlerCtxt = nullptr);
};

raw_ostream &operator<<(raw_ostream &OS, const MultilibSet &MS);

} // namespace driver
} // namespace clang

#endif // LLVM_CLANG_DRIVER_MULTILIB_H
