/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) KATO Takenori, 1997, 1998.
 * 
 * All rights reserved.  Unpublished rights reserved under the copyright
 * laws of Japan.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified.
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

#include "opt_cpu.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/sysctl.h>

#include <machine/cputypes.h>
#include <machine/md_var.h>
#include <machine/specialreg.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#ifdef I486_CPU
static void init_5x86(void);
static void init_bluelightning(void);
static void init_486dlc(void);
static void init_cy486dx(void);
#ifdef CPU_I486_ON_386
static void init_i486_on_386(void);
#endif
static void init_6x86(void);
#endif /* I486_CPU */

#if defined(I586_CPU) && defined(CPU_WT_ALLOC)
static void	enable_K5_wt_alloc(void);
static void	enable_K6_wt_alloc(void);
static void	enable_K6_2_wt_alloc(void);
#endif

#ifdef I686_CPU
static void	init_6x86MX(void);
static void	init_ppro(void);
static void	init_mendocino(void);
#endif

static int	hw_instruction_sse;
SYSCTL_INT(_hw, OID_AUTO, instruction_sse, CTLFLAG_RD,
    &hw_instruction_sse, 0, "SIMD/MMX2 instructions available in CPU");
/*
 * -1: automatic (default)
 *  0: keep enable CLFLUSH
 *  1: force disable CLFLUSH
 */
static int	hw_clflush_disable = -1;

u_int	cyrix_did;		/* Device ID of Cyrix CPU */

#ifdef I486_CPU
/*
 * IBM Blue Lightning
 */
static void
init_bluelightning(void)
{
	register_t saveintr;

	saveintr = intr_disable();

	load_cr0(rcr0() | CR0_CD | CR0_NW);
	invd();

#ifdef CPU_BLUELIGHTNING_FPU_OP_CACHE
	wrmsr(0x1000, 0x9c92LL);	/* FP operand can be cacheable on Cyrix FPU */
#else
	wrmsr(0x1000, 0x1c92LL);	/* Intel FPU */
#endif
	/* Enables 13MB and 0-640KB cache. */
	wrmsr(0x1001, (0xd0LL << 32) | 0x3ff);
#ifdef CPU_BLUELIGHTNING_3X
	wrmsr(0x1002, 0x04000000LL);	/* Enables triple-clock mode. */
#else
	wrmsr(0x1002, 0x03000000LL);	/* Enables double-clock mode. */
#endif

	/* Enable caching in CR0. */
	load_cr0(rcr0() & ~(CR0_CD | CR0_NW));	/* CD = 0 and NW = 0 */
	invd();
	intr_restore(saveintr);
}

/*
 * Cyrix 486SLC/DLC/SR/DR series
 */
static void
init_486dlc(void)
{
	register_t saveintr;
	u_char	ccr0;

	saveintr = intr_disable();
	invd();

	ccr0 = read_cyrix_reg(CCR0);
#ifndef CYRIX_CACHE_WORKS
	ccr0 |= CCR0_NC1 | CCR0_BARB;
	write_cyrix_reg(CCR0, ccr0);
	invd();
#else
	ccr0 &= ~CCR0_NC0;
#ifndef CYRIX_CACHE_REALLY_WORKS
	ccr0 |= CCR0_NC1 | CCR0_BARB;
#else
	ccr0 |= CCR0_NC1;
#endif
#ifdef CPU_DIRECT_MAPPED_CACHE
	ccr0 |= CCR0_CO;			/* Direct mapped mode. */
#endif
	write_cyrix_reg(CCR0, ccr0);

	/* Clear non-cacheable region. */
	write_cyrix_reg(NCR1+2, NCR_SIZE_0K);
	write_cyrix_reg(NCR2+2, NCR_SIZE_0K);
	write_cyrix_reg(NCR3+2, NCR_SIZE_0K);
	write_cyrix_reg(NCR4+2, NCR_SIZE_0K);

	write_cyrix_reg(0, 0);	/* dummy write */

	/* Enable caching in CR0. */
	load_cr0(rcr0() & ~(CR0_CD | CR0_NW));	/* CD = 0 and NW = 0 */
	invd();
#endif /* !CYRIX_CACHE_WORKS */
	intr_restore(saveintr);
}


/*
 * Cyrix 486S/DX series
 */
static void
init_cy486dx(void)
{
	register_t saveintr;
	u_char	ccr2;

	saveintr = intr_disable();
	invd();

	ccr2 = read_cyrix_reg(CCR2);
#ifdef CPU_SUSP_HLT
	ccr2 |= CCR2_SUSP_HLT;
#endif

	write_cyrix_reg(CCR2, ccr2);
	intr_restore(saveintr);
}


/*
 * Cyrix 5x86
 */
static void
init_5x86(void)
{
	register_t saveintr;
	u_char	ccr2, ccr3, ccr4, pcr0;

	saveintr = intr_disable();

	load_cr0(rcr0() | CR0_CD | CR0_NW);
	wbinvd();

	(void)read_cyrix_reg(CCR3);		/* dummy */

	/* Initialize CCR2. */
	ccr2 = read_cyrix_reg(CCR2);
	ccr2 |= CCR2_WB;
#ifdef CPU_SUSP_HLT
	ccr2 |= CCR2_SUSP_HLT;
#else
	ccr2 &= ~CCR2_SUSP_HLT;
#endif
	ccr2 |= CCR2_WT1;
	write_cyrix_reg(CCR2, ccr2);

	/* Initialize CCR4. */
	ccr3 = read_cyrix_reg(CCR3);
	write_cyrix_reg(CCR3, CCR3_MAPEN0);

	ccr4 = read_cyrix_reg(CCR4);
	ccr4 |= CCR4_DTE;
	ccr4 |= CCR4_MEM;
#ifdef CPU_FASTER_5X86_FPU
	ccr4 |= CCR4_FASTFPE;
#else
	ccr4 &= ~CCR4_FASTFPE;
#endif
	ccr4 &= ~CCR4_IOMASK;
	/********************************************************************
	 * WARNING: The "BIOS Writers Guide" mentions that I/O recovery time
	 * should be 0 for errata fix.
	 ********************************************************************/
#ifdef CPU_IORT
	ccr4 |= CPU_IORT & CCR4_IOMASK;
#endif
	write_cyrix_reg(CCR4, ccr4);

	/* Initialize PCR0. */
	/****************************************************************
	 * WARNING: RSTK_EN and LOOP_EN could make your system unstable.
	 * BTB_EN might make your system unstable.
	 ****************************************************************/
	pcr0 = read_cyrix_reg(PCR0);
#ifdef CPU_RSTK_EN
	pcr0 |= PCR0_RSTK;
#else
	pcr0 &= ~PCR0_RSTK;
#endif
#ifdef CPU_BTB_EN
	pcr0 |= PCR0_BTB;
#else
	pcr0 &= ~PCR0_BTB;
#endif
#ifdef CPU_LOOP_EN
	pcr0 |= PCR0_LOOP;
#else
	pcr0 &= ~PCR0_LOOP;
#endif

	/****************************************************************
	 * WARNING: if you use a memory mapped I/O device, don't use
	 * DISABLE_5X86_LSSER option, which may reorder memory mapped
	 * I/O access.
	 * IF YOUR MOTHERBOARD HAS PCI BUS, DON'T DISABLE LSSER.
	 ****************************************************************/
#ifdef CPU_DISABLE_5X86_LSSER
	pcr0 &= ~PCR0_LSSER;
#else
	pcr0 |= PCR0_LSSER;
#endif
	write_cyrix_reg(PCR0, pcr0);

	/* Restore CCR3. */
	write_cyrix_reg(CCR3, ccr3);

	(void)read_cyrix_reg(0x80);		/* dummy */

	/* Unlock NW bit in CR0. */
	write_cyrix_reg(CCR2, read_cyrix_reg(CCR2) & ~CCR2_LOCK_NW);
	load_cr0((rcr0() & ~CR0_CD) | CR0_NW);	/* CD = 0, NW = 1 */
	/* Lock NW bit in CR0. */
	write_cyrix_reg(CCR2, read_cyrix_reg(CCR2) | CCR2_LOCK_NW);

	intr_restore(saveintr);
}

#ifdef CPU_I486_ON_386
/*
 * There are i486 based upgrade products for i386 machines.
 * In this case, BIOS doesn't enable CPU cache.
 */
static void
init_i486_on_386(void)
{
	register_t saveintr;

	saveintr = intr_disable();

	load_cr0(rcr0() & ~(CR0_CD | CR0_NW));	/* CD = 0, NW = 0 */

	intr_restore(saveintr);
}
#endif

/*
 * Cyrix 6x86
 *
 * XXX - What should I do here?  Please let me know.
 */
static void
init_6x86(void)
{
	register_t saveintr;
	u_char	ccr3, ccr4;

	saveintr = intr_disable();

	load_cr0(rcr0() | CR0_CD | CR0_NW);
	wbinvd();

	/* Initialize CCR0. */
	write_cyrix_reg(CCR0, read_cyrix_reg(CCR0) | CCR0_NC1);

	/* Initialize CCR1. */
#ifdef CPU_CYRIX_NO_LOCK
	write_cyrix_reg(CCR1, read_cyrix_reg(CCR1) | CCR1_NO_LOCK);
#else
	write_cyrix_reg(CCR1, read_cyrix_reg(CCR1) & ~CCR1_NO_LOCK);
#endif

	/* Initialize CCR2. */
#ifdef CPU_SUSP_HLT
	write_cyrix_reg(CCR2, read_cyrix_reg(CCR2) | CCR2_SUSP_HLT);
#else
	write_cyrix_reg(CCR2, read_cyrix_reg(CCR2) & ~CCR2_SUSP_HLT);
#endif

	ccr3 = read_cyrix_reg(CCR3);
	write_cyrix_reg(CCR3, CCR3_MAPEN0);

	/* Initialize CCR4. */
	ccr4 = read_cyrix_reg(CCR4);
	ccr4 |= CCR4_DTE;
	ccr4 &= ~CCR4_IOMASK;
#ifdef CPU_IORT
	write_cyrix_reg(CCR4, ccr4 | (CPU_IORT & CCR4_IOMASK));
#else
	write_cyrix_reg(CCR4, ccr4 | 7);
#endif

	/* Initialize CCR5. */
#ifdef CPU_WT_ALLOC
	write_cyrix_reg(CCR5, read_cyrix_reg(CCR5) | CCR5_WT_ALLOC);
#endif

	/* Restore CCR3. */
	write_cyrix_reg(CCR3, ccr3);

	/* Unlock NW bit in CR0. */
	write_cyrix_reg(CCR2, read_cyrix_reg(CCR2) & ~CCR2_LOCK_NW);

	/*
	 * Earlier revision of the 6x86 CPU could crash the system if
	 * L1 cache is in write-back mode.
	 */
	if ((cyrix_did & 0xff00) > 0x1600)
		load_cr0(rcr0() & ~(CR0_CD | CR0_NW));	/* CD = 0 and NW = 0 */
	else {
		/* Revision 2.6 and lower. */
#ifdef CYRIX_CACHE_REALLY_WORKS
		load_cr0(rcr0() & ~(CR0_CD | CR0_NW));	/* CD = 0 and NW = 0 */
#else
		load_cr0((rcr0() & ~CR0_CD) | CR0_NW);	/* CD = 0 and NW = 1 */
#endif
	}

	/* Lock NW bit in CR0. */
	write_cyrix_reg(CCR2, read_cyrix_reg(CCR2) | CCR2_LOCK_NW);

	intr_restore(saveintr);
}
#endif /* I486_CPU */

#ifdef I586_CPU
/*
 * Rise mP6
 */
static void
init_rise(void)
{

	/*
	 * The CMPXCHG8B instruction is always available but hidden.
	 */
	cpu_feature |= CPUID_CX8;
}

/*
 * IDT WinChip C6/2/2A/2B/3
 *
 * http://www.centtech.com/winchip_bios_writers_guide_v4_0.pdf
 */
static void
init_winchip(void)
{
	u_int regs[4];
	uint64_t fcr;

	fcr = rdmsr(0x0107);

	/*
	 * Set ECX8, DSMC, DTLOCK/EDCTLB, EMMX, and ERETSTK and clear DPDC.
	 */
	fcr |= (1 << 1) | (1 << 7) | (1 << 8) | (1 << 9) | (1 << 16);
	fcr &= ~(1ULL << 11);

	/*
	 * Additionally, set EBRPRED, E2MMX and EAMD3D for WinChip 2 and 3.
	 */
	if (CPUID_TO_MODEL(cpu_id) >= 8)
		fcr |= (1 << 12) | (1 << 19) | (1 << 20);

	wrmsr(0x0107, fcr);
	do_cpuid(1, regs);
	cpu_feature = regs[3];
}
#endif

#ifdef I686_CPU
/*
 * Cyrix 6x86MX (code-named M2)
 *
 * XXX - What should I do here?  Please let me know.
 */
static void
init_6x86MX(void)
{
	register_t saveintr;
	u_char	ccr3, ccr4;

	saveintr = intr_disable();

	load_cr0(rcr0() | CR0_CD | CR0_NW);
	wbinvd();

	/* Initialize CCR0. */
	write_cyrix_reg(CCR0, read_cyrix_reg(CCR0) | CCR0_NC1);

	/* Initialize CCR1. */
#ifdef CPU_CYRIX_NO_LOCK
	write_cyrix_reg(CCR1, read_cyrix_reg(CCR1) | CCR1_NO_LOCK);
#else
	write_cyrix_reg(CCR1, read_cyrix_reg(CCR1) & ~CCR1_NO_LOCK);
#endif

	/* Initialize CCR2. */
#ifdef CPU_SUSP_HLT
	write_cyrix_reg(CCR2, read_cyrix_reg(CCR2) | CCR2_SUSP_HLT);
#else
	write_cyrix_reg(CCR2, read_cyrix_reg(CCR2) & ~CCR2_SUSP_HLT);
#endif

	ccr3 = read_cyrix_reg(CCR3);
	write_cyrix_reg(CCR3, CCR3_MAPEN0);

	/* Initialize CCR4. */
	ccr4 = read_cyrix_reg(CCR4);
	ccr4 &= ~CCR4_IOMASK;
#ifdef CPU_IORT
	write_cyrix_reg(CCR4, ccr4 | (CPU_IORT & CCR4_IOMASK));
#else
	write_cyrix_reg(CCR4, ccr4 | 7);
#endif

	/* Initialize CCR5. */
#ifdef CPU_WT_ALLOC
	write_cyrix_reg(CCR5, read_cyrix_reg(CCR5) | CCR5_WT_ALLOC);
#endif

	/* Restore CCR3. */
	write_cyrix_reg(CCR3, ccr3);

	/* Unlock NW bit in CR0. */
	write_cyrix_reg(CCR2, read_cyrix_reg(CCR2) & ~CCR2_LOCK_NW);

	load_cr0(rcr0() & ~(CR0_CD | CR0_NW));	/* CD = 0 and NW = 0 */

	/* Lock NW bit in CR0. */
	write_cyrix_reg(CCR2, read_cyrix_reg(CCR2) | CCR2_LOCK_NW);

	intr_restore(saveintr);
}

static int ppro_apic_used = -1;

static void
init_ppro(void)
{
	u_int64_t	apicbase;

	/*
	 * Local APIC should be disabled if it is not going to be used.
	 */
	if (ppro_apic_used != 1) {
		apicbase = rdmsr(MSR_APICBASE);
		apicbase &= ~APICBASE_ENABLED;
		wrmsr(MSR_APICBASE, apicbase);
		ppro_apic_used = 0;
	}
}

/*
 * If the local APIC is going to be used after being disabled above,
 * re-enable it and don't disable it in the future.
 */
void
ppro_reenable_apic(void)
{
	u_int64_t	apicbase;

	if (ppro_apic_used == 0) {
		apicbase = rdmsr(MSR_APICBASE);
		apicbase |= APICBASE_ENABLED;
		wrmsr(MSR_APICBASE, apicbase);
		ppro_apic_used = 1;
	}
}

/*
 * Initialize BBL_CR_CTL3 (Control register 3: used to configure the
 * L2 cache).
 */
static void
init_mendocino(void)
{
#ifdef CPU_PPRO2CELERON
	register_t	saveintr;
	u_int64_t	bbl_cr_ctl3;

	saveintr = intr_disable();

	load_cr0(rcr0() | CR0_CD | CR0_NW);
	wbinvd();

	bbl_cr_ctl3 = rdmsr(MSR_BBL_CR_CTL3);

	/* If the L2 cache is configured, do nothing. */
	if (!(bbl_cr_ctl3 & 1)) {
		bbl_cr_ctl3 = 0x134052bLL;

		/* Set L2 Cache Latency (Default: 5). */
#ifdef	CPU_CELERON_L2_LATENCY
#if CPU_L2_LATENCY > 15
#error invalid CPU_L2_LATENCY.
#endif
		bbl_cr_ctl3 |= CPU_L2_LATENCY << 1;
#else
		bbl_cr_ctl3 |= 5 << 1;
#endif
		wrmsr(MSR_BBL_CR_CTL3, bbl_cr_ctl3);
	}

	load_cr0(rcr0() & ~(CR0_CD | CR0_NW));
	intr_restore(saveintr);
#endif /* CPU_PPRO2CELERON */
}

/*
 * Initialize special VIA features
 */
static void
init_via(void)
{
	u_int regs[4], val;
	uint64_t fcr;

	/*
	 * Explicitly enable CX8 and PGE on C3.
	 *
	 * http://www.via.com.tw/download/mainboards/6/13/VIA_C3_EBGA%20datasheet110.pdf
	 */
	if (CPUID_TO_MODEL(cpu_id) <= 9)
		fcr = (1 << 1) | (1 << 7);
	else
		fcr = 0;

	/*
	 * Check extended CPUID for PadLock features.
	 *
	 * http://www.via.com.tw/en/downloads/whitepapers/initiatives/padlock/programming_guide.pdf
	 */
	do_cpuid(0xc0000000, regs);
	if (regs[0] >= 0xc0000001) {
		do_cpuid(0xc0000001, regs);
		val = regs[3];
	} else
		val = 0;

	/* Enable RNG if present. */
	if ((val & VIA_CPUID_HAS_RNG) != 0) {
		via_feature_rng = VIA_HAS_RNG;
		wrmsr(0x110B, rdmsr(0x110B) | VIA_CPUID_DO_RNG);
	}

	/* Enable PadLock if present. */
	if ((val & VIA_CPUID_HAS_ACE) != 0)
		via_feature_xcrypt |= VIA_HAS_AES;
	if ((val & VIA_CPUID_HAS_ACE2) != 0)
		via_feature_xcrypt |= VIA_HAS_AESCTR;
	if ((val & VIA_CPUID_HAS_PHE) != 0)
		via_feature_xcrypt |= VIA_HAS_SHA;
	if ((val & VIA_CPUID_HAS_PMM) != 0)
		via_feature_xcrypt |= VIA_HAS_MM;
	if (via_feature_xcrypt != 0)
		fcr |= 1 << 28;

	wrmsr(0x1107, rdmsr(0x1107) | fcr);
}

#endif /* I686_CPU */

#if defined(I586_CPU) || defined(I686_CPU)
static void
init_transmeta(void)
{
	u_int regs[0];

	/* Expose all hidden features. */
	wrmsr(0x80860004, rdmsr(0x80860004) | ~0UL);
	do_cpuid(1, regs);
	cpu_feature = regs[3];
}
#endif

extern int elf32_nxstack;

void
initializecpu(void)
{
	uint64_t msr;

	switch (cpu) {
#ifdef I486_CPU
	case CPU_BLUE:
		init_bluelightning();
		break;
	case CPU_486DLC:
		init_486dlc();
		break;
	case CPU_CY486DX:
		init_cy486dx();
		break;
	case CPU_M1SC:
		init_5x86();
		break;
#ifdef CPU_I486_ON_386
	case CPU_486:
		init_i486_on_386();
		break;
#endif
	case CPU_M1:
		init_6x86();
		break;
#endif /* I486_CPU */
#ifdef I586_CPU
	case CPU_586:
		switch (cpu_vendor_id) {
		case CPU_VENDOR_AMD:
#ifdef CPU_WT_ALLOC
			if (((cpu_id & 0x0f0) > 0) &&
			    ((cpu_id & 0x0f0) < 0x60) &&
			    ((cpu_id & 0x00f) > 3))
				enable_K5_wt_alloc();
			else if (((cpu_id & 0x0f0) > 0x80) ||
			    (((cpu_id & 0x0f0) == 0x80) &&
				(cpu_id & 0x00f) > 0x07))
				enable_K6_2_wt_alloc();
			else if ((cpu_id & 0x0f0) > 0x50)
				enable_K6_wt_alloc();
#endif
			if ((cpu_id & 0xf0) == 0xa0)
				/*
				 * Make sure the TSC runs through
				 * suspension, otherwise we can't use
				 * it as timecounter
				 */
				wrmsr(0x1900, rdmsr(0x1900) | 0x20ULL);
			break;
		case CPU_VENDOR_CENTAUR:
			init_winchip();
			break;
		case CPU_VENDOR_TRANSMETA:
			init_transmeta();
			break;
		case CPU_VENDOR_RISE:
			init_rise();
			break;
		}
		break;
#endif
#ifdef I686_CPU
	case CPU_M2:
		init_6x86MX();
		break;
	case CPU_686:
		switch (cpu_vendor_id) {
		case CPU_VENDOR_INTEL:
			switch (cpu_id & 0xff0) {
			case 0x610:
				init_ppro();
				break;
			case 0x660:
				init_mendocino();
				break;
			}
			break;
#ifdef CPU_ATHLON_SSE_HACK
		case CPU_VENDOR_AMD:
			/*
			 * Sometimes the BIOS doesn't enable SSE instructions.
			 * According to AMD document 20734, the mobile
			 * Duron, the (mobile) Athlon 4 and the Athlon MP
			 * support SSE. These correspond to cpu_id 0x66X
			 * or 0x67X.
			 */
			if ((cpu_feature & CPUID_XMM) == 0 &&
			    ((cpu_id & ~0xf) == 0x660 ||
			     (cpu_id & ~0xf) == 0x670 ||
			     (cpu_id & ~0xf) == 0x680)) {
				u_int regs[4];
				wrmsr(MSR_HWCR, rdmsr(MSR_HWCR) & ~0x08000);
				do_cpuid(1, regs);
				cpu_feature = regs[3];
			}
			break;
#endif
		case CPU_VENDOR_CENTAUR:
			init_via();
			break;
		case CPU_VENDOR_TRANSMETA:
			init_transmeta();
			break;
		}
		break;
#endif
	default:
		break;
	}
	if ((cpu_feature & CPUID_XMM) && (cpu_feature & CPUID_FXSR)) {
		load_cr4(rcr4() | CR4_FXSR | CR4_XMM);
		cpu_fxsr = hw_instruction_sse = 1;
	}
	if (elf32_nxstack) {
		msr = rdmsr(MSR_EFER) | EFER_NXE;
		wrmsr(MSR_EFER, msr);
	}
	if ((amd_feature & AMDID_RDTSCP) != 0 ||
	    (cpu_stdext_feature2 & CPUID_STDEXT2_RDPID) != 0)
		wrmsr(MSR_TSC_AUX, PCPU_GET(cpuid));
}

void
initializecpucache(void)
{

	/*
	 * CPUID with %eax = 1, %ebx returns
	 * Bits 15-8: CLFLUSH line size
	 * 	(Value * 8 = cache line size in bytes)
	 */
	if ((cpu_feature & CPUID_CLFSH) != 0)
		cpu_clflush_line_size = ((cpu_procinfo >> 8) & 0xff) * 8;
	/*
	 * XXXKIB: (temporary) hack to work around traps generated
	 * when CLFLUSHing APIC register window under virtualization
	 * environments.  These environments tend to disable the
	 * CPUID_SS feature even though the native CPU supports it.
	 */
	TUNABLE_INT_FETCH("hw.clflush_disable", &hw_clflush_disable);
	if (vm_guest != VM_GUEST_NO && hw_clflush_disable == -1) {
		cpu_feature &= ~CPUID_CLFSH;
		cpu_stdext_feature &= ~CPUID_STDEXT_CLFLUSHOPT;
	}
	/*
	 * The kernel's use of CLFLUSH{,OPT} can be disabled manually
	 * by setting the hw.clflush_disable tunable.
	 */
	if (hw_clflush_disable == 1) {
		cpu_feature &= ~CPUID_CLFSH;
		cpu_stdext_feature &= ~CPUID_STDEXT_CLFLUSHOPT;
	}
}

#if defined(I586_CPU) && defined(CPU_WT_ALLOC)
/*
 * Enable write allocate feature of AMD processors.
 * Following two functions require the Maxmem variable being set.
 */
static void
enable_K5_wt_alloc(void)
{
	u_int64_t	msr;
	register_t	saveintr;

	/*
	 * Write allocate is supported only on models 1, 2, and 3, with
	 * a stepping of 4 or greater.
	 */
	if (((cpu_id & 0xf0) > 0) && ((cpu_id & 0x0f) > 3)) {
		saveintr = intr_disable();
		msr = rdmsr(0x83);		/* HWCR */
		wrmsr(0x83, msr & !(0x10));

		/*
		 * We have to tell the chip where the top of memory is,
		 * since video cards could have frame bufferes there,
		 * memory-mapped I/O could be there, etc.
		 */
		if(Maxmem > 0)
		  msr = Maxmem / 16;
		else
		  msr = 0;
		msr |= AMD_WT_ALLOC_TME | AMD_WT_ALLOC_FRE;

		/*
		 * There is no way to know wheter 15-16M hole exists or not. 
		 * Therefore, we disable write allocate for this range.
		 */
		wrmsr(0x86, 0x0ff00f0);
		msr |= AMD_WT_ALLOC_PRE;
		wrmsr(0x85, msr);

		msr=rdmsr(0x83);
		wrmsr(0x83, msr|0x10); /* enable write allocate */
		intr_restore(saveintr);
	}
}

static void
enable_K6_wt_alloc(void)
{
	quad_t	size;
	u_int64_t	whcr;
	register_t	saveintr;

	saveintr = intr_disable();
	wbinvd();

#ifdef CPU_DISABLE_CACHE
	/*
	 * Certain K6-2 box becomes unstable when write allocation is
	 * enabled.
	 */
	/*
	 * The AMD-K6 processer provides the 64-bit Test Register 12(TR12),
	 * but only the Cache Inhibit(CI) (bit 3 of TR12) is suppported.
	 * All other bits in TR12 have no effect on the processer's operation.
	 * The I/O Trap Restart function (bit 9 of TR12) is always enabled
	 * on the AMD-K6.
	 */
	wrmsr(0x0000000e, (u_int64_t)0x0008);
#endif
	/* Don't assume that memory size is aligned with 4M. */
	if (Maxmem > 0)
	  size = ((Maxmem >> 8) + 3) >> 2;
	else
	  size = 0;

	/* Limit is 508M bytes. */
	if (size > 0x7f)
		size = 0x7f;
	whcr = (rdmsr(0xc0000082) & ~(0x7fLL << 1)) | (size << 1);

#if defined(NO_MEMORY_HOLE)
	if (whcr & (0x7fLL << 1))
		whcr |=  0x0001LL;
#else
	/*
	 * There is no way to know wheter 15-16M hole exists or not. 
	 * Therefore, we disable write allocate for this range.
	 */
	whcr &= ~0x0001LL;
#endif
	wrmsr(0x0c0000082, whcr);

	intr_restore(saveintr);
}

static void
enable_K6_2_wt_alloc(void)
{
	quad_t	size;
	u_int64_t	whcr;
	register_t	saveintr;

	saveintr = intr_disable();
	wbinvd();

#ifdef CPU_DISABLE_CACHE
	/*
	 * Certain K6-2 box becomes unstable when write allocation is
	 * enabled.
	 */
	/*
	 * The AMD-K6 processer provides the 64-bit Test Register 12(TR12),
	 * but only the Cache Inhibit(CI) (bit 3 of TR12) is suppported.
	 * All other bits in TR12 have no effect on the processer's operation.
	 * The I/O Trap Restart function (bit 9 of TR12) is always enabled
	 * on the AMD-K6.
	 */
	wrmsr(0x0000000e, (u_int64_t)0x0008);
#endif
	/* Don't assume that memory size is aligned with 4M. */
	if (Maxmem > 0)
	  size = ((Maxmem >> 8) + 3) >> 2;
	else
	  size = 0;

	/* Limit is 4092M bytes. */
	if (size > 0x3fff)
		size = 0x3ff;
	whcr = (rdmsr(0xc0000082) & ~(0x3ffLL << 22)) | (size << 22);

#if defined(NO_MEMORY_HOLE)
	if (whcr & (0x3ffLL << 22))
		whcr |=  1LL << 16;
#else
	/*
	 * There is no way to know wheter 15-16M hole exists or not. 
	 * Therefore, we disable write allocate for this range.
	 */
	whcr &= ~(1LL << 16);
#endif
	wrmsr(0x0c0000082, whcr);

	intr_restore(saveintr);
}
#endif /* I585_CPU && CPU_WT_ALLOC */

#include "opt_ddb.h"
#ifdef DDB
#include <ddb/ddb.h>

DB_SHOW_COMMAND(cyrixreg, cyrixreg)
{
	register_t saveintr;
	u_int	cr0;
	u_char	ccr1, ccr2, ccr3;
	u_char	ccr0 = 0, ccr4 = 0, ccr5 = 0, pcr0 = 0;

	cr0 = rcr0();
	if (cpu_vendor_id == CPU_VENDOR_CYRIX) {
		saveintr = intr_disable();


		if ((cpu != CPU_M1SC) && (cpu != CPU_CY486DX)) {
			ccr0 = read_cyrix_reg(CCR0);
		}
		ccr1 = read_cyrix_reg(CCR1);
		ccr2 = read_cyrix_reg(CCR2);
		ccr3 = read_cyrix_reg(CCR3);
		if ((cpu == CPU_M1SC) || (cpu == CPU_M1) || (cpu == CPU_M2)) {
			write_cyrix_reg(CCR3, CCR3_MAPEN0);
			ccr4 = read_cyrix_reg(CCR4);
			if ((cpu == CPU_M1) || (cpu == CPU_M2))
				ccr5 = read_cyrix_reg(CCR5);
			else
				pcr0 = read_cyrix_reg(PCR0);
			write_cyrix_reg(CCR3, ccr3);		/* Restore CCR3. */
		}
		intr_restore(saveintr);

		if ((cpu != CPU_M1SC) && (cpu != CPU_CY486DX))
			printf("CCR0=%x, ", (u_int)ccr0);

		printf("CCR1=%x, CCR2=%x, CCR3=%x",
			(u_int)ccr1, (u_int)ccr2, (u_int)ccr3);
		if ((cpu == CPU_M1SC) || (cpu == CPU_M1) || (cpu == CPU_M2)) {
			printf(", CCR4=%x, ", (u_int)ccr4);
			if (cpu == CPU_M1SC)
				printf("PCR0=%x\n", pcr0);
			else
				printf("CCR5=%x\n", ccr5);
		}
	}
	printf("CR0=%x\n", cr0);
}
#endif /* DDB */
