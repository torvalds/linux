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
#include <subcmd/parse-options.h>
#include "symbol.h"

static bool dont_fork;

struct test __weak arch_tests[] = {
	{
		.func = NULL,
	},
};

static struct test generic_tests[] = {
	{
		.desc = "vmlinux symtab matches kallsyms",
		.func = test__vmlinux_matches_kallsyms,
	},
	{
		.desc = "Detect openat syscall event",
		.func = test__openat_syscall_event,
	},
	{
		.desc = "Detect openat syscall event on all cpus",
		.func = test__openat_syscall_event_on_all_cpus,
	},
	{
		.desc = "Read samples using the mmap interface",
		.func = test__basic_mmap,
	},
	{
		.desc = "Parse event definition strings",
		.func = test__parse_events,
	},
	{
		.desc = "PERF_RECORD_* events & perf_sample fields",
		.func = test__PERF_RECORD,
	},
	{
		.desc = "Parse perf pmu format",
		.func = test__pmu,
	},
	{
		.desc = "DSO data read",
		.func = test__dso_data,
	},
	{
		.desc = "DSO data cache",
		.func = test__dso_data_cache,
	},
	{
		.desc = "DSO data reopen",
		.func = test__dso_data_reopen,
	},
	{
		.desc = "Roundtrip evsel->name",
		.func = test__perf_evsel__roundtrip_name_test,
	},
	{
		.desc = "Parse sched tracepoints fields",
		.func = test__perf_evsel__tp_sched_test,
	},
	{
		.desc = "syscalls:sys_enter_openat event fields",
		.func = test__syscall_openat_tp_fields,
	},
	{
		.desc = "Setup struct perf_event_attr",
		.func = test__attr,
	},
	{
		.desc = "Match and link multiple hists",
		.func = test__hists_link,
	},
	{
		.desc = "'import perf' in python",
		.func = test__python_use,
	},
	{
		.desc = "Breakpoint overflow signal handler",
		.func = test__bp_signal,
	},
	{
		.desc = "Breakpoint overflow sampling",
		.func = test__bp_signal_overflow,
	},
	{
		.desc = "Number of exit events of a simple workload",
		.func = test__task_exit,
	},
	{
		.desc = "Software clock events period values",
		.func = test__sw_clock_freq,
	},
	{
		.desc = "Object code reading",
		.func = test__code_reading,
	},
	{
		.desc = "Sample parsing",
		.func = test__sample_parsing,
	},
	{
		.desc = "Use a dummy software event to keep tracking",
		.func = test__keep_tracking,
	},
	{
		.desc = "Parse with no sample_id_all bit set",
		.func = test__parse_no_sample_id_all,
	},
	{
		.desc = "Filter hist entries",
		.func = test__hists_filter,
	},
	{
		.desc = "Lookup mmap thread",
		.func = test__mmap_thread_lookup,
	},
	{
		.desc = "Share thread mg",
		.func = test__thread_mg_share,
	},
	{
		.desc = "Sort output of hist entries",
		.func = test__hists_output,
	},
	{
		.desc = "Cumulate child hist entries",
		.func = test__hists_cumulate,
	},
	{
		.desc = "Track with sched_switch",
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
		.desc = "kmod_path__parse",
		.func = test__kmod_path__parse,
	},
	{
		.desc = "Thread map",
		.func = test__thread_map,
	},
	{
		.desc = "LLVM search and compile",
		.func = test__llvm,
		.subtest = {
			.skip_if_fail	= true,
			.get_nr		= test__llvm_subtest_get_nr,
			.get_desc	= test__llvm_subtest_get_desc,
		},
	},
	{
		.desc = "Session topology",
		.func = test_session_topology,
	},
	{
		.desc = "BPF filter",
		.func = test__bpf,
		.subtest = {
			.skip_if_fail	= true,
			.get_nr		= test__bpf_subtest_get_nr,
			.get_desc	= test__bpf_subtest_get_desc,
		},
	},
	{
		.desc = "Synthesize thread map",
		.func = test__thread_map_synthesize,
	},
	{
		.desc = "Remove thread map",
		.func = test__thread_map_remove,
	},
	{
		.desc = "Synthesize cpu map",
		.func = test__cpu_map_synthesize,
	},
	{
		.desc = "Synthesize stat config",
		.func = test__synthesize_stat_config,
	},
	{
		.desc = "Synthesize stat",
		.func = test__synthesize_stat,
	},
	{
		.desc = "Synthesize stat round",
		.func = test__synthesize_stat_round,
	},
	{
		.desc = "Synthesize attr update",
		.func = test__event_update,
	},
	{
		.desc = "Event times",
		.func = test__event_times,
	},
	{
		.desc = "Read backward ring buffer",
		.func = test__backward_ring_buffer,
	},
	{
		.desc = "Print cpu map",
		.func = test__cpu_map_print,
	},
	{
		.desc = "Probe SDT events",
		.func = test__sdt_event,
	},
	{
		.desc = "is_printable_array",
		.func = test__is_printable_array,
	},
	{
		.desc = "Print bitmap",
		.func = test__bitmap_print,
	},
	{
		.desc = "perf hooks",
		.func = test__perf_hooks,
	},
	{
		.desc = "builtin clang support",
		.func = test__clang,
		.subtest = {
			.skip_if_fail	= true,
			.get_nr		= test__clang_subtest_get_nr,
			.get_desc	= test__clang_subtest_get_desc,
		}
	},
	{
		.desc = "unit_number__scnprintf",
		.func = test__unit_number__scnprint,
	},
	{
		.func = NULL,
	},
};

static struct test *tests[] = {
	generic_tests,
	arch_tests,
};

static bool perf_test__matches(struct test *test, int curr, int argc, const char *argv[])
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

		if (strcasestr(test->desc, argv[i]))
			return true;
	}

	return false;
}

static int run_test(struct test *test, int subtest)
{
	int status, err = -1, child = dont_fork ? 0 : fork();
	char sbuf[STRERR_BUFSIZE];

	if (child < 0) {
		pr_err("failed to fork test: %s\n",
			str_error_r(errno, sbuf, sizeof(sbuf)));
		return -1;
	}

	if (!child) {
		if (!dont_fork) {
			pr_debug("test child forked, pid %d\n", getpid());

			if (!verbose) {
				int nullfd = open("/dev/null", O_WRONLY);

				if (nullfd >= 0) {
					close(STDERR_FILENO);
					close(STDOUT_FILENO);

					dup2(nullfd, STDOUT_FILENO);
					dup2(STDOUT_FILENO, STDERR_FILENO);
					close(nullfd);
				}
			} else {
				signal(SIGSEGV, sighandler_dump_stack);
				signal(SIGFPE, sighandler_dump_stack);
			}
		}

		err = test->func(subtest);
		if (!dont_fork)
			exit(err);
	}

	if (!dont_fork) {
		wait(&status);

		if (WIFEXITED(status)) {
			err = (signed char)WEXITSTATUS(status);
			pr_debug("test child finished with %d\n", err);
		} else if (WIFSIGNALED(status)) {
			err = -1;
			pr_debug("test child interrupted\n");
		}
	}

	return err;
}

#define for_each_test(j, t)	 				\
	for (j = 0; j < ARRAY_SIZE(tests); j++)	\
		for (t = &tests[j][0]; t->func; t++)

static int test_and_print(struct test *t, bool force_skip, int subtest)
{
	int err;

	if (!force_skip) {
		pr_debug("\n--- start ---\n");
		err = run_test(t, subtest);
		pr_debug("---- end ----\n");
	} else {
		pr_debug("\n--- force skipped ---\n");
		err = TEST_SKIP;
	}

	if (!t->subtest.get_nr)
		pr_debug("%s:", t->desc);
	else
		pr_debug("%s subtest %d:", t->desc, subtest);

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

	return err;
}

static int __cmd_test(int argc, const char *argv[], struct intlist *skiplist)
{
	struct test *t;
	unsigned int j;
	int i = 0;
	int width = 0;

	for_each_test(j, t) {
		int len = strlen(t->desc);

		if (width < len)
			width = len;
	}

	for_each_test(j, t) {
		int curr = i++, err;

		if (!perf_test__matches(t, curr, argc, argv))
			continue;

		pr_info("%2d: %-*s:", i, width, t->desc);

		if (intlist__find(skiplist, i)) {
			color_fprintf(stderr, PERF_COLOR_YELLOW, " Skip (user override)\n");
			continue;
		}

		if (!t->subtest.get_nr) {
			test_and_print(t, false, -1);
		} else {
			int subn = t->subtest.get_nr();
			/*
			 * minus 2 to align with normal testcases.
			 * For subtest we print additional '.x' in number.
			 * for example:
			 *
			 * 35: Test LLVM searching and compiling                        :
			 * 35.1: Basic BPF llvm compiling test                          : Ok
			 */
			int subw = width > 2 ? width - 2 : width;
			bool skip = false;
			int subi;

			if (subn <= 0) {
				color_fprintf(stderr, PERF_COLOR_YELLOW,
					      " Skip (not compiled in)\n");
				continue;
			}
			pr_info("\n");

			for (subi = 0; subi < subn; subi++) {
				int len = strlen(t->subtest.get_desc(subi));

				if (subw < len)
					subw = len;
			}

			for (subi = 0; subi < subn; subi++) {
				pr_info("%2d.%1d: %-*s:", i, subi + 1, subw,
					t->subtest.get_desc(subi));
				err = test_and_print(t, skip, subi);
				if (err != TEST_OK && t->subtest.skip_if_fail)
					skip = true;
			}
		}
	}

	return 0;
}

static int perf_test__list(int argc, const char **argv)
{
	unsigned int j;
	struct test *t;
	int i = 0;

	for_each_test(j, t) {
		if (argc > 1 && !strstr(t->desc, argv[1]))
			continue;

		pr_info("%2d: %s\n", ++i, t->desc);
	}

	return 0;
}

int cmd_test(int argc, const char **argv, const char *prefix __maybe_unused)
{
	const char *test_usage[] = {
	"perf test [<options>] [{list <test-name-fragment>|[<test-name-fragments>|<test-numbers>]}]",
	NULL,
	};
	const char *skip = NULL;
	const struct option test_options[] = {
	OPT_STRING('s', "skip", &skip, "tests", "tests to skip"),
	OPT_INCR('v', "verbose", &verbose,
		    "be more verbose (show symbol address, etc)"),
	OPT_BOOLEAN('F', "dont-fork", &dont_fork,
		    "Do not fork for testcase"),
	OPT_END()
	};
	const char * const test_subcommands[] = { "list", NULL };
	struct intlist *skiplist = NULL;
        int ret = hists__init();

        if (ret < 0)
                return ret;

	argc = parse_options_subcommand(argc, argv, test_options, test_subcommands, test_usage, 0);
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
