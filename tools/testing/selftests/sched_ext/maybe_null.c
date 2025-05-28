/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 Meta Platforms, Inc. and affiliates.
 */
#include <bpf/bpf.h>
#include <scx/common.h>
#include <sys/wait.h>
#include <unistd.h>
#include "maybe_null.bpf.skel.h"
#include "maybe_null_fail_dsp.bpf.skel.h"
#include "maybe_null_fail_yld.bpf.skel.h"
#include "scx_test.h"

static enum scx_test_status run(void *ctx)
{
	struct maybe_null *skel;
	struct maybe_null_fail_dsp *fail_dsp;
	struct maybe_null_fail_yld *fail_yld;

	skel = maybe_null__open_and_load();
	if (!skel) {
		SCX_ERR("Failed to open and load maybe_null skel");
		return SCX_TEST_FAIL;
	}
	maybe_null__destroy(skel);

	fail_dsp = maybe_null_fail_dsp__open_and_load();
	if (fail_dsp) {
		maybe_null_fail_dsp__destroy(fail_dsp);
		SCX_ERR("Should failed to open and load maybe_null_fail_dsp skel");
		return SCX_TEST_FAIL;
	}

	fail_yld = maybe_null_fail_yld__open_and_load();
	if (fail_yld) {
		maybe_null_fail_yld__destroy(fail_yld);
		SCX_ERR("Should failed to open and load maybe_null_fail_yld skel");
		return SCX_TEST_FAIL;
	}

	return SCX_TEST_PASS;
}

struct scx_test maybe_null = {
	.name = "maybe_null",
	.description = "Verify if PTR_MAYBE_NULL works for .dispatch",
	.run = run,
};
REGISTER_SCX_TEST(&maybe_null)
