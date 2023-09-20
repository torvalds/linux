// SPDX-License-Identifier: GPL-2.0
#include "tests.h"
#include "debug.h"
#include "symbol.h"
#include "sort.h"
#include "evsel.h"
#include "evlist.h"
#include "machine.h"
#include "map.h"
#include "parse-events.h"
#include "thread.h"
#include "hists_common.h"
#include "util/mmap.h"
#include <errno.h>
#include <linux/kernel.h>

struct sample {
	u32 pid;
	u64 ip;
	struct thread *thread;
	struct map *map;
	struct symbol *sym;
};

/* For the numbers, see hists_common.c */
static struct sample fake_common_samples[] = {
	/* perf [kernel] schedule() */
	{ .pid = FAKE_PID_PERF1, .ip = FAKE_IP_KERNEL_SCHEDULE, },
	/* perf [perf]   main() */
	{ .pid = FAKE_PID_PERF2, .ip = FAKE_IP_PERF_MAIN, },
	/* perf [perf]   cmd_record() */
	{ .pid = FAKE_PID_PERF2, .ip = FAKE_IP_PERF_CMD_RECORD, },
	/* bash [bash]   xmalloc() */
	{ .pid = FAKE_PID_BASH,  .ip = FAKE_IP_BASH_XMALLOC, },
	/* bash [libc]   malloc() */
	{ .pid = FAKE_PID_BASH,  .ip = FAKE_IP_LIBC_MALLOC, },
};

static struct sample fake_samples[][5] = {
	{
		/* perf [perf]   run_command() */
		{ .pid = FAKE_PID_PERF1, .ip = FAKE_IP_PERF_RUN_COMMAND, },
		/* perf [libc]   malloc() */
		{ .pid = FAKE_PID_PERF1, .ip = FAKE_IP_LIBC_MALLOC, },
		/* perf [kernel] page_fault() */
		{ .pid = FAKE_PID_PERF1, .ip = FAKE_IP_KERNEL_PAGE_FAULT, },
		/* perf [kernel] sys_perf_event_open() */
		{ .pid = FAKE_PID_PERF2, .ip = FAKE_IP_KERNEL_SYS_PERF_EVENT_OPEN, },
		/* bash [libc]   free() */
		{ .pid = FAKE_PID_BASH,  .ip = FAKE_IP_LIBC_FREE, },
	},
	{
		/* perf [libc]   free() */
		{ .pid = FAKE_PID_PERF2, .ip = FAKE_IP_LIBC_FREE, },
		/* bash [libc]   malloc() */
		{ .pid = FAKE_PID_BASH,  .ip = FAKE_IP_LIBC_MALLOC, }, /* will be merged */
		/* bash [bash]   xfee() */
		{ .pid = FAKE_PID_BASH,  .ip = FAKE_IP_BASH_XFREE, },
		/* bash [libc]   realloc() */
		{ .pid = FAKE_PID_BASH,  .ip = FAKE_IP_LIBC_REALLOC, },
		/* bash [kernel] page_fault() */
		{ .pid = FAKE_PID_BASH,  .ip = FAKE_IP_KERNEL_PAGE_FAULT, },
	},
};

static int add_hist_entries(struct evlist *evlist, struct machine *machine)
{
	struct evsel *evsel;
	struct addr_location al;
	struct hist_entry *he;
	struct perf_sample sample = { .period = 1, .weight = 1, };
	size_t i = 0, k;

	addr_location__init(&al);
	/*
	 * each evsel will have 10 samples - 5 common and 5 distinct.
	 * However the second evsel also has a collapsed entry for
	 * "bash [libc] malloc" so total 9 entries will be in the tree.
	 */
	evlist__for_each_entry(evlist, evsel) {
		struct hists *hists = evsel__hists(evsel);

		for (k = 0; k < ARRAY_SIZE(fake_common_samples); k++) {
			sample.cpumode = PERF_RECORD_MISC_USER;
			sample.pid = fake_common_samples[k].pid;
			sample.tid = fake_common_samples[k].pid;
			sample.ip = fake_common_samples[k].ip;

			if (machine__resolve(machine, &al, &sample) < 0)
				goto out;

			he = hists__add_entry(hists, &al, NULL,
					      NULL, NULL, NULL, &sample, true);
			if (he == NULL) {
				goto out;
			}

			thread__put(fake_common_samples[k].thread);
			fake_common_samples[k].thread = thread__get(al.thread);
			map__put(fake_common_samples[k].map);
			fake_common_samples[k].map = map__get(al.map);
			fake_common_samples[k].sym = al.sym;
		}

		for (k = 0; k < ARRAY_SIZE(fake_samples[i]); k++) {
			sample.pid = fake_samples[i][k].pid;
			sample.tid = fake_samples[i][k].pid;
			sample.ip = fake_samples[i][k].ip;
			if (machine__resolve(machine, &al, &sample) < 0)
				goto out;

			he = hists__add_entry(hists, &al, NULL,
					      NULL, NULL, NULL, &sample, true);
			if (he == NULL) {
				goto out;
			}

			thread__put(fake_samples[i][k].thread);
			fake_samples[i][k].thread = thread__get(al.thread);
			map__put(fake_samples[i][k].map);
			fake_samples[i][k].map = map__get(al.map);
			fake_samples[i][k].sym = al.sym;
		}
		i++;
	}

	addr_location__exit(&al);
	return 0;
out:
	addr_location__exit(&al);
	pr_debug("Not enough memory for adding a hist entry\n");
	return -1;
}

static void put_fake_samples(void)
{
	size_t i, j;

	for (i = 0; i < ARRAY_SIZE(fake_common_samples); i++)
		map__put(fake_common_samples[i].map);
	for (i = 0; i < ARRAY_SIZE(fake_samples); i++) {
		for (j = 0; j < ARRAY_SIZE(fake_samples[0]); j++)
			map__put(fake_samples[i][j].map);
	}
}

static int find_sample(struct sample *samples, size_t nr_samples,
		       struct thread *t, struct map *m, struct symbol *s)
{
	while (nr_samples--) {
		if (RC_CHK_ACCESS(samples->thread) == RC_CHK_ACCESS(t) &&
		    RC_CHK_ACCESS(samples->map) == RC_CHK_ACCESS(m) &&
		    samples->sym == s)
			return 1;
		samples++;
	}
	return 0;
}

static int __validate_match(struct hists *hists)
{
	size_t count = 0;
	struct rb_root_cached *root;
	struct rb_node *node;

	/*
	 * Only entries from fake_common_samples should have a pair.
	 */
	if (hists__has(hists, need_collapse))
		root = &hists->entries_collapsed;
	else
		root = hists->entries_in;

	node = rb_first_cached(root);
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
	struct rb_root_cached *root;
	struct rb_node *node;

	/*
	 * Leader hists (idx = 0) will have dummy entries from other,
	 * and some entries will have no pair.  However every entry
	 * in other hists should have (dummy) pair.
	 */
	if (hists__has(hists, need_collapse))
		root = &hists->entries_collapsed;
	else
		root = hists->entries_in;

	node = rb_first_cached(root);
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

static int test__hists_link(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	int err = -1;
	struct hists *hists, *first_hists;
	struct machines machines;
	struct machine *machine = NULL;
	struct evsel *evsel, *first;
	struct evlist *evlist = evlist__new();

	if (evlist == NULL)
                return -ENOMEM;

	err = parse_event(evlist, "cpu-clock");
	if (err)
		goto out;
	err = parse_event(evlist, "task-clock");
	if (err)
		goto out;

	err = TEST_FAIL;
	/* default sort order (comm,dso,sym) will be used */
	if (setup_sorting(NULL) < 0)
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

	evlist__for_each_entry(evlist, evsel) {
		hists = evsel__hists(evsel);
		hists__collapse_resort(hists, NULL);

		if (verbose > 2)
			print_hists_in(hists);
	}

	first = evlist__first(evlist);
	evsel = evlist__last(evlist);

	first_hists = evsel__hists(first);
	hists = evsel__hists(evsel);

	/* match common entries */
	hists__match(first_hists, hists);
	err = validate_match(first_hists, hists);
	if (err)
		goto out;

	/* link common and/or dummy entries */
	hists__link(first_hists, hists);
	err = validate_link(first_hists, hists);
	if (err)
		goto out;

	err = 0;

out:
	/* tear down everything */
	evlist__delete(evlist);
	reset_output_field();
	machines__exit(&machines);
	put_fake_samples();

	return err;
}

DEFINE_SUITE("Match and link multiple hists", hists_link);
