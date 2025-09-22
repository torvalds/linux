//===- FuzzerUtil.h - Internal header for the Fuzzer Utils ------*- C++ -* ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Util functions.
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZER_UTIL_H
#define LLVM_FUZZER_UTIL_H

#include "FuzzerBuiltins.h"
#include "FuzzerBuiltinsMsvc.h"
#include "FuzzerCommand.h"
#include "FuzzerDefs.h"

namespace fuzzer {

void PrintHexArray(const Unit &U, const char *PrintAfter = "");

void PrintHexArray(const uint8_t *Data, size_t Size,
                   const char *PrintAfter = "");

void PrintASCII(const uint8_t *Data, size_t Size, const char *PrintAfter = "");

void PrintASCII(const Unit &U, const char *PrintAfter = "");

// Changes U to contain only ASCII (isprint+isspace) characters.
// Returns true iff U has been changed.
bool ToASCII(uint8_t *Data, size_t Size);

bool IsASCII(const Unit &U);

bool IsASCII(const uint8_t *Data, size_t Size);

std::string Base64(const Unit &U);

void PrintPC(const char *SymbolizedFMT, const char *FallbackFMT, uintptr_t PC);

std::string DescribePC(const char *SymbolizedFMT, uintptr_t PC);

void PrintStackTrace();

void PrintMemoryProfile();

unsigned NumberOfCpuCores();

// Platform specific functions.
void SetSignalHandler(const FuzzingOptions& Options);

void SleepSeconds(int Seconds);

unsigned long GetPid();

size_t GetPeakRSSMb();

int ExecuteCommand(const Command &Cmd);
bool ExecuteCommand(const Command &Cmd, std::string *CmdOutput);

void SetThreadName(std::thread &thread, const std::string &name);

// Fuchsia does not have popen/pclose.
FILE *OpenProcessPipe(const char *Command, const char *Mode);
int CloseProcessPipe(FILE *F);

const void *SearchMemory(const void *haystack, size_t haystacklen,
                         const void *needle, size_t needlelen);

std::string CloneArgsWithoutX(const std::vector<std::string> &Args,
                              const char *X1, const char *X2);

inline std::string CloneArgsWithoutX(const std::vector<std::string> &Args,
                                     const char *X) {
  return CloneArgsWithoutX(Args, X, X);
}

inline std::pair<std::string, std::string> SplitBefore(std::string X,
                                                       std::string S) {
  auto Pos = S.find(X);
  if (Pos == std::string::npos)
    return std::make_pair(S, "");
  return std::make_pair(S.substr(0, Pos), S.substr(Pos));
}

void DiscardOutput(int Fd);

std::string DisassembleCmd(const std::string &FileName);

std::string SearchRegexCmd(const std::string &Regex);

uint64_t SimpleFastHash(const void *Data, size_t Size, uint64_t Initial = 0);

inline size_t Log(size_t X) {
  return static_cast<size_t>((sizeof(unsigned long long) * 8) - Clzll(X) - 1);
}

size_t PageSize();

inline uint8_t *RoundUpByPage(uint8_t *P) {
  uintptr_t X = reinterpret_cast<uintptr_t>(P);
  size_t Mask = PageSize() - 1;
  X = (X + Mask) & ~Mask;
  return reinterpret_cast<uint8_t *>(X);
}
inline uint8_t *RoundDownByPage(uint8_t *P) {
  uintptr_t X = reinterpret_cast<uintptr_t>(P);
  size_t Mask = PageSize() - 1;
  X = X & ~Mask;
  return reinterpret_cast<uint8_t *>(X);
}

#if __BYTE_ORDER == __LITTLE_ENDIAN
template <typename T> T HostToLE(T X) { return X; }
#else
template <typename T> T HostToLE(T X) { return Bswap(X); }
#endif

}  // namespace fuzzer

#endif  // LLVM_FUZZER_UTIL_H
