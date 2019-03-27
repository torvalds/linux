/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
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
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/smp.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/hid.h>
#include <machine/intr_machdep.h>
#include <machine/pcb.h>
#include <machine/psl.h>
#include <machine/smp.h>
#include <machine/spr.h>
#include <machine/trap.h>

#include <dev/ofw/openfirm.h>
#include <machine/ofw_machdep.h>

void *ap_pcpu;

static register_t bsp_state[8] __aligned(8);

static void cpudep_save_config(void *dummy);
SYSINIT(cpu_save_config, SI_SUB_CPU, SI_ORDER_ANY, cpudep_save_config, NULL);

void
cpudep_ap_early_bootstrap(void)
{
#ifndef __powerpc64__
	register_t reg;
#endif

	switch (mfpvr() >> 16) {
	case IBM970:
	case IBM970FX:
	case IBM970MP:
		/* Restore HID4 and HID5, which are necessary for the MMU */

#ifdef __powerpc64__
		mtspr(SPR_HID4, bsp_state[2]); powerpc_sync(); isync();
		mtspr(SPR_HID5, bsp_state[3]); powerpc_sync(); isync();
#else
		__asm __volatile("ld %0, 16(%2); sync; isync;	\
		    mtspr %1, %0; sync; isync;"
		    : "=r"(reg) : "K"(SPR_HID4), "b"(bsp_state));
		__asm __volatile("ld %0, 24(%2); sync; isync;	\
		    mtspr %1, %0; sync; isync;"
		    : "=r"(reg) : "K"(SPR_HID5), "b"(bsp_state));
#endif
		powerpc_sync();
		break;
	case IBMPOWER8:
	case IBMPOWER8E:
	case IBMPOWER9:
#ifdef __powerpc64__
		if (mfmsr() & PSL_HV) {
			isync();
			/*
			 * Direct interrupts to SRR instead of HSRR and
			 * reset LPCR otherwise
			 */
			mtspr(SPR_LPID, 0);
			isync();

			mtspr(SPR_LPCR, lpcr);
			isync();
		}
#endif
		break;
	}

	__asm __volatile("mtsprg 0, %0" :: "r"(ap_pcpu));
	powerpc_sync();
}

uintptr_t
cpudep_ap_bootstrap(void)
{
	register_t msr, sp;

	msr = psl_kernset & ~PSL_EE;
	mtmsr(msr);

	pcpup->pc_curthread = pcpup->pc_idlethread;
#ifdef __powerpc64__
	__asm __volatile("mr 13,%0" :: "r"(pcpup->pc_curthread));
#else
	__asm __volatile("mr 2,%0" :: "r"(pcpup->pc_curthread));
#endif
	pcpup->pc_curpcb = pcpup->pc_curthread->td_pcb;
	sp = pcpup->pc_curpcb->pcb_sp;

	return (sp);
}

static register_t
mpc74xx_l2_enable(register_t l2cr_config)
{
	register_t ccr, bit;
	uint16_t	vers;

	vers = mfpvr() >> 16;
	switch (vers) {
	case MPC7400:
	case MPC7410:
		bit = L2CR_L2IP;
		break;
	default:
		bit = L2CR_L2I;
		break;
	}

	ccr = mfspr(SPR_L2CR);
	if (ccr & L2CR_L2E)
		return (ccr);

	/* Configure L2 cache. */
	ccr = l2cr_config & ~L2CR_L2E;
	mtspr(SPR_L2CR, ccr | L2CR_L2I);
	do {
		ccr = mfspr(SPR_L2CR);
	} while (ccr & bit);
	powerpc_sync();
	mtspr(SPR_L2CR, l2cr_config);
	powerpc_sync();

	return (l2cr_config);
}

static register_t
mpc745x_l3_enable(register_t l3cr_config)
{
	register_t ccr;

	ccr = mfspr(SPR_L3CR);
	if (ccr & L3CR_L3E)
		return (ccr);

	/* Configure L3 cache. */
	ccr = l3cr_config & ~(L3CR_L3E | L3CR_L3I | L3CR_L3PE | L3CR_L3CLKEN);
	mtspr(SPR_L3CR, ccr);
	ccr |= 0x4000000;       /* Magic, but documented. */
	mtspr(SPR_L3CR, ccr);
	ccr |= L3CR_L3CLKEN;
	mtspr(SPR_L3CR, ccr);
	mtspr(SPR_L3CR, ccr | L3CR_L3I);
	while (mfspr(SPR_L3CR) & L3CR_L3I)
		;
	mtspr(SPR_L3CR, ccr & ~L3CR_L3CLKEN);
	powerpc_sync();
	DELAY(100);
	mtspr(SPR_L3CR, ccr);
	powerpc_sync();
	DELAY(100);
	ccr |= L3CR_L3E;
	mtspr(SPR_L3CR, ccr);
	powerpc_sync();

	return(ccr);
}

static register_t
mpc74xx_l1d_enable(void)
{
	register_t hid;

	hid = mfspr(SPR_HID0);
	if (hid & HID0_DCE)
		return (hid);

	/* Enable L1 D-cache */
	hid |= HID0_DCE;
	powerpc_sync();
	mtspr(SPR_HID0, hid | HID0_DCFI);
	powerpc_sync();

	return (hid);
}

static register_t
mpc74xx_l1i_enable(void)
{
	register_t hid;

	hid = mfspr(SPR_HID0);
	if (hid & HID0_ICE)
		return (hid);

	/* Enable L1 I-cache */
	hid |= HID0_ICE;
	isync();
	mtspr(SPR_HID0, hid | HID0_ICFI);
	isync();

	return (hid);
}

static void
cpudep_save_config(void *dummy)
{
	uint16_t	vers;

	vers = mfpvr() >> 16;

	switch(vers) {
	case IBM970:
	case IBM970FX:
	case IBM970MP:
		#ifdef __powerpc64__
		bsp_state[0] = mfspr(SPR_HID0);
		bsp_state[1] = mfspr(SPR_HID1);
		bsp_state[2] = mfspr(SPR_HID4);
		bsp_state[3] = mfspr(SPR_HID5);
		#else
		__asm __volatile ("mfspr %0,%2; mr %1,%0; srdi %0,%0,32"
		    : "=r" (bsp_state[0]),"=r" (bsp_state[1]) : "K" (SPR_HID0));
		__asm __volatile ("mfspr %0,%2; mr %1,%0; srdi %0,%0,32"
		    : "=r" (bsp_state[2]),"=r" (bsp_state[3]) : "K" (SPR_HID1));
		__asm __volatile ("mfspr %0,%2; mr %1,%0; srdi %0,%0,32"
		    : "=r" (bsp_state[4]),"=r" (bsp_state[5]) : "K" (SPR_HID4));
		__asm __volatile ("mfspr %0,%2; mr %1,%0; srdi %0,%0,32"
		    : "=r" (bsp_state[6]),"=r" (bsp_state[7]) : "K" (SPR_HID5));
		#endif

		powerpc_sync();

		break;
	case IBMCELLBE:
		#ifdef NOTYET /* Causes problems if in instruction stream on 970 */
		if (mfmsr() & PSL_HV) {
			bsp_state[0] = mfspr(SPR_HID0);
			bsp_state[1] = mfspr(SPR_HID1);
			bsp_state[2] = mfspr(SPR_HID4);
			bsp_state[3] = mfspr(SPR_HID6);

			bsp_state[4] = mfspr(SPR_CELL_TSCR);
		}
		#endif

		bsp_state[5] = mfspr(SPR_CELL_TSRL);

		break;
	case MPC7450:
	case MPC7455:
	case MPC7457:
		/* Only MPC745x CPUs have an L3 cache. */
		bsp_state[3] = mfspr(SPR_L3CR);

		/* Fallthrough */
	case MPC7400:
	case MPC7410:
	case MPC7447A:
	case MPC7448:
		bsp_state[2] = mfspr(SPR_L2CR);
		bsp_state[1] = mfspr(SPR_HID1);
		bsp_state[0] = mfspr(SPR_HID0);
		break;
	}
}

void
cpudep_ap_setup()
{ 
	register_t	reg;
	uint16_t	vers;

	vers = mfpvr() >> 16;

	/* The following is needed for restoring from sleep. */
	platform_smp_timebase_sync(0, 1);

	switch(vers) {
	case IBM970:
	case IBM970FX:
	case IBM970MP:
		/* Set HIOR to 0 */
		__asm __volatile("mtspr 311,%0" :: "r"(0));
		powerpc_sync();

		/*
		 * The 970 has strange rules about how to update HID registers.
		 * See Table 2-3, 970MP manual
		 *
		 * Note: HID4 and HID5 restored already in
		 * cpudep_ap_early_bootstrap()
		 */

		__asm __volatile("mtasr %0; sync" :: "r"(0));
	#ifdef __powerpc64__
		__asm __volatile(" \
			sync; isync;					\
			mtspr	%1, %0;					\
			mfspr	%0, %1;	mfspr	%0, %1;	mfspr	%0, %1;	\
			mfspr	%0, %1;	mfspr	%0, %1;	mfspr	%0, %1; \
			sync; isync" 
		    :: "r"(bsp_state[0]), "K"(SPR_HID0));
		__asm __volatile("sync; isync;	\
		    mtspr %1, %0; mtspr %1, %0; sync; isync"
		    :: "r"(bsp_state[1]), "K"(SPR_HID1));
	#else
		__asm __volatile(" \
			ld	%0,0(%2);				\
			sync; isync;					\
			mtspr	%1, %0;					\
			mfspr	%0, %1;	mfspr	%0, %1;	mfspr	%0, %1;	\
			mfspr	%0, %1;	mfspr	%0, %1;	mfspr	%0, %1; \
			sync; isync" 
		    : "=r"(reg) : "K"(SPR_HID0), "b"(bsp_state));
		__asm __volatile("ld %0, 8(%2); sync; isync;	\
		    mtspr %1, %0; mtspr %1, %0; sync; isync"
		    : "=r"(reg) : "K"(SPR_HID1), "b"(bsp_state));
	#endif

		powerpc_sync();
		break;
	case IBMCELLBE:
		#ifdef NOTYET /* Causes problems if in instruction stream on 970 */
		if (mfmsr() & PSL_HV) {
			mtspr(SPR_HID0, bsp_state[0]);
			mtspr(SPR_HID1, bsp_state[1]);
			mtspr(SPR_HID4, bsp_state[2]);
			mtspr(SPR_HID6, bsp_state[3]);

			mtspr(SPR_CELL_TSCR, bsp_state[4]);
		}
		#endif

		mtspr(SPR_CELL_TSRL, bsp_state[5]);

		break;
	case MPC7400:
	case MPC7410:
	case MPC7447A:
	case MPC7448:
	case MPC7450:
	case MPC7455:
	case MPC7457:
		/* XXX: Program the CPU ID into PIR */
		__asm __volatile("mtspr 1023,%0" :: "r"(PCPU_GET(cpuid)));

		powerpc_sync();
		isync();

		mtspr(SPR_HID0, bsp_state[0]); isync();
		mtspr(SPR_HID1, bsp_state[1]); isync();

		/* Now enable the L3 cache. */
		switch (vers) {
		case MPC7450:
		case MPC7455:
		case MPC7457:
			/* Only MPC745x CPUs have an L3 cache. */
			reg = mpc745x_l3_enable(bsp_state[3]);
		default:
			break;
		}
		
		reg = mpc74xx_l2_enable(bsp_state[2]);
		reg = mpc74xx_l1d_enable();
		reg = mpc74xx_l1i_enable();

		break;
	case IBMPOWER7:
	case IBMPOWER7PLUS:
	case IBMPOWER8:
	case IBMPOWER8E:
	case IBMPOWER9:
#ifdef __powerpc64__
		if (mfmsr() & PSL_HV) {
			mtspr(SPR_LPCR, mfspr(SPR_LPCR) | lpcr |
			    LPCR_PECE_WAKESET);
			isync();
		}
#endif
		break;
	default:
#ifdef __powerpc64__
		if (!(mfmsr() & PSL_HV)) /* Rely on HV to have set things up */
			break;
#endif
		printf("WARNING: Unknown CPU type. Cache performace may be "
		    "suboptimal.\n");
		break;
	}
}

