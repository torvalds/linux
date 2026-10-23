// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include <linux/filter.h>

#define MAY_GOTO(off)	BPF_RAW_INSN(BPF_JMP | BPF_JCOND, 0, 0, off, 0)
#define FILL_CNT	33000

static struct bpf_insn *fill(struct bpf_insn *insn, int cnt, int reg)
{
	while (cnt--)
		*insn++ = BPF_MOV64_REG(BPF_REG_0, reg);
	return insn;
}

/*
 * Syscall prog that loops until may_goto expires. Its target is 'off' insns
 * away:
 *
 *	r6 = r7 = 0, but not a constant for the verifier
 *	r8 = 2
 *	r9 = 0
 *	r0 = r8		x FILL_CNT or 1
 * 1:	if r6 == r9 goto +1
 *	exit
 *	r6 = r8
 *	r0 = r9		x N when off is negative
 *	if r7 != r9 goto 3f
 * 2:	may_goto off	(1b or 4f)
 *	goto 2b
 * 3:	r0 = r8		x N when off is positive
 * 4:	exit
 *
 * The prog returns 0 when may_goto jumps to its target and 2 when
 * it lands in one of 'r0 = r8' areas. There are no constants between
 * may_goto and its target to keep the distance when constants are blinded.
 */
static void test_far(int off)
{
	LIBBPF_OPTS(bpf_prog_load_opts, opts, .prog_flags = BPF_F_SLEEPABLE);
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	int front = off < 0 ? 1 : FILL_CNT;
	int mid = off < 0 ? -off - 5 : 1;
	int back = off < 0 ? FILL_CNT : off - 1;
	struct bpf_insn *insns, *insn;
	int fd, err;

	insns = calloc(front + mid + back + 16, sizeof(*insns));
	if (!ASSERT_OK_PTR(insns, "calloc"))
		return;

	insn = insns;
	*insn++ = BPF_EMIT_CALL(BPF_FUNC_ktime_get_ns);
	*insn++ = BPF_MOV64_REG(BPF_REG_6, BPF_REG_0);
	*insn++ = BPF_ALU64_IMM(BPF_RSH, BPF_REG_6, 63);
	*insn++ = BPF_MOV64_REG(BPF_REG_7, BPF_REG_0);
	*insn++ = BPF_ALU64_IMM(BPF_RSH, BPF_REG_7, 63);
	*insn++ = BPF_MOV64_IMM(BPF_REG_8, 2);
	*insn++ = BPF_MOV64_IMM(BPF_REG_9, 0);
	insn = fill(insn, front, BPF_REG_8);
	*insn++ = BPF_JMP_REG(BPF_JEQ, BPF_REG_6, BPF_REG_9, 1);
	*insn++ = BPF_EXIT_INSN();
	*insn++ = BPF_MOV64_REG(BPF_REG_6, BPF_REG_8);
	insn = fill(insn, mid, BPF_REG_9);
	*insn++ = BPF_JMP_REG(BPF_JNE, BPF_REG_7, BPF_REG_9, 2);
	*insn++ = MAY_GOTO(off);
	*insn++ = BPF_JMP_A(-2);
	insn = fill(insn, back, BPF_REG_8);
	*insn++ = BPF_EXIT_INSN();

	fd = bpf_prog_load(BPF_PROG_TYPE_SYSCALL, NULL, "GPL", insns, insn - insns, &opts);
	free(insns);
	if (!ASSERT_GE(fd, 0, "prog_load"))
		return;

	err = bpf_prog_test_run_opts(fd, &topts);
	ASSERT_OK(err, "test_run");
	ASSERT_EQ(topts.retval, 0, "retval");
	close(fd);
}

void test_may_goto_far(void)
{
	if (test__start_subtest("32762"))
		test_far(32762);
	if (test__start_subtest("32767"))
		test_far(32767);
	if (test__start_subtest("-32767"))
		test_far(-32767);
	if (test__start_subtest("-32768"))
		test_far(-32768);
}
