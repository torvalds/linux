/*-
 * Copyright (c) 2014 Ian Lepore <ian@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <machine/elf.h>
#include <link.h>
#include <stddef.h>

/*
 * ARM EABI unwind helper.
 *
 * This finds the exidx section address and size associated with a given code
 * address.  There are separate implementations for static and dynamic code.
 *
 * GCC expects this function to exist as __gnu_Unwind_Find_exidx(), clang and
 * BSD tools expect it to be dl_unwind_find_exidx().  Both have the same API, so
 * we set up an alias for GCC.
 */
__strong_reference(dl_unwind_find_exidx, __gnu_Unwind_Find_exidx);

/*
 * Each entry in the exidx section is a pair of 32-bit words.  We don't
 * interpret the contents of the entries here; this typedef is just a local
 * convenience for using sizeof() and doing pointer math.
 */
typedef struct exidx_entry {
	uint32_t data[2];
} exidx_entry;

#ifdef __PIC__

/*
 * Unwind helper for dynamically linked code.
 *
 * This finds the shared object that contains the given address, and returns the
 * address of the exidx section in that shared object along with the number of
 * entries in that section, or NULL if it wasn't found.
 */
void *
dl_unwind_find_exidx(const void *pc, int *pcount)
{
	const Elf_Phdr *hdr;
	struct dl_phdr_info info;
	int i;

	if (_rtld_addr_phdr(pc, &info)) {
		hdr = info.dlpi_phdr;
		for (i = 0; i < info.dlpi_phnum; i++, hdr++) {
			if (hdr->p_type == PT_ARM_EXIDX) {
				*pcount = hdr->p_memsz / sizeof(exidx_entry);
				return ((void *)(info.dlpi_addr + hdr->p_vaddr));
			}
		}
	}
	return (NULL);
}

#else	/* !__PIC__ */

/*
 * Unwind helper for statically linked code.
 *
 * In a statically linked program, the linker populates a pair of symbols with
 * the addresses of the start and end of the exidx table, so returning the
 * address and count of elements is pretty straighforward.
 */
void *
dl_unwind_find_exidx(const void *pc, int *pcount)
{
	extern struct exidx_entry __exidx_start;
	extern struct exidx_entry __exidx_end;

	*pcount = (int)(&__exidx_end - &__exidx_start);
	return (&__exidx_start);
}

#endif	/* __PIC__ */

