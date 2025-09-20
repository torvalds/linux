// SPDX-License-Identifier: GPL-2.0-only
/*
 * System call table mapper
 *
 * (C) 2016 Arnaldo Carvalho de Melo <acme@redhat.com>
 */

#include "syscalltbl.h"
#include <stdlib.h>
#include <asm/bitsperlong.h>
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/zalloc.h>

#include <string.h>
#include "string2.h"

#include "trace/beauty/generated/syscalltbl.c"

static const struct syscalltbl *find_table(int e_machine)
{
	static const struct syscalltbl *last_table;
	static int last_table_machine = EM_NONE;

	/* Tables only exist for EM_SPARC. */
	if (e_machine == EM_SPARCV9)
		e_machine = EM_SPARC;

	if (last_table_machine == e_machine && last_table != NULL)
		return last_table;

	for (size_t i = 0; i < ARRAY_SIZE(syscalltbls); i++) {
		const struct syscalltbl *entry = &syscalltbls[i];

		if (entry->e_machine != e_machine && entry->e_machine != EM_NONE)
			continue;

		last_table = entry;
		last_table_machine = e_machine;
		return entry;
	}
	return NULL;
}

const char *syscalltbl__name(int e_machine, int id)
{
	const struct syscalltbl *table = find_table(e_machine);

	if (e_machine == EM_MIPS && id > 1000) {
		/*
		 * MIPS may encode the N32/64/O32 type in the high part of
		 * syscall number. Mask this off if present. See the values of
		 * __NR_N32_Linux, __NR_64_Linux, __NR_O32_Linux and __NR_Linux.
		 */
		id = id % 1000;
	}
	if (table && id >= 0 && id < table->num_to_name_len)
		return table->num_to_name[id];
	return NULL;
}

struct syscall_cmp_key {
	const char *name;
	const char *const *tbl;
};

static int syscallcmpname(const void *vkey, const void *ventry)
{
	const struct syscall_cmp_key *key = vkey;
	const uint16_t *entry = ventry;

	return strcmp(key->name, key->tbl[*entry]);
}

int syscalltbl__id(int e_machine, const char *name)
{
	const struct syscalltbl *table = find_table(e_machine);
	struct syscall_cmp_key key;
	const uint16_t *id;

	if (!table)
		return -1;

	key.name = name;
	key.tbl = table->num_to_name;
	id = bsearch(&key, table->sorted_names, table->sorted_names_len,
		     sizeof(table->sorted_names[0]), syscallcmpname);

	return id ? *id : -1;
}

int syscalltbl__num_idx(int e_machine)
{
	const struct syscalltbl *table = find_table(e_machine);

	if (!table)
		return 0;

	return table->sorted_names_len;
}

int syscalltbl__id_at_idx(int e_machine, int idx)
{
	const struct syscalltbl *table = find_table(e_machine);

	if (!table)
		return -1;

	assert(idx >= 0 && idx < table->sorted_names_len);
	return table->sorted_names[idx];
}

int syscalltbl__strglobmatch_next(int e_machine, const char *syscall_glob, int *idx)
{
	const struct syscalltbl *table = find_table(e_machine);

	for (int i = *idx + 1; table && i < table->sorted_names_len; ++i) {
		const char *name = table->num_to_name[table->sorted_names[i]];

		if (strglobmatch(name, syscall_glob)) {
			*idx = i;
			return table->sorted_names[i];
		}
	}

	return -1;
}

int syscalltbl__strglobmatch_first(int e_machine, const char *syscall_glob, int *idx)
{
	*idx = -1;
	return syscalltbl__strglobmatch_next(e_machine, syscall_glob, idx);
}
