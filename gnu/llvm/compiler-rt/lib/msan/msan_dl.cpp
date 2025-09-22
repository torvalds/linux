//===-- msan_dl.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemorySanitizer.
//
// Helper functions for unpoisoning results of dladdr and dladdr1.
//===----------------------------------------------------------------------===//

#include "msan_dl.h"

#include <dlfcn.h>
#include <elf.h>
#include <link.h>

#include "msan_poisoning.h"

namespace __msan {

void UnpoisonDllAddrInfo(void *info) {
  Dl_info *ptr = (Dl_info *)(info);
  __msan_unpoison(ptr, sizeof(*ptr));
  if (ptr->dli_fname)
    __msan_unpoison(ptr->dli_fname, internal_strlen(ptr->dli_fname) + 1);
  if (ptr->dli_sname)
    __msan_unpoison(ptr->dli_sname, internal_strlen(ptr->dli_sname) + 1);
}

#if SANITIZER_GLIBC
void UnpoisonDllAddr1ExtraInfo(void **extra_info, int flags) {
  if (flags == RTLD_DL_SYMENT) {
    __msan_unpoison(extra_info, sizeof(void *));

    ElfW(Sym) *s = *((ElfW(Sym) **)(extra_info));
    __msan_unpoison(s, sizeof(ElfW(Sym)));
  } else if (flags == RTLD_DL_LINKMAP) {
    __msan_unpoison(extra_info, sizeof(void *));

    struct link_map *map = *((struct link_map **)(extra_info));

    // Walk forward
    for (auto *ptr = map; ptr; ptr = ptr->l_next) {
      __msan_unpoison(ptr, sizeof(struct link_map));
      if (ptr->l_name)
        __msan_unpoison(ptr->l_name, internal_strlen(ptr->l_name) + 1);
    }

    if (!map)
      return;

    // Walk backward
    for (auto *ptr = map->l_prev; ptr; ptr = ptr->l_prev) {
      __msan_unpoison(ptr, sizeof(struct link_map));
      if (ptr->l_name)
        __msan_unpoison(ptr->l_name, internal_strlen(ptr->l_name) + 1);
    }
  }
}
#endif

}  // namespace __msan
