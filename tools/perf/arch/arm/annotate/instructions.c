// SPDX-License-Identifier: GPL-2.0
#include <sys/types.h>
#include <regex.h>

struct arm_annotate {
	regex_t call_insn,
		jump_insn;
};

static struct ins_ops *arm__associate_instruction_ops(struct arch *arch, const char *name)
{
	struct arm_annotate *arm = arch->priv;
	struct ins_ops *ops;
	regmatch_t match[2];

	if (!regexec(&arm->call_insn, name, 2, match, 0))
		ops = &call_ops;
	else if (!regexec(&arm->jump_insn, name, 2, match, 0))
		ops = &jump_ops;
	else
		return NULL;

	arch__associate_ins_ops(arch, name, ops);
	return ops;
}

static int arm__annotate_init(struct arch *arch)
{
	struct arm_annotate *arm;
	int err;

	if (arch->initialized)
		return 0;

	arm = zalloc(sizeof(*arm));
	if (!arm)
		return -1;

#define ARM_CONDS "(cc|cs|eq|ge|gt|hi|le|ls|lt|mi|ne|pl|vc|vs)"
	err = regcomp(&arm->call_insn, "^blx?" ARM_CONDS "?$", REG_EXTENDED);
	if (err)
		goto out_free_arm;
	err = regcomp(&arm->jump_insn, "^bx?" ARM_CONDS "?$", REG_EXTENDED);
	if (err)
		goto out_free_call;
#undef ARM_CONDS

	arch->initialized = true;
	arch->priv	  = arm;
	arch->associate_instruction_ops   = arm__associate_instruction_ops;
	arch->objdump.comment_char	  = ';';
	arch->objdump.skip_functions_char = '+';
	return 0;

out_free_call:
	regfree(&arm->call_insn);
out_free_arm:
	free(arm);
	return -1;
}
