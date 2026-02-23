// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2019 Hangzhou C-SKY Microsystems co.,ltd.
#include <string.h>
#include <linux/compiler.h>
#include <linux/zalloc.h>
#include "../disasm.h"

static const struct ins_ops *csky__associate_ins_ops(struct arch *arch,
						     const char *name)
{
	const struct ins_ops *ops = NULL;

	/* catch all kind of jumps */
	if (!strcmp(name, "bt") ||
	    !strcmp(name, "bf") ||
	    !strcmp(name, "bez") ||
	    !strcmp(name, "bnez") ||
	    !strcmp(name, "bnezad") ||
	    !strcmp(name, "bhsz") ||
	    !strcmp(name, "bhz") ||
	    !strcmp(name, "blsz") ||
	    !strcmp(name, "blz") ||
	    !strcmp(name, "br") ||
	    !strcmp(name, "jmpi") ||
	    !strcmp(name, "jmp"))
		ops = &jump_ops;

	/* catch function call */
	if (!strcmp(name, "bsr") ||
	    !strcmp(name, "jsri") ||
	    !strcmp(name, "jsr"))
		ops = &call_ops;

	/* catch function return */
	if (!strcmp(name, "rts"))
		ops = &ret_ops;

	if (ops)
		arch__associate_ins_ops(arch, name, ops);
	return ops;
}

const struct arch *arch__new_csky(const struct e_machine_and_e_flags *id,
				  const char *cpuid __maybe_unused)
{
	struct arch *arch = zalloc(sizeof(*arch));

	if (!arch)
		return NULL;

	arch->name = "csky";
	arch->id = *id;
	arch->objdump.comment_char = '/';
	arch->associate_instruction_ops = csky__associate_ins_ops;
	return arch;
}
