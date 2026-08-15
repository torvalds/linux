// SPDX-License-Identifier: GPL-2.0
/*
 * builtin-test.c
 *
 * Builtin regression testing command: ever growing number of sanity tests
 */
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#ifdef HAVE_BACKTRACE_SUPPORT
#include <execinfo.h>
#endif
#include <poll.h>
#include <unistd.h>
#include <setjmp.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include "util/term.h"
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

static const char *junit_filename;
static struct strbuf junit_xml_buf = STRBUF_INIT;

/*
 * Command line option to not fork the test running in the same process and
 * making them easier to debug.
 */
static bool dont_fork;
/* Fork the tests in parallel and wait for their completion. */
static bool sequential;
/* Number of times each test is run. */
static unsigned int runs_per_test = 1;
/* Number of lines to include in failure snippet. */
static unsigned int failure_snippet_lines = 10;
const char *dso_to_test;
const char *test_objdump_path = "objdump";
static const char *workload_control;

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
	&suite__uncore_event_sorting,
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
	&suite__maps,
	&suite__demangle_java,
	&suite__demangle_ocaml,
	&suite__demangle_rust,
	&suite__parse_metric,
	&suite__pe_file_parsing,
	&suite__expand_cgroup_events,
	&suite__perf_time_to_tsc,
	&suite__dlfilter,
	&suite__sigtrap,
	&suite__event_groups,
	&suite__symbols,
	&suite__util,
	&suite__subcmd_help,
	&suite__kallsyms_split,
	NULL,
};

static struct test_workload *workloads[] = {
	&workload__noploop,
	&workload__thloop,
	&workload__named_threads,
	&workload__leafloop,
	&workload__sqrtloop,
	&workload__brstack,
	&workload__datasym,
	&workload__landlock,
	&workload__traploop,
	&workload__inlineloop,
	&workload__jitdump,
	&workload__context_switch_loop,
	&workload__deterministic,

#ifdef HAVE_RUST_SUPPORT
	&workload__code_with_type,
#endif
};

struct workload_control {
	int ctl_fd;
	int ack_fd;
};

#define workloads__for_each(workload) \
	for (unsigned i = 0; i < ARRAY_SIZE(workloads) && ({ workload = workloads[i]; 1; }); i++)

#define test_suite__for_each_test_case(suite, idx)			\
	for (idx = 0; (suite)->test_cases && (suite)->test_cases[idx].name != NULL; idx++)

static void close_parent_fds(void)
{
	DIR *dir = opendir("/proc/self/fd");
	struct dirent *ent;

	while ((ent = readdir(dir))) {
		char *end;
		long fd;

		if (ent->d_type != DT_LNK)
			continue;

		if (!isdigit(ent->d_name[0]))
			continue;

		fd = strtol(ent->d_name, &end, 10);
		if (*end)
			continue;

		if (fd <= 3 || fd == dirfd(dir))
			continue;

		close(fd);
	}
	closedir(dir);
}

static void check_leaks(void)
{
	DIR *dir = opendir("/proc/self/fd");
	struct dirent *ent;
	int leaks = 0;

	while ((ent = readdir(dir))) {
		char path[PATH_MAX];
		char *end;
		long fd;
		ssize_t len;

		if (ent->d_type != DT_LNK)
			continue;

		if (!isdigit(ent->d_name[0]))
			continue;

		fd = strtol(ent->d_name, &end, 10);
		if (*end)
			continue;

		if (fd <= 3 || fd == dirfd(dir))
			continue;

		leaks++;
		len = readlinkat(dirfd(dir), ent->d_name, path, sizeof(path));
		if (len > 0 && (size_t)len < sizeof(path))
			path[len] = '\0';
		else
			strncpy(path, ent->d_name, sizeof(path));
		pr_err("Leak of file descriptor %s that opened: '%s'\n", ent->d_name, path);
	}
	closedir(dir);
	if (leaks)
		abort();
}

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
	struct strbuf err_output;
	int result;
	bool done;
	struct timespec start_time;
	struct timespec end_time;
};

static jmp_buf run_test_jmp_buf;

static void child_test_sig_handler(int sig)
{
#ifdef HAVE_BACKTRACE_SUPPORT
	void *stackdump[32];
	size_t stackdump_size;
#endif

	fprintf(stderr, "\n---- unexpected signal (%d) ----\n", sig);
#ifdef HAVE_BACKTRACE_SUPPORT
	stackdump_size = backtrace(stackdump, ARRAY_SIZE(stackdump));
	__dump_stack(stderr, stackdump, stackdump_size);
#endif
	siglongjmp(run_test_jmp_buf, sig);
}

static int run_test_child(struct child_process *process)
{
	const int signals[] = {
		SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGINT, SIGPIPE, SIGQUIT, SIGSEGV, SIGTERM,
	};
	struct child_test *child = container_of(process, struct child_test, process);
	int err;

	close_parent_fds();

	err = sigsetjmp(run_test_jmp_buf, 1);
	if (err) {
		/* Received signal. */
		err = err > 0 ? -err : -1;
		goto err_out;
	}

	for (size_t i = 0; i < ARRAY_SIZE(signals); i++)
		signal(signals[i], child_test_sig_handler);

	pr_debug("---- start ----\n");
	pr_debug("test child forked, pid %d\n", getpid());
	err = test_function(child->test, child->test_case_num)(child->test, child->test_case_num);
	pr_debug("---- end(%d) ----\n", err);

	check_leaks();
err_out:
	fflush(NULL);
	for (size_t i = 0; i < ARRAY_SIZE(signals); i++)
		signal(signals[i], SIG_DFL);
	return -err;
}

#define TEST_RUNNING -3

static struct pollfd *global_pfds;
static size_t *global_pfd_indices;
static unsigned int summary_tests_passed;
static unsigned int summary_subtests_passed;
static unsigned int summary_tests_skipped;
static unsigned int summary_tests_failed;
static struct strbuf summary_failed_tests_buf = STRBUF_INIT;

static int strbuf_addstr_safe(struct strbuf *sb, const char *s);
static int __printf(2, 3) strbuf_addf_safe(struct strbuf *sb, const char *fmt, ...);

static char *xml_escape(const char *str)
{
	struct strbuf buf = STRBUF_INIT;
	const char *p;
	char *res;

	if (!str)
		return strdup("");

	for (p = str; *p; p++) {
		if (*p == '&')
			strbuf_addstr(&buf, "&amp;");
		else if (*p == '<')
			strbuf_addstr(&buf, "&lt;");
		else if (*p == '>')
			strbuf_addstr(&buf, "&gt;");
		else if (*p == '"')
			strbuf_addstr(&buf, "&quot;");
		else if ((unsigned char)*p >= 32 || *p == '\n' || *p == '\t')
			strbuf_addch(&buf, *p);
	}
	res = strbuf_detach(&buf, NULL);
	return res ? res : strdup("");
}

static const char *format_test_description(const char *desc, int max_desc_width,
					  char *buf, size_t buf_sz)
{
	int len = strlen(desc);

	/*
	 * Clamp to buf_sz to prevent GCC format-truncation warnings
	 * when terminal width is very large.
	 */
	if (max_desc_width >= (int)buf_sz)
		max_desc_width = buf_sz - 1;

	if (len > max_desc_width) {
		snprintf(buf, buf_sz, "%.*s...", max_desc_width - 3, desc);
		return buf;
	}
	return desc;
}

static int print_test_result(struct test_suite *t, int curr_suite, int curr_test_case,
			     int result, int width, int running,
			     const char *err_output, double elapsed)
{
	char desc_buf[256];
	const char *desc = test_description(t, curr_test_case);
	struct winsize ws;
	int max_desc_area_width;
	int target_desc_area_width;
	int desc_padding;

	get_term_dimensions(&ws);
	/*
	 * Total terminal columns minus space for status e.g. " Running (12 active)"
	 * which is 20 chars, plus a margin of 3 chars = 23 chars.
	 */
	max_desc_area_width = ws.ws_col - 23;
	if (max_desc_area_width < 40)
		max_desc_area_width = 40;

	/* Standard test has prefix "%3d: " which is 5 chars */
	target_desc_area_width = width + 5;
	if (target_desc_area_width > max_desc_area_width)
		target_desc_area_width = max_desc_area_width;

	if (test_suite__num_test_cases(t) > 1) {
		char prefix[32];
		int len = snprintf(prefix, sizeof(prefix), "%3d.%1d:",
				   curr_suite + 1, curr_test_case + 1);

		desc_padding = target_desc_area_width - (len + 1);
		if (desc_padding < 20)
			desc_padding = 20;

		desc = format_test_description(desc, desc_padding, desc_buf, sizeof(desc_buf));
		pr_info("%s %-*s:", prefix, desc_padding, desc);
	} else {
		desc_padding = target_desc_area_width - 5;
		if (desc_padding < 20)
			desc_padding = 20;

		desc = format_test_description(desc, desc_padding, desc_buf, sizeof(desc_buf));
		pr_info("%3d: %-*s:", curr_suite + 1, desc_padding, desc);
	}

	switch (result) {
	case TEST_RUNNING:
		color_fprintf(stderr, PERF_COLOR_YELLOW, " Running (%d active)\n", running);
		break;
	case TEST_OK:
		if (test_suite__num_test_cases(t) > 1)
			summary_subtests_passed++;
		else
			summary_tests_passed++;
		pr_info(" Ok\n");
		break;
	case TEST_SKIP: {
		const char *reason = skip_reason(t, curr_test_case);

		summary_tests_skipped++;
		if (reason)
			color_fprintf(stderr, PERF_COLOR_YELLOW, " Skip (%s)\n", reason);
		else
			color_fprintf(stderr, PERF_COLOR_YELLOW, " Skip\n");
	}
		break;
	case TEST_FAIL:
	default:
		summary_tests_failed++;
		if (test_suite__num_test_cases(t) > 1)
			strbuf_addf_safe(&summary_failed_tests_buf, "  %3d.%1d: %s\n",
				    curr_suite + 1, curr_test_case + 1,
				    test_description(t, curr_test_case));
		else
			strbuf_addf_safe(&summary_failed_tests_buf, "  %3d: %s\n",
				    curr_suite + 1,
				    test_description(t, curr_test_case));
		color_fprintf(stderr, PERF_COLOR_RED, " FAILED!\n");
		break;
	}

	if (junit_filename && result != TEST_RUNNING) {
		const char *classname = t->desc;
		const char *testname = test_description(t, curr_test_case);
		char *escaped_err = xml_escape(err_output);
		char *escaped_class = xml_escape(classname);
		char *escaped_test = xml_escape(testname);

		strbuf_addf(&junit_xml_buf,
			    "    <testcase classname=\"%s\" name=\"%s\" time=\"%.2f\">\n",
			    escaped_class, escaped_test, elapsed);
		if (result != TEST_OK && result != TEST_SKIP) {
			strbuf_addf(&junit_xml_buf,
				    "      <failure message=\"FAILED\">\n%s\n      </failure>\n",
				    escaped_err);
		} else if (result == TEST_SKIP) {
			const char *reason = skip_reason(t, curr_test_case);
			char *escaped_reason = xml_escape(reason ? reason : "Skip");

			strbuf_addf(&junit_xml_buf, "      <skipped message=\"%s\"/>\n",
				    escaped_reason);
			free(escaped_reason);
		}
		strbuf_addstr(&junit_xml_buf, "    </testcase>\n");
		free(escaped_err);
		free(escaped_class);
		free(escaped_test);
	}

	return 0;
}

static const char * const fail_keywords[] = {
	"error", "fail", "segv", "abort",
	"signal", "fatal", "panic", "corrupt", NULL
};

static const char *find_next_keyword(const char *str, size_t max_len, size_t *kw_len)
{
	const char *best = NULL;
	size_t best_len = 0;
	int k;

	for (k = 0; fail_keywords[k]; k++) {
		const char *s = str;
		size_t len = strlen(fail_keywords[k]);

		while ((size_t)(s - str) + len <= max_len) {
			size_t i;

			if (best && s >= best)
				break;

			for (i = 0; i < len; i++) {
				if (tolower(s[i]) != fail_keywords[k][i])
					break;
			}
			if (i == len) {
				if (!best || s < best) {
					best = s;
					best_len = len;
				}
				break;
			}
			s++;
		}
	}
	if (best) {
		*kw_len = best_len;
		return best;
	}
	return NULL;
}

static void print_line_highlighted(FILE *fp, const char *line, size_t len)
{
	const char *s = line;

	while (len > 0) {
		size_t kw_len = 0;
		const char *match = find_next_keyword(s, len, &kw_len);

		if (!match) {
			fwrite(s, 1, len, fp);
			break;
		}
		if (match > s)
			fwrite(s, 1, match - s, fp);
		if (perf_use_color_default)
			fprintf(fp, "%s", PERF_COLOR_RED);
		fwrite(match, 1, kw_len, fp);
		if (perf_use_color_default)
			fprintf(fp, "%s", PERF_COLOR_RESET);

		len -= (match + kw_len) - s;
		s = match + kw_len;
	}
}


static void print_test_failure_snippet(FILE *fp, const char *buf)
{
	size_t num_lines = 0;
	size_t max_lines = 128;
	const char **lines = calloc(max_lines, sizeof(const char *));
	size_t *line_lens = calloc(max_lines, sizeof(size_t));
	const char *s = buf;
	size_t i;
	unsigned int picked_count = 0;
	bool *pick;
	int last_printed = -1;

	if (!lines || !line_lens) {
		free(lines); free(line_lens);
		fprintf(fp, "%s", buf);
		return;
	}

	while (*s) {
		const char *eol = strchr(s, '\n');
		size_t len;

		if (eol)
			len = eol - s + 1;
		else
			len = strlen(s);

		if (num_lines == max_lines) {
			const char **new_lines;
			size_t *new_lens;

			max_lines *= 2;
			new_lines = realloc(lines, max_lines * sizeof(const char *));
			if (!new_lines) {
				free(lines); free(line_lens);
				fprintf(fp, "%s", buf);
				return;
			}
			lines = new_lines;

			new_lens = realloc(line_lens, max_lines * sizeof(size_t));
			if (!new_lens) {
				free(lines); free(line_lens);
				fprintf(fp, "%s", buf);
				return;
			}
			line_lens = new_lens;
		}
		lines[num_lines] = s;
		line_lens[num_lines] = len;
		num_lines++;
		s += len;
	}

	if (num_lines <= failure_snippet_lines) {
		for (i = 0; i < num_lines; i++)
			print_line_highlighted(fp, lines[i], line_lens[i]);
		free(lines); free(line_lens);
		return;
	}

	pick = calloc(num_lines, sizeof(bool));
	if (!pick) {
		for (i = 0; i < num_lines; i++)
			print_line_highlighted(fp, lines[i], line_lens[i]);
		free(lines); free(line_lens);
		return;
	}

	/* Pass 0: Always pick the very first line */
	if (num_lines > 0 && picked_count < failure_snippet_lines) {
		pick[0] = true;
		picked_count++;
	}

	/* Pass 1: Pick lines with failure keywords from start (Highest Priority) */
	for (i = 0; i < num_lines && picked_count < failure_snippet_lines; i++) {
		size_t dummy;

		if (find_next_keyword(lines[i], line_lens[i], &dummy)) {
			if (!pick[i]) {
				pick[i] = true;
				picked_count++;
			}
			/* Prioritize getting the immediate next line for context */
			if (i + 1 < num_lines && !pick[i + 1] &&
			    picked_count < failure_snippet_lines) {
				pick[i + 1] = true;
				picked_count++;
			}
		}
	}

	/* Pass 2: Fill remaining quota from the end backwards */
	i = num_lines;
	while (i > 0 && picked_count < failure_snippet_lines) {
		i--;
		if (!pick[i]) {
			pick[i] = true;
			picked_count++;
		}
	}

	for (i = 0; i < num_lines; i++) {
		if (!pick[i])
			continue;
		if (last_printed != -1 && (int)i > last_printed + 1) {
			if (perf_use_color_default)
				fprintf(fp, "%s...%s\n", PERF_COLOR_BLUE, PERF_COLOR_RESET);
			else
				fprintf(fp, "...\n");
		}
		print_line_highlighted(fp, lines[i], line_lens[i]);
		last_printed = i;
	}

	free(pick);
	free(lines);
	free(line_lens);
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
	struct timespec end_time;
	double elapsed;

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
		pr_info("%3d: %s:\n", curr_suite + 1, test_description(t, -1));

	/*
	 * Busy loop reading from the child's stdout/stderr that are set to be
	 * non-blocking until EOF.
	 */
	if (err >= 0)
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
						  width, running, NULL, 0.0);
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
					strbuf_addstr_safe(&err_output, buf);
				}
			}
		}
		if (err_done)
			err_done = check_if_command_finished(&child_test->process);
	}
	/* Drain any remaining data from the pipe. */
	if (err >= 0) {
		char buf[512];
		ssize_t len;

		while ((len = read(err, buf, sizeof(buf) - 1)) > 0) {
			buf[len] = '\0';
			strbuf_addstr_safe(&err_output, buf);
		}
	}
	if (perf_use_color_default && last_running != -1) {
		/* Erase "Running (.. active)" line printed before poll/sleep. */
		fprintf(debug_file(), PERF_COLOR_DELETE_LINE);
	}
	/* Clean up child process. */
	ret = finish_command(&child_test->process);
	child_test->process.pid = 0;
	if (child_test->err_output.len > 0) {
		struct strbuf merged = STRBUF_INIT;

		if (child_test->err_output.buf)
			strbuf_addstr_safe(&merged, child_test->err_output.buf);
		if (err_output.buf)
			strbuf_addstr_safe(&merged, err_output.buf);
		strbuf_release(&err_output);
		err_output = merged;
	}
	if (verbose > 1)
		fprintf(stderr, "%s", err_output.buf);
	else if (verbose == 1 && ret == TEST_FAIL)
		print_test_failure_snippet(stderr, err_output.buf);

	clock_gettime(CLOCK_MONOTONIC, &end_time);
	elapsed = (end_time.tv_sec - child_test->start_time.tv_sec) +
		  (end_time.tv_nsec - child_test->start_time.tv_nsec) / 1000000000.0;

	print_test_result(t, curr_suite, curr_test_case, ret, width, /*running=*/0,
			  err_output.buf, elapsed);
	strbuf_release(&err_output);
	strbuf_release(&child_test->err_output);
	if (err > 0)
		close(err);
	zfree(&child_tests[running_test]);
}

static int strbuf_addstr_safe(struct strbuf *sb, const char *s)
{
	sigset_t set, oldset;
	int ret;

	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	sigaddset(&set, SIGTERM);
	pthread_sigmask(SIG_BLOCK, &set, &oldset);
	ret = strbuf_addstr(sb, s);
	pthread_sigmask(SIG_SETMASK, &oldset, NULL);
	return ret;
}

static int __printf(2, 3) strbuf_addf_safe(struct strbuf *sb, const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	int len;
	sigset_t set, oldset;
	int ret;

	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	sigaddset(&set, SIGTERM);
	sigprocmask(SIG_BLOCK, &set, &oldset);

	va_start(ap, fmt);
	len = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	if (len < 0) {
		sigprocmask(SIG_SETMASK, &oldset, NULL);
		return len;
	}
	if ((size_t)len >= sizeof(buf)) {
		char *dynamic_buf = malloc(len + 1);

		if (!dynamic_buf) {
			sigprocmask(SIG_SETMASK, &oldset, NULL);
			return -ENOMEM;
		}
		va_start(ap, fmt);
		vsnprintf(dynamic_buf, len + 1, fmt, ap);
		va_end(ap);
		ret = strbuf_addstr(sb, dynamic_buf);
		free(dynamic_buf);
	} else {
		ret = strbuf_addstr(sb, buf);
	}

	sigprocmask(SIG_SETMASK, &oldset, NULL);
	return ret;
}

static void drain_child_process_err(struct child_test *child)
{
	char buf[512];
	ssize_t len;

	while ((len = read(child->process.err, buf, sizeof(buf) - 1)) > 0) {
		buf[len] = '\0';
		strbuf_addstr_safe(&child->err_output, buf);
	}
}

static void handle_child_pipe_activity(struct child_test *child, short revents)
{
	if (!revents)
		return;

	drain_child_process_err(child);
	/*
	 * If the child closed its end of the pipe (EOF) or encountered
	 * an error, close the file descriptor immediately and set it
	 * to -1. This removes it from the pfds array for subsequent
	 * iterations, preventing a tight CPU busy-loop while waiting
	 * for the process itself to exit.
	 */
	if (revents & (POLLHUP | POLLERR | POLLNVAL)) {
		close(child->process.err);
		child->process.err = -1;
	}
}

static int finish_tests_parallel(struct child_test **child_tests, size_t num_tests, int width)
{
	size_t next_to_print = 0;
	struct pollfd *pfds;
	size_t *pfd_indices;
	size_t num_pfds = 0;
	int last_running = -1;
	size_t i;
	int last_suite_printed = -1;
	sigset_t set, oldset;

	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	sigaddset(&set, SIGTERM);

	pthread_sigmask(SIG_BLOCK, &set, &oldset);
	global_pfds = calloc(num_tests, sizeof(*pfds));
	global_pfd_indices = calloc(num_tests, sizeof(*pfd_indices));
	pfds = global_pfds;
	pfd_indices = global_pfd_indices;
	if (!pfds || !pfd_indices) {
		free(pfds);
		free(pfd_indices);
		global_pfds = NULL;
		global_pfd_indices = NULL;
		pthread_sigmask(SIG_SETMASK, &oldset, NULL);
		return -ENOMEM;
	}
	pthread_sigmask(SIG_SETMASK, &oldset, NULL);

	for (i = 0; i < num_tests; i++) {
		struct child_test *child = child_tests[i];

		if (!child)
			continue;
		strbuf_init(&child->err_output, 0);
		if (child->process.err >= 0)
			fcntl(child->process.err, F_SETFL, O_NONBLOCK);
	}

	while (next_to_print < num_tests) {
		size_t running_count = 0;
		size_t p;

		while (next_to_print < num_tests &&
		       (!child_tests[next_to_print] || child_tests[next_to_print]->done))
			next_to_print++;

		if (next_to_print >= num_tests)
			break;

		num_pfds = 0;

		for (i = next_to_print; i < num_tests; i++) {
			struct child_test *child = child_tests[i];

			if (!child || child->done)
				continue;

			if (!check_if_command_finished(&child->process))
				running_count++;

			if (child->process.err >= 0) {
				pfds[num_pfds].fd = child->process.err;
				pfds[num_pfds].events = POLLIN | POLLERR | POLLHUP | POLLNVAL;
				pfd_indices[num_pfds] = i;
				num_pfds++;
			}
		}

		if (perf_use_color_default && running_count != (size_t)last_running) {
			struct child_test *next_child = child_tests[next_to_print];

			if (last_running != -1)
				fprintf(debug_file(), PERF_COLOR_DELETE_LINE);

			if (next_child) {
				if (test_suite__num_test_cases(next_child->test) > 1 &&
				    last_suite_printed != next_child->suite_num) {
					pr_info("%3d: %s:\n", next_child->suite_num + 1,
						test_description(next_child->test, -1));
					last_suite_printed = next_child->suite_num;
				}
				print_test_result(next_child->test, next_child->suite_num,
						  next_child->test_case_num, TEST_RUNNING, width,
						  running_count, NULL, 0.0);
			}
			last_running = running_count;
		}

		if (num_pfds == 0) {
			if (running_count > 0)
				usleep(10 * 1000);
		} else {
			int pret = poll(pfds, num_pfds, 100);

			if (pret > 0) {
				for (p = 0; p < num_pfds; p++) {
					size_t idx = pfd_indices[p];

					handle_child_pipe_activity(child_tests[idx],
								   pfds[p].revents);
				}
			}
		}

		for (i = next_to_print; i < num_tests; i++) {
			struct child_test *child = child_tests[i];

			if (!child || child->done)
				continue;

			if (check_if_command_finished(&child->process)) {
				if (child->process.err >= 0) {
					drain_child_process_err(child);
					close(child->process.err);
					child->process.err = -1;
				}
				child->result = finish_command(&child->process);
				child->process.pid = 0;
				clock_gettime(CLOCK_MONOTONIC, &child->end_time);
				child->done = true;
			}
		}

		while (next_to_print < num_tests) {
			struct child_test *child = child_tests[next_to_print];
			double elapsed;

			if (!child) {
				next_to_print++;
				continue;
			}
			if (!child->done)
				break;

			if (perf_use_color_default && last_running != -1) {
				fprintf(debug_file(), PERF_COLOR_DELETE_LINE);
				last_running = -1;
			}

			if (test_suite__num_test_cases(child->test) > 1 &&
			    last_suite_printed != child->suite_num) {
				pr_info("%3d: %s:\n", child->suite_num + 1,
					test_description(child->test, -1));
				last_suite_printed = child->suite_num;
			}

			if (verbose > 1) {
				if (test_suite__num_test_cases(child->test) > 1) {
					pr_info("%3d.%1d: %s:\n", child->suite_num + 1,
						child->test_case_num + 1,
						test_description(child->test,
								 child->test_case_num));
				} else {
					pr_info("%3d: %s:\n", child->suite_num + 1,
						test_description(child->test, -1));
				}
			}

			if (verbose > 1)
				fprintf(stderr, "%s", child->err_output.buf);
			else if (verbose == 1 && child->result == TEST_FAIL)
				print_test_failure_snippet(stderr, child->err_output.buf);

			elapsed = (child->end_time.tv_sec - child->start_time.tv_sec) +
				  (child->end_time.tv_nsec -
				   child->start_time.tv_nsec) / 1000000000.0;

			print_test_result(child->test, child->suite_num, child->test_case_num,
					  child->result, width, 0, child->err_output.buf, elapsed);
			pthread_sigmask(SIG_BLOCK, &set, &oldset);
			strbuf_release(&child->err_output);
			child_tests[next_to_print] = NULL;
			zfree(&child);
			pthread_sigmask(SIG_SETMASK, &oldset, NULL);
			next_to_print++;
		}
	}

	pthread_sigmask(SIG_BLOCK, &set, &oldset);
	free(global_pfds);
	free(global_pfd_indices);
	global_pfds = NULL;
	global_pfd_indices = NULL;
	pthread_sigmask(SIG_SETMASK, &oldset, NULL);
	return 0;
}

static int start_test(struct test_suite *test, int curr_suite, int curr_test_case,
		struct child_test **child, int width, int pass)
{
	int err;

	*child = NULL;
	if (dont_fork) {
		if (pass == 1) {
			struct timespec start_time, end_time;
			double elapsed;

			clock_gettime(CLOCK_MONOTONIC, &start_time);
			pr_debug("--- start ---\n");
			err = test_function(test, curr_test_case)(test, curr_test_case);
			pr_debug("---- end ----\n");
			clock_gettime(CLOCK_MONOTONIC, &end_time);
			elapsed = (end_time.tv_sec - start_time.tv_sec) +
				  (end_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;
			print_test_result(test, curr_suite, curr_test_case, err, width,
					  /*running=*/0, NULL, elapsed);
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
	(*child)->process.in = -1;
	(*child)->process.out = -1;
	(*child)->process.err = -1;
	if (verbose <= 0) {
		(*child)->process.no_stdout = 1;
		(*child)->process.no_stderr = 1;
	} else {
		(*child)->process.stdout_to_stderr = 1;
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

static void print_tests_summary(void)
{
	pr_info("\n=== Test Summary ===\n");
	pr_info("Passed main tests : %u\n", summary_tests_passed);
	pr_info("Passed subtests   : %u\n", summary_subtests_passed);
	pr_info("Skipped tests     : %u\n", summary_tests_skipped);
	if (summary_tests_failed > 0) {
		color_fprintf(stderr, PERF_COLOR_RED, "Failed tests      : %u\n",
			      summary_tests_failed);
		pr_info("List of failed tests:\n");
		pr_info("%s", summary_failed_tests_buf.buf);
	} else {
		color_fprintf(stderr, PERF_COLOR_GREEN, "Failed tests      : 0\n");
	}

	if (junit_filename) {
		int fd;
		FILE *fp;

		fd = open(junit_filename, O_CREAT | O_TRUNC | O_WRONLY | O_NOFOLLOW, 0644);
		if (fd >= 0) {
			fp = fdopen(fd, "w");
			if (fp) {
				unsigned int total = summary_tests_passed +
						     summary_subtests_passed +
						     summary_tests_skipped +
						     summary_tests_failed;
				fprintf(fp, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
				fprintf(fp, "<testsuites>\n");
				fprintf(fp,
					"  <testsuite name=\"perf-tests\" tests=\"%u\" failures=\"%u\" skipped=\"%u\">\n",
					total, summary_tests_failed,
					summary_tests_skipped);
				fprintf(fp, "%s", junit_xml_buf.buf);
				fprintf(fp, "  </testsuite>\n");
				fprintf(fp, "</testsuites>\n");
				fclose(fp);
				pr_info("Wrote junit XML output to %s\n", junit_filename);
			} else {
				close(fd);
				pr_err("Failed to associate stream with fd for %s: %s\n",
				       junit_filename, strerror(errno));
			}
		} else {
			pr_err("Failed to open %s for writing junit XML output: %s\n",
			       junit_filename, strerror(errno));
		}
	}
	strbuf_release(&junit_xml_buf);
	strbuf_release(&summary_failed_tests_buf);
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
			bool suite_matched = false;

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
			} else {
				suite_matched = true;
			}

			if (intlist__find(skiplist, curr_suite + 1)) {
				if (pass == 1) {
					pr_info("%3d: %-*s:", curr_suite + 1, width,
						test_description(*t, -1));
					color_fprintf(stderr, PERF_COLOR_YELLOW,
						      " Skip (user override)\n");
					summary_tests_skipped++;
					if (junit_filename) {
						char *escaped_class =
							xml_escape((const char *)
								   test_description(*t, -1));
						char *escaped_test = xml_escape("override");
						char *escaped_reason =
							xml_escape("user override");

						strbuf_addf(&junit_xml_buf,
							"    <testcase classname=\"%s\" name=\"%s\" time=\"0.000\">\n",
							escaped_class, escaped_test);
						strbuf_addf(&junit_xml_buf,
							"      <skipped message=\"%s\"/>\n",
							escaped_reason);
						strbuf_addstr(&junit_xml_buf, "    </testcase>\n");
						free(escaped_reason);
						free(escaped_test);
						free(escaped_class);
					}
				}
				continue;
			}

			for (unsigned int run = 0; run < runs_per_test; run++) {
				test_suite__for_each_test_case(*t, curr_test_case) {
					if (!suite_matched &&
					    !perf_test__matches(test_description(*t, curr_test_case),
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
			err = finish_tests_parallel(child_tests, num_tests, width);
			if (err)
				goto err_out;
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
	print_tests_summary();
	free(global_pfds);
	free(global_pfd_indices);
	global_pfds = NULL;
	global_pfd_indices = NULL;
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

static int perf_control_open_fifo(struct workload_control *ctl, const char *str)
{
	char *s, *p;
	int ret;

	if (strncmp(str, "fifo:", 5))
		return -EINVAL;

	str += 5;
	if (!*str || *str == ',')
		return -EINVAL;

	s = strdup(str);
	if (!s)
		return -ENOMEM;

	p = strchr(s, ',');
	if (p)
		*p = '\0';

	ctl->ctl_fd = open(s, O_WRONLY | O_CLOEXEC);
	if (ctl->ctl_fd < 0) {
		ret = -errno;
		pr_err("Failed to open workload control FIFO '%s': %m\n", s);
		free(s);
		return ret;
	}

	if (p && *++p) {
		ctl->ack_fd = open(p, O_RDONLY | O_CLOEXEC);
		if (ctl->ack_fd < 0) {
			ret = -errno;
			pr_err("Failed to open workload control ack FIFO '%s': %m\n", p);
			close(ctl->ctl_fd);
			ctl->ctl_fd = -1;
			free(s);
			return ret;
		}
	}

	free(s);
	return 0;
}

static int perf_control_open(struct workload_control *ctl)
{
	int ret;

	if (!workload_control)
		return 0;

	ret = perf_control_open_fifo(ctl, workload_control);

	if (ret == -EINVAL) {
		pr_err("Unsupported workload control spec '%s', expected fifo:ctl-fifo[,ack-fifo]\n",
			workload_control);
	}

	return ret;
}

static void perf_control_close(struct workload_control *ctl)
{
	if (ctl->ctl_fd >= 0) {
		close(ctl->ctl_fd);
		ctl->ctl_fd = -1;
	}
	if (ctl->ack_fd >= 0) {
		close(ctl->ack_fd);
		ctl->ack_fd = -1;
	}
}

static int perf_control_write_cmd(int fd, const char *cmd)
{
	size_t len = strlen(cmd);
	ssize_t ret;

	while (len) {
		ret = write(fd, cmd, len);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			pr_err("Failed to write perf control command: %m\n");
			return -1;
		}

		if (!ret) {
			pr_err("Failed to write perf control command: short write\n");
			return -1;
		}

		cmd += ret;
		len -= ret;
	}

	return 0;
}

static int perf_control_read_ack(int fd)
{
	char buf[16];
	ssize_t ret;

	do {
		ret = read(fd, buf, sizeof(buf) - 1);
	} while (ret < 0 && errno == EINTR);

	if (ret < 0) {
		pr_err("Failed to read perf control ack: %m\n");
		return -1;
	}

	if (!ret) {
		pr_err("Unexpected EOF while reading perf control ack\n");
		return -1;
	}

	buf[ret] = '\0';
	for (ssize_t i = 0; i < ret; i++) {
		if (buf[i] == '\n' || buf[i] == '\0') {
			buf[i] = '\0';
			break;
		}
	}

	if (strcmp(buf, "ack")) {
		pr_err("Unexpected perf control ack: %s\n", buf);
		return -1;
	}

	return 0;
}

static int perf_control_send(struct workload_control *ctl, const char *cmd)
{
	if (ctl->ctl_fd < 0)
		return 0;

	if (perf_control_write_cmd(ctl->ctl_fd, cmd))
		return -1;

	if (ctl->ack_fd >= 0 && perf_control_read_ack(ctl->ack_fd))
		return -1;

	return 0;
}

static int run_workload(const char *work, int argc, const char **argv)
{
	struct test_workload *twl;

	workloads__for_each(twl) {
		struct workload_control ctl = {
			.ctl_fd = -1,
			.ack_fd = -1,
		};
		int control_ret, ret;

		if (strcmp(twl->name, work))
			continue;

		ret = perf_control_open(&ctl);
		if (ret)
			return ret;

		if (perf_control_send(&ctl, "enable\n")) {
			perf_control_close(&ctl);
			return -1;
		}

		ret = twl->func(argc, argv);

		control_ret = perf_control_send(&ctl, "disable\n");
		perf_control_close(&ctl);
		if (control_ret)
			return -1;

		return ret;
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

	for_each_suite(t) {
		if (t->setup) {
			int ret = t->setup(t);

			if (ret < 0) {
				errno = -ret;
				return NULL;
			}
		}
		num_suites++;
	}

	result = calloc(num_suites + 1, sizeof(struct test_suite *));
	if (!result)
		return NULL;

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
	OPT_STRING(0, "record-ctl", &workload_control, "fifo:ctl-fifo[,ack-fifo]",
		   "Write enable to the fifo just before running the workload and disable after, with optional ack from ack-fifo"),
	OPT_BOOLEAN(0, "list-workloads", &list_workloads, "List the available builtin workloads to use with -w/--workload"),
	OPT_STRING(0, "dso", &dso_to_test, "dso", "dso to test"),
	OPT_STRING(0, "objdump", &test_objdump_path, "path",
		   "objdump binary to use for disassembly and annotations"),
	OPT_UINTEGER(0, "failure-snippet-lines", &failure_snippet_lines,
		     "Number of lines to include in failure snippet, default 10"),
	OPT_STRING_OPTARG('j', "junit", &junit_filename, "file",
			  "Generate junit XML output, default test.xml", "test.xml"),
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
		if (!suites)
			return errno ? -errno : -ENOMEM;
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
	if (!suites)
		return errno ? -errno : -ENOMEM;
	ret = __cmd_test(suites, argc, argv, skiplist);
	free(suites);
	return ret;
}
