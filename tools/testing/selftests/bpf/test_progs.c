// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2017 Facebook
 */
#define _GNU_SOURCE
#include "test_progs.h"
#include "testing_helpers.h"
#include "cgroup_helpers.h"
#include <argp.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <string.h>
#include <sys/sysinfo.h> /* get_nprocs */
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <linux/keyctl.h>
#include <sys/un.h>
#include <bpf/btf.h>
#include <time.h>
#include "json_writer.h"

#include "network_helpers.h"
#include "verification_cert.h"

/* backtrace() and backtrace_symbols_fd() are glibc specific,
 * use header file when glibc is available and provide stub
 * implementations when another libc implementation is used.
 */
#ifdef __GLIBC__
#include <execinfo.h> /* backtrace */
#else
__weak int backtrace(void **buffer, int size)
{
	return 0;
}

__weak void backtrace_symbols_fd(void *const *buffer, int size, int fd)
{
	dprintf(fd, "<backtrace not supported>\n");
}
#endif /*__GLIBC__ */

int env_verbosity = 0;

static bool verbose(void)
{
	return env.verbosity > VERBOSE_NONE;
}

static void stdio_hijack_init(char **log_buf, size_t *log_cnt)
{
#ifdef __GLIBC__
	if (verbose() && env.worker_id == -1) {
		/* nothing to do, output to stdout by default */
		return;
	}

	fflush(stdout);
	fflush(stderr);

	stdout = open_memstream(log_buf, log_cnt);
	if (!stdout) {
		stdout = env.stdout_saved;
		perror("open_memstream");
		return;
	}

	if (env.subtest_state)
		env.subtest_state->stdout_saved = stdout;
	else
		env.test_state->stdout_saved = stdout;

	stderr = stdout;
#endif
}

static void stdio_hijack(char **log_buf, size_t *log_cnt)
{
#ifdef __GLIBC__
	if (verbose() && env.worker_id == -1) {
		/* nothing to do, output to stdout by default */
		return;
	}

	env.stdout_saved = stdout;
	env.stderr_saved = stderr;

	stdio_hijack_init(log_buf, log_cnt);
#endif
}

static pthread_mutex_t stdout_lock = PTHREAD_MUTEX_INITIALIZER;

static void stdio_restore(void)
{
#ifdef __GLIBC__
	if (verbose() && env.worker_id == -1) {
		/* nothing to do, output to stdout by default */
		return;
	}

	fflush(stdout);

	pthread_mutex_lock(&stdout_lock);

	if (env.subtest_state) {
		if (env.subtest_state->stdout_saved)
			fclose(env.subtest_state->stdout_saved);
		env.subtest_state->stdout_saved = NULL;
		stdout = env.test_state->stdout_saved;
		stderr = env.test_state->stdout_saved;
	} else {
		if (env.test_state->stdout_saved)
			fclose(env.test_state->stdout_saved);
		env.test_state->stdout_saved = NULL;
		stdout = env.stdout_saved;
		stderr = env.stderr_saved;
	}

	pthread_mutex_unlock(&stdout_lock);
#endif
}

static int traffic_monitor_print_fn(const char *format, va_list args)
{
	pthread_mutex_lock(&stdout_lock);
	vfprintf(stdout, format, args);
	pthread_mutex_unlock(&stdout_lock);

	return 0;
}

/* Adapted from perf/util/string.c */
static bool glob_match(const char *str, const char *pat)
{
	while (*str && *pat && *pat != '*') {
		if (*str != *pat)
			return false;
		str++;
		pat++;
	}
	/* Check wild card */
	if (*pat == '*') {
		while (*pat == '*')
			pat++;
		if (!*pat) /* Tail wild card matches all */
			return true;
		while (*str)
			if (glob_match(str++, pat))
				return true;
	}
	return !*str && !*pat;
}

#define EXIT_NO_TEST		2
#define EXIT_ERR_SETUP_INFRA	3

/* defined in test_progs.h */
struct test_env env = {};

struct prog_test_def {
	const char *test_name;
	int test_num;
	void (*run_test)(void);
	void (*run_serial_test)(void);
	bool should_run;
	bool need_cgroup_cleanup;
	bool should_tmon;
};

/* Override C runtime library's usleep() implementation to ensure nanosleep()
 * is always called. Usleep is frequently used in selftests as a way to
 * trigger kprobe and tracepoints.
 */
int usleep(useconds_t usec)
{
	struct timespec ts = {
		.tv_sec = usec / 1000000,
		.tv_nsec = (usec % 1000000) * 1000,
	};

	return syscall(__NR_nanosleep, &ts, NULL);
}

/* Watchdog timer is started by watchdog_start() and stopped by watchdog_stop().
 * If timer is active for longer than env.secs_till_notify,
 * it prints the name of the current test to the stderr.
 * If timer is active for longer than env.secs_till_kill,
 * it kills the thread executing the test by sending a SIGSEGV signal to it.
 */
static void watchdog_timer_func(union sigval sigval)
{
	struct itimerspec timeout = {};
	char test_name[256];
	int err;

	if (env.subtest_state)
		snprintf(test_name, sizeof(test_name), "%s/%s",
			 env.test->test_name, env.subtest_state->name);
	else
		snprintf(test_name, sizeof(test_name), "%s",
			 env.test->test_name);

	switch (env.watchdog_state) {
	case WD_NOTIFY:
		fprintf(env.stderr_saved, "WATCHDOG: test case %s executes for %d seconds...\n",
			test_name, env.secs_till_notify);
		timeout.it_value.tv_sec = env.secs_till_kill - env.secs_till_notify;
		env.watchdog_state = WD_KILL;
		err = timer_settime(env.watchdog, 0, &timeout, NULL);
		if (err)
			fprintf(env.stderr_saved, "Failed to arm watchdog timer\n");
		break;
	case WD_KILL:
		fprintf(env.stderr_saved,
			"WATCHDOG: test case %s executes for %d seconds, terminating with SIGSEGV\n",
			test_name, env.secs_till_kill);
		pthread_kill(env.main_thread, SIGSEGV);
		break;
	}
}

static void watchdog_start(void)
{
	struct itimerspec timeout = {};
	int err;

	if (env.secs_till_kill == 0)
		return;
	if (env.secs_till_notify > 0) {
		env.watchdog_state = WD_NOTIFY;
		timeout.it_value.tv_sec = env.secs_till_notify;
	} else {
		env.watchdog_state = WD_KILL;
		timeout.it_value.tv_sec = env.secs_till_kill;
	}
	err = timer_settime(env.watchdog, 0, &timeout, NULL);
	if (err)
		fprintf(env.stderr_saved, "Failed to start watchdog timer\n");
}

static void watchdog_stop(void)
{
	struct itimerspec timeout = {};
	int err;

	env.watchdog_state = WD_NOTIFY;
	err = timer_settime(env.watchdog, 0, &timeout, NULL);
	if (err)
		fprintf(env.stderr_saved, "Failed to stop watchdog timer\n");
}

static void watchdog_init(void)
{
	struct sigevent watchdog_sev = {
		.sigev_notify = SIGEV_THREAD,
		.sigev_notify_function = watchdog_timer_func,
	};
	int err;

	env.main_thread = pthread_self();
	err = timer_create(CLOCK_MONOTONIC, &watchdog_sev, &env.watchdog);
	if (err)
		fprintf(stderr, "Failed to initialize watchdog timer\n");
}

static bool should_run(struct test_selector *sel, int num, const char *name)
{
	int i;

	for (i = 0; i < sel->blacklist.cnt; i++) {
		if (glob_match(name, sel->blacklist.tests[i].name) &&
		    !sel->blacklist.tests[i].subtest_cnt)
			return false;
	}

	for (i = 0; i < sel->whitelist.cnt; i++) {
		if (glob_match(name, sel->whitelist.tests[i].name))
			return true;
	}

	if (!sel->whitelist.cnt && !sel->num_set)
		return true;

	return num < sel->num_set_len && sel->num_set[num];
}

static bool match_subtest(struct test_filter_set *filter,
			  const char *test_name,
			  const char *subtest_name)
{
	int i, j;

	for (i = 0; i < filter->cnt; i++) {
		if (glob_match(test_name, filter->tests[i].name)) {
			if (!filter->tests[i].subtest_cnt)
				return true;

			for (j = 0; j < filter->tests[i].subtest_cnt; j++) {
				if (glob_match(subtest_name,
					       filter->tests[i].subtests[j]))
					return true;
			}
		}
	}

	return false;
}

static bool should_run_subtest(struct test_selector *sel,
			       struct test_selector *subtest_sel,
			       int subtest_num,
			       const char *test_name,
			       const char *subtest_name)
{
	if (match_subtest(&sel->blacklist, test_name, subtest_name))
		return false;

	if (match_subtest(&sel->whitelist, test_name, subtest_name))
		return true;

	if (!sel->whitelist.cnt && !subtest_sel->num_set)
		return true;

	return subtest_num < subtest_sel->num_set_len && subtest_sel->num_set[subtest_num];
}

static bool should_tmon(struct test_selector *sel, const char *name)
{
	int i;

	for (i = 0; i < sel->whitelist.cnt; i++) {
		if (glob_match(name, sel->whitelist.tests[i].name) &&
		    !sel->whitelist.tests[i].subtest_cnt)
			return true;
	}

	return false;
}

static char *test_result(bool failed, bool skipped)
{
	return failed ? "FAIL" : (skipped ? "SKIP" : "OK");
}

#define TEST_NUM_WIDTH 7

static void print_test_result(const struct prog_test_def *test, const struct test_state *test_state)
{
	int skipped_cnt = test_state->skip_cnt;
	int subtests_cnt = test_state->subtest_num;

	fprintf(env.stdout_saved, "#%-*d %s:", TEST_NUM_WIDTH, test->test_num, test->test_name);
	if (test_state->error_cnt)
		fprintf(env.stdout_saved, "FAIL");
	else if (!skipped_cnt)
		fprintf(env.stdout_saved, "OK");
	else if (skipped_cnt == subtests_cnt || !subtests_cnt)
		fprintf(env.stdout_saved, "SKIP");
	else
		fprintf(env.stdout_saved, "OK (SKIP: %d/%d)", skipped_cnt, subtests_cnt);

	fprintf(env.stdout_saved, "\n");
}

static void print_test_log(char *log_buf, size_t log_cnt)
{
	log_buf[log_cnt] = '\0';
	fprintf(env.stdout_saved, "%s", log_buf);
	if (log_buf[log_cnt - 1] != '\n')
		fprintf(env.stdout_saved, "\n");
}

static void print_subtest_name(int test_num, int subtest_num,
			       const char *test_name, char *subtest_name,
			       char *result)
{
	char test_num_str[32];

	snprintf(test_num_str, sizeof(test_num_str), "%d/%d", test_num, subtest_num);

	fprintf(env.stdout_saved, "#%-*s %s/%s",
		TEST_NUM_WIDTH, test_num_str,
		test_name, subtest_name);

	if (result)
		fprintf(env.stdout_saved, ":%s", result);

	fprintf(env.stdout_saved, "\n");
}

static void jsonw_write_log_message(json_writer_t *w, char *log_buf, size_t log_cnt)
{
	/* open_memstream (from stdio_hijack_init) ensures that log_bug is terminated by a
	 * null byte. Yet in parallel mode, log_buf will be NULL if there is no message.
	 */
	if (log_cnt) {
		jsonw_string_field(w, "message", log_buf);
	} else {
		jsonw_string_field(w, "message", "");
	}
}

static void dump_test_log(const struct prog_test_def *test,
			  const struct test_state *test_state,
			  bool skip_ok_subtests,
			  bool par_exec_result,
			  json_writer_t *w)
{
	bool test_failed = test_state->error_cnt > 0;
	bool force_log = test_state->force_log;
	bool print_test = verbose() || force_log || test_failed;
	int i;
	struct subtest_state *subtest_state;
	bool subtest_failed;
	bool subtest_filtered;
	bool print_subtest;

	/* we do not print anything in the worker thread */
	if (env.worker_id != -1)
		return;

	/* there is nothing to print when verbose log is used and execution
	 * is not in parallel mode
	 */
	if (verbose() && !par_exec_result)
		return;

	if (test_state->log_cnt && print_test)
		print_test_log(test_state->log_buf, test_state->log_cnt);

	if (w && print_test) {
		jsonw_start_object(w);
		jsonw_string_field(w, "name", test->test_name);
		jsonw_uint_field(w, "number", test->test_num);
		jsonw_write_log_message(w, test_state->log_buf, test_state->log_cnt);
		jsonw_bool_field(w, "failed", test_failed);
		jsonw_name(w, "subtests");
		jsonw_start_array(w);
	}

	for (i = 0; i < test_state->subtest_num; i++) {
		subtest_state = &test_state->subtest_states[i];
		subtest_failed = subtest_state->error_cnt;
		subtest_filtered = subtest_state->filtered;
		print_subtest = verbose() || force_log || subtest_failed;

		if ((skip_ok_subtests && !subtest_failed) || subtest_filtered)
			continue;

		if (subtest_state->log_cnt && print_subtest) {
			print_test_log(subtest_state->log_buf,
				       subtest_state->log_cnt);
		}

		print_subtest_name(test->test_num, i + 1,
				   test->test_name, subtest_state->name,
				   test_result(subtest_state->error_cnt,
					       subtest_state->skipped));

		if (w && print_subtest) {
			jsonw_start_object(w);
			jsonw_string_field(w, "name", subtest_state->name);
			jsonw_uint_field(w, "number", i+1);
			jsonw_write_log_message(w, subtest_state->log_buf, subtest_state->log_cnt);
			jsonw_bool_field(w, "failed", subtest_failed);
			jsonw_end_object(w);
		}
	}

	if (w && print_test) {
		jsonw_end_array(w);
		jsonw_end_object(w);
	}

	print_test_result(test, test_state);
}

/* A bunch of tests set custom affinity per-thread and/or per-process. Reset
 * it after each test/sub-test.
 */
static void reset_affinity(void)
{
	cpu_set_t cpuset;
	int i, err;

	CPU_ZERO(&cpuset);
	for (i = 0; i < env.nr_cpus; i++)
		CPU_SET(i, &cpuset);

	err = sched_setaffinity(0, sizeof(cpuset), &cpuset);
	if (err < 0) {
		fprintf(stderr, "Failed to reset process affinity: %d!\n", err);
		exit(EXIT_ERR_SETUP_INFRA);
	}
	err = pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
	if (err < 0) {
		fprintf(stderr, "Failed to reset thread affinity: %d!\n", err);
		exit(EXIT_ERR_SETUP_INFRA);
	}
}

static void save_netns(void)
{
	env.saved_netns_fd = open("/proc/self/ns/net", O_RDONLY);
	if (env.saved_netns_fd == -1) {
		perror("open(/proc/self/ns/net)");
		exit(EXIT_ERR_SETUP_INFRA);
	}
}

static void restore_netns(void)
{
	if (setns(env.saved_netns_fd, CLONE_NEWNET) == -1) {
		perror("setns(CLONE_NEWNS)");
		exit(EXIT_ERR_SETUP_INFRA);
	}
}

void test__end_subtest(void)
{
	struct prog_test_def *test = env.test;
	struct test_state *test_state = env.test_state;
	struct subtest_state *subtest_state = env.subtest_state;

	if (subtest_state->error_cnt) {
		test_state->error_cnt++;
	} else {
		if (!subtest_state->skipped)
			test_state->sub_succ_cnt++;
		else
			test_state->skip_cnt++;
	}

	if (verbose() && !env.workers)
		print_subtest_name(test->test_num, test_state->subtest_num,
				   test->test_name, subtest_state->name,
				   test_result(subtest_state->error_cnt,
					       subtest_state->skipped));

	stdio_restore();

	env.subtest_state = NULL;
}

bool test__start_subtest(const char *subtest_name)
{
	struct prog_test_def *test = env.test;
	struct test_state *state = env.test_state;
	struct subtest_state *subtest_state;
	size_t sub_state_size = sizeof(*subtest_state);

	if (env.subtest_state)
		test__end_subtest();

	state->subtest_num++;
	state->subtest_states =
		realloc(state->subtest_states,
			state->subtest_num * sub_state_size);
	if (!state->subtest_states) {
		fprintf(stderr, "Not enough memory to allocate subtest result\n");
		return false;
	}

	subtest_state = &state->subtest_states[state->subtest_num - 1];

	memset(subtest_state, 0, sub_state_size);

	if (!subtest_name || !subtest_name[0]) {
		fprintf(env.stderr_saved,
			"Subtest #%d didn't provide sub-test name!\n",
			state->subtest_num);
		return false;
	}

	subtest_state->name = strdup(subtest_name);
	if (!subtest_state->name) {
		fprintf(env.stderr_saved,
			"Subtest #%d: failed to copy subtest name!\n",
			state->subtest_num);
		return false;
	}

	if (!should_run_subtest(&env.test_selector,
				&env.subtest_selector,
				state->subtest_num,
				test->test_name,
				subtest_name)) {
		subtest_state->filtered = true;
		return false;
	}

	subtest_state->should_tmon = match_subtest(&env.tmon_selector.whitelist,
						   test->test_name,
						   subtest_name);

	env.subtest_state = subtest_state;
	stdio_hijack_init(&subtest_state->log_buf, &subtest_state->log_cnt);
	watchdog_start();

	return true;
}

void test__force_log(void)
{
	env.test_state->force_log = true;
}

void test__skip(void)
{
	if (env.subtest_state)
		env.subtest_state->skipped = true;
	else
		env.test_state->skip_cnt++;
}

void test__fail(void)
{
	if (env.subtest_state)
		env.subtest_state->error_cnt++;
	else
		env.test_state->error_cnt++;
}

int test__join_cgroup(const char *path)
{
	int fd;

	if (!env.test->need_cgroup_cleanup) {
		if (setup_cgroup_environment()) {
			fprintf(stderr,
				"#%d %s: Failed to setup cgroup environment\n",
				env.test->test_num, env.test->test_name);
			return -1;
		}

		env.test->need_cgroup_cleanup = true;
	}

	fd = create_and_get_cgroup(path);
	if (fd < 0) {
		fprintf(stderr,
			"#%d %s: Failed to create cgroup '%s' (errno=%d)\n",
			env.test->test_num, env.test->test_name, path, errno);
		return fd;
	}

	if (join_cgroup(path)) {
		fprintf(stderr,
			"#%d %s: Failed to join cgroup '%s' (errno=%d)\n",
			env.test->test_num, env.test->test_name, path, errno);
		return -1;
	}

	return fd;
}

int bpf_find_map(const char *test, struct bpf_object *obj, const char *name)
{
	struct bpf_map *map;

	map = bpf_object__find_map_by_name(obj, name);
	if (!map) {
		fprintf(stdout, "%s:FAIL:map '%s' not found\n", test, name);
		test__fail();
		return -1;
	}
	return bpf_map__fd(map);
}

int compare_map_keys(int map1_fd, int map2_fd)
{
	__u32 key, next_key;
	char val_buf[PERF_MAX_STACK_DEPTH *
		     sizeof(struct bpf_stack_build_id)];
	int err;

	err = bpf_map_get_next_key(map1_fd, NULL, &key);
	if (err)
		return err;
	err = bpf_map_lookup_elem(map2_fd, &key, val_buf);
	if (err)
		return err;

	while (bpf_map_get_next_key(map1_fd, &key, &next_key) == 0) {
		err = bpf_map_lookup_elem(map2_fd, &next_key, val_buf);
		if (err)
			return err;

		key = next_key;
	}
	if (errno != ENOENT)
		return -1;

	return 0;
}

int compare_stack_ips(int smap_fd, int amap_fd, int stack_trace_len)
{
	__u32 key, next_key, *cur_key_p, *next_key_p;
	char *val_buf1, *val_buf2;
	int i, err = 0;

	val_buf1 = malloc(stack_trace_len);
	val_buf2 = malloc(stack_trace_len);
	cur_key_p = NULL;
	next_key_p = &key;
	while (bpf_map_get_next_key(smap_fd, cur_key_p, next_key_p) == 0) {
		err = bpf_map_lookup_elem(smap_fd, next_key_p, val_buf1);
		if (err)
			goto out;
		err = bpf_map_lookup_elem(amap_fd, next_key_p, val_buf2);
		if (err)
			goto out;
		for (i = 0; i < stack_trace_len; i++) {
			if (val_buf1[i] != val_buf2[i]) {
				err = -1;
				goto out;
			}
		}
		key = *next_key_p;
		cur_key_p = &key;
		next_key_p = &next_key;
	}
	if (errno != ENOENT)
		err = -1;

out:
	free(val_buf1);
	free(val_buf2);
	return err;
}

struct netns_obj {
	char *nsname;
	struct tmonitor_ctx *tmon;
	struct nstoken *nstoken;
};

/* Create a new network namespace with the given name.
 *
 * Create a new network namespace and set the network namespace of the
 * current process to the new network namespace if the argument "open" is
 * true. This function should be paired with netns_free() to release the
 * resource and delete the network namespace.
 *
 * It also implements the functionality of the option "-m" by starting
 * traffic monitor on the background to capture the packets in this network
 * namespace if the current test or subtest matching the pattern.
 *
 * nsname: the name of the network namespace to create.
 * open: open the network namespace if true.
 *
 * Return: the network namespace object on success, NULL on failure.
 */
struct netns_obj *netns_new(const char *nsname, bool open)
{
	struct netns_obj *netns_obj = malloc(sizeof(*netns_obj));
	const char *test_name, *subtest_name;
	int r;

	if (!netns_obj)
		return NULL;
	memset(netns_obj, 0, sizeof(*netns_obj));

	netns_obj->nsname = strdup(nsname);
	if (!netns_obj->nsname)
		goto fail;

	/* Create the network namespace */
	r = make_netns(nsname);
	if (r)
		goto fail;

	/* Start traffic monitor */
	if (env.test->should_tmon ||
	    (env.subtest_state && env.subtest_state->should_tmon)) {
		test_name = env.test->test_name;
		subtest_name = env.subtest_state ? env.subtest_state->name : NULL;
		netns_obj->tmon = traffic_monitor_start(nsname, test_name, subtest_name);
		if (!netns_obj->tmon) {
			fprintf(stderr, "Failed to start traffic monitor for %s\n", nsname);
			goto fail;
		}
	} else {
		netns_obj->tmon = NULL;
	}

	if (open) {
		netns_obj->nstoken = open_netns(nsname);
		if (!netns_obj->nstoken)
			goto fail;
	}

	return netns_obj;
fail:
	traffic_monitor_stop(netns_obj->tmon);
	remove_netns(nsname);
	free(netns_obj->nsname);
	free(netns_obj);
	return NULL;
}

/* Delete the network namespace.
 *
 * This function should be paired with netns_new() to delete the namespace
 * created by netns_new().
 */
void netns_free(struct netns_obj *netns_obj)
{
	if (!netns_obj)
		return;
	traffic_monitor_stop(netns_obj->tmon);
	close_netns(netns_obj->nstoken);
	remove_netns(netns_obj->nsname);
	free(netns_obj->nsname);
	free(netns_obj);
}

/* extern declarations for test funcs */
#define DEFINE_TEST(name)				\
	extern void test_##name(void) __weak;		\
	extern void serial_test_##name(void) __weak;
#include <prog_tests/tests.h>
#undef DEFINE_TEST

static struct prog_test_def prog_test_defs[] = {
#define DEFINE_TEST(name) {			\
	.test_name = #name,			\
	.run_test = &test_##name,		\
	.run_serial_test = &serial_test_##name,	\
},
#include <prog_tests/tests.h>
#undef DEFINE_TEST
};

static const int prog_test_cnt = ARRAY_SIZE(prog_test_defs);

static struct test_state test_states[ARRAY_SIZE(prog_test_defs)];

const char *argp_program_version = "test_progs 0.1";
const char *argp_program_bug_address = "<bpf@vger.kernel.org>";
static const char argp_program_doc[] =
"BPF selftests test runner\v"
"Options accepting the NAMES parameter take either a comma-separated list\n"
"of test names, or a filename prefixed with @. The file contains one name\n"
"(or wildcard pattern) per line, and comments beginning with # are ignored.\n"
"\n"
"These options can be passed repeatedly to read multiple files.\n";

enum ARG_KEYS {
	ARG_TEST_NUM = 'n',
	ARG_TEST_NAME = 't',
	ARG_TEST_NAME_BLACKLIST = 'b',
	ARG_VERIFIER_STATS = 's',
	ARG_VERBOSE = 'v',
	ARG_GET_TEST_CNT = 'c',
	ARG_LIST_TEST_NAMES = 'l',
	ARG_TEST_NAME_GLOB_ALLOWLIST = 'a',
	ARG_TEST_NAME_GLOB_DENYLIST = 'd',
	ARG_NUM_WORKERS = 'j',
	ARG_DEBUG = -1,
	ARG_JSON_SUMMARY = 'J',
	ARG_TRAFFIC_MONITOR = 'm',
	ARG_WATCHDOG_TIMEOUT = 'w',
};

static const struct argp_option opts[] = {
	{ "num", ARG_TEST_NUM, "NUM", 0,
	  "Run test number NUM only " },
	{ "name", ARG_TEST_NAME, "NAMES", 0,
	  "Run tests with names containing any string from NAMES list" },
	{ "name-blacklist", ARG_TEST_NAME_BLACKLIST, "NAMES", 0,
	  "Don't run tests with names containing any string from NAMES list" },
	{ "verifier-stats", ARG_VERIFIER_STATS, NULL, 0,
	  "Output verifier statistics", },
	{ "verbose", ARG_VERBOSE, "LEVEL", OPTION_ARG_OPTIONAL,
	  "Verbose output (use -vv or -vvv for progressively verbose output)" },
	{ "count", ARG_GET_TEST_CNT, NULL, 0,
	  "Get number of selected top-level tests " },
	{ "list", ARG_LIST_TEST_NAMES, NULL, 0,
	  "List test names that would run (without running them) " },
	{ "allow", ARG_TEST_NAME_GLOB_ALLOWLIST, "NAMES", 0,
	  "Run tests with name matching the pattern (supports '*' wildcard)." },
	{ "deny", ARG_TEST_NAME_GLOB_DENYLIST, "NAMES", 0,
	  "Don't run tests with name matching the pattern (supports '*' wildcard)." },
	{ "workers", ARG_NUM_WORKERS, "WORKERS", OPTION_ARG_OPTIONAL,
	  "Number of workers to run in parallel, default to number of cpus." },
	{ "debug", ARG_DEBUG, NULL, 0,
	  "print extra debug information for test_progs." },
	{ "json-summary", ARG_JSON_SUMMARY, "FILE", 0, "Write report in json format to this file."},
#ifdef TRAFFIC_MONITOR
	{ "traffic-monitor", ARG_TRAFFIC_MONITOR, "NAMES", 0,
	  "Monitor network traffic of tests with name matching the pattern (supports '*' wildcard)." },
#endif
	{ "watchdog-timeout", ARG_WATCHDOG_TIMEOUT, "SECONDS", 0,
	  "Kill the process if tests are not making progress for specified number of seconds." },
	{},
};

static FILE *libbpf_capture_stream;

static struct {
	char *buf;
	size_t buf_sz;
} libbpf_output_capture;

/* Creates a global memstream capturing INFO and WARN level output
 * passed to libbpf_print_fn.
 * Returns 0 on success, negative value on failure.
 * On failure the description is printed using PRINT_FAIL and
 * current test case is marked as fail.
 */
int start_libbpf_log_capture(void)
{
	if (libbpf_capture_stream) {
		PRINT_FAIL("%s: libbpf_capture_stream != NULL\n", __func__);
		return -EINVAL;
	}

	libbpf_capture_stream = open_memstream(&libbpf_output_capture.buf,
					       &libbpf_output_capture.buf_sz);
	if (!libbpf_capture_stream) {
		PRINT_FAIL("%s: open_memstream failed errno=%d\n", __func__, errno);
		return -EINVAL;
	}

	return 0;
}

/* Destroys global memstream created by start_libbpf_log_capture().
 * Returns a pointer to captured data which has to be freed.
 * Returned buffer is null terminated.
 */
char *stop_libbpf_log_capture(void)
{
	char *buf;

	if (!libbpf_capture_stream)
		return NULL;

	fputc(0, libbpf_capture_stream);
	fclose(libbpf_capture_stream);
	libbpf_capture_stream = NULL;
	/* get 'buf' after fclose(), see open_memstream() documentation */
	buf = libbpf_output_capture.buf;
	memset(&libbpf_output_capture, 0, sizeof(libbpf_output_capture));
	return buf;
}

static int libbpf_print_fn(enum libbpf_print_level level,
			   const char *format, va_list args)
{
	if (libbpf_capture_stream && level != LIBBPF_DEBUG) {
		va_list args2;

		va_copy(args2, args);
		vfprintf(libbpf_capture_stream, format, args2);
		va_end(args2);
	}

	if (env.verbosity < VERBOSE_VERY && level == LIBBPF_DEBUG)
		return 0;

	vfprintf(stdout, format, args);
	return 0;
}

static void free_test_filter_set(const struct test_filter_set *set)
{
	int i, j;

	if (!set)
		return;

	for (i = 0; i < set->cnt; i++) {
		free((void *)set->tests[i].name);
		for (j = 0; j < set->tests[i].subtest_cnt; j++)
			free((void *)set->tests[i].subtests[j]);

		free((void *)set->tests[i].subtests);
	}

	free((void *)set->tests);
}

static void free_test_selector(struct test_selector *test_selector)
{
	free_test_filter_set(&test_selector->blacklist);
	free_test_filter_set(&test_selector->whitelist);
	free(test_selector->num_set);
}

extern int extra_prog_load_log_flags;

static error_t parse_arg(int key, char *arg, struct argp_state *state)
{
	struct test_env *env = state->input;
	int err = 0;

	switch (key) {
	case ARG_TEST_NUM: {
		char *subtest_str = strchr(arg, '/');

		if (subtest_str) {
			*subtest_str = '\0';
			if (parse_num_list(subtest_str + 1,
					   &env->subtest_selector.num_set,
					   &env->subtest_selector.num_set_len)) {
				fprintf(stderr,
					"Failed to parse subtest numbers.\n");
				return -EINVAL;
			}
		}
		if (parse_num_list(arg, &env->test_selector.num_set,
				   &env->test_selector.num_set_len)) {
			fprintf(stderr, "Failed to parse test numbers.\n");
			return -EINVAL;
		}
		break;
	}
	case ARG_TEST_NAME_GLOB_ALLOWLIST:
	case ARG_TEST_NAME: {
		if (arg[0] == '@')
			err = parse_test_list_file(arg + 1,
						   &env->test_selector.whitelist,
						   key == ARG_TEST_NAME_GLOB_ALLOWLIST);
		else
			err = parse_test_list(arg,
					      &env->test_selector.whitelist,
					      key == ARG_TEST_NAME_GLOB_ALLOWLIST);

		break;
	}
	case ARG_TEST_NAME_GLOB_DENYLIST:
	case ARG_TEST_NAME_BLACKLIST: {
		if (arg[0] == '@')
			err = parse_test_list_file(arg + 1,
						   &env->test_selector.blacklist,
						   key == ARG_TEST_NAME_GLOB_DENYLIST);
		else
			err = parse_test_list(arg,
					      &env->test_selector.blacklist,
					      key == ARG_TEST_NAME_GLOB_DENYLIST);

		break;
	}
	case ARG_VERIFIER_STATS:
		env->verifier_stats = true;
		break;
	case ARG_VERBOSE:
		env->verbosity = VERBOSE_NORMAL;
		if (arg) {
			if (strcmp(arg, "v") == 0) {
				env->verbosity = VERBOSE_VERY;
				extra_prog_load_log_flags = 1;
			} else if (strcmp(arg, "vv") == 0) {
				env->verbosity = VERBOSE_SUPER;
				extra_prog_load_log_flags = 2;
			} else {
				fprintf(stderr,
					"Unrecognized verbosity setting ('%s'), only -v and -vv are supported\n",
					arg);
				return -EINVAL;
			}
		}
		env_verbosity = env->verbosity;

		if (verbose()) {
			if (setenv("SELFTESTS_VERBOSE", "1", 1) == -1) {
				fprintf(stderr,
					"Unable to setenv SELFTESTS_VERBOSE=1 (errno=%d)",
					errno);
				return -EINVAL;
			}
		}

		break;
	case ARG_GET_TEST_CNT:
		env->get_test_cnt = true;
		break;
	case ARG_LIST_TEST_NAMES:
		env->list_test_names = true;
		break;
	case ARG_NUM_WORKERS:
		if (arg) {
			env->workers = atoi(arg);
			if (!env->workers) {
				fprintf(stderr, "Invalid number of worker: %s.", arg);
				return -EINVAL;
			}
		} else {
			env->workers = get_nprocs();
		}
		break;
	case ARG_DEBUG:
		env->debug = true;
		break;
	case ARG_JSON_SUMMARY:
		env->json = fopen(arg, "w");
		if (env->json == NULL) {
			perror("Failed to open json summary file");
			return -errno;
		}
		break;
	case ARGP_KEY_ARG:
		argp_usage(state);
		break;
	case ARGP_KEY_END:
		break;
#ifdef TRAFFIC_MONITOR
	case ARG_TRAFFIC_MONITOR:
		if (arg[0] == '@')
			err = parse_test_list_file(arg + 1,
						   &env->tmon_selector.whitelist,
						   true);
		else
			err = parse_test_list(arg,
					      &env->tmon_selector.whitelist,
					      true);
		break;
#endif
	case ARG_WATCHDOG_TIMEOUT:
		env->secs_till_kill = atoi(arg);
		if (env->secs_till_kill < 0) {
			fprintf(stderr, "Invalid watchdog timeout: %s.\n", arg);
			return -EINVAL;
		}
		if (env->secs_till_kill < env->secs_till_notify) {
			env->secs_till_notify = 0;
		}
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}
	return err;
}

/*
 * Determine if test_progs is running as a "flavored" test runner and switch
 * into corresponding sub-directory to load correct BPF objects.
 *
 * This is done by looking at executable name. If it contains "-flavor"
 * suffix, then we are running as a flavored test runner.
 */
int cd_flavor_subdir(const char *exec_name)
{
	/* General form of argv[0] passed here is:
	 * some/path/to/test_progs[-flavor], where -flavor part is optional.
	 * First cut out "test_progs[-flavor]" part, then extract "flavor"
	 * part, if it's there.
	 */
	const char *flavor = strrchr(exec_name, '/');

	if (!flavor)
		flavor = exec_name;
	else
		flavor++;

	flavor = strrchr(flavor, '-');
	if (!flavor)
		return 0;
	flavor++;
	if (verbose())
		fprintf(stdout,	"Switching to flavor '%s' subdirectory...\n", flavor);

	return chdir(flavor);
}

int trigger_module_test_read(int read_sz)
{
	int fd, err;

	fd = open(BPF_TESTMOD_TEST_FILE, O_RDONLY);
	err = -errno;
	if (!ASSERT_GE(fd, 0, "testmod_file_open"))
		return err;

	read(fd, NULL, read_sz);
	close(fd);

	return 0;
}

int trigger_module_test_write(int write_sz)
{
	int fd, err;
	char *buf = malloc(write_sz);

	if (!buf)
		return -ENOMEM;

	memset(buf, 'a', write_sz);
	buf[write_sz-1] = '\0';

	fd = open(BPF_TESTMOD_TEST_FILE, O_WRONLY);
	err = -errno;
	if (!ASSERT_GE(fd, 0, "testmod_file_open")) {
		free(buf);
		return err;
	}

	write(fd, buf, write_sz);
	close(fd);
	free(buf);
	return 0;
}

int write_sysctl(const char *sysctl, const char *value)
{
	int fd, err, len;

	fd = open(sysctl, O_WRONLY);
	if (!ASSERT_NEQ(fd, -1, "open sysctl"))
		return -1;

	len = strlen(value);
	err = write(fd, value, len);
	close(fd);
	if (!ASSERT_EQ(err, len, "write sysctl"))
		return -1;

	return 0;
}

int get_bpf_max_tramp_links_from(struct btf *btf)
{
	const struct btf_enum *e;
	const struct btf_type *t;
	__u32 i, type_cnt;
	const char *name;
	__u16 j, vlen;

	for (i = 1, type_cnt = btf__type_cnt(btf); i < type_cnt; i++) {
		t = btf__type_by_id(btf, i);
		if (!t || !btf_is_enum(t) || t->name_off)
			continue;
		e = btf_enum(t);
		for (j = 0, vlen = btf_vlen(t); j < vlen; j++, e++) {
			name = btf__str_by_offset(btf, e->name_off);
			if (name && !strcmp(name, "BPF_MAX_TRAMP_LINKS"))
				return e->val;
		}
	}

	return -1;
}

int get_bpf_max_tramp_links(void)
{
	struct btf *vmlinux_btf;
	int ret;

	vmlinux_btf = btf__load_vmlinux_btf();
	if (!ASSERT_OK_PTR(vmlinux_btf, "vmlinux btf"))
		return -1;
	ret = get_bpf_max_tramp_links_from(vmlinux_btf);
	btf__free(vmlinux_btf);

	return ret;
}

#define MAX_BACKTRACE_SZ 128
void crash_handler(int signum)
{
	void *bt[MAX_BACKTRACE_SZ];
	size_t sz;

	sz = backtrace(bt, ARRAY_SIZE(bt));

	fflush(stdout);
	stdout = env.stdout_saved;
	stderr = env.stderr_saved;

	if (env.test) {
		env.test_state->error_cnt++;
		dump_test_log(env.test, env.test_state, true, false, NULL);
	}
	if (env.worker_id != -1)
		fprintf(stderr, "[%d]: ", env.worker_id);
	fprintf(stderr, "Caught signal #%d!\nStack trace:\n", signum);
	backtrace_symbols_fd(bt, sz, STDERR_FILENO);
}

void hexdump(const char *prefix, const void *buf, size_t len)
{
	for (int i = 0; i < len; i++) {
		if (!(i % 16)) {
			if (i)
				fprintf(stdout, "\n");
			fprintf(stdout, "%s", prefix);
		}
		if (i && !(i % 8) && (i % 16))
			fprintf(stdout, "\t");
		fprintf(stdout, "%02X ", ((uint8_t *)(buf))[i]);
	}
	fprintf(stdout, "\n");
}

static void sigint_handler(int signum)
{
	int i;

	for (i = 0; i < env.workers; i++)
		if (env.worker_socks[i] > 0)
			close(env.worker_socks[i]);
}

static int current_test_idx;
static pthread_mutex_t current_test_lock;
static pthread_mutex_t stdout_output_lock;

static inline const char *str_msg(const struct msg *msg, char *buf)
{
	switch (msg->type) {
	case MSG_DO_TEST:
		sprintf(buf, "MSG_DO_TEST %d", msg->do_test.num);
		break;
	case MSG_TEST_DONE:
		sprintf(buf, "MSG_TEST_DONE %d (log: %d)",
			msg->test_done.num,
			msg->test_done.have_log);
		break;
	case MSG_SUBTEST_DONE:
		sprintf(buf, "MSG_SUBTEST_DONE %d (log: %d)",
			msg->subtest_done.num,
			msg->subtest_done.have_log);
		break;
	case MSG_TEST_LOG:
		sprintf(buf, "MSG_TEST_LOG (cnt: %zu, last: %d)",
			strlen(msg->test_log.log_buf),
			msg->test_log.is_last);
		break;
	case MSG_EXIT:
		sprintf(buf, "MSG_EXIT");
		break;
	default:
		sprintf(buf, "UNKNOWN");
		break;
	}

	return buf;
}

static int send_message(int sock, const struct msg *msg)
{
	char buf[256];

	if (env.debug)
		fprintf(stderr, "Sending msg: %s\n", str_msg(msg, buf));
	return send(sock, msg, sizeof(*msg), 0);
}

static int recv_message(int sock, struct msg *msg)
{
	int ret;
	char buf[256];

	memset(msg, 0, sizeof(*msg));
	ret = recv(sock, msg, sizeof(*msg), 0);
	if (ret >= 0) {
		if (env.debug)
			fprintf(stderr, "Received msg: %s\n", str_msg(msg, buf));
	}
	return ret;
}

static bool ns_is_needed(const char *test_name)
{
	if (strlen(test_name) < 3)
		return false;

	return !strncmp(test_name, "ns_", 3);
}

static void run_one_test(int test_num)
{
	struct prog_test_def *test = &prog_test_defs[test_num];
	struct test_state *state = &test_states[test_num];
	struct netns_obj *ns = NULL;

	env.test = test;
	env.test_state = state;

	stdio_hijack(&state->log_buf, &state->log_cnt);

	watchdog_start();
	if (ns_is_needed(test->test_name))
		ns = netns_new(test->test_name, true);
	if (test->run_test)
		test->run_test();
	else if (test->run_serial_test)
		test->run_serial_test();
	netns_free(ns);
	watchdog_stop();

	/* ensure last sub-test is finalized properly */
	if (env.subtest_state)
		test__end_subtest();

	state->tested = true;

	stdio_restore();

	if (verbose() && env.worker_id == -1)
		print_test_result(test, state);

	reset_affinity();
	restore_netns();
	if (test->need_cgroup_cleanup)
		cleanup_cgroup_environment();

	free(stop_libbpf_log_capture());

	dump_test_log(test, state, false, false, NULL);
}

struct dispatch_data {
	int worker_id;
	int sock_fd;
};

static int read_prog_test_msg(int sock_fd, struct msg *msg, enum msg_type type)
{
	if (recv_message(sock_fd, msg) < 0)
		return 1;

	if (msg->type != type) {
		printf("%s: unexpected message type %d. expected %d\n", __func__, msg->type, type);
		return 1;
	}

	return 0;
}

static int dispatch_thread_read_log(int sock_fd, char **log_buf, size_t *log_cnt)
{
	FILE *log_fp = NULL;
	int result = 0;

	log_fp = open_memstream(log_buf, log_cnt);
	if (!log_fp)
		return 1;

	while (true) {
		struct msg msg;

		if (read_prog_test_msg(sock_fd, &msg, MSG_TEST_LOG)) {
			result = 1;
			goto out;
		}

		fprintf(log_fp, "%s", msg.test_log.log_buf);
		if (msg.test_log.is_last)
			break;
	}

out:
	fclose(log_fp);
	log_fp = NULL;
	return result;
}

static int dispatch_thread_send_subtests(int sock_fd, struct test_state *state)
{
	struct msg msg;
	struct subtest_state *subtest_state;
	int subtest_num = state->subtest_num;

	state->subtest_states = malloc(subtest_num * sizeof(*subtest_state));

	for (int i = 0; i < subtest_num; i++) {
		subtest_state = &state->subtest_states[i];

		memset(subtest_state, 0, sizeof(*subtest_state));

		if (read_prog_test_msg(sock_fd, &msg, MSG_SUBTEST_DONE))
			return 1;

		subtest_state->name = strdup(msg.subtest_done.name);
		subtest_state->error_cnt = msg.subtest_done.error_cnt;
		subtest_state->skipped = msg.subtest_done.skipped;
		subtest_state->filtered = msg.subtest_done.filtered;

		/* collect all logs */
		if (msg.subtest_done.have_log)
			if (dispatch_thread_read_log(sock_fd,
						     &subtest_state->log_buf,
						     &subtest_state->log_cnt))
				return 1;
	}

	return 0;
}

static void *dispatch_thread(void *ctx)
{
	struct dispatch_data *data = ctx;
	int sock_fd;

	sock_fd = data->sock_fd;

	while (true) {
		int test_to_run = -1;
		struct prog_test_def *test;
		struct test_state *state;

		/* grab a test */
		{
			pthread_mutex_lock(&current_test_lock);

			if (current_test_idx >= prog_test_cnt) {
				pthread_mutex_unlock(&current_test_lock);
				goto done;
			}

			test = &prog_test_defs[current_test_idx];
			test_to_run = current_test_idx;
			current_test_idx++;

			pthread_mutex_unlock(&current_test_lock);
		}

		if (!test->should_run || test->run_serial_test)
			continue;

		/* run test through worker */
		{
			struct msg msg_do_test;

			memset(&msg_do_test, 0, sizeof(msg_do_test));
			msg_do_test.type = MSG_DO_TEST;
			msg_do_test.do_test.num = test_to_run;
			if (send_message(sock_fd, &msg_do_test) < 0) {
				perror("Fail to send command");
				goto done;
			}
			env.worker_current_test[data->worker_id] = test_to_run;
		}

		/* wait for test done */
		do {
			struct msg msg;

			if (read_prog_test_msg(sock_fd, &msg, MSG_TEST_DONE))
				goto error;
			if (test_to_run != msg.test_done.num)
				goto error;

			state = &test_states[test_to_run];
			state->tested = true;
			state->error_cnt = msg.test_done.error_cnt;
			state->skip_cnt = msg.test_done.skip_cnt;
			state->sub_succ_cnt = msg.test_done.sub_succ_cnt;
			state->subtest_num = msg.test_done.subtest_num;

			/* collect all logs */
			if (msg.test_done.have_log) {
				if (dispatch_thread_read_log(sock_fd,
							     &state->log_buf,
							     &state->log_cnt))
					goto error;
			}

			/* collect all subtests and subtest logs */
			if (!state->subtest_num)
				break;

			if (dispatch_thread_send_subtests(sock_fd, state))
				goto error;
		} while (false);

		pthread_mutex_lock(&stdout_output_lock);
		dump_test_log(test, state, false, true, NULL);
		pthread_mutex_unlock(&stdout_output_lock);
	} /* while (true) */
error:
	if (env.debug)
		fprintf(stderr, "[%d]: Protocol/IO error: %s.\n", data->worker_id, strerror(errno));

done:
	{
		struct msg msg_exit;

		msg_exit.type = MSG_EXIT;
		if (send_message(sock_fd, &msg_exit) < 0) {
			if (env.debug)
				fprintf(stderr, "[%d]: send_message msg_exit: %s.\n",
					data->worker_id, strerror(errno));
		}
	}
	return NULL;
}

static void calculate_summary_and_print_errors(struct test_env *env)
{
	int i;
	int succ_cnt = 0, fail_cnt = 0, sub_succ_cnt = 0, skip_cnt = 0;
	json_writer_t *w = NULL;

	for (i = 0; i < prog_test_cnt; i++) {
		struct test_state *state = &test_states[i];

		if (!state->tested)
			continue;

		sub_succ_cnt += state->sub_succ_cnt;
		skip_cnt += state->skip_cnt;

		if (state->error_cnt)
			fail_cnt++;
		else
			succ_cnt++;
	}

	if (env->json) {
		w = jsonw_new(env->json);
		if (!w)
			fprintf(env->stderr_saved, "Failed to create new JSON stream.");
	}

	if (w) {
		jsonw_start_object(w);
		jsonw_uint_field(w, "success", succ_cnt);
		jsonw_uint_field(w, "success_subtest", sub_succ_cnt);
		jsonw_uint_field(w, "skipped", skip_cnt);
		jsonw_uint_field(w, "failed", fail_cnt);
		jsonw_name(w, "results");
		jsonw_start_array(w);
	}

	/*
	 * We only print error logs summary when there are failed tests and
	 * verbose mode is not enabled. Otherwise, results may be inconsistent.
	 *
	 */
	if (!verbose() && fail_cnt) {
		printf("\nAll error logs:\n");

		/* print error logs again */
		for (i = 0; i < prog_test_cnt; i++) {
			struct prog_test_def *test = &prog_test_defs[i];
			struct test_state *state = &test_states[i];

			if (!state->tested || !state->error_cnt)
				continue;

			dump_test_log(test, state, true, true, w);
		}
	}

	if (w) {
		jsonw_end_array(w);
		jsonw_end_object(w);
		jsonw_destroy(&w);
	}

	if (env->json)
		fclose(env->json);

	printf("Summary: %d/%d PASSED, %d SKIPPED, %d FAILED\n",
	       succ_cnt, sub_succ_cnt, skip_cnt, fail_cnt);

	env->succ_cnt = succ_cnt;
	env->sub_succ_cnt = sub_succ_cnt;
	env->fail_cnt = fail_cnt;
	env->skip_cnt = skip_cnt;
}

static void server_main(void)
{
	pthread_t *dispatcher_threads;
	struct dispatch_data *data;
	struct sigaction sigact_int = {
		.sa_handler = sigint_handler,
		.sa_flags = SA_RESETHAND,
	};
	int i;

	sigaction(SIGINT, &sigact_int, NULL);

	dispatcher_threads = calloc(sizeof(pthread_t), env.workers);
	data = calloc(sizeof(struct dispatch_data), env.workers);

	env.worker_current_test = calloc(sizeof(int), env.workers);
	for (i = 0; i < env.workers; i++) {
		int rc;

		data[i].worker_id = i;
		data[i].sock_fd = env.worker_socks[i];
		rc = pthread_create(&dispatcher_threads[i], NULL, dispatch_thread, &data[i]);
		if (rc < 0) {
			perror("Failed to launch dispatcher thread");
			exit(EXIT_ERR_SETUP_INFRA);
		}
	}

	/* wait for all dispatcher to finish */
	for (i = 0; i < env.workers; i++) {
		while (true) {
			int ret = pthread_tryjoin_np(dispatcher_threads[i], NULL);

			if (!ret) {
				break;
			} else if (ret == EBUSY) {
				if (env.debug)
					fprintf(stderr, "Still waiting for thread %d (test %d).\n",
						i,  env.worker_current_test[i] + 1);
				usleep(1000 * 1000);
				continue;
			} else {
				fprintf(stderr, "Unexpected error joining dispatcher thread: %d", ret);
				break;
			}
		}
	}
	free(dispatcher_threads);
	free(env.worker_current_test);
	free(data);

	/* run serial tests */
	save_netns();

	for (int i = 0; i < prog_test_cnt; i++) {
		struct prog_test_def *test = &prog_test_defs[i];

		if (!test->should_run || !test->run_serial_test)
			continue;

		run_one_test(i);
	}

	/* generate summary */
	fflush(stderr);
	fflush(stdout);

	calculate_summary_and_print_errors(&env);

	/* reap all workers */
	for (i = 0; i < env.workers; i++) {
		int wstatus, pid;

		pid = waitpid(env.worker_pids[i], &wstatus, 0);
		if (pid != env.worker_pids[i])
			perror("Unable to reap worker");
	}
}

static void worker_main_send_log(int sock, char *log_buf, size_t log_cnt)
{
	char *src;
	size_t slen;

	src = log_buf;
	slen = log_cnt;
	while (slen) {
		struct msg msg_log;
		char *dest;
		size_t len;

		memset(&msg_log, 0, sizeof(msg_log));
		msg_log.type = MSG_TEST_LOG;
		dest = msg_log.test_log.log_buf;
		len = slen >= MAX_LOG_TRUNK_SIZE ? MAX_LOG_TRUNK_SIZE : slen;
		memcpy(dest, src, len);

		src += len;
		slen -= len;
		if (!slen)
			msg_log.test_log.is_last = true;

		assert(send_message(sock, &msg_log) >= 0);
	}
}

static void free_subtest_state(struct subtest_state *state)
{
	if (state->log_buf) {
		free(state->log_buf);
		state->log_buf = NULL;
		state->log_cnt = 0;
	}
	free(state->name);
	state->name = NULL;
}

static int worker_main_send_subtests(int sock, struct test_state *state)
{
	int i, result = 0;
	struct msg msg;
	struct subtest_state *subtest_state;

	memset(&msg, 0, sizeof(msg));
	msg.type = MSG_SUBTEST_DONE;

	for (i = 0; i < state->subtest_num; i++) {
		subtest_state = &state->subtest_states[i];

		msg.subtest_done.num = i;

		strncpy(msg.subtest_done.name, subtest_state->name, MAX_SUBTEST_NAME);

		msg.subtest_done.error_cnt = subtest_state->error_cnt;
		msg.subtest_done.skipped = subtest_state->skipped;
		msg.subtest_done.filtered = subtest_state->filtered;
		msg.subtest_done.have_log = false;

		if (verbose() || state->force_log || subtest_state->error_cnt) {
			if (subtest_state->log_cnt)
				msg.subtest_done.have_log = true;
		}

		if (send_message(sock, &msg) < 0) {
			perror("Fail to send message done");
			result = 1;
			goto out;
		}

		/* send logs */
		if (msg.subtest_done.have_log)
			worker_main_send_log(sock, subtest_state->log_buf, subtest_state->log_cnt);

		free_subtest_state(subtest_state);
		free(subtest_state->name);
	}

out:
	for (; i < state->subtest_num; i++)
		free_subtest_state(&state->subtest_states[i]);
	free(state->subtest_states);
	return result;
}

static int worker_main(int sock)
{
	save_netns();
	watchdog_init();

	while (true) {
		/* receive command */
		struct msg msg;

		if (recv_message(sock, &msg) < 0)
			goto out;

		switch (msg.type) {
		case MSG_EXIT:
			if (env.debug)
				fprintf(stderr, "[%d]: worker exit.\n",
					env.worker_id);
			goto out;
		case MSG_DO_TEST: {
			int test_to_run = msg.do_test.num;
			struct prog_test_def *test = &prog_test_defs[test_to_run];
			struct test_state *state = &test_states[test_to_run];
			struct msg msg;

			if (env.debug)
				fprintf(stderr, "[%d]: #%d:%s running.\n",
					env.worker_id,
					test_to_run + 1,
					test->test_name);

			run_one_test(test_to_run);

			memset(&msg, 0, sizeof(msg));
			msg.type = MSG_TEST_DONE;
			msg.test_done.num = test_to_run;
			msg.test_done.error_cnt = state->error_cnt;
			msg.test_done.skip_cnt = state->skip_cnt;
			msg.test_done.sub_succ_cnt = state->sub_succ_cnt;
			msg.test_done.subtest_num = state->subtest_num;
			msg.test_done.have_log = false;

			if (verbose() || state->force_log || state->error_cnt) {
				if (state->log_cnt)
					msg.test_done.have_log = true;
			}
			if (send_message(sock, &msg) < 0) {
				perror("Fail to send message done");
				goto out;
			}

			/* send logs */
			if (msg.test_done.have_log)
				worker_main_send_log(sock, state->log_buf, state->log_cnt);

			if (state->log_buf) {
				free(state->log_buf);
				state->log_buf = NULL;
				state->log_cnt = 0;
			}

			if (state->subtest_num)
				if (worker_main_send_subtests(sock, state))
					goto out;

			if (env.debug)
				fprintf(stderr, "[%d]: #%d:%s done.\n",
					env.worker_id,
					test_to_run + 1,
					test->test_name);
			break;
		} /* case MSG_DO_TEST */
		default:
			if (env.debug)
				fprintf(stderr, "[%d]: unknown message.\n",  env.worker_id);
			return -1;
		}
	}
out:
	return 0;
}

static void free_test_states(void)
{
	int i, j;

	for (i = 0; i < ARRAY_SIZE(prog_test_defs); i++) {
		struct test_state *test_state = &test_states[i];

		for (j = 0; j < test_state->subtest_num; j++)
			free_subtest_state(&test_state->subtest_states[j]);

		free(test_state->subtest_states);
		free(test_state->log_buf);
		test_state->subtest_states = NULL;
		test_state->log_buf = NULL;
	}
}

static __u32 register_session_key(const char *key_data, size_t key_data_size)
{
	return syscall(__NR_add_key, "asymmetric", "libbpf_session_key",
			(const void *)key_data, key_data_size,
			KEY_SPEC_SESSION_KEYRING);
}

int main(int argc, char **argv)
{
	static const struct argp argp = {
		.options = opts,
		.parser = parse_arg,
		.doc = argp_program_doc,
	};
	struct sigaction sigact = {
		.sa_handler = crash_handler,
		.sa_flags = SA_RESETHAND,
		};
	int err, i;

	sigaction(SIGSEGV, &sigact, NULL);

	env.stdout_saved = stdout;
	env.stderr_saved = stderr;

	env.secs_till_notify = 10;
	env.secs_till_kill = 120;
	err = argp_parse(&argp, argc, argv, 0, NULL, &env);
	if (err)
		return err;

	err = cd_flavor_subdir(argv[0]);
	if (err)
		return err;

	watchdog_init();

	/* Use libbpf 1.0 API mode */
	libbpf_set_strict_mode(LIBBPF_STRICT_ALL);
	libbpf_set_print(libbpf_print_fn);
	err = register_session_key((const char *)test_progs_verification_cert,
				   test_progs_verification_cert_len);
	if (err < 0)
		return err;

	traffic_monitor_set_print(traffic_monitor_print_fn);

	srand(time(NULL));

	env.jit_enabled = is_jit_enabled();
	env.nr_cpus = libbpf_num_possible_cpus();
	if (env.nr_cpus < 0) {
		fprintf(stderr, "Failed to get number of CPUs: %d!\n",
			env.nr_cpus);
		return -1;
	}

	env.has_testmod = true;
	if (!env.list_test_names) {
		/* ensure previous instance of the module is unloaded */
		unload_bpf_testmod(verbose());

		if (load_bpf_testmod(verbose())) {
			fprintf(env.stderr_saved, "WARNING! Selftests relying on bpf_testmod.ko will be skipped.\n");
			env.has_testmod = false;
		}
	}

	/* initializing tests */
	for (i = 0; i < prog_test_cnt; i++) {
		struct prog_test_def *test = &prog_test_defs[i];

		test->test_num = i + 1;
		test->should_run = should_run(&env.test_selector,
					      test->test_num, test->test_name);

		if ((test->run_test == NULL && test->run_serial_test == NULL) ||
		    (test->run_test != NULL && test->run_serial_test != NULL)) {
			fprintf(stderr, "Test %d:%s must have either test_%s() or serial_test_%sl() defined.\n",
				test->test_num, test->test_name, test->test_name, test->test_name);
			exit(EXIT_ERR_SETUP_INFRA);
		}
		if (test->should_run)
			test->should_tmon = should_tmon(&env.tmon_selector, test->test_name);
	}

	/* ignore workers if we are just listing */
	if (env.get_test_cnt || env.list_test_names)
		env.workers = 0;

	/* launch workers if requested */
	env.worker_id = -1; /* main process */
	if (env.workers) {
		env.worker_pids = calloc(sizeof(pid_t), env.workers);
		env.worker_socks = calloc(sizeof(int), env.workers);
		if (env.debug)
			fprintf(stdout, "Launching %d workers.\n", env.workers);
		for (i = 0; i < env.workers; i++) {
			int sv[2];
			pid_t pid;

			if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sv) < 0) {
				perror("Fail to create worker socket");
				return -1;
			}
			pid = fork();
			if (pid < 0) {
				perror("Failed to fork worker");
				return -1;
			} else if (pid != 0) { /* main process */
				close(sv[1]);
				env.worker_pids[i] = pid;
				env.worker_socks[i] = sv[0];
			} else { /* inside each worker process */
				close(sv[0]);
				env.worker_id = i;
				return worker_main(sv[1]);
			}
		}

		if (env.worker_id == -1) {
			server_main();
			goto out;
		}
	}

	/* The rest of the main process */

	/* on single mode */
	save_netns();

	for (i = 0; i < prog_test_cnt; i++) {
		struct prog_test_def *test = &prog_test_defs[i];

		if (!test->should_run)
			continue;

		if (env.get_test_cnt) {
			env.succ_cnt++;
			continue;
		}

		if (env.list_test_names) {
			fprintf(env.stdout_saved, "%s\n", test->test_name);
			env.succ_cnt++;
			continue;
		}

		run_one_test(i);
	}

	if (env.get_test_cnt) {
		printf("%d\n", env.succ_cnt);
		goto out;
	}

	if (env.list_test_names)
		goto out;

	calculate_summary_and_print_errors(&env);

	close(env.saved_netns_fd);
out:
	if (!env.list_test_names && env.has_testmod)
		unload_bpf_testmod(verbose());

	free_test_selector(&env.test_selector);
	free_test_selector(&env.subtest_selector);
	free_test_selector(&env.tmon_selector);
	free_test_states();

	if (env.succ_cnt + env.fail_cnt + env.skip_cnt == 0)
		return EXIT_NO_TEST;

	return env.fail_cnt ? EXIT_FAILURE : EXIT_SUCCESS;
}
