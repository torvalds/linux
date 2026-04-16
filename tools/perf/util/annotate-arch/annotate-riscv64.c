// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include <linux/compiler.h>
#include <linux/zalloc.h>
#include "../disasm.h"

static
const struct ins_ops *riscv64__associate_ins_ops(struct arch *arch, const char *name)
{
	const struct ins_ops *ops = NULL;

	if (!strncmp(name, "jal", 3) ||
	    !strncmp(name, "jr", 2) ||
	    !strncmp(name, "call", 4))
		ops = &call_ops;
	else if (!strncmp(name, "ret", 3))
		ops = &ret_ops;
	else if (name[0] == 'j' || name[0] == 'b')
		ops = &jump_ops;
	else
		return NULL;

	arch__associate_ins_ops(arch, name, ops);

	return ops;
}

const struct arch *arch__new_riscv64(const struct e_machine_and_e_flags *id,
				     const char *cpuid __maybe_unused)
{
	struct arch *arch = zalloc(sizeof(*arch));

	if (!arch)
		return NULL;

	arch->name = "riscv";
	arch->id = *id;
	arch->objdump.comment_char = '#';
	arch->associate_instruction_ops = riscv64__associate_ins_ops;
	return arch;
}
