//===- Timer.cpp ----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lld/Common/Timer.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Format.h"
#include <ratio>

using namespace lld;
using namespace llvm;

ScopedTimer::ScopedTimer(Timer &t) : t(&t) {
  startTime = std::chrono::high_resolution_clock::now();
}

void ScopedTimer::stop() {
  if (!t)
    return;
  t->addToTotal(std::chrono::high_resolution_clock::now() - startTime);
  t = nullptr;
}

ScopedTimer::~ScopedTimer() { stop(); }

Timer::Timer(llvm::StringRef name) : total(0), name(std::string(name)) {}
Timer::Timer(llvm::StringRef name, Timer &parent)
    : total(0), name(std::string(name)) {
  parent.children.push_back(this);
}

void Timer::print() {
  double totalDuration = static_cast<double>(millis());

  // We want to print the grand total under all the intermediate phases, so we
  // print all children first, then print the total under that.
  for (const auto &child : children)
    if (child->total > 0)
      child->print(1, totalDuration);

  message(std::string(50, '-'));

  print(0, millis(), false);
}

double Timer::millis() const {
  return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
             std::chrono::nanoseconds(total))
      .count();
}

void Timer::print(int depth, double totalDuration, bool recurse) const {
  double p = 100.0 * millis() / totalDuration;

  SmallString<32> str;
  llvm::raw_svector_ostream stream(str);
  std::string s = std::string(depth * 2, ' ') + name + std::string(":");
  stream << format("%-30s%7d ms (%5.1f%%)", s.c_str(), (int)millis(), p);

  message(str);

  if (recurse) {
    for (const auto &child : children)
      if (child->total > 0)
        child->print(depth + 1, totalDuration);
  }
}
