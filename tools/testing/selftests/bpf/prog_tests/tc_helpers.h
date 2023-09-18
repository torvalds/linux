/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2023 Isovalent */
#ifndef TC_HELPERS
#define TC_HELPERS
#include <test_progs.h>

static inline __u32 id_from_prog_fd(int fd)
{
	struct bpf_prog_info prog_info = {};
	__u32 prog_info_len = sizeof(prog_info);
	int err;

	err = bpf_obj_get_info_by_fd(fd, &prog_info, &prog_info_len);
	if (!ASSERT_OK(err, "id_from_prog_fd"))
		return 0;

	ASSERT_NEQ(prog_info.id, 0, "prog_info.id");
	return prog_info.id;
}

static inline __u32 id_from_link_fd(int fd)
{
	struct bpf_link_info link_info = {};
	__u32 link_info_len = sizeof(link_info);
	int err;

	err = bpf_link_get_info_by_fd(fd, &link_info, &link_info_len);
	if (!ASSERT_OK(err, "id_from_link_fd"))
		return 0;

	ASSERT_NEQ(link_info.id, 0, "link_info.id");
	return link_info.id;
}

static inline __u32 ifindex_from_link_fd(int fd)
{
	struct bpf_link_info link_info = {};
	__u32 link_info_len = sizeof(link_info);
	int err;

	err = bpf_link_get_info_by_fd(fd, &link_info, &link_info_len);
	if (!ASSERT_OK(err, "id_from_link_fd"))
		return 0;

	return link_info.tcx.ifindex;
}

static inline void __assert_mprog_count(int target, int expected, bool miniq, int ifindex)
{
	__u32 count = 0, attach_flags = 0;
	int err;

	err = bpf_prog_query(ifindex, target, 0, &attach_flags,
			     NULL, &count);
	ASSERT_EQ(count, expected, "count");
	if (!expected && !miniq)
		ASSERT_EQ(err, -ENOENT, "prog_query");
	else
		ASSERT_EQ(err, 0, "prog_query");
}

static inline void assert_mprog_count(int target, int expected)
{
	__assert_mprog_count(target, expected, false, loopback);
}

static inline void assert_mprog_count_ifindex(int ifindex, int target, int expected)
{
	__assert_mprog_count(target, expected, false, ifindex);
}

#endif /* TC_HELPERS */
