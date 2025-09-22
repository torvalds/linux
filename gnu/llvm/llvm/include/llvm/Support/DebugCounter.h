//===- llvm/Support/DebugCounter.h - Debug counter support ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file provides an implementation of debug counters.  Debug
/// counters are a tool that let you narrow down a miscompilation to a specific
/// thing happening.
///
/// To give a use case: Imagine you have a file, very large, and you
/// are trying to understand the minimal transformation that breaks it. Bugpoint
/// and bisection is often helpful here in narrowing it down to a specific pass,
/// but it's still a very large file, and a very complicated pass to try to
/// debug.  That is where debug counting steps in.  You can instrument the pass
/// with a debug counter before it does a certain thing, and depending on the
/// counts, it will either execute that thing or not.  The debug counter itself
/// consists of a list of chunks (inclusive numeric ranges). `shouldExecute`
/// returns true iff the list is empty or the current count is in one of the
/// chunks.
///
/// Note that a counter set to a negative number will always execute. For a
/// concrete example, during predicateinfo creation, the renaming pass replaces
/// each use with a renamed use.
////
/// If I use DEBUG_COUNTER to create a counter called "predicateinfo", and
/// variable name RenameCounter, and then instrument this renaming with a debug
/// counter, like so:
///
/// if (!DebugCounter::shouldExecute(RenameCounter)
/// <continue or return or whatever not executing looks like>
///
/// Now I can, from the command line, make it rename or not rename certain uses
/// by setting the chunk list.
/// So for example
/// bin/opt -debug-counter=predicateinfo=47
/// will skip renaming the first 47 uses, then rename one, then skip the rest.
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_DEBUGCOUNTER_H
#define LLVM_SUPPORT_DEBUGCOUNTER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/UniqueVector.h"
#include "llvm/Support/Debug.h"
#include <string>

namespace llvm {

class raw_ostream;

class DebugCounter {
public:
  struct Chunk {
    int64_t Begin;
    int64_t End;
    void print(llvm::raw_ostream &OS);
    bool contains(int64_t Idx) { return Idx >= Begin && Idx <= End; }
  };

  static void printChunks(raw_ostream &OS, ArrayRef<Chunk>);

  /// Return true on parsing error and print the error message on the
  /// llvm::errs()
  static bool parseChunks(StringRef Str, SmallVector<Chunk> &Res);

  /// Returns a reference to the singleton instance.
  static DebugCounter &instance();

  // Used by the command line option parser to push a new value it parsed.
  void push_back(const std::string &);

  // Register a counter with the specified name.
  //
  // FIXME: Currently, counter registration is required to happen before command
  // line option parsing. The main reason to register counters is to produce a
  // nice list of them on the command line, but i'm not sure this is worth it.
  static unsigned registerCounter(StringRef Name, StringRef Desc) {
    return instance().addCounter(std::string(Name), std::string(Desc));
  }
  static bool shouldExecuteImpl(unsigned CounterName);

  inline static bool shouldExecute(unsigned CounterName) {
    if (!isCountingEnabled())
      return true;
    return shouldExecuteImpl(CounterName);
  }

  // Return true if a given counter had values set (either programatically or on
  // the command line).  This will return true even if those values are
  // currently in a state where the counter will always execute.
  static bool isCounterSet(unsigned ID) {
    return instance().Counters[ID].IsSet;
  }

  struct CounterState {
    int64_t Count;
    uint64_t ChunkIdx;
  };

  // Return the state of a counter. This only works for set counters.
  static CounterState getCounterState(unsigned ID) {
    auto &Us = instance();
    auto Result = Us.Counters.find(ID);
    assert(Result != Us.Counters.end() && "Asking about a non-set counter");
    return {Result->second.Count, Result->second.CurrChunkIdx};
  }

  // Set a registered counter to a given state.
  static void setCounterState(unsigned ID, CounterState State) {
    auto &Us = instance();
    auto &Counter = Us.Counters[ID];
    Counter.Count = State.Count;
    Counter.CurrChunkIdx = State.ChunkIdx;
  }

  // Dump or print the current counter set into llvm::dbgs().
  LLVM_DUMP_METHOD void dump() const;

  void print(raw_ostream &OS) const;

  // Get the counter ID for a given named counter, or return 0 if none is found.
  unsigned getCounterId(const std::string &Name) const {
    return RegisteredCounters.idFor(Name);
  }

  // Return the number of registered counters.
  unsigned int getNumCounters() const { return RegisteredCounters.size(); }

  // Return the name and description of the counter with the given ID.
  std::pair<std::string, std::string> getCounterInfo(unsigned ID) const {
    return std::make_pair(RegisteredCounters[ID], Counters.lookup(ID).Desc);
  }

  // Iterate through the registered counters
  typedef UniqueVector<std::string> CounterVector;
  CounterVector::const_iterator begin() const {
    return RegisteredCounters.begin();
  }
  CounterVector::const_iterator end() const { return RegisteredCounters.end(); }

  // Force-enables counting all DebugCounters.
  //
  // Since DebugCounters are incompatible with threading (not only do they not
  // make sense, but we'll also see data races), this should only be used in
  // contexts where we're certain we won't spawn threads.
  static void enableAllCounters() { instance().Enabled = true; }

  static bool isCountingEnabled() {
// Compile to nothing when debugging is off
#ifdef NDEBUG
    return false;
#else
    return instance().Enabled || instance().ShouldPrintCounter;
#endif
  }

protected:
  unsigned addCounter(const std::string &Name, const std::string &Desc) {
    unsigned Result = RegisteredCounters.insert(Name);
    Counters[Result] = {};
    Counters[Result].Desc = Desc;
    return Result;
  }
  // Struct to store counter info.
  struct CounterInfo {
    int64_t Count = 0;
    uint64_t CurrChunkIdx = 0;
    bool IsSet = false;
    std::string Desc;
    SmallVector<Chunk> Chunks;
  };

  DenseMap<unsigned, CounterInfo> Counters;
  CounterVector RegisteredCounters;

  // Whether we should do DebugCounting at all. DebugCounters aren't
  // thread-safe, so this should always be false in multithreaded scenarios.
  bool Enabled = false;

  bool ShouldPrintCounter = false;

  bool BreakOnLast = false;
};

#define DEBUG_COUNTER(VARNAME, COUNTERNAME, DESC)                              \
  static const unsigned VARNAME =                                              \
      DebugCounter::registerCounter(COUNTERNAME, DESC)

} // namespace llvm
#endif
