// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <test_progs.h>
#include <bpf/btf.h>

#include "test_log_buf.skel.h"


static bool check_prog_load(int prog_fd, bool expect_err, const char *tag)
{
	if (expect_err) {
		if (!ASSERT_LT(prog_fd, 0, tag)) {
			close(prog_fd);
			return false;
		}
	} else /* !expect_err */ {
		if (!ASSERT_GT(prog_fd, 0, tag))
			return false;
	}
	return true;
}

static void verif_log_subtest(const char *name, bool expect_load_error, int log_level)
{
	LIBBPF_OPTS(bpf_prog_load_opts, opts);
	struct {
		/* strategically placed before others to avoid accidental modification by kernel */
		char filler[1024];
		char buf[1024];
		/* strategically placed after buf[] to catch more accidental corruptions */
		char reference[1024];
	} logs;
	char *exp_log, prog_name[16], op_name[32];
	struct test_log_buf *skel;
	struct bpf_program *prog;
	const struct bpf_insn *insns;
	size_t insn_cnt, fixed_log_sz;
	int i, mode, err, prog_fd;

	skel = test_log_buf__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	bpf_object__for_each_program(prog, skel->obj) {
		if (strcmp(bpf_program__name(prog), name) == 0)
			bpf_program__set_autoload(prog, true);
		else
			bpf_program__set_autoload(prog, false);
	}

	err = test_log_buf__load(skel);
	if (!expect_load_error && !ASSERT_OK(err, "unexpected_load_failure"))
		goto cleanup;
	if (expect_load_error && !ASSERT_ERR(err, "unexpected_load_success"))
		goto cleanup;

	insns = bpf_program__insns(skel->progs.good_prog);
	insn_cnt = bpf_program__insn_cnt(skel->progs.good_prog);

	opts.log_buf = logs.reference;
	opts.log_size = sizeof(logs.reference);
	opts.log_level = log_level | 8 /* BPF_LOG_FIXED */;
	prog_fd = bpf_prog_load(BPF_PROG_TYPE_RAW_TRACEPOINT, "log_fixed",
				"GPL", insns, insn_cnt, &opts);
	if (!check_prog_load(prog_fd, expect_load_error, "fixed_buf_prog_load"))
		goto cleanup;
	close(prog_fd);

	fixed_log_sz = strlen(logs.reference) + 1;
	if (!ASSERT_GT(fixed_log_sz, 50, "fixed_log_sz"))
		goto cleanup;
	memset(logs.reference + fixed_log_sz, 0, sizeof(logs.reference) - fixed_log_sz);

	/* validate BPF_LOG_FIXED works as verifier log used to work, that is:
	 * we get -ENOSPC and beginning of the full verifier log. This only
	 * works for log_level 2 and log_level 1 + failed program. For log
	 * level 2 we don't reset log at all. For log_level 1 + failed program
	 * we don't get to verification stats output. With log level 1
	 * for successful program  final result will be just verifier stats.
	 * But if provided too short log buf, kernel will NULL-out log->ubuf
	 * and will stop emitting further log. This means we'll never see
	 * predictable verifier stats.
	 * Long story short, we do the following -ENOSPC test only for
	 * predictable combinations.
	 */
	if (log_level >= 2 || expect_load_error) {
		opts.log_buf = logs.buf;
		opts.log_level = log_level | 8; /* fixed-length log */
		opts.log_size = 25;

		prog_fd = bpf_prog_load(BPF_PROG_TYPE_RAW_TRACEPOINT, "log_fixed50",
					"GPL", insns, insn_cnt, &opts);
		if (!ASSERT_EQ(prog_fd, -ENOSPC, "unexpected_log_fixed_prog_load_result")) {
			if (prog_fd >= 0)
				close(prog_fd);
			goto cleanup;
		}
		if (!ASSERT_EQ(strlen(logs.buf), 24, "log_fixed_25"))
			goto cleanup;
		if (!ASSERT_STRNEQ(logs.buf, logs.reference, 24, op_name))
			goto cleanup;
	}

	/* validate rolling verifier log logic: try all variations of log buf
	 * length to force various truncation scenarios
	 */
	opts.log_buf = logs.buf;

	/* rotating mode, then fixed mode */
	for (mode = 1; mode >= 0; mode--) {
		/* prefill logs.buf with 'A's to detect any write beyond allowed length */
		memset(logs.filler, 'A', sizeof(logs.filler));
		logs.filler[sizeof(logs.filler) - 1] = '\0';
		memset(logs.buf, 'A', sizeof(logs.buf));
		logs.buf[sizeof(logs.buf) - 1] = '\0';

		for (i = 1; i < fixed_log_sz; i++) {
			opts.log_size = i;
			opts.log_level = log_level | (mode ? 0 : 8 /* BPF_LOG_FIXED */);

			snprintf(prog_name, sizeof(prog_name),
				 "log_%s_%d", mode ? "roll" : "fixed", i);
			prog_fd = bpf_prog_load(BPF_PROG_TYPE_RAW_TRACEPOINT, prog_name,
						"GPL", insns, insn_cnt, &opts);

			snprintf(op_name, sizeof(op_name),
				 "log_%s_prog_load_%d", mode ? "roll" : "fixed", i);
			if (!ASSERT_EQ(prog_fd, -ENOSPC, op_name)) {
				if (prog_fd >= 0)
					close(prog_fd);
				goto cleanup;
			}

			snprintf(op_name, sizeof(op_name),
				 "log_%s_strlen_%d", mode ? "roll" : "fixed", i);
			ASSERT_EQ(strlen(logs.buf), i - 1, op_name);

			if (mode)
				exp_log = logs.reference + fixed_log_sz - i;
			else
				exp_log = logs.reference;

			snprintf(op_name, sizeof(op_name),
				 "log_%s_contents_%d", mode ? "roll" : "fixed", i);
			if (!ASSERT_STRNEQ(logs.buf, exp_log, i - 1, op_name)) {
				printf("CMP:%d\nS1:'%s'\nS2:'%s'\n",
					strncmp(logs.buf, exp_log, i - 1),
					logs.buf, exp_log);
				goto cleanup;
			}

			/* check that unused portions of logs.buf is not overwritten */
			snprintf(op_name, sizeof(op_name),
				 "log_%s_unused_%d", mode ? "roll" : "fixed", i);
			if (!ASSERT_STREQ(logs.buf + i, logs.filler + i, op_name)) {
				printf("CMP:%d\nS1:'%s'\nS2:'%s'\n",
					strcmp(logs.buf + i, logs.filler + i),
					logs.buf + i, logs.filler + i);
				goto cleanup;
			}
		}
	}

cleanup:
	test_log_buf__destroy(skel);
}

void test_verifier_log(void)
{
	if (test__start_subtest("good_prog-level1"))
		verif_log_subtest("good_prog", false, 1);
	if (test__start_subtest("good_prog-level2"))
		verif_log_subtest("good_prog", false, 2);
	if (test__start_subtest("bad_prog-level1"))
		verif_log_subtest("bad_prog", true, 1);
	if (test__start_subtest("bad_prog-level2"))
		verif_log_subtest("bad_prog", true, 2);
}
