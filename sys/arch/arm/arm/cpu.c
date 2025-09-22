/*	$OpenBSD: cpu.c,v 1.60 2024/06/11 15:44:55 kettenis Exp $	*/
/*	$NetBSD: cpu.c,v 1.56 2004/04/14 04:01:49 bsh Exp $	*/


/*
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/sched.h>
#include <sys/sysctl.h>
#include <sys/task.h>

#include <machine/cpu.h>
#include <machine/fdt.h>

#include <arm/cpufunc.h>
#include <arm/vfp.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_regulator.h>
#include <dev/ofw/ofw_thermal.h>
#include <dev/ofw/fdt.h>

#include "psci.h"
#if NPSCI > 0
#include <dev/fdt/pscivar.h>
#endif

/* CPU Identification */
#define CPU_IMPL_ARM		0x41
#define CPU_IMPL_AMCC		0x50

#define CPU_PART_CORTEX_A5	0xc05
#define CPU_PART_CORTEX_A7	0xc07
#define CPU_PART_CORTEX_A8	0xc08
#define CPU_PART_CORTEX_A9	0xc09
#define CPU_PART_CORTEX_A12	0xc0d
#define CPU_PART_CORTEX_A15	0xc0f
#define CPU_PART_CORTEX_A17	0xc0e
#define CPU_PART_CORTEX_A32	0xd01	
#define CPU_PART_CORTEX_A53	0xd03
#define CPU_PART_CORTEX_A35	0xd04
#define CPU_PART_CORTEX_A55	0xd05
#define CPU_PART_CORTEX_A57	0xd07
#define CPU_PART_CORTEX_A72	0xd08
#define CPU_PART_CORTEX_A73	0xd09
#define CPU_PART_CORTEX_A75	0xd0a

#define CPU_PART_X_GENE		0x000

#define CPU_IMPL(midr)  (((midr) >> 24) & 0xff)
#define CPU_PART(midr)  (((midr) >> 4) & 0xfff)
#define CPU_VAR(midr)   (((midr) >> 20) & 0xf)
#define CPU_REV(midr)   (((midr) >> 0) & 0xf)

struct cpu_cores {
	int	id;
	char	*name;
};

struct cpu_cores cpu_cores_none[] = {
	{ 0, NULL },
};

struct cpu_cores cpu_cores_arm[] = {
	{ CPU_PART_CORTEX_A5, "Cortex-A5" },
	{ CPU_PART_CORTEX_A7, "Cortex-A7" },
	{ CPU_PART_CORTEX_A8, "Cortex-A8" },
	{ CPU_PART_CORTEX_A9, "Cortex-A9" },
	{ CPU_PART_CORTEX_A12, "Cortex-A12" },
	{ CPU_PART_CORTEX_A15, "Cortex-A15" },
	{ CPU_PART_CORTEX_A17, "Cortex-A17" },
	{ CPU_PART_CORTEX_A32, "Cortex-A32" },
	{ CPU_PART_CORTEX_A35, "Cortex-A35" },
	{ CPU_PART_CORTEX_A53, "Cortex-A53" },
	{ CPU_PART_CORTEX_A55, "Cortex-A55" },
	{ CPU_PART_CORTEX_A57, "Cortex-A57" },
	{ CPU_PART_CORTEX_A72, "Cortex-A72" },
	{ CPU_PART_CORTEX_A73, "Cortex-A73" },
	{ CPU_PART_CORTEX_A75, "Cortex-A75" },
	{ 0, NULL },
};

struct cpu_cores cpu_cores_amcc[] = {
	{ CPU_PART_X_GENE, "X-Gene" },
	{ 0, NULL },
};

/* arm cores makers */
const struct implementers {
	int			id;
	char			*name;
	struct cpu_cores	*corelist;
} cpu_implementers[] = {
	{ CPU_IMPL_ARM,	"ARM", cpu_cores_arm },
	{ CPU_IMPL_AMCC, "Applied Micro", cpu_cores_amcc },
	{ 0, NULL },
};

char cpu_model[64];
int cpu_node;

int	cpu_match(struct device *, void *, void *);
void	cpu_attach(struct device *, struct device *, void *);

const struct cfattach cpu_ca = {
	sizeof(struct device), cpu_match, cpu_attach
};

struct cfdriver cpu_cd = {
	NULL, "cpu", DV_DULL
};

void	cpu_opp_init_legacy(struct cpu_info *);
void	cpu_opp_init(struct cpu_info *, uint32_t);

void	cpu_flush_bp_noop(void);

void
cpu_identify(struct cpu_info *ci)
{
	uint32_t midr, impl, part;
	uint32_t clidr;
	uint32_t ctr, ccsidr, sets, ways, line;
	const char *impl_name = NULL;
	const char *part_name = NULL;
	const char *il1p_name = NULL;
	const char *sep;
	struct cpu_cores *coreselecter = cpu_cores_none;
	int i;

	__asm volatile("mrc p15, 0, %0, c0, c0, 0" : "=r"(midr));
	impl = CPU_IMPL(midr);
	part = CPU_PART(midr);

	for (i = 0; cpu_implementers[i].name; i++) {
		if (impl == cpu_implementers[i].id) {
			impl_name = cpu_implementers[i].name;
			coreselecter = cpu_implementers[i].corelist;
			break;
		}
	}

	for (i = 0; coreselecter[i].name; i++) {
		if (part == coreselecter[i].id) {
			part_name = coreselecter[i].name;
			break;
		}
	}

	if (impl_name && part_name) {
		printf(" %s %s r%up%u", impl_name, part_name, CPU_VAR(midr),
		    CPU_REV(midr));

		if (CPU_IS_PRIMARY(ci))
			snprintf(cpu_model, sizeof(cpu_model),
			    "%s %s r%up%u", impl_name, part_name,
			    CPU_VAR(midr), CPU_REV(midr));
	} else {
		printf(" Unknown, MIDR 0x%x", midr);

		if (CPU_IS_PRIMARY(ci))
			snprintf(cpu_model, sizeof(cpu_model), "Unknown");
	}

	/* Print cache information. */

	__asm volatile("mrc p15, 0, %0, c0, c0, 1" : "=r"(ctr));
	switch (ctr & CTR_IL1P_MASK) {
	case CTR_IL1P_AIVIVT:
		il1p_name = "AIVIVT ";
		break;
	case CTR_IL1P_VIPT:
		il1p_name = "VIPT ";
		break;
	case CTR_IL1P_PIPT:
		il1p_name = "PIPT ";
		break;
	}

	__asm volatile("mrc p15, 1, %0, c0, c0, 1" : "=r"(clidr));
	for (i = 0; i < 7; i++) {
		if ((clidr & CLIDR_CTYPE_MASK) == 0)
			break;
		printf("\n%s:", ci->ci_dev->dv_xname);
		sep = "";
		if (clidr & CLIDR_CTYPE_INSN) {
			__asm volatile("mcr p15, 2, %0, c0, c0, 0" ::
			    "r"(i << CSSELR_LEVEL_SHIFT | CSSELR_IND));
			__asm volatile("mrc p15, 1, %0, c0, c0, 0" : "=r"(ccsidr));
			sets = CCSIDR_SETS(ccsidr);
			ways = CCSIDR_WAYS(ccsidr);
			line = CCSIDR_LINE_SIZE(ccsidr);
			printf("%s %dKB %db/line %d-way L%d %sI-cache", sep,
			    (sets * ways * line) / 1024, line, ways, (i + 1),
			    il1p_name);
			il1p_name = "";
			sep = ",";
		}
		if (clidr & CLIDR_CTYPE_DATA) {
			__asm volatile("mcr p15, 2, %0, c0, c0, 0" ::
			    "r"(i << CSSELR_LEVEL_SHIFT));
			__asm volatile("mrc p15, 1, %0, c0, c0, 0" : "=r"(ccsidr));
			sets = CCSIDR_SETS(ccsidr);
			ways = CCSIDR_WAYS(ccsidr);
			line = CCSIDR_LINE_SIZE(ccsidr);
			printf("%s %dKB %db/line %d-way L%d D-cache", sep,
			    (sets * ways * line) / 1024, line, ways, (i + 1));
			sep = ",";
		}
		if (clidr & CLIDR_CTYPE_UNIFIED) {
			__asm volatile("mcr p15, 2, %0, c0, c0, 0" ::
			    "r"(i << CSSELR_LEVEL_SHIFT));
			__asm volatile("mrc p15, 1, %0, c0, c0, 0" : "=r"(ccsidr));
			sets = CCSIDR_SETS(ccsidr);
			ways = CCSIDR_WAYS(ccsidr);
			line = CCSIDR_LINE_SIZE(ccsidr);
			printf("%s %dKB %db/line %d-way L%d cache", sep,
			    (sets * ways * line) / 1024, line, ways, (i + 1));
		}
		clidr >>= 3;
	}

	/*
	 * Some ARM processors are vulnerable to branch target
	 * injection attacks (CVE-2017-5715).
	 */
	switch (impl) {
	case CPU_IMPL_ARM:
		switch (part) {
		case CPU_PART_CORTEX_A5:
		case CPU_PART_CORTEX_A7:
		case CPU_PART_CORTEX_A32:
		case CPU_PART_CORTEX_A35:
		case CPU_PART_CORTEX_A53:
		case CPU_PART_CORTEX_A55:
			/* Not vulnerable. */
			ci->ci_flush_bp = cpu_flush_bp_noop;
			break;
		case CPU_PART_CORTEX_A8:
		case CPU_PART_CORTEX_A9:
		case CPU_PART_CORTEX_A12:
		case CPU_PART_CORTEX_A17:
		case CPU_PART_CORTEX_A73:
		case CPU_PART_CORTEX_A75:
		default:
			/* Vulnerable; flush BP cache. */
			ci->ci_flush_bp = armv7_flush_bp;
			break;
		case CPU_PART_CORTEX_A15:
		case CPU_PART_CORTEX_A72:
			/*
			 * Vulnerable; BPIALL is "not effective" so
			 * must use ICIALLU and hope the firmware set
			 * the magic bit in the ACTLR that actually
			 * forces a BTB flush.
			 */
			ci->ci_flush_bp = cortex_a15_flush_bp;
			break;
		case CPU_PART_CORTEX_A57:
			/*
			 * Vulnerable; must disable and enable the MMU
			 * which can be done by a PSCI call on
			 * firmware with the appropriate fixes.  Punt
			 * for now.
			 */
			ci->ci_flush_bp = cpu_flush_bp_noop;
		}
		break;
	default:
		/* Not much we can do for an unknown processor.  */
		ci->ci_flush_bp = cpu_flush_bp_noop;
		break;
	}
}

int	cpu_hatch_secondary(struct cpu_info *ci, int, uint64_t);
int	cpu_clockspeed(int *);

int
cpu_match(struct device *parent, void *cfdata, void *aux)
{
	struct fdt_attach_args *faa = aux;
	uint32_t mpidr;
	char buf[32];

	__asm volatile("mrc p15, 0, %0, c0, c0, 5" : "=r"(mpidr));

	if (OF_getprop(faa->fa_node, "device_type", buf, sizeof(buf)) <= 0 ||
	    strcmp(buf, "cpu") != 0)
		return 0;

	if (ncpus < MAXCPUS || faa->fa_reg[0].addr == (mpidr & MPIDR_AFF))
		return 1;

	return 0;
}

void
cpu_attach(struct device *parent, struct device *dev, void *aux)
{
	struct fdt_attach_args *faa = aux;
	struct cpu_info *ci;
	uint32_t mpidr;
	uint32_t opp;

	__asm volatile("mrc p15, 0, %0, c0, c0, 5" : "=r"(mpidr));
	KASSERT(faa->fa_nreg > 0);

#ifdef MULTIPROCESSOR
	if (faa->fa_reg[0].addr == (mpidr & MPIDR_AFF)) {
		ci = &cpu_info_primary;
		ci->ci_flags |= CPUF_RUNNING | CPUF_PRESENT | CPUF_PRIMARY;
	} else {
		ci = malloc(sizeof(*ci), M_DEVBUF, M_WAITOK | M_ZERO);
		cpu_info[dev->dv_unit] = ci;
		ci->ci_next = cpu_info_list->ci_next;
		cpu_info_list->ci_next = ci;
		ci->ci_flags |= CPUF_AP;
		ncpus++;
	}
#else
	ci = &cpu_info_primary;
#endif

	ci->ci_dev = dev;
	ci->ci_cpuid = dev->dv_unit;
	ci->ci_mpidr = faa->fa_reg[0].addr;
	ci->ci_node = faa->fa_node;
	ci->ci_self = ci;

	printf(" mpidr %llx:", ci->ci_mpidr);

#ifdef MULTIPROCESSOR
	if (ci->ci_flags & CPUF_AP) {
		char buf[32];
		uint64_t spinup_data = 0;
		int spinup_method = 0;
		int timeout = 10000;
		int len;

		len = OF_getprop(ci->ci_node, "enable-method",
		    buf, sizeof(buf));
		if (strcmp(buf, "psci") == 0) {
			spinup_method = 1;
		} else if (strcmp(buf, "spin-table") == 0) {
			spinup_method = 2;
			spinup_data = OF_getpropint64(ci->ci_node,
			    "cpu-release-addr", 0);
		}

		clockqueue_init(&ci->ci_queue);
		sched_init_cpu(ci);
		if (cpu_hatch_secondary(ci, spinup_method, spinup_data)) {
			atomic_setbits_int(&ci->ci_flags, CPUF_IDENTIFY);
			__asm volatile("dsb sy; sev");

			while ((ci->ci_flags & CPUF_IDENTIFIED) == 0 &&
			    --timeout)
				delay(1000);
			if (timeout == 0) {
				printf(" failed to identify");
				ci->ci_flags = 0;
			}
		} else {
			printf(" failed to spin up");
			ci->ci_flags = 0;
		}
	} else {
#endif
		cpu_identify(ci);

		vfp_init();

		if (OF_getproplen(ci->ci_node, "clocks") > 0) {
			cpu_node = ci->ci_node;
			cpu_cpuspeed = cpu_clockspeed;
		}
#ifdef MULTIPROCESSOR
	}
#endif

	opp = OF_getpropint(ci->ci_node, "operating-points-v2", 0);
	if (opp)
		cpu_opp_init(ci, opp);
	else
		cpu_opp_init_legacy(ci);

	printf("\n");
}

void
cpu_flush_bp_noop(void)
{
}

int
cpu_clockspeed(int *freq)
{
	*freq = clock_get_frequency(cpu_node, NULL) / 1000000;
	return 0;
}

#ifdef MULTIPROCESSOR

void cpu_boot_secondary(struct cpu_info *ci);
void cpu_hatch(void);

void
cpu_boot_secondary_processors(void)
{
	struct cpu_info *ci;
	CPU_INFO_ITERATOR cii;

	CPU_INFO_FOREACH(cii, ci) {
		if ((ci->ci_flags & CPUF_AP) == 0)
			continue;
		if (ci->ci_flags & CPUF_PRIMARY)
			continue;

		ci->ci_randseed = (arc4random() & 0x7fffffff) + 1;
		cpu_boot_secondary(ci);
	}
}

int
cpu_hatch_secondary(struct cpu_info *ci, int method, uint64_t data)
{
	extern paddr_t cpu_hatch_ci;
	paddr_t startaddr;
	void *kstack;
	uint32_t ttbr0;
	int rc = 0;

	kstack = km_alloc(USPACE, &kv_any, &kp_zero, &kd_waitok);
	ci->ci_pl1_stkend = (vaddr_t)kstack + USPACE - 16;

	kstack = km_alloc(PAGE_SIZE, &kv_any, &kp_zero, &kd_waitok);
	ci->ci_irq_stkend = (vaddr_t)kstack + PAGE_SIZE;
	kstack = km_alloc(PAGE_SIZE, &kv_any, &kp_zero, &kd_waitok);
	ci->ci_abt_stkend = (vaddr_t)kstack + PAGE_SIZE;
	kstack = km_alloc(PAGE_SIZE, &kv_any, &kp_zero, &kd_waitok);
	ci->ci_und_stkend = (vaddr_t)kstack + PAGE_SIZE;

	pmap_extract(pmap_kernel(), (vaddr_t)ci, &cpu_hatch_ci);

	__asm volatile("mrc p15, 0, %0, c2, c0, 0" : "=r"(ttbr0));
	ci->ci_ttbr0 = ttbr0;

	cpu_dcache_wb_range((vaddr_t)&cpu_hatch_ci, sizeof(paddr_t));
	cpu_dcache_wb_range((vaddr_t)ci, sizeof(*ci));

	pmap_extract(pmap_kernel(), (vaddr_t)cpu_hatch, &startaddr);

	switch (method) {
	case 1:
		/* psci  */
#if NPSCI > 0
		rc = (psci_cpu_on(ci->ci_mpidr, startaddr, 0) == PSCI_SUCCESS);
#endif
		break;
#ifdef notyet
	case 2:
		/* spin-table */
		cpu_hatch_spin_table(ci, startaddr, data);
		rc = 1;
		break;
#endif
	default:
		/* no method to spin up CPU */
		ci->ci_flags = 0;	/* mark cpu as not AP */
	}

	return rc;
}

void
cpu_boot_secondary(struct cpu_info *ci)
{
	atomic_setbits_int(&ci->ci_flags, CPUF_GO);
	__asm volatile("dsb sy; sev");

	while ((ci->ci_flags & CPUF_RUNNING) == 0)
		__asm volatile("wfe");
}

void
cpu_start_secondary(struct cpu_info *ci)
{
	int s;

	cpu_setup();

	set_stackptr(PSR_IRQ32_MODE, ci->ci_irq_stkend);
	set_stackptr(PSR_ABT32_MODE, ci->ci_abt_stkend);
	set_stackptr(PSR_UND32_MODE, ci->ci_und_stkend);
	
	ci->ci_flags |= CPUF_PRESENT;
	__asm volatile("dsb sy");

	while ((ci->ci_flags & CPUF_IDENTIFY) == 0)
		__asm volatile("wfe");

	cpu_identify(ci);
	atomic_setbits_int(&ci->ci_flags, CPUF_IDENTIFIED);
	__asm volatile("dsb sy");

	while ((ci->ci_flags & CPUF_GO) == 0)
		__asm volatile("wfe");

	s = splhigh();
	arm_intr_cpu_enable();
	cpu_startclock();

	atomic_setbits_int(&ci->ci_flags, CPUF_RUNNING);
	__asm volatile("dsb sy; sev");

	spllower(IPL_NONE);

	sched_toidle();
}

void
cpu_kick(struct cpu_info *ci)
{
	/* force cpu to enter kernel */
	if (ci != curcpu())
		arm_send_ipi(ci, ARM_IPI_NOP);
}

void
cpu_unidle(struct cpu_info *ci)
{
	/*
	 * This could send IPI or SEV depending on if the other
	 * processor is sleeping (WFI or WFE), in userland, or if the
	 * cpu is in other possible wait states?
	 */
	if (ci != curcpu())
		arm_send_ipi(ci, ARM_IPI_NOP);
}

#endif /* MULTIPROCESSOR */

/*
 * Dynamic voltage and frequency scaling implementation.
 */

extern int perflevel;

struct opp {
	uint64_t opp_hz;
	uint32_t opp_microvolt;
};

struct opp_table {
	LIST_ENTRY(opp_table) ot_list;
	uint32_t ot_phandle;

	struct opp *ot_opp;
	u_int ot_nopp;
	uint64_t ot_opp_hz_min;
	uint64_t ot_opp_hz_max;

	struct cpu_info *ot_master;
};

LIST_HEAD(, opp_table) opp_tables = LIST_HEAD_INITIALIZER(opp_tables);
struct task cpu_opp_task;

void	cpu_opp_mountroot(struct device *);
void	cpu_opp_dotask(void *);
void	cpu_opp_setperf(int);

uint32_t cpu_opp_get_cooling_level(void *, uint32_t *);
void	cpu_opp_set_cooling_level(void *, uint32_t *, uint32_t);

void
cpu_opp_init_legacy(struct cpu_info *ci)
{
	struct opp_table *ot;
	struct cooling_device *cd;
	uint32_t opp_hz, opp_microvolt;
	uint32_t *opps, supply;
	int i, j, len, count;

	supply = OF_getpropint(ci->ci_node, "cpu-supply", 0);
	if (supply == 0)
		return;

	len = OF_getproplen(ci->ci_node, "operating-points");
	if (len <= 0)
		return;

	opps = malloc(len, M_TEMP, M_WAITOK);
	OF_getpropintarray(ci->ci_node, "operating-points", opps, len);

	count = len / (2 * sizeof(uint32_t));

	ot = malloc(sizeof(struct opp_table), M_DEVBUF, M_ZERO | M_WAITOK);
	ot->ot_opp = mallocarray(count, sizeof(struct opp),
	    M_DEVBUF, M_ZERO | M_WAITOK);
	ot->ot_nopp = count;

	count = 0;
	while (count < len / (2 * sizeof(uint32_t))) {
		opp_hz = opps[2 * count] * 1000;
		opp_microvolt = opps[2 * count + 1];

		/* Insert into the array, keeping things sorted. */
		for (i = 0; i < count; i++) {
			if (opp_hz < ot->ot_opp[i].opp_hz)
				break;
		}
		for (j = count; j > i; j--)
			ot->ot_opp[j] = ot->ot_opp[j - 1];
		ot->ot_opp[i].opp_hz = opp_hz;
		ot->ot_opp[i].opp_microvolt = opp_microvolt;
		count++;
	}

	ot->ot_opp_hz_min = ot->ot_opp[0].opp_hz;
	ot->ot_opp_hz_max = ot->ot_opp[count - 1].opp_hz;
	
	free(opps, M_TEMP, len);

	ci->ci_opp_table = ot;
	ci->ci_opp_max = ot->ot_nopp - 1;
	ci->ci_cpu_supply = supply;

	cd = malloc(sizeof(struct cooling_device), M_DEVBUF, M_ZERO | M_WAITOK);
	cd->cd_node = ci->ci_node;
	cd->cd_cookie = ci;
	cd->cd_get_level = cpu_opp_get_cooling_level;
	cd->cd_set_level = cpu_opp_set_cooling_level;
	cooling_device_register(cd);

	/*
	 * Do additional checks at mountroot when all the clocks and
	 * regulators are available.
	 */
	config_mountroot(ci->ci_dev, cpu_opp_mountroot);
}

void
cpu_opp_init(struct cpu_info *ci, uint32_t phandle)
{
	struct opp_table *ot;
	struct cooling_device *cd;
	int count, node, child;
	uint32_t opp_hz, opp_microvolt;
	uint32_t values[3];
	int i, j, len;

	LIST_FOREACH(ot, &opp_tables, ot_list) {
		if (ot->ot_phandle == phandle) {
			ci->ci_opp_table = ot;
			return;
		}
	}

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return;

	if (!OF_is_compatible(node, "operating-points-v2"))
		return;

	count = 0;
	for (child = OF_child(node); child != 0; child = OF_peer(child)) {
		if (OF_getproplen(child, "turbo-mode") == 0)
			continue;
		count++;
	}
	if (count == 0)
		return;

	ot = malloc(sizeof(struct opp_table), M_DEVBUF, M_ZERO | M_WAITOK);
	ot->ot_phandle = phandle;
	ot->ot_opp = mallocarray(count, sizeof(struct opp),
	    M_DEVBUF, M_ZERO | M_WAITOK);
	ot->ot_nopp = count;

	count = 0;
	for (child = OF_child(node); child != 0; child = OF_peer(child)) {
		if (OF_getproplen(child, "turbo-mode") == 0)
			continue;
		opp_hz = OF_getpropint64(child, "opp-hz", 0);
		len = OF_getpropintarray(child, "opp-microvolt",
		    values, sizeof(values));
		opp_microvolt = 0;
		if (len == sizeof(uint32_t) || len == 3 * sizeof(uint32_t))
			opp_microvolt = values[0];

		/* Insert into the array, keeping things sorted. */
		for (i = 0; i < count; i++) {
			if (opp_hz < ot->ot_opp[i].opp_hz)
				break;
		}
		for (j = count; j > i; j--)
			ot->ot_opp[j] = ot->ot_opp[j - 1];
		ot->ot_opp[i].opp_hz = opp_hz;
		ot->ot_opp[i].opp_microvolt = opp_microvolt;
		count++;
	}

	ot->ot_opp_hz_min = ot->ot_opp[0].opp_hz;
	ot->ot_opp_hz_max = ot->ot_opp[count - 1].opp_hz;

	if (OF_getproplen(node, "opp-shared") == 0)
		ot->ot_master = ci;

	LIST_INSERT_HEAD(&opp_tables, ot, ot_list);

	ci->ci_opp_table = ot;
	ci->ci_opp_max = ot->ot_nopp - 1;
	ci->ci_cpu_supply = OF_getpropint(ci->ci_node, "cpu-supply", 0);

	cd = malloc(sizeof(struct cooling_device), M_DEVBUF, M_ZERO | M_WAITOK);
	cd->cd_node = ci->ci_node;
	cd->cd_cookie = ci;
	cd->cd_get_level = cpu_opp_get_cooling_level;
	cd->cd_set_level = cpu_opp_set_cooling_level;
	cooling_device_register(cd);

	/*
	 * Do additional checks at mountroot when all the clocks and
	 * regulators are available.
	 */
	config_mountroot(ci->ci_dev, cpu_opp_mountroot);
}

void
cpu_opp_mountroot(struct device *self)
{
	struct cpu_info *ci;
	CPU_INFO_ITERATOR cii;
	int count = 0;
	int level = 0;

	if (cpu_setperf)
		return;

	CPU_INFO_FOREACH(cii, ci) {
		struct opp_table *ot = ci->ci_opp_table;
		uint64_t curr_hz;
		uint32_t curr_microvolt;
		int error;

		if (ot == NULL)
			continue;

		/* Skip if this table is shared and we're not the master. */
		if (ot->ot_master && ot->ot_master != ci)
			continue;

		/* PWM regulators may need to be explicitly enabled. */
		regulator_enable(ci->ci_cpu_supply);

		curr_hz = clock_get_frequency(ci->ci_node, NULL);
		curr_microvolt = regulator_get_voltage(ci->ci_cpu_supply);

		/* Disable if clock isn't implemented. */
		error = ENODEV;
		if (curr_hz != 0)
			error = clock_set_frequency(ci->ci_node, NULL, curr_hz);
		if (error) {
			ci->ci_opp_table = NULL;
			printf("%s: clock not implemented\n",
			       ci->ci_dev->dv_xname);
			continue;
		}

		/* Disable if regulator isn't implemented. */
		error = ci->ci_cpu_supply ? ENODEV : 0;
		if (ci->ci_cpu_supply && curr_microvolt != 0)
			error = regulator_set_voltage(ci->ci_cpu_supply,
			    curr_microvolt);
		if (error) {
			ci->ci_opp_table = NULL;
			printf("%s: regulator not implemented\n",
			    ci->ci_dev->dv_xname);
			continue;
		}

		/*
		 * Initialize performance level based on the current
		 * speed of the first CPU that supports DVFS.
		 */
		if (level == 0) {
			uint64_t min, max;
			uint64_t level_hz;

			min = ot->ot_opp_hz_min;
			max = ot->ot_opp_hz_max;
			level_hz = clock_get_frequency(ci->ci_node, NULL);
			if (level_hz < min)
				level_hz = min;
			if (level_hz > max)
				level_hz = max;
			level = howmany(100 * (level_hz - min), (max - min));
		}

		count++;
	}

	if (count > 0) {
		task_set(&cpu_opp_task, cpu_opp_dotask, NULL);
		cpu_setperf = cpu_opp_setperf;

		perflevel = (level > 0) ? level : 0;
		cpu_setperf(perflevel);
	}
}

void
cpu_opp_dotask(void *arg)
{
	struct cpu_info *ci;
	CPU_INFO_ITERATOR cii;

	CPU_INFO_FOREACH(cii, ci) {
		struct opp_table *ot = ci->ci_opp_table;
		uint64_t curr_hz, opp_hz;
		uint32_t curr_microvolt, opp_microvolt;
		int opp_idx;
		int error = 0;

		if (ot == NULL)
			continue;

		/* Skip if this table is shared and we're not the master. */
		if (ot->ot_master && ot->ot_master != ci)
			continue;

		opp_idx = MIN(ci->ci_opp_idx, ci->ci_opp_max);
		opp_hz = ot->ot_opp[opp_idx].opp_hz;
		opp_microvolt = ot->ot_opp[opp_idx].opp_microvolt;

		curr_hz = clock_get_frequency(ci->ci_node, NULL);
		curr_microvolt = regulator_get_voltage(ci->ci_cpu_supply);

		if (error == 0 && opp_hz < curr_hz)
			error = clock_set_frequency(ci->ci_node, NULL, opp_hz);
		if (error == 0 && ci->ci_cpu_supply &&
		    opp_microvolt != 0 && opp_microvolt != curr_microvolt) {
			error = regulator_set_voltage(ci->ci_cpu_supply,
			    opp_microvolt);
		}
		if (error == 0 && opp_hz > curr_hz)
			error = clock_set_frequency(ci->ci_node, NULL, opp_hz);

		if (error)
			printf("%s: DVFS failed\n", ci->ci_dev->dv_xname);
	}
}

void
cpu_opp_setperf(int level)
{
	struct cpu_info *ci;
	CPU_INFO_ITERATOR cii;

	CPU_INFO_FOREACH(cii, ci) {
		struct opp_table *ot = ci->ci_opp_table;
		uint64_t min, max;
		uint64_t level_hz, opp_hz;
		int opp_idx = -1;
		int i;

		if (ot == NULL)
			continue;

		/* Skip if this table is shared and we're not the master. */
		if (ot->ot_master && ot->ot_master != ci)
			continue;

		min = ot->ot_opp_hz_min;
		max = ot->ot_opp_hz_max;
		level_hz = min + (level * (max - min)) / 100;
		opp_hz = min;
		for (i = 0; i < ot->ot_nopp; i++) {
			if (ot->ot_opp[i].opp_hz <= level_hz &&
			    ot->ot_opp[i].opp_hz >= opp_hz)
				opp_hz = ot->ot_opp[i].opp_hz;
		}

		/* Find index of selected operating point. */
		for (i = 0; i < ot->ot_nopp; i++) {
			if (ot->ot_opp[i].opp_hz == opp_hz) {
				opp_idx = i;
				break;
			}
		}
		KASSERT(opp_idx >= 0);

		ci->ci_opp_idx = opp_idx;
	}

	/*
	 * Update the hardware from a task since setting the
	 * regulators might need process context.
	 */
	task_add(systq, &cpu_opp_task);
}

uint32_t
cpu_opp_get_cooling_level(void *cookie, uint32_t *cells)
{
	struct cpu_info *ci = cookie;
	struct opp_table *ot = ci->ci_opp_table;
	
	return ot->ot_nopp - ci->ci_opp_max - 1;
}

void
cpu_opp_set_cooling_level(void *cookie, uint32_t *cells, uint32_t level)
{
	struct cpu_info *ci = cookie;
	struct opp_table *ot = ci->ci_opp_table;
	int opp_max;

	if (level > (ot->ot_nopp - 1))
		level = ot->ot_nopp - 1;

	opp_max = (ot->ot_nopp - level - 1);
	if (ci->ci_opp_max != opp_max) {
		ci->ci_opp_max = opp_max;
		task_add(systq, &cpu_opp_task);
	}
}
