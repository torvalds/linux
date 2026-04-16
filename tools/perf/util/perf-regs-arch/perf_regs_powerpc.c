// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
#include <string.h>
#include <regex.h>
#include <linux/zalloc.h>

#include "../debug.h"
#include "../event.h"
#include "../header.h"
#include "../perf_regs.h"
#include "../../perf-sys.h"
#include "../../arch/powerpc/util/utils_header.h"
#include "../../arch/powerpc/include/perf_regs.h"

#include <linux/kernel.h>

#define PVR_POWER9		0x004E
#define PVR_POWER10		0x0080
#define PVR_POWER11		0x0082

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
int __perf_sdt_arg_parse_op_powerpc(char *old_op, char **new_op)
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

/*
 * mfspr is a POWERPC specific instruction, ensure it's only
 * built and called on POWERPC by guarding with __powerpc64__
 * or __powerpc__.
 */
#if defined(__powerpc64__) && defined(__powerpc__)
uint64_t __perf_reg_mask_powerpc(bool intr)
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

	if (!intr)
		return PERF_REGS_MASK;

	/*
	 * Get the PVR value to set the extended
	 * mask specific to platform.
	 */
	version = (((mfspr(SPRN_PVR)) >>  16) & 0xFFFF);
	if (version == PVR_POWER9)
		extended_mask = PERF_REG_PMU_MASK_300;
	else if ((version == PVR_POWER10) || (version == PVR_POWER11))
		extended_mask = PERF_REG_PMU_MASK_31;
	else
		return mask;

	attr.sample_regs_intr = extended_mask;
	attr.sample_period = 1;
	event_attr_init(&attr);

	/*
	 * Check if the pmu supports perf extended regs, before
	 * returning the register mask to sample. Open the event
	 * on the perf process to check this.
	 */
	fd = sys_perf_event_open(&attr, /*pid=*/0, /*cpu=*/-1,
				 /*group_fd=*/-1, /*flags=*/0);
	if (fd != -1) {
		close(fd);
		mask |= extended_mask;
	}
	return mask;
}
#else
uint64_t __perf_reg_mask_powerpc(bool intr __maybe_unused)
{
	return PERF_REGS_MASK;
}
#endif

const char *__perf_reg_name_powerpc(int id)
{
	switch (id) {
	case PERF_REG_POWERPC_R0:
		return "r0";
	case PERF_REG_POWERPC_R1:
		return "r1";
	case PERF_REG_POWERPC_R2:
		return "r2";
	case PERF_REG_POWERPC_R3:
		return "r3";
	case PERF_REG_POWERPC_R4:
		return "r4";
	case PERF_REG_POWERPC_R5:
		return "r5";
	case PERF_REG_POWERPC_R6:
		return "r6";
	case PERF_REG_POWERPC_R7:
		return "r7";
	case PERF_REG_POWERPC_R8:
		return "r8";
	case PERF_REG_POWERPC_R9:
		return "r9";
	case PERF_REG_POWERPC_R10:
		return "r10";
	case PERF_REG_POWERPC_R11:
		return "r11";
	case PERF_REG_POWERPC_R12:
		return "r12";
	case PERF_REG_POWERPC_R13:
		return "r13";
	case PERF_REG_POWERPC_R14:
		return "r14";
	case PERF_REG_POWERPC_R15:
		return "r15";
	case PERF_REG_POWERPC_R16:
		return "r16";
	case PERF_REG_POWERPC_R17:
		return "r17";
	case PERF_REG_POWERPC_R18:
		return "r18";
	case PERF_REG_POWERPC_R19:
		return "r19";
	case PERF_REG_POWERPC_R20:
		return "r20";
	case PERF_REG_POWERPC_R21:
		return "r21";
	case PERF_REG_POWERPC_R22:
		return "r22";
	case PERF_REG_POWERPC_R23:
		return "r23";
	case PERF_REG_POWERPC_R24:
		return "r24";
	case PERF_REG_POWERPC_R25:
		return "r25";
	case PERF_REG_POWERPC_R26:
		return "r26";
	case PERF_REG_POWERPC_R27:
		return "r27";
	case PERF_REG_POWERPC_R28:
		return "r28";
	case PERF_REG_POWERPC_R29:
		return "r29";
	case PERF_REG_POWERPC_R30:
		return "r30";
	case PERF_REG_POWERPC_R31:
		return "r31";
	case PERF_REG_POWERPC_NIP:
		return "nip";
	case PERF_REG_POWERPC_MSR:
		return "msr";
	case PERF_REG_POWERPC_ORIG_R3:
		return "orig_r3";
	case PERF_REG_POWERPC_CTR:
		return "ctr";
	case PERF_REG_POWERPC_LINK:
		return "link";
	case PERF_REG_POWERPC_XER:
		return "xer";
	case PERF_REG_POWERPC_CCR:
		return "ccr";
	case PERF_REG_POWERPC_SOFTE:
		return "softe";
	case PERF_REG_POWERPC_TRAP:
		return "trap";
	case PERF_REG_POWERPC_DAR:
		return "dar";
	case PERF_REG_POWERPC_DSISR:
		return "dsisr";
	case PERF_REG_POWERPC_SIER:
		return "sier";
	case PERF_REG_POWERPC_MMCRA:
		return "mmcra";
	case PERF_REG_POWERPC_MMCR0:
		return "mmcr0";
	case PERF_REG_POWERPC_MMCR1:
		return "mmcr1";
	case PERF_REG_POWERPC_MMCR2:
		return "mmcr2";
	case PERF_REG_POWERPC_MMCR3:
		return "mmcr3";
	case PERF_REG_POWERPC_SIER2:
		return "sier2";
	case PERF_REG_POWERPC_SIER3:
		return "sier3";
	case PERF_REG_POWERPC_PMC1:
		return "pmc1";
	case PERF_REG_POWERPC_PMC2:
		return "pmc2";
	case PERF_REG_POWERPC_PMC3:
		return "pmc3";
	case PERF_REG_POWERPC_PMC4:
		return "pmc4";
	case PERF_REG_POWERPC_PMC5:
		return "pmc5";
	case PERF_REG_POWERPC_PMC6:
		return "pmc6";
	case PERF_REG_POWERPC_SDAR:
		return "sdar";
	case PERF_REG_POWERPC_SIAR:
		return "siar";
	default:
		break;
	}
	return NULL;
}

uint64_t __perf_reg_ip_powerpc(void)
{
	return PERF_REG_POWERPC_NIP;
}

uint64_t __perf_reg_sp_powerpc(void)
{
	return PERF_REG_POWERPC_R1;
}
