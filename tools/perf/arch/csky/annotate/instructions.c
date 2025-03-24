// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2019 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/compiler.h>

static struct ins_ops *csky__associate_ins_ops(struct arch *arch,
					       const char *name)
{
	struct ins_ops *ops = NULL;

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

static int csky__annotate_init(struct arch *arch, char *cpuid __maybe_unused)
{
	arch->initialized = true;
	arch->objdump.comment_char = '/';
	arch->associate_instruction_ops = csky__associate_ins_ops;
	arch->e_machine = EM_CSKY;
#if defined(__CSKYABIV2__)
	arch->e_flags = EF_CSKY_ABIV2;
#else
	arch->e_flags = EF_CSKY_ABIV1;
#endif
	return 0;
}
