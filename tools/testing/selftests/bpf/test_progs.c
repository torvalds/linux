/* Copyright (c) 2017 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#include <linux/types.h>
typedef __u16 __sum16;
#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>

#include <sys/wait.h>
#include <sys/resource.h>

#include <linux/bpf.h>
#include <linux/err.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#define _htons __builtin_bswap16

static int error_cnt, pass_cnt;

/* ipv4 test vector */
static struct {
	struct ethhdr eth;
	struct iphdr iph;
	struct tcphdr tcp;
} __packed pkt_v4 = {
	.eth.h_proto = _htons(ETH_P_IP),
	.iph.ihl = 5,
	.iph.protocol = 6,
	.tcp.urg_ptr = 123,
};

/* ipv6 test vector */
static struct {
	struct ethhdr eth;
	struct ipv6hdr iph;
	struct tcphdr tcp;
} __packed pkt_v6 = {
	.eth.h_proto = _htons(ETH_P_IPV6),
	.iph.nexthdr = 6,
	.tcp.urg_ptr = 123,
};

#define CHECK(condition, tag, format...) ({				\
	int __ret = !!(condition);					\
	if (__ret) {							\
		error_cnt++;						\
		printf("%s:FAIL:%s ", __func__, tag);			\
		printf(format);						\
	} else {							\
		pass_cnt++;						\
		printf("%s:PASS:%s %d nsec\n", __func__, tag, duration);\
	}								\
})

static int bpf_prog_load(const char *file, enum bpf_prog_type type,
			 struct bpf_object **pobj, int *prog_fd)
{
	struct bpf_program *prog;
	struct bpf_object *obj;
	int err;

	obj = bpf_object__open(file);
	if (IS_ERR(obj)) {
		error_cnt++;
		return -ENOENT;
	}

	prog = bpf_program__next(NULL, obj);
	if (!prog) {
		bpf_object__close(obj);
		error_cnt++;
		return -ENOENT;
	}

	bpf_program__set_type(prog, type);
	err = bpf_object__load(obj);
	if (err) {
		bpf_object__close(obj);
		error_cnt++;
		return -EINVAL;
	}

	*pobj = obj;
	*prog_fd = bpf_program__fd(prog);
	return 0;
}

static void test_pkt_access(void)
{
	const char *file = "./test_pkt_access.o";
	struct bpf_object *obj;
	__u32 duration, retval;
	int err, prog_fd;

	err = bpf_prog_load(file, BPF_PROG_TYPE_SCHED_CLS, &obj, &prog_fd);
	if (err)
		return;

	err = bpf_prog_test_run(prog_fd, 100000, &pkt_v4, sizeof(pkt_v4),
				NULL, NULL, &retval, &duration);
	CHECK(err || errno || retval, "ipv4",
	      "err %d errno %d retval %d duration %d\n",
	      err, errno, retval, duration);

	err = bpf_prog_test_run(prog_fd, 100000, &pkt_v6, sizeof(pkt_v6),
				NULL, NULL, &retval, &duration);
	CHECK(err || errno || retval, "ipv6",
	      "err %d errno %d retval %d duration %d\n",
	      err, errno, retval, duration);
	bpf_object__close(obj);
}

int main(void)
{
	struct rlimit rinf = { RLIM_INFINITY, RLIM_INFINITY };

	setrlimit(RLIMIT_MEMLOCK, &rinf);

	test_pkt_access();

	printf("Summary: %d PASSED, %d FAILED\n", pass_cnt, error_cnt);
	return 0;
}
