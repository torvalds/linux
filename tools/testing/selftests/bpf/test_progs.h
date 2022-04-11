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
#include <netinet/tcp.h>
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
#include "bpf_endian.h"
#include "trace_helpers.h"
#include "flow_dissector_load.h"

struct test_selector {
	const char *name;
	bool *num_set;
	int num_set_len;
};

struct test_env {
	struct test_selector test_selector;
	struct test_selector subtest_selector;
	bool verifier_stats;
	bool verbose;
	bool very_verbose;

	bool jit_enabled;

	struct prog_test_def *test;
	FILE *stdout;
	FILE *stderr;
	char *log_buf;
	size_t log_cnt;

	int succ_cnt; /* successful tests */
	int sub_succ_cnt; /* successful sub-tests */
	int fail_cnt; /* total failed tests + sub-tests */
	int skip_cnt; /* skipped tests */
};

extern struct test_env env;

extern void test__force_log();
extern bool test__start_subtest(const char *name);
extern void test__skip(void);
extern void test__fail(void);
extern int test__join_cgroup(const char *path);

#define MAGIC_BYTES 123

/* ipv4 test vector */
struct ipv4_packet {
	struct ethhdr eth;
	struct iphdr iph;
	struct tcphdr tcp;
} __packed;
extern struct ipv4_packet pkt_v4;

/* ipv6 test vector */
struct ipv6_packet {
	struct ethhdr eth;
	struct ipv6hdr iph;
	struct tcphdr tcp;
} __packed;
extern struct ipv6_packet pkt_v6;

#define _CHECK(condition, tag, duration, format...) ({			\
	int __ret = !!(condition);					\
	if (__ret) {							\
		test__fail();						\
		printf("%s:FAIL:%s ", __func__, tag);			\
		printf(format);						\
	} else {							\
		printf("%s:PASS:%s %d nsec\n",				\
		       __func__, tag, duration);			\
	}								\
	__ret;								\
})

#define CHECK_FAIL(condition) ({					\
	int __ret = !!(condition);					\
	if (__ret) {							\
		test__fail();						\
		printf("%s:FAIL:%d\n", __func__, __LINE__);		\
	}								\
	__ret;								\
})

#define CHECK(condition, tag, format...) \
	_CHECK(condition, tag, duration, format)
#define CHECK_ATTR(condition, tag, format...) \
	_CHECK(condition, tag, tattr.duration, format)

#define MAGIC_VAL 0x1234
#define NUM_ITER 100000
#define VIP_NUM 5

static inline __u64 ptr_to_u64(const void *ptr)
{
	return (__u64) (unsigned long) ptr;
}

int bpf_find_map(const char *test, struct bpf_object *obj, const char *name);
int compare_map_keys(int map1_fd, int map2_fd);
int compare_stack_ips(int smap_fd, int amap_fd, int stack_trace_len);
int extract_build_id(char *build_id, size_t size);
void *spin_lock_thread(void *arg);

#ifdef __x86_64__
#define SYS_NANOSLEEP_KPROBE_NAME "__x64_sys_nanosleep"
#elif defined(__s390x__)
#define SYS_NANOSLEEP_KPROBE_NAME "__s390x_sys_nanosleep"
#else
#define SYS_NANOSLEEP_KPROBE_NAME "sys_nanosleep"
#endif
