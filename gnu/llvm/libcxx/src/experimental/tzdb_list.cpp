//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// For information see https://libcxx.llvm.org/DesignDocs/TimeZone.html

#include <chrono>

#include "include/tzdb/tzdb_list_private.h"

_LIBCPP_BEGIN_NAMESPACE_STD

namespace chrono {

_LIBCPP_EXPORTED_FROM_ABI tzdb_list::~tzdb_list() { delete __impl_; }

[[nodiscard]] _LIBCPP_EXPORTED_FROM_ABI const tzdb& tzdb_list::__front() const noexcept { return __impl_->__front(); }

_LIBCPP_EXPORTED_FROM_ABI tzdb_list::const_iterator tzdb_list::__erase_after(const_iterator __p) {
  return __impl_->__erase_after(__p);
}

[[nodiscard]] _LIBCPP_EXPORTED_FROM_ABI tzdb_list::const_iterator tzdb_list::__begin() const noexcept {
  return __impl_->__begin();
}
[[nodiscard]] _LIBCPP_EXPORTED_FROM_ABI tzdb_list::const_iterator tzdb_list::__end() const noexcept {
  return __impl_->__end();
}

[[nodiscard]] _LIBCPP_EXPORTED_FROM_ABI tzdb_list::const_iterator tzdb_list::__cbegin() const noexcept {
  return __impl_->__begin();
}
[[nodiscard]] _LIBCPP_EXPORTED_FROM_ABI tzdb_list::const_iterator tzdb_list::__cend() const noexcept {
  return __impl_->__end();
}

} // namespace chrono

_LIBCPP_END_NAMESPACE_STD
