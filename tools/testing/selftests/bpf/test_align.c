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

struct bpf_align_test {
	const char *descr;
	struct bpf_insn	insns[MAX_INSNS];
	enum {
		UNDEF,
		ACCEPT,
		REJECT
	} result;
	enum bpf_prog_type prog_type;
	const char *matches[MAX_MATCHES];
};

static struct bpf_align_test tests[] = {
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
			"1: R1=ctx R3=imm2,min_value=2,max_value=2,min_align=2 R10=fp",
			"2: R1=ctx R3=imm4,min_value=4,max_value=4,min_align=4 R10=fp",
			"3: R1=ctx R3=imm8,min_value=8,max_value=8,min_align=8 R10=fp",
			"4: R1=ctx R3=imm16,min_value=16,max_value=16,min_align=16 R10=fp",
			"5: R1=ctx R3=imm32,min_value=32,max_value=32,min_align=32 R10=fp",
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
			"1: R1=ctx R3=imm1,min_value=1,max_value=1,min_align=1 R10=fp",
			"2: R1=ctx R3=imm2,min_value=2,max_value=2,min_align=2 R10=fp",
			"3: R1=ctx R3=imm4,min_value=4,max_value=4,min_align=4 R10=fp",
			"4: R1=ctx R3=imm8,min_value=8,max_value=8,min_align=8 R10=fp",
			"5: R1=ctx R3=imm16,min_value=16,max_value=16,min_align=16 R10=fp",
			"6: R1=ctx R3=imm1,min_value=1,max_value=1,min_align=1 R10=fp",
			"7: R1=ctx R3=imm1,min_value=1,max_value=1,min_align=1 R4=imm32,min_value=32,max_value=32,min_align=32 R10=fp",
			"8: R1=ctx R3=imm1,min_value=1,max_value=1,min_align=1 R4=imm16,min_value=16,max_value=16,min_align=16 R10=fp",
			"9: R1=ctx R3=imm1,min_value=1,max_value=1,min_align=1 R4=imm8,min_value=8,max_value=8,min_align=8 R10=fp",
			"10: R1=ctx R3=imm1,min_value=1,max_value=1,min_align=1 R4=imm4,min_value=4,max_value=4,min_align=4 R10=fp",
			"11: R1=ctx R3=imm1,min_value=1,max_value=1,min_align=1 R4=imm2,min_value=2,max_value=2,min_align=2 R10=fp",
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
			"1: R1=ctx R3=imm4,min_value=4,max_value=4,min_align=4 R10=fp",
			"2: R1=ctx R3=imm8,min_value=8,max_value=8,min_align=4 R10=fp",
			"3: R1=ctx R3=imm10,min_value=10,max_value=10,min_align=2 R10=fp",
			"4: R1=ctx R3=imm10,min_value=10,max_value=10,min_align=2 R4=imm8,min_value=8,max_value=8,min_align=8 R10=fp",
			"5: R1=ctx R3=imm10,min_value=10,max_value=10,min_align=2 R4=imm12,min_value=12,max_value=12,min_align=4 R10=fp",
			"6: R1=ctx R3=imm10,min_value=10,max_value=10,min_align=2 R4=imm14,min_value=14,max_value=14,min_align=2 R10=fp",
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
			"1: R1=ctx R3=imm7,min_value=7,max_value=7,min_align=1 R10=fp",
			"2: R1=ctx R3=imm7,min_value=7,max_value=7,min_align=1 R10=fp",
			"3: R1=ctx R3=imm14,min_value=14,max_value=14,min_align=2 R10=fp",
			"4: R1=ctx R3=imm56,min_value=56,max_value=56,min_align=4 R10=fp",
		},
	},

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
			"7: R0=pkt(id=0,off=8,r=8) R1=ctx R2=pkt(id=0,off=0,r=8) R3=inv56 R10=fp",
			"8: R0=pkt(id=0,off=8,r=8) R1=ctx R2=pkt(id=0,off=0,r=8) R3=inv55,min_align=2 R10=fp",
			"9: R0=pkt(id=0,off=8,r=8) R1=ctx R2=pkt(id=0,off=0,r=8) R3=inv54,min_align=4 R10=fp",
			"10: R0=pkt(id=0,off=8,r=8) R1=ctx R2=pkt(id=0,off=0,r=8) R3=inv53,min_align=8 R10=fp",
			"11: R0=pkt(id=0,off=8,r=8) R1=ctx R2=pkt(id=0,off=0,r=8) R3=inv52,min_align=16 R10=fp",
			"18: R0=pkt(id=0,off=8,r=8) R1=ctx R2=pkt(id=0,off=0,r=8) R3=pkt_end R4=inv56 R10=fp",
			"19: R0=pkt(id=0,off=8,r=8) R1=ctx R2=pkt(id=0,off=0,r=8) R3=pkt_end R4=inv51,min_align=32 R10=fp",
			"20: R0=pkt(id=0,off=8,r=8) R1=ctx R2=pkt(id=0,off=0,r=8) R3=pkt_end R4=inv52,min_align=16 R10=fp",
			"21: R0=pkt(id=0,off=8,r=8) R1=ctx R2=pkt(id=0,off=0,r=8) R3=pkt_end R4=inv53,min_align=8 R10=fp",
			"22: R0=pkt(id=0,off=8,r=8) R1=ctx R2=pkt(id=0,off=0,r=8) R3=pkt_end R4=inv54,min_align=4 R10=fp",
			"23: R0=pkt(id=0,off=8,r=8) R1=ctx R2=pkt(id=0,off=0,r=8) R3=pkt_end R4=inv55,min_align=2 R10=fp",
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
			"7: R0=pkt(id=0,off=8,r=8) R1=ctx R2=pkt(id=0,off=0,r=8) R3=inv56 R10=fp",
			"8: R0=pkt(id=0,off=8,r=8) R1=ctx R2=pkt(id=0,off=0,r=8) R3=inv56 R4=inv56 R10=fp",
			"9: R0=pkt(id=0,off=8,r=8) R1=ctx R2=pkt(id=0,off=0,r=8) R3=inv56 R4=inv55,min_align=1 R10=fp",
			"10: R0=pkt(id=0,off=8,r=8) R1=ctx R2=pkt(id=0,off=0,r=8) R3=inv56 R4=inv56 R10=fp",
			"11: R0=pkt(id=0,off=8,r=8) R1=ctx R2=pkt(id=0,off=0,r=8) R3=inv56 R4=inv54,min_align=2 R10=fp",
			"12: R0=pkt(id=0,off=8,r=8) R1=ctx R2=pkt(id=0,off=0,r=8) R3=inv56 R4=inv56 R10=fp",
			"13: R0=pkt(id=0,off=8,r=8) R1=ctx R2=pkt(id=0,off=0,r=8) R3=inv56 R4=inv53,min_align=4 R10=fp",
			"14: R0=pkt(id=0,off=8,r=8) R1=ctx R2=pkt(id=0,off=0,r=8) R3=inv56 R4=inv56 R10=fp",
			"15: R0=pkt(id=0,off=8,r=8) R1=ctx R2=pkt(id=0,off=0,r=8) R3=inv56 R4=inv52,min_align=8 R10=fp",
			"16: R0=pkt(id=0,off=8,r=8) R1=ctx R2=pkt(id=0,off=0,r=8) R3=inv56 R4=inv50,min_align=8 R10=fp"
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
			"4: R0=imm0,min_value=0,max_value=0,min_align=2147483648 R1=ctx R2=pkt(id=0,off=0,r=0) R3=pkt_end R5=pkt(id=0,off=0,r=0) R10=fp",
			"5: R0=imm0,min_value=0,max_value=0,min_align=2147483648 R1=ctx R2=pkt(id=0,off=0,r=0) R3=pkt_end R5=pkt(id=0,off=14,r=0) R10=fp",
			"6: R0=imm0,min_value=0,max_value=0,min_align=2147483648 R1=ctx R2=pkt(id=0,off=0,r=0) R3=pkt_end R4=pkt(id=0,off=14,r=0) R5=pkt(id=0,off=14,r=0) R10=fp",
			"10: R0=imm0,min_value=0,max_value=0,min_align=2147483648 R1=ctx R2=pkt(id=0,off=0,r=18) R3=pkt_end R4=inv56 R5=pkt(id=0,off=14,r=18) R10=fp",
			"14: R0=imm0,min_value=0,max_value=0,min_align=2147483648 R1=ctx R2=pkt(id=0,off=0,r=18) R3=pkt_end R4=inv48 R5=pkt(id=0,off=14,r=18) R10=fp",
			"15: R0=imm0,min_value=0,max_value=0,min_align=2147483648 R1=ctx R2=pkt(id=0,off=0,r=18) R3=pkt_end R4=inv48 R5=pkt(id=0,off=14,r=18) R10=fp",
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
			"8: R0=pkt(id=0,off=8,r=8) R1=ctx R2=pkt(id=0,off=0,r=8) R3=pkt_end R6=inv54,min_align=4 R10=fp",

			/* Offset is added to packet pointer R5, resulting in known
			 * auxiliary alignment and offset.
			 */
			"11: R0=pkt(id=0,off=8,r=8) R1=ctx R2=pkt(id=0,off=0,r=8) R3=pkt_end R5=pkt(id=1,off=0,r=0),aux_off=14,aux_off_align=4 R6=inv54,min_align=4 R10=fp",

			/* At the time the word size load is performed from R5,
			 * it's total offset is NET_IP_ALIGN + reg->off (0) +
			 * reg->aux_off (14) which is 16.  Then the variable
			 * offset is considered using reg->aux_off_align which
			 * is 4 and meets the load's requirements.
			 */
			"15: R0=pkt(id=0,off=8,r=8) R1=ctx R2=pkt(id=0,off=0,r=8) R3=pkt_end R4=pkt(id=1,off=4,r=4),aux_off=14,aux_off_align=4 R5=pkt(id=1,off=0,r=4),aux_off=14,aux_off_align=4 R6=inv54,min_align=4 R10=fp",


			/* Variable offset is added to R5 packet pointer,
			 * resulting in auxiliary alignment of 4.
			 */
			"18: R0=pkt(id=0,off=8,r=8) R1=ctx R2=pkt(id=0,off=0,r=8) R3=pkt_end R4=inv,aux_off=14,aux_off_align=4 R5=pkt(id=2,off=0,r=0),aux_off_align=4 R6=inv54,min_align=4 R10=fp",

			/* Constant offset is added to R5, resulting in
			 * reg->off of 14.
			 */
			"19: R0=pkt(id=0,off=8,r=8) R1=ctx R2=pkt(id=0,off=0,r=8) R3=pkt_end R4=inv,aux_off=14,aux_off_align=4 R5=pkt(id=2,off=14,r=0),aux_off_align=4 R6=inv54,min_align=4 R10=fp",

			/* At the time the word size load is performed from R5,
			 * it's total offset is NET_IP_ALIGN + reg->off (14) which
			 * is 16.  Then the variable offset is considered using
			 * reg->aux_off_align which is 4 and meets the load's
			 * requirements.
			 */
			"23: R0=pkt(id=0,off=8,r=8) R1=ctx R2=pkt(id=0,off=0,r=8) R3=pkt_end R4=pkt(id=2,off=18,r=18),aux_off_align=4 R5=pkt(id=2,off=14,r=18),aux_off_align=4 R6=inv54,min_align=4 R10=fp",

			/* Constant offset is added to R5 packet pointer,
			 * resulting in reg->off value of 14.
			 */
			"26: R0=pkt(id=0,off=8,r=8) R1=ctx R2=pkt(id=0,off=0,r=8) R3=pkt_end R4=inv,aux_off_align=4 R5=pkt(id=0,off=14,r=8) R6=inv54,min_align=4 R10=fp",
			/* Variable offset is added to R5, resulting in an
			 * auxiliary offset of 14, and an auxiliary alignment of 4.
			 */
			"27: R0=pkt(id=0,off=8,r=8) R1=ctx R2=pkt(id=0,off=0,r=8) R3=pkt_end R4=inv,aux_off_align=4 R5=pkt(id=3,off=0,r=0),aux_off=14,aux_off_align=4 R6=inv54,min_align=4 R10=fp",
			/* Constant is added to R5 again, setting reg->off to 4. */
			"28: R0=pkt(id=0,off=8,r=8) R1=ctx R2=pkt(id=0,off=0,r=8) R3=pkt_end R4=inv,aux_off_align=4 R5=pkt(id=3,off=4,r=0),aux_off=14,aux_off_align=4 R6=inv54,min_align=4 R10=fp",
			/* And once more we add a variable, which causes an accumulation
			 * of reg->off into reg->aux_off_align, with resulting value of
			 * 18.  The auxiliary alignment stays at 4.
			 */
			"29: R0=pkt(id=0,off=8,r=8) R1=ctx R2=pkt(id=0,off=0,r=8) R3=pkt_end R4=inv,aux_off_align=4 R5=pkt(id=4,off=0,r=0),aux_off=18,aux_off_align=4 R6=inv54,min_align=4 R10=fp",
			/* At the time the word size load is performed from R5,
			 * it's total offset is NET_IP_ALIGN + reg->off (0) +
			 * reg->aux_off (18) which is 20.  Then the variable offset
			 * is considered using reg->aux_off_align which is 4 and meets
			 * the load's requirements.
			 */
			"33: R0=pkt(id=0,off=8,r=8) R1=ctx R2=pkt(id=0,off=0,r=8) R3=pkt_end R4=pkt(id=4,off=4,r=4),aux_off=18,aux_off_align=4 R5=pkt(id=4,off=0,r=4),aux_off=18,aux_off_align=4 R6=inv54,min_align=4 R10=fp",
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
	int prog_len, i;
	int fd_prog;
	int ret;

	prog_len = probe_filter_length(prog);
	fd_prog = bpf_verify_program(prog_type ? : BPF_PROG_TYPE_SOCKET_FILTER,
				     prog, prog_len, 1, "GPL", 0,
				     bpf_vlog, sizeof(bpf_vlog));
	if (fd_prog < 0) {
		printf("Failed to load program.\n");
		printf("%s", bpf_vlog);
		ret = 1;
	} else {
		ret = 0;
		for (i = 0; i < MAX_MATCHES; i++) {
			const char *t, *m = test->matches[i];

			if (!m)
				break;
			t = strstr(bpf_vlog, m);
			if (!t) {
				printf("Failed to find match: %s\n", m);
				ret = 1;
				printf("%s", bpf_vlog);
				break;
			}
		}
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
	return 0;
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
