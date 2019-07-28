// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2017 Facebook
 */
#include "test_progs.h"
#include "bpf_rlimit.h"
#include <argp.h>
#include <string.h>

/* defined in test_progs.h */
struct test_env env;
int error_cnt, pass_cnt;

struct prog_test_def {
	const char *test_name;
	int test_num;
	void (*run_test)(void);
	bool force_log;
	int pass_cnt;
	int error_cnt;
	bool tested;

	const char *subtest_name;
	int subtest_num;

	/* store counts before subtest started */
	int old_pass_cnt;
	int old_error_cnt;
};

static bool should_run(struct test_selector *sel, int num, const char *name)
{
	if (sel->name && sel->name[0] && !strstr(name, sel->name))
		return false;

	if (!sel->num_set)
		return true;

	return num < sel->num_set_len && sel->num_set[num];
}

static void dump_test_log(const struct prog_test_def *test, bool failed)
{
	if (env.verbose || test->force_log || failed) {
		if (env.log_cnt) {
			fprintf(stdout, "%s", env.log_buf);
			if (env.log_buf[env.log_cnt - 1] != '\n')
				fprintf(stdout, "\n");
		}
		env.log_cnt = 0;
	}
}

void test__end_subtest()
{
	struct prog_test_def *test = env.test;
	int sub_error_cnt = error_cnt - test->old_error_cnt;

	if (sub_error_cnt)
		env.fail_cnt++;
	else
		env.sub_succ_cnt++;

	dump_test_log(test, sub_error_cnt);

	printf("#%d/%d %s:%s\n",
	       test->test_num, test->subtest_num,
	       test->subtest_name, sub_error_cnt ? "FAIL" : "OK");
}

bool test__start_subtest(const char *name)
{
	struct prog_test_def *test = env.test;

	if (test->subtest_name) {
		test__end_subtest();
		test->subtest_name = NULL;
	}

	test->subtest_num++;

	if (!name || !name[0]) {
		fprintf(stderr, "Subtest #%d didn't provide sub-test name!\n",
			test->subtest_num);
		return false;
	}

	if (!should_run(&env.subtest_selector, test->subtest_num, name))
		return false;

	test->subtest_name = name;
	env.test->old_pass_cnt = pass_cnt;
	env.test->old_error_cnt = error_cnt;

	return true;
}

void test__force_log() {
	env.test->force_log = true;
}

void test__vprintf(const char *fmt, va_list args)
{
	size_t rem_sz;
	int ret = 0;

	if (env.verbose || (env.test && env.test->force_log)) {
		vfprintf(stderr, fmt, args);
		return;
	}

try_again:
	rem_sz = env.log_cap - env.log_cnt;
	if (rem_sz) {
		va_list ap;

		va_copy(ap, args);
		/* we reserved extra byte for \0 at the end */
		ret = vsnprintf(env.log_buf + env.log_cnt, rem_sz + 1, fmt, ap);
		va_end(ap);

		if (ret < 0) {
			env.log_buf[env.log_cnt] = '\0';
			fprintf(stderr, "failed to log w/ fmt '%s'\n", fmt);
			return;
		}
	}

	if (!rem_sz || ret > rem_sz) {
		size_t new_sz = env.log_cap * 3 / 2;
		char *new_buf;

		if (new_sz < 4096)
			new_sz = 4096;
		if (new_sz < ret + env.log_cnt)
			new_sz = ret + env.log_cnt;

		/* +1 for guaranteed space for terminating \0 */
		new_buf = realloc(env.log_buf, new_sz + 1);
		if (!new_buf) {
			fprintf(stderr, "failed to realloc log buffer: %d\n",
				errno);
			return;
		}
		env.log_buf = new_buf;
		env.log_cap = new_sz;
		goto try_again;
	}

	env.log_cnt += ret;
}

void test__printf(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	test__vprintf(fmt, args);
	va_end(args);
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
		error_cnt++;
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
#define DEFINE_TEST(name) extern void test_##name();
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
	ARG_VERIFIER_STATS = 's',
	ARG_VERBOSE = 'v',
};
	
static const struct argp_option opts[] = {
	{ "num", ARG_TEST_NUM, "NUM", 0,
	  "Run test number NUM only " },
	{ "name", ARG_TEST_NAME, "NAME", 0,
	  "Run tests with names containing NAME" },
	{ "verifier-stats", ARG_VERIFIER_STATS, NULL, 0,
	  "Output verifier statistics", },
	{ "verbose", ARG_VERBOSE, "LEVEL", OPTION_ARG_OPTIONAL,
	  "Verbose output (use -vv for extra verbose output)" },
	{},
};

static int libbpf_print_fn(enum libbpf_print_level level,
			   const char *format, va_list args)
{
	if (!env.very_verbose && level == LIBBPF_DEBUG)
		return 0;
	test__vprintf(format, args);
	return 0;
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
			env->subtest_selector.name = strdup(subtest_str + 1);
			if (!env->subtest_selector.name)
				return -ENOMEM;
		}
		env->test_selector.name = strdup(arg);
		if (!env->test_selector.name)
			return -ENOMEM;
		break;
	}
	case ARG_VERIFIER_STATS:
		env->verifier_stats = true;
		break;
	case ARG_VERBOSE:
		if (arg) {
			if (strcmp(arg, "v") == 0) {
				env->very_verbose = true;
			} else {
				fprintf(stderr,
					"Unrecognized verbosity setting ('%s'), only -v and -vv are supported\n",
					arg);
				return -EINVAL;
			}
		}
		env->verbose = true;
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

int main(int argc, char **argv)
{
	static const struct argp argp = {
		.options = opts,
		.parser = parse_arg,
		.doc = argp_program_doc,
	};
	int err, i;

	err = argp_parse(&argp, argc, argv, 0, NULL, &env);
	if (err)
		return err;

	libbpf_set_print(libbpf_print_fn);

	srand(time(NULL));

	env.jit_enabled = is_jit_enabled();

	for (i = 0; i < prog_test_cnt; i++) {
		struct prog_test_def *test = &prog_test_defs[i];
		int old_pass_cnt = pass_cnt;
		int old_error_cnt = error_cnt;

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
		test->pass_cnt = pass_cnt - old_pass_cnt;
		test->error_cnt = error_cnt - old_error_cnt;
		if (test->error_cnt)
			env.fail_cnt++;
		else
			env.succ_cnt++;

		dump_test_log(test, test->error_cnt);

		printf("#%d %s:%s\n", test->test_num, test->test_name,
		       test->error_cnt ? "FAIL" : "OK");
	}
	printf("Summary: %d/%d PASSED, %d FAILED\n",
	       env.succ_cnt, env.sub_succ_cnt, env.fail_cnt);

	free(env.log_buf);
	free(env.test_selector.num_set);
	free(env.subtest_selector.num_set);

	return error_cnt ? EXIT_FAILURE : EXIT_SUCCESS;
}
