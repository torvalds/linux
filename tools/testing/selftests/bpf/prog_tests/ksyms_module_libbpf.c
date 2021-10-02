// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include <network_helpers.h>
#include "test_ksyms_module.skel.h"

void test_ksyms_module_libbpf(void)
{
	struct test_ksyms_module *skel;
	int retval, err;

	if (!env.has_testmod) {
		test__skip();
		return;
	}

	skel = test_ksyms_module__open_and_load();
	if (!ASSERT_OK_PTR(skel, "test_ksyms_module__open"))
		return;
	err = bpf_prog_test_run(bpf_program__fd(skel->progs.load), 1, &pkt_v4,
				sizeof(pkt_v4), NULL, NULL, (__u32 *)&retval, NULL);
	if (!ASSERT_OK(err, "bpf_prog_test_run"))
		goto cleanup;
	ASSERT_EQ(retval, 0, "retval");
	ASSERT_EQ(skel->bss->out_bpf_testmod_ksym, 42, "bpf_testmod_ksym");
cleanup:
	test_ksyms_module__destroy(skel);
}
