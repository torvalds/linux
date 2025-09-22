//===- Arrays.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_ARRAYS_H
#define LLD_ARRAYS_H

#include "llvm/ADT/ArrayRef.h"

#include <vector>

namespace lld {
// Split one uint8 array into small pieces of uint8 arrays.
inline std::vector<llvm::ArrayRef<uint8_t>> split(llvm::ArrayRef<uint8_t> arr,
                                                  size_t chunkSize) {
  std::vector<llvm::ArrayRef<uint8_t>> ret;
  while (arr.size() > chunkSize) {
    ret.push_back(arr.take_front(chunkSize));
    arr = arr.drop_front(chunkSize);
  }
  if (!arr.empty())
    ret.push_back(arr);
  return ret;
}

} // namespace lld

#endif
