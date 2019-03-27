/*-
 * Copyright (C) 2006-2012 Semihalf
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
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*-
 * Copyright (C) 2001 Benno Rice
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
 * THIS SOFTWARE IS PROVIDED BY Benno Rice ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * $NetBSD: machdep.c,v 1.74.2.1 2000/11/01 16:13:48 tv Exp $
 */
/*-
 * Copyright (C) 1995, 1996 Wolfgang Solfrank.
 * Copyright (C) 1995, 1996 TooLs GmbH.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"
#include "opt_hwpmc_hooks.h"
#include "opt_kstack_pages.h"
#include "opt_platform.h"

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/bus.h>
#include <sys/cons.h>
#include <sys/cpu.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/rwlock.h>
#include <sys/sysctl.h>
#include <sys/exec.h>
#include <sys/ktr.h>
#include <sys/syscallsubr.h>
#include <sys/sysproto.h>
#include <sys/signalvar.h>
#include <sys/sysent.h>
#include <sys/imgact.h>
#include <sys/msgbuf.h>
#include <sys/ptrace.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_page.h>
#include <vm/vm_object.h>
#include <vm/vm_pager.h>

#include <machine/cpu.h>
#include <machine/kdb.h>
#include <machine/reg.h>
#include <machine/vmparam.h>
#include <machine/spr.h>
#include <machine/hid.h>
#include <machine/psl.h>
#include <machine/trap.h>
#include <machine/md_var.h>
#include <machine/mmuvar.h>
#include <machine/sigframe.h>
#include <machine/machdep.h>
#include <machine/metadata.h>
#include <machine/platform.h>

#include <sys/linker.h>
#include <sys/reboot.h>

#include <contrib/libfdt/libfdt.h>
#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>

#ifdef DDB
#include <ddb/ddb.h>
#endif

#ifdef  DEBUG
#define debugf(fmt, args...) printf(fmt, ##args)
#else
#define debugf(fmt, args...)
#endif

extern unsigned char _etext[];
extern unsigned char _edata[];
extern unsigned char __bss_start[];
extern unsigned char __sbss_start[];
extern unsigned char __sbss_end[];
extern unsigned char _end[];
extern vm_offset_t __endkernel;
extern vm_paddr_t kernload;

/*
 * Bootinfo is passed to us by legacy loaders. Save the address of the
 * structure to handle backward compatibility.
 */
uint32_t *bootinfo;

void print_kernel_section_addr(void);
void print_kenv(void);
uintptr_t booke_init(u_long, u_long);
void ivor_setup(void);

extern void *interrupt_vector_base;
extern void *int_critical_input;
extern void *int_machine_check;
extern void *int_data_storage;
extern void *int_instr_storage;
extern void *int_external_input;
extern void *int_alignment;
extern void *int_fpu;
extern void *int_program;
extern void *int_syscall;
extern void *int_decrementer;
extern void *int_fixed_interval_timer;
extern void *int_watchdog;
extern void *int_data_tlb_error;
extern void *int_inst_tlb_error;
extern void *int_debug;
extern void *int_debug_ed;
extern void *int_vec;
extern void *int_vecast;
#ifdef __SPE__
extern void *int_spe_fpdata;
extern void *int_spe_fpround;
#endif
#ifdef HWPMC_HOOKS
extern void *int_performance_counter;
#endif

#define SET_TRAP(ivor, handler) \
	KASSERT(((uintptr_t)(&handler) & ~0xffffUL) == \
	    ((uintptr_t)(&interrupt_vector_base) & ~0xffffUL), \
	    ("Handler " #handler " too far from interrupt vector base")); \
	mtspr(ivor, (uintptr_t)(&handler) & 0xffffUL);

uintptr_t powerpc_init(vm_offset_t fdt, vm_offset_t, vm_offset_t, void *mdp,
    uint32_t mdp_cookie);
void booke_cpu_init(void);

void
booke_cpu_init(void)
{

	cpu_features |= PPC_FEATURE_BOOKE;

	psl_kernset = PSL_CE | PSL_ME | PSL_EE;
#ifdef __powerpc64__
	psl_kernset |= PSL_CM;
#endif
	psl_userset = psl_kernset | PSL_PR;
#ifdef __powerpc64__
	psl_userset32 = psl_userset & ~PSL_CM;
#endif
	psl_userstatic = ~(PSL_VEC | PSL_FP | PSL_FE0 | PSL_FE1);

	pmap_mmu_install(MMU_TYPE_BOOKE, BUS_PROBE_GENERIC);
}

void
ivor_setup(void)
{

	mtspr(SPR_IVPR, ((uintptr_t)&interrupt_vector_base) & ~0xffffUL);

	SET_TRAP(SPR_IVOR0, int_critical_input);
	SET_TRAP(SPR_IVOR1, int_machine_check);
	SET_TRAP(SPR_IVOR2, int_data_storage);
	SET_TRAP(SPR_IVOR3, int_instr_storage);
	SET_TRAP(SPR_IVOR4, int_external_input);
	SET_TRAP(SPR_IVOR5, int_alignment);
	SET_TRAP(SPR_IVOR6, int_program);
	SET_TRAP(SPR_IVOR8, int_syscall);
	SET_TRAP(SPR_IVOR10, int_decrementer);
	SET_TRAP(SPR_IVOR11, int_fixed_interval_timer);
	SET_TRAP(SPR_IVOR12, int_watchdog);
	SET_TRAP(SPR_IVOR13, int_data_tlb_error);
	SET_TRAP(SPR_IVOR14, int_inst_tlb_error);
	SET_TRAP(SPR_IVOR15, int_debug);
#ifdef HWPMC_HOOKS
	SET_TRAP(SPR_IVOR35, int_performance_counter);
#endif
	switch ((mfpvr() >> 16) & 0xffff) {
	case FSL_E6500:
		SET_TRAP(SPR_IVOR32, int_vec);
		SET_TRAP(SPR_IVOR33, int_vecast);
		/* FALLTHROUGH */
	case FSL_E500mc:
	case FSL_E5500:
		SET_TRAP(SPR_IVOR7, int_fpu);
		SET_TRAP(SPR_IVOR15, int_debug_ed);
		break;
	case FSL_E500v1:
	case FSL_E500v2:
		SET_TRAP(SPR_IVOR32, int_vec);
#ifdef __SPE__
		SET_TRAP(SPR_IVOR33, int_spe_fpdata);
		SET_TRAP(SPR_IVOR34, int_spe_fpround);
#endif
		break;
	}

#ifdef __powerpc64__
	/* Set 64-bit interrupt mode. */
	mtspr(SPR_EPCR, mfspr(SPR_EPCR) | EPCR_ICM);
#endif
}

static int
booke_check_for_fdt(uint32_t arg1, vm_offset_t *dtbp)
{
	void *ptr;
	int fdt_size;

	if (arg1 % 8 != 0)
		return (-1);

	ptr = (void *)pmap_early_io_map(arg1, PAGE_SIZE);
	if (fdt_check_header(ptr) != 0)
		return (-1);

	/*
	 * Read FDT total size from the header of FDT.
	 * This for sure hits within first page which is
	 * already mapped.
	 */
	fdt_size = fdt_totalsize((void *)ptr);

	/* 
	 * Ok, arg1 points to FDT, so we need to map it in.
	 * First, unmap this page and then map FDT again with full size
	 */
	pmap_early_io_unmap((vm_offset_t)ptr, PAGE_SIZE);
	ptr = (void *)pmap_early_io_map(arg1, fdt_size); 
	*dtbp = (vm_offset_t)ptr;

	return (0);
}

uintptr_t
booke_init(u_long arg1, u_long arg2)
{
	uintptr_t ret;
	void *mdp;
	vm_offset_t dtbp, end;

	end = (uintptr_t)_end;
	dtbp = (vm_offset_t)NULL;

	/* Set up TLB initially */
	bootinfo = NULL;
	bzero(__sbss_start, __sbss_end - __sbss_start);
	bzero(__bss_start, _end - __bss_start);
	tlb1_init();

	/*
	 * Handle the various ways we can get loaded and started:
	 *  -	FreeBSD's loader passes the pointer to the metadata
	 *	in arg1, with arg2 undefined. arg1 has a value that's
	 *	relative to the kernel's link address (i.e. larger
	 *	than 0xc0000000).
	 *  -	Juniper's loader passes the metadata pointer in arg2
	 *	and sets arg1 to zero. This is to signal that the
	 *	loader maps the kernel and starts it at its link
	 *	address (unlike the FreeBSD loader).
	 *  -	U-Boot passes the standard argc and argv parameters
	 *	in arg1 and arg2 (resp). arg1 is between 1 and some
	 *	relatively small number, such as 64K. arg2 is the
	 *	physical address of the argv vector.
	 *  -   ePAPR loaders pass an FDT blob in r3 (arg1) and the magic hex
	 *      string 0x45504150 ('EPAP') in r6 (which has been lost by now).
	 *      r4 (arg2) is supposed to be set to zero, but is not always.
	 */
	
	if (arg1 == 0)				/* Juniper loader */
		mdp = (void *)arg2;
	else if (booke_check_for_fdt(arg1, &dtbp) == 0) { /* ePAPR */
		end = roundup(end, 8);
		memmove((void *)end, (void *)dtbp, fdt_totalsize((void *)dtbp));
		dtbp = end;
		end += fdt_totalsize((void *)dtbp);
		__endkernel = end;
		mdp = NULL;
	} else if (arg1 > (uintptr_t)kernload)	/* FreeBSD loader */
		mdp = (void *)arg1;
	else					/* U-Boot */
		mdp = NULL;

	/* Default to 32 byte cache line size. */
	switch ((mfpvr()) >> 16) {
	case FSL_E500mc:
	case FSL_E5500:
	case FSL_E6500:
		cacheline_size = 64;
		break;
	}

	/*
	 * Last element is a magic cookie that indicates that the metadata
	 * pointer is meaningful.
	 */
	ret = powerpc_init(dtbp, 0, 0, mdp, (mdp == NULL) ? 0 : 0xfb5d104d);

	/* Enable caches */
	booke_enable_l1_cache();
	booke_enable_l2_cache();

	booke_enable_bpred();

	return (ret);
}

#define RES_GRANULE cacheline_size
extern uintptr_t tlb0_miss_locks[];

/* Initialise a struct pcpu. */
void
cpu_pcpu_init(struct pcpu *pcpu, int cpuid, size_t sz)
{

	pcpu->pc_booke.tid_next = TID_MIN;

#ifdef SMP
	uintptr_t *ptr;
	int words_per_gran = RES_GRANULE / sizeof(uintptr_t);

	ptr = &tlb0_miss_locks[cpuid * words_per_gran];
	pcpu->pc_booke.tlb_lock = ptr;
	*ptr = TLB_UNLOCKED;
	*(ptr + 1) = 0;		/* recurse counter */
#endif
}

/* Shutdown the CPU as much as possible. */
void
cpu_halt(void)
{

	mtmsr(mfmsr() & ~(PSL_CE | PSL_EE | PSL_ME | PSL_DE));
	while (1)
		;
}

int
ptrace_single_step(struct thread *td)
{
	struct trapframe *tf;

	tf = td->td_frame;
	tf->srr1 |= PSL_DE;
	tf->cpu.booke.dbcr0 |= (DBCR0_IDM | DBCR0_IC);
	return (0);
}

int
ptrace_clear_single_step(struct thread *td)
{
	struct trapframe *tf;

	tf = td->td_frame;
	tf->srr1 &= ~PSL_DE;
	tf->cpu.booke.dbcr0 &= ~(DBCR0_IDM | DBCR0_IC);
	return (0);
}

void
kdb_cpu_clear_singlestep(void)
{
	register_t r;

	r = mfspr(SPR_DBCR0);
	mtspr(SPR_DBCR0, r & ~DBCR0_IC);
	kdb_frame->srr1 &= ~PSL_DE;
}

void
kdb_cpu_set_singlestep(void)
{
	register_t r;

	r = mfspr(SPR_DBCR0);
	mtspr(SPR_DBCR0, r | DBCR0_IC | DBCR0_IDM);
	kdb_frame->srr1 |= PSL_DE;
}

