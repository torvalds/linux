// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include "test_kmods/bpf_testmod.h"
#include "bpf_util.h"

static void check_attach_reject(const struct bpf_insn *program, size_t prog_len)
{
	LIBBPF_OPTS(bpf_prog_load_opts, opts);
	char error[4096];
	int bpf_fd, tp_fd;

	opts.log_level = 2;
	opts.log_buf = error;
	opts.log_size = sizeof(error);

	bpf_fd = bpf_prog_load(BPF_PROG_TYPE_RAW_TRACEPOINT_WRITABLE, NULL, "GPL v2",
			       program, prog_len, &opts);
	if (!ASSERT_GE(bpf_fd, 0, "prog_load"))
		return;

	tp_fd = bpf_raw_tracepoint_open("bpf_testmod_test_writable_bare_tp", bpf_fd);
	ASSERT_EQ(tp_fd, -EINVAL, "bpf_raw_tracepoint_open");
	if (tp_fd >= 0)
		close(tp_fd);

	close(bpf_fd);
}

void test_raw_tp_writable_reject_bad_access(void)
{
	const struct bpf_insn program[] = {
		/* r6 is our tp buffer */
		BPF_LDX_MEM(BPF_DW, BPF_REG_6, BPF_REG_1, 0),
		/* one byte beyond the end of the writable context */
		BPF_LDX_MEM(BPF_B, BPF_REG_0, BPF_REG_6,
			    sizeof(struct bpf_testmod_test_writable_ctx)),
		BPF_EXIT_INSN(),
	};

	const struct bpf_insn negative_var_off_program[] = {
		BPF_LDX_MEM(BPF_DW, BPF_REG_6, BPF_REG_1, 0),
		/* make var_off negative, but keep the effective access offset non-negative */
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_6, -8),
		/* one byte beyond the end of the writable context */
		BPF_LDX_MEM(BPF_B, BPF_REG_0, BPF_REG_6,
			    sizeof(struct bpf_testmod_test_writable_ctx) + 8),
		BPF_EXIT_INSN(),
	};

	if (test__start_subtest("past_end"))
		check_attach_reject(program, ARRAY_SIZE(program));

	if (test__start_subtest("negative_var_off_past_end"))
		check_attach_reject(negative_var_off_program,
				    ARRAY_SIZE(negative_var_off_program));
}
