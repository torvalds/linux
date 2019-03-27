/*-
 * SPDX-License-Identifier: BSD-4-Clause AND BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Matt Thomas.
 * Copyright (c) 2001 Tsubai Masanari.
 * Copyright (c) 1998, 1999, 2001 Internet Research Institute, Inc.
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
 *	This product includes software developed by
 *	Internet Research Institute, Inc.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*-
 * Copyright (C) 2003 Benno Rice.
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
 *
 * from $NetBSD: cpu_subr.c,v 1.1 2003/02/03 17:10:09 matt Exp $
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/cpu.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/sched.h>
#include <sys/smp.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/hid.h>
#include <machine/md_var.h>
#include <machine/smp.h>
#include <machine/spr.h>

#include <dev/ofw/openfirm.h>

static void	cpu_6xx_setup(int cpuid, uint16_t vers);
static void	cpu_970_setup(int cpuid, uint16_t vers);
static void	cpu_booke_setup(int cpuid, uint16_t vers);
static void	cpu_powerx_setup(int cpuid, uint16_t vers);

int powerpc_pow_enabled;
void (*cpu_idle_hook)(sbintime_t) = NULL;
static void	cpu_idle_60x(sbintime_t);
static void	cpu_idle_booke(sbintime_t);
#ifdef BOOKE_E500
static void	cpu_idle_e500mc(sbintime_t sbt);
#endif
#if defined(__powerpc64__) && defined(AIM)
static void	cpu_idle_powerx(sbintime_t);
static void	cpu_idle_power9(sbintime_t);
#endif

struct cputab {
	const char	*name;
	uint16_t	version;
	uint16_t	revfmt;
	int		features;	/* Do not include PPC_FEATURE_32 or
					 * PPC_FEATURE_HAS_MMU */
	int		features2;
	void		(*cpu_setup)(int cpuid, uint16_t vers);
};
#define	REVFMT_MAJMIN	1	/* %u.%u */
#define	REVFMT_HEX	2	/* 0x%04x */
#define	REVFMT_DEC	3	/* %u */
static const struct cputab models[] = {
        { "Motorola PowerPC 601",	MPC601,		REVFMT_DEC,
	   PPC_FEATURE_HAS_FPU | PPC_FEATURE_UNIFIED_CACHE, 0, cpu_6xx_setup },
        { "Motorola PowerPC 602",	MPC602,		REVFMT_DEC,
	   PPC_FEATURE_HAS_FPU, 0, cpu_6xx_setup },
        { "Motorola PowerPC 603",	MPC603,		REVFMT_MAJMIN,
	   PPC_FEATURE_HAS_FPU, 0, cpu_6xx_setup },
        { "Motorola PowerPC 603e",	MPC603e,	REVFMT_MAJMIN,
	   PPC_FEATURE_HAS_FPU, 0, cpu_6xx_setup },
        { "Motorola PowerPC 603ev",	MPC603ev,	REVFMT_MAJMIN,
	   PPC_FEATURE_HAS_FPU, 0, cpu_6xx_setup },
        { "Motorola PowerPC 604",	MPC604,		REVFMT_MAJMIN,
	   PPC_FEATURE_HAS_FPU, 0, cpu_6xx_setup },
        { "Motorola PowerPC 604ev",	MPC604ev,	REVFMT_MAJMIN,
	   PPC_FEATURE_HAS_FPU, 0, cpu_6xx_setup },
        { "Motorola PowerPC 620",	MPC620,		REVFMT_HEX,
	   PPC_FEATURE_64 | PPC_FEATURE_HAS_FPU, 0, NULL },
        { "Motorola PowerPC 750",	MPC750,		REVFMT_MAJMIN,
	   PPC_FEATURE_HAS_FPU, 0, cpu_6xx_setup },
        { "IBM PowerPC 750FX",		IBM750FX,	REVFMT_MAJMIN,
	   PPC_FEATURE_HAS_FPU, 0, cpu_6xx_setup },
        { "IBM PowerPC 970",		IBM970,		REVFMT_MAJMIN,
	   PPC_FEATURE_64 | PPC_FEATURE_HAS_ALTIVEC | PPC_FEATURE_HAS_FPU,
	   0, cpu_970_setup },
        { "IBM PowerPC 970FX",		IBM970FX,	REVFMT_MAJMIN,
	   PPC_FEATURE_64 | PPC_FEATURE_HAS_ALTIVEC | PPC_FEATURE_HAS_FPU,
	   0, cpu_970_setup },
        { "IBM PowerPC 970GX",		IBM970GX,	REVFMT_MAJMIN,
	   PPC_FEATURE_64 | PPC_FEATURE_HAS_ALTIVEC | PPC_FEATURE_HAS_FPU,
	   0, cpu_970_setup },
        { "IBM PowerPC 970MP",		IBM970MP,	REVFMT_MAJMIN,
	   PPC_FEATURE_64 | PPC_FEATURE_HAS_ALTIVEC | PPC_FEATURE_HAS_FPU,
	   0, cpu_970_setup },
        { "IBM POWER4",		IBMPOWER4,	REVFMT_MAJMIN,
	   PPC_FEATURE_64 | PPC_FEATURE_HAS_FPU | PPC_FEATURE_POWER4, 0, NULL },
        { "IBM POWER4+",	IBMPOWER4PLUS,	REVFMT_MAJMIN,
	   PPC_FEATURE_64 | PPC_FEATURE_HAS_FPU | PPC_FEATURE_POWER4, 0, NULL },
        { "IBM POWER5",		IBMPOWER5,	REVFMT_MAJMIN,
	   PPC_FEATURE_64 | PPC_FEATURE_HAS_FPU | PPC_FEATURE_POWER4 |
	   PPC_FEATURE_SMT | PPC_FEATURE_ICACHE_SNOOP, 0, NULL },
        { "IBM POWER5+",	IBMPOWER5PLUS,	REVFMT_MAJMIN,
	   PPC_FEATURE_64 | PPC_FEATURE_HAS_FPU | PPC_FEATURE_POWER5_PLUS |
	   PPC_FEATURE_SMT | PPC_FEATURE_ICACHE_SNOOP, 0, NULL },
        { "IBM POWER6",		IBMPOWER6,	REVFMT_MAJMIN,
	   PPC_FEATURE_64 | PPC_FEATURE_HAS_ALTIVEC | PPC_FEATURE_HAS_FPU |
	   PPC_FEATURE_SMT | PPC_FEATURE_ICACHE_SNOOP | PPC_FEATURE_ARCH_2_05 |
	   PPC_FEATURE_TRUE_LE, 0, NULL },
        { "IBM POWER7",		IBMPOWER7,	REVFMT_MAJMIN,
	   PPC_FEATURE_64 | PPC_FEATURE_HAS_ALTIVEC | PPC_FEATURE_HAS_FPU |
	   PPC_FEATURE_SMT | PPC_FEATURE_ARCH_2_05 | PPC_FEATURE_ARCH_2_06 |
	   PPC_FEATURE_HAS_VSX | PPC_FEATURE_TRUE_LE, PPC_FEATURE2_DSCR, NULL },
        { "IBM POWER7+",	IBMPOWER7PLUS,	REVFMT_MAJMIN,
	   PPC_FEATURE_64 | PPC_FEATURE_HAS_ALTIVEC | PPC_FEATURE_HAS_FPU |
	   PPC_FEATURE_SMT | PPC_FEATURE_ARCH_2_05 | PPC_FEATURE_ARCH_2_06 |
	   PPC_FEATURE_HAS_VSX, PPC_FEATURE2_DSCR, NULL },
        { "IBM POWER8E",	IBMPOWER8E,	REVFMT_MAJMIN,
	   PPC_FEATURE_64 | PPC_FEATURE_HAS_ALTIVEC | PPC_FEATURE_HAS_FPU |
	   PPC_FEATURE_SMT | PPC_FEATURE_ICACHE_SNOOP | PPC_FEATURE_ARCH_2_05 |
	   PPC_FEATURE_ARCH_2_06 | PPC_FEATURE_HAS_VSX | PPC_FEATURE_TRUE_LE,
	   PPC_FEATURE2_ARCH_2_07 | PPC_FEATURE2_HTM | PPC_FEATURE2_DSCR | 
	   PPC_FEATURE2_ISEL | PPC_FEATURE2_TAR | PPC_FEATURE2_HAS_VEC_CRYPTO |
	   PPC_FEATURE2_HTM_NOSC, cpu_powerx_setup },
        { "IBM POWER8",		IBMPOWER8,	REVFMT_MAJMIN,
	   PPC_FEATURE_64 | PPC_FEATURE_HAS_ALTIVEC | PPC_FEATURE_HAS_FPU |
	   PPC_FEATURE_SMT | PPC_FEATURE_ICACHE_SNOOP | PPC_FEATURE_ARCH_2_05 |
	   PPC_FEATURE_ARCH_2_06 | PPC_FEATURE_HAS_VSX | PPC_FEATURE_TRUE_LE,
	   PPC_FEATURE2_ARCH_2_07 | PPC_FEATURE2_HTM | PPC_FEATURE2_DSCR | 
	   PPC_FEATURE2_ISEL | PPC_FEATURE2_TAR | PPC_FEATURE2_HAS_VEC_CRYPTO |
	   PPC_FEATURE2_HTM_NOSC, cpu_powerx_setup },
        { "IBM POWER9",		IBMPOWER9,	REVFMT_MAJMIN,
	   PPC_FEATURE_64 | PPC_FEATURE_HAS_ALTIVEC | PPC_FEATURE_HAS_FPU |
	   PPC_FEATURE_SMT | PPC_FEATURE_ICACHE_SNOOP | PPC_FEATURE_ARCH_2_05 |
	   PPC_FEATURE_ARCH_2_06 | PPC_FEATURE_HAS_VSX | PPC_FEATURE_TRUE_LE,
	   PPC_FEATURE2_ARCH_2_07 | PPC_FEATURE2_HTM | PPC_FEATURE2_DSCR |
	   PPC_FEATURE2_ISEL | PPC_FEATURE2_TAR | PPC_FEATURE2_HAS_VEC_CRYPTO |
	   PPC_FEATURE2_ARCH_3_00 | PPC_FEATURE2_HAS_IEEE128 |
	   PPC_FEATURE2_DARN, cpu_powerx_setup },
        { "Motorola PowerPC 7400",	MPC7400,	REVFMT_MAJMIN,
	   PPC_FEATURE_HAS_ALTIVEC | PPC_FEATURE_HAS_FPU, 0, cpu_6xx_setup },
        { "Motorola PowerPC 7410",	MPC7410,	REVFMT_MAJMIN,
	   PPC_FEATURE_HAS_ALTIVEC | PPC_FEATURE_HAS_FPU, 0, cpu_6xx_setup },
        { "Motorola PowerPC 7450",	MPC7450,	REVFMT_MAJMIN,
	   PPC_FEATURE_HAS_ALTIVEC | PPC_FEATURE_HAS_FPU, 0, cpu_6xx_setup },
        { "Motorola PowerPC 7455",	MPC7455,	REVFMT_MAJMIN,
	   PPC_FEATURE_HAS_ALTIVEC | PPC_FEATURE_HAS_FPU, 0, cpu_6xx_setup },
        { "Motorola PowerPC 7457",	MPC7457,	REVFMT_MAJMIN,
	   PPC_FEATURE_HAS_ALTIVEC | PPC_FEATURE_HAS_FPU, 0, cpu_6xx_setup },
        { "Motorola PowerPC 7447A",	MPC7447A,	REVFMT_MAJMIN,
	   PPC_FEATURE_HAS_ALTIVEC | PPC_FEATURE_HAS_FPU, 0, cpu_6xx_setup },
        { "Motorola PowerPC 7448",	MPC7448,	REVFMT_MAJMIN,
	   PPC_FEATURE_HAS_ALTIVEC | PPC_FEATURE_HAS_FPU, 0, cpu_6xx_setup },
        { "Motorola PowerPC 8240",	MPC8240,	REVFMT_MAJMIN,
	   PPC_FEATURE_HAS_FPU, 0, cpu_6xx_setup },
        { "Motorola PowerPC 8245",	MPC8245,	REVFMT_MAJMIN,
	   PPC_FEATURE_HAS_FPU, 0, cpu_6xx_setup },
        { "Freescale e500v1 core",	FSL_E500v1,	REVFMT_MAJMIN,
	   PPC_FEATURE_HAS_SPE | PPC_FEATURE_HAS_EFP_SINGLE | PPC_FEATURE_BOOKE,
	   PPC_FEATURE2_ISEL, cpu_booke_setup },
        { "Freescale e500v2 core",	FSL_E500v2,	REVFMT_MAJMIN,
	   PPC_FEATURE_HAS_SPE | PPC_FEATURE_BOOKE |
	   PPC_FEATURE_HAS_EFP_SINGLE | PPC_FEATURE_HAS_EFP_DOUBLE,
	   PPC_FEATURE2_ISEL, cpu_booke_setup },
	{ "Freescale e500mc core",	FSL_E500mc,	REVFMT_MAJMIN,
	   PPC_FEATURE_HAS_FPU | PPC_FEATURE_BOOKE | PPC_FEATURE_ARCH_2_05 |
	   PPC_FEATURE_ARCH_2_06, PPC_FEATURE2_ISEL,
	   cpu_booke_setup },
	{ "Freescale e5500 core",	FSL_E5500,	REVFMT_MAJMIN,
	   PPC_FEATURE_64 | PPC_FEATURE_HAS_FPU | PPC_FEATURE_BOOKE |
	   PPC_FEATURE_ARCH_2_05 | PPC_FEATURE_ARCH_2_06,
	   PPC_FEATURE2_ISEL, cpu_booke_setup },
	{ "Freescale e6500 core",	FSL_E6500,	REVFMT_MAJMIN,
	   PPC_FEATURE_64 | PPC_FEATURE_HAS_ALTIVEC | PPC_FEATURE_HAS_FPU |
	   PPC_FEATURE_BOOKE | PPC_FEATURE_ARCH_2_05 | PPC_FEATURE_ARCH_2_06,
	   PPC_FEATURE2_ISEL, cpu_booke_setup },
        { "IBM Cell Broadband Engine",	IBMCELLBE,	REVFMT_MAJMIN,
	   PPC_FEATURE_64 | PPC_FEATURE_HAS_ALTIVEC | PPC_FEATURE_HAS_FPU |
	   PPC_FEATURE_CELL | PPC_FEATURE_SMT, 0, NULL},
        { "Unknown PowerPC CPU",	0,		REVFMT_HEX, 0, 0, NULL },
};

static void	cpu_6xx_print_cacheinfo(u_int, uint16_t);
static int	cpu_feature_bit(SYSCTL_HANDLER_ARGS);

static char model[64];
SYSCTL_STRING(_hw, HW_MODEL, model, CTLFLAG_RD, model, 0, "");

static const struct cputab	*cput;

u_long cpu_features = PPC_FEATURE_32 | PPC_FEATURE_HAS_MMU;
u_long cpu_features2 = 0;
SYSCTL_OPAQUE(_hw, OID_AUTO, cpu_features, CTLFLAG_RD,
    &cpu_features, sizeof(cpu_features), "LX", "PowerPC CPU features");
SYSCTL_OPAQUE(_hw, OID_AUTO, cpu_features2, CTLFLAG_RD,
    &cpu_features2, sizeof(cpu_features2), "LX", "PowerPC CPU features 2");

#ifdef __powerpc64__
register_t	lpcr = LPCR_LPES;
#endif

/* Provide some user-friendly aliases for bits in cpu_features */
SYSCTL_PROC(_hw, OID_AUTO, floatingpoint, CTLTYPE_INT | CTLFLAG_RD,
    0, PPC_FEATURE_HAS_FPU, cpu_feature_bit, "I",
    "Floating point instructions executed in hardware");
SYSCTL_PROC(_hw, OID_AUTO, altivec, CTLTYPE_INT | CTLFLAG_RD,
    0, PPC_FEATURE_HAS_ALTIVEC, cpu_feature_bit, "I", "CPU supports Altivec");

/*
 * Phase 1 (early) CPU setup.  Setup the cpu_features/cpu_features2 variables,
 * so they can be used during platform and MMU bringup.
 */
void
cpu_feature_setup()
{
	u_int		pvr;
	uint16_t	vers;
	const struct	cputab *cp;

	pvr = mfpvr();
	vers = pvr >> 16;
	for (cp = models; cp->version != 0; cp++) {
		if (cp->version == vers)
			break;
	}

	cput = cp;
	cpu_features |= cp->features;
	cpu_features2 |= cp->features2;
}


void
cpu_setup(u_int cpuid)
{
	uint64_t	cps;
	const char	*name;
	u_int		maj, min, pvr;
	uint16_t	rev, revfmt, vers;

	pvr = mfpvr();
	vers = pvr >> 16;
	rev = pvr;
	switch (vers) {
		case MPC7410:
			min = (pvr >> 0) & 0xff;
			maj = min <= 4 ? 1 : 2;
			break;
		case FSL_E500v1:
		case FSL_E500v2:
		case FSL_E500mc:
		case FSL_E5500:
			maj = (pvr >>  4) & 0xf;
			min = (pvr >>  0) & 0xf;
			break;
		default:
			maj = (pvr >>  8) & 0xf;
			min = (pvr >>  0) & 0xf;
	}

	revfmt = cput->revfmt;
	name = cput->name;
	if (rev == MPC750 && pvr == 15) {
		name = "Motorola MPC755";
		revfmt = REVFMT_HEX;
	}
	strncpy(model, name, sizeof(model) - 1);

	printf("cpu%d: %s revision ", cpuid, name);

	switch (revfmt) {
		case REVFMT_MAJMIN:
			printf("%u.%u", maj, min);
			break;
		case REVFMT_HEX:
			printf("0x%04x", rev);
			break;
		case REVFMT_DEC:
			printf("%u", rev);
			break;
	}

	if (cpu_est_clockrate(0, &cps) == 0)
		printf(", %jd.%02jd MHz", cps / 1000000, (cps / 10000) % 100);
	printf("\n");

	printf("cpu%d: Features %b\n", cpuid, (int)cpu_features,
	    PPC_FEATURE_BITMASK);
	if (cpu_features2 != 0)
		printf("cpu%d: Features2 %b\n", cpuid, (int)cpu_features2,
		    PPC_FEATURE2_BITMASK);

	/*
	 * Configure CPU
	 */
	if (cput->cpu_setup != NULL)
		cput->cpu_setup(cpuid, vers);
}

/* Get current clock frequency for the given cpu id. */
int
cpu_est_clockrate(int cpu_id, uint64_t *cps)
{
	uint16_t	vers;
	register_t	msr;
	phandle_t	cpu, dev, root;
	int		res  = 0;
	char		buf[8];

	vers = mfpvr() >> 16;
	msr = mfmsr();
	mtmsr(msr & ~PSL_EE);

	switch (vers) {
		case MPC7450:
		case MPC7455:
		case MPC7457:
		case MPC750:
		case IBM750FX:
		case MPC7400:
		case MPC7410:
		case MPC7447A:
		case MPC7448:
			mtspr(SPR_MMCR0, SPR_MMCR0_FC);
			mtspr(SPR_PMC1, 0);
			mtspr(SPR_MMCR0, SPR_MMCR0_PMC1SEL(PMCN_CYCLES));
			DELAY(1000);
			*cps = (mfspr(SPR_PMC1) * 1000) + 4999;
			mtspr(SPR_MMCR0, SPR_MMCR0_FC);

			mtmsr(msr);
			return (0);
		case IBM970:
		case IBM970FX:
		case IBM970MP:
			isync();
			mtspr(SPR_970MMCR0, SPR_MMCR0_FC);
			isync();
			mtspr(SPR_970MMCR1, 0);
			mtspr(SPR_970MMCRA, 0);
			mtspr(SPR_970PMC1, 0);
			mtspr(SPR_970MMCR0,
			    SPR_970MMCR0_PMC1SEL(PMC970N_CYCLES));
			isync();
			DELAY(1000);
			powerpc_sync();
			mtspr(SPR_970MMCR0, SPR_MMCR0_FC);
			*cps = (mfspr(SPR_970PMC1) * 1000) + 4999;

			mtmsr(msr);
			return (0);

		default:
			root = OF_peer(0);
			if (root == 0)
				return (ENXIO);

			dev = OF_child(root);
			while (dev != 0) {
				res = OF_getprop(dev, "name", buf, sizeof(buf));
				if (res > 0 && strcmp(buf, "cpus") == 0)
					break;
				dev = OF_peer(dev);
			}
			cpu = OF_child(dev);
			while (cpu != 0) {
				res = OF_getprop(cpu, "device_type", buf,
						sizeof(buf));
				if (res > 0 && strcmp(buf, "cpu") == 0)
					break;
				cpu = OF_peer(cpu);
			}
			if (cpu == 0)
				return (ENOENT);
			if (OF_getprop(cpu, "ibm,extended-clock-frequency",
			    cps, sizeof(*cps)) >= 0) {
				return (0);
			} else if (OF_getprop(cpu, "clock-frequency", cps, 
			    sizeof(cell_t)) >= 0) {
				*cps >>= 32;
				return (0);
			} else {
				return (ENOENT);
			}
	}
}

void
cpu_6xx_setup(int cpuid, uint16_t vers)
{
	register_t hid0, pvr;
	const char *bitmask;

	hid0 = mfspr(SPR_HID0);
	pvr = mfpvr();

	/*
	 * Configure power-saving mode.
	 */
	switch (vers) {
		case MPC603:
		case MPC603e:
		case MPC603ev:
		case MPC604ev:
		case MPC750:
		case IBM750FX:
		case MPC7400:
		case MPC7410:
		case MPC8240:
		case MPC8245:
			/* Select DOZE mode. */
			hid0 &= ~(HID0_DOZE | HID0_NAP | HID0_SLEEP);
			hid0 |= HID0_DOZE | HID0_DPM;
			powerpc_pow_enabled = 1;
			break;

		case MPC7448:
		case MPC7447A:
		case MPC7457:
		case MPC7455:
		case MPC7450:
			/* Enable the 7450 branch caches */
			hid0 |= HID0_SGE | HID0_BTIC;
			hid0 |= HID0_LRSTK | HID0_FOLD | HID0_BHT;
			/* Disable BTIC on 7450 Rev 2.0 or earlier and on 7457 */
			if (((pvr >> 16) == MPC7450 && (pvr & 0xFFFF) <= 0x0200)
					|| (pvr >> 16) == MPC7457)
				hid0 &= ~HID0_BTIC;
			/* Select NAP mode. */
			hid0 &= ~(HID0_DOZE | HID0_NAP | HID0_SLEEP);
			hid0 |= HID0_NAP | HID0_DPM;
			powerpc_pow_enabled = 1;
			break;

		default:
			/* No power-saving mode is available. */ ;
	}

	switch (vers) {
		case IBM750FX:
		case MPC750:
			hid0 &= ~HID0_DBP;		/* XXX correct? */
			hid0 |= HID0_EMCP | HID0_BTIC | HID0_SGE | HID0_BHT;
			break;

		case MPC7400:
		case MPC7410:
			hid0 &= ~HID0_SPD;
			hid0 |= HID0_EMCP | HID0_BTIC | HID0_SGE | HID0_BHT;
			hid0 |= HID0_EIEC;
			break;

	}

	mtspr(SPR_HID0, hid0);

	if (bootverbose)
		cpu_6xx_print_cacheinfo(cpuid, vers);

	switch (vers) {
		case MPC7447A:
		case MPC7448:
		case MPC7450:
		case MPC7455:
		case MPC7457:
			bitmask = HID0_7450_BITMASK;
			break;
		default:
			bitmask = HID0_BITMASK;
			break;
	}

	printf("cpu%d: HID0 %b\n", cpuid, (int)hid0, bitmask);

	if (cpu_idle_hook == NULL)
		cpu_idle_hook = cpu_idle_60x;
}


static void
cpu_6xx_print_cacheinfo(u_int cpuid, uint16_t vers)
{
	register_t hid;

	hid = mfspr(SPR_HID0);
	printf("cpu%u: ", cpuid);
	printf("L1 I-cache %sabled, ", (hid & HID0_ICE) ? "en" : "dis");
	printf("L1 D-cache %sabled\n", (hid & HID0_DCE) ? "en" : "dis");

	printf("cpu%u: ", cpuid);
  	if (mfspr(SPR_L2CR) & L2CR_L2E) {
		switch (vers) {
		case MPC7450:
		case MPC7455:
		case MPC7457:
			printf("256KB L2 cache, ");
			if (mfspr(SPR_L3CR) & L3CR_L3E)
				printf("%cMB L3 backside cache",
				    mfspr(SPR_L3CR) & L3CR_L3SIZ ? '2' : '1');
			else
				printf("L3 cache disabled");
			printf("\n");
			break;
		case IBM750FX:
			printf("512KB L2 cache\n");
			break; 
		default:
			switch (mfspr(SPR_L2CR) & L2CR_L2SIZ) {
			case L2SIZ_256K:
				printf("256KB ");
				break;
			case L2SIZ_512K:
				printf("512KB ");
				break;
			case L2SIZ_1M:
				printf("1MB ");
				break;
			}
			printf("write-%s", (mfspr(SPR_L2CR) & L2CR_L2WT)
			    ? "through" : "back");
			if (mfspr(SPR_L2CR) & L2CR_L2PE)
				printf(", with parity");
			printf(" backside cache\n");
			break;
		}
	} else
		printf("L2 cache disabled\n");
}

static void
cpu_booke_setup(int cpuid, uint16_t vers)
{
#ifdef BOOKE_E500
	register_t hid0;
	const char *bitmask;

	hid0 = mfspr(SPR_HID0);

	switch (vers) {
	case FSL_E500mc:
		bitmask = HID0_E500MC_BITMASK;
		cpu_idle_hook = cpu_idle_e500mc;
		break;
	case FSL_E5500:
	case FSL_E6500:
		bitmask = HID0_E5500_BITMASK;
		cpu_idle_hook = cpu_idle_e500mc;
		break;
	case FSL_E500v1:
	case FSL_E500v2:
		/* Only e500v1/v2 support HID0 power management setup. */

		/* Program power-management mode. */
		hid0 &= ~(HID0_DOZE | HID0_NAP | HID0_SLEEP);
		hid0 |= HID0_DOZE;

		mtspr(SPR_HID0, hid0);
	default:
		bitmask = HID0_E500_BITMASK;
		break;
	}
	printf("cpu%d: HID0 %b\n", cpuid, (int)hid0, bitmask);
#endif

	if (cpu_idle_hook == NULL)
		cpu_idle_hook = cpu_idle_booke;
}

static void
cpu_970_setup(int cpuid, uint16_t vers)
{
#ifdef AIM
	uint32_t hid0_hi, hid0_lo;

	__asm __volatile ("mfspr %0,%2; clrldi %1,%0,32; srdi %0,%0,32;"
	    : "=r" (hid0_hi), "=r" (hid0_lo) : "K" (SPR_HID0));

	/* Configure power-saving mode */
	switch (vers) {
	case IBM970MP:
		hid0_hi |= (HID0_DEEPNAP | HID0_NAP | HID0_DPM);
		hid0_hi &= ~HID0_DOZE;
		break;
	default:
		hid0_hi |= (HID0_NAP | HID0_DPM);
		hid0_hi &= ~(HID0_DOZE | HID0_DEEPNAP);
		break;
	}
	powerpc_pow_enabled = 1;

	__asm __volatile (" \
		sync; isync;					\
		sldi	%0,%0,32; or %0,%0,%1;			\
		mtspr	%2, %0;					\
		mfspr   %0, %2; mfspr   %0, %2; mfspr   %0, %2; \
		mfspr   %0, %2; mfspr   %0, %2; mfspr   %0, %2; \
		sync; isync"
	    :: "r" (hid0_hi), "r"(hid0_lo), "K" (SPR_HID0));

	__asm __volatile ("mfspr %0,%1; srdi %0,%0,32;"
	    : "=r" (hid0_hi) : "K" (SPR_HID0));
	printf("cpu%d: HID0 %b\n", cpuid, (int)(hid0_hi), HID0_970_BITMASK);
#endif

	cpu_idle_hook = cpu_idle_60x;
}

static void
cpu_powerx_setup(int cpuid, uint16_t vers)
{

#if defined(__powerpc64__) && defined(AIM)
	if ((mfmsr() & PSL_HV) == 0)
		return;

	/* Configure power-saving */
	switch (vers) {
	case IBMPOWER8:
	case IBMPOWER8E:
		cpu_idle_hook = cpu_idle_powerx;
		mtspr(SPR_LPCR, mfspr(SPR_LPCR) | LPCR_PECE_WAKESET);
		isync();
		break;
	case IBMPOWER9:
		cpu_idle_hook = cpu_idle_power9;
		mtspr(SPR_LPCR, mfspr(SPR_LPCR) | LPCR_PECE_WAKESET);
		isync();
		break;
	default:
		return;
	}

#endif
}

static int
cpu_feature_bit(SYSCTL_HANDLER_ARGS)
{
	int result;

	result = (cpu_features & arg2) ? 1 : 0;

	return (sysctl_handle_int(oidp, &result, 0, req));
}

void
cpu_idle(int busy)
{
	sbintime_t sbt = -1;

#ifdef INVARIANTS
	if ((mfmsr() & PSL_EE) != PSL_EE) {
		struct thread *td = curthread;
		printf("td msr %#lx\n", (u_long)td->td_md.md_saved_msr);
		panic("ints disabled in idleproc!");
	}
#endif

	CTR2(KTR_SPARE2, "cpu_idle(%d) at %d",
	    busy, curcpu);

	if (cpu_idle_hook != NULL) {
		if (!busy) {
			critical_enter();
			sbt = cpu_idleclock();
		}
		cpu_idle_hook(sbt);
		if (!busy) {
			cpu_activeclock();
			critical_exit();
		}
	}

	CTR2(KTR_SPARE2, "cpu_idle(%d) at %d done",
	    busy, curcpu);
}

static void
cpu_idle_60x(sbintime_t sbt)
{
	register_t msr;
	uint16_t vers;

	if (!powerpc_pow_enabled)
		return;

	msr = mfmsr();
	vers = mfpvr() >> 16;

#ifdef AIM
	switch (vers) {
	case IBM970:
	case IBM970FX:
	case IBM970MP:
	case MPC7447A:
	case MPC7448:
	case MPC7450:
	case MPC7455:
	case MPC7457:
		__asm __volatile("\
			    dssall; sync; mtmsr %0; isync"
			    :: "r"(msr | PSL_POW));
		break;
	default:
		powerpc_sync();
		mtmsr(msr | PSL_POW);
		break;
	}
#endif
}

#ifdef BOOKE_E500
static void
cpu_idle_e500mc(sbintime_t sbt)
{
	/*
	 * Base binutils doesn't know what the 'wait' instruction is, so
	 * use the opcode encoding here.
	 */
	__asm __volatile(".long 0x7c00007c");
}
#endif

static void
cpu_idle_booke(sbintime_t sbt)
{
	register_t msr;

	msr = mfmsr();

#ifdef BOOKE_E500
	powerpc_sync();
	mtmsr(msr | PSL_WE);
#endif
}

#if defined(__powerpc64__) && defined(AIM)
static void
cpu_idle_powerx(sbintime_t sbt)
{

	/* Sleeping when running on one cpu gives no advantages - avoid it */
	if (smp_started == 0)
		return;

	spinlock_enter();
	if (sched_runnable()) {
		spinlock_exit();
		return;
	}

	if (can_wakeup == 0)
		can_wakeup = 1;
	mb();

	enter_idle_powerx();
	spinlock_exit();
}

static void
cpu_idle_power9(sbintime_t sbt)
{
	register_t msr;

	msr = mfmsr();

	/* Suspend external interrupts until stop instruction completes. */
	mtmsr(msr &  ~PSL_EE);
	/* Set the stop state to lowest latency, wake up to next instruction */
	mtspr(SPR_PSSCR, 0);
	/* "stop" instruction (PowerISA 3.0) */
	__asm __volatile (".long 0x4c0002e4");
	/*
	 * Re-enable external interrupts to capture the interrupt that caused
	 * the wake up.
	 */
	mtmsr(msr);
	
}
#endif

int
cpu_idle_wakeup(int cpu)
{

	return (0);
}
