// SPDX-License-Identifier: GPL-2.0
#include "debug.h"
#include "env.h"
#include "lock-contention.h"
#include "machine.h"
#include "symbol.h"

#include <limits.h>
#include <string.h>

#include <linux/hash.h>
#include <linux/zalloc.h>

#define __lockhashfn(key)	hash_long((unsigned long)key, LOCKHASH_BITS)
#define lockhashentry(key)	(lockhash_table + __lockhashfn((key)))

struct callstack_filter {
	struct list_head list;
	char name[];
};

static LIST_HEAD(callstack_filters);
struct hlist_head *lockhash_table;

int parse_call_stack(const struct option *opt __maybe_unused, const char *str,
		     int unset __maybe_unused)
{
	char *s, *tmp, *tok;
	int ret = 0;

	s = strdup(str);
	if (s == NULL)
		return -1;

	for (tok = strtok_r(s, ", ", &tmp); tok; tok = strtok_r(NULL, ", ", &tmp)) {
		struct callstack_filter *entry;

		entry = malloc(sizeof(*entry) + strlen(tok) + 1);
		if (entry == NULL) {
			pr_err("Memory allocation failure\n");
			free(s);
			return -1;
		}

		strcpy(entry->name, tok);
		list_add_tail(&entry->list, &callstack_filters);
	}

	free(s);
	return ret;
}

bool needs_callstack(void)
{
	return !list_empty(&callstack_filters);
}

struct lock_stat *lock_stat_find(u64 addr)
{
	struct hlist_head *entry = lockhashentry(addr);
	struct lock_stat *ret;

	hlist_for_each_entry(ret, entry, hash_entry) {
		if (ret->addr == addr)
			return ret;
	}
	return NULL;
}

struct lock_stat *lock_stat_findnew(u64 addr, const char *name, int flags)
{
	struct hlist_head *entry = lockhashentry(addr);
	struct lock_stat *ret, *new;

	hlist_for_each_entry(ret, entry, hash_entry) {
		if (ret->addr == addr)
			return ret;
	}

	new = zalloc(sizeof(struct lock_stat));
	if (!new)
		goto alloc_failed;

	new->addr = addr;
	new->name = strdup(name);
	if (!new->name) {
		free(new);
		goto alloc_failed;
	}

	new->flags = flags;
	new->wait_time_min = ULLONG_MAX;

	hlist_add_head(&new->hash_entry, entry);
	return new;

alloc_failed:
	pr_err("memory allocation failed\n");
	return NULL;
}

bool match_callstack_filter(struct machine *machine, u64 *callstack, int max_stack_depth)
{
	struct map *kmap;
	struct symbol *sym;
	u64 ip;
	const char *arch = perf_env__arch(machine->env);

	if (list_empty(&callstack_filters))
		return true;

	for (int i = 0; i < max_stack_depth; i++) {
		struct callstack_filter *filter;

		/*
		 * In powerpc, the callchain saved by kernel always includes
		 * first three entries as the NIP (next instruction pointer),
		 * LR (link register), and the contents of LR save area in the
		 * second stack frame. In certain scenarios its possible to have
		 * invalid kernel instruction addresses in either LR or the second
		 * stack frame's LR. In that case, kernel will store that address as
		 * zero.
		 *
		 * The below check will continue to look into callstack,
		 * incase first or second callstack index entry has 0
		 * address for powerpc.
		 */
		if (!callstack || (!callstack[i] && (strcmp(arch, "powerpc") ||
						(i != 1 && i != 2))))
			break;

		ip = callstack[i];
		sym = machine__find_kernel_symbol(machine, ip, &kmap);
		if (sym == NULL)
			continue;

		list_for_each_entry(filter, &callstack_filters, list) {
			if (strstr(sym->name, filter->name))
				return true;
		}
	}
	return false;
}
