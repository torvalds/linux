/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2023 Isovalent */
#ifndef TC_HELPERS
#define TC_HELPERS
#include <test_progs.h>

#ifndef loopback
# define loopback 1
#endif

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

static inline void __assert_mprog_count(int target, int expected, int ifindex)
{
	__u32 count = 0, attach_flags = 0;
	int err;

	err = bpf_prog_query(ifindex, target, 0, &attach_flags,
			     NULL, &count);
	ASSERT_EQ(count, expected, "count");
	ASSERT_EQ(err, 0, "prog_query");
}

static inline void assert_mprog_count(int target, int expected)
{
	__assert_mprog_count(target, expected, loopback);
}

static inline void assert_mprog_count_ifindex(int ifindex, int target, int expected)
{
	__assert_mprog_count(target, expected, ifindex);
}

static inline void tc_skel_reset_all_seen(struct test_tc_link *skel)
{
	memset(skel->bss, 0, sizeof(*skel->bss));
}

#endif /* TC_HELPERS */
