// SPDX-License-Identifier: GPL-2.0-only
/*
 * System call table mapper
 *
 * (C) 2016 Arnaldo Carvalho de Melo <acme@redhat.com>
 */

#include "syscalltbl.h"
#include <stdlib.h>
#include <linux/compiler.h>
#include <linux/zalloc.h>

#include <string.h>
#include "string2.h"

#include <syscall_table.h>
const int syscalltbl_native_max_id = SYSCALLTBL_MAX_ID;
static const char *const *syscalltbl_native = syscalltbl;

struct syscall {
	int id;
	const char *name;
};

static int syscallcmpname(const void *vkey, const void *ventry)
{
	const char *key = vkey;
	const struct syscall *entry = ventry;

	return strcmp(key, entry->name);
}

static int syscallcmp(const void *va, const void *vb)
{
	const struct syscall *a = va, *b = vb;

	return strcmp(a->name, b->name);
}

static int syscalltbl__init_native(struct syscalltbl *tbl)
{
	int nr_entries = 0, i, j;
	struct syscall *entries;

	for (i = 0; i <= syscalltbl_native_max_id; ++i)
		if (syscalltbl_native[i])
			++nr_entries;

	entries = tbl->syscalls.entries = malloc(sizeof(struct syscall) * nr_entries);
	if (tbl->syscalls.entries == NULL)
		return -1;

	for (i = 0, j = 0; i <= syscalltbl_native_max_id; ++i) {
		if (syscalltbl_native[i]) {
			entries[j].name = syscalltbl_native[i];
			entries[j].id = i;
			++j;
		}
	}

	qsort(tbl->syscalls.entries, nr_entries, sizeof(struct syscall), syscallcmp);
	tbl->syscalls.nr_entries = nr_entries;
	tbl->syscalls.max_id	 = syscalltbl_native_max_id;
	return 0;
}

struct syscalltbl *syscalltbl__new(void)
{
	struct syscalltbl *tbl = malloc(sizeof(*tbl));
	if (tbl) {
		if (syscalltbl__init_native(tbl)) {
			free(tbl);
			return NULL;
		}
	}
	return tbl;
}

void syscalltbl__delete(struct syscalltbl *tbl)
{
	zfree(&tbl->syscalls.entries);
	free(tbl);
}

const char *syscalltbl__name(const struct syscalltbl *tbl __maybe_unused, int id)
{
	return id <= syscalltbl_native_max_id ? syscalltbl_native[id]: NULL;
}

int syscalltbl__id(struct syscalltbl *tbl, const char *name)
{
	struct syscall *sc = bsearch(name, tbl->syscalls.entries,
				     tbl->syscalls.nr_entries, sizeof(*sc),
				     syscallcmpname);

	return sc ? sc->id : -1;
}

int syscalltbl__id_at_idx(struct syscalltbl *tbl, int idx)
{
	struct syscall *syscalls = tbl->syscalls.entries;

	return idx < tbl->syscalls.nr_entries ? syscalls[idx].id : -1;
}

int syscalltbl__strglobmatch_next(struct syscalltbl *tbl, const char *syscall_glob, int *idx)
{
	int i;
	struct syscall *syscalls = tbl->syscalls.entries;

	for (i = *idx + 1; i < tbl->syscalls.nr_entries; ++i) {
		if (strglobmatch(syscalls[i].name, syscall_glob)) {
			*idx = i;
			return syscalls[i].id;
		}
	}

	return -1;
}

int syscalltbl__strglobmatch_first(struct syscalltbl *tbl, const char *syscall_glob, int *idx)
{
	*idx = -1;
	return syscalltbl__strglobmatch_next(tbl, syscall_glob, idx);
}
