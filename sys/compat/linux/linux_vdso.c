/*-
 * Copyright (c) 2013 Dmitry Chagin
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
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

#include "opt_compat.h"

#if defined(__i386__) || (defined(__amd64__) && defined(COMPAT_LINUX32))
#define	__ELF_WORD_SIZE	32
#else
#define	__ELF_WORD_SIZE	64
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/elf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/rwlock.h>
#include <sys/queue.h>
#include <sys/sysent.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>

#include <compat/linux/linux_vdso.h>

SLIST_HEAD(, linux_vdso_sym) __elfN(linux_vdso_syms) =
    SLIST_HEAD_INITIALIZER(__elfN(linux_vdso_syms));

static int __elfN(symtabindex);
static int __elfN(symstrindex);

static void
__elfN(linux_vdso_lookup)(Elf_Ehdr *, struct linux_vdso_sym *);


void
__elfN(linux_vdso_sym_init)(struct linux_vdso_sym *s)
{

	SLIST_INSERT_HEAD(&__elfN(linux_vdso_syms), s, sym);
}

vm_object_t
__elfN(linux_shared_page_init)(char **mapping)
{
	vm_page_t m;
	vm_object_t obj;
	vm_offset_t addr;

	obj = vm_pager_allocate(OBJT_PHYS, 0, PAGE_SIZE,
	    VM_PROT_DEFAULT, 0, NULL);
	VM_OBJECT_WLOCK(obj);
	m = vm_page_grab(obj, 0, VM_ALLOC_NOBUSY | VM_ALLOC_ZERO);
	m->valid = VM_PAGE_BITS_ALL;
	VM_OBJECT_WUNLOCK(obj);
	addr = kva_alloc(PAGE_SIZE);
	pmap_qenter(addr, &m, 1);
	*mapping = (char *)addr;
	return (obj);
}

void
__elfN(linux_shared_page_fini)(vm_object_t obj)
{

	vm_object_deallocate(obj);
}

void
__elfN(linux_vdso_fixup)(struct sysentvec *sv)
{
	Elf_Ehdr *ehdr;
	Elf_Shdr *shdr;
	int i;

	ehdr = (Elf_Ehdr *) sv->sv_sigcode;

	if (!IS_ELF(*ehdr) ||
	    ehdr->e_ident[EI_CLASS] != ELF_TARG_CLASS ||
	    ehdr->e_ident[EI_DATA] != ELF_TARG_DATA ||
	    ehdr->e_ident[EI_VERSION] != EV_CURRENT ||
	    ehdr->e_shoff == 0 ||
	    ehdr->e_shentsize != sizeof(Elf_Shdr))
		panic("Linux invalid vdso header.\n");

	if (ehdr->e_type != ET_DYN)
		panic("Linux invalid vdso header.\n");

	shdr = (Elf_Shdr *) ((caddr_t)ehdr + ehdr->e_shoff);

	__elfN(symtabindex) = -1;
	__elfN(symstrindex) = -1;
	for (i = 0; i < ehdr->e_shnum; i++) {
		if (shdr[i].sh_size == 0)
			continue;
		if (shdr[i].sh_type == SHT_DYNSYM) {
			__elfN(symtabindex) = i;
			__elfN(symstrindex) = shdr[i].sh_link;
		}
	}

	if (__elfN(symtabindex) == -1 || __elfN(symstrindex) == -1)
		panic("Linux invalid vdso header.\n");

	ehdr->e_ident[EI_OSABI] = ELFOSABI_LINUX;
}

void
__elfN(linux_vdso_reloc)(struct sysentvec *sv)
{
	struct linux_vdso_sym *lsym;
	Elf_Ehdr *ehdr;
	Elf_Phdr *phdr;
	Elf_Shdr *shdr;
	Elf_Dyn *dyn;
	Elf_Sym *sym;
	int i, j, symcnt;

	ehdr = (Elf_Ehdr *) sv->sv_sigcode;

	/* Adjust our so relative to the sigcode_base */
	if (sv->sv_shared_page_base != 0) {
		ehdr->e_entry += sv->sv_shared_page_base;
		phdr = (Elf_Phdr *)((caddr_t)ehdr + ehdr->e_phoff);

		/* phdrs */
		for (i = 0; i < ehdr->e_phnum; i++) {
			phdr[i].p_vaddr += sv->sv_shared_page_base;
			if (phdr[i].p_type != PT_DYNAMIC)
				continue;
			dyn = (Elf_Dyn *)((caddr_t)ehdr + phdr[i].p_offset);
			for(; dyn->d_tag != DT_NULL; dyn++) {
				switch (dyn->d_tag) {
				case DT_PLTGOT:
				case DT_HASH:
				case DT_STRTAB:
				case DT_SYMTAB:
				case DT_RELA:
				case DT_INIT:
				case DT_FINI:
				case DT_REL:
				case DT_DEBUG:
				case DT_JMPREL:
				case DT_VERSYM:
				case DT_VERDEF:
				case DT_VERNEED:
				case DT_ADDRRNGLO ... DT_ADDRRNGHI:
					dyn->d_un.d_ptr += sv->sv_shared_page_base;
					break;
				case DT_ENCODING ... DT_LOOS-1:
				case DT_LOOS ... DT_HIOS:
					if (dyn->d_tag >= DT_ENCODING &&
					    (dyn->d_tag & 1) == 0)
						dyn->d_un.d_ptr += sv->sv_shared_page_base;
					break;
				default:
					break;
				}
			}
		}

		/* sections */
		shdr = (Elf_Shdr *)((caddr_t)ehdr + ehdr->e_shoff);
		for(i = 0; i < ehdr->e_shnum; i++) {
			if (!(shdr[i].sh_flags & SHF_ALLOC))
				continue;
			shdr[i].sh_addr += sv->sv_shared_page_base;
			if (shdr[i].sh_type != SHT_SYMTAB &&
			    shdr[i].sh_type != SHT_DYNSYM)
				continue;

			sym = (Elf_Sym *)((caddr_t)ehdr + shdr[i].sh_offset);
			symcnt = shdr[i].sh_size / sizeof(*sym);

			for(j = 0; j < symcnt; j++, sym++) {
				if (sym->st_shndx == SHN_UNDEF ||
				    sym->st_shndx == SHN_ABS)
					continue;
				sym->st_value += sv->sv_shared_page_base;
			}
		}
	}

	SLIST_FOREACH(lsym, &__elfN(linux_vdso_syms), sym)
		__elfN(linux_vdso_lookup)(ehdr, lsym);
}

static void
__elfN(linux_vdso_lookup)(Elf_Ehdr *ehdr, struct linux_vdso_sym *vsym)
{
	vm_offset_t strtab, symname;
	uint32_t symcnt;
	Elf_Shdr *shdr;
	int i;

	shdr = (Elf_Shdr *) ((caddr_t)ehdr + ehdr->e_shoff);

	strtab = (vm_offset_t)((caddr_t)ehdr +
	    shdr[__elfN(symstrindex)].sh_offset);
	Elf_Sym *sym = (Elf_Sym *)((caddr_t)ehdr +
	    shdr[__elfN(symtabindex)].sh_offset);
	symcnt = shdr[__elfN(symtabindex)].sh_size / sizeof(*sym);

	for (i = 0; i < symcnt; ++i, ++sym) {
		symname = strtab + sym->st_name;
		if (strncmp(vsym->symname, (char *)symname, vsym->size) == 0) {
			*vsym->ptr = (uintptr_t)sym->st_value;
			break;
		}
	}
}
