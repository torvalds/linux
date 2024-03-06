// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include "bad_struct_ops.skel.h"

static void invalid_prog_reuse(void)
{
	struct bad_struct_ops *skel;
	char *log = NULL;
	int err;

	skel = bad_struct_ops__open();
	if (!ASSERT_OK_PTR(skel, "bad_struct_ops__open"))
		return;

	if (start_libbpf_log_capture())
		goto cleanup;

	err = bad_struct_ops__load(skel);
	log = stop_libbpf_log_capture();
	ASSERT_ERR(err, "bad_struct_ops__load should fail");
	ASSERT_HAS_SUBSTR(log,
		"struct_ops init_kern testmod_2 func ptr test_1: invalid reuse of prog test_1",
		"expected init_kern message");

cleanup:
	free(log);
	bad_struct_ops__destroy(skel);
}

void test_bad_struct_ops(void)
{
	if (test__start_subtest("invalid_prog_reuse"))
		invalid_prog_reuse();
}
