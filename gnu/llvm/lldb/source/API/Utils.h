//===-- Utils.h -------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_API_UTILS_H
#define LLDB_SOURCE_API_UTILS_H

#include "llvm/ADT/STLExtras.h"
#include <memory>

namespace lldb_private {

template <typename T> std::unique_ptr<T> clone(const std::unique_ptr<T> &src) {
  if (src)
    return std::make_unique<T>(*src);
  return nullptr;
}

template <typename T> std::shared_ptr<T> clone(const std::shared_ptr<T> &src) {
  if (src)
    return std::make_shared<T>(*src);
  return nullptr;
}

} // namespace lldb_private
#endif
