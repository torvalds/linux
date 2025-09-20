// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <test_progs.h>
#include "cgroup_helpers.h"

#define TEST_NS "sock_post_bind"

static char bpf_log_buf[4096];

static struct sock_post_bind_test {
	const char			*descr;
	/* BPF prog properties */
	const struct bpf_insn		insns[64];
	enum bpf_attach_type		attach_type;
	enum bpf_attach_type		expected_attach_type;
	/* Socket properties */
	int				domain;
	int				type;
	/* Endpoint to bind() to */
	const char *ip;
	unsigned short port;
	unsigned short port_retry;

	/* Expected test result */
	enum {
		ATTACH_REJECT,
		BIND_REJECT,
		SUCCESS,
		RETRY_SUCCESS,
		RETRY_REJECT
	} result;
} tests[] = {
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

static int load_prog(const struct bpf_insn *insns,
		     enum bpf_attach_type expected_attach_type)
{
	LIBBPF_OPTS(bpf_prog_load_opts, opts,
		    .expected_attach_type = expected_attach_type,
		    .log_level = 2,
		    .log_buf = bpf_log_buf,
		    .log_size = sizeof(bpf_log_buf),
	);
	int fd, insns_cnt = 0;

	for (;
	     insns[insns_cnt].code != (BPF_JMP | BPF_EXIT);
	     insns_cnt++) {
	}
	insns_cnt++;

	fd = bpf_prog_load(BPF_PROG_TYPE_CGROUP_SOCK, NULL, "GPL", insns,
			   insns_cnt, &opts);
	if (fd < 0)
		fprintf(stderr, "%s\n", bpf_log_buf);

	return fd;
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

static int run_test(int cgroup_fd, struct sock_post_bind_test *test)
{
	int err, prog_fd, res, ret = 0;

	prog_fd = load_prog(test->insns, test->expected_attach_type);
	if (prog_fd < 0)
		goto err;

	err = bpf_prog_attach(prog_fd, cgroup_fd, test->attach_type, 0);
	if (err < 0) {
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
	ret = -1;
out:
	/* Detaching w/o checking return code: best effort attempt. */
	if (prog_fd != -1)
		bpf_prog_detach(cgroup_fd, test->attach_type);
	close(prog_fd);
	return ret;
}

void test_sock_post_bind(void)
{
	struct netns_obj *ns;
	int cgroup_fd;
	int i;

	cgroup_fd = test__join_cgroup("/post_bind");
	if (!ASSERT_OK_FD(cgroup_fd, "join_cgroup"))
		return;

	ns = netns_new(TEST_NS, true);
	if (!ASSERT_OK_PTR(ns, "netns_new"))
		goto cleanup;

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		if (!test__start_subtest(tests[i].descr))
			continue;

		ASSERT_OK(run_test(cgroup_fd, &tests[i]), tests[i].descr);
	}

cleanup:
	netns_free(ns);
	close(cgroup_fd);
}
