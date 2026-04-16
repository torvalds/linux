// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include <linux/compiler.h>
#include "../debug.h"
#include "../disasm.h"
#include "../map.h"
#include "../maps.h"
#include "../symbol.h"
#include "../thread.h"
#include "../annotate.h"
#include "../annotate-data.h"

static int s390_call__parse(const struct arch *arch, struct ins_operands *ops,
			    struct map_symbol *ms,
			    struct disasm_line *dl __maybe_unused)
{
	char *endptr, *tok, *name;
	struct map *map = ms->map;
	struct addr_map_symbol target;

	tok = strchr(ops->raw, ',');
	if (!tok)
		return -1;

	ops->target.addr = strtoull(tok + 1, &endptr, 16);

	name = strchr(endptr, '<');
	if (name == NULL)
		return -1;

	name++;

	if (arch->objdump.skip_functions_char &&
	    strchr(name, arch->objdump.skip_functions_char))
		return -1;

	tok = strchr(name, '>');
	if (tok == NULL)
		return -1;

	*tok = '\0';
	ops->target.name = strdup(name);
	*tok = '>';

	if (ops->target.name == NULL)
		return -1;

	target = (struct addr_map_symbol) {
		.ms = { .map = map__get(map), },
		.addr = map__objdump_2mem(map, ops->target.addr),
	};

	if (maps__find_ams(thread__maps(ms->thread), &target) == 0 &&
	    map__rip_2objdump(target.ms.map, map__map_ip(target.ms.map, target.addr)) == ops->target.addr)
		ops->target.sym = target.ms.sym;

	addr_map_symbol__exit(&target);
	return 0;
}

static const struct ins_ops s390_call_ops = {
	.parse	   = s390_call__parse,
	.scnprintf = call__scnprintf,
	.is_call   = true,
};

static int s390_mov__parse(const struct arch *arch __maybe_unused,
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
	ops->target.name = strdup(s + 1);
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


static const struct ins_ops s390_mov_ops = {
	.parse	   = s390_mov__parse,
	.scnprintf = mov__scnprintf,
};

static const struct ins_ops *s390__associate_ins_ops(struct arch *arch, const char *name)
{
	const struct ins_ops *ops = NULL;

	/* catch all kind of jumps */
	if (strchr(name, 'j') ||
	    !strncmp(name, "bct", 3) ||
	    !strncmp(name, "br", 2))
		ops = &jump_ops;
	/* override call/returns */
	if (!strcmp(name, "bras") ||
	    !strcmp(name, "brasl") ||
	    !strcmp(name, "basr"))
		ops = &s390_call_ops;
	if (!strcmp(name, "br"))
		ops = &ret_ops;
	/* override load/store relative to PC */
	if (!strcmp(name, "lrl") ||
	    !strcmp(name, "lgrl") ||
	    !strcmp(name, "lgfrl") ||
	    !strcmp(name, "llgfrl") ||
	    !strcmp(name, "strl") ||
	    !strcmp(name, "stgrl"))
		ops = &s390_mov_ops;

	if (ops)
		arch__associate_ins_ops(arch, name, ops);
	return ops;
}

static int s390__cpuid_parse(struct arch *arch, const char *cpuid)
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

const struct arch *arch__new_s390(const struct e_machine_and_e_flags *id, const char *cpuid)
{
	struct arch *arch = zalloc(sizeof(*arch));

	if (!arch)
		return NULL;

	arch->name = "s390";
	arch->id = *id;
	arch->associate_instruction_ops = s390__associate_ins_ops;
	if (cpuid) {
		if (s390__cpuid_parse(arch, cpuid)) {
			errno = SYMBOL_ANNOTATE_ERRNO__ARCH_INIT_CPUID_PARSING;
			return NULL;
		}
	}
	arch->objdump.comment_char = '#';
	return arch;
}
