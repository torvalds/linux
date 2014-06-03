#include "perf.h"
#include "util/debug.h"
#include "util/symbol.h"
#include "util/sort.h"
#include "util/evsel.h"
#include "util/evlist.h"
#include "util/machine.h"
#include "util/thread.h"
#include "tests/hists_common.h"

static struct {
	u32 pid;
	const char *comm;
} fake_threads[] = {
	{ 100, "perf" },
	{ 200, "perf" },
	{ 300, "bash" },
};

static struct {
	u32 pid;
	u64 start;
	const char *filename;
} fake_mmap_info[] = {
	{ 100, 0x40000, "perf" },
	{ 100, 0x50000, "libc" },
	{ 100, 0xf0000, "[kernel]" },
	{ 200, 0x40000, "perf" },
	{ 200, 0x50000, "libc" },
	{ 200, 0xf0000, "[kernel]" },
	{ 300, 0x40000, "bash" },
	{ 300, 0x50000, "libc" },
	{ 300, 0xf0000, "[kernel]" },
};

struct fake_sym {
	u64 start;
	u64 length;
	const char *name;
};

static struct fake_sym perf_syms[] = {
	{ 700, 100, "main" },
	{ 800, 100, "run_command" },
	{ 900, 100, "cmd_record" },
};

static struct fake_sym bash_syms[] = {
	{ 700, 100, "main" },
	{ 800, 100, "xmalloc" },
	{ 900, 100, "xfree" },
};

static struct fake_sym libc_syms[] = {
	{ 700, 100, "malloc" },
	{ 800, 100, "free" },
	{ 900, 100, "realloc" },
};

static struct fake_sym kernel_syms[] = {
	{ 700, 100, "schedule" },
	{ 800, 100, "page_fault" },
	{ 900, 100, "sys_perf_event_open" },
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
	}

	for (i = 0; i < ARRAY_SIZE(fake_mmap_info); i++) {
		union perf_event fake_mmap_event = {
			.mmap = {
				.header = { .misc = PERF_RECORD_MISC_USER, },
				.pid = fake_mmap_info[i].pid,
				.tid = fake_mmap_info[i].pid,
				.start = fake_mmap_info[i].start,
				.len = 0x1000ULL,
				.pgoff = 0ULL,
			},
		};

		strcpy(fake_mmap_event.mmap.filename,
		       fake_mmap_info[i].filename);

		machine__process_mmap_event(machine, &fake_mmap_event, NULL);
	}

	for (i = 0; i < ARRAY_SIZE(fake_symbols); i++) {
		size_t k;
		struct dso *dso;

		dso = __dsos__findnew(&machine->user_dsos,
				      fake_symbols[i].dso_name);
		if (dso == NULL)
			goto out;

		/* emulate dso__load() */
		dso__set_loaded(dso, MAP__FUNCTION);

		for (k = 0; k < fake_symbols[i].nr_syms; k++) {
			struct symbol *sym;
			struct fake_sym *fsym = &fake_symbols[i].syms[k];

			sym = symbol__new(fsym->start, fsym->length,
					  STB_GLOBAL, fsym->name);
			if (sym == NULL)
				goto out;

			symbols__insert(&dso->symbols[MAP__FUNCTION], sym);
		}
	}

	return machine;

out:
	pr_debug("Not enough memory for machine setup\n");
	machine__delete_threads(machine);
	machine__delete(machine);
	return NULL;
}

void print_hists_in(struct hists *hists)
{
	int i = 0;
	struct rb_root *root;
	struct rb_node *node;

	if (sort__need_collapse)
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
			pr_info("%2d: entry: %8s:%5d [%-8s] %20s: period = %"PRIu64"\n",
				i, thread__comm_str(he->thread), he->thread->tid,
				he->ms.map->dso->short_name,
				he->ms.sym->name, he->stat.period);
		}

		i++;
		node = rb_next(node);
	}
}
