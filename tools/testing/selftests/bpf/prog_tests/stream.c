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

	ASSERT_LT(bpf_prog_stream_read(prog_fd, BPF_STREAM_STDOUT, buf, UINT_MAX, NULL), 0, "error");
	ret = -errno;
	ASSERT_EQ(ret, -EINVAL, "large stream buf");

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

void test_stream_oversize(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	struct stream *skel;
	int ret, prog_fd;

	skel = stream__open_and_load();
	if (!ASSERT_OK_PTR(skel, "stream__open_and_load"))
		return;

	prog_fd = bpf_program__fd(skel->progs.stream_oversize);
	ret = bpf_prog_test_run_opts(prog_fd, &opts);
	ASSERT_OK(ret, "oversize run");
	ASSERT_OK(opts.retval, "oversize retval");

	stream__destroy(skel);
}

void test_stream_partial_read(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	struct stream *skel;
	int ret, prog_fd;
	long page_size;
	char *page, *buf;
	char rest[8] = {};

	skel = stream__open_and_load();
	if (!ASSERT_OK_PTR(skel, "stream__open_and_load"))
		return;

	prog_fd = bpf_program__fd(skel->progs.stream_syscall);
	ret = bpf_prog_test_run_opts(prog_fd, &opts);
	ASSERT_OK(ret, "ret");
	ASSERT_OK(opts.retval, "retval");

	page_size = sysconf(_SC_PAGESIZE);
	page = mmap(NULL, page_size * 2, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (!ASSERT_NEQ(page, MAP_FAILED, "mmap")) {
		stream__destroy(skel);
		return;
	}
	/* Leave only the first page mapped so a straddling copy faults. */
	if (!ASSERT_OK(munmap(page + page_size, page_size), "munmap second page")) {
		munmap(page, page_size * 2);
		stream__destroy(skel);
		return;
	}

	buf = page + page_size - 1;
	ret = bpf_prog_stream_read(prog_fd, BPF_STREAM_STDOUT, buf, 3, NULL);
	ASSERT_EQ(ret, 1, "partial bytes");
	ASSERT_EQ(buf[0], 'f', "first byte");

	ret = bpf_prog_stream_read(prog_fd, BPF_STREAM_STDOUT, rest, sizeof(rest), NULL);
	ASSERT_EQ(ret, 2, "remaining bytes");
	ASSERT_OK(memcmp(rest, "oo", 2), "remaining data");

	munmap(page, page_size);
	stream__destroy(skel);
}

static void test_address(struct bpf_program *prog, unsigned long *fault_addr_p)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	LIBBPF_OPTS(bpf_prog_stream_read_opts, ropts);
	int ret, prog_fd;
	char fault_addr[64];
	char buf[1024];

	prog_fd = bpf_program__fd(prog);

	ret = bpf_prog_test_run_opts(prog_fd, &opts);
	ASSERT_OK(ret, "ret");
	ASSERT_OK(opts.retval, "retval");

	sprintf(fault_addr, "0x%lx", *fault_addr_p);

	ret = bpf_prog_stream_read(prog_fd, BPF_STREAM_STDERR, buf, sizeof(buf), &ropts);
	ASSERT_GT(ret, 0, "stream read");
	ASSERT_LE(ret, 1023, "len for buf");
	buf[ret] = '\0';

	if (!ASSERT_HAS_SUBSTR(buf, fault_addr, "fault_addr")) {
		fprintf(stderr, "Output from stream:\n%s\n", buf);
		fprintf(stderr, "Fault Addr: %s\n", fault_addr);
	}
}

void test_stream_arena_fault_address(void)
{
	struct stream *skel;

#if !defined(__x86_64__) && !defined(__aarch64__)
	printf("%s:SKIP: arena fault reporting not supported\n", __func__);
	test__skip();
	return;
#endif

	skel = stream__open_and_load();
	if (!ASSERT_OK_PTR(skel, "stream__open_and_load"))
		return;

	if (test__start_subtest("read_fault"))
		test_address(skel->progs.stream_arena_read_fault, &skel->bss->fault_addr);
	if (test__start_subtest("write_fault"))
		test_address(skel->progs.stream_arena_write_fault, &skel->bss->fault_addr);
	if (test__start_subtest("load_acquire_fault"))
		test_address(skel->progs.stream_arena_load_acquire_fault, &skel->bss->fault_addr);
	if (test__start_subtest("xchg_fault"))
		test_address(skel->progs.stream_arena_xchg_fault, &skel->bss->fault_addr);
	if (test__start_subtest("cmpxchg_fault"))
		test_address(skel->progs.stream_arena_cmpxchg_fault, &skel->bss->fault_addr);

	stream__destroy(skel);
}
