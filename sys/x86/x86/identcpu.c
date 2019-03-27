/*-
 * Copyright (c) 1992 Terrence R. Lambert.
 * Copyright (c) 1982, 1987, 1990 The Regents of the University of California.
 * Copyright (c) 1997 KATO Takenori.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: Id: machdep.c,v 1.193 1996/06/18 01:22:04 bde Exp
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_cpu.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/eventhandler.h>
#include <sys/limits.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/power.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/asmacros.h>
#include <machine/clock.h>
#include <machine/cputypes.h>
#include <machine/frame.h>
#include <machine/intr_machdep.h>
#include <machine/md_var.h>
#include <machine/segments.h>
#include <machine/specialreg.h>

#include <amd64/vmm/intel/vmx_controls.h>
#include <x86/isa/icu.h>
#include <x86/vmware.h>

#ifdef __i386__
#define	IDENTBLUE_CYRIX486	0
#define	IDENTBLUE_IBMCPU	1
#define	IDENTBLUE_CYRIXM2	2

static void identifycyrix(void);
static void print_transmeta_info(void);
#endif
static u_int find_cpu_vendor_id(void);
static void print_AMD_info(void);
static void print_INTEL_info(void);
static void print_INTEL_TLB(u_int data);
static void print_hypervisor_info(void);
static void print_svm_info(void);
static void print_via_padlock_info(void);
static void print_vmx_info(void);

#ifdef __i386__
int	cpu;			/* Are we 386, 386sx, 486, etc? */
int	cpu_class;
#endif
u_int	cpu_feature;		/* Feature flags */
u_int	cpu_feature2;		/* Feature flags */
u_int	amd_feature;		/* AMD feature flags */
u_int	amd_feature2;		/* AMD feature flags */
u_int	amd_rascap;		/* AMD RAS capabilities */
u_int	amd_pminfo;		/* AMD advanced power management info */
u_int	amd_extended_feature_extensions;
u_int	via_feature_rng;	/* VIA RNG features */
u_int	via_feature_xcrypt;	/* VIA ACE features */
u_int	cpu_high;		/* Highest arg to CPUID */
u_int	cpu_exthigh;		/* Highest arg to extended CPUID */
u_int	cpu_id;			/* Stepping ID */
u_int	cpu_procinfo;		/* HyperThreading Info / Brand Index / CLFUSH */
u_int	cpu_procinfo2;		/* Multicore info */
char	cpu_vendor[20];		/* CPU Origin code */
u_int	cpu_vendor_id;		/* CPU vendor ID */
u_int	cpu_fxsr;		/* SSE enabled */
u_int	cpu_mxcsr_mask;		/* Valid bits in mxcsr */
u_int	cpu_clflush_line_size = 32;
u_int	cpu_stdext_feature;	/* %ebx */
u_int	cpu_stdext_feature2;	/* %ecx */
u_int	cpu_stdext_feature3;	/* %edx */
uint64_t cpu_ia32_arch_caps;
u_int	cpu_max_ext_state_size;
u_int	cpu_mon_mwait_flags;	/* MONITOR/MWAIT flags (CPUID.05H.ECX) */
u_int	cpu_mon_min_size;	/* MONITOR minimum range size, bytes */
u_int	cpu_mon_max_size;	/* MONITOR minimum range size, bytes */
u_int	cpu_maxphyaddr;		/* Max phys addr width in bits */
char machine[] = MACHINE;

SYSCTL_UINT(_hw, OID_AUTO, via_feature_rng, CTLFLAG_RD,
    &via_feature_rng, 0,
    "VIA RNG feature available in CPU");
SYSCTL_UINT(_hw, OID_AUTO, via_feature_xcrypt, CTLFLAG_RD,
    &via_feature_xcrypt, 0,
    "VIA xcrypt feature available in CPU");

#ifdef __amd64__
#ifdef SCTL_MASK32
extern int adaptive_machine_arch;
#endif

static int
sysctl_hw_machine(SYSCTL_HANDLER_ARGS)
{
#ifdef SCTL_MASK32
	static const char machine32[] = "i386";
#endif
	int error;

#ifdef SCTL_MASK32
	if ((req->flags & SCTL_MASK32) != 0 && adaptive_machine_arch)
		error = SYSCTL_OUT(req, machine32, sizeof(machine32));
	else
#endif
		error = SYSCTL_OUT(req, machine, sizeof(machine));
	return (error);

}
SYSCTL_PROC(_hw, HW_MACHINE, machine, CTLTYPE_STRING | CTLFLAG_RD |
    CTLFLAG_MPSAFE, NULL, 0, sysctl_hw_machine, "A", "Machine class");
#else
SYSCTL_STRING(_hw, HW_MACHINE, machine, CTLFLAG_RD,
    machine, 0, "Machine class");
#endif

static char cpu_model[128];
SYSCTL_STRING(_hw, HW_MODEL, model, CTLFLAG_RD | CTLFLAG_MPSAFE,
    cpu_model, 0, "Machine model");

static int hw_clockrate;
SYSCTL_INT(_hw, OID_AUTO, clockrate, CTLFLAG_RD,
    &hw_clockrate, 0, "CPU instruction clock rate");

u_int hv_high;
char hv_vendor[16];
SYSCTL_STRING(_hw, OID_AUTO, hv_vendor, CTLFLAG_RD | CTLFLAG_MPSAFE, hv_vendor,
    0, "Hypervisor vendor");

static eventhandler_tag tsc_post_tag;

static char cpu_brand[48];

#ifdef __i386__
#define	MAX_BRAND_INDEX	8

static const char *cpu_brandtable[MAX_BRAND_INDEX + 1] = {
	NULL,			/* No brand */
	"Intel Celeron",
	"Intel Pentium III",
	"Intel Pentium III Xeon",
	NULL,
	NULL,
	NULL,
	NULL,
	"Intel Pentium 4"
};

static struct {
	char	*cpu_name;
	int	cpu_class;
} cpus[] = {
	{ "Intel 80286",	CPUCLASS_286 },		/* CPU_286   */
	{ "i386SX",		CPUCLASS_386 },		/* CPU_386SX */
	{ "i386DX",		CPUCLASS_386 },		/* CPU_386   */
	{ "i486SX",		CPUCLASS_486 },		/* CPU_486SX */
	{ "i486DX",		CPUCLASS_486 },		/* CPU_486   */
	{ "Pentium",		CPUCLASS_586 },		/* CPU_586   */
	{ "Cyrix 486",		CPUCLASS_486 },		/* CPU_486DLC */
	{ "Pentium Pro",	CPUCLASS_686 },		/* CPU_686 */
	{ "Cyrix 5x86",		CPUCLASS_486 },		/* CPU_M1SC */
	{ "Cyrix 6x86",		CPUCLASS_486 },		/* CPU_M1 */
	{ "Blue Lightning",	CPUCLASS_486 },		/* CPU_BLUE */
	{ "Cyrix 6x86MX",	CPUCLASS_686 },		/* CPU_M2 */
	{ "NexGen 586",		CPUCLASS_386 },		/* CPU_NX586 (XXX) */
	{ "Cyrix 486S/DX",	CPUCLASS_486 },		/* CPU_CY486DX */
	{ "Pentium II",		CPUCLASS_686 },		/* CPU_PII */
	{ "Pentium III",	CPUCLASS_686 },		/* CPU_PIII */
	{ "Pentium 4",		CPUCLASS_686 },		/* CPU_P4 */
};
#endif

static struct {
	char	*vendor;
	u_int	vendor_id;
} cpu_vendors[] = {
	{ INTEL_VENDOR_ID,	CPU_VENDOR_INTEL },	/* GenuineIntel */
	{ AMD_VENDOR_ID,	CPU_VENDOR_AMD },	/* AuthenticAMD */
	{ CENTAUR_VENDOR_ID,	CPU_VENDOR_CENTAUR },	/* CentaurHauls */
#ifdef __i386__
	{ NSC_VENDOR_ID,	CPU_VENDOR_NSC },	/* Geode by NSC */
	{ CYRIX_VENDOR_ID,	CPU_VENDOR_CYRIX },	/* CyrixInstead */
	{ TRANSMETA_VENDOR_ID,	CPU_VENDOR_TRANSMETA },	/* GenuineTMx86 */
	{ SIS_VENDOR_ID,	CPU_VENDOR_SIS },	/* SiS SiS SiS  */
	{ UMC_VENDOR_ID,	CPU_VENDOR_UMC },	/* UMC UMC UMC  */
	{ NEXGEN_VENDOR_ID,	CPU_VENDOR_NEXGEN },	/* NexGenDriven */
	{ RISE_VENDOR_ID,	CPU_VENDOR_RISE },	/* RiseRiseRise */
#if 0
	/* XXX CPUID 8000_0000h and 8086_0000h, not 0000_0000h */
	{ "TransmetaCPU",	CPU_VENDOR_TRANSMETA },
#endif
#endif
};

void
printcpuinfo(void)
{
	u_int regs[4], i;
	char *brand;

	printf("CPU: ");
#ifdef __i386__
	cpu_class = cpus[cpu].cpu_class;
	strncpy(cpu_model, cpus[cpu].cpu_name, sizeof (cpu_model));
#else
	strncpy(cpu_model, "Hammer", sizeof (cpu_model));
#endif

	/* Check for extended CPUID information and a processor name. */
	if (cpu_exthigh >= 0x80000004) {
		brand = cpu_brand;
		for (i = 0x80000002; i < 0x80000005; i++) {
			do_cpuid(i, regs);
			memcpy(brand, regs, sizeof(regs));
			brand += sizeof(regs);
		}
	}

	switch (cpu_vendor_id) {
	case CPU_VENDOR_INTEL:
#ifdef __i386__
		if ((cpu_id & 0xf00) > 0x300) {
			u_int brand_index;

			cpu_model[0] = '\0';

			switch (cpu_id & 0x3000) {
			case 0x1000:
				strcpy(cpu_model, "Overdrive ");
				break;
			case 0x2000:
				strcpy(cpu_model, "Dual ");
				break;
			}

			switch (cpu_id & 0xf00) {
			case 0x400:
				strcat(cpu_model, "i486 ");
			        /* Check the particular flavor of 486 */
				switch (cpu_id & 0xf0) {
				case 0x00:
				case 0x10:
					strcat(cpu_model, "DX");
					break;
				case 0x20:
					strcat(cpu_model, "SX");
					break;
				case 0x30:
					strcat(cpu_model, "DX2");
					break;
				case 0x40:
					strcat(cpu_model, "SL");
					break;
				case 0x50:
					strcat(cpu_model, "SX2");
					break;
				case 0x70:
					strcat(cpu_model,
					    "DX2 Write-Back Enhanced");
					break;
				case 0x80:
					strcat(cpu_model, "DX4");
					break;
				}
				break;
			case 0x500:
			        /* Check the particular flavor of 586 */
			        strcat(cpu_model, "Pentium");
			        switch (cpu_id & 0xf0) {
				case 0x00:
				        strcat(cpu_model, " A-step");
					break;
				case 0x10:
				        strcat(cpu_model, "/P5");
					break;
				case 0x20:
				        strcat(cpu_model, "/P54C");
					break;
				case 0x30:
				        strcat(cpu_model, "/P24T");
					break;
				case 0x40:
				        strcat(cpu_model, "/P55C");
					break;
				case 0x70:
				        strcat(cpu_model, "/P54C");
					break;
				case 0x80:
				        strcat(cpu_model, "/P55C (quarter-micron)");
					break;
				default:
				        /* nothing */
					break;
				}
#if defined(I586_CPU) && !defined(NO_F00F_HACK)
				/*
				 * XXX - If/when Intel fixes the bug, this
				 * should also check the version of the
				 * CPU, not just that it's a Pentium.
				 */
				has_f00f_bug = 1;
#endif
				break;
			case 0x600:
			        /* Check the particular flavor of 686 */
  			        switch (cpu_id & 0xf0) {
				case 0x00:
				        strcat(cpu_model, "Pentium Pro A-step");
					break;
				case 0x10:
				        strcat(cpu_model, "Pentium Pro");
					break;
				case 0x30:
				case 0x50:
				case 0x60:
				        strcat(cpu_model,
				"Pentium II/Pentium II Xeon/Celeron");
					cpu = CPU_PII;
					break;
				case 0x70:
				case 0x80:
				case 0xa0:
				case 0xb0:
				        strcat(cpu_model,
					"Pentium III/Pentium III Xeon/Celeron");
					cpu = CPU_PIII;
					break;
				default:
				        strcat(cpu_model, "Unknown 80686");
					break;
				}
				break;
			case 0xf00:
				strcat(cpu_model, "Pentium 4");
				cpu = CPU_P4;
				break;
			default:
				strcat(cpu_model, "unknown");
				break;
			}

			/*
			 * If we didn't get a brand name from the extended
			 * CPUID, try to look it up in the brand table.
			 */
			if (cpu_high > 0 && *cpu_brand == '\0') {
				brand_index = cpu_procinfo & CPUID_BRAND_INDEX;
				if (brand_index <= MAX_BRAND_INDEX &&
				    cpu_brandtable[brand_index] != NULL)
					strcpy(cpu_brand,
					    cpu_brandtable[brand_index]);
			}
		}
#else
		/* Please make up your mind folks! */
		strcat(cpu_model, "EM64T");
#endif
		break;
	case CPU_VENDOR_AMD:
		/*
		 * Values taken from AMD Processor Recognition
		 * http://www.amd.com/K6/k6docs/pdf/20734g.pdf
		 * (also describes ``Features'' encodings.
		 */
		strcpy(cpu_model, "AMD ");
#ifdef __i386__
		switch (cpu_id & 0xFF0) {
		case 0x410:
			strcat(cpu_model, "Standard Am486DX");
			break;
		case 0x430:
			strcat(cpu_model, "Enhanced Am486DX2 Write-Through");
			break;
		case 0x470:
			strcat(cpu_model, "Enhanced Am486DX2 Write-Back");
			break;
		case 0x480:
			strcat(cpu_model, "Enhanced Am486DX4/Am5x86 Write-Through");
			break;
		case 0x490:
			strcat(cpu_model, "Enhanced Am486DX4/Am5x86 Write-Back");
			break;
		case 0x4E0:
			strcat(cpu_model, "Am5x86 Write-Through");
			break;
		case 0x4F0:
			strcat(cpu_model, "Am5x86 Write-Back");
			break;
		case 0x500:
			strcat(cpu_model, "K5 model 0");
			break;
		case 0x510:
			strcat(cpu_model, "K5 model 1");
			break;
		case 0x520:
			strcat(cpu_model, "K5 PR166 (model 2)");
			break;
		case 0x530:
			strcat(cpu_model, "K5 PR200 (model 3)");
			break;
		case 0x560:
			strcat(cpu_model, "K6");
			break;
		case 0x570:
			strcat(cpu_model, "K6 266 (model 1)");
			break;
		case 0x580:
			strcat(cpu_model, "K6-2");
			break;
		case 0x590:
			strcat(cpu_model, "K6-III");
			break;
		case 0x5a0:
			strcat(cpu_model, "Geode LX");
			break;
		default:
			strcat(cpu_model, "Unknown");
			break;
		}
#else
		if ((cpu_id & 0xf00) == 0xf00)
			strcat(cpu_model, "AMD64 Processor");
		else
			strcat(cpu_model, "Unknown");
#endif
		break;
#ifdef __i386__
	case CPU_VENDOR_CYRIX:
		strcpy(cpu_model, "Cyrix ");
		switch (cpu_id & 0xff0) {
		case 0x440:
			strcat(cpu_model, "MediaGX");
			break;
		case 0x520:
			strcat(cpu_model, "6x86");
			break;
		case 0x540:
			cpu_class = CPUCLASS_586;
			strcat(cpu_model, "GXm");
			break;
		case 0x600:
			strcat(cpu_model, "6x86MX");
			break;
		default:
			/*
			 * Even though CPU supports the cpuid
			 * instruction, it can be disabled.
			 * Therefore, this routine supports all Cyrix
			 * CPUs.
			 */
			switch (cyrix_did & 0xf0) {
			case 0x00:
				switch (cyrix_did & 0x0f) {
				case 0x00:
					strcat(cpu_model, "486SLC");
					break;
				case 0x01:
					strcat(cpu_model, "486DLC");
					break;
				case 0x02:
					strcat(cpu_model, "486SLC2");
					break;
				case 0x03:
					strcat(cpu_model, "486DLC2");
					break;
				case 0x04:
					strcat(cpu_model, "486SRx");
					break;
				case 0x05:
					strcat(cpu_model, "486DRx");
					break;
				case 0x06:
					strcat(cpu_model, "486SRx2");
					break;
				case 0x07:
					strcat(cpu_model, "486DRx2");
					break;
				case 0x08:
					strcat(cpu_model, "486SRu");
					break;
				case 0x09:
					strcat(cpu_model, "486DRu");
					break;
				case 0x0a:
					strcat(cpu_model, "486SRu2");
					break;
				case 0x0b:
					strcat(cpu_model, "486DRu2");
					break;
				default:
					strcat(cpu_model, "Unknown");
					break;
				}
				break;
			case 0x10:
				switch (cyrix_did & 0x0f) {
				case 0x00:
					strcat(cpu_model, "486S");
					break;
				case 0x01:
					strcat(cpu_model, "486S2");
					break;
				case 0x02:
					strcat(cpu_model, "486Se");
					break;
				case 0x03:
					strcat(cpu_model, "486S2e");
					break;
				case 0x0a:
					strcat(cpu_model, "486DX");
					break;
				case 0x0b:
					strcat(cpu_model, "486DX2");
					break;
				case 0x0f:
					strcat(cpu_model, "486DX4");
					break;
				default:
					strcat(cpu_model, "Unknown");
					break;
				}
				break;
			case 0x20:
				if ((cyrix_did & 0x0f) < 8)
					strcat(cpu_model, "6x86");	/* Where did you get it? */
				else
					strcat(cpu_model, "5x86");
				break;
			case 0x30:
				strcat(cpu_model, "6x86");
				break;
			case 0x40:
				if ((cyrix_did & 0xf000) == 0x3000) {
					cpu_class = CPUCLASS_586;
					strcat(cpu_model, "GXm");
				} else
					strcat(cpu_model, "MediaGX");
				break;
			case 0x50:
				strcat(cpu_model, "6x86MX");
				break;
			case 0xf0:
				switch (cyrix_did & 0x0f) {
				case 0x0d:
					strcat(cpu_model, "Overdrive CPU");
					break;
				case 0x0e:
					strcpy(cpu_model, "Texas Instruments 486SXL");
					break;
				case 0x0f:
					strcat(cpu_model, "486SLC/DLC");
					break;
				default:
					strcat(cpu_model, "Unknown");
					break;
				}
				break;
			default:
				strcat(cpu_model, "Unknown");
				break;
			}
			break;
		}
		break;
	case CPU_VENDOR_RISE:
		strcpy(cpu_model, "Rise ");
		switch (cpu_id & 0xff0) {
		case 0x500:	/* 6401 and 6441 (Kirin) */
		case 0x520:	/* 6510 (Lynx) */
			strcat(cpu_model, "mP6");
			break;
		default:
			strcat(cpu_model, "Unknown");
		}
		break;
#endif
	case CPU_VENDOR_CENTAUR:
#ifdef __i386__
		switch (cpu_id & 0xff0) {
		case 0x540:
			strcpy(cpu_model, "IDT WinChip C6");
			break;
		case 0x580:
			strcpy(cpu_model, "IDT WinChip 2");
			break;
		case 0x590:
			strcpy(cpu_model, "IDT WinChip 3");
			break;
		case 0x660:
			strcpy(cpu_model, "VIA C3 Samuel");
			break;
		case 0x670:
			if (cpu_id & 0x8)
				strcpy(cpu_model, "VIA C3 Ezra");
			else
				strcpy(cpu_model, "VIA C3 Samuel 2");
			break;
		case 0x680:
			strcpy(cpu_model, "VIA C3 Ezra-T");
			break;
		case 0x690:
			strcpy(cpu_model, "VIA C3 Nehemiah");
			break;
		case 0x6a0:
		case 0x6d0:
			strcpy(cpu_model, "VIA C7 Esther");
			break;
		case 0x6f0:
			strcpy(cpu_model, "VIA Nano");
			break;
		default:
			strcpy(cpu_model, "VIA/IDT Unknown");
		}
#else
		strcpy(cpu_model, "VIA ");
		if ((cpu_id & 0xff0) == 0x6f0)
			strcat(cpu_model, "Nano Processor");
		else
			strcat(cpu_model, "Unknown");
#endif
		break;
#ifdef __i386__
	case CPU_VENDOR_IBM:
		strcpy(cpu_model, "Blue Lightning CPU");
		break;
	case CPU_VENDOR_NSC:
		switch (cpu_id & 0xff0) {
		case 0x540:
			strcpy(cpu_model, "Geode SC1100");
			cpu = CPU_GEODE1100;
			break;
		default:
			strcpy(cpu_model, "Geode/NSC unknown");
			break;
		}
		break;
#endif
	default:
		strcat(cpu_model, "Unknown");
		break;
	}

	/*
	 * Replace cpu_model with cpu_brand minus leading spaces if
	 * we have one.
	 */
	brand = cpu_brand;
	while (*brand == ' ')
		++brand;
	if (*brand != '\0')
		strcpy(cpu_model, brand);

	printf("%s (", cpu_model);
	if (tsc_freq != 0) {
		hw_clockrate = (tsc_freq + 5000) / 1000000;
		printf("%jd.%02d-MHz ",
		    (intmax_t)(tsc_freq + 4999) / 1000000,
		    (u_int)((tsc_freq + 4999) / 10000) % 100);
	}
#ifdef __i386__
	switch(cpu_class) {
	case CPUCLASS_286:
		printf("286");
		break;
	case CPUCLASS_386:
		printf("386");
		break;
#if defined(I486_CPU)
	case CPUCLASS_486:
		printf("486");
		break;
#endif
#if defined(I586_CPU)
	case CPUCLASS_586:
		printf("586");
		break;
#endif
#if defined(I686_CPU)
	case CPUCLASS_686:
		printf("686");
		break;
#endif
	default:
		printf("Unknown");	/* will panic below... */
	}
#else
	printf("K8");
#endif
	printf("-class CPU)\n");
	if (*cpu_vendor)
		printf("  Origin=\"%s\"", cpu_vendor);
	if (cpu_id)
		printf("  Id=0x%x", cpu_id);

	if (cpu_vendor_id == CPU_VENDOR_INTEL ||
	    cpu_vendor_id == CPU_VENDOR_AMD ||
	    cpu_vendor_id == CPU_VENDOR_CENTAUR ||
#ifdef __i386__
	    cpu_vendor_id == CPU_VENDOR_TRANSMETA ||
	    cpu_vendor_id == CPU_VENDOR_RISE ||
	    cpu_vendor_id == CPU_VENDOR_NSC ||
	    (cpu_vendor_id == CPU_VENDOR_CYRIX && ((cpu_id & 0xf00) > 0x500)) ||
#endif
	    0) {
		printf("  Family=0x%x", CPUID_TO_FAMILY(cpu_id));
		printf("  Model=0x%x", CPUID_TO_MODEL(cpu_id));
		printf("  Stepping=%u", cpu_id & CPUID_STEPPING);
#ifdef __i386__
		if (cpu_vendor_id == CPU_VENDOR_CYRIX)
			printf("\n  DIR=0x%04x", cyrix_did);
#endif

		/*
		 * AMD CPUID Specification
		 * http://support.amd.com/us/Embedded_TechDocs/25481.pdf
		 *
		 * Intel Processor Identification and CPUID Instruction
		 * http://www.intel.com/assets/pdf/appnote/241618.pdf
		 */
		if (cpu_high > 0) {

			/*
			 * Here we should probably set up flags indicating
			 * whether or not various features are available.
			 * The interesting ones are probably VME, PSE, PAE,
			 * and PGE.  The code already assumes without bothering
			 * to check that all CPUs >= Pentium have a TSC and
			 * MSRs.
			 */
			printf("\n  Features=0x%b", cpu_feature,
			"\020"
			"\001FPU"	/* Integral FPU */
			"\002VME"	/* Extended VM86 mode support */
			"\003DE"	/* Debugging Extensions (CR4.DE) */
			"\004PSE"	/* 4MByte page tables */
			"\005TSC"	/* Timestamp counter */
			"\006MSR"	/* Machine specific registers */
			"\007PAE"	/* Physical address extension */
			"\010MCE"	/* Machine Check support */
			"\011CX8"	/* CMPEXCH8 instruction */
			"\012APIC"	/* SMP local APIC */
			"\013oldMTRR"	/* Previous implementation of MTRR */
			"\014SEP"	/* Fast System Call */
			"\015MTRR"	/* Memory Type Range Registers */
			"\016PGE"	/* PG_G (global bit) support */
			"\017MCA"	/* Machine Check Architecture */
			"\020CMOV"	/* CMOV instruction */
			"\021PAT"	/* Page attributes table */
			"\022PSE36"	/* 36 bit address space support */
			"\023PN"	/* Processor Serial number */
			"\024CLFLUSH"	/* Has the CLFLUSH instruction */
			"\025<b20>"
			"\026DTS"	/* Debug Trace Store */
			"\027ACPI"	/* ACPI support */
			"\030MMX"	/* MMX instructions */
			"\031FXSR"	/* FXSAVE/FXRSTOR */
			"\032SSE"	/* Streaming SIMD Extensions */
			"\033SSE2"	/* Streaming SIMD Extensions #2 */
			"\034SS"	/* Self snoop */
			"\035HTT"	/* Hyperthreading (see EBX bit 16-23) */
			"\036TM"	/* Thermal Monitor clock slowdown */
			"\037IA64"	/* CPU can execute IA64 instructions */
			"\040PBE"	/* Pending Break Enable */
			);

			if (cpu_feature2 != 0) {
				printf("\n  Features2=0x%b", cpu_feature2,
				"\020"
				"\001SSE3"	/* SSE3 */
				"\002PCLMULQDQ"	/* Carry-Less Mul Quadword */
				"\003DTES64"	/* 64-bit Debug Trace */
				"\004MON"	/* MONITOR/MWAIT Instructions */
				"\005DS_CPL"	/* CPL Qualified Debug Store */
				"\006VMX"	/* Virtual Machine Extensions */
				"\007SMX"	/* Safer Mode Extensions */
				"\010EST"	/* Enhanced SpeedStep */
				"\011TM2"	/* Thermal Monitor 2 */
				"\012SSSE3"	/* SSSE3 */
				"\013CNXT-ID"	/* L1 context ID available */
				"\014SDBG"	/* IA32 silicon debug */
				"\015FMA"	/* Fused Multiply Add */
				"\016CX16"	/* CMPXCHG16B Instruction */
				"\017xTPR"	/* Send Task Priority Messages*/
				"\020PDCM"	/* Perf/Debug Capability MSR */
				"\021<b16>"
				"\022PCID"	/* Process-context Identifiers*/
				"\023DCA"	/* Direct Cache Access */
				"\024SSE4.1"	/* SSE 4.1 */
				"\025SSE4.2"	/* SSE 4.2 */
				"\026x2APIC"	/* xAPIC Extensions */
				"\027MOVBE"	/* MOVBE Instruction */
				"\030POPCNT"	/* POPCNT Instruction */
				"\031TSCDLT"	/* TSC-Deadline Timer */
				"\032AESNI"	/* AES Crypto */
				"\033XSAVE"	/* XSAVE/XRSTOR States */
				"\034OSXSAVE"	/* OS-Enabled State Management*/
				"\035AVX"	/* Advanced Vector Extensions */
				"\036F16C"	/* Half-precision conversions */
				"\037RDRAND"	/* RDRAND Instruction */
				"\040HV"	/* Hypervisor */
				);
			}

			if (amd_feature != 0) {
				printf("\n  AMD Features=0x%b", amd_feature,
				"\020"		/* in hex */
				"\001<s0>"	/* Same */
				"\002<s1>"	/* Same */
				"\003<s2>"	/* Same */
				"\004<s3>"	/* Same */
				"\005<s4>"	/* Same */
				"\006<s5>"	/* Same */
				"\007<s6>"	/* Same */
				"\010<s7>"	/* Same */
				"\011<s8>"	/* Same */
				"\012<s9>"	/* Same */
				"\013<b10>"	/* Undefined */
				"\014SYSCALL"	/* Have SYSCALL/SYSRET */
				"\015<s12>"	/* Same */
				"\016<s13>"	/* Same */
				"\017<s14>"	/* Same */
				"\020<s15>"	/* Same */
				"\021<s16>"	/* Same */
				"\022<s17>"	/* Same */
				"\023<b18>"	/* Reserved, unknown */
				"\024MP"	/* Multiprocessor Capable */
				"\025NX"	/* Has EFER.NXE, NX */
				"\026<b21>"	/* Undefined */
				"\027MMX+"	/* AMD MMX Extensions */
				"\030<s23>"	/* Same */
				"\031<s24>"	/* Same */
				"\032FFXSR"	/* Fast FXSAVE/FXRSTOR */
				"\033Page1GB"	/* 1-GB large page support */
				"\034RDTSCP"	/* RDTSCP */
				"\035<b28>"	/* Undefined */
				"\036LM"	/* 64 bit long mode */
				"\0373DNow!+"	/* AMD 3DNow! Extensions */
				"\0403DNow!"	/* AMD 3DNow! */
				);
			}

			if (amd_feature2 != 0) {
				printf("\n  AMD Features2=0x%b", amd_feature2,
				"\020"
				"\001LAHF"	/* LAHF/SAHF in long mode */
				"\002CMP"	/* CMP legacy */
				"\003SVM"	/* Secure Virtual Mode */
				"\004ExtAPIC"	/* Extended APIC register */
				"\005CR8"	/* CR8 in legacy mode */
				"\006ABM"	/* LZCNT instruction */
				"\007SSE4A"	/* SSE4A */
				"\010MAS"	/* Misaligned SSE mode */
				"\011Prefetch"	/* 3DNow! Prefetch/PrefetchW */
				"\012OSVW"	/* OS visible workaround */
				"\013IBS"	/* Instruction based sampling */
				"\014XOP"	/* XOP extended instructions */
				"\015SKINIT"	/* SKINIT/STGI */
				"\016WDT"	/* Watchdog timer */
				"\017<b14>"
				"\020LWP"	/* Lightweight Profiling */
				"\021FMA4"	/* 4-operand FMA instructions */
				"\022TCE"	/* Translation Cache Extension */
				"\023<b18>"
				"\024NodeId"	/* NodeId MSR support */
				"\025<b20>"
				"\026TBM"	/* Trailing Bit Manipulation */
				"\027Topology"	/* Topology Extensions */
				"\030PCXC"	/* Core perf count */
				"\031PNXC"	/* NB perf count */
				"\032<b25>"
				"\033DBE"	/* Data Breakpoint extension */
				"\034PTSC"	/* Performance TSC */
				"\035PL2I"	/* L2I perf count */
				"\036MWAITX"	/* MONITORX/MWAITX instructions */
				"\037<b30>"
				"\040<b31>"
				);
			}

			if (cpu_stdext_feature != 0) {
				printf("\n  Structured Extended Features=0x%b",
				    cpu_stdext_feature,
				       "\020"
				       /* RDFSBASE/RDGSBASE/WRFSBASE/WRGSBASE */
				       "\001FSGSBASE"
				       "\002TSCADJ"
				       "\003SGX"
				       /* Bit Manipulation Instructions */
				       "\004BMI1"
				       /* Hardware Lock Elision */
				       "\005HLE"
				       /* Advanced Vector Instructions 2 */
				       "\006AVX2"
				       /* FDP_EXCPTN_ONLY */
				       "\007FDPEXC"
				       /* Supervisor Mode Execution Prot. */
				       "\010SMEP"
				       /* Bit Manipulation Instructions */
				       "\011BMI2"
				       "\012ERMS"
				       /* Invalidate Processor Context ID */
				       "\013INVPCID"
				       /* Restricted Transactional Memory */
				       "\014RTM"
				       "\015PQM"
				       "\016NFPUSG"
				       /* Intel Memory Protection Extensions */
				       "\017MPX"
				       "\020PQE"
				       /* AVX512 Foundation */
				       "\021AVX512F"
				       "\022AVX512DQ"
				       /* Enhanced NRBG */
				       "\023RDSEED"
				       /* ADCX + ADOX */
				       "\024ADX"
				       /* Supervisor Mode Access Prevention */
				       "\025SMAP"
				       "\026AVX512IFMA"
				       "\027PCOMMIT"
				       "\030CLFLUSHOPT"
				       "\031CLWB"
				       "\032PROCTRACE"
				       "\033AVX512PF"
				       "\034AVX512ER"
				       "\035AVX512CD"
				       "\036SHA"
				       "\037AVX512BW"
				       "\040AVX512VL"
				       );
			}

			if (cpu_stdext_feature2 != 0) {
				printf("\n  Structured Extended Features2=0x%b",
				    cpu_stdext_feature2,
				       "\020"
				       "\001PREFETCHWT1"
				       "\002AVX512VBMI"
				       "\003UMIP"
				       "\004PKU"
				       "\005OSPKE"
				       "\006WAITPKG"
				       "\011GFNI"
				       "\027RDPID"
				       "\032CLDEMOTE"
				       "\034MOVDIRI"
				       "\035MOVDIRI64B"
				       "\037SGXLC"
				       );
			}

			if (cpu_stdext_feature3 != 0) {
				printf("\n  Structured Extended Features3=0x%b",
				    cpu_stdext_feature3,
				       "\020"
				       "\016TSXFA"
				       "\033IBPB"
				       "\034STIBP"
				       "\035L1DFL"
				       "\036ARCH_CAP"
				       "\037CORE_CAP"
				       "\040SSBD"
				       );
			}

			if ((cpu_feature2 & CPUID2_XSAVE) != 0) {
				cpuid_count(0xd, 0x1, regs);
				if (regs[0] != 0) {
					printf("\n  XSAVE Features=0x%b",
					    regs[0],
					    "\020"
					    "\001XSAVEOPT"
					    "\002XSAVEC"
					    "\003XINUSE"
					    "\004XSAVES");
				}
			}

			if (cpu_ia32_arch_caps != 0) {
				printf("\n  IA32_ARCH_CAPS=0x%b",
				    (u_int)cpu_ia32_arch_caps,
				       "\020"
				       "\001RDCL_NO"
				       "\002IBRS_ALL"
				       "\003RSBA"
				       "\004SKIP_L1DFL_VME"
				       "\005SSB_NO"
				       );
			}

			if (amd_extended_feature_extensions != 0) {
				u_int amd_fe_masked;

				amd_fe_masked = amd_extended_feature_extensions;
				if ((amd_fe_masked & AMDFEID_IBRS) == 0)
					amd_fe_masked &=
					    ~(AMDFEID_IBRS_ALWAYSON |
						AMDFEID_PREFER_IBRS);
				if ((amd_fe_masked & AMDFEID_STIBP) == 0)
					amd_fe_masked &=
					    ~AMDFEID_STIBP_ALWAYSON;

				printf("\n  "
				    "AMD Extended Feature Extensions ID EBX="
				    "0x%b", amd_fe_masked,
				    "\020"
				    "\001CLZERO"
				    "\002IRPerf"
				    "\003XSaveErPtr"
				    "\015IBPB"
				    "\017IBRS"
				    "\020STIBP"
				    "\021IBRS_ALWAYSON"
				    "\022STIBP_ALWAYSON"
				    "\023PREFER_IBRS"
				    "\031SSBD"
				    "\032VIRT_SSBD"
				    "\033SSB_NO"
				    );
			}

			if (via_feature_rng != 0 || via_feature_xcrypt != 0)
				print_via_padlock_info();

			if (cpu_feature2 & CPUID2_VMX)
				print_vmx_info();

			if (amd_feature2 & AMDID2_SVM)
				print_svm_info();

			if ((cpu_feature & CPUID_HTT) &&
			    cpu_vendor_id == CPU_VENDOR_AMD)
				cpu_feature &= ~CPUID_HTT;

			/*
			 * If this CPU supports P-state invariant TSC then
			 * mention the capability.
			 */
			if (tsc_is_invariant) {
				printf("\n  TSC: P-state invariant");
				if (tsc_perf_stat)
					printf(", performance statistics");
			}
		}
#ifdef __i386__
	} else if (cpu_vendor_id == CPU_VENDOR_CYRIX) {
		printf("  DIR=0x%04x", cyrix_did);
		printf("  Stepping=%u", (cyrix_did & 0xf000) >> 12);
		printf("  Revision=%u", (cyrix_did & 0x0f00) >> 8);
#ifndef CYRIX_CACHE_REALLY_WORKS
		if (cpu == CPU_M1 && (cyrix_did & 0xff00) < 0x1700)
			printf("\n  CPU cache: write-through mode");
#endif
#endif
	}

	/* Avoid ugly blank lines: only print newline when we have to. */
	if (*cpu_vendor || cpu_id)
		printf("\n");

	if (bootverbose) {
		if (cpu_vendor_id == CPU_VENDOR_AMD)
			print_AMD_info();
		else if (cpu_vendor_id == CPU_VENDOR_INTEL)
			print_INTEL_info();
#ifdef __i386__
		else if (cpu_vendor_id == CPU_VENDOR_TRANSMETA)
			print_transmeta_info();
#endif
	}

	print_hypervisor_info();
}

#ifdef __i386__
void
panicifcpuunsupported(void)
{

#if !defined(lint)
#if !defined(I486_CPU) && !defined(I586_CPU) && !defined(I686_CPU)
#error This kernel is not configured for one of the supported CPUs
#endif
#else /* lint */
#endif /* lint */
	/*
	 * Now that we have told the user what they have,
	 * let them know if that machine type isn't configured.
	 */
	switch (cpu_class) {
	case CPUCLASS_286:	/* a 286 should not make it this far, anyway */
	case CPUCLASS_386:
#if !defined(I486_CPU)
	case CPUCLASS_486:
#endif
#if !defined(I586_CPU)
	case CPUCLASS_586:
#endif
#if !defined(I686_CPU)
	case CPUCLASS_686:
#endif
		panic("CPU class not configured");
	default:
		break;
	}
}

static	volatile u_int trap_by_rdmsr;

/*
 * Special exception 6 handler.
 * The rdmsr instruction generates invalid opcodes fault on 486-class
 * Cyrix CPU.  Stacked eip register points the rdmsr instruction in the
 * function identblue() when this handler is called.  Stacked eip should
 * be advanced.
 */
inthand_t	bluetrap6;
#ifdef __GNUCLIKE_ASM
__asm
("									\n\
	.text								\n\
	.p2align 2,0x90							\n\
	.type	" __XSTRING(CNAME(bluetrap6)) ",@function		\n\
" __XSTRING(CNAME(bluetrap6)) ":					\n\
	ss								\n\
	movl	$0xa8c1d," __XSTRING(CNAME(trap_by_rdmsr)) "		\n\
	addl	$2, (%esp)	/* rdmsr is a 2-byte instruction */	\n\
	iret								\n\
");
#endif

/*
 * Special exception 13 handler.
 * Accessing non-existent MSR generates general protection fault.
 */
inthand_t	bluetrap13;
#ifdef __GNUCLIKE_ASM
__asm
("									\n\
	.text								\n\
	.p2align 2,0x90							\n\
	.type	" __XSTRING(CNAME(bluetrap13)) ",@function		\n\
" __XSTRING(CNAME(bluetrap13)) ":					\n\
	ss								\n\
	movl	$0xa89c4," __XSTRING(CNAME(trap_by_rdmsr)) "		\n\
	popl	%eax		/* discard error code */		\n\
	addl	$2, (%esp)	/* rdmsr is a 2-byte instruction */	\n\
	iret								\n\
");
#endif

/*
 * Distinguish IBM Blue Lightning CPU from Cyrix CPUs that does not
 * support cpuid instruction.  This function should be called after
 * loading interrupt descriptor table register.
 *
 * I don't like this method that handles fault, but I couldn't get
 * information for any other methods.  Does blue giant know?
 */
static int
identblue(void)
{

	trap_by_rdmsr = 0;

	/*
	 * Cyrix 486-class CPU does not support rdmsr instruction.
	 * The rdmsr instruction generates invalid opcode fault, and exception
	 * will be trapped by bluetrap6() on Cyrix 486-class CPU.  The
	 * bluetrap6() set the magic number to trap_by_rdmsr.
	 */
	setidt(IDT_UD, bluetrap6, SDT_SYS386TGT, SEL_KPL,
	    GSEL(GCODE_SEL, SEL_KPL));

	/*
	 * Certain BIOS disables cpuid instruction of Cyrix 6x86MX CPU.
	 * In this case, rdmsr generates general protection fault, and
	 * exception will be trapped by bluetrap13().
	 */
	setidt(IDT_GP, bluetrap13, SDT_SYS386TGT, SEL_KPL,
	    GSEL(GCODE_SEL, SEL_KPL));

	rdmsr(0x1002);		/* Cyrix CPU generates fault. */

	if (trap_by_rdmsr == 0xa8c1d)
		return IDENTBLUE_CYRIX486;
	else if (trap_by_rdmsr == 0xa89c4)
		return IDENTBLUE_CYRIXM2;
	return IDENTBLUE_IBMCPU;
}


/*
 * identifycyrix() set lower 16 bits of cyrix_did as follows:
 *
 *  F E D C B A 9 8 7 6 5 4 3 2 1 0
 * +-------+-------+---------------+
 * |  SID  |  RID  |   Device ID   |
 * |    (DIR 1)    |    (DIR 0)    |
 * +-------+-------+---------------+
 */
static void
identifycyrix(void)
{
	register_t saveintr;
	int	ccr2_test = 0, dir_test = 0;
	u_char	ccr2, ccr3;

	saveintr = intr_disable();

	ccr2 = read_cyrix_reg(CCR2);
	write_cyrix_reg(CCR2, ccr2 ^ CCR2_LOCK_NW);
	read_cyrix_reg(CCR2);
	if (read_cyrix_reg(CCR2) != ccr2)
		ccr2_test = 1;
	write_cyrix_reg(CCR2, ccr2);

	ccr3 = read_cyrix_reg(CCR3);
	write_cyrix_reg(CCR3, ccr3 ^ CCR3_MAPEN3);
	read_cyrix_reg(CCR3);
	if (read_cyrix_reg(CCR3) != ccr3)
		dir_test = 1;					/* CPU supports DIRs. */
	write_cyrix_reg(CCR3, ccr3);

	if (dir_test) {
		/* Device ID registers are available. */
		cyrix_did = read_cyrix_reg(DIR1) << 8;
		cyrix_did += read_cyrix_reg(DIR0);
	} else if (ccr2_test)
		cyrix_did = 0x0010;		/* 486S A-step */
	else
		cyrix_did = 0x00ff;		/* Old 486SLC/DLC and TI486SXLC/SXL */

	intr_restore(saveintr);
}
#endif

/* Update TSC freq with the value indicated by the caller. */
static void
tsc_freq_changed(void *arg __unused, const struct cf_level *level, int status)
{

	/* If there was an error during the transition, don't do anything. */
	if (status != 0)
		return;

	/* Total setting for this level gives the new frequency in MHz. */
	hw_clockrate = level->total_set.freq;
}

static void
hook_tsc_freq(void *arg __unused)
{

	if (tsc_is_invariant)
		return;

	tsc_post_tag = EVENTHANDLER_REGISTER(cpufreq_post_change,
	    tsc_freq_changed, NULL, EVENTHANDLER_PRI_ANY);
}

SYSINIT(hook_tsc_freq, SI_SUB_CONFIGURE, SI_ORDER_ANY, hook_tsc_freq, NULL);

static const char *const vm_bnames[] = {
	"QEMU",				/* QEMU */
	"Plex86",			/* Plex86 */
	"Bochs",			/* Bochs */
	"Xen",				/* Xen */
	"BHYVE",			/* bhyve */
	"Seabios",			/* KVM */
	NULL
};

static const char *const vm_pnames[] = {
	"VMware Virtual Platform",	/* VMWare VM */
	"Virtual Machine",		/* Microsoft VirtualPC */
	"VirtualBox",			/* Sun xVM VirtualBox */
	"Parallels Virtual Platform",	/* Parallels VM */
	"KVM",				/* KVM */
	NULL
};

void
identify_hypervisor(void)
{
	u_int regs[4];
	char *p;
	int i;

	/*
	 * [RFC] CPUID usage for interaction between Hypervisors and Linux.
	 * http://lkml.org/lkml/2008/10/1/246
	 *
	 * KB1009458: Mechanisms to determine if software is running in
	 * a VMware virtual machine
	 * http://kb.vmware.com/kb/1009458
	 */
	if (cpu_feature2 & CPUID2_HV) {
		vm_guest = VM_GUEST_VM;
		do_cpuid(0x40000000, regs);

		/*
		 * KVM from Linux kernels prior to commit
		 * 57c22e5f35aa4b9b2fe11f73f3e62bbf9ef36190 set %eax
		 * to 0 rather than a valid hv_high value.  Check for
		 * the KVM signature bytes and fixup %eax to the
		 * highest supported leaf in that case.
		 */
		if (regs[0] == 0 && regs[1] == 0x4b4d564b &&
		    regs[2] == 0x564b4d56 && regs[3] == 0x0000004d)
			regs[0] = 0x40000001;
			
		if (regs[0] >= 0x40000000) {
			hv_high = regs[0];
			((u_int *)&hv_vendor)[0] = regs[1];
			((u_int *)&hv_vendor)[1] = regs[2];
			((u_int *)&hv_vendor)[2] = regs[3];
			hv_vendor[12] = '\0';
			if (strcmp(hv_vendor, "VMwareVMware") == 0)
				vm_guest = VM_GUEST_VMWARE;
			else if (strcmp(hv_vendor, "Microsoft Hv") == 0)
				vm_guest = VM_GUEST_HV;
			else if (strcmp(hv_vendor, "KVMKVMKVM") == 0)
				vm_guest = VM_GUEST_KVM;
			else if (strcmp(hv_vendor, "bhyve bhyve") == 0)
				vm_guest = VM_GUEST_BHYVE;
		}
		return;
	}

	/*
	 * Examine SMBIOS strings for older hypervisors.
	 */
	p = kern_getenv("smbios.system.serial");
	if (p != NULL) {
		if (strncmp(p, "VMware-", 7) == 0 || strncmp(p, "VMW", 3) == 0) {
			vmware_hvcall(VMW_HVCMD_GETVERSION, regs);
			if (regs[1] == VMW_HVMAGIC) {
				vm_guest = VM_GUEST_VMWARE;			
				freeenv(p);
				return;
			}
		}
		freeenv(p);
	}

	/*
	 * XXX: Some of these entries may not be needed since they were
	 * added to FreeBSD before the checks above.
	 */
	p = kern_getenv("smbios.bios.vendor");
	if (p != NULL) {
		for (i = 0; vm_bnames[i] != NULL; i++)
			if (strcmp(p, vm_bnames[i]) == 0) {
				vm_guest = VM_GUEST_VM;
				freeenv(p);
				return;
			}
		freeenv(p);
	}
	p = kern_getenv("smbios.system.product");
	if (p != NULL) {
		for (i = 0; vm_pnames[i] != NULL; i++)
			if (strcmp(p, vm_pnames[i]) == 0) {
				vm_guest = VM_GUEST_VM;
				freeenv(p);
				return;
			}
		freeenv(p);
	}
}

bool
fix_cpuid(void)
{
	uint64_t msr;

	/*
	 * Clear "Limit CPUID Maxval" bit and return true if the caller should
	 * get the largest standard CPUID function number again if it is set
	 * from BIOS.  It is necessary for probing correct CPU topology later
	 * and for the correct operation of the AVX-aware userspace.
	 */
	if (cpu_vendor_id == CPU_VENDOR_INTEL &&
	    ((CPUID_TO_FAMILY(cpu_id) == 0xf &&
	    CPUID_TO_MODEL(cpu_id) >= 0x3) ||
	    (CPUID_TO_FAMILY(cpu_id) == 0x6 &&
	    CPUID_TO_MODEL(cpu_id) >= 0xe))) {
		msr = rdmsr(MSR_IA32_MISC_ENABLE);
		if ((msr & IA32_MISC_EN_LIMCPUID) != 0) {
			msr &= ~IA32_MISC_EN_LIMCPUID;
			wrmsr(MSR_IA32_MISC_ENABLE, msr);
			return (true);
		}
	}

	/*
	 * Re-enable AMD Topology Extension that could be disabled by BIOS
	 * on some notebook processors.  Without the extension it's really
	 * hard to determine the correct CPU cache topology.
	 * See BIOS and Kernel Developer’s Guide (BKDG) for AMD Family 15h
	 * Models 60h-6Fh Processors, Publication # 50742.
	 */
	if (vm_guest == VM_GUEST_NO && cpu_vendor_id == CPU_VENDOR_AMD &&
	    CPUID_TO_FAMILY(cpu_id) == 0x15) {
		msr = rdmsr(MSR_EXTFEATURES);
		if ((msr & ((uint64_t)1 << 54)) == 0) {
			msr |= (uint64_t)1 << 54;
			wrmsr(MSR_EXTFEATURES, msr);
			return (true);
		}
	}
	return (false);
}

void
identify_cpu1(void)
{
	u_int regs[4];

	do_cpuid(0, regs);
	cpu_high = regs[0];
	((u_int *)&cpu_vendor)[0] = regs[1];
	((u_int *)&cpu_vendor)[1] = regs[3];
	((u_int *)&cpu_vendor)[2] = regs[2];
	cpu_vendor[12] = '\0';

	do_cpuid(1, regs);
	cpu_id = regs[0];
	cpu_procinfo = regs[1];
	cpu_feature = regs[3];
	cpu_feature2 = regs[2];
}

void
identify_cpu2(void)
{
	u_int regs[4], cpu_stdext_disable;

	if (cpu_high >= 7) {
		cpuid_count(7, 0, regs);
		cpu_stdext_feature = regs[1];

		/*
		 * Some hypervisors failed to filter out unsupported
		 * extended features.  Allow to disable the
		 * extensions, activation of which requires setting a
		 * bit in CR4, and which VM monitors do not support.
		 */
		cpu_stdext_disable = 0;
		TUNABLE_INT_FETCH("hw.cpu_stdext_disable", &cpu_stdext_disable);
		cpu_stdext_feature &= ~cpu_stdext_disable;

		cpu_stdext_feature2 = regs[2];
		cpu_stdext_feature3 = regs[3];

		if ((cpu_stdext_feature3 & CPUID_STDEXT3_ARCH_CAP) != 0)
			cpu_ia32_arch_caps = rdmsr(MSR_IA32_ARCH_CAP);
	}
}

void
identify_cpu_fixup_bsp(void)
{
	u_int regs[4];

	cpu_vendor_id = find_cpu_vendor_id();

	if (fix_cpuid()) {
		do_cpuid(0, regs);
		cpu_high = regs[0];
	}
}

/*
 * Final stage of CPU identification.
 */
void
finishidentcpu(void)
{
	u_int regs[4];
#ifdef __i386__
	u_char ccr3;
#endif

	identify_cpu_fixup_bsp();

	if (cpu_high >= 5 && (cpu_feature2 & CPUID2_MON) != 0) {
		do_cpuid(5, regs);
		cpu_mon_mwait_flags = regs[2];
		cpu_mon_min_size = regs[0] &  CPUID5_MON_MIN_SIZE;
		cpu_mon_max_size = regs[1] &  CPUID5_MON_MAX_SIZE;
	}

	identify_cpu2();

#ifdef __i386__
	if (cpu_high > 0 &&
	    (cpu_vendor_id == CPU_VENDOR_INTEL ||
	     cpu_vendor_id == CPU_VENDOR_AMD ||
	     cpu_vendor_id == CPU_VENDOR_TRANSMETA ||
	     cpu_vendor_id == CPU_VENDOR_CENTAUR ||
	     cpu_vendor_id == CPU_VENDOR_NSC)) {
		do_cpuid(0x80000000, regs);
		if (regs[0] >= 0x80000000)
			cpu_exthigh = regs[0];
	}
#else
	if (cpu_vendor_id == CPU_VENDOR_INTEL ||
	    cpu_vendor_id == CPU_VENDOR_AMD ||
	    cpu_vendor_id == CPU_VENDOR_CENTAUR) {
		do_cpuid(0x80000000, regs);
		cpu_exthigh = regs[0];
	}
#endif
	if (cpu_exthigh >= 0x80000001) {
		do_cpuid(0x80000001, regs);
		amd_feature = regs[3] & ~(cpu_feature & 0x0183f3ff);
		amd_feature2 = regs[2];
	}
	if (cpu_exthigh >= 0x80000007) {
		do_cpuid(0x80000007, regs);
		amd_rascap = regs[1];
		amd_pminfo = regs[3];
	}
	if (cpu_exthigh >= 0x80000008) {
		do_cpuid(0x80000008, regs);
		cpu_maxphyaddr = regs[0] & 0xff;
		amd_extended_feature_extensions = regs[1];
		cpu_procinfo2 = regs[2];
	} else {
		cpu_maxphyaddr = (cpu_feature & CPUID_PAE) != 0 ? 36 : 32;
	}

#ifdef __i386__
	if (cpu_vendor_id == CPU_VENDOR_CYRIX) {
		if (cpu == CPU_486) {
			/*
			 * These conditions are equivalent to:
			 *     - CPU does not support cpuid instruction.
			 *     - Cyrix/IBM CPU is detected.
			 */
			if (identblue() == IDENTBLUE_IBMCPU) {
				strcpy(cpu_vendor, "IBM");
				cpu_vendor_id = CPU_VENDOR_IBM;
				cpu = CPU_BLUE;
				return;
			}
		}
		switch (cpu_id & 0xf00) {
		case 0x600:
			/*
			 * Cyrix's datasheet does not describe DIRs.
			 * Therefor, I assume it does not have them
			 * and use the result of the cpuid instruction.
			 * XXX they seem to have it for now at least. -Peter
			 */
			identifycyrix();
			cpu = CPU_M2;
			break;
		default:
			identifycyrix();
			/*
			 * This routine contains a trick.
			 * Don't check (cpu_id & 0x00f0) == 0x50 to detect M2, now.
			 */
			switch (cyrix_did & 0x00f0) {
			case 0x00:
			case 0xf0:
				cpu = CPU_486DLC;
				break;
			case 0x10:
				cpu = CPU_CY486DX;
				break;
			case 0x20:
				if ((cyrix_did & 0x000f) < 8)
					cpu = CPU_M1;
				else
					cpu = CPU_M1SC;
				break;
			case 0x30:
				cpu = CPU_M1;
				break;
			case 0x40:
				/* MediaGX CPU */
				cpu = CPU_M1SC;
				break;
			default:
				/* M2 and later CPUs are treated as M2. */
				cpu = CPU_M2;

				/*
				 * enable cpuid instruction.
				 */
				ccr3 = read_cyrix_reg(CCR3);
				write_cyrix_reg(CCR3, CCR3_MAPEN0);
				write_cyrix_reg(CCR4, read_cyrix_reg(CCR4) | CCR4_CPUID);
				write_cyrix_reg(CCR3, ccr3);

				do_cpuid(0, regs);
				cpu_high = regs[0];	/* eax */
				do_cpuid(1, regs);
				cpu_id = regs[0];	/* eax */
				cpu_feature = regs[3];	/* edx */
				break;
			}
		}
	} else if (cpu == CPU_486 && *cpu_vendor == '\0') {
		/*
		 * There are BlueLightning CPUs that do not change
		 * undefined flags by dividing 5 by 2.  In this case,
		 * the CPU identification routine in locore.s leaves
		 * cpu_vendor null string and puts CPU_486 into the
		 * cpu.
		 */
		if (identblue() == IDENTBLUE_IBMCPU) {
			strcpy(cpu_vendor, "IBM");
			cpu_vendor_id = CPU_VENDOR_IBM;
			cpu = CPU_BLUE;
			return;
		}
	}
#endif
}

int
pti_get_default(void)
{

	if (strcmp(cpu_vendor, AMD_VENDOR_ID) == 0)
		return (0);
	if ((cpu_ia32_arch_caps & IA32_ARCH_CAP_RDCL_NO) != 0)
		return (0);
	return (1);
}

static u_int
find_cpu_vendor_id(void)
{
	int	i;

	for (i = 0; i < nitems(cpu_vendors); i++)
		if (strcmp(cpu_vendor, cpu_vendors[i].vendor) == 0)
			return (cpu_vendors[i].vendor_id);
	return (0);
}

static void
print_AMD_assoc(int i)
{
	if (i == 255)
		printf(", fully associative\n");
	else
		printf(", %d-way associative\n", i);
}

static void
print_AMD_l2_assoc(int i)
{
	switch (i & 0x0f) {
	case 0: printf(", disabled/not present\n"); break;
	case 1: printf(", direct mapped\n"); break;
	case 2: printf(", 2-way associative\n"); break;
	case 4: printf(", 4-way associative\n"); break;
	case 6: printf(", 8-way associative\n"); break;
	case 8: printf(", 16-way associative\n"); break;
	case 15: printf(", fully associative\n"); break;
	default: printf(", reserved configuration\n"); break;
	}
}

static void
print_AMD_info(void)
{
#ifdef __i386__
	uint64_t amd_whcr;
#endif
	u_int regs[4];

	if (cpu_exthigh >= 0x80000005) {
		do_cpuid(0x80000005, regs);
		printf("L1 2MB data TLB: %d entries", (regs[0] >> 16) & 0xff);
		print_AMD_assoc(regs[0] >> 24);

		printf("L1 2MB instruction TLB: %d entries", regs[0] & 0xff);
		print_AMD_assoc((regs[0] >> 8) & 0xff);

		printf("L1 4KB data TLB: %d entries", (regs[1] >> 16) & 0xff);
		print_AMD_assoc(regs[1] >> 24);

		printf("L1 4KB instruction TLB: %d entries", regs[1] & 0xff);
		print_AMD_assoc((regs[1] >> 8) & 0xff);

		printf("L1 data cache: %d kbytes", regs[2] >> 24);
		printf(", %d bytes/line", regs[2] & 0xff);
		printf(", %d lines/tag", (regs[2] >> 8) & 0xff);
		print_AMD_assoc((regs[2] >> 16) & 0xff);

		printf("L1 instruction cache: %d kbytes", regs[3] >> 24);
		printf(", %d bytes/line", regs[3] & 0xff);
		printf(", %d lines/tag", (regs[3] >> 8) & 0xff);
		print_AMD_assoc((regs[3] >> 16) & 0xff);
	}

	if (cpu_exthigh >= 0x80000006) {
		do_cpuid(0x80000006, regs);
		if ((regs[0] >> 16) != 0) {
			printf("L2 2MB data TLB: %d entries",
			    (regs[0] >> 16) & 0xfff);
			print_AMD_l2_assoc(regs[0] >> 28);
			printf("L2 2MB instruction TLB: %d entries",
			    regs[0] & 0xfff);
			print_AMD_l2_assoc((regs[0] >> 28) & 0xf);
		} else {
			printf("L2 2MB unified TLB: %d entries",
			    regs[0] & 0xfff);
			print_AMD_l2_assoc((regs[0] >> 28) & 0xf);
		}
		if ((regs[1] >> 16) != 0) {
			printf("L2 4KB data TLB: %d entries",
			    (regs[1] >> 16) & 0xfff);
			print_AMD_l2_assoc(regs[1] >> 28);

			printf("L2 4KB instruction TLB: %d entries",
			    (regs[1] >> 16) & 0xfff);
			print_AMD_l2_assoc((regs[1] >> 28) & 0xf);
		} else {
			printf("L2 4KB unified TLB: %d entries",
			    (regs[1] >> 16) & 0xfff);
			print_AMD_l2_assoc((regs[1] >> 28) & 0xf);
		}
		printf("L2 unified cache: %d kbytes", regs[2] >> 16);
		printf(", %d bytes/line", regs[2] & 0xff);
		printf(", %d lines/tag", (regs[2] >> 8) & 0x0f);
		print_AMD_l2_assoc((regs[2] >> 12) & 0x0f);
	}

#ifdef __i386__
	if (((cpu_id & 0xf00) == 0x500)
	    && (((cpu_id & 0x0f0) > 0x80)
		|| (((cpu_id & 0x0f0) == 0x80)
		    && (cpu_id & 0x00f) > 0x07))) {
		/* K6-2(new core [Stepping 8-F]), K6-III or later */
		amd_whcr = rdmsr(0xc0000082);
		if (!(amd_whcr & (0x3ff << 22))) {
			printf("Write Allocate Disable\n");
		} else {
			printf("Write Allocate Enable Limit: %dM bytes\n",
			    (u_int32_t)((amd_whcr & (0x3ff << 22)) >> 22) * 4);
			printf("Write Allocate 15-16M bytes: %s\n",
			    (amd_whcr & (1 << 16)) ? "Enable" : "Disable");
		}
	} else if (((cpu_id & 0xf00) == 0x500)
		   && ((cpu_id & 0x0f0) > 0x50)) {
		/* K6, K6-2(old core) */
		amd_whcr = rdmsr(0xc0000082);
		if (!(amd_whcr & (0x7f << 1))) {
			printf("Write Allocate Disable\n");
		} else {
			printf("Write Allocate Enable Limit: %dM bytes\n",
			    (u_int32_t)((amd_whcr & (0x7f << 1)) >> 1) * 4);
			printf("Write Allocate 15-16M bytes: %s\n",
			    (amd_whcr & 0x0001) ? "Enable" : "Disable");
			printf("Hardware Write Allocate Control: %s\n",
			    (amd_whcr & 0x0100) ? "Enable" : "Disable");
		}
	}
#endif
	/*
	 * Opteron Rev E shows a bug as in very rare occasions a read memory
	 * barrier is not performed as expected if it is followed by a
	 * non-atomic read-modify-write instruction.
	 * As long as that bug pops up very rarely (intensive machine usage
	 * on other operating systems generally generates one unexplainable
	 * crash any 2 months) and as long as a model specific fix would be
	 * impractical at this stage, print out a warning string if the broken
	 * model and family are identified.
	 */
	if (CPUID_TO_FAMILY(cpu_id) == 0xf && CPUID_TO_MODEL(cpu_id) >= 0x20 &&
	    CPUID_TO_MODEL(cpu_id) <= 0x3f)
		printf("WARNING: This architecture revision has known SMP "
		    "hardware bugs which may cause random instability\n");
}

static void
print_INTEL_info(void)
{
	u_int regs[4];
	u_int rounds, regnum;
	u_int nwaycode, nway;

	if (cpu_high >= 2) {
		rounds = 0;
		do {
			do_cpuid(0x2, regs);
			if (rounds == 0 && (rounds = (regs[0] & 0xff)) == 0)
				break;	/* we have a buggy CPU */

			for (regnum = 0; regnum <= 3; ++regnum) {
				if (regs[regnum] & (1<<31))
					continue;
				if (regnum != 0)
					print_INTEL_TLB(regs[regnum] & 0xff);
				print_INTEL_TLB((regs[regnum] >> 8) & 0xff);
				print_INTEL_TLB((regs[regnum] >> 16) & 0xff);
				print_INTEL_TLB((regs[regnum] >> 24) & 0xff);
			}
		} while (--rounds > 0);
	}

	if (cpu_exthigh >= 0x80000006) {
		do_cpuid(0x80000006, regs);
		nwaycode = (regs[2] >> 12) & 0x0f;
		if (nwaycode >= 0x02 && nwaycode <= 0x08)
			nway = 1 << (nwaycode / 2);
		else
			nway = 0;
		printf("L2 cache: %u kbytes, %u-way associative, %u bytes/line\n",
		    (regs[2] >> 16) & 0xffff, nway, regs[2] & 0xff);
	}
}

static void
print_INTEL_TLB(u_int data)
{
	switch (data) {
	case 0x0:
	case 0x40:
	default:
		break;
	case 0x1:
		printf("Instruction TLB: 4 KB pages, 4-way set associative, 32 entries\n");
		break;
	case 0x2:
		printf("Instruction TLB: 4 MB pages, fully associative, 2 entries\n");
		break;
	case 0x3:
		printf("Data TLB: 4 KB pages, 4-way set associative, 64 entries\n");
		break;
	case 0x4:
		printf("Data TLB: 4 MB Pages, 4-way set associative, 8 entries\n");
		break;
	case 0x6:
		printf("1st-level instruction cache: 8 KB, 4-way set associative, 32 byte line size\n");
		break;
	case 0x8:
		printf("1st-level instruction cache: 16 KB, 4-way set associative, 32 byte line size\n");
		break;
	case 0x9:
		printf("1st-level instruction cache: 32 KB, 4-way set associative, 64 byte line size\n");
		break;
	case 0xa:
		printf("1st-level data cache: 8 KB, 2-way set associative, 32 byte line size\n");
		break;
	case 0xb:
		printf("Instruction TLB: 4 MByte pages, 4-way set associative, 4 entries\n");
		break;
	case 0xc:
		printf("1st-level data cache: 16 KB, 4-way set associative, 32 byte line size\n");
		break;
	case 0xd:
		printf("1st-level data cache: 16 KBytes, 4-way set associative, 64 byte line size");
		break;
	case 0xe:
		printf("1st-level data cache: 24 KBytes, 6-way set associative, 64 byte line size\n");
		break;
	case 0x1d:
		printf("2nd-level cache: 128 KBytes, 2-way set associative, 64 byte line size\n");
		break;
	case 0x21:
		printf("2nd-level cache: 256 KBytes, 8-way set associative, 64 byte line size\n");
		break;
	case 0x22:
		printf("3rd-level cache: 512 KB, 4-way set associative, sectored cache, 64 byte line size\n");
		break;
	case 0x23:
		printf("3rd-level cache: 1 MB, 8-way set associative, sectored cache, 64 byte line size\n");
		break;
	case 0x24:
		printf("2nd-level cache: 1 MBytes, 16-way set associative, 64 byte line size\n");
		break;
	case 0x25:
		printf("3rd-level cache: 2 MB, 8-way set associative, sectored cache, 64 byte line size\n");
		break;
	case 0x29:
		printf("3rd-level cache: 4 MB, 8-way set associative, sectored cache, 64 byte line size\n");
		break;
	case 0x2c:
		printf("1st-level data cache: 32 KB, 8-way set associative, 64 byte line size\n");
		break;
	case 0x30:
		printf("1st-level instruction cache: 32 KB, 8-way set associative, 64 byte line size\n");
		break;
	case 0x39: /* De-listed in SDM rev. 54 */
		printf("2nd-level cache: 128 KB, 4-way set associative, sectored cache, 64 byte line size\n");
		break;
	case 0x3b: /* De-listed in SDM rev. 54 */
		printf("2nd-level cache: 128 KB, 2-way set associative, sectored cache, 64 byte line size\n");
		break;
	case 0x3c: /* De-listed in SDM rev. 54 */
		printf("2nd-level cache: 256 KB, 4-way set associative, sectored cache, 64 byte line size\n");
		break;
	case 0x41:
		printf("2nd-level cache: 128 KB, 4-way set associative, 32 byte line size\n");
		break;
	case 0x42:
		printf("2nd-level cache: 256 KB, 4-way set associative, 32 byte line size\n");
		break;
	case 0x43:
		printf("2nd-level cache: 512 KB, 4-way set associative, 32 byte line size\n");
		break;
	case 0x44:
		printf("2nd-level cache: 1 MB, 4-way set associative, 32 byte line size\n");
		break;
	case 0x45:
		printf("2nd-level cache: 2 MB, 4-way set associative, 32 byte line size\n");
		break;
	case 0x46:
		printf("3rd-level cache: 4 MB, 4-way set associative, 64 byte line size\n");
		break;
	case 0x47:
		printf("3rd-level cache: 8 MB, 8-way set associative, 64 byte line size\n");
		break;
	case 0x48:
		printf("2nd-level cache: 3MByte, 12-way set associative, 64 byte line size\n");
		break;
	case 0x49:
		if (CPUID_TO_FAMILY(cpu_id) == 0xf &&
		    CPUID_TO_MODEL(cpu_id) == 0x6)
			printf("3rd-level cache: 4MB, 16-way set associative, 64-byte line size\n");
		else
			printf("2nd-level cache: 4 MByte, 16-way set associative, 64 byte line size");
		break;
	case 0x4a:
		printf("3rd-level cache: 6MByte, 12-way set associative, 64 byte line size\n");
		break;
	case 0x4b:
		printf("3rd-level cache: 8MByte, 16-way set associative, 64 byte line size\n");
		break;
	case 0x4c:
		printf("3rd-level cache: 12MByte, 12-way set associative, 64 byte line size\n");
		break;
	case 0x4d:
		printf("3rd-level cache: 16MByte, 16-way set associative, 64 byte line size\n");
		break;
	case 0x4e:
		printf("2nd-level cache: 6MByte, 24-way set associative, 64 byte line size\n");
		break;
	case 0x4f:
		printf("Instruction TLB: 4 KByte pages, 32 entries\n");
		break;
	case 0x50:
		printf("Instruction TLB: 4 KB, 2 MB or 4 MB pages, fully associative, 64 entries\n");
		break;
	case 0x51:
		printf("Instruction TLB: 4 KB, 2 MB or 4 MB pages, fully associative, 128 entries\n");
		break;
	case 0x52:
		printf("Instruction TLB: 4 KB, 2 MB or 4 MB pages, fully associative, 256 entries\n");
		break;
	case 0x55:
		printf("Instruction TLB: 2-MByte or 4-MByte pages, fully associative, 7 entries\n");
		break;
	case 0x56:
		printf("Data TLB0: 4 MByte pages, 4-way set associative, 16 entries\n");
		break;
	case 0x57:
		printf("Data TLB0: 4 KByte pages, 4-way associative, 16 entries\n");
		break;
	case 0x59:
		printf("Data TLB0: 4 KByte pages, fully associative, 16 entries\n");
		break;
	case 0x5a:
		printf("Data TLB0: 2-MByte or 4 MByte pages, 4-way set associative, 32 entries\n");
		break;
	case 0x5b:
		printf("Data TLB: 4 KB or 4 MB pages, fully associative, 64 entries\n");
		break;
	case 0x5c:
		printf("Data TLB: 4 KB or 4 MB pages, fully associative, 128 entries\n");
		break;
	case 0x5d:
		printf("Data TLB: 4 KB or 4 MB pages, fully associative, 256 entries\n");
		break;
	case 0x60:
		printf("1st-level data cache: 16 KB, 8-way set associative, sectored cache, 64 byte line size\n");
		break;
	case 0x61:
		printf("Instruction TLB: 4 KByte pages, fully associative, 48 entries\n");
		break;
	case 0x63:
		printf("Data TLB: 2 MByte or 4 MByte pages, 4-way set associative, 32 entries and a separate array with 1 GByte pages, 4-way set associative, 4 entries\n");
		break;
	case 0x64:
		printf("Data TLB: 4 KBytes pages, 4-way set associative, 512 entries\n");
		break;
	case 0x66:
		printf("1st-level data cache: 8 KB, 4-way set associative, sectored cache, 64 byte line size\n");
		break;
	case 0x67:
		printf("1st-level data cache: 16 KB, 4-way set associative, sectored cache, 64 byte line size\n");
		break;
	case 0x68:
		printf("1st-level data cache: 32 KB, 4 way set associative, sectored cache, 64 byte line size\n");
		break;
	case 0x6a:
		printf("uTLB: 4KByte pages, 8-way set associative, 64 entries\n");
		break;
	case 0x6b:
		printf("DTLB: 4KByte pages, 8-way set associative, 256 entries\n");
		break;
	case 0x6c:
		printf("DTLB: 2M/4M pages, 8-way set associative, 128 entries\n");
		break;
	case 0x6d:
		printf("DTLB: 1 GByte pages, fully associative, 16 entries\n");
		break;
	case 0x70:
		printf("Trace cache: 12K-uops, 8-way set associative\n");
		break;
	case 0x71:
		printf("Trace cache: 16K-uops, 8-way set associative\n");
		break;
	case 0x72:
		printf("Trace cache: 32K-uops, 8-way set associative\n");
		break;
	case 0x76:
		printf("Instruction TLB: 2M/4M pages, fully associative, 8 entries\n");
		break;
	case 0x78:
		printf("2nd-level cache: 1 MB, 4-way set associative, 64-byte line size\n");
		break;
	case 0x79:
		printf("2nd-level cache: 128 KB, 8-way set associative, sectored cache, 64 byte line size\n");
		break;
	case 0x7a:
		printf("2nd-level cache: 256 KB, 8-way set associative, sectored cache, 64 byte line size\n");
		break;
	case 0x7b:
		printf("2nd-level cache: 512 KB, 8-way set associative, sectored cache, 64 byte line size\n");
		break;
	case 0x7c:
		printf("2nd-level cache: 1 MB, 8-way set associative, sectored cache, 64 byte line size\n");
		break;
	case 0x7d:
		printf("2nd-level cache: 2-MB, 8-way set associative, 64-byte line size\n");
		break;
	case 0x7f:
		printf("2nd-level cache: 512-KB, 2-way set associative, 64-byte line size\n");
		break;
	case 0x80:
		printf("2nd-level cache: 512 KByte, 8-way set associative, 64-byte line size\n");
		break;
	case 0x82:
		printf("2nd-level cache: 256 KB, 8-way set associative, 32 byte line size\n");
		break;
	case 0x83:
		printf("2nd-level cache: 512 KB, 8-way set associative, 32 byte line size\n");
		break;
	case 0x84:
		printf("2nd-level cache: 1 MB, 8-way set associative, 32 byte line size\n");
		break;
	case 0x85:
		printf("2nd-level cache: 2 MB, 8-way set associative, 32 byte line size\n");
		break;
	case 0x86:
		printf("2nd-level cache: 512 KB, 4-way set associative, 64 byte line size\n");
		break;
	case 0x87:
		printf("2nd-level cache: 1 MB, 8-way set associative, 64 byte line size\n");
		break;
	case 0xa0:
		printf("DTLB: 4k pages, fully associative, 32 entries\n");
		break;
	case 0xb0:
		printf("Instruction TLB: 4 KB Pages, 4-way set associative, 128 entries\n");
		break;
	case 0xb1:
		printf("Instruction TLB: 2M pages, 4-way, 8 entries or 4M pages, 4-way, 4 entries\n");
		break;
	case 0xb2:
		printf("Instruction TLB: 4KByte pages, 4-way set associative, 64 entries\n");
		break;
	case 0xb3:
		printf("Data TLB: 4 KB Pages, 4-way set associative, 128 entries\n");
		break;
	case 0xb4:
		printf("Data TLB1: 4 KByte pages, 4-way associative, 256 entries\n");
		break;
	case 0xb5:
		printf("Instruction TLB: 4KByte pages, 8-way set associative, 64 entries\n");
		break;
	case 0xb6:
		printf("Instruction TLB: 4KByte pages, 8-way set associative, 128 entries\n");
		break;
	case 0xba:
		printf("Data TLB1: 4 KByte pages, 4-way associative, 64 entries\n");
		break;
	case 0xc0:
		printf("Data TLB: 4 KByte and 4 MByte pages, 4-way associative, 8 entries\n");
		break;
	case 0xc1:
		printf("Shared 2nd-Level TLB: 4 KByte/2MByte pages, 8-way associative, 1024 entries\n");
		break;
	case 0xc2:
		printf("DTLB: 4 KByte/2 MByte pages, 4-way associative, 16 entries\n");
		break;
	case 0xc3:
		printf("Shared 2nd-Level TLB: 4 KByte /2 MByte pages, 6-way associative, 1536 entries. Also 1GBbyte pages, 4-way, 16 entries\n");
		break;
	case 0xc4:
		printf("DTLB: 2M/4M Byte pages, 4-way associative, 32 entries\n");
		break;
	case 0xca:
		printf("Shared 2nd-Level TLB: 4 KByte pages, 4-way associative, 512 entries\n");
		break;
	case 0xd0:
		printf("3rd-level cache: 512 KByte, 4-way set associative, 64 byte line size\n");
		break;
	case 0xd1:
		printf("3rd-level cache: 1 MByte, 4-way set associative, 64 byte line size\n");
		break;
	case 0xd2:
		printf("3rd-level cache: 2 MByte, 4-way set associative, 64 byte line size\n");
		break;
	case 0xd6:
		printf("3rd-level cache: 1 MByte, 8-way set associative, 64 byte line size\n");
		break;
	case 0xd7:
		printf("3rd-level cache: 2 MByte, 8-way set associative, 64 byte line size\n");
		break;
	case 0xd8:
		printf("3rd-level cache: 4 MByte, 8-way set associative, 64 byte line size\n");
		break;
	case 0xdc:
		printf("3rd-level cache: 1.5 MByte, 12-way set associative, 64 byte line size\n");
		break;
	case 0xdd:
		printf("3rd-level cache: 3 MByte, 12-way set associative, 64 byte line size\n");
		break;
	case 0xde:
		printf("3rd-level cache: 6 MByte, 12-way set associative, 64 byte line size\n");
		break;
	case 0xe2:
		printf("3rd-level cache: 2 MByte, 16-way set associative, 64 byte line size\n");
		break;
	case 0xe3:
		printf("3rd-level cache: 4 MByte, 16-way set associative, 64 byte line size\n");
		break;
	case 0xe4:
		printf("3rd-level cache: 8 MByte, 16-way set associative, 64 byte line size\n");
		break;
	case 0xea:
		printf("3rd-level cache: 12MByte, 24-way set associative, 64 byte line size\n");
		break;
	case 0xeb:
		printf("3rd-level cache: 18MByte, 24-way set associative, 64 byte line size\n");
		break;
	case 0xec:
		printf("3rd-level cache: 24MByte, 24-way set associative, 64 byte line size\n");
		break;
	case 0xf0:
		printf("64-Byte prefetching\n");
		break;
	case 0xf1:
		printf("128-Byte prefetching\n");
		break;
	}
}

static void
print_svm_info(void)
{
	u_int features, regs[4];
	uint64_t msr;
	int comma;

	printf("\n  SVM: ");
	do_cpuid(0x8000000A, regs);
	features = regs[3];

	msr = rdmsr(MSR_VM_CR);
	if ((msr & VM_CR_SVMDIS) == VM_CR_SVMDIS)
		printf("(disabled in BIOS) ");

	if (!bootverbose) {
		comma = 0;
		if (features & (1 << 0)) {
			printf("%sNP", comma ? "," : "");
                        comma = 1; 
		}
		if (features & (1 << 3)) {
			printf("%sNRIP", comma ? "," : "");
                        comma = 1; 
		}
		if (features & (1 << 5)) {
			printf("%sVClean", comma ? "," : "");
                        comma = 1; 
		}
		if (features & (1 << 6)) {
			printf("%sAFlush", comma ? "," : "");
                        comma = 1; 
		}
		if (features & (1 << 7)) {
			printf("%sDAssist", comma ? "," : "");
                        comma = 1; 
		}
		printf("%sNAsids=%d", comma ? "," : "", regs[1]);
		return;
	}

	printf("Features=0x%b", features,
	       "\020"
	       "\001NP"			/* Nested paging */
	       "\002LbrVirt"		/* LBR virtualization */
	       "\003SVML"		/* SVM lock */
	       "\004NRIPS"		/* NRIP save */
	       "\005TscRateMsr"		/* MSR based TSC rate control */
	       "\006VmcbClean"		/* VMCB clean bits */
	       "\007FlushByAsid"	/* Flush by ASID */
	       "\010DecodeAssist"	/* Decode assist */
	       "\011<b8>"
	       "\012<b9>"
	       "\013PauseFilter"	/* PAUSE intercept filter */    
	       "\014EncryptedMcodePatch"
	       "\015PauseFilterThreshold" /* PAUSE filter threshold */
	       "\016AVIC"		/* virtual interrupt controller */
	       "\017<b14>"
	       "\020V_VMSAVE_VMLOAD"
	       "\021vGIF"
	       "\022<b17>"
	       "\023<b18>"
	       "\024<b19>"
	       "\025<b20>"
	       "\026<b21>"
	       "\027<b22>"
	       "\030<b23>"
	       "\031<b24>"
	       "\032<b25>"
	       "\033<b26>"
	       "\034<b27>"
	       "\035<b28>"
	       "\036<b29>"
	       "\037<b30>"
	       "\040<b31>"
                );
	printf("\nRevision=%d, ASIDs=%d", regs[0] & 0xff, regs[1]);
}

#ifdef __i386__
static void
print_transmeta_info(void)
{
	u_int regs[4], nreg = 0;

	do_cpuid(0x80860000, regs);
	nreg = regs[0];
	if (nreg >= 0x80860001) {
		do_cpuid(0x80860001, regs);
		printf("  Processor revision %u.%u.%u.%u\n",
		       (regs[1] >> 24) & 0xff,
		       (regs[1] >> 16) & 0xff,
		       (regs[1] >> 8) & 0xff,
		       regs[1] & 0xff);
	}
	if (nreg >= 0x80860002) {
		do_cpuid(0x80860002, regs);
		printf("  Code Morphing Software revision %u.%u.%u-%u-%u\n",
		       (regs[1] >> 24) & 0xff,
		       (regs[1] >> 16) & 0xff,
		       (regs[1] >> 8) & 0xff,
		       regs[1] & 0xff,
		       regs[2]);
	}
	if (nreg >= 0x80860006) {
		char info[65];
		do_cpuid(0x80860003, (u_int*) &info[0]);
		do_cpuid(0x80860004, (u_int*) &info[16]);
		do_cpuid(0x80860005, (u_int*) &info[32]);
		do_cpuid(0x80860006, (u_int*) &info[48]);
		info[64] = 0;
		printf("  %s\n", info);
	}
}
#endif

static void
print_via_padlock_info(void)
{
	u_int regs[4];

	do_cpuid(0xc0000001, regs);
	printf("\n  VIA Padlock Features=0x%b", regs[3],
	"\020"
	"\003RNG"		/* RNG */
	"\007AES"		/* ACE */
	"\011AES-CTR"		/* ACE2 */
	"\013SHA1,SHA256"	/* PHE */
	"\015RSA"		/* PMM */
	);
}

static uint32_t
vmx_settable(uint64_t basic, int msr, int true_msr)
{
	uint64_t val;

	if (basic & (1ULL << 55))
		val = rdmsr(true_msr);
	else
		val = rdmsr(msr);

	/* Just report the controls that can be set to 1. */
	return (val >> 32);
}

static void
print_vmx_info(void)
{
	uint64_t basic, msr;
	uint32_t entry, exit, mask, pin, proc, proc2;
	int comma;

	printf("\n  VT-x: ");
	msr = rdmsr(MSR_IA32_FEATURE_CONTROL);
	if (!(msr & IA32_FEATURE_CONTROL_VMX_EN))
		printf("(disabled in BIOS) ");
	basic = rdmsr(MSR_VMX_BASIC);
	pin = vmx_settable(basic, MSR_VMX_PINBASED_CTLS,
	    MSR_VMX_TRUE_PINBASED_CTLS);
	proc = vmx_settable(basic, MSR_VMX_PROCBASED_CTLS,
	    MSR_VMX_TRUE_PROCBASED_CTLS);
	if (proc & PROCBASED_SECONDARY_CONTROLS)
		proc2 = vmx_settable(basic, MSR_VMX_PROCBASED_CTLS2,
		    MSR_VMX_PROCBASED_CTLS2);
	else
		proc2 = 0;
	exit = vmx_settable(basic, MSR_VMX_EXIT_CTLS, MSR_VMX_TRUE_EXIT_CTLS);
	entry = vmx_settable(basic, MSR_VMX_ENTRY_CTLS, MSR_VMX_TRUE_ENTRY_CTLS);

	if (!bootverbose) {
		comma = 0;
		if (exit & VM_EXIT_SAVE_PAT && exit & VM_EXIT_LOAD_PAT &&
		    entry & VM_ENTRY_LOAD_PAT) {
			printf("%sPAT", comma ? "," : "");
			comma = 1;
		}
		if (proc & PROCBASED_HLT_EXITING) {
			printf("%sHLT", comma ? "," : "");
			comma = 1;
		}
		if (proc & PROCBASED_MTF) {
			printf("%sMTF", comma ? "," : "");
			comma = 1;
		}
		if (proc & PROCBASED_PAUSE_EXITING) {
			printf("%sPAUSE", comma ? "," : "");
			comma = 1;
		}
		if (proc2 & PROCBASED2_ENABLE_EPT) {
			printf("%sEPT", comma ? "," : "");
			comma = 1;
		}
		if (proc2 & PROCBASED2_UNRESTRICTED_GUEST) {
			printf("%sUG", comma ? "," : "");
			comma = 1;
		}
		if (proc2 & PROCBASED2_ENABLE_VPID) {
			printf("%sVPID", comma ? "," : "");
			comma = 1;
		}
		if (proc & PROCBASED_USE_TPR_SHADOW &&
		    proc2 & PROCBASED2_VIRTUALIZE_APIC_ACCESSES &&
		    proc2 & PROCBASED2_VIRTUALIZE_X2APIC_MODE &&
		    proc2 & PROCBASED2_APIC_REGISTER_VIRTUALIZATION &&
		    proc2 & PROCBASED2_VIRTUAL_INTERRUPT_DELIVERY) {
			printf("%sVID", comma ? "," : "");
			comma = 1;
			if (pin & PINBASED_POSTED_INTERRUPT)
				printf(",PostIntr");
		}
		return;
	}

	mask = basic >> 32;
	printf("Basic Features=0x%b", mask,
	"\020"
	"\02132PA"		/* 32-bit physical addresses */
	"\022SMM"		/* SMM dual-monitor */
	"\027INS/OUTS"		/* VM-exit info for INS and OUTS */
	"\030TRUE"		/* TRUE_CTLS MSRs */
	);
	printf("\n        Pin-Based Controls=0x%b", pin,
	"\020"
	"\001ExtINT"		/* External-interrupt exiting */
	"\004NMI"		/* NMI exiting */
	"\006VNMI"		/* Virtual NMIs */
	"\007PreTmr"		/* Activate VMX-preemption timer */
	"\010PostIntr"		/* Process posted interrupts */
	);
	printf("\n        Primary Processor Controls=0x%b", proc,
	"\020"
	"\003INTWIN"		/* Interrupt-window exiting */
	"\004TSCOff"		/* Use TSC offsetting */
	"\010HLT"		/* HLT exiting */
	"\012INVLPG"		/* INVLPG exiting */
	"\013MWAIT"		/* MWAIT exiting */
	"\014RDPMC"		/* RDPMC exiting */
	"\015RDTSC"		/* RDTSC exiting */
	"\020CR3-LD"		/* CR3-load exiting */
	"\021CR3-ST"		/* CR3-store exiting */
	"\024CR8-LD"		/* CR8-load exiting */
	"\025CR8-ST"		/* CR8-store exiting */
	"\026TPR"		/* Use TPR shadow */
	"\027NMIWIN"		/* NMI-window exiting */
	"\030MOV-DR"		/* MOV-DR exiting */
	"\031IO"		/* Unconditional I/O exiting */
	"\032IOmap"		/* Use I/O bitmaps */
	"\034MTF"		/* Monitor trap flag */
	"\035MSRmap"		/* Use MSR bitmaps */
	"\036MONITOR"		/* MONITOR exiting */
	"\037PAUSE"		/* PAUSE exiting */
	);
	if (proc & PROCBASED_SECONDARY_CONTROLS)
		printf("\n        Secondary Processor Controls=0x%b", proc2,
		"\020"
		"\001APIC"		/* Virtualize APIC accesses */
		"\002EPT"		/* Enable EPT */
		"\003DT"		/* Descriptor-table exiting */
		"\004RDTSCP"		/* Enable RDTSCP */
		"\005x2APIC"		/* Virtualize x2APIC mode */
		"\006VPID"		/* Enable VPID */
		"\007WBINVD"		/* WBINVD exiting */
		"\010UG"		/* Unrestricted guest */
		"\011APIC-reg"		/* APIC-register virtualization */
		"\012VID"		/* Virtual-interrupt delivery */
		"\013PAUSE-loop"	/* PAUSE-loop exiting */
		"\014RDRAND"		/* RDRAND exiting */
		"\015INVPCID"		/* Enable INVPCID */
		"\016VMFUNC"		/* Enable VM functions */
		"\017VMCS"		/* VMCS shadowing */
		"\020EPT#VE"		/* EPT-violation #VE */
		"\021XSAVES"		/* Enable XSAVES/XRSTORS */
		);
	printf("\n        Exit Controls=0x%b", mask,
	"\020"
	"\003DR"		/* Save debug controls */
				/* Ignore Host address-space size */
	"\015PERF"		/* Load MSR_PERF_GLOBAL_CTRL */
	"\020AckInt"		/* Acknowledge interrupt on exit */
	"\023PAT-SV"		/* Save MSR_PAT */
	"\024PAT-LD"		/* Load MSR_PAT */
	"\025EFER-SV"		/* Save MSR_EFER */
	"\026EFER-LD"		/* Load MSR_EFER */
	"\027PTMR-SV"		/* Save VMX-preemption timer value */
	);
	printf("\n        Entry Controls=0x%b", mask,
	"\020"
	"\003DR"		/* Save debug controls */
				/* Ignore IA-32e mode guest */
				/* Ignore Entry to SMM */
				/* Ignore Deactivate dual-monitor treatment */
	"\016PERF"		/* Load MSR_PERF_GLOBAL_CTRL */
	"\017PAT"		/* Load MSR_PAT */
	"\020EFER"		/* Load MSR_EFER */
	);
	if (proc & PROCBASED_SECONDARY_CONTROLS &&
	    (proc2 & (PROCBASED2_ENABLE_EPT | PROCBASED2_ENABLE_VPID)) != 0) {
		msr = rdmsr(MSR_VMX_EPT_VPID_CAP);
		mask = msr;
		printf("\n        EPT Features=0x%b", mask,
		"\020"
		"\001XO"		/* Execute-only translations */
		"\007PW4"		/* Page-walk length of 4 */
		"\011UC"		/* EPT paging-structure mem can be UC */
		"\017WB"		/* EPT paging-structure mem can be WB */
		"\0212M"		/* EPT PDE can map a 2-Mbyte page */
		"\0221G"		/* EPT PDPTE can map a 1-Gbyte page */
		"\025INVEPT"		/* INVEPT is supported */
		"\026AD"		/* Accessed and dirty flags for EPT */
		"\032single"		/* INVEPT single-context type */
		"\033all"		/* INVEPT all-context type */
		);
		mask = msr >> 32;
		printf("\n        VPID Features=0x%b", mask,
		"\020"
		"\001INVVPID"		/* INVVPID is supported */
		"\011individual"	/* INVVPID individual-address type */
		"\012single"		/* INVVPID single-context type */
		"\013all"		/* INVVPID all-context type */
		 /* INVVPID single-context-retaining-globals type */
		"\014single-globals"
		);
	}
}

static void
print_hypervisor_info(void)
{

	if (*hv_vendor)
		printf("Hypervisor: Origin = \"%s\"\n", hv_vendor);
}

/*
 * Returns the maximum physical address that can be used with the
 * current system.
 */
vm_paddr_t
cpu_getmaxphyaddr(void)
{

#if defined(__i386__)
	if (!pae_mode)
		return (0xffffffff);
#endif
	return ((1ULL << cpu_maxphyaddr) - 1);
}
