// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>
#include <sys/mman.h>

#include "stream.skel.h"
#include "stream_fail.skel.h"

void test_stream_failure(void)
{
	RUN_TESTS(stream_fail);
}

void test_stream_success(void)
{
	RUN_TESTS(stream);
	return;
}

void test_stream_syscall(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	LIBBPF_OPTS(bpf_prog_stream_read_opts, ropts);
	struct stream *skel;
	int ret, prog_fd;
	char buf[64];

	skel = stream__open_and_load();
	if (!ASSERT_OK_PTR(skel, "stream__open_and_load"))
		return;

	prog_fd = bpf_program__fd(skel->progs.stream_syscall);
	ret = bpf_prog_test_run_opts(prog_fd, &opts);
	ASSERT_OK(ret, "ret");
	ASSERT_OK(opts.retval, "retval");

	ASSERT_LT(bpf_prog_stream_read(0, BPF_STREAM_STDOUT, buf, sizeof(buf), &ropts), 0, "error");
	ret = -errno;
	ASSERT_EQ(ret, -EINVAL, "bad prog_fd");

	ASSERT_LT(bpf_prog_stream_read(prog_fd, 0, buf, sizeof(buf), &ropts), 0, "error");
	ret = -errno;
	ASSERT_EQ(ret, -ENOENT, "bad stream id");

	ASSERT_LT(bpf_prog_stream_read(prog_fd, BPF_STREAM_STDOUT, NULL, sizeof(buf), NULL), 0, "error");
	ret = -errno;
	ASSERT_EQ(ret, -EFAULT, "bad stream buf");

	ret = bpf_prog_stream_read(prog_fd, BPF_STREAM_STDOUT, buf, 2, NULL);
	ASSERT_EQ(ret, 2, "bytes");
	ret = bpf_prog_stream_read(prog_fd, BPF_STREAM_STDOUT, buf, 2, NULL);
	ASSERT_EQ(ret, 1, "bytes");
	ret = bpf_prog_stream_read(prog_fd, BPF_STREAM_STDOUT, buf, 1, &ropts);
	ASSERT_EQ(ret, 0, "no bytes stdout");
	ret = bpf_prog_stream_read(prog_fd, BPF_STREAM_STDERR, buf, 1, &ropts);
	ASSERT_EQ(ret, 0, "no bytes stderr");

	stream__destroy(skel);
}
