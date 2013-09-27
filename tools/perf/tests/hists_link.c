#include "perf.h"
#include "tests.h"
#include "debug.h"
#include "symbol.h"
#include "sort.h"
#include "evsel.h"
#include "evlist.h"
#include "machine.h"
#include "thread.h"
#include "parse-events.h"

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

static struct machine *setup_fake_machine(struct machines *machines)
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

		thread__set_comm(thread, fake_threads[i].comm);
	}

	for (i = 0; i < ARRAY_SIZE(fake_mmap_info); i++) {
		union perf_event fake_mmap_event = {
			.mmap = {
				.header = { .misc = PERF_RECORD_MISC_USER, },
				.pid = fake_mmap_info[i].pid,
				.start = fake_mmap_info[i].start,
				.len = 0x1000ULL,
				.pgoff = 0ULL,
			},
		};

		strcpy(fake_mmap_event.mmap.filename,
		       fake_mmap_info[i].filename);

		machine__process_mmap_event(machine, &fake_mmap_event);
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

struct sample {
	u32 pid;
	u64 ip;
	struct thread *thread;
	struct map *map;
	struct symbol *sym;
};

static struct sample fake_common_samples[] = {
	/* perf [kernel] schedule() */
	{ .pid = 100, .ip = 0xf0000 + 700, },
	/* perf [perf]   main() */
	{ .pid = 200, .ip = 0x40000 + 700, },
	/* perf [perf]   cmd_record() */
	{ .pid = 200, .ip = 0x40000 + 900, },
	/* bash [bash]   xmalloc() */
	{ .pid = 300, .ip = 0x40000 + 800, },
	/* bash [libc]   malloc() */
	{ .pid = 300, .ip = 0x50000 + 700, },
};

static struct sample fake_samples[][5] = {
	{
		/* perf [perf]   run_command() */
		{ .pid = 100, .ip = 0x40000 + 800, },
		/* perf [libc]   malloc() */
		{ .pid = 100, .ip = 0x50000 + 700, },
		/* perf [kernel] page_fault() */
		{ .pid = 100, .ip = 0xf0000 + 800, },
		/* perf [kernel] sys_perf_event_open() */
		{ .pid = 200, .ip = 0xf0000 + 900, },
		/* bash [libc]   free() */
		{ .pid = 300, .ip = 0x50000 + 800, },
	},
	{
		/* perf [libc]   free() */
		{ .pid = 200, .ip = 0x50000 + 800, },
		/* bash [libc]   malloc() */
		{ .pid = 300, .ip = 0x50000 + 700, }, /* will be merged */
		/* bash [bash]   xfee() */
		{ .pid = 300, .ip = 0x40000 + 900, },
		/* bash [libc]   realloc() */
		{ .pid = 300, .ip = 0x50000 + 900, },
		/* bash [kernel] page_fault() */
		{ .pid = 300, .ip = 0xf0000 + 800, },
	},
};

static int add_hist_entries(struct perf_evlist *evlist, struct machine *machine)
{
	struct perf_evsel *evsel;
	struct addr_location al;
	struct hist_entry *he;
	struct perf_sample sample = { .cpu = 0, };
	size_t i = 0, k;

	/*
	 * each evsel will have 10 samples - 5 common and 5 distinct.
	 * However the second evsel also has a collapsed entry for
	 * "bash [libc] malloc" so total 9 entries will be in the tree.
	 */
	list_for_each_entry(evsel, &evlist->entries, node) {
		for (k = 0; k < ARRAY_SIZE(fake_common_samples); k++) {
			const union perf_event event = {
				.header = {
					.misc = PERF_RECORD_MISC_USER,
				},
			};

			sample.pid = fake_common_samples[k].pid;
			sample.ip = fake_common_samples[k].ip;
			if (perf_event__preprocess_sample(&event, machine, &al,
							  &sample) < 0)
				goto out;

			he = __hists__add_entry(&evsel->hists, &al, NULL, 1, 1);
			if (he == NULL)
				goto out;

			fake_common_samples[k].thread = al.thread;
			fake_common_samples[k].map = al.map;
			fake_common_samples[k].sym = al.sym;
		}

		for (k = 0; k < ARRAY_SIZE(fake_samples[i]); k++) {
			const union perf_event event = {
				.header = {
					.misc = PERF_RECORD_MISC_USER,
				},
			};

			sample.pid = fake_samples[i][k].pid;
			sample.ip = fake_samples[i][k].ip;
			if (perf_event__preprocess_sample(&event, machine, &al,
							  &sample) < 0)
				goto out;

			he = __hists__add_entry(&evsel->hists, &al, NULL, 1, 1);
			if (he == NULL)
				goto out;

			fake_samples[i][k].thread = al.thread;
			fake_samples[i][k].map = al.map;
			fake_samples[i][k].sym = al.sym;
		}
		i++;
	}

	return 0;

out:
	pr_debug("Not enough memory for adding a hist entry\n");
	return -1;
}

static int find_sample(struct sample *samples, size_t nr_samples,
		       struct thread *t, struct map *m, struct symbol *s)
{
	while (nr_samples--) {
		if (samples->thread == t && samples->map == m &&
		    samples->sym == s)
			return 1;
		samples++;
	}
	return 0;
}

static int __validate_match(struct hists *hists)
{
	size_t count = 0;
	struct rb_root *root;
	struct rb_node *node;

	/*
	 * Only entries from fake_common_samples should have a pair.
	 */
	if (sort__need_collapse)
		root = &hists->entries_collapsed;
	else
		root = hists->entries_in;

	node = rb_first(root);
	while (node) {
		struct hist_entry *he;

		he = rb_entry(node, struct hist_entry, rb_node_in);

		if (hist_entry__has_pairs(he)) {
			if (find_sample(fake_common_samples,
					ARRAY_SIZE(fake_common_samples),
					he->thread, he->ms.map, he->ms.sym)) {
				count++;
			} else {
				pr_debug("Can't find the matched entry\n");
				return -1;
			}
		}

		node = rb_next(node);
	}

	if (count != ARRAY_SIZE(fake_common_samples)) {
		pr_debug("Invalid count for matched entries: %zd of %zd\n",
			 count, ARRAY_SIZE(fake_common_samples));
		return -1;
	}

	return 0;
}

static int validate_match(struct hists *leader, struct hists *other)
{
	return __validate_match(leader) || __validate_match(other);
}

static int __validate_link(struct hists *hists, int idx)
{
	size_t count = 0;
	size_t count_pair = 0;
	size_t count_dummy = 0;
	struct rb_root *root;
	struct rb_node *node;

	/*
	 * Leader hists (idx = 0) will have dummy entries from other,
	 * and some entries will have no pair.  However every entry
	 * in other hists should have (dummy) pair.
	 */
	if (sort__need_collapse)
		root = &hists->entries_collapsed;
	else
		root = hists->entries_in;

	node = rb_first(root);
	while (node) {
		struct hist_entry *he;

		he = rb_entry(node, struct hist_entry, rb_node_in);

		if (hist_entry__has_pairs(he)) {
			if (!find_sample(fake_common_samples,
					 ARRAY_SIZE(fake_common_samples),
					 he->thread, he->ms.map, he->ms.sym) &&
			    !find_sample(fake_samples[idx],
					 ARRAY_SIZE(fake_samples[idx]),
					 he->thread, he->ms.map, he->ms.sym)) {
				count_dummy++;
			}
			count_pair++;
		} else if (idx) {
			pr_debug("A entry from the other hists should have pair\n");
			return -1;
		}

		count++;
		node = rb_next(node);
	}

	/*
	 * Note that we have a entry collapsed in the other (idx = 1) hists.
	 */
	if (idx == 0) {
		if (count_dummy != ARRAY_SIZE(fake_samples[1]) - 1) {
			pr_debug("Invalid count of dummy entries: %zd of %zd\n",
				 count_dummy, ARRAY_SIZE(fake_samples[1]) - 1);
			return -1;
		}
		if (count != count_pair + ARRAY_SIZE(fake_samples[0])) {
			pr_debug("Invalid count of total leader entries: %zd of %zd\n",
				 count, count_pair + ARRAY_SIZE(fake_samples[0]));
			return -1;
		}
	} else {
		if (count != count_pair) {
			pr_debug("Invalid count of total other entries: %zd of %zd\n",
				 count, count_pair);
			return -1;
		}
		if (count_dummy > 0) {
			pr_debug("Other hists should not have dummy entries: %zd\n",
				 count_dummy);
			return -1;
		}
	}

	return 0;
}

static int validate_link(struct hists *leader, struct hists *other)
{
	return __validate_link(leader, 0) || __validate_link(other, 1);
}

static void print_hists(struct hists *hists)
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

		pr_info("%2d: entry: %-8s [%-8s] %20s: period = %"PRIu64"\n",
			i, he->thread->comm, he->ms.map->dso->short_name,
			he->ms.sym->name, he->stat.period);

		i++;
		node = rb_next(node);
	}
}

int test__hists_link(void)
{
	int err = -1;
	struct machines machines;
	struct machine *machine = NULL;
	struct perf_evsel *evsel, *first;
	struct perf_evlist *evlist = perf_evlist__new();

	if (evlist == NULL)
                return -ENOMEM;

	err = parse_events(evlist, "cpu-clock");
	if (err)
		goto out;
	err = parse_events(evlist, "task-clock");
	if (err)
		goto out;

	/* default sort order (comm,dso,sym) will be used */
	if (setup_sorting() < 0)
		goto out;

	machines__init(&machines);

	/* setup threads/dso/map/symbols also */
	machine = setup_fake_machine(&machines);
	if (!machine)
		goto out;

	if (verbose > 1)
		machine__fprintf(machine, stderr);

	/* process sample events */
	err = add_hist_entries(evlist, machine);
	if (err < 0)
		goto out;

	list_for_each_entry(evsel, &evlist->entries, node) {
		hists__collapse_resort(&evsel->hists);

		if (verbose > 2)
			print_hists(&evsel->hists);
	}

	first = perf_evlist__first(evlist);
	evsel = perf_evlist__last(evlist);

	/* match common entries */
	hists__match(&first->hists, &evsel->hists);
	err = validate_match(&first->hists, &evsel->hists);
	if (err)
		goto out;

	/* link common and/or dummy entries */
	hists__link(&first->hists, &evsel->hists);
	err = validate_link(&first->hists, &evsel->hists);
	if (err)
		goto out;

	err = 0;

out:
	/* tear down everything */
	perf_evlist__delete(evlist);
	machines__exit(&machines);

	return err;
}
