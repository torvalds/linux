/* SPDX-License-Identifier: GPL-2.0 */
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
#include "flow_dissector_load.h"

enum verbosity {
	VERBOSE_NONE,
	VERBOSE_NORMAL,
	VERBOSE_VERY,
	VERBOSE_SUPER,
};

struct str_set {
	const char **strs;
	int cnt;
};

struct test_selector {
	struct str_set whitelist;
	struct str_set blacklist;
	bool *num_set;
	int num_set_len;
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

	struct prog_test_def *test; /* current running tests */

	FILE *stdout;
	FILE *stderr;
	char *log_buf;
	size_t log_cnt;
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
enum msg_type {
	MSG_DO_TEST = 0,
	MSG_TEST_DONE = 1,
	MSG_TEST_LOG = 2,
	MSG_EXIT = 255,
};
struct msg {
	enum msg_type type;
	union {
		struct {
			int test_num;
		} do_test;
		struct {
			int test_num;
			int sub_succ_cnt;
			int error_cnt;
			int skip_cnt;
			bool have_log;
		} test_done;
		struct {
			char log_buf[MAX_LOG_TRUNK_SIZE + 1];
			bool is_last;
		} test_log;
	};
};

extern struct test_env env;

extern void test__force_log();
extern bool test__start_subtest(const char *name);
extern void test__skip(void);
extern void test__fail(void);
extern int test__join_cgroup(const char *path);

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
