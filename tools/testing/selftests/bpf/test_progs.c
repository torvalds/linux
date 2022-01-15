// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2017 Facebook
 */
#define _GNU_SOURCE
#include "test_progs.h"
#include "cgroup_helpers.h"
#include "bpf_rlimit.h"
#include <argp.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <string.h>
#include <execinfo.h> /* backtrace */
#include <linux/membarrier.h>

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
	bool force_log;
	int error_cnt;
	int skip_cnt;
	bool tested;
	bool need_cgroup_cleanup;

	char *subtest_name;
	int subtest_num;

	/* store counts before subtest started */
	int old_error_cnt;
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

static bool should_run(struct test_selector *sel, int num, const char *name)
{
	int i;

	for (i = 0; i < sel->blacklist.cnt; i++) {
		if (glob_match(name, sel->blacklist.strs[i]))
			return false;
	}

	for (i = 0; i < sel->whitelist.cnt; i++) {
		if (glob_match(name, sel->whitelist.strs[i]))
			return true;
	}

	if (!sel->whitelist.cnt && !sel->num_set)
		return true;

	return num < sel->num_set_len && sel->num_set[num];
}

static void dump_test_log(const struct prog_test_def *test, bool failed)
{
	if (stdout == env.stdout)
		return;

	fflush(stdout); /* exports env.log_buf & env.log_cnt */

	if (env.verbosity > VERBOSE_NONE || test->force_log || failed) {
		if (env.log_cnt) {
			env.log_buf[env.log_cnt] = '\0';
			fprintf(env.stdout, "%s", env.log_buf);
			if (env.log_buf[env.log_cnt - 1] != '\n')
				fprintf(env.stdout, "\n");
		}
	}

	fseeko(stdout, 0, SEEK_SET); /* rewind */
}

static void skip_account(void)
{
	if (env.test->skip_cnt) {
		env.skip_cnt++;
		env.test->skip_cnt = 0;
	}
}

static void stdio_restore(void);

/* A bunch of tests set custom affinity per-thread and/or per-process. Reset
 * it after each test/sub-test.
 */
static void reset_affinity() {

	cpu_set_t cpuset;
	int i, err;

	CPU_ZERO(&cpuset);
	for (i = 0; i < env.nr_cpus; i++)
		CPU_SET(i, &cpuset);

	err = sched_setaffinity(0, sizeof(cpuset), &cpuset);
	if (err < 0) {
		stdio_restore();
		fprintf(stderr, "Failed to reset process affinity: %d!\n", err);
		exit(EXIT_ERR_SETUP_INFRA);
	}
	err = pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
	if (err < 0) {
		stdio_restore();
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
		stdio_restore();
		perror("setns(CLONE_NEWNS)");
		exit(EXIT_ERR_SETUP_INFRA);
	}
}

void test__end_subtest()
{
	struct prog_test_def *test = env.test;
	int sub_error_cnt = test->error_cnt - test->old_error_cnt;

	dump_test_log(test, sub_error_cnt);

	fprintf(env.stdout, "#%d/%d %s/%s:%s\n",
	       test->test_num, test->subtest_num, test->test_name, test->subtest_name,
	       sub_error_cnt ? "FAIL" : (test->skip_cnt ? "SKIP" : "OK"));

	if (sub_error_cnt)
		env.fail_cnt++;
	else if (test->skip_cnt == 0)
		env.sub_succ_cnt++;
	skip_account();

	free(test->subtest_name);
	test->subtest_name = NULL;
}

bool test__start_subtest(const char *name)
{
	struct prog_test_def *test = env.test;

	if (test->subtest_name)
		test__end_subtest();

	test->subtest_num++;

	if (!name || !name[0]) {
		fprintf(env.stderr,
			"Subtest #%d didn't provide sub-test name!\n",
			test->subtest_num);
		return false;
	}

	if (!should_run(&env.subtest_selector, test->subtest_num, name))
		return false;

	test->subtest_name = strdup(name);
	if (!test->subtest_name) {
		fprintf(env.stderr,
			"Subtest #%d: failed to copy subtest name!\n",
			test->subtest_num);
		return false;
	}
	env.test->old_error_cnt = env.test->error_cnt;

	return true;
}

void test__force_log() {
	env.test->force_log = true;
}

void test__skip(void)
{
	env.test->skip_cnt++;
}

void test__fail(void)
{
	env.test->error_cnt++;
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

static bool is_jit_enabled(void)
{
	const char *jit_sysctl = "/proc/sys/net/core/bpf_jit_enable";
	bool enabled = false;
	int sysctl_fd;

	sysctl_fd = open(jit_sysctl, 0, O_RDONLY);
	if (sysctl_fd != -1) {
		char tmpc;

		if (read(sysctl_fd, &tmpc, sizeof(tmpc)) == 1)
			enabled = (tmpc != '0');
		close(sysctl_fd);
	}

	return enabled;
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

int extract_build_id(char *build_id, size_t size)
{
	FILE *fp;
	char *line = NULL;
	size_t len = 0;

	fp = popen("readelf -n ./urandom_read | grep 'Build ID'", "r");
	if (fp == NULL)
		return -1;

	if (getline(&line, &len, fp) == -1)
		goto err;
	fclose(fp);

	if (len > size)
		len = size;
	memcpy(build_id, line, len);
	build_id[len] = '\0';
	free(line);
	return 0;
err:
	fclose(fp);
	return -1;
}

static int finit_module(int fd, const char *param_values, int flags)
{
	return syscall(__NR_finit_module, fd, param_values, flags);
}

static int delete_module(const char *name, int flags)
{
	return syscall(__NR_delete_module, name, flags);
}

/*
 * Trigger synchronize_rcu() in kernel.
 */
int kern_sync_rcu(void)
{
	return syscall(__NR_membarrier, MEMBARRIER_CMD_SHARED, 0, 0);
}

static void unload_bpf_testmod(void)
{
	if (kern_sync_rcu())
		fprintf(env.stderr, "Failed to trigger kernel-side RCU sync!\n");
	if (delete_module("bpf_testmod", 0)) {
		if (errno == ENOENT) {
			if (env.verbosity > VERBOSE_NONE)
				fprintf(stdout, "bpf_testmod.ko is already unloaded.\n");
			return;
		}
		fprintf(env.stderr, "Failed to unload bpf_testmod.ko from kernel: %d\n", -errno);
		return;
	}
	if (env.verbosity > VERBOSE_NONE)
		fprintf(stdout, "Successfully unloaded bpf_testmod.ko.\n");
}

static int load_bpf_testmod(void)
{
	int fd;

	/* ensure previous instance of the module is unloaded */
	unload_bpf_testmod();

	if (env.verbosity > VERBOSE_NONE)
		fprintf(stdout, "Loading bpf_testmod.ko...\n");

	fd = open("bpf_testmod.ko", O_RDONLY);
	if (fd < 0) {
		fprintf(env.stderr, "Can't find bpf_testmod.ko kernel module: %d\n", -errno);
		return -ENOENT;
	}
	if (finit_module(fd, "", 0)) {
		fprintf(env.stderr, "Failed to load bpf_testmod.ko into the kernel: %d\n", -errno);
		close(fd);
		return -EINVAL;
	}
	close(fd);

	if (env.verbosity > VERBOSE_NONE)
		fprintf(stdout, "Successfully loaded bpf_testmod.ko.\n");
	return 0;
}

/* extern declarations for test funcs */
#define DEFINE_TEST(name) extern void test_##name(void);
#include <prog_tests/tests.h>
#undef DEFINE_TEST

static struct prog_test_def prog_test_defs[] = {
#define DEFINE_TEST(name) {		\
	.test_name = #name,		\
	.run_test = &test_##name,	\
},
#include <prog_tests/tests.h>
#undef DEFINE_TEST
};
const int prog_test_cnt = ARRAY_SIZE(prog_test_defs);

const char *argp_program_version = "test_progs 0.1";
const char *argp_program_bug_address = "<bpf@vger.kernel.org>";
const char argp_program_doc[] = "BPF selftests test runner";

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
	{},
};

static int libbpf_print_fn(enum libbpf_print_level level,
			   const char *format, va_list args)
{
	if (env.verbosity < VERBOSE_VERY && level == LIBBPF_DEBUG)
		return 0;
	vfprintf(stdout, format, args);
	return 0;
}

static void free_str_set(const struct str_set *set)
{
	int i;

	if (!set)
		return;

	for (i = 0; i < set->cnt; i++)
		free((void *)set->strs[i]);
	free(set->strs);
}

static int parse_str_list(const char *s, struct str_set *set, bool is_glob_pattern)
{
	char *input, *state = NULL, *next, **tmp, **strs = NULL;
	int i, cnt = 0;

	input = strdup(s);
	if (!input)
		return -ENOMEM;

	while ((next = strtok_r(state ? NULL : input, ",", &state))) {
		tmp = realloc(strs, sizeof(*strs) * (cnt + 1));
		if (!tmp)
			goto err;
		strs = tmp;

		if (is_glob_pattern) {
			strs[cnt] = strdup(next);
			if (!strs[cnt])
				goto err;
		} else {
			strs[cnt] = malloc(strlen(next) + 2 + 1);
			if (!strs[cnt])
				goto err;
			sprintf(strs[cnt], "*%s*", next);
		}

		cnt++;
	}

	tmp = realloc(set->strs, sizeof(*strs) * (cnt + set->cnt));
	if (!tmp)
		goto err;
	memcpy(tmp + set->cnt, strs, sizeof(*strs) * cnt);
	set->strs = (const char **)tmp;
	set->cnt += cnt;

	free(input);
	free(strs);
	return 0;
err:
	for (i = 0; i < cnt; i++)
		free(strs[i]);
	free(strs);
	free(input);
	return -ENOMEM;
}

extern int extra_prog_load_log_flags;

static error_t parse_arg(int key, char *arg, struct argp_state *state)
{
	struct test_env *env = state->input;

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
		char *subtest_str = strchr(arg, '/');

		if (subtest_str) {
			*subtest_str = '\0';
			if (parse_str_list(subtest_str + 1,
					   &env->subtest_selector.whitelist,
					   key == ARG_TEST_NAME_GLOB_ALLOWLIST))
				return -ENOMEM;
		}
		if (parse_str_list(arg, &env->test_selector.whitelist,
				   key == ARG_TEST_NAME_GLOB_ALLOWLIST))
			return -ENOMEM;
		break;
	}
	case ARG_TEST_NAME_GLOB_DENYLIST:
	case ARG_TEST_NAME_BLACKLIST: {
		char *subtest_str = strchr(arg, '/');

		if (subtest_str) {
			*subtest_str = '\0';
			if (parse_str_list(subtest_str + 1,
					   &env->subtest_selector.blacklist,
					   key == ARG_TEST_NAME_GLOB_DENYLIST))
				return -ENOMEM;
		}
		if (parse_str_list(arg, &env->test_selector.blacklist,
				   key == ARG_TEST_NAME_GLOB_DENYLIST))
			return -ENOMEM;
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

		if (env->verbosity > VERBOSE_NONE) {
			if (setenv("SELFTESTS_VERBOSE", "1", 1) == -1) {
				fprintf(stderr,
					"Unable to setenv SELFTESTS_VERBOSE=1 (errno=%d)",
					errno);
				return -1;
			}
		}

		break;
	case ARG_GET_TEST_CNT:
		env->get_test_cnt = true;
		break;
	case ARG_LIST_TEST_NAMES:
		env->list_test_names = true;
		break;
	case ARGP_KEY_ARG:
		argp_usage(state);
		break;
	case ARGP_KEY_END:
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

static void stdio_hijack(void)
{
#ifdef __GLIBC__
	env.stdout = stdout;
	env.stderr = stderr;

	if (env.verbosity > VERBOSE_NONE) {
		/* nothing to do, output to stdout by default */
		return;
	}

	/* stdout and stderr -> buffer */
	fflush(stdout);

	stdout = open_memstream(&env.log_buf, &env.log_cnt);
	if (!stdout) {
		stdout = env.stdout;
		perror("open_memstream");
		return;
	}

	stderr = stdout;
#endif
}

static void stdio_restore(void)
{
#ifdef __GLIBC__
	if (stdout == env.stdout)
		return;

	fclose(stdout);
	free(env.log_buf);

	env.log_buf = NULL;
	env.log_cnt = 0;

	stdout = env.stdout;
	stderr = env.stderr;
#endif
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
		return 0;
	flavor++;
	flavor = strrchr(flavor, '-');
	if (!flavor)
		return 0;
	flavor++;
	if (env.verbosity > VERBOSE_NONE)
		fprintf(stdout,	"Switching to flavor '%s' subdirectory...\n", flavor);

	return chdir(flavor);
}

#define MAX_BACKTRACE_SZ 128
void crash_handler(int signum)
{
	void *bt[MAX_BACKTRACE_SZ];
	size_t sz;

	sz = backtrace(bt, ARRAY_SIZE(bt));

	if (env.test)
		dump_test_log(env.test, true);
	if (env.stdout)
		stdio_restore();

	fprintf(stderr, "Caught signal #%d!\nStack trace:\n", signum);
	backtrace_symbols_fd(bt, sz, STDERR_FILENO);
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

	err = argp_parse(&argp, argc, argv, 0, NULL, &env);
	if (err)
		return err;

	err = cd_flavor_subdir(argv[0]);
	if (err)
		return err;

	/* Use libbpf 1.0 API mode */
	libbpf_set_strict_mode(LIBBPF_STRICT_ALL);

	libbpf_set_print(libbpf_print_fn);

	srand(time(NULL));

	env.jit_enabled = is_jit_enabled();
	env.nr_cpus = libbpf_num_possible_cpus();
	if (env.nr_cpus < 0) {
		fprintf(stderr, "Failed to get number of CPUs: %d!\n",
			env.nr_cpus);
		return -1;
	}

	save_netns();
	stdio_hijack();
	env.has_testmod = true;
	if (!env.list_test_names && load_bpf_testmod()) {
		fprintf(env.stderr, "WARNING! Selftests relying on bpf_testmod.ko will be skipped.\n");
		env.has_testmod = false;
	}
	for (i = 0; i < prog_test_cnt; i++) {
		struct prog_test_def *test = &prog_test_defs[i];

		env.test = test;
		test->test_num = i + 1;

		if (!should_run(&env.test_selector,
				test->test_num, test->test_name))
			continue;

		if (env.get_test_cnt) {
			env.succ_cnt++;
			continue;
		}

		if (env.list_test_names) {
			fprintf(env.stdout, "%s\n", test->test_name);
			env.succ_cnt++;
			continue;
		}

		test->run_test();
		/* ensure last sub-test is finalized properly */
		if (test->subtest_name)
			test__end_subtest();

		test->tested = true;

		dump_test_log(test, test->error_cnt);

		fprintf(env.stdout, "#%d %s:%s\n",
			test->test_num, test->test_name,
			test->error_cnt ? "FAIL" : (test->skip_cnt ? "SKIP" : "OK"));

		if (test->error_cnt)
			env.fail_cnt++;
		else
			env.succ_cnt++;
		skip_account();

		reset_affinity();
		restore_netns();
		if (test->need_cgroup_cleanup)
			cleanup_cgroup_environment();
	}
	if (!env.list_test_names && env.has_testmod)
		unload_bpf_testmod();
	stdio_restore();

	if (env.get_test_cnt) {
		printf("%d\n", env.succ_cnt);
		goto out;
	}

	if (env.list_test_names)
		goto out;

	fprintf(stdout, "Summary: %d/%d PASSED, %d SKIPPED, %d FAILED\n",
		env.succ_cnt, env.sub_succ_cnt, env.skip_cnt, env.fail_cnt);

out:
	free_str_set(&env.test_selector.blacklist);
	free_str_set(&env.test_selector.whitelist);
	free(env.test_selector.num_set);
	free_str_set(&env.subtest_selector.blacklist);
	free_str_set(&env.subtest_selector.whitelist);
	free(env.subtest_selector.num_set);
	close(env.saved_netns_fd);

	if (env.succ_cnt + env.fail_cnt + env.skip_cnt == 0)
		return EXIT_NO_TEST;

	return env.fail_cnt ? EXIT_FAILURE : EXIT_SUCCESS;
}
