// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef BENCHMARK_UTILITIES_H
#define BENCHMARK_UTILITIES_H

#include <cassert>
#include <type_traits>

#include "benchmark/benchmark.h"

namespace UtilitiesInternal {
template <class Container>
auto HaveDataImpl(int) -> decltype((std::declval<Container&>().data(), std::true_type{}));
template <class Container>
auto HaveDataImpl(long) -> std::false_type;
template <class T>
using HasData = decltype(HaveDataImpl<T>(0));
} // namespace UtilitiesInternal

template <class Container, std::enable_if_t<UtilitiesInternal::HasData<Container>::value>* = nullptr>
void DoNotOptimizeData(Container& c) {
  benchmark::DoNotOptimize(c.data());
}

template <class Container, std::enable_if_t<!UtilitiesInternal::HasData<Container>::value>* = nullptr>
void DoNotOptimizeData(Container& c) {
  benchmark::DoNotOptimize(&c);
}

#endif // BENCHMARK_UTILITIES_H
