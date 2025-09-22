//===-- RandomNumberGenerator.cpp - Implement RNG class -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements deterministic random number generation (RNG).
// The current implementation is NOT cryptographically secure as it uses
// the C++11 <random> facilities.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/RandomNumberGenerator.h"

#include "DebugOptions.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/raw_ostream.h"
#ifdef _WIN32
#include "llvm/Support/Windows/WindowsSupport.h"
#else
#include "Unix/Unix.h"
#endif

using namespace llvm;

#define DEBUG_TYPE "rng"
namespace {
struct CreateSeed {
  static void *call() {
    return new cl::opt<uint64_t>(
        "rng-seed", cl::value_desc("seed"), cl::Hidden,
        cl::desc("Seed for the random number generator"), cl::init(0));
  }
};
} // namespace
static ManagedStatic<cl::opt<uint64_t>, CreateSeed> Seed;
void llvm::initRandomSeedOptions() { *Seed; }

RandomNumberGenerator::RandomNumberGenerator(StringRef Salt) {
  LLVM_DEBUG(if (*Seed == 0) dbgs()
             << "Warning! Using unseeded random number generator.\n");

  // Combine seed and salts using std::seed_seq.
  // Data: Seed-low, Seed-high, Salt
  // Note: std::seed_seq can only store 32-bit values, even though we
  // are using a 64-bit RNG. This isn't a problem since the Mersenne
  // twister constructor copies these correctly into its initial state.
  std::vector<uint32_t> Data;
  Data.resize(2 + Salt.size());
  Data[0] = *Seed;
  Data[1] = *Seed >> 32;

  llvm::copy(Salt, Data.begin() + 2);

  std::seed_seq SeedSeq(Data.begin(), Data.end());
  Generator.seed(SeedSeq);
}

RandomNumberGenerator::result_type RandomNumberGenerator::operator()() {
  return Generator();
}

// Get random vector of specified size
std::error_code llvm::getRandomBytes(void *Buffer, size_t Size) {
#ifdef _WIN32
  HCRYPTPROV hProvider;
  if (CryptAcquireContext(&hProvider, 0, 0, PROV_RSA_FULL,
                           CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
    ScopedCryptContext ScopedHandle(hProvider);
    if (CryptGenRandom(hProvider, Size, static_cast<BYTE *>(Buffer)))
      return std::error_code();
  }
  return std::error_code(GetLastError(), std::system_category());
#else
  int Fd = open("/dev/urandom", O_RDONLY);
  if (Fd != -1) {
    std::error_code Ret;
    ssize_t BytesRead = read(Fd, Buffer, Size);
    if (BytesRead == -1)
      Ret = errnoAsErrorCode();
    else if (BytesRead != static_cast<ssize_t>(Size))
      Ret = std::error_code(EIO, std::system_category());
    if (close(Fd) == -1)
      Ret = errnoAsErrorCode();

    return Ret;
  }
  return errnoAsErrorCode();
#endif
}
