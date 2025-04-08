// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */

#include <test_progs.h>
#include "pro_epilogue.skel.h"
#include "epilogue_tailcall.skel.h"
#include "pro_epilogue_goto_start.skel.h"
#include "epilogue_exit.skel.h"
#include "pro_epilogue_with_kfunc.skel.h"

struct st_ops_args {
	__u64 a;
};

static void test_tailcall(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	struct epilogue_tailcall *skel;
	struct st_ops_args args;
	int err, prog_fd;

	skel = epilogue_tailcall__open_and_load();
	if (!ASSERT_OK_PTR(skel, "epilogue_tailcall__open_and_load"))
		return;

	topts.ctx_in = &args;
	topts.ctx_size_in = sizeof(args);

	skel->links.epilogue_tailcall =
		bpf_map__attach_struct_ops(skel->maps.epilogue_tailcall);
	if (!ASSERT_OK_PTR(skel->links.epilogue_tailcall, "attach_struct_ops"))
		goto done;

	/* Both test_epilogue_tailcall and test_epilogue_subprog are
	 * patched with epilogue. When syscall_epilogue_tailcall()
	 * is run, test_epilogue_tailcall() is triggered.
	 * It executes a tail call and control is transferred to
	 * test_epilogue_subprog(). Only test_epilogue_subprog()
	 * does args->a += 1, thus final args.a value of 10001
	 * guarantees that only the epilogue of the
	 * test_epilogue_subprog is executed.
	 */
	memset(&args, 0, sizeof(args));
	prog_fd = bpf_program__fd(skel->progs.syscall_epilogue_tailcall);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "bpf_prog_test_run_opts");
	ASSERT_EQ(args.a, 10001, "args.a");
	ASSERT_EQ(topts.retval, 10001 * 2, "topts.retval");

done:
	epilogue_tailcall__destroy(skel);
}

void test_pro_epilogue(void)
{
	RUN_TESTS(pro_epilogue);
	RUN_TESTS(pro_epilogue_goto_start);
	RUN_TESTS(epilogue_exit);
	RUN_TESTS(pro_epilogue_with_kfunc);
	if (test__start_subtest("tailcall"))
		test_tailcall();
}
