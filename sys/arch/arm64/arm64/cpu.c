/*	$OpenBSD: cpu.c,v 1.143 2025/09/11 05:54:08 jsg Exp $	*/

/*
 * Copyright (c) 2016 Dale Rahn <drahn@dalerahn.com>
 * Copyright (c) 2017 Mark Kettenis <kettenis@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "kstat.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/sysctl.h>
#include <sys/task.h>
#include <sys/user.h>
#include <sys/kstat.h>

#include <uvm/uvm_extern.h>

#include <machine/fdt.h>
#include <machine/elf.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_regulator.h>
#include <dev/ofw/ofw_thermal.h>
#include <dev/ofw/fdt.h>

#include <machine/cpufunc.h>

#include "psci.h"
#if NPSCI > 0
#include <dev/fdt/pscivar.h>
#endif

/*
 * Fool the compiler into accessing these registers without enabling
 * SVE code generation.  The ID_AA64ZFR0_EL1 can always be accessed
 * and the code that writes to ZCR_EL1 is only executed if the CPU has
 * SVE support.
 */
#define id_aa64zfr0_el1		s3_0_c0_c4_4
#define zcr_el1			s3_0_c1_c2_0

/* CPU Identification */
#define CPU_IMPL_ARM		0x41
#define CPU_IMPL_CAVIUM		0x43
#define CPU_IMPL_AMCC		0x50
#define CPU_IMPL_QCOM		0x51
#define CPU_IMPL_APPLE		0x61
#define CPU_IMPL_AMPERE		0xc0

/* ARM */
#define CPU_PART_CORTEX_A34	0xd02
#define CPU_PART_CORTEX_A53	0xd03
#define CPU_PART_CORTEX_A35	0xd04
#define CPU_PART_CORTEX_A55	0xd05
#define CPU_PART_CORTEX_A65	0xd06
#define CPU_PART_CORTEX_A57	0xd07
#define CPU_PART_CORTEX_A72	0xd08
#define CPU_PART_CORTEX_A73	0xd09
#define CPU_PART_CORTEX_A75	0xd0a
#define CPU_PART_CORTEX_A76	0xd0b
#define CPU_PART_NEOVERSE_N1	0xd0c
#define CPU_PART_CORTEX_A77	0xd0d
#define CPU_PART_CORTEX_A76AE	0xd0e
#define CPU_PART_NEOVERSE_V1	0xd40
#define CPU_PART_CORTEX_A78	0xd41
#define CPU_PART_CORTEX_A78AE	0xd42
#define CPU_PART_CORTEX_A65AE	0xd43
#define CPU_PART_CORTEX_X1	0xd44
#define CPU_PART_CORTEX_A510	0xd46
#define CPU_PART_CORTEX_A710	0xd47
#define CPU_PART_CORTEX_X2	0xd48
#define CPU_PART_NEOVERSE_N2	0xd49
#define CPU_PART_NEOVERSE_E1	0xd4a
#define CPU_PART_CORTEX_A78C	0xd4b
#define CPU_PART_CORTEX_X1C	0xd4c
#define CPU_PART_CORTEX_A715	0xd4d
#define CPU_PART_CORTEX_X3	0xd4e
#define CPU_PART_NEOVERSE_V2	0xd4f
#define CPU_PART_CORTEX_A520	0xd80
#define CPU_PART_CORTEX_A720	0xd81
#define CPU_PART_CORTEX_X4	0xd82
#define CPU_PART_NEOVERSE_V3AE	0xd83
#define CPU_PART_NEOVERSE_V3	0xd84
#define CPU_PART_CORTEX_X925	0xd85
#define CPU_PART_CORTEX_A725	0xd87
#define CPU_PART_CORTEX_A520AE	0xd88
#define CPU_PART_CORTEX_A720AE	0xd89
#define CPU_PART_C1_NANO	0xd8a
#define CPU_PART_C1_PRO		0xd8b
#define CPU_PART_C1_ULTRA	0xd8c
#define CPU_PART_NEOVERSE_N3	0xd8e
#define CPU_PART_CORTEX_A320	0xd8f
#define CPU_PART_C1_PREMIUM	0xd90

/* Cavium */
#define CPU_PART_THUNDERX_T88	0x0a1
#define CPU_PART_THUNDERX_T81	0x0a2
#define CPU_PART_THUNDERX_T83	0x0a3
#define CPU_PART_THUNDERX2_T99	0x0af

/* Applied Micro */
#define CPU_PART_X_GENE		0x000

/* Qualcomm */
#define CPU_PART_ORYON		0x001
#define CPU_PART_KRYO400_GOLD	0x804
#define CPU_PART_KRYO400_SILVER	0x805

/* Apple */
#define CPU_PART_ICESTORM	0x022
#define CPU_PART_FIRESTORM	0x023
#define CPU_PART_ICESTORM_PRO	0x024
#define CPU_PART_FIRESTORM_PRO	0x025
#define CPU_PART_ICESTORM_MAX	0x028
#define CPU_PART_FIRESTORM_MAX	0x029
#define CPU_PART_BLIZZARD	0x032
#define CPU_PART_AVALANCHE	0x033
#define CPU_PART_BLIZZARD_PRO	0x034
#define CPU_PART_AVALANCHE_PRO	0x035
#define CPU_PART_BLIZZARD_MAX	0x038
#define CPU_PART_AVALANCHE_MAX	0x039

/* Ampere */
#define CPU_PART_AMPERE1	0xac3

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
	{ CPU_PART_C1_NANO, "C1-Nano" },
	{ CPU_PART_C1_PREMIUM, "C1-Premium" },
	{ CPU_PART_C1_PRO, "C1-Pro" },
	{ CPU_PART_C1_ULTRA, "C1-Ultra" },
	{ CPU_PART_CORTEX_A34, "Cortex-A34" },
	{ CPU_PART_CORTEX_A35, "Cortex-A35" },
	{ CPU_PART_CORTEX_A53, "Cortex-A53" },
	{ CPU_PART_CORTEX_A55, "Cortex-A55" },
	{ CPU_PART_CORTEX_A57, "Cortex-A57" },
	{ CPU_PART_CORTEX_A65, "Cortex-A65" },
	{ CPU_PART_CORTEX_A65AE, "Cortex-A65AE" },
	{ CPU_PART_CORTEX_A72, "Cortex-A72" },
	{ CPU_PART_CORTEX_A73, "Cortex-A73" },
	{ CPU_PART_CORTEX_A75, "Cortex-A75" },
	{ CPU_PART_CORTEX_A76, "Cortex-A76" },
	{ CPU_PART_CORTEX_A76AE, "Cortex-A76AE" },
	{ CPU_PART_CORTEX_A77, "Cortex-A77" },
	{ CPU_PART_CORTEX_A78, "Cortex-A78" },
	{ CPU_PART_CORTEX_A78AE, "Cortex-A78AE" },
	{ CPU_PART_CORTEX_A78C, "Cortex-A78C" },
	{ CPU_PART_CORTEX_A320, "Cortex-A320" },
	{ CPU_PART_CORTEX_A510, "Cortex-A510" },
	{ CPU_PART_CORTEX_A520, "Cortex-A520" },
	{ CPU_PART_CORTEX_A520AE, "Cortex-A520AE" },
	{ CPU_PART_CORTEX_A710, "Cortex-A710" },
	{ CPU_PART_CORTEX_A715, "Cortex-A715" },
	{ CPU_PART_CORTEX_A720, "Cortex-A720" },
	{ CPU_PART_CORTEX_A720AE, "Cortex-A720AE" },
	{ CPU_PART_CORTEX_A725, "Cortex-A725" },
	{ CPU_PART_CORTEX_X1, "Cortex-X1" },
	{ CPU_PART_CORTEX_X1C, "Cortex-X1C" },
	{ CPU_PART_CORTEX_X2, "Cortex-X2" },
	{ CPU_PART_CORTEX_X3, "Cortex-X3" },
	{ CPU_PART_CORTEX_X4, "Cortex-X4" },
	{ CPU_PART_CORTEX_X925, "Cortex-X925" },
	{ CPU_PART_NEOVERSE_E1, "Neoverse E1" },
	{ CPU_PART_NEOVERSE_N1, "Neoverse N1" },
	{ CPU_PART_NEOVERSE_N2, "Neoverse N2" },
	{ CPU_PART_NEOVERSE_N3, "Neoverse N3" },
	{ CPU_PART_NEOVERSE_V1, "Neoverse V1" },
	{ CPU_PART_NEOVERSE_V2, "Neoverse V2" },
	{ CPU_PART_NEOVERSE_V3, "Neoverse V3" },
	{ CPU_PART_NEOVERSE_V3AE, "Neoverse V3AE" },
	{ 0, NULL },
};

struct cpu_cores cpu_cores_cavium[] = {
	{ CPU_PART_THUNDERX_T88, "ThunderX T88" },
	{ CPU_PART_THUNDERX_T81, "ThunderX T81" },
	{ CPU_PART_THUNDERX_T83, "ThunderX T83" },
	{ CPU_PART_THUNDERX2_T99, "ThunderX2 T99" },
	{ 0, NULL },
};

struct cpu_cores cpu_cores_amcc[] = {
	{ CPU_PART_X_GENE, "X-Gene" },
	{ 0, NULL },
};

struct cpu_cores cpu_cores_qcom[] = {
	{ CPU_PART_KRYO400_GOLD, "Kryo 400 Gold" },
	{ CPU_PART_KRYO400_SILVER, "Kryo 400 Silver" },
	{ CPU_PART_ORYON, "Oryon" },
	{ 0, NULL },
};

struct cpu_cores cpu_cores_apple[] = {
	{ CPU_PART_ICESTORM, "Icestorm" },
	{ CPU_PART_FIRESTORM, "Firestorm" },
	{ CPU_PART_ICESTORM_PRO, "Icestorm Pro" },
	{ CPU_PART_FIRESTORM_PRO, "Firestorm Pro" },
	{ CPU_PART_ICESTORM_MAX, "Icestorm Max" },
	{ CPU_PART_FIRESTORM_MAX, "Firestorm Max" },
	{ CPU_PART_BLIZZARD, "Blizzard" },
	{ CPU_PART_AVALANCHE, "Avalanche" },
	{ CPU_PART_BLIZZARD_PRO, "Blizzard Pro" },
	{ CPU_PART_AVALANCHE_PRO, "Avalanche Pro" },
	{ CPU_PART_BLIZZARD_MAX, "Blizzard Max" },
	{ CPU_PART_AVALANCHE_MAX, "Avalanche Max" },
	{ 0, NULL },
};

struct cpu_cores cpu_cores_ampere[] = {
	{ CPU_PART_AMPERE1, "AmpereOne" },
	{ 0, NULL },
};

/* arm cores makers */
const struct implementers {
	int			id;
	char			*name;
	struct cpu_cores	*corelist;
} cpu_implementers[] = {
	{ CPU_IMPL_ARM,	"ARM", cpu_cores_arm },
	{ CPU_IMPL_CAVIUM, "Cavium", cpu_cores_cavium },
	{ CPU_IMPL_AMCC, "Applied Micro", cpu_cores_amcc },
	{ CPU_IMPL_QCOM, "Qualcomm", cpu_cores_qcom },
	{ CPU_IMPL_APPLE, "Apple", cpu_cores_apple },
	{ CPU_IMPL_AMPERE, "Ampere", cpu_cores_ampere },
	{ 0, NULL },
};

char cpu_model[64];
int cpu_node;

uint64_t cpu_id_aa64isar0;
uint64_t cpu_id_aa64isar1;
uint64_t cpu_id_aa64isar2;
uint64_t cpu_id_aa64mmfr0;
uint64_t cpu_id_aa64mmfr1;
uint64_t cpu_id_aa64mmfr2;
uint64_t cpu_id_aa64pfr0;
uint64_t cpu_id_aa64pfr1;
uint64_t cpu_id_aa64zfr0;

int arm64_has_lse;
int arm64_has_rng;
#ifdef CRYPTO
int arm64_has_aes;
#endif

extern char trampoline_vectors_none[];
extern char trampoline_vectors_loop_8[];
extern char trampoline_vectors_loop_11[];
extern char trampoline_vectors_loop_24[];
extern char trampoline_vectors_loop_32[];
extern char trampoline_vectors_loop_132[];
#if NPSCI > 0
extern char trampoline_vectors_psci_hvc[];
extern char trampoline_vectors_psci_smc[];
#endif
extern char trampoline_vectors_clrbhb[];

struct cpu_info *cpu_info_list = &cpu_info_primary;

int	cpu_match(struct device *, void *, void *);
void	cpu_attach(struct device *, struct device *, void *);

const struct cfattach cpu_ca = {
	sizeof(struct device), cpu_match, cpu_attach
};

struct cfdriver cpu_cd = {
	NULL, "cpu", DV_DULL
};

struct timeout cpu_rng_to;
void	cpu_rng(void *);

void	cpu_opp_init(struct cpu_info *, uint32_t);
void	cpu_psci_init(struct cpu_info *);
void	cpu_psci_idle_cycle(void);

void	cpu_flush_bp_noop(void);
void	cpu_flush_bp_psci(void);
void	cpu_serror_apple(void);

#if NKSTAT > 0
void	cpu_kstat_attach(struct cpu_info *ci);
void	cpu_opp_kstat_attach(struct cpu_info *ci);
#endif

void
cpu_rng(void *arg)
{
	struct timeout *to = arg;
	uint64_t rndr;
	int ret;

	ret = __builtin_arm_rndrrs(&rndr);
	if (ret)
		ret = __builtin_arm_rndr(&rndr);
	if (ret == 0) {
		enqueue_randomness(rndr & 0xffffffff);
		enqueue_randomness(rndr >> 32);
	}

	if (to)
		timeout_add_msec(to, 1000);
}

/*
 * Enable mitigation for Spectre-V2 branch target injection
 * vulnerabilities (CVE-2017-5715).
 */
void
cpu_mitigate_spectre_v2(struct cpu_info *ci)
{
	uint64_t id;

	/*
	 * By default we let the firmware decide what mitigation is
	 * necessary.
	 */
	ci->ci_flush_bp = cpu_flush_bp_psci;

	/* Some specific CPUs are known not to be vulnerable. */
	switch (CPU_IMPL(ci->ci_midr)) {
	case CPU_IMPL_ARM:
		switch (CPU_PART(ci->ci_midr)) {
		case CPU_PART_CORTEX_A35:
		case CPU_PART_CORTEX_A53:
		case CPU_PART_CORTEX_A55:
			/* Not vulnerable. */
			ci->ci_flush_bp = cpu_flush_bp_noop;
			break;
		}
		break;
	case CPU_IMPL_QCOM:
		switch (CPU_PART(ci->ci_midr)) {
		case CPU_PART_KRYO400_SILVER:
			/* Not vulnerable. */
			ci->ci_flush_bp = cpu_flush_bp_noop;
			break;
		}
	}

	/*
	 * The architecture has been updated to explicitly tell us if
	 * we're not vulnerable to Spectre-V2.
	 */
	id = READ_SPECIALREG(id_aa64pfr0_el1);
	if (ID_AA64PFR0_CSV2(id) >= ID_AA64PFR0_CSV2_IMPL)
		ci->ci_flush_bp = cpu_flush_bp_noop;
}

/*
 * Enable mitigation for Spectre-BHB branch history injection
 * vulnerabilities (CVE-2022-23960).
*/
void
cpu_mitigate_spectre_bhb(struct cpu_info *ci)
{
	uint64_t id;

	/*
	 * If we know the CPU, we can add a branchy loop that cleans
	 * the BHB.
	 */
	switch (CPU_IMPL(ci->ci_midr)) {
	case CPU_IMPL_ARM:
		switch (CPU_PART(ci->ci_midr)) {
		case CPU_PART_CORTEX_A57:
		case CPU_PART_CORTEX_A72:
			ci->ci_trampoline_vectors =
			    (vaddr_t)trampoline_vectors_loop_8;
			break;
		case CPU_PART_CORTEX_A76:
		case CPU_PART_CORTEX_A76AE:
		case CPU_PART_CORTEX_A77:
		case CPU_PART_NEOVERSE_N1:
			ci->ci_trampoline_vectors =
			    (vaddr_t)trampoline_vectors_loop_24;
			break;
		case CPU_PART_CORTEX_A78:
		case CPU_PART_CORTEX_A78AE:
		case CPU_PART_CORTEX_A78C:
		case CPU_PART_CORTEX_X1:
		case CPU_PART_CORTEX_X1C:
		case CPU_PART_CORTEX_X2:
		case CPU_PART_CORTEX_A710:
		case CPU_PART_NEOVERSE_N2:
		case CPU_PART_NEOVERSE_V1:
			ci->ci_trampoline_vectors =
			    (vaddr_t)trampoline_vectors_loop_32;
			break;
		case CPU_PART_CORTEX_X3:
		case CPU_PART_CORTEX_X4:
		case CPU_PART_CORTEX_X925:
		case CPU_PART_NEOVERSE_V2:
		case CPU_PART_NEOVERSE_V3:
		case CPU_PART_NEOVERSE_V3AE:
			ci->ci_trampoline_vectors =
			    (vaddr_t)trampoline_vectors_loop_132;
			break;
		}
		break;
	case CPU_IMPL_AMPERE:
		switch (CPU_PART(ci->ci_midr)) {
		case CPU_PART_AMPERE1:
			ci->ci_trampoline_vectors =
			    (vaddr_t)trampoline_vectors_loop_11;
			break;
		}
		break;
	}

	/*
	 * If we're not using a loop, let firmware decide.  This also
	 * covers the original Spectre-V2 in addition to Spectre-BHB.
	 */
#if NPSCI > 0
	if (ci->ci_trampoline_vectors == (vaddr_t)trampoline_vectors_none &&
	    smccc_needs_arch_workaround_3()) {
		ci->ci_flush_bp = cpu_flush_bp_noop;
		if (psci_method() == PSCI_METHOD_HVC)
			ci->ci_trampoline_vectors =
			    (vaddr_t)trampoline_vectors_psci_hvc;
		if (psci_method() == PSCI_METHOD_SMC)
			ci->ci_trampoline_vectors =
			    (vaddr_t)trampoline_vectors_psci_smc;
	}
#endif

	/* Prefer CLRBHB to mitigate Spectre-BHB. */
	id = READ_SPECIALREG(id_aa64isar2_el1);
	if (ID_AA64ISAR2_CLRBHB(id) >= ID_AA64ISAR2_CLRBHB_IMPL)
		ci->ci_trampoline_vectors = (vaddr_t)trampoline_vectors_clrbhb;

	/* ECBHB tells us Spectre-BHB is mitigated. */
	id = READ_SPECIALREG(id_aa64mmfr1_el1);
	if (ID_AA64MMFR1_ECBHB(id) >= ID_AA64MMFR1_ECBHB_IMPL)
		ci->ci_trampoline_vectors = (vaddr_t)trampoline_vectors_none;

	/*
	 * The architecture has been updated to explicitly tell us if
	 * we're not vulnerable to Spectre-BHB.
	 */
	id = READ_SPECIALREG(id_aa64pfr0_el1);
	if (ID_AA64PFR0_CSV2(id) >= ID_AA64PFR0_CSV2_HCXT)
		ci->ci_trampoline_vectors = (vaddr_t)trampoline_vectors_none;
}

/*
 * Enable mitigation for Spectre-V4 speculative store bypass
 * vulnerabilities (CVE-2018-3639).
 */
void
cpu_mitigate_spectre_v4(struct cpu_info *ci)
{
	uint64_t id;

	switch (CPU_IMPL(ci->ci_midr)) {
	case CPU_IMPL_ARM:
		switch (CPU_PART(ci->ci_midr)) {
		case CPU_PART_CORTEX_A35:
		case CPU_PART_CORTEX_A53:
		case CPU_PART_CORTEX_A55:
			/* Not vulnerable. */
			return;
		}
		break;
	case CPU_IMPL_QCOM:
		switch (CPU_PART(ci->ci_midr)) {
		case CPU_PART_KRYO400_SILVER:
			/* Not vulnerable. */
			return;
		}
		break;
	}

	/* SSBS tells us Spectre-V4 is mitigated. */
	id = READ_SPECIALREG(id_aa64pfr1_el1);
	if (ID_AA64PFR1_SSBS(id) >= ID_AA64PFR1_SSBS_PSTATE)
		return;

	/* Enable firmware workaround if required. */
	smccc_enable_arch_workaround_2();
}

void
cpu_identify(struct cpu_info *ci)
{
	static uint64_t prev_id_aa64isar0;
	static uint64_t prev_id_aa64isar1;
	static uint64_t prev_id_aa64isar2;
	static uint64_t prev_id_aa64mmfr0;
	static uint64_t prev_id_aa64mmfr1;
	static uint64_t prev_id_aa64mmfr2;
	static uint64_t prev_id_aa64pfr0;
	static uint64_t prev_id_aa64pfr1;
	static uint64_t prev_id_aa64zfr0;
	uint64_t midr, impl, part;
	uint64_t clidr, ccsidr, id;
	uint32_t ctr, sets, ways, line;
	const char *impl_name = NULL;
	const char *part_name = NULL;
	const char *il1p_name = NULL;
	const char *sep;
	struct cpu_cores *coreselecter = cpu_cores_none;
	int ccidx;
	int i;

	midr = READ_SPECIALREG(midr_el1);
	impl = CPU_IMPL(midr);
	part = CPU_PART(midr);
	ci->ci_midr = midr;

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
		printf(" %s %s r%llup%llu", impl_name, part_name, CPU_VAR(midr),
		    CPU_REV(midr));

		if (CPU_IS_PRIMARY(ci))
			snprintf(cpu_model, sizeof(cpu_model),
			    "%s %s r%llup%llu", impl_name, part_name,
			    CPU_VAR(midr), CPU_REV(midr));
	} else {
		printf(" Unknown, MIDR 0x%llx", midr);

		if (CPU_IS_PRIMARY(ci))
			snprintf(cpu_model, sizeof(cpu_model), "Unknown");
	}

	/* Print cache information. */

	ctr = READ_SPECIALREG(ctr_el0);
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

	id = READ_SPECIALREG(id_aa64mmfr2_el1);
	clidr = READ_SPECIALREG(clidr_el1);
	if (ID_AA64MMFR2_CCIDX(id) > ID_AA64MMFR2_CCIDX_IMPL) {
		/* Reserved value.  Don't print cache information. */
		clidr = 0;
	} else if (ID_AA64MMFR2_CCIDX(id) == ID_AA64MMFR2_CCIDX_IMPL) {
		/* CCSIDR_EL1 uses the new 64-bit format. */
		ccidx = 1;
	} else {
		/* CCSIDR_EL1 uses the old 32-bit format. */
		ccidx = 0;
	}
	for (i = 0; i < 7; i++) {
		if ((clidr & CLIDR_CTYPE_MASK) == 0)
			break;
		printf("\n%s:", ci->ci_dev->dv_xname);
		sep = "";
		if (clidr & CLIDR_CTYPE_INSN) {
			WRITE_SPECIALREG(csselr_el1,
			    i << CSSELR_LEVEL_SHIFT | CSSELR_IND);
			__asm volatile("isb");
			ccsidr = READ_SPECIALREG(ccsidr_el1);
			if (ccidx) {
				sets = CCSIDR_CCIDX_SETS(ccsidr);
				ways = CCSIDR_CCIDX_WAYS(ccsidr);
				line = CCSIDR_CCIDX_LINE_SIZE(ccsidr);
			} else {
				sets = CCSIDR_SETS(ccsidr);
				ways = CCSIDR_WAYS(ccsidr);
				line = CCSIDR_LINE_SIZE(ccsidr);
			}
			printf("%s %dKB %db/line %d-way L%d %sI-cache", sep,
			    (sets * ways * line) / 1024, line, ways, (i + 1),
			    il1p_name);
			il1p_name = "";
			sep = ",";
		}
		if (clidr & CLIDR_CTYPE_DATA) {
			WRITE_SPECIALREG(csselr_el1, i << CSSELR_LEVEL_SHIFT);
			__asm volatile("isb");
			ccsidr = READ_SPECIALREG(ccsidr_el1);
			if (ccidx) {
				sets = CCSIDR_CCIDX_SETS(ccsidr);
				ways = CCSIDR_CCIDX_WAYS(ccsidr);
				line = CCSIDR_CCIDX_LINE_SIZE(ccsidr);
			} else {
				sets = CCSIDR_SETS(ccsidr);
				ways = CCSIDR_WAYS(ccsidr);
				line = CCSIDR_LINE_SIZE(ccsidr);
			}
			printf("%s %dKB %db/line %d-way L%d D-cache", sep,
			    (sets * ways * line) / 1024, line, ways, (i + 1));
			sep = ",";
		}
		if (clidr & CLIDR_CTYPE_UNIFIED) {
			WRITE_SPECIALREG(csselr_el1, i << CSSELR_LEVEL_SHIFT);
			__asm volatile("isb");
			ccsidr = READ_SPECIALREG(ccsidr_el1);
			if (ccidx) {
				sets = CCSIDR_CCIDX_SETS(ccsidr);
				ways = CCSIDR_CCIDX_WAYS(ccsidr);
				line = CCSIDR_CCIDX_LINE_SIZE(ccsidr);
			} else {
				sets = CCSIDR_SETS(ccsidr);
				ways = CCSIDR_WAYS(ccsidr);
				line = CCSIDR_LINE_SIZE(ccsidr);
			}
			printf("%s %dKB %db/line %d-way L%d cache", sep,
			    (sets * ways * line) / 1024, line, ways, (i + 1));
		}
		clidr >>= 3;
	}

	cpu_mitigate_spectre_v2(ci);
	cpu_mitigate_spectre_bhb(ci);
	cpu_mitigate_spectre_v4(ci);

	/*
	 * Apple CPUs provide detailed information for SError.
	 */
	if (impl == CPU_IMPL_APPLE)
		ci->ci_serror = cpu_serror_apple;

	/*
	 * Skip printing CPU features if they are identical to the
	 * previous CPU.
	 */
	if (READ_SPECIALREG(id_aa64isar0_el1) == prev_id_aa64isar0 &&
	    READ_SPECIALREG(id_aa64isar1_el1) == prev_id_aa64isar1 &&
	    READ_SPECIALREG(id_aa64isar2_el1) == prev_id_aa64isar2 &&
	    READ_SPECIALREG(id_aa64mmfr0_el1) == prev_id_aa64mmfr0 &&
	    READ_SPECIALREG(id_aa64mmfr1_el1) == prev_id_aa64mmfr1 &&
	    READ_SPECIALREG(id_aa64mmfr2_el1) == prev_id_aa64mmfr2 &&
	    READ_SPECIALREG(id_aa64pfr0_el1) == prev_id_aa64pfr0 &&
	    READ_SPECIALREG(id_aa64pfr1_el1) == prev_id_aa64pfr1 &&
	    READ_SPECIALREG(id_aa64zfr0_el1) == prev_id_aa64zfr0)
		return;

	/*
	 * Print CPU features encoded in the ID registers.
	 */

	if (READ_SPECIALREG(id_aa64isar0_el1) != cpu_id_aa64isar0) {
		printf("\n%s: mismatched ID_AA64ISAR0_EL1",
		    ci->ci_dev->dv_xname);
	}
	if (READ_SPECIALREG(id_aa64isar1_el1) != cpu_id_aa64isar1) {
		printf("\n%s: mismatched ID_AA64ISAR1_EL1",
		    ci->ci_dev->dv_xname);
	}
	if (READ_SPECIALREG(id_aa64isar2_el1) != cpu_id_aa64isar2) {
		printf("\n%s: mismatched ID_AA64ISAR2_EL1",
		    ci->ci_dev->dv_xname);
	}
	if (READ_SPECIALREG(id_aa64mmfr0_el1) != cpu_id_aa64mmfr0) {
		printf("\n%s: mismatched ID_AA64MMFR0_EL1",
		    ci->ci_dev->dv_xname);
	}
	id = READ_SPECIALREG(id_aa64mmfr1_el1);
	/* Allow SpecSEI to be different. */
	id &= ~ID_AA64MMFR1_SPECSEI_MASK;
	if (id != cpu_id_aa64mmfr1) {
		printf("\n%s: mismatched ID_AA64MMFR1_EL1",
		    ci->ci_dev->dv_xname);
	}
	if (READ_SPECIALREG(id_aa64mmfr2_el1) != cpu_id_aa64mmfr2) {
		printf("\n%s: mismatched ID_AA64MMFR2_EL1",
		    ci->ci_dev->dv_xname);
	}
	id = READ_SPECIALREG(id_aa64pfr0_el1);
	/* Allow CSV2/CVS3 to be different. */
	id &= ~ID_AA64PFR0_CSV2_MASK;
	id &= ~ID_AA64PFR0_CSV3_MASK;
	/* Ignore 32-bit support in all exception levels. */
	id &= ~ID_AA64PFR0_EL0_MASK;
	id &= ~ID_AA64PFR0_EL1_MASK;
	id &= ~ID_AA64PFR0_EL2_MASK;
	id &= ~ID_AA64PFR0_EL3_MASK;
	if (id != cpu_id_aa64pfr0) {
		printf("\n%s: mismatched ID_AA64PFR0_EL1",
		    ci->ci_dev->dv_xname);
	}
	if (READ_SPECIALREG(id_aa64pfr1_el1) != cpu_id_aa64pfr1) {
		printf("\n%s: mismatched ID_AA64PFR1_EL1",
		    ci->ci_dev->dv_xname);
	}

	printf("\n%s: ", ci->ci_dev->dv_xname);

	/*
	 * ID_AA64ISAR0
	 */
	id = READ_SPECIALREG(id_aa64isar0_el1);
	sep = "";

	if (ID_AA64ISAR0_RNDR(id) >= ID_AA64ISAR0_RNDR_IMPL) {
		printf("%sRNDR", sep);
		sep = ",";
		arm64_has_rng = 1;
	}

	if (ID_AA64ISAR0_TLB(id) >= ID_AA64ISAR0_TLB_IOS) {
		printf("%sTLBIOS", sep);
		sep = ",";
	}
	if (ID_AA64ISAR0_TLB(id) >= ID_AA64ISAR0_TLB_IRANGE)
		printf("+IRANGE");

	if (ID_AA64ISAR0_TS(id) >= ID_AA64ISAR0_TS_BASE) {
		printf("%sTS", sep);
		sep = ",";
	}
	if (ID_AA64ISAR0_TS(id) >= ID_AA64ISAR0_TS_AXFLAG)
		printf("+AXFLAG");

	if (ID_AA64ISAR0_FHM(id) >= ID_AA64ISAR0_FHM_IMPL) {
		printf("%sFHM", sep);
		sep = ",";
	}

	if (ID_AA64ISAR0_DP(id) >= ID_AA64ISAR0_DP_IMPL) {
		printf("%sDP", sep);
		sep = ",";
	}

	if (ID_AA64ISAR0_SM4(id) >= ID_AA64ISAR0_SM4_IMPL) {
		printf("%sSM4", sep);
		sep = ",";
	}

	if (ID_AA64ISAR0_SM3(id) >= ID_AA64ISAR0_SM3_IMPL) {
		printf("%sSM3", sep);
		sep = ",";
	}

	if (ID_AA64ISAR0_SHA3(id) >= ID_AA64ISAR0_SHA3_IMPL) {
		printf("%sSHA3", sep);
		sep = ",";
	}

	if (ID_AA64ISAR0_RDM(id) >= ID_AA64ISAR0_RDM_IMPL) {
		printf("%sRDM", sep);
		sep = ",";
	}

	if (ID_AA64ISAR0_ATOMIC(id) >= ID_AA64ISAR0_ATOMIC_IMPL) {
		printf("%sAtomic", sep);
		sep = ",";
		arm64_has_lse = 1;
	}

	if (ID_AA64ISAR0_CRC32(id) >= ID_AA64ISAR0_CRC32_BASE) {
		printf("%sCRC32", sep);
		sep = ",";
	}

	if (ID_AA64ISAR0_SHA2(id) >= ID_AA64ISAR0_SHA2_BASE) {
		printf("%sSHA2", sep);
		sep = ",";
	}
	if (ID_AA64ISAR0_SHA2(id) >= ID_AA64ISAR0_SHA2_512)
		printf("+SHA512");

	if (ID_AA64ISAR0_SHA1(id) >= ID_AA64ISAR0_SHA1_BASE) {
		printf("%sSHA1", sep);
		sep = ",";
	}

	if (ID_AA64ISAR0_AES(id) >= ID_AA64ISAR0_AES_BASE) {
		printf("%sAES", sep);
		sep = ",";
#ifdef CRYPTO
		arm64_has_aes = 1;
#endif
	}
	if (ID_AA64ISAR0_AES(id) >= ID_AA64ISAR0_AES_PMULL)
		printf("+PMULL");

	/*
	 * ID_AA64ISAR1
	 */
	id = READ_SPECIALREG(id_aa64isar1_el1);

	if (ID_AA64ISAR1_LS64(id) >= ID_AA64ISAR1_LS64_BASE) {
		printf("%sLS64", sep);
		sep = ",";
	}
	if (ID_AA64ISAR1_LS64(id) >= ID_AA64ISAR1_LS64_V)
		printf("+V");
	if (ID_AA64ISAR1_LS64(id) >= ID_AA64ISAR1_LS64_ACCDATA)
		printf("+ACCDATA");

	if (ID_AA64ISAR1_XS(id) >= ID_AA64ISAR1_XS_IMPL) {
		printf("%sXS", sep);
		sep = ",";
	}

	if (ID_AA64ISAR1_I8MM(id) >= ID_AA64ISAR1_I8MM_IMPL) {
		printf("%sI8MM", sep);
		sep = ",";
	}

	if (ID_AA64ISAR1_DGH(id) >= ID_AA64ISAR1_DGH_IMPL) {
		printf("%sDGH", sep);
		sep = ",";
	}

	if (ID_AA64ISAR1_BF16(id) >= ID_AA64ISAR1_BF16_BASE) {
		printf("%sBF16", sep);
		sep = ",";
	}
	if (ID_AA64ISAR1_BF16(id) >= ID_AA64ISAR1_BF16_EBF)
		printf("+EBF");

	if (ID_AA64ISAR1_SPECRES(id) >= ID_AA64ISAR1_SPECRES_IMPL) {
		printf("%sSPECRES", sep);
		sep = ",";
	}

	if (ID_AA64ISAR1_SB(id) >= ID_AA64ISAR1_SB_IMPL) {
		printf("%sSB", sep);
		sep = ",";
	}

	if (ID_AA64ISAR1_FRINTTS(id) >= ID_AA64ISAR1_FRINTTS_IMPL) {
		printf("%sFRINTTS", sep);
		sep = ",";
	}

	if (ID_AA64ISAR1_GPI(id) >= ID_AA64ISAR1_GPI_IMPL) {
		printf("%sGPI", sep);
		sep = ",";
	}

	if (ID_AA64ISAR1_GPA(id) >= ID_AA64ISAR1_GPA_IMPL) {
		printf("%sGPA", sep);
		sep = ",";
	}

	if (ID_AA64ISAR1_LRCPC(id) >= ID_AA64ISAR1_LRCPC_BASE) {
		printf("%sLRCPC", sep);
		sep = ",";
	}
	if (ID_AA64ISAR1_LRCPC(id) >= ID_AA64ISAR1_LRCPC_LDAPUR)
		printf("+LDAPUR");

	if (ID_AA64ISAR1_FCMA(id) >= ID_AA64ISAR1_FCMA_IMPL) {
		printf("%sFCMA", sep);
		sep = ",";
	}

	if (ID_AA64ISAR1_JSCVT(id) >= ID_AA64ISAR1_JSCVT_IMPL) {
		printf("%sJSCVT", sep);
		sep = ",";
	}

	if (ID_AA64ISAR1_API(id) >= ID_AA64ISAR1_API_PAC) {
		printf("%sAPI", sep);
		sep = ",";
	}
	if (ID_AA64ISAR1_API(id) == ID_AA64ISAR1_API_EPAC)
		printf("+EPAC");
	else if (ID_AA64ISAR1_API(id) >= ID_AA64ISAR1_API_EPAC2)
		printf("+EPAC2");
	if (ID_AA64ISAR1_API(id) >= ID_AA64ISAR1_API_FPAC)
		printf("+FPAC");
	if (ID_AA64ISAR1_API(id) >= ID_AA64ISAR1_API_FPAC_COMBINED)
		printf("+COMBINED");

	if (ID_AA64ISAR1_APA(id) >= ID_AA64ISAR1_APA_PAC) {
		printf("%sAPA", sep);
		sep = ",";
	}
	if (ID_AA64ISAR1_APA(id) == ID_AA64ISAR1_APA_EPAC)
		printf("+EPAC");
	else if (ID_AA64ISAR1_APA(id) >= ID_AA64ISAR1_APA_EPAC2)
		printf("+EPAC2");
	if (ID_AA64ISAR1_APA(id) >= ID_AA64ISAR1_APA_FPAC)
		printf("+FPAC");
	if (ID_AA64ISAR1_APA(id) >= ID_AA64ISAR1_APA_FPAC_COMBINED)
		printf("+COMBINED");

	if (ID_AA64ISAR1_DPB(id) >= ID_AA64ISAR1_DPB_IMPL) {
		printf("%sDPB", sep);
		sep = ",";
	}
	if (ID_AA64ISAR1_DPB(id) >= ID_AA64ISAR1_DPB_DCCVADP)
		printf("+DCCVADP");

	/*
	 * ID_AA64ISAR2
	 */
	id = READ_SPECIALREG(id_aa64isar2_el1);

	if (ID_AA64ISAR2_CSSC(id) >= ID_AA64ISAR2_CSSC_IMPL) {
		printf("%sCSSC", sep);
		sep = ",";
	}

	if (ID_AA64ISAR2_RPRFM(id) >= ID_AA64ISAR2_RPRFM_IMPL) {
		printf("%sRPRFM", sep);
		sep = ",";
	}

	if (ID_AA64ISAR2_CLRBHB(id) >= ID_AA64ISAR2_CLRBHB_IMPL) {
		printf("%sCLRBHB", sep);
		sep = ",";
	}

	if (ID_AA64ISAR2_BC(id) >= ID_AA64ISAR2_BC_IMPL) {
		printf("%sBC", sep);
		sep = ",";
	}

	if (ID_AA64ISAR2_MOPS(id) >= ID_AA64ISAR2_MOPS_IMPL) {
		printf("%sMOPS", sep);
		sep = ",";
	}

	if (ID_AA64ISAR2_GPA3(id) >= ID_AA64ISAR2_GPA3_IMPL) {
		printf("%sGPA3", sep);
		sep = ",";
	}

	if (ID_AA64ISAR2_APA3(id) >= ID_AA64ISAR2_APA3_PAC) {
		printf("%sAPA3", sep);
		sep = ",";
	}
	if (ID_AA64ISAR2_APA3(id) == ID_AA64ISAR2_APA3_EPAC)
		printf("+EPAC");
	else if (ID_AA64ISAR2_APA3(id) >= ID_AA64ISAR2_APA3_EPAC2)
		printf("+EPAC2");
	if (ID_AA64ISAR2_APA3(id) >= ID_AA64ISAR2_APA3_FPAC)
		printf("+FPAC");
	if (ID_AA64ISAR2_APA3(id) >= ID_AA64ISAR2_APA3_FPAC_COMBINED)
		printf("+COMBINED");

	if (ID_AA64ISAR2_RPRES(id) >= ID_AA64ISAR2_RPRES_IMPL) {
		printf("%sRPRES", sep);
		sep = ",";
	}

	if (ID_AA64ISAR2_WFXT(id) >= ID_AA64ISAR2_WFXT_IMPL) {
		printf("%sWFXT", sep);
		sep = ",";
	}

	/*
	 * ID_AA64MMFR0
	 *
	 * We only print ASIDBits for now.
	 */
	id = READ_SPECIALREG(id_aa64mmfr0_el1);

	if (ID_AA64MMFR0_ECV(id) >= ID_AA64MMFR0_ECV_IMPL) {
		printf("%sECV", sep);
		sep = ",";
	}
	if (ID_AA64MMFR0_ECV(id) >= ID_AA64MMFR0_ECV_CNTHCTL)
		printf("+CNTHCTL");

	if (ID_AA64MMFR0_ASID_BITS(id) == ID_AA64MMFR0_ASID_BITS_16) {
		printf("%sASID16", sep);
		sep = ",";
	}

	/*
	 * ID_AA64MMFR1
	 *
	 * We omit printing most virtualization related fields for now.
	 */
	id = READ_SPECIALREG(id_aa64mmfr1_el1);

	if (ID_AA64MMFR1_AFP(id) >= ID_AA64MMFR1_AFP_IMPL) {
		printf("%sAFP", sep);
		sep = ",";
	}

	if (ID_AA64MMFR1_SPECSEI(id) >= ID_AA64MMFR1_SPECSEI_IMPL) {
		printf("%sSpecSEI", sep);
		sep = ",";
	}

	if (ID_AA64MMFR1_PAN(id) >= ID_AA64MMFR1_PAN_IMPL) {
		printf("%sPAN", sep);
		sep = ",";
	}
	if (ID_AA64MMFR1_PAN(id) >= ID_AA64MMFR1_PAN_ATS1E1)
		printf("+ATS1E1");
	if (ID_AA64MMFR1_PAN(id) >= ID_AA64MMFR1_PAN_EPAN)
		printf("+EPAN");

	if (ID_AA64MMFR1_LO(id) >= ID_AA64MMFR1_LO_IMPL) {
		printf("%sLO", sep);
		sep = ",";
	}

	if (ID_AA64MMFR1_HPDS(id) >= ID_AA64MMFR1_HPDS_IMPL) {
		printf("%sHPDS", sep);
		sep = ",";
	}

	if (ID_AA64MMFR1_VH(id) >= ID_AA64MMFR1_VH_IMPL) {
		printf("%sVH", sep);
		sep = ",";
	}

	if (ID_AA64MMFR1_HAFDBS(id) >= ID_AA64MMFR1_HAFDBS_AF) {
		printf("%sHAF", sep);
		sep = ",";
	}
	if (ID_AA64MMFR1_HAFDBS(id) >= ID_AA64MMFR1_HAFDBS_AF_DBS)
		printf("DBS");

	if (ID_AA64MMFR1_ECBHB(id) >= ID_AA64MMFR1_ECBHB_IMPL) {
		printf("%sECBHB", sep);
		sep = ",";
	}

	/*
	 * ID_AA64MMFR2
	 */
	id = READ_SPECIALREG(id_aa64mmfr2_el1);

	if (ID_AA64MMFR2_IDS(id) >= ID_AA64MMFR2_IDS_IMPL) {
		printf("%sIDS", sep);
		sep = ",";
	}

	if (ID_AA64MMFR2_AT(id) >= ID_AA64MMFR2_AT_IMPL) {
		printf("%sAT", sep);
		sep = ",";
	}

	/*
	 * ID_AA64PFR0
	 */
	id = READ_SPECIALREG(id_aa64pfr0_el1);

	if (ID_AA64PFR0_CSV3(id) >= ID_AA64PFR0_CSV3_IMPL) {
		printf("%sCSV3", sep);
		sep = ",";
	}

	if (ID_AA64PFR0_CSV2(id) >= ID_AA64PFR0_CSV2_IMPL) {
		printf("%sCSV2", sep);
		sep = ",";
	}
	if (ID_AA64PFR0_CSV2(id) >= ID_AA64PFR0_CSV2_SCXT)
		printf("+SCXT");
	if (ID_AA64PFR0_CSV2(id) >= ID_AA64PFR0_CSV2_HCXT)
		printf("+HCXT");

	if (ID_AA64PFR0_DIT(id) >= ID_AA64PFR0_DIT_IMPL) {
		printf("%sDIT", sep);
		sep = ",";
	}

	if (ID_AA64PFR0_AMU(id) >= ID_AA64PFR0_AMU_IMPL) {
		printf("%sAMU", sep);
		if (ID_AA64PFR0_AMU(id) >= ID_AA64PFR0_AMU_IMPL_V1P1)
			printf("v1p1");
		sep = ",";
	}

	if (ID_AA64PFR0_RAS(id) >= ID_AA64PFR0_RAS_IMPL) {
		printf("%sRAS", sep);
		if (ID_AA64PFR0_RAS(id) >= ID_AA64PFR0_RAS_IMPL_V1P1)
			printf("v1p1");
		sep = ",";
	}

	if (ID_AA64PFR0_SVE(id) >= ID_AA64PFR0_SVE_IMPL) {
		printf("%sSVE", sep);
		sep = ",";
	}

	if (ID_AA64PFR0_ADV_SIMD(id) != ID_AA64PFR0_ADV_SIMD_NONE &&
	    ID_AA64PFR0_ADV_SIMD(id) >= ID_AA64PFR0_ADV_SIMD_HP) {
		printf("%sAdvSIMD+HP", sep);
		sep = ",";
	}

	if (ID_AA64PFR0_FP(id) != ID_AA64PFR0_FP_NONE &&
	    ID_AA64PFR0_FP(id) >= ID_AA64PFR0_FP_HP) {
		printf("%sFP+HP", sep);
		sep = ",";
	}

	/*
	 * ID_AA64PFR1
	 */
	id = READ_SPECIALREG(id_aa64pfr1_el1);

	if (ID_AA64PFR1_BT(id) >= ID_AA64PFR1_BT_IMPL) {
		printf("%sBT", sep);
		sep = ",";
	}

	if (ID_AA64PFR1_SSBS(id) >= ID_AA64PFR1_SSBS_PSTATE) {
		printf("%sSSBS", sep);
		sep = ",";
	}
	if (ID_AA64PFR1_SSBS(id) >= ID_AA64PFR1_SSBS_PSTATE_MSR)
		printf("+MSR");

	if (ID_AA64PFR1_MTE(id) >= ID_AA64PFR1_MTE_IMPL) {
		printf("%sMTE", sep);
		sep = ",";
	}

	/*
	 * ID_AA64ZFR0
	 */
	id = READ_SPECIALREG(id_aa64zfr0_el1);
	if (id & ID_AA64ZFR0_MASK) {
		printf("\n%s: SVE", ci->ci_dev->dv_xname);
		if (ID_AA64ZFR0_SVEVER(id) >= ID_AA64ZFR0_SVEVER_SVE2)
			printf("2");
		if (ID_AA64ZFR0_SVEVER(id) >= ID_AA64ZFR0_SVEVER_SVE2P1)
			printf("p1");

		if (ID_AA64ZFR0_F64MM(id) >= ID_AA64ZFR0_F64MM_IMPL)
			printf(",F64MM");

		if (ID_AA64ZFR0_F32MM(id) >= ID_AA64ZFR0_F32MM_IMPL)
			printf(",F32MM");

		if (ID_AA64ZFR0_I8MM(id) >= ID_AA64ZFR0_I8MM_IMPL)
			printf(",I8MM");

		if (ID_AA64ZFR0_SM4(id) >= ID_AA64ZFR0_SM4_IMPL)
			printf(",SM4");

		if (ID_AA64ZFR0_SHA3(id) >= ID_AA64ZFR0_SHA3_IMPL)
			printf(",SHA3");

		if (ID_AA64ZFR0_BF16(id) >= ID_AA64ZFR0_BF16_BASE)
			printf(",BF16");
		if (ID_AA64ZFR0_BF16(id) >= ID_AA64ZFR0_BF16_EBF)
			printf("+EBF");

		if (ID_AA64ZFR0_BITPERM(id) >= ID_AA64ZFR0_BITPERM_IMPL)
			printf(",BitPerm");
		
		if (ID_AA64ZFR0_AES(id) >= ID_AA64ZFR0_AES_BASE)
			printf(",AES");
		if (ID_AA64ZFR0_AES(id) >= ID_AA64ZFR0_AES_PMULL)
			printf("+PMULL");
	}

	prev_id_aa64isar0 = READ_SPECIALREG(id_aa64isar0_el1);
	prev_id_aa64isar1 = READ_SPECIALREG(id_aa64isar1_el1);
	prev_id_aa64isar2 = READ_SPECIALREG(id_aa64isar2_el1);
	prev_id_aa64mmfr0 = READ_SPECIALREG(id_aa64mmfr0_el1);
	prev_id_aa64mmfr1 = READ_SPECIALREG(id_aa64mmfr1_el1);
	prev_id_aa64mmfr2 = READ_SPECIALREG(id_aa64mmfr2_el1);
	prev_id_aa64pfr0 = READ_SPECIALREG(id_aa64pfr0_el1);
	prev_id_aa64pfr1 = READ_SPECIALREG(id_aa64pfr1_el1);
	prev_id_aa64zfr0 = READ_SPECIALREG(id_aa64zfr0_el1);

#ifdef CPU_DEBUG
	id = READ_SPECIALREG(id_aa64afr0_el1);
	printf("\nID_AA64AFR0_EL1: 0x%016llx", id);
	id = READ_SPECIALREG(id_aa64afr1_el1);
	printf("\nID_AA64AFR1_EL1: 0x%016llx", id);
	id = READ_SPECIALREG(id_aa64dfr0_el1);
	printf("\nID_AA64DFR0_EL1: 0x%016llx", id);
	id = READ_SPECIALREG(id_aa64dfr1_el1);
	printf("\nID_AA64DFR1_EL1: 0x%016llx", id);
	id = READ_SPECIALREG(id_aa64isar0_el1);
	printf("\nID_AA64ISAR0_EL1: 0x%016llx", id);
	id = READ_SPECIALREG(id_aa64isar1_el1);
	printf("\nID_AA64ISAR1_EL1: 0x%016llx", id);
	id = READ_SPECIALREG(id_aa64isar2_el1);
	printf("\nID_AA64ISAR2_EL1: 0x%016llx", id);
	id = READ_SPECIALREG(id_aa64mmfr0_el1);
	printf("\nID_AA64MMFR0_EL1: 0x%016llx", id);
	id = READ_SPECIALREG(id_aa64mmfr1_el1);
	printf("\nID_AA64MMFR1_EL1: 0x%016llx", id);
	id = READ_SPECIALREG(id_aa64mmfr2_el1);
	printf("\nID_AA64MMFR2_EL1: 0x%016llx", id);
	id = READ_SPECIALREG(id_aa64pfr0_el1);
	printf("\nID_AA64PFR0_EL1: 0x%016llx", id);
	id = READ_SPECIALREG(id_aa64pfr1_el1);
	printf("\nID_AA64PFR1_EL1: 0x%016llx", id);
	id = READ_SPECIALREG(id_aa64zfr0_el1);
	printf("\nID_AA64ZFR0_EL1: 0x%016llx", id);
#endif
}

void
cpu_identify_cleanup(void)
{
	uint64_t id_aa64mmfr2;
	uint64_t value;

	/* ID_AA64ISAR0_EL1 */
	value = cpu_id_aa64isar0 & ID_AA64ISAR0_MASK;
	value &= ~ID_AA64ISAR0_TLB_MASK;
	cpu_id_aa64isar0 = value;

	/* ID_AA64ISAR1_EL1 */
	value = cpu_id_aa64isar1 & ID_AA64ISAR1_MASK;
	value &= ~ID_AA64ISAR1_SPECRES_MASK;
	cpu_id_aa64isar1 = value;

	/* ID_AA64ISAR2_EL1 */
	value = cpu_id_aa64isar2 & ID_AA64ISAR2_MASK;
	value &= ~ID_AA64ISAR2_CLRBHB_MASK;
	cpu_id_aa64isar2 = value;

	/* ID_AA64MMFR0_EL1 */
	value = 0;
	value |= cpu_id_aa64mmfr0 & ID_AA64MMFR0_ECV_MASK;
	cpu_id_aa64mmfr0 = value;

	/* ID_AA64MMFR1_EL1 */
	value = 0;
	value |= cpu_id_aa64mmfr1 & ID_AA64MMFR1_AFP_MASK;
	cpu_id_aa64mmfr1 = value;

	/* ID_AA64MMFR2_EL1 */
	value = 0;
	value |= cpu_id_aa64mmfr2 & ID_AA64MMFR2_AT_MASK;
	cpu_id_aa64mmfr2 = value;

	/* ID_AA64PFR0_EL1 */
	value = 0;
	value |= cpu_id_aa64pfr0 & ID_AA64PFR0_FP_MASK;
	value |= cpu_id_aa64pfr0 & ID_AA64PFR0_ADV_SIMD_MASK;
	value |= cpu_id_aa64pfr0 & ID_AA64PFR0_SVE_MASK;
	value |= cpu_id_aa64pfr0 & ID_AA64PFR0_DIT_MASK;
	cpu_id_aa64pfr0 = value;

	/* ID_AA64PFR1_EL1 */
	value = 0;
	value |= cpu_id_aa64pfr1 & ID_AA64PFR1_BT_MASK;
	value |= cpu_id_aa64pfr1 & ID_AA64PFR1_SSBS_MASK;
	cpu_id_aa64pfr1 = value;

	/* ID_AA64ZFR0_EL1 */
	value = cpu_id_aa64zfr0 & ID_AA64ZFR0_MASK;
	cpu_id_aa64zfr0 = value;

	/* HWCAP */
	hwcap |= HWCAP_FP;	/* OpenBSD assumes Floating-point support */
	hwcap |= HWCAP_ASIMD;	/* OpenBSD assumes Advanced SIMD support */
	/* HWCAP_EVTSTRM: OpenBSD kernel doesn't configure event stream */
	if (ID_AA64ISAR0_AES(cpu_id_aa64isar0) >= ID_AA64ISAR0_AES_BASE)
		hwcap |= HWCAP_AES;
	if (ID_AA64ISAR0_AES(cpu_id_aa64isar0) >= ID_AA64ISAR0_AES_PMULL)
		hwcap |= HWCAP_PMULL;
	if (ID_AA64ISAR0_SHA1(cpu_id_aa64isar0) >= ID_AA64ISAR0_SHA1_BASE)
		hwcap |= HWCAP_SHA1;
	if (ID_AA64ISAR0_SHA2(cpu_id_aa64isar0) >= ID_AA64ISAR0_SHA2_BASE)
		hwcap |= HWCAP_SHA2;
	if (ID_AA64ISAR0_CRC32(cpu_id_aa64isar0) >= ID_AA64ISAR0_CRC32_BASE)
		hwcap |= HWCAP_CRC32;
	if (ID_AA64ISAR0_ATOMIC(cpu_id_aa64isar0) >= ID_AA64ISAR0_ATOMIC_IMPL)
		hwcap |= HWCAP_ATOMICS;
	if (ID_AA64PFR0_FP(cpu_id_aa64pfr0) != ID_AA64PFR0_FP_NONE &&
	    ID_AA64PFR0_FP(cpu_id_aa64pfr0) >= ID_AA64PFR0_FP_HP)
		hwcap |= HWCAP_FPHP;
	if (ID_AA64PFR0_ADV_SIMD(cpu_id_aa64pfr0) != ID_AA64PFR0_ADV_SIMD_NONE &&
	    ID_AA64PFR0_ADV_SIMD(cpu_id_aa64pfr0) >= ID_AA64PFR0_ADV_SIMD_HP)
		hwcap |= HWCAP_ASIMDHP;
	id_aa64mmfr2 = READ_SPECIALREG(id_aa64mmfr2_el1);
	if (ID_AA64MMFR2_IDS(id_aa64mmfr2) >= ID_AA64MMFR2_IDS_IMPL)
		hwcap |= HWCAP_CPUID;
	if (ID_AA64ISAR0_RDM(cpu_id_aa64isar0) >= ID_AA64ISAR0_RDM_IMPL)
		hwcap |= HWCAP_ASIMDRDM;
	if (ID_AA64ISAR1_JSCVT(cpu_id_aa64isar1) >= ID_AA64ISAR1_JSCVT_IMPL)
		hwcap |= HWCAP_JSCVT;
	if (ID_AA64ISAR1_FCMA(cpu_id_aa64isar1) >= ID_AA64ISAR1_FCMA_IMPL)
		hwcap |= HWCAP_FCMA;
	if (ID_AA64ISAR1_LRCPC(cpu_id_aa64isar1) >= ID_AA64ISAR1_LRCPC_BASE)
		hwcap |= HWCAP_LRCPC;
	if (ID_AA64ISAR1_DPB(cpu_id_aa64isar1) >= ID_AA64ISAR1_DPB_IMPL)
		hwcap |= HWCAP_DCPOP;
	if (ID_AA64ISAR0_SHA3(cpu_id_aa64isar0) >= ID_AA64ISAR0_SHA3_IMPL)
		hwcap |= HWCAP_SHA3;
	if (ID_AA64ISAR0_SM3(cpu_id_aa64isar0) >= ID_AA64ISAR0_SM3_IMPL)
		hwcap |= HWCAP_SM3;
	if (ID_AA64ISAR0_SM4(cpu_id_aa64isar0) >= ID_AA64ISAR0_SM4_IMPL)
		hwcap |= HWCAP_SM4;
	if (ID_AA64ISAR0_DP(cpu_id_aa64isar0) >= ID_AA64ISAR0_DP_IMPL)
		hwcap |= HWCAP_ASIMDDP;
	if (ID_AA64ISAR0_SHA2(cpu_id_aa64isar0) >= ID_AA64ISAR0_SHA2_512)
		hwcap |= HWCAP_SHA512;
	if (ID_AA64PFR0_SVE(cpu_id_aa64pfr0) >= ID_AA64PFR0_SVE_IMPL)
		hwcap |= HWCAP_SVE;
	if (ID_AA64ISAR0_FHM(cpu_id_aa64isar0) >= ID_AA64ISAR0_FHM_IMPL)
		hwcap |= HWCAP_ASIMDFHM;
	if (ID_AA64PFR0_DIT(cpu_id_aa64pfr0) >= ID_AA64PFR0_DIT_IMPL)
		hwcap |= HWCAP_DIT;
	if (ID_AA64MMFR2_AT(cpu_id_aa64mmfr2) >= ID_AA64MMFR2_AT_IMPL)
		hwcap |= HWCAP_USCAT;
	if (ID_AA64ISAR1_LRCPC(cpu_id_aa64isar1) >= ID_AA64ISAR1_LRCPC_LDAPUR)
		hwcap |= HWCAP_ILRCPC;
	if (ID_AA64ISAR0_TS(cpu_id_aa64isar0) >= ID_AA64ISAR0_TS_BASE)
		hwcap |= HWCAP_FLAGM;
	if (ID_AA64PFR1_SSBS(cpu_id_aa64pfr1) >= ID_AA64PFR1_SSBS_PSTATE_MSR)
		hwcap |= HWCAP_SSBS;
	if (ID_AA64ISAR1_SB(cpu_id_aa64isar1) >= ID_AA64ISAR1_SB_IMPL)
		hwcap |= HWCAP_SB;
	if (ID_AA64ISAR1_APA(cpu_id_aa64isar1) >= ID_AA64ISAR1_APA_PAC ||
	    ID_AA64ISAR1_API(cpu_id_aa64isar1) >= ID_AA64ISAR1_API_PAC ||
	    ID_AA64ISAR2_APA3(cpu_id_aa64isar2) >= ID_AA64ISAR2_APA3_PAC)
		hwcap |= HWCAP_PACA;
	if (ID_AA64ISAR1_GPA(cpu_id_aa64isar1) >= ID_AA64ISAR1_GPA_IMPL ||
	    ID_AA64ISAR1_GPI(cpu_id_aa64isar1) >= ID_AA64ISAR1_GPI_IMPL ||
	    ID_AA64ISAR2_GPA3(cpu_id_aa64isar2) >= ID_AA64ISAR2_GPA3_IMPL)
		hwcap |= HWCAP_PACG;

	/* HWCAP2 */
	if (ID_AA64ISAR1_DPB(cpu_id_aa64isar1) >= ID_AA64ISAR1_DPB_DCCVADP)
		hwcap2 |= HWCAP2_DCPODP;
	if (ID_AA64ZFR0_SVEVER(cpu_id_aa64zfr0) >= ID_AA64ZFR0_SVEVER_SVE2)
		hwcap2 |= HWCAP2_SVE2;
	if (ID_AA64ZFR0_AES(cpu_id_aa64zfr0) >= ID_AA64ZFR0_AES_BASE)
		hwcap2 |= HWCAP2_SVEAES;
	if (ID_AA64ZFR0_AES(cpu_id_aa64zfr0) >= ID_AA64ZFR0_AES_PMULL)
		hwcap2 |= HWCAP2_SVEPMULL;
	if (ID_AA64ZFR0_BITPERM(cpu_id_aa64zfr0) >= ID_AA64ZFR0_BITPERM_IMPL)
		hwcap2 |= HWCAP2_SVEBITPERM;
	if (ID_AA64ZFR0_SHA3(cpu_id_aa64zfr0) >= ID_AA64ZFR0_SHA3_IMPL)
		hwcap2 |= HWCAP2_SVESHA3;
	if (ID_AA64ZFR0_SM4(cpu_id_aa64zfr0) >= ID_AA64ZFR0_SM4_IMPL)
		hwcap2 |= HWCAP2_SVESM4;
	if (ID_AA64ISAR0_TS(cpu_id_aa64isar0) >= ID_AA64ISAR0_TS_AXFLAG)
		hwcap2 |= HWCAP2_FLAGM2;
	if (ID_AA64ISAR1_FRINTTS(cpu_id_aa64isar1) >= ID_AA64ISAR1_FRINTTS_IMPL)
		hwcap2 |= HWCAP2_FRINT;
	if (ID_AA64ZFR0_I8MM(cpu_id_aa64zfr0) >= ID_AA64ZFR0_I8MM_IMPL)
		hwcap2 |= HWCAP2_SVEI8MM;
	if (ID_AA64ZFR0_F32MM(cpu_id_aa64zfr0) >= ID_AA64ZFR0_F32MM_IMPL)
		hwcap2 |= HWCAP2_SVEF32MM;
	if (ID_AA64ZFR0_F64MM(cpu_id_aa64zfr0) >= ID_AA64ZFR0_F64MM_IMPL)
		hwcap2 |= HWCAP2_SVEF64MM;
	if (ID_AA64ZFR0_BF16(cpu_id_aa64zfr0) >= ID_AA64ZFR0_BF16_BASE)
		hwcap2 |= HWCAP2_SVEBF16;
	if (ID_AA64ISAR1_I8MM(cpu_id_aa64isar1) >= ID_AA64ISAR1_I8MM_IMPL)
		hwcap2 |= HWCAP2_I8MM;
	if (ID_AA64ISAR1_BF16(cpu_id_aa64isar1) >= ID_AA64ISAR1_BF16_BASE)
		hwcap2 |= HWCAP2_BF16;
	if (ID_AA64ISAR1_DGH(cpu_id_aa64isar1) >= ID_AA64ISAR1_DGH_IMPL)
		hwcap2 |= HWCAP2_DGH;
	if (ID_AA64ISAR0_RNDR(cpu_id_aa64isar0) >= ID_AA64ISAR0_RNDR_IMPL)
		hwcap2 |= HWCAP2_RNG;
	if (ID_AA64PFR1_BT(cpu_id_aa64pfr1) >= ID_AA64PFR1_BT_IMPL)
		hwcap2 |= HWCAP2_BTI;
	/* HWCAP2_MTE: OpenBSD kernel doesn't provide MTE support */
	if (ID_AA64MMFR0_ECV(cpu_id_aa64mmfr0) >= ID_AA64MMFR0_ECV_IMPL)
		hwcap2 |= HWCAP2_ECV;
	if (ID_AA64MMFR1_AFP(cpu_id_aa64mmfr1) >= ID_AA64MMFR1_AFP_IMPL)
		hwcap2 |= HWCAP2_AFP;
	if (ID_AA64ISAR2_RPRES(cpu_id_aa64isar2) >= ID_AA64ISAR2_RPRES_IMPL)
		hwcap2 |= HWCAP2_RPRES;
	/* HWCAP2_MTE3: OpenBSD kernel doesn't provide MTE support */
	/* HWCAP2_SME: OpenBSD kernel doesn't provide SME support */
	/* HWCAP2_SME_I16I64: OpenBSD kernel doesn't provide SME support */
	/* HWCAP2_SME_F64F64: OpenBSD kernel doesn't provide SME support */
	/* HWCAP2_SME_I8I32: OpenBSD kernel doesn't provide SME support */
	/* HWCAP2_SME_F16F32: OpenBSD kernel doesn't provide SME support */
	/* HWCAP2_SME_B16F32: OpenBSD kernel doesn't provide SME support */
	/* HWCAP2_SME_F32F32: OpenBSD kernel doesn't provide SME support */
	/* HWCAP2_SME_FA64: OpenBSD kernel doesn't provide SME support */
	if (ID_AA64ISAR2_WFXT(cpu_id_aa64isar2) >= ID_AA64ISAR2_WFXT_IMPL)
		hwcap2 |= HWCAP2_WFXT;
	if (ID_AA64ISAR1_BF16(cpu_id_aa64isar1) >= ID_AA64ISAR1_BF16_EBF)
		hwcap2 |= HWCAP2_EBF16;
	if (ID_AA64ZFR0_BF16(cpu_id_aa64zfr0) >= ID_AA64ZFR0_BF16_EBF)
		hwcap2 |= HWCAP2_SVE_EBF16;
	if (ID_AA64ISAR2_CSSC(cpu_id_aa64isar2) >= ID_AA64ISAR2_CSSC_IMPL)
		hwcap2 |= HWCAP2_CSSC;
	if (ID_AA64ISAR2_RPRFM(cpu_id_aa64isar2) >= ID_AA64ISAR2_RPRFM_IMPL)
		hwcap2 |= HWCAP2_RPRFM;
	if (ID_AA64ZFR0_SVEVER(cpu_id_aa64zfr0) >= ID_AA64ZFR0_SVEVER_SVE2P1)
		hwcap2 |= HWCAP2_SVE2P1;
	/* HWCAP2_SME2: OpenBSD kernel doesn't provide SME support */
	/* HWCAP2_SME2P1: OpenBSD kernel doesn't provide SME support */
	/* HWCAP2_SME_I16I32: OpenBSD kernel doesn't provide SME support */
	/* HWCAP2_SME_BI32I32: OpenBSD kernel doesn't provide SME support */
	/* HWCAP2_SME_B16B16: OpenBSD kernel doesn't provide SME support */
	/* HWCAP2_SME_F16F16: OpenBSD kernel doesn't provide SME support */
	if (ID_AA64ISAR2_MOPS(cpu_id_aa64isar2) >= ID_AA64ISAR2_MOPS_IMPL)
		hwcap2 |= HWCAP2_MOPS;
	if (ID_AA64ISAR2_BC(cpu_id_aa64isar2) >= ID_AA64ISAR2_BC_IMPL)
		hwcap2 |= HWCAP2_HBC;
}

void	cpu_init(void);
int	cpu_start_secondary(struct cpu_info *ci, int, uint64_t);
int	cpu_clockspeed(int *);

int
cpu_match(struct device *parent, void *cfdata, void *aux)
{
	struct fdt_attach_args *faa = aux;
	uint64_t mpidr = READ_SPECIALREG(mpidr_el1);
	char buf[32];

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
	void *kstack;
#ifdef MULTIPROCESSOR
	uint64_t mpidr = READ_SPECIALREG(mpidr_el1);
#endif
	uint32_t opp;

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

	kstack = km_alloc(USPACE, &kv_any, &kp_zero, &kd_waitok);
	ci->ci_el1_stkend = (vaddr_t)kstack + USPACE - 16;
	ci->ci_trampoline_vectors = (vaddr_t)trampoline_vectors_none;

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
		if (cpu_start_secondary(ci, spinup_method, spinup_data)) {
			atomic_setbits_int(&ci->ci_flags, CPUF_IDENTIFY);
			__asm volatile("dsb sy; sev" ::: "memory");

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
		cpu_id_aa64isar0 = READ_SPECIALREG(id_aa64isar0_el1);
		cpu_id_aa64isar1 = READ_SPECIALREG(id_aa64isar1_el1);
		cpu_id_aa64isar2 = READ_SPECIALREG(id_aa64isar2_el1);
		cpu_id_aa64mmfr0 = READ_SPECIALREG(id_aa64mmfr0_el1);
		cpu_id_aa64mmfr1 = READ_SPECIALREG(id_aa64mmfr1_el1);
		cpu_id_aa64mmfr2 = READ_SPECIALREG(id_aa64mmfr2_el1);
		cpu_id_aa64pfr0 = READ_SPECIALREG(id_aa64pfr0_el1);
		cpu_id_aa64pfr1 = READ_SPECIALREG(id_aa64pfr1_el1);
		cpu_id_aa64zfr0 = READ_SPECIALREG(id_aa64zfr0_el1);

		/*
		 * The SpecSEI "feature" isn't relevant for userland.
		 * So it is fine if this field differs between CPU
		 * cores.  Mask off this field to prevent exporting it
		 * to userland.
		 */
		cpu_id_aa64mmfr1 &= ~ID_AA64MMFR1_SPECSEI_MASK;

		/*
		 * The CSV2/CSV3 "features" are handled on a
		 * per-processor basis.  So it is fine if these fields
		 * differ between CPU cores.  Mask off these fields to
		 * prevent exporting these to userland.
		 */
		cpu_id_aa64pfr0 &= ~ID_AA64PFR0_CSV2_MASK;
		cpu_id_aa64pfr0 &= ~ID_AA64PFR0_CSV3_MASK;

		/*
		 * We only support 64-bit mode, so we don't care about
		 * differences in support for 32-bit mode between
		 * cores.  Mask off these fields as well.
		 */
		cpu_id_aa64pfr0 &= ~ID_AA64PFR0_EL0_MASK;
		cpu_id_aa64pfr0 &= ~ID_AA64PFR0_EL1_MASK;
		cpu_id_aa64pfr0 &= ~ID_AA64PFR0_EL2_MASK;
		cpu_id_aa64pfr0 &= ~ID_AA64PFR0_EL3_MASK;

		/*
		 * Lenovo X13s ships with broken EL2 firmware that
		 * hangs the machine if we enable PAuth.
		 */
		if (hw_vendor && hw_prod && strcmp(hw_vendor, "LENOVO") == 0) {
			if (strncmp(hw_prod, "21BX", 4) == 0 ||
			    strncmp(hw_prod, "21BY", 4) == 0) {
				cpu_id_aa64isar1 &= ~ID_AA64ISAR1_APA_MASK;
				cpu_id_aa64isar1 &= ~ID_AA64ISAR1_GPA_MASK;
			}
		}

		cpu_identify(ci);

		if (OF_getproplen(ci->ci_node, "clocks") > 0) {
			cpu_node = ci->ci_node;
			cpu_cpuspeed = cpu_clockspeed;
		}

		cpu_init();

		if (arm64_has_rng) {
			timeout_set(&cpu_rng_to, cpu_rng, &cpu_rng_to);
			cpu_rng(&cpu_rng_to);
		}
#ifdef MULTIPROCESSOR
	}
#endif

#if NKSTAT > 0
	cpu_kstat_attach(ci);
#endif

	opp = OF_getpropint(ci->ci_node, "operating-points-v2", 0);
	if (opp)
		cpu_opp_init(ci, opp);

	cpu_psci_init(ci);

	printf("\n");
}

void
cpu_init(void)
{
	uint64_t id_aa64mmfr1, sctlr;
	uint64_t id_aa64pfr0;
	uint64_t cpacr;
	uint64_t tcr;

	WRITE_SPECIALREG(ttbr0_el1, pmap_kernel()->pm_pt0pa);
	__asm volatile("isb");
	tcr = READ_SPECIALREG(tcr_el1);
	tcr &= ~TCR_T0SZ(0x3f);
	tcr |= TCR_T0SZ(64 - USER_SPACE_BITS);
	tcr |= TCR_A1;
	WRITE_SPECIALREG(tcr_el1, tcr);
	cpu_tlb_flush();

	/* Enable PAN. */
	id_aa64mmfr1 = READ_SPECIALREG(id_aa64mmfr1_el1);
	if (ID_AA64MMFR1_PAN(id_aa64mmfr1) >= ID_AA64MMFR1_PAN_IMPL) {
		sctlr = READ_SPECIALREG(sctlr_el1);
		sctlr &= ~SCTLR_SPAN;
		if (ID_AA64MMFR1_PAN(id_aa64mmfr1) >= ID_AA64MMFR1_PAN_EPAN)
			sctlr |= SCTLR_EPAN;
		WRITE_SPECIALREG(sctlr_el1, sctlr);
	}

	/* Enable DIT. */
	id_aa64pfr0 = READ_SPECIALREG(id_aa64pfr0_el1);
	if (ID_AA64PFR0_DIT(id_aa64pfr0) >= ID_AA64PFR0_DIT_IMPL)
		__asm volatile (".arch armv8.4-a; msr dit, #1");

	/* Enable PAuth. */
	if (ID_AA64ISAR1_APA(cpu_id_aa64isar1) >= ID_AA64ISAR1_APA_PAC ||
	    ID_AA64ISAR1_API(cpu_id_aa64isar1) >= ID_AA64ISAR1_API_PAC ||
	    ID_AA64ISAR2_APA3(cpu_id_aa64isar2) >= ID_AA64ISAR2_APA3_PAC) {
		sctlr = READ_SPECIALREG(sctlr_el1);
		sctlr |= SCTLR_EnIA | SCTLR_EnDA;
		sctlr |= SCTLR_EnIB | SCTLR_EnDB;
		WRITE_SPECIALREG(sctlr_el1, sctlr);
	}

	/* Enable strict BTI compatibility for PACIASP and PACIBSP. */
	if (ID_AA64PFR1_BT(cpu_id_aa64pfr1) >= ID_AA64PFR1_BT_IMPL) {
		sctlr = READ_SPECIALREG(sctlr_el1);
		sctlr |= SCTLR_BT0 | SCTLR_BT1;
		WRITE_SPECIALREG(sctlr_el1, sctlr);
	}

	/* Setup SVE with the default 128-bit vector length. */
	if (ID_AA64PFR0_SVE(cpu_id_aa64pfr0) >= ID_AA64PFR0_SVE_IMPL) {
		cpacr = READ_SPECIALREG(cpacr_el1);
		cpacr &= ~CPACR_ZEN_MASK;
		cpacr |= CPACR_ZEN_TRAP_EL0;
		WRITE_SPECIALREG(cpacr_el1, cpacr);
		__asm volatile ("isb");
		WRITE_SPECIALREG(zcr_el1, 0);
		cpacr &= ~CPACR_ZEN_MASK;
		cpacr |= CPACR_ZEN_TRAP_ALL1;
		WRITE_SPECIALREG(cpacr_el1, cpacr);
		__asm volatile ("isb");
	}

	/* Initialize debug registers. */
	WRITE_SPECIALREG(mdscr_el1, DBG_MDSCR_TDCC);
	WRITE_SPECIALREG(oslar_el1, 0);
}

void
cpu_flush_bp_noop(void)
{
}

void
cpu_flush_bp_psci(void)
{
#if NPSCI > 0
	psci_flush_bp();
#endif
}

void
cpu_serror_apple(void)
{
	__asm volatile("dsb sy; isb" ::: "memory");
	printf("l2c_err_sts 0x%llx\n", READ_SPECIALREG(s3_3_c15_c8_0));
	printf("l2c_err_adr 0x%llx\n", READ_SPECIALREG(s3_3_c15_c9_0));
	printf("l2c_err_inf 0x%llx\n", READ_SPECIALREG(s3_3_c15_c10_0));
}

int
cpu_clockspeed(int *freq)
{
	*freq = clock_get_frequency(cpu_node, NULL) / 1000000;
	return 0;
}

#ifdef MULTIPROCESSOR

void cpu_boot_secondary(struct cpu_info *ci);
void cpu_hatch_secondary(void);
void cpu_hatch_secondary_spin(void);

void cpu_suspend_cycle(void);

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

void
cpu_start_spin_table(struct cpu_info *ci, uint64_t start, uint64_t data)
{
	extern paddr_t cpu_hatch_ci;

	pmap_extract(pmap_kernel(), (vaddr_t)ci, &cpu_hatch_ci);
	cpu_dcache_wb_range((vaddr_t)&cpu_hatch_ci, sizeof(paddr_t));

	/* this reuses the zero page for the core */
	vaddr_t start_pg = zero_page + (PAGE_SIZE * ci->ci_cpuid);
	paddr_t pa = trunc_page(data);
	uint64_t offset = data - pa;
	uint64_t *startvec = (uint64_t *)(start_pg + offset);

	pmap_kenter_cache(start_pg, pa, PROT_READ|PROT_WRITE, PMAP_CACHE_CI);

	*startvec = start;
	__asm volatile("dsb sy; sev" ::: "memory");

	pmap_kremove(start_pg, PAGE_SIZE);
}

int
cpu_start_secondary(struct cpu_info *ci, int method, uint64_t data)
{
	vaddr_t start_va;
	paddr_t ci_pa, start_pa;
	uint64_t ttbr1;
	int32_t status;

	__asm("mrs %x0, ttbr1_el1": "=r"(ttbr1));
	ci->ci_ttbr1 = ttbr1;
	cpu_dcache_wb_range((vaddr_t)ci, sizeof(*ci));

	switch (method) {
#if NPSCI > 0
	case 1:
		/* psci */
		start_va = (vaddr_t)cpu_hatch_secondary;
		pmap_extract(pmap_kernel(), start_va, &start_pa);
		pmap_extract(pmap_kernel(), (vaddr_t)ci, &ci_pa);
		status = psci_cpu_on(ci->ci_mpidr, start_pa, ci_pa);
		return (status == PSCI_SUCCESS);
#endif
	case 2:
		/* spin-table */
		start_va = (vaddr_t)cpu_hatch_secondary_spin;
		pmap_extract(pmap_kernel(), start_va, &start_pa);
		cpu_start_spin_table(ci, start_pa, data);
		return 1;
	}

	return 0;
}

void
cpu_boot_secondary(struct cpu_info *ci)
{
	atomic_setbits_int(&ci->ci_flags, CPUF_GO);
	__asm volatile("dsb sy; sev" ::: "memory");

	/*
	 * Send an interrupt as well to make sure the CPU wakes up
	 * regardless of whether it is in a WFE or a WFI loop.
	 */
	arm_send_ipi(ci, ARM_IPI_NOP);

	while ((ci->ci_flags & CPUF_RUNNING) == 0)
		__asm volatile("wfe");
}

void
cpu_init_secondary(struct cpu_info *ci)
{
	struct proc *p;
	struct pcb *pcb;
	struct trapframe *tf;
	struct switchframe *sf;
	int s;

	ci->ci_flags |= CPUF_PRESENT;
	__asm volatile("dsb sy" ::: "memory");

	if ((ci->ci_flags & CPUF_IDENTIFIED) == 0) {
		while ((ci->ci_flags & CPUF_IDENTIFY) == 0)
			__asm volatile("wfe");

		cpu_identify(ci);
		atomic_setbits_int(&ci->ci_flags, CPUF_IDENTIFIED);
		__asm volatile("dsb sy" ::: "memory");
	}

	while ((ci->ci_flags & CPUF_GO) == 0)
		__asm volatile("wfe");

	cpu_init();

	/*
	 * Start from a clean slate regardless of whether this is the
	 * initial power up or a wakeup of a suspended CPU.
	 */

	ci->ci_curproc = NULL;
	ci->ci_curpcb = NULL;
	ci->ci_curpm = NULL;
	ci->ci_cpl = IPL_NONE;
	ci->ci_ipending = 0;
	ci->ci_idepth = 0;

#ifdef DIAGNOSTIC
	ci->ci_mutex_level = 0;
#endif

	/*
	 * Re-create the switchframe for this CPUs idle process.
	 */

	p = ci->ci_schedstate.spc_idleproc;
	pcb = &p->p_addr->u_pcb;

	tf = (struct trapframe *)((u_long)p->p_addr
	    + USPACE
	    - sizeof(struct trapframe)
	    - 0x10);

	tf = (struct trapframe *)STACKALIGN(tf);
	pcb->pcb_tf = tf;

	sf = (struct switchframe *)tf - 1;
	sf->sf_x19 = (uint64_t)sched_idle;
	sf->sf_x20 = (uint64_t)ci;
	sf->sf_lr = (uint64_t)proc_trampoline;
	pcb->pcb_sp = (uint64_t)sf;

	s = splhigh();
	arm_intr_cpu_enable();
	cpu_startclock();

	atomic_setbits_int(&ci->ci_flags, CPUF_RUNNING);
	__asm volatile("dsb sy; sev" ::: "memory");

	spllower(IPL_NONE);

	sched_toidle();
}

void
cpu_halt(void)
{
	struct cpu_info *ci = curcpu();
	vaddr_t start_va;
	paddr_t ci_pa, start_pa;
	int count = 0;
	u_long psw;
	int32_t status;

	KERNEL_ASSERT_UNLOCKED();
	SCHED_ASSERT_UNLOCKED();

	start_va = (vaddr_t)cpu_hatch_secondary;
	pmap_extract(pmap_kernel(), start_va, &start_pa);
	pmap_extract(pmap_kernel(), (vaddr_t)ci, &ci_pa);

	psw = intr_disable();

	atomic_clearbits_int(&ci->ci_flags,
	    CPUF_RUNNING | CPUF_PRESENT | CPUF_GO);

#if NPSCI > 0
	if (psci_can_suspend())
		psci_cpu_off();
#endif

	/*
	 * If we failed to turn ourselves off using PSCI, declare that
	 * we're still present and spin in a low power state until
	 * we're told to wake up again by the primary CPU.
	 */

	atomic_setbits_int(&ci->ci_flags, CPUF_PRESENT);

	/* Mask clock interrupts. */
	WRITE_SPECIALREG(cntv_ctl_el0,
	    READ_SPECIALREG(cntv_ctl_el0) | CNTV_CTL_IMASK);

	while ((ci->ci_flags & CPUF_GO) == 0) {
#if NPSCI > 0
		if (ci->ci_psci_suspend_param) {
			status = psci_cpu_suspend(ci->ci_psci_suspend_param,
			    start_pa, ci_pa);
			if (status != PSCI_SUCCESS)
				ci->ci_psci_suspend_param = 0;
		} else
#endif
			cpu_suspend_cycle();
		count++;
	}

	atomic_setbits_int(&ci->ci_flags, CPUF_RUNNING);
	__asm volatile("dsb sy; sev" ::: "memory");

	intr_restore(psw);

	/* Unmask clock interrupts. */
	WRITE_SPECIALREG(cntv_ctl_el0,
	    READ_SPECIALREG(cntv_ctl_el0) & ~CNTV_CTL_IMASK);
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

#endif

int cpu_suspended;

#ifdef SUSPEND

void cpu_hatch_primary(void);

void (*cpu_suspend_cycle_fcn)(void) = cpu_wfi;
label_t cpu_suspend_jmpbuf;

void
cpu_suspend_cycle(void)
{
	cpu_suspend_cycle_fcn();
}

void
cpu_init_primary(void)
{
	cpu_init();

	cpu_startclock();

	longjmp(&cpu_suspend_jmpbuf);
}

int
cpu_suspend_primary(void)
{
	struct cpu_info *ci = curcpu();
	vaddr_t start_va;
	paddr_t ci_pa, start_pa;
	uint64_t ttbr1;
	int32_t status;
	int count = 0;

	__asm("mrs %x0, ttbr1_el1": "=r"(ttbr1));
	ci->ci_ttbr1 = ttbr1;
	cpu_dcache_wb_range((vaddr_t)ci, sizeof(*ci));

	start_va = (vaddr_t)cpu_hatch_primary;
	pmap_extract(pmap_kernel(), start_va, &start_pa);
	pmap_extract(pmap_kernel(), (vaddr_t)ci, &ci_pa);

#if NPSCI > 0
	if (psci_can_suspend()) {
		if (setjmp(&cpu_suspend_jmpbuf)) {
			/* XXX wait for debug output on Allwinner A64 */
			delay(200000);
			return 0;
		}

		psci_system_suspend(start_pa, ci_pa);

		return EOPNOTSUPP;
	}
#endif

	if (setjmp(&cpu_suspend_jmpbuf))
		goto resume;

	/*
	 * If PSCI doesn't support SYSTEM_SUSPEND, spin in a low power
	 * state waiting for an interrupt that wakes us up again.
	 */

	/* Mask clock interrupts. */
	WRITE_SPECIALREG(cntv_ctl_el0,
	    READ_SPECIALREG(cntv_ctl_el0) | CNTV_CTL_IMASK);

	/*
	 * All non-wakeup interrupts should be masked at this point;
	 * re-enable interrupts such that wakeup interrupts actually
	 * wake us up.  Set a flag such that drivers can tell we're
	 * suspended and change their behaviour accordingly.  They can
	 * wake us up by clearing the flag.
	 */
	cpu_suspended = 1;
	arm_intr_func.setipl(IPL_NONE);
	intr_enable();

	while (cpu_suspended) {
#if NPSCI > 0
		if (ci->ci_psci_suspend_param) {
			status = psci_cpu_suspend(ci->ci_psci_suspend_param,
			    start_pa, ci_pa);
			if (status != PSCI_SUCCESS)
				ci->ci_psci_suspend_param = 0;
		} else
#endif
			cpu_suspend_cycle();
		count++;
	}

resume:
	intr_disable();
	arm_intr_func.setipl(IPL_HIGH);

	/* Unmask clock interrupts. */
	WRITE_SPECIALREG(cntv_ctl_el0,
	    READ_SPECIALREG(cntv_ctl_el0) & ~CNTV_CTL_IMASK);

	return 0;
}

#ifdef MULTIPROCESSOR

void
cpu_resume_secondary(struct cpu_info *ci)
{
	int timeout = 10000;

	if (ci->ci_flags & CPUF_PRESENT)
		return;

	cpu_start_secondary(ci, 1, 0);
	while ((ci->ci_flags & CPUF_PRESENT) == 0 && --timeout)
		delay(1000);
	if (timeout == 0) {
		printf("%s: failed to spin up\n",
		    ci->ci_dev->dv_xname);
		ci->ci_flags = 0;
	}
}

#endif

#endif

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

#if NKSTAT > 0
		cpu_opp_kstat_attach(ci);
#endif

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


void
cpu_psci_init(struct cpu_info *ci)
{
	uint32_t *domains;
	uint32_t *domain;
	uint32_t *states;
	uint32_t ncells;
	uint32_t cluster;
	int idx, len, node;

	/*
	 * Find the shallowest (for now) idle state for this CPU.
	 * This should be the first one that is listed.  We'll use it
	 * in the idle loop.
	 */

	len = OF_getproplen(ci->ci_node, "cpu-idle-states");
	if (len < (int)sizeof(uint32_t))
		return;

	states = malloc(len, M_TEMP, M_WAITOK);
	OF_getpropintarray(ci->ci_node, "cpu-idle-states", states, len);
	node = OF_getnodebyphandle(states[0]);
	free(states, M_TEMP, len);
	if (node) {
		uint32_t entry, exit, residency, param;
		int32_t features;

		param = OF_getpropint(node, "arm,psci-suspend-param", 0);
		entry = OF_getpropint(node, "entry-latency-us", 0);
		exit = OF_getpropint(node, "exit-latency-us", 0);
		residency = OF_getpropint(node, "min-residency-us", 0);
		ci->ci_psci_idle_latency += entry + exit + 2 * residency;

		/* Skip states that stop the local timer. */
		if (OF_getpropbool(node, "local-timer-stop"))
			ci->ci_psci_idle_param = 0;

		/* Skip powerdown states. */
		features = psci_features(CPU_SUSPEND);
		if (features == PSCI_NOT_SUPPORTED ||
		    (features & PSCI_FEATURE_POWER_STATE_EXT) == 0) {
			if (param & PSCI_POWER_STATE_POWERDOWN)
				param = 0;
		} else {
			if (param & PSCI_POWER_STATE_EXT_POWERDOWN)
				param = 0;
		}

		if (param) {
			ci->ci_psci_idle_param = param;
			cpu_idle_cycle_fcn = cpu_psci_idle_cycle;
		}
	}

	/*
	 * Hunt for the deepest idle state for this CPU.  This is
	 * fairly complicated as it requires traversing quite a few
	 * nodes in the device tree.  The first step is to look up the
	 * "psci" power domain for this CPU.
	 */

	idx = OF_getindex(ci->ci_node, "psci", "power-domain-names");
	if (idx < 0)
		return;

	len = OF_getproplen(ci->ci_node, "power-domains");
	if (len <= 0)
		return;

	domains = malloc(len, M_TEMP, M_WAITOK);
	OF_getpropintarray(ci->ci_node, "power-domains", domains, len);

	domain = domains;
	while (domain && domain < domains + (len / sizeof(uint32_t))) {
		if (idx == 0)
			break;

		node = OF_getnodebyphandle(domain[0]);
		if (node == 0)
			break;

		ncells = OF_getpropint(node, "#power-domain-cells", 0);
		domain = domain + ncells + 1;
		idx--;
	}

	node = idx == 0 ? OF_getnodebyphandle(domain[0]) : 0;
	free(domains, M_TEMP, len);
	if (node == 0)
		return;

	/*
	 * We found the "psci" power domain.  If this power domain has
	 * a parent power domain, stash its phandle away for later.
	 */
 
	cluster = OF_getpropint(node, "power-domains", 0);

	/*
	 * Get the deepest idle state for the CPU; this should be the
	 * last one that is listed.
	 */

	len = OF_getproplen(node, "domain-idle-states");
	if (len < (int)sizeof(uint32_t))
		return;

	states = malloc(len, M_TEMP, M_WAITOK);
	OF_getpropintarray(node, "domain-idle-states", states, len);

	node = OF_getnodebyphandle(states[len / sizeof(uint32_t) - 1]);
	free(states, M_TEMP, len);
	if (node == 0)
		return;

	ci->ci_psci_suspend_param =
		OF_getpropint(node, "arm,psci-suspend-param", 0);

	/*
	 * Qualcomm Snapdragon always seem to operate in OS Initiated
	 * mode.  This means that the last CPU to suspend can pick the
	 * idle state that powers off the entire cluster.  In our case
	 * that will always be the primary CPU.
	 */

#ifdef MULTIPROCESSOR
	if (ci->ci_flags & CPUF_AP)
		return;
#endif

	node = OF_getnodebyphandle(cluster);
	if (node == 0)
		return;

	/*
	 * Get the deepest idle state for the cluster; this should be
	 * the last one that is listed.
	 */

	states = malloc(len, M_TEMP, M_WAITOK);
	OF_getpropintarray(node, "domain-idle-states", states, len);

	node = OF_getnodebyphandle(states[len / sizeof(uint32_t) - 1]);
	free(states, M_TEMP, len);
	if (node == 0)
		return;

	ci->ci_psci_suspend_param =
		OF_getpropint(node, "arm,psci-suspend-param", 0);
}

void
cpu_psci_idle_cycle(void)
{
	struct cpu_info *ci = curcpu();
	struct timeval start, stop;
	u_long itime;

	microuptime(&start);

	if (ci->ci_prev_sleep > ci->ci_psci_idle_latency)
		psci_cpu_suspend(ci->ci_psci_idle_param, 0, 0);
	else
		cpu_wfi();

	microuptime(&stop);
	timersub(&stop, &start, &stop);
	itime = stop.tv_sec * 1000000 + stop.tv_usec;

	ci->ci_last_itime = itime;
	itime >>= 1;
	ci->ci_prev_sleep = (ci->ci_prev_sleep + (ci->ci_prev_sleep >> 1)
	    + itime) >> 1;
}

#if NKSTAT > 0

struct cpu_kstats {
	struct kstat_kv		ck_impl;
	struct kstat_kv		ck_part;
	struct kstat_kv		ck_rev;
};

void
cpu_kstat_attach(struct cpu_info *ci)
{
	struct kstat *ks;
	struct cpu_kstats *ck;
	uint64_t impl, part;
	const char *impl_name = NULL, *part_name = NULL;
	const struct cpu_cores *coreselecter = cpu_cores_none;
	int i;

	ks = kstat_create(ci->ci_dev->dv_xname, 0, "mach", 0, KSTAT_T_KV, 0);
	if (ks == NULL) {
		printf("%s: unable to create cpu kstats\n",
		    ci->ci_dev->dv_xname);
		return;
	}

	ck = malloc(sizeof(*ck), M_DEVBUF, M_WAITOK);

	impl = CPU_IMPL(ci->ci_midr);
	part = CPU_PART(ci->ci_midr);

	for (i = 0; cpu_implementers[i].name; i++) {
		if (impl == cpu_implementers[i].id) {
			impl_name = cpu_implementers[i].name;
			coreselecter = cpu_implementers[i].corelist;
			break;
		}
	}

	if (impl_name) {
		kstat_kv_init(&ck->ck_impl, "impl", KSTAT_KV_T_ISTR);
		strlcpy(kstat_kv_istr(&ck->ck_impl), impl_name,
		    sizeof(kstat_kv_istr(&ck->ck_impl)));
	} else
		kstat_kv_init(&ck->ck_impl, "impl", KSTAT_KV_T_NULL);

	for (i = 0; coreselecter[i].name; i++) {
		if (part == coreselecter[i].id) {
			part_name = coreselecter[i].name;
			break;
		}
	}

	if (part_name) {
		kstat_kv_init(&ck->ck_part, "part", KSTAT_KV_T_ISTR);
		strlcpy(kstat_kv_istr(&ck->ck_part), part_name,
		    sizeof(kstat_kv_istr(&ck->ck_part)));
	} else
		kstat_kv_init(&ck->ck_part, "part", KSTAT_KV_T_NULL);

	kstat_kv_init(&ck->ck_rev, "rev", KSTAT_KV_T_ISTR);
	snprintf(kstat_kv_istr(&ck->ck_rev), sizeof(kstat_kv_istr(&ck->ck_rev)),
	    "r%llup%llu", CPU_VAR(ci->ci_midr), CPU_REV(ci->ci_midr));

	ks->ks_softc = ci;
	ks->ks_data = ck;
	ks->ks_datalen = sizeof(*ck);
	ks->ks_read = kstat_read_nop;

	kstat_install(ks);

	/* XXX should we have a ci->ci_kstat = ks? */
}

struct cpu_opp_kstats {
	struct kstat_kv		coppk_freq;
	struct kstat_kv		coppk_supply_v;
};

int
cpu_opp_kstat_read(struct kstat *ks)
{
	struct cpu_info *ci = ks->ks_softc;
	struct cpu_opp_kstats *coppk = ks->ks_data;

	struct opp_table *ot = ci->ci_opp_table;
	struct cpu_info *oci;
	struct timespec now, diff;

	/* rate limit */
	getnanouptime(&now);
	timespecsub(&now, &ks->ks_updated, &diff);
	if (diff.tv_sec < 1)
		return (0);

	if (ot == NULL)
		return (0);

	oci = ot->ot_master;
	if (oci == NULL)
		oci = ci;

	kstat_kv_freq(&coppk->coppk_freq) =
	    clock_get_frequency(oci->ci_node, NULL);

	if (oci->ci_cpu_supply) {
		kstat_kv_volts(&coppk->coppk_supply_v) =
		    regulator_get_voltage(oci->ci_cpu_supply);
	}

	ks->ks_updated = now;

	return (0);
}

void
cpu_opp_kstat_attach(struct cpu_info *ci)
{
	struct kstat *ks;
	struct cpu_opp_kstats *coppk;
	struct opp_table *ot = ci->ci_opp_table;
	struct cpu_info *oci = ot->ot_master;

	if (oci == NULL)
		oci = ci;

	ks = kstat_create(ci->ci_dev->dv_xname, 0, "dt-opp", 0,
	    KSTAT_T_KV, 0);
	if (ks == NULL) {
		printf("%s: unable to create cpu dt-opp kstats\n",
		    ci->ci_dev->dv_xname);
		return;
	}

	coppk = malloc(sizeof(*coppk), M_DEVBUF, M_WAITOK);

	kstat_kv_init(&coppk->coppk_freq, "freq", KSTAT_KV_T_FREQ);
	kstat_kv_init(&coppk->coppk_supply_v, "supply",
	    oci->ci_cpu_supply ? KSTAT_KV_T_VOLTS_DC : KSTAT_KV_T_NULL);

	ks->ks_softc = oci;
	ks->ks_data = coppk;
	ks->ks_datalen = sizeof(*coppk);
	ks->ks_read = cpu_opp_kstat_read;

	kstat_install(ks);

	/* XXX should we have a ci->ci_opp_kstat = ks? */
}

#endif /* NKSTAT > 0 */
