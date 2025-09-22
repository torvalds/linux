/*	$OpenBSD: boot_md.h,v 1.5 2023/11/18 18:02:47 deraadt Exp $ */

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

#define	DT_PROC(n)	((n) - DT_LOPROC)

#if RELOC_TAG == DT_RELA
typedef	Elf_RelA	RELOC_TYPE;
#elif RELOC_TAG == DT_REL
typedef	Elf_Rel		RELOC_TYPE;
#else
# error "unknown RELOC_TAG"
#endif

/* The set of dynamic tags that we're interested in for bootstrapping */
struct boot_dyn {
	RELOC_TYPE	*dt_reloc;	/* DT_RELA   or DT_REL */
	Elf_Addr	dt_relocsz;	/* DT_RELASZ or DT_RELSZ */
	Elf_Addr	*dt_pltgot;
	const Elf_Sym	*dt_symtab;
	u_long		dt_proc[DT_PROCNUM];
};

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
	struct boot_dyn	dynld;		/* Resolver data for the loader */
	AuxInfo		*auxstack;
	long		*stack;
	int		n, argc;
	char		**argv, **envp;
	long		loff;
	RELOC_TYPE	*rp;
	Elf_Phdr	*phdp;
	Elf_Addr	i;

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
	 * We need to do 'selfreloc' in case the code weren't
	 * loaded at the address it was linked to.
	 *
	 * Scan the DYNAMIC section for the loader.
	 * Cache the data for easier access.
	 */
	_dl_memset(&dynld, 0, sizeof(dynld));
	while (dynp->d_tag != DT_NULL) {
		/* first the tags that are pointers to be relocated */
		if (dynp->d_tag == DT_PLTGOT)
			dynld.dt_pltgot = (void *)(dynp->d_un.d_ptr + loff);
		else if (dynp->d_tag == DT_SYMTAB)
			dynld.dt_symtab = (void *)(dynp->d_un.d_ptr + loff);
		else if (dynp->d_tag == RELOC_TAG)	/* DT_{RELA,REL} */
			dynld.dt_reloc = (void *)(dynp->d_un.d_ptr + loff);

		/* Now for the tags that are just sizes or counts */
		else if (dynp->d_tag == RELOC_TAG+1)	/* DT_{RELA,REL}SZ */
			dynld.dt_relocsz = dynp->d_un.d_val;
		else if (dynp->d_tag >= DT_LOPROC &&
		    dynp->d_tag < DT_LOPROC + DT_PROCNUM)
			dynld.dt_proc[dynp->d_tag - DT_LOPROC] =
			    dynp->d_un.d_val;
		dynp++;
	}

	rp = dynld.dt_reloc;
	for (i = 0; i < dynld.dt_relocsz; i += sizeof *rp) {
		Elf_Addr *ra;
		const Elf_Sym *sp;

		sp = dynld.dt_symtab + ELF_R_SYM(rp->r_info);
		if (!ELF_R_SYM(rp->r_info) || sp->st_value != 0) {
			ra = (Elf_Addr *)(rp->r_offset + loff);
			RELOC_DYN(rp, sp, ra, loff);
		}
		rp++;
	}

	RELOC_GOT(&dynld, loff);

	/* apply GNU_RELRO */
	phdp = (Elf_Phdr *)dl_data[AUX_phdr];
	for (i = 0; i < dl_data[AUX_phnum]; i++, phdp++) {
		switch (phdp->p_type) {
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
