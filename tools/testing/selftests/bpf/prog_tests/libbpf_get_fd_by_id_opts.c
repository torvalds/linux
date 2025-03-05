// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (C) 2022 Huawei Technologies Duesseldorf GmbH
 *
 * Author: Roberto Sassu <roberto.sassu@huawei.com>
 */

#include <test_progs.h>

#include "test_libbpf_get_fd_by_id_opts.skel.h"

void test_libbpf_get_fd_by_id_opts(void)
{
	struct test_libbpf_get_fd_by_id_opts *skel;
	struct bpf_map_info info_m = {};
	__u32 len = sizeof(info_m), value;
	int ret, zero = 0, fd = -1;
	LIBBPF_OPTS(bpf_get_fd_by_id_opts, fd_opts_rdonly,
		.open_flags = BPF_F_RDONLY,
	);

	skel = test_libbpf_get_fd_by_id_opts__open_and_load();
	if (!ASSERT_OK_PTR(skel,
			   "test_libbpf_get_fd_by_id_opts__open_and_load"))
		return;

	ret = test_libbpf_get_fd_by_id_opts__attach(skel);
	if (!ASSERT_OK(ret, "test_libbpf_get_fd_by_id_opts__attach"))
		goto close_prog;

	ret = bpf_obj_get_info_by_fd(bpf_map__fd(skel->maps.data_input),
				     &info_m, &len);
	if (!ASSERT_OK(ret, "bpf_obj_get_info_by_fd"))
		goto close_prog;

	fd = bpf_map_get_fd_by_id(info_m.id);
	if (!ASSERT_LT(fd, 0, "bpf_map_get_fd_by_id"))
		goto close_prog;

	fd = bpf_map_get_fd_by_id_opts(info_m.id, NULL);
	if (!ASSERT_LT(fd, 0, "bpf_map_get_fd_by_id_opts"))
		goto close_prog;

	fd = bpf_map_get_fd_by_id_opts(info_m.id, &fd_opts_rdonly);
	if (!ASSERT_GE(fd, 0, "bpf_map_get_fd_by_id_opts"))
		goto close_prog;

	/* Map lookup should work with read-only fd. */
	ret = bpf_map_lookup_elem(fd, &zero, &value);
	if (!ASSERT_OK(ret, "bpf_map_lookup_elem"))
		goto close_prog;

	if (!ASSERT_EQ(value, 0, "map value mismatch"))
		goto close_prog;

	/* Map update should not work with read-only fd. */
	ret = bpf_map_update_elem(fd, &zero, &len, BPF_ANY);
	if (!ASSERT_LT(ret, 0, "bpf_map_update_elem"))
		goto close_prog;

	/* Map update should work with read-write fd. */
	ret = bpf_map_update_elem(bpf_map__fd(skel->maps.data_input), &zero,
				  &len, BPF_ANY);
	if (!ASSERT_OK(ret, "bpf_map_update_elem"))
		goto close_prog;

	/* Prog get fd with opts set should not work (no kernel support). */
	ret = bpf_prog_get_fd_by_id_opts(0, &fd_opts_rdonly);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_prog_get_fd_by_id_opts"))
		goto close_prog;

	/* Link get fd with opts set should not work (no kernel support). */
	ret = bpf_link_get_fd_by_id_opts(0, &fd_opts_rdonly);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_link_get_fd_by_id_opts"))
		goto close_prog;

	/* BTF get fd with opts set should not work (no kernel support). */
	ret = bpf_btf_get_fd_by_id_opts(0, &fd_opts_rdonly);
	ASSERT_EQ(ret, -EINVAL, "bpf_btf_get_fd_by_id_opts");

close_prog:
	if (fd >= 0)
		close(fd);

	test_libbpf_get_fd_by_id_opts__destroy(skel);
}
