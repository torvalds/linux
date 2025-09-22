//===- FuzzerRandom.h - Internal header for the Fuzzer ----------*- C++ -* ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// fuzzer::Random
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZER_RANDOM_H
#define LLVM_FUZZER_RANDOM_H

#include <random>

namespace fuzzer {
class Random : public std::minstd_rand {
 public:
  Random(unsigned int seed) : std::minstd_rand(seed) {}
  result_type operator()() { return this->std::minstd_rand::operator()(); }
  template <typename T>
  typename std::enable_if<std::is_integral<T>::value, T>::type Rand() {
    return static_cast<T>(this->operator()());
  }
  size_t RandBool() { return this->operator()() % 2; }
  size_t SkewTowardsLast(size_t n) {
    size_t T = this->operator()(n * n);
    size_t Res = static_cast<size_t>(sqrt(T));
    return Res;
  }
  template <typename T>
  typename std::enable_if<std::is_integral<T>::value, T>::type operator()(T n) {
    return n ? Rand<T>() % n : 0;
  }
  template <typename T>
  typename std::enable_if<std::is_integral<T>::value, T>::type
  operator()(T From, T To) {
    assert(From < To);
    auto RangeSize = static_cast<unsigned long long>(To) -
                     static_cast<unsigned long long>(From) + 1;
    return static_cast<T>(this->operator()(RangeSize) + From);
  }
};

}  // namespace fuzzer

#endif  // LLVM_FUZZER_RANDOM_H
