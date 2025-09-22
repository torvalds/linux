//===- FuzzerDefs.h - Internal header for the Fuzzer ------------*- C++ -* ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Basic definitions.
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZER_DEFS_H
#define LLVM_FUZZER_DEFS_H

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <vector>


namespace fuzzer {

template <class T> T Min(T a, T b) { return a < b ? a : b; }
template <class T> T Max(T a, T b) { return a > b ? a : b; }

class Random;
class Dictionary;
class DictionaryEntry;
class MutationDispatcher;
struct FuzzingOptions;
class InputCorpus;
struct InputInfo;
struct ExternalFunctions;

// Global interface to functions that may or may not be available.
extern ExternalFunctions *EF;

typedef std::vector<uint8_t> Unit;
typedef std::vector<Unit> UnitVector;
typedef int (*UserCallback)(const uint8_t *Data, size_t Size);

int FuzzerDriver(int *argc, char ***argv, UserCallback Callback);

uint8_t *ExtraCountersBegin();
uint8_t *ExtraCountersEnd();
void ClearExtraCounters();

extern bool RunningUserCallback;

}  // namespace fuzzer

#endif  // LLVM_FUZZER_DEFS_H
