// SPDX-License-Identifier: GPL-2.0
#include <linux/compiler.h>
#include <sys/types.h>
#include <regex.h>
#include <stdlib.h>

struct arm64_annotate {
	regex_t call_insn,
		jump_insn;
};

static int arm64_mov__parse(struct arch *arch __maybe_unused,
			    struct ins_operands *ops,
			    struct map_symbol *ms __maybe_unused,
			    struct disasm_line *dl __maybe_unused)
{
	char *s = strchr(ops->raw, ','), *target, *endptr;

	if (s == NULL)
		return -1;

	*s = '\0';
	ops->source.raw = strdup(ops->raw);
	*s = ',';

	if (ops->source.raw == NULL)
		return -1;

	target = ++s;
	ops->target.raw = strdup(target);
	if (ops->target.raw == NULL)
		goto out_free_source;

	ops->target.addr = strtoull(target, &endptr, 16);
	if (endptr == target)
		goto out_free_target;

	s = strchr(endptr, '<');
	if (s == NULL)
		goto out_free_target;
	endptr = strchr(s + 1, '>');
	if (endptr == NULL)
		goto out_free_target;

	*endptr = '\0';
	*s = ' ';
	ops->target.name = strdup(s);
	*s = '<';
	*endptr = '>';
	if (ops->target.name == NULL)
		goto out_free_target;

	return 0;

out_free_target:
	zfree(&ops->target.raw);
out_free_source:
	zfree(&ops->source.raw);
	return -1;
}

static int mov__scnprintf(struct ins *ins, char *bf, size_t size,
			  struct ins_operands *ops, int max_ins_name);

static struct ins_ops arm64_mov_ops = {
	.parse	   = arm64_mov__parse,
	.scnprintf = mov__scnprintf,
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
		ops = &arm64_mov_ops;

	arch__associate_ins_ops(arch, name, ops);
	return ops;
}

static int arm64__annotate_init(struct arch *arch, char *cpuid __maybe_unused)
{
	struct arm64_annotate *arm;
	int err;

	if (arch->initialized)
		return 0;

	arm = zalloc(sizeof(*arm));
	if (!arm)
		return ENOMEM;

	/* bl, blr */
	err = regcomp(&arm->call_insn, "^blr?$", REG_EXTENDED);
	if (err)
		goto out_free_arm;
	/* b, b.cond, br, cbz/cbnz, tbz/tbnz */
	err = regcomp(&arm->jump_insn, "^[ct]?br?\\.?(cc|cs|eq|ge|gt|hi|hs|le|lo|ls|lt|mi|ne|pl|vc|vs)?n?z?$",
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
	return SYMBOL_ANNOTATE_ERRNO__ARCH_INIT_REGEXP;
}
