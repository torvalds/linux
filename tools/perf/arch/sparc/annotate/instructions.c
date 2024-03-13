// SPDX-License-Identifier: GPL-2.0

static int is_branch_cond(const char *cond)
{
	if (cond[0] == '\0')
		return 1;

	if (cond[0] == 'a' && cond[1] == '\0')
		return 1;

	if (cond[0] == 'c' &&
	    (cond[1] == 'c' || cond[1] == 's') &&
	    cond[2] == '\0')
		return 1;

	if (cond[0] == 'e' &&
	    (cond[1] == '\0' ||
	     (cond[1] == 'q' && cond[2] == '\0')))
		return 1;

	if (cond[0] == 'g' &&
	    (cond[1] == '\0' ||
	     (cond[1] == 't' && cond[2] == '\0') ||
	     (cond[1] == 'e' && cond[2] == '\0') ||
	     (cond[1] == 'e' && cond[2] == 'u' && cond[3] == '\0')))
		return 1;

	if (cond[0] == 'l' &&
	    (cond[1] == '\0' ||
	     (cond[1] == 't' && cond[2] == '\0') ||
	     (cond[1] == 'u' && cond[2] == '\0') ||
	     (cond[1] == 'e' && cond[2] == '\0') ||
	     (cond[1] == 'e' && cond[2] == 'u' && cond[3] == '\0')))
		return 1;

	if (cond[0] == 'n' &&
	    (cond[1] == '\0' ||
	     (cond[1] == 'e' && cond[2] == '\0') ||
	     (cond[1] == 'z' && cond[2] == '\0') ||
	     (cond[1] == 'e' && cond[2] == 'g' && cond[3] == '\0')))
		return 1;

	if (cond[0] == 'b' &&
	    cond[1] == 'p' &&
	    cond[2] == 'o' &&
	    cond[3] == 's' &&
	    cond[4] == '\0')
		return 1;

	if (cond[0] == 'v' &&
	    (cond[1] == 'c' || cond[1] == 's') &&
	    cond[2] == '\0')
		return 1;

	if (cond[0] == 'b' &&
	    cond[1] == 'z' &&
	    cond[2] == '\0')
		return 1;

	return 0;
}

static int is_branch_reg_cond(const char *cond)
{
	if ((cond[0] == 'n' || cond[0] == 'l') &&
	    cond[1] == 'z' &&
	    cond[2] == '\0')
		return 1;

	if (cond[0] == 'z' &&
	    cond[1] == '\0')
		return 1;

	if ((cond[0] == 'g' || cond[0] == 'l') &&
	    cond[1] == 'e' &&
	    cond[2] == 'z' &&
	    cond[3] == '\0')
		return 1;

	if (cond[0] == 'g' &&
	    cond[1] == 'z' &&
	    cond[2] == '\0')
		return 1;

	return 0;
}

static int is_branch_float_cond(const char *cond)
{
	if (cond[0] == '\0')
		return 1;

	if ((cond[0] == 'a' || cond[0] == 'e' ||
	     cond[0] == 'z' || cond[0] == 'g' ||
	     cond[0] == 'l' || cond[0] == 'n' ||
	     cond[0] == 'o' || cond[0] == 'u') &&
	    cond[1] == '\0')
		return 1;

	if (((cond[0] == 'g' && cond[1] == 'e') ||
	     (cond[0] == 'l' && (cond[1] == 'e' ||
				 cond[1] == 'g')) ||
	     (cond[0] == 'n' && (cond[1] == 'e' ||
				 cond[1] == 'z')) ||
	     (cond[0] == 'u' && (cond[1] == 'e' ||
				 cond[1] == 'g' ||
				 cond[1] == 'l'))) &&
	    cond[2] == '\0')
		return 1;

	if (cond[0] == 'u' &&
	    (cond[1] == 'g' || cond[1] == 'l') &&
	    cond[2] == 'e' &&
	    cond[3] == '\0')
		return 1;

	return 0;
}

static struct ins_ops *sparc__associate_instruction_ops(struct arch *arch, const char *name)
{
	struct ins_ops *ops = NULL;

	if (!strcmp(name, "call") ||
	    !strcmp(name, "jmp") ||
	    !strcmp(name, "jmpl")) {
		ops = &call_ops;
	} else if (!strcmp(name, "ret") ||
		   !strcmp(name, "retl") ||
		   !strcmp(name, "return")) {
		ops = &ret_ops;
	} else if (!strcmp(name, "mov")) {
		ops = &mov_ops;
	} else {
		if (name[0] == 'c' &&
		    (name[1] == 'w' || name[1] == 'x'))
			name += 2;

		if (name[0] == 'b') {
			const char *cond = name + 1;

			if (cond[0] == 'r') {
				if (is_branch_reg_cond(cond + 1))
					ops = &jump_ops;
			} else if (is_branch_cond(cond)) {
				ops = &jump_ops;
			}
		} else if (name[0] == 'f' && name[1] == 'b') {
			if (is_branch_float_cond(name + 2))
				ops = &jump_ops;
		}
	}

	if (ops)
		arch__associate_ins_ops(arch, name, ops);

	return ops;
}

static int sparc__annotate_init(struct arch *arch, char *cpuid __maybe_unused)
{
	if (!arch->initialized) {
		arch->initialized = true;
		arch->associate_instruction_ops = sparc__associate_instruction_ops;
		arch->objdump.comment_char = '#';
	}

	return 0;
}
