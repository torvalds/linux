// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include <test_progs.h>
#include "syscall.skel.h"

struct args {
	__u64 log_buf;
	__u32 log_size;
	int max_entries;
	int map_fd;
	int prog_fd;
	int btf_fd;
};

void test_syscall(void)
{
	static char verifier_log[8192];
	struct args ctx = {
		.max_entries = 1024,
		.log_buf = (uintptr_t) verifier_log,
		.log_size = sizeof(verifier_log),
	};
	LIBBPF_OPTS(bpf_test_run_opts, tattr,
		.ctx_in = &ctx,
		.ctx_size_in = sizeof(ctx),
	);
	struct syscall *skel = NULL;
	__u64 key = 12, value = 0;
	int err, prog_fd;

	skel = syscall__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_load"))
		goto cleanup;

	prog_fd = bpf_program__fd(skel->progs.bpf_prog);
	err = bpf_prog_test_run_opts(prog_fd, &tattr);
	ASSERT_EQ(err, 0, "err");
	ASSERT_EQ(tattr.retval, 1, "retval");
	ASSERT_GT(ctx.map_fd, 0, "ctx.map_fd");
	ASSERT_GT(ctx.prog_fd, 0, "ctx.prog_fd");
	ASSERT_OK(memcmp(verifier_log, "processed", sizeof("processed") - 1),
		  "verifier_log");

	err = bpf_map_lookup_elem(ctx.map_fd, &key, &value);
	ASSERT_EQ(err, 0, "map_lookup");
	ASSERT_EQ(value, 34, "map lookup value");
cleanup:
	syscall__destroy(skel);
	if (ctx.prog_fd > 0)
		close(ctx.prog_fd);
	if (ctx.map_fd > 0)
		close(ctx.map_fd);
	if (ctx.btf_fd > 0)
		close(ctx.btf_fd);
}
