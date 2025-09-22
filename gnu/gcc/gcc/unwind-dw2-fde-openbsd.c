/* XXX This file provides a few defines such that we can compile the
   source from unwind-dw2-fde-glibc.c on OpenBSD.  Hopefully we can
   integrate these defines in that file and rename it to something
   like unwind-de2-fde-phdr.c in the up-stream sources.  */

#define ElfW(type) Elf_##type

#define __GLIBC__ 3		/* Fool unwind-dw2-fde-glibc.c.  */
#include "unwind-dw2-fde-glibc.c"
