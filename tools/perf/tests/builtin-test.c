/*
 * builtin-test.c
 *
 * Builtin regression testing command: ever growing number of sanity tests
 */
#include "builtin.h"
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
		.desc = "Test dso data interface",
		.func = test__dso_data,
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
		.desc = "Test matching and linking mutliple hists",
		.func = test__hists_link,
	},
	{
		.desc = "Try 'use perf' in python, checking link problems",
		.func = test__python_use,
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
		err = tests[curr].func();
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

	argc = parse_options(argc, argv, test_options, test_usage, 0);
	if (argc >= 1 && !strcmp(argv[0], "list"))
		return perf_test__list(argc, argv);

	symbol_conf.priv_size = sizeof(int);
	symbol_conf.sort_by_name = true;
	symbol_conf.try_vmlinux_path = true;

	if (symbol__init() < 0)
		return -1;

	if (skip != NULL)
		skiplist = intlist__new(skip);

	return __cmd_test(argc, argv, skiplist);
}
