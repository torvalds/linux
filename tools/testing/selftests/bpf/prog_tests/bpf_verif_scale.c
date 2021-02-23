// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook
#include <test_progs.h>
static int libbpf_debug_print(enum libbpf_print_level level,
			      const char *format, va_list args)
{
	if (level != LIBBPF_DEBUG) {
		vprintf(format, args);
		return 0;
	}

	if (!strstr(format, "verifier log"))
		return 0;
	vprintf("%s", args);
	return 0;
}

extern int extra_prog_load_log_flags;

static int check_load(const char *file, enum bpf_prog_type type)
{
	struct bpf_prog_load_attr attr;
	struct bpf_object *obj = NULL;
	int err, prog_fd;

	memset(&attr, 0, sizeof(struct bpf_prog_load_attr));
	attr.file = file;
	attr.prog_type = type;
	attr.log_level = 4 | extra_prog_load_log_flags;
	attr.prog_flags = BPF_F_TEST_RND_HI32;
	err = bpf_prog_load_xattr(&attr, &obj, &prog_fd);
	bpf_object__close(obj);
	return err;
}

struct scale_test_def {
	const char *file;
	enum bpf_prog_type attach_type;
	bool fails;
};

void test_bpf_verif_scale(void)
{
	struct scale_test_def tests[] = {
		{ "loop3.o", BPF_PROG_TYPE_RAW_TRACEPOINT, true /* fails */ },

		{ "test_verif_scale1.o", BPF_PROG_TYPE_SCHED_CLS },
		{ "test_verif_scale2.o", BPF_PROG_TYPE_SCHED_CLS },
		{ "test_verif_scale3.o", BPF_PROG_TYPE_SCHED_CLS },

		{ "pyperf_global.o", BPF_PROG_TYPE_RAW_TRACEPOINT },
		{ "pyperf_subprogs.o", BPF_PROG_TYPE_RAW_TRACEPOINT },

		/* full unroll by llvm */
		{ "pyperf50.o", BPF_PROG_TYPE_RAW_TRACEPOINT },
		{ "pyperf100.o", BPF_PROG_TYPE_RAW_TRACEPOINT },
		{ "pyperf180.o", BPF_PROG_TYPE_RAW_TRACEPOINT },

		/* partial unroll. llvm will unroll loop ~150 times.
		 * C loop count -> 600.
		 * Asm loop count -> 4.
		 * 16k insns in loop body.
		 * Total of 5 such loops. Total program size ~82k insns.
		 */
		{ "pyperf600.o", BPF_PROG_TYPE_RAW_TRACEPOINT },

		/* no unroll at all.
		 * C loop count -> 600.
		 * ASM loop count -> 600.
		 * ~110 insns in loop body.
		 * Total of 5 such loops. Total program size ~1500 insns.
		 */
		{ "pyperf600_nounroll.o", BPF_PROG_TYPE_RAW_TRACEPOINT },

		{ "loop1.o", BPF_PROG_TYPE_RAW_TRACEPOINT },
		{ "loop2.o", BPF_PROG_TYPE_RAW_TRACEPOINT },
		{ "loop4.o", BPF_PROG_TYPE_SCHED_CLS },
		{ "loop5.o", BPF_PROG_TYPE_SCHED_CLS },

		/* partial unroll. 19k insn in a loop.
		 * Total program size 20.8k insn.
		 * ~350k processed_insns
		 */
		{ "strobemeta.o", BPF_PROG_TYPE_RAW_TRACEPOINT },

		/* no unroll, tiny loops */
		{ "strobemeta_nounroll1.o", BPF_PROG_TYPE_RAW_TRACEPOINT },
		{ "strobemeta_nounroll2.o", BPF_PROG_TYPE_RAW_TRACEPOINT },

		/* non-inlined subprogs */
		{ "strobemeta_subprogs.o", BPF_PROG_TYPE_RAW_TRACEPOINT },

		{ "test_sysctl_loop1.o", BPF_PROG_TYPE_CGROUP_SYSCTL },
		{ "test_sysctl_loop2.o", BPF_PROG_TYPE_CGROUP_SYSCTL },

		{ "test_xdp_loop.o", BPF_PROG_TYPE_XDP },
		{ "test_seg6_loop.o", BPF_PROG_TYPE_LWT_SEG6LOCAL },
	};
	libbpf_print_fn_t old_print_fn = NULL;
	int err, i;

	if (env.verifier_stats) {
		test__force_log();
		old_print_fn = libbpf_set_print(libbpf_debug_print);
	}

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		const struct scale_test_def *test = &tests[i];

		if (!test__start_subtest(test->file))
			continue;

		err = check_load(test->file, test->attach_type);
		CHECK_FAIL(err && !test->fails);
	}

	if (env.verifier_stats)
		libbpf_set_print(old_print_fn);
}
