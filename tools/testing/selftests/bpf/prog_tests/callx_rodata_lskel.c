// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>

#include "callx_rodata.lskel.h"

#if defined(__x86_64__) || defined(__aarch64__) || \
    (defined(__riscv) && __riscv_xlen == 64)

static void run(int prog_fd, int expected, const char *name)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	char pkt[64] = {};

	topts.data_in = pkt;
	topts.data_size_in = sizeof(pkt);
	if (ASSERT_OK(bpf_prog_test_run_opts(prog_fd, &topts), name))
		ASSERT_EQ(topts.retval, expected, name);
}

/*
 * Every program that has callx gets its own copy of .rodata with the offsets
 * of its functions. The copies have what user space puts into .rodata before
 * the load. The program that doesn't have callx uses .rodata.
 */
static void __test_callx_rodata_lskel(void)
{
	struct callx_rodata_lskel *skel;

	/* there is no support for callx in the interpreter */
	if (!is_jit_enabled()) {
		test__skip();
		return;
	}

	skel = callx_rodata_lskel__open();
	if (!ASSERT_OK_PTR(skel, "open"))
		return;

	/* gcc doesn't support indirect calls */
	if (skel->rodata->skip) {
		test__skip();
		goto out;
	}

	skel->rodata->bias = 7;

	if (!ASSERT_OK(callx_rodata_lskel__load(skel), "load"))
		goto out;

	skel->bss->op_idx = 0;
	run(skel->progs.select_op.prog_fd, 17, "add_bias");
	skel->bss->op_idx = 1;
	run(skel->progs.select_op.prog_fd, 30, "mul3");
	skel->bss->op_idx = 2;
	run(skel->progs.select_op.prog_fd, -1, "out_of_range");
	/* mul3(add_bias(4)) */
	run(skel->progs.both_ops.prog_fd, 33, "both_ops");
	run(skel->progs.read_bias.prog_fd, 7, "read_bias");
out:
	callx_rodata_lskel__destroy(skel);
}

#else

static void __test_callx_rodata_lskel(void)
{
	test__skip();
}

#endif

void test_callx_rodata_lskel(void)
{
	__test_callx_rodata_lskel();
}
