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
#include <machine/md_var.h>

char machine[] = "arm";

SYSCTL_STRING(_hw, HW_MACHINE, machine, CTLFLAG_RD,
	machine, 0, "Machine class");

static char cpu_model[64];
SYSCTL_STRING(_hw, HW_MODEL, model, CTLFLAG_RD,
    cpu_model, sizeof(cpu_model), "Machine model");

static char hw_buf[81];
static int hw_buf_idx;
static bool hw_buf_newline;

enum cpu_class cpu_class = CPU_CLASS_NONE;

static struct {
	int	implementer;
	int	part_number;
	char 	*impl_name;
	char 	*core_name;
	enum	cpu_class cpu_class;
} cpu_names[] =  {
	{CPU_IMPLEMENTER_ARM, CPU_ARCH_ARM1176,    "ARM", "ARM1176",
	    CPU_CLASS_ARM11J},
	{CPU_IMPLEMENTER_ARM, CPU_ARCH_CORTEX_A5 , "ARM", "Cortex-A5",
	    CPU_CLASS_CORTEXA},
	{CPU_IMPLEMENTER_ARM, CPU_ARCH_CORTEX_A7 , "ARM", "Cortex-A7",
	    CPU_CLASS_CORTEXA},
	{CPU_IMPLEMENTER_ARM, CPU_ARCH_CORTEX_A8 , "ARM", "Cortex-A8",
	    CPU_CLASS_CORTEXA},
	{CPU_IMPLEMENTER_ARM, CPU_ARCH_CORTEX_A9 , "ARM", "Cortex-A9",
	    CPU_CLASS_CORTEXA},
	{CPU_IMPLEMENTER_ARM, CPU_ARCH_CORTEX_A12, "ARM", "Cortex-A12",
	    CPU_CLASS_CORTEXA},
	{CPU_IMPLEMENTER_ARM, CPU_ARCH_CORTEX_A15, "ARM", "Cortex-A15",
	    CPU_CLASS_CORTEXA},
	{CPU_IMPLEMENTER_ARM, CPU_ARCH_CORTEX_A17, "ARM", "Cortex-A17",
	    CPU_CLASS_CORTEXA},
	{CPU_IMPLEMENTER_ARM, CPU_ARCH_CORTEX_A53, "ARM", "Cortex-A53",
	    CPU_CLASS_CORTEXA},
	{CPU_IMPLEMENTER_ARM, CPU_ARCH_CORTEX_A57, "ARM", "Cortex-A57",
	    CPU_CLASS_CORTEXA},
	{CPU_IMPLEMENTER_ARM, CPU_ARCH_CORTEX_A72, "ARM", "Cortex-A72",
	    CPU_CLASS_CORTEXA},
	{CPU_IMPLEMENTER_ARM, CPU_ARCH_CORTEX_A73, "ARM", "Cortex-A73",
	    CPU_CLASS_CORTEXA},

	{CPU_IMPLEMENTER_MRVL, CPU_ARCH_SHEEVA_581, "Marvell", "PJ4 v7",
	    CPU_CLASS_MARVELL},
	{CPU_IMPLEMENTER_MRVL, CPU_ARCH_SHEEVA_584, "Marvell", "PJ4MP v7",
	    CPU_CLASS_MARVELL},

	{CPU_IMPLEMENTER_QCOM, CPU_ARCH_KRAIT_300, "Qualcomm", "Krait 300",
	    CPU_CLASS_KRAIT},
};


static void
print_v5_cache(void)
{
	uint32_t isize, dsize;
	uint32_t multiplier;
	int pcache_type;
	int pcache_unified;
	int picache_size;
	int picache_line_size;
	int picache_ways;
	int pdcache_size;
	int pdcache_line_size;
	int pdcache_ways;

	pcache_unified = 0;
	picache_size = 0 ;
	picache_line_size = 0 ;
	picache_ways = 0 ;
	pdcache_size = 0;
	pdcache_line_size = 0;
	pdcache_ways = 0;

	if ((cpuinfo.ctr & CPU_CT_S) == 0)
		pcache_unified = 1;

	/*
	 * If you want to know how this code works, go read the ARM ARM.
	 */
	pcache_type = CPU_CT_CTYPE(cpuinfo.ctr);

	if (pcache_unified == 0) {
		isize = CPU_CT_ISIZE(cpuinfo.ctr);
		multiplier = (isize & CPU_CT_xSIZE_M) ? 3 : 2;
		picache_line_size = 1U << (CPU_CT_xSIZE_LEN(isize) + 3);
		if (CPU_CT_xSIZE_ASSOC(isize) == 0) {
			if (isize & CPU_CT_xSIZE_M)
				picache_line_size = 0; /* not present */
			else
				picache_ways = 1;
		} else {
			picache_ways = multiplier <<
			    (CPU_CT_xSIZE_ASSOC(isize) - 1);
		}
		picache_size = multiplier << (CPU_CT_xSIZE_SIZE(isize) + 8);
	}

	dsize = CPU_CT_DSIZE(cpuinfo.ctr);
	multiplier = (dsize & CPU_CT_xSIZE_M) ? 3 : 2;
	pdcache_line_size = 1U << (CPU_CT_xSIZE_LEN(dsize) + 3);
	if (CPU_CT_xSIZE_ASSOC(dsize) == 0) {
		if (dsize & CPU_CT_xSIZE_M)
			pdcache_line_size = 0; /* not present */
		else
			pdcache_ways = 1;
	} else {
		pdcache_ways = multiplier <<
		    (CPU_CT_xSIZE_ASSOC(dsize) - 1);
		}
	pdcache_size = multiplier << (CPU_CT_xSIZE_SIZE(dsize) + 8);


	/* Print cache info. */
	if (picache_line_size == 0 && pdcache_line_size == 0)
		return;

	if (pcache_unified) {
		printf("  %dKB/%dB %d-way %s unified cache\n",
		    pdcache_size / 1024,
		    pdcache_line_size, pdcache_ways,
		    pcache_type == 0 ? "WT" : "WB");
	} else {
		printf("  %dKB/%dB %d-way instruction cache\n",
		    picache_size / 1024,
		    picache_line_size, picache_ways);
		printf("  %dKB/%dB %d-way %s data cache\n",
		    pdcache_size / 1024,
		    pdcache_line_size, pdcache_ways,
		    pcache_type == 0 ? "WT" : "WB");
	}
}

static void
print_v7_cache(void )
{
	uint32_t type, val, size, sets, ways, linesize;
	int i;

	printf("LoUU:%d LoC:%d LoUIS:%d \n",
	    CPU_CLIDR_LOUU(cpuinfo.clidr) + 1,
	    CPU_CLIDR_LOC(cpuinfo.clidr) + 1,
	    CPU_CLIDR_LOUIS(cpuinfo.clidr) + 1);

	for (i = 0; i < 7; i++) {
		type = CPU_CLIDR_CTYPE(cpuinfo.clidr, i);
		if (type == 0)
			break;
		printf("Cache level %d:\n", i + 1);
		if (type == CACHE_DCACHE || type == CACHE_UNI_CACHE ||
		    type == CACHE_SEP_CACHE) {
			cp15_csselr_set(i << 1);
			val = cp15_ccsidr_get();
			ways = CPUV7_CT_xSIZE_ASSOC(val) + 1;
			sets = CPUV7_CT_xSIZE_SET(val) + 1;
			linesize = 1 << (CPUV7_CT_xSIZE_LEN(val) + 4);
			size = (ways * sets * linesize) / 1024;

			if (type == CACHE_UNI_CACHE)
				printf(" %dKB/%dB %d-way unified cache",
				    size, linesize,ways);
			else
				printf(" %dKB/%dB %d-way data cache",
				    size, linesize, ways);
			if (val & CPUV7_CT_CTYPE_WT)
				printf(" WT");
			if (val & CPUV7_CT_CTYPE_WB)
				printf(" WB");
				if (val & CPUV7_CT_CTYPE_RA)
				printf(" Read-Alloc");
			if (val & CPUV7_CT_CTYPE_WA)
				printf(" Write-Alloc");
			printf("\n");
		}

		if (type == CACHE_ICACHE || type == CACHE_SEP_CACHE) {
			cp15_csselr_set(i << 1 | 1);
			val = cp15_ccsidr_get();
			ways = CPUV7_CT_xSIZE_ASSOC(val) + 1;
			sets = CPUV7_CT_xSIZE_SET(val) + 1;
			linesize = 1 << (CPUV7_CT_xSIZE_LEN(val) + 4);
			size = (ways * sets * linesize) / 1024;
				printf(" %dKB/%dB %d-way instruction cache",
			    size, linesize, ways);
			if (val & CPUV7_CT_CTYPE_WT)
				printf(" WT");
			if (val & CPUV7_CT_CTYPE_WB)
				printf(" WB");
			if (val & CPUV7_CT_CTYPE_RA)
				printf(" Read-Alloc");
			if (val & CPUV7_CT_CTYPE_WA)
				printf(" Write-Alloc");
			printf("\n");
		}
	}
	cp15_csselr_set(0);
}

static void
add_cap(char *cap)
{
	int len;

	len = strlen(cap);

	if ((hw_buf_idx + len + 2) >= 79) {
		printf("%s,\n", hw_buf);
		hw_buf_idx  = 0;
		hw_buf_newline = true;
	}
	if (hw_buf_newline)
		hw_buf_idx += sprintf(hw_buf + hw_buf_idx, "  ");
	else
		hw_buf_idx += sprintf(hw_buf + hw_buf_idx, ", ");
	hw_buf_newline = false;


	hw_buf_idx += sprintf(hw_buf + hw_buf_idx, "%s", cap);
}

void
identify_arm_cpu(void)
{
	int i;
	u_int val;

	/*
	 * CPU
	 */
	for(i = 0; i < nitems(cpu_names); i++) {
		if (cpu_names[i].implementer == cpuinfo.implementer &&
		    cpu_names[i].part_number == cpuinfo.part_number) {
			cpu_class = cpu_names[i].cpu_class;
			snprintf(cpu_model, sizeof(cpu_model),
			    "%s %s r%dp%d (ECO: 0x%08X)",
			    cpu_names[i].impl_name, cpu_names[i].core_name,
			    cpuinfo.revision, cpuinfo.patch,
			    cpuinfo.midr != cpuinfo.revidr ?
			    cpuinfo.revidr : 0);
			printf("CPU: %s\n", cpu_model);
			break;
		}

	}
	if (i >= nitems(cpu_names))
		printf("unknown CPU (ID = 0x%x)\n", cpuinfo.midr);

	printf("CPU Features: \n");
	hw_buf_idx = 0;
	hw_buf_newline = true;

	val = (cpuinfo.mpidr >> 4)& 0xF;
	if (cpuinfo.mpidr & (1 << 31U))
		add_cap("Multiprocessing");
	val = (cpuinfo.id_pfr0 >> 4)& 0xF;
	if (val == 1)
		add_cap("Thumb");
	else if (val == 3)
		add_cap("Thumb2");

	val = (cpuinfo.id_pfr1 >> 4)& 0xF;
	if (val == 1 || val == 2)
		add_cap("Security");

	val = (cpuinfo.id_pfr1 >> 12)& 0xF;
	if (val == 1)
		add_cap("Virtualization");

	val = (cpuinfo.id_pfr1 >> 16)& 0xF;
	if (val == 1)
		add_cap("Generic Timer");

	val = (cpuinfo.id_mmfr0 >> 0)& 0xF;
	if (val == 2) {
		add_cap("VMSAv6");
	} else if (val >= 3) {
		add_cap("VMSAv7");
		if (val >= 4)
			add_cap("PXN");
		if (val >= 5)
			add_cap("LPAE");
	}

	val = (cpuinfo.id_mmfr3 >> 20)& 0xF;
	if (val == 1)
		add_cap("Coherent Walk");

	if (hw_buf_idx != 0)
		printf("%s\n", hw_buf);

	printf("Optional instructions: \n");
	hw_buf_idx = 0;
	hw_buf_newline = true;
	val = (cpuinfo.id_isar0 >> 24)& 0xF;
	if (val == 1)
		add_cap("SDIV/UDIV (Thumb)");
	else if (val == 2)
		add_cap("SDIV/UDIV");

	val = (cpuinfo.id_isar2 >> 20)& 0xF;
	if (val == 1 || val == 2)
		add_cap("UMULL");

	val = (cpuinfo.id_isar2 >> 16)& 0xF;
	if (val == 1 || val == 2 || val == 3)
		add_cap("SMULL");

	val = (cpuinfo.id_isar2 >> 12)& 0xF;
	if (val == 1)
		add_cap("MLA");

	val = (cpuinfo.id_isar3 >> 4)& 0xF;
	if (val == 1)
		add_cap("SIMD");
	else if (val == 3)
		add_cap("SIMD(ext)");
	if (hw_buf_idx != 0)
		printf("%s\n", hw_buf);

	/*
	 * Cache
	 */
	if (CPU_CT_FORMAT(cpuinfo.ctr) == CPU_CT_ARMV7)
		print_v7_cache();
	else
		print_v5_cache();
}
