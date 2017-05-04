#include <sys/types.h>
#include <regex.h>

struct arm64_annotate {
	regex_t call_insn,
		jump_insn;
};

static struct ins_ops *arm64__associate_instruction_ops(struct arch *arch, const char *name)
{
	struct arm64_annotate *arm = arch->priv;
	struct ins_ops *ops;
	regmatch_t match[2];

	if (!regexec(&arm->jump_insn, name, 2, match, 0))
		ops = &jump_ops;
	else if (!regexec(&arm->call_insn, name, 2, match, 0))
		ops = &call_ops;
	else if (!strcmp(name, "ret"))
		ops = &ret_ops;
	else
		return NULL;

	arch__associate_ins_ops(arch, name, ops);
	return ops;
}

static int arm64__annotate_init(struct arch *arch)
{
	struct arm64_annotate *arm;
	int err;

	if (arch->initialized)
		return 0;

	arm = zalloc(sizeof(*arm));
	if (!arm)
		return -1;

	/* bl, blr */
	err = regcomp(&arm->call_insn, "^blr?$", REG_EXTENDED);
	if (err)
		goto out_free_arm;
	/* b, b.cond, br, cbz/cbnz, tbz/tbnz */
	err = regcomp(&arm->jump_insn, "^[ct]?br?\\.?(cc|cs|eq|ge|gt|hi|le|ls|lt|mi|ne|pl)?n?z?$",
		      REG_EXTENDED);
	if (err)
		goto out_free_call;

	arch->initialized = true;
	arch->priv	  = arm;
	arch->associate_instruction_ops   = arm64__associate_instruction_ops;
	arch->objdump.comment_char	  = '/';
	arch->objdump.skip_functions_char = '+';
	return 0;

out_free_call:
	regfree(&arm->call_insn);
out_free_arm:
	free(arm);
	return -1;
}
