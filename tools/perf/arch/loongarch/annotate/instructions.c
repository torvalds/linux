// SPDX-License-Identifier: GPL-2.0
/*
 * Perf annotate functions.
 *
 * Copyright (C) 2020-2023 Loongson Technology Corporation Limited
 */

static
struct ins_ops *loongarch__associate_ins_ops(struct arch *arch, const char *name)
{
	struct ins_ops *ops = NULL;

	if (!strncmp(name, "beqz", 4) ||
	    !strncmp(name, "bnez", 4) ||
	    !strncmp(name, "beq", 3) ||
	    !strncmp(name, "bne", 3) ||
	    !strncmp(name, "blt", 3) ||
	    !strncmp(name, "bge", 3) ||
	    !strncmp(name, "bltu", 4) ||
	    !strncmp(name, "bgeu", 4) ||
	    !strncmp(name, "bl", 2))
		ops = &call_ops;
	else if (!strncmp(name, "jirl", 4))
		ops = &ret_ops;
	else if (name[0] == 'b')
		ops = &jump_ops;
	else
		return NULL;

	arch__associate_ins_ops(arch, name, ops);

	return ops;
}

static
int loongarch__annotate_init(struct arch *arch, char *cpuid __maybe_unused)
{
	if (!arch->initialized) {
		arch->associate_instruction_ops = loongarch__associate_ins_ops;
		arch->initialized = true;
		arch->objdump.comment_char = '#';
	}

	return 0;
}
