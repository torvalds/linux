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
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>

#define __ELF_WORD_SIZE 32

#include <sys/exec.h>
#include <sys/imgact.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/fcntl.h>
#include <sys/sysent.h>
#include <sys/imgact_elf.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <sys/signalvar.h>
#include <sys/vnode.h>
#include <sys/linker.h>

#include <vm/vm.h>
#include <vm/vm_param.h>

#include <machine/altivec.h>
#include <machine/cpu.h>
#include <machine/fpu.h>
#include <machine/elf.h>
#include <machine/reg.h>
#include <machine/md_var.h>

#ifdef __powerpc64__
#include <compat/freebsd32/freebsd32_proto.h>
#include <compat/freebsd32/freebsd32_util.h>

extern const char *freebsd32_syscallnames[];
static void ppc32_fixlimit(struct rlimit *rl, int which);

static SYSCTL_NODE(_compat, OID_AUTO, ppc32, CTLFLAG_RW, 0, "32-bit mode");

#define PPC32_MAXDSIZ (1024*1024*1024)
static u_long ppc32_maxdsiz = PPC32_MAXDSIZ;
SYSCTL_ULONG(_compat_ppc32, OID_AUTO, maxdsiz, CTLFLAG_RWTUN, &ppc32_maxdsiz,
             0, "");
#define PPC32_MAXSSIZ (64*1024*1024)
u_long ppc32_maxssiz = PPC32_MAXSSIZ;
SYSCTL_ULONG(_compat_ppc32, OID_AUTO, maxssiz, CTLFLAG_RWTUN, &ppc32_maxssiz,
             0, "");
#endif

struct sysentvec elf32_freebsd_sysvec = {
	.sv_size	= SYS_MAXSYSCALL,
#ifdef __powerpc64__
	.sv_table	= freebsd32_sysent,
#else
	.sv_table	= sysent,
#endif
	.sv_errsize	= 0,
	.sv_errtbl	= NULL,
	.sv_transtrap	= NULL,
	.sv_fixup	= __elfN(freebsd_fixup),
	.sv_sendsig	= sendsig,
	.sv_sigcode	= sigcode32,
	.sv_szsigcode	= &szsigcode32,
	.sv_name	= "FreeBSD ELF32",
	.sv_coredump	= __elfN(coredump),
	.sv_imgact_try	= NULL,
	.sv_minsigstksz	= MINSIGSTKSZ,
	.sv_minuser	= VM_MIN_ADDRESS,
	.sv_stackprot	= VM_PROT_ALL,
#ifdef __powerpc64__
	.sv_maxuser	= VM_MAXUSER_ADDRESS,
	.sv_usrstack	= FREEBSD32_USRSTACK,
	.sv_psstrings	= FREEBSD32_PS_STRINGS,
	.sv_copyout_strings = freebsd32_copyout_strings,
	.sv_setregs	= ppc32_setregs,
	.sv_syscallnames = freebsd32_syscallnames,
	.sv_fixlimit	= ppc32_fixlimit,
#else
	.sv_maxuser	= VM_MAXUSER_ADDRESS,
	.sv_usrstack	= USRSTACK,
	.sv_psstrings	= PS_STRINGS,
	.sv_copyout_strings = exec_copyout_strings,
	.sv_setregs	= exec_setregs,
	.sv_syscallnames = syscallnames,
	.sv_fixlimit	= NULL,
#endif
	.sv_maxssiz	= NULL,
	.sv_flags	= SV_ABI_FREEBSD | SV_ILP32 | SV_SHP | SV_ASLR,
	.sv_set_syscall_retval = cpu_set_syscall_retval,
	.sv_fetch_syscall_args = cpu_fetch_syscall_args,
	.sv_shared_page_base = FREEBSD32_SHAREDPAGE,
	.sv_shared_page_len = PAGE_SIZE,
	.sv_schedtail	= NULL,
	.sv_thread_detach = NULL,
	.sv_trap	= NULL,
	.sv_hwcap	= &cpu_features,
	.sv_hwcap2	= &cpu_features2,
};
INIT_SYSENTVEC(elf32_sysvec, &elf32_freebsd_sysvec);

static Elf32_Brandinfo freebsd_brand_info = {
	.brand		= ELFOSABI_FREEBSD,
	.machine	= EM_PPC,
	.compat_3_brand	= "FreeBSD",
	.emul_path	= NULL,
	.interp_path	= "/libexec/ld-elf.so.1",
	.sysvec		= &elf32_freebsd_sysvec,
#ifdef __powerpc64__
	.interp_newpath	= "/libexec/ld-elf32.so.1",
#else
	.interp_newpath	= NULL,
#endif
	.brand_note	= &elf32_freebsd_brandnote,
	.flags		= BI_CAN_EXEC_DYN | BI_BRAND_NOTE
};

SYSINIT(elf32, SI_SUB_EXEC, SI_ORDER_FIRST,
    (sysinit_cfunc_t) elf32_insert_brand_entry,
    &freebsd_brand_info);

static Elf32_Brandinfo freebsd_brand_oinfo = {
	.brand		= ELFOSABI_FREEBSD,
	.machine	= EM_PPC,
	.compat_3_brand	= "FreeBSD",
	.emul_path	= NULL,
	.interp_path	= "/usr/libexec/ld-elf.so.1",
	.sysvec		= &elf32_freebsd_sysvec,
	.interp_newpath	= NULL,
	.brand_note	= &elf32_freebsd_brandnote,
	.flags		= BI_CAN_EXEC_DYN | BI_BRAND_NOTE
};

SYSINIT(oelf32, SI_SUB_EXEC, SI_ORDER_ANY,
	(sysinit_cfunc_t) elf32_insert_brand_entry,
	&freebsd_brand_oinfo);

void elf_reloc_self(Elf_Dyn *dynp, Elf_Addr relocbase);

void
elf32_dump_thread(struct thread *td, void *dst, size_t *off)
{
	size_t len;
	struct pcb *pcb;
	uint64_t vshr[32];
	uint64_t *vsr_dw1;
	int vsr_idx;

	len = 0;
	pcb = td->td_pcb;

	if (pcb->pcb_flags & PCB_VEC) {
		save_vec_nodrop(td);
		if (dst != NULL) {
			len += elf32_populate_note(NT_PPC_VMX,
			    &pcb->pcb_vec, (char *)dst + len,
			    sizeof(pcb->pcb_vec), NULL);
		} else
			len += elf32_populate_note(NT_PPC_VMX, NULL, NULL,
			    sizeof(pcb->pcb_vec), NULL);
	}

	if (pcb->pcb_flags & PCB_VSX) {
		save_fpu_nodrop(td);
		if (dst != NULL) {
			/*
			 * Doubleword 0 of VSR0-VSR31 overlap with FPR0-FPR31 and
			 * VSR32-VSR63 overlap with VR0-VR31, so we only copy
			 * the non-overlapping data, which is doubleword 1 of VSR0-VSR31.
			 */
			for (vsr_idx = 0; vsr_idx < nitems(vshr); vsr_idx++) {
				vsr_dw1 = (uint64_t *)&pcb->pcb_fpu.fpr[vsr_idx].vsr[2];
				vshr[vsr_idx] = *vsr_dw1;
			}
			len += elf32_populate_note(NT_PPC_VSX,
			    vshr, (char *)dst + len,
			    sizeof(vshr), NULL);
		} else
			len += elf32_populate_note(NT_PPC_VSX, NULL, NULL,
			    sizeof(vshr), NULL);
	}

	*off = len;
}

#ifndef __powerpc64__
bool
elf_is_ifunc_reloc(Elf_Size r_info __unused)
{

	return (false);
}

/* Process one elf relocation with addend. */
static int
elf_reloc_internal(linker_file_t lf, Elf_Addr relocbase, const void *data,
    int type, int local, elf_lookup_fn lookup)
{
	Elf_Addr *where;
	Elf_Half *hwhere;
	Elf_Addr addr;
	Elf_Addr addend;
	Elf_Word rtype, symidx;
	const Elf_Rela *rela;
	int error;

	switch (type) {
	case ELF_RELOC_REL:
		panic("PPC only supports RELA relocations");
		break;
	case ELF_RELOC_RELA:
		rela = (const Elf_Rela *)data;
		where = (Elf_Addr *) ((uintptr_t)relocbase + rela->r_offset);
		hwhere = (Elf_Half *) ((uintptr_t)relocbase + rela->r_offset);
		addend = rela->r_addend;
		rtype = ELF_R_TYPE(rela->r_info);
		symidx = ELF_R_SYM(rela->r_info);
		break;
	default:
		panic("elf_reloc: unknown relocation mode %d\n", type);
	}

	switch (rtype) {

	case R_PPC_NONE:
		break;

	case R_PPC_ADDR32: /* word32 S + A */
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return -1;
		*where = elf_relocaddr(lf, addr + addend);
			break;

	case R_PPC_ADDR16_LO: /* #lo(S) */
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return -1;
		/*
		 * addend values are sometimes relative to sections
		 * (i.e. .rodata) in rela, where in reality they
		 * are relative to relocbase. Detect this condition.
		 */
		if (addr > relocbase && addr <= (relocbase + addend))
			addr = relocbase;
		addr = elf_relocaddr(lf, addr + addend);
		*hwhere = addr & 0xffff;
		break;

	case R_PPC_ADDR16_HA: /* #ha(S) */
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return -1;
		/*
		 * addend values are sometimes relative to sections
		 * (i.e. .rodata) in rela, where in reality they
		 * are relative to relocbase. Detect this condition.
		 */
		if (addr > relocbase && addr <= (relocbase + addend))
			addr = relocbase;
		addr = elf_relocaddr(lf, addr + addend);
		*hwhere = ((addr >> 16) + ((addr & 0x8000) ? 1 : 0))
		    & 0xffff;
		break;

	case R_PPC_RELATIVE: /* word32 B + A */
		*where = elf_relocaddr(lf, relocbase + addend);
		break;

	default:
		printf("kldload: unexpected relocation type %d\n",
		    (int) rtype);
		return -1;
	}
	return(0);
}

void
elf_reloc_self(Elf_Dyn *dynp, Elf_Addr relocbase)
{
	Elf_Rela *rela = NULL, *relalim;
	Elf_Addr relasz = 0;
	Elf_Addr *where;

	/*
	 * Extract the rela/relasz values from the dynamic section
	 */
	for (; dynp->d_tag != DT_NULL; dynp++) {
		switch (dynp->d_tag) {
		case DT_RELA:
			rela = (Elf_Rela *)(relocbase+dynp->d_un.d_ptr);
			break;
		case DT_RELASZ:
			relasz = dynp->d_un.d_val;
			break;
		}
	}

	/*
	 * Relocate these values
	 */
	relalim = (Elf_Rela *)((caddr_t)rela + relasz);
	for (; rela < relalim; rela++) {
		if (ELF_R_TYPE(rela->r_info) != R_PPC_RELATIVE)
			continue;
		where = (Elf_Addr *)(relocbase + rela->r_offset);
		*where = (Elf_Addr)(relocbase + rela->r_addend);
	}
}

int
elf_reloc(linker_file_t lf, Elf_Addr relocbase, const void *data, int type,
    elf_lookup_fn lookup)
{

	return (elf_reloc_internal(lf, relocbase, data, type, 0, lookup));
}

int
elf_reloc_local(linker_file_t lf, Elf_Addr relocbase, const void *data,
    int type, elf_lookup_fn lookup)
{

	return (elf_reloc_internal(lf, relocbase, data, type, 1, lookup));
}

int
elf_cpu_load_file(linker_file_t lf)
{
	/* Only sync the cache for non-kernel modules */
	if (lf->id != 1)
		__syncicache(lf->address, lf->size);
	return (0);
}

int
elf_cpu_unload_file(linker_file_t lf __unused)
{

	return (0);
}
#endif

#ifdef __powerpc64__
static void
ppc32_fixlimit(struct rlimit *rl, int which)
{
	switch (which) {
	case RLIMIT_DATA:
		if (ppc32_maxdsiz != 0) {
			if (rl->rlim_cur > ppc32_maxdsiz)
				rl->rlim_cur = ppc32_maxdsiz;
			if (rl->rlim_max > ppc32_maxdsiz)
				rl->rlim_max = ppc32_maxdsiz;
		}
		break;
	case RLIMIT_STACK:
		if (ppc32_maxssiz != 0) {
			if (rl->rlim_cur > ppc32_maxssiz)
				rl->rlim_cur = ppc32_maxssiz;
			if (rl->rlim_max > ppc32_maxssiz)
				rl->rlim_max = ppc32_maxssiz;
		}
		break;
	}
}
#endif
