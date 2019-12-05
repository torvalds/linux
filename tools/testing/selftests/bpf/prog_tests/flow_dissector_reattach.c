// SPDX-License-Identifier: GPL-2.0
/*
 * Test that the flow_dissector program can be updated with a single
 * syscall by attaching a new program that replaces the existing one.
 *
 * Corner case - the same program cannot be attached twice.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdbool.h>
#include <unistd.h>

#include <linux/bpf.h>
#include <bpf/bpf.h>

#include "test_progs.h"

static bool is_attached(int netns)
{
	__u32 cnt;
	int err;

	err = bpf_prog_query(netns, BPF_FLOW_DISSECTOR, 0, NULL, NULL, &cnt);
	if (CHECK_FAIL(err)) {
		perror("bpf_prog_query");
		return true; /* fail-safe */
	}

	return cnt > 0;
}

static int load_prog(void)
{
	struct bpf_insn prog[] = {
		BPF_MOV64_IMM(BPF_REG_0, BPF_OK),
		BPF_EXIT_INSN(),
	};
	int fd;

	fd = bpf_load_program(BPF_PROG_TYPE_FLOW_DISSECTOR, prog,
			      ARRAY_SIZE(prog), "GPL", 0, NULL, 0);
	if (CHECK_FAIL(fd < 0))
		perror("bpf_load_program");

	return fd;
}

static void do_flow_dissector_reattach(void)
{
	int prog_fd[2] = { -1, -1 };
	int err;

	prog_fd[0] = load_prog();
	if (prog_fd[0] < 0)
		return;

	prog_fd[1] = load_prog();
	if (prog_fd[1] < 0)
		goto out_close;

	err = bpf_prog_attach(prog_fd[0], 0, BPF_FLOW_DISSECTOR, 0);
	if (CHECK_FAIL(err)) {
		perror("bpf_prog_attach-0");
		goto out_close;
	}

	/* Expect success when attaching a different program */
	err = bpf_prog_attach(prog_fd[1], 0, BPF_FLOW_DISSECTOR, 0);
	if (CHECK_FAIL(err)) {
		perror("bpf_prog_attach-1");
		goto out_detach;
	}

	/* Expect failure when attaching the same program twice */
	err = bpf_prog_attach(prog_fd[1], 0, BPF_FLOW_DISSECTOR, 0);
	if (CHECK_FAIL(!err || errno != EINVAL))
		perror("bpf_prog_attach-2");

out_detach:
	err = bpf_prog_detach(0, BPF_FLOW_DISSECTOR);
	if (CHECK_FAIL(err))
		perror("bpf_prog_detach");

out_close:
	close(prog_fd[1]);
	close(prog_fd[0]);
}

void test_flow_dissector_reattach(void)
{
	int init_net, self_net, err;

	self_net = open("/proc/self/ns/net", O_RDONLY);
	if (CHECK_FAIL(self_net < 0)) {
		perror("open(/proc/self/ns/net");
		return;
	}

	init_net = open("/proc/1/ns/net", O_RDONLY);
	if (CHECK_FAIL(init_net < 0)) {
		perror("open(/proc/1/ns/net)");
		goto out_close;
	}

	err = setns(init_net, CLONE_NEWNET);
	if (CHECK_FAIL(err)) {
		perror("setns(/proc/1/ns/net)");
		goto out_close;
	}

	if (is_attached(init_net)) {
		test__skip();
		printf("Can't test with flow dissector attached to init_net\n");
		goto out_setns;
	}

	/* First run tests in root network namespace */
	do_flow_dissector_reattach();

	/* Then repeat tests in a non-root namespace */
	err = unshare(CLONE_NEWNET);
	if (CHECK_FAIL(err)) {
		perror("unshare(CLONE_NEWNET)");
		goto out_setns;
	}
	do_flow_dissector_reattach();

out_setns:
	/* Move back to netns we started in. */
	err = setns(self_net, CLONE_NEWNET);
	if (CHECK_FAIL(err))
		perror("setns(/proc/self/ns/net)");

out_close:
	close(init_net);
	close(self_net);
}
