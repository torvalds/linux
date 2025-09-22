//===-- sanitizer_solaris.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of Sanitizer runtime. It contains Solaris-specific
// definitions.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_SOLARIS_H
#define SANITIZER_SOLARIS_H

#include "sanitizer_internal_defs.h"

#if SANITIZER_SOLARIS

#include <link.h>

namespace __sanitizer {

// Beginning of declaration from OpenSolaris/Illumos
// $SRC/cmd/sgs/include/rtld.h.
struct Rt_map {
  Link_map rt_public;
  const char *rt_pathname;
  ulong_t rt_padstart;
  ulong_t rt_padimlen;
  ulong_t rt_msize;
  uint_t rt_flags;
  uint_t rt_flags1;
  ulong_t rt_tlsmodid;
};

// Structure matching the Solaris 11.4 struct dl_phdr_info used to determine
// presence of dlpi_tls_modid field at runtime.  Cf. Solaris 11.4
// dl_iterate_phdr(3C), Example 2.
struct dl_phdr_info_test {
  ElfW(Addr) dlpi_addr;
  const char *dlpi_name;
  const ElfW(Phdr) * dlpi_phdr;
  ElfW(Half) dlpi_phnum;
  u_longlong_t dlpi_adds;
  u_longlong_t dlpi_subs;
  size_t dlpi_tls_modid;
  void *dlpi_tls_data;
};

}  // namespace __sanitizer

#endif  // SANITIZER_SOLARIS

#endif  // SANITIZER_SOLARIS_H
