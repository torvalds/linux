// SPDX-License-Identifier: GPL-2.0
#include <linux/compiler.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/zalloc.h>
#include <regex.h>
#include "../annotate.h"
#include "../disasm.h"
#include "../annotate-data.h"
#include "../debug.h"
#include "../map.h"
#include "../symbol.h"

struct arch_arm64 {
	struct arch arch;
	regex_t call_insn;
	regex_t jump_insn;
	regex_t ldst_insn; /* load and store instruction */
};

static bool arm64__is_reg(const char *op)
{
	if (!op || !*op)
		return false;

	/*
	 * General-purpose registers: x0-x30, w0-w30.
	 * Check for 'x' or 'w' prefix followed by a numeric index.
	 */
	if ((op[0] == 'x' || op[0] == 'w') && isdigit(op[1]))
		return true;

	/* Special-purpose registers: sp, wzr, xzr. */
	if (!strncmp(op, "sp", 2) || !strncmp(op, "xzr", 3) ||
	    !strncmp(op, "wzr", 3))
		return true;

	/* TODO: Support more registers. */
	return false;
}

static bool arm64__check_multi_regs(const struct arch *arch, const char *op)
{
	const char *p = op;
	int reg_count = 0;

	while (p && *p) {
		p = skip_spaces(p);
		if (*p == arch->objdump.memory_ref_char)
			p++;

		if (arm64__is_reg(p))
			reg_count++;

		if (reg_count >= 2)
			return true;

		/* Move to next operand after comma */
		p = strchr(p, ',');
		if (p)
			p++;
	}

	return false;
}

/*
 * Duplicate @insn, stripping the comment and trailing whitespace.
 * Returns a newly allocated string which the caller must free(),
 * or NULL on allocation failure or if @insn is NULL.
 */
static char *rstrip_space_and_comment(const char *insn, char comment_char)
{
	const char *end, *comment;
	size_t len;
	char *result;

	if (insn == NULL)
		return NULL;

	comment = strchr(insn, comment_char);
	if (comment != NULL)
		end = comment;
	else
		end = insn + strlen(insn);

	while (end > insn && isspace(end[-1]))
		--end;

	len = end - insn;
	result = malloc(len + 1);
	if (result == NULL)
		return NULL;

	memcpy(result, insn, len);
	result[len] = '\0';

	return result;
}

static int arm64_mov__parse(const struct arch *arch,
			    struct ins_operands *ops,
			    struct map_symbol *ms __maybe_unused,
			    struct disasm_line *dl __maybe_unused)
{
	char *s = strchr(ops->raw, ','), *source, *endptr;

	if (s == NULL)
		return -1;

	/* Parse target */
	*s = '\0';
	ops->target.raw = strdup(ops->raw);
	*s = ',';

	if (ops->target.raw == NULL)
		return -1;

	ops->target.multi_regs = arm64__check_multi_regs(arch, ops->target.raw);

	/* Parse source, stripping comment if present */
	source = skip_spaces(++s);
	ops->source.raw = rstrip_space_and_comment(source, arch->objdump.comment_char);
	if (ops->source.raw == NULL) {
		zfree(&ops->target.raw);
		return -1;
	}

	ops->source.multi_regs = arm64__check_multi_regs(arch, ops->source.raw);

	/*
	 * Parse 'addr <symbol>' from source (if any).
	 *
	 * A raw hex string may be a scalar immediate, not an address.
	 * Only validate 'source.addr' if accompanied by a '<symbol>' tag,
	 * otherwise reset it to 0 to avoid false positive address tracking.
	 */
	ops->source.addr = strtoull(ops->source.raw, &endptr, 16);
	if (endptr != ops->source.raw) {
		s = strchr(endptr, '<');
		if (s == NULL) {
			ops->source.addr = 0;
			return 0;
		}
		endptr = strrchr(s + 1, '>');
		if (endptr == NULL) {
			ops->source.addr = 0;
			return 0;
		}

		*endptr = '\0';
		ops->source.name = strdup(s + 1);
		*endptr = '>';
		if (ops->source.name == NULL) {
			ops->source.addr = 0;
			return 0;
		}
	}

	return 0;
}

static int arm64_mov__scnprintf(const struct ins *ins, char *bf, size_t size,
				struct ins_operands *ops, int max_ins_name)
{
	return scnprintf(bf, size, "%-*s %s, %s", max_ins_name, ins->name,
			 ops->target.raw, ops->source.name ?: ops->source.raw);
}

static const struct ins_ops arm64_mov_ops = {
	.parse	   = arm64_mov__parse,
	.scnprintf = arm64_mov__scnprintf,
};

static bool arm64__insn_is_target_on_right(const char *ins_name)
{
	/*
	 * Store instructions write to the memory operand on the right,
	 * unlike standard syntax where the target is the left operand.
	 */
	return !strncmp(ins_name, "st", 2);
}

/*
 * This function is used to parse arm64 load/store instructions into
 * instruction operands.
 *
 * Typical instructions and their parsing logic:
 *
 * 1. Immediate offset:
 *    ldr   x2, [x0]                -> target="x2", source="[x0]"
 *    ldr   x2, [x0, #24]           -> target="x2", source="[x0, #24]"
 *    ldp   x19, x20, [sp, #16]     -> target="x19, x20", source="[sp, #16]"
 *
 * 2. Pre-index addressing:
 *    stp   x29, x30, [sp, #-64]!   -> target="[sp, #-64]!", source="x29, x30"
 *
 * 3. Post-index addressing:
 *    str   x1, [x0], #8            -> target="[x0], #8", source="x1"
 *    ldr   w1, [x21], #4           -> target="w1", source="[x21], #4"
 *    ldp   x29, x30, [sp], #32     -> target="x29, x30", source="[sp], #32"
 *
 * 4. Register offset / extension:
 *    ldr   x0, [x1, w0, sxtw #3]   -> target="x0", source="[x1, w0, sxtw #3]"
 *    ldr   x0, [x1, x0, lsl #3]    -> target="x0", source="[x1, x0, lsl #3]"
 *
 * 5. Atomic operations:
 *    cas   w3, w1, [x0]            -> target="w3, w1", source="[x0]"
 *    swp   x3, x0, [x2]            -> target="x3, x0", source="[x2]"
 *
 * 6. Prefetch memory:
 *    prfm  pstl1strm, [x4]         -> target="pstl1strm", source="[x4]"
 *
 * 7. PC-relative loads (No bracket found):
 *    ldr   x0, ffff800080f40c68 <__kvm_nvhe_$d>  -> Fallback to default parser
 *
 * Parsing strategy:
 * Use the '[' bracket as the boundary to split the operands into left
 * and right sides. For non-store instructions, the left side is the
 * target and the right side is the source. For store instructions, the
 * roles are reversed.
 */
static int arm64_ldst__parse(const struct arch *arch, struct ins_operands *ops,
			     struct map_symbol *ms, struct disasm_line *dl)
{
	char *raw, *s, *left, *right;
	int ret = -1;

	raw = rstrip_space_and_comment(ops->raw, arch->objdump.comment_char);
	if (!raw)
		return -1;

	s = strchr(raw, arch->objdump.memory_ref_char);
	if (!s) {
		/* Fallback to default parser for PC-relative loads. */
		free(raw);
		return arm64_mov__parse(arch, ops, ms, dl);
	}

	right = strdup(s);
	if (!right)
		goto out_free_raw;

	while (s > raw && *s != ',')
		--s;

	if (s == raw)
		goto out_free_right;

	*s = '\0';
	left = strdup(raw);
	*s = ',';
	if (!left)
		goto out_free_right;

	free(raw);

	if (arm64__insn_is_target_on_right(dl->ins.name)) {
		ops->source.raw = left;
		ops->source.mem_ref = false;

		ops->target.raw = right;
		ops->target.mem_ref = true;
	} else {
		ops->source.raw = right;
		ops->source.mem_ref = true;

		ops->target.raw = left;
		ops->target.mem_ref = false;
	}

	ops->source.multi_regs = arm64__check_multi_regs(arch, ops->source.raw);
	ops->target.multi_regs = arm64__check_multi_regs(arch, ops->target.raw);

	return 0;

out_free_right:
	free(right);
out_free_raw:
	free(raw);
	return ret;
}

static int arm64_ldst__scnprintf(const struct ins *ins, char *bf, size_t size,
				 struct ins_operands *ops, int max_ins_name)
{
	if (arm64__insn_is_target_on_right(ins->name))
		return scnprintf(bf, size, "%-*s %s", max_ins_name, ins->name, ops->raw);

	return scnprintf(bf, size, "%-*s %s, %s", max_ins_name, ins->name,
			 ops->target.raw, ops->source.name ?: ops->source.raw);
}

static struct ins_ops arm64_ldst_ops = {
	.parse	   = arm64_ldst__parse,
	.scnprintf = arm64_ldst__scnprintf,
};

static const struct ins_ops *arm64__associate_instruction_ops(struct arch *arch, const char *name)
{
	struct arch_arm64 *arm = container_of(arch, struct arch_arm64, arch);
	const struct ins_ops *ops;
	regmatch_t match[2];

	if (!regexec(&arm->jump_insn, name, 2, match, 0))
		ops = &jump_ops;
	else if (!regexec(&arm->call_insn, name, 2, match, 0))
		ops = &call_ops;
	else if (!regexec(&arm->ldst_insn, name, 2, match, 0))
		ops = &arm64_ldst_ops;
	else if (!strcmp(name, "ret"))
		ops = &ret_ops;
	else
		ops = &arm64_mov_ops;

	arch__associate_ins_ops(arch, name, ops);
	return ops;
}

static enum annotated_ext_type arm64__check_ext_type(const char *op)
{
	if (!strncmp(op, "uxtb", 4)) return PERF_EXT_UXTB;
	if (!strncmp(op, "uxth", 4)) return PERF_EXT_UXTH;
	if (!strncmp(op, "uxtw", 4)) return PERF_EXT_UXTW;
	if (!strncmp(op, "uxtx", 4)) return PERF_EXT_UXTX;
	if (!strncmp(op, "sxtb", 4)) return PERF_EXT_SXTB;
	if (!strncmp(op, "sxth", 4)) return PERF_EXT_SXTH;
	if (!strncmp(op, "sxtw", 4)) return PERF_EXT_SXTW;
	if (!strncmp(op, "sxtx", 4)) return PERF_EXT_SXTX;

	return PERF_EXT_NONE;
}

static enum annotated_shift_type arm64__check_shift_type(const char *op)
{
	if (!strncmp(op, "lsl", 3)) return PERF_SHIFT_LSL;
	if (!strncmp(op, "lsr", 3)) return PERF_SHIFT_LSR;
	if (!strncmp(op, "asr", 3)) return PERF_SHIFT_ASR;
	if (!strncmp(op, "ror", 3)) return PERF_SHIFT_ROR;

	return PERF_SHIFT_NONE;
}

static const char *next_operand_field(const char *s)
{
	s = strchr(s, ',');
	return s ? skip_spaces(s + 1) : NULL;
}

static void extract_op_location_arm64(const struct arch *arch,
				      struct disasm_line *dl __maybe_unused,
				      const char *op_str, int op_idx __maybe_unused,
				      struct annotated_op_loc *op_loc)
{
	const char *s = op_str;
	char *p = NULL;

	if (op_str == NULL)
		return;

	/*
	 * Handle immediate operand.
	 * e.g., "#0xc600" -> offset = 0xc600, imm = true
	 *
	 * After parsing, check for post-modifiers like "lsl #16", which
	 * typically occur in instructions such as movk, movz, movn, etc.
	 */
	if (*s == arch->objdump.imm_char) {
		op_loc->offset = strtol(s + 1, &p, 0);
		if (p && p != s + 1) {
			op_loc->imm = true;
			s = p;
		}
		s = next_operand_field(s);
		goto check_modifiers;
	}

	/*
	 * Handle memory references, identify arm64 specific addressing modes.
	 * Reference: Arm Architecture Reference Manual
	 *            (DDI 0487), Chapter C1.3.3: Load/store addressing modes.
	 */
	if (*s == arch->objdump.memory_ref_char) {
		op_loc->mem_ref = true;

		p = (char *)strchr(s, ']');
		if (p == NULL)
			return;

		/* Pre-index: [base, #imm]! */
		if (p[1] == '!')
			op_loc->addr_mode = PERF_AAM_PRE_INDEX;
		/* Post-index: [base], #imm|reg */
		else if (p[1] == ',' &&
			 (strchr(p + 1, arch->objdump.imm_char) ||
			  arm64__is_reg(skip_spaces(p + 2))))
			op_loc->addr_mode = PERF_AAM_POST_INDEX;
		/* Signed offset: [base{, #imm|reg}] */
		else
			op_loc->addr_mode = PERF_AAM_SIGNED_OFFSET;

		s++;
	}

	/* Extract the primary register */
	op_loc->reg1 = arch__dwarf_regnum(arch, s);
	if (op_loc->reg1 == -1)
		return;

	s = next_operand_field(s);
	if (s == NULL)
		return;

	/* Extract secondary register or immediate offset */
	if (op_loc->multi_regs) {
		op_loc->reg2 = arch__dwarf_regnum(arch, s);
		s = next_operand_field(s);
	} else if (*s == arch->objdump.imm_char) {
		op_loc->offset = strtol(s + 1, NULL, 0);
		s = next_operand_field(s);
	}

check_modifiers:
	/*
	 * Look for a following shift or extension modifier:
	 *   "lsl #3"   -> extend_type = PERF_EXT_NONE,
	 *                 shift_type = PERF_SHIFT_LSL, amount = 3
	 *   "uxtw #3"  -> extend_type = PERF_EXT_UXTW,
	 *                 shift_type = PERF_SHIFT_LSL, amount = 3
	 *   "uxtw"     -> extend_type = PERF_EXT_UXTW,
	 *                 shift_type = PERF_SHIFT_LSL, amount = 0
	 */
	if (s == NULL)
		return;

	op_loc->extend_type = arm64__check_ext_type(s);
	op_loc->shift_type = arm64__check_shift_type(s);
	/* ARM64 extended operands are implicitly shifted by LSL. */
	if (op_loc->extend_type != PERF_EXT_NONE)
		op_loc->shift_type = PERF_SHIFT_LSL;

	/* Parse shift amount if present */
	op_loc->amount = 0;
	if (op_loc->extend_type != PERF_EXT_NONE ||
	    op_loc->shift_type != PERF_SHIFT_NONE) {
		s = strchr(s, arch->objdump.imm_char);
		if (s)
			op_loc->amount = (u8)strtol(s + 1, NULL, 0);
	}
}

#ifdef HAVE_LIBDW_SUPPORT
/* Check if the instruction is read-only (does not modify register state). */
static bool is_readonly_insn(const char *name)
{
	return !strncmp(name, "cb", 2) || !strncmp(name, "tb", 2) ||
	       !strcmp(name, "tst") || !strcmp(name, "teq") ||
	       !strcmp(name, "cmp") || !strcmp(name, "cmn") ||
	       !strcmp(name, "ccmp") || !strcmp(name, "ccmn");
}

static void update_call_insn_state(struct type_state *state,
				   struct data_loc_info *dloc,
				   struct disasm_line *dl)
{
	struct symbol *func = dl->ops.target.sym;
	const char *call_name;
	u64 call_addr;
	u32 insn_offset = dl->al.offset;

	call_name = func ? func->name : dl->ops.target.name;
	pr_debug_dtp("call [%x] %s\n", insn_offset, call_name ?: "<unknown>");

	/* Invalidate caller-saved registers after call */
	call_addr = map__rip_2objdump(dloc->ms->map,
				      dloc->ms->sym->start + dl->al.offset);
	for (unsigned int i = 0; i < ARRAY_SIZE(state->regs); i++) {
		struct type_state_reg *reg = &state->regs[i];

		if (!reg->caller_saved)
			continue;
		/* Keep register valid within DWARF location lifetime */
		if (reg->lifetime_active && call_addr < reg->lifetime_end)
			continue;
		invalidate_reg_state(reg);
	}
}

/* Invalidate registers explicitly written by an unhandled instruction. */
static void invalidate_unhandled_insn_regs(struct type_state *state,
					   struct disasm_line *dl,
					   struct annotated_op_loc *dst)
{
	u32 insn_offset = dl->al.offset;

	/* Invalidate destination registers */
	if (has_reg_type(state, dst->reg1) && !dst->mem_ref) {
		pr_debug_dtp("%s [%x] invalidate reg%d",
			     dl->ins.name, insn_offset, dst->reg1);
		invalidate_reg_state(&state->regs[dst->reg1]);

		if (dst->multi_regs && has_reg_type(state, dst->reg2)) {
			pr_debug_dtp(" and reg%d", dst->reg2);
			invalidate_reg_state(&state->regs[dst->reg2]);
		}
		pr_debug_dtp("\n");
	}

	/* TODO: Extend invalidation support to more special instructions. */
}

static void update_insn_state_arm64(struct type_state *state,
				    struct data_loc_info *dloc, Dwarf_Die *cu_die __maybe_unused,
				    struct disasm_line *dl)
{
	struct annotated_insn_loc loc;
	struct annotated_op_loc *dst = &loc.ops[INSN_OP_TARGET];

	if (is_readonly_insn(dl->ins.name))
		return;

	if (annotate_get_insn_location(dloc->arch, dl, &loc) < 0)
		return;

	if (ins__is_call(&dl->ins))
		update_call_insn_state(state, dloc, dl);
	else
		invalidate_unhandled_insn_regs(state, dl, dst);
}
#endif

const struct arch *arch__new_arm64(const struct e_machine_and_e_flags *id,
				   const char *cpuid __maybe_unused)
{
	int err;
	struct arch_arm64 *arm = zalloc(sizeof(*arm));
	struct arch *arch;

	if (!arm)
		return NULL;

	arch = &arm->arch;
	arch->name = "arm64";
	arch->id = *id;
	arch->objdump.comment_char	  = '/';
	arch->objdump.skip_functions_char = '+';
	arch->objdump.memory_ref_char	  = '[';
	arch->objdump.imm_char		  = '#';
	arch->associate_instruction_ops   = arm64__associate_instruction_ops;
	arch->extract_op_location	  = extract_op_location_arm64;
#ifdef HAVE_LIBDW_SUPPORT
	arch->update_insn_state		  = update_insn_state_arm64;
#endif

	/* bl, blr */
	err = regcomp(&arm->call_insn, "^blr?$", REG_EXTENDED);
	if (err)
		goto out_free_arm;

	/* b, b.cond, br, cbz/cbnz, tbz/tbnz */
	err = regcomp(&arm->jump_insn, "^[ct]?br?\\.?(cc|cs|eq|ge|gt|hi|hs|le|lo|ls|lt|mi|ne|pl|vc|vs)?n?z?$",
		      REG_EXTENDED);
	if (err)
		goto out_free_call;

	/*
	 * The ARM64 architecture has many variants of load/store instructions.
	 * It is quite challenging to match all of them completely. Here, we
	 * only match the prefixes of these instructions.
	 */
	err = regcomp(&arm->ldst_insn, "^(ld|st|cas|prf|swp)",
		      REG_EXTENDED);
	if (err)
		goto out_free_jump;

	return arch;

out_free_jump:
	regfree(&arm->jump_insn);
out_free_call:
	regfree(&arm->call_insn);
out_free_arm:
	free(arm);
	errno = SYMBOL_ANNOTATE_ERRNO__ARCH_INIT_REGEXP;
	return NULL;
}
