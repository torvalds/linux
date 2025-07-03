// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>
#include <sys/mman.h>
#include <regex.h>

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

struct {
	int prog_off;
	const char *errstr;
} stream_error_arr[] = {
	{
		offsetof(struct stream, progs.stream_cond_break),
		"ERROR: Timeout detected for may_goto instruction\n"
		"CPU: [0-9]+ UID: 0 PID: [0-9]+ Comm: .*\n"
		"Call trace:\n"
		"([a-zA-Z_][a-zA-Z0-9_]*\\+0x[0-9a-fA-F]+/0x[0-9a-fA-F]+\n"
		"|[ \t]+[^\n]+\n)*",
	},
	{
		offsetof(struct stream, progs.stream_deadlock),
		"ERROR: AA or ABBA deadlock detected for bpf_res_spin_lock\n"
		"Attempted lock   = (0x[0-9a-fA-F]+)\n"
		"Total held locks = 1\n"
		"Held lock\\[ 0\\] = \\1\n"  // Lock address must match
		"CPU: [0-9]+ UID: 0 PID: [0-9]+ Comm: .*\n"
		"Call trace:\n"
		"([a-zA-Z_][a-zA-Z0-9_]*\\+0x[0-9a-fA-F]+/0x[0-9a-fA-F]+\n"
		"|[ \t]+[^\n]+\n)*",
	},
};

static int match_regex(const char *pattern, const char *string)
{
	int err, rc;
	regex_t re;

	err = regcomp(&re, pattern, REG_EXTENDED | REG_NEWLINE);
	if (err)
		return -1;
	rc = regexec(&re, string, 0, NULL, 0);
	regfree(&re);
	return rc == 0 ? 1 : 0;
}

void test_stream_errors(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	LIBBPF_OPTS(bpf_prog_stream_read_opts, ropts);
	struct stream *skel;
	int ret, prog_fd;
	char buf[1024];

	skel = stream__open_and_load();
	if (!ASSERT_OK_PTR(skel, "stream__open_and_load"))
		return;

	for (int i = 0; i < ARRAY_SIZE(stream_error_arr); i++) {
		struct bpf_program **prog;

		prog = (struct bpf_program **)(((char *)skel) + stream_error_arr[i].prog_off);
		prog_fd = bpf_program__fd(*prog);
		ret = bpf_prog_test_run_opts(prog_fd, &opts);
		ASSERT_OK(ret, "ret");
		ASSERT_OK(opts.retval, "retval");

#if !defined(__x86_64__)
		ASSERT_TRUE(1, "Timed may_goto unsupported, skip.");
		if (i == 0) {
			ret = bpf_prog_stream_read(prog_fd, 2, buf, sizeof(buf), &ropts);
			ASSERT_EQ(ret, 0, "stream read");
			continue;
		}
#endif

		ret = bpf_prog_stream_read(prog_fd, BPF_STREAM_STDERR, buf, sizeof(buf), &ropts);
		ASSERT_GT(ret, 0, "stream read");
		ASSERT_LE(ret, 1023, "len for buf");
		buf[ret] = '\0';

		ret = match_regex(stream_error_arr[i].errstr, buf);
		if (!ASSERT_TRUE(ret == 1, "regex match"))
			fprintf(stderr, "Output from stream:\n%s\n", buf);
	}

	stream__destroy(skel);
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
