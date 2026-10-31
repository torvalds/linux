// SPDX-License-Identifier: GPL-2.0
#include <stdlib.h>
#include <string.h>
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/zalloc.h>
#include "../../util/disasm.h"
#include "../../util/map.h"
#include "../../util/maps.h"
#include "../../util/symbol.h"
#include "../../util/thread.h"

/*
 * Alpha control-transfer instructions, as printed by objdump:
 *
 *   PC-relative (opcode group 0x30-0x3f), 21-bit displacement:
 *     br, bsr                             unconditional / to-subroutine
 *     beq bne blt ble bgt bge blbc blbs   integer conditional
 *     fbeq fbne fblt fble fbgt fbge       floating conditional
 *
 *   Register-indirect (JSR group, opcode 0x1a):
 *     jmp, jsr, ret, jcr
 *
 * bsr/jsr (and jcr, the coroutine form, which binutils prints in preference to
 * the jsr_coroutine spelling) save a return address, so they are calls; ret
 * returns; everything else that transfers control is a jump.
 *
 * Alpha has no machine "mov"; objdump prints "mov"/"fmov" as pseudos for
 * bis/cpys, so map them to mov_ops when present.  The no-ops are deliberately
 * left alone: nop_ops would let delete_last_nop() trim the padding gcc leaves
 * at the end of a function, but its scnprintf() prints the literal "nop", and
 * Alpha pads with unop (ldq_u $31) rather than nop.
 */

/*
 * The generic call__parse() expects the target address to be the first thing
 * in the operand string, but a bsr prints its return-address register first:
 *
 *	bsr	t0,fffffc0001031dc0 <cserve_ena>
 *
 * so take the address from after the comma.  Without this the address comes
 * out as 0, and neither the callee symbol nor the annotation browser's
 * "go to target" work.
 */
static int alpha_call__parse(const struct arch *arch, struct ins_operands *ops,
			     struct map_symbol *ms,
			     struct disasm_line *dl __maybe_unused)
{
	char *endptr, *tok, *name;
	struct map *map = ms->map;
	struct addr_map_symbol target;

	tok = strchr(ops->raw, ',');
	if (tok == NULL)
		return -1;

	ops->target.addr = strtoull(tok + 1, &endptr, 16);
	if (endptr == tok + 1)
		return -1;

	/* A stripped object has no "<symbol>" to name the target with. */
	name = strchr(endptr, '<');
	if (name == NULL)
		goto find_target;

	name++;

	if (arch->objdump.skip_functions_char &&
	    strchr(name, arch->objdump.skip_functions_char))
		return -1;

	tok = strchr(name, '>');
	if (tok == NULL)
		return -1;

	*tok = '\0';
	ops->target.name = strdup(name);
	*tok = '>';

	if (ops->target.name == NULL)
		return -1;

find_target:
	target = (struct addr_map_symbol) {
		.ms = { .map = map__get(map), },
		.addr = map__objdump_2mem(map, ops->target.addr),
	};

	if (maps__find_ams(thread__maps(ms->thread), &target) == 0 &&
	    map__rip_2objdump(target.ms.map,
			      map__map_ip(target.ms.map, target.addr)) == ops->target.addr)
		ops->target.sym = target.ms.sym;

	addr_map_symbol__exit(&target);
	return 0;
}

static const struct ins_ops alpha_call_ops = {
	.parse	   = alpha_call__parse,
	.scnprintf = call__scnprintf,
	.is_call   = true,
};

/*
 * jsr and jmp transfer control to a register, and their trailing operand is
 * only a branch prediction hint:
 *
 *	jsr	ra,(t12),fffffc0001014ee8 <_printk>
 *
 * binutils extracts that hint as a 14-bit signed field scaled by four and
 * prints it relative to the next instruction (extract_jhint() in alpha-opc.c,
 * print_insn_alpha() in alpha-dis.c), so it can name the callee only when the
 * callee lies within the resulting +-32KB.  It also defaults to zero, which
 * prints as the next instruction.  Most hints are therefore not the callee at
 * all, and parsing one would invent a call target, so these resolve no target
 * and keep their operands, as an indirect call does elsewhere.  The hint on
 * jcr is not even an address.
 */
static const struct ins_ops alpha_indirect_call_ops = {
	.scnprintf = ins__raw_scnprintf,
	.is_call   = true,
};

static const struct ins_ops alpha_indirect_jump_ops = {
	.scnprintf = ins__raw_scnprintf,
	.is_jump   = true,
};

static int is_alpha_cond_branch(const char *name)
{
	static const char *const branches[] = {
		"beq", "bne", "blt", "ble", "bgt", "bge", "blbc", "blbs",
		"fbeq", "fbne", "fblt", "fble", "fbgt", "fbge",
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(branches); i++) {
		if (!strcmp(name, branches[i]))
			return 1;
	}
	return 0;
}

static const struct ins_ops *alpha__associate_instruction_ops(struct arch *arch, const char *name)
{
	const struct ins_ops *ops = NULL;

	if (!strcmp(name, "bsr")) {
		ops = &alpha_call_ops;
	} else if (!strcmp(name, "jsr") ||
		   !strcmp(name, "jcr")) {
		ops = &alpha_indirect_call_ops;
	} else if (!strcmp(name, "ret")) {
		ops = &ret_ops;
	} else if (!strcmp(name, "jmp")) {
		ops = &alpha_indirect_jump_ops;
	} else if (!strcmp(name, "br") ||
		   is_alpha_cond_branch(name)) {
		ops = &jump_ops;
	} else if (!strcmp(name, "mov") ||
		   !strcmp(name, "fmov")) {
		ops = &mov_ops;
	}

	if (ops)
		arch__associate_ins_ops(arch, name, ops);

	return ops;
}

const struct arch *arch__new_alpha(const struct e_machine_and_e_flags *id,
				   const char *cpuid __maybe_unused)
{
	struct arch *arch = zalloc(sizeof(*arch));

	if (!arch)
		return NULL;

	arch->name = "alpha";
	arch->id = *id;
	arch->associate_instruction_ops = alpha__associate_instruction_ops;
	/* objdump emits no comments for Alpha; '#' is what the assembler uses. */
	arch->objdump.comment_char = '#';
	return arch;
}
