// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2017 Facebook
 */
#include "test_progs.h"
#include "cgroup_helpers.h"
#include "bpf_rlimit.h"
#include <argp.h>
#include <string.h>
#include <signal.h>
#include <execinfo.h> /* backtrace */

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

static bool should_run(struct test_selector *sel, int num, const char *name)
{
	int i;

	for (i = 0; i < sel->blacklist.cnt; i++) {
		if (strstr(name, sel->blacklist.strs[i]))
			return false;
	}

	for (i = 0; i < sel->whitelist.cnt; i++) {
		if (strstr(name, sel->whitelist.strs[i]))
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

void test__end_subtest()
{
	struct prog_test_def *test = env.test;
	int sub_error_cnt = test->error_cnt - test->old_error_cnt;

	if (sub_error_cnt)
		env.fail_cnt++;
	else
		env.sub_succ_cnt++;
	skip_account();

	dump_test_log(test, sub_error_cnt);

	fprintf(env.stdout, "#%d/%d %s:%s\n",
	       test->test_num, test->subtest_num,
	       test->subtest_name, sub_error_cnt ? "FAIL" : "OK");

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

struct ipv4_packet pkt_v4 = {
	.eth.h_proto = __bpf_constant_htons(ETH_P_IP),
	.iph.ihl = 5,
	.iph.protocol = IPPROTO_TCP,
	.iph.tot_len = __bpf_constant_htons(MAGIC_BYTES),
	.tcp.urg_ptr = 123,
	.tcp.doff = 5,
};

struct ipv6_packet pkt_v6 = {
	.eth.h_proto = __bpf_constant_htons(ETH_P_IPV6),
	.iph.nexthdr = IPPROTO_TCP,
	.iph.payload_len = __bpf_constant_htons(MAGIC_BYTES),
	.tcp.urg_ptr = 123,
	.tcp.doff = 5,
};

int bpf_find_map(const char *test, struct bpf_object *obj, const char *name)
{
	struct bpf_map *map;

	map = bpf_object__find_map_by_name(obj, name);
	if (!map) {
		printf("%s:FAIL:map '%s' not found\n", test, name);
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
	return 0;
err:
	fclose(fp);
	return -1;
}

void *spin_lock_thread(void *arg)
{
	__u32 duration, retval;
	int err, prog_fd = *(u32 *) arg;

	err = bpf_prog_test_run(prog_fd, 10000, &pkt_v4, sizeof(pkt_v4),
				NULL, NULL, &retval, &duration);
	CHECK(err || retval, "",
	      "err %d errno %d retval %d duration %d\n",
	      err, errno, retval, duration);
	pthread_exit(arg);
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
	{},
};

static int libbpf_print_fn(enum libbpf_print_level level,
			   const char *format, va_list args)
{
	if (env.verbosity < VERBOSE_VERY && level == LIBBPF_DEBUG)
		return 0;
	vprintf(format, args);
	return 0;
}

static int parse_str_list(const char *s, struct str_set *set)
{
	char *input, *state = NULL, *next, **tmp, **strs = NULL;
	int cnt = 0;

	input = strdup(s);
	if (!input)
		return -ENOMEM;

	set->cnt = 0;
	set->strs = NULL;

	while ((next = strtok_r(state ? NULL : input, ",", &state))) {
		tmp = realloc(strs, sizeof(*strs) * (cnt + 1));
		if (!tmp)
			goto err;
		strs = tmp;

		strs[cnt] = strdup(next);
		if (!strs[cnt])
			goto err;

		cnt++;
	}

	set->cnt = cnt;
	set->strs = (const char **)strs;
	free(input);
	return 0;
err:
	free(strs);
	free(input);
	return -ENOMEM;
}

int parse_num_list(const char *s, struct test_selector *sel)
{
	int i, set_len = 0, num, start = 0, end = -1;
	bool *set = NULL, *tmp, parsing_end = false;
	char *next;

	while (s[0]) {
		errno = 0;
		num = strtol(s, &next, 10);
		if (errno)
			return -errno;

		if (parsing_end)
			end = num;
		else
			start = num;

		if (!parsing_end && *next == '-') {
			s = next + 1;
			parsing_end = true;
			continue;
		} else if (*next == ',') {
			parsing_end = false;
			s = next + 1;
			end = num;
		} else if (*next == '\0') {
			parsing_end = false;
			s = next;
			end = num;
		} else {
			return -EINVAL;
		}

		if (start > end)
			return -EINVAL;

		if (end + 1 > set_len) {
			set_len = end + 1;
			tmp = realloc(set, set_len);
			if (!tmp) {
				free(set);
				return -ENOMEM;
			}
			set = tmp;
		}
		for (i = start; i <= end; i++) {
			set[i] = true;
		}

	}

	if (!set)
		return -EINVAL;

	sel->num_set = set;
	sel->num_set_len = set_len;

	return 0;
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
					   &env->subtest_selector)) {
				fprintf(stderr,
					"Failed to parse subtest numbers.\n");
				return -EINVAL;
			}
		}
		if (parse_num_list(arg, &env->test_selector)) {
			fprintf(stderr, "Failed to parse test numbers.\n");
			return -EINVAL;
		}
		break;
	}
	case ARG_TEST_NAME: {
		char *subtest_str = strchr(arg, '/');

		if (subtest_str) {
			*subtest_str = '\0';
			if (parse_str_list(subtest_str + 1,
					   &env->subtest_selector.whitelist))
				return -ENOMEM;
		}
		if (parse_str_list(arg, &env->test_selector.whitelist))
			return -ENOMEM;
		break;
	}
	case ARG_TEST_NAME_BLACKLIST: {
		char *subtest_str = strchr(arg, '/');

		if (subtest_str) {
			*subtest_str = '\0';
			if (parse_str_list(subtest_str + 1,
					   &env->subtest_selector.blacklist))
				return -ENOMEM;
		}
		if (parse_str_list(arg, &env->test_selector.blacklist))
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
	printf("Switching to flavor '%s' subdirectory...\n", flavor);
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

	libbpf_set_print(libbpf_print_fn);

	srand(time(NULL));

	env.jit_enabled = is_jit_enabled();

	stdio_hijack();
	for (i = 0; i < prog_test_cnt; i++) {
		struct prog_test_def *test = &prog_test_defs[i];

		env.test = test;
		test->test_num = i + 1;

		if (!should_run(&env.test_selector,
				test->test_num, test->test_name))
			continue;

		test->run_test();
		/* ensure last sub-test is finalized properly */
		if (test->subtest_name)
			test__end_subtest();

		test->tested = true;
		if (test->error_cnt)
			env.fail_cnt++;
		else
			env.succ_cnt++;
		skip_account();

		dump_test_log(test, test->error_cnt);

		fprintf(env.stdout, "#%d %s:%s\n",
			test->test_num, test->test_name,
			test->error_cnt ? "FAIL" : "OK");

		if (test->need_cgroup_cleanup)
			cleanup_cgroup_environment();
	}
	stdio_restore();
	printf("Summary: %d/%d PASSED, %d SKIPPED, %d FAILED\n",
	       env.succ_cnt, env.sub_succ_cnt, env.skip_cnt, env.fail_cnt);

	free(env.test_selector.blacklist.strs);
	free(env.test_selector.whitelist.strs);
	free(env.test_selector.num_set);
	free(env.subtest_selector.blacklist.strs);
	free(env.subtest_selector.whitelist.strs);
	free(env.subtest_selector.num_set);

	return env.fail_cnt ? EXIT_FAILURE : EXIT_SUCCESS;
}
