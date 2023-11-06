// SPDX-License-Identifier: GPL-2.0
/*
 * Perf annotate functions.
 *
 * Copyright (C) 2020-2023 Loongson Technology Corporation Limited
 */

static int loongarch_call__parse(struct arch *arch, struct ins_operands *ops, struct map_symbol *ms)
{
	char *c, *endptr, *tok, *name;
	struct map *map = ms->map;
	struct addr_map_symbol target = {
		.ms = { .map = map, },
	};

	c = strchr(ops->raw, '#');
	if (c++ == NULL)
		return -1;

	ops->target.addr = strtoull(c, &endptr, 16);

	name = strchr(endptr, '<');
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

	target.addr = map__objdump_2mem(map, ops->target.addr);

	if (maps__find_ams(ms->maps, &target) == 0 &&
	    map__rip_2objdump(target.ms.map, map__map_ip(target.ms.map, target.addr)) == ops->target.addr)
		ops->target.sym = target.ms.sym;

	return 0;
}

static struct ins_ops loongarch_call_ops = {
	.parse	   = loongarch_call__parse,
	.scnprintf = call__scnprintf,
};

static int loongarch_jump__parse(struct arch *arch, struct ins_operands *ops, struct map_symbol *ms)
{
	struct map *map = ms->map;
	struct symbol *sym = ms->sym;
	struct addr_map_symbol target = {
		.ms = { .map = map, },
	};
	const char *c = strchr(ops->raw, '#');
	u64 start, end;

	ops->raw_comment = strchr(ops->raw, arch->objdump.comment_char);
	ops->raw_func_start = strchr(ops->raw, '<');

	if (ops->raw_func_start && c > ops->raw_func_start)
		c = NULL;

	if (c++ != NULL)
		ops->target.addr = strtoull(c, NULL, 16);
	else
		ops->target.addr = strtoull(ops->raw, NULL, 16);

	target.addr = map__objdump_2mem(map, ops->target.addr);
	start = map__unmap_ip(map, sym->start);
	end = map__unmap_ip(map, sym->end);

	ops->target.outside = target.addr < start || target.addr > end;

	if (maps__find_ams(ms->maps, &target) == 0 &&
	    map__rip_2objdump(target.ms.map, map__map_ip(target.ms.map, target.addr)) == ops->target.addr)
		ops->target.sym = target.ms.sym;

	if (!ops->target.outside) {
		ops->target.offset = target.addr - start;
		ops->target.offset_avail = true;
	} else {
		ops->target.offset_avail = false;
	}

	return 0;
}

static struct ins_ops loongarch_jump_ops = {
	.parse	   = loongarch_jump__parse,
	.scnprintf = jump__scnprintf,
};

static
struct ins_ops *loongarch__associate_ins_ops(struct arch *arch, const char *name)
{
	struct ins_ops *ops = NULL;

	if (!strcmp(name, "bl"))
		ops = &loongarch_call_ops;
	else if (!strcmp(name, "jirl"))
		ops = &ret_ops;
	else if (!strcmp(name, "b") ||
		 !strncmp(name, "beq", 3) ||
		 !strncmp(name, "bne", 3) ||
		 !strncmp(name, "blt", 3) ||
		 !strncmp(name, "bge", 3) ||
		 !strncmp(name, "bltu", 4) ||
		 !strncmp(name, "bgeu", 4))
		ops = &loongarch_jump_ops;
	else
		return NULL;

	arch__associate_ins_ops(arch, name, ops);

	return ops;
}

static
int loongarch__annotate_init(struct arch *arch, char *cpuid __maybe_unused)
{
	if (!arch->initialized) {
		arch->associate_instruction_ops = loongarch__associate_ins_ops;
		arch->initialized = true;
		arch->objdump.comment_char = '#';
	}

	return 0;
}
