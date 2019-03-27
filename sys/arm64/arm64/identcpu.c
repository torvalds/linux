/*-
 * Copyright (c) 2014 Andrew Turner
 * Copyright (c) 2014 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Semihalf
 * under sponsorship of the FreeBSD Foundation.
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
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/pcpu.h>
#include <sys/sbuf.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <machine/atomic.h>
#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/undefined.h>

static int ident_lock;

char machine[] = "arm64";

#ifdef SCTL_MASK32
extern int adaptive_machine_arch;
#endif

static int
sysctl_hw_machine(SYSCTL_HANDLER_ARGS)
{
#ifdef SCTL_MASK32
	static const char machine32[] = "arm";
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

/*
 * Per-CPU affinity as provided in MPIDR_EL1
 * Indexed by CPU number in logical order selected by the system.
 * Relevant fields can be extracted using CPU_AFFn macros,
 * Aff3.Aff2.Aff1.Aff0 construct a unique CPU address in the system.
 *
 * Fields used by us:
 * Aff1 - Cluster number
 * Aff0 - CPU number in Aff1 cluster
 */
uint64_t __cpu_affinity[MAXCPU];
static u_int cpu_aff_levels;

struct cpu_desc {
	u_int		cpu_impl;
	u_int		cpu_part_num;
	u_int		cpu_variant;
	u_int		cpu_revision;
	const char	*cpu_impl_name;
	const char	*cpu_part_name;

	uint64_t	mpidr;
	uint64_t	id_aa64afr0;
	uint64_t	id_aa64afr1;
	uint64_t	id_aa64dfr0;
	uint64_t	id_aa64dfr1;
	uint64_t	id_aa64isar0;
	uint64_t	id_aa64isar1;
	uint64_t	id_aa64mmfr0;
	uint64_t	id_aa64mmfr1;
	uint64_t	id_aa64mmfr2;
	uint64_t	id_aa64pfr0;
	uint64_t	id_aa64pfr1;
};

struct cpu_desc cpu_desc[MAXCPU];
struct cpu_desc user_cpu_desc;
static u_int cpu_print_regs;
#define	PRINT_ID_AA64_AFR0	0x00000001
#define	PRINT_ID_AA64_AFR1	0x00000002
#define	PRINT_ID_AA64_DFR0	0x00000010
#define	PRINT_ID_AA64_DFR1	0x00000020
#define	PRINT_ID_AA64_ISAR0	0x00000100
#define	PRINT_ID_AA64_ISAR1	0x00000200
#define	PRINT_ID_AA64_MMFR0	0x00001000
#define	PRINT_ID_AA64_MMFR1	0x00002000
#define	PRINT_ID_AA64_MMFR2	0x00004000
#define	PRINT_ID_AA64_PFR0	0x00010000
#define	PRINT_ID_AA64_PFR1	0x00020000

struct cpu_parts {
	u_int		part_id;
	const char	*part_name;
};
#define	CPU_PART_NONE	{ 0, "Unknown Processor" }

struct cpu_implementers {
	u_int			impl_id;
	const char		*impl_name;
	/*
	 * Part number is implementation defined
	 * so each vendor will have its own set of values and names.
	 */
	const struct cpu_parts	*cpu_parts;
};
#define	CPU_IMPLEMENTER_NONE	{ 0, "Unknown Implementer", cpu_parts_none }

/*
 * Per-implementer table of (PartNum, CPU Name) pairs.
 */
/* ARM Ltd. */
static const struct cpu_parts cpu_parts_arm[] = {
	{ CPU_PART_FOUNDATION, "Foundation-Model" },
	{ CPU_PART_CORTEX_A35, "Cortex-A35" },
	{ CPU_PART_CORTEX_A53, "Cortex-A53" },
	{ CPU_PART_CORTEX_A55, "Cortex-A55" },
	{ CPU_PART_CORTEX_A57, "Cortex-A57" },
	{ CPU_PART_CORTEX_A72, "Cortex-A72" },
	{ CPU_PART_CORTEX_A73, "Cortex-A73" },
	{ CPU_PART_CORTEX_A75, "Cortex-A75" },
	CPU_PART_NONE,
};
/* Cavium */
static const struct cpu_parts cpu_parts_cavium[] = {
	{ CPU_PART_THUNDERX, "ThunderX" },
	{ CPU_PART_THUNDERX2, "ThunderX2" },
	CPU_PART_NONE,
};

/* Unknown */
static const struct cpu_parts cpu_parts_none[] = {
	CPU_PART_NONE,
};

/*
 * Implementers table.
 */
const struct cpu_implementers cpu_implementers[] = {
	{ CPU_IMPL_ARM,		"ARM",		cpu_parts_arm },
	{ CPU_IMPL_BROADCOM,	"Broadcom",	cpu_parts_none },
	{ CPU_IMPL_CAVIUM,	"Cavium",	cpu_parts_cavium },
	{ CPU_IMPL_DEC,		"DEC",		cpu_parts_none },
	{ CPU_IMPL_INFINEON,	"IFX",		cpu_parts_none },
	{ CPU_IMPL_FREESCALE,	"Freescale",	cpu_parts_none },
	{ CPU_IMPL_NVIDIA,	"NVIDIA",	cpu_parts_none },
	{ CPU_IMPL_APM,		"APM",		cpu_parts_none },
	{ CPU_IMPL_QUALCOMM,	"Qualcomm",	cpu_parts_none },
	{ CPU_IMPL_MARVELL,	"Marvell",	cpu_parts_none },
	{ CPU_IMPL_INTEL,	"Intel",	cpu_parts_none },
	CPU_IMPLEMENTER_NONE,
};

#define	MRS_TYPE_MASK		0xf
#define	MRS_INVALID		0
#define	MRS_EXACT		1
#define	MRS_EXACT_VAL(x)	(MRS_EXACT | ((x) << 4))
#define	MRS_EXACT_FIELD(x)	((x) >> 4)
#define	MRS_LOWER		2

struct mrs_field {
	bool		sign;
	u_int		type;
	u_int		shift;
};

#define	MRS_FIELD(_sign, _type, _shift)					\
	{								\
		.sign = (_sign),					\
		.type = (_type),					\
		.shift = (_shift),					\
	}

#define	MRS_FIELD_END	{ .type = MRS_INVALID, }

static struct mrs_field id_aa64isar0_fields[] = {
	MRS_FIELD(false, MRS_LOWER, ID_AA64ISAR0_DP_SHIFT),
	MRS_FIELD(false, MRS_LOWER, ID_AA64ISAR0_SM4_SHIFT),
	MRS_FIELD(false, MRS_LOWER, ID_AA64ISAR0_SM3_SHIFT),
	MRS_FIELD(false, MRS_LOWER, ID_AA64ISAR0_SHA3_SHIFT),
	MRS_FIELD(false, MRS_LOWER, ID_AA64ISAR0_RDM_SHIFT),
	MRS_FIELD(false, MRS_LOWER, ID_AA64ISAR0_ATOMIC_SHIFT),
	MRS_FIELD(false, MRS_LOWER, ID_AA64ISAR0_CRC32_SHIFT),
	MRS_FIELD(false, MRS_LOWER, ID_AA64ISAR0_SHA2_SHIFT),
	MRS_FIELD(false, MRS_LOWER, ID_AA64ISAR0_SHA1_SHIFT),
	MRS_FIELD(false, MRS_LOWER, ID_AA64ISAR0_AES_SHIFT),
	MRS_FIELD_END,
};

static struct mrs_field id_aa64isar1_fields[] = {
	MRS_FIELD(false, MRS_EXACT, ID_AA64ISAR1_GPI_SHIFT),
	MRS_FIELD(false, MRS_EXACT, ID_AA64ISAR1_GPA_SHIFT),
	MRS_FIELD(false, MRS_LOWER, ID_AA64ISAR1_LRCPC_SHIFT),
	MRS_FIELD(false, MRS_LOWER, ID_AA64ISAR1_FCMA_SHIFT),
	MRS_FIELD(false, MRS_LOWER, ID_AA64ISAR1_JSCVT_SHIFT),
	MRS_FIELD(false, MRS_EXACT, ID_AA64ISAR1_API_SHIFT),
	MRS_FIELD(false, MRS_EXACT, ID_AA64ISAR1_APA_SHIFT),
	MRS_FIELD(false, MRS_LOWER, ID_AA64ISAR1_DPB_SHIFT),
	MRS_FIELD_END,
};

static struct mrs_field id_aa64pfr0_fields[] = {
	MRS_FIELD(false, MRS_EXACT, ID_AA64PFR0_SVE_SHIFT),
	MRS_FIELD(false, MRS_EXACT, ID_AA64PFR0_RAS_SHIFT),
	MRS_FIELD(false, MRS_EXACT, ID_AA64PFR0_GIC_SHIFT),
	MRS_FIELD(true,  MRS_LOWER, ID_AA64PFR0_ADV_SIMD_SHIFT),
	MRS_FIELD(true,  MRS_LOWER, ID_AA64PFR0_FP_SHIFT),
	MRS_FIELD(false, MRS_EXACT, ID_AA64PFR0_EL3_SHIFT),
	MRS_FIELD(false, MRS_EXACT, ID_AA64PFR0_EL2_SHIFT),
	MRS_FIELD(false, MRS_LOWER, ID_AA64PFR0_EL1_SHIFT),
	MRS_FIELD(false, MRS_LOWER, ID_AA64PFR0_EL0_SHIFT),
	MRS_FIELD_END,
};

static struct mrs_field id_aa64dfr0_fields[] = {
	MRS_FIELD(false, MRS_EXACT, ID_AA64DFR0_PMS_VER_SHIFT),
	MRS_FIELD(false, MRS_EXACT, ID_AA64DFR0_CTX_CMPS_SHIFT),
	MRS_FIELD(false, MRS_EXACT, ID_AA64DFR0_WRPS_SHIFT),
	MRS_FIELD(false, MRS_EXACT, ID_AA64DFR0_BRPS_SHIFT),
	MRS_FIELD(false, MRS_EXACT, ID_AA64DFR0_PMU_VER_SHIFT),
	MRS_FIELD(false, MRS_EXACT, ID_AA64DFR0_TRACE_VER_SHIFT),
	MRS_FIELD(false, MRS_EXACT_VAL(0x6), ID_AA64DFR0_DEBUG_VER_SHIFT),
	MRS_FIELD_END,
};

struct mrs_user_reg {
	u_int		CRm;
	u_int		Op2;
	size_t		offset;
	struct mrs_field *fields;
};

static struct mrs_user_reg user_regs[] = {
	{	/* id_aa64isar0_el1 */
		.CRm = 6,
		.Op2 = 0,
		.offset = __offsetof(struct cpu_desc, id_aa64isar0),
		.fields = id_aa64isar0_fields,
	},
	{	/* id_aa64isar1_el1 */
		.CRm = 6,
		.Op2 = 1,
		.offset = __offsetof(struct cpu_desc, id_aa64isar1),
		.fields = id_aa64isar1_fields,
	},
	{	/* id_aa64pfr0_el1 */
		.CRm = 4,
		.Op2 = 0,
		.offset = __offsetof(struct cpu_desc, id_aa64pfr0),
		.fields = id_aa64pfr0_fields,
	},
	{	/* id_aa64dfr0_el1 */
		.CRm = 5,
		.Op2 = 0,
		.offset = __offsetof(struct cpu_desc, id_aa64dfr0),
		.fields = id_aa64dfr0_fields,
	},
};

#define	CPU_DESC_FIELD(desc, idx)					\
    *(uint64_t *)((char *)&(desc) + user_regs[(idx)].offset)

static int
user_mrs_handler(vm_offset_t va, uint32_t insn, struct trapframe *frame,
    uint32_t esr)
{
	uint64_t value;
	int CRm, Op2, i, reg;

	if ((insn & MRS_MASK) != MRS_VALUE)
		return (0);

	/*
	 * We only emulate Op0 == 3, Op1 == 0, CRn == 0, CRm == {0, 4-7}.
	 * These are in the EL1 CPU identification space.
	 * CRm == 0 holds MIDR_EL1, MPIDR_EL1, and REVID_EL1.
	 * CRm == {4-7} holds the ID_AA64 registers.
	 *
	 * For full details see the ARMv8 ARM (ARM DDI 0487C.a)
	 * Table D9-2 System instruction encodings for non-Debug System
	 * register accesses.
	 */
	if (mrs_Op0(insn) != 3 || mrs_Op1(insn) != 0 || mrs_CRn(insn) != 0)
		return (0);

	CRm = mrs_CRm(insn);
	if (CRm > 7 || (CRm < 4 && CRm != 0))
		return (0);

	Op2 = mrs_Op2(insn);
	value = 0;

	for (i = 0; i < nitems(user_regs); i++) {
		if (user_regs[i].CRm == CRm && user_regs[i].Op2 == Op2) {
			value = CPU_DESC_FIELD(user_cpu_desc, i);
			break;
		}
	}

	if (CRm == 0) {
		switch (Op2) {
		case 0:
			value = READ_SPECIALREG(midr_el1);
			break;
		case 5:
			value = READ_SPECIALREG(mpidr_el1);
			break;
		case 6:
			value = READ_SPECIALREG(revidr_el1);
			break;
		default:
			return (0);
		}
	}

	/*
	 * We will handle this instruction, move to the next so we
	 * don't trap here again.
	 */
	frame->tf_elr += INSN_SIZE;

	reg = MRS_REGISTER(insn);
	/* If reg is 31 then write to xzr, i.e. do nothing */
	if (reg == 31)
		return (1);

	if (reg < nitems(frame->tf_x))
		frame->tf_x[reg] = value;
	else if (reg == 30)
		frame->tf_lr = value;

	return (1);
}

static void
update_user_regs(u_int cpu)
{
	struct mrs_field *fields;
	uint64_t cur, value;
	int i, j, cur_field, new_field;

	for (i = 0; i < nitems(user_regs); i++) {
		value = CPU_DESC_FIELD(cpu_desc[cpu], i);
		if (cpu == 0)
			cur = value;
		else
			cur = CPU_DESC_FIELD(user_cpu_desc, i);

		fields = user_regs[i].fields;
		for (j = 0; fields[j].type != 0; j++) {
			switch (fields[j].type & MRS_TYPE_MASK) {
			case MRS_EXACT:
				cur &= ~(0xfu << fields[j].shift);
				cur |=
				    (uint64_t)MRS_EXACT_FIELD(fields[j].type) <<
				    fields[j].shift;
				break;
			case MRS_LOWER:
				new_field = (value >> fields[j].shift) & 0xf;
				cur_field = (cur >> fields[j].shift) & 0xf;
				if ((fields[j].sign &&
				     (int)new_field < (int)cur_field) ||
				    (!fields[j].sign &&
				     (u_int)new_field < (u_int)cur_field)) {
					cur &= ~(0xfu << fields[j].shift);
					cur |= new_field << fields[j].shift;
				}
				break;
			default:
				panic("Invalid field type: %d", fields[j].type);
			}
		}

		CPU_DESC_FIELD(user_cpu_desc, i) = cur;
	}
}

static void
identify_cpu_sysinit(void *dummy __unused)
{
	int cpu;

	/* Create a user visible cpu description with safe values */
	memset(&user_cpu_desc, 0, sizeof(user_cpu_desc));
	/* Safe values for these registers */
	user_cpu_desc.id_aa64pfr0 = ID_AA64PFR0_ADV_SIMD_NONE |
	    ID_AA64PFR0_FP_NONE | ID_AA64PFR0_EL1_64 | ID_AA64PFR0_EL0_64;
	user_cpu_desc.id_aa64dfr0 = ID_AA64DFR0_DEBUG_VER_8;


	CPU_FOREACH(cpu) {
		print_cpu_features(cpu);
		update_user_regs(cpu);
	}

	install_undef_handler(true, user_mrs_handler);
}
SYSINIT(idenrity_cpu, SI_SUB_SMP, SI_ORDER_ANY, identify_cpu_sysinit, NULL);

void
print_cpu_features(u_int cpu)
{
	struct sbuf *sb;
	int printed;

	sb = sbuf_new_auto();
	sbuf_printf(sb, "CPU%3d: %s %s r%dp%d", cpu,
	    cpu_desc[cpu].cpu_impl_name, cpu_desc[cpu].cpu_part_name,
	    cpu_desc[cpu].cpu_variant, cpu_desc[cpu].cpu_revision);

	sbuf_cat(sb, " affinity:");
	switch(cpu_aff_levels) {
	default:
	case 4:
		sbuf_printf(sb, " %2d", CPU_AFF3(cpu_desc[cpu].mpidr));
		/* FALLTHROUGH */
	case 3:
		sbuf_printf(sb, " %2d", CPU_AFF2(cpu_desc[cpu].mpidr));
		/* FALLTHROUGH */
	case 2:
		sbuf_printf(sb, " %2d", CPU_AFF1(cpu_desc[cpu].mpidr));
		/* FALLTHROUGH */
	case 1:
	case 0: /* On UP this will be zero */
		sbuf_printf(sb, " %2d", CPU_AFF0(cpu_desc[cpu].mpidr));
		break;
	}
	sbuf_finish(sb);
	printf("%s\n", sbuf_data(sb));
	sbuf_clear(sb);

	/*
	 * There is a hardware errata where, if one CPU is performing a TLB
	 * invalidation while another is performing a store-exclusive the
	 * store-exclusive may return the wrong status. A workaround seems
	 * to be to use an IPI to invalidate on each CPU, however given the
	 * limited number of affected units (pass 1.1 is the evaluation
	 * hardware revision), and the lack of information from Cavium
	 * this has not been implemented.
	 *
	 * At the time of writing this the only information is from:
	 * https://lkml.org/lkml/2016/8/4/722
	 */
	/*
	 * XXX: CPU_MATCH_ERRATA_CAVIUM_THUNDERX_1_1 on its own also
	 * triggers on pass 2.0+.
	 */
	if (cpu == 0 && CPU_VAR(PCPU_GET(midr)) == 0 &&
	    CPU_MATCH_ERRATA_CAVIUM_THUNDERX_1_1)
		printf("WARNING: ThunderX Pass 1.1 detected.\nThis has known "
		    "hardware bugs that may cause the incorrect operation of "
		    "atomic operations.\n");

	if (cpu != 0 && cpu_print_regs == 0)
		return;

#define SEP_STR	((printed++) == 0) ? "" : ","

	/* AArch64 Instruction Set Attribute Register 0 */
	if (cpu == 0 || (cpu_print_regs & PRINT_ID_AA64_ISAR0) != 0) {
		printed = 0;
		sbuf_printf(sb, " Instruction Set Attributes 0 = <");

		switch (ID_AA64ISAR0_DP(cpu_desc[cpu].id_aa64isar0)) {
		case ID_AA64ISAR0_DP_NONE:
			break;
		case ID_AA64ISAR0_DP_IMPL:
			sbuf_printf(sb, "%sDotProd", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown DP", SEP_STR);
			break;
		}

		switch (ID_AA64ISAR0_SM4(cpu_desc[cpu].id_aa64isar0)) {
		case ID_AA64ISAR0_SM4_NONE:
			break;
		case ID_AA64ISAR0_SM4_IMPL:
			sbuf_printf(sb, "%sSM4", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown SM4", SEP_STR);
			break;
		}

		switch (ID_AA64ISAR0_SM3(cpu_desc[cpu].id_aa64isar0)) {
		case ID_AA64ISAR0_SM3_NONE:
			break;
		case ID_AA64ISAR0_SM3_IMPL:
			sbuf_printf(sb, "%sSM3", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown SM3", SEP_STR);
			break;
		}

		switch (ID_AA64ISAR0_SHA3(cpu_desc[cpu].id_aa64isar0)) {
		case ID_AA64ISAR0_SHA3_NONE:
			break;
		case ID_AA64ISAR0_SHA3_IMPL:
			sbuf_printf(sb, "%sSHA3", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown SHA3", SEP_STR);
			break;
		}

		switch (ID_AA64ISAR0_RDM(cpu_desc[cpu].id_aa64isar0)) {
		case ID_AA64ISAR0_RDM_NONE:
			break;
		case ID_AA64ISAR0_RDM_IMPL:
			sbuf_printf(sb, "%sRDM", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown RDM", SEP_STR);
		}

		switch (ID_AA64ISAR0_ATOMIC(cpu_desc[cpu].id_aa64isar0)) {
		case ID_AA64ISAR0_ATOMIC_NONE:
			break;
		case ID_AA64ISAR0_ATOMIC_IMPL:
			sbuf_printf(sb, "%sAtomic", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown Atomic", SEP_STR);
		}

		switch (ID_AA64ISAR0_CRC32(cpu_desc[cpu].id_aa64isar0)) {
		case ID_AA64ISAR0_CRC32_NONE:
			break;
		case ID_AA64ISAR0_CRC32_BASE:
			sbuf_printf(sb, "%sCRC32", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown CRC32", SEP_STR);
			break;
		}

		switch (ID_AA64ISAR0_SHA2(cpu_desc[cpu].id_aa64isar0)) {
		case ID_AA64ISAR0_SHA2_NONE:
			break;
		case ID_AA64ISAR0_SHA2_BASE:
			sbuf_printf(sb, "%sSHA2", SEP_STR);
			break;
		case ID_AA64ISAR0_SHA2_512:
			sbuf_printf(sb, "%sSHA2+SHA512", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown SHA2", SEP_STR);
			break;
		}

		switch (ID_AA64ISAR0_SHA1(cpu_desc[cpu].id_aa64isar0)) {
		case ID_AA64ISAR0_SHA1_NONE:
			break;
		case ID_AA64ISAR0_SHA1_BASE:
			sbuf_printf(sb, "%sSHA1", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown SHA1", SEP_STR);
			break;
		}

		switch (ID_AA64ISAR0_AES(cpu_desc[cpu].id_aa64isar0)) {
		case ID_AA64ISAR0_AES_NONE:
			break;
		case ID_AA64ISAR0_AES_BASE:
			sbuf_printf(sb, "%sAES", SEP_STR);
			break;
		case ID_AA64ISAR0_AES_PMULL:
			sbuf_printf(sb, "%sAES+PMULL", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown AES", SEP_STR);
			break;
		}

		if ((cpu_desc[cpu].id_aa64isar0 & ~ID_AA64ISAR0_MASK) != 0)
			sbuf_printf(sb, "%s%#lx", SEP_STR,
			    cpu_desc[cpu].id_aa64isar0 & ~ID_AA64ISAR0_MASK);

		sbuf_finish(sb);
		printf("%s>\n", sbuf_data(sb));
		sbuf_clear(sb);
	}

	/* AArch64 Instruction Set Attribute Register 1 */
	if (cpu == 0 || (cpu_print_regs & PRINT_ID_AA64_ISAR1) != 0) {
		printed = 0;
		sbuf_printf(sb, " Instruction Set Attributes 1 = <");

		switch (ID_AA64ISAR1_GPI(cpu_desc[cpu].id_aa64isar1)) {
		case ID_AA64ISAR1_GPI_NONE:
			break;
		case ID_AA64ISAR1_GPI_IMPL:
			sbuf_printf(sb, "%sImpl GenericAuth", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown GenericAuth", SEP_STR);
			break;
		}

		switch (ID_AA64ISAR1_GPA(cpu_desc[cpu].id_aa64isar1)) {
		case ID_AA64ISAR1_GPA_NONE:
			break;
		case ID_AA64ISAR1_GPA_IMPL:
			sbuf_printf(sb, "%sPrince GenericAuth", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown GenericAuth", SEP_STR);
			break;
		}

		switch (ID_AA64ISAR1_LRCPC(cpu_desc[cpu].id_aa64isar1)) {
		case ID_AA64ISAR1_LRCPC_NONE:
			break;
		case ID_AA64ISAR1_LRCPC_IMPL:
			sbuf_printf(sb, "%sRCpc", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown RCpc", SEP_STR);
			break;
		}

		switch (ID_AA64ISAR1_FCMA(cpu_desc[cpu].id_aa64isar1)) {
		case ID_AA64ISAR1_FCMA_NONE:
			break;
		case ID_AA64ISAR1_FCMA_IMPL:
			sbuf_printf(sb, "%sFCMA", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown FCMA", SEP_STR);
			break;
		}

		switch (ID_AA64ISAR1_JSCVT(cpu_desc[cpu].id_aa64isar1)) {
		case ID_AA64ISAR1_JSCVT_NONE:
			break;
		case ID_AA64ISAR1_JSCVT_IMPL:
			sbuf_printf(sb, "%sJS Conv", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown JS Conv", SEP_STR);
			break;
		}

		switch (ID_AA64ISAR1_API(cpu_desc[cpu].id_aa64isar1)) {
		case ID_AA64ISAR1_API_NONE:
			break;
		case ID_AA64ISAR1_API_IMPL:
			sbuf_printf(sb, "%sImpl AddrAuth", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown Impl AddrAuth", SEP_STR);
			break;
		}

		switch (ID_AA64ISAR1_APA(cpu_desc[cpu].id_aa64isar1)) {
		case ID_AA64ISAR1_APA_NONE:
			break;
		case ID_AA64ISAR1_APA_IMPL:
			sbuf_printf(sb, "%sPrince AddrAuth", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown Prince AddrAuth", SEP_STR);
			break;
		}

		switch (ID_AA64ISAR1_DPB(cpu_desc[cpu].id_aa64isar1)) {
		case ID_AA64ISAR1_DPB_NONE:
			break;
		case ID_AA64ISAR1_DPB_IMPL:
			sbuf_printf(sb, "%sDC CVAP", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown DC CVAP", SEP_STR);
			break;
		}

		if ((cpu_desc[cpu].id_aa64isar1 & ~ID_AA64ISAR1_MASK) != 0)
			sbuf_printf(sb, "%s%#lx", SEP_STR,
			    cpu_desc[cpu].id_aa64isar1 & ~ID_AA64ISAR1_MASK);
		sbuf_finish(sb);
		printf("%s>\n", sbuf_data(sb));
		sbuf_clear(sb);
	}

	/* AArch64 Processor Feature Register 0 */
	if (cpu == 0 || (cpu_print_regs & PRINT_ID_AA64_PFR0) != 0) {
		printed = 0;
		sbuf_printf(sb, "         Processor Features 0 = <");

		switch (ID_AA64PFR0_SVE(cpu_desc[cpu].id_aa64pfr0)) {
		case ID_AA64PFR0_SVE_NONE:
			break;
		case ID_AA64PFR0_SVE_IMPL:
			sbuf_printf(sb, "%sSVE", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown SVE", SEP_STR);
			break;
		}

		switch (ID_AA64PFR0_RAS(cpu_desc[cpu].id_aa64pfr0)) {
		case ID_AA64PFR0_RAS_NONE:
			break;
		case ID_AA64PFR0_RAS_V1:
			sbuf_printf(sb, "%sRASv1", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown RAS", SEP_STR);
			break;
		}

		switch (ID_AA64PFR0_GIC(cpu_desc[cpu].id_aa64pfr0)) {
		case ID_AA64PFR0_GIC_CPUIF_NONE:
			break;
		case ID_AA64PFR0_GIC_CPUIF_EN:
			sbuf_printf(sb, "%sGIC", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown GIC interface", SEP_STR);
			break;
		}

		switch (ID_AA64PFR0_ADV_SIMD(cpu_desc[cpu].id_aa64pfr0)) {
		case ID_AA64PFR0_ADV_SIMD_NONE:
			break;
		case ID_AA64PFR0_ADV_SIMD_IMPL:
			sbuf_printf(sb, "%sAdvSIMD", SEP_STR);
			break;
		case ID_AA64PFR0_ADV_SIMD_HP:
			sbuf_printf(sb, "%sAdvSIMD+HP", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown AdvSIMD", SEP_STR);
			break;
		}

		switch (ID_AA64PFR0_FP(cpu_desc[cpu].id_aa64pfr0)) {
		case ID_AA64PFR0_FP_NONE:
			break;
		case ID_AA64PFR0_FP_IMPL:
			sbuf_printf(sb, "%sFloat", SEP_STR);
			break;
		case ID_AA64PFR0_FP_HP:
			sbuf_printf(sb, "%sFloat+HP", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown Float", SEP_STR);
			break;
		}

		switch (ID_AA64PFR0_EL3(cpu_desc[cpu].id_aa64pfr0)) {
		case ID_AA64PFR0_EL3_NONE:
			sbuf_printf(sb, "%sNo EL3", SEP_STR);
			break;
		case ID_AA64PFR0_EL3_64:
			sbuf_printf(sb, "%sEL3", SEP_STR);
			break;
		case ID_AA64PFR0_EL3_64_32:
			sbuf_printf(sb, "%sEL3 32", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown EL3", SEP_STR);
			break;
		}

		switch (ID_AA64PFR0_EL2(cpu_desc[cpu].id_aa64pfr0)) {
		case ID_AA64PFR0_EL2_NONE:
			sbuf_printf(sb, "%sNo EL2", SEP_STR);
			break;
		case ID_AA64PFR0_EL2_64:
			sbuf_printf(sb, "%sEL2", SEP_STR);
			break;
		case ID_AA64PFR0_EL2_64_32:
			sbuf_printf(sb, "%sEL2 32", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown EL2", SEP_STR);
			break;
		}

		switch (ID_AA64PFR0_EL1(cpu_desc[cpu].id_aa64pfr0)) {
		case ID_AA64PFR0_EL1_64:
			sbuf_printf(sb, "%sEL1", SEP_STR);
			break;
		case ID_AA64PFR0_EL1_64_32:
			sbuf_printf(sb, "%sEL1 32", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown EL1", SEP_STR);
			break;
		}

		switch (ID_AA64PFR0_EL0(cpu_desc[cpu].id_aa64pfr0)) {
		case ID_AA64PFR0_EL0_64:
			sbuf_printf(sb, "%sEL0", SEP_STR);
			break;
		case ID_AA64PFR0_EL0_64_32:
			sbuf_printf(sb, "%sEL0 32", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown EL0", SEP_STR);
			break;
		}

		if ((cpu_desc[cpu].id_aa64pfr0 & ~ID_AA64PFR0_MASK) != 0)
			sbuf_printf(sb, "%s%#lx", SEP_STR,
			    cpu_desc[cpu].id_aa64pfr0 & ~ID_AA64PFR0_MASK);

		sbuf_finish(sb);
		printf("%s>\n", sbuf_data(sb));
		sbuf_clear(sb);
	}

	/* AArch64 Processor Feature Register 1 */
	if (cpu == 0 || (cpu_print_regs & PRINT_ID_AA64_PFR1) != 0) {
		printf("         Processor Features 1 = <%#lx>\n",
		    cpu_desc[cpu].id_aa64pfr1);
	}

	/* AArch64 Memory Model Feature Register 0 */
	if (cpu == 0 || (cpu_print_regs & PRINT_ID_AA64_MMFR0) != 0) {
		printed = 0;
		sbuf_printf(sb, "      Memory Model Features 0 = <");
		switch (ID_AA64MMFR0_TGRAN4(cpu_desc[cpu].id_aa64mmfr0)) {
		case ID_AA64MMFR0_TGRAN4_NONE:
			break;
		case ID_AA64MMFR0_TGRAN4_IMPL:
			sbuf_printf(sb, "%s4k Granule", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown 4k Granule", SEP_STR);
			break;
		}

		switch (ID_AA64MMFR0_TGRAN64(cpu_desc[cpu].id_aa64mmfr0)) {
		case ID_AA64MMFR0_TGRAN64_NONE:
			break;
		case ID_AA64MMFR0_TGRAN64_IMPL:
			sbuf_printf(sb, "%s64k Granule", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown 64k Granule", SEP_STR);
			break;
		}

		switch (ID_AA64MMFR0_TGRAN16(cpu_desc[cpu].id_aa64mmfr0)) {
		case ID_AA64MMFR0_TGRAN16_NONE:
			break;
		case ID_AA64MMFR0_TGRAN16_IMPL:
			sbuf_printf(sb, "%s16k Granule", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown 16k Granule", SEP_STR);
			break;
		}

		switch (ID_AA64MMFR0_BIGEND_EL0(cpu_desc[cpu].id_aa64mmfr0)) {
		case ID_AA64MMFR0_BIGEND_EL0_FIXED:
			break;
		case ID_AA64MMFR0_BIGEND_EL0_MIXED:
			sbuf_printf(sb, "%sEL0 MixEndian", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown EL0 Endian switching", SEP_STR);
			break;
		}

		switch (ID_AA64MMFR0_S_NS_MEM(cpu_desc[cpu].id_aa64mmfr0)) {
		case ID_AA64MMFR0_S_NS_MEM_NONE:
			break;
		case ID_AA64MMFR0_S_NS_MEM_DISTINCT:
			sbuf_printf(sb, "%sS/NS Mem", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown S/NS Mem", SEP_STR);
			break;
		}

		switch (ID_AA64MMFR0_BIGEND(cpu_desc[cpu].id_aa64mmfr0)) {
		case ID_AA64MMFR0_BIGEND_FIXED:
			break;
		case ID_AA64MMFR0_BIGEND_MIXED:
			sbuf_printf(sb, "%sMixedEndian", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown Endian switching", SEP_STR);
			break;
		}

		switch (ID_AA64MMFR0_ASID_BITS(cpu_desc[cpu].id_aa64mmfr0)) {
		case ID_AA64MMFR0_ASID_BITS_8:
			sbuf_printf(sb, "%s8bit ASID", SEP_STR);
			break;
		case ID_AA64MMFR0_ASID_BITS_16:
			sbuf_printf(sb, "%s16bit ASID", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown ASID", SEP_STR);
			break;
		}

		switch (ID_AA64MMFR0_PA_RANGE(cpu_desc[cpu].id_aa64mmfr0)) {
		case ID_AA64MMFR0_PA_RANGE_4G:
			sbuf_printf(sb, "%s4GB PA", SEP_STR);
			break;
		case ID_AA64MMFR0_PA_RANGE_64G:
			sbuf_printf(sb, "%s64GB PA", SEP_STR);
			break;
		case ID_AA64MMFR0_PA_RANGE_1T:
			sbuf_printf(sb, "%s1TB PA", SEP_STR);
			break;
		case ID_AA64MMFR0_PA_RANGE_4T:
			sbuf_printf(sb, "%s4TB PA", SEP_STR);
			break;
		case ID_AA64MMFR0_PA_RANGE_16T:
			sbuf_printf(sb, "%s16TB PA", SEP_STR);
			break;
		case ID_AA64MMFR0_PA_RANGE_256T:
			sbuf_printf(sb, "%s256TB PA", SEP_STR);
			break;
		case ID_AA64MMFR0_PA_RANGE_4P:
			sbuf_printf(sb, "%s4PB PA", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown PA Range", SEP_STR);
			break;
		}

		if ((cpu_desc[cpu].id_aa64mmfr0 & ~ID_AA64MMFR0_MASK) != 0)
			sbuf_printf(sb, "%s%#lx", SEP_STR,
			    cpu_desc[cpu].id_aa64mmfr0 & ~ID_AA64MMFR0_MASK);
		sbuf_finish(sb);
		printf("%s>\n", sbuf_data(sb));
		sbuf_clear(sb);
	}

	/* AArch64 Memory Model Feature Register 1 */
	if (cpu == 0 || (cpu_print_regs & PRINT_ID_AA64_MMFR1) != 0) {
		printed = 0;
		sbuf_printf(sb, "      Memory Model Features 1 = <");

		switch (ID_AA64MMFR1_XNX(cpu_desc[cpu].id_aa64mmfr1)) {
		case ID_AA64MMFR1_XNX_NONE:
			break;
		case ID_AA64MMFR1_XNX_IMPL:
			sbuf_printf(sb, "%sEL2 XN", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown XNX", SEP_STR);
			break;
		}

		switch (ID_AA64MMFR1_SPEC_SEI(cpu_desc[cpu].id_aa64mmfr1)) {
		case ID_AA64MMFR1_SPEC_SEI_NONE:
			break;
		case ID_AA64MMFR1_SPEC_SEI_IMPL:
			sbuf_printf(sb, "%sSpecSEI", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown SpecSEI", SEP_STR);
			break;
		}

		switch (ID_AA64MMFR1_PAN(cpu_desc[cpu].id_aa64mmfr1)) {
		case ID_AA64MMFR1_PAN_NONE:
			break;
		case ID_AA64MMFR1_PAN_IMPL:
			sbuf_printf(sb, "%sPAN", SEP_STR);
			break;
		case ID_AA64MMFR1_PAN_ATS1E1:
			sbuf_printf(sb, "%sPAN+AT", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown PAN", SEP_STR);
			break;
		}

		switch (ID_AA64MMFR1_LO(cpu_desc[cpu].id_aa64mmfr1)) {
		case ID_AA64MMFR1_LO_NONE:
			break;
		case ID_AA64MMFR1_LO_IMPL:
			sbuf_printf(sb, "%sLO", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown LO", SEP_STR);
			break;
		}

		switch (ID_AA64MMFR1_HPDS(cpu_desc[cpu].id_aa64mmfr1)) {
		case ID_AA64MMFR1_HPDS_NONE:
			break;
		case ID_AA64MMFR1_HPDS_HPD:
			sbuf_printf(sb, "%sHPDS", SEP_STR);
			break;
		case ID_AA64MMFR1_HPDS_TTPBHA:
			sbuf_printf(sb, "%sTTPBHA", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown HPDS", SEP_STR);
			break;
		}

		switch (ID_AA64MMFR1_VH(cpu_desc[cpu].id_aa64mmfr1)) {
		case ID_AA64MMFR1_VH_NONE:
			break;
		case ID_AA64MMFR1_VH_IMPL:
			sbuf_printf(sb, "%sVHE", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown VHE", SEP_STR);
			break;
		}

		switch (ID_AA64MMFR1_VMIDBITS(cpu_desc[cpu].id_aa64mmfr1)) {
		case ID_AA64MMFR1_VMIDBITS_8:
			break;
		case ID_AA64MMFR1_VMIDBITS_16:
			sbuf_printf(sb, "%s16 VMID bits", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown VMID bits", SEP_STR);
			break;
		}

		switch (ID_AA64MMFR1_HAFDBS(cpu_desc[cpu].id_aa64mmfr1)) {
		case ID_AA64MMFR1_HAFDBS_NONE:
			break;
		case ID_AA64MMFR1_HAFDBS_AF:
			sbuf_printf(sb, "%sAF", SEP_STR);
			break;
		case ID_AA64MMFR1_HAFDBS_AF_DBS:
			sbuf_printf(sb, "%sAF+DBS", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown Hardware update AF/DBS", SEP_STR);
			break;
		}

		if ((cpu_desc[cpu].id_aa64mmfr1 & ~ID_AA64MMFR1_MASK) != 0)
			sbuf_printf(sb, "%s%#lx", SEP_STR,
			    cpu_desc[cpu].id_aa64mmfr1 & ~ID_AA64MMFR1_MASK);
		sbuf_finish(sb);
		printf("%s>\n", sbuf_data(sb));
		sbuf_clear(sb);
	}

	/* AArch64 Memory Model Feature Register 2 */
	if (cpu == 0 || (cpu_print_regs & PRINT_ID_AA64_MMFR2) != 0) {
		printed = 0;
		sbuf_printf(sb, "      Memory Model Features 2 = <");

		switch (ID_AA64MMFR2_NV(cpu_desc[cpu].id_aa64mmfr2)) {
		case ID_AA64MMFR2_NV_NONE:
			break;
		case ID_AA64MMFR2_NV_IMPL:
			sbuf_printf(sb, "%sNestedVirt", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown NestedVirt", SEP_STR);
			break;
		}

		switch (ID_AA64MMFR2_CCIDX(cpu_desc[cpu].id_aa64mmfr2)) {
		case ID_AA64MMFR2_CCIDX_32:
			sbuf_printf(sb, "%s32b CCIDX", SEP_STR);
			break;
		case ID_AA64MMFR2_CCIDX_64:
			sbuf_printf(sb, "%s64b CCIDX", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown CCIDX", SEP_STR);
			break;
		}

		switch (ID_AA64MMFR2_VA_RANGE(cpu_desc[cpu].id_aa64mmfr2)) {
		case ID_AA64MMFR2_VA_RANGE_48:
			sbuf_printf(sb, "%s48b VA", SEP_STR);
			break;
		case ID_AA64MMFR2_VA_RANGE_52:
			sbuf_printf(sb, "%s52b VA", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown VA Range", SEP_STR);
			break;
		}

		switch (ID_AA64MMFR2_IESB(cpu_desc[cpu].id_aa64mmfr2)) {
		case ID_AA64MMFR2_IESB_NONE:
			break;
		case ID_AA64MMFR2_IESB_IMPL:
			sbuf_printf(sb, "%sIESB", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown IESB", SEP_STR);
			break;
		}

		switch (ID_AA64MMFR2_LSM(cpu_desc[cpu].id_aa64mmfr2)) {
		case ID_AA64MMFR2_LSM_NONE:
			break;
		case ID_AA64MMFR2_LSM_IMPL:
			sbuf_printf(sb, "%sLSM", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown LSM", SEP_STR);
			break;
		}

		switch (ID_AA64MMFR2_UAO(cpu_desc[cpu].id_aa64mmfr2)) {
		case ID_AA64MMFR2_UAO_NONE:
			break;
		case ID_AA64MMFR2_UAO_IMPL:
			sbuf_printf(sb, "%sUAO", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown UAO", SEP_STR);
			break;
		}

		switch (ID_AA64MMFR2_CNP(cpu_desc[cpu].id_aa64mmfr2)) {
		case ID_AA64MMFR2_CNP_NONE:
			break;
		case ID_AA64MMFR2_CNP_IMPL:
			sbuf_printf(sb, "%sCnP", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown CnP", SEP_STR);
			break;
		}

		if ((cpu_desc[cpu].id_aa64mmfr2 & ~ID_AA64MMFR2_MASK) != 0)
			sbuf_printf(sb, "%s%#lx", SEP_STR,
			    cpu_desc[cpu].id_aa64mmfr2 & ~ID_AA64MMFR2_MASK);
		sbuf_finish(sb);
		printf("%s>\n", sbuf_data(sb));
		sbuf_clear(sb);
	}

	/* AArch64 Debug Feature Register 0 */
	if (cpu == 0 || (cpu_print_regs & PRINT_ID_AA64_DFR0) != 0) {
		printed = 0;
		sbuf_printf(sb, "             Debug Features 0 = <");
		switch(ID_AA64DFR0_PMS_VER(cpu_desc[cpu].id_aa64dfr0)) {
		case ID_AA64DFR0_PMS_VER_NONE:
			break;
		case ID_AA64DFR0_PMS_VER_V1:
			sbuf_printf(sb, "%sSPE v1", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown SPE", SEP_STR);
			break;
		}

		sbuf_printf(sb, "%s%lu CTX Breakpoints", SEP_STR,
		    ID_AA64DFR0_CTX_CMPS(cpu_desc[cpu].id_aa64dfr0));

		sbuf_printf(sb, "%s%lu Watchpoints", SEP_STR,
		    ID_AA64DFR0_WRPS(cpu_desc[cpu].id_aa64dfr0));

		sbuf_printf(sb, "%s%lu Breakpoints", SEP_STR,
		    ID_AA64DFR0_BRPS(cpu_desc[cpu].id_aa64dfr0));

		switch (ID_AA64DFR0_PMU_VER(cpu_desc[cpu].id_aa64dfr0)) {
		case ID_AA64DFR0_PMU_VER_NONE:
			break;
		case ID_AA64DFR0_PMU_VER_3:
			sbuf_printf(sb, "%sPMUv3", SEP_STR);
			break;
		case ID_AA64DFR0_PMU_VER_3_1:
			sbuf_printf(sb, "%sPMUv3+16 bit evtCount", SEP_STR);
			break;
		case ID_AA64DFR0_PMU_VER_IMPL:
			sbuf_printf(sb, "%sImplementation defined PMU", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown PMU", SEP_STR);
			break;
		}

		switch (ID_AA64DFR0_TRACE_VER(cpu_desc[cpu].id_aa64dfr0)) {
		case ID_AA64DFR0_TRACE_VER_NONE:
			break;
		case ID_AA64DFR0_TRACE_VER_IMPL:
			sbuf_printf(sb, "%sTrace", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown Trace", SEP_STR);
			break;
		}

		switch (ID_AA64DFR0_DEBUG_VER(cpu_desc[cpu].id_aa64dfr0)) {
		case ID_AA64DFR0_DEBUG_VER_8:
			sbuf_printf(sb, "%sDebug v8", SEP_STR);
			break;
		case ID_AA64DFR0_DEBUG_VER_8_VHE:
			sbuf_printf(sb, "%sDebug v8+VHE", SEP_STR);
			break;
		case ID_AA64DFR0_DEBUG_VER_8_2:
			sbuf_printf(sb, "%sDebug v8.2", SEP_STR);
			break;
		default:
			sbuf_printf(sb, "%sUnknown Debug", SEP_STR);
			break;
		}

		if (cpu_desc[cpu].id_aa64dfr0 & ~ID_AA64DFR0_MASK)
			sbuf_printf(sb, "%s%#lx", SEP_STR,
			    cpu_desc[cpu].id_aa64dfr0 & ~ID_AA64DFR0_MASK);
		sbuf_finish(sb);
		printf("%s>\n", sbuf_data(sb));
		sbuf_clear(sb);
	}

	/* AArch64 Memory Model Feature Register 1 */
	if (cpu == 0 || (cpu_print_regs & PRINT_ID_AA64_DFR1) != 0) {
		printf("             Debug Features 1 = <%#lx>\n",
		    cpu_desc[cpu].id_aa64dfr1);
	}

	/* AArch64 Auxiliary Feature Register 0 */
	if (cpu == 0 || (cpu_print_regs & PRINT_ID_AA64_AFR0) != 0) {
		printf("         Auxiliary Features 0 = <%#lx>\n",
		    cpu_desc[cpu].id_aa64afr0);
	}

	/* AArch64 Auxiliary Feature Register 1 */
	if (cpu == 0 || (cpu_print_regs & PRINT_ID_AA64_AFR1) != 0) {
		printf("         Auxiliary Features 1 = <%#lx>\n",
		    cpu_desc[cpu].id_aa64afr1);
	}

	sbuf_delete(sb);
	sb = NULL;
#undef SEP_STR
}

void
identify_cpu(void)
{
	u_int midr;
	u_int impl_id;
	u_int part_id;
	u_int cpu;
	size_t i;
	const struct cpu_parts *cpu_partsp = NULL;

	cpu = PCPU_GET(cpuid);
	midr = get_midr();

	/*
	 * Store midr to pcpu to allow fast reading
	 * from EL0, EL1 and assembly code.
	 */
	PCPU_SET(midr, midr);

	impl_id = CPU_IMPL(midr);
	for (i = 0; i < nitems(cpu_implementers); i++) {
		if (impl_id == cpu_implementers[i].impl_id ||
		    cpu_implementers[i].impl_id == 0) {
			cpu_desc[cpu].cpu_impl = impl_id;
			cpu_desc[cpu].cpu_impl_name = cpu_implementers[i].impl_name;
			cpu_partsp = cpu_implementers[i].cpu_parts;
			break;
		}
	}

	part_id = CPU_PART(midr);
	for (i = 0; &cpu_partsp[i] != NULL; i++) {
		if (part_id == cpu_partsp[i].part_id ||
		    cpu_partsp[i].part_id == 0) {
			cpu_desc[cpu].cpu_part_num = part_id;
			cpu_desc[cpu].cpu_part_name = cpu_partsp[i].part_name;
			break;
		}
	}

	cpu_desc[cpu].cpu_revision = CPU_REV(midr);
	cpu_desc[cpu].cpu_variant = CPU_VAR(midr);

	/* Save affinity for current CPU */
	cpu_desc[cpu].mpidr = get_mpidr();
	CPU_AFFINITY(cpu) = cpu_desc[cpu].mpidr & CPU_AFF_MASK;

	cpu_desc[cpu].id_aa64dfr0 = READ_SPECIALREG(ID_AA64DFR0_EL1);
	cpu_desc[cpu].id_aa64dfr1 = READ_SPECIALREG(ID_AA64DFR1_EL1);
	cpu_desc[cpu].id_aa64isar0 = READ_SPECIALREG(ID_AA64ISAR0_EL1);
	cpu_desc[cpu].id_aa64isar1 = READ_SPECIALREG(ID_AA64ISAR1_EL1);
	cpu_desc[cpu].id_aa64mmfr0 = READ_SPECIALREG(ID_AA64MMFR0_EL1);
	cpu_desc[cpu].id_aa64mmfr1 = READ_SPECIALREG(ID_AA64MMFR1_EL1);
	cpu_desc[cpu].id_aa64mmfr2 = READ_SPECIALREG(ID_AA64MMFR2_EL1);
	cpu_desc[cpu].id_aa64pfr0 = READ_SPECIALREG(ID_AA64PFR0_EL1);
	cpu_desc[cpu].id_aa64pfr1 = READ_SPECIALREG(ID_AA64PFR1_EL1);

	if (cpu != 0) {
		/*
		 * This code must run on one cpu at a time, but we are
		 * not scheduling on the current core so implement a
		 * simple spinlock.
		 */
		while (atomic_cmpset_acq_int(&ident_lock, 0, 1) == 0)
			__asm __volatile("wfe" ::: "memory");

		switch (cpu_aff_levels) {
		case 0:
			if (CPU_AFF0(cpu_desc[cpu].mpidr) !=
			    CPU_AFF0(cpu_desc[0].mpidr))
				cpu_aff_levels = 1;
			/* FALLTHROUGH */
		case 1:
			if (CPU_AFF1(cpu_desc[cpu].mpidr) !=
			    CPU_AFF1(cpu_desc[0].mpidr))
				cpu_aff_levels = 2;
			/* FALLTHROUGH */
		case 2:
			if (CPU_AFF2(cpu_desc[cpu].mpidr) !=
			    CPU_AFF2(cpu_desc[0].mpidr))
				cpu_aff_levels = 3;
			/* FALLTHROUGH */
		case 3:
			if (CPU_AFF3(cpu_desc[cpu].mpidr) !=
			    CPU_AFF3(cpu_desc[0].mpidr))
				cpu_aff_levels = 4;
			break;
		}

		if (cpu_desc[cpu].id_aa64afr0 != cpu_desc[0].id_aa64afr0)
			cpu_print_regs |= PRINT_ID_AA64_AFR0;
		if (cpu_desc[cpu].id_aa64afr1 != cpu_desc[0].id_aa64afr1)
			cpu_print_regs |= PRINT_ID_AA64_AFR1;

		if (cpu_desc[cpu].id_aa64dfr0 != cpu_desc[0].id_aa64dfr0)
			cpu_print_regs |= PRINT_ID_AA64_DFR0;
		if (cpu_desc[cpu].id_aa64dfr1 != cpu_desc[0].id_aa64dfr1)
			cpu_print_regs |= PRINT_ID_AA64_DFR1;

		if (cpu_desc[cpu].id_aa64isar0 != cpu_desc[0].id_aa64isar0)
			cpu_print_regs |= PRINT_ID_AA64_ISAR0;
		if (cpu_desc[cpu].id_aa64isar1 != cpu_desc[0].id_aa64isar1)
			cpu_print_regs |= PRINT_ID_AA64_ISAR1;

		if (cpu_desc[cpu].id_aa64mmfr0 != cpu_desc[0].id_aa64mmfr0)
			cpu_print_regs |= PRINT_ID_AA64_MMFR0;
		if (cpu_desc[cpu].id_aa64mmfr1 != cpu_desc[0].id_aa64mmfr1)
			cpu_print_regs |= PRINT_ID_AA64_MMFR1;
		if (cpu_desc[cpu].id_aa64mmfr2 != cpu_desc[0].id_aa64mmfr2)
			cpu_print_regs |= PRINT_ID_AA64_MMFR2;

		if (cpu_desc[cpu].id_aa64pfr0 != cpu_desc[0].id_aa64pfr0)
			cpu_print_regs |= PRINT_ID_AA64_PFR0;
		if (cpu_desc[cpu].id_aa64pfr1 != cpu_desc[0].id_aa64pfr1)
			cpu_print_regs |= PRINT_ID_AA64_PFR1;

		/* Wake up the other CPUs */
		atomic_store_rel_int(&ident_lock, 0);
		__asm __volatile("sev" ::: "memory");
	}
}
