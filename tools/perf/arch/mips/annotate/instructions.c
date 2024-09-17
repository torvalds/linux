// SPDX-License-Identifier: GPL-2.0

static
struct ins_ops *mips__associate_ins_ops(struct arch *arch, const char *name)
{
	struct ins_ops *ops = NULL;

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

static
int mips__annotate_init(struct arch *arch, char *cpuid __maybe_unused)
{
	if (!arch->initialized) {
		arch->associate_instruction_ops = mips__associate_ins_ops;
		arch->initialized = true;
		arch->objdump.comment_char = '#';
	}

	return 0;
}
