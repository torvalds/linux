//===- MultilibBuilder.h
//-----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_DRIVER_MULTILIBBUILDER_H
#define LLVM_CLANG_DRIVER_MULTILIBBUILDER_H

#include "clang/Driver/Multilib.h"

namespace clang {
namespace driver {

/// This corresponds to a single GCC multilib, or a segment of one controlled
/// by a command line flag. This class can be used to create a Multilib, and
/// contains helper functions to mutate it before creating a Multilib instance
/// with makeMultilib().
class MultilibBuilder {
public:
  using flags_list = std::vector<std::string>;

private:
  std::string GCCSuffix;
  std::string OSSuffix;
  std::string IncludeSuffix;
  flags_list Flags;

public:
  MultilibBuilder(StringRef GCCSuffix, StringRef OSSuffix,
                  StringRef IncludeSuffix);

  /// Initializes GCCSuffix, OSSuffix & IncludeSuffix to the same value.
  MultilibBuilder(StringRef Suffix = {});

  /// Get the detected GCC installation path suffix for the multi-arch
  /// target variant. Always starts with a '/', unless empty
  const std::string &gccSuffix() const {
    assert(GCCSuffix.empty() ||
           (StringRef(GCCSuffix).front() == '/' && GCCSuffix.size() > 1));
    return GCCSuffix;
  }

  /// Set the GCC installation path suffix.
  MultilibBuilder &gccSuffix(StringRef S);

  /// Get the detected os path suffix for the multi-arch
  /// target variant. Always starts with a '/', unless empty
  const std::string &osSuffix() const {
    assert(OSSuffix.empty() ||
           (StringRef(OSSuffix).front() == '/' && OSSuffix.size() > 1));
    return OSSuffix;
  }

  /// Set the os path suffix.
  MultilibBuilder &osSuffix(StringRef S);

  /// Get the include directory suffix. Always starts with a '/', unless
  /// empty
  const std::string &includeSuffix() const {
    assert(IncludeSuffix.empty() || (StringRef(IncludeSuffix).front() == '/' &&
                                     IncludeSuffix.size() > 1));
    return IncludeSuffix;
  }

  /// Set the include directory suffix
  MultilibBuilder &includeSuffix(StringRef S);

  /// Get the flags that indicate or contraindicate this multilib's use
  /// All elements begin with either '-' or '!'
  const flags_list &flags() const { return Flags; }
  flags_list &flags() { return Flags; }

  /// Add a flag to the flags list
  /// \p Flag must be a flag accepted by the driver.
  /// \p Disallow defines whether the flag is negated and therefore disallowed.
  MultilibBuilder &flag(StringRef Flag, bool Disallow = false);

  Multilib makeMultilib() const;

  /// Check whether any of the 'against' flags contradict the 'for' flags.
  bool isValid() const;

  /// Check whether the default is selected
  bool isDefault() const {
    return GCCSuffix.empty() && OSSuffix.empty() && IncludeSuffix.empty();
  }
};

/// This class can be used to create a MultilibSet, and contains helper
/// functions to add combinations of multilibs before creating a MultilibSet
/// instance with makeMultilibSet().
class MultilibSetBuilder {
public:
  using multilib_list = std::vector<MultilibBuilder>;

  MultilibSetBuilder() = default;

  /// Add an optional Multilib segment
  MultilibSetBuilder &Maybe(const MultilibBuilder &M);

  /// Add a set of mutually incompatible Multilib segments
  MultilibSetBuilder &Either(const MultilibBuilder &M1,
                             const MultilibBuilder &M2);
  MultilibSetBuilder &Either(const MultilibBuilder &M1,
                             const MultilibBuilder &M2,
                             const MultilibBuilder &M3);
  MultilibSetBuilder &Either(const MultilibBuilder &M1,
                             const MultilibBuilder &M2,
                             const MultilibBuilder &M3,
                             const MultilibBuilder &M4);
  MultilibSetBuilder &Either(const MultilibBuilder &M1,
                             const MultilibBuilder &M2,
                             const MultilibBuilder &M3,
                             const MultilibBuilder &M4,
                             const MultilibBuilder &M5);
  MultilibSetBuilder &Either(ArrayRef<MultilibBuilder> Ms);

  /// Filter out those Multilibs whose gccSuffix matches the given expression
  MultilibSetBuilder &FilterOut(const char *Regex);

  MultilibSet makeMultilibSet() const;

private:
  multilib_list Multilibs;
};

} // namespace driver
} // namespace clang

#endif // LLVM_CLANG_DRIVER_MULTILIBBUILDER_H
