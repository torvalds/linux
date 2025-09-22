//===- Reproduce.cpp - Utilities for creating reproducers -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lld/Common/Reproduce.h"
#include "llvm/Option/Arg.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

using namespace lld;
using namespace llvm;
using namespace llvm::sys;

// Makes a given pathname an absolute path first, and then remove
// beginning /. For example, "../foo.o" is converted to "home/john/foo.o",
// assuming that the current directory is "/home/john/bar".
// Returned string is a forward slash separated path even on Windows to avoid
// a mess with backslash-as-escape and backslash-as-path-separator.
std::string lld::relativeToRoot(StringRef path) {
  SmallString<128> abs = path;
  if (fs::make_absolute(abs))
    return std::string(path);
  path::remove_dots(abs, /*remove_dot_dot=*/true);

  // This is Windows specific. root_name() returns a drive letter
  // (e.g. "c:") or a UNC name (//net). We want to keep it as part
  // of the result.
  SmallString<128> res;
  StringRef root = path::root_name(abs);
  if (root.ends_with(":"))
    res = root.drop_back();
  else if (root.starts_with("//"))
    res = root.substr(2);

  path::append(res, path::relative_path(abs));
  return path::convert_to_slash(res);
}

// Quote a given string if it contains a space character.
std::string lld::quote(StringRef s) {
  if (s.contains(' '))
    return ("\"" + s + "\"").str();
  return std::string(s);
}

// Converts an Arg to a string representation suitable for a response file.
// To show an Arg in a diagnostic, use Arg::getAsString() instead.
std::string lld::toString(const opt::Arg &arg) {
  std::string k = std::string(arg.getSpelling());
  if (arg.getNumValues() == 0)
    return k;
  std::string v;
  for (size_t i = 0; i < arg.getNumValues(); ++i) {
    if (i > 0)
      v.push_back(' ');
    v += quote(arg.getValue(i));
  }
  if (arg.getOption().getRenderStyle() == opt::Option::RenderJoinedStyle)
    return k + v;
  return k + " " + v;
}
