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
#include <sys/exec.h>
#include <sys/imgact.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/fcntl.h>
#include <sys/sysent.h>
#include <sys/imgact_elf.h>
#include <sys/syscall.h>
#include <sys/signalvar.h>
#include <sys/vnode.h>
#include <sys/linker.h>

#include <vm/vm.h>
#include <vm/vm_param.h>

#include <machine/altivec.h>
#include <machine/cpu.h>
#include <machine/fpu.h>
#include <machine/elf.h>
#include <machine/md_var.h>

static void exec_setregs_funcdesc(struct thread *td, struct image_params *imgp,
    u_long stack);

struct sysentvec elf64_freebsd_sysvec_v1 = {
	.sv_size	= SYS_MAXSYSCALL,
	.sv_table	= sysent,
	.sv_errsize	= 0,
	.sv_errtbl	= NULL,
	.sv_transtrap	= NULL,
	.sv_fixup	= __elfN(freebsd_fixup),
	.sv_sendsig	= sendsig,
	.sv_sigcode	= sigcode64,
	.sv_szsigcode	= &szsigcode64,
	.sv_name	= "FreeBSD ELF64",
	.sv_coredump	= __elfN(coredump),
	.sv_imgact_try	= NULL,
	.sv_minsigstksz	= MINSIGSTKSZ,
	.sv_minuser	= VM_MIN_ADDRESS,
	.sv_maxuser	= VM_MAXUSER_ADDRESS,
	.sv_usrstack	= USRSTACK,
	.sv_psstrings	= PS_STRINGS,
	.sv_stackprot	= VM_PROT_ALL,
	.sv_copyout_strings = exec_copyout_strings,
	.sv_setregs	= exec_setregs_funcdesc,
	.sv_fixlimit	= NULL,
	.sv_maxssiz	= NULL,
	.sv_flags	= SV_ABI_FREEBSD | SV_LP64 | SV_SHP | SV_ASLR,
	.sv_set_syscall_retval = cpu_set_syscall_retval,
	.sv_fetch_syscall_args = cpu_fetch_syscall_args,
	.sv_syscallnames = syscallnames,
	.sv_shared_page_base = SHAREDPAGE,
	.sv_shared_page_len = PAGE_SIZE,
	.sv_schedtail	= NULL,
	.sv_thread_detach = NULL,
	.sv_trap	= NULL,
	.sv_hwcap	= &cpu_features,
	.sv_hwcap2	= &cpu_features2,
};
INIT_SYSENTVEC(elf64_sysvec_v1, &elf64_freebsd_sysvec_v1);

struct sysentvec elf64_freebsd_sysvec_v2 = {
	.sv_size	= SYS_MAXSYSCALL,
	.sv_table	= sysent,
	.sv_errsize	= 0,
	.sv_errtbl	= NULL,
	.sv_transtrap	= NULL,
	.sv_fixup	= __elfN(freebsd_fixup),
	.sv_sendsig	= sendsig,
	.sv_sigcode	= sigcode64_elfv2,
	.sv_szsigcode	= &szsigcode64_elfv2,
	.sv_name	= "FreeBSD ELF64 V2",
	.sv_coredump	= __elfN(coredump),
	.sv_imgact_try	= NULL,
	.sv_minsigstksz	= MINSIGSTKSZ,
	.sv_minuser	= VM_MIN_ADDRESS,
	.sv_maxuser	= VM_MAXUSER_ADDRESS,
	.sv_usrstack	= USRSTACK,
	.sv_psstrings	= PS_STRINGS,
	.sv_stackprot	= VM_PROT_ALL,
	.sv_copyout_strings = exec_copyout_strings,
	.sv_setregs	= exec_setregs,
	.sv_fixlimit	= NULL,
	.sv_maxssiz	= NULL,
	.sv_flags	= SV_ABI_FREEBSD | SV_LP64 | SV_SHP,
	.sv_set_syscall_retval = cpu_set_syscall_retval,
	.sv_fetch_syscall_args = cpu_fetch_syscall_args,
	.sv_syscallnames = syscallnames,
	.sv_shared_page_base = SHAREDPAGE,
	.sv_shared_page_len = PAGE_SIZE,
	.sv_schedtail	= NULL,
	.sv_thread_detach = NULL,
	.sv_trap	= NULL,
	.sv_hwcap	= &cpu_features,
	.sv_hwcap2	= &cpu_features2,
};
INIT_SYSENTVEC(elf64_sysvec_v2, &elf64_freebsd_sysvec_v2);

static boolean_t ppc64_elfv1_header_match(struct image_params *params);
static boolean_t ppc64_elfv2_header_match(struct image_params *params);

static Elf64_Brandinfo freebsd_brand_info_elfv1 = {
	.brand		= ELFOSABI_FREEBSD,
	.machine	= EM_PPC64,
	.compat_3_brand	= "FreeBSD",
	.emul_path	= NULL,
	.interp_path	= "/libexec/ld-elf.so.1",
	.sysvec		= &elf64_freebsd_sysvec_v1,
	.interp_newpath	= NULL,
	.brand_note	= &elf64_freebsd_brandnote,
	.flags		= BI_CAN_EXEC_DYN | BI_BRAND_NOTE,
	.header_supported = &ppc64_elfv1_header_match
};

SYSINIT(elf64v1, SI_SUB_EXEC, SI_ORDER_ANY,
    (sysinit_cfunc_t) elf64_insert_brand_entry,
    &freebsd_brand_info_elfv1);

static Elf64_Brandinfo freebsd_brand_info_elfv2 = {
	.brand		= ELFOSABI_FREEBSD,
	.machine	= EM_PPC64,
	.compat_3_brand	= "FreeBSD",
	.emul_path	= NULL,
	.interp_path	= "/libexec/ld-elf.so.1",
	.sysvec		= &elf64_freebsd_sysvec_v2,
	.interp_newpath	= NULL,
	.brand_note	= &elf64_freebsd_brandnote,
	.flags		= BI_CAN_EXEC_DYN | BI_BRAND_NOTE,
	.header_supported = &ppc64_elfv2_header_match
};

SYSINIT(elf64v2, SI_SUB_EXEC, SI_ORDER_ANY,
    (sysinit_cfunc_t) elf64_insert_brand_entry,
    &freebsd_brand_info_elfv2);

static Elf64_Brandinfo freebsd_brand_oinfo = {
	.brand		= ELFOSABI_FREEBSD,
	.machine	= EM_PPC64,
	.compat_3_brand	= "FreeBSD",
	.emul_path	= NULL,
	.interp_path	= "/usr/libexec/ld-elf.so.1",
	.sysvec		= &elf64_freebsd_sysvec_v1,
	.interp_newpath	= NULL,
	.brand_note	= &elf64_freebsd_brandnote,
	.flags		= BI_CAN_EXEC_DYN | BI_BRAND_NOTE,
	.header_supported = &ppc64_elfv1_header_match
};

SYSINIT(oelf64, SI_SUB_EXEC, SI_ORDER_ANY,
	(sysinit_cfunc_t) elf64_insert_brand_entry,
	&freebsd_brand_oinfo);

void elf_reloc_self(Elf_Dyn *dynp, Elf_Addr relocbase);

static boolean_t
ppc64_elfv1_header_match(struct image_params *params)
{
	const Elf64_Ehdr *hdr = (const Elf64_Ehdr *)params->image_header;
	int abi = (hdr->e_flags & 3);

	return (abi == 0 || abi == 1);
}

static boolean_t
ppc64_elfv2_header_match(struct image_params *params)
{
	const Elf64_Ehdr *hdr = (const Elf64_Ehdr *)params->image_header;
	int abi = (hdr->e_flags & 3);

	return (abi == 2);
}

static void  
exec_setregs_funcdesc(struct thread *td, struct image_params *imgp,
    u_long stack)
{
	struct trapframe *tf;
	register_t entry_desc[3];

	tf = trapframe(td);
	exec_setregs(td, imgp, stack);

	/*
	 * For 64-bit ELFv1, we need to disentangle the function
	 * descriptor
	 *
	 * 0. entry point
	 * 1. TOC value (r2)
	 * 2. Environment pointer (r11)
	 */

	(void)copyin((void *)imgp->entry_addr, entry_desc,
	    sizeof(entry_desc));
	tf->srr0 = entry_desc[0] + imgp->reloc_base;
	tf->fixreg[2] = entry_desc[1] + imgp->reloc_base;
	tf->fixreg[11] = entry_desc[2] + imgp->reloc_base;
}

void
elf64_dump_thread(struct thread *td, void *dst, size_t *off)
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
			len += elf64_populate_note(NT_PPC_VMX,
			    &pcb->pcb_vec, (char *)dst + len,
			    sizeof(pcb->pcb_vec), NULL);
		} else
			len += elf64_populate_note(NT_PPC_VMX, NULL, NULL,
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
			len += elf64_populate_note(NT_PPC_VSX,
			    vshr, (char *)dst + len,
			    sizeof(vshr), NULL);
		} else
			len += elf64_populate_note(NT_PPC_VSX, NULL, NULL,
			    sizeof(vshr), NULL);
	}

	*off = len;
}

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
		where = (Elf_Addr *) (relocbase + rela->r_offset);
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

	case R_PPC64_ADDR64:	/* doubleword64 S + A */
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return -1;
		addr += addend;
		*where = addr;
		break;

	case R_PPC_RELATIVE:	/* doubleword64 B + A */
		*where = elf_relocaddr(lf, relocbase + addend);
		break;

	case R_PPC_JMP_SLOT:	/* function descriptor copy */
		lookup(lf, symidx, 1, &addr);
#if !defined(_CALL_ELF) || _CALL_ELF == 1
		memcpy(where, (Elf_Addr *)addr, 3*sizeof(Elf_Addr));
#else
		*where = addr;
#endif
		__asm __volatile("dcbst 0,%0; sync" :: "r"(where) : "memory");
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
