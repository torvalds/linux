// SPDX-License-Identifier: GPL-2.0
#include <linux/compiler.h>

static struct ins_ops *s390__associate_ins_ops(struct arch *arch, const char *name)
{
	struct ins_ops *ops = NULL;

	/* catch all kind of jumps */
	if (strchr(name, 'j') ||
	    !strncmp(name, "bct", 3) ||
	    !strncmp(name, "br", 2))
		ops = &jump_ops;
	/* override call/returns */
	if (!strcmp(name, "bras") ||
	    !strcmp(name, "brasl") ||
	    !strcmp(name, "basr"))
		ops = &call_ops;
	if (!strcmp(name, "br"))
		ops = &ret_ops;

	arch__associate_ins_ops(arch, name, ops);
	return ops;
}

static int s390__annotate_init(struct arch *arch, char *cpuid __maybe_unused)
{
	if (!arch->initialized) {
		arch->initialized = true;
		arch->associate_instruction_ops = s390__associate_ins_ops;
	}

	return 0;
}
