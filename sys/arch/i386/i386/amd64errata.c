/*	$OpenBSD: amd64errata.c,v 1.16 2022/10/10 03:01:11 jsg Exp $	*/
/*	$NetBSD: errata.c,v 1.6 2007/02/05 21:05:45 ad Exp $	*/

/*-
 * Copyright (c) 2007 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Andrew Doran.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Detect, report on, and work around known errata with AMD amd64 CPUs.
 *
 * This is generalised because there are quite a few problems that the
 * BIOS can patch via MSR, but it is not known if the OS can patch these
 * yet.  The list is expected to grow over time.
 *
 * The data here is from:
 *
 * Revision Guide for AMD Athlon 64 and AMD Opteron Processors (0Fh)
 * Publication #25759, Revision: 3.79, Issue Date: July 2009
 * BH-E4, CH-CG, CH-D0, DH-CG, DH-D0, DH-E3, DH-E6, JH-E1, JH-E6, SH-B0,
 * SH-B3, SH-C0, SH-CG, SH-D0, SH-E4, SH-E5
 *
 * Revision Guide for AMD Family 10h Processors
 * Publication #41322, Revision: 3.92, Issue Date: March 2012
 * BL-C2, BL-C3, DA-C2, DA-C3, DR-B2, DR-B3, DR-BA, HY-D0, HY-D1,
 * HY-D1-G34R1, PH-E0, RB-C2, RB-C3
 *
 * Revision Guide for AMD Family 12h Processors
 * Publication #44739, Revision: 3.10, Issue Date: March 2012
 * LN-B0
 */

#include <sys/param.h>

#include <sys/systm.h>

#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/specialreg.h>

typedef struct errata {
	u_short		e_num;
	u_short		e_reported;
	u_int		e_data1;
	const uint8_t	*e_set;
	int		(*e_act)(struct cpu_info *, struct errata *);
	uint64_t	e_data2;
} errata_t;

typedef enum cpurev {
	BH_E4, CH_CG, CH_D0, DH_CG, DH_D0, DH_E3, DH_E6, JH_E1,
	JH_E6, SH_B0, SH_B3, SH_C0, SH_CG, SH_D0, SH_E4, SH_E5,
	DR_BA, DR_B2, DR_B3, RB_C2, RB_C3, BL_C2, BL_C3, DA_C2,
	DA_C3, HY_D0, HY_D1, PH_E0, LN_B0,
	OINK
} cpurev_t;

static const u_int cpurevs[] = {
	BH_E4, 0x0020fb1, CH_CG, 0x0000f82, CH_CG, 0x0000fb2,
	CH_D0, 0x0010f80, CH_D0, 0x0010fb0, DH_CG, 0x0000fc0,
	DH_CG, 0x0000fe0, DH_CG, 0x0000ff0, DH_D0, 0x0010fc0,
	DH_D0, 0x0010ff0, DH_E3, 0x0020fc0, DH_E3, 0x0020ff0,
	DH_E6, 0x0020fc2, DH_E6, 0x0020ff2, JH_E1, 0x0020f10,
	JH_E6, 0x0020f12, JH_E6, 0x0020f32, SH_B0, 0x0000f40,
	SH_B3, 0x0000f51, SH_C0, 0x0000f48, SH_C0, 0x0000f58,
	SH_CG, 0x0000f4a, SH_CG, 0x0000f5a, SH_CG, 0x0000f7a,
	SH_D0, 0x0010f40, SH_D0, 0x0010f50, SH_D0, 0x0010f70,
	SH_E4, 0x0020f51, SH_E4, 0x0020f71, SH_E5, 0x0020f42,
	DR_BA, 0x0100f2a, DR_B2, 0x0100f22, DR_B3, 0x0100f23,
	RB_C2, 0x0100f42, RB_C3, 0x0100f43, BL_C2, 0x0100f52,
	BL_C3, 0x0100f53, DA_C2, 0x0100f62, DA_C3, 0x0100f63,
	HY_D0, 0x0100f80, HY_D1, 0x0100f81, HY_D1, 0x0100f91,
	PH_E0, 0x0100fa0, LN_B0, 0x0300f10, SH_B0, 0x0000f50,
	OINK
};

static const uint8_t amd64_errata_set1[] = {
	SH_B3, SH_C0, SH_CG, DH_CG, CH_CG, OINK
};

#ifdef MULTIPROCESSOR
static const uint8_t amd64_errata_set2[] = {
	SH_B3, SH_C0, SH_CG, DH_CG, CH_CG, SH_D0, DH_D0, CH_D0, OINK
};
#endif

static const uint8_t amd64_errata_set3[] = {
	JH_E1, DH_E3, OINK
};

#if 0
static const uint8_t amd64_errata_set4[] = {
	SH_C0, SH_CG, DH_CG, CH_CG, SH_D0, DH_D0, CH_D0, JH_E1,
	DH_E3, SH_E4, BH_E4, SH_E5, DH_E6, JH_E6, OINK
};
#endif

static const uint8_t amd64_errata_set5[] = {
	SH_B3, OINK
};

static const uint8_t amd64_errata_set6[] = {
	SH_C0, SH_CG, DH_CG, CH_CG, OINK
};

static const uint8_t amd64_errata_set7[] = {
	SH_C0, SH_CG, DH_CG, CH_CG, SH_D0, DH_D0, CH_D0, OINK
};

static const uint8_t amd64_errata_set8[] = {
	BH_E4, CH_CG, CH_CG, CH_D0, CH_D0, DH_CG, DH_CG, DH_CG,
	DH_D0, DH_D0, DH_E3, DH_E3, DH_E6, DH_E6, JH_E1, JH_E6,
	JH_E6, SH_B0, SH_B3, SH_C0, SH_C0, SH_CG, SH_CG, SH_CG, 
	SH_D0, SH_D0, SH_D0, SH_E4, SH_E4, SH_E5, OINK
};

static const uint8_t amd64_errata_set9[] = {
	DR_BA, DR_B2, DR_B3, RB_C2, RB_C3, BL_C2, BL_C3, DA_C2,
	DA_C3, HY_D0, HY_D1, PH_E0, LN_B0, OINK
};

int amd64_errata_setmsr(struct cpu_info *, errata_t *);
int amd64_errata_testmsr(struct cpu_info *, errata_t *);

static errata_t errata[] = {
	/*
	 * 81: Cache Coherency Problem with Hardware Prefetching
	 * and Streaming Stores
	 */
	{
		81, 0, MSR_DC_CFG, amd64_errata_set5,
		amd64_errata_testmsr, DC_CFG_DIS_SMC_CHK_BUF
	},
	/*
	 * 86: DRAM Data Masking Feature Can Cause ECC Failures
	 */
	{
		86, 0, MSR_NB_CFG, amd64_errata_set1,
		amd64_errata_testmsr, NB_CFG_DISDATMSK
	},
	/*
	 * 89: Potential Deadlock With Locked Transactions
	 */
	{
		89, 0, MSR_NB_CFG, amd64_errata_set8,
		amd64_errata_testmsr, NB_CFG_DISIOREQLOCK
	},
	/*
	 * 94: Sequential Prefetch Feature May Cause Incorrect
	 * Processor Operation
	 */
	{
		94, 0, MSR_IC_CFG, amd64_errata_set1,
		amd64_errata_testmsr, IC_CFG_DIS_SEQ_PREFETCH
	},
	/*
	 * 97: 128-Bit Streaming Stores May Cause Coherency
	 * Failure
	 *
	 * XXX "This workaround must not be applied to processors
	 * prior to revision C0."  We don't apply it, but if it
	 * can't be applied, it shouldn't be reported.
	 */
	{
		97, 0, MSR_DC_CFG, amd64_errata_set6,
		amd64_errata_testmsr, DC_CFG_DIS_CNV_WC_SSO
	},
	/*
	 * 104: DRAM Data Masking Feature Causes ChipKill ECC
	 * Failures When Enabled With x8/x16 DRAM Devices
	 */
	{
		104, 0, MSR_NB_CFG, amd64_errata_set7,
		amd64_errata_testmsr, NB_CFG_DISDATMSK
	},
	/*
	 * 113: Enhanced Write-Combining Feature Causes System Hang
	 */
	{
		113, 0, MSR_BU_CFG, amd64_errata_set3,
		amd64_errata_setmsr, BU_CFG_WBENHWSBDIS
	},
#ifdef MULTIPROCESSOR
	/*
	 * 69: Multiprocessor Coherency Problem with Hardware
	 * Prefetch Mechanism
	 */
	{
		69, 0, MSR_BU_CFG, amd64_errata_set5,
		amd64_errata_setmsr, BU_CFG_WBPFSMCCHKDIS
	},
	/*
	 * 101: DRAM Scrubber May Cause Data Corruption When Using
	 * Node-Interleaved Memory
	 */
	{
		101, 0, 0, amd64_errata_set2,
		NULL, 0
	},
	/*
	 * 106: Potential Deadlock with Tightly Coupled Semaphores
	 * in an MP System
	 */
	{
		106, 0, MSR_LS_CFG, amd64_errata_set2,
		amd64_errata_testmsr, LS_CFG_DIS_LS2_SQUISH
	},
	/*
	 * 107: Possible Multiprocessor Coherency Problem with
	 * Setting Page Table A/D Bits
	 */
	{
		107, 0, MSR_BU_CFG, amd64_errata_set2,
		amd64_errata_testmsr, BU_CFG_THRL2IDXCMPDIS
	},
#if 0
	/*
	 * 122: TLB Flush Filter May Cause Coherency Problem in
	 * Multiprocessor Systems
	 */
	{
		122, 0, MSR_HWCR, amd64_errata_set4,
		amd64_errata_setmsr, HWCR_FFDIS
	},
#endif
#endif	/* MULTIPROCESSOR */
	/*
	 * 721: Processor May Incorrectly Update Stack Pointer
	 */
	{
		721, 0, MSR_DE_CFG, amd64_errata_set9,
		amd64_errata_setmsr, DE_CFG_721
	},
};

int 
amd64_errata_testmsr(struct cpu_info *ci, errata_t *e)
{
	uint64_t val;

	(void)ci;

	val = rdmsr_locked(e->e_data1, OPTERON_MSR_PASSCODE);
	if ((val & e->e_data2) != 0)
		return 0;		/* not found */

	e->e_reported = 1;
	return 1;			/* found */
}

int 
amd64_errata_setmsr(struct cpu_info *ci, errata_t *e)
{
	uint64_t val;

	(void)ci;

	val = rdmsr_locked(e->e_data1, OPTERON_MSR_PASSCODE);
	if ((val & e->e_data2) != 0)
		return 0;		/* not found */

	wrmsr_locked(e->e_data1, OPTERON_MSR_PASSCODE, val | e->e_data2);

#ifdef ERRATA_DEBUG
	printf("ERRATA: writing a fix\n");
	val = rdmsr_locked(e->e_data1, OPTERON_MSR_PASSCODE);
	if ((val & e->e_data2) != 0)
		printf("ERRATA: fix seems to have worked!\n");
#endif

	e->e_reported = 1;
	return 2;			/* found and fixed */
}

void
amd64_errata(struct cpu_info *ci)
{
	errata_t *e, *ex;
	cpurev_t rev;
	int i, j;
	int rc;
	int found = 0;
	int corrected = 0;
	u_int32_t regs[4];
	static int printed = 0;

	cpuid(0x80000001, regs);

	for (i = 0; ; i += 2) {
		if ((rev = cpurevs[i]) == OINK) {
#ifdef ERRATA_DEBUG
			printf("ERRATA: this CPU ok\n");
#endif
			return;
		}
		if (cpurevs[i + 1] == regs[0]) {
#ifdef ERRATA_DEBUG
			printf("ERRATA: this CPU has errata\n");
#endif
			break;
		}
	}

	ex = errata + sizeof(errata) / sizeof(errata[0]);

	/* Reset e_reporteds (for multiple CPUs) */
	for (e = errata; e < ex; e++)
		e->e_reported = 0;

	for (e = errata; e < ex; e++) {
		if (e->e_reported)
			continue;
		if (e->e_set != NULL) {
			for (j = 0; e->e_set[j] != OINK; j++)
				if (e->e_set[j] == rev)
					break;
			if (e->e_set[j] == OINK)
				continue;
		}

#ifdef ERRATA_DEBUG
		printf("%s: testing for erratum %d\n",
		    ci->ci_dev->dv_xname, e->e_num);
#endif

		/*
		 * If we have an action routine, call it, otherwise
		 * the default is that this erratum is present.
		 */
		rc = (e->e_act == NULL) ? 1 : (*e->e_act)(ci, e);

		if (rc == 0)			/* not found */
			continue;
		if (rc == 1)
			found++;
		if (rc == 2)
			corrected++;

		e->e_reported = rc;

#ifdef ERRATA_DEBUG
		printf("%s: erratum %d present%s\n",
		    ci->ci_dev->dv_xname, e->e_num,
		    (rc == 2) ? " and patched" : "");
#endif
	}

#define ERRATA_VERBOSE
#ifdef ERRATA_VERBOSE
	if (corrected) {
		int first = 1;

		/* Print out found and corrected */
		if (!printed) {
			printf("%s: AMD %s", ci->ci_dev->dv_xname,
			    (corrected == 1) ? "erratum" : "errata");
		}
		for (e = errata; e < ex; e++) {
			if (e->e_reported == 2) {
				if (!printed) {
					if (! first)
						printf(",");
					printf(" %d", e->e_num);
				}
				first = 0;
			}
		}
		if (!printed)
			printf(" detected and fixed\n");
	}
#endif

	if (found) {
		int first = 1;

		/* Print out found but not corrected */
		if (!printed) {
			printf("%s: AMD %s", ci->ci_dev->dv_xname,
			    (found == 1) ? "erratum" : "errata");
		}
		for (e = errata; e < ex; e++) {
			if (e->e_reported == 1) {
				if (!printed) {
					if (! first)
						printf(",");
					printf(" %d", e->e_num);
				}
				first = 0;
			}
		}
		if (!printed)
			printf(" present, BIOS upgrade may be required\n");
	}

	/* Print only one time for the first CPU */
	printed = 1;
}
