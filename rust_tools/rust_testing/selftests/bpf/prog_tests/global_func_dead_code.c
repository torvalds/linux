// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <test_progs.h>
#include "verifier_global_subprogs.skel.h"
#include "freplace_dead_global_func.skel.h"

void test_global_func_dead_code(void)
{
	struct verifier_global_subprogs *tgt_skel = NULL;
	struct freplace_dead_global_func *skel = NULL;
	char log_buf[4096];
	int err, tgt_fd;

	/* first, try to load target with good global subprog */
	tgt_skel = verifier_global_subprogs__open();
	if (!ASSERT_OK_PTR(tgt_skel, "tgt_skel_good_open"))
		return;

	bpf_program__set_autoload(tgt_skel->progs.chained_global_func_calls_success, true);

	err = verifier_global_subprogs__load(tgt_skel);
	if (!ASSERT_OK(err, "tgt_skel_good_load"))
		goto out;

	tgt_fd = bpf_program__fd(tgt_skel->progs.chained_global_func_calls_success);

	/* Attach to good non-eliminated subprog */
	skel = freplace_dead_global_func__open();
	if (!ASSERT_OK_PTR(skel, "skel_good_open"))
		goto out;

	err = bpf_program__set_attach_target(skel->progs.freplace_prog, tgt_fd, "global_good");
	ASSERT_OK(err, "attach_target_good");

	err = freplace_dead_global_func__load(skel);
	if (!ASSERT_OK(err, "skel_good_load"))
		goto out;

	freplace_dead_global_func__destroy(skel);

	/* Try attaching to dead code-eliminated subprog */
	skel = freplace_dead_global_func__open();
	if (!ASSERT_OK_PTR(skel, "skel_dead_open"))
		goto out;

	bpf_program__set_log_buf(skel->progs.freplace_prog, log_buf, sizeof(log_buf));
	err = bpf_program__set_attach_target(skel->progs.freplace_prog, tgt_fd, "global_dead");
	ASSERT_OK(err, "attach_target_dead");

	err = freplace_dead_global_func__load(skel);
	if (!ASSERT_ERR(err, "skel_dead_load"))
		goto out;

	ASSERT_HAS_SUBSTR(log_buf, "Subprog global_dead doesn't exist", "dead_subprog_missing_msg");

out:
	verifier_global_subprogs__destroy(tgt_skel);
	freplace_dead_global_func__destroy(skel);
}
