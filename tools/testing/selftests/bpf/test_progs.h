/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TEST_PROGS_H
#define __TEST_PROGS_H

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <signal.h>

#include <linux/types.h>
typedef __u16 __sum16;
#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/filter.h>
#include <linux/perf_event.h>
#include <linux/socket.h>
#include <linux/unistd.h>

#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/param.h>
#include <fcntl.h>
#include <pthread.h>
#include <linux/bpf.h>
#include <linux/err.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "test_iptunnel_common.h"
#include "bpf_util.h"
#include <bpf/bpf_endian.h>
#include "trace_helpers.h"
#include "testing_helpers.h"

enum verbosity {
	VERBOSE_NONE,
	VERBOSE_NORMAL,
	VERBOSE_VERY,
	VERBOSE_SUPER,
};

struct test_filter {
	char *name;
	char **subtests;
	int subtest_cnt;
};

struct test_filter_set {
	struct test_filter *tests;
	int cnt;
};

struct test_selector {
	struct test_filter_set whitelist;
	struct test_filter_set blacklist;
	bool *num_set;
	int num_set_len;
};

struct subtest_state {
	char *name;
	size_t log_cnt;
	char *log_buf;
	int error_cnt;
	bool skipped;
	bool filtered;

	FILE *stdout;
};

struct test_state {
	bool tested;
	bool force_log;

	int error_cnt;
	int skip_cnt;
	int sub_succ_cnt;

	struct subtest_state *subtest_states;
	int subtest_num;

	size_t log_cnt;
	char *log_buf;

	FILE *stdout;
};

struct test_env {
	struct test_selector test_selector;
	struct test_selector subtest_selector;
	bool verifier_stats;
	bool debug;
	enum verbosity verbosity;

	bool jit_enabled;
	bool has_testmod;
	bool get_test_cnt;
	bool list_test_names;

	struct prog_test_def *test; /* current running test */
	struct test_state *test_state; /* current running test state */
	struct subtest_state *subtest_state; /* current running subtest state */

	FILE *stdout;
	FILE *stderr;
	int nr_cpus;

	int succ_cnt; /* successful tests */
	int sub_succ_cnt; /* successful sub-tests */
	int fail_cnt; /* total failed tests + sub-tests */
	int skip_cnt; /* skipped tests */

	int saved_netns_fd;
	int workers; /* number of worker process */
	int worker_id; /* id number of current worker, main process is -1 */
	pid_t *worker_pids; /* array of worker pids */
	int *worker_socks; /* array of worker socks */
	int *worker_current_test; /* array of current running test for each worker */
};

#define MAX_LOG_TRUNK_SIZE 8192
#define MAX_SUBTEST_NAME 1024
enum msg_type {
	MSG_DO_TEST = 0,
	MSG_TEST_DONE = 1,
	MSG_TEST_LOG = 2,
	MSG_SUBTEST_DONE = 3,
	MSG_EXIT = 255,
};
struct msg {
	enum msg_type type;
	union {
		struct {
			int num;
		} do_test;
		struct {
			int num;
			int sub_succ_cnt;
			int error_cnt;
			int skip_cnt;
			bool have_log;
			int subtest_num;
		} test_done;
		struct {
			char log_buf[MAX_LOG_TRUNK_SIZE + 1];
			bool is_last;
		} test_log;
		struct {
			int num;
			char name[MAX_SUBTEST_NAME + 1];
			int error_cnt;
			bool skipped;
			bool filtered;
			bool have_log;
		} subtest_done;
	};
};

extern struct test_env env;

void test__force_log(void);
bool test__start_subtest(const char *name);
void test__end_subtest(void);
void test__skip(void);
void test__fail(void);
int test__join_cgroup(const char *path);

#define PRINT_FAIL(format...)                                                  \
	({                                                                     \
		test__fail();                                                  \
		fprintf(stdout, "%s:FAIL:%d ", __func__, __LINE__);            \
		fprintf(stdout, ##format);                                     \
	})

#define _CHECK(condition, tag, duration, format...) ({			\
	int __ret = !!(condition);					\
	int __save_errno = errno;					\
	if (__ret) {							\
		test__fail();						\
		fprintf(stdout, "%s:FAIL:%s ", __func__, tag);		\
		fprintf(stdout, ##format);				\
	} else {							\
		fprintf(stdout, "%s:PASS:%s %d nsec\n",			\
		       __func__, tag, duration);			\
	}								\
	errno = __save_errno;						\
	__ret;								\
})

#define CHECK_FAIL(condition) ({					\
	int __ret = !!(condition);					\
	int __save_errno = errno;					\
	if (__ret) {							\
		test__fail();						\
		fprintf(stdout, "%s:FAIL:%d\n", __func__, __LINE__);	\
	}								\
	errno = __save_errno;						\
	__ret;								\
})

#define CHECK(condition, tag, format...) \
	_CHECK(condition, tag, duration, format)
#define CHECK_ATTR(condition, tag, format...) \
	_CHECK(condition, tag, tattr.duration, format)

#define ASSERT_FAIL(fmt, args...) ({					\
	static int duration = 0;					\
	CHECK(false, "", fmt"\n", ##args);				\
	false;								\
})

#define ASSERT_TRUE(actual, name) ({					\
	static int duration = 0;					\
	bool ___ok = (actual);						\
	CHECK(!___ok, (name), "unexpected %s: got FALSE\n", (name));	\
	___ok;								\
})

#define ASSERT_FALSE(actual, name) ({					\
	static int duration = 0;					\
	bool ___ok = !(actual);						\
	CHECK(!___ok, (name), "unexpected %s: got TRUE\n", (name));	\
	___ok;								\
})

#define ASSERT_EQ(actual, expected, name) ({				\
	static int duration = 0;					\
	typeof(actual) ___act = (actual);				\
	typeof(expected) ___exp = (expected);				\
	bool ___ok = ___act == ___exp;					\
	CHECK(!___ok, (name),						\
	      "unexpected %s: actual %lld != expected %lld\n",		\
	      (name), (long long)(___act), (long long)(___exp));	\
	___ok;								\
})

#define ASSERT_NEQ(actual, expected, name) ({				\
	static int duration = 0;					\
	typeof(actual) ___act = (actual);				\
	typeof(expected) ___exp = (expected);				\
	bool ___ok = ___act != ___exp;					\
	CHECK(!___ok, (name),						\
	      "unexpected %s: actual %lld == expected %lld\n",		\
	      (name), (long long)(___act), (long long)(___exp));	\
	___ok;								\
})

#define ASSERT_LT(actual, expected, name) ({				\
	static int duration = 0;					\
	typeof(actual) ___act = (actual);				\
	typeof(expected) ___exp = (expected);				\
	bool ___ok = ___act < ___exp;					\
	CHECK(!___ok, (name),						\
	      "unexpected %s: actual %lld >= expected %lld\n",		\
	      (name), (long long)(___act), (long long)(___exp));	\
	___ok;								\
})

#define ASSERT_LE(actual, expected, name) ({				\
	static int duration = 0;					\
	typeof(actual) ___act = (actual);				\
	typeof(expected) ___exp = (expected);				\
	bool ___ok = ___act <= ___exp;					\
	CHECK(!___ok, (name),						\
	      "unexpected %s: actual %lld > expected %lld\n",		\
	      (name), (long long)(___act), (long long)(___exp));	\
	___ok;								\
})

#define ASSERT_GT(actual, expected, name) ({				\
	static int duration = 0;					\
	typeof(actual) ___act = (actual);				\
	typeof(expected) ___exp = (expected);				\
	bool ___ok = ___act > ___exp;					\
	CHECK(!___ok, (name),						\
	      "unexpected %s: actual %lld <= expected %lld\n",		\
	      (name), (long long)(___act), (long long)(___exp));	\
	___ok;								\
})

#define ASSERT_GE(actual, expected, name) ({				\
	static int duration = 0;					\
	typeof(actual) ___act = (actual);				\
	typeof(expected) ___exp = (expected);				\
	bool ___ok = ___act >= ___exp;					\
	CHECK(!___ok, (name),						\
	      "unexpected %s: actual %lld < expected %lld\n",		\
	      (name), (long long)(___act), (long long)(___exp));	\
	___ok;								\
})

#define ASSERT_STREQ(actual, expected, name) ({				\
	static int duration = 0;					\
	const char *___act = actual;					\
	const char *___exp = expected;					\
	bool ___ok = strcmp(___act, ___exp) == 0;			\
	CHECK(!___ok, (name),						\
	      "unexpected %s: actual '%s' != expected '%s'\n",		\
	      (name), ___act, ___exp);					\
	___ok;								\
})

#define ASSERT_STRNEQ(actual, expected, len, name) ({			\
	static int duration = 0;					\
	const char *___act = actual;					\
	const char *___exp = expected;					\
	int ___len = len;						\
	bool ___ok = strncmp(___act, ___exp, ___len) == 0;		\
	CHECK(!___ok, (name),						\
	      "unexpected %s: actual '%.*s' != expected '%.*s'\n",	\
	      (name), ___len, ___act, ___len, ___exp);			\
	___ok;								\
})

#define ASSERT_HAS_SUBSTR(str, substr, name) ({				\
	static int duration = 0;					\
	const char *___str = str;					\
	const char *___substr = substr;					\
	bool ___ok = strstr(___str, ___substr) != NULL;			\
	CHECK(!___ok, (name),						\
	      "unexpected %s: '%s' is not a substring of '%s'\n",	\
	      (name), ___substr, ___str);				\
	___ok;								\
})

#define ASSERT_OK(res, name) ({						\
	static int duration = 0;					\
	long long ___res = (res);					\
	bool ___ok = ___res == 0;					\
	CHECK(!___ok, (name), "unexpected error: %lld (errno %d)\n",	\
	      ___res, errno);						\
	___ok;								\
})

#define ASSERT_ERR(res, name) ({					\
	static int duration = 0;					\
	long long ___res = (res);					\
	bool ___ok = ___res < 0;					\
	CHECK(!___ok, (name), "unexpected success: %lld\n", ___res);	\
	___ok;								\
})

#define ASSERT_NULL(ptr, name) ({					\
	static int duration = 0;					\
	const void *___res = (ptr);					\
	bool ___ok = !___res;						\
	CHECK(!___ok, (name), "unexpected pointer: %p\n", ___res);	\
	___ok;								\
})

#define ASSERT_OK_PTR(ptr, name) ({					\
	static int duration = 0;					\
	const void *___res = (ptr);					\
	int ___err = libbpf_get_error(___res);				\
	bool ___ok = ___err == 0;					\
	CHECK(!___ok, (name), "unexpected error: %d\n", ___err);	\
	___ok;								\
})

#define ASSERT_ERR_PTR(ptr, name) ({					\
	static int duration = 0;					\
	const void *___res = (ptr);					\
	int ___err = libbpf_get_error(___res);				\
	bool ___ok = ___err != 0;					\
	CHECK(!___ok, (name), "unexpected pointer: %p\n", ___res);	\
	___ok;								\
})

static inline __u64 ptr_to_u64(const void *ptr)
{
	return (__u64) (unsigned long) ptr;
}

static inline void *u64_to_ptr(__u64 ptr)
{
	return (void *) (unsigned long) ptr;
}

int bpf_find_map(const char *test, struct bpf_object *obj, const char *name);
int compare_map_keys(int map1_fd, int map2_fd);
int compare_stack_ips(int smap_fd, int amap_fd, int stack_trace_len);
int extract_build_id(char *build_id, size_t size);
int kern_sync_rcu(void);
int trigger_module_test_read(int read_sz);
int trigger_module_test_write(int write_sz);
int write_sysctl(const char *sysctl, const char *value);
int get_bpf_max_tramp_links_from(struct btf *btf);
int get_bpf_max_tramp_links(void);

#ifdef __x86_64__
#define SYS_NANOSLEEP_KPROBE_NAME "__x64_sys_nanosleep"
#elif defined(__s390x__)
#define SYS_NANOSLEEP_KPROBE_NAME "__s390x_sys_nanosleep"
#elif defined(__aarch64__)
#define SYS_NANOSLEEP_KPROBE_NAME "__arm64_sys_nanosleep"
#else
#define SYS_NANOSLEEP_KPROBE_NAME "sys_nanosleep"
#endif

#define BPF_TESTMOD_TEST_FILE "/sys/kernel/bpf_testmod"

struct test_loader {
	char *log_buf;
	size_t log_buf_sz;

	struct bpf_object *obj;
};

typedef const void *(*skel_elf_bytes_fn)(size_t *sz);

extern void test_loader__run_subtests(struct test_loader *tester,
				      const char *skel_name,
				      skel_elf_bytes_fn elf_bytes_factory);

extern void test_loader_fini(struct test_loader *tester);

#define RUN_TESTS(skel) ({						       \
	struct test_loader tester = {};					       \
									       \
	test_loader__run_subtests(&tester, #skel, skel##__elf_bytes);	       \
	test_loader_fini(&tester);					       \
})

#endif /* __TEST_PROGS_H */
