// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Facebook

#include <stdio.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <linux/filter.h>

#include <bpf/bpf.h>

#include "cgroup_helpers.h"
#include <bpf/bpf_endian.h>
#include "bpf_util.h"

#define CG_PATH		"/foo"
#define MAX_INSNS	512

char bpf_log_buf[BPF_LOG_BUF_SIZE];
static bool verbose = false;

struct sock_test {
	const char *descr;
	/* BPF prog properties */
	struct bpf_insn	insns[MAX_INSNS];
	enum bpf_attach_type expected_attach_type;
	enum bpf_attach_type attach_type;
	/* Socket properties */
	int domain;
	int type;
	/* Endpoint to bind() to */
	const char *ip;
	unsigned short port;
	unsigned short port_retry;
	/* Expected test result */
	enum {
		LOAD_REJECT,
		ATTACH_REJECT,
		BIND_REJECT,
		SUCCESS,
		RETRY_SUCCESS,
		RETRY_REJECT
	} result;
};

static struct sock_test tests[] = {
	{
		.descr = "bind4 load with invalid access: src_ip6",
		.insns = {
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_LDX_MEM(BPF_W, BPF_REG_7, BPF_REG_6,
				    offsetof(struct bpf_sock, src_ip6[0])),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.expected_attach_type = BPF_CGROUP_INET4_POST_BIND,
		.attach_type = BPF_CGROUP_INET4_POST_BIND,
		.result = LOAD_REJECT,
	},
	{
		.descr = "bind4 load with invalid access: mark",
		.insns = {
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_LDX_MEM(BPF_W, BPF_REG_7, BPF_REG_6,
				    offsetof(struct bpf_sock, mark)),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.expected_attach_type = BPF_CGROUP_INET4_POST_BIND,
		.attach_type = BPF_CGROUP_INET4_POST_BIND,
		.result = LOAD_REJECT,
	},
	{
		.descr = "bind6 load with invalid access: src_ip4",
		.insns = {
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_LDX_MEM(BPF_W, BPF_REG_7, BPF_REG_6,
				    offsetof(struct bpf_sock, src_ip4)),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.expected_attach_type = BPF_CGROUP_INET6_POST_BIND,
		.attach_type = BPF_CGROUP_INET6_POST_BIND,
		.result = LOAD_REJECT,
	},
	{
		.descr = "sock_create load with invalid access: src_port",
		.insns = {
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_LDX_MEM(BPF_W, BPF_REG_7, BPF_REG_6,
				    offsetof(struct bpf_sock, src_port)),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.expected_attach_type = BPF_CGROUP_INET_SOCK_CREATE,
		.attach_type = BPF_CGROUP_INET_SOCK_CREATE,
		.result = LOAD_REJECT,
	},
	{
		.descr = "sock_create load w/o expected_attach_type (compat mode)",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.expected_attach_type = 0,
		.attach_type = BPF_CGROUP_INET_SOCK_CREATE,
		.domain = AF_INET,
		.type = SOCK_STREAM,
		.ip = "127.0.0.1",
		.port = 8097,
		.result = SUCCESS,
	},
	{
		.descr = "sock_create load w/ expected_attach_type",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.expected_attach_type = BPF_CGROUP_INET_SOCK_CREATE,
		.attach_type = BPF_CGROUP_INET_SOCK_CREATE,
		.domain = AF_INET,
		.type = SOCK_STREAM,
		.ip = "127.0.0.1",
		.port = 8097,
		.result = SUCCESS,
	},
	{
		.descr = "attach type mismatch bind4 vs bind6",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.expected_attach_type = BPF_CGROUP_INET4_POST_BIND,
		.attach_type = BPF_CGROUP_INET6_POST_BIND,
		.result = ATTACH_REJECT,
	},
	{
		.descr = "attach type mismatch bind6 vs bind4",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.expected_attach_type = BPF_CGROUP_INET6_POST_BIND,
		.attach_type = BPF_CGROUP_INET4_POST_BIND,
		.result = ATTACH_REJECT,
	},
	{
		.descr = "attach type mismatch default vs bind4",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.expected_attach_type = 0,
		.attach_type = BPF_CGROUP_INET4_POST_BIND,
		.result = ATTACH_REJECT,
	},
	{
		.descr = "attach type mismatch bind6 vs sock_create",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.expected_attach_type = BPF_CGROUP_INET6_POST_BIND,
		.attach_type = BPF_CGROUP_INET_SOCK_CREATE,
		.result = ATTACH_REJECT,
	},
	{
		.descr = "bind4 reject all",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.expected_attach_type = BPF_CGROUP_INET4_POST_BIND,
		.attach_type = BPF_CGROUP_INET4_POST_BIND,
		.domain = AF_INET,
		.type = SOCK_STREAM,
		.ip = "0.0.0.0",
		.result = BIND_REJECT,
	},
	{
		.descr = "bind6 reject all",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.expected_attach_type = BPF_CGROUP_INET6_POST_BIND,
		.attach_type = BPF_CGROUP_INET6_POST_BIND,
		.domain = AF_INET6,
		.type = SOCK_STREAM,
		.ip = "::",
		.result = BIND_REJECT,
	},
	{
		.descr = "bind6 deny specific IP & port",
		.insns = {
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),

			/* if (ip == expected && port == expected) */
			BPF_LDX_MEM(BPF_W, BPF_REG_7, BPF_REG_6,
				    offsetof(struct bpf_sock, src_ip6[3])),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_7,
				    __bpf_constant_ntohl(0x00000001), 4),
			BPF_LDX_MEM(BPF_W, BPF_REG_7, BPF_REG_6,
				    offsetof(struct bpf_sock, src_port)),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_7, 0x2001, 2),

			/* return DENY; */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_JMP_A(1),

			/* else return ALLOW; */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.expected_attach_type = BPF_CGROUP_INET6_POST_BIND,
		.attach_type = BPF_CGROUP_INET6_POST_BIND,
		.domain = AF_INET6,
		.type = SOCK_STREAM,
		.ip = "::1",
		.port = 8193,
		.result = BIND_REJECT,
	},
	{
		.descr = "bind4 allow specific IP & port",
		.insns = {
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),

			/* if (ip == expected && port == expected) */
			BPF_LDX_MEM(BPF_W, BPF_REG_7, BPF_REG_6,
				    offsetof(struct bpf_sock, src_ip4)),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_7,
				    __bpf_constant_ntohl(0x7F000001), 4),
			BPF_LDX_MEM(BPF_W, BPF_REG_7, BPF_REG_6,
				    offsetof(struct bpf_sock, src_port)),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_7, 0x1002, 2),

			/* return ALLOW; */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_JMP_A(1),

			/* else return DENY; */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.expected_attach_type = BPF_CGROUP_INET4_POST_BIND,
		.attach_type = BPF_CGROUP_INET4_POST_BIND,
		.domain = AF_INET,
		.type = SOCK_STREAM,
		.ip = "127.0.0.1",
		.port = 4098,
		.result = SUCCESS,
	},
	{
		.descr = "bind4 deny specific IP & port of TCP, and retry",
		.insns = {
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),

			/* if (ip == expected && port == expected) */
			BPF_LDX_MEM(BPF_W, BPF_REG_7, BPF_REG_6,
				    offsetof(struct bpf_sock, src_ip4)),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_7,
				    __bpf_constant_ntohl(0x7F000001), 4),
			BPF_LDX_MEM(BPF_W, BPF_REG_7, BPF_REG_6,
				    offsetof(struct bpf_sock, src_port)),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_7, 0x1002, 2),

			/* return DENY; */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_JMP_A(1),

			/* else return ALLOW; */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.expected_attach_type = BPF_CGROUP_INET4_POST_BIND,
		.attach_type = BPF_CGROUP_INET4_POST_BIND,
		.domain = AF_INET,
		.type = SOCK_STREAM,
		.ip = "127.0.0.1",
		.port = 4098,
		.port_retry = 5000,
		.result = RETRY_SUCCESS,
	},
	{
		.descr = "bind4 deny specific IP & port of UDP, and retry",
		.insns = {
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),

			/* if (ip == expected && port == expected) */
			BPF_LDX_MEM(BPF_W, BPF_REG_7, BPF_REG_6,
				    offsetof(struct bpf_sock, src_ip4)),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_7,
				    __bpf_constant_ntohl(0x7F000001), 4),
			BPF_LDX_MEM(BPF_W, BPF_REG_7, BPF_REG_6,
				    offsetof(struct bpf_sock, src_port)),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_7, 0x1002, 2),

			/* return DENY; */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_JMP_A(1),

			/* else return ALLOW; */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.expected_attach_type = BPF_CGROUP_INET4_POST_BIND,
		.attach_type = BPF_CGROUP_INET4_POST_BIND,
		.domain = AF_INET,
		.type = SOCK_DGRAM,
		.ip = "127.0.0.1",
		.port = 4098,
		.port_retry = 5000,
		.result = RETRY_SUCCESS,
	},
	{
		.descr = "bind6 deny specific IP & port, and retry",
		.insns = {
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),

			/* if (ip == expected && port == expected) */
			BPF_LDX_MEM(BPF_W, BPF_REG_7, BPF_REG_6,
				    offsetof(struct bpf_sock, src_ip6[3])),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_7,
				    __bpf_constant_ntohl(0x00000001), 4),
			BPF_LDX_MEM(BPF_W, BPF_REG_7, BPF_REG_6,
				    offsetof(struct bpf_sock, src_port)),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_7, 0x2001, 2),

			/* return DENY; */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_JMP_A(1),

			/* else return ALLOW; */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.expected_attach_type = BPF_CGROUP_INET6_POST_BIND,
		.attach_type = BPF_CGROUP_INET6_POST_BIND,
		.domain = AF_INET6,
		.type = SOCK_STREAM,
		.ip = "::1",
		.port = 8193,
		.port_retry = 9000,
		.result = RETRY_SUCCESS,
	},
	{
		.descr = "bind4 allow all",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.expected_attach_type = BPF_CGROUP_INET4_POST_BIND,
		.attach_type = BPF_CGROUP_INET4_POST_BIND,
		.domain = AF_INET,
		.type = SOCK_STREAM,
		.ip = "0.0.0.0",
		.result = SUCCESS,
	},
	{
		.descr = "bind6 allow all",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.expected_attach_type = BPF_CGROUP_INET6_POST_BIND,
		.attach_type = BPF_CGROUP_INET6_POST_BIND,
		.domain = AF_INET6,
		.type = SOCK_STREAM,
		.ip = "::",
		.result = SUCCESS,
	},
};

static size_t probe_prog_length(const struct bpf_insn *fp)
{
	size_t len;

	for (len = MAX_INSNS - 1; len > 0; --len)
		if (fp[len].code != 0 || fp[len].imm != 0)
			break;
	return len + 1;
}

static int load_sock_prog(const struct bpf_insn *prog,
			  enum bpf_attach_type attach_type)
{
	LIBBPF_OPTS(bpf_prog_load_opts, opts);
	int ret, insn_cnt;

	insn_cnt = probe_prog_length(prog);

	opts.expected_attach_type = attach_type;
	opts.log_buf = bpf_log_buf;
	opts.log_size = BPF_LOG_BUF_SIZE;
	opts.log_level = 2;

	ret = bpf_prog_load(BPF_PROG_TYPE_CGROUP_SOCK, NULL, "GPL", prog, insn_cnt, &opts);
	if (verbose && ret < 0)
		fprintf(stderr, "%s\n", bpf_log_buf);

	return ret;
}

static int attach_sock_prog(int cgfd, int progfd,
			    enum bpf_attach_type attach_type)
{
	return bpf_prog_attach(progfd, cgfd, attach_type, BPF_F_ALLOW_OVERRIDE);
}

static int bind_sock(int domain, int type, const char *ip,
		     unsigned short port, unsigned short port_retry)
{
	struct sockaddr_storage addr;
	struct sockaddr_in6 *addr6;
	struct sockaddr_in *addr4;
	int sockfd = -1;
	socklen_t len;
	int res = SUCCESS;

	sockfd = socket(domain, type, 0);
	if (sockfd < 0)
		goto err;

	memset(&addr, 0, sizeof(addr));

	if (domain == AF_INET) {
		len = sizeof(struct sockaddr_in);
		addr4 = (struct sockaddr_in *)&addr;
		addr4->sin_family = domain;
		addr4->sin_port = htons(port);
		if (inet_pton(domain, ip, (void *)&addr4->sin_addr) != 1)
			goto err;
	} else if (domain == AF_INET6) {
		len = sizeof(struct sockaddr_in6);
		addr6 = (struct sockaddr_in6 *)&addr;
		addr6->sin6_family = domain;
		addr6->sin6_port = htons(port);
		if (inet_pton(domain, ip, (void *)&addr6->sin6_addr) != 1)
			goto err;
	} else {
		goto err;
	}

	if (bind(sockfd, (const struct sockaddr *)&addr, len) == -1) {
		/* sys_bind() may fail for different reasons, errno has to be
		 * checked to confirm that BPF program rejected it.
		 */
		if (errno != EPERM)
			goto err;
		if (port_retry)
			goto retry;
		res = BIND_REJECT;
		goto out;
	}

	goto out;
retry:
	if (domain == AF_INET)
		addr4->sin_port = htons(port_retry);
	else
		addr6->sin6_port = htons(port_retry);
	if (bind(sockfd, (const struct sockaddr *)&addr, len) == -1) {
		if (errno != EPERM)
			goto err;
		res = RETRY_REJECT;
	} else {
		res = RETRY_SUCCESS;
	}
	goto out;
err:
	res = -1;
out:
	close(sockfd);
	return res;
}

static int run_test_case(int cgfd, const struct sock_test *test)
{
	int progfd = -1;
	int err = 0;
	int res;

	printf("Test case: %s .. ", test->descr);
	progfd = load_sock_prog(test->insns, test->expected_attach_type);
	if (progfd < 0) {
		if (test->result == LOAD_REJECT)
			goto out;
		else
			goto err;
	}

	if (attach_sock_prog(cgfd, progfd, test->attach_type) < 0) {
		if (test->result == ATTACH_REJECT)
			goto out;
		else
			goto err;
	}

	res = bind_sock(test->domain, test->type, test->ip, test->port,
			test->port_retry);
	if (res > 0 && test->result == res)
		goto out;

err:
	err = -1;
out:
	/* Detaching w/o checking return code: best effort attempt. */
	if (progfd != -1)
		bpf_prog_detach(cgfd, test->attach_type);
	close(progfd);
	printf("[%s]\n", err ? "FAIL" : "PASS");
	return err;
}

static int run_tests(int cgfd)
{
	int passes = 0;
	int fails = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(tests); ++i) {
		if (run_test_case(cgfd, &tests[i]))
			++fails;
		else
			++passes;
	}
	printf("Summary: %d PASSED, %d FAILED\n", passes, fails);
	return fails ? -1 : 0;
}

int main(int argc, char **argv)
{
	int cgfd = -1;
	int err = 0;

	cgfd = cgroup_setup_and_join(CG_PATH);
	if (cgfd < 0)
		goto err;

	/* Use libbpf 1.0 API mode */
	libbpf_set_strict_mode(LIBBPF_STRICT_ALL);

	if (run_tests(cgfd))
		goto err;

	goto out;
err:
	err = -1;
out:
	close(cgfd);
	cleanup_cgroup_environment();
	return err;
}
