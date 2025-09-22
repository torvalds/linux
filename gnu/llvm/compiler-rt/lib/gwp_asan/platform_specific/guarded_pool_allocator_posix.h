//===-- guarded_pool_allocator_posix.h --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(__unix__)
#ifndef GWP_ASAN_GUARDED_POOL_ALLOCATOR_POSIX_H_
#define GWP_ASAN_GUARDED_POOL_ALLOCATOR_POSIX_H_

namespace gwp_asan {
struct PlatformSpecificMapData {};
} // namespace gwp_asan

#endif // GWP_ASAN_GUARDED_POOL_ALLOCATOR_POSIX_H_
#endif // defined(__unix__)
