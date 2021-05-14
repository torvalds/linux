// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include <test_progs.h>
#include <bpf/libbpf.h>
#include <bpf/btf.h>
#include "test_ksyms_module.lskel.h"

static int duration;

void test_ksyms_module(void)
{
	struct test_ksyms_module* skel;
	int err;

	skel = test_ksyms_module__open_and_load();
	if (CHECK(!skel, "skel_open", "failed to open skeleton\n"))
		return;

	err = test_ksyms_module__attach(skel);
	if (CHECK(err, "skel_attach", "skeleton attach failed: %d\n", err))
		goto cleanup;

	usleep(1);

	ASSERT_EQ(skel->bss->triggered, true, "triggered");
	ASSERT_EQ(skel->bss->out_mod_ksym_global, 123, "global_ksym_val");

cleanup:
	test_ksyms_module__destroy(skel);
}
