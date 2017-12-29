// SPDX-License-Identifier: GPL-2.0
#include <inttypes.h>
#include "perf.h"
#include "util/debug.h"
#include "util/symbol.h"
#include "util/sort.h"
#include "util/evsel.h"
#include "util/evlist.h"
#include "util/machine.h"
#include "util/thread.h"
#include "tests/hists_common.h"
#include <linux/kernel.h>

static struct {
	u32 pid;
	const char *comm;
} fake_threads[] = {
	{ FAKE_PID_PERF1, "perf" },
	{ FAKE_PID_PERF2, "perf" },
	{ FAKE_PID_BASH,  "bash" },
};

static struct {
	u32 pid;
	u64 start;
	const char *filename;
} fake_mmap_info[] = {
	{ FAKE_PID_PERF1, FAKE_MAP_PERF,   "perf" },
	{ FAKE_PID_PERF1, FAKE_MAP_LIBC,   "libc" },
	{ FAKE_PID_PERF1, FAKE_MAP_KERNEL, "[kernel]" },
	{ FAKE_PID_PERF2, FAKE_MAP_PERF,   "perf" },
	{ FAKE_PID_PERF2, FAKE_MAP_LIBC,   "libc" },
	{ FAKE_PID_PERF2, FAKE_MAP_KERNEL, "[kernel]" },
	{ FAKE_PID_BASH,  FAKE_MAP_BASH,   "bash" },
	{ FAKE_PID_BASH,  FAKE_MAP_LIBC,   "libc" },
	{ FAKE_PID_BASH,  FAKE_MAP_KERNEL, "[kernel]" },
};

struct fake_sym {
	u64 start;
	u64 length;
	const char *name;
};

static struct fake_sym perf_syms[] = {
	{ FAKE_SYM_OFFSET1, FAKE_SYM_LENGTH, "main" },
	{ FAKE_SYM_OFFSET2, FAKE_SYM_LENGTH, "run_command" },
	{ FAKE_SYM_OFFSET3, FAKE_SYM_LENGTH, "cmd_record" },
};

static struct fake_sym bash_syms[] = {
	{ FAKE_SYM_OFFSET1, FAKE_SYM_LENGTH, "main" },
	{ FAKE_SYM_OFFSET2, FAKE_SYM_LENGTH, "xmalloc" },
	{ FAKE_SYM_OFFSET3, FAKE_SYM_LENGTH, "xfree" },
};

static struct fake_sym libc_syms[] = {
	{ 700, 100, "malloc" },
	{ 800, 100, "free" },
	{ 900, 100, "realloc" },
	{ FAKE_SYM_OFFSET1, FAKE_SYM_LENGTH, "malloc" },
	{ FAKE_SYM_OFFSET2, FAKE_SYM_LENGTH, "free" },
	{ FAKE_SYM_OFFSET3, FAKE_SYM_LENGTH, "realloc" },
};

static struct fake_sym kernel_syms[] = {
	{ FAKE_SYM_OFFSET1, FAKE_SYM_LENGTH, "schedule" },
	{ FAKE_SYM_OFFSET2, FAKE_SYM_LENGTH, "page_fault" },
	{ FAKE_SYM_OFFSET3, FAKE_SYM_LENGTH, "sys_perf_event_open" },
};

static struct {
	const char *dso_name;
	struct fake_sym *syms;
	size_t nr_syms;
} fake_symbols[] = {
	{ "perf", perf_syms, ARRAY_SIZE(perf_syms) },
	{ "bash", bash_syms, ARRAY_SIZE(bash_syms) },
	{ "libc", libc_syms, ARRAY_SIZE(libc_syms) },
	{ "[kernel]", kernel_syms, ARRAY_SIZE(kernel_syms) },
};

struct machine *setup_fake_machine(struct machines *machines)
{
	struct machine *machine = machines__find(machines, HOST_KERNEL_ID);
	size_t i;

	if (machine == NULL) {
		pr_debug("Not enough memory for machine setup\n");
		return NULL;
	}

	for (i = 0; i < ARRAY_SIZE(fake_threads); i++) {
		struct thread *thread;

		thread = machine__findnew_thread(machine, fake_threads[i].pid,
						 fake_threads[i].pid);
		if (thread == NULL)
			goto out;

		thread__set_comm(thread, fake_threads[i].comm, 0);
		thread__put(thread);
	}

	for (i = 0; i < ARRAY_SIZE(fake_mmap_info); i++) {
		struct perf_sample sample = {
			.cpumode = PERF_RECORD_MISC_USER,
		};
		union perf_event fake_mmap_event = {
			.mmap = {
				.pid = fake_mmap_info[i].pid,
				.tid = fake_mmap_info[i].pid,
				.start = fake_mmap_info[i].start,
				.len = FAKE_MAP_LENGTH,
				.pgoff = 0ULL,
			},
		};

		strcpy(fake_mmap_event.mmap.filename,
		       fake_mmap_info[i].filename);

		machine__process_mmap_event(machine, &fake_mmap_event, &sample);
	}

	for (i = 0; i < ARRAY_SIZE(fake_symbols); i++) {
		size_t k;
		struct dso *dso;

		dso = machine__findnew_dso(machine, fake_symbols[i].dso_name);
		if (dso == NULL)
			goto out;

		/* emulate dso__load() */
		dso__set_loaded(dso, MAP__FUNCTION);

		for (k = 0; k < fake_symbols[i].nr_syms; k++) {
			struct symbol *sym;
			struct fake_sym *fsym = &fake_symbols[i].syms[k];

			sym = symbol__new(fsym->start, fsym->length,
					  STB_GLOBAL, fsym->name);
			if (sym == NULL) {
				dso__put(dso);
				goto out;
			}

			symbols__insert(&dso->symbols[MAP__FUNCTION], sym);
		}

		dso__put(dso);
	}

	return machine;

out:
	pr_debug("Not enough memory for machine setup\n");
	machine__delete_threads(machine);
	return NULL;
}

void print_hists_in(struct hists *hists)
{
	int i = 0;
	struct rb_root *root;
	struct rb_node *node;

	if (hists__has(hists, need_collapse))
		root = &hists->entries_collapsed;
	else
		root = hists->entries_in;

	pr_info("----- %s --------\n", __func__);
	node = rb_first(root);
	while (node) {
		struct hist_entry *he;

		he = rb_entry(node, struct hist_entry, rb_node_in);

		if (!he->filtered) {
			pr_info("%2d: entry: %-8s [%-8s] %20s: period = %"PRIu64"\n",
				i, thread__comm_str(he->thread),
				he->ms.map->dso->short_name,
				he->ms.sym->name, he->stat.period);
		}

		i++;
		node = rb_next(node);
	}
}

void print_hists_out(struct hists *hists)
{
	int i = 0;
	struct rb_root *root;
	struct rb_node *node;

	root = &hists->entries;

	pr_info("----- %s --------\n", __func__);
	node = rb_first(root);
	while (node) {
		struct hist_entry *he;

		he = rb_entry(node, struct hist_entry, rb_node);

		if (!he->filtered) {
			pr_info("%2d: entry: %8s:%5d [%-8s] %20s: period = %"PRIu64"/%"PRIu64"\n",
				i, thread__comm_str(he->thread), he->thread->tid,
				he->ms.map->dso->short_name,
				he->ms.sym->name, he->stat.period,
				he->stat_acc ? he->stat_acc->period : 0);
		}

		i++;
		node = rb_next(node);
	}
}
