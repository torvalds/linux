//===- Args.cpp -----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lld/Common/Args.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/Path.h"

using namespace llvm;
using namespace lld;

// TODO(sbc): Remove this once CGOptLevel can be set completely based on bitcode
// function metadata.
int lld::args::getCGOptLevel(int optLevelLTO) {
  return std::clamp(optLevelLTO, 2, 3);
}

static int64_t getInteger(opt::InputArgList &args, unsigned key,
                          int64_t Default, unsigned base) {
  auto *a = args.getLastArg(key);
  if (!a)
    return Default;

  int64_t v;
  StringRef s = a->getValue();
  if (base == 16)
    s.consume_front_insensitive("0x");
  if (to_integer(s, v, base))
    return v;

  StringRef spelling = args.getArgString(a->getIndex());
  error(spelling + ": number expected, but got '" + a->getValue() + "'");
  return 0;
}

int64_t lld::args::getInteger(opt::InputArgList &args, unsigned key,
                              int64_t Default) {
  return ::getInteger(args, key, Default, 10);
}

int64_t lld::args::getHex(opt::InputArgList &args, unsigned key,
                          int64_t Default) {
  return ::getInteger(args, key, Default, 16);
}

SmallVector<StringRef, 0> lld::args::getStrings(opt::InputArgList &args,
                                                int id) {
  SmallVector<StringRef, 0> v;
  for (auto *arg : args.filtered(id))
    v.push_back(arg->getValue());
  return v;
}

uint64_t lld::args::getZOptionValue(opt::InputArgList &args, int id,
                                    StringRef key, uint64_t defaultValue) {
  for (auto *arg : args.filtered(id)) {
    std::pair<StringRef, StringRef> kv = StringRef(arg->getValue()).split('=');
    if (kv.first == key) {
      if (!to_integer(kv.second, defaultValue))
        error("invalid " + key + ": " + kv.second);
      arg->claim();
    }
  }
  return defaultValue;
}

std::vector<StringRef> lld::args::getLines(MemoryBufferRef mb) {
  SmallVector<StringRef, 0> arr;
  mb.getBuffer().split(arr, '\n');

  std::vector<StringRef> ret;
  for (StringRef s : arr) {
    s = s.trim();
    if (!s.empty() && s[0] != '#')
      ret.push_back(s);
  }
  return ret;
}

StringRef lld::args::getFilenameWithoutExe(StringRef path) {
  if (path.ends_with_insensitive(".exe"))
    return sys::path::stem(path);
  return sys::path::filename(path);
}
