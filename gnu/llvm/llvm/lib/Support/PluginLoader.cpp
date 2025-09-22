//===-- PluginLoader.cpp - Implement -load command line option ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the -load <plugin> command line option handler.
//
//===----------------------------------------------------------------------===//

#define DONT_GET_PLUGIN_LOADER_OPTION
#include "llvm/Support/PluginLoader.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/Mutex.h"
#include "llvm/Support/raw_ostream.h"
#include <vector>
using namespace llvm;

namespace {

struct Plugins {
  sys::SmartMutex<true> Lock;
  std::vector<std::string> List;
};

Plugins &getPlugins() {
  static Plugins P;
  return P;
}

} // anonymous namespace

void PluginLoader::operator=(const std::string &Filename) {
  auto &P = getPlugins();
  sys::SmartScopedLock<true> Lock(P.Lock);
  std::string Error;
  if (sys::DynamicLibrary::LoadLibraryPermanently(Filename.c_str(), &Error)) {
    errs() << "Error opening '" << Filename << "': " << Error
           << "\n  -load request ignored.\n";
  } else {
    P.List.push_back(Filename);
  }
}

unsigned PluginLoader::getNumPlugins() {
  auto &P = getPlugins();
  sys::SmartScopedLock<true> Lock(P.Lock);
  return P.List.size();
}

std::string &PluginLoader::getPlugin(unsigned num) {
  auto &P = getPlugins();
  sys::SmartScopedLock<true> Lock(P.Lock);
  assert(num < P.List.size() && "Asking for an out of bounds plugin");
  return P.List[num];
}
