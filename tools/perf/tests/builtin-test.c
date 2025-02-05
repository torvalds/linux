// SPDX-License-Identifier: GPL-2.0
/*
 * builtin-test.c
 *
 * Builtin regression testing command: ever growing number of sanity tests
 */
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <unistd.h>
#include <setjmp.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include "builtin.h"
#include "config.h"
#include "hist.h"
#include "intlist.h"
#include "tests.h"
#include "debug.h"
#include "color.h"
#include <subcmd/parse-options.h>
#include <subcmd/run-command.h>
#include "string2.h"
#include "symbol.h"
#include "util/rlimit.h"
#include "util/strbuf.h"
#include <linux/kernel.h>
#include <linux/string.h>
#include <subcmd/exec-cmd.h>
#include <linux/zalloc.h>

#include "tests-scripts.h"

/*
 * Command line option to not fork the test running in the same process and
 * making them easier to debug.
 */
static bool dont_fork;
/* Fork the tests in parallel and wait for their completion. */
static bool sequential;
/* Number of times each test is run. */
static unsigned int runs_per_test = 1;
const char *dso_to_test;
const char *test_objdump_path = "objdump";

/*
 * List of architecture specific tests. Not a weak symbol as the array length is
 * dependent on the initialization, as such GCC with LTO complains of
 * conflicting definitions with a weak symbol.
 */
#if defined(__i386__) || defined(__x86_64__) || defined(__aarch64__) || defined(__powerpc64__)
extern struct test_suite *arch_tests[];
#else
static struct test_suite *arch_tests[] = {
	NULL,
};
#endif

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
	&suite__hwmon_pmu,
	&suite__tool_pmu,
	&suite__dso_data,
	&suite__perf_evsel__roundtrip_name_test,
#ifdef HAVE_LIBTRACEEVENT
	&suite__perf_evsel__tp_sched_test,
	&suite__syscall_openat_tp_fields,
#endif
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
#ifdef HAVE_LIBTRACEEVENT
	&suite__switch_tracking,
#endif
	&suite__fdarray__filter,
	&suite__fdarray__add,
	&suite__kmod_path__parse,
	&suite__thread_map,
	&suite__session_topology,
	&suite__thread_map_synthesize,
	&suite__thread_map_remove,
	&suite__cpu_map,
	&suite__synthesize_stat_config,
	&suite__synthesize_stat,
	&suite__synthesize_stat_round,
	&suite__event_update,
	&suite__event_times,
	&suite__backward_ring_buffer,
	&suite__sdt_event,
	&suite__is_printable_array,
	&suite__bitmap_print,
	&suite__perf_hooks,
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
	&suite__symbols,
	&suite__util,
	NULL,
};

static struct test_workload *workloads[] = {
	&workload__noploop,
	&workload__thloop,
	&workload__leafloop,
	&workload__sqrtloop,
	&workload__brstack,
	&workload__datasym,
	&workload__landlock,
};

#define workloads__for_each(workload) \
	for (unsigned i = 0; i < ARRAY_SIZE(workloads) && ({ workload = workloads[i]; 1; }); i++)

#define test_suite__for_each_test_case(suite, idx)			\
	for (idx = 0; (suite)->test_cases && (suite)->test_cases[idx].name != NULL; idx++)

static int test_suite__num_test_cases(const struct test_suite *t)
{
	int num;

	test_suite__for_each_test_case(t, num);

	return num;
}

static const char *skip_reason(const struct test_suite *t, int test_case)
{
	if (!t->test_cases)
		return NULL;

	return t->test_cases[test_case >= 0 ? test_case : 0].skip_reason;
}

static const char *test_description(const struct test_suite *t, int test_case)
{
	if (t->test_cases && test_case >= 0)
		return t->test_cases[test_case].desc;

	return t->desc;
}

static test_fnptr test_function(const struct test_suite *t, int test_case)
{
	if (test_case <= 0)
		return t->test_cases[0].run_case;

	return t->test_cases[test_case].run_case;
}

static bool test_exclusive(const struct test_suite *t, int test_case)
{
	if (test_case <= 0)
		return t->test_cases[0].exclusive;

	return t->test_cases[test_case].exclusive;
}

static bool perf_test__matches(const char *desc, int suite_num, int argc, const char *argv[])
{
	int i;

	if (argc == 0)
		return true;

	for (i = 0; i < argc; ++i) {
		char *end;
		long nr = strtoul(argv[i], &end, 10);

		if (*end == '\0') {
			if (nr == suite_num + 1)
				return true;
			continue;
		}

		if (strcasestr(desc, argv[i]))
			return true;
	}

	return false;
}

struct child_test {
	struct child_process process;
	struct test_suite *test;
	int suite_num;
	int test_case_num;
};

static jmp_buf run_test_jmp_buf;

static void child_test_sig_handler(int sig)
{
	siglongjmp(run_test_jmp_buf, sig);
}

static int run_test_child(struct child_process *process)
{
	const int signals[] = {
		SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGINT, SIGPIPE, SIGQUIT, SIGSEGV, SIGTERM,
	};
	struct child_test *child = container_of(process, struct child_test, process);
	int err;

	err = sigsetjmp(run_test_jmp_buf, 1);
	if (err) {
		fprintf(stderr, "\n---- unexpected signal (%d) ----\n", err);
		err = err > 0 ? -err : -1;
		goto err_out;
	}

	for (size_t i = 0; i < ARRAY_SIZE(signals); i++)
		signal(signals[i], child_test_sig_handler);

	pr_debug("--- start ---\n");
	pr_debug("test child forked, pid %d\n", getpid());
	err = test_function(child->test, child->test_case_num)(child->test, child->test_case_num);
	pr_debug("---- end(%d) ----\n", err);

err_out:
	fflush(NULL);
	for (size_t i = 0; i < ARRAY_SIZE(signals); i++)
		signal(signals[i], SIG_DFL);
	return -err;
}

#define TEST_RUNNING -3

static int print_test_result(struct test_suite *t, int curr_suite, int curr_test_case,
			     int result, int width, int running)
{
	if (test_suite__num_test_cases(t) > 1) {
		int subw = width > 2 ? width - 2 : width;

		pr_info("%3d.%1d: %-*s:", curr_suite + 1, curr_test_case + 1, subw,
			test_description(t, curr_test_case));
	} else
		pr_info("%3d: %-*s:", curr_suite + 1, width, test_description(t, curr_test_case));

	switch (result) {
	case TEST_RUNNING:
		color_fprintf(stderr, PERF_COLOR_YELLOW, " Running (%d active)\n", running);
		break;
	case TEST_OK:
		pr_info(" Ok\n");
		break;
	case TEST_SKIP: {
		const char *reason = skip_reason(t, curr_test_case);

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

	return 0;
}

static void finish_test(struct child_test **child_tests, int running_test, int child_test_num,
		int width)
{
	struct child_test *child_test = child_tests[running_test];
	struct test_suite *t;
	int curr_suite, curr_test_case, err;
	bool err_done = false;
	struct strbuf err_output = STRBUF_INIT;
	int last_running = -1;
	int ret;

	if (child_test == NULL) {
		/* Test wasn't started. */
		return;
	}
	t = child_test->test;
	curr_suite = child_test->suite_num;
	curr_test_case = child_test->test_case_num;
	err = child_test->process.err;
	/*
	 * For test suites with subtests, display the suite name ahead of the
	 * sub test names.
	 */
	if (test_suite__num_test_cases(t) > 1 && curr_test_case == 0)
		pr_info("%3d: %-*s:\n", curr_suite + 1, width, test_description(t, -1));

	/*
	 * Busy loop reading from the child's stdout/stderr that are set to be
	 * non-blocking until EOF.
	 */
	if (err > 0)
		fcntl(err, F_SETFL, O_NONBLOCK);
	if (verbose > 1) {
		if (test_suite__num_test_cases(t) > 1)
			pr_info("%3d.%1d: %s:\n", curr_suite + 1, curr_test_case + 1,
				test_description(t, curr_test_case));
		else
			pr_info("%3d: %s:\n", curr_suite + 1, test_description(t, -1));
	}
	while (!err_done) {
		struct pollfd pfds[1] = {
			{ .fd = err,
			  .events = POLLIN | POLLERR | POLLHUP | POLLNVAL,
			},
		};
		if (perf_use_color_default) {
			int running = 0;

			for (int y = running_test; y < child_test_num; y++) {
				if (child_tests[y] == NULL)
					continue;
				if (check_if_command_finished(&child_tests[y]->process) == 0)
					running++;
			}
			if (running != last_running) {
				if (last_running != -1) {
					/*
					 * Erase "Running (.. active)" line
					 * printed before poll/sleep.
					 */
					fprintf(debug_file(), PERF_COLOR_DELETE_LINE);
				}
				print_test_result(t, curr_suite, curr_test_case, TEST_RUNNING,
						  width, running);
				last_running = running;
			}
		}

		err_done = true;
		if (err <= 0) {
			/* No child stderr to poll, sleep for 10ms for child to complete. */
			usleep(10 * 1000);
		} else {
			/* Poll to avoid excessive spinning, timeout set for 100ms. */
			poll(pfds, ARRAY_SIZE(pfds), /*timeout=*/100);
			if (pfds[0].revents) {
				char buf[512];
				ssize_t len;

				len = read(err, buf, sizeof(buf) - 1);

				if (len > 0) {
					err_done = false;
					buf[len] = '\0';
					strbuf_addstr(&err_output, buf);
				}
			}
		}
		if (err_done)
			err_done = check_if_command_finished(&child_test->process);
	}
	if (perf_use_color_default && last_running != -1) {
		/* Erase "Running (.. active)" line printed before poll/sleep. */
		fprintf(debug_file(), PERF_COLOR_DELETE_LINE);
	}
	/* Clean up child process. */
	ret = finish_command(&child_test->process);
	if (verbose > 1 || (verbose == 1 && ret == TEST_FAIL))
		fprintf(stderr, "%s", err_output.buf);

	strbuf_release(&err_output);
	print_test_result(t, curr_suite, curr_test_case, ret, width, /*running=*/0);
	if (err > 0)
		close(err);
	zfree(&child_tests[running_test]);
}

static int start_test(struct test_suite *test, int curr_suite, int curr_test_case,
		struct child_test **child, int width, int pass)
{
	int err;

	*child = NULL;
	if (dont_fork) {
		if (pass == 1) {
			pr_debug("--- start ---\n");
			err = test_function(test, curr_test_case)(test, curr_test_case);
			pr_debug("---- end ----\n");
			print_test_result(test, curr_suite, curr_test_case, err, width,
					  /*running=*/0);
		}
		return 0;
	}
	if (pass == 1 && !sequential && test_exclusive(test, curr_test_case)) {
		/* When parallel, skip exclusive tests on the first pass. */
		return 0;
	}
	if (pass != 1 && (sequential || !test_exclusive(test, curr_test_case))) {
		/* Sequential and non-exclusive tests were run on the first pass. */
		return 0;
	}
	*child = zalloc(sizeof(**child));
	if (!*child)
		return -ENOMEM;

	(*child)->test = test;
	(*child)->suite_num = curr_suite;
	(*child)->test_case_num = curr_test_case;
	(*child)->process.pid = -1;
	(*child)->process.no_stdin = 1;
	if (verbose <= 0) {
		(*child)->process.no_stdout = 1;
		(*child)->process.no_stderr = 1;
	} else {
		(*child)->process.stdout_to_stderr = 1;
		(*child)->process.out = -1;
		(*child)->process.err = -1;
	}
	(*child)->process.no_exec_cmd = run_test_child;
	if (sequential || pass == 2) {
		err = start_command(&(*child)->process);
		if (err)
			return err;
		finish_test(child, /*running_test=*/0, /*child_test_num=*/1, width);
		return 0;
	}
	return start_command(&(*child)->process);
}

/* State outside of __cmd_test for the sake of the signal handler. */

static size_t num_tests;
static struct child_test **child_tests;
static jmp_buf cmd_test_jmp_buf;

static void cmd_test_sig_handler(int sig)
{
	siglongjmp(cmd_test_jmp_buf, sig);
}

static int __cmd_test(struct test_suite **suites, int argc, const char *argv[],
		      struct intlist *skiplist)
{
	static int width = 0;
	int err = 0;

	for (struct test_suite **t = suites; *t; t++) {
		int i, len = strlen(test_description(*t, -1));

		if (width < len)
			width = len;

		test_suite__for_each_test_case(*t, i) {
			len = strlen(test_description(*t, i));
			if (width < len)
				width = len;
			num_tests += runs_per_test;
		}
	}
	child_tests = calloc(num_tests, sizeof(*child_tests));
	if (!child_tests)
		return -ENOMEM;

	err = sigsetjmp(cmd_test_jmp_buf, 1);
	if (err) {
		pr_err("\nSignal (%d) while running tests.\nTerminating tests with the same signal\n",
		       err);
		for (size_t x = 0; x < num_tests; x++) {
			struct child_test *child_test = child_tests[x];

			if (!child_test || child_test->process.pid <= 0)
				continue;

			pr_debug3("Killing %d pid %d\n",
				  child_test->suite_num + 1,
				  child_test->process.pid);
			kill(child_test->process.pid, err);
		}
		goto err_out;
	}
	signal(SIGINT, cmd_test_sig_handler);
	signal(SIGTERM, cmd_test_sig_handler);

	/*
	 * In parallel mode pass 1 runs non-exclusive tests in parallel, pass 2
	 * runs the exclusive tests sequentially. In other modes all tests are
	 * run in pass 1.
	 */
	for (int pass = 1; pass <= 2; pass++) {
		int child_test_num = 0;
		int curr_suite = 0;

		for (struct test_suite **t = suites; *t; t++, curr_suite++) {
			int curr_test_case;

			if (!perf_test__matches(test_description(*t, -1), curr_suite, argc, argv)) {
				/*
				 * Test suite shouldn't be run based on
				 * description. See if any test case should.
				 */
				bool skip = true;

				test_suite__for_each_test_case(*t, curr_test_case) {
					if (perf_test__matches(test_description(*t, curr_test_case),
							       curr_suite, argc, argv)) {
						skip = false;
						break;
					}
				}
				if (skip)
					continue;
			}

			if (intlist__find(skiplist, curr_suite + 1)) {
				pr_info("%3d: %-*s:", curr_suite + 1, width,
					test_description(*t, -1));
				color_fprintf(stderr, PERF_COLOR_YELLOW, " Skip (user override)\n");
				continue;
			}

			for (unsigned int run = 0; run < runs_per_test; run++) {
				test_suite__for_each_test_case(*t, curr_test_case) {
					if (!perf_test__matches(test_description(*t, curr_test_case),
								curr_suite, argc, argv))
						continue;

					err = start_test(*t, curr_suite, curr_test_case,
							 &child_tests[child_test_num++],
							 width, pass);
					if (err)
						goto err_out;
				}
			}
		}
		if (!sequential) {
			/* Parallel mode starts tests but doesn't finish them. Do that now. */
			for (size_t x = 0; x < num_tests; x++)
				finish_test(child_tests, x, num_tests, width);
		}
	}
err_out:
	signal(SIGINT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
	if (err) {
		pr_err("Internal test harness failure. Completing any started tests:\n:");
		for (size_t x = 0; x < num_tests; x++)
			finish_test(child_tests, x, num_tests, width);
	}
	free(child_tests);
	return err;
}

static int perf_test__list(FILE *fp, struct test_suite **suites, int argc, const char **argv)
{
	int curr_suite = 0;

	for (struct test_suite **t = suites; *t; t++, curr_suite++) {
		int curr_test_case;

		if (!perf_test__matches(test_description(*t, -1), curr_suite, argc, argv))
			continue;

		fprintf(fp, "%3d: %s\n", curr_suite + 1, test_description(*t, -1));

		if (test_suite__num_test_cases(*t) <= 1)
			continue;

		test_suite__for_each_test_case(*t, curr_test_case) {
			fprintf(fp, "%3d.%1d: %s\n", curr_suite + 1, curr_test_case + 1,
				test_description(*t, curr_test_case));
		}
	}
	return 0;
}

static int workloads__fprintf_list(FILE *fp)
{
	struct test_workload *twl;
	int printed = 0;

	workloads__for_each(twl)
		printed += fprintf(fp, "%s\n", twl->name);

	return printed;
}

static int run_workload(const char *work, int argc, const char **argv)
{
	struct test_workload *twl;

	workloads__for_each(twl) {
		if (!strcmp(twl->name, work))
			return twl->func(argc, argv);
	}

	pr_info("No workload found: %s\n", work);
	return -1;
}

static int perf_test__config(const char *var, const char *value,
			     void *data __maybe_unused)
{
	if (!strcmp(var, "annotate.objdump"))
		test_objdump_path = value;

	return 0;
}

static struct test_suite **build_suites(void)
{
	/*
	 * TODO: suites is static to avoid needing to clean up the scripts tests
	 * for leak sanitizer.
	 */
	static struct test_suite **suites[] = {
		generic_tests,
		arch_tests,
		NULL,
	};
	struct test_suite **result;
	struct test_suite *t;
	size_t n = 0, num_suites = 0;

	if (suites[2] == NULL)
		suites[2] = create_script_test_suites();

#define for_each_suite(suite)						\
	for (size_t i = 0, j = 0; i < ARRAY_SIZE(suites); i++, j = 0)	\
		while ((suite = suites[i][j++]) != NULL)

	for_each_suite(t)
		num_suites++;

	result = calloc(num_suites + 1, sizeof(struct test_suite *));

	for (int pass = 1; pass <= 2; pass++) {
		for_each_suite(t) {
			bool exclusive = false;
			int curr_test_case;

			test_suite__for_each_test_case(t, curr_test_case) {
				if (test_exclusive(t, curr_test_case)) {
					exclusive = true;
					break;
				}
			}
			if ((!exclusive && pass == 1) || (exclusive && pass == 2))
				result[n++] = t;
		}
	}
	return result;
#undef for_each_suite
}

int cmd_test(int argc, const char **argv)
{
	const char *test_usage[] = {
	"perf test [<options>] [{list <test-name-fragment>|[<test-name-fragments>|<test-numbers>]}]",
	NULL,
	};
	const char *skip = NULL;
	const char *workload = NULL;
	bool list_workloads = false;
	const struct option test_options[] = {
	OPT_STRING('s', "skip", &skip, "tests", "tests to skip"),
	OPT_INCR('v', "verbose", &verbose,
		    "be more verbose (show symbol address, etc)"),
	OPT_BOOLEAN('F', "dont-fork", &dont_fork,
		    "Do not fork for testcase"),
	OPT_BOOLEAN('S', "sequential", &sequential,
		    "Run the tests one after another rather than in parallel"),
	OPT_UINTEGER('r', "runs-per-test", &runs_per_test,
		     "Run each test the given number of times, default 1"),
	OPT_STRING('w', "workload", &workload, "work", "workload to run for testing, use '--list-workloads' to list the available ones."),
	OPT_BOOLEAN(0, "list-workloads", &list_workloads, "List the available builtin workloads to use with -w/--workload"),
	OPT_STRING(0, "dso", &dso_to_test, "dso", "dso to test"),
	OPT_STRING(0, "objdump", &test_objdump_path, "path",
		   "objdump binary to use for disassembly and annotations"),
	OPT_END()
	};
	const char * const test_subcommands[] = { "list", NULL };
	struct intlist *skiplist = NULL;
        int ret = hists__init();
	struct test_suite **suites;

        if (ret < 0)
                return ret;

	perf_config(perf_test__config, NULL);

	/* Unbuffered output */
	setvbuf(stdout, NULL, _IONBF, 0);

	argc = parse_options_subcommand(argc, argv, test_options, test_subcommands, test_usage, 0);
	if (argc >= 1 && !strcmp(argv[0], "list")) {
		suites = build_suites();
		ret = perf_test__list(stdout, suites, argc - 1, argv + 1);
		free(suites);
		return ret;
	}

	if (workload)
		return run_workload(workload, argc, argv);

	if (list_workloads) {
		workloads__fprintf_list(stdout);
		return 0;
	}

	if (dont_fork)
		sequential = true;

	symbol_conf.priv_size = sizeof(int);
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

	suites = build_suites();
	ret = __cmd_test(suites, argc, argv, skiplist);
	free(suites);
	return ret;
}
