// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include <linux/compiler.h>
#include <linux/zalloc.h>
#include "../disasm.h"

static
const struct ins_ops *mips__associate_ins_ops(struct arch *arch, const char *name)
{
	const struct ins_ops *ops = NULL;

	if (!strncmp(name, "bal", 3) ||
	    !strncmp(name, "bgezal", 6) ||
	    !strncmp(name, "bltzal", 6) ||
	    !strncmp(name, "bgtzal", 6) ||
	    !strncmp(name, "blezal", 6) ||
	    !strncmp(name, "beqzal", 6) ||
	    !strncmp(name, "bnezal", 6) ||
	    !strncmp(name, "bgtzl", 5) ||
	    !strncmp(name, "bltzl", 5) ||
	    !strncmp(name, "bgezl", 5) ||
	    !strncmp(name, "blezl", 5) ||
	    !strncmp(name, "jialc", 5) ||
	    !strncmp(name, "beql", 4) ||
	    !strncmp(name, "bnel", 4) ||
	    !strncmp(name, "jal", 3))
		ops = &call_ops;
	else if (!strncmp(name, "jr", 2))
		ops = &ret_ops;
	else if (name[0] == 'j' || name[0] == 'b')
		ops = &jump_ops;
	else
		return NULL;

	arch__associate_ins_ops(arch, name, ops);

	return ops;
}

const struct arch *arch__new_mips(const struct e_machine_and_e_flags *id,
				  const char *cpuid __maybe_unused)
{
	struct arch *arch = zalloc(sizeof(*arch));

	if (!arch)
		return NULL;

	arch->name = "mips";
	arch->id = *id;
	arch->objdump.comment_char = '#';
	arch->associate_instruction_ops = mips__associate_ins_ops;
	return arch;
}
