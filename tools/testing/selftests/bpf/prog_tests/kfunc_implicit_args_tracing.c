// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <test_progs.h>
#include "kfunc_implicit_args_tracing.skel.h"

void test_kfunc_implicit_args_tracing(void)
{
	struct kfunc_implicit_args_tracing *skel;
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	int err, fd;

	skel = kfunc_implicit_args_tracing__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		return;

	err = kfunc_implicit_args_tracing__attach(skel);
	if (!ASSERT_OK(err, "attach"))
		goto cleanup;

	fd = bpf_program__fd(skel->progs.trigger_implicit_arg);
	err = bpf_prog_test_run_opts(fd, &topts);
	if (!ASSERT_OK(err, "test_run"))
		goto cleanup;

	ASSERT_EQ(topts.retval, 5, "kfunc_retval");
	ASSERT_EQ(skel->bss->fentry_arg_cnt, 2, "fentry_arg_cnt");
	ASSERT_NEQ(skel->bss->fentry_aux_arg, 0, "fentry_aux_arg");
	ASSERT_EQ(skel->bss->fentry_result, 1, "fentry_result");
	ASSERT_EQ(skel->bss->fexit_arg_cnt, 2, "fexit_arg_cnt");
	ASSERT_NEQ(skel->bss->fexit_aux_arg, 0, "fexit_aux_arg");
	ASSERT_EQ(skel->bss->fexit_result, 1, "fexit_result");

cleanup:
	kfunc_implicit_args_tracing__destroy(skel);
}
