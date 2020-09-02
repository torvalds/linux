// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Facebook

#include <test_progs.h>
#include <network_helpers.h>

#include "map_ptr_kern.skel.h"

void test_map_ptr(void)
{
	struct map_ptr_kern *skel;
	__u32 duration = 0, retval;
	char buf[128];
	int err;

	skel = map_ptr_kern__open_and_load();
	if (CHECK(!skel, "skel_open_load", "open_load failed\n"))
		return;

	err = bpf_prog_test_run(bpf_program__fd(skel->progs.cg_skb), 1, &pkt_v4,
				sizeof(pkt_v4), buf, NULL, &retval, NULL);

	if (CHECK(err, "test_run", "err=%d errno=%d\n", err, errno))
		goto cleanup;

	if (CHECK(!retval, "retval", "retval=%d map_type=%u line=%u\n", retval,
		  skel->bss->g_map_type, skel->bss->g_line))
		goto cleanup;

cleanup:
	map_ptr_kern__destroy(skel);
}
