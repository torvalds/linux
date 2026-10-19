// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>
#include "kfunc_call_test.skel.h"

/*
 * bpf_program__clone() loads a program from a prepared object with the
 * fd_array of the object, where module BTF fds start at slot 1 and slot 0 is
 * empty. It used to pass the fill index of that array as fd_array_cnt, which
 * made the kernel resolve slot 0 and fail the load.
 */
void test_clone_module_kfunc(void)
{
	struct kfunc_call_test *skel;
	int err, fd;

	skel = kfunc_call_test__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	err = bpf_object__prepare(skel->obj);
	if (!ASSERT_OK(err, "prepare"))
		goto out;

	fd = bpf_program__clone(skel->progs.kfunc_call_test1, NULL);
	if (ASSERT_GE(fd, 0, "clone"))
		close(fd);
out:
	kfunc_call_test__destroy(skel);
}
