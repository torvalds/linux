/*-
 * Copyright 2014 Svatopluk Kraus <onwahe@gmail.com>
 * Copyright 2014 Michal Meloun <meloun@miracle.cz>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/pcpu.h>
#include <sys/smp.h>
#include <sys/sysctl.h>

#include <machine/cpu.h>
#include <machine/cpuinfo.h>
#include <machine/elf.h>
#include <machine/md_var.h>

#if __ARM_ARCH >= 6
void reinit_mmu(uint32_t ttb, uint32_t aux_clr, uint32_t aux_set);

int disable_bp_hardening;
int spectre_v2_safe = 1;
#endif

struct cpuinfo cpuinfo =
{
	/* Use safe defaults for start */
	.dcache_line_size = 32,
	.dcache_line_mask = 31,
	.icache_line_size = 32,
	.icache_line_mask = 31,
};

static SYSCTL_NODE(_hw, OID_AUTO, cpu, CTLFLAG_RD, 0,
    "CPU");
static SYSCTL_NODE(_hw_cpu, OID_AUTO, quirks, CTLFLAG_RD, 0,
    "CPU quirks");

/*
 * Tunable CPU quirks.
 * Be careful, ACTRL cannot be changed if CPU is started in secure
 * mode(world) and write to ACTRL can cause exception!
 * These quirks are intended for optimizing CPU performance, not for
 * applying errata workarounds. Nobody can expect that CPU with unfixed
 * errata is stable enough to execute the kernel until quirks are applied.
 */
static uint32_t cpu_quirks_actlr_mask;
SYSCTL_INT(_hw_cpu_quirks, OID_AUTO, actlr_mask,
    CTLFLAG_RDTUN | CTLFLAG_NOFETCH, &cpu_quirks_actlr_mask, 0,
    "Bits to be masked in ACTLR");

static uint32_t cpu_quirks_actlr_set;
SYSCTL_INT(_hw_cpu_quirks, OID_AUTO, actlr_set,
    CTLFLAG_RDTUN | CTLFLAG_NOFETCH, &cpu_quirks_actlr_set, 0,
    "Bits to be set in ACTLR");


/* Read and parse CPU id scheme */
void
cpuinfo_init(void)
{
#if __ARM_ARCH >= 6
	uint32_t tmp;
#endif

	/*
	 * Prematurely fetch CPU quirks. Standard fetch for tunable
	 * sysctls is handled using SYSINIT, thus too late for boot CPU.
	 * Keep names in sync with sysctls.
	 */
	TUNABLE_INT_FETCH("hw.cpu.quirks.actlr_mask", &cpu_quirks_actlr_mask);
	TUNABLE_INT_FETCH("hw.cpu.quirks.actlr_set", &cpu_quirks_actlr_set);

	cpuinfo.midr = cp15_midr_get();
	/* Test old version id schemes first */
	if ((cpuinfo.midr & CPU_ID_IMPLEMENTOR_MASK) == CPU_ID_ARM_LTD) {
		if (CPU_ID_ISOLD(cpuinfo.midr)) {
			/* obsolete ARMv2 or ARMv3 CPU */
			cpuinfo.midr = 0;
			return;
		}
		if (CPU_ID_IS7(cpuinfo.midr)) {
			if ((cpuinfo.midr & (1 << 23)) == 0) {
				/* obsolete ARMv3 CPU */
				cpuinfo.midr = 0;
				return;
			}
			/* ARMv4T CPU */
			cpuinfo.architecture = 1;
			cpuinfo.revision = (cpuinfo.midr >> 16) & 0x7F;
		} else {
			/* ARM new id scheme */
			cpuinfo.architecture = (cpuinfo.midr >> 16) & 0x0F;
			cpuinfo.revision = (cpuinfo.midr >> 20) & 0x0F;
		}
	} else {
		/* non ARM -> must be new id scheme */
		cpuinfo.architecture = (cpuinfo.midr >> 16) & 0x0F;
		cpuinfo.revision = (cpuinfo.midr >> 20) & 0x0F;
	}
	/* Parse rest of MIDR  */
	cpuinfo.implementer = (cpuinfo.midr >> 24) & 0xFF;
	cpuinfo.part_number = (cpuinfo.midr >> 4) & 0xFFF;
	cpuinfo.patch = cpuinfo.midr & 0x0F;

	/* CP15 c0,c0 regs 0-7 exist on all CPUs (although aliased with MIDR) */
	cpuinfo.ctr = cp15_ctr_get();
	cpuinfo.tcmtr = cp15_tcmtr_get();
#if __ARM_ARCH >= 6
	cpuinfo.tlbtr = cp15_tlbtr_get();
	cpuinfo.mpidr = cp15_mpidr_get();
	cpuinfo.revidr = cp15_revidr_get();
#endif

	/* if CPU is not v7 cpu id scheme */
	if (cpuinfo.architecture != 0xF)
		return;
#if __ARM_ARCH >= 6
	cpuinfo.id_pfr0 = cp15_id_pfr0_get();
	cpuinfo.id_pfr1 = cp15_id_pfr1_get();
	cpuinfo.id_dfr0 = cp15_id_dfr0_get();
	cpuinfo.id_afr0 = cp15_id_afr0_get();
	cpuinfo.id_mmfr0 = cp15_id_mmfr0_get();
	cpuinfo.id_mmfr1 = cp15_id_mmfr1_get();
	cpuinfo.id_mmfr2 = cp15_id_mmfr2_get();
	cpuinfo.id_mmfr3 = cp15_id_mmfr3_get();
	cpuinfo.id_isar0 = cp15_id_isar0_get();
	cpuinfo.id_isar1 = cp15_id_isar1_get();
	cpuinfo.id_isar2 = cp15_id_isar2_get();
	cpuinfo.id_isar3 = cp15_id_isar3_get();
	cpuinfo.id_isar4 = cp15_id_isar4_get();
	cpuinfo.id_isar5 = cp15_id_isar5_get();

/* Not yet - CBAR only exist on ARM SMP Cortex A CPUs
	cpuinfo.cbar = cp15_cbar_get();
*/
	if (CPU_CT_FORMAT(cpuinfo.ctr) == CPU_CT_ARMV7) {
		cpuinfo.ccsidr = cp15_ccsidr_get();
		cpuinfo.clidr = cp15_clidr_get();
	}

	/* Test if revidr is implemented */
	if (cpuinfo.revidr == cpuinfo.midr)
		cpuinfo.revidr = 0;

	/* parsed bits of above registers */
	/* id_mmfr0 */
	cpuinfo.outermost_shareability =  (cpuinfo.id_mmfr0 >> 8) & 0xF;
	cpuinfo.shareability_levels = (cpuinfo.id_mmfr0 >> 12) & 0xF;
	cpuinfo.auxiliary_registers = (cpuinfo.id_mmfr0 >> 20) & 0xF;
	cpuinfo.innermost_shareability = (cpuinfo.id_mmfr0 >> 28) & 0xF;
	/* id_mmfr2 */
	cpuinfo.mem_barrier = (cpuinfo.id_mmfr2 >> 20) & 0xF;
	/* id_mmfr3 */
	cpuinfo.coherent_walk = (cpuinfo.id_mmfr3 >> 20) & 0xF;
	cpuinfo.maintenance_broadcast =(cpuinfo.id_mmfr3 >> 12) & 0xF;
	/* id_pfr1 */
	cpuinfo.generic_timer_ext = (cpuinfo.id_pfr1 >> 16) & 0xF;
	cpuinfo.virtualization_ext = (cpuinfo.id_pfr1 >> 12) & 0xF;
	cpuinfo.security_ext = (cpuinfo.id_pfr1 >> 4) & 0xF;
	/* mpidr */
	cpuinfo.mp_ext = (cpuinfo.mpidr >> 31u) & 0x1;

	/* L1 Cache sizes */
	if (CPU_CT_FORMAT(cpuinfo.ctr) == CPU_CT_ARMV7) {
		cpuinfo.dcache_line_size =
		    1 << (CPU_CT_DMINLINE(cpuinfo.ctr) + 2);
		cpuinfo.icache_line_size =
		    1 << (CPU_CT_IMINLINE(cpuinfo.ctr) + 2);
	} else {
		cpuinfo.dcache_line_size =
		    1 << (CPU_CT_xSIZE_LEN(CPU_CT_DSIZE(cpuinfo.ctr)) + 3);
		cpuinfo.icache_line_size =
		    1 << (CPU_CT_xSIZE_LEN(CPU_CT_ISIZE(cpuinfo.ctr)) + 3);
	}
	cpuinfo.dcache_line_mask = cpuinfo.dcache_line_size - 1;
	cpuinfo.icache_line_mask = cpuinfo.icache_line_size - 1;

	/* Fill AT_HWCAP bits. */
	elf_hwcap |= HWCAP_HALF | HWCAP_FAST_MULT; /* Required for all CPUs */
	elf_hwcap |= HWCAP_TLS | HWCAP_EDSP;	   /* Required for v6+ CPUs */

	tmp = (cpuinfo.id_isar0 >> 24) & 0xF;	/* Divide_instrs */
	if (tmp >= 1)
		elf_hwcap |= HWCAP_IDIVT;
	if (tmp >= 2)
		elf_hwcap |= HWCAP_IDIVA;

	tmp = (cpuinfo.id_pfr0 >> 4) & 0xF; 	/* State1  */
	if (tmp >= 1)
		elf_hwcap |= HWCAP_THUMB;

	tmp = (cpuinfo.id_pfr0 >> 12) & 0xF; 	/* State3  */
	if (tmp >= 1)
		elf_hwcap |= HWCAP_THUMBEE;

	tmp = (cpuinfo.id_mmfr0 >> 0) & 0xF; 	/* VMSA */
	if (tmp >= 5)
		elf_hwcap |= HWCAP_LPAE;

	/* Fill AT_HWCAP2 bits. */
	tmp = (cpuinfo.id_isar5 >> 4) & 0xF;	/* AES */
	if (tmp >= 1)
		elf_hwcap2 |= HWCAP2_AES;
	if (tmp >= 2)
		elf_hwcap2 |= HWCAP2_PMULL;

	tmp = (cpuinfo.id_isar5 >> 8) & 0xF;	/* SHA1 */
	if (tmp >= 1)
		elf_hwcap2 |= HWCAP2_SHA1;

	tmp = (cpuinfo.id_isar5 >> 12) & 0xF;	/* SHA2 */
	if (tmp >= 1)
		elf_hwcap2 |= HWCAP2_SHA2;

	tmp = (cpuinfo.id_isar5 >> 16) & 0xF;	/* CRC32 */
	if (tmp >= 1)
		elf_hwcap2 |= HWCAP2_CRC32;
#endif
}

#if __ARM_ARCH >= 6
/*
 * Get bits that must be set or cleared in ACLR register.
 * Note: Bits in ACLR register are IMPLEMENTATION DEFINED.
 * Its expected that SCU is in operational state before this
 * function is called.
 */
static void
cpuinfo_get_actlr_modifier(uint32_t *actlr_mask, uint32_t *actlr_set)
{

	*actlr_mask = 0;
	*actlr_set = 0;

	if (cpuinfo.implementer == CPU_IMPLEMENTER_ARM) {
		switch (cpuinfo.part_number) {
		case CPU_ARCH_CORTEX_A75:
		case CPU_ARCH_CORTEX_A73:
		case CPU_ARCH_CORTEX_A72:
		case CPU_ARCH_CORTEX_A57:
		case CPU_ARCH_CORTEX_A53:
			/* Nothing to do for AArch32 */
			break;
		case CPU_ARCH_CORTEX_A17:
		case CPU_ARCH_CORTEX_A12: /* A12 is merged to A17 */
			/*
			 * Enable SMP mode
			 */
			*actlr_mask = (1 << 6);
			*actlr_set = (1 << 6);
			break;
		case CPU_ARCH_CORTEX_A15:
			/*
			 * Enable snoop-delayed exclusive handling
			 * Enable SMP mode
			 */
			*actlr_mask = (1U << 31) |(1 << 6);
			*actlr_set = (1U << 31) |(1 << 6);
			break;
		case CPU_ARCH_CORTEX_A9:
			/*
			 * Disable exclusive L1/L2 cache control
			 * Enable SMP mode
			 * Enable Cache and TLB maintenance broadcast
			 */
			*actlr_mask = (1 << 7) | (1 << 6) | (1 << 0);
			*actlr_set = (1 << 6) | (1 << 0);
			break;
		case CPU_ARCH_CORTEX_A8:
			/*
			 * Enable L2 cache
			 * Enable L1 data cache hardware alias checks
			 */
			*actlr_mask = (1 << 1) | (1 << 0);
			*actlr_set = (1 << 1);
			break;
		case CPU_ARCH_CORTEX_A7:
			/*
			 * Enable SMP mode
			 */
			*actlr_mask = (1 << 6);
			*actlr_set = (1 << 6);
			break;
		case CPU_ARCH_CORTEX_A5:
			/*
			 * Disable exclusive L1/L2 cache control
			 * Enable SMP mode
			 * Enable Cache and TLB maintenance broadcast
			 */
			*actlr_mask = (1 << 7) | (1 << 6) | (1 << 0);
			*actlr_set = (1 << 6) | (1 << 0);
			break;
		case CPU_ARCH_ARM1176:
			/*
			 * Restrict cache size to 16KB
			 * Enable the return stack
			 * Enable dynamic branch prediction
			 * Enable static branch prediction
			 */
			*actlr_mask = (1 << 6) | (1 << 2) | (1 << 1) | (1 << 0);
			*actlr_set = (1 << 6) | (1 << 2) | (1 << 1) | (1 << 0);
			break;
		}
		return;
	}
}

/* Reinitialize MMU to final kernel mapping and apply all CPU quirks. */
void
cpuinfo_reinit_mmu(uint32_t ttb)
{
	uint32_t actlr_mask;
	uint32_t actlr_set;

	cpuinfo_get_actlr_modifier(&actlr_mask, &actlr_set);
	actlr_mask |= cpu_quirks_actlr_mask;
	actlr_set |= cpu_quirks_actlr_set;
	reinit_mmu(ttb, actlr_mask, actlr_set);
}

static bool
modify_actlr(uint32_t clear, uint32_t set)
{
	uint32_t reg, newreg;

	reg = cp15_actlr_get();
	newreg = reg;
	newreg &= ~clear;
	newreg |= set;
	if (reg == newreg)
		return (true);
	cp15_actlr_set(newreg);

	reg = cp15_actlr_get();
	if (reg == newreg)
		return (true);
	return (false);
}

/* Apply/restore BP hardening on current core. */
static int
apply_bp_hardening(bool enable, int kind, bool actrl, uint32_t set_mask)
{
	if (enable) {
		if (actrl && !modify_actlr(0, set_mask))
			return (-1);
		PCPU_SET(bp_harden_kind, kind);
	} else {
		PCPU_SET(bp_harden_kind, PCPU_BP_HARDEN_KIND_NONE);
		if (actrl)
			modify_actlr(~0, PCPU_GET(original_actlr));
		spectre_v2_safe = 0;
	}
	return (0);
}

static void
handle_bp_hardening(bool enable)
{
	int kind;
	char *kind_str;

	kind = PCPU_BP_HARDEN_KIND_NONE;
	/*
	 * Note: Access to ACTRL is locked to secure world on most boards.
	 * This means that full BP hardening depends on updated u-boot/firmware
	 * or is impossible at all (if secure monitor is in on-chip ROM).
	 */
	if (cpuinfo.implementer == CPU_IMPLEMENTER_ARM) {
		switch (cpuinfo.part_number) {
		case CPU_ARCH_CORTEX_A8:
			/*
			 * For Cortex-A8, IBE bit must be set otherwise
			 * BPIALL is effectively NOP.
			 * Unfortunately, Cortex-A is also affected by
			 * ARM erratum 687067 which causes non-working
			 * BPIALL if IBE bit is set and 'Instruction L1 System
			 * Array Debug Register 0' is not 0.
			 * This register is not reset on power-up and is
			 * accessible only from secure world, so we cannot do
			 * nothing (nor detect) to fix this issue.
			 * I afraid that on chip ROM based secure monitor on
			 * AM335x (BeagleBone) doesn't reset this debug
			 * register.
			 */
			kind = PCPU_BP_HARDEN_KIND_BPIALL;
			if (apply_bp_hardening(enable, kind, true, 1 << 6) != 0)
				goto actlr_err;
			break;
		break;

		case CPU_ARCH_CORTEX_A9:
		case CPU_ARCH_CORTEX_A12:
		case CPU_ARCH_CORTEX_A17:
		case CPU_ARCH_CORTEX_A57:
		case CPU_ARCH_CORTEX_A72:
		case CPU_ARCH_CORTEX_A73:
		case CPU_ARCH_CORTEX_A75:
			kind = PCPU_BP_HARDEN_KIND_BPIALL;
			if (apply_bp_hardening(enable, kind, false, 0) != 0)
				goto actlr_err;
			break;

		case CPU_ARCH_CORTEX_A15:
			/*
			 * For Cortex-A15, set 'Enable invalidates of BTB' bit.
			 * Despite this, the BPIALL is still effectively NOP,
			 * but with this bit set, the ICIALLU also flushes
			 * branch predictor as side effect.
			 */
			kind = PCPU_BP_HARDEN_KIND_ICIALLU;
			if (apply_bp_hardening(enable, kind, true, 1 << 0) != 0)
				goto actlr_err;
			break;

		default:
			break;
		}
	} else if (cpuinfo.implementer == CPU_IMPLEMENTER_QCOM) {
		printf("!!!WARNING!!! CPU(%d) is vulnerable to speculative "
		    "branch attacks. !!!\n"
		    "Qualcomm Krait cores are known (or believed) to be "
		    "vulnerable to \n"
		    "speculative branch attacks, no mitigation exists yet.\n",
		    PCPU_GET(cpuid));
		goto unkonown_mitigation;
	}  else {
		goto unkonown_mitigation;
	}

	if (bootverbose) {
		switch (kind) {
		case PCPU_BP_HARDEN_KIND_NONE:
			kind_str = "not necessary";
			break;
		case PCPU_BP_HARDEN_KIND_BPIALL:
			kind_str = "BPIALL";
			break;
		case PCPU_BP_HARDEN_KIND_ICIALLU:
			kind_str = "ICIALLU";
			break;
		default:
			panic("Unknown BP hardering kind (%d).", kind);
		}
		printf("CPU(%d) applied BP hardening: %s\n", PCPU_GET(cpuid),
		    kind_str);
	}

	return;

unkonown_mitigation:
	PCPU_SET(bp_harden_kind, PCPU_BP_HARDEN_KIND_NONE);
	spectre_v2_safe = 0;
	return;

actlr_err:
	PCPU_SET(bp_harden_kind, PCPU_BP_HARDEN_KIND_NONE);
	spectre_v2_safe = 0;
	printf("!!!WARNING!!! CPU(%d) is vulnerable to speculative branch "
	    "attacks. !!!\n"
	    "We cannot enable required bit(s) in ACTRL register\n"
	    "because it's locked by secure monitor and/or firmware.\n",
	    PCPU_GET(cpuid));
}

void
cpuinfo_init_bp_hardening(void)
{

	/*
	 * Store original unmodified ACTRL, so we can restore it when
	 * BP hardening is disabled by sysctl.
	 */
	PCPU_SET(original_actlr, cp15_actlr_get());
	handle_bp_hardening(true);
}

static void
bp_hardening_action(void *arg)
{

	handle_bp_hardening(disable_bp_hardening == 0);
}

static int
sysctl_disable_bp_hardening(SYSCTL_HANDLER_ARGS)
{
	int rv;

	rv = sysctl_handle_int(oidp, oidp->oid_arg1, oidp->oid_arg2, req);

	if (!rv && req->newptr) {
		spectre_v2_safe = 1;
		dmb();
#ifdef SMP
		smp_rendezvous_cpus(all_cpus, smp_no_rendezvous_barrier,
		bp_hardening_action, NULL, NULL);
#else
		bp_hardening_action(NULL);
#endif
	}

	return (rv);
}

SYSCTL_PROC(_machdep, OID_AUTO, disable_bp_hardening,
    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE,
    &disable_bp_hardening, 0, sysctl_disable_bp_hardening, "I",
    "Disable BP hardening mitigation.");

SYSCTL_INT(_machdep, OID_AUTO, spectre_v2_safe, CTLFLAG_RD,
    &spectre_v2_safe, 0, "System is safe to Spectre Version 2 attacks");

#endif /* __ARM_ARCH >= 6 */
