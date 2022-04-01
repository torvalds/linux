/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2021 Hengqi Chen */

#include <test_progs.h>
#include "test_prog_array_init.skel.h"

void test_prog_array_init(void)
{
	struct test_prog_array_init *skel;
	int err;

	skel = test_prog_array_init__open();
	if (!ASSERT_OK_PTR(skel, "could not open BPF object"))
		return;

	skel->rodata->my_pid = getpid();

	err = test_prog_array_init__load(skel);
	if (!ASSERT_OK(err, "could not load BPF object"))
		goto cleanup;

	skel->links.entry = bpf_program__attach_raw_tracepoint(skel->progs.entry, "sys_enter");
	if (!ASSERT_OK_PTR(skel->links.entry, "could not attach BPF program"))
		goto cleanup;

	usleep(1);

	ASSERT_EQ(skel->bss->value, 42, "unexpected value");

cleanup:
	test_prog_array_init__destroy(skel);
}
