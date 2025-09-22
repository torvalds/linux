//===-- MmapUtils.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains compatibility-related preprocessor directives related
// to mmap.
//
//===----------------------------------------------------------------------===//

#ifdef __linux__
#include <sys/mman.h>
#include <sys/syscall.h>

// Before kernel 4.17, Linux did not support MAP_FIXED_NOREPLACE, so if it is
// not available, simplfy define it as MAP_FIXED which performs the same
// function but does not guarantee existing mappings won't get clobbered.
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE MAP_FIXED
#endif

// Some 32-bit architectures don't have mmap and define mmap2 instead. The only
// difference between the two syscalls is that mmap2's offset parameter is in
// terms 4096 byte offsets rather than individual bytes, so for our purposes
// they are effectively the same as all ofsets here are set to 0.
#if defined(SYS_mmap2) && !defined(SYS_mmap)
#define SYS_mmap SYS_mmap2
#endif
#endif // __linux__
