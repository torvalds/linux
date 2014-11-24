/*
 * builtin-test.c
 *
 * Builtin regression testing command: ever growing number of sanity tests
 */
#include <unistd.h>
#include <string.h>
#include "builtin.h"
#include "hist.h"
#include "intlist.h"
#include "tests.h"
#include "debug.h"
#include "color.h"
#include "parse-options.h"
#include "symbol.h"

static struct test {
	const char *desc;
	int (*func)(void);
} tests[] = {
	{
		.desc = "vmlinux symtab matches kallsyms",
		.func = test__vmlinux_matches_kallsyms,
	},
	{
		.desc = "detect open syscall event",
		.func = test__open_syscall_event,
	},
	{
		.desc = "detect open syscall event on all cpus",
		.func = test__open_syscall_event_on_all_cpus,
	},
	{
		.desc = "read samples using the mmap interface",
		.func = test__basic_mmap,
	},
	{
		.desc = "parse events tests",
		.func = test__parse_events,
	},
#if defined(__x86_64__) || defined(__i386__)
	{
		.desc = "x86 rdpmc test",
		.func = test__rdpmc,
	},
#endif
	{
		.desc = "Validate PERF_RECORD_* events & perf_sample fields",
		.func = test__PERF_RECORD,
	},
	{
		.desc = "Test perf pmu format parsing",
		.func = test__pmu,
	},
	{
		.desc = "Test dso data read",
		.func = test__dso_data,
	},
	{
		.desc = "Test dso data cache",
		.func = test__dso_data_cache,
	},
	{
		.desc = "Test dso data reopen",
		.func = test__dso_data_reopen,
	},
	{
		.desc = "roundtrip evsel->name check",
		.func = test__perf_evsel__roundtrip_name_test,
	},
	{
		.desc = "Check parsing of sched tracepoints fields",
		.func = test__perf_evsel__tp_sched_test,
	},
	{
		.desc = "Generate and check syscalls:sys_enter_open event fields",
		.func = test__syscall_open_tp_fields,
	},
	{
		.desc = "struct perf_event_attr setup",
		.func = test__attr,
	},
	{
		.desc = "Test matching and linking multiple hists",
		.func = test__hists_link,
	},
	{
		.desc = "Try 'use perf' in python, checking link problems",
		.func = test__python_use,
	},
	{
		.desc = "Test breakpoint overflow signal handler",
		.func = test__bp_signal,
	},
	{
		.desc = "Test breakpoint overflow sampling",
		.func = test__bp_signal_overflow,
	},
	{
		.desc = "Test number of exit event of a simple workload",
		.func = test__task_exit,
	},
	{
		.desc = "Test software clock events have valid period values",
		.func = test__sw_clock_freq,
	},
#if defined(__x86_64__) || defined(__i386__)
	{
		.desc = "Test converting perf time to TSC",
		.func = test__perf_time_to_tsc,
	},
#endif
	{
		.desc = "Test object code reading",
		.func = test__code_reading,
	},
	{
		.desc = "Test sample parsing",
		.func = test__sample_parsing,
	},
	{
		.desc = "Test using a dummy software event to keep tracking",
		.func = test__keep_tracking,
	},
	{
		.desc = "Test parsing with no sample_id_all bit set",
		.func = test__parse_no_sample_id_all,
	},
#if defined(__x86_64__) || defined(__i386__) || defined(__arm__)
#ifdef HAVE_DWARF_UNWIND_SUPPORT
	{
		.desc = "Test dwarf unwind",
		.func = test__dwarf_unwind,
	},
#endif
#endif
	{
		.desc = "Test filtering hist entries",
		.func = test__hists_filter,
	},
	{
		.desc = "Test mmap thread lookup",
		.func = test__mmap_thread_lookup,
	},
	{
		.desc = "Test thread mg sharing",
		.func = test__thread_mg_share,
	},
	{
		.desc = "Test output sorting of hist entries",
		.func = test__hists_output,
	},
	{
		.desc = "Test cumulation of child hist entries",
		.func = test__hists_cumulate,
	},
	{
		.desc = "Test tracking with sched_switch",
		.func = test__switch_tracking,
	},
	{
		.desc = "Filter fds with revents mask in a fdarray",
		.func = test__fdarray__filter,
	},
	{
		.desc = "Add fd to a fdarray, making it autogrow",
		.func = test__fdarray__add,
	},
	{
		.func = NULL,
	},
};

static bool perf_test__matches(int curr, int argc, const char *argv[])
{
	int i;

	if (argc == 0)
		return true;

	for (i = 0; i < argc; ++i) {
		char *end;
		long nr = strtoul(argv[i], &end, 10);

		if (*end == '\0') {
			if (nr == curr + 1)
				return true;
			continue;
		}

		if (strstr(tests[curr].desc, argv[i]))
			return true;
	}

	return false;
}

static int run_test(struct test *test)
{
	int status, err = -1, child = fork();
	char sbuf[STRERR_BUFSIZE];

	if (child < 0) {
		pr_err("failed to fork test: %s\n",
			strerror_r(errno, sbuf, sizeof(sbuf)));
		return -1;
	}

	if (!child) {
		pr_debug("test child forked, pid %d\n", getpid());
		err = test->func();
		exit(err);
	}

	wait(&status);

	if (WIFEXITED(status)) {
		err = WEXITSTATUS(status);
		pr_debug("test child finished with %d\n", err);
	} else if (WIFSIGNALED(status)) {
		err = -1;
		pr_debug("test child interrupted\n");
	}

	return err;
}

static int __cmd_test(int argc, const char *argv[], struct intlist *skiplist)
{
	int i = 0;
	int width = 0;

	while (tests[i].func) {
		int len = strlen(tests[i].desc);

		if (width < len)
			width = len;
		++i;
	}

	i = 0;
	while (tests[i].func) {
		int curr = i++, err;

		if (!perf_test__matches(curr, argc, argv))
			continue;

		pr_info("%2d: %-*s:", i, width, tests[curr].desc);

		if (intlist__find(skiplist, i)) {
			color_fprintf(stderr, PERF_COLOR_YELLOW, " Skip (user override)\n");
			continue;
		}

		pr_debug("\n--- start ---\n");
		err = run_test(&tests[curr]);
		pr_debug("---- end ----\n%s:", tests[curr].desc);

		switch (err) {
		case TEST_OK:
			pr_info(" Ok\n");
			break;
		case TEST_SKIP:
			color_fprintf(stderr, PERF_COLOR_YELLOW, " Skip\n");
			break;
		case TEST_FAIL:
		default:
			color_fprintf(stderr, PERF_COLOR_RED, " FAILED!\n");
			break;
		}
	}

	return 0;
}

static int perf_test__list(int argc, const char **argv)
{
	int i = 0;

	while (tests[i].func) {
		int curr = i++;

		if (argc > 1 && !strstr(tests[curr].desc, argv[1]))
			continue;

		pr_info("%2d: %s\n", i, tests[curr].desc);
	}

	return 0;
}

int cmd_test(int argc, const char **argv, const char *prefix __maybe_unused)
{
	const char * const test_usage[] = {
	"perf test [<options>] [{list <test-name-fragment>|[<test-name-fragments>|<test-numbers>]}]",
	NULL,
	};
	const char *skip = NULL;
	const struct option test_options[] = {
	OPT_STRING('s', "skip", &skip, "tests", "tests to skip"),
	OPT_INCR('v', "verbose", &verbose,
		    "be more verbose (show symbol address, etc)"),
	OPT_END()
	};
	struct intlist *skiplist = NULL;
        int ret = hists__init();

        if (ret < 0)
                return ret;

	argc = parse_options(argc, argv, test_options, test_usage, 0);
	if (argc >= 1 && !strcmp(argv[0], "list"))
		return perf_test__list(argc, argv);

	symbol_conf.priv_size = sizeof(int);
	symbol_conf.sort_by_name = true;
	symbol_conf.try_vmlinux_path = true;

	if (symbol__init(NULL) < 0)
		return -1;

	if (skip != NULL)
		skiplist = intlist__new(skip);

	return __cmd_test(argc, argv, skiplist);
}
