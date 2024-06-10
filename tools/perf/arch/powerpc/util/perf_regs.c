// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <string.h>
#include <regex.h>
#include <linux/zalloc.h>

#include "perf_regs.h"
#include "../../../util/perf_regs.h"
#include "../../../util/debug.h"
#include "../../../util/event.h"
#include "../../../util/header.h"
#include "../../../perf-sys.h"
#include "utils_header.h"

#include <linux/kernel.h>

#define PVR_POWER9		0x004E
#define PVR_POWER10		0x0080

static const struct sample_reg sample_reg_masks[] = {
	SMPL_REG(r0, PERF_REG_POWERPC_R0),
	SMPL_REG(r1, PERF_REG_POWERPC_R1),
	SMPL_REG(r2, PERF_REG_POWERPC_R2),
	SMPL_REG(r3, PERF_REG_POWERPC_R3),
	SMPL_REG(r4, PERF_REG_POWERPC_R4),
	SMPL_REG(r5, PERF_REG_POWERPC_R5),
	SMPL_REG(r6, PERF_REG_POWERPC_R6),
	SMPL_REG(r7, PERF_REG_POWERPC_R7),
	SMPL_REG(r8, PERF_REG_POWERPC_R8),
	SMPL_REG(r9, PERF_REG_POWERPC_R9),
	SMPL_REG(r10, PERF_REG_POWERPC_R10),
	SMPL_REG(r11, PERF_REG_POWERPC_R11),
	SMPL_REG(r12, PERF_REG_POWERPC_R12),
	SMPL_REG(r13, PERF_REG_POWERPC_R13),
	SMPL_REG(r14, PERF_REG_POWERPC_R14),
	SMPL_REG(r15, PERF_REG_POWERPC_R15),
	SMPL_REG(r16, PERF_REG_POWERPC_R16),
	SMPL_REG(r17, PERF_REG_POWERPC_R17),
	SMPL_REG(r18, PERF_REG_POWERPC_R18),
	SMPL_REG(r19, PERF_REG_POWERPC_R19),
	SMPL_REG(r20, PERF_REG_POWERPC_R20),
	SMPL_REG(r21, PERF_REG_POWERPC_R21),
	SMPL_REG(r22, PERF_REG_POWERPC_R22),
	SMPL_REG(r23, PERF_REG_POWERPC_R23),
	SMPL_REG(r24, PERF_REG_POWERPC_R24),
	SMPL_REG(r25, PERF_REG_POWERPC_R25),
	SMPL_REG(r26, PERF_REG_POWERPC_R26),
	SMPL_REG(r27, PERF_REG_POWERPC_R27),
	SMPL_REG(r28, PERF_REG_POWERPC_R28),
	SMPL_REG(r29, PERF_REG_POWERPC_R29),
	SMPL_REG(r30, PERF_REG_POWERPC_R30),
	SMPL_REG(r31, PERF_REG_POWERPC_R31),
	SMPL_REG(nip, PERF_REG_POWERPC_NIP),
	SMPL_REG(msr, PERF_REG_POWERPC_MSR),
	SMPL_REG(orig_r3, PERF_REG_POWERPC_ORIG_R3),
	SMPL_REG(ctr, PERF_REG_POWERPC_CTR),
	SMPL_REG(link, PERF_REG_POWERPC_LINK),
	SMPL_REG(xer, PERF_REG_POWERPC_XER),
	SMPL_REG(ccr, PERF_REG_POWERPC_CCR),
	SMPL_REG(softe, PERF_REG_POWERPC_SOFTE),
	SMPL_REG(trap, PERF_REG_POWERPC_TRAP),
	SMPL_REG(dar, PERF_REG_POWERPC_DAR),
	SMPL_REG(dsisr, PERF_REG_POWERPC_DSISR),
	SMPL_REG(sier, PERF_REG_POWERPC_SIER),
	SMPL_REG(mmcra, PERF_REG_POWERPC_MMCRA),
	SMPL_REG(mmcr0, PERF_REG_POWERPC_MMCR0),
	SMPL_REG(mmcr1, PERF_REG_POWERPC_MMCR1),
	SMPL_REG(mmcr2, PERF_REG_POWERPC_MMCR2),
	SMPL_REG(mmcr3, PERF_REG_POWERPC_MMCR3),
	SMPL_REG(sier2, PERF_REG_POWERPC_SIER2),
	SMPL_REG(sier3, PERF_REG_POWERPC_SIER3),
	SMPL_REG(pmc1, PERF_REG_POWERPC_PMC1),
	SMPL_REG(pmc2, PERF_REG_POWERPC_PMC2),
	SMPL_REG(pmc3, PERF_REG_POWERPC_PMC3),
	SMPL_REG(pmc4, PERF_REG_POWERPC_PMC4),
	SMPL_REG(pmc5, PERF_REG_POWERPC_PMC5),
	SMPL_REG(pmc6, PERF_REG_POWERPC_PMC6),
	SMPL_REG(sdar, PERF_REG_POWERPC_SDAR),
	SMPL_REG(siar, PERF_REG_POWERPC_SIAR),
	SMPL_REG_END
};

/* REG or %rREG */
#define SDT_OP_REGEX1  "^(%r)?([1-2]?[0-9]|3[0-1])$"

/* -NUM(REG) or NUM(REG) or -NUM(%rREG) or NUM(%rREG) */
#define SDT_OP_REGEX2  "^(\\-)?([0-9]+)\\((%r)?([1-2]?[0-9]|3[0-1])\\)$"

static regex_t sdt_op_regex1, sdt_op_regex2;

static int sdt_init_op_regex(void)
{
	static int initialized;
	int ret = 0;

	if (initialized)
		return 0;

	ret = regcomp(&sdt_op_regex1, SDT_OP_REGEX1, REG_EXTENDED);
	if (ret)
		goto error;

	ret = regcomp(&sdt_op_regex2, SDT_OP_REGEX2, REG_EXTENDED);
	if (ret)
		goto free_regex1;

	initialized = 1;
	return 0;

free_regex1:
	regfree(&sdt_op_regex1);
error:
	pr_debug4("Regex compilation error.\n");
	return ret;
}

/*
 * Parse OP and convert it into uprobe format, which is, +/-NUM(%gprREG).
 * Possible variants of OP are:
 *	Format		Example
 *	-------------------------
 *	NUM(REG)	48(18)
 *	-NUM(REG)	-48(18)
 *	NUM(%rREG)	48(%r18)
 *	-NUM(%rREG)	-48(%r18)
 *	REG		18
 *	%rREG		%r18
 *	iNUM		i0
 *	i-NUM		i-1
 *
 * SDT marker arguments on Powerpc uses %rREG form with -mregnames flag
 * and REG form with -mno-regnames. Here REG is general purpose register,
 * which is in 0 to 31 range.
 */
int arch_sdt_arg_parse_op(char *old_op, char **new_op)
{
	int ret, new_len;
	regmatch_t rm[5];
	char prefix;

	/* Constant argument. Uprobe does not support it */
	if (old_op[0] == 'i') {
		pr_debug4("Skipping unsupported SDT argument: %s\n", old_op);
		return SDT_ARG_SKIP;
	}

	ret = sdt_init_op_regex();
	if (ret < 0)
		return ret;

	if (!regexec(&sdt_op_regex1, old_op, 3, rm, 0)) {
		/* REG or %rREG --> %gprREG */

		new_len = 5;	/* % g p r NULL */
		new_len += (int)(rm[2].rm_eo - rm[2].rm_so);

		*new_op = zalloc(new_len);
		if (!*new_op)
			return -ENOMEM;

		scnprintf(*new_op, new_len, "%%gpr%.*s",
			(int)(rm[2].rm_eo - rm[2].rm_so), old_op + rm[2].rm_so);
	} else if (!regexec(&sdt_op_regex2, old_op, 5, rm, 0)) {
		/*
		 * -NUM(REG) or NUM(REG) or -NUM(%rREG) or NUM(%rREG) -->
		 *	+/-NUM(%gprREG)
		 */
		prefix = (rm[1].rm_so == -1) ? '+' : '-';

		new_len = 8;	/* +/- ( % g p r ) NULL */
		new_len += (int)(rm[2].rm_eo - rm[2].rm_so);
		new_len += (int)(rm[4].rm_eo - rm[4].rm_so);

		*new_op = zalloc(new_len);
		if (!*new_op)
			return -ENOMEM;

		scnprintf(*new_op, new_len, "%c%.*s(%%gpr%.*s)", prefix,
			(int)(rm[2].rm_eo - rm[2].rm_so), old_op + rm[2].rm_so,
			(int)(rm[4].rm_eo - rm[4].rm_so), old_op + rm[4].rm_so);
	} else {
		pr_debug4("Skipping unsupported SDT argument: %s\n", old_op);
		return SDT_ARG_SKIP;
	}

	return SDT_ARG_VALID;
}

uint64_t arch__intr_reg_mask(void)
{
	struct perf_event_attr attr = {
		.type                   = PERF_TYPE_HARDWARE,
		.config                 = PERF_COUNT_HW_CPU_CYCLES,
		.sample_type            = PERF_SAMPLE_REGS_INTR,
		.precise_ip             = 1,
		.disabled               = 1,
		.exclude_kernel         = 1,
	};
	int fd;
	u32 version;
	u64 extended_mask = 0, mask = PERF_REGS_MASK;

	/*
	 * Get the PVR value to set the extended
	 * mask specific to platform.
	 */
	version = (((mfspr(SPRN_PVR)) >>  16) & 0xFFFF);
	if (version == PVR_POWER9)
		extended_mask = PERF_REG_PMU_MASK_300;
	else if (version == PVR_POWER10)
		extended_mask = PERF_REG_PMU_MASK_31;
	else
		return mask;

	attr.sample_regs_intr = extended_mask;
	attr.sample_period = 1;
	event_attr_init(&attr);

	/*
	 * check if the pmu supports perf extended regs, before
	 * returning the register mask to sample.
	 */
	fd = sys_perf_event_open(&attr, 0, -1, -1, 0);
	if (fd != -1) {
		close(fd);
		mask |= extended_mask;
	}
	return mask;
}

uint64_t arch__user_reg_mask(void)
{
	return PERF_REGS_MASK;
}

const struct sample_reg *arch__sample_reg_masks(void)
{
	return sample_reg_masks;
}
