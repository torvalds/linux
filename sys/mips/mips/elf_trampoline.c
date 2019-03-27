/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Olivier Houchard.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <machine/asm.h>
#include <sys/param.h>

#if ELFSIZE == 64
#include <sys/elf64.h>
#else
#include <sys/elf32.h>
#endif

/*
 * Since we are compiled outside of the normal kernel build process, we
 * need to include opt_global.h manually.
 */
#include "opt_global.h"

#include <sys/inflate.h>
#include <machine/elf.h>
#include <machine/cpufunc.h>
#include <machine/stdarg.h>

#ifndef KERNNAME
#error Kernel name not provided
#endif

extern char kernel_start[];
extern char kernel_end[];

static __inline void *
memcpy(void *dst, const void *src, size_t len)
{
	const char *s = src;
    	char *d = dst;

	while (len) {
		if (0 && len >= 4 && !((vm_offset_t)d & 3) &&
		    !((vm_offset_t)s & 3)) {
			*(uint32_t *)d = *(uint32_t *)s;
			s += 4;
			d += 4;
			len -= 4;
		} else {
			*d++ = *s++;
			len--;
		}
	}
	return (dst);
}

static __inline void
bzero(void *addr, size_t count)
{
	char *tmp = (char *)addr;

	while (count > 0) {
		if (count >= 4 && !((vm_offset_t)tmp & 3)) {
			*(uint32_t *)tmp = 0;
			tmp += 4;
			count -= 4;
		} else {
			*tmp = 0;
			tmp++;
			count--;
		}
	}
}

/*
 * Convert number to pointer, truncate on 64->32 case, sign extend
 * in 32->64 case
 */
#define	mkptr(x)	((void *)(intptr_t)(int)(x))

/*
 * Relocate PT_LOAD segements of kernel ELF image to their respective
 * virtual addresses and return entry point
 */
void *
load_kernel(void * kstart)
{
#if ELFSIZE == 64
	Elf64_Ehdr *eh;
	Elf64_Phdr phdr[64] /* XXX */;
	Elf64_Shdr shdr[64] /* XXX */;
#else
	Elf32_Ehdr *eh;
	Elf32_Phdr phdr[64] /* XXX */;
	Elf32_Shdr shdr[64] /* XXX */;
#endif
	int i, j;
	void *entry_point;
	vm_offset_t loadend = 0;
	intptr_t lastaddr;
	int symtabindex = -1;
	int symstrindex = -1;
	Elf_Size tmp;
	
#if ELFSIZE == 64
	eh = (Elf64_Ehdr *)kstart;
#else
	eh = (Elf32_Ehdr *)kstart;
#endif
	entry_point = mkptr(eh->e_entry);
	memcpy(phdr, (void *)(kstart + eh->e_phoff),
	    eh->e_phnum * sizeof(phdr[0]));

	memcpy(shdr, (void *)(kstart + eh->e_shoff),
	    sizeof(*shdr) * eh->e_shnum);

	if (eh->e_shnum * eh->e_shentsize != 0 && eh->e_shoff != 0) {
		for (i = 0; i < eh->e_shnum; i++) {
			if (shdr[i].sh_type == SHT_SYMTAB) {
				/*
				 * XXX: check if .symtab is in PT_LOAD?
				 */
				if (shdr[i].sh_offset != 0 && 
				    shdr[i].sh_size != 0) {
					symtabindex = i;
					symstrindex = shdr[i].sh_link;
				}
			}
		}
	}

	/*
	 * Copy loadable segments
	 */
	for (i = 0; i < eh->e_phnum; i++) {
		volatile char c;

		if (phdr[i].p_type != PT_LOAD)
			continue;
		
		memcpy(mkptr(phdr[i].p_vaddr),
		    (void*)(kstart + phdr[i].p_offset), phdr[i].p_filesz);

		/* Clean space from oversized segments, eg: bss. */
		if (phdr[i].p_filesz < phdr[i].p_memsz)
			bzero(mkptr(phdr[i].p_vaddr + phdr[i].p_filesz),
			    phdr[i].p_memsz - phdr[i].p_filesz);

		if (loadend < phdr[i].p_vaddr + phdr[i].p_memsz)
			loadend = phdr[i].p_vaddr + phdr[i].p_memsz;
	}

	/* Now grab the symbol tables. */
	lastaddr = (intptr_t)(int)loadend;
	if (symtabindex >= 0 && symstrindex >= 0) {
		tmp = SYMTAB_MAGIC;
		memcpy((void *)lastaddr, &tmp, sizeof(tmp));
		lastaddr += sizeof(Elf_Size);
		tmp = shdr[symtabindex].sh_size +
		    shdr[symstrindex].sh_size + 2*sizeof(Elf_Size);
		memcpy((void *)lastaddr, &tmp, sizeof(tmp));
		lastaddr += sizeof(Elf_Size);
		/* .symtab size */
		tmp = shdr[symtabindex].sh_size;
		memcpy((void *)lastaddr, &tmp, sizeof(tmp));
		lastaddr += sizeof(shdr[symtabindex].sh_size);
		/* .symtab data */
		memcpy((void*)lastaddr,
		    shdr[symtabindex].sh_offset + kstart,
		    shdr[symtabindex].sh_size);
		lastaddr += shdr[symtabindex].sh_size;

		/* .strtab size */
		tmp = shdr[symstrindex].sh_size;
		memcpy((void *)lastaddr, &tmp, sizeof(tmp));
		lastaddr += sizeof(shdr[symstrindex].sh_size);

		/* .strtab data */
		memcpy((void*)lastaddr,
		    shdr[symstrindex].sh_offset + kstart,
		    shdr[symstrindex].sh_size);
	} else {
		/* Do not take any chances */
		tmp = 0;
		memcpy((void *)lastaddr, &tmp, sizeof(tmp));
	}

	return entry_point;
}

void
_startC(register_t a0, register_t a1, register_t a2, register_t a3)
{
	unsigned int * code;
	int i;
	void (*entry_point)(register_t, register_t, register_t, register_t);

	/* 
	 * Relocate segment to the predefined memory location
	 * Most likely it will be KSEG0/KSEG1 address
	 */
	entry_point = load_kernel(kernel_start);

	/* Pass saved registers to original _start */
	entry_point(a0, a1, a2, a3);
}
