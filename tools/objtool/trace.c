// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2025, Oracle and/or its affiliates.
 */

#include <objtool/trace.h>

bool trace;
int trace_depth;

/*
 * Macros to trace CFI state attributes changes.
 */

#define TRACE_CFI_ATTR(attr, prev, next, fmt, ...)		\
({								\
	if ((prev)->attr != (next)->attr)			\
		TRACE("%s=" fmt " ", #attr, __VA_ARGS__);	\
})

#define TRACE_CFI_ATTR_BOOL(attr, prev, next)			\
	TRACE_CFI_ATTR(attr, prev, next,			\
		       "%s", (next)->attr ? "true" : "false")

#define TRACE_CFI_ATTR_NUM(attr, prev, next, fmt)		\
	TRACE_CFI_ATTR(attr, prev, next, fmt, (next)->attr)

#define CFI_REG_NAME_MAXLEN   16

/*
 * Return the name of a register. Note that the same static buffer
 * is returned if the name is dynamically generated.
 */
static const char *cfi_reg_name(unsigned int reg)
{
	static char rname_buffer[CFI_REG_NAME_MAXLEN];
	const char *rname;

	switch (reg) {
	case CFI_UNDEFINED:
		return "<undefined>";
	case CFI_CFA:
		return "cfa";
	case CFI_SP_INDIRECT:
		return "(sp)";
	case CFI_BP_INDIRECT:
		return "(bp)";
	}

	if (reg < CFI_NUM_REGS) {
		rname = arch_reg_name[reg];
		if (rname)
			return rname;
	}

	if (snprintf(rname_buffer, CFI_REG_NAME_MAXLEN, "r%d", reg) == -1)
		return "<error>";

	return (const char *)rname_buffer;
}

/*
 * Functions and macros to trace CFI registers changes.
 */

static void trace_cfi_reg(const char *prefix, int reg, const char *fmt,
			  int base_prev, int offset_prev,
			  int base_next, int offset_next)
{
	char *rname;

	if (base_prev == base_next && offset_prev == offset_next)
		return;

	if (prefix)
		TRACE("%s:", prefix);

	if (base_next == CFI_UNDEFINED) {
		TRACE("%1$s=<undef> ", cfi_reg_name(reg));
	} else {
		rname = strdup(cfi_reg_name(reg));
		TRACE(fmt, rname, cfi_reg_name(base_next), offset_next);
		free(rname);
	}
}

static void trace_cfi_reg_val(const char *prefix, int reg,
			      int base_prev, int offset_prev,
			      int base_next, int offset_next)
{
	trace_cfi_reg(prefix, reg, "%1$s=%2$s%3$+d ",
		      base_prev, offset_prev, base_next, offset_next);
}

static void trace_cfi_reg_ref(const char *prefix, int reg,
			      int base_prev, int offset_prev,
			      int base_next, int offset_next)
{
	trace_cfi_reg(prefix, reg, "%1$s=(%2$s%3$+d) ",
		      base_prev, offset_prev, base_next, offset_next);
}

#define TRACE_CFI_REG_VAL(reg, prev, next)				\
	trace_cfi_reg_val(NULL, reg, prev.base, prev.offset,		\
			  next.base, next.offset)

#define TRACE_CFI_REG_REF(reg, prev, next)				\
	trace_cfi_reg_ref(NULL, reg, prev.base, prev.offset,		\
			  next.base, next.offset)

void trace_insn_state(struct instruction *insn, struct insn_state *sprev,
		      struct insn_state *snext)
{
	struct cfi_state *cprev, *cnext;
	int i;

	if (!memcmp(sprev, snext, sizeof(struct insn_state)))
		return;

	cprev = &sprev->cfi;
	cnext = &snext->cfi;

	disas_print_insn(stderr, objtool_disas_ctx, insn,
			 trace_depth - 1, "state: ");

	/* print registers changes */
	TRACE_CFI_REG_VAL(CFI_CFA, cprev->cfa, cnext->cfa);
	for (i = 0; i < CFI_NUM_REGS; i++) {
		TRACE_CFI_REG_VAL(i, cprev->vals[i], cnext->vals[i]);
		TRACE_CFI_REG_REF(i, cprev->regs[i], cnext->regs[i]);
	}

	/* print attributes changes */
	TRACE_CFI_ATTR_NUM(stack_size, cprev, cnext, "%d");
	TRACE_CFI_ATTR_BOOL(drap, cprev, cnext);
	if (cnext->drap) {
		trace_cfi_reg_val("drap", cnext->drap_reg,
				  cprev->drap_reg, cprev->drap_offset,
				  cnext->drap_reg, cnext->drap_offset);
	}
	TRACE_CFI_ATTR_BOOL(bp_scratch, cprev, cnext);
	TRACE_CFI_ATTR_NUM(instr, sprev, snext, "%d");
	TRACE_CFI_ATTR_NUM(uaccess_stack, sprev, snext, "%u");

	TRACE("\n");

	insn->trace = 1;
}

void trace_alt_begin(struct instruction *orig_insn, struct alternative *alt,
		     char *alt_name)
{
	struct instruction *alt_insn;
	char suffix[2];

	alt_insn = alt->insn;

	if (alt->type == ALT_TYPE_EX_TABLE) {
		/*
		 * When there is an exception table then the instruction
		 * at the original location is executed but it can cause
		 * an exception. In that case, the execution will be
		 * redirected to the alternative instruction.
		 *
		 * The instruction at the original location can have
		 * instruction alternatives, so we just print the location
		 * of the instruction that can cause the exception and
		 * not the instruction itself.
		 */
		TRACE_ALT_INFO_NOADDR(orig_insn, "/ ", "%s for instruction at 0x%lx <%s+0x%lx>",
				      alt_name,
				      orig_insn->offset, orig_insn->sym->name,
				      orig_insn->offset - orig_insn->sym->offset);
	} else {
		TRACE_ALT_INFO_NOADDR(orig_insn, "/ ", "%s", alt_name);
	}

	if (alt->type == ALT_TYPE_JUMP_TABLE) {
		/*
		 * For a jump alternative, if the default instruction is
		 * a NOP then it is replaced with the jmp instruction,
		 * otherwise it is replaced with a NOP instruction.
		 */
		trace_depth++;
		if (orig_insn->type == INSN_NOP) {
			suffix[0] = (orig_insn->len == 5) ? 'q' : '\0';
			TRACE_ADDR(orig_insn, "jmp%-3s %lx <%s+0x%lx>", suffix,
				   alt_insn->offset, alt_insn->sym->name,
				   alt_insn->offset - alt_insn->sym->offset);
		} else {
			TRACE_ADDR(orig_insn, "nop%d", orig_insn->len);
			trace_depth--;
		}
	}
}

void trace_alt_end(struct instruction *orig_insn, struct alternative *alt,
		   char *alt_name)
{
	if (alt->type == ALT_TYPE_JUMP_TABLE && orig_insn->type == INSN_NOP)
		trace_depth--;
	TRACE_ALT_INFO_NOADDR(orig_insn, "\\ ", "%s", alt_name);
}
