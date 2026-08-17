// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>
#include "cgroup_helpers.h"
#include "cgroup_preorder.skel.h"

static int run_getsockopt_test(int cg_parent, int cg_child, int sock_fd, bool all_preorder)
{
	LIBBPF_OPTS(bpf_prog_attach_opts, opts);
	enum bpf_attach_type prog_c_atype, prog_c2_atype, prog_p_atype, prog_p2_atype;
	int prog_c_fd, prog_c2_fd, prog_p_fd, prog_p2_fd;
	struct cgroup_preorder *skel = NULL;
	struct bpf_program *prog;
	__u8 *result, buf;
	socklen_t optlen;
	int err = 0;

	skel = cgroup_preorder__open_and_load();
	if (!ASSERT_OK_PTR(skel, "cgroup_preorder__open_and_load"))
		return 0;

	buf = 0x00;
	err = setsockopt(sock_fd, SOL_IP, IP_TOS, &buf, 1);
	if (!ASSERT_OK(err, "setsockopt"))
		goto close_skel;

	opts.flags = BPF_F_ALLOW_MULTI;
	if (all_preorder)
		opts.flags |= BPF_F_PREORDER;
	prog = skel->progs.child;
	prog_c_fd = bpf_program__fd(prog);
	prog_c_atype = bpf_program__expected_attach_type(prog);
	err = bpf_prog_attach_opts(prog_c_fd, cg_child, prog_c_atype, &opts);
	if (!ASSERT_OK(err, "bpf_prog_attach_opts-child"))
		goto close_skel;

	opts.flags = BPF_F_ALLOW_MULTI | BPF_F_PREORDER;
	prog = skel->progs.child_2;
	prog_c2_fd = bpf_program__fd(prog);
	prog_c2_atype = bpf_program__expected_attach_type(prog);
	err = bpf_prog_attach_opts(prog_c2_fd, cg_child, prog_c2_atype, &opts);
	if (!ASSERT_OK(err, "bpf_prog_attach_opts-child_2"))
		goto detach_child;

	optlen = 1;
	err = getsockopt(sock_fd, SOL_IP, IP_TOS, &buf, &optlen);
	if (!ASSERT_OK(err, "getsockopt"))
		goto detach_child_2;

	result = skel->bss->result;
	if (all_preorder)
		ASSERT_TRUE(result[0] == 1 && result[1] == 2, "child only");
	else
		ASSERT_TRUE(result[0] == 2 && result[1] == 1, "child only");

	skel->bss->idx = 0;
	memset(result, 0, 4);

	opts.flags = BPF_F_ALLOW_MULTI;
	if (all_preorder)
		opts.flags |= BPF_F_PREORDER;
	prog = skel->progs.parent;
	prog_p_fd = bpf_program__fd(prog);
	prog_p_atype = bpf_program__expected_attach_type(prog);
	err = bpf_prog_attach_opts(prog_p_fd, cg_parent, prog_p_atype, &opts);
	if (!ASSERT_OK(err, "bpf_prog_attach_opts-parent"))
		goto detach_child_2;

	opts.flags = BPF_F_ALLOW_MULTI | BPF_F_PREORDER;
	prog = skel->progs.parent_2;
	prog_p2_fd = bpf_program__fd(prog);
	prog_p2_atype = bpf_program__expected_attach_type(prog);
	err = bpf_prog_attach_opts(prog_p2_fd, cg_parent, prog_p2_atype, &opts);
	if (!ASSERT_OK(err, "bpf_prog_attach_opts-parent_2"))
		goto detach_parent;

	err = getsockopt(sock_fd, SOL_IP, IP_TOS, &buf, &optlen);
	if (!ASSERT_OK(err, "getsockopt"))
		goto detach_parent_2;

	if (all_preorder)
		ASSERT_TRUE(result[0] == 3 && result[1] == 4 && result[2] == 1 && result[3] == 2,
			    "parent and child");
	else
		ASSERT_TRUE(result[0] == 4 && result[1] == 2 && result[2] == 1 && result[3] == 3,
			    "parent and child");

detach_parent_2:
	ASSERT_OK(bpf_prog_detach2(prog_p2_fd, cg_parent, prog_p2_atype),
		  "bpf_prog_detach2-parent_2");
detach_parent:
	ASSERT_OK(bpf_prog_detach2(prog_p_fd, cg_parent, prog_p_atype),
		  "bpf_prog_detach2-parent");
detach_child_2:
	ASSERT_OK(bpf_prog_detach2(prog_c2_fd, cg_child, prog_c2_atype),
		  "bpf_prog_detach2-child_2");
detach_child:
	ASSERT_OK(bpf_prog_detach2(prog_c_fd, cg_child, prog_c_atype),
		  "bpf_prog_detach2-child");
close_skel:
	cgroup_preorder__destroy(skel);
	return err;
}

/*
 * Replacing a link's program (bpf_link_update) must target the correct slot in
 * the effective array even when a BPF_F_PREORDER program is attached to the
 * same cgroup. All programs here are attached to a single cgroup; "parent" is
 * reused only as a third distinct program.
 *
 * Attach child(1) normally and child_2(2) with BPF_F_PREORDER, so the effective
 * order is [2, 1]. Then replace child(1)'s program with parent(3): only the
 * non-preorder slot changes, giving [2, 3].
 */
static int run_link_replace_test(int cgroup_fd, int sock_fd)
{
	LIBBPF_OPTS(bpf_link_create_opts, create_opts);
	int err = 0, normal_link = -1, preorder_link = -1;
	struct cgroup_preorder *skel = NULL;
	enum bpf_attach_type atype;
	__u8 *result, buf = 0x00;
	socklen_t optlen = 1;

	skel = cgroup_preorder__open_and_load();
	if (!ASSERT_OK_PTR(skel, "cgroup_preorder__open_and_load"))
		return -1;

	err = setsockopt(sock_fd, SOL_IP, IP_TOS, &buf, 1);
	if (!ASSERT_OK(err, "setsockopt"))
		goto close_skel;

	atype = bpf_program__expected_attach_type(skel->progs.child);

	create_opts.flags = 0;
	normal_link = bpf_link_create(bpf_program__fd(skel->progs.child),
				      cgroup_fd, atype, &create_opts);
	if (!ASSERT_GE(normal_link, 0, "create_normal_link")) {
		err = normal_link;
		goto close_skel;
	}

	create_opts.flags = BPF_F_PREORDER;
	preorder_link = bpf_link_create(bpf_program__fd(skel->progs.child_2),
					cgroup_fd, atype, &create_opts);
	if (!ASSERT_GE(preorder_link, 0, "create_preorder_link")) {
		err = preorder_link;
		goto close_links;
	}

	result = skel->bss->result;
	skel->bss->idx = 0;
	memset(result, 0, 4);

	err = getsockopt(sock_fd, SOL_IP, IP_TOS, &buf, &optlen);
	if (!ASSERT_OK(err, "getsockopt-before"))
		goto close_links;
	ASSERT_TRUE(result[0] == 2 && result[1] == 1, "order before update");

	/* Replace the normal link's program child(1) -> parent(3). */
	err = bpf_link_update(normal_link, bpf_program__fd(skel->progs.parent), NULL);
	if (!ASSERT_OK(err, "bpf_link_update"))
		goto close_links;

	skel->bss->idx = 0;
	memset(result, 0, 4);

	err = getsockopt(sock_fd, SOL_IP, IP_TOS, &buf, &optlen);
	if (!ASSERT_OK(err, "getsockopt-after"))
		goto close_links;
	ASSERT_TRUE(result[0] == 2 && result[1] == 3, "order after update");

close_links:
	if (preorder_link >= 0)
		close(preorder_link);
	close(normal_link);
close_skel:
	cgroup_preorder__destroy(skel);
	return err;
}

void test_cgroup_preorder(void)
{
	int cg_parent = -1, cg_child = -1, sock_fd = -1;

	cg_parent = test__join_cgroup("/parent");
	if (!ASSERT_GE(cg_parent, 0, "join_cgroup /parent"))
		goto out;

	cg_child = test__join_cgroup("/parent/child");
	if (!ASSERT_GE(cg_child, 0, "join_cgroup /parent/child"))
		goto out;

	sock_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (!ASSERT_GE(sock_fd, 0, "socket"))
		goto out;

	ASSERT_OK(run_getsockopt_test(cg_parent, cg_child, sock_fd, false), "getsockopt_test_1");
	ASSERT_OK(run_getsockopt_test(cg_parent, cg_child, sock_fd, true), "getsockopt_test_2");
	ASSERT_OK(run_link_replace_test(cg_child, sock_fd), "link_replace_test");

out:
	close(sock_fd);
	close(cg_child);
	close(cg_parent);
}
