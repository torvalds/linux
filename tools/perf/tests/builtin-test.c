// SPDX-License-Identifier: GPL-2.0
/*
 * builtin-test.c
 *
 * Builtin regression testing command: ever growing number of sanity tests
 */
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include "builtin.h"
#include "hist.h"
#include "intlist.h"
#include "tests.h"
#include "debug.h"
#include "color.h"
#include <subcmd/parse-options.h>
#include "string2.h"
#include "symbol.h"
#include "util/rlimit.h"
#include <linux/kernel.h>
#include <linux/string.h>
#include <subcmd/exec-cmd.h>
#include <linux/zalloc.h>

#include "builtin-test-list.h"

static bool dont_fork;

struct test_suite *__weak arch_tests[] = {
	NULL,
};

static struct test_suite *generic_tests[] = {
	&suite__vmlinux_matches_kallsyms,
	&suite__openat_syscall_event,
	&suite__openat_syscall_event_on_all_cpus,
	&suite__basic_mmap,
	&suite__mem,
	&suite__parse_events,
	&suite__expr,
	&suite__PERF_RECORD,
	&suite__pmu,
	&suite__pmu_events,
	&suite__dso_data,
	&suite__dso_data_cache,
	&suite__dso_data_reopen,
	&suite__perf_evsel__roundtrip_name_test,
	&suite__perf_evsel__tp_sched_test,
	&suite__syscall_openat_tp_fields,
	&suite__attr,
	&suite__hists_link,
	&suite__python_use,
	&suite__bp_signal,
	&suite__bp_signal_overflow,
	&suite__bp_accounting,
	&suite__wp,
	&suite__task_exit,
	&suite__sw_clock_freq,
	&suite__code_reading,
	&suite__sample_parsing,
	&suite__keep_tracking,
	&suite__parse_no_sample_id_all,
	&suite__hists_filter,
	&suite__mmap_thread_lookup,
	&suite__thread_maps_share,
	&suite__hists_output,
	&suite__hists_cumulate,
	&suite__switch_tracking,
	&suite__fdarray__filter,
	&suite__fdarray__add,
	&suite__kmod_path__parse,
	&suite__thread_map,
	&suite__llvm,
	&suite__session_topology,
	&suite__bpf,
	&suite__thread_map_synthesize,
	&suite__thread_map_remove,
	&suite__cpu_map_synthesize,
	&suite__synthesize_stat_config,
	&suite__synthesize_stat,
	&suite__synthesize_stat_round,
	&suite__event_update,
	&suite__event_times,
	&suite__backward_ring_buffer,
	&suite__cpu_map_print,
	&suite__cpu_map_merge,
	&suite__sdt_event,
	&suite__is_printable_array,
	&suite__bitmap_print,
	&suite__perf_hooks,
	&suite__clang,
	&suite__unit_number__scnprint,
	&suite__mem2node,
	&suite__time_utils,
	&suite__jit_write_elf,
	&suite__pfm,
	&suite__api_io,
	&suite__maps__merge_in,
	&suite__demangle_java,
	&suite__demangle_ocaml,
	&suite__parse_metric,
	&suite__pe_file_parsing,
	&suite__expand_cgroup_events,
	&suite__perf_time_to_tsc,
	&suite__dlfilter,
	&suite__sigtrap,
	&suite__event_groups,
	NULL,
};

static struct test_suite **tests[] = {
	generic_tests,
	arch_tests,
};

static struct test_workload *workloads[] = {
	&workload__noploop,
	&workload__thloop,
	&workload__leafloop,
	&workload__sqrtloop,
	&workload__brstack,
	&workload__datasym,
};

static int num_subtests(const struct test_suite *t)
{
	int num;

	if (!t->test_cases)
		return 0;

	num = 0;
	while (t->test_cases[num].name)
		num++;

	return num;
}

static bool has_subtests(const struct test_suite *t)
{
	return num_subtests(t) > 1;
}

static const char *skip_reason(const struct test_suite *t, int subtest)
{
	if (!t->test_cases)
		return NULL;

	return t->test_cases[subtest >= 0 ? subtest : 0].skip_reason;
}

static const char *test_description(const struct test_suite *t, int subtest)
{
	if (t->test_cases && subtest >= 0)
		return t->test_cases[subtest].desc;

	return t->desc;
}

static test_fnptr test_function(const struct test_suite *t, int subtest)
{
	if (subtest <= 0)
		return t->test_cases[0].run_case;

	return t->test_cases[subtest].run_case;
}

static bool perf_test__matches(const char *desc, int curr, int argc, const char *argv[])
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

		if (strcasestr(desc, argv[i]))
			return true;
	}

	return false;
}

static int run_test(struct test_suite *test, int subtest)
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

			if (verbose <= 0) {
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

		err = test_function(test, subtest)(test, subtest);
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

#define for_each_test(j, k, t)			\
	for (j = 0; j < ARRAY_SIZE(tests); j++)	\
		for (k = 0, t = tests[j][k]; tests[j][k]; k++, t = tests[j][k])

static int test_and_print(struct test_suite *t, int subtest)
{
	int err;

	pr_debug("\n--- start ---\n");
	err = run_test(t, subtest);
	pr_debug("---- end ----\n");

	if (!has_subtests(t))
		pr_debug("%s:", t->desc);
	else
		pr_debug("%s subtest %d:", t->desc, subtest + 1);

	switch (err) {
	case TEST_OK:
		pr_info(" Ok\n");
		break;
	case TEST_SKIP: {
		const char *reason = skip_reason(t, subtest);

		if (reason)
			color_fprintf(stderr, PERF_COLOR_YELLOW, " Skip (%s)\n", reason);
		else
			color_fprintf(stderr, PERF_COLOR_YELLOW, " Skip\n");
	}
		break;
	case TEST_FAIL:
	default:
		color_fprintf(stderr, PERF_COLOR_RED, " FAILED!\n");
		break;
	}

	return err;
}

struct shell_test {
	const char *dir;
	const char *file;
};

static int shell_test__run(struct test_suite *test, int subdir __maybe_unused)
{
	int err;
	char script[PATH_MAX];
	struct shell_test *st = test->priv;

	path__join(script, sizeof(script) - 3, st->dir, st->file);

	if (verbose)
		strncat(script, " -v", sizeof(script) - strlen(script) - 1);

	err = system(script);
	if (!err)
		return TEST_OK;

	return WEXITSTATUS(err) == 2 ? TEST_SKIP : TEST_FAIL;
}

static int run_shell_tests(int argc, const char *argv[], int i, int width,
				struct intlist *skiplist)
{
	struct shell_test st;
	const struct script_file *files, *file;

	files = list_script_files();
	if (!files)
		return 0;
	for (file = files; file->dir; file++) {
		int curr = i++;
		struct test_case test_cases[] = {
			{
				.desc = file->desc,
				.run_case = shell_test__run,
			},
			{ .name = NULL, }
		};
		struct test_suite test_suite = {
			.desc = test_cases[0].desc,
			.test_cases = test_cases,
			.priv = &st,
		};
		st.dir = file->dir;

		if (test_suite.desc == NULL ||
		    !perf_test__matches(test_suite.desc, curr, argc, argv))
			continue;

		st.file = file->file;
		pr_info("%3d: %-*s:", i, width, test_suite.desc);

		if (intlist__find(skiplist, i)) {
			color_fprintf(stderr, PERF_COLOR_YELLOW, " Skip (user override)\n");
			continue;
		}

		test_and_print(&test_suite, 0);
	}
	return 0;
}

static int __cmd_test(int argc, const char *argv[], struct intlist *skiplist)
{
	struct test_suite *t;
	unsigned int j, k;
	int i = 0;
	int width = list_script_max_width();

	for_each_test(j, k, t) {
		int len = strlen(test_description(t, -1));

		if (width < len)
			width = len;
	}

	for_each_test(j, k, t) {
		int curr = i++;
		int subi;

		if (!perf_test__matches(test_description(t, -1), curr, argc, argv)) {
			bool skip = true;
			int subn;

			subn = num_subtests(t);

			for (subi = 0; subi < subn; subi++) {
				if (perf_test__matches(test_description(t, subi),
							curr, argc, argv))
					skip = false;
			}

			if (skip)
				continue;
		}

		pr_info("%3d: %-*s:", i, width, test_description(t, -1));

		if (intlist__find(skiplist, i)) {
			color_fprintf(stderr, PERF_COLOR_YELLOW, " Skip (user override)\n");
			continue;
		}

		if (!has_subtests(t)) {
			test_and_print(t, -1);
		} else {
			int subn = num_subtests(t);
			/*
			 * minus 2 to align with normal testcases.
			 * For subtest we print additional '.x' in number.
			 * for example:
			 *
			 * 35: Test LLVM searching and compiling                        :
			 * 35.1: Basic BPF llvm compiling test                          : Ok
			 */
			int subw = width > 2 ? width - 2 : width;

			if (subn <= 0) {
				color_fprintf(stderr, PERF_COLOR_YELLOW,
					      " Skip (not compiled in)\n");
				continue;
			}
			pr_info("\n");

			for (subi = 0; subi < subn; subi++) {
				int len = strlen(test_description(t, subi));

				if (subw < len)
					subw = len;
			}

			for (subi = 0; subi < subn; subi++) {
				if (!perf_test__matches(test_description(t, subi),
							curr, argc, argv))
					continue;

				pr_info("%3d.%1d: %-*s:", i, subi + 1, subw,
					test_description(t, subi));
				test_and_print(t, subi);
			}
		}
	}

	return run_shell_tests(argc, argv, i, width, skiplist);
}

static int perf_test__list_shell(int argc, const char **argv, int i)
{
	const struct script_file *files, *file;

	files = list_script_files();
	if (!files)
		return 0;
	for (file = files; file->dir; file++) {
		int curr = i++;
		struct test_suite t = {
			.desc = file->desc
		};

		if (!perf_test__matches(t.desc, curr, argc, argv))
			continue;

		pr_info("%3d: %s\n", i, t.desc);
	}
	return 0;
}

static int perf_test__list(int argc, const char **argv)
{
	unsigned int j, k;
	struct test_suite *t;
	int i = 0;

	for_each_test(j, k, t) {
		int curr = i++;

		if (!perf_test__matches(test_description(t, -1), curr, argc, argv))
			continue;

		pr_info("%3d: %s\n", i, test_description(t, -1));

		if (has_subtests(t)) {
			int subn = num_subtests(t);
			int subi;

			for (subi = 0; subi < subn; subi++)
				pr_info("%3d:%1d: %s\n", i, subi + 1,
					test_description(t, subi));
		}
	}

	perf_test__list_shell(argc, argv, i);

	return 0;
}

static int run_workload(const char *work, int argc, const char **argv)
{
	unsigned int i = 0;
	struct test_workload *twl;

	for (i = 0; i < ARRAY_SIZE(workloads); i++) {
		twl = workloads[i];
		if (!strcmp(twl->name, work))
			return twl->func(argc, argv);
	}

	pr_info("No workload found: %s\n", work);
	return -1;
}

int cmd_test(int argc, const char **argv)
{
	const char *test_usage[] = {
	"perf test [<options>] [{list <test-name-fragment>|[<test-name-fragments>|<test-numbers>]}]",
	NULL,
	};
	const char *skip = NULL;
	const char *workload = NULL;
	const struct option test_options[] = {
	OPT_STRING('s', "skip", &skip, "tests", "tests to skip"),
	OPT_INCR('v', "verbose", &verbose,
		    "be more verbose (show symbol address, etc)"),
	OPT_BOOLEAN('F', "dont-fork", &dont_fork,
		    "Do not fork for testcase"),
	OPT_STRING('w', "workload", &workload, "work", "workload to run for testing"),
	OPT_END()
	};
	const char * const test_subcommands[] = { "list", NULL };
	struct intlist *skiplist = NULL;
        int ret = hists__init();

        if (ret < 0)
                return ret;

	/* Unbuffered output */
	setvbuf(stdout, NULL, _IONBF, 0);

	argc = parse_options_subcommand(argc, argv, test_options, test_subcommands, test_usage, 0);
	if (argc >= 1 && !strcmp(argv[0], "list"))
		return perf_test__list(argc - 1, argv + 1);

	if (workload)
		return run_workload(workload, argc, argv);

	symbol_conf.priv_size = sizeof(int);
	symbol_conf.sort_by_name = true;
	symbol_conf.try_vmlinux_path = true;

	if (symbol__init(NULL) < 0)
		return -1;

	if (skip != NULL)
		skiplist = intlist__new(skip);
	/*
	 * Tests that create BPF maps, for instance, need more than the 64K
	 * default:
	 */
	rlimit__bump_memlock();

	return __cmd_test(argc, argv, skiplist);
}
