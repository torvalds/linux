// SPDX-License-Identifier: GPL-2.0
#include "util/debug.h"
#include "util/evlist.h"
#include "util/machine.h"
#include "util/map.h"
#include "util/symbol.h"
#include "util/target.h"
#include "util/thread_map.h"
#include "util/lock-contention.h"
#include <linux/zalloc.h>
#include <linux/string.h>
#include <bpf/bpf.h>

#include "bpf_skel/lock_contention.skel.h"

static struct lock_contention_bpf *skel;

/* should be same as bpf_skel/lock_contention.bpf.c */
struct lock_contention_key {
	s32 stack_id;
};

struct lock_contention_data {
	u64 total_time;
	u64 min_time;
	u64 max_time;
	u32 count;
	u32 flags;
};

int lock_contention_prepare(struct lock_contention *con)
{
	int i, fd;
	int ncpus = 1, ntasks = 1;
	struct evlist *evlist = con->evlist;
	struct target *target = con->target;

	skel = lock_contention_bpf__open();
	if (!skel) {
		pr_err("Failed to open lock-contention BPF skeleton\n");
		return -1;
	}

	bpf_map__set_max_entries(skel->maps.stacks, con->map_nr_entries);
	bpf_map__set_max_entries(skel->maps.lock_stat, con->map_nr_entries);

	if (target__has_cpu(target))
		ncpus = perf_cpu_map__nr(evlist->core.user_requested_cpus);
	if (target__has_task(target))
		ntasks = perf_thread_map__nr(evlist->core.threads);

	bpf_map__set_max_entries(skel->maps.cpu_filter, ncpus);
	bpf_map__set_max_entries(skel->maps.task_filter, ntasks);

	if (lock_contention_bpf__load(skel) < 0) {
		pr_err("Failed to load lock-contention BPF skeleton\n");
		return -1;
	}

	if (target__has_cpu(target)) {
		u32 cpu;
		u8 val = 1;

		skel->bss->has_cpu = 1;
		fd = bpf_map__fd(skel->maps.cpu_filter);

		for (i = 0; i < ncpus; i++) {
			cpu = perf_cpu_map__cpu(evlist->core.user_requested_cpus, i).cpu;
			bpf_map_update_elem(fd, &cpu, &val, BPF_ANY);
		}
	}

	if (target__has_task(target)) {
		u32 pid;
		u8 val = 1;

		skel->bss->has_task = 1;
		fd = bpf_map__fd(skel->maps.task_filter);

		for (i = 0; i < ntasks; i++) {
			pid = perf_thread_map__pid(evlist->core.threads, i);
			bpf_map_update_elem(fd, &pid, &val, BPF_ANY);
		}
	}

	if (target__none(target) && evlist->workload.pid > 0) {
		u32 pid = evlist->workload.pid;
		u8 val = 1;

		skel->bss->has_task = 1;
		fd = bpf_map__fd(skel->maps.task_filter);
		bpf_map_update_elem(fd, &pid, &val, BPF_ANY);
	}

	lock_contention_bpf__attach(skel);
	return 0;
}

int lock_contention_start(void)
{
	skel->bss->enabled = 1;
	return 0;
}

int lock_contention_stop(void)
{
	skel->bss->enabled = 0;
	return 0;
}

int lock_contention_read(struct lock_contention *con)
{
	int fd, stack;
	s32 prev_key, key;
	struct lock_contention_data data;
	struct lock_stat *st;
	struct machine *machine = con->machine;
	u64 stack_trace[CONTENTION_STACK_DEPTH];

	fd = bpf_map__fd(skel->maps.lock_stat);
	stack = bpf_map__fd(skel->maps.stacks);

	con->lost = skel->bss->lost;

	prev_key = 0;
	while (!bpf_map_get_next_key(fd, &prev_key, &key)) {
		struct map *kmap;
		struct symbol *sym;
		int idx;

		bpf_map_lookup_elem(fd, &key, &data);
		st = zalloc(sizeof(*st));
		if (st == NULL)
			return -1;

		st->nr_contended = data.count;
		st->wait_time_total = data.total_time;
		st->wait_time_max = data.max_time;
		st->wait_time_min = data.min_time;

		if (data.count)
			st->avg_wait_time = data.total_time / data.count;

		st->flags = data.flags;

		bpf_map_lookup_elem(stack, &key, stack_trace);

		/* skip BPF + lock internal functions */
		idx = CONTENTION_STACK_SKIP;
		while (is_lock_function(machine, stack_trace[idx]) &&
		       idx < CONTENTION_STACK_DEPTH - 1)
			idx++;

		st->addr = stack_trace[idx];
		sym = machine__find_kernel_symbol(machine, st->addr, &kmap);

		if (sym) {
			unsigned long offset;
			int ret = 0;

			offset = kmap->map_ip(kmap, st->addr) - sym->start;

			if (offset)
				ret = asprintf(&st->name, "%s+%#lx", sym->name, offset);
			else
				st->name = strdup(sym->name);

			if (ret < 0 || st->name == NULL)
				return -1;
		} else if (asprintf(&st->name, "%#lx", (unsigned long)st->addr) < 0) {
			free(st);
			return -1;
		}

		if (verbose) {
			st->callstack = memdup(stack_trace, sizeof(stack_trace));
			if (st->callstack == NULL) {
				free(st);
				return -1;
			}
		}

		hlist_add_head(&st->hash_entry, con->result);
		prev_key = key;
	}

	return 0;
}

int lock_contention_finish(void)
{
	if (skel) {
		skel->bss->enabled = 0;
		lock_contention_bpf__destroy(skel);
	}

	return 0;
}
