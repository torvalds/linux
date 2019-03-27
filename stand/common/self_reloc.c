/*-
 * Copyright (c) 2008-2010 Rui Paulo <rpaulo@FreeBSD.org>
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
#include <elf.h>
#include <bootstrap.h>

#if defined(__aarch64__) || defined(__amd64__)
#define	ElfW_Rel	Elf64_Rela
#define	ElfW_Dyn	Elf64_Dyn
#define	ELFW_R_TYPE	ELF64_R_TYPE
#define	ELF_RELA
#elif defined(__arm__) || defined(__i386__)
#define	ElfW_Rel	Elf32_Rel
#define	ElfW_Dyn	Elf32_Dyn
#define	ELFW_R_TYPE	ELF32_R_TYPE
#else
#error architecture not supported
#endif
#if defined(__aarch64__)
#define	RELOC_TYPE_NONE		R_AARCH64_NONE
#define	RELOC_TYPE_RELATIVE	R_AARCH64_RELATIVE
#elif defined(__amd64__)
#define	RELOC_TYPE_NONE		R_X86_64_NONE
#define	RELOC_TYPE_RELATIVE	R_X86_64_RELATIVE
#elif defined(__arm__)
#define	RELOC_TYPE_NONE		R_ARM_NONE
#define	RELOC_TYPE_RELATIVE	R_ARM_RELATIVE
#elif defined(__i386__)
#define	RELOC_TYPE_NONE		R_386_NONE
#define	RELOC_TYPE_RELATIVE	R_386_RELATIVE
#endif

void self_reloc(Elf_Addr baseaddr, ElfW_Dyn *dynamic);

/*
 * A simple elf relocator.
 */
void
self_reloc(Elf_Addr baseaddr, ElfW_Dyn *dynamic)
{
	Elf_Word relsz, relent;
	Elf_Addr *newaddr;
	ElfW_Rel *rel;
	ElfW_Dyn *dynp;

	/*
	 * Find the relocation address, its size and the relocation entry.
	 */
	relsz = 0;
	relent = 0;
	for (dynp = dynamic; dynp->d_tag != DT_NULL; dynp++) {
		switch (dynp->d_tag) {
		case DT_REL:
		case DT_RELA:
			rel = (ElfW_Rel *)(dynp->d_un.d_ptr + baseaddr);
			break;
		case DT_RELSZ:
		case DT_RELASZ:
			relsz = dynp->d_un.d_val;
			break;
		case DT_RELENT:
		case DT_RELAENT:
			relent = dynp->d_un.d_val;
			break;
		default:
			break;
		}
	}

	/*
	 * Perform the actual relocation. We rely on the object having been
	 * linked at 0, so that the difference between the load and link
	 * address is the same as the load address.
	 */
	for (; relsz > 0; relsz -= relent) {
		switch (ELFW_R_TYPE(rel->r_info)) {
		case RELOC_TYPE_NONE:
			/* No relocation needs be performed. */
			break;

		case RELOC_TYPE_RELATIVE:
			newaddr = (Elf_Addr *)(rel->r_offset + baseaddr);
#ifdef ELF_RELA
			/* Addend relative to the base address. */
			*newaddr = baseaddr + rel->r_addend;
#else
			/* Address relative to the base address. */
			*newaddr += baseaddr;
#endif
			break;
		default:
			/* XXX: do we need other relocations ? */
			break;
		}
		rel = (ElfW_Rel *)(void *)((caddr_t) rel + relent);
	}
}
