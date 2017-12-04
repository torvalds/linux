#include <asm/types.h>
#include <linux/types.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>

#include <sys/resource.h>

#include <linux/unistd.h>
#include <linux/filter.h>
#include <linux/bpf_perf_event.h>
#include <linux/bpf.h>

#include <bpf/bpf.h>

#include "../../../include/linux/filter.h"

#ifndef ARRAY_SIZE
# define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

#define MAX_INSNS	512
#define MAX_MATCHES	16

struct bpf_reg_match {
	unsigned int line;
	const char *match;
};

struct bpf_align_test {
	const char *descr;
	struct bpf_insn	insns[MAX_INSNS];
	enum {
		UNDEF,
		ACCEPT,
		REJECT
	} result;
	enum bpf_prog_type prog_type;
	/* Matches must be in order of increasing line */
	struct bpf_reg_match matches[MAX_MATCHES];
};

static struct bpf_align_test tests[] = {
	/* Four tests of known constants.  These aren't staggeringly
	 * interesting since we track exact values now.
	 */
	{
		.descr = "mov",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_3, 2),
			BPF_MOV64_IMM(BPF_REG_3, 4),
			BPF_MOV64_IMM(BPF_REG_3, 8),
			BPF_MOV64_IMM(BPF_REG_3, 16),
			BPF_MOV64_IMM(BPF_REG_3, 32),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.matches = {
			{1, "R1=ctx(id=0,off=0,imm=0)"},
			{1, "R10=fp0"},
			{1, "R3_w=inv2"},
			{2, "R3_w=inv4"},
			{3, "R3_w=inv8"},
			{4, "R3_w=inv16"},
			{5, "R3_w=inv32"},
		},
	},
	{
		.descr = "shift",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_3, 1),
			BPF_ALU64_IMM(BPF_LSH, BPF_REG_3, 1),
			BPF_ALU64_IMM(BPF_LSH, BPF_REG_3, 1),
			BPF_ALU64_IMM(BPF_LSH, BPF_REG_3, 1),
			BPF_ALU64_IMM(BPF_LSH, BPF_REG_3, 1),
			BPF_ALU64_IMM(BPF_RSH, BPF_REG_3, 4),
			BPF_MOV64_IMM(BPF_REG_4, 32),
			BPF_ALU64_IMM(BPF_RSH, BPF_REG_4, 1),
			BPF_ALU64_IMM(BPF_RSH, BPF_REG_4, 1),
			BPF_ALU64_IMM(BPF_RSH, BPF_REG_4, 1),
			BPF_ALU64_IMM(BPF_RSH, BPF_REG_4, 1),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.matches = {
			{1, "R1=ctx(id=0,off=0,imm=0)"},
			{1, "R10=fp0"},
			{1, "R3_w=inv1"},
			{2, "R3_w=inv2"},
			{3, "R3_w=inv4"},
			{4, "R3_w=inv8"},
			{5, "R3_w=inv16"},
			{6, "R3_w=inv1"},
			{7, "R4_w=inv32"},
			{8, "R4_w=inv16"},
			{9, "R4_w=inv8"},
			{10, "R4_w=inv4"},
			{11, "R4_w=inv2"},
		},
	},
	{
		.descr = "addsub",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_3, 4),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_3, 4),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_3, 2),
			BPF_MOV64_IMM(BPF_REG_4, 8),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, 4),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, 2),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.matches = {
			{1, "R1=ctx(id=0,off=0,imm=0)"},
			{1, "R10=fp0"},
			{1, "R3_w=inv4"},
			{2, "R3_w=inv8"},
			{3, "R3_w=inv10"},
			{4, "R4_w=inv8"},
			{5, "R4_w=inv12"},
			{6, "R4_w=inv14"},
		},
	},
	{
		.descr = "mul",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_3, 7),
			BPF_ALU64_IMM(BPF_MUL, BPF_REG_3, 1),
			BPF_ALU64_IMM(BPF_MUL, BPF_REG_3, 2),
			BPF_ALU64_IMM(BPF_MUL, BPF_REG_3, 4),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.matches = {
			{1, "R1=ctx(id=0,off=0,imm=0)"},
			{1, "R10=fp0"},
			{1, "R3_w=inv7"},
			{2, "R3_w=inv7"},
			{3, "R3_w=inv14"},
			{4, "R3_w=inv56"},
		},
	},

	/* Tests using unknown values */
#define PREP_PKT_POINTERS \
	BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1, \
		    offsetof(struct __sk_buff, data)), \
	BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1, \
		    offsetof(struct __sk_buff, data_end))

#define LOAD_UNKNOWN(DST_REG) \
	PREP_PKT_POINTERS, \
	BPF_MOV64_REG(BPF_REG_0, BPF_REG_2), \
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 8), \
	BPF_JMP_REG(BPF_JGE, BPF_REG_3, BPF_REG_0, 1), \
	BPF_EXIT_INSN(), \
	BPF_LDX_MEM(BPF_B, DST_REG, BPF_REG_2, 0)

	{
		.descr = "unknown shift",
		.insns = {
			LOAD_UNKNOWN(BPF_REG_3),
			BPF_ALU64_IMM(BPF_LSH, BPF_REG_3, 1),
			BPF_ALU64_IMM(BPF_LSH, BPF_REG_3, 1),
			BPF_ALU64_IMM(BPF_LSH, BPF_REG_3, 1),
			BPF_ALU64_IMM(BPF_LSH, BPF_REG_3, 1),
			LOAD_UNKNOWN(BPF_REG_4),
			BPF_ALU64_IMM(BPF_LSH, BPF_REG_4, 5),
			BPF_ALU64_IMM(BPF_RSH, BPF_REG_4, 1),
			BPF_ALU64_IMM(BPF_RSH, BPF_REG_4, 1),
			BPF_ALU64_IMM(BPF_RSH, BPF_REG_4, 1),
			BPF_ALU64_IMM(BPF_RSH, BPF_REG_4, 1),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.matches = {
			{7, "R0=pkt(id=0,off=8,r=8,imm=0)"},
			{7, "R3_w=inv(id=0,umax_value=255,var_off=(0x0; 0xff))"},
			{8, "R3_w=inv(id=0,umax_value=510,var_off=(0x0; 0x1fe))"},
			{9, "R3_w=inv(id=0,umax_value=1020,var_off=(0x0; 0x3fc))"},
			{10, "R3_w=inv(id=0,umax_value=2040,var_off=(0x0; 0x7f8))"},
			{11, "R3_w=inv(id=0,umax_value=4080,var_off=(0x0; 0xff0))"},
			{18, "R3=pkt_end(id=0,off=0,imm=0)"},
			{18, "R4_w=inv(id=0,umax_value=255,var_off=(0x0; 0xff))"},
			{19, "R4_w=inv(id=0,umax_value=8160,var_off=(0x0; 0x1fe0))"},
			{20, "R4_w=inv(id=0,umax_value=4080,var_off=(0x0; 0xff0))"},
			{21, "R4_w=inv(id=0,umax_value=2040,var_off=(0x0; 0x7f8))"},
			{22, "R4_w=inv(id=0,umax_value=1020,var_off=(0x0; 0x3fc))"},
			{23, "R4_w=inv(id=0,umax_value=510,var_off=(0x0; 0x1fe))"},
		},
	},
	{
		.descr = "unknown mul",
		.insns = {
			LOAD_UNKNOWN(BPF_REG_3),
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_3),
			BPF_ALU64_IMM(BPF_MUL, BPF_REG_4, 1),
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_3),
			BPF_ALU64_IMM(BPF_MUL, BPF_REG_4, 2),
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_3),
			BPF_ALU64_IMM(BPF_MUL, BPF_REG_4, 4),
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_3),
			BPF_ALU64_IMM(BPF_MUL, BPF_REG_4, 8),
			BPF_ALU64_IMM(BPF_MUL, BPF_REG_4, 2),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.matches = {
			{7, "R3_w=inv(id=0,umax_value=255,var_off=(0x0; 0xff))"},
			{8, "R4_w=inv(id=0,umax_value=255,var_off=(0x0; 0xff))"},
			{9, "R4_w=inv(id=0,umax_value=255,var_off=(0x0; 0xff))"},
			{10, "R4_w=inv(id=0,umax_value=255,var_off=(0x0; 0xff))"},
			{11, "R4_w=inv(id=0,umax_value=510,var_off=(0x0; 0x1fe))"},
			{12, "R4_w=inv(id=0,umax_value=255,var_off=(0x0; 0xff))"},
			{13, "R4_w=inv(id=0,umax_value=1020,var_off=(0x0; 0x3fc))"},
			{14, "R4_w=inv(id=0,umax_value=255,var_off=(0x0; 0xff))"},
			{15, "R4_w=inv(id=0,umax_value=2040,var_off=(0x0; 0x7f8))"},
			{16, "R4_w=inv(id=0,umax_value=4080,var_off=(0x0; 0xff0))"},
		},
	},
	{
		.descr = "packet const offset",
		.insns = {
			PREP_PKT_POINTERS,
			BPF_MOV64_REG(BPF_REG_5, BPF_REG_2),

			BPF_MOV64_IMM(BPF_REG_0, 0),

			/* Skip over ethernet header.  */
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_5, 14),
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_5),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, 4),
			BPF_JMP_REG(BPF_JGE, BPF_REG_3, BPF_REG_4, 1),
			BPF_EXIT_INSN(),

			BPF_LDX_MEM(BPF_B, BPF_REG_4, BPF_REG_5, 0),
			BPF_LDX_MEM(BPF_B, BPF_REG_4, BPF_REG_5, 1),
			BPF_LDX_MEM(BPF_B, BPF_REG_4, BPF_REG_5, 2),
			BPF_LDX_MEM(BPF_B, BPF_REG_4, BPF_REG_5, 3),
			BPF_LDX_MEM(BPF_H, BPF_REG_4, BPF_REG_5, 0),
			BPF_LDX_MEM(BPF_H, BPF_REG_4, BPF_REG_5, 2),
			BPF_LDX_MEM(BPF_W, BPF_REG_4, BPF_REG_5, 0),

			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.matches = {
			{4, "R5_w=pkt(id=0,off=0,r=0,imm=0)"},
			{5, "R5_w=pkt(id=0,off=14,r=0,imm=0)"},
			{6, "R4_w=pkt(id=0,off=14,r=0,imm=0)"},
			{10, "R2=pkt(id=0,off=0,r=18,imm=0)"},
			{10, "R5=pkt(id=0,off=14,r=18,imm=0)"},
			{10, "R4_w=inv(id=0,umax_value=255,var_off=(0x0; 0xff))"},
			{14, "R4_w=inv(id=0,umax_value=65535,var_off=(0x0; 0xffff))"},
			{15, "R4_w=inv(id=0,umax_value=65535,var_off=(0x0; 0xffff))"},
		},
	},
	{
		.descr = "packet variable offset",
		.insns = {
			LOAD_UNKNOWN(BPF_REG_6),
			BPF_ALU64_IMM(BPF_LSH, BPF_REG_6, 2),

			/* First, add a constant to the R5 packet pointer,
			 * then a variable with a known alignment.
			 */
			BPF_MOV64_REG(BPF_REG_5, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_5, 14),
			BPF_ALU64_REG(BPF_ADD, BPF_REG_5, BPF_REG_6),
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_5),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, 4),
			BPF_JMP_REG(BPF_JGE, BPF_REG_3, BPF_REG_4, 1),
			BPF_EXIT_INSN(),
			BPF_LDX_MEM(BPF_W, BPF_REG_4, BPF_REG_5, 0),

			/* Now, test in the other direction.  Adding first
			 * the variable offset to R5, then the constant.
			 */
			BPF_MOV64_REG(BPF_REG_5, BPF_REG_2),
			BPF_ALU64_REG(BPF_ADD, BPF_REG_5, BPF_REG_6),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_5, 14),
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_5),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, 4),
			BPF_JMP_REG(BPF_JGE, BPF_REG_3, BPF_REG_4, 1),
			BPF_EXIT_INSN(),
			BPF_LDX_MEM(BPF_W, BPF_REG_4, BPF_REG_5, 0),

			/* Test multiple accumulations of unknown values
			 * into a packet pointer.
			 */
			BPF_MOV64_REG(BPF_REG_5, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_5, 14),
			BPF_ALU64_REG(BPF_ADD, BPF_REG_5, BPF_REG_6),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_5, 4),
			BPF_ALU64_REG(BPF_ADD, BPF_REG_5, BPF_REG_6),
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_5),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, 4),
			BPF_JMP_REG(BPF_JGE, BPF_REG_3, BPF_REG_4, 1),
			BPF_EXIT_INSN(),
			BPF_LDX_MEM(BPF_W, BPF_REG_4, BPF_REG_5, 0),

			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.matches = {
			/* Calculated offset in R6 has unknown value, but known
			 * alignment of 4.
			 */
			{8, "R2=pkt(id=0,off=0,r=8,imm=0)"},
			{8, "R6_w=inv(id=0,umax_value=1020,var_off=(0x0; 0x3fc))"},
			/* Offset is added to packet pointer R5, resulting in
			 * known fixed offset, and variable offset from R6.
			 */
			{11, "R5_w=pkt(id=1,off=14,r=0,umax_value=1020,var_off=(0x0; 0x3fc))"},
			/* At the time the word size load is performed from R5,
			 * it's total offset is NET_IP_ALIGN + reg->off (0) +
			 * reg->aux_off (14) which is 16.  Then the variable
			 * offset is considered using reg->aux_off_align which
			 * is 4 and meets the load's requirements.
			 */
			{15, "R4=pkt(id=1,off=18,r=18,umax_value=1020,var_off=(0x0; 0x3fc))"},
			{15, "R5=pkt(id=1,off=14,r=18,umax_value=1020,var_off=(0x0; 0x3fc))"},
			/* Variable offset is added to R5 packet pointer,
			 * resulting in auxiliary alignment of 4.
			 */
			{18, "R5_w=pkt(id=2,off=0,r=0,umax_value=1020,var_off=(0x0; 0x3fc))"},
			/* Constant offset is added to R5, resulting in
			 * reg->off of 14.
			 */
			{19, "R5_w=pkt(id=2,off=14,r=0,umax_value=1020,var_off=(0x0; 0x3fc))"},
			/* At the time the word size load is performed from R5,
			 * its total fixed offset is NET_IP_ALIGN + reg->off
			 * (14) which is 16.  Then the variable offset is 4-byte
			 * aligned, so the total offset is 4-byte aligned and
			 * meets the load's requirements.
			 */
			{23, "R4=pkt(id=2,off=18,r=18,umax_value=1020,var_off=(0x0; 0x3fc))"},
			{23, "R5=pkt(id=2,off=14,r=18,umax_value=1020,var_off=(0x0; 0x3fc))"},
			/* Constant offset is added to R5 packet pointer,
			 * resulting in reg->off value of 14.
			 */
			{26, "R5_w=pkt(id=0,off=14,r=8"},
			/* Variable offset is added to R5, resulting in a
			 * variable offset of (4n).
			 */
			{27, "R5_w=pkt(id=3,off=14,r=0,umax_value=1020,var_off=(0x0; 0x3fc))"},
			/* Constant is added to R5 again, setting reg->off to 18. */
			{28, "R5_w=pkt(id=3,off=18,r=0,umax_value=1020,var_off=(0x0; 0x3fc))"},
			/* And once more we add a variable; resulting var_off
			 * is still (4n), fixed offset is not changed.
			 * Also, we create a new reg->id.
			 */
			{29, "R5_w=pkt(id=4,off=18,r=0,umax_value=2040,var_off=(0x0; 0x7fc))"},
			/* At the time the word size load is performed from R5,
			 * its total fixed offset is NET_IP_ALIGN + reg->off (18)
			 * which is 20.  Then the variable offset is (4n), so
			 * the total offset is 4-byte aligned and meets the
			 * load's requirements.
			 */
			{33, "R4=pkt(id=4,off=22,r=22,umax_value=2040,var_off=(0x0; 0x7fc))"},
			{33, "R5=pkt(id=4,off=18,r=22,umax_value=2040,var_off=(0x0; 0x7fc))"},
		},
	},
	{
		.descr = "packet variable offset 2",
		.insns = {
			/* Create an unknown offset, (4n+2)-aligned */
			LOAD_UNKNOWN(BPF_REG_6),
			BPF_ALU64_IMM(BPF_LSH, BPF_REG_6, 2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_6, 14),
			/* Add it to the packet pointer */
			BPF_MOV64_REG(BPF_REG_5, BPF_REG_2),
			BPF_ALU64_REG(BPF_ADD, BPF_REG_5, BPF_REG_6),
			/* Check bounds and perform a read */
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_5),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, 4),
			BPF_JMP_REG(BPF_JGE, BPF_REG_3, BPF_REG_4, 1),
			BPF_EXIT_INSN(),
			BPF_LDX_MEM(BPF_W, BPF_REG_6, BPF_REG_5, 0),
			/* Make a (4n) offset from the value we just read */
			BPF_ALU64_IMM(BPF_AND, BPF_REG_6, 0xff),
			BPF_ALU64_IMM(BPF_LSH, BPF_REG_6, 2),
			/* Add it to the packet pointer */
			BPF_ALU64_REG(BPF_ADD, BPF_REG_5, BPF_REG_6),
			/* Check bounds and perform a read */
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_5),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, 4),
			BPF_JMP_REG(BPF_JGE, BPF_REG_3, BPF_REG_4, 1),
			BPF_EXIT_INSN(),
			BPF_LDX_MEM(BPF_W, BPF_REG_6, BPF_REG_5, 0),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.matches = {
			/* Calculated offset in R6 has unknown value, but known
			 * alignment of 4.
			 */
			{8, "R2=pkt(id=0,off=0,r=8,imm=0)"},
			{8, "R6_w=inv(id=0,umax_value=1020,var_off=(0x0; 0x3fc))"},
			/* Adding 14 makes R6 be (4n+2) */
			{9, "R6_w=inv(id=0,umin_value=14,umax_value=1034,var_off=(0x2; 0x7fc))"},
			/* Packet pointer has (4n+2) offset */
			{11, "R5_w=pkt(id=1,off=0,r=0,umin_value=14,umax_value=1034,var_off=(0x2; 0x7fc))"},
			{13, "R4=pkt(id=1,off=4,r=0,umin_value=14,umax_value=1034,var_off=(0x2; 0x7fc))"},
			/* At the time the word size load is performed from R5,
			 * its total fixed offset is NET_IP_ALIGN + reg->off (0)
			 * which is 2.  Then the variable offset is (4n+2), so
			 * the total offset is 4-byte aligned and meets the
			 * load's requirements.
			 */
			{15, "R5=pkt(id=1,off=0,r=4,umin_value=14,umax_value=1034,var_off=(0x2; 0x7fc))"},
			/* Newly read value in R6 was shifted left by 2, so has
			 * known alignment of 4.
			 */
			{18, "R6_w=inv(id=0,umax_value=1020,var_off=(0x0; 0x3fc))"},
			/* Added (4n) to packet pointer's (4n+2) var_off, giving
			 * another (4n+2).
			 */
			{19, "R5_w=pkt(id=2,off=0,r=0,umin_value=14,umax_value=2054,var_off=(0x2; 0xffc))"},
			{21, "R4=pkt(id=2,off=4,r=0,umin_value=14,umax_value=2054,var_off=(0x2; 0xffc))"},
			/* At the time the word size load is performed from R5,
			 * its total fixed offset is NET_IP_ALIGN + reg->off (0)
			 * which is 2.  Then the variable offset is (4n+2), so
			 * the total offset is 4-byte aligned and meets the
			 * load's requirements.
			 */
			{23, "R5=pkt(id=2,off=0,r=4,umin_value=14,umax_value=2054,var_off=(0x2; 0xffc))"},
		},
	},
	{
		.descr = "dubious pointer arithmetic",
		.insns = {
			PREP_PKT_POINTERS,
			BPF_MOV64_IMM(BPF_REG_0, 0),
			/* ptr & const => unknown & const */
			BPF_MOV64_REG(BPF_REG_5, BPF_REG_2),
			BPF_ALU64_IMM(BPF_AND, BPF_REG_5, 0x40),
			/* ptr << const => unknown << const */
			BPF_MOV64_REG(BPF_REG_5, BPF_REG_2),
			BPF_ALU64_IMM(BPF_LSH, BPF_REG_5, 2),
			/* We have a (4n) value.  Let's make a packet offset
			 * out of it.  First add 14, to make it a (4n+2)
			 */
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_5, 14),
			/* Then make sure it's nonnegative */
			BPF_JMP_IMM(BPF_JSGE, BPF_REG_5, 0, 1),
			BPF_EXIT_INSN(),
			/* Add it to packet pointer */
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_2),
			BPF_ALU64_REG(BPF_ADD, BPF_REG_6, BPF_REG_5),
			/* Check bounds and perform a read */
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_6),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, 4),
			BPF_JMP_REG(BPF_JGE, BPF_REG_3, BPF_REG_4, 1),
			BPF_EXIT_INSN(),
			BPF_LDX_MEM(BPF_W, BPF_REG_4, BPF_REG_6, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = REJECT,
		.matches = {
			{4, "R5_w=pkt(id=0,off=0,r=0,imm=0)"},
			/* ptr & 0x40 == either 0 or 0x40 */
			{5, "R5_w=inv(id=0,umax_value=64,var_off=(0x0; 0x40))"},
			/* ptr << 2 == unknown, (4n) */
			{7, "R5_w=inv(id=0,smax_value=9223372036854775804,umax_value=18446744073709551612,var_off=(0x0; 0xfffffffffffffffc))"},
			/* (4n) + 14 == (4n+2).  We blow our bounds, because
			 * the add could overflow.
			 */
			{8, "R5=inv(id=0,var_off=(0x2; 0xfffffffffffffffc))"},
			/* Checked s>=0 */
			{10, "R5=inv(id=0,umin_value=2,umax_value=9223372036854775806,var_off=(0x2; 0x7ffffffffffffffc))"},
			/* packet pointer + nonnegative (4n+2) */
			{12, "R6_w=pkt(id=1,off=0,r=0,umin_value=2,umax_value=9223372036854775806,var_off=(0x2; 0x7ffffffffffffffc))"},
			{14, "R4=pkt(id=1,off=4,r=0,umin_value=2,umax_value=9223372036854775806,var_off=(0x2; 0x7ffffffffffffffc))"},
			/* NET_IP_ALIGN + (4n+2) == (4n), alignment is fine.
			 * We checked the bounds, but it might have been able
			 * to overflow if the packet pointer started in the
			 * upper half of the address space.
			 * So we did not get a 'range' on R6, and the access
			 * attempt will fail.
			 */
			{16, "R6=pkt(id=1,off=0,r=0,umin_value=2,umax_value=9223372036854775806,var_off=(0x2; 0x7ffffffffffffffc))"},
		}
	},
	{
		.descr = "variable subtraction",
		.insns = {
			/* Create an unknown offset, (4n+2)-aligned */
			LOAD_UNKNOWN(BPF_REG_6),
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_6),
			BPF_ALU64_IMM(BPF_LSH, BPF_REG_6, 2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_6, 14),
			/* Create another unknown, (4n)-aligned, and subtract
			 * it from the first one
			 */
			BPF_ALU64_IMM(BPF_LSH, BPF_REG_7, 2),
			BPF_ALU64_REG(BPF_SUB, BPF_REG_6, BPF_REG_7),
			/* Bounds-check the result */
			BPF_JMP_IMM(BPF_JSGE, BPF_REG_6, 0, 1),
			BPF_EXIT_INSN(),
			/* Add it to the packet pointer */
			BPF_MOV64_REG(BPF_REG_5, BPF_REG_2),
			BPF_ALU64_REG(BPF_ADD, BPF_REG_5, BPF_REG_6),
			/* Check bounds and perform a read */
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_5),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, 4),
			BPF_JMP_REG(BPF_JGE, BPF_REG_3, BPF_REG_4, 1),
			BPF_EXIT_INSN(),
			BPF_LDX_MEM(BPF_W, BPF_REG_6, BPF_REG_5, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.matches = {
			/* Calculated offset in R6 has unknown value, but known
			 * alignment of 4.
			 */
			{7, "R2=pkt(id=0,off=0,r=8,imm=0)"},
			{9, "R6_w=inv(id=0,umax_value=1020,var_off=(0x0; 0x3fc))"},
			/* Adding 14 makes R6 be (4n+2) */
			{10, "R6_w=inv(id=0,umin_value=14,umax_value=1034,var_off=(0x2; 0x7fc))"},
			/* New unknown value in R7 is (4n) */
			{11, "R7_w=inv(id=0,umax_value=1020,var_off=(0x0; 0x3fc))"},
			/* Subtracting it from R6 blows our unsigned bounds */
			{12, "R6=inv(id=0,smin_value=-1006,smax_value=1034,var_off=(0x2; 0xfffffffffffffffc))"},
			/* Checked s>= 0 */
			{14, "R6=inv(id=0,umin_value=2,umax_value=1034,var_off=(0x2; 0x7fc))"},
			/* At the time the word size load is performed from R5,
			 * its total fixed offset is NET_IP_ALIGN + reg->off (0)
			 * which is 2.  Then the variable offset is (4n+2), so
			 * the total offset is 4-byte aligned and meets the
			 * load's requirements.
			 */
			{20, "R5=pkt(id=1,off=0,r=4,umin_value=2,umax_value=1034,var_off=(0x2; 0x7fc))"},
		},
	},
	{
		.descr = "pointer variable subtraction",
		.insns = {
			/* Create an unknown offset, (4n+2)-aligned and bounded
			 * to [14,74]
			 */
			LOAD_UNKNOWN(BPF_REG_6),
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_6),
			BPF_ALU64_IMM(BPF_AND, BPF_REG_6, 0xf),
			BPF_ALU64_IMM(BPF_LSH, BPF_REG_6, 2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_6, 14),
			/* Subtract it from the packet pointer */
			BPF_MOV64_REG(BPF_REG_5, BPF_REG_2),
			BPF_ALU64_REG(BPF_SUB, BPF_REG_5, BPF_REG_6),
			/* Create another unknown, (4n)-aligned and >= 74.
			 * That in fact means >= 76, since 74 % 4 == 2
			 */
			BPF_ALU64_IMM(BPF_LSH, BPF_REG_7, 2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, 76),
			/* Add it to the packet pointer */
			BPF_ALU64_REG(BPF_ADD, BPF_REG_5, BPF_REG_7),
			/* Check bounds and perform a read */
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_5),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, 4),
			BPF_JMP_REG(BPF_JGE, BPF_REG_3, BPF_REG_4, 1),
			BPF_EXIT_INSN(),
			BPF_LDX_MEM(BPF_W, BPF_REG_6, BPF_REG_5, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.matches = {
			/* Calculated offset in R6 has unknown value, but known
			 * alignment of 4.
			 */
			{7, "R2=pkt(id=0,off=0,r=8,imm=0)"},
			{10, "R6_w=inv(id=0,umax_value=60,var_off=(0x0; 0x3c))"},
			/* Adding 14 makes R6 be (4n+2) */
			{11, "R6_w=inv(id=0,umin_value=14,umax_value=74,var_off=(0x2; 0x7c))"},
			/* Subtracting from packet pointer overflows ubounds */
			{13, "R5_w=pkt(id=1,off=0,r=8,umin_value=18446744073709551542,umax_value=18446744073709551602,var_off=(0xffffffffffffff82; 0x7c))"},
			/* New unknown value in R7 is (4n), >= 76 */
			{15, "R7_w=inv(id=0,umin_value=76,umax_value=1096,var_off=(0x0; 0x7fc))"},
			/* Adding it to packet pointer gives nice bounds again */
			{16, "R5_w=pkt(id=2,off=0,r=0,umin_value=2,umax_value=1082,var_off=(0x2; 0x7fc))"},
			/* At the time the word size load is performed from R5,
			 * its total fixed offset is NET_IP_ALIGN + reg->off (0)
			 * which is 2.  Then the variable offset is (4n+2), so
			 * the total offset is 4-byte aligned and meets the
			 * load's requirements.
			 */
			{20, "R5=pkt(id=2,off=0,r=4,umin_value=2,umax_value=1082,var_off=(0x2; 0x7fc))"},
		},
	},
};

static int probe_filter_length(const struct bpf_insn *fp)
{
	int len;

	for (len = MAX_INSNS - 1; len > 0; --len)
		if (fp[len].code != 0 || fp[len].imm != 0)
			break;
	return len + 1;
}

static char bpf_vlog[32768];

static int do_test_single(struct bpf_align_test *test)
{
	struct bpf_insn *prog = test->insns;
	int prog_type = test->prog_type;
	char bpf_vlog_copy[32768];
	const char *line_ptr;
	int cur_line = -1;
	int prog_len, i;
	int fd_prog;
	int ret;

	prog_len = probe_filter_length(prog);
	fd_prog = bpf_verify_program(prog_type ? : BPF_PROG_TYPE_SOCKET_FILTER,
				     prog, prog_len, 1, "GPL", 0,
				     bpf_vlog, sizeof(bpf_vlog), 2);
	if (fd_prog < 0 && test->result != REJECT) {
		printf("Failed to load program.\n");
		printf("%s", bpf_vlog);
		ret = 1;
	} else if (fd_prog >= 0 && test->result == REJECT) {
		printf("Unexpected success to load!\n");
		printf("%s", bpf_vlog);
		ret = 1;
		close(fd_prog);
	} else {
		ret = 0;
		/* We make a local copy so that we can strtok() it */
		strncpy(bpf_vlog_copy, bpf_vlog, sizeof(bpf_vlog_copy));
		line_ptr = strtok(bpf_vlog_copy, "\n");
		for (i = 0; i < MAX_MATCHES; i++) {
			struct bpf_reg_match m = test->matches[i];

			if (!m.match)
				break;
			while (line_ptr) {
				cur_line = -1;
				sscanf(line_ptr, "%u: ", &cur_line);
				if (cur_line == m.line)
					break;
				line_ptr = strtok(NULL, "\n");
			}
			if (!line_ptr) {
				printf("Failed to find line %u for match: %s\n",
				       m.line, m.match);
				ret = 1;
				printf("%s", bpf_vlog);
				break;
			}
			if (!strstr(line_ptr, m.match)) {
				printf("Failed to find match %u: %s\n",
				       m.line, m.match);
				ret = 1;
				printf("%s", bpf_vlog);
				break;
			}
		}
		if (fd_prog >= 0)
			close(fd_prog);
	}
	return ret;
}

static int do_test(unsigned int from, unsigned int to)
{
	int all_pass = 0;
	int all_fail = 0;
	unsigned int i;

	for (i = from; i < to; i++) {
		struct bpf_align_test *test = &tests[i];
		int fail;

		printf("Test %3d: %s ... ",
		       i, test->descr);
		fail = do_test_single(test);
		if (fail) {
			all_fail++;
			printf("FAIL\n");
		} else {
			all_pass++;
			printf("PASS\n");
		}
	}
	printf("Results: %d pass %d fail\n",
	       all_pass, all_fail);
	return all_fail ? EXIT_FAILURE : EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
	unsigned int from = 0, to = ARRAY_SIZE(tests);
	struct rlimit rinf = { RLIM_INFINITY, RLIM_INFINITY };

	setrlimit(RLIMIT_MEMLOCK, &rinf);

	if (argc == 3) {
		unsigned int l = atoi(argv[argc - 2]);
		unsigned int u = atoi(argv[argc - 1]);

		if (l < to && u < to) {
			from = l;
			to   = u + 1;
		}
	} else if (argc == 2) {
		unsigned int t = atoi(argv[argc - 1]);

		if (t < to) {
			from = t;
			to   = t + 1;
		}
	}
	return do_test(from, to);
}
