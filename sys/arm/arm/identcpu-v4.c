/*	$NetBSD: cpu.c,v 1.55 2004/02/13 11:36:10 wiz Exp $	*/

/*-
 * Copyright (c) 1995 Mark Brinicombe.
 * Copyright (c) 1995 Brini.
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
 *	This product includes software developed by Brini.
 * 4. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BRINI ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL BRINI OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * RiscBSD kernel project
 *
 * cpu.c
 *
 * Probing and configuration for the master CPU
 *
 * Created      : 10/10/95
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <machine/cpu.h>
#include <machine/endian.h>

#include <machine/md_var.h>

char machine[] = "arm";

SYSCTL_STRING(_hw, HW_MACHINE, machine, CTLFLAG_RD,
	machine, 0, "Machine class");

static const char * const generic_steppings[16] = {
	"rev 0",	"rev 1",	"rev 2",	"rev 3",
	"rev 4",	"rev 5",	"rev 6",	"rev 7",
	"rev 8",	"rev 9",	"rev 10",	"rev 11",
	"rev 12",	"rev 13",	"rev 14",	"rev 15",
};

static const char * const xscale_steppings[16] = {
	"step A-0",	"step A-1",	"step B-0",	"step C-0",
	"step D-0",	"rev 5",	"rev 6",	"rev 7",
	"rev 8",	"rev 9",	"rev 10",	"rev 11",
	"rev 12",	"rev 13",	"rev 14",	"rev 15",
};

static const char * const i80219_steppings[16] = {
	"step A-0",	"rev 1",	"rev 2",	"rev 3",
	"rev 4",	"rev 5",	"rev 6",	"rev 7",
	"rev 8",	"rev 9",	"rev 10",	"rev 11",
	"rev 12",	"rev 13",	"rev 14",	"rev 15",
};

static const char * const i80321_steppings[16] = {
	"step A-0",	"step B-0",	"rev 2",	"rev 3",
	"rev 4",	"rev 5",	"rev 6",	"rev 7",
	"rev 8",	"rev 9",	"rev 10",	"rev 11",
	"rev 12",	"rev 13",	"rev 14",	"rev 15",
};

static const char * const i81342_steppings[16] = {
	"step A-0",	"rev 1",	"rev 2",	"rev 3",
	"rev 4",	"rev 5",	"rev 6",	"rev 7",
	"rev 8",	"rev 9",	"rev 10",	"rev 11",
	"rev 12",	"rev 13",	"rev 14",	"rev 15",
};

/* Steppings for PXA2[15]0 */
static const char * const pxa2x0_steppings[16] = {
	"step A-0",	"step A-1",	"step B-0",	"step B-1",
	"step B-2",	"step C-0",	"rev 6",	"rev 7",
	"rev 8",	"rev 9",	"rev 10",	"rev 11",
	"rev 12",	"rev 13",	"rev 14",	"rev 15",
};

/* Steppings for PXA255/26x.
 * rev 5: PXA26x B0, rev 6: PXA255 A0
 */
static const char * const pxa255_steppings[16] = {
	"rev 0",	"rev 1",	"rev 2",	"step A-0",
	"rev 4",	"step B-0",	"step A-0",	"rev 7",
	"rev 8",	"rev 9",	"rev 10",	"rev 11",
	"rev 12",	"rev 13",	"rev 14",	"rev 15",
};

/* Stepping for PXA27x */
static const char * const pxa27x_steppings[16] = {
	"step A-0",	"step A-1",	"step B-0",	"step B-1",
	"step C-0",	"rev 5",	"rev 6",	"rev 7",
	"rev 8",	"rev 9",	"rev 10",	"rev 11",
	"rev 12",	"rev 13",	"rev 14",	"rev 15",
};

struct cpuidtab {
	u_int32_t	cpuid;
	enum		cpu_class cpu_class;
	const char	*cpu_name;
	const char * const *cpu_steppings;
};

const struct cpuidtab cpuids[] = {
	{ CPU_ID_ARM920T,	CPU_CLASS_ARM9TDMI,	"ARM920T",
	  generic_steppings },
	{ CPU_ID_ARM920T_ALT,	CPU_CLASS_ARM9TDMI,	"ARM920T",
	  generic_steppings },
	{ CPU_ID_ARM922T,	CPU_CLASS_ARM9TDMI,	"ARM922T",
	  generic_steppings },
	{ CPU_ID_ARM926EJS,	CPU_CLASS_ARM9EJS,	"ARM926EJ-S",
	  generic_steppings },
	{ CPU_ID_ARM940T,	CPU_CLASS_ARM9TDMI,	"ARM940T",
	  generic_steppings },
	{ CPU_ID_ARM946ES,	CPU_CLASS_ARM9ES,	"ARM946E-S",
	  generic_steppings },
	{ CPU_ID_ARM966ES,	CPU_CLASS_ARM9ES,	"ARM966E-S",
	  generic_steppings },
	{ CPU_ID_ARM966ESR1,	CPU_CLASS_ARM9ES,	"ARM966E-S",
	  generic_steppings },
	{ CPU_ID_FA526,		CPU_CLASS_ARM9TDMI,	"FA526",
	  generic_steppings },
	{ CPU_ID_FA626TE,	CPU_CLASS_ARM9ES,	"FA626TE",
	  generic_steppings },

	{ CPU_ID_TI925T,	CPU_CLASS_ARM9TDMI,	"TI ARM925T",
	  generic_steppings },

	{ CPU_ID_ARM1020E,	CPU_CLASS_ARM10E,	"ARM1020E",
	  generic_steppings },
	{ CPU_ID_ARM1022ES,	CPU_CLASS_ARM10E,	"ARM1022E-S",
	  generic_steppings },
	{ CPU_ID_ARM1026EJS,	CPU_CLASS_ARM10EJ,	"ARM1026EJ-S",
	  generic_steppings },

	{ CPU_ID_80200,		CPU_CLASS_XSCALE,	"i80200",
	  xscale_steppings },

	{ CPU_ID_80321_400,	CPU_CLASS_XSCALE,	"i80321 400MHz",
	  i80321_steppings },
	{ CPU_ID_80321_600,	CPU_CLASS_XSCALE,	"i80321 600MHz",
	  i80321_steppings },
	{ CPU_ID_80321_400_B0,	CPU_CLASS_XSCALE,	"i80321 400MHz",
	  i80321_steppings },
	{ CPU_ID_80321_600_B0,	CPU_CLASS_XSCALE,	"i80321 600MHz",
	  i80321_steppings },

	{ CPU_ID_81342,		CPU_CLASS_XSCALE,	"i81342",
	  i81342_steppings },

	{ CPU_ID_80219_400,	CPU_CLASS_XSCALE,	"i80219 400MHz",
	  i80219_steppings },
	{ CPU_ID_80219_600,	CPU_CLASS_XSCALE,	"i80219 600MHz",
	  i80219_steppings },

	{ CPU_ID_PXA27X,	CPU_CLASS_XSCALE,	"PXA27x",
	  pxa27x_steppings },
	{ CPU_ID_PXA250A,	CPU_CLASS_XSCALE,	"PXA250",
	  pxa2x0_steppings },
	{ CPU_ID_PXA210A,	CPU_CLASS_XSCALE,	"PXA210",
	  pxa2x0_steppings },
	{ CPU_ID_PXA250B,	CPU_CLASS_XSCALE,	"PXA250",
	  pxa2x0_steppings },
	{ CPU_ID_PXA210B,	CPU_CLASS_XSCALE,	"PXA210",
	  pxa2x0_steppings },
	{ CPU_ID_PXA250C, 	CPU_CLASS_XSCALE,	"PXA255",
	  pxa255_steppings },
	{ CPU_ID_PXA210C, 	CPU_CLASS_XSCALE,	"PXA210",
	  pxa2x0_steppings },

	{ CPU_ID_MV88FR131,	CPU_CLASS_MARVELL,	"Feroceon 88FR131",
	  generic_steppings },

	{ CPU_ID_MV88FR571_VD,	CPU_CLASS_MARVELL,	"Feroceon 88FR571-VD",
	  generic_steppings },

	{ 0, CPU_CLASS_NONE, NULL, NULL }
};

struct cpu_classtab {
	const char	*class_name;
	const char	*class_option;
};

const struct cpu_classtab cpu_classes[] = {
	{ "unknown",	NULL },			/* CPU_CLASS_NONE */
	{ "ARM9TDMI",	"CPU_ARM9TDMI" },	/* CPU_CLASS_ARM9TDMI */
	{ "ARM9E-S",	"CPU_ARM9E" },		/* CPU_CLASS_ARM9ES */
	{ "ARM9EJ-S",	"CPU_ARM9E" },		/* CPU_CLASS_ARM9EJS */
	{ "ARM10E",	"CPU_ARM10" },		/* CPU_CLASS_ARM10E */
	{ "ARM10EJ",	"CPU_ARM10" },		/* CPU_CLASS_ARM10EJ */
	{ "XScale",	"CPU_XSCALE_..." },	/* CPU_CLASS_XSCALE */
	{ "Marvell",	"CPU_MARVELL" },	/* CPU_CLASS_MARVELL */
};

/*
 * Report the type of the specified arm processor. This uses the generic and
 * arm specific information in the cpu structure to identify the processor.
 * The remaining fields in the cpu structure are filled in appropriately.
 */

static const char * const wtnames[] = {
	"write-through",
	"write-back",
	"write-back",
	"**unknown 3**",
	"**unknown 4**",
	"write-back-locking",		/* XXX XScale-specific? */
	"write-back-locking-A",
	"write-back-locking-B",
	"**unknown 8**",
	"**unknown 9**",
	"**unknown 10**",
	"**unknown 11**",
	"**unknown 12**",
	"**unknown 13**",
	"write-back-locking-C",
	"**unknown 15**",
};

static void
print_enadis(int enadis, char *s)
{

	printf(" %s %sabled", s, (enadis == 0) ? "dis" : "en");
}

enum cpu_class cpu_class = CPU_CLASS_NONE;

void
identify_arm_cpu(void)
{
	u_int cpuid, ctrl;
	int i;

	ctrl = cp15_sctlr_get();
	cpuid = cp15_midr_get();

	if (cpuid == 0) {
		printf("Processor failed probe - no CPU ID\n");
		return;
	}

	for (i = 0; cpuids[i].cpuid != 0; i++)
		if (cpuids[i].cpuid == (cpuid & CPU_ID_CPU_MASK)) {
			cpu_class = cpuids[i].cpu_class;
			printf("CPU: %s %s (%s core)\n",
			    cpuids[i].cpu_name,
			    cpuids[i].cpu_steppings[cpuid &
			    CPU_ID_REVISION_MASK],
			    cpu_classes[cpu_class].class_name);
			break;
		}
	if (cpuids[i].cpuid == 0)
		printf("unknown CPU (ID = 0x%x)\n", cpuid);

	printf(" ");

	if (ctrl & CPU_CONTROL_BEND_ENABLE)
		printf(" Big-endian");
	else
		printf(" Little-endian");

	switch (cpu_class) {
	case CPU_CLASS_ARM9TDMI:
	case CPU_CLASS_ARM9ES:
	case CPU_CLASS_ARM9EJS:
	case CPU_CLASS_ARM10E:
	case CPU_CLASS_ARM10EJ:
	case CPU_CLASS_XSCALE:
	case CPU_CLASS_MARVELL:
		print_enadis(ctrl & CPU_CONTROL_DC_ENABLE, "DC");
		print_enadis(ctrl & CPU_CONTROL_IC_ENABLE, "IC");
#if defined(SOC_MV_KIRKWOOD) || defined(SOC_MV_DISCOVERY)
		i = sheeva_control_ext(0, 0);
		print_enadis(i & MV_WA_ENABLE, "WA");
		print_enadis(i & MV_DC_STREAM_ENABLE, "DC streaming");
		printf("\n ");
		print_enadis((i & MV_BTB_DISABLE) == 0, "BTB");
		print_enadis(i & MV_L2_ENABLE, "L2");
		print_enadis((i & MV_L2_PREFETCH_DISABLE) == 0,
		    "L2 prefetch");
		printf("\n ");
#endif
		break;
	default:
		break;
	}

	print_enadis(ctrl & CPU_CONTROL_WBUF_ENABLE, "WB");
	if (ctrl & CPU_CONTROL_LABT_ENABLE)
		printf(" LABT");
	else
		printf(" EABT");

	print_enadis(ctrl & CPU_CONTROL_BPRD_ENABLE, "branch prediction");
	printf("\n");

	/* Print cache info. */
	if (arm_picache_line_size == 0 && arm_pdcache_line_size == 0)
		return;

	if (arm_pcache_unified) {
		printf("  %dKB/%dB %d-way %s unified cache\n",
		    arm_pdcache_size / 1024,
		    arm_pdcache_line_size, arm_pdcache_ways,
		    wtnames[arm_pcache_type]);
	} else {
		printf("  %dKB/%dB %d-way instruction cache\n",
		    arm_picache_size / 1024,
		    arm_picache_line_size, arm_picache_ways);
		printf("  %dKB/%dB %d-way %s data cache\n",
		    arm_pdcache_size / 1024,
		    arm_pdcache_line_size, arm_pdcache_ways,
		    wtnames[arm_pcache_type]);
	}
}
