//===-- guarded_pool_allocator_fuchsia.h ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(__Fuchsia__)
#ifndef GWP_ASAN_GUARDED_POOL_ALLOCATOR_FUCHSIA_H_
#define GWP_ASAN_GUARDED_POOL_ALLOCATOR_FUCHSIA_H_

#include <zircon/types.h>

namespace gwp_asan {
struct PlatformSpecificMapData {
  zx_handle_t Vmar;
};
} // namespace gwp_asan

#endif // GWP_ASAN_GUARDED_POOL_ALLOCATOR_FUCHSIA_H_
#endif // defined(__Fuchsia__)
