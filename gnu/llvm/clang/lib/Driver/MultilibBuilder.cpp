//===- MultilibBuilder.cpp - MultilibBuilder Implementation -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Driver/MultilibBuilder.h"
#include "ToolChains/CommonArgs.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace driver;

/// normalize Segment to "/foo/bar" or "".
static void normalizePathSegment(std::string &Segment) {
  StringRef seg = Segment;

  // Prune trailing "/" or "./"
  while (true) {
    StringRef last = llvm::sys::path::filename(seg);
    if (last != ".")
      break;
    seg = llvm::sys::path::parent_path(seg);
  }

  if (seg.empty() || seg == "/") {
    Segment.clear();
    return;
  }

  // Add leading '/'
  if (seg.front() != '/') {
    Segment = "/" + seg.str();
  } else {
    Segment = std::string(seg);
  }
}

MultilibBuilder::MultilibBuilder(StringRef GCC, StringRef OS, StringRef Include)
    : GCCSuffix(GCC), OSSuffix(OS), IncludeSuffix(Include) {
  normalizePathSegment(GCCSuffix);
  normalizePathSegment(OSSuffix);
  normalizePathSegment(IncludeSuffix);
}

MultilibBuilder::MultilibBuilder(StringRef Suffix)
    : MultilibBuilder(Suffix, Suffix, Suffix) {}

MultilibBuilder &MultilibBuilder::gccSuffix(StringRef S) {
  GCCSuffix = std::string(S);
  normalizePathSegment(GCCSuffix);
  return *this;
}

MultilibBuilder &MultilibBuilder::osSuffix(StringRef S) {
  OSSuffix = std::string(S);
  normalizePathSegment(OSSuffix);
  return *this;
}

MultilibBuilder &MultilibBuilder::includeSuffix(StringRef S) {
  IncludeSuffix = std::string(S);
  normalizePathSegment(IncludeSuffix);
  return *this;
}

bool MultilibBuilder::isValid() const {
  llvm::StringMap<int> FlagSet;
  for (unsigned I = 0, N = Flags.size(); I != N; ++I) {
    StringRef Flag(Flags[I]);
    llvm::StringMap<int>::iterator SI = FlagSet.find(Flag.substr(1));

    assert(StringRef(Flag).front() == '-' || StringRef(Flag).front() == '!');

    if (SI == FlagSet.end())
      FlagSet[Flag.substr(1)] = I;
    else if (Flags[I] != Flags[SI->getValue()])
      return false;
  }
  return true;
}

MultilibBuilder &MultilibBuilder::flag(StringRef Flag, bool Disallow) {
  tools::addMultilibFlag(!Disallow, Flag, Flags);
  return *this;
}

Multilib MultilibBuilder::makeMultilib() const {
  return Multilib(GCCSuffix, OSSuffix, IncludeSuffix, Flags);
}

MultilibSetBuilder &MultilibSetBuilder::Maybe(const MultilibBuilder &M) {
  MultilibBuilder Opposite;
  // Negate positive flags
  for (StringRef Flag : M.flags()) {
    if (Flag.front() == '-')
      Opposite.flag(Flag, /*Disallow=*/true);
  }
  return Either(M, Opposite);
}

MultilibSetBuilder &MultilibSetBuilder::Either(const MultilibBuilder &M1,
                                               const MultilibBuilder &M2) {
  return Either({M1, M2});
}

MultilibSetBuilder &MultilibSetBuilder::Either(const MultilibBuilder &M1,
                                               const MultilibBuilder &M2,
                                               const MultilibBuilder &M3) {
  return Either({M1, M2, M3});
}

MultilibSetBuilder &MultilibSetBuilder::Either(const MultilibBuilder &M1,
                                               const MultilibBuilder &M2,
                                               const MultilibBuilder &M3,
                                               const MultilibBuilder &M4) {
  return Either({M1, M2, M3, M4});
}

MultilibSetBuilder &MultilibSetBuilder::Either(const MultilibBuilder &M1,
                                               const MultilibBuilder &M2,
                                               const MultilibBuilder &M3,
                                               const MultilibBuilder &M4,
                                               const MultilibBuilder &M5) {
  return Either({M1, M2, M3, M4, M5});
}

static MultilibBuilder compose(const MultilibBuilder &Base,
                               const MultilibBuilder &New) {
  SmallString<128> GCCSuffix;
  llvm::sys::path::append(GCCSuffix, "/", Base.gccSuffix(), New.gccSuffix());
  SmallString<128> OSSuffix;
  llvm::sys::path::append(OSSuffix, "/", Base.osSuffix(), New.osSuffix());
  SmallString<128> IncludeSuffix;
  llvm::sys::path::append(IncludeSuffix, "/", Base.includeSuffix(),
                          New.includeSuffix());

  MultilibBuilder Composed(GCCSuffix, OSSuffix, IncludeSuffix);

  MultilibBuilder::flags_list &Flags = Composed.flags();

  Flags.insert(Flags.end(), Base.flags().begin(), Base.flags().end());
  Flags.insert(Flags.end(), New.flags().begin(), New.flags().end());

  return Composed;
}

MultilibSetBuilder &
MultilibSetBuilder::Either(ArrayRef<MultilibBuilder> MultilibSegments) {
  multilib_list Composed;

  if (Multilibs.empty())
    Multilibs.insert(Multilibs.end(), MultilibSegments.begin(),
                     MultilibSegments.end());
  else {
    for (const auto &New : MultilibSegments) {
      for (const auto &Base : Multilibs) {
        MultilibBuilder MO = compose(Base, New);
        if (MO.isValid())
          Composed.push_back(MO);
      }
    }

    Multilibs = Composed;
  }

  return *this;
}

MultilibSetBuilder &MultilibSetBuilder::FilterOut(const char *Regex) {
  llvm::Regex R(Regex);
#ifndef NDEBUG
  std::string Error;
  if (!R.isValid(Error)) {
    llvm::errs() << Error;
    llvm_unreachable("Invalid regex!");
  }
#endif
  llvm::erase_if(Multilibs, [&R](const MultilibBuilder &M) {
    return R.match(M.gccSuffix());
  });
  return *this;
}

MultilibSet MultilibSetBuilder::makeMultilibSet() const {
  MultilibSet Result;
  for (const auto &M : Multilibs) {
    Result.push_back(M.makeMultilib());
  }
  return Result;
}
