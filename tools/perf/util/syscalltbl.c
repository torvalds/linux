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

#if __BITS_PER_LONG == 64
  #include <asm/syscalls_64.h>
#else
  #include <asm/syscalls_32.h>
#endif

const char *syscalltbl__name(int e_machine __maybe_unused, int id)
{
	if (id >= 0 && id <= (int)ARRAY_SIZE(syscall_num_to_name))
		return syscall_num_to_name[id];
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

int syscalltbl__id(int e_machine __maybe_unused, const char *name)
{
	struct syscall_cmp_key key = {
		.name = name,
		.tbl = syscall_num_to_name,
	};
	const int *id = bsearch(&key, syscall_sorted_names,
				ARRAY_SIZE(syscall_sorted_names),
				sizeof(syscall_sorted_names[0]),
				syscallcmpname);

	return id ? *id : -1;
}

int syscalltbl__num_idx(int e_machine __maybe_unused)
{
	return ARRAY_SIZE(syscall_sorted_names);
}

int syscalltbl__id_at_idx(int e_machine __maybe_unused, int idx)
{
	return syscall_sorted_names[idx];
}

int syscalltbl__strglobmatch_next(int e_machine __maybe_unused, const char *syscall_glob, int *idx)
{
	for (int i = *idx + 1; i < (int)ARRAY_SIZE(syscall_sorted_names); ++i) {
		const char *name = syscall_num_to_name[syscall_sorted_names[i]];

		if (strglobmatch(name, syscall_glob)) {
			*idx = i;
			return syscall_sorted_names[i];
		}
	}

	return -1;
}

int syscalltbl__strglobmatch_first(int e_machine, const char *syscall_glob, int *idx)
{
	*idx = -1;
	return syscalltbl__strglobmatch_next(e_machine, syscall_glob, idx);
}
