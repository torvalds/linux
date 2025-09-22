//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// For information see https://libcxx.llvm.org/DesignDocs/TimeZone.html

#ifndef _LIBCPP_SRC_INCLUDE_TZDB_TZ_PRIVATE_H
#define _LIBCPP_SRC_INCLUDE_TZDB_TZ_PRIVATE_H

#include <chrono>

#include "types_private.h"

_LIBCPP_BEGIN_NAMESPACE_STD

namespace chrono {

void __init_tzdb(tzdb& __tzdb, __tz::__rules_storage_type& __rules);

} // namespace chrono

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_SRC_INCLUDE_TZDB_TZ_PRIVATE_H
