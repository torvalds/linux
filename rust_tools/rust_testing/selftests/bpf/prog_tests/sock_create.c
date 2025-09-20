// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <test_progs.h>
#include "cgroup_helpers.h"

static char bpf_log_buf[4096];
static bool verbose;

enum sock_create_test_error {
	OK = 0,
	DENY_CREATE,
};

static struct sock_create_test {
	const char			*descr;
	const struct bpf_insn		insns[64];
	enum bpf_attach_type		attach_type;
	enum bpf_attach_type		expected_attach_type;

	int				domain;
	int				type;
	int				protocol;

	int				optname;
	int				optval;
	enum sock_create_test_error	error;
} tests[] = {
	{
		.descr = "AF_INET set priority",
		.insns = {
			/* r3 = 123 (priority) */
			BPF_MOV64_IMM(BPF_REG_3, 123),
			BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_3,
				    offsetof(struct bpf_sock, priority)),

			/* return 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.expected_attach_type = BPF_CGROUP_INET_SOCK_CREATE,
		.attach_type = BPF_CGROUP_INET_SOCK_CREATE,

		.domain = AF_INET,
		.type = SOCK_DGRAM,

		.optname = SO_PRIORITY,
		.optval = 123,
	},
	{
		.descr = "AF_INET6 set priority",
		.insns = {
			/* r3 = 123 (priority) */
			BPF_MOV64_IMM(BPF_REG_3, 123),
			BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_3,
				    offsetof(struct bpf_sock, priority)),

			/* return 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.expected_attach_type = BPF_CGROUP_INET_SOCK_CREATE,
		.attach_type = BPF_CGROUP_INET_SOCK_CREATE,

		.domain = AF_INET6,
		.type = SOCK_DGRAM,

		.optname = SO_PRIORITY,
		.optval = 123,
	},
	{
		.descr = "AF_INET set mark",
		.insns = {
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),

			/* get uid of process */
			BPF_EMIT_CALL(BPF_FUNC_get_current_uid_gid),
			BPF_ALU64_IMM(BPF_AND, BPF_REG_0, 0xffffffff),

			/* if uid is 0, use given mark(666), else use uid as the mark */
			BPF_MOV64_REG(BPF_REG_3, BPF_REG_0),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 1),
			BPF_MOV64_IMM(BPF_REG_3, 666),

			BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
			BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_3,
				    offsetof(struct bpf_sock, mark)),

			/* return 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.expected_attach_type = BPF_CGROUP_INET_SOCK_CREATE,
		.attach_type = BPF_CGROUP_INET_SOCK_CREATE,

		.domain = AF_INET,
		.type = SOCK_DGRAM,

		.optname = SO_MARK,
		.optval = 666,
	},
	{
		.descr = "AF_INET6 set mark",
		.insns = {
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),

			/* get uid of process */
			BPF_EMIT_CALL(BPF_FUNC_get_current_uid_gid),
			BPF_ALU64_IMM(BPF_AND, BPF_REG_0, 0xffffffff),

			/* if uid is 0, use given mark(666), else use uid as the mark */
			BPF_MOV64_REG(BPF_REG_3, BPF_REG_0),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 1),
			BPF_MOV64_IMM(BPF_REG_3, 666),

			BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
			BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_3,
				    offsetof(struct bpf_sock, mark)),

			/* return 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.expected_attach_type = BPF_CGROUP_INET_SOCK_CREATE,
		.attach_type = BPF_CGROUP_INET_SOCK_CREATE,

		.domain = AF_INET6,
		.type = SOCK_DGRAM,

		.optname = SO_MARK,
		.optval = 666,
	},
	{
		.descr = "AF_INET bound to iface",
		.insns = {
			/* r3 = 1 (lo interface) */
			BPF_MOV64_IMM(BPF_REG_3, 1),
			BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_3,
				    offsetof(struct bpf_sock, bound_dev_if)),

			/* return 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.expected_attach_type = BPF_CGROUP_INET_SOCK_CREATE,
		.attach_type = BPF_CGROUP_INET_SOCK_CREATE,

		.domain = AF_INET,
		.type = SOCK_DGRAM,

		.optname = SO_BINDTOIFINDEX,
		.optval = 1,
	},
	{
		.descr = "AF_INET6 bound to iface",
		.insns = {
			/* r3 = 1 (lo interface) */
			BPF_MOV64_IMM(BPF_REG_3, 1),
			BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_3,
				    offsetof(struct bpf_sock, bound_dev_if)),

			/* return 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.expected_attach_type = BPF_CGROUP_INET_SOCK_CREATE,
		.attach_type = BPF_CGROUP_INET_SOCK_CREATE,

		.domain = AF_INET6,
		.type = SOCK_DGRAM,

		.optname = SO_BINDTOIFINDEX,
		.optval = 1,
	},
	{
		.descr = "block AF_INET, SOCK_DGRAM, IPPROTO_ICMP socket",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 1),	/* r0 = verdict */

			/* sock->family == AF_INET */
			BPF_LDX_MEM(BPF_H, BPF_REG_2, BPF_REG_1,
				    offsetof(struct bpf_sock, family)),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_2, AF_INET, 5),

			/* sock->type == SOCK_DGRAM */
			BPF_LDX_MEM(BPF_H, BPF_REG_2, BPF_REG_1,
				    offsetof(struct bpf_sock, type)),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_2, SOCK_DGRAM, 3),

			/* sock->protocol == IPPROTO_ICMP */
			BPF_LDX_MEM(BPF_H, BPF_REG_2, BPF_REG_1,
				    offsetof(struct bpf_sock, protocol)),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_2, IPPROTO_ICMP, 1),

			/* return 0 (block) */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.expected_attach_type = BPF_CGROUP_INET_SOCK_CREATE,
		.attach_type = BPF_CGROUP_INET_SOCK_CREATE,

		.domain = AF_INET,
		.type = SOCK_DGRAM,
		.protocol = IPPROTO_ICMP,

		.error = DENY_CREATE,
	},
	{
		.descr = "block AF_INET6, SOCK_DGRAM, IPPROTO_ICMPV6 socket",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 1),	/* r0 = verdict */

			/* sock->family == AF_INET6 */
			BPF_LDX_MEM(BPF_H, BPF_REG_2, BPF_REG_1,
				    offsetof(struct bpf_sock, family)),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_2, AF_INET6, 5),

			/* sock->type == SOCK_DGRAM */
			BPF_LDX_MEM(BPF_H, BPF_REG_2, BPF_REG_1,
				    offsetof(struct bpf_sock, type)),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_2, SOCK_DGRAM, 3),

			/* sock->protocol == IPPROTO_ICMPV6 */
			BPF_LDX_MEM(BPF_H, BPF_REG_2, BPF_REG_1,
				    offsetof(struct bpf_sock, protocol)),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_2, IPPROTO_ICMPV6, 1),

			/* return 0 (block) */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.expected_attach_type = BPF_CGROUP_INET_SOCK_CREATE,
		.attach_type = BPF_CGROUP_INET_SOCK_CREATE,

		.domain = AF_INET,
		.type = SOCK_DGRAM,
		.protocol = IPPROTO_ICMPV6,

		.error = DENY_CREATE,
	},
	{
		.descr = "load w/o expected_attach_type (compat mode)",
		.insns = {
			/* return 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.expected_attach_type = 0,
		.attach_type = BPF_CGROUP_INET_SOCK_CREATE,

		.domain = AF_INET,
		.type = SOCK_STREAM,
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
	if (verbose && fd < 0)
		fprintf(stderr, "%s\n", bpf_log_buf);

	return fd;
}

static int run_test(int cgroup_fd, struct sock_create_test *test)
{
	int sock_fd, err, prog_fd, optval, ret = -1;
	socklen_t optlen = sizeof(optval);

	prog_fd = load_prog(test->insns, test->expected_attach_type);
	if (prog_fd < 0) {
		log_err("Failed to load BPF program");
		return -1;
	}

	err = bpf_prog_attach(prog_fd, cgroup_fd, test->attach_type, 0);
	if (err < 0) {
		log_err("Failed to attach BPF program");
		goto close_prog_fd;
	}

	sock_fd = socket(test->domain, test->type, test->protocol);
	if (sock_fd < 0) {
		if (test->error == DENY_CREATE)
			ret = 0;
		else
			log_err("Failed to create socket");

		goto detach_prog;
	}

	if (test->optname) {
		err = getsockopt(sock_fd, SOL_SOCKET, test->optname, &optval, &optlen);
		if (err) {
			log_err("Failed to call getsockopt");
			goto cleanup;
		}

		if (optval != test->optval) {
			errno = 0;
			log_err("getsockopt returned unexpected optval");
			goto cleanup;
		}
	}

	ret = test->error != OK;

cleanup:
	close(sock_fd);
detach_prog:
	bpf_prog_detach2(prog_fd, cgroup_fd, test->attach_type);
close_prog_fd:
	close(prog_fd);
	return ret;
}

void test_sock_create(void)
{
	int cgroup_fd, i;

	cgroup_fd = test__join_cgroup("/sock_create");
	if (!ASSERT_GE(cgroup_fd, 0, "join_cgroup"))
		return;

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		if (!test__start_subtest(tests[i].descr))
			continue;

		ASSERT_OK(run_test(cgroup_fd, &tests[i]), tests[i].descr);
	}

	close(cgroup_fd);
}
