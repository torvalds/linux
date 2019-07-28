// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2017 Facebook
 */
#include "test_progs.h"
#include "bpf_rlimit.h"
#include <argp.h>
#include <string.h>

/* defined in test_progs.h */
struct test_env env = {
	.test_num_selector = -1,
};
int error_cnt, pass_cnt;

struct prog_test_def {
	const char *test_name;
	int test_num;
	void (*run_test)(void);
	bool force_log;
	int pass_cnt;
	int error_cnt;
	bool tested;
};

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

static error_t parse_arg(int key, char *arg, struct argp_state *state)
{
	struct test_env *env = state->input;

	switch (key) {
	case ARG_TEST_NUM: {
		int test_num;

		errno = 0;
		test_num = strtol(arg, NULL, 10);
		if (errno)
			return -errno;
		env->test_num_selector = test_num;
		break;
	}
	case ARG_TEST_NAME:
		env->test_name_selector = arg;
		break;
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

		if (env.test_num_selector >= 0 &&
		    test->test_num != env.test_num_selector)
			continue;
		if (env.test_name_selector &&
		    !strstr(test->test_name, env.test_name_selector))
			continue;

		test->run_test();
		test->tested = true;
		test->pass_cnt = pass_cnt - old_pass_cnt;
		test->error_cnt = error_cnt - old_error_cnt;
		if (test->error_cnt)
			env.fail_cnt++;
		else
			env.succ_cnt++;

		if (env.verbose || test->force_log || test->error_cnt) {
			if (env.log_cnt) {
				fprintf(stdout, "%s", env.log_buf);
				if (env.log_buf[env.log_cnt - 1] != '\n')
					fprintf(stdout, "\n");
			}
		}
		env.log_cnt = 0;

		printf("#%d %s:%s\n", test->test_num, test->test_name,
		       test->error_cnt ? "FAIL" : "OK");
	}
	printf("Summary: %d PASSED, %d FAILED\n", env.succ_cnt, env.fail_cnt);

	free(env.log_buf);

	return error_cnt ? EXIT_FAILURE : EXIT_SUCCESS;
}
