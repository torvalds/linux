//===-- PosixSpawnResponsible.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_POSIXSPAWNRESPONSIBLE_H
#define LLDB_HOST_POSIXSPAWNRESPONSIBLE_H

#include <spawn.h>

#if __has_include(<responsibility.h>)
#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <responsibility.h>

// Older SDKs  have responsibility.h but not this particular function. Let's
// include the prototype here.
errno_t responsibility_spawnattrs_setdisclaim(posix_spawnattr_t *attrs,
                                              bool disclaim);

#endif

static inline int setup_posix_spawn_responsible_flag(posix_spawnattr_t *attr) {
  if (@available(macOS 10.14, *)) {
#if __has_include(<responsibility.h>)
    static __typeof__(responsibility_spawnattrs_setdisclaim)
        *responsibility_spawnattrs_setdisclaim_ptr;
    static dispatch_once_t pred;
    dispatch_once(&pred, ^{
      responsibility_spawnattrs_setdisclaim_ptr =
          reinterpret_cast<__typeof__(&responsibility_spawnattrs_setdisclaim)>
          (dlsym(RTLD_DEFAULT, "responsibility_spawnattrs_setdisclaim"));
    });
    if (responsibility_spawnattrs_setdisclaim_ptr)
      return responsibility_spawnattrs_setdisclaim_ptr(attr, true);
#endif
  }
  return 0;
}

#endif // LLDB_HOST_POSIXSPAWNRESPONSIBLE_H
