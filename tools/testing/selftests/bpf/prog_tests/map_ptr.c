// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Facebook

#include <test_progs.h>
#include <network_helpers.h>

#include "map_ptr_kern.lskel.h"

void test_map_ptr(void)
{
	struct map_ptr_kern_lskel *skel;
	char buf[128];
	int err;
	int page_size = getpagesize();
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.data_out = buf,
		.data_size_out = sizeof(buf),
		.repeat = 1,
	);

	skel = map_ptr_kern_lskel__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	skel->maps.m_ringbuf.max_entries = page_size;

	err = map_ptr_kern_lskel__load(skel);
	if (!ASSERT_OK(err, "skel_load"))
		goto cleanup;

	skel->bss->page_size = page_size;

	err = bpf_prog_test_run_opts(skel->progs.cg_skb.prog_fd, &topts);

	if (!ASSERT_OK(err, "test_run"))
		goto cleanup;

	if (!ASSERT_NEQ(topts.retval, 0, "test_run retval"))
		goto cleanup;

cleanup:
	map_ptr_kern_lskel__destroy(skel);
}
