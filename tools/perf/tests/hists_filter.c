// SPDX-License-Identifier: GPL-2.0
#include "util/debug.h"
#include "util/map.h"
#include "util/symbol.h"
#include "util/sort.h"
#include "util/evsel.h"
#include "util/event.h"
#include "util/evlist.h"
#include "util/machine.h"
#include "util/parse-events.h"
#include "tests/tests.h"
#include "tests/hists_common.h"
#include <linux/kernel.h>

struct sample {
	u32 pid;
	u64 ip;
	struct thread *thread;
	struct map *map;
	struct symbol *sym;
	int socket;
};

/* For the numbers, see hists_common.c */
static struct sample fake_samples[] = {
	/* perf [kernel] schedule() */
	{ .pid = FAKE_PID_PERF1, .ip = FAKE_IP_KERNEL_SCHEDULE, .socket = 0 },
	/* perf [perf]   main() */
	{ .pid = FAKE_PID_PERF1, .ip = FAKE_IP_PERF_MAIN, .socket = 0 },
	/* perf [libc]   malloc() */
	{ .pid = FAKE_PID_PERF1, .ip = FAKE_IP_LIBC_MALLOC, .socket = 0 },
	/* perf [perf]   main() */
	{ .pid = FAKE_PID_PERF2, .ip = FAKE_IP_PERF_MAIN, .socket = 0 }, /* will be merged */
	/* perf [perf]   cmd_record() */
	{ .pid = FAKE_PID_PERF2, .ip = FAKE_IP_PERF_CMD_RECORD, .socket = 1 },
	/* perf [kernel] page_fault() */
	{ .pid = FAKE_PID_PERF2, .ip = FAKE_IP_KERNEL_PAGE_FAULT, .socket = 1 },
	/* bash [bash]   main() */
	{ .pid = FAKE_PID_BASH,  .ip = FAKE_IP_BASH_MAIN, .socket = 2 },
	/* bash [bash]   xmalloc() */
	{ .pid = FAKE_PID_BASH,  .ip = FAKE_IP_BASH_XMALLOC, .socket = 2 },
	/* bash [libc]   malloc() */
	{ .pid = FAKE_PID_BASH,  .ip = FAKE_IP_LIBC_MALLOC, .socket = 3 },
	/* bash [kernel] page_fault() */
	{ .pid = FAKE_PID_BASH,  .ip = FAKE_IP_KERNEL_PAGE_FAULT, .socket = 3 },
};

static int add_hist_entries(struct evlist *evlist,
			    struct machine *machine)
{
	struct evsel *evsel;
	struct addr_location al;
	struct perf_sample sample = { .period = 100, };
	size_t i;

	/*
	 * each evsel will have 10 samples but the 4th sample
	 * (perf [perf] main) will be collapsed to an existing entry
	 * so total 9 entries will be in the tree.
	 */
	evlist__for_each_entry(evlist, evsel) {
		for (i = 0; i < ARRAY_SIZE(fake_samples); i++) {
			struct hist_entry_iter iter = {
				.evsel = evsel,
				.sample = &sample,
				.ops = &hist_iter_normal,
				.hide_unresolved = false,
			};
			struct hists *hists = evsel__hists(evsel);

			/* make sure it has no filter at first */
			hists->thread_filter = NULL;
			hists->dso_filter = NULL;
			hists->symbol_filter_str = NULL;

			sample.cpumode = PERF_RECORD_MISC_USER;
			sample.pid = fake_samples[i].pid;
			sample.tid = fake_samples[i].pid;
			sample.ip = fake_samples[i].ip;

			if (machine__resolve(machine, &al, &sample) < 0)
				goto out;

			al.socket = fake_samples[i].socket;
			if (hist_entry_iter__add(&iter, &al,
						 sysctl_perf_event_max_stack, NULL) < 0) {
				addr_location__put(&al);
				goto out;
			}

			fake_samples[i].thread = al.thread;
			fake_samples[i].map = al.map;
			fake_samples[i].sym = al.sym;
		}
	}

	return 0;

out:
	pr_debug("Not enough memory for adding a hist entry\n");
	return TEST_FAIL;
}

static int test__hists_filter(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	int err = TEST_FAIL;
	struct machines machines;
	struct machine *machine;
	struct evsel *evsel;
	struct evlist *evlist = evlist__new();

	TEST_ASSERT_VAL("No memory", evlist);

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
		struct hists *hists = evsel__hists(evsel);

		hists__collapse_resort(hists, NULL);
		evsel__output_resort(evsel, NULL);

		if (verbose > 2) {
			pr_info("Normal histogram\n");
			print_hists_out(hists);
		}

		TEST_ASSERT_VAL("Invalid nr samples",
				hists->stats.nr_samples == 10);
		TEST_ASSERT_VAL("Invalid nr hist entries",
				hists->nr_entries == 9);
		TEST_ASSERT_VAL("Invalid total period",
				hists->stats.total_period == 1000);
		TEST_ASSERT_VAL("Unmatched nr samples",
				hists->stats.nr_samples ==
				hists->stats.nr_non_filtered_samples);
		TEST_ASSERT_VAL("Unmatched nr hist entries",
				hists->nr_entries == hists->nr_non_filtered_entries);
		TEST_ASSERT_VAL("Unmatched total period",
				hists->stats.total_period ==
				hists->stats.total_non_filtered_period);

		/* now applying thread filter for 'bash' */
		hists->thread_filter = fake_samples[9].thread;
		hists__filter_by_thread(hists);

		if (verbose > 2) {
			pr_info("Histogram for thread filter\n");
			print_hists_out(hists);
		}

		/* normal stats should be invariant */
		TEST_ASSERT_VAL("Invalid nr samples",
				hists->stats.nr_samples == 10);
		TEST_ASSERT_VAL("Invalid nr hist entries",
				hists->nr_entries == 9);
		TEST_ASSERT_VAL("Invalid total period",
				hists->stats.total_period == 1000);

		/* but filter stats are changed */
		TEST_ASSERT_VAL("Unmatched nr samples for thread filter",
				hists->stats.nr_non_filtered_samples == 4);
		TEST_ASSERT_VAL("Unmatched nr hist entries for thread filter",
				hists->nr_non_filtered_entries == 4);
		TEST_ASSERT_VAL("Unmatched total period for thread filter",
				hists->stats.total_non_filtered_period == 400);

		/* remove thread filter first */
		hists->thread_filter = NULL;
		hists__filter_by_thread(hists);

		/* now applying dso filter for 'kernel' */
		hists->dso_filter = fake_samples[0].map->dso;
		hists__filter_by_dso(hists);

		if (verbose > 2) {
			pr_info("Histogram for dso filter\n");
			print_hists_out(hists);
		}

		/* normal stats should be invariant */
		TEST_ASSERT_VAL("Invalid nr samples",
				hists->stats.nr_samples == 10);
		TEST_ASSERT_VAL("Invalid nr hist entries",
				hists->nr_entries == 9);
		TEST_ASSERT_VAL("Invalid total period",
				hists->stats.total_period == 1000);

		/* but filter stats are changed */
		TEST_ASSERT_VAL("Unmatched nr samples for dso filter",
				hists->stats.nr_non_filtered_samples == 3);
		TEST_ASSERT_VAL("Unmatched nr hist entries for dso filter",
				hists->nr_non_filtered_entries == 3);
		TEST_ASSERT_VAL("Unmatched total period for dso filter",
				hists->stats.total_non_filtered_period == 300);

		/* remove dso filter first */
		hists->dso_filter = NULL;
		hists__filter_by_dso(hists);

		/*
		 * now applying symbol filter for 'main'.  Also note that
		 * there's 3 samples that have 'main' symbol but the 4th
		 * entry of fake_samples was collapsed already so it won't
		 * be counted as a separate entry but the sample count and
		 * total period will be remained.
		 */
		hists->symbol_filter_str = "main";
		hists__filter_by_symbol(hists);

		if (verbose > 2) {
			pr_info("Histogram for symbol filter\n");
			print_hists_out(hists);
		}

		/* normal stats should be invariant */
		TEST_ASSERT_VAL("Invalid nr samples",
				hists->stats.nr_samples == 10);
		TEST_ASSERT_VAL("Invalid nr hist entries",
				hists->nr_entries == 9);
		TEST_ASSERT_VAL("Invalid total period",
				hists->stats.total_period == 1000);

		/* but filter stats are changed */
		TEST_ASSERT_VAL("Unmatched nr samples for symbol filter",
				hists->stats.nr_non_filtered_samples == 3);
		TEST_ASSERT_VAL("Unmatched nr hist entries for symbol filter",
				hists->nr_non_filtered_entries == 2);
		TEST_ASSERT_VAL("Unmatched total period for symbol filter",
				hists->stats.total_non_filtered_period == 300);

		/* remove symbol filter first */
		hists->symbol_filter_str = NULL;
		hists__filter_by_symbol(hists);

		/* now applying socket filters */
		hists->socket_filter = 2;
		hists__filter_by_socket(hists);

		if (verbose > 2) {
			pr_info("Histogram for socket filters\n");
			print_hists_out(hists);
		}

		/* normal stats should be invariant */
		TEST_ASSERT_VAL("Invalid nr samples",
				hists->stats.nr_samples == 10);
		TEST_ASSERT_VAL("Invalid nr hist entries",
				hists->nr_entries == 9);
		TEST_ASSERT_VAL("Invalid total period",
				hists->stats.total_period == 1000);

		/* but filter stats are changed */
		TEST_ASSERT_VAL("Unmatched nr samples for socket filter",
				hists->stats.nr_non_filtered_samples == 2);
		TEST_ASSERT_VAL("Unmatched nr hist entries for socket filter",
				hists->nr_non_filtered_entries == 2);
		TEST_ASSERT_VAL("Unmatched total period for socket filter",
				hists->stats.total_non_filtered_period == 200);

		/* remove socket filter first */
		hists->socket_filter = -1;
		hists__filter_by_socket(hists);

		/* now applying all filters at once. */
		hists->thread_filter = fake_samples[1].thread;
		hists->dso_filter = fake_samples[1].map->dso;
		hists__filter_by_thread(hists);
		hists__filter_by_dso(hists);

		if (verbose > 2) {
			pr_info("Histogram for all filters\n");
			print_hists_out(hists);
		}

		/* normal stats should be invariant */
		TEST_ASSERT_VAL("Invalid nr samples",
				hists->stats.nr_samples == 10);
		TEST_ASSERT_VAL("Invalid nr hist entries",
				hists->nr_entries == 9);
		TEST_ASSERT_VAL("Invalid total period",
				hists->stats.total_period == 1000);

		/* but filter stats are changed */
		TEST_ASSERT_VAL("Unmatched nr samples for all filter",
				hists->stats.nr_non_filtered_samples == 2);
		TEST_ASSERT_VAL("Unmatched nr hist entries for all filter",
				hists->nr_non_filtered_entries == 1);
		TEST_ASSERT_VAL("Unmatched total period for all filter",
				hists->stats.total_non_filtered_period == 200);
	}


	err = TEST_OK;

out:
	/* tear down everything */
	evlist__delete(evlist);
	reset_output_field();
	machines__exit(&machines);

	return err;
}

DEFINE_SUITE("Filter hist entries", hists_filter);
