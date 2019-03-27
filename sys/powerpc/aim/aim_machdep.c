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
 *	$NetBSD: machdep.c,v 1.74.2.1 2000/11/01 16:13:48 tv Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"
#include "opt_kstack_pages.h"
#include "opt_platform.h"

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/bus.h>
#include <sys/cons.h>
#include <sys/cpu.h>
#include <sys/eventhandler.h>
#include <sys/exec.h>
#include <sys/imgact.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/linker.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/msgbuf.h>
#include <sys/mutex.h>
#include <sys/ptrace.h>
#include <sys/reboot.h>
#include <sys/rwlock.h>
#include <sys/signalvar.h>
#include <sys/syscallsubr.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/sysproto.h>
#include <sys/ucontext.h>
#include <sys/uio.h>
#include <sys/vmmeter.h>
#include <sys/vnode.h>

#include <net/netisr.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_pager.h>

#include <machine/altivec.h>
#ifndef __powerpc64__
#include <machine/bat.h>
#endif
#include <machine/cpu.h>
#include <machine/elf.h>
#include <machine/fpu.h>
#include <machine/hid.h>
#include <machine/kdb.h>
#include <machine/md_var.h>
#include <machine/metadata.h>
#include <machine/mmuvar.h>
#include <machine/pcb.h>
#include <machine/reg.h>
#include <machine/sigframe.h>
#include <machine/spr.h>
#include <machine/trap.h>
#include <machine/vmparam.h>
#include <machine/ofw_machdep.h>

#include <ddb/ddb.h>

#include <dev/ofw/openfirm.h>

#ifdef __powerpc64__
#include "mmu_oea64.h"
#endif

#ifndef __powerpc64__
struct bat	battable[16];
#endif

#ifndef __powerpc64__
/* Bits for running on 64-bit systems in 32-bit mode. */
extern void	*testppc64, *testppc64size;
extern void	*restorebridge, *restorebridgesize;
extern void	*rfid_patch, *rfi_patch1, *rfi_patch2;
extern void	*trapcode64;

extern Elf_Addr	_GLOBAL_OFFSET_TABLE_[];
#endif

extern void	*rstcode, *rstcodeend;
extern void	*trapcode, *trapcodeend;
extern void	*hypertrapcode, *hypertrapcodeend;
extern void	*generictrap, *generictrap64;
extern void	*alitrap, *aliend;
extern void	*dsitrap, *dsiend;
extern void	*decrint, *decrsize;
extern void     *extint, *extsize;
extern void	*dblow, *dbend;
extern void	*imisstrap, *imisssize;
extern void	*dlmisstrap, *dlmisssize;
extern void	*dsmisstrap, *dsmisssize;

extern void *ap_pcpu;
extern void __restartkernel(vm_offset_t, vm_offset_t, vm_offset_t, void *, uint32_t, register_t offset, register_t msr);

void aim_early_init(vm_offset_t fdt, vm_offset_t toc, vm_offset_t ofentry,
    void *mdp, uint32_t mdp_cookie);
void aim_cpu_init(vm_offset_t toc);

void
aim_early_init(vm_offset_t fdt, vm_offset_t toc, vm_offset_t ofentry, void *mdp,
    uint32_t mdp_cookie)
{
	register_t	scratch;

	/*
	 * If running from an FDT, make sure we are in real mode to avoid
	 * tromping on firmware page tables. Everything in the kernel assumes
	 * 1:1 mappings out of firmware, so this won't break anything not
	 * already broken. This doesn't work if there is live OF, since OF
	 * may internally use non-1:1 mappings.
	 */
	if (ofentry == 0)
		mtmsr(mfmsr() & ~(PSL_IR | PSL_DR));

#ifdef __powerpc64__
	/*
	 * If in real mode, relocate to high memory so that the kernel
	 * can execute from the direct map.
	 */
	if (!(mfmsr() & PSL_DR) &&
	    (vm_offset_t)&aim_early_init < DMAP_BASE_ADDRESS)
		__restartkernel(fdt, 0, ofentry, mdp, mdp_cookie,
		    DMAP_BASE_ADDRESS, mfmsr());
#endif

	/* Various very early CPU fix ups */
	switch (mfpvr() >> 16) {
		/*
		 * PowerPC 970 CPUs have a misfeature requested by Apple that
		 * makes them pretend they have a 32-byte cacheline. Turn this
		 * off before we measure the cacheline size.
		 */
		case IBM970:
		case IBM970FX:
		case IBM970MP:
		case IBM970GX:
			scratch = mfspr(SPR_HID5);
			scratch &= ~HID5_970_DCBZ_SIZE_HI;
			mtspr(SPR_HID5, scratch);
			break;
	#ifdef __powerpc64__
		case IBMPOWER7:
		case IBMPOWER7PLUS:
		case IBMPOWER8:
		case IBMPOWER8E:
		case IBMPOWER9:
			/* XXX: get from ibm,slb-size in device tree */
			n_slbs = 32;
			break;
	#endif
	}
}

void
aim_cpu_init(vm_offset_t toc)
{
	size_t		trap_offset, trapsize;
	vm_offset_t	trap;
	register_t	msr;
	uint8_t		*cache_check;
	int		cacheline_warn;
#ifndef __powerpc64__
	register_t	scratch;
	int		ppc64;
#endif

	trap_offset = 0;
	cacheline_warn = 0;

	/* General setup for AIM CPUs */
	psl_kernset = PSL_EE | PSL_ME | PSL_IR | PSL_DR | PSL_RI;

#ifdef __powerpc64__
	psl_kernset |= PSL_SF;
	if (mfmsr() & PSL_HV)
		psl_kernset |= PSL_HV;
#endif
	psl_userset = psl_kernset | PSL_PR;
#ifdef __powerpc64__
	psl_userset32 = psl_userset & ~PSL_SF;
#endif

	/* Bits that users aren't allowed to change */
	psl_userstatic = ~(PSL_VEC | PSL_FP | PSL_FE0 | PSL_FE1);
	/*
	 * Mask bits from the SRR1 that aren't really the MSR:
	 * Bits 1-4, 10-15 (ppc32), 33-36, 42-47 (ppc64)
	 */
	psl_userstatic &= ~0x783f0000UL;

	/*
	 * Initialize the interrupt tables and figure out our cache line
	 * size and whether or not we need the 64-bit bridge code.
	 */

	/*
	 * Disable translation in case the vector area hasn't been
	 * mapped (G5). Note that no OFW calls can be made until
	 * translation is re-enabled.
	 */

	msr = mfmsr();
	mtmsr((msr & ~(PSL_IR | PSL_DR)) | PSL_RI);

	/*
	 * Measure the cacheline size using dcbz
	 *
	 * Use EXC_PGM as a playground. We are about to overwrite it
	 * anyway, we know it exists, and we know it is cache-aligned.
	 */

	cache_check = (void *)EXC_PGM;

	for (cacheline_size = 0; cacheline_size < 0x100; cacheline_size++)
		cache_check[cacheline_size] = 0xff;

	__asm __volatile("dcbz 0,%0":: "r" (cache_check) : "memory");

	/* Find the first byte dcbz did not zero to get the cache line size */
	for (cacheline_size = 0; cacheline_size < 0x100 &&
	    cache_check[cacheline_size] == 0; cacheline_size++);

	/* Work around psim bug */
	if (cacheline_size == 0) {
		cacheline_warn = 1;
		cacheline_size = 32;
	}

	#ifndef __powerpc64__
	/*
	 * Figure out whether we need to use the 64 bit PMAP. This works by
	 * executing an instruction that is only legal on 64-bit PPC (mtmsrd),
	 * and setting ppc64 = 0 if that causes a trap.
	 */

	ppc64 = 1;

	bcopy(&testppc64, (void *)EXC_PGM,  (size_t)&testppc64size);
	__syncicache((void *)EXC_PGM, (size_t)&testppc64size);

	__asm __volatile("\
		mfmsr %0;	\
		mtsprg2 %1;	\
				\
		mtmsrd %0;	\
		mfsprg2 %1;"
	    : "=r"(scratch), "=r"(ppc64));

	if (ppc64)
		cpu_features |= PPC_FEATURE_64;

	/*
	 * Now copy restorebridge into all the handlers, if necessary,
	 * and set up the trap tables.
	 */

	if (cpu_features & PPC_FEATURE_64) {
		/* Patch the two instances of rfi -> rfid */
		bcopy(&rfid_patch,&rfi_patch1,4);
	#ifdef KDB
		/* rfi_patch2 is at the end of dbleave */
		bcopy(&rfid_patch,&rfi_patch2,4);
	#endif
	}
	#else /* powerpc64 */
	cpu_features |= PPC_FEATURE_64;
	#endif

	trapsize = (size_t)&trapcodeend - (size_t)&trapcode;

	/*
	 * Copy generic handler into every possible trap. Special cases will get
	 * different ones in a minute.
	 */
	for (trap = EXC_RST; trap < EXC_LAST; trap += 0x20)
		bcopy(&trapcode, (void *)trap, trapsize);

	#ifndef __powerpc64__
	if (cpu_features & PPC_FEATURE_64) {
		/*
		 * Copy a code snippet to restore 32-bit bridge mode
		 * to the top of every non-generic trap handler
		 */

		trap_offset += (size_t)&restorebridgesize;
		bcopy(&restorebridge, (void *)EXC_RST, trap_offset);
		bcopy(&restorebridge, (void *)EXC_DSI, trap_offset);
		bcopy(&restorebridge, (void *)EXC_ALI, trap_offset);
		bcopy(&restorebridge, (void *)EXC_PGM, trap_offset);
		bcopy(&restorebridge, (void *)EXC_MCHK, trap_offset);
		bcopy(&restorebridge, (void *)EXC_TRC, trap_offset);
		bcopy(&restorebridge, (void *)EXC_BPT, trap_offset);
	}
	#else
	trapsize = (size_t)&hypertrapcodeend - (size_t)&hypertrapcode;
	bcopy(&hypertrapcode, (void *)(EXC_HEA + trap_offset), trapsize);
	bcopy(&hypertrapcode, (void *)(EXC_HMI + trap_offset), trapsize);
	bcopy(&hypertrapcode, (void *)(EXC_HVI + trap_offset), trapsize);
	bcopy(&hypertrapcode, (void *)(EXC_SOFT_PATCH + trap_offset), trapsize);
	#endif

	bcopy(&rstcode, (void *)(EXC_RST + trap_offset), (size_t)&rstcodeend -
	    (size_t)&rstcode);

#ifdef KDB
	bcopy(&dblow, (void *)(EXC_MCHK + trap_offset), (size_t)&dbend -
	    (size_t)&dblow);
	bcopy(&dblow, (void *)(EXC_PGM + trap_offset), (size_t)&dbend -
	    (size_t)&dblow);
	bcopy(&dblow, (void *)(EXC_TRC + trap_offset), (size_t)&dbend -
	    (size_t)&dblow);
	bcopy(&dblow, (void *)(EXC_BPT + trap_offset), (size_t)&dbend -
	    (size_t)&dblow);
#endif
	bcopy(&alitrap,  (void *)(EXC_ALI + trap_offset),  (size_t)&aliend -
	    (size_t)&alitrap);
	bcopy(&dsitrap,  (void *)(EXC_DSI + trap_offset),  (size_t)&dsiend -
	    (size_t)&dsitrap);

	#ifdef __powerpc64__
	/* Set TOC base so that the interrupt code can get at it */
	*((void **)TRAP_GENTRAP) = &generictrap;
	*((register_t *)TRAP_TOCBASE) = toc;
	#else
	/* Set branch address for trap code */
	if (cpu_features & PPC_FEATURE_64)
		*((void **)TRAP_GENTRAP) = &generictrap64;
	else
		*((void **)TRAP_GENTRAP) = &generictrap;
	*((void **)TRAP_TOCBASE) = _GLOBAL_OFFSET_TABLE_;

	/* G2-specific TLB miss helper handlers */
	bcopy(&imisstrap, (void *)EXC_IMISS,  (size_t)&imisssize);
	bcopy(&dlmisstrap, (void *)EXC_DLMISS,  (size_t)&dlmisssize);
	bcopy(&dsmisstrap, (void *)EXC_DSMISS,  (size_t)&dsmisssize);
	#endif
	__syncicache(EXC_RSVD, EXC_LAST - EXC_RSVD);

	/*
	 * Restore MSR
	 */
	mtmsr(msr);

	/* Warn if cachline size was not determined */
	if (cacheline_warn == 1) {
		printf("WARNING: cacheline size undetermined, setting to 32\n");
	}

	/*
	 * Initialise virtual memory. Use BUS_PROBE_GENERIC priority
	 * in case the platform module had a better idea of what we
	 * should do.
	 */
	if (cpu_features & PPC_FEATURE_64)
		pmap_mmu_install(MMU_TYPE_G5, BUS_PROBE_GENERIC);
	else
		pmap_mmu_install(MMU_TYPE_OEA, BUS_PROBE_GENERIC);
}

/*
 * Shutdown the CPU as much as possible.
 */
void
cpu_halt(void)
{

	OF_exit();
}

int
ptrace_single_step(struct thread *td)
{
	struct trapframe *tf;

	tf = td->td_frame;
	tf->srr1 |= PSL_SE;

	return (0);
}

int
ptrace_clear_single_step(struct thread *td)
{
	struct trapframe *tf;

	tf = td->td_frame;
	tf->srr1 &= ~PSL_SE;

	return (0);
}

void
kdb_cpu_clear_singlestep(void)
{

	kdb_frame->srr1 &= ~PSL_SE;
}

void
kdb_cpu_set_singlestep(void)
{

	kdb_frame->srr1 |= PSL_SE;
}

/*
 * Initialise a struct pcpu.
 */
void
cpu_pcpu_init(struct pcpu *pcpu, int cpuid, size_t sz)
{
#ifdef __powerpc64__
/* Copy the SLB contents from the current CPU */
memcpy(pcpu->pc_aim.slb, PCPU_GET(aim.slb), sizeof(pcpu->pc_aim.slb));
#endif
}

#ifndef __powerpc64__
uint64_t
va_to_vsid(pmap_t pm, vm_offset_t va)
{
	return ((pm->pm_sr[(uintptr_t)va >> ADDR_SR_SHFT]) & SR_VSID_MASK);
}

#endif

/*
 * These functions need to provide addresses that both (a) work in real mode
 * (or whatever mode/circumstances the kernel is in in early boot (now)) and
 * (b) can still, in principle, work once the kernel is going. Because these
 * rely on existing mappings/real mode, unmap is a no-op.
 */
vm_offset_t
pmap_early_io_map(vm_paddr_t pa, vm_size_t size)
{
	KASSERT(!pmap_bootstrapped, ("Not available after PMAP started!"));

	/*
	 * If we have the MMU up in early boot, assume it is 1:1. Otherwise,
	 * try to get the address in a memory region compatible with the
	 * direct map for efficiency later.
	 */
	if (mfmsr() & PSL_DR)
		return (pa);
	else
		return (DMAP_BASE_ADDRESS + pa);
}

void
pmap_early_io_unmap(vm_offset_t va, vm_size_t size)
{

	KASSERT(!pmap_bootstrapped, ("Not available after PMAP started!"));
}

/* From p3-53 of the MPC7450 RISC Microprocessor Family Reference Manual */
void
flush_disable_caches(void)
{
	register_t msr;
	register_t msscr0;
	register_t cache_reg;
	volatile uint32_t *memp;
	uint32_t temp;
	int i;
	int x;

	msr = mfmsr();
	powerpc_sync();
	mtmsr(msr & ~(PSL_EE | PSL_DR));
	msscr0 = mfspr(SPR_MSSCR0);
	msscr0 &= ~MSSCR0_L2PFE;
	mtspr(SPR_MSSCR0, msscr0);
	powerpc_sync();
	isync();
	__asm__ __volatile__("dssall; sync");
	powerpc_sync();
	isync();
	__asm__ __volatile__("dcbf 0,%0" :: "r"(0));
	__asm__ __volatile__("dcbf 0,%0" :: "r"(0));
	__asm__ __volatile__("dcbf 0,%0" :: "r"(0));

	/* Lock the L1 Data cache. */
	mtspr(SPR_LDSTCR, mfspr(SPR_LDSTCR) | 0xFF);
	powerpc_sync();
	isync();

	mtspr(SPR_LDSTCR, 0);

	/*
	 * Perform this in two stages: Flush the cache starting in RAM, then do it
	 * from ROM.
	 */
	memp = (volatile uint32_t *)0x00000000;
	for (i = 0; i < 128 * 1024; i++) {
		temp = *memp;
		__asm__ __volatile__("dcbf 0,%0" :: "r"(memp));
		memp += 32/sizeof(*memp);
	}

	memp = (volatile uint32_t *)0xfff00000;
	x = 0xfe;

	for (; x != 0xff;) {
		mtspr(SPR_LDSTCR, x);
		for (i = 0; i < 128; i++) {
			temp = *memp;
			__asm__ __volatile__("dcbf 0,%0" :: "r"(memp));
			memp += 32/sizeof(*memp);
		}
		x = ((x << 1) | 1) & 0xff;
	}
	mtspr(SPR_LDSTCR, 0);

	cache_reg = mfspr(SPR_L2CR);
	if (cache_reg & L2CR_L2E) {
		cache_reg &= ~(L2CR_L2IO_7450 | L2CR_L2DO_7450);
		mtspr(SPR_L2CR, cache_reg);
		powerpc_sync();
		mtspr(SPR_L2CR, cache_reg | L2CR_L2HWF);
		while (mfspr(SPR_L2CR) & L2CR_L2HWF)
			; /* Busy wait for cache to flush */
		powerpc_sync();
		cache_reg &= ~L2CR_L2E;
		mtspr(SPR_L2CR, cache_reg);
		powerpc_sync();
		mtspr(SPR_L2CR, cache_reg | L2CR_L2I);
		powerpc_sync();
		while (mfspr(SPR_L2CR) & L2CR_L2I)
			; /* Busy wait for L2 cache invalidate */
		powerpc_sync();
	}

	cache_reg = mfspr(SPR_L3CR);
	if (cache_reg & L3CR_L3E) {
		cache_reg &= ~(L3CR_L3IO | L3CR_L3DO);
		mtspr(SPR_L3CR, cache_reg);
		powerpc_sync();
		mtspr(SPR_L3CR, cache_reg | L3CR_L3HWF);
		while (mfspr(SPR_L3CR) & L3CR_L3HWF)
			; /* Busy wait for cache to flush */
		powerpc_sync();
		cache_reg &= ~L3CR_L3E;
		mtspr(SPR_L3CR, cache_reg);
		powerpc_sync();
		mtspr(SPR_L3CR, cache_reg | L3CR_L3I);
		powerpc_sync();
		while (mfspr(SPR_L3CR) & L3CR_L3I)
			; /* Busy wait for L3 cache invalidate */
		powerpc_sync();
	}

	mtspr(SPR_HID0, mfspr(SPR_HID0) & ~HID0_DCE);
	powerpc_sync();
	isync();

	mtmsr(msr);
}

void
cpu_sleep()
{
	static u_quad_t timebase = 0;
	static register_t sprgs[4];
	static register_t srrs[2];

	jmp_buf resetjb;
	struct thread *fputd;
	struct thread *vectd;
	register_t hid0;
	register_t msr;
	register_t saved_msr;

	ap_pcpu = pcpup;

	PCPU_SET(restore, &resetjb);

	saved_msr = mfmsr();
	fputd = PCPU_GET(fputhread);
	vectd = PCPU_GET(vecthread);
	if (fputd != NULL)
		save_fpu(fputd);
	if (vectd != NULL)
		save_vec(vectd);
	if (setjmp(resetjb) == 0) {
		sprgs[0] = mfspr(SPR_SPRG0);
		sprgs[1] = mfspr(SPR_SPRG1);
		sprgs[2] = mfspr(SPR_SPRG2);
		sprgs[3] = mfspr(SPR_SPRG3);
		srrs[0] = mfspr(SPR_SRR0);
		srrs[1] = mfspr(SPR_SRR1);
		timebase = mftb();
		powerpc_sync();
		flush_disable_caches();
		hid0 = mfspr(SPR_HID0);
		hid0 = (hid0 & ~(HID0_DOZE | HID0_NAP)) | HID0_SLEEP;
		powerpc_sync();
		isync();
		msr = mfmsr() | PSL_POW;
		mtspr(SPR_HID0, hid0);
		powerpc_sync();

		while (1)
			mtmsr(msr);
	}
	platform_smp_timebase_sync(timebase, 0);
	PCPU_SET(curthread, curthread);
	PCPU_SET(curpcb, curthread->td_pcb);
	pmap_activate(curthread);
	powerpc_sync();
	mtspr(SPR_SPRG0, sprgs[0]);
	mtspr(SPR_SPRG1, sprgs[1]);
	mtspr(SPR_SPRG2, sprgs[2]);
	mtspr(SPR_SPRG3, sprgs[3]);
	mtspr(SPR_SRR0, srrs[0]);
	mtspr(SPR_SRR1, srrs[1]);
	mtmsr(saved_msr);
	if (fputd == curthread)
		enable_fpu(curthread);
	if (vectd == curthread)
		enable_vec(curthread);
	powerpc_sync();
}

