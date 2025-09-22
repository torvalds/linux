//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <valarray>

_LIBCPP_BEGIN_NAMESPACE_STD

// These two symbols are part of the v1 ABI but not part of the >=v2 ABI.
#if _LIBCPP_ABI_VERSION == 1
template _LIBCPP_EXPORTED_FROM_ABI valarray<size_t>::valarray(size_t);
template _LIBCPP_EXPORTED_FROM_ABI valarray<size_t>::~valarray();
#endif

template void valarray<size_t>::resize(size_t, size_t);

void gslice::__init(size_t __start) {
  valarray<size_t> __indices(__size_.size());
  size_t __k = __size_.size() != 0;
  for (size_t __i = 0; __i < __size_.size(); ++__i)
    __k *= __size_[__i];
  __1d_.resize(__k);
  if (__1d_.size()) {
    __k        = 0;
    __1d_[__k] = __start;
    while (true) {
      size_t __i = __indices.size() - 1;
      while (true) {
        if (++__indices[__i] < __size_[__i]) {
          ++__k;
          __1d_[__k] = __1d_[__k - 1] + __stride_[__i];
          for (size_t __j = __i + 1; __j != __indices.size(); ++__j)
            __1d_[__k] -= __stride_[__j] * (__size_[__j] - 1);
          break;
        } else {
          if (__i == 0)
            return;
          __indices[__i--] = 0;
        }
      }
    }
  }
}

_LIBCPP_END_NAMESPACE_STD
