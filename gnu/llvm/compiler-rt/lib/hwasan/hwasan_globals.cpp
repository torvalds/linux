//===-- hwasan_globals.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of HWAddressSanitizer.
//
// HWAddressSanitizer globals-specific runtime.
//===----------------------------------------------------------------------===//

#include "hwasan_globals.h"

#include "sanitizer_common/sanitizer_array_ref.h"

namespace __hwasan {

enum { NT_LLVM_HWASAN_GLOBALS = 3 };
struct hwasan_global_note {
  s32 begin_relptr;
  s32 end_relptr;
};

// Check that the given library meets the code model requirements for tagged
// globals. These properties are not checked at link time so they need to be
// checked at runtime.
static void CheckCodeModel(ElfW(Addr) base, const ElfW(Phdr) * phdr,
                           ElfW(Half) phnum) {
  ElfW(Addr) min_addr = -1ull, max_addr = 0;
  for (unsigned i = 0; i != phnum; ++i) {
    if (phdr[i].p_type != PT_LOAD)
      continue;
    ElfW(Addr) lo = base + phdr[i].p_vaddr, hi = lo + phdr[i].p_memsz;
    if (min_addr > lo)
      min_addr = lo;
    if (max_addr < hi)
      max_addr = hi;
  }

  if (max_addr - min_addr > 1ull << 32) {
    Report("FATAL: HWAddressSanitizer: library size exceeds 2^32\n");
    Die();
  }
  if (max_addr > 1ull << 48) {
    Report("FATAL: HWAddressSanitizer: library loaded above address 2^48\n");
    Die();
  }
}

ArrayRef<const hwasan_global> HwasanGlobalsFor(ElfW(Addr) base,
                                               const ElfW(Phdr) * phdr,
                                               ElfW(Half) phnum) {
  // Read the phdrs from this DSO.
  for (unsigned i = 0; i != phnum; ++i) {
    if (phdr[i].p_type != PT_NOTE)
      continue;

    const char *note = reinterpret_cast<const char *>(base + phdr[i].p_vaddr);
    const char *nend = note + phdr[i].p_memsz;

    // Traverse all the notes until we find a HWASan note.
    while (note < nend) {
      auto *nhdr = reinterpret_cast<const ElfW(Nhdr) *>(note);
      const char *name = note + sizeof(ElfW(Nhdr));
      const char *desc = name + RoundUpTo(nhdr->n_namesz, 4);

      // Discard non-HWASan-Globals notes.
      if (nhdr->n_type != NT_LLVM_HWASAN_GLOBALS ||
          internal_strcmp(name, "LLVM") != 0) {
        note = desc + RoundUpTo(nhdr->n_descsz, 4);
        continue;
      }

      // Only libraries with instrumented globals need to be checked against the
      // code model since they use relocations that aren't checked at link time.
      CheckCodeModel(base, phdr, phnum);

      auto *global_note = reinterpret_cast<const hwasan_global_note *>(desc);
      auto *globals_begin = reinterpret_cast<const hwasan_global *>(
          note + global_note->begin_relptr);
      auto *globals_end = reinterpret_cast<const hwasan_global *>(
          note + global_note->end_relptr);

      return {globals_begin, globals_end};
    }
  }

  return {};
}

}  // namespace __hwasan
