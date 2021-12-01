// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Facebook

#include <test_progs.h>
#include <network_helpers.h>

#include "map_ptr_kern.lskel.h"

void test_map_ptr(void)
{
	struct map_ptr_kern_lskel *skel;
	__u32 duration = 0, retval;
	char buf[128];
	int err;
	int page_size = getpagesize();

	skel = map_ptr_kern_lskel__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	skel->maps.m_ringbuf.max_entries = page_size;

	err = map_ptr_kern_lskel__load(skel);
	if (!ASSERT_OK(err, "skel_load"))
		goto cleanup;

	skel->bss->page_size = page_size;

	err = bpf_prog_test_run(skel->progs.cg_skb.prog_fd, 1, &pkt_v4,
				sizeof(pkt_v4), buf, NULL, &retval, NULL);

	if (CHECK(err, "test_run", "err=%d errno=%d\n", err, errno))
		goto cleanup;

	if (CHECK(!retval, "retval", "retval=%d map_type=%u line=%u\n", retval,
		  skel->bss->g_map_type, skel->bss->g_line))
		goto cleanup;

cleanup:
	map_ptr_kern_lskel__destroy(skel);
}
