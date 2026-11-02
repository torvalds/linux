// SPDX-License-Identifier: GPL-2.0
#include <linux/compiler.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/zalloc.h>
#include <regex.h>
#include "../annotate.h"
#include "../disasm.h"
#include "../annotate-data.h"
#include "../debug.h"
#include "../dso.h"
#include "../map.h"
#include "../symbol.h"
#include "../strbuf.h"

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
				   struct data_loc_info *dloc, Dwarf_Die *cu_die,
				   struct disasm_line *dl)
{
	struct symbol *func = dl->ops.target.sym;
	const char *call_name;
	u64 call_addr;
	struct type_state_reg *tsr;
	Dwarf_Die type_die;
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

	/* Update register with the return type (if any) */
	if (call_name && die_find_func_rettype(cu_die, call_name, &type_die)) {
		tsr = &state->regs[state->ret_reg];
		invalidate_reg_state(tsr);
		tsr->type = type_die;
		tsr->kind = TSR_KIND_TYPE;
		tsr->offset = 0;
		tsr->ok = true;

		pr_debug_dtp("call [%x] return -> reg%d", insn_offset, state->ret_reg);
		pr_debug_type_name(&type_die, tsr->kind);
	}
}

/* Invalidate registers explicitly written by an unhandled instruction. */
static void invalidate_unhandled_insn_regs(struct type_state *state,
					   struct disasm_line *dl,
					   struct annotated_op_loc *src,
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

	/*
	 * Exclusive store instructions (stxr, stxp, stlxr, stlxp, etc.)
	 * write their execution status (0 or 1) into the status register
	 * (parsed into src->reg1). Invalidate this status register to prevent
	 * stale type propagation.
	 */
	if (!strncmp(dl->ins.name, "stx", 3) ||  /* stxr, stxp, stxrb, stxrh */
	    !strncmp(dl->ins.name, "stlx", 4)) { /* stlxr, stlxp, stlxrb, stlxrh */
		if (has_reg_type(state, src->reg1)) {
			pr_debug_dtp("%s [%x] invalidate reg%d\n",
				     dl->ins.name, insn_offset, src->reg1);
			invalidate_reg_state(&state->regs[src->reg1]);
		}
	}

	/* TODO: Extend invalidation support to more special instructions. */
}

static int arm64__reg_size(const char *reg)
{
	if (!reg || !*reg || !arm64__is_reg(reg))
		return -1;

	if (reg[0] == 'w')
		return 4;

	if (reg[0] == 'x' || !strncmp(reg, "sp", 2))
		return 8;

	return -1;
}

/* Apply addressing mode (pre-index, post-index) to register state */
static void adjust_reg_index_state(struct type_state *state,
				   struct data_loc_info *dloc,
				   struct disasm_line *dl,
				   struct annotated_op_loc *op_loc)
{
	struct type_state_reg *tsr;
	int reg = op_loc->reg1;
	int offset;

	if (op_loc->addr_mode != PERF_AAM_PRE_INDEX &&
	    op_loc->addr_mode != PERF_AAM_POST_INDEX)
		return;

	if (!has_reg_type(state, reg) || !state->regs[reg].ok)
		return;

	tsr = &state->regs[reg];
	tsr->copied_from = -1;

	if (arch_get_reg_offset(dloc->arch, op_loc, reg, state, true, &offset)) {
		invalidate_reg_state(tsr);
		return;
	}

	tsr->offset += offset;

	pr_debug_dtp("%s [%x] %s-index %#x(reg%d) -> reg%d", dl->ins.name,
		     (u32) dl->al.offset, op_loc->addr_mode == PERF_AAM_PRE_INDEX ?
		     "pre" : "post", offset, reg, reg);
	pr_debug_type_name(&tsr->type, tsr->kind);
}

/*
 * Match standard load variants (ldr, ldur, ldar, ldp) that follow
 * straightforward load semantics: memory source on the right
 * and target registers on the left (written). Excludes exclusive loads
 * (ldxr, ldxp, etc.) and acquire/exclusive combination variants.
 */
static bool is_standard_load_insn(const char *name)
{
	return !strncmp(name, "ldr", 3)  ||  /* ldr, ldrb, ldrh, ldrsb, ldrsh, ldrsw */
	       !strncmp(name, "ldur", 4) ||  /* ldur, ldurb, ldurh, ldursb, ldursh, ldursw */
	       !strncmp(name, "ldar", 4) ||  /* ldar, ldarb, ldarh */
	       !strncmp(name, "ldp", 3);     /* ldp, ldpsw */
}

/*
 * For load insns: propagate type from source reg state @src_states to @dreg.
 *
 * @mem_spacing accounts for the additional in-memory spacing between the two
 * loads when handling multi-reg loads (e.g. the second target reg in 'ldp'),
 * which is added to the reg offset.
 */
static int propagate_load_reg_state(struct type_state *state,
				    struct data_loc_info *dloc, Dwarf_Die *cu_die,
				    struct disasm_line *dl, int dreg,
				    struct annotated_op_loc *src,
				    struct type_state_reg **src_states,
				    int mem_spacing)
{
	struct type_state_reg *tsr;
	struct type_state_reg *src_tsr = src_states[0];
	Dwarf_Die type_die;
	u32 insn_offset = dl->al.offset;
	int sreg = src->reg1;
	int reg_offset;
	int fbreg = dloc->fbreg;
	int fboff = 0;

	if (!has_reg_type(state, dreg))
		return -1;

	tsr = &state->regs[dreg];
	tsr->copied_from = -1;
	tsr->lifetime_active = false;
	tsr->lifetime_end = 0;

	if (dloc->fb_cfa) {
		u64 ip = dloc->ms->sym->start + dl->al.offset;
		u64 pc = map__rip_2objdump(dloc->ms->map, ip);

		if (die_get_cfa(dloc->di->dbg, pc, &fbreg, &fboff) < 0)
			fbreg = -1;
	}

retry:
	if (arch_get_reg_offset(dloc->arch, src, sreg, state, false, &reg_offset))
		goto load_non_regoff;

	reg_offset += mem_spacing;

	/* Check stack variables with offset */
	if (sreg != -1 && (sreg == fbreg || sreg == state->stack_reg)) {
		struct type_state_stack *stack;
		int offset = sreg == fbreg ? reg_offset - fboff : reg_offset;

		stack = find_stack_state(state, offset);
		if (stack == NULL) {
			return -1;
		} else if (!stack->compound) {
			tsr->type = stack->type;
			tsr->kind = stack->kind;
			tsr->offset = stack->ptr_offset;
			tsr->imm_value = stack->imm_value;
			tsr->ok = true;
		} else if (die_get_member_type(&stack->type,
					       offset - stack->offset,
					       &type_die)) {
			tsr->type = type_die;
			tsr->kind = TSR_KIND_TYPE;
			tsr->offset = 0;
			tsr->imm_value = 0;
			tsr->ok = true;
		} else {
			return -1;
		}

		if (sreg == fbreg) {
			pr_debug_dtp("%s [%x] -%#x(stack) -> reg%d",
				     dl->ins.name, insn_offset, -offset, dreg);
		} else {
			pr_debug_dtp("%s [%x] %#x(reg%d) -> reg%d",
				     dl->ins.name, insn_offset, offset, sreg, dreg);
		}
		pr_debug_type_name(&tsr->type, tsr->kind);
		return 0;
	}

	if (!src_tsr || !src_tsr->ok)
		return -1;

	/* Dereference the pointer if it has one */
	if (src_tsr->kind == TSR_KIND_TYPE &&
	    die_deref_ptr_type(&src_tsr->type,
			       src_tsr->offset + reg_offset, &type_die)) {
		tsr->type = type_die;
		tsr->kind = TSR_KIND_TYPE;
		tsr->offset = 0;
		tsr->imm_value = 0;
		tsr->ok = true;

		pr_debug_dtp("%s [%x] %#x(reg%d) -> reg%d",
			     dl->ins.name, insn_offset, reg_offset, sreg, dreg);
		pr_debug_type_name(&tsr->type, tsr->kind);
		return 0;
	}

	/* Handle dereference of TSR_KIND_POINTER registers */
	if (src_tsr->kind == TSR_KIND_POINTER &&
	    die_get_member_type(&src_tsr->type,
				src_tsr->offset + reg_offset, &type_die)) {
		tsr->type = src_tsr->type;
		tsr->kind = TSR_KIND_TYPE;
		tsr->offset = src_tsr->offset + reg_offset;
		tsr->imm_value = 0;
		tsr->ok = true;

		pr_debug_dtp("%s [%x] addr %#x(reg%d) -> reg%d",
			     dl->ins.name, insn_offset, reg_offset, sreg, dreg);
		pr_debug_type_name(&tsr->type, tsr->kind);
		return 0;
	}

	/* Or check if it's a global variable */
	if (src_tsr->kind == TSR_KIND_GLOBAL_ADDR) {
		u64 ip = dloc->ms->sym->start + dl->al.offset;
		u64 addr = src_tsr->imm_value + reg_offset;
		int offset;
		u8 kind;
		const char *var_name = NULL;

		/* it might be per-cpu offset */
		if (dso__kernel(map__dso(dloc->ms->map)) &&
		    get_global_var_info(dloc, addr, &var_name, &offset) &&
		    !strcmp(var_name, "__per_cpu_offset"))
			kind = TSR_KIND_PERCPU_BASE;
		else
			kind = TSR_KIND_TYPE;

		if (!get_global_var_type(cu_die, dloc, ip, addr, &offset, &type_die) ||
		    !die_get_member_type(&type_die, offset, &type_die))
			return -1;

		tsr->type = type_die;
		tsr->kind = kind;
		tsr->offset = 0;
		tsr->imm_value = 0;
		tsr->ok = true;

		if (src->multi_regs) {
			pr_debug_dtp("%s [%x] global (reg%d, reg%d) -> reg%d",
				     dl->ins.name, insn_offset, src->reg1, src->reg2, dreg);
		} else {
			pr_debug_dtp("%s [%x] global (reg%d) -> reg%d",
				     dl->ins.name, insn_offset, sreg, dreg);
		}
		pr_debug_type_name(&tsr->type, tsr->kind);
		return 0;
	}

	/*
	 * If src_tsr->kind is TSR_KIND_PERCPU_BASE, treat it as an array.
	 * No special handling is needed here; just fall through to retry
	 * and resolve the type from the second register.
	 */

load_non_regoff:
	/* Try another register if any */
	if (src->multi_regs && src->reg1 != src->reg2 && sreg != src->reg2 &&
	    !(src->extend_type || src->shift_type)) {
		sreg = src->reg2;
		src_tsr = src_states[1];
		goto retry;
	}

	return -1;
}

static void update_load_insn_state(struct type_state *state,
				   struct data_loc_info *dloc, Dwarf_Die *cu_die,
				   struct disasm_line *dl,
				   struct annotated_op_loc *src,
				   struct annotated_op_loc *dst)
{
	struct type_state_reg snapshots[2];
	struct type_state_reg *src_states[2] = {};
	int mem_spacing = 0;

	if (!src->mem_ref ||  /* exclude PC-relative loads */
	    !has_reg_type(state, src->reg1)) {
		/* Invalidate tracked destination registers */
		if (has_reg_type(state, dst->reg1))
			invalidate_reg_state(&state->regs[dst->reg1]);
		if (dst->multi_regs && has_reg_type(state, dst->reg2))
			invalidate_reg_state(&state->regs[dst->reg2]);
		goto out_adjust;
	}

	/*
	 * Snapshot source register states before updating destination registers.
	 * This preserves the original source state for aliased loads (e.g.,
	 * ldp x0, x1, [x0]), where updating the first destination may overwrite
	 * the source register.
	 */
	snapshots[0] = state->regs[src->reg1];
	src_states[0] = &snapshots[0];
	if (src->multi_regs && has_reg_type(state, src->reg2)) {
		snapshots[1] = state->regs[src->reg2];
		src_states[1] = &snapshots[1];
	}

	/* Handle the first destination register */
	if (has_reg_type(state, dst->reg1)) {
		if (propagate_load_reg_state(state, dloc, cu_die, dl, dst->reg1,
					     src, src_states, mem_spacing))
			invalidate_reg_state(&state->regs[dst->reg1]);
	}

	if (!dst->multi_regs || !has_reg_type(state, dst->reg2))
		goto out_adjust;

	/* Handle the second destination register */
	if (!strcmp(dl->ins.name, "ldpsw"))
		mem_spacing = 4;  /* ldpsw loads two 32-bit signed words (4-byte spacing) */
	else
		mem_spacing = arm64__reg_size(dl->ops.target.raw);

	if (mem_spacing < 0 ||
	    propagate_load_reg_state(state, dloc, cu_die, dl, dst->reg2,
				     src, src_states, mem_spacing))
		invalidate_reg_state(&state->regs[dst->reg2]);

out_adjust:
	adjust_reg_index_state(state, dloc, dl, src);
}

/*
 * Match standard store variants (str, stur, stlr, stp) that follow
 * straightforward store semantics: source registers on the left (read-only)
 * and memory target on the right. Excludes exclusive stores (stxr, stxp, etc.)
 * which have status/result register side effects.
 */
static bool is_standard_store_insn(const char *name)
{
	return !strncmp(name, "str", 3)  ||  /* str, strb, strh */
	       !strncmp(name, "stur", 4) ||  /* stur, sturb, sturh */
	       !strncmp(name, "stlr", 4) ||  /* stlr, stlrb, stlrh */
	       !strncmp(name, "stp", 3);     /* stp */
}

/*
 * For store insns: propagate type from @sreg to the memory location referenced
 * by @dst, applying @mem_spacing to account for multi-regs stores (e.g. stp).
 */
static void propagate_store_reg_state(struct type_state *state,
				      struct data_loc_info *dloc,
				      struct disasm_line *dl, int sreg,
				      struct annotated_op_loc *dst,
				      int mem_spacing)
{
	struct type_state_reg *tsr;
	u32 insn_offset = dl->al.offset;
	int dreg = dst->reg1;
	int reg_offset;
	int fbreg = dloc->fbreg;
	int fboff = 0;

	if (arch_get_reg_offset(dloc->arch, dst, dreg, state, false, &reg_offset))
		return;

	reg_offset += mem_spacing;

	if (dloc->fb_cfa) {
		u64 ip = dloc->ms->sym->start + dl->al.offset;
		u64 pc = map__rip_2objdump(dloc->ms->map, ip);

		if (die_get_cfa(dloc->di->dbg, pc, &fbreg, &fboff) < 0)
			fbreg = -1;
	}

	/* Check stack variables with offset */
	if (dreg != -1 && (dreg == fbreg || dreg == state->stack_reg)) {
		struct type_state_stack *stack;
		int offset = dreg == fbreg ? reg_offset - fboff : reg_offset;

		if (!has_reg_type(state, sreg) || !state->regs[sreg].ok) {
			stack = find_stack_state(state, offset);
			/* Preserve compound states when only a member is overwritten. */
			if (stack && !stack->compound)
				delete_stack_state(state, offset);

			return;
		}

		tsr = &state->regs[sreg];

		stack = find_stack_state(state, offset);
		if (stack) {
			if (!stack->compound)
				set_stack_state(stack, offset, tsr->kind, &tsr->type,
						tsr->offset, tsr->imm_value);
			/*
			 * If it's a compound type, it means attempting to
			 * write to a member value of the compound type without
			 * changing the compound type itself, so do nothing.
			 */
		} else {
			findnew_stack_state(state, offset, tsr->kind, &tsr->type,
					    tsr->offset, tsr->imm_value);
		}

		if (dreg == fbreg) {
			pr_debug_dtp("%s [%x] reg%d -> -%#x(stack)",
				     dl->ins.name, insn_offset, sreg, -offset);
		} else {
			pr_debug_dtp("%s [%x] reg%d -> %#x(reg%d)",
				     dl->ins.name, insn_offset, sreg, offset, dreg);
		}
		if (tsr->offset != 0)
			pr_debug_dtp(" reg%d offset %#x ->", sreg, tsr->offset);

		pr_debug_type_name(&tsr->type, tsr->kind);
	}
	/*
	 * Ignore other transfers since it'd set a value in a struct
	 * and won't change the type.
	 */
}

static void update_store_insn_state(struct type_state *state,
				    struct data_loc_info *dloc,
				    struct disasm_line *dl,
				    struct annotated_op_loc *src,
				    struct annotated_op_loc *dst)
{
	int mem_spacing = 0;

	if (!has_reg_type(state, dst->reg1))
		return;

	/* Handle the first source register */
	propagate_store_reg_state(state, dloc, dl, src->reg1, dst, mem_spacing);

	/* Handle the second source register (if any) */
	if (src->multi_regs) {
		mem_spacing = arm64__reg_size(dl->ops.source.raw);

		if (mem_spacing >= 0)
			propagate_store_reg_state(state, dloc, dl, src->reg2,
						  dst, mem_spacing);
	}

	adjust_reg_index_state(state, dloc, dl, dst);
}

static void update_mov_insn_state(struct type_state *state,
				  struct disasm_line *dl,
				  struct annotated_op_loc *src,
				  struct annotated_op_loc *dst)
{
	struct type_state_reg *tsr;
	u32 insn_offset = dl->al.offset;
	int sreg = src->reg1;
	int dreg = dst->reg1;

	if (!has_reg_type(state, dreg))
		return;

	tsr = &state->regs[dreg];
	tsr->copied_from = -1;

	if (src->imm) {
		invalidate_reg_state(tsr);
		tsr->kind = TSR_KIND_CONST;
		tsr->imm_value = (s64)src->offset;
		tsr->offset = 0;
		tsr->ok = true;

		pr_debug_dtp("mov [%x] imm=%#"PRIx64" -> reg%d\n",
			     insn_offset, tsr->imm_value, dreg);
		return;
	}

	if (!has_reg_type(state, sreg) || !state->regs[sreg].ok) {
		invalidate_reg_state(tsr);
		return;
	}

	tsr->type = state->regs[sreg].type;
	tsr->kind = state->regs[sreg].kind;
	tsr->imm_value = state->regs[sreg].imm_value;
	tsr->offset = state->regs[sreg].offset;
	tsr->lifetime_active = state->regs[sreg].lifetime_active;
	tsr->lifetime_end = state->regs[sreg].lifetime_end;
	tsr->ok = state->regs[sreg].ok;

	if (tsr->kind == TSR_KIND_TYPE || tsr->kind == TSR_KIND_POINTER)
		tsr->copied_from = sreg;

	pr_debug_dtp("mov [%x] reg%d -> reg%d", insn_offset, sreg, dreg);
	pr_debug_type_name(&tsr->type, tsr->kind);
}

static void update_add_insn_state(struct type_state *state,
				  struct data_loc_info *dloc,
				  struct disasm_line *dl,
				  struct annotated_op_loc *src,
				  struct annotated_op_loc *dst)
{
	struct type_state_reg *tsr;
	struct type_state_reg src_tsr;
	u32 insn_offset = dl->al.offset;
	int sreg = src->reg1;
	int dreg = dst->reg1;
	int reg_offset;

	if (!has_reg_type(state, dreg))
		return;

	tsr = &state->regs[dreg];
	tsr->copied_from = -1;
	tsr->lifetime_active = false;
	tsr->lifetime_end = 0;

retry:
	if (!has_reg_type(state, sreg) || !state->regs[sreg].ok) {
		invalidate_reg_state(tsr);
		return;
	}

	src_tsr = state->regs[sreg];

	if (arch_get_reg_offset(dloc->arch, src, sreg, state, false, &reg_offset))
		goto add_non_regoff;

	if (src_tsr.kind == TSR_KIND_CONST || src_tsr.kind == TSR_KIND_GLOBAL_ADDR) {
		tsr->kind = src_tsr.kind;
		/* For 'adrp + add' pair: resolve the full global variable address. */
		tsr->imm_value = src_tsr.imm_value + reg_offset;
		tsr->offset = 0;
		tsr->ok = src_tsr.ok;

		pr_debug_dtp("add [%x] %s %#x(reg%d) -> reg%d\n",
			     insn_offset, src_tsr.kind == TSR_KIND_CONST ?
			     "imm" : "global", reg_offset, sreg, dreg);
		return;
	}

	if (src_tsr.kind == TSR_KIND_POINTER ||
	    (src_tsr.kind == TSR_KIND_TYPE &&
	     dwarf_tag(&src_tsr.type) == DW_TAG_pointer_type)) {
		tsr->type = src_tsr.type;
		tsr->kind = src_tsr.kind;
		tsr->imm_value = src_tsr.imm_value;
		tsr->offset = src_tsr.offset + reg_offset;
		tsr->ok = src_tsr.ok;

		pr_debug_dtp("add [%x] address of %#x(reg%d) -> reg%d",
			     insn_offset, reg_offset, sreg, dreg);
		pr_debug_type_name(&tsr->type, tsr->kind);
		return;
	}

	/*
	 * If src_tsr->kind is TSR_KIND_PERCPU_BASE, treat it as an array.
	 * No special handling is needed here; just fall through to retry
	 * and resolve the type from the second register.
	 */

add_non_regoff:
	/* Try another register if any */
	if (src->multi_regs && src->reg1 != src->reg2 && sreg != src->reg2 &&
	    !(src->extend_type || src->shift_type)) {
		sreg = src->reg2;
		goto retry;
	}
	invalidate_reg_state(tsr);
}

static void update_adrp_insn_state(struct type_state *state,
				   struct disasm_line *dl,
				   struct annotated_op_loc *dst)
{
	struct type_state_reg *tsr;
	u32 insn_offset = dl->al.offset;
	int dreg = dst->reg1;

	if (!has_reg_type(state, dreg))
		return;

	tsr = &state->regs[dreg];
	tsr->copied_from = -1;

	if (!dl->ops.source.addr) {
		invalidate_reg_state(tsr);
		return;
	}

	tsr->kind = TSR_KIND_GLOBAL_ADDR;
	/*
	 * Stores a partial page-relative address. The full absolute address
	 * of the global variable will be resolved when a subsequent 'add' or
	 * 'ldr' instruction consumes this register.
	 */
	tsr->imm_value = dl->ops.source.addr;
	tsr->offset = 0;
	tsr->lifetime_active = false;
	tsr->lifetime_end = 0;
	tsr->ok = true;

	pr_debug_dtp("adrp [%x] global addr=%#"PRIx64" -> reg%d\n",
		     insn_offset, tsr->imm_value, dreg);
}

static void update_mrs_insn_state(struct type_state *state,
				  struct data_loc_info *dloc, Dwarf_Die *cu_die,
				  struct disasm_line *dl,
				  struct annotated_op_loc *dst)
{
	struct type_state_reg *tsr;
	Dwarf_Die type_die;
	u32 insn_offset = dl->al.offset;
	int dreg = dst->reg1;
	Dwarf_Die func_die;
	Dwarf_Attribute attr;
	u64 ip, pc;
	struct strbuf sb;
	char *type_name;

	if (!has_reg_type(state, dreg))
		return;

	tsr = &state->regs[dreg];
	tsr->copied_from = -1;

	/* Handle case difference: LLVM (SP_EL0) vs objdump (sp_el0) */
	if (!dso__kernel(map__dso(dloc->ms->map)) ||
	    strcasecmp(dl->ops.source.raw, "sp_el0")) {
		invalidate_reg_state(tsr);
		return;
	}

	ip = dloc->ms->sym->start + dl->al.offset;
	pc = map__rip_2objdump(dloc->ms->map, ip);

	/*
	 * Find the inline function 'get_current()' Dwarf_Die and obtain its
	 * return value data type, which should be 'struct task_struct*'.
	 */
	if (!die_find_inlinefunc(cu_die, pc, &func_die) ||
	    !die_compare_name(&func_die, "get_current") ||
	    !dwarf_attr_integrate(&func_die, DW_AT_type, &attr) ||
	    !dwarf_formref_die(&attr, &type_die)) {
		invalidate_reg_state(tsr);
		return;
	}

	strbuf_init(&sb, 32);
	die_get_typename_from_type(&type_die, &sb);
	type_name = strbuf_detach(&sb, NULL);

	if (!type_name || strcmp(type_name, "struct task_struct*")) {
		invalidate_reg_state(tsr);
		goto out_free;
	}

	tsr->type = type_die;
	tsr->kind = TSR_KIND_TYPE;
	tsr->offset = 0;
	tsr->imm_value = 0;
	tsr->lifetime_active = false;
	tsr->lifetime_end = 0;
	tsr->ok = true;

	pr_debug_dtp("mrs [%x] sp_el0 -> reg%d", insn_offset, dreg);
	pr_debug_type_name(&type_die, tsr->kind);

out_free:
	free(type_name);
}

static void update_insn_state_arm64(struct type_state *state,
				    struct data_loc_info *dloc, Dwarf_Die *cu_die,
				    struct disasm_line *dl)
{
	struct annotated_insn_loc loc;
	struct annotated_op_loc *src = &loc.ops[INSN_OP_SOURCE];
	struct annotated_op_loc *dst = &loc.ops[INSN_OP_TARGET];

	if (is_readonly_insn(dl->ins.name))
		return;

	if (annotate_get_insn_location(dloc->arch, dl, &loc) < 0)
		return;

	if (ins__is_call(&dl->ins))
		update_call_insn_state(state, dloc, cu_die, dl);
	else if (!strcmp(dl->ins.name, "mrs"))
		update_mrs_insn_state(state, dloc, cu_die, dl, dst);
	else if (!strcmp(dl->ins.name, "adrp"))
		update_adrp_insn_state(state, dl, dst);
	else if (!strcmp(dl->ins.name, "add"))
		update_add_insn_state(state, dloc, dl, src, dst);
	/* Register to register or imm value to register transfers */
	else if (!strcmp(dl->ins.name, "mov"))
		update_mov_insn_state(state, dl, src, dst);
	/* Memory to register transfers */
	else if (is_standard_load_insn(dl->ins.name))
		update_load_insn_state(state, dloc, cu_die, dl, src, dst);
	/* Register to memory transfers */
	else if (is_standard_store_insn(dl->ins.name))
		update_store_insn_state(state, dloc, dl, src, dst);
	else
		invalidate_unhandled_insn_regs(state, dl, src, dst);
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
