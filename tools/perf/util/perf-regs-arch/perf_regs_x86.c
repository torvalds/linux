// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
#include <string.h>
#include <regex.h>
#include <unistd.h>
#include <stdbool.h>
#include <linux/kernel.h>
#include <linux/zalloc.h>

#include "../debug.h"
#include "../event.h"
#include "../pmu.h"
#include "../pmus.h"
#include "../perf_regs.h"
#include "../../perf-sys.h"
#include "../../arch/x86/include/perf_regs.h"

struct sdt_name_reg {
	const char *sdt_name;
	const char *uprobe_name;
};
#define SDT_NAME_REG(n, m) {.sdt_name = "%" #n, .uprobe_name = "%" #m}
#define SDT_NAME_REG_END {.sdt_name = NULL, .uprobe_name = NULL}

static const struct sdt_name_reg sdt_reg_tbl[] = {
	SDT_NAME_REG(eax, ax),
	SDT_NAME_REG(rax, ax),
	SDT_NAME_REG(al,  ax),
	SDT_NAME_REG(ah,  ax),
	SDT_NAME_REG(ebx, bx),
	SDT_NAME_REG(rbx, bx),
	SDT_NAME_REG(bl,  bx),
	SDT_NAME_REG(bh,  bx),
	SDT_NAME_REG(ecx, cx),
	SDT_NAME_REG(rcx, cx),
	SDT_NAME_REG(cl,  cx),
	SDT_NAME_REG(ch,  cx),
	SDT_NAME_REG(edx, dx),
	SDT_NAME_REG(rdx, dx),
	SDT_NAME_REG(dl,  dx),
	SDT_NAME_REG(dh,  dx),
	SDT_NAME_REG(esi, si),
	SDT_NAME_REG(rsi, si),
	SDT_NAME_REG(sil, si),
	SDT_NAME_REG(edi, di),
	SDT_NAME_REG(rdi, di),
	SDT_NAME_REG(dil, di),
	SDT_NAME_REG(ebp, bp),
	SDT_NAME_REG(rbp, bp),
	SDT_NAME_REG(bpl, bp),
	SDT_NAME_REG(rsp, sp),
	SDT_NAME_REG(esp, sp),
	SDT_NAME_REG(spl, sp),

	/* rNN registers */
	SDT_NAME_REG(r8b,  r8),
	SDT_NAME_REG(r8w,  r8),
	SDT_NAME_REG(r8d,  r8),
	SDT_NAME_REG(r9b,  r9),
	SDT_NAME_REG(r9w,  r9),
	SDT_NAME_REG(r9d,  r9),
	SDT_NAME_REG(r10b, r10),
	SDT_NAME_REG(r10w, r10),
	SDT_NAME_REG(r10d, r10),
	SDT_NAME_REG(r11b, r11),
	SDT_NAME_REG(r11w, r11),
	SDT_NAME_REG(r11d, r11),
	SDT_NAME_REG(r12b, r12),
	SDT_NAME_REG(r12w, r12),
	SDT_NAME_REG(r12d, r12),
	SDT_NAME_REG(r13b, r13),
	SDT_NAME_REG(r13w, r13),
	SDT_NAME_REG(r13d, r13),
	SDT_NAME_REG(r14b, r14),
	SDT_NAME_REG(r14w, r14),
	SDT_NAME_REG(r14d, r14),
	SDT_NAME_REG(r15b, r15),
	SDT_NAME_REG(r15w, r15),
	SDT_NAME_REG(r15d, r15),
	SDT_NAME_REG_END,
};

/*
 * Perf only supports OP which is in  +/-NUM(REG)  form.
 * Here plus-minus sign, NUM and parenthesis are optional,
 * only REG is mandatory.
 *
 * SDT events also supports indirect addressing mode with a
 * symbol as offset, scaled mode and constants in OP. But
 * perf does not support them yet. Below are few examples.
 *
 * OP with scaled mode:
 *     (%rax,%rsi,8)
 *     10(%ras,%rsi,8)
 *
 * OP with indirect addressing mode:
 *     check_action(%rip)
 *     mp_+52(%rip)
 *     44+mp_(%rip)
 *
 * OP with constant values:
 *     $0
 *     $123
 *     $-1
 */
#define SDT_OP_REGEX  "^([+\\-]?)([0-9]*)(\\(?)(%[a-z][a-z0-9]+)(\\)?)$"

static regex_t sdt_op_regex;

static int sdt_init_op_regex(void)
{
	static int initialized;
	int ret = 0;

	if (initialized)
		return 0;

	ret = regcomp(&sdt_op_regex, SDT_OP_REGEX, REG_EXTENDED);
	if (ret < 0) {
		pr_debug4("Regex compilation error.\n");
		return ret;
	}

	initialized = 1;
	return 0;
}

/*
 * Max x86 register name length is 5(ex: %r15d). So, 6th char
 * should always contain NULL. This helps to find register name
 * length using strlen, instead of maintaining one more variable.
 */
#define SDT_REG_NAME_SIZE  6

/*
 * The uprobe parser does not support all gas register names;
 * so, we have to replace them (ex. for x86_64: %rax -> %ax).
 * Note: If register does not require renaming, just copy
 * paste as it is, but don't leave it empty.
 */
static void sdt_rename_register(char *sdt_reg, int sdt_len, char *uprobe_reg)
{
	int i = 0;

	for (i = 0; sdt_reg_tbl[i].sdt_name != NULL; i++) {
		if (!strncmp(sdt_reg_tbl[i].sdt_name, sdt_reg, sdt_len)) {
			strcpy(uprobe_reg, sdt_reg_tbl[i].uprobe_name);
			return;
		}
	}

	strncpy(uprobe_reg, sdt_reg, sdt_len);
}

int __perf_sdt_arg_parse_op_x86(char *old_op, char **new_op)
{
	char new_reg[SDT_REG_NAME_SIZE] = {0};
	int new_len = 0, ret;
	/*
	 * rm[0]:  +/-NUM(REG)
	 * rm[1]:  +/-
	 * rm[2]:  NUM
	 * rm[3]:  (
	 * rm[4]:  REG
	 * rm[5]:  )
	 */
	regmatch_t rm[6];
	/*
	 * Max prefix length is 2 as it may contains sign(+/-)
	 * and displacement 0 (Both sign and displacement 0 are
	 * optional so it may be empty). Use one more character
	 * to hold last NULL so that strlen can be used to find
	 * prefix length, instead of maintaining one more variable.
	 */
	char prefix[3] = {0};

	ret = sdt_init_op_regex();
	if (ret < 0)
		return ret;

	/*
	 * If unsupported OR does not match with regex OR
	 * register name too long, skip it.
	 */
	if (strchr(old_op, ',') || strchr(old_op, '$') ||
	    regexec(&sdt_op_regex, old_op, 6, rm, 0)   ||
	    rm[4].rm_eo - rm[4].rm_so > SDT_REG_NAME_SIZE) {
		pr_debug4("Skipping unsupported SDT argument: %s\n", old_op);
		return SDT_ARG_SKIP;
	}

	/*
	 * Prepare prefix.
	 * If SDT OP has parenthesis but does not provide
	 * displacement, add 0 for displacement.
	 *     SDT         Uprobe     Prefix
	 *     -----------------------------
	 *     +24(%rdi)   +24(%di)   +
	 *     24(%rdi)    +24(%di)   +
	 *     %rdi        %di
	 *     (%rdi)      +0(%di)    +0
	 *     -80(%rbx)   -80(%bx)   -
	 */
	if (rm[3].rm_so != rm[3].rm_eo) {
		if (rm[1].rm_so != rm[1].rm_eo)
			prefix[0] = *(old_op + rm[1].rm_so);
		else if (rm[2].rm_so != rm[2].rm_eo)
			prefix[0] = '+';
		else
			scnprintf(prefix, sizeof(prefix), "+0");
	}

	/* Rename register */
	sdt_rename_register(old_op + rm[4].rm_so, rm[4].rm_eo - rm[4].rm_so,
			    new_reg);

	/* Prepare final OP which should be valid for uprobe_events */
	new_len = strlen(prefix)              +
		  (rm[2].rm_eo - rm[2].rm_so) +
		  (rm[3].rm_eo - rm[3].rm_so) +
		  strlen(new_reg)             +
		  (rm[5].rm_eo - rm[5].rm_so) +
		  1;					/* NULL */

	*new_op = zalloc(new_len);
	if (!*new_op)
		return -ENOMEM;

	scnprintf(*new_op, new_len, "%.*s%.*s%.*s%.*s%.*s",
		  strlen(prefix), prefix,
		  (int)(rm[2].rm_eo - rm[2].rm_so), old_op + rm[2].rm_so,
		  (int)(rm[3].rm_eo - rm[3].rm_so), old_op + rm[3].rm_so,
		  strlen(new_reg), new_reg,
		  (int)(rm[5].rm_eo - rm[5].rm_so), old_op + rm[5].rm_so);

	return SDT_ARG_VALID;
}

static uint64_t __arch__reg_mask(u64 sample_type, u64 mask, bool has_simd_regs)
{
	struct perf_event_attr attr = {
		.type				= PERF_TYPE_HARDWARE,
		.config				= PERF_COUNT_HW_CPU_CYCLES,
		.sample_type			= sample_type,
		.precise_ip			= 1,
		.disabled			= 1,
		.exclude_kernel			= 1,
		.sample_simd_regs_enabled	= has_simd_regs,
	};
	int fd;
	/*
	 * In an unnamed union, init it here to build on older gcc versions
	 */
	attr.sample_period = 1;
	if (sample_type == PERF_SAMPLE_REGS_INTR)
		attr.sample_regs_intr = mask;
	else
		attr.sample_regs_user = mask;

	if (perf_pmus__num_core_pmus() > 1) {
		struct perf_pmu *pmu = NULL;
		__u64 type = PERF_TYPE_RAW;

		/*
		 * The same register set is supported among different hybrid PMUs.
		 * Only check the first available one.
		 */
		while ((pmu = perf_pmus__scan_core(pmu)) != NULL) {
			type = pmu->type;
			break;
		}
		attr.config |= type << PERF_PMU_TYPE_SHIFT;
	}

	event_attr_init(&attr);
	fd = sys_perf_event_open(&attr, /*pid=*/0, /*cpu=*/-1,
				 /*group_fd=*/-1, /*flags=*/0);
	if (fd != -1) {
		close(fd);
		return mask;
	}

	return 0;
}

static bool __support_simd_sampling(bool intr);
uint64_t __perf_reg_mask_x86(bool intr, int *abi)
{
	u64 sample_type = intr ? PERF_SAMPLE_REGS_INTR : PERF_SAMPLE_REGS_USER;
	uint64_t mask = PERF_REGS_MASK;

	/* -1 indicates only basic GPRs are needed. */
	if (*abi < 0)
		return PERF_REGS_MASK;

	*abi = 0;
	mask |= __arch__reg_mask(sample_type,
				 GENMASK_ULL(PERF_REG_X86_R31, PERF_REG_X86_R16),
				 true);
	mask |= __arch__reg_mask(sample_type, BIT_ULL(PERF_REG_X86_SSP), true);

	if (mask != PERF_REGS_MASK) {
		*abi |= PERF_SAMPLE_REGS_ABI_SIMD;
	} else {
		/*
		 * Use the SIMD sampling interface for XMM registers
		 * when available.
		 */
		if (!__support_simd_sampling(intr)) {
			mask |= __arch__reg_mask(sample_type,
						 PERF_REG_EXTENDED_MASK, false);
		}
	}

	return mask;
}

static const char *__arch_reg_gpr_name(int id)
{
	switch (id) {
	case PERF_REG_X86_AX:
		return "AX";
	case PERF_REG_X86_BX:
		return "BX";
	case PERF_REG_X86_CX:
		return "CX";
	case PERF_REG_X86_DX:
		return "DX";
	case PERF_REG_X86_SI:
		return "SI";
	case PERF_REG_X86_DI:
		return "DI";
	case PERF_REG_X86_BP:
		return "BP";
	case PERF_REG_X86_SP:
		return "SP";
	case PERF_REG_X86_IP:
		return "IP";
	case PERF_REG_X86_FLAGS:
		return "FLAGS";
	case PERF_REG_X86_CS:
		return "CS";
	case PERF_REG_X86_SS:
		return "SS";
	case PERF_REG_X86_DS:
		return "DS";
	case PERF_REG_X86_ES:
		return "ES";
	case PERF_REG_X86_FS:
		return "FS";
	case PERF_REG_X86_GS:
		return "GS";
	case PERF_REG_X86_R8:
		return "R8";
	case PERF_REG_X86_R9:
		return "R9";
	case PERF_REG_X86_R10:
		return "R10";
	case PERF_REG_X86_R11:
		return "R11";
	case PERF_REG_X86_R12:
		return "R12";
	case PERF_REG_X86_R13:
		return "R13";
	case PERF_REG_X86_R14:
		return "R14";
	case PERF_REG_X86_R15:
		return "R15";
	default:
		return NULL;
	}

	return NULL;
}

static const char *__arch_reg_egpr_name(int id)
{
	switch (id) {
	case PERF_REG_X86_R16:
		return "R16";
	case PERF_REG_X86_R17:
		return "R17";
	case PERF_REG_X86_R18:
		return "R18";
	case PERF_REG_X86_R19:
		return "R19";
	case PERF_REG_X86_R20:
		return "R20";
	case PERF_REG_X86_R21:
		return "R21";
	case PERF_REG_X86_R22:
		return "R22";
	case PERF_REG_X86_R23:
		return "R23";
	case PERF_REG_X86_R24:
		return "R24";
	case PERF_REG_X86_R25:
		return "R25";
	case PERF_REG_X86_R26:
		return "R26";
	case PERF_REG_X86_R27:
		return "R27";
	case PERF_REG_X86_R28:
		return "R28";
	case PERF_REG_X86_R29:
		return "R29";
	case PERF_REG_X86_R30:
		return "R30";
	case PERF_REG_X86_R31:
		return "R31";
	case PERF_REG_X86_SSP:
		return "SSP";
	default:
		return NULL;
	}

	return NULL;
}

static const char *__arch_reg_xmm_name(int id)
{
	switch (id) {
#define XMM(x) \
	case PERF_REG_X86_XMM ## x:	\
	case PERF_REG_X86_XMM ## x + 1:	\
		return "XMM" #x;
	XMM(0)
	XMM(1)
	XMM(2)
	XMM(3)
	XMM(4)
	XMM(5)
	XMM(6)
	XMM(7)
	XMM(8)
	XMM(9)
	XMM(10)
	XMM(11)
	XMM(12)
	XMM(13)
	XMM(14)
	XMM(15)
#undef XMM
	default:
		return NULL;
	}

	return NULL;
}

const char *__perf_reg_name_x86(int id, int abi)
{
	const char *name;

	name = __arch_reg_gpr_name(id);
	if (name)
		return name;

	if (abi & PERF_SAMPLE_REGS_ABI_SIMD)
		name = __arch_reg_egpr_name(id);
	else
		name = __arch_reg_xmm_name(id);

	return name;
}

uint64_t __perf_reg_ip_x86(void)
{
	return PERF_REG_X86_IP;
}

uint64_t __perf_reg_sp_x86(void)
{
	return PERF_REG_X86_SP;
}

enum {
	PERF_REG_CLASS_X86_OPMASK = 0,
	PERF_REG_CLASS_X86_XMM,
	PERF_REG_CLASS_X86_YMM,
	PERF_REG_CLASS_X86_ZMM,
	PERF_REG_X86_MAX_SIMD_CLASSES,
};

#define PERF_REG_CLASS_X86_PRED_MASK	(BIT(PERF_REG_CLASS_X86_OPMASK))
#define PERF_REG_CLASS_X86_SIMD_MASK	(BIT(PERF_REG_CLASS_X86_XMM) | \
					 BIT(PERF_REG_CLASS_X86_YMM) | \
					 BIT(PERF_REG_CLASS_X86_ZMM))

/*
 * This function is used to determine whether kernel perf subsystem
 * supports which kinds of SIMD registers (OPMASK/XMM/YMM/ZMM) sampling.
 *
 * @sample_type: PERF_SAMPLE_REGS_INTR or PERF_SAMPLE_REGS_USER
 * @qwords: the length of SIMD register, like 1/2/4/8 qwords for
 *          OPMASK/XMM/YMM/ZMM registers.
 * @mask: the bitmask of SIMD register, like 0xffff for XMM0 ~ XMM15
 * @pred: whether It's a predicate SIMD register, like OPMASK register.
 *
 * Return value: true indicates support, otherwise no support.
 */
static bool
__support_simd_reg_class(uint64_t sample_type, uint16_t qwords,
			 uint64_t mask, bool pred)
{
	struct perf_event_attr attr = {
		.type				= PERF_TYPE_HARDWARE,
		.config				= PERF_COUNT_HW_CPU_CYCLES,
		.sample_type			= sample_type,
		.disabled			= 1,
		.exclude_kernel			= 1,
		.sample_simd_regs_enabled	= 1,
	};
	int fd;

	attr.sample_period = 1;

	if (!pred) {
		attr.sample_simd_vec_reg_qwords = qwords;
		if (sample_type == PERF_SAMPLE_REGS_INTR)
			attr.sample_simd_vec_reg_intr = mask;
		else
			attr.sample_simd_vec_reg_user = mask;
	} else {
		attr.sample_simd_pred_reg_qwords = PERF_X86_OPMASK_QWORDS;
		if (sample_type == PERF_SAMPLE_REGS_INTR)
			attr.sample_simd_pred_reg_intr = PERF_X86_SIMD_PRED_MASK;
		else
			attr.sample_simd_pred_reg_user = PERF_X86_SIMD_PRED_MASK;
	}

	if (perf_pmus__num_core_pmus() > 1) {
		__u64 type = perf_pmus__find_core_pmu()->type;

		attr.config |= type << PERF_PMU_TYPE_SHIFT;
	}

	event_attr_init(&attr);

	fd = sys_perf_event_open(&attr, 0, -1, -1, 0);
	if (fd != -1) {
		close(fd);
		return true;
	}

	return false;
}

#define PERF_X86_SIMD_ZMM_LOW_REGS	(PERF_X86_SIMD_ZMM_REGS / 2)

static bool __arch_has_simd_reg_class(uint64_t sample_type, int reg_class,
				      uint64_t *mask, uint16_t *qwords)
{
	bool supported = false;
	uint64_t bits;

	*mask = 0;
	*qwords = 0;

	switch (reg_class) {
	case PERF_REG_CLASS_X86_OPMASK:
		bits = BIT_ULL(PERF_X86_SIMD_OPMASK_REGS) - 1;
		supported = __support_simd_reg_class(sample_type,
						     PERF_X86_OPMASK_QWORDS,
						     bits, true);
		if (supported) {
			*mask = bits;
			*qwords = PERF_X86_OPMASK_QWORDS;
		}
		break;
	case PERF_REG_CLASS_X86_XMM:
		bits = BIT_ULL(PERF_X86_SIMD_XMM_REGS) - 1;
		supported = __support_simd_reg_class(sample_type,
						     PERF_X86_XMM_QWORDS,
						     bits, false);
		if (supported) {
			*mask = bits;
			*qwords = PERF_X86_XMM_QWORDS;
		}
		break;
	case PERF_REG_CLASS_X86_YMM:
		bits = BIT_ULL(PERF_X86_SIMD_YMM_REGS) - 1;
		supported = __support_simd_reg_class(sample_type,
						     PERF_X86_YMM_QWORDS,
						     bits, false);
		if (supported) {
			*mask = bits;
			*qwords = PERF_X86_YMM_QWORDS;
		}
		break;
	case PERF_REG_CLASS_X86_ZMM:
		bits = BIT_ULL(PERF_X86_SIMD_ZMM_REGS) - 1;
		supported = __support_simd_reg_class(sample_type,
						     PERF_X86_ZMM_QWORDS,
						     bits, false);
		if (supported) {
			*mask = bits;
			*qwords = PERF_X86_ZMM_QWORDS;
			break;
		}

		bits = BIT_ULL(PERF_X86_SIMD_ZMM_LOW_REGS) - 1;
		supported = __support_simd_reg_class(sample_type,
						     PERF_X86_ZMM_QWORDS,
						     bits, false);
		if (supported) {
			*mask = bits;
			*qwords = PERF_X86_ZMM_QWORDS;
		}
		break;
	default:
		break;
	}

	return supported;
}

static bool __support_simd_sampling(bool intr)
{
	static bool intr_simd_sampling_supported;
	static bool user_simd_sampling_supported;
	static bool intr_cached;
	static bool user_cached;
	uint64_t mask;
	uint16_t qwords;

	if (intr && intr_cached)
		return intr_simd_sampling_supported;
	if (!intr && user_cached)
		return user_simd_sampling_supported;

	if (intr) {
		intr_simd_sampling_supported =
			__arch_has_simd_reg_class(PERF_SAMPLE_REGS_INTR,
						  PERF_REG_CLASS_X86_XMM,
						  &mask, &qwords);
		intr_cached = true;
	} else {
		user_simd_sampling_supported =
			__arch_has_simd_reg_class(PERF_SAMPLE_REGS_USER,
						  PERF_REG_CLASS_X86_XMM,
						  &mask, &qwords);
		user_cached = true;
	}

	return intr ? intr_simd_sampling_supported :
		      user_simd_sampling_supported;
}

/*
 * @x86_intr_simd_cached: indicates the data of below 3
 *  x86_intr_simd_* items has been retrieved from kernel and cached.
 * @x86_intr_simd_reg_class_mask: indicates which kinds of PRED/SIMD
 *  registers are supported for intr-regs option. Assume kernel perf
 *  subsystem supports XMM/YMM sampling, then the mask is
 *  PERF_REG_CLASS_X86_XMM|PERF_REG_CLASS_X86_YMM.
 * @x86_intr_simd_mask: indicates register bitmask for each kind of
 *  supported PRED/SIMD register, like
 *  x86_intr_simd_mask[PERF_REG_CLASS_X86_XMM] = 0xffff.
 * @x86_intr_simd_qwords: indicates the register length (qwords unit)
 *  for each kind of supported PRED/SIMD register, like
 *  x86_intr_simd_qwords[PERF_REG_CLASS_X86_XMM] = 2.
 */
static bool x86_intr_simd_cached;
static uint64_t x86_intr_simd_reg_class_mask;
static uint64_t x86_intr_simd_mask[PERF_REG_X86_MAX_SIMD_CLASSES];
static uint16_t x86_intr_simd_qwords[PERF_REG_X86_MAX_SIMD_CLASSES];

/*
 * Similar with above x86_intr_simd_* items, the difference is these
 * items are used for user-regs option.
 */
static bool x86_user_simd_cached;
static uint64_t x86_user_simd_reg_class_mask;
static uint64_t x86_user_simd_mask[PERF_REG_X86_MAX_SIMD_CLASSES];
static uint16_t x86_user_simd_qwords[PERF_REG_X86_MAX_SIMD_CLASSES];

static uint64_t __arch__simd_reg_class_mask(bool intr)
{
	uint64_t mask = 0;
	bool supported;
	int reg_c;

	if (!__support_simd_sampling(intr))
		goto done;

	if (intr && x86_intr_simd_cached)
		return x86_intr_simd_reg_class_mask;

	if (!intr && x86_user_simd_cached)
		return x86_user_simd_reg_class_mask;

	for (reg_c = 0; reg_c < PERF_REG_X86_MAX_SIMD_CLASSES; reg_c++) {
		supported = false;

		if (intr) {
			supported = __arch_has_simd_reg_class(
						PERF_SAMPLE_REGS_INTR,
						reg_c,
						&x86_intr_simd_mask[reg_c],
						&x86_intr_simd_qwords[reg_c]);
		} else {
			supported = __arch_has_simd_reg_class(
						PERF_SAMPLE_REGS_USER,
						reg_c,
						&x86_user_simd_mask[reg_c],
						&x86_user_simd_qwords[reg_c]);
		}
		if (supported)
			mask |= BIT_ULL(reg_c);
	}

done:
	if (intr) {
		x86_intr_simd_reg_class_mask = mask;
		x86_intr_simd_cached = true;
	} else {
		x86_user_simd_reg_class_mask = mask;
		x86_user_simd_cached = true;
	}

	return mask;
}

static uint64_t
__arch__simd_reg_class_bitmap_qwords(bool intr, int reg_c, uint16_t *qwords)
{
	uint64_t mask = 0;
	uint64_t class_mask;

	*qwords = 0;
	class_mask = intr ? x86_intr_simd_reg_class_mask :
			    x86_user_simd_reg_class_mask;
	if (!(class_mask & BIT_ULL(reg_c)))
		return 0;

	if (intr) {
		mask = x86_intr_simd_mask[reg_c];
		*qwords = x86_intr_simd_qwords[reg_c];
	} else {
		mask = x86_user_simd_mask[reg_c];
		*qwords = x86_user_simd_qwords[reg_c];
	}

	return mask;
}

uint64_t __perf_simd_reg_class_mask_x86(bool intr, bool pred)
{
	uint64_t mask = __arch__simd_reg_class_mask(intr);

	return pred ? mask & PERF_REG_CLASS_X86_PRED_MASK :
		      mask & PERF_REG_CLASS_X86_SIMD_MASK;
}

uint64_t __perf_simd_reg_class_bitmap_qwords_x86(int reg_c, uint16_t *qwords,
						 bool intr, bool pred)
{
	if (intr ? !x86_intr_simd_cached : !x86_user_simd_cached)
		__perf_simd_reg_class_mask_x86(intr, pred);
	return __arch__simd_reg_class_bitmap_qwords(intr, reg_c, qwords);
}

const char *__perf_simd_reg_class_name_x86(int id, bool pred __maybe_unused)
{
	switch (id) {
	case PERF_REG_CLASS_X86_OPMASK:
		return "OPMASK";
	case PERF_REG_CLASS_X86_XMM:
		return "XMM";
	case PERF_REG_CLASS_X86_YMM:
		return "YMM";
	case PERF_REG_CLASS_X86_ZMM:
		return "ZMM";
	default:
		return NULL;
	}

	return NULL;
}
