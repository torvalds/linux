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
	struct bpf_object *obj = NULL;
	struct bpf_program *prog;
	int err;

	obj = bpf_object__open_file(file, NULL);
	err = libbpf_get_error(obj);
	if (err)
		return err;

	prog = bpf_object__next_program(obj, NULL);
	if (!prog) {
		err = -ENOENT;
		goto err_out;
	}

	bpf_program__set_type(prog, type);
	bpf_program__set_flags(prog, BPF_F_TEST_RND_HI32 | BPF_F_TEST_REG_INVARIANTS);
	bpf_program__set_log_level(prog, 4 | extra_prog_load_log_flags);

	err = bpf_object__load(obj);

err_out:
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
	scale_test("test_verif_scale1.bpf.o", BPF_PROG_TYPE_SCHED_CLS, false);
}

void test_verif_scale2()
{
	scale_test("test_verif_scale2.bpf.o", BPF_PROG_TYPE_SCHED_CLS, false);
}

void test_verif_scale3()
{
	scale_test("test_verif_scale3.bpf.o", BPF_PROG_TYPE_SCHED_CLS, false);
}

void test_verif_scale_pyperf_global()
{
	scale_test("pyperf_global.bpf.o", BPF_PROG_TYPE_RAW_TRACEPOINT, false);
}

void test_verif_scale_pyperf_subprogs()
{
	scale_test("pyperf_subprogs.bpf.o", BPF_PROG_TYPE_RAW_TRACEPOINT, false);
}

void test_verif_scale_pyperf50()
{
	/* full unroll by llvm */
	scale_test("pyperf50.bpf.o", BPF_PROG_TYPE_RAW_TRACEPOINT, false);
}

void test_verif_scale_pyperf100()
{
	/* full unroll by llvm */
	scale_test("pyperf100.bpf.o", BPF_PROG_TYPE_RAW_TRACEPOINT, false);
}

void test_verif_scale_pyperf180()
{
	/* full unroll by llvm */
	scale_test("pyperf180.bpf.o", BPF_PROG_TYPE_RAW_TRACEPOINT, false);
}

void test_verif_scale_pyperf600()
{
	/* partial unroll. llvm will unroll loop ~150 times.
	 * C loop count -> 600.
	 * Asm loop count -> 4.
	 * 16k insns in loop body.
	 * Total of 5 such loops. Total program size ~82k insns.
	 */
	scale_test("pyperf600.bpf.o", BPF_PROG_TYPE_RAW_TRACEPOINT, false);
}

void test_verif_scale_pyperf600_bpf_loop(void)
{
	/* use the bpf_loop helper*/
	scale_test("pyperf600_bpf_loop.bpf.o", BPF_PROG_TYPE_RAW_TRACEPOINT, false);
}

void test_verif_scale_pyperf600_nounroll()
{
	/* no unroll at all.
	 * C loop count -> 600.
	 * ASM loop count -> 600.
	 * ~110 insns in loop body.
	 * Total of 5 such loops. Total program size ~1500 insns.
	 */
	scale_test("pyperf600_nounroll.bpf.o", BPF_PROG_TYPE_RAW_TRACEPOINT, false);
}

void test_verif_scale_pyperf600_iter()
{
	/* open-coded BPF iterator version */
	scale_test("pyperf600_iter.bpf.o", BPF_PROG_TYPE_RAW_TRACEPOINT, false);
}

void test_verif_scale_loop1()
{
	scale_test("loop1.bpf.o", BPF_PROG_TYPE_RAW_TRACEPOINT, false);
}

void test_verif_scale_loop2()
{
	scale_test("loop2.bpf.o", BPF_PROG_TYPE_RAW_TRACEPOINT, false);
}

void test_verif_scale_loop3_fail()
{
	scale_test("loop3.bpf.o", BPF_PROG_TYPE_RAW_TRACEPOINT, true /* fails */);
}

void test_verif_scale_loop4()
{
	scale_test("loop4.bpf.o", BPF_PROG_TYPE_SCHED_CLS, false);
}

void test_verif_scale_loop5()
{
	scale_test("loop5.bpf.o", BPF_PROG_TYPE_SCHED_CLS, false);
}

void test_verif_scale_loop6()
{
	scale_test("loop6.bpf.o", BPF_PROG_TYPE_KPROBE, false);
}

void test_verif_scale_strobemeta()
{
	/* partial unroll. 19k insn in a loop.
	 * Total program size 20.8k insn.
	 * ~350k processed_insns
	 */
	scale_test("strobemeta.bpf.o", BPF_PROG_TYPE_RAW_TRACEPOINT, false);
}

void test_verif_scale_strobemeta_bpf_loop(void)
{
	/* use the bpf_loop helper*/
	scale_test("strobemeta_bpf_loop.bpf.o", BPF_PROG_TYPE_RAW_TRACEPOINT, false);
}

void test_verif_scale_strobemeta_nounroll1()
{
	/* no unroll, tiny loops */
	scale_test("strobemeta_nounroll1.bpf.o", BPF_PROG_TYPE_RAW_TRACEPOINT, false);
}

void test_verif_scale_strobemeta_nounroll2()
{
	/* no unroll, tiny loops */
	scale_test("strobemeta_nounroll2.bpf.o", BPF_PROG_TYPE_RAW_TRACEPOINT, false);
}

void test_verif_scale_strobemeta_subprogs()
{
	/* non-inlined subprogs */
	scale_test("strobemeta_subprogs.bpf.o", BPF_PROG_TYPE_RAW_TRACEPOINT, false);
}

void test_verif_scale_sysctl_loop1()
{
	scale_test("test_sysctl_loop1.bpf.o", BPF_PROG_TYPE_CGROUP_SYSCTL, false);
}

void test_verif_scale_sysctl_loop2()
{
	scale_test("test_sysctl_loop2.bpf.o", BPF_PROG_TYPE_CGROUP_SYSCTL, false);
}

void test_verif_scale_xdp_loop()
{
	scale_test("test_xdp_loop.bpf.o", BPF_PROG_TYPE_XDP, false);
}

void test_verif_scale_seg6_loop()
{
	scale_test("test_seg6_loop.bpf.o", BPF_PROG_TYPE_LWT_SEG6LOCAL, false);
}

void test_verif_twfw()
{
	scale_test("twfw.bpf.o", BPF_PROG_TYPE_CGROUP_SKB, false);
}
