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
/* Fork the tests in parallel and then wait for their completion. */
static bool parallel;
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
#ifdef HAVE_LIBTRACEEVENT
	&suite__openat_syscall_event,
	&suite__openat_syscall_event_on_all_cpus,
	&suite__basic_mmap,
#endif
	&suite__mem,
	&suite__parse_events,
	&suite__expr,
	&suite__PERF_RECORD,
	&suite__pmu,
	&suite__pmu_events,
	&suite__dso_data,
	&suite__perf_evsel__roundtrip_name_test,
#ifdef HAVE_LIBTRACEEVENT
	&suite__perf_evsel__tp_sched_test,
	&suite__syscall_openat_tp_fields,
#endif
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

static struct test_suite **tests[] = {
	generic_tests,
	arch_tests,
	NULL, /* shell tests created at runtime. */
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

struct child_test {
	struct child_process process;
	struct test_suite *test;
	int test_num;
	int subtest;
};

static int run_test_child(struct child_process *process)
{
	struct child_test *child = container_of(process, struct child_test, process);
	int err;

	pr_debug("--- start ---\n");
	pr_debug("test child forked, pid %d\n", getpid());
	err = test_function(child->test, child->subtest)(child->test, child->subtest);
	pr_debug("---- end(%d) ----\n", err);
	fflush(NULL);
	return -err;
}

static int print_test_result(struct test_suite *t, int i, int subtest, int result, int width)
{
	if (has_subtests(t)) {
		int subw = width > 2 ? width - 2 : width;

		pr_info("%3d.%1d: %-*s:", i + 1, subtest + 1, subw, test_description(t, subtest));
	} else
		pr_info("%3d: %-*s:", i + 1, width, test_description(t, subtest));

	switch (result) {
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

	return 0;
}

static int finish_test(struct child_test *child_test, int width)
{
	struct test_suite *t = child_test->test;
	int i = child_test->test_num;
	int subi = child_test->subtest;
	int err = child_test->process.err;
	bool err_done = err <= 0;
	struct strbuf err_output = STRBUF_INIT;
	int ret;

	/*
	 * For test suites with subtests, display the suite name ahead of the
	 * sub test names.
	 */
	if (has_subtests(t) && subi == 0)
		pr_info("%3d: %-*s:\n", i + 1, width, test_description(t, -1));

	/*
	 * Busy loop reading from the child's stdout/stderr that are set to be
	 * non-blocking until EOF.
	 */
	if (!err_done)
		fcntl(err, F_SETFL, O_NONBLOCK);
	if (verbose > 1) {
		if (has_subtests(t))
			pr_info("%3d.%1d: %s:\n", i + 1, subi + 1, test_description(t, subi));
		else
			pr_info("%3d: %s:\n", i + 1, test_description(t, -1));
	}
	while (!err_done) {
		struct pollfd pfds[1] = {
			{ .fd = err,
			  .events = POLLIN | POLLERR | POLLHUP | POLLNVAL,
			},
		};
		char buf[512];
		ssize_t len;

		/* Poll to avoid excessive spinning, timeout set for 1000ms. */
		poll(pfds, ARRAY_SIZE(pfds), /*timeout=*/1000);
		if (!err_done && pfds[0].revents) {
			errno = 0;
			len = read(err, buf, sizeof(buf) - 1);

			if (len <= 0) {
				err_done = errno != EAGAIN;
			} else {
				buf[len] = '\0';
				if (verbose > 1)
					fprintf(stdout, "%s", buf);
				else
					strbuf_addstr(&err_output, buf);
			}
		}
	}
	/* Clean up child process. */
	ret = finish_command(&child_test->process);
	if (verbose == 1 && ret == TEST_FAIL) {
		/* Add header for test that was skipped above. */
		if (has_subtests(t))
			pr_info("%3d.%1d: %s:\n", i + 1, subi + 1, test_description(t, subi));
		else
			pr_info("%3d: %s:\n", i + 1, test_description(t, -1));
		fprintf(stderr, "%s", err_output.buf);
	}
	strbuf_release(&err_output);
	print_test_result(t, i, subi, ret, width);
	if (err > 0)
		close(err);
	return 0;
}

static int start_test(struct test_suite *test, int i, int subi, struct child_test **child,
		      int width)
{
	int err;

	*child = NULL;
	if (dont_fork) {
		pr_debug("--- start ---\n");
		err = test_function(test, subi)(test, subi);
		pr_debug("---- end ----\n");
		print_test_result(test, i, subi, err, width);
		return 0;
	}

	*child = zalloc(sizeof(**child));
	if (!*child)
		return -ENOMEM;

	(*child)->test = test;
	(*child)->test_num = i;
	(*child)->subtest = subi;
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
	err = start_command(&(*child)->process);
	if (err || parallel)
		return  err;
	return finish_test(*child, width);
}

#define for_each_test(j, k, t)					\
	for (j = 0, k = 0; j < ARRAY_SIZE(tests); j++, k = 0)	\
		while ((t = tests[j][k++]) != NULL)

static int __cmd_test(int argc, const char *argv[], struct intlist *skiplist)
{
	struct test_suite *t;
	unsigned int j, k;
	int i = 0;
	int width = 0;
	size_t num_tests = 0;
	struct child_test **child_tests;
	int child_test_num = 0;

	for_each_test(j, k, t) {
		int len = strlen(test_description(t, -1));

		if (width < len)
			width = len;

		if (has_subtests(t)) {
			for (int subi = 0, subn = num_subtests(t); subi < subn; subi++) {
				len = strlen(test_description(t, subi));
				if (width < len)
					width = len;
				num_tests++;
			}
		} else {
			num_tests++;
		}
	}
	child_tests = calloc(num_tests, sizeof(*child_tests));
	if (!child_tests)
		return -ENOMEM;

	for_each_test(j, k, t) {
		int curr = i++;

		if (!perf_test__matches(test_description(t, -1), curr, argc, argv)) {
			bool skip = true;

			for (int subi = 0, subn = num_subtests(t); subi < subn; subi++) {
				if (perf_test__matches(test_description(t, subi),
							curr, argc, argv))
					skip = false;
			}

			if (skip)
				continue;
		}

		if (intlist__find(skiplist, i)) {
			pr_info("%3d: %-*s:", curr + 1, width, test_description(t, -1));
			color_fprintf(stderr, PERF_COLOR_YELLOW, " Skip (user override)\n");
			continue;
		}

		if (!has_subtests(t)) {
			int err = start_test(t, curr, -1, &child_tests[child_test_num++], width);

			if (err) {
				/* TODO: if parallel waitpid the already forked children. */
				free(child_tests);
				return err;
			}
		} else {
			for (int subi = 0, subn = num_subtests(t); subi < subn; subi++) {
				int err;

				if (!perf_test__matches(test_description(t, subi),
							curr, argc, argv))
					continue;

				err = start_test(t, curr, subi, &child_tests[child_test_num++],
						 width);
				if (err)
					return err;
			}
		}
	}
	for (i = 0; i < child_test_num; i++) {
		if (parallel) {
			int ret  = finish_test(child_tests[i], width);

			if (ret)
				return ret;
		}
		free(child_tests[i]);
	}
	free(child_tests);
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

static int perf_test__config(const char *var, const char *value,
			     void *data __maybe_unused)
{
	if (!strcmp(var, "annotate.objdump"))
		test_objdump_path = value;

	return 0;
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
	OPT_BOOLEAN('p', "parallel", &parallel,
		    "Run the tests altogether in parallel"),
	OPT_STRING('w', "workload", &workload, "work", "workload to run for testing"),
	OPT_STRING(0, "dso", &dso_to_test, "dso", "dso to test"),
	OPT_STRING(0, "objdump", &test_objdump_path, "path",
		   "objdump binary to use for disassembly and annotations"),
	OPT_END()
	};
	const char * const test_subcommands[] = { "list", NULL };
	struct intlist *skiplist = NULL;
        int ret = hists__init();

        if (ret < 0)
                return ret;

	perf_config(perf_test__config, NULL);

	/* Unbuffered output */
	setvbuf(stdout, NULL, _IONBF, 0);

	tests[2] = create_script_test_suites();
	argc = parse_options_subcommand(argc, argv, test_options, test_subcommands, test_usage, 0);
	if (argc >= 1 && !strcmp(argv[0], "list"))
		return perf_test__list(argc - 1, argv + 1);

	if (workload)
		return run_workload(workload, argc, argv);

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

	return __cmd_test(argc, argv, skiplist);
}
