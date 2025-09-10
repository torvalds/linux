// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>
#include "cgroup_helpers.h"
#include "cgroup_preorder.skel.h"

static int run_getsockopt_test(int cg_parent, int sock_fd, bool has_relative_fd)
{
	LIBBPF_OPTS(bpf_prog_attach_opts, opts);
	enum bpf_attach_type prog_p_atype, prog_p2_atype;
	int prog_p_fd, prog_p2_fd;
	struct cgroup_preorder *skel = NULL;
	struct bpf_program *prog;
	__u8 *result, buf;
	socklen_t optlen = 1;
	int err = 0;

	skel = cgroup_preorder__open_and_load();
	if (!ASSERT_OK_PTR(skel, "cgroup_preorder__open_and_load"))
		return 0;

	LIBBPF_OPTS_RESET(opts);
	opts.flags = BPF_F_ALLOW_MULTI;
	prog = skel->progs.parent;
	prog_p_fd = bpf_program__fd(prog);
	prog_p_atype = bpf_program__expected_attach_type(prog);
	err = bpf_prog_attach_opts(prog_p_fd, cg_parent, prog_p_atype, &opts);
	if (!ASSERT_OK(err, "bpf_prog_attach_opts-parent"))
		goto close_skel;

	opts.flags = BPF_F_ALLOW_MULTI | BPF_F_BEFORE;
	if (has_relative_fd)
		opts.relative_fd = prog_p_fd;
	prog = skel->progs.parent_2;
	prog_p2_fd = bpf_program__fd(prog);
	prog_p2_atype = bpf_program__expected_attach_type(prog);
	err = bpf_prog_attach_opts(prog_p2_fd, cg_parent, prog_p2_atype, &opts);
	if (!ASSERT_OK(err, "bpf_prog_attach_opts-parent_2"))
		goto detach_parent;

	err = getsockopt(sock_fd, SOL_IP, IP_TOS, &buf, &optlen);
	if (!ASSERT_OK(err, "getsockopt"))
		goto detach_parent_2;

	result = skel->bss->result;
	ASSERT_TRUE(result[0] == 4 && result[1] == 3, "result values");

detach_parent_2:
	ASSERT_OK(bpf_prog_detach2(prog_p2_fd, cg_parent, prog_p2_atype),
		  "bpf_prog_detach2-parent_2");
detach_parent:
	ASSERT_OK(bpf_prog_detach2(prog_p_fd, cg_parent, prog_p_atype),
		  "bpf_prog_detach2-parent");
close_skel:
	cgroup_preorder__destroy(skel);
	return err;
}

void test_cgroup_mprog_ordering(void)
{
	int cg_parent = -1, sock_fd = -1;

	cg_parent = test__join_cgroup("/parent");
	if (!ASSERT_GE(cg_parent, 0, "join_cgroup /parent"))
		goto out;

	sock_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (!ASSERT_GE(sock_fd, 0, "socket"))
		goto out;

	ASSERT_OK(run_getsockopt_test(cg_parent, sock_fd, false), "getsockopt_test_1");
	ASSERT_OK(run_getsockopt_test(cg_parent, sock_fd, true), "getsockopt_test_2");

out:
	close(sock_fd);
	close(cg_parent);
}
