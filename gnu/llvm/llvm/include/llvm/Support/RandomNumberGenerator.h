//==- llvm/Support/RandomNumberGenerator.h - RNG for diversity ---*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines an abstraction for deterministic random number
// generation (RNG).  Note that the current implementation is not
// cryptographically secure as it uses the C++11 <random> facilities.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_RANDOMNUMBERGENERATOR_H_
#define LLVM_SUPPORT_RANDOMNUMBERGENERATOR_H_

#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h" // Needed for uint64_t on Windows.
#include <random>
#include <system_error>

namespace llvm {
class StringRef;

/// A random number generator.
///
/// Instances of this class should not be shared across threads. The
/// seed should be set by passing the -rng-seed=<uint64> option. Use
/// Module::createRNG to create a new RNG instance for use with that
/// module.
class RandomNumberGenerator {

  // 64-bit Mersenne Twister by Matsumoto and Nishimura, 2000
  // http://en.cppreference.com/w/cpp/numeric/random/mersenne_twister_engine
  // This RNG is deterministically portable across C++11
  // implementations.
  using generator_type = std::mt19937_64;

public:
  using result_type = generator_type::result_type;

  /// Returns a random number in the range [0, Max).
  result_type operator()();

  static constexpr result_type min() { return generator_type::min(); }
  static constexpr result_type max() { return generator_type::max(); }

private:
  /// Seeds and salts the underlying RNG engine.
  ///
  /// This constructor should not be used directly. Instead use
  /// Module::createRNG to create a new RNG salted with the Module ID.
  RandomNumberGenerator(StringRef Salt);

  generator_type Generator;

  // Noncopyable.
  RandomNumberGenerator(const RandomNumberGenerator &other) = delete;
  RandomNumberGenerator &operator=(const RandomNumberGenerator &other) = delete;

  friend class Module;
};

// Get random vector of specified size
std::error_code getRandomBytes(void *Buffer, size_t Size);
}

#endif
