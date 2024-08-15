// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include "test_progs.h"
#include "core_kern.lskel.h"

void test_core_kern_lskel(void)
{
	struct core_kern_lskel *skel;
	int link_fd;

	skel = core_kern_lskel__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		return;

	link_fd = core_kern_lskel__core_relo_proto__attach(skel);
	if (!ASSERT_GT(link_fd, 0, "attach(core_relo_proto)"))
		goto cleanup;

	/* trigger tracepoints */
	usleep(1);
	ASSERT_TRUE(skel->bss->proto_out[0], "bpf_core_type_exists");
	ASSERT_FALSE(skel->bss->proto_out[1], "!bpf_core_type_exists");
	ASSERT_TRUE(skel->bss->proto_out[2], "bpf_core_type_exists. nested");

cleanup:
	core_kern_lskel__destroy(skel);
}
