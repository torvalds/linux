// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#include <uapi/linux/if_link.h>
#include <test_progs.h>
#include "test_xdp_link.skel.h"

#define IFINDEX_LO 1

void serial_test_xdp_link(void)
{
	DECLARE_LIBBPF_OPTS(bpf_xdp_set_link_opts, opts, .old_fd = -1);
	struct test_xdp_link *skel1 = NULL, *skel2 = NULL;
	__u32 id1, id2, id0 = 0, prog_fd1, prog_fd2;
	struct bpf_link_info link_info;
	struct bpf_prog_info prog_info;
	struct bpf_link *link;
	int err;
	__u32 link_info_len = sizeof(link_info);
	__u32 prog_info_len = sizeof(prog_info);

	skel1 = test_xdp_link__open_and_load();
	if (!ASSERT_OK_PTR(skel1, "skel_load"))
		goto cleanup;
	prog_fd1 = bpf_program__fd(skel1->progs.xdp_handler);

	skel2 = test_xdp_link__open_and_load();
	if (!ASSERT_OK_PTR(skel2, "skel_load"))
		goto cleanup;
	prog_fd2 = bpf_program__fd(skel2->progs.xdp_handler);

	memset(&prog_info, 0, sizeof(prog_info));
	err = bpf_obj_get_info_by_fd(prog_fd1, &prog_info, &prog_info_len);
	if (!ASSERT_OK(err, "fd_info1"))
		goto cleanup;
	id1 = prog_info.id;

	memset(&prog_info, 0, sizeof(prog_info));
	err = bpf_obj_get_info_by_fd(prog_fd2, &prog_info, &prog_info_len);
	if (!ASSERT_OK(err, "fd_info2"))
		goto cleanup;
	id2 = prog_info.id;

	/* set initial prog attachment */
	err = bpf_set_link_xdp_fd_opts(IFINDEX_LO, prog_fd1, XDP_FLAGS_REPLACE, &opts);
	if (!ASSERT_OK(err, "fd_attach"))
		goto cleanup;

	/* validate prog ID */
	err = bpf_get_link_xdp_id(IFINDEX_LO, &id0, 0);
	if (!ASSERT_OK(err, "id1_check_err") || !ASSERT_EQ(id0, id1, "id1_check_val"))
		goto cleanup;

	/* BPF link is not allowed to replace prog attachment */
	link = bpf_program__attach_xdp(skel1->progs.xdp_handler, IFINDEX_LO);
	if (!ASSERT_ERR_PTR(link, "link_attach_should_fail")) {
		bpf_link__destroy(link);
		/* best-effort detach prog */
		opts.old_fd = prog_fd1;
		bpf_set_link_xdp_fd_opts(IFINDEX_LO, -1, XDP_FLAGS_REPLACE, &opts);
		goto cleanup;
	}

	/* detach BPF program */
	opts.old_fd = prog_fd1;
	err = bpf_set_link_xdp_fd_opts(IFINDEX_LO, -1, XDP_FLAGS_REPLACE, &opts);
	if (!ASSERT_OK(err, "prog_detach"))
		goto cleanup;

	/* now BPF link should attach successfully */
	link = bpf_program__attach_xdp(skel1->progs.xdp_handler, IFINDEX_LO);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;
	skel1->links.xdp_handler = link;

	/* validate prog ID */
	err = bpf_get_link_xdp_id(IFINDEX_LO, &id0, 0);
	if (!ASSERT_OK(err, "id1_check_err") || !ASSERT_EQ(id0, id1, "id1_check_val"))
		goto cleanup;

	/* BPF prog attach is not allowed to replace BPF link */
	opts.old_fd = prog_fd1;
	err = bpf_set_link_xdp_fd_opts(IFINDEX_LO, prog_fd2, XDP_FLAGS_REPLACE, &opts);
	if (!ASSERT_ERR(err, "prog_attach_fail"))
		goto cleanup;

	/* Can't force-update when BPF link is active */
	err = bpf_set_link_xdp_fd(IFINDEX_LO, prog_fd2, 0);
	if (!ASSERT_ERR(err, "prog_update_fail"))
		goto cleanup;

	/* Can't force-detach when BPF link is active */
	err = bpf_set_link_xdp_fd(IFINDEX_LO, -1, 0);
	if (!ASSERT_ERR(err, "prog_detach_fail"))
		goto cleanup;

	/* BPF link is not allowed to replace another BPF link */
	link = bpf_program__attach_xdp(skel2->progs.xdp_handler, IFINDEX_LO);
	if (!ASSERT_ERR_PTR(link, "link_attach_should_fail")) {
		bpf_link__destroy(link);
		goto cleanup;
	}

	bpf_link__destroy(skel1->links.xdp_handler);
	skel1->links.xdp_handler = NULL;

	/* new link attach should succeed */
	link = bpf_program__attach_xdp(skel2->progs.xdp_handler, IFINDEX_LO);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;
	skel2->links.xdp_handler = link;

	err = bpf_get_link_xdp_id(IFINDEX_LO, &id0, 0);
	if (!ASSERT_OK(err, "id2_check_err") || !ASSERT_EQ(id0, id2, "id2_check_val"))
		goto cleanup;

	/* updating program under active BPF link works as expected */
	err = bpf_link__update_program(link, skel1->progs.xdp_handler);
	if (!ASSERT_OK(err, "link_upd"))
		goto cleanup;

	memset(&link_info, 0, sizeof(link_info));
	err = bpf_obj_get_info_by_fd(bpf_link__fd(link), &link_info, &link_info_len);
	if (!ASSERT_OK(err, "link_info"))
		goto cleanup;

	ASSERT_EQ(link_info.type, BPF_LINK_TYPE_XDP, "link_type");
	ASSERT_EQ(link_info.prog_id, id1, "link_prog_id");
	ASSERT_EQ(link_info.xdp.ifindex, IFINDEX_LO, "link_ifindex");

	err = bpf_link__detach(link);
	if (!ASSERT_OK(err, "link_detach"))
		goto cleanup;

	memset(&link_info, 0, sizeof(link_info));
	err = bpf_obj_get_info_by_fd(bpf_link__fd(link), &link_info, &link_info_len);

	ASSERT_OK(err, "link_info");
	ASSERT_EQ(link_info.prog_id, id1, "link_prog_id");
	/* ifindex should be zeroed out */
	ASSERT_EQ(link_info.xdp.ifindex, 0, "link_ifindex");

cleanup:
	test_xdp_link__destroy(skel1);
	test_xdp_link__destroy(skel2);
}
