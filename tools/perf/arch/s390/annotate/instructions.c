// SPDX-License-Identifier: GPL-2.0
#include <linux/compiler.h>

static struct ins_ops *s390__associate_ins_ops(struct arch *arch, const char *name)
{
	struct ins_ops *ops = NULL;

	/* catch all kind of jumps */
	if (strchr(name, 'j') ||
	    !strncmp(name, "bct", 3) ||
	    !strncmp(name, "br", 2))
		ops = &jump_ops;
	/* override call/returns */
	if (!strcmp(name, "bras") ||
	    !strcmp(name, "brasl") ||
	    !strcmp(name, "basr"))
		ops = &call_ops;
	if (!strcmp(name, "br"))
		ops = &ret_ops;

	if (ops)
		arch__associate_ins_ops(arch, name, ops);
	return ops;
}

static int s390__cpuid_parse(struct arch *arch, char *cpuid)
{
	unsigned int family;
	char model[16], model_c[16], cpumf_v[16], cpumf_a[16];
	int ret;

	/*
	 * cpuid string format:
	 * "IBM,family,model-capacity,model[,cpum_cf-version,cpum_cf-authorization]"
	 */
	ret = sscanf(cpuid, "%*[^,],%u,%[^,],%[^,],%[^,],%s", &family, model_c,
		     model, cpumf_v, cpumf_a);
	if (ret >= 2) {
		arch->family = family;
		arch->model = 0;
		return 0;
	}

	return -1;
}

static int s390__annotate_init(struct arch *arch, char *cpuid __maybe_unused)
{
	int err = 0;

	if (!arch->initialized) {
		arch->initialized = true;
		arch->associate_instruction_ops = s390__associate_ins_ops;
		if (cpuid)
			err = s390__cpuid_parse(arch, cpuid);
	}

	return err;
}
