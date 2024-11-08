// SPDX-License-Identifier: GPL-2.0

static
struct ins_ops *riscv64__associate_ins_ops(struct arch *arch, const char *name)
{
	struct ins_ops *ops = NULL;

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

static
int riscv64__annotate_init(struct arch *arch, char *cpuid __maybe_unused)
{
	if (!arch->initialized) {
		arch->associate_instruction_ops = riscv64__associate_ins_ops;
		arch->initialized = true;
		arch->objdump.comment_char = '#';
		arch->e_machine = EM_RISCV;
		arch->e_flags = 0;
	}

	return 0;
}
