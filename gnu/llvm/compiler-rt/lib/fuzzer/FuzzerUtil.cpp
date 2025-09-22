//===- FuzzerUtil.cpp - Misc utils ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Misc utils.
//===----------------------------------------------------------------------===//

#include "FuzzerUtil.h"
#include "FuzzerIO.h"
#include "FuzzerInternal.h"
#include <cassert>
#include <chrono>
#include <cstring>
#include <errno.h>
#include <mutex>
#include <signal.h>
#include <sstream>
#include <stdio.h>
#include <sys/types.h>
#include <thread>

namespace fuzzer {

void PrintHexArray(const uint8_t *Data, size_t Size,
                   const char *PrintAfter) {
  for (size_t i = 0; i < Size; i++)
    Printf("0x%x,", (unsigned)Data[i]);
  Printf("%s", PrintAfter);
}

void Print(const Unit &v, const char *PrintAfter) {
  PrintHexArray(v.data(), v.size(), PrintAfter);
}

void PrintASCIIByte(uint8_t Byte) {
  if (Byte == '\\')
    Printf("\\\\");
  else if (Byte == '"')
    Printf("\\\"");
  else if (Byte >= 32 && Byte < 127)
    Printf("%c", Byte);
  else
    Printf("\\%03o", Byte);
}

void PrintASCII(const uint8_t *Data, size_t Size, const char *PrintAfter) {
  for (size_t i = 0; i < Size; i++)
    PrintASCIIByte(Data[i]);
  Printf("%s", PrintAfter);
}

void PrintASCII(const Unit &U, const char *PrintAfter) {
  PrintASCII(U.data(), U.size(), PrintAfter);
}

bool ToASCII(uint8_t *Data, size_t Size) {
  bool Changed = false;
  for (size_t i = 0; i < Size; i++) {
    uint8_t &X = Data[i];
    auto NewX = X;
    NewX &= 127;
    if (!isspace(NewX) && !isprint(NewX))
      NewX = ' ';
    Changed |= NewX != X;
    X = NewX;
  }
  return Changed;
}

bool IsASCII(const Unit &U) { return IsASCII(U.data(), U.size()); }

bool IsASCII(const uint8_t *Data, size_t Size) {
  for (size_t i = 0; i < Size; i++)
    if (!(isprint(Data[i]) || isspace(Data[i]))) return false;
  return true;
}

bool ParseOneDictionaryEntry(const std::string &Str, Unit *U) {
  U->clear();
  if (Str.empty()) return false;
  size_t L = 0, R = Str.size() - 1;  // We are parsing the range [L,R].
  // Skip spaces from both sides.
  while (L < R && isspace(Str[L])) L++;
  while (R > L && isspace(Str[R])) R--;
  if (R - L < 2) return false;
  // Check the closing "
  if (Str[R] != '"') return false;
  R--;
  // Find the opening "
  while (L < R && Str[L] != '"') L++;
  if (L >= R) return false;
  assert(Str[L] == '\"');
  L++;
  assert(L <= R);
  for (size_t Pos = L; Pos <= R; Pos++) {
    uint8_t V = (uint8_t)Str[Pos];
    if (!isprint(V) && !isspace(V)) return false;
    if (V =='\\') {
      // Handle '\\'
      if (Pos + 1 <= R && (Str[Pos + 1] == '\\' || Str[Pos + 1] == '"')) {
        U->push_back(Str[Pos + 1]);
        Pos++;
        continue;
      }
      // Handle '\xAB'
      if (Pos + 3 <= R && Str[Pos + 1] == 'x'
           && isxdigit(Str[Pos + 2]) && isxdigit(Str[Pos + 3])) {
        char Hex[] = "0xAA";
        Hex[2] = Str[Pos + 2];
        Hex[3] = Str[Pos + 3];
        U->push_back(static_cast<uint8_t>(strtol(Hex, nullptr, 16)));
        Pos += 3;
        continue;
      }
      return false;  // Invalid escape.
    } else {
      // Any other character.
      U->push_back(V);
    }
  }
  return true;
}

bool ParseDictionaryFile(const std::string &Text, std::vector<Unit> *Units) {
  if (Text.empty()) {
    Printf("ParseDictionaryFile: file does not exist or is empty\n");
    return false;
  }
  std::istringstream ISS(Text);
  Units->clear();
  Unit U;
  int LineNo = 0;
  std::string S;
  while (std::getline(ISS, S, '\n')) {
    LineNo++;
    size_t Pos = 0;
    while (Pos < S.size() && isspace(S[Pos])) Pos++;  // Skip spaces.
    if (Pos == S.size()) continue;  // Empty line.
    if (S[Pos] == '#') continue;  // Comment line.
    if (ParseOneDictionaryEntry(S, &U)) {
      Units->push_back(U);
    } else {
      Printf("ParseDictionaryFile: error in line %d\n\t\t%s\n", LineNo,
             S.c_str());
      return false;
    }
  }
  return true;
}

// Code duplicated (and tested) in llvm/include/llvm/Support/Base64.h
std::string Base64(const Unit &U) {
  static const char Table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                              "abcdefghijklmnopqrstuvwxyz"
                              "0123456789+/";
  std::string Buffer;
  Buffer.resize(((U.size() + 2) / 3) * 4);

  size_t i = 0, j = 0;
  for (size_t n = U.size() / 3 * 3; i < n; i += 3, j += 4) {
    uint32_t x = ((unsigned char)U[i] << 16) | ((unsigned char)U[i + 1] << 8) |
                 (unsigned char)U[i + 2];
    Buffer[j + 0] = Table[(x >> 18) & 63];
    Buffer[j + 1] = Table[(x >> 12) & 63];
    Buffer[j + 2] = Table[(x >> 6) & 63];
    Buffer[j + 3] = Table[x & 63];
  }
  if (i + 1 == U.size()) {
    uint32_t x = ((unsigned char)U[i] << 16);
    Buffer[j + 0] = Table[(x >> 18) & 63];
    Buffer[j + 1] = Table[(x >> 12) & 63];
    Buffer[j + 2] = '=';
    Buffer[j + 3] = '=';
  } else if (i + 2 == U.size()) {
    uint32_t x = ((unsigned char)U[i] << 16) | ((unsigned char)U[i + 1] << 8);
    Buffer[j + 0] = Table[(x >> 18) & 63];
    Buffer[j + 1] = Table[(x >> 12) & 63];
    Buffer[j + 2] = Table[(x >> 6) & 63];
    Buffer[j + 3] = '=';
  }
  return Buffer;
}

static std::mutex SymbolizeMutex;

std::string DescribePC(const char *SymbolizedFMT, uintptr_t PC) {
  std::unique_lock<std::mutex> l(SymbolizeMutex, std::try_to_lock);
  if (!EF->__sanitizer_symbolize_pc || !l.owns_lock())
    return "<can not symbolize>";
  char PcDescr[1024] = {};
  EF->__sanitizer_symbolize_pc(reinterpret_cast<void*>(PC),
                               SymbolizedFMT, PcDescr, sizeof(PcDescr));
  PcDescr[sizeof(PcDescr) - 1] = 0;  // Just in case.
  return PcDescr;
}

void PrintPC(const char *SymbolizedFMT, const char *FallbackFMT, uintptr_t PC) {
  if (EF->__sanitizer_symbolize_pc)
    Printf("%s", DescribePC(SymbolizedFMT, PC).c_str());
  else
    Printf(FallbackFMT, PC);
}

void PrintStackTrace() {
  std::unique_lock<std::mutex> l(SymbolizeMutex, std::try_to_lock);
  if (EF->__sanitizer_print_stack_trace && l.owns_lock())
    EF->__sanitizer_print_stack_trace();
}

void PrintMemoryProfile() {
  std::unique_lock<std::mutex> l(SymbolizeMutex, std::try_to_lock);
  if (EF->__sanitizer_print_memory_profile && l.owns_lock())
    EF->__sanitizer_print_memory_profile(95, 8);
}

unsigned NumberOfCpuCores() {
  unsigned N = std::thread::hardware_concurrency();
  if (!N) {
    Printf("WARNING: std::thread::hardware_concurrency not well defined for "
           "your platform. Assuming CPU count of 1.\n");
    N = 1;
  }
  return N;
}

uint64_t SimpleFastHash(const void *Data, size_t Size, uint64_t Initial) {
  uint64_t Res = Initial;
  const uint8_t *Bytes = static_cast<const uint8_t *>(Data);
  for (size_t i = 0; i < Size; i++)
    Res = Res * 11 + Bytes[i];
  return Res;
}

}  // namespace fuzzer
