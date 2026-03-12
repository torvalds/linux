// SPDX-License-Identifier: GPL-2.0
#include <stdlib.h>
#include <linux/compiler.h>
#include <linux/zalloc.h>
#include <errno.h>
#include <regex.h>
#include "../annotate.h"
#include "../disasm.h"

struct arch_arm {
	struct arch arch;
	regex_t call_insn;
	regex_t jump_insn;
};

static const struct ins_ops *arm__associate_instruction_ops(struct arch *arch, const char *name)
{
	struct arch_arm *arm = container_of(arch, struct arch_arm, arch);
	const struct ins_ops *ops;
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

const struct arch *arch__new_arm(const struct e_machine_and_e_flags *id,
				 const char *cpuid __maybe_unused)
{
	int err;
	struct arch_arm *arm = zalloc(sizeof(*arm));
	struct arch *arch;

	if (!arm)
		return NULL;

	arch = &arm->arch;
	arch->name = "arm";
	arch->id = *id;
	arch->objdump.comment_char	  = ';';
	arch->objdump.skip_functions_char = '+';
	arch->associate_instruction_ops   = arm__associate_instruction_ops;

#define ARM_CONDS "(cc|cs|eq|ge|gt|hi|le|ls|lt|mi|ne|pl|vc|vs)"
	err = regcomp(&arm->call_insn, "^blx?" ARM_CONDS "?$", REG_EXTENDED);
	if (err)
		goto out_free_arm;

	err = regcomp(&arm->jump_insn, "^bx?" ARM_CONDS "?$", REG_EXTENDED);
	if (err)
		goto out_free_call;
#undef ARM_CONDS

	return arch;

out_free_call:
	regfree(&arm->call_insn);
out_free_arm:
	free(arm);
	errno = SYMBOL_ANNOTATE_ERRNO__ARCH_INIT_REGEXP;
	return NULL;
}
