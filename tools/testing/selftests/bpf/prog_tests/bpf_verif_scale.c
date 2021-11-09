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

static void scale_test(const char *file,
		       enum bpf_prog_type attach_type,
		       bool should_fail)
{
	libbpf_print_fn_t old_print_fn = NULL;
	int err;

	if (env.verifier_stats) {
		test__force_log();
		old_print_fn = libbpf_set_print(libbpf_debug_print);
	}

	err = check_load(file, attach_type);
	if (should_fail)
		ASSERT_ERR(err, "expect_error");
	else
		ASSERT_OK(err, "expect_success");

	if (env.verifier_stats)
		libbpf_set_print(old_print_fn);
}

void test_verif_scale1()
{
	scale_test("test_verif_scale1.o", BPF_PROG_TYPE_SCHED_CLS, false);
}

void test_verif_scale2()
{
	scale_test("test_verif_scale2.o", BPF_PROG_TYPE_SCHED_CLS, false);
}

void test_verif_scale3()
{
	scale_test("test_verif_scale3.o", BPF_PROG_TYPE_SCHED_CLS, false);
}

void test_verif_scale_pyperf_global()
{
	scale_test("pyperf_global.o", BPF_PROG_TYPE_RAW_TRACEPOINT, false);
}

void test_verif_scale_pyperf_subprogs()
{
	scale_test("pyperf_subprogs.o", BPF_PROG_TYPE_RAW_TRACEPOINT, false);
}

void test_verif_scale_pyperf50()
{
	/* full unroll by llvm */
	scale_test("pyperf50.o", BPF_PROG_TYPE_RAW_TRACEPOINT, false);
}

void test_verif_scale_pyperf100()
{
	/* full unroll by llvm */
	scale_test("pyperf100.o", BPF_PROG_TYPE_RAW_TRACEPOINT, false);
}

void test_verif_scale_pyperf180()
{
	/* full unroll by llvm */
	scale_test("pyperf180.o", BPF_PROG_TYPE_RAW_TRACEPOINT, false);
}

void test_verif_scale_pyperf600()
{
	/* partial unroll. llvm will unroll loop ~150 times.
	 * C loop count -> 600.
	 * Asm loop count -> 4.
	 * 16k insns in loop body.
	 * Total of 5 such loops. Total program size ~82k insns.
	 */
	scale_test("pyperf600.o", BPF_PROG_TYPE_RAW_TRACEPOINT, false);
}

void test_verif_scale_pyperf600_nounroll()
{
	/* no unroll at all.
	 * C loop count -> 600.
	 * ASM loop count -> 600.
	 * ~110 insns in loop body.
	 * Total of 5 such loops. Total program size ~1500 insns.
	 */
	scale_test("pyperf600_nounroll.o", BPF_PROG_TYPE_RAW_TRACEPOINT, false);
}

void test_verif_scale_loop1()
{
	scale_test("loop1.o", BPF_PROG_TYPE_RAW_TRACEPOINT, false);
}

void test_verif_scale_loop2()
{
	scale_test("loop2.o", BPF_PROG_TYPE_RAW_TRACEPOINT, false);
}

void test_verif_scale_loop3_fail()
{
	scale_test("loop3.o", BPF_PROG_TYPE_RAW_TRACEPOINT, true /* fails */);
}

void test_verif_scale_loop4()
{
	scale_test("loop4.o", BPF_PROG_TYPE_SCHED_CLS, false);
}

void test_verif_scale_loop5()
{
	scale_test("loop5.o", BPF_PROG_TYPE_SCHED_CLS, false);
}

void test_verif_scale_loop6()
{
	scale_test("loop6.o", BPF_PROG_TYPE_KPROBE, false);
}

void test_verif_scale_strobemeta()
{
	/* partial unroll. 19k insn in a loop.
	 * Total program size 20.8k insn.
	 * ~350k processed_insns
	 */
	scale_test("strobemeta.o", BPF_PROG_TYPE_RAW_TRACEPOINT, false);
}

void test_verif_scale_strobemeta_nounroll1()
{
	/* no unroll, tiny loops */
	scale_test("strobemeta_nounroll1.o", BPF_PROG_TYPE_RAW_TRACEPOINT, false);
}

void test_verif_scale_strobemeta_nounroll2()
{
	/* no unroll, tiny loops */
	scale_test("strobemeta_nounroll2.o", BPF_PROG_TYPE_RAW_TRACEPOINT, false);
}

void test_verif_scale_strobemeta_subprogs()
{
	/* non-inlined subprogs */
	scale_test("strobemeta_subprogs.o", BPF_PROG_TYPE_RAW_TRACEPOINT, false);
}

void test_verif_scale_sysctl_loop1()
{
	scale_test("test_sysctl_loop1.o", BPF_PROG_TYPE_CGROUP_SYSCTL, false);
}

void test_verif_scale_sysctl_loop2()
{
	scale_test("test_sysctl_loop2.o", BPF_PROG_TYPE_CGROUP_SYSCTL, false);
}

void test_verif_scale_xdp_loop()
{
	scale_test("test_xdp_loop.o", BPF_PROG_TYPE_XDP, false);
}

void test_verif_scale_seg6_loop()
{
	scale_test("test_seg6_loop.o", BPF_PROG_TYPE_LWT_SEG6LOCAL, false);
}
