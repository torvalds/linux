// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include <test_progs.h>
#include <network_helpers.h>
#include "test_ksyms_module.lskel.h"
#include "test_ksyms_module.skel.h"

static void test_ksyms_module_lskel(void)
{
	struct test_ksyms_module_lskel *skel;
	int err;
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.repeat = 1,
	);

	if (!env.has_testmod) {
		test__skip();
		return;
	}

	skel = test_ksyms_module_lskel__open_and_load();
	if (!ASSERT_OK_PTR(skel, "test_ksyms_module_lskel__open_and_load"))
		return;
	err = bpf_prog_test_run_opts(skel->progs.load.prog_fd, &topts);
	if (!ASSERT_OK(err, "bpf_prog_test_run"))
		goto cleanup;
	ASSERT_EQ(topts.retval, 0, "retval");
	ASSERT_EQ(skel->bss->out_bpf_testmod_ksym, 42, "bpf_testmod_ksym");
cleanup:
	test_ksyms_module_lskel__destroy(skel);
}

static void test_ksyms_module_libbpf(void)
{
	struct test_ksyms_module *skel;
	int err;
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.repeat = 1,
	);

	if (!env.has_testmod) {
		test__skip();
		return;
	}

	skel = test_ksyms_module__open_and_load();
	if (!ASSERT_OK_PTR(skel, "test_ksyms_module__open"))
		return;
	err = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.load), &topts);
	if (!ASSERT_OK(err, "bpf_prog_test_run"))
		goto cleanup;
	ASSERT_EQ(topts.retval, 0, "retval");
	ASSERT_EQ(skel->bss->out_bpf_testmod_ksym, 42, "bpf_testmod_ksym");
cleanup:
	test_ksyms_module__destroy(skel);
}

void test_ksyms_module(void)
{
	if (test__start_subtest("lskel"))
		test_ksyms_module_lskel();
	if (test__start_subtest("libbpf"))
		test_ksyms_module_libbpf();
}
