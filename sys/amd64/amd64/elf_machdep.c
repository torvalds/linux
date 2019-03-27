/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 1996-1998 John D. Polstra.
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

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/exec.h>
#include <sys/imgact.h>
#include <sys/linker.h>
#include <sys/proc.h>
#include <sys/sysent.h>
#include <sys/imgact_elf.h>
#include <sys/syscall.h>
#include <sys/signalvar.h>
#include <sys/vnode.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_param.h>

#include <machine/elf.h>
#include <machine/fpu.h>
#include <machine/md_var.h>

struct sysentvec elf64_freebsd_sysvec = {
	.sv_size	= SYS_MAXSYSCALL,
	.sv_table	= sysent,
	.sv_errsize	= 0,
	.sv_errtbl	= NULL,
	.sv_transtrap	= NULL,
	.sv_fixup	= __elfN(freebsd_fixup),
	.sv_sendsig	= sendsig,
	.sv_sigcode	= sigcode,
	.sv_szsigcode	= &szsigcode,
	.sv_name	= "FreeBSD ELF64",
	.sv_coredump	= __elfN(coredump),
	.sv_imgact_try	= NULL,
	.sv_minsigstksz	= MINSIGSTKSZ,
	.sv_minuser	= VM_MIN_ADDRESS,
	.sv_maxuser	= VM_MAXUSER_ADDRESS,
	.sv_usrstack	= USRSTACK,
	.sv_psstrings	= PS_STRINGS,
	.sv_stackprot	= VM_PROT_ALL,
	.sv_copyout_strings	= exec_copyout_strings,
	.sv_setregs	= exec_setregs,
	.sv_fixlimit	= NULL,
	.sv_maxssiz	= NULL,
	.sv_flags	= SV_ABI_FREEBSD | SV_ASLR | SV_LP64 | SV_SHP |
			    SV_TIMEKEEP,
	.sv_set_syscall_retval = cpu_set_syscall_retval,
	.sv_fetch_syscall_args = cpu_fetch_syscall_args,
	.sv_syscallnames = syscallnames,
	.sv_shared_page_base = SHAREDPAGE,
	.sv_shared_page_len = PAGE_SIZE,
	.sv_schedtail	= NULL,
	.sv_thread_detach = NULL,
	.sv_trap	= NULL,
};
INIT_SYSENTVEC(elf64_sysvec, &elf64_freebsd_sysvec);

void
amd64_lower_shared_page(struct sysentvec *sv)
{
	if (hw_lower_amd64_sharedpage != 0) {
		sv->sv_maxuser -= PAGE_SIZE;
		sv->sv_shared_page_base -= PAGE_SIZE;
		sv->sv_usrstack -= PAGE_SIZE;
		sv->sv_psstrings -= PAGE_SIZE;
	}
}

/*
 * Do this fixup before INIT_SYSENTVEC (SI_ORDER_ANY) because the latter
 * uses the value of sv_shared_page_base.
 */
SYSINIT(elf64_sysvec_fixup, SI_SUB_EXEC, SI_ORDER_FIRST,
	(sysinit_cfunc_t) amd64_lower_shared_page,
	&elf64_freebsd_sysvec);

static Elf64_Brandinfo freebsd_brand_info = {
	.brand		= ELFOSABI_FREEBSD,
	.machine	= EM_X86_64,
	.compat_3_brand	= "FreeBSD",
	.emul_path	= NULL,
	.interp_path	= "/libexec/ld-elf.so.1",
	.sysvec		= &elf64_freebsd_sysvec,
	.interp_newpath	= NULL,
	.brand_note	= &elf64_freebsd_brandnote,
	.flags		= BI_CAN_EXEC_DYN | BI_BRAND_NOTE
};

SYSINIT(elf64, SI_SUB_EXEC, SI_ORDER_FIRST,
	(sysinit_cfunc_t) elf64_insert_brand_entry,
	&freebsd_brand_info);

static Elf64_Brandinfo freebsd_brand_oinfo = {
	.brand		= ELFOSABI_FREEBSD,
	.machine	= EM_X86_64,
	.compat_3_brand	= "FreeBSD",
	.emul_path	= NULL,
	.interp_path	= "/usr/libexec/ld-elf.so.1",
	.sysvec		= &elf64_freebsd_sysvec,
	.interp_newpath	= NULL,
	.brand_note	= &elf64_freebsd_brandnote,
	.flags		= BI_CAN_EXEC_DYN | BI_BRAND_NOTE
};

SYSINIT(oelf64, SI_SUB_EXEC, SI_ORDER_ANY,
	(sysinit_cfunc_t) elf64_insert_brand_entry,
	&freebsd_brand_oinfo);

static Elf64_Brandinfo kfreebsd_brand_info = {
	.brand		= ELFOSABI_FREEBSD,
	.machine	= EM_X86_64,
	.compat_3_brand	= "FreeBSD",
	.emul_path	= NULL,
	.interp_path	= "/lib/ld-kfreebsd-x86-64.so.1",
	.sysvec		= &elf64_freebsd_sysvec,
	.interp_newpath	= NULL,
	.brand_note	= &elf64_kfreebsd_brandnote,
	.flags		= BI_CAN_EXEC_DYN | BI_BRAND_NOTE_MANDATORY
};

SYSINIT(kelf64, SI_SUB_EXEC, SI_ORDER_ANY,
	(sysinit_cfunc_t) elf64_insert_brand_entry,
	&kfreebsd_brand_info);

void
elf64_dump_thread(struct thread *td, void *dst, size_t *off)
{
	void *buf;
	size_t len;

	len = 0;
	if (use_xsave) {
		if (dst != NULL) {
			fpugetregs(td);
			len += elf64_populate_note(NT_X86_XSTATE,
			    get_pcb_user_save_td(td), dst,
			    cpu_max_ext_state_size, &buf);
			*(uint64_t *)((char *)buf + X86_XSTATE_XCR0_OFFSET) =
			    xsave_mask;
		} else
			len += elf64_populate_note(NT_X86_XSTATE, NULL, NULL,
			    cpu_max_ext_state_size, NULL);
	}
	*off = len;
}

bool
elf_is_ifunc_reloc(Elf_Size r_info)
{

	return (ELF_R_TYPE(r_info) == R_X86_64_IRELATIVE);
}

/* Process one elf relocation with addend. */
static int
elf_reloc_internal(linker_file_t lf, Elf_Addr relocbase, const void *data,
    int type, elf_lookup_fn lookup)
{
	Elf64_Addr *where, val;
	Elf32_Addr *where32, val32;
	Elf_Addr addr;
	Elf_Addr addend;
	Elf_Size rtype, symidx;
	const Elf_Rel *rel;
	const Elf_Rela *rela;
	int error;

	switch (type) {
	case ELF_RELOC_REL:
		rel = (const Elf_Rel *)data;
		where = (Elf_Addr *) (relocbase + rel->r_offset);
		rtype = ELF_R_TYPE(rel->r_info);
		symidx = ELF_R_SYM(rel->r_info);
		/* Addend is 32 bit on 32 bit relocs */
		switch (rtype) {
		case R_X86_64_PC32:
		case R_X86_64_32S:
		case R_X86_64_PLT32:
			addend = *(Elf32_Addr *)where;
			break;
		default:
			addend = *where;
			break;
		}
		break;
	case ELF_RELOC_RELA:
		rela = (const Elf_Rela *)data;
		where = (Elf_Addr *) (relocbase + rela->r_offset);
		addend = rela->r_addend;
		rtype = ELF_R_TYPE(rela->r_info);
		symidx = ELF_R_SYM(rela->r_info);
		break;
	default:
		panic("unknown reloc type %d\n", type);
	}

	switch (rtype) {
		case R_X86_64_NONE:	/* none */
			break;

		case R_X86_64_64:		/* S + A */
			error = lookup(lf, symidx, 1, &addr);
			val = addr + addend;
			if (error != 0)
				return -1;
			if (*where != val)
				*where = val;
			break;

		case R_X86_64_PC32:	/* S + A - P */
		case R_X86_64_PLT32:	/* L + A - P, L is PLT location for
					   the symbol, which we treat as S */
			error = lookup(lf, symidx, 1, &addr);
			where32 = (Elf32_Addr *)where;
			val32 = (Elf32_Addr)(addr + addend - (Elf_Addr)where);
			if (error != 0)
				return -1;
			if (*where32 != val32)
				*where32 = val32;
			break;

		case R_X86_64_32S:	/* S + A sign extend */
			error = lookup(lf, symidx, 1, &addr);
			val32 = (Elf32_Addr)(addr + addend);
			where32 = (Elf32_Addr *)where;
			if (error != 0)
				return -1;
			if (*where32 != val32)
				*where32 = val32;
			break;

		case R_X86_64_COPY:	/* none */
			/*
			 * There shouldn't be copy relocations in kernel
			 * objects.
			 */
			printf("kldload: unexpected R_COPY relocation\n");
			return (-1);
			break;

		case R_X86_64_GLOB_DAT:	/* S */
		case R_X86_64_JMP_SLOT:	/* XXX need addend + offset */
			error = lookup(lf, symidx, 1, &addr);
			if (error != 0)
				return -1;
			if (*where != addr)
				*where = addr;
			break;

		case R_X86_64_RELATIVE:	/* B + A */
			addr = relocbase + addend;
			val = addr;
			if (*where != val)
				*where = val;
			break;

		case R_X86_64_IRELATIVE:
			addr = relocbase + addend;
			val = ((Elf64_Addr (*)(void))addr)();
			if (*where != val)
				*where = val;
			break;

		default:
			printf("kldload: unexpected relocation type %ld\n",
			       rtype);
			return (-1);
	}
	return (0);
}

int
elf_reloc(linker_file_t lf, Elf_Addr relocbase, const void *data, int type,
    elf_lookup_fn lookup)
{

	return (elf_reloc_internal(lf, relocbase, data, type, lookup));
}

int
elf_reloc_local(linker_file_t lf, Elf_Addr relocbase, const void *data,
    int type, elf_lookup_fn lookup)
{

	return (elf_reloc_internal(lf, relocbase, data, type, lookup));
}

int
elf_cpu_load_file(linker_file_t lf __unused)
{

	return (0);
}

int
elf_cpu_unload_file(linker_file_t lf __unused)
{

	return (0);
}
