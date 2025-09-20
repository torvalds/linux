// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023. Huawei Technologies Co., Ltd */
#include <test_progs.h>

#include "linux/filter.h"
#include "kptr_xchg_inline.skel.h"

void test_kptr_xchg_inline(void)
{
	struct kptr_xchg_inline *skel;
	struct bpf_insn *insn = NULL;
	struct bpf_insn exp;
	unsigned int cnt;
	int err;

#if !(defined(__x86_64__) || defined(__aarch64__) || \
      (defined(__riscv) && __riscv_xlen == 64))
	test__skip();
	return;
#endif

	skel = kptr_xchg_inline__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_load"))
		return;

	err = get_xlated_program(bpf_program__fd(skel->progs.kptr_xchg_inline), &insn, &cnt);
	if (!ASSERT_OK(err, "prog insn"))
		goto out;

	/* The original instructions are:
	 * r1 = map[id:xxx][0]+0
	 * r2 = 0
	 * call bpf_kptr_xchg#yyy
	 *
	 * call bpf_kptr_xchg#yyy will be inlined as:
	 * r0 = r2
	 * r0 = atomic64_xchg((u64 *)(r1 +0), r0)
	 */
	if (!ASSERT_GT(cnt, 5, "insn cnt"))
		goto out;

	exp = BPF_MOV64_REG(BPF_REG_0, BPF_REG_2);
	if (!ASSERT_OK(memcmp(&insn[3], &exp, sizeof(exp)), "mov"))
		goto out;

	exp = BPF_ATOMIC_OP(BPF_DW, BPF_XCHG, BPF_REG_1, BPF_REG_0, 0);
	if (!ASSERT_OK(memcmp(&insn[4], &exp, sizeof(exp)), "xchg"))
		goto out;
out:
	free(insn);
	kptr_xchg_inline__destroy(skel);
}
