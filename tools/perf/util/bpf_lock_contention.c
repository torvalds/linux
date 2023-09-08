// SPDX-License-Identifier: GPL-2.0
#include "util/debug.h"
#include "util/evlist.h"
#include "util/machine.h"
#include "util/map.h"
#include "util/symbol.h"
#include "util/target.h"
#include "util/thread.h"
#include "util/thread_map.h"
#include "util/lock-contention.h"
#include <linux/zalloc.h>
#include <linux/string.h>
#include <bpf/bpf.h>

#include "bpf_skel/lock_contention.skel.h"
#include "bpf_skel/lock_data.h"

static struct lock_contention_bpf *skel;

int lock_contention_prepare(struct lock_contention *con)
{
	int i, fd;
	int ncpus = 1, ntasks = 1, ntypes = 1, naddrs = 1;
	struct evlist *evlist = con->evlist;
	struct target *target = con->target;

	skel = lock_contention_bpf__open();
	if (!skel) {
		pr_err("Failed to open lock-contention BPF skeleton\n");
		return -1;
	}

	bpf_map__set_value_size(skel->maps.stacks, con->max_stack * sizeof(u64));
	bpf_map__set_max_entries(skel->maps.lock_stat, con->map_nr_entries);
	bpf_map__set_max_entries(skel->maps.tstamp, con->map_nr_entries);

	if (con->aggr_mode == LOCK_AGGR_TASK)
		bpf_map__set_max_entries(skel->maps.task_data, con->map_nr_entries);
	else
		bpf_map__set_max_entries(skel->maps.task_data, 1);

	if (con->save_callstack)
		bpf_map__set_max_entries(skel->maps.stacks, con->map_nr_entries);
	else
		bpf_map__set_max_entries(skel->maps.stacks, 1);

	if (target__has_cpu(target))
		ncpus = perf_cpu_map__nr(evlist->core.user_requested_cpus);
	if (target__has_task(target))
		ntasks = perf_thread_map__nr(evlist->core.threads);
	if (con->filters->nr_types)
		ntypes = con->filters->nr_types;

	/* resolve lock name filters to addr */
	if (con->filters->nr_syms) {
		struct symbol *sym;
		struct map *kmap;
		unsigned long *addrs;

		for (i = 0; i < con->filters->nr_syms; i++) {
			sym = machine__find_kernel_symbol_by_name(con->machine,
								  con->filters->syms[i],
								  &kmap);
			if (sym == NULL) {
				pr_warning("ignore unknown symbol: %s\n",
					   con->filters->syms[i]);
				continue;
			}

			addrs = realloc(con->filters->addrs,
					(con->filters->nr_addrs + 1) * sizeof(*addrs));
			if (addrs == NULL) {
				pr_warning("memory allocation failure\n");
				continue;
			}

			addrs[con->filters->nr_addrs++] = map__unmap_ip(kmap, sym->start);
			con->filters->addrs = addrs;
		}
		naddrs = con->filters->nr_addrs;
	}

	bpf_map__set_max_entries(skel->maps.cpu_filter, ncpus);
	bpf_map__set_max_entries(skel->maps.task_filter, ntasks);
	bpf_map__set_max_entries(skel->maps.type_filter, ntypes);
	bpf_map__set_max_entries(skel->maps.addr_filter, naddrs);

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

	if (con->filters->nr_types) {
		u8 val = 1;

		skel->bss->has_type = 1;
		fd = bpf_map__fd(skel->maps.type_filter);

		for (i = 0; i < con->filters->nr_types; i++)
			bpf_map_update_elem(fd, &con->filters->types[i], &val, BPF_ANY);
	}

	if (con->filters->nr_addrs) {
		u8 val = 1;

		skel->bss->has_addr = 1;
		fd = bpf_map__fd(skel->maps.addr_filter);

		for (i = 0; i < con->filters->nr_addrs; i++)
			bpf_map_update_elem(fd, &con->filters->addrs[i], &val, BPF_ANY);
	}

	/* these don't work well if in the rodata section */
	skel->bss->stack_skip = con->stack_skip;
	skel->bss->aggr_mode = con->aggr_mode;
	skel->bss->needs_callstack = con->save_callstack;
	skel->bss->lock_owner = con->owner;

	bpf_program__set_autoload(skel->progs.collect_lock_syms, false);

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

static const char *lock_contention_get_name(struct lock_contention *con,
					    struct contention_key *key,
					    u64 *stack_trace, u32 flags)
{
	int idx = 0;
	u64 addr;
	const char *name = "";
	static char name_buf[KSYM_NAME_LEN];
	struct symbol *sym;
	struct map *kmap;
	struct machine *machine = con->machine;

	if (con->aggr_mode == LOCK_AGGR_TASK) {
		struct contention_task_data task;
		int pid = key->pid;
		int task_fd = bpf_map__fd(skel->maps.task_data);

		/* do not update idle comm which contains CPU number */
		if (pid) {
			struct thread *t = __machine__findnew_thread(machine, /*pid=*/-1, pid);

			if (t == NULL)
				return name;
			if (!bpf_map_lookup_elem(task_fd, &pid, &task) &&
			    thread__set_comm(t, task.comm, /*timestamp=*/0))
				name = task.comm;
		}
		return name;
	}

	if (con->aggr_mode == LOCK_AGGR_ADDR) {
		int lock_fd = bpf_map__fd(skel->maps.lock_syms);

		/* per-process locks set upper bits of the flags */
		if (flags & LCD_F_MMAP_LOCK)
			return "mmap_lock";
		if (flags & LCD_F_SIGHAND_LOCK)
			return "siglock";

		/* global locks with symbols */
		sym = machine__find_kernel_symbol(machine, key->lock_addr, &kmap);
		if (sym)
			return sym->name;

		/* try semi-global locks collected separately */
		if (!bpf_map_lookup_elem(lock_fd, &key->lock_addr, &flags)) {
			if (flags == LOCK_CLASS_RQLOCK)
				return "rq_lock";
		}

		return "";
	}

	/* LOCK_AGGR_CALLER: skip lock internal functions */
	while (machine__is_lock_function(machine, stack_trace[idx]) &&
	       idx < con->max_stack - 1)
		idx++;

	addr = stack_trace[idx];
	sym = machine__find_kernel_symbol(machine, addr, &kmap);

	if (sym) {
		unsigned long offset;

		offset = map__map_ip(kmap, addr) - sym->start;

		if (offset == 0)
			return sym->name;

		snprintf(name_buf, sizeof(name_buf), "%s+%#lx", sym->name, offset);
	} else {
		snprintf(name_buf, sizeof(name_buf), "%#lx", (unsigned long)addr);
	}

	return name_buf;
}

int lock_contention_read(struct lock_contention *con)
{
	int fd, stack, err = 0;
	struct contention_key *prev_key, key = {};
	struct contention_data data = {};
	struct lock_stat *st = NULL;
	struct machine *machine = con->machine;
	u64 *stack_trace;
	size_t stack_size = con->max_stack * sizeof(*stack_trace);

	fd = bpf_map__fd(skel->maps.lock_stat);
	stack = bpf_map__fd(skel->maps.stacks);

	con->fails.task = skel->bss->task_fail;
	con->fails.stack = skel->bss->stack_fail;
	con->fails.time = skel->bss->time_fail;
	con->fails.data = skel->bss->data_fail;

	stack_trace = zalloc(stack_size);
	if (stack_trace == NULL)
		return -1;

	if (con->aggr_mode == LOCK_AGGR_TASK) {
		struct thread *idle = __machine__findnew_thread(machine,
								/*pid=*/0,
								/*tid=*/0);
		thread__set_comm(idle, "swapper", /*timestamp=*/0);
	}

	if (con->aggr_mode == LOCK_AGGR_ADDR) {
		DECLARE_LIBBPF_OPTS(bpf_test_run_opts, opts,
			.flags = BPF_F_TEST_RUN_ON_CPU,
		);
		int prog_fd = bpf_program__fd(skel->progs.collect_lock_syms);

		bpf_prog_test_run_opts(prog_fd, &opts);
	}

	/* make sure it loads the kernel map */
	map__load(maps__first(machine->kmaps)->map);

	prev_key = NULL;
	while (!bpf_map_get_next_key(fd, prev_key, &key)) {
		s64 ls_key;
		const char *name;

		/* to handle errors in the loop body */
		err = -1;

		bpf_map_lookup_elem(fd, &key, &data);
		if (con->save_callstack) {
			bpf_map_lookup_elem(stack, &key.stack_id, stack_trace);

			if (!match_callstack_filter(machine, stack_trace)) {
				con->nr_filtered += data.count;
				goto next;
			}
		}

		switch (con->aggr_mode) {
		case LOCK_AGGR_CALLER:
			ls_key = key.stack_id;
			break;
		case LOCK_AGGR_TASK:
			ls_key = key.pid;
			break;
		case LOCK_AGGR_ADDR:
			ls_key = key.lock_addr;
			break;
		default:
			goto next;
		}

		st = lock_stat_find(ls_key);
		if (st != NULL) {
			st->wait_time_total += data.total_time;
			if (st->wait_time_max < data.max_time)
				st->wait_time_max = data.max_time;
			if (st->wait_time_min > data.min_time)
				st->wait_time_min = data.min_time;

			st->nr_contended += data.count;
			if (st->nr_contended)
				st->avg_wait_time = st->wait_time_total / st->nr_contended;
			goto next;
		}

		name = lock_contention_get_name(con, &key, stack_trace, data.flags);
		st = lock_stat_findnew(ls_key, name, data.flags);
		if (st == NULL)
			break;

		st->nr_contended = data.count;
		st->wait_time_total = data.total_time;
		st->wait_time_max = data.max_time;
		st->wait_time_min = data.min_time;

		if (data.count)
			st->avg_wait_time = data.total_time / data.count;

		if (con->aggr_mode == LOCK_AGGR_CALLER && verbose > 0) {
			st->callstack = memdup(stack_trace, stack_size);
			if (st->callstack == NULL)
				break;
		}

next:
		prev_key = &key;

		/* we're fine now, reset the error */
		err = 0;
	}

	free(stack_trace);

	return err;
}

int lock_contention_finish(void)
{
	if (skel) {
		skel->bss->enabled = 0;
		lock_contention_bpf__destroy(skel);
	}

	return 0;
}
