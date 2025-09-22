//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <bit>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class Comp, class RandomAccessIterator>
void __sort(RandomAccessIterator first, RandomAccessIterator last, Comp comp) {
  auto depth_limit = 2 * std::__bit_log2(static_cast<size_t>(last - first));

  // Only use bitset partitioning for arithmetic types.  We should also check
  // that the default comparator is in use so that we are sure that there are no
  // branches in the comparator.
  std::__introsort<_ClassicAlgPolicy,
                   ranges::less,
                   RandomAccessIterator,
                   __use_branchless_sort<ranges::less, RandomAccessIterator>::value>(
      first, last, ranges::less{}, depth_limit);
}

// clang-format off
template void __sort<__less<char>&, char*>(char*, char*, __less<char>&);
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
template void __sort<__less<wchar_t>&, wchar_t*>(wchar_t*, wchar_t*, __less<wchar_t>&);
#endif
template void __sort<__less<signed char>&, signed char*>(signed char*, signed char*, __less<signed char>&);
template void __sort<__less<unsigned char>&, unsigned char*>(unsigned char*, unsigned char*, __less<unsigned char>&);
template void __sort<__less<short>&, short*>(short*, short*, __less<short>&);
template void __sort<__less<unsigned short>&, unsigned short*>(unsigned short*, unsigned short*, __less<unsigned short>&);
template void __sort<__less<int>&, int*>(int*, int*, __less<int>&);
template void __sort<__less<unsigned>&, unsigned*>(unsigned*, unsigned*, __less<unsigned>&);
template void __sort<__less<long>&, long*>(long*, long*, __less<long>&);
template void __sort<__less<unsigned long>&, unsigned long*>(unsigned long*, unsigned long*, __less<unsigned long>&);
template void __sort<__less<long long>&, long long*>(long long*, long long*, __less<long long>&);
template void __sort<__less<unsigned long long>&, unsigned long long*>(unsigned long long*, unsigned long long*, __less<unsigned long long>&);
template void __sort<__less<float>&, float*>(float*, float*, __less<float>&);
template void __sort<__less<double>&, double*>(double*, double*, __less<double>&);
template void __sort<__less<long double>&, long double*>(long double*, long double*, __less<long double>&);
// clang-format on

_LIBCPP_END_NAMESPACE_STD
