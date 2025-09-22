/*	$OpenBSD: boot_md.h,v 1.5 2023/11/18 16:26:16 deraadt Exp $ */

/*
 * Copyright (c) 1998 Per Fogelstrom, Opsycon AB
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/*
 * IMPORTANT: any functions below are NOT protected by SSP.  Please
 * do not add anything except what is required to reach GOT with
 * an adjustment.
 */

#define	_DYN_LOADER

#include <sys/exec_elf.h>
#include <sys/mman.h>

#include <machine/reloc.h>

__dead
void _csu_abort(void);

#include "archdep.h"

/*
 * Use the internal, hidden name for any syscalls we need, to avoid
 * accidental override by application code
 */
#define REDIRECT_SYSCALL(x)	typeof(x) x asm("_libc_"#x) __dso_hidden
REDIRECT_SYSCALL(mprotect);
REDIRECT_SYSCALL(mimmutable);

typedef	Elf_RelA	RELOC_TYPE;

static void *relro_addr;
static size_t relro_size;
#define RCRT0_RELRO()							\
	do {								\
		if (relro_addr != NULL && relro_size != 0)		\
			mprotect(relro_addr, relro_size, PROT_READ);	\
			mimmutable(relro_addr, relro_size);		\
	} while (0)

/*
 * Local decls.
 */
void _dl_boot_bind(const long, long *, Elf_Dyn *);

void
_dl_boot_bind(const long sp, long *dl_data, Elf_Dyn *dynp)
{
	AuxInfo			*auxstack;
	long			*stack;
	int			n, argc;
	char			**argv, **envp;
	long			loff;
	Elf_Phdr		*phdp;
	const RELOC_TYPE	*rend;
	const RELOC_TYPE	*dt_reloc;	/* DT_RELA */
	Elf_Addr		dt_relocsz;	/* DT_RELASZ */
	Elf_Addr		dt_pltgot;
	Elf_Addr		dt_pltrelsz;
	const Elf_Sym		*dt_symtab;
	const RELOC_TYPE	*dt_jmprel;
	Elf_Addr		i;

	/*
	 * Scan argument and environment vectors. Find dynamic
	 * data vector put after them.
	 */
	stack = (long *)sp;
	argc = *stack++;
	argv = (char **)stack;
	envp = &argv[argc + 1];
	stack = (long *)envp;
	while (*stack++ != 0L)
		;

	/*
	 * Zero out dl_data.
	 */
	for (n = 0; n <= AUX_entry; n++)
		dl_data[n] = 0;

	/*
	 * Dig out auxiliary data set up by exec call. Move all known
	 * tags to an indexed local table for easy access.
	 */
	for (auxstack = (AuxInfo *)stack; auxstack->au_id != AUX_null;
	    auxstack++) {
		if (auxstack->au_id > AUX_entry)
			continue;
		dl_data[auxstack->au_id] = auxstack->au_v;
	}
	loff = dl_data[AUX_base];	/* XXX assumes ld.so is linked at 0x0 */

	/*
	 * Scan the DYNAMIC section for the items we need
	 */
	dt_pltrelsz = dt_relocsz = dt_pltgot = 0;
	dt_jmprel = dt_reloc = NULL;
	dt_symtab = NULL;
	while (dynp->d_tag != DT_NULL) {
		/* first the tags that are pointers to be relocated */
		if (dynp->d_tag == DT_PLTGOT)
			dt_pltgot = dynp->d_un.d_ptr + loff;
		else if (dynp->d_tag == DT_SYMTAB)
			dt_symtab = (void *)(dynp->d_un.d_ptr + loff);
		else if (dynp->d_tag == RELOC_TAG)	/* DT_{RELA,REL} */
			dt_reloc = (void *)(dynp->d_un.d_ptr + loff);
		else if (dynp->d_tag == DT_JMPREL)
			dt_jmprel = (void *)(dynp->d_un.d_ptr + loff);

		/* Now for the tags that are just sizes or counts */
		else if (dynp->d_tag == DT_PLTRELSZ)
			dt_pltrelsz = dynp->d_un.d_val;
		else if (dynp->d_tag == RELOC_TAG+1)	/* DT_{RELA,REL}SZ */
			dt_relocsz = dynp->d_un.d_val;
		dynp++;
	}

	rend = (RELOC_TYPE *)((char *)dt_jmprel + dt_pltrelsz);
	for (; dt_jmprel < rend; dt_jmprel++) {
		const Elf_Sym *sp;

		sp = dt_symtab + ELF_R_SYM(dt_jmprel->r_info);
		if (!ELF_R_SYM(dt_jmprel->r_info) || sp->st_value != 0) {
			Elf_Addr *ra = (Elf_Addr *)(dt_jmprel->r_offset + loff);
			RELOC_JMPREL(dt_jmprel, sp, ra, loff, dt_pltgot);
		}
	}

	rend = (RELOC_TYPE *)((char *)dt_reloc + dt_relocsz);
	for (; dt_reloc < rend; dt_reloc++) {
		Elf_Addr *ra;
		const Elf_Sym *sp;

		sp = dt_symtab + ELF_R_SYM(dt_reloc->r_info);
		if (!ELF_R_SYM(dt_reloc->r_info) || sp->st_value != 0) {
			ra = (Elf_Addr *)(dt_reloc->r_offset + loff);
			RELOC_DYN(dt_reloc, sp, ra, loff);
		}
	}

	/* do any RWX -> RX fixups for executable PLTs and apply GNU_RELRO */
	phdp = (Elf_Phdr *)dl_data[AUX_phdr];
	for (i = 0; i < dl_data[AUX_phnum]; i++, phdp++) {
		switch (phdp->p_type) {
		case PT_LOAD:
			if ((phdp->p_flags & (PF_X | PF_W)) != (PF_X | PF_W))
				break;
			mprotect((void *)(phdp->p_vaddr + loff), phdp->p_memsz,
			    PROT_READ);
			break;
		case PT_GNU_RELRO:
			relro_addr = (void *)(phdp->p_vaddr + loff);
			relro_size = phdp->p_memsz;
			/*
			 * GNU_RELRO (a) covers the GOT, and (b) comes after
			 * all LOAD sections, so if we found it then we're done
			 */
			break;
		}
	}
}
