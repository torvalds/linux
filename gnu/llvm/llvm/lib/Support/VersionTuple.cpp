//===- VersionTuple.cpp - Version Number Handling ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the VersionTuple class, which represents a version in
// the form major[.minor[.subminor]].
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/VersionTuple.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

using namespace llvm;

std::string VersionTuple::getAsString() const {
  std::string Result;
  {
    llvm::raw_string_ostream Out(Result);
    Out << *this;
  }
  return Result;
}

raw_ostream &llvm::operator<<(raw_ostream &Out, const VersionTuple &V) {
  Out << V.getMajor();
  if (std::optional<unsigned> Minor = V.getMinor())
    Out << '.' << *Minor;
  if (std::optional<unsigned> Subminor = V.getSubminor())
    Out << '.' << *Subminor;
  if (std::optional<unsigned> Build = V.getBuild())
    Out << '.' << *Build;
  return Out;
}

static bool parseInt(StringRef &input, unsigned &value) {
  assert(value == 0);
  if (input.empty())
    return true;

  char next = input[0];
  input = input.substr(1);
  if (next < '0' || next > '9')
    return true;
  value = (unsigned)(next - '0');

  while (!input.empty()) {
    next = input[0];
    if (next < '0' || next > '9')
      return false;
    input = input.substr(1);
    value = value * 10 + (unsigned)(next - '0');
  }

  return false;
}

bool VersionTuple::tryParse(StringRef input) {
  unsigned major = 0, minor = 0, micro = 0, build = 0;

  // Parse the major version, [0-9]+
  if (parseInt(input, major))
    return true;

  if (input.empty()) {
    *this = VersionTuple(major);
    return false;
  }

  // If we're not done, parse the minor version, \.[0-9]+
  if (input[0] != '.')
    return true;
  input = input.substr(1);
  if (parseInt(input, minor))
    return true;

  if (input.empty()) {
    *this = VersionTuple(major, minor);
    return false;
  }

  // If we're not done, parse the micro version, \.[0-9]+
  if (!input.consume_front("."))
    return true;
  if (parseInt(input, micro))
    return true;

  if (input.empty()) {
    *this = VersionTuple(major, minor, micro);
    return false;
  }

  // If we're not done, parse the micro version, \.[0-9]+
  if (!input.consume_front("."))
    return true;
  if (parseInt(input, build))
    return true;

  // If we have characters left over, it's an error.
  if (!input.empty())
    return true;

  *this = VersionTuple(major, minor, micro, build);
  return false;
}
