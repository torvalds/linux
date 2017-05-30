/* Copyright (c) 2011-2014 PLUMgrid, http://plumgrid.com
 * Copyright (c) 2016 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/bpf.h>
#include <linux/bpf_verifier.h>
#include <linux/filter.h>
#include <net/netlink.h>
#include <linux/file.h>
#include <linux/vmalloc.h>
#include <linux/stringify.h>

/* bpf_check() is a static code analyzer that walks eBPF program
 * instruction by instruction and updates register/stack state.
 * All paths of conditional branches are analyzed until 'bpf_exit' insn.
 *
 * The first pass is depth-first-search to check that the program is a DAG.
 * It rejects the following programs:
 * - larger than BPF_MAXINSNS insns
 * - if loop is present (detected via back-edge)
 * - unreachable insns exist (shouldn't be a forest. program = one function)
 * - out of bounds or malformed jumps
 * The second pass is all possible path descent from the 1st insn.
 * Since it's analyzing all pathes through the program, the length of the
 * analysis is limited to 64k insn, which may be hit even if total number of
 * insn is less then 4K, but there are too many branches that change stack/regs.
 * Number of 'branches to be analyzed' is limited to 1k
 *
 * On entry to each instruction, each register has a type, and the instruction
 * changes the types of the registers depending on instruction semantics.
 * If instruction is BPF_MOV64_REG(BPF_REG_1, BPF_REG_5), then type of R5 is
 * copied to R1.
 *
 * All registers are 64-bit.
 * R0 - return register
 * R1-R5 argument passing registers
 * R6-R9 callee saved registers
 * R10 - frame pointer read-only
 *
 * At the start of BPF program the register R1 contains a pointer to bpf_context
 * and has type PTR_TO_CTX.
 *
 * Verifier tracks arithmetic operations on pointers in case:
 *    BPF_MOV64_REG(BPF_REG_1, BPF_REG_10),
 *    BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, -20),
 * 1st insn copies R10 (which has FRAME_PTR) type into R1
 * and 2nd arithmetic instruction is pattern matched to recognize
 * that it wants to construct a pointer to some element within stack.
 * So after 2nd insn, the register R1 has type PTR_TO_STACK
 * (and -20 constant is saved for further stack bounds checking).
 * Meaning that this reg is a pointer to stack plus known immediate constant.
 *
 * Most of the time the registers have UNKNOWN_VALUE type, which
 * means the register has some value, but it's not a valid pointer.
 * (like pointer plus pointer becomes UNKNOWN_VALUE type)
 *
 * When verifier sees load or store instructions the type of base register
 * can be: PTR_TO_MAP_VALUE, PTR_TO_CTX, FRAME_PTR. These are three pointer
 * types recognized by check_mem_access() function.
 *
 * PTR_TO_MAP_VALUE means that this register is pointing to 'map element value'
 * and the range of [ptr, ptr + map's value_size) is accessible.
 *
 * registers used to pass values to function calls are checked against
 * function argument constraints.
 *
 * ARG_PTR_TO_MAP_KEY is one of such argument constraints.
 * It means that the register type passed to this function must be
 * PTR_TO_STACK and it will be used inside the function as
 * 'pointer to map element key'
 *
 * For example the argument constraints for bpf_map_lookup_elem():
 *   .ret_type = RET_PTR_TO_MAP_VALUE_OR_NULL,
 *   .arg1_type = ARG_CONST_MAP_PTR,
 *   .arg2_type = ARG_PTR_TO_MAP_KEY,
 *
 * ret_type says that this function returns 'pointer to map elem value or null'
 * function expects 1st argument to be a const pointer to 'struct bpf_map' and
 * 2nd argument should be a pointer to stack, which will be used inside
 * the helper function as a pointer to map element key.
 *
 * On the kernel side the helper function looks like:
 * u64 bpf_map_lookup_elem(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5)
 * {
 *    struct bpf_map *map = (struct bpf_map *) (unsigned long) r1;
 *    void *key = (void *) (unsigned long) r2;
 *    void *value;
 *
 *    here kernel can access 'key' and 'map' pointers safely, knowing that
 *    [key, key + map->key_size) bytes are valid and were initialized on
 *    the stack of eBPF program.
 * }
 *
 * Corresponding eBPF program may look like:
 *    BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),  // after this insn R2 type is FRAME_PTR
 *    BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -4), // after this insn R2 type is PTR_TO_STACK
 *    BPF_LD_MAP_FD(BPF_REG_1, map_fd),      // after this insn R1 type is CONST_PTR_TO_MAP
 *    BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
 * here verifier looks at prototype of map_lookup_elem() and sees:
 * .arg1_type == ARG_CONST_MAP_PTR and R1->type == CONST_PTR_TO_MAP, which is ok,
 * Now verifier knows that this map has key of R1->map_ptr->key_size bytes
 *
 * Then .arg2_type == ARG_PTR_TO_MAP_KEY and R2->type == PTR_TO_STACK, ok so far,
 * Now verifier checks that [R2, R2 + map's key_size) are within stack limits
 * and were initialized prior to this call.
 * If it's ok, then verifier allows this BPF_CALL insn and looks at
 * .ret_type which is RET_PTR_TO_MAP_VALUE_OR_NULL, so it sets
 * R0->type = PTR_TO_MAP_VALUE_OR_NULL which means bpf_map_lookup_elem() function
 * returns ether pointer to map value or NULL.
 *
 * When type PTR_TO_MAP_VALUE_OR_NULL passes through 'if (reg != 0) goto +off'
 * insn, the register holding that pointer in the true branch changes state to
 * PTR_TO_MAP_VALUE and the same register changes state to CONST_IMM in the false
 * branch. See check_cond_jmp_op().
 *
 * After the call R0 is set to return type of the function and registers R1-R5
 * are set to NOT_INIT to indicate that they are no longer readable.
 */

/* verifier_state + insn_idx are pushed to stack when branch is encountered */
struct bpf_verifier_stack_elem {
	/* verifer state is 'st'
	 * before processing instruction 'insn_idx'
	 * and after processing instruction 'prev_insn_idx'
	 */
	struct bpf_verifier_state st;
	int insn_idx;
	int prev_insn_idx;
	struct bpf_verifier_stack_elem *next;
};

#define BPF_COMPLEXITY_LIMIT_INSNS	98304
#define BPF_COMPLEXITY_LIMIT_STACK	1024

#define BPF_MAP_PTR_POISON ((void *)0xeB9F + POISON_POINTER_DELTA)

struct bpf_call_arg_meta {
	struct bpf_map *map_ptr;
	bool raw_mode;
	bool pkt_access;
	int regno;
	int access_size;
};

/* verbose verifier prints what it's seeing
 * bpf_check() is called under lock, so no race to access these global vars
 */
static u32 log_level, log_size, log_len;
static char *log_buf;

static DEFINE_MUTEX(bpf_verifier_lock);

/* log_level controls verbosity level of eBPF verifier.
 * verbose() is used to dump the verification trace to the log, so the user
 * can figure out what's wrong with the program
 */
static __printf(1, 2) void verbose(const char *fmt, ...)
{
	va_list args;

	if (log_level == 0 || log_len >= log_size - 1)
		return;

	va_start(args, fmt);
	log_len += vscnprintf(log_buf + log_len, log_size - log_len, fmt, args);
	va_end(args);
}

/* string representation of 'enum bpf_reg_type' */
static const char * const reg_type_str[] = {
	[NOT_INIT]		= "?",
	[UNKNOWN_VALUE]		= "inv",
	[PTR_TO_CTX]		= "ctx",
	[CONST_PTR_TO_MAP]	= "map_ptr",
	[PTR_TO_MAP_VALUE]	= "map_value",
	[PTR_TO_MAP_VALUE_OR_NULL] = "map_value_or_null",
	[PTR_TO_MAP_VALUE_ADJ]	= "map_value_adj",
	[FRAME_PTR]		= "fp",
	[PTR_TO_STACK]		= "fp",
	[CONST_IMM]		= "imm",
	[PTR_TO_PACKET]		= "pkt",
	[PTR_TO_PACKET_END]	= "pkt_end",
};

#define __BPF_FUNC_STR_FN(x) [BPF_FUNC_ ## x] = __stringify(bpf_ ## x)
static const char * const func_id_str[] = {
	__BPF_FUNC_MAPPER(__BPF_FUNC_STR_FN)
};
#undef __BPF_FUNC_STR_FN

static const char *func_id_name(int id)
{
	BUILD_BUG_ON(ARRAY_SIZE(func_id_str) != __BPF_FUNC_MAX_ID);

	if (id >= 0 && id < __BPF_FUNC_MAX_ID && func_id_str[id])
		return func_id_str[id];
	else
		return "unknown";
}

static void print_verifier_state(struct bpf_verifier_state *state)
{
	struct bpf_reg_state *reg;
	enum bpf_reg_type t;
	int i;

	for (i = 0; i < MAX_BPF_REG; i++) {
		reg = &state->regs[i];
		t = reg->type;
		if (t == NOT_INIT)
			continue;
		verbose(" R%d=%s", i, reg_type_str[t]);
		if (t == CONST_IMM || t == PTR_TO_STACK)
			verbose("%lld", reg->imm);
		else if (t == PTR_TO_PACKET)
			verbose("(id=%d,off=%d,r=%d)",
				reg->id, reg->off, reg->range);
		else if (t == UNKNOWN_VALUE && reg->imm)
			verbose("%lld", reg->imm);
		else if (t == CONST_PTR_TO_MAP || t == PTR_TO_MAP_VALUE ||
			 t == PTR_TO_MAP_VALUE_OR_NULL ||
			 t == PTR_TO_MAP_VALUE_ADJ)
			verbose("(ks=%d,vs=%d,id=%u)",
				reg->map_ptr->key_size,
				reg->map_ptr->value_size,
				reg->id);
		if (reg->min_value != BPF_REGISTER_MIN_RANGE)
			verbose(",min_value=%lld",
				(long long)reg->min_value);
		if (reg->max_value != BPF_REGISTER_MAX_RANGE)
			verbose(",max_value=%llu",
				(unsigned long long)reg->max_value);
		if (reg->min_align)
			verbose(",min_align=%u", reg->min_align);
		if (reg->aux_off)
			verbose(",aux_off=%u", reg->aux_off);
		if (reg->aux_off_align)
			verbose(",aux_off_align=%u", reg->aux_off_align);
	}
	for (i = 0; i < MAX_BPF_STACK; i += BPF_REG_SIZE) {
		if (state->stack_slot_type[i] == STACK_SPILL)
			verbose(" fp%d=%s", -MAX_BPF_STACK + i,
				reg_type_str[state->spilled_regs[i / BPF_REG_SIZE].type]);
	}
	verbose("\n");
}

static const char *const bpf_class_string[] = {
	[BPF_LD]    = "ld",
	[BPF_LDX]   = "ldx",
	[BPF_ST]    = "st",
	[BPF_STX]   = "stx",
	[BPF_ALU]   = "alu",
	[BPF_JMP]   = "jmp",
	[BPF_RET]   = "BUG",
	[BPF_ALU64] = "alu64",
};

static const char *const bpf_alu_string[16] = {
	[BPF_ADD >> 4]  = "+=",
	[BPF_SUB >> 4]  = "-=",
	[BPF_MUL >> 4]  = "*=",
	[BPF_DIV >> 4]  = "/=",
	[BPF_OR  >> 4]  = "|=",
	[BPF_AND >> 4]  = "&=",
	[BPF_LSH >> 4]  = "<<=",
	[BPF_RSH >> 4]  = ">>=",
	[BPF_NEG >> 4]  = "neg",
	[BPF_MOD >> 4]  = "%=",
	[BPF_XOR >> 4]  = "^=",
	[BPF_MOV >> 4]  = "=",
	[BPF_ARSH >> 4] = "s>>=",
	[BPF_END >> 4]  = "endian",
};

static const char *const bpf_ldst_string[] = {
	[BPF_W >> 3]  = "u32",
	[BPF_H >> 3]  = "u16",
	[BPF_B >> 3]  = "u8",
	[BPF_DW >> 3] = "u64",
};

static const char *const bpf_jmp_string[16] = {
	[BPF_JA >> 4]   = "jmp",
	[BPF_JEQ >> 4]  = "==",
	[BPF_JGT >> 4]  = ">",
	[BPF_JGE >> 4]  = ">=",
	[BPF_JSET >> 4] = "&",
	[BPF_JNE >> 4]  = "!=",
	[BPF_JSGT >> 4] = "s>",
	[BPF_JSGE >> 4] = "s>=",
	[BPF_CALL >> 4] = "call",
	[BPF_EXIT >> 4] = "exit",
};

static void print_bpf_insn(const struct bpf_verifier_env *env,
			   const struct bpf_insn *insn)
{
	u8 class = BPF_CLASS(insn->code);

	if (class == BPF_ALU || class == BPF_ALU64) {
		if (BPF_SRC(insn->code) == BPF_X)
			verbose("(%02x) %sr%d %s %sr%d\n",
				insn->code, class == BPF_ALU ? "(u32) " : "",
				insn->dst_reg,
				bpf_alu_string[BPF_OP(insn->code) >> 4],
				class == BPF_ALU ? "(u32) " : "",
				insn->src_reg);
		else
			verbose("(%02x) %sr%d %s %s%d\n",
				insn->code, class == BPF_ALU ? "(u32) " : "",
				insn->dst_reg,
				bpf_alu_string[BPF_OP(insn->code) >> 4],
				class == BPF_ALU ? "(u32) " : "",
				insn->imm);
	} else if (class == BPF_STX) {
		if (BPF_MODE(insn->code) == BPF_MEM)
			verbose("(%02x) *(%s *)(r%d %+d) = r%d\n",
				insn->code,
				bpf_ldst_string[BPF_SIZE(insn->code) >> 3],
				insn->dst_reg,
				insn->off, insn->src_reg);
		else if (BPF_MODE(insn->code) == BPF_XADD)
			verbose("(%02x) lock *(%s *)(r%d %+d) += r%d\n",
				insn->code,
				bpf_ldst_string[BPF_SIZE(insn->code) >> 3],
				insn->dst_reg, insn->off,
				insn->src_reg);
		else
			verbose("BUG_%02x\n", insn->code);
	} else if (class == BPF_ST) {
		if (BPF_MODE(insn->code) != BPF_MEM) {
			verbose("BUG_st_%02x\n", insn->code);
			return;
		}
		verbose("(%02x) *(%s *)(r%d %+d) = %d\n",
			insn->code,
			bpf_ldst_string[BPF_SIZE(insn->code) >> 3],
			insn->dst_reg,
			insn->off, insn->imm);
	} else if (class == BPF_LDX) {
		if (BPF_MODE(insn->code) != BPF_MEM) {
			verbose("BUG_ldx_%02x\n", insn->code);
			return;
		}
		verbose("(%02x) r%d = *(%s *)(r%d %+d)\n",
			insn->code, insn->dst_reg,
			bpf_ldst_string[BPF_SIZE(insn->code) >> 3],
			insn->src_reg, insn->off);
	} else if (class == BPF_LD) {
		if (BPF_MODE(insn->code) == BPF_ABS) {
			verbose("(%02x) r0 = *(%s *)skb[%d]\n",
				insn->code,
				bpf_ldst_string[BPF_SIZE(insn->code) >> 3],
				insn->imm);
		} else if (BPF_MODE(insn->code) == BPF_IND) {
			verbose("(%02x) r0 = *(%s *)skb[r%d + %d]\n",
				insn->code,
				bpf_ldst_string[BPF_SIZE(insn->code) >> 3],
				insn->src_reg, insn->imm);
		} else if (BPF_MODE(insn->code) == BPF_IMM &&
			   BPF_SIZE(insn->code) == BPF_DW) {
			/* At this point, we already made sure that the second
			 * part of the ldimm64 insn is accessible.
			 */
			u64 imm = ((u64)(insn + 1)->imm << 32) | (u32)insn->imm;
			bool map_ptr = insn->src_reg == BPF_PSEUDO_MAP_FD;

			if (map_ptr && !env->allow_ptr_leaks)
				imm = 0;

			verbose("(%02x) r%d = 0x%llx\n", insn->code,
				insn->dst_reg, (unsigned long long)imm);
		} else {
			verbose("BUG_ld_%02x\n", insn->code);
			return;
		}
	} else if (class == BPF_JMP) {
		u8 opcode = BPF_OP(insn->code);

		if (opcode == BPF_CALL) {
			verbose("(%02x) call %s#%d\n", insn->code,
				func_id_name(insn->imm), insn->imm);
		} else if (insn->code == (BPF_JMP | BPF_JA)) {
			verbose("(%02x) goto pc%+d\n",
				insn->code, insn->off);
		} else if (insn->code == (BPF_JMP | BPF_EXIT)) {
			verbose("(%02x) exit\n", insn->code);
		} else if (BPF_SRC(insn->code) == BPF_X) {
			verbose("(%02x) if r%d %s r%d goto pc%+d\n",
				insn->code, insn->dst_reg,
				bpf_jmp_string[BPF_OP(insn->code) >> 4],
				insn->src_reg, insn->off);
		} else {
			verbose("(%02x) if r%d %s 0x%x goto pc%+d\n",
				insn->code, insn->dst_reg,
				bpf_jmp_string[BPF_OP(insn->code) >> 4],
				insn->imm, insn->off);
		}
	} else {
		verbose("(%02x) %s\n", insn->code, bpf_class_string[class]);
	}
}

static int pop_stack(struct bpf_verifier_env *env, int *prev_insn_idx)
{
	struct bpf_verifier_stack_elem *elem;
	int insn_idx;

	if (env->head == NULL)
		return -1;

	memcpy(&env->cur_state, &env->head->st, sizeof(env->cur_state));
	insn_idx = env->head->insn_idx;
	if (prev_insn_idx)
		*prev_insn_idx = env->head->prev_insn_idx;
	elem = env->head->next;
	kfree(env->head);
	env->head = elem;
	env->stack_size--;
	return insn_idx;
}

static struct bpf_verifier_state *push_stack(struct bpf_verifier_env *env,
					     int insn_idx, int prev_insn_idx)
{
	struct bpf_verifier_stack_elem *elem;

	elem = kmalloc(sizeof(struct bpf_verifier_stack_elem), GFP_KERNEL);
	if (!elem)
		goto err;

	memcpy(&elem->st, &env->cur_state, sizeof(env->cur_state));
	elem->insn_idx = insn_idx;
	elem->prev_insn_idx = prev_insn_idx;
	elem->next = env->head;
	env->head = elem;
	env->stack_size++;
	if (env->stack_size > BPF_COMPLEXITY_LIMIT_STACK) {
		verbose("BPF program is too complex\n");
		goto err;
	}
	return &elem->st;
err:
	/* pop all elements and return */
	while (pop_stack(env, NULL) >= 0);
	return NULL;
}

#define CALLER_SAVED_REGS 6
static const int caller_saved[CALLER_SAVED_REGS] = {
	BPF_REG_0, BPF_REG_1, BPF_REG_2, BPF_REG_3, BPF_REG_4, BPF_REG_5
};

static void mark_reg_not_init(struct bpf_reg_state *regs, u32 regno)
{
	BUG_ON(regno >= MAX_BPF_REG);

	memset(&regs[regno], 0, sizeof(regs[regno]));
	regs[regno].type = NOT_INIT;
	regs[regno].min_value = BPF_REGISTER_MIN_RANGE;
	regs[regno].max_value = BPF_REGISTER_MAX_RANGE;
}

static void init_reg_state(struct bpf_reg_state *regs)
{
	int i;

	for (i = 0; i < MAX_BPF_REG; i++)
		mark_reg_not_init(regs, i);

	/* frame pointer */
	regs[BPF_REG_FP].type = FRAME_PTR;

	/* 1st arg to a function */
	regs[BPF_REG_1].type = PTR_TO_CTX;
}

static void __mark_reg_unknown_value(struct bpf_reg_state *regs, u32 regno)
{
	regs[regno].type = UNKNOWN_VALUE;
	regs[regno].id = 0;
	regs[regno].imm = 0;
}

static void mark_reg_unknown_value(struct bpf_reg_state *regs, u32 regno)
{
	BUG_ON(regno >= MAX_BPF_REG);
	__mark_reg_unknown_value(regs, regno);
}

static void reset_reg_range_values(struct bpf_reg_state *regs, u32 regno)
{
	regs[regno].min_value = BPF_REGISTER_MIN_RANGE;
	regs[regno].max_value = BPF_REGISTER_MAX_RANGE;
	regs[regno].min_align = 0;
}

static void mark_reg_unknown_value_and_range(struct bpf_reg_state *regs,
					     u32 regno)
{
	mark_reg_unknown_value(regs, regno);
	reset_reg_range_values(regs, regno);
}

enum reg_arg_type {
	SRC_OP,		/* register is used as source operand */
	DST_OP,		/* register is used as destination operand */
	DST_OP_NO_MARK	/* same as above, check only, don't mark */
};

static int check_reg_arg(struct bpf_reg_state *regs, u32 regno,
			 enum reg_arg_type t)
{
	if (regno >= MAX_BPF_REG) {
		verbose("R%d is invalid\n", regno);
		return -EINVAL;
	}

	if (t == SRC_OP) {
		/* check whether register used as source operand can be read */
		if (regs[regno].type == NOT_INIT) {
			verbose("R%d !read_ok\n", regno);
			return -EACCES;
		}
	} else {
		/* check whether register used as dest operand can be written to */
		if (regno == BPF_REG_FP) {
			verbose("frame pointer is read only\n");
			return -EACCES;
		}
		if (t == DST_OP)
			mark_reg_unknown_value(regs, regno);
	}
	return 0;
}

static int bpf_size_to_bytes(int bpf_size)
{
	if (bpf_size == BPF_W)
		return 4;
	else if (bpf_size == BPF_H)
		return 2;
	else if (bpf_size == BPF_B)
		return 1;
	else if (bpf_size == BPF_DW)
		return 8;
	else
		return -EINVAL;
}

static bool is_spillable_regtype(enum bpf_reg_type type)
{
	switch (type) {
	case PTR_TO_MAP_VALUE:
	case PTR_TO_MAP_VALUE_OR_NULL:
	case PTR_TO_MAP_VALUE_ADJ:
	case PTR_TO_STACK:
	case PTR_TO_CTX:
	case PTR_TO_PACKET:
	case PTR_TO_PACKET_END:
	case FRAME_PTR:
	case CONST_PTR_TO_MAP:
		return true;
	default:
		return false;
	}
}

/* check_stack_read/write functions track spill/fill of registers,
 * stack boundary and alignment are checked in check_mem_access()
 */
static int check_stack_write(struct bpf_verifier_state *state, int off,
			     int size, int value_regno)
{
	int i;
	/* caller checked that off % size == 0 and -MAX_BPF_STACK <= off < 0,
	 * so it's aligned access and [off, off + size) are within stack limits
	 */

	if (value_regno >= 0 &&
	    is_spillable_regtype(state->regs[value_regno].type)) {

		/* register containing pointer is being spilled into stack */
		if (size != BPF_REG_SIZE) {
			verbose("invalid size of register spill\n");
			return -EACCES;
		}

		/* save register state */
		state->spilled_regs[(MAX_BPF_STACK + off) / BPF_REG_SIZE] =
			state->regs[value_regno];

		for (i = 0; i < BPF_REG_SIZE; i++)
			state->stack_slot_type[MAX_BPF_STACK + off + i] = STACK_SPILL;
	} else {
		/* regular write of data into stack */
		state->spilled_regs[(MAX_BPF_STACK + off) / BPF_REG_SIZE] =
			(struct bpf_reg_state) {};

		for (i = 0; i < size; i++)
			state->stack_slot_type[MAX_BPF_STACK + off + i] = STACK_MISC;
	}
	return 0;
}

static int check_stack_read(struct bpf_verifier_state *state, int off, int size,
			    int value_regno)
{
	u8 *slot_type;
	int i;

	slot_type = &state->stack_slot_type[MAX_BPF_STACK + off];

	if (slot_type[0] == STACK_SPILL) {
		if (size != BPF_REG_SIZE) {
			verbose("invalid size of register spill\n");
			return -EACCES;
		}
		for (i = 1; i < BPF_REG_SIZE; i++) {
			if (slot_type[i] != STACK_SPILL) {
				verbose("corrupted spill memory\n");
				return -EACCES;
			}
		}

		if (value_regno >= 0)
			/* restore register state from stack */
			state->regs[value_regno] =
				state->spilled_regs[(MAX_BPF_STACK + off) / BPF_REG_SIZE];
		return 0;
	} else {
		for (i = 0; i < size; i++) {
			if (slot_type[i] != STACK_MISC) {
				verbose("invalid read from stack off %d+%d size %d\n",
					off, i, size);
				return -EACCES;
			}
		}
		if (value_regno >= 0)
			/* have read misc data from the stack */
			mark_reg_unknown_value_and_range(state->regs,
							 value_regno);
		return 0;
	}
}

/* check read/write into map element returned by bpf_map_lookup_elem() */
static int check_map_access(struct bpf_verifier_env *env, u32 regno, int off,
			    int size)
{
	struct bpf_map *map = env->cur_state.regs[regno].map_ptr;

	if (off < 0 || size <= 0 || off + size > map->value_size) {
		verbose("invalid access to map value, value_size=%d off=%d size=%d\n",
			map->value_size, off, size);
		return -EACCES;
	}
	return 0;
}

/* check read/write into an adjusted map element */
static int check_map_access_adj(struct bpf_verifier_env *env, u32 regno,
				int off, int size)
{
	struct bpf_verifier_state *state = &env->cur_state;
	struct bpf_reg_state *reg = &state->regs[regno];
	int err;

	/* We adjusted the register to this map value, so we
	 * need to change off and size to min_value and max_value
	 * respectively to make sure our theoretical access will be
	 * safe.
	 */
	if (log_level)
		print_verifier_state(state);
	env->varlen_map_value_access = true;
	/* The minimum value is only important with signed
	 * comparisons where we can't assume the floor of a
	 * value is 0.  If we are using signed variables for our
	 * index'es we need to make sure that whatever we use
	 * will have a set floor within our range.
	 */
	if (reg->min_value < 0) {
		verbose("R%d min value is negative, either use unsigned index or do a if (index >=0) check.\n",
			regno);
		return -EACCES;
	}
	err = check_map_access(env, regno, reg->min_value + off, size);
	if (err) {
		verbose("R%d min value is outside of the array range\n",
			regno);
		return err;
	}

	/* If we haven't set a max value then we need to bail
	 * since we can't be sure we won't do bad things.
	 */
	if (reg->max_value == BPF_REGISTER_MAX_RANGE) {
		verbose("R%d unbounded memory access, make sure to bounds check any array access into a map\n",
			regno);
		return -EACCES;
	}
	return check_map_access(env, regno, reg->max_value + off, size);
}

#define MAX_PACKET_OFF 0xffff

static bool may_access_direct_pkt_data(struct bpf_verifier_env *env,
				       const struct bpf_call_arg_meta *meta,
				       enum bpf_access_type t)
{
	switch (env->prog->type) {
	case BPF_PROG_TYPE_LWT_IN:
	case BPF_PROG_TYPE_LWT_OUT:
		/* dst_input() and dst_output() can't write for now */
		if (t == BPF_WRITE)
			return false;
		/* fallthrough */
	case BPF_PROG_TYPE_SCHED_CLS:
	case BPF_PROG_TYPE_SCHED_ACT:
	case BPF_PROG_TYPE_XDP:
	case BPF_PROG_TYPE_LWT_XMIT:
		if (meta)
			return meta->pkt_access;

		env->seen_direct_write = true;
		return true;
	default:
		return false;
	}
}

static int check_packet_access(struct bpf_verifier_env *env, u32 regno, int off,
			       int size)
{
	struct bpf_reg_state *regs = env->cur_state.regs;
	struct bpf_reg_state *reg = &regs[regno];

	off += reg->off;
	if (off < 0 || size <= 0 || off + size > reg->range) {
		verbose("invalid access to packet, off=%d size=%d, R%d(id=%d,off=%d,r=%d)\n",
			off, size, regno, reg->id, reg->off, reg->range);
		return -EACCES;
	}
	return 0;
}

/* check access to 'struct bpf_context' fields */
static int check_ctx_access(struct bpf_verifier_env *env, int off, int size,
			    enum bpf_access_type t, enum bpf_reg_type *reg_type)
{
	/* for analyzer ctx accesses are already validated and converted */
	if (env->analyzer_ops)
		return 0;

	if (env->prog->aux->ops->is_valid_access &&
	    env->prog->aux->ops->is_valid_access(off, size, t, reg_type)) {
		/* remember the offset of last byte accessed in ctx */
		if (env->prog->aux->max_ctx_offset < off + size)
			env->prog->aux->max_ctx_offset = off + size;
		return 0;
	}

	verbose("invalid bpf_context access off=%d size=%d\n", off, size);
	return -EACCES;
}

static bool is_pointer_value(struct bpf_verifier_env *env, int regno)
{
	if (env->allow_ptr_leaks)
		return false;

	switch (env->cur_state.regs[regno].type) {
	case UNKNOWN_VALUE:
	case CONST_IMM:
		return false;
	default:
		return true;
	}
}

static int check_pkt_ptr_alignment(const struct bpf_reg_state *reg,
				   int off, int size, bool strict)
{
	int ip_align;
	int reg_off;

	/* Byte size accesses are always allowed. */
	if (!strict || size == 1)
		return 0;

	reg_off = reg->off;
	if (reg->id) {
		if (reg->aux_off_align % size) {
			verbose("Packet access is only %u byte aligned, %d byte access not allowed\n",
				reg->aux_off_align, size);
			return -EACCES;
		}
		reg_off += reg->aux_off;
	}

	/* For platforms that do not have a Kconfig enabling
	 * CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS the value of
	 * NET_IP_ALIGN is universally set to '2'.  And on platforms
	 * that do set CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS, we get
	 * to this code only in strict mode where we want to emulate
	 * the NET_IP_ALIGN==2 checking.  Therefore use an
	 * unconditional IP align value of '2'.
	 */
	ip_align = 2;
	if ((ip_align + reg_off + off) % size != 0) {
		verbose("misaligned packet access off %d+%d+%d size %d\n",
			ip_align, reg_off, off, size);
		return -EACCES;
	}

	return 0;
}

static int check_val_ptr_alignment(const struct bpf_reg_state *reg,
				   int size, bool strict)
{
	if (strict && size != 1) {
		verbose("Unknown alignment. Only byte-sized access allowed in value access.\n");
		return -EACCES;
	}

	return 0;
}

static int check_ptr_alignment(struct bpf_verifier_env *env,
			       const struct bpf_reg_state *reg,
			       int off, int size)
{
	bool strict = env->strict_alignment;

	switch (reg->type) {
	case PTR_TO_PACKET:
		return check_pkt_ptr_alignment(reg, off, size, strict);
	case PTR_TO_MAP_VALUE_ADJ:
		return check_val_ptr_alignment(reg, size, strict);
	default:
		if (off % size != 0) {
			verbose("misaligned access off %d size %d\n",
				off, size);
			return -EACCES;
		}

		return 0;
	}
}

/* check whether memory at (regno + off) is accessible for t = (read | write)
 * if t==write, value_regno is a register which value is stored into memory
 * if t==read, value_regno is a register which will receive the value from memory
 * if t==write && value_regno==-1, some unknown value is stored into memory
 * if t==read && value_regno==-1, don't care what we read from memory
 */
static int check_mem_access(struct bpf_verifier_env *env, u32 regno, int off,
			    int bpf_size, enum bpf_access_type t,
			    int value_regno)
{
	struct bpf_verifier_state *state = &env->cur_state;
	struct bpf_reg_state *reg = &state->regs[regno];
	int size, err = 0;

	if (reg->type == PTR_TO_STACK)
		off += reg->imm;

	size = bpf_size_to_bytes(bpf_size);
	if (size < 0)
		return size;

	err = check_ptr_alignment(env, reg, off, size);
	if (err)
		return err;

	if (reg->type == PTR_TO_MAP_VALUE ||
	    reg->type == PTR_TO_MAP_VALUE_ADJ) {
		if (t == BPF_WRITE && value_regno >= 0 &&
		    is_pointer_value(env, value_regno)) {
			verbose("R%d leaks addr into map\n", value_regno);
			return -EACCES;
		}

		if (reg->type == PTR_TO_MAP_VALUE_ADJ)
			err = check_map_access_adj(env, regno, off, size);
		else
			err = check_map_access(env, regno, off, size);
		if (!err && t == BPF_READ && value_regno >= 0)
			mark_reg_unknown_value_and_range(state->regs,
							 value_regno);

	} else if (reg->type == PTR_TO_CTX) {
		enum bpf_reg_type reg_type = UNKNOWN_VALUE;

		if (t == BPF_WRITE && value_regno >= 0 &&
		    is_pointer_value(env, value_regno)) {
			verbose("R%d leaks addr into ctx\n", value_regno);
			return -EACCES;
		}
		err = check_ctx_access(env, off, size, t, &reg_type);
		if (!err && t == BPF_READ && value_regno >= 0) {
			mark_reg_unknown_value_and_range(state->regs,
							 value_regno);
			/* note that reg.[id|off|range] == 0 */
			state->regs[value_regno].type = reg_type;
			state->regs[value_regno].aux_off = 0;
			state->regs[value_regno].aux_off_align = 0;
		}

	} else if (reg->type == FRAME_PTR || reg->type == PTR_TO_STACK) {
		if (off >= 0 || off < -MAX_BPF_STACK) {
			verbose("invalid stack off=%d size=%d\n", off, size);
			return -EACCES;
		}

		if (env->prog->aux->stack_depth < -off)
			env->prog->aux->stack_depth = -off;

		if (t == BPF_WRITE) {
			if (!env->allow_ptr_leaks &&
			    state->stack_slot_type[MAX_BPF_STACK + off] == STACK_SPILL &&
			    size != BPF_REG_SIZE) {
				verbose("attempt to corrupt spilled pointer on stack\n");
				return -EACCES;
			}
			err = check_stack_write(state, off, size, value_regno);
		} else {
			err = check_stack_read(state, off, size, value_regno);
		}
	} else if (state->regs[regno].type == PTR_TO_PACKET) {
		if (t == BPF_WRITE && !may_access_direct_pkt_data(env, NULL, t)) {
			verbose("cannot write into packet\n");
			return -EACCES;
		}
		if (t == BPF_WRITE && value_regno >= 0 &&
		    is_pointer_value(env, value_regno)) {
			verbose("R%d leaks addr into packet\n", value_regno);
			return -EACCES;
		}
		err = check_packet_access(env, regno, off, size);
		if (!err && t == BPF_READ && value_regno >= 0)
			mark_reg_unknown_value_and_range(state->regs,
							 value_regno);
	} else {
		verbose("R%d invalid mem access '%s'\n",
			regno, reg_type_str[reg->type]);
		return -EACCES;
	}

	if (!err && size <= 2 && value_regno >= 0 && env->allow_ptr_leaks &&
	    state->regs[value_regno].type == UNKNOWN_VALUE) {
		/* 1 or 2 byte load zero-extends, determine the number of
		 * zero upper bits. Not doing it fo 4 byte load, since
		 * such values cannot be added to ptr_to_packet anyway.
		 */
		state->regs[value_regno].imm = 64 - size * 8;
	}
	return err;
}

static int check_xadd(struct bpf_verifier_env *env, struct bpf_insn *insn)
{
	struct bpf_reg_state *regs = env->cur_state.regs;
	int err;

	if ((BPF_SIZE(insn->code) != BPF_W && BPF_SIZE(insn->code) != BPF_DW) ||
	    insn->imm != 0) {
		verbose("BPF_XADD uses reserved fields\n");
		return -EINVAL;
	}

	/* check src1 operand */
	err = check_reg_arg(regs, insn->src_reg, SRC_OP);
	if (err)
		return err;

	/* check src2 operand */
	err = check_reg_arg(regs, insn->dst_reg, SRC_OP);
	if (err)
		return err;

	/* check whether atomic_add can read the memory */
	err = check_mem_access(env, insn->dst_reg, insn->off,
			       BPF_SIZE(insn->code), BPF_READ, -1);
	if (err)
		return err;

	/* check whether atomic_add can write into the same memory */
	return check_mem_access(env, insn->dst_reg, insn->off,
				BPF_SIZE(insn->code), BPF_WRITE, -1);
}

/* when register 'regno' is passed into function that will read 'access_size'
 * bytes from that pointer, make sure that it's within stack boundary
 * and all elements of stack are initialized
 */
static int check_stack_boundary(struct bpf_verifier_env *env, int regno,
				int access_size, bool zero_size_allowed,
				struct bpf_call_arg_meta *meta)
{
	struct bpf_verifier_state *state = &env->cur_state;
	struct bpf_reg_state *regs = state->regs;
	int off, i;

	if (regs[regno].type != PTR_TO_STACK) {
		if (zero_size_allowed && access_size == 0 &&
		    regs[regno].type == CONST_IMM &&
		    regs[regno].imm  == 0)
			return 0;

		verbose("R%d type=%s expected=%s\n", regno,
			reg_type_str[regs[regno].type],
			reg_type_str[PTR_TO_STACK]);
		return -EACCES;
	}

	off = regs[regno].imm;
	if (off >= 0 || off < -MAX_BPF_STACK || off + access_size > 0 ||
	    access_size <= 0) {
		verbose("invalid stack type R%d off=%d access_size=%d\n",
			regno, off, access_size);
		return -EACCES;
	}

	if (env->prog->aux->stack_depth < -off)
		env->prog->aux->stack_depth = -off;

	if (meta && meta->raw_mode) {
		meta->access_size = access_size;
		meta->regno = regno;
		return 0;
	}

	for (i = 0; i < access_size; i++) {
		if (state->stack_slot_type[MAX_BPF_STACK + off + i] != STACK_MISC) {
			verbose("invalid indirect read from stack off %d+%d size %d\n",
				off, i, access_size);
			return -EACCES;
		}
	}
	return 0;
}

static int check_helper_mem_access(struct bpf_verifier_env *env, int regno,
				   int access_size, bool zero_size_allowed,
				   struct bpf_call_arg_meta *meta)
{
	struct bpf_reg_state *regs = env->cur_state.regs;

	switch (regs[regno].type) {
	case PTR_TO_PACKET:
		return check_packet_access(env, regno, 0, access_size);
	case PTR_TO_MAP_VALUE:
		return check_map_access(env, regno, 0, access_size);
	case PTR_TO_MAP_VALUE_ADJ:
		return check_map_access_adj(env, regno, 0, access_size);
	default: /* const_imm|ptr_to_stack or invalid ptr */
		return check_stack_boundary(env, regno, access_size,
					    zero_size_allowed, meta);
	}
}

static int check_func_arg(struct bpf_verifier_env *env, u32 regno,
			  enum bpf_arg_type arg_type,
			  struct bpf_call_arg_meta *meta)
{
	struct bpf_reg_state *regs = env->cur_state.regs, *reg = &regs[regno];
	enum bpf_reg_type expected_type, type = reg->type;
	int err = 0;

	if (arg_type == ARG_DONTCARE)
		return 0;

	if (type == NOT_INIT) {
		verbose("R%d !read_ok\n", regno);
		return -EACCES;
	}

	if (arg_type == ARG_ANYTHING) {
		if (is_pointer_value(env, regno)) {
			verbose("R%d leaks addr into helper function\n", regno);
			return -EACCES;
		}
		return 0;
	}

	if (type == PTR_TO_PACKET &&
	    !may_access_direct_pkt_data(env, meta, BPF_READ)) {
		verbose("helper access to the packet is not allowed\n");
		return -EACCES;
	}

	if (arg_type == ARG_PTR_TO_MAP_KEY ||
	    arg_type == ARG_PTR_TO_MAP_VALUE) {
		expected_type = PTR_TO_STACK;
		if (type != PTR_TO_PACKET && type != expected_type)
			goto err_type;
	} else if (arg_type == ARG_CONST_SIZE ||
		   arg_type == ARG_CONST_SIZE_OR_ZERO) {
		expected_type = CONST_IMM;
		/* One exception. Allow UNKNOWN_VALUE registers when the
		 * boundaries are known and don't cause unsafe memory accesses
		 */
		if (type != UNKNOWN_VALUE && type != expected_type)
			goto err_type;
	} else if (arg_type == ARG_CONST_MAP_PTR) {
		expected_type = CONST_PTR_TO_MAP;
		if (type != expected_type)
			goto err_type;
	} else if (arg_type == ARG_PTR_TO_CTX) {
		expected_type = PTR_TO_CTX;
		if (type != expected_type)
			goto err_type;
	} else if (arg_type == ARG_PTR_TO_MEM ||
		   arg_type == ARG_PTR_TO_UNINIT_MEM) {
		expected_type = PTR_TO_STACK;
		/* One exception here. In case function allows for NULL to be
		 * passed in as argument, it's a CONST_IMM type. Final test
		 * happens during stack boundary checking.
		 */
		if (type == CONST_IMM && reg->imm == 0)
			/* final test in check_stack_boundary() */;
		else if (type != PTR_TO_PACKET && type != PTR_TO_MAP_VALUE &&
			 type != PTR_TO_MAP_VALUE_ADJ && type != expected_type)
			goto err_type;
		meta->raw_mode = arg_type == ARG_PTR_TO_UNINIT_MEM;
	} else {
		verbose("unsupported arg_type %d\n", arg_type);
		return -EFAULT;
	}

	if (arg_type == ARG_CONST_MAP_PTR) {
		/* bpf_map_xxx(map_ptr) call: remember that map_ptr */
		meta->map_ptr = reg->map_ptr;
	} else if (arg_type == ARG_PTR_TO_MAP_KEY) {
		/* bpf_map_xxx(..., map_ptr, ..., key) call:
		 * check that [key, key + map->key_size) are within
		 * stack limits and initialized
		 */
		if (!meta->map_ptr) {
			/* in function declaration map_ptr must come before
			 * map_key, so that it's verified and known before
			 * we have to check map_key here. Otherwise it means
			 * that kernel subsystem misconfigured verifier
			 */
			verbose("invalid map_ptr to access map->key\n");
			return -EACCES;
		}
		if (type == PTR_TO_PACKET)
			err = check_packet_access(env, regno, 0,
						  meta->map_ptr->key_size);
		else
			err = check_stack_boundary(env, regno,
						   meta->map_ptr->key_size,
						   false, NULL);
	} else if (arg_type == ARG_PTR_TO_MAP_VALUE) {
		/* bpf_map_xxx(..., map_ptr, ..., value) call:
		 * check [value, value + map->value_size) validity
		 */
		if (!meta->map_ptr) {
			/* kernel subsystem misconfigured verifier */
			verbose("invalid map_ptr to access map->value\n");
			return -EACCES;
		}
		if (type == PTR_TO_PACKET)
			err = check_packet_access(env, regno, 0,
						  meta->map_ptr->value_size);
		else
			err = check_stack_boundary(env, regno,
						   meta->map_ptr->value_size,
						   false, NULL);
	} else if (arg_type == ARG_CONST_SIZE ||
		   arg_type == ARG_CONST_SIZE_OR_ZERO) {
		bool zero_size_allowed = (arg_type == ARG_CONST_SIZE_OR_ZERO);

		/* bpf_xxx(..., buf, len) call will access 'len' bytes
		 * from stack pointer 'buf'. Check it
		 * note: regno == len, regno - 1 == buf
		 */
		if (regno == 0) {
			/* kernel subsystem misconfigured verifier */
			verbose("ARG_CONST_SIZE cannot be first argument\n");
			return -EACCES;
		}

		/* If the register is UNKNOWN_VALUE, the access check happens
		 * using its boundaries. Otherwise, just use its imm
		 */
		if (type == UNKNOWN_VALUE) {
			/* For unprivileged variable accesses, disable raw
			 * mode so that the program is required to
			 * initialize all the memory that the helper could
			 * just partially fill up.
			 */
			meta = NULL;

			if (reg->min_value < 0) {
				verbose("R%d min value is negative, either use unsigned or 'var &= const'\n",
					regno);
				return -EACCES;
			}

			if (reg->min_value == 0) {
				err = check_helper_mem_access(env, regno - 1, 0,
							      zero_size_allowed,
							      meta);
				if (err)
					return err;
			}

			if (reg->max_value == BPF_REGISTER_MAX_RANGE) {
				verbose("R%d unbounded memory access, use 'var &= const' or 'if (var < const)'\n",
					regno);
				return -EACCES;
			}
			err = check_helper_mem_access(env, regno - 1,
						      reg->max_value,
						      zero_size_allowed, meta);
			if (err)
				return err;
		} else {
			/* register is CONST_IMM */
			err = check_helper_mem_access(env, regno - 1, reg->imm,
						      zero_size_allowed, meta);
		}
	}

	return err;
err_type:
	verbose("R%d type=%s expected=%s\n", regno,
		reg_type_str[type], reg_type_str[expected_type]);
	return -EACCES;
}

static int check_map_func_compatibility(struct bpf_map *map, int func_id)
{
	if (!map)
		return 0;

	/* We need a two way check, first is from map perspective ... */
	switch (map->map_type) {
	case BPF_MAP_TYPE_PROG_ARRAY:
		if (func_id != BPF_FUNC_tail_call)
			goto error;
		break;
	case BPF_MAP_TYPE_PERF_EVENT_ARRAY:
		if (func_id != BPF_FUNC_perf_event_read &&
		    func_id != BPF_FUNC_perf_event_output)
			goto error;
		break;
	case BPF_MAP_TYPE_STACK_TRACE:
		if (func_id != BPF_FUNC_get_stackid)
			goto error;
		break;
	case BPF_MAP_TYPE_CGROUP_ARRAY:
		if (func_id != BPF_FUNC_skb_under_cgroup &&
		    func_id != BPF_FUNC_current_task_under_cgroup)
			goto error;
		break;
	case BPF_MAP_TYPE_ARRAY_OF_MAPS:
	case BPF_MAP_TYPE_HASH_OF_MAPS:
		if (func_id != BPF_FUNC_map_lookup_elem)
			goto error;
	default:
		break;
	}

	/* ... and second from the function itself. */
	switch (func_id) {
	case BPF_FUNC_tail_call:
		if (map->map_type != BPF_MAP_TYPE_PROG_ARRAY)
			goto error;
		break;
	case BPF_FUNC_perf_event_read:
	case BPF_FUNC_perf_event_output:
		if (map->map_type != BPF_MAP_TYPE_PERF_EVENT_ARRAY)
			goto error;
		break;
	case BPF_FUNC_get_stackid:
		if (map->map_type != BPF_MAP_TYPE_STACK_TRACE)
			goto error;
		break;
	case BPF_FUNC_current_task_under_cgroup:
	case BPF_FUNC_skb_under_cgroup:
		if (map->map_type != BPF_MAP_TYPE_CGROUP_ARRAY)
			goto error;
		break;
	default:
		break;
	}

	return 0;
error:
	verbose("cannot pass map_type %d into func %s#%d\n",
		map->map_type, func_id_name(func_id), func_id);
	return -EINVAL;
}

static int check_raw_mode(const struct bpf_func_proto *fn)
{
	int count = 0;

	if (fn->arg1_type == ARG_PTR_TO_UNINIT_MEM)
		count++;
	if (fn->arg2_type == ARG_PTR_TO_UNINIT_MEM)
		count++;
	if (fn->arg3_type == ARG_PTR_TO_UNINIT_MEM)
		count++;
	if (fn->arg4_type == ARG_PTR_TO_UNINIT_MEM)
		count++;
	if (fn->arg5_type == ARG_PTR_TO_UNINIT_MEM)
		count++;

	return count > 1 ? -EINVAL : 0;
}

static void clear_all_pkt_pointers(struct bpf_verifier_env *env)
{
	struct bpf_verifier_state *state = &env->cur_state;
	struct bpf_reg_state *regs = state->regs, *reg;
	int i;

	for (i = 0; i < MAX_BPF_REG; i++)
		if (regs[i].type == PTR_TO_PACKET ||
		    regs[i].type == PTR_TO_PACKET_END)
			mark_reg_unknown_value(regs, i);

	for (i = 0; i < MAX_BPF_STACK; i += BPF_REG_SIZE) {
		if (state->stack_slot_type[i] != STACK_SPILL)
			continue;
		reg = &state->spilled_regs[i / BPF_REG_SIZE];
		if (reg->type != PTR_TO_PACKET &&
		    reg->type != PTR_TO_PACKET_END)
			continue;
		reg->type = UNKNOWN_VALUE;
		reg->imm = 0;
	}
}

static int check_call(struct bpf_verifier_env *env, int func_id, int insn_idx)
{
	struct bpf_verifier_state *state = &env->cur_state;
	const struct bpf_func_proto *fn = NULL;
	struct bpf_reg_state *regs = state->regs;
	struct bpf_call_arg_meta meta;
	bool changes_data;
	int i, err;

	/* find function prototype */
	if (func_id < 0 || func_id >= __BPF_FUNC_MAX_ID) {
		verbose("invalid func %s#%d\n", func_id_name(func_id), func_id);
		return -EINVAL;
	}

	if (env->prog->aux->ops->get_func_proto)
		fn = env->prog->aux->ops->get_func_proto(func_id);

	if (!fn) {
		verbose("unknown func %s#%d\n", func_id_name(func_id), func_id);
		return -EINVAL;
	}

	/* eBPF programs must be GPL compatible to use GPL-ed functions */
	if (!env->prog->gpl_compatible && fn->gpl_only) {
		verbose("cannot call GPL only function from proprietary program\n");
		return -EINVAL;
	}

	changes_data = bpf_helper_changes_pkt_data(fn->func);

	memset(&meta, 0, sizeof(meta));
	meta.pkt_access = fn->pkt_access;

	/* We only support one arg being in raw mode at the moment, which
	 * is sufficient for the helper functions we have right now.
	 */
	err = check_raw_mode(fn);
	if (err) {
		verbose("kernel subsystem misconfigured func %s#%d\n",
			func_id_name(func_id), func_id);
		return err;
	}

	/* check args */
	err = check_func_arg(env, BPF_REG_1, fn->arg1_type, &meta);
	if (err)
		return err;
	err = check_func_arg(env, BPF_REG_2, fn->arg2_type, &meta);
	if (err)
		return err;
	err = check_func_arg(env, BPF_REG_3, fn->arg3_type, &meta);
	if (err)
		return err;
	err = check_func_arg(env, BPF_REG_4, fn->arg4_type, &meta);
	if (err)
		return err;
	err = check_func_arg(env, BPF_REG_5, fn->arg5_type, &meta);
	if (err)
		return err;

	/* Mark slots with STACK_MISC in case of raw mode, stack offset
	 * is inferred from register state.
	 */
	for (i = 0; i < meta.access_size; i++) {
		err = check_mem_access(env, meta.regno, i, BPF_B, BPF_WRITE, -1);
		if (err)
			return err;
	}

	/* reset caller saved regs */
	for (i = 0; i < CALLER_SAVED_REGS; i++)
		mark_reg_not_init(regs, caller_saved[i]);

	/* update return register */
	if (fn->ret_type == RET_INTEGER) {
		regs[BPF_REG_0].type = UNKNOWN_VALUE;
	} else if (fn->ret_type == RET_VOID) {
		regs[BPF_REG_0].type = NOT_INIT;
	} else if (fn->ret_type == RET_PTR_TO_MAP_VALUE_OR_NULL) {
		struct bpf_insn_aux_data *insn_aux;

		regs[BPF_REG_0].type = PTR_TO_MAP_VALUE_OR_NULL;
		regs[BPF_REG_0].max_value = regs[BPF_REG_0].min_value = 0;
		/* remember map_ptr, so that check_map_access()
		 * can check 'value_size' boundary of memory access
		 * to map element returned from bpf_map_lookup_elem()
		 */
		if (meta.map_ptr == NULL) {
			verbose("kernel subsystem misconfigured verifier\n");
			return -EINVAL;
		}
		regs[BPF_REG_0].map_ptr = meta.map_ptr;
		regs[BPF_REG_0].id = ++env->id_gen;
		insn_aux = &env->insn_aux_data[insn_idx];
		if (!insn_aux->map_ptr)
			insn_aux->map_ptr = meta.map_ptr;
		else if (insn_aux->map_ptr != meta.map_ptr)
			insn_aux->map_ptr = BPF_MAP_PTR_POISON;
	} else {
		verbose("unknown return type %d of func %s#%d\n",
			fn->ret_type, func_id_name(func_id), func_id);
		return -EINVAL;
	}

	err = check_map_func_compatibility(meta.map_ptr, func_id);
	if (err)
		return err;

	if (changes_data)
		clear_all_pkt_pointers(env);
	return 0;
}

static int check_packet_ptr_add(struct bpf_verifier_env *env,
				struct bpf_insn *insn)
{
	struct bpf_reg_state *regs = env->cur_state.regs;
	struct bpf_reg_state *dst_reg = &regs[insn->dst_reg];
	struct bpf_reg_state *src_reg = &regs[insn->src_reg];
	struct bpf_reg_state tmp_reg;
	s32 imm;

	if (BPF_SRC(insn->code) == BPF_K) {
		/* pkt_ptr += imm */
		imm = insn->imm;

add_imm:
		if (imm < 0) {
			verbose("addition of negative constant to packet pointer is not allowed\n");
			return -EACCES;
		}
		if (imm >= MAX_PACKET_OFF ||
		    imm + dst_reg->off >= MAX_PACKET_OFF) {
			verbose("constant %d is too large to add to packet pointer\n",
				imm);
			return -EACCES;
		}
		/* a constant was added to pkt_ptr.
		 * Remember it while keeping the same 'id'
		 */
		dst_reg->off += imm;
	} else {
		bool had_id;

		if (src_reg->type == PTR_TO_PACKET) {
			/* R6=pkt(id=0,off=0,r=62) R7=imm22; r7 += r6 */
			tmp_reg = *dst_reg;  /* save r7 state */
			*dst_reg = *src_reg; /* copy pkt_ptr state r6 into r7 */
			src_reg = &tmp_reg;  /* pretend it's src_reg state */
			/* if the checks below reject it, the copy won't matter,
			 * since we're rejecting the whole program. If all ok,
			 * then imm22 state will be added to r7
			 * and r7 will be pkt(id=0,off=22,r=62) while
			 * r6 will stay as pkt(id=0,off=0,r=62)
			 */
		}

		if (src_reg->type == CONST_IMM) {
			/* pkt_ptr += reg where reg is known constant */
			imm = src_reg->imm;
			goto add_imm;
		}
		/* disallow pkt_ptr += reg
		 * if reg is not uknown_value with guaranteed zero upper bits
		 * otherwise pkt_ptr may overflow and addition will become
		 * subtraction which is not allowed
		 */
		if (src_reg->type != UNKNOWN_VALUE) {
			verbose("cannot add '%s' to ptr_to_packet\n",
				reg_type_str[src_reg->type]);
			return -EACCES;
		}
		if (src_reg->imm < 48) {
			verbose("cannot add integer value with %lld upper zero bits to ptr_to_packet\n",
				src_reg->imm);
			return -EACCES;
		}

		had_id = (dst_reg->id != 0);

		/* dst_reg stays as pkt_ptr type and since some positive
		 * integer value was added to the pointer, increment its 'id'
		 */
		dst_reg->id = ++env->id_gen;

		/* something was added to pkt_ptr, set range to zero */
		dst_reg->aux_off += dst_reg->off;
		dst_reg->off = 0;
		dst_reg->range = 0;
		if (had_id)
			dst_reg->aux_off_align = min(dst_reg->aux_off_align,
						     src_reg->min_align);
		else
			dst_reg->aux_off_align = src_reg->min_align;
	}
	return 0;
}

static int evaluate_reg_alu(struct bpf_verifier_env *env, struct bpf_insn *insn)
{
	struct bpf_reg_state *regs = env->cur_state.regs;
	struct bpf_reg_state *dst_reg = &regs[insn->dst_reg];
	u8 opcode = BPF_OP(insn->code);
	s64 imm_log2;

	/* for type == UNKNOWN_VALUE:
	 * imm > 0 -> number of zero upper bits
	 * imm == 0 -> don't track which is the same as all bits can be non-zero
	 */

	if (BPF_SRC(insn->code) == BPF_X) {
		struct bpf_reg_state *src_reg = &regs[insn->src_reg];

		if (src_reg->type == UNKNOWN_VALUE && src_reg->imm > 0 &&
		    dst_reg->imm && opcode == BPF_ADD) {
			/* dreg += sreg
			 * where both have zero upper bits. Adding them
			 * can only result making one more bit non-zero
			 * in the larger value.
			 * Ex. 0xffff (imm=48) + 1 (imm=63) = 0x10000 (imm=47)
			 *     0xffff (imm=48) + 0xffff = 0x1fffe (imm=47)
			 */
			dst_reg->imm = min(dst_reg->imm, src_reg->imm);
			dst_reg->imm--;
			return 0;
		}
		if (src_reg->type == CONST_IMM && src_reg->imm > 0 &&
		    dst_reg->imm && opcode == BPF_ADD) {
			/* dreg += sreg
			 * where dreg has zero upper bits and sreg is const.
			 * Adding them can only result making one more bit
			 * non-zero in the larger value.
			 */
			imm_log2 = __ilog2_u64((long long)src_reg->imm);
			dst_reg->imm = min(dst_reg->imm, 63 - imm_log2);
			dst_reg->imm--;
			return 0;
		}
		/* all other cases non supported yet, just mark dst_reg */
		dst_reg->imm = 0;
		return 0;
	}

	/* sign extend 32-bit imm into 64-bit to make sure that
	 * negative values occupy bit 63. Note ilog2() would have
	 * been incorrect, since sizeof(insn->imm) == 4
	 */
	imm_log2 = __ilog2_u64((long long)insn->imm);

	if (dst_reg->imm && opcode == BPF_LSH) {
		/* reg <<= imm
		 * if reg was a result of 2 byte load, then its imm == 48
		 * which means that upper 48 bits are zero and shifting this reg
		 * left by 4 would mean that upper 44 bits are still zero
		 */
		dst_reg->imm -= insn->imm;
	} else if (dst_reg->imm && opcode == BPF_MUL) {
		/* reg *= imm
		 * if multiplying by 14 subtract 4
		 * This is conservative calculation of upper zero bits.
		 * It's not trying to special case insn->imm == 1 or 0 cases
		 */
		dst_reg->imm -= imm_log2 + 1;
	} else if (opcode == BPF_AND) {
		/* reg &= imm */
		dst_reg->imm = 63 - imm_log2;
	} else if (dst_reg->imm && opcode == BPF_ADD) {
		/* reg += imm */
		dst_reg->imm = min(dst_reg->imm, 63 - imm_log2);
		dst_reg->imm--;
	} else if (opcode == BPF_RSH) {
		/* reg >>= imm
		 * which means that after right shift, upper bits will be zero
		 * note that verifier already checked that
		 * 0 <= imm < 64 for shift insn
		 */
		dst_reg->imm += insn->imm;
		if (unlikely(dst_reg->imm > 64))
			/* some dumb code did:
			 * r2 = *(u32 *)mem;
			 * r2 >>= 32;
			 * and all bits are zero now */
			dst_reg->imm = 64;
	} else {
		/* all other alu ops, means that we don't know what will
		 * happen to the value, mark it with unknown number of zero bits
		 */
		dst_reg->imm = 0;
	}

	if (dst_reg->imm < 0) {
		/* all 64 bits of the register can contain non-zero bits
		 * and such value cannot be added to ptr_to_packet, since it
		 * may overflow, mark it as unknown to avoid further eval
		 */
		dst_reg->imm = 0;
	}
	return 0;
}

static int evaluate_reg_imm_alu(struct bpf_verifier_env *env,
				struct bpf_insn *insn)
{
	struct bpf_reg_state *regs = env->cur_state.regs;
	struct bpf_reg_state *dst_reg = &regs[insn->dst_reg];
	struct bpf_reg_state *src_reg = &regs[insn->src_reg];
	u8 opcode = BPF_OP(insn->code);
	u64 dst_imm = dst_reg->imm;

	/* dst_reg->type == CONST_IMM here. Simulate execution of insns
	 * containing ALU ops. Don't care about overflow or negative
	 * values, just add/sub/... them; registers are in u64.
	 */
	if (opcode == BPF_ADD && BPF_SRC(insn->code) == BPF_K) {
		dst_imm += insn->imm;
	} else if (opcode == BPF_ADD && BPF_SRC(insn->code) == BPF_X &&
		   src_reg->type == CONST_IMM) {
		dst_imm += src_reg->imm;
	} else if (opcode == BPF_SUB && BPF_SRC(insn->code) == BPF_K) {
		dst_imm -= insn->imm;
	} else if (opcode == BPF_SUB && BPF_SRC(insn->code) == BPF_X &&
		   src_reg->type == CONST_IMM) {
		dst_imm -= src_reg->imm;
	} else if (opcode == BPF_MUL && BPF_SRC(insn->code) == BPF_K) {
		dst_imm *= insn->imm;
	} else if (opcode == BPF_MUL && BPF_SRC(insn->code) == BPF_X &&
		   src_reg->type == CONST_IMM) {
		dst_imm *= src_reg->imm;
	} else if (opcode == BPF_OR && BPF_SRC(insn->code) == BPF_K) {
		dst_imm |= insn->imm;
	} else if (opcode == BPF_OR && BPF_SRC(insn->code) == BPF_X &&
		   src_reg->type == CONST_IMM) {
		dst_imm |= src_reg->imm;
	} else if (opcode == BPF_AND && BPF_SRC(insn->code) == BPF_K) {
		dst_imm &= insn->imm;
	} else if (opcode == BPF_AND && BPF_SRC(insn->code) == BPF_X &&
		   src_reg->type == CONST_IMM) {
		dst_imm &= src_reg->imm;
	} else if (opcode == BPF_RSH && BPF_SRC(insn->code) == BPF_K) {
		dst_imm >>= insn->imm;
	} else if (opcode == BPF_RSH && BPF_SRC(insn->code) == BPF_X &&
		   src_reg->type == CONST_IMM) {
		dst_imm >>= src_reg->imm;
	} else if (opcode == BPF_LSH && BPF_SRC(insn->code) == BPF_K) {
		dst_imm <<= insn->imm;
	} else if (opcode == BPF_LSH && BPF_SRC(insn->code) == BPF_X &&
		   src_reg->type == CONST_IMM) {
		dst_imm <<= src_reg->imm;
	} else {
		mark_reg_unknown_value(regs, insn->dst_reg);
		goto out;
	}

	dst_reg->imm = dst_imm;
out:
	return 0;
}

static void check_reg_overflow(struct bpf_reg_state *reg)
{
	if (reg->max_value > BPF_REGISTER_MAX_RANGE)
		reg->max_value = BPF_REGISTER_MAX_RANGE;
	if (reg->min_value < BPF_REGISTER_MIN_RANGE ||
	    reg->min_value > BPF_REGISTER_MAX_RANGE)
		reg->min_value = BPF_REGISTER_MIN_RANGE;
}

static u32 calc_align(u32 imm)
{
	if (!imm)
		return 1U << 31;
	return imm - ((imm - 1) & imm);
}

static void adjust_reg_min_max_vals(struct bpf_verifier_env *env,
				    struct bpf_insn *insn)
{
	struct bpf_reg_state *regs = env->cur_state.regs, *dst_reg;
	s64 min_val = BPF_REGISTER_MIN_RANGE;
	u64 max_val = BPF_REGISTER_MAX_RANGE;
	u8 opcode = BPF_OP(insn->code);
	u32 dst_align, src_align;

	dst_reg = &regs[insn->dst_reg];
	src_align = 0;
	if (BPF_SRC(insn->code) == BPF_X) {
		check_reg_overflow(&regs[insn->src_reg]);
		min_val = regs[insn->src_reg].min_value;
		max_val = regs[insn->src_reg].max_value;

		/* If the source register is a random pointer then the
		 * min_value/max_value values represent the range of the known
		 * accesses into that value, not the actual min/max value of the
		 * register itself.  In this case we have to reset the reg range
		 * values so we know it is not safe to look at.
		 */
		if (regs[insn->src_reg].type != CONST_IMM &&
		    regs[insn->src_reg].type != UNKNOWN_VALUE) {
			min_val = BPF_REGISTER_MIN_RANGE;
			max_val = BPF_REGISTER_MAX_RANGE;
			src_align = 0;
		} else {
			src_align = regs[insn->src_reg].min_align;
		}
	} else if (insn->imm < BPF_REGISTER_MAX_RANGE &&
		   (s64)insn->imm > BPF_REGISTER_MIN_RANGE) {
		min_val = max_val = insn->imm;
		src_align = calc_align(insn->imm);
	}

	dst_align = dst_reg->min_align;

	/* We don't know anything about what was done to this register, mark it
	 * as unknown.
	 */
	if (min_val == BPF_REGISTER_MIN_RANGE &&
	    max_val == BPF_REGISTER_MAX_RANGE) {
		reset_reg_range_values(regs, insn->dst_reg);
		return;
	}

	/* If one of our values was at the end of our ranges then we can't just
	 * do our normal operations to the register, we need to set the values
	 * to the min/max since they are undefined.
	 */
	if (min_val == BPF_REGISTER_MIN_RANGE)
		dst_reg->min_value = BPF_REGISTER_MIN_RANGE;
	if (max_val == BPF_REGISTER_MAX_RANGE)
		dst_reg->max_value = BPF_REGISTER_MAX_RANGE;

	switch (opcode) {
	case BPF_ADD:
		if (dst_reg->min_value != BPF_REGISTER_MIN_RANGE)
			dst_reg->min_value += min_val;
		if (dst_reg->max_value != BPF_REGISTER_MAX_RANGE)
			dst_reg->max_value += max_val;
		dst_reg->min_align = min(src_align, dst_align);
		break;
	case BPF_SUB:
		if (dst_reg->min_value != BPF_REGISTER_MIN_RANGE)
			dst_reg->min_value -= min_val;
		if (dst_reg->max_value != BPF_REGISTER_MAX_RANGE)
			dst_reg->max_value -= max_val;
		dst_reg->min_align = min(src_align, dst_align);
		break;
	case BPF_MUL:
		if (dst_reg->min_value != BPF_REGISTER_MIN_RANGE)
			dst_reg->min_value *= min_val;
		if (dst_reg->max_value != BPF_REGISTER_MAX_RANGE)
			dst_reg->max_value *= max_val;
		dst_reg->min_align = max(src_align, dst_align);
		break;
	case BPF_AND:
		/* Disallow AND'ing of negative numbers, ain't nobody got time
		 * for that.  Otherwise the minimum is 0 and the max is the max
		 * value we could AND against.
		 */
		if (min_val < 0)
			dst_reg->min_value = BPF_REGISTER_MIN_RANGE;
		else
			dst_reg->min_value = 0;
		dst_reg->max_value = max_val;
		dst_reg->min_align = max(src_align, dst_align);
		break;
	case BPF_LSH:
		/* Gotta have special overflow logic here, if we're shifting
		 * more than MAX_RANGE then just assume we have an invalid
		 * range.
		 */
		if (min_val > ilog2(BPF_REGISTER_MAX_RANGE)) {
			dst_reg->min_value = BPF_REGISTER_MIN_RANGE;
			dst_reg->min_align = 1;
		} else {
			if (dst_reg->min_value != BPF_REGISTER_MIN_RANGE)
				dst_reg->min_value <<= min_val;
			if (!dst_reg->min_align)
				dst_reg->min_align = 1;
			dst_reg->min_align <<= min_val;
		}
		if (max_val > ilog2(BPF_REGISTER_MAX_RANGE))
			dst_reg->max_value = BPF_REGISTER_MAX_RANGE;
		else if (dst_reg->max_value != BPF_REGISTER_MAX_RANGE)
			dst_reg->max_value <<= max_val;
		break;
	case BPF_RSH:
		/* RSH by a negative number is undefined, and the BPF_RSH is an
		 * unsigned shift, so make the appropriate casts.
		 */
		if (min_val < 0 || dst_reg->min_value < 0) {
			dst_reg->min_value = BPF_REGISTER_MIN_RANGE;
		} else {
			dst_reg->min_value =
				(u64)(dst_reg->min_value) >> min_val;
		}
		if (min_val < 0) {
			dst_reg->min_align = 1;
		} else {
			dst_reg->min_align >>= (u64) min_val;
			if (!dst_reg->min_align)
				dst_reg->min_align = 1;
		}
		if (dst_reg->max_value != BPF_REGISTER_MAX_RANGE)
			dst_reg->max_value >>= max_val;
		break;
	default:
		reset_reg_range_values(regs, insn->dst_reg);
		break;
	}

	check_reg_overflow(dst_reg);
}

/* check validity of 32-bit and 64-bit arithmetic operations */
static int check_alu_op(struct bpf_verifier_env *env, struct bpf_insn *insn)
{
	struct bpf_reg_state *regs = env->cur_state.regs, *dst_reg;
	u8 opcode = BPF_OP(insn->code);
	int err;

	if (opcode == BPF_END || opcode == BPF_NEG) {
		if (opcode == BPF_NEG) {
			if (BPF_SRC(insn->code) != 0 ||
			    insn->src_reg != BPF_REG_0 ||
			    insn->off != 0 || insn->imm != 0) {
				verbose("BPF_NEG uses reserved fields\n");
				return -EINVAL;
			}
		} else {
			if (insn->src_reg != BPF_REG_0 || insn->off != 0 ||
			    (insn->imm != 16 && insn->imm != 32 && insn->imm != 64)) {
				verbose("BPF_END uses reserved fields\n");
				return -EINVAL;
			}
		}

		/* check src operand */
		err = check_reg_arg(regs, insn->dst_reg, SRC_OP);
		if (err)
			return err;

		if (is_pointer_value(env, insn->dst_reg)) {
			verbose("R%d pointer arithmetic prohibited\n",
				insn->dst_reg);
			return -EACCES;
		}

		/* check dest operand */
		err = check_reg_arg(regs, insn->dst_reg, DST_OP);
		if (err)
			return err;

	} else if (opcode == BPF_MOV) {

		if (BPF_SRC(insn->code) == BPF_X) {
			if (insn->imm != 0 || insn->off != 0) {
				verbose("BPF_MOV uses reserved fields\n");
				return -EINVAL;
			}

			/* check src operand */
			err = check_reg_arg(regs, insn->src_reg, SRC_OP);
			if (err)
				return err;
		} else {
			if (insn->src_reg != BPF_REG_0 || insn->off != 0) {
				verbose("BPF_MOV uses reserved fields\n");
				return -EINVAL;
			}
		}

		/* check dest operand */
		err = check_reg_arg(regs, insn->dst_reg, DST_OP);
		if (err)
			return err;

		/* we are setting our register to something new, we need to
		 * reset its range values.
		 */
		reset_reg_range_values(regs, insn->dst_reg);

		if (BPF_SRC(insn->code) == BPF_X) {
			if (BPF_CLASS(insn->code) == BPF_ALU64) {
				/* case: R1 = R2
				 * copy register state to dest reg
				 */
				regs[insn->dst_reg] = regs[insn->src_reg];
			} else {
				if (is_pointer_value(env, insn->src_reg)) {
					verbose("R%d partial copy of pointer\n",
						insn->src_reg);
					return -EACCES;
				}
				mark_reg_unknown_value(regs, insn->dst_reg);
			}
		} else {
			/* case: R = imm
			 * remember the value we stored into this reg
			 */
			regs[insn->dst_reg].type = CONST_IMM;
			regs[insn->dst_reg].imm = insn->imm;
			regs[insn->dst_reg].max_value = insn->imm;
			regs[insn->dst_reg].min_value = insn->imm;
			regs[insn->dst_reg].min_align = calc_align(insn->imm);
		}

	} else if (opcode > BPF_END) {
		verbose("invalid BPF_ALU opcode %x\n", opcode);
		return -EINVAL;

	} else {	/* all other ALU ops: and, sub, xor, add, ... */

		if (BPF_SRC(insn->code) == BPF_X) {
			if (insn->imm != 0 || insn->off != 0) {
				verbose("BPF_ALU uses reserved fields\n");
				return -EINVAL;
			}
			/* check src1 operand */
			err = check_reg_arg(regs, insn->src_reg, SRC_OP);
			if (err)
				return err;
		} else {
			if (insn->src_reg != BPF_REG_0 || insn->off != 0) {
				verbose("BPF_ALU uses reserved fields\n");
				return -EINVAL;
			}
		}

		/* check src2 operand */
		err = check_reg_arg(regs, insn->dst_reg, SRC_OP);
		if (err)
			return err;

		if ((opcode == BPF_MOD || opcode == BPF_DIV) &&
		    BPF_SRC(insn->code) == BPF_K && insn->imm == 0) {
			verbose("div by zero\n");
			return -EINVAL;
		}

		if ((opcode == BPF_LSH || opcode == BPF_RSH ||
		     opcode == BPF_ARSH) && BPF_SRC(insn->code) == BPF_K) {
			int size = BPF_CLASS(insn->code) == BPF_ALU64 ? 64 : 32;

			if (insn->imm < 0 || insn->imm >= size) {
				verbose("invalid shift %d\n", insn->imm);
				return -EINVAL;
			}
		}

		/* check dest operand */
		err = check_reg_arg(regs, insn->dst_reg, DST_OP_NO_MARK);
		if (err)
			return err;

		dst_reg = &regs[insn->dst_reg];

		/* first we want to adjust our ranges. */
		adjust_reg_min_max_vals(env, insn);

		/* pattern match 'bpf_add Rx, imm' instruction */
		if (opcode == BPF_ADD && BPF_CLASS(insn->code) == BPF_ALU64 &&
		    dst_reg->type == FRAME_PTR && BPF_SRC(insn->code) == BPF_K) {
			dst_reg->type = PTR_TO_STACK;
			dst_reg->imm = insn->imm;
			return 0;
		} else if (opcode == BPF_ADD &&
			   BPF_CLASS(insn->code) == BPF_ALU64 &&
			   dst_reg->type == PTR_TO_STACK &&
			   ((BPF_SRC(insn->code) == BPF_X &&
			     regs[insn->src_reg].type == CONST_IMM) ||
			    BPF_SRC(insn->code) == BPF_K)) {
			if (BPF_SRC(insn->code) == BPF_X)
				dst_reg->imm += regs[insn->src_reg].imm;
			else
				dst_reg->imm += insn->imm;
			return 0;
		} else if (opcode == BPF_ADD &&
			   BPF_CLASS(insn->code) == BPF_ALU64 &&
			   (dst_reg->type == PTR_TO_PACKET ||
			    (BPF_SRC(insn->code) == BPF_X &&
			     regs[insn->src_reg].type == PTR_TO_PACKET))) {
			/* ptr_to_packet += K|X */
			return check_packet_ptr_add(env, insn);
		} else if (BPF_CLASS(insn->code) == BPF_ALU64 &&
			   dst_reg->type == UNKNOWN_VALUE &&
			   env->allow_ptr_leaks) {
			/* unknown += K|X */
			return evaluate_reg_alu(env, insn);
		} else if (BPF_CLASS(insn->code) == BPF_ALU64 &&
			   dst_reg->type == CONST_IMM &&
			   env->allow_ptr_leaks) {
			/* reg_imm += K|X */
			return evaluate_reg_imm_alu(env, insn);
		} else if (is_pointer_value(env, insn->dst_reg)) {
			verbose("R%d pointer arithmetic prohibited\n",
				insn->dst_reg);
			return -EACCES;
		} else if (BPF_SRC(insn->code) == BPF_X &&
			   is_pointer_value(env, insn->src_reg)) {
			verbose("R%d pointer arithmetic prohibited\n",
				insn->src_reg);
			return -EACCES;
		}

		/* If we did pointer math on a map value then just set it to our
		 * PTR_TO_MAP_VALUE_ADJ type so we can deal with any stores or
		 * loads to this register appropriately, otherwise just mark the
		 * register as unknown.
		 */
		if (env->allow_ptr_leaks &&
		    BPF_CLASS(insn->code) == BPF_ALU64 && opcode == BPF_ADD &&
		    (dst_reg->type == PTR_TO_MAP_VALUE ||
		     dst_reg->type == PTR_TO_MAP_VALUE_ADJ))
			dst_reg->type = PTR_TO_MAP_VALUE_ADJ;
		else
			mark_reg_unknown_value(regs, insn->dst_reg);
	}

	return 0;
}

static void find_good_pkt_pointers(struct bpf_verifier_state *state,
				   struct bpf_reg_state *dst_reg)
{
	struct bpf_reg_state *regs = state->regs, *reg;
	int i;

	/* LLVM can generate two kind of checks:
	 *
	 * Type 1:
	 *
	 *   r2 = r3;
	 *   r2 += 8;
	 *   if (r2 > pkt_end) goto <handle exception>
	 *   <access okay>
	 *
	 *   Where:
	 *     r2 == dst_reg, pkt_end == src_reg
	 *     r2=pkt(id=n,off=8,r=0)
	 *     r3=pkt(id=n,off=0,r=0)
	 *
	 * Type 2:
	 *
	 *   r2 = r3;
	 *   r2 += 8;
	 *   if (pkt_end >= r2) goto <access okay>
	 *   <handle exception>
	 *
	 *   Where:
	 *     pkt_end == dst_reg, r2 == src_reg
	 *     r2=pkt(id=n,off=8,r=0)
	 *     r3=pkt(id=n,off=0,r=0)
	 *
	 * Find register r3 and mark its range as r3=pkt(id=n,off=0,r=8)
	 * so that range of bytes [r3, r3 + 8) is safe to access.
	 */

	for (i = 0; i < MAX_BPF_REG; i++)
		if (regs[i].type == PTR_TO_PACKET && regs[i].id == dst_reg->id)
			/* keep the maximum range already checked */
			regs[i].range = max(regs[i].range, dst_reg->off);

	for (i = 0; i < MAX_BPF_STACK; i += BPF_REG_SIZE) {
		if (state->stack_slot_type[i] != STACK_SPILL)
			continue;
		reg = &state->spilled_regs[i / BPF_REG_SIZE];
		if (reg->type == PTR_TO_PACKET && reg->id == dst_reg->id)
			reg->range = max(reg->range, dst_reg->off);
	}
}

/* Adjusts the register min/max values in the case that the dst_reg is the
 * variable register that we are working on, and src_reg is a constant or we're
 * simply doing a BPF_K check.
 */
static void reg_set_min_max(struct bpf_reg_state *true_reg,
			    struct bpf_reg_state *false_reg, u64 val,
			    u8 opcode)
{
	switch (opcode) {
	case BPF_JEQ:
		/* If this is false then we know nothing Jon Snow, but if it is
		 * true then we know for sure.
		 */
		true_reg->max_value = true_reg->min_value = val;
		break;
	case BPF_JNE:
		/* If this is true we know nothing Jon Snow, but if it is false
		 * we know the value for sure;
		 */
		false_reg->max_value = false_reg->min_value = val;
		break;
	case BPF_JGT:
		/* Unsigned comparison, the minimum value is 0. */
		false_reg->min_value = 0;
		/* fallthrough */
	case BPF_JSGT:
		/* If this is false then we know the maximum val is val,
		 * otherwise we know the min val is val+1.
		 */
		false_reg->max_value = val;
		true_reg->min_value = val + 1;
		break;
	case BPF_JGE:
		/* Unsigned comparison, the minimum value is 0. */
		false_reg->min_value = 0;
		/* fallthrough */
	case BPF_JSGE:
		/* If this is false then we know the maximum value is val - 1,
		 * otherwise we know the mimimum value is val.
		 */
		false_reg->max_value = val - 1;
		true_reg->min_value = val;
		break;
	default:
		break;
	}

	check_reg_overflow(false_reg);
	check_reg_overflow(true_reg);
}

/* Same as above, but for the case that dst_reg is a CONST_IMM reg and src_reg
 * is the variable reg.
 */
static void reg_set_min_max_inv(struct bpf_reg_state *true_reg,
				struct bpf_reg_state *false_reg, u64 val,
				u8 opcode)
{
	switch (opcode) {
	case BPF_JEQ:
		/* If this is false then we know nothing Jon Snow, but if it is
		 * true then we know for sure.
		 */
		true_reg->max_value = true_reg->min_value = val;
		break;
	case BPF_JNE:
		/* If this is true we know nothing Jon Snow, but if it is false
		 * we know the value for sure;
		 */
		false_reg->max_value = false_reg->min_value = val;
		break;
	case BPF_JGT:
		/* Unsigned comparison, the minimum value is 0. */
		true_reg->min_value = 0;
		/* fallthrough */
	case BPF_JSGT:
		/*
		 * If this is false, then the val is <= the register, if it is
		 * true the register <= to the val.
		 */
		false_reg->min_value = val;
		true_reg->max_value = val - 1;
		break;
	case BPF_JGE:
		/* Unsigned comparison, the minimum value is 0. */
		true_reg->min_value = 0;
		/* fallthrough */
	case BPF_JSGE:
		/* If this is false then constant < register, if it is true then
		 * the register < constant.
		 */
		false_reg->min_value = val + 1;
		true_reg->max_value = val;
		break;
	default:
		break;
	}

	check_reg_overflow(false_reg);
	check_reg_overflow(true_reg);
}

static void mark_map_reg(struct bpf_reg_state *regs, u32 regno, u32 id,
			 enum bpf_reg_type type)
{
	struct bpf_reg_state *reg = &regs[regno];

	if (reg->type == PTR_TO_MAP_VALUE_OR_NULL && reg->id == id) {
		if (type == UNKNOWN_VALUE) {
			__mark_reg_unknown_value(regs, regno);
		} else if (reg->map_ptr->inner_map_meta) {
			reg->type = CONST_PTR_TO_MAP;
			reg->map_ptr = reg->map_ptr->inner_map_meta;
		} else {
			reg->type = type;
		}
		/* We don't need id from this point onwards anymore, thus we
		 * should better reset it, so that state pruning has chances
		 * to take effect.
		 */
		reg->id = 0;
	}
}

/* The logic is similar to find_good_pkt_pointers(), both could eventually
 * be folded together at some point.
 */
static void mark_map_regs(struct bpf_verifier_state *state, u32 regno,
			  enum bpf_reg_type type)
{
	struct bpf_reg_state *regs = state->regs;
	u32 id = regs[regno].id;
	int i;

	for (i = 0; i < MAX_BPF_REG; i++)
		mark_map_reg(regs, i, id, type);

	for (i = 0; i < MAX_BPF_STACK; i += BPF_REG_SIZE) {
		if (state->stack_slot_type[i] != STACK_SPILL)
			continue;
		mark_map_reg(state->spilled_regs, i / BPF_REG_SIZE, id, type);
	}
}

static int check_cond_jmp_op(struct bpf_verifier_env *env,
			     struct bpf_insn *insn, int *insn_idx)
{
	struct bpf_verifier_state *other_branch, *this_branch = &env->cur_state;
	struct bpf_reg_state *regs = this_branch->regs, *dst_reg;
	u8 opcode = BPF_OP(insn->code);
	int err;

	if (opcode > BPF_EXIT) {
		verbose("invalid BPF_JMP opcode %x\n", opcode);
		return -EINVAL;
	}

	if (BPF_SRC(insn->code) == BPF_X) {
		if (insn->imm != 0) {
			verbose("BPF_JMP uses reserved fields\n");
			return -EINVAL;
		}

		/* check src1 operand */
		err = check_reg_arg(regs, insn->src_reg, SRC_OP);
		if (err)
			return err;

		if (is_pointer_value(env, insn->src_reg)) {
			verbose("R%d pointer comparison prohibited\n",
				insn->src_reg);
			return -EACCES;
		}
	} else {
		if (insn->src_reg != BPF_REG_0) {
			verbose("BPF_JMP uses reserved fields\n");
			return -EINVAL;
		}
	}

	/* check src2 operand */
	err = check_reg_arg(regs, insn->dst_reg, SRC_OP);
	if (err)
		return err;

	dst_reg = &regs[insn->dst_reg];

	/* detect if R == 0 where R was initialized to zero earlier */
	if (BPF_SRC(insn->code) == BPF_K &&
	    (opcode == BPF_JEQ || opcode == BPF_JNE) &&
	    dst_reg->type == CONST_IMM && dst_reg->imm == insn->imm) {
		if (opcode == BPF_JEQ) {
			/* if (imm == imm) goto pc+off;
			 * only follow the goto, ignore fall-through
			 */
			*insn_idx += insn->off;
			return 0;
		} else {
			/* if (imm != imm) goto pc+off;
			 * only follow fall-through branch, since
			 * that's where the program will go
			 */
			return 0;
		}
	}

	other_branch = push_stack(env, *insn_idx + insn->off + 1, *insn_idx);
	if (!other_branch)
		return -EFAULT;

	/* detect if we are comparing against a constant value so we can adjust
	 * our min/max values for our dst register.
	 */
	if (BPF_SRC(insn->code) == BPF_X) {
		if (regs[insn->src_reg].type == CONST_IMM)
			reg_set_min_max(&other_branch->regs[insn->dst_reg],
					dst_reg, regs[insn->src_reg].imm,
					opcode);
		else if (dst_reg->type == CONST_IMM)
			reg_set_min_max_inv(&other_branch->regs[insn->src_reg],
					    &regs[insn->src_reg], dst_reg->imm,
					    opcode);
	} else {
		reg_set_min_max(&other_branch->regs[insn->dst_reg],
					dst_reg, insn->imm, opcode);
	}

	/* detect if R == 0 where R is returned from bpf_map_lookup_elem() */
	if (BPF_SRC(insn->code) == BPF_K &&
	    insn->imm == 0 && (opcode == BPF_JEQ || opcode == BPF_JNE) &&
	    dst_reg->type == PTR_TO_MAP_VALUE_OR_NULL) {
		/* Mark all identical map registers in each branch as either
		 * safe or unknown depending R == 0 or R != 0 conditional.
		 */
		mark_map_regs(this_branch, insn->dst_reg,
			      opcode == BPF_JEQ ? PTR_TO_MAP_VALUE : UNKNOWN_VALUE);
		mark_map_regs(other_branch, insn->dst_reg,
			      opcode == BPF_JEQ ? UNKNOWN_VALUE : PTR_TO_MAP_VALUE);
	} else if (BPF_SRC(insn->code) == BPF_X && opcode == BPF_JGT &&
		   dst_reg->type == PTR_TO_PACKET &&
		   regs[insn->src_reg].type == PTR_TO_PACKET_END) {
		find_good_pkt_pointers(this_branch, dst_reg);
	} else if (BPF_SRC(insn->code) == BPF_X && opcode == BPF_JGE &&
		   dst_reg->type == PTR_TO_PACKET_END &&
		   regs[insn->src_reg].type == PTR_TO_PACKET) {
		find_good_pkt_pointers(other_branch, &regs[insn->src_reg]);
	} else if (is_pointer_value(env, insn->dst_reg)) {
		verbose("R%d pointer comparison prohibited\n", insn->dst_reg);
		return -EACCES;
	}
	if (log_level)
		print_verifier_state(this_branch);
	return 0;
}

/* return the map pointer stored inside BPF_LD_IMM64 instruction */
static struct bpf_map *ld_imm64_to_map_ptr(struct bpf_insn *insn)
{
	u64 imm64 = ((u64) (u32) insn[0].imm) | ((u64) (u32) insn[1].imm) << 32;

	return (struct bpf_map *) (unsigned long) imm64;
}

/* verify BPF_LD_IMM64 instruction */
static int check_ld_imm(struct bpf_verifier_env *env, struct bpf_insn *insn)
{
	struct bpf_reg_state *regs = env->cur_state.regs;
	int err;

	if (BPF_SIZE(insn->code) != BPF_DW) {
		verbose("invalid BPF_LD_IMM insn\n");
		return -EINVAL;
	}
	if (insn->off != 0) {
		verbose("BPF_LD_IMM64 uses reserved fields\n");
		return -EINVAL;
	}

	err = check_reg_arg(regs, insn->dst_reg, DST_OP);
	if (err)
		return err;

	if (insn->src_reg == 0) {
		u64 imm = ((u64)(insn + 1)->imm << 32) | (u32)insn->imm;

		regs[insn->dst_reg].type = CONST_IMM;
		regs[insn->dst_reg].imm = imm;
		return 0;
	}

	/* replace_map_fd_with_map_ptr() should have caught bad ld_imm64 */
	BUG_ON(insn->src_reg != BPF_PSEUDO_MAP_FD);

	regs[insn->dst_reg].type = CONST_PTR_TO_MAP;
	regs[insn->dst_reg].map_ptr = ld_imm64_to_map_ptr(insn);
	return 0;
}

static bool may_access_skb(enum bpf_prog_type type)
{
	switch (type) {
	case BPF_PROG_TYPE_SOCKET_FILTER:
	case BPF_PROG_TYPE_SCHED_CLS:
	case BPF_PROG_TYPE_SCHED_ACT:
		return true;
	default:
		return false;
	}
}

/* verify safety of LD_ABS|LD_IND instructions:
 * - they can only appear in the programs where ctx == skb
 * - since they are wrappers of function calls, they scratch R1-R5 registers,
 *   preserve R6-R9, and store return value into R0
 *
 * Implicit input:
 *   ctx == skb == R6 == CTX
 *
 * Explicit input:
 *   SRC == any register
 *   IMM == 32-bit immediate
 *
 * Output:
 *   R0 - 8/16/32-bit skb data converted to cpu endianness
 */
static int check_ld_abs(struct bpf_verifier_env *env, struct bpf_insn *insn)
{
	struct bpf_reg_state *regs = env->cur_state.regs;
	u8 mode = BPF_MODE(insn->code);
	int i, err;

	if (!may_access_skb(env->prog->type)) {
		verbose("BPF_LD_[ABS|IND] instructions not allowed for this program type\n");
		return -EINVAL;
	}

	if (insn->dst_reg != BPF_REG_0 || insn->off != 0 ||
	    BPF_SIZE(insn->code) == BPF_DW ||
	    (mode == BPF_ABS && insn->src_reg != BPF_REG_0)) {
		verbose("BPF_LD_[ABS|IND] uses reserved fields\n");
		return -EINVAL;
	}

	/* check whether implicit source operand (register R6) is readable */
	err = check_reg_arg(regs, BPF_REG_6, SRC_OP);
	if (err)
		return err;

	if (regs[BPF_REG_6].type != PTR_TO_CTX) {
		verbose("at the time of BPF_LD_ABS|IND R6 != pointer to skb\n");
		return -EINVAL;
	}

	if (mode == BPF_IND) {
		/* check explicit source operand */
		err = check_reg_arg(regs, insn->src_reg, SRC_OP);
		if (err)
			return err;
	}

	/* reset caller saved regs to unreadable */
	for (i = 0; i < CALLER_SAVED_REGS; i++)
		mark_reg_not_init(regs, caller_saved[i]);

	/* mark destination R0 register as readable, since it contains
	 * the value fetched from the packet
	 */
	regs[BPF_REG_0].type = UNKNOWN_VALUE;
	return 0;
}

/* non-recursive DFS pseudo code
 * 1  procedure DFS-iterative(G,v):
 * 2      label v as discovered
 * 3      let S be a stack
 * 4      S.push(v)
 * 5      while S is not empty
 * 6            t <- S.pop()
 * 7            if t is what we're looking for:
 * 8                return t
 * 9            for all edges e in G.adjacentEdges(t) do
 * 10               if edge e is already labelled
 * 11                   continue with the next edge
 * 12               w <- G.adjacentVertex(t,e)
 * 13               if vertex w is not discovered and not explored
 * 14                   label e as tree-edge
 * 15                   label w as discovered
 * 16                   S.push(w)
 * 17                   continue at 5
 * 18               else if vertex w is discovered
 * 19                   label e as back-edge
 * 20               else
 * 21                   // vertex w is explored
 * 22                   label e as forward- or cross-edge
 * 23           label t as explored
 * 24           S.pop()
 *
 * convention:
 * 0x10 - discovered
 * 0x11 - discovered and fall-through edge labelled
 * 0x12 - discovered and fall-through and branch edges labelled
 * 0x20 - explored
 */

enum {
	DISCOVERED = 0x10,
	EXPLORED = 0x20,
	FALLTHROUGH = 1,
	BRANCH = 2,
};

#define STATE_LIST_MARK ((struct bpf_verifier_state_list *) -1L)

static int *insn_stack;	/* stack of insns to process */
static int cur_stack;	/* current stack index */
static int *insn_state;

/* t, w, e - match pseudo-code above:
 * t - index of current instruction
 * w - next instruction
 * e - edge
 */
static int push_insn(int t, int w, int e, struct bpf_verifier_env *env)
{
	if (e == FALLTHROUGH && insn_state[t] >= (DISCOVERED | FALLTHROUGH))
		return 0;

	if (e == BRANCH && insn_state[t] >= (DISCOVERED | BRANCH))
		return 0;

	if (w < 0 || w >= env->prog->len) {
		verbose("jump out of range from insn %d to %d\n", t, w);
		return -EINVAL;
	}

	if (e == BRANCH)
		/* mark branch target for state pruning */
		env->explored_states[w] = STATE_LIST_MARK;

	if (insn_state[w] == 0) {
		/* tree-edge */
		insn_state[t] = DISCOVERED | e;
		insn_state[w] = DISCOVERED;
		if (cur_stack >= env->prog->len)
			return -E2BIG;
		insn_stack[cur_stack++] = w;
		return 1;
	} else if ((insn_state[w] & 0xF0) == DISCOVERED) {
		verbose("back-edge from insn %d to %d\n", t, w);
		return -EINVAL;
	} else if (insn_state[w] == EXPLORED) {
		/* forward- or cross-edge */
		insn_state[t] = DISCOVERED | e;
	} else {
		verbose("insn state internal bug\n");
		return -EFAULT;
	}
	return 0;
}

/* non-recursive depth-first-search to detect loops in BPF program
 * loop == back-edge in directed graph
 */
static int check_cfg(struct bpf_verifier_env *env)
{
	struct bpf_insn *insns = env->prog->insnsi;
	int insn_cnt = env->prog->len;
	int ret = 0;
	int i, t;

	insn_state = kcalloc(insn_cnt, sizeof(int), GFP_KERNEL);
	if (!insn_state)
		return -ENOMEM;

	insn_stack = kcalloc(insn_cnt, sizeof(int), GFP_KERNEL);
	if (!insn_stack) {
		kfree(insn_state);
		return -ENOMEM;
	}

	insn_state[0] = DISCOVERED; /* mark 1st insn as discovered */
	insn_stack[0] = 0; /* 0 is the first instruction */
	cur_stack = 1;

peek_stack:
	if (cur_stack == 0)
		goto check_state;
	t = insn_stack[cur_stack - 1];

	if (BPF_CLASS(insns[t].code) == BPF_JMP) {
		u8 opcode = BPF_OP(insns[t].code);

		if (opcode == BPF_EXIT) {
			goto mark_explored;
		} else if (opcode == BPF_CALL) {
			ret = push_insn(t, t + 1, FALLTHROUGH, env);
			if (ret == 1)
				goto peek_stack;
			else if (ret < 0)
				goto err_free;
			if (t + 1 < insn_cnt)
				env->explored_states[t + 1] = STATE_LIST_MARK;
		} else if (opcode == BPF_JA) {
			if (BPF_SRC(insns[t].code) != BPF_K) {
				ret = -EINVAL;
				goto err_free;
			}
			/* unconditional jump with single edge */
			ret = push_insn(t, t + insns[t].off + 1,
					FALLTHROUGH, env);
			if (ret == 1)
				goto peek_stack;
			else if (ret < 0)
				goto err_free;
			/* tell verifier to check for equivalent states
			 * after every call and jump
			 */
			if (t + 1 < insn_cnt)
				env->explored_states[t + 1] = STATE_LIST_MARK;
		} else {
			/* conditional jump with two edges */
			env->explored_states[t] = STATE_LIST_MARK;
			ret = push_insn(t, t + 1, FALLTHROUGH, env);
			if (ret == 1)
				goto peek_stack;
			else if (ret < 0)
				goto err_free;

			ret = push_insn(t, t + insns[t].off + 1, BRANCH, env);
			if (ret == 1)
				goto peek_stack;
			else if (ret < 0)
				goto err_free;
		}
	} else {
		/* all other non-branch instructions with single
		 * fall-through edge
		 */
		ret = push_insn(t, t + 1, FALLTHROUGH, env);
		if (ret == 1)
			goto peek_stack;
		else if (ret < 0)
			goto err_free;
	}

mark_explored:
	insn_state[t] = EXPLORED;
	if (cur_stack-- <= 0) {
		verbose("pop stack internal bug\n");
		ret = -EFAULT;
		goto err_free;
	}
	goto peek_stack;

check_state:
	for (i = 0; i < insn_cnt; i++) {
		if (insn_state[i] != EXPLORED) {
			verbose("unreachable insn %d\n", i);
			ret = -EINVAL;
			goto err_free;
		}
	}
	ret = 0; /* cfg looks good */

err_free:
	kfree(insn_state);
	kfree(insn_stack);
	return ret;
}

/* the following conditions reduce the number of explored insns
 * from ~140k to ~80k for ultra large programs that use a lot of ptr_to_packet
 */
static bool compare_ptrs_to_packet(struct bpf_verifier_env *env,
				   struct bpf_reg_state *old,
				   struct bpf_reg_state *cur)
{
	if (old->id != cur->id)
		return false;

	/* old ptr_to_packet is more conservative, since it allows smaller
	 * range. Ex:
	 * old(off=0,r=10) is equal to cur(off=0,r=20), because
	 * old(off=0,r=10) means that with range=10 the verifier proceeded
	 * further and found no issues with the program. Now we're in the same
	 * spot with cur(off=0,r=20), so we're safe too, since anything further
	 * will only be looking at most 10 bytes after this pointer.
	 */
	if (old->off == cur->off && old->range < cur->range)
		return true;

	/* old(off=20,r=10) is equal to cur(off=22,re=22 or 5 or 0)
	 * since both cannot be used for packet access and safe(old)
	 * pointer has smaller off that could be used for further
	 * 'if (ptr > data_end)' check
	 * Ex:
	 * old(off=20,r=10) and cur(off=22,r=22) and cur(off=22,r=0) mean
	 * that we cannot access the packet.
	 * The safe range is:
	 * [ptr, ptr + range - off)
	 * so whenever off >=range, it means no safe bytes from this pointer.
	 * When comparing old->off <= cur->off, it means that older code
	 * went with smaller offset and that offset was later
	 * used to figure out the safe range after 'if (ptr > data_end)' check
	 * Say, 'old' state was explored like:
	 * ... R3(off=0, r=0)
	 * R4 = R3 + 20
	 * ... now R4(off=20,r=0)  <-- here
	 * if (R4 > data_end)
	 * ... R4(off=20,r=20), R3(off=0,r=20) and R3 can be used to access.
	 * ... the code further went all the way to bpf_exit.
	 * Now the 'cur' state at the mark 'here' has R4(off=30,r=0).
	 * old_R4(off=20,r=0) equal to cur_R4(off=30,r=0), since if the verifier
	 * goes further, such cur_R4 will give larger safe packet range after
	 * 'if (R4 > data_end)' and all further insn were already good with r=20,
	 * so they will be good with r=30 and we can prune the search.
	 */
	if (!env->strict_alignment && old->off <= cur->off &&
	    old->off >= old->range && cur->off >= cur->range)
		return true;

	return false;
}

/* compare two verifier states
 *
 * all states stored in state_list are known to be valid, since
 * verifier reached 'bpf_exit' instruction through them
 *
 * this function is called when verifier exploring different branches of
 * execution popped from the state stack. If it sees an old state that has
 * more strict register state and more strict stack state then this execution
 * branch doesn't need to be explored further, since verifier already
 * concluded that more strict state leads to valid finish.
 *
 * Therefore two states are equivalent if register state is more conservative
 * and explored stack state is more conservative than the current one.
 * Example:
 *       explored                   current
 * (slot1=INV slot2=MISC) == (slot1=MISC slot2=MISC)
 * (slot1=MISC slot2=MISC) != (slot1=INV slot2=MISC)
 *
 * In other words if current stack state (one being explored) has more
 * valid slots than old one that already passed validation, it means
 * the verifier can stop exploring and conclude that current state is valid too
 *
 * Similarly with registers. If explored state has register type as invalid
 * whereas register type in current state is meaningful, it means that
 * the current state will reach 'bpf_exit' instruction safely
 */
static bool states_equal(struct bpf_verifier_env *env,
			 struct bpf_verifier_state *old,
			 struct bpf_verifier_state *cur)
{
	bool varlen_map_access = env->varlen_map_value_access;
	struct bpf_reg_state *rold, *rcur;
	int i;

	for (i = 0; i < MAX_BPF_REG; i++) {
		rold = &old->regs[i];
		rcur = &cur->regs[i];

		if (memcmp(rold, rcur, sizeof(*rold)) == 0)
			continue;

		/* If the ranges were not the same, but everything else was and
		 * we didn't do a variable access into a map then we are a-ok.
		 */
		if (!varlen_map_access &&
		    memcmp(rold, rcur, offsetofend(struct bpf_reg_state, id)) == 0)
			continue;

		/* If we didn't map access then again we don't care about the
		 * mismatched range values and it's ok if our old type was
		 * UNKNOWN and we didn't go to a NOT_INIT'ed reg.
		 */
		if (rold->type == NOT_INIT ||
		    (!varlen_map_access && rold->type == UNKNOWN_VALUE &&
		     rcur->type != NOT_INIT))
			continue;

		/* Don't care about the reg->id in this case. */
		if (rold->type == PTR_TO_MAP_VALUE_OR_NULL &&
		    rcur->type == PTR_TO_MAP_VALUE_OR_NULL &&
		    rold->map_ptr == rcur->map_ptr)
			continue;

		if (rold->type == PTR_TO_PACKET && rcur->type == PTR_TO_PACKET &&
		    compare_ptrs_to_packet(env, rold, rcur))
			continue;

		return false;
	}

	for (i = 0; i < MAX_BPF_STACK; i++) {
		if (old->stack_slot_type[i] == STACK_INVALID)
			continue;
		if (old->stack_slot_type[i] != cur->stack_slot_type[i])
			/* Ex: old explored (safe) state has STACK_SPILL in
			 * this stack slot, but current has has STACK_MISC ->
			 * this verifier states are not equivalent,
			 * return false to continue verification of this path
			 */
			return false;
		if (i % BPF_REG_SIZE)
			continue;
		if (memcmp(&old->spilled_regs[i / BPF_REG_SIZE],
			   &cur->spilled_regs[i / BPF_REG_SIZE],
			   sizeof(old->spilled_regs[0])))
			/* when explored and current stack slot types are
			 * the same, check that stored pointers types
			 * are the same as well.
			 * Ex: explored safe path could have stored
			 * (bpf_reg_state) {.type = PTR_TO_STACK, .imm = -8}
			 * but current path has stored:
			 * (bpf_reg_state) {.type = PTR_TO_STACK, .imm = -16}
			 * such verifier states are not equivalent.
			 * return false to continue verification of this path
			 */
			return false;
		else
			continue;
	}
	return true;
}

static int is_state_visited(struct bpf_verifier_env *env, int insn_idx)
{
	struct bpf_verifier_state_list *new_sl;
	struct bpf_verifier_state_list *sl;

	sl = env->explored_states[insn_idx];
	if (!sl)
		/* this 'insn_idx' instruction wasn't marked, so we will not
		 * be doing state search here
		 */
		return 0;

	while (sl != STATE_LIST_MARK) {
		if (states_equal(env, &sl->state, &env->cur_state))
			/* reached equivalent register/stack state,
			 * prune the search
			 */
			return 1;
		sl = sl->next;
	}

	/* there were no equivalent states, remember current one.
	 * technically the current state is not proven to be safe yet,
	 * but it will either reach bpf_exit (which means it's safe) or
	 * it will be rejected. Since there are no loops, we won't be
	 * seeing this 'insn_idx' instruction again on the way to bpf_exit
	 */
	new_sl = kmalloc(sizeof(struct bpf_verifier_state_list), GFP_USER);
	if (!new_sl)
		return -ENOMEM;

	/* add new state to the head of linked list */
	memcpy(&new_sl->state, &env->cur_state, sizeof(env->cur_state));
	new_sl->next = env->explored_states[insn_idx];
	env->explored_states[insn_idx] = new_sl;
	return 0;
}

static int ext_analyzer_insn_hook(struct bpf_verifier_env *env,
				  int insn_idx, int prev_insn_idx)
{
	if (!env->analyzer_ops || !env->analyzer_ops->insn_hook)
		return 0;

	return env->analyzer_ops->insn_hook(env, insn_idx, prev_insn_idx);
}

static int do_check(struct bpf_verifier_env *env)
{
	struct bpf_verifier_state *state = &env->cur_state;
	struct bpf_insn *insns = env->prog->insnsi;
	struct bpf_reg_state *regs = state->regs;
	int insn_cnt = env->prog->len;
	int insn_idx, prev_insn_idx = 0;
	int insn_processed = 0;
	bool do_print_state = false;

	init_reg_state(regs);
	insn_idx = 0;
	env->varlen_map_value_access = false;
	for (;;) {
		struct bpf_insn *insn;
		u8 class;
		int err;

		if (insn_idx >= insn_cnt) {
			verbose("invalid insn idx %d insn_cnt %d\n",
				insn_idx, insn_cnt);
			return -EFAULT;
		}

		insn = &insns[insn_idx];
		class = BPF_CLASS(insn->code);

		if (++insn_processed > BPF_COMPLEXITY_LIMIT_INSNS) {
			verbose("BPF program is too large. Processed %d insn\n",
				insn_processed);
			return -E2BIG;
		}

		err = is_state_visited(env, insn_idx);
		if (err < 0)
			return err;
		if (err == 1) {
			/* found equivalent state, can prune the search */
			if (log_level) {
				if (do_print_state)
					verbose("\nfrom %d to %d: safe\n",
						prev_insn_idx, insn_idx);
				else
					verbose("%d: safe\n", insn_idx);
			}
			goto process_bpf_exit;
		}

		if (need_resched())
			cond_resched();

		if (log_level > 1 || (log_level && do_print_state)) {
			if (log_level > 1)
				verbose("%d:", insn_idx);
			else
				verbose("\nfrom %d to %d:",
					prev_insn_idx, insn_idx);
			print_verifier_state(&env->cur_state);
			do_print_state = false;
		}

		if (log_level) {
			verbose("%d: ", insn_idx);
			print_bpf_insn(env, insn);
		}

		err = ext_analyzer_insn_hook(env, insn_idx, prev_insn_idx);
		if (err)
			return err;

		if (class == BPF_ALU || class == BPF_ALU64) {
			err = check_alu_op(env, insn);
			if (err)
				return err;

		} else if (class == BPF_LDX) {
			enum bpf_reg_type *prev_src_type, src_reg_type;

			/* check for reserved fields is already done */

			/* check src operand */
			err = check_reg_arg(regs, insn->src_reg, SRC_OP);
			if (err)
				return err;

			err = check_reg_arg(regs, insn->dst_reg, DST_OP_NO_MARK);
			if (err)
				return err;

			src_reg_type = regs[insn->src_reg].type;

			/* check that memory (src_reg + off) is readable,
			 * the state of dst_reg will be updated by this func
			 */
			err = check_mem_access(env, insn->src_reg, insn->off,
					       BPF_SIZE(insn->code), BPF_READ,
					       insn->dst_reg);
			if (err)
				return err;

			if (BPF_SIZE(insn->code) != BPF_W &&
			    BPF_SIZE(insn->code) != BPF_DW) {
				insn_idx++;
				continue;
			}

			prev_src_type = &env->insn_aux_data[insn_idx].ptr_type;

			if (*prev_src_type == NOT_INIT) {
				/* saw a valid insn
				 * dst_reg = *(u32 *)(src_reg + off)
				 * save type to validate intersecting paths
				 */
				*prev_src_type = src_reg_type;

			} else if (src_reg_type != *prev_src_type &&
				   (src_reg_type == PTR_TO_CTX ||
				    *prev_src_type == PTR_TO_CTX)) {
				/* ABuser program is trying to use the same insn
				 * dst_reg = *(u32*) (src_reg + off)
				 * with different pointer types:
				 * src_reg == ctx in one branch and
				 * src_reg == stack|map in some other branch.
				 * Reject it.
				 */
				verbose("same insn cannot be used with different pointers\n");
				return -EINVAL;
			}

		} else if (class == BPF_STX) {
			enum bpf_reg_type *prev_dst_type, dst_reg_type;

			if (BPF_MODE(insn->code) == BPF_XADD) {
				err = check_xadd(env, insn);
				if (err)
					return err;
				insn_idx++;
				continue;
			}

			/* check src1 operand */
			err = check_reg_arg(regs, insn->src_reg, SRC_OP);
			if (err)
				return err;
			/* check src2 operand */
			err = check_reg_arg(regs, insn->dst_reg, SRC_OP);
			if (err)
				return err;

			dst_reg_type = regs[insn->dst_reg].type;

			/* check that memory (dst_reg + off) is writeable */
			err = check_mem_access(env, insn->dst_reg, insn->off,
					       BPF_SIZE(insn->code), BPF_WRITE,
					       insn->src_reg);
			if (err)
				return err;

			prev_dst_type = &env->insn_aux_data[insn_idx].ptr_type;

			if (*prev_dst_type == NOT_INIT) {
				*prev_dst_type = dst_reg_type;
			} else if (dst_reg_type != *prev_dst_type &&
				   (dst_reg_type == PTR_TO_CTX ||
				    *prev_dst_type == PTR_TO_CTX)) {
				verbose("same insn cannot be used with different pointers\n");
				return -EINVAL;
			}

		} else if (class == BPF_ST) {
			if (BPF_MODE(insn->code) != BPF_MEM ||
			    insn->src_reg != BPF_REG_0) {
				verbose("BPF_ST uses reserved fields\n");
				return -EINVAL;
			}
			/* check src operand */
			err = check_reg_arg(regs, insn->dst_reg, SRC_OP);
			if (err)
				return err;

			/* check that memory (dst_reg + off) is writeable */
			err = check_mem_access(env, insn->dst_reg, insn->off,
					       BPF_SIZE(insn->code), BPF_WRITE,
					       -1);
			if (err)
				return err;

		} else if (class == BPF_JMP) {
			u8 opcode = BPF_OP(insn->code);

			if (opcode == BPF_CALL) {
				if (BPF_SRC(insn->code) != BPF_K ||
				    insn->off != 0 ||
				    insn->src_reg != BPF_REG_0 ||
				    insn->dst_reg != BPF_REG_0) {
					verbose("BPF_CALL uses reserved fields\n");
					return -EINVAL;
				}

				err = check_call(env, insn->imm, insn_idx);
				if (err)
					return err;

			} else if (opcode == BPF_JA) {
				if (BPF_SRC(insn->code) != BPF_K ||
				    insn->imm != 0 ||
				    insn->src_reg != BPF_REG_0 ||
				    insn->dst_reg != BPF_REG_0) {
					verbose("BPF_JA uses reserved fields\n");
					return -EINVAL;
				}

				insn_idx += insn->off + 1;
				continue;

			} else if (opcode == BPF_EXIT) {
				if (BPF_SRC(insn->code) != BPF_K ||
				    insn->imm != 0 ||
				    insn->src_reg != BPF_REG_0 ||
				    insn->dst_reg != BPF_REG_0) {
					verbose("BPF_EXIT uses reserved fields\n");
					return -EINVAL;
				}

				/* eBPF calling convetion is such that R0 is used
				 * to return the value from eBPF program.
				 * Make sure that it's readable at this time
				 * of bpf_exit, which means that program wrote
				 * something into it earlier
				 */
				err = check_reg_arg(regs, BPF_REG_0, SRC_OP);
				if (err)
					return err;

				if (is_pointer_value(env, BPF_REG_0)) {
					verbose("R0 leaks addr as return value\n");
					return -EACCES;
				}

process_bpf_exit:
				insn_idx = pop_stack(env, &prev_insn_idx);
				if (insn_idx < 0) {
					break;
				} else {
					do_print_state = true;
					continue;
				}
			} else {
				err = check_cond_jmp_op(env, insn, &insn_idx);
				if (err)
					return err;
			}
		} else if (class == BPF_LD) {
			u8 mode = BPF_MODE(insn->code);

			if (mode == BPF_ABS || mode == BPF_IND) {
				err = check_ld_abs(env, insn);
				if (err)
					return err;

			} else if (mode == BPF_IMM) {
				err = check_ld_imm(env, insn);
				if (err)
					return err;

				insn_idx++;
			} else {
				verbose("invalid BPF_LD mode\n");
				return -EINVAL;
			}
			reset_reg_range_values(regs, insn->dst_reg);
		} else {
			verbose("unknown insn class %d\n", class);
			return -EINVAL;
		}

		insn_idx++;
	}

	verbose("processed %d insns, stack depth %d\n",
		insn_processed, env->prog->aux->stack_depth);
	return 0;
}

static int check_map_prealloc(struct bpf_map *map)
{
	return (map->map_type != BPF_MAP_TYPE_HASH &&
		map->map_type != BPF_MAP_TYPE_PERCPU_HASH &&
		map->map_type != BPF_MAP_TYPE_HASH_OF_MAPS) ||
		!(map->map_flags & BPF_F_NO_PREALLOC);
}

static int check_map_prog_compatibility(struct bpf_map *map,
					struct bpf_prog *prog)

{
	/* Make sure that BPF_PROG_TYPE_PERF_EVENT programs only use
	 * preallocated hash maps, since doing memory allocation
	 * in overflow_handler can crash depending on where nmi got
	 * triggered.
	 */
	if (prog->type == BPF_PROG_TYPE_PERF_EVENT) {
		if (!check_map_prealloc(map)) {
			verbose("perf_event programs can only use preallocated hash map\n");
			return -EINVAL;
		}
		if (map->inner_map_meta &&
		    !check_map_prealloc(map->inner_map_meta)) {
			verbose("perf_event programs can only use preallocated inner hash map\n");
			return -EINVAL;
		}
	}
	return 0;
}

/* look for pseudo eBPF instructions that access map FDs and
 * replace them with actual map pointers
 */
static int replace_map_fd_with_map_ptr(struct bpf_verifier_env *env)
{
	struct bpf_insn *insn = env->prog->insnsi;
	int insn_cnt = env->prog->len;
	int i, j, err;

	err = bpf_prog_calc_tag(env->prog);
	if (err)
		return err;

	for (i = 0; i < insn_cnt; i++, insn++) {
		if (BPF_CLASS(insn->code) == BPF_LDX &&
		    (BPF_MODE(insn->code) != BPF_MEM || insn->imm != 0)) {
			verbose("BPF_LDX uses reserved fields\n");
			return -EINVAL;
		}

		if (BPF_CLASS(insn->code) == BPF_STX &&
		    ((BPF_MODE(insn->code) != BPF_MEM &&
		      BPF_MODE(insn->code) != BPF_XADD) || insn->imm != 0)) {
			verbose("BPF_STX uses reserved fields\n");
			return -EINVAL;
		}

		if (insn[0].code == (BPF_LD | BPF_IMM | BPF_DW)) {
			struct bpf_map *map;
			struct fd f;

			if (i == insn_cnt - 1 || insn[1].code != 0 ||
			    insn[1].dst_reg != 0 || insn[1].src_reg != 0 ||
			    insn[1].off != 0) {
				verbose("invalid bpf_ld_imm64 insn\n");
				return -EINVAL;
			}

			if (insn->src_reg == 0)
				/* valid generic load 64-bit imm */
				goto next_insn;

			if (insn->src_reg != BPF_PSEUDO_MAP_FD) {
				verbose("unrecognized bpf_ld_imm64 insn\n");
				return -EINVAL;
			}

			f = fdget(insn->imm);
			map = __bpf_map_get(f);
			if (IS_ERR(map)) {
				verbose("fd %d is not pointing to valid bpf_map\n",
					insn->imm);
				return PTR_ERR(map);
			}

			err = check_map_prog_compatibility(map, env->prog);
			if (err) {
				fdput(f);
				return err;
			}

			/* store map pointer inside BPF_LD_IMM64 instruction */
			insn[0].imm = (u32) (unsigned long) map;
			insn[1].imm = ((u64) (unsigned long) map) >> 32;

			/* check whether we recorded this map already */
			for (j = 0; j < env->used_map_cnt; j++)
				if (env->used_maps[j] == map) {
					fdput(f);
					goto next_insn;
				}

			if (env->used_map_cnt >= MAX_USED_MAPS) {
				fdput(f);
				return -E2BIG;
			}

			/* hold the map. If the program is rejected by verifier,
			 * the map will be released by release_maps() or it
			 * will be used by the valid program until it's unloaded
			 * and all maps are released in free_bpf_prog_info()
			 */
			map = bpf_map_inc(map, false);
			if (IS_ERR(map)) {
				fdput(f);
				return PTR_ERR(map);
			}
			env->used_maps[env->used_map_cnt++] = map;

			fdput(f);
next_insn:
			insn++;
			i++;
		}
	}

	/* now all pseudo BPF_LD_IMM64 instructions load valid
	 * 'struct bpf_map *' into a register instead of user map_fd.
	 * These pointers will be used later by verifier to validate map access.
	 */
	return 0;
}

/* drop refcnt of maps used by the rejected program */
static void release_maps(struct bpf_verifier_env *env)
{
	int i;

	for (i = 0; i < env->used_map_cnt; i++)
		bpf_map_put(env->used_maps[i]);
}

/* convert pseudo BPF_LD_IMM64 into generic BPF_LD_IMM64 */
static void convert_pseudo_ld_imm64(struct bpf_verifier_env *env)
{
	struct bpf_insn *insn = env->prog->insnsi;
	int insn_cnt = env->prog->len;
	int i;

	for (i = 0; i < insn_cnt; i++, insn++)
		if (insn->code == (BPF_LD | BPF_IMM | BPF_DW))
			insn->src_reg = 0;
}

/* single env->prog->insni[off] instruction was replaced with the range
 * insni[off, off + cnt).  Adjust corresponding insn_aux_data by copying
 * [0, off) and [off, end) to new locations, so the patched range stays zero
 */
static int adjust_insn_aux_data(struct bpf_verifier_env *env, u32 prog_len,
				u32 off, u32 cnt)
{
	struct bpf_insn_aux_data *new_data, *old_data = env->insn_aux_data;

	if (cnt == 1)
		return 0;
	new_data = vzalloc(sizeof(struct bpf_insn_aux_data) * prog_len);
	if (!new_data)
		return -ENOMEM;
	memcpy(new_data, old_data, sizeof(struct bpf_insn_aux_data) * off);
	memcpy(new_data + off + cnt - 1, old_data + off,
	       sizeof(struct bpf_insn_aux_data) * (prog_len - off - cnt + 1));
	env->insn_aux_data = new_data;
	vfree(old_data);
	return 0;
}

static struct bpf_prog *bpf_patch_insn_data(struct bpf_verifier_env *env, u32 off,
					    const struct bpf_insn *patch, u32 len)
{
	struct bpf_prog *new_prog;

	new_prog = bpf_patch_insn_single(env->prog, off, patch, len);
	if (!new_prog)
		return NULL;
	if (adjust_insn_aux_data(env, new_prog->len, off, len))
		return NULL;
	return new_prog;
}

/* convert load instructions that access fields of 'struct __sk_buff'
 * into sequence of instructions that access fields of 'struct sk_buff'
 */
static int convert_ctx_accesses(struct bpf_verifier_env *env)
{
	const struct bpf_verifier_ops *ops = env->prog->aux->ops;
	const int insn_cnt = env->prog->len;
	struct bpf_insn insn_buf[16], *insn;
	struct bpf_prog *new_prog;
	enum bpf_access_type type;
	int i, cnt, delta = 0;

	if (ops->gen_prologue) {
		cnt = ops->gen_prologue(insn_buf, env->seen_direct_write,
					env->prog);
		if (cnt >= ARRAY_SIZE(insn_buf)) {
			verbose("bpf verifier is misconfigured\n");
			return -EINVAL;
		} else if (cnt) {
			new_prog = bpf_patch_insn_data(env, 0, insn_buf, cnt);
			if (!new_prog)
				return -ENOMEM;

			env->prog = new_prog;
			delta += cnt - 1;
		}
	}

	if (!ops->convert_ctx_access)
		return 0;

	insn = env->prog->insnsi + delta;

	for (i = 0; i < insn_cnt; i++, insn++) {
		if (insn->code == (BPF_LDX | BPF_MEM | BPF_B) ||
		    insn->code == (BPF_LDX | BPF_MEM | BPF_H) ||
		    insn->code == (BPF_LDX | BPF_MEM | BPF_W) ||
		    insn->code == (BPF_LDX | BPF_MEM | BPF_DW))
			type = BPF_READ;
		else if (insn->code == (BPF_STX | BPF_MEM | BPF_B) ||
			 insn->code == (BPF_STX | BPF_MEM | BPF_H) ||
			 insn->code == (BPF_STX | BPF_MEM | BPF_W) ||
			 insn->code == (BPF_STX | BPF_MEM | BPF_DW))
			type = BPF_WRITE;
		else
			continue;

		if (env->insn_aux_data[i + delta].ptr_type != PTR_TO_CTX)
			continue;

		cnt = ops->convert_ctx_access(type, insn, insn_buf, env->prog);
		if (cnt == 0 || cnt >= ARRAY_SIZE(insn_buf)) {
			verbose("bpf verifier is misconfigured\n");
			return -EINVAL;
		}

		new_prog = bpf_patch_insn_data(env, i + delta, insn_buf, cnt);
		if (!new_prog)
			return -ENOMEM;

		delta += cnt - 1;

		/* keep walking new program and skip insns we just inserted */
		env->prog = new_prog;
		insn      = new_prog->insnsi + i + delta;
	}

	return 0;
}

/* fixup insn->imm field of bpf_call instructions
 * and inline eligible helpers as explicit sequence of BPF instructions
 *
 * this function is called after eBPF program passed verification
 */
static int fixup_bpf_calls(struct bpf_verifier_env *env)
{
	struct bpf_prog *prog = env->prog;
	struct bpf_insn *insn = prog->insnsi;
	const struct bpf_func_proto *fn;
	const int insn_cnt = prog->len;
	struct bpf_insn insn_buf[16];
	struct bpf_prog *new_prog;
	struct bpf_map *map_ptr;
	int i, cnt, delta = 0;

	for (i = 0; i < insn_cnt; i++, insn++) {
		if (insn->code != (BPF_JMP | BPF_CALL))
			continue;

		if (insn->imm == BPF_FUNC_get_route_realm)
			prog->dst_needed = 1;
		if (insn->imm == BPF_FUNC_get_prandom_u32)
			bpf_user_rnd_init_once();
		if (insn->imm == BPF_FUNC_tail_call) {
			/* If we tail call into other programs, we
			 * cannot make any assumptions since they can
			 * be replaced dynamically during runtime in
			 * the program array.
			 */
			prog->cb_access = 1;

			/* mark bpf_tail_call as different opcode to avoid
			 * conditional branch in the interpeter for every normal
			 * call and to prevent accidental JITing by JIT compiler
			 * that doesn't support bpf_tail_call yet
			 */
			insn->imm = 0;
			insn->code = BPF_JMP | BPF_TAIL_CALL;
			continue;
		}

		if (ebpf_jit_enabled() && insn->imm == BPF_FUNC_map_lookup_elem) {
			map_ptr = env->insn_aux_data[i + delta].map_ptr;
			if (map_ptr == BPF_MAP_PTR_POISON ||
			    !map_ptr->ops->map_gen_lookup)
				goto patch_call_imm;

			cnt = map_ptr->ops->map_gen_lookup(map_ptr, insn_buf);
			if (cnt == 0 || cnt >= ARRAY_SIZE(insn_buf)) {
				verbose("bpf verifier is misconfigured\n");
				return -EINVAL;
			}

			new_prog = bpf_patch_insn_data(env, i + delta, insn_buf,
						       cnt);
			if (!new_prog)
				return -ENOMEM;

			delta += cnt - 1;

			/* keep walking new program and skip insns we just inserted */
			env->prog = prog = new_prog;
			insn      = new_prog->insnsi + i + delta;
			continue;
		}

patch_call_imm:
		fn = prog->aux->ops->get_func_proto(insn->imm);
		/* all functions that have prototype and verifier allowed
		 * programs to call them, must be real in-kernel functions
		 */
		if (!fn->func) {
			verbose("kernel subsystem misconfigured func %s#%d\n",
				func_id_name(insn->imm), insn->imm);
			return -EFAULT;
		}
		insn->imm = fn->func - __bpf_call_base;
	}

	return 0;
}

static void free_states(struct bpf_verifier_env *env)
{
	struct bpf_verifier_state_list *sl, *sln;
	int i;

	if (!env->explored_states)
		return;

	for (i = 0; i < env->prog->len; i++) {
		sl = env->explored_states[i];

		if (sl)
			while (sl != STATE_LIST_MARK) {
				sln = sl->next;
				kfree(sl);
				sl = sln;
			}
	}

	kfree(env->explored_states);
}

int bpf_check(struct bpf_prog **prog, union bpf_attr *attr)
{
	char __user *log_ubuf = NULL;
	struct bpf_verifier_env *env;
	int ret = -EINVAL;

	/* 'struct bpf_verifier_env' can be global, but since it's not small,
	 * allocate/free it every time bpf_check() is called
	 */
	env = kzalloc(sizeof(struct bpf_verifier_env), GFP_KERNEL);
	if (!env)
		return -ENOMEM;

	env->insn_aux_data = vzalloc(sizeof(struct bpf_insn_aux_data) *
				     (*prog)->len);
	ret = -ENOMEM;
	if (!env->insn_aux_data)
		goto err_free_env;
	env->prog = *prog;

	/* grab the mutex to protect few globals used by verifier */
	mutex_lock(&bpf_verifier_lock);

	if (attr->log_level || attr->log_buf || attr->log_size) {
		/* user requested verbose verifier output
		 * and supplied buffer to store the verification trace
		 */
		log_level = attr->log_level;
		log_ubuf = (char __user *) (unsigned long) attr->log_buf;
		log_size = attr->log_size;
		log_len = 0;

		ret = -EINVAL;
		/* log_* values have to be sane */
		if (log_size < 128 || log_size > UINT_MAX >> 8 ||
		    log_level == 0 || log_ubuf == NULL)
			goto err_unlock;

		ret = -ENOMEM;
		log_buf = vmalloc(log_size);
		if (!log_buf)
			goto err_unlock;
	} else {
		log_level = 0;
	}

	env->strict_alignment = !!(attr->prog_flags & BPF_F_STRICT_ALIGNMENT);
	if (!IS_ENABLED(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS))
		env->strict_alignment = true;

	ret = replace_map_fd_with_map_ptr(env);
	if (ret < 0)
		goto skip_full_check;

	env->explored_states = kcalloc(env->prog->len,
				       sizeof(struct bpf_verifier_state_list *),
				       GFP_USER);
	ret = -ENOMEM;
	if (!env->explored_states)
		goto skip_full_check;

	ret = check_cfg(env);
	if (ret < 0)
		goto skip_full_check;

	env->allow_ptr_leaks = capable(CAP_SYS_ADMIN);

	ret = do_check(env);

skip_full_check:
	while (pop_stack(env, NULL) >= 0);
	free_states(env);

	if (ret == 0)
		/* program is valid, convert *(u32*)(ctx + off) accesses */
		ret = convert_ctx_accesses(env);

	if (ret == 0)
		ret = fixup_bpf_calls(env);

	if (log_level && log_len >= log_size - 1) {
		BUG_ON(log_len >= log_size);
		/* verifier log exceeded user supplied buffer */
		ret = -ENOSPC;
		/* fall through to return what was recorded */
	}

	/* copy verifier log back to user space including trailing zero */
	if (log_level && copy_to_user(log_ubuf, log_buf, log_len + 1) != 0) {
		ret = -EFAULT;
		goto free_log_buf;
	}

	if (ret == 0 && env->used_map_cnt) {
		/* if program passed verifier, update used_maps in bpf_prog_info */
		env->prog->aux->used_maps = kmalloc_array(env->used_map_cnt,
							  sizeof(env->used_maps[0]),
							  GFP_KERNEL);

		if (!env->prog->aux->used_maps) {
			ret = -ENOMEM;
			goto free_log_buf;
		}

		memcpy(env->prog->aux->used_maps, env->used_maps,
		       sizeof(env->used_maps[0]) * env->used_map_cnt);
		env->prog->aux->used_map_cnt = env->used_map_cnt;

		/* program is valid. Convert pseudo bpf_ld_imm64 into generic
		 * bpf_ld_imm64 instructions
		 */
		convert_pseudo_ld_imm64(env);
	}

free_log_buf:
	if (log_level)
		vfree(log_buf);
	if (!env->prog->aux->used_maps)
		/* if we didn't copy map pointers into bpf_prog_info, release
		 * them now. Otherwise free_bpf_prog_info() will release them.
		 */
		release_maps(env);
	*prog = env->prog;
err_unlock:
	mutex_unlock(&bpf_verifier_lock);
	vfree(env->insn_aux_data);
err_free_env:
	kfree(env);
	return ret;
}

int bpf_analyzer(struct bpf_prog *prog, const struct bpf_ext_analyzer_ops *ops,
		 void *priv)
{
	struct bpf_verifier_env *env;
	int ret;

	env = kzalloc(sizeof(struct bpf_verifier_env), GFP_KERNEL);
	if (!env)
		return -ENOMEM;

	env->insn_aux_data = vzalloc(sizeof(struct bpf_insn_aux_data) *
				     prog->len);
	ret = -ENOMEM;
	if (!env->insn_aux_data)
		goto err_free_env;
	env->prog = prog;
	env->analyzer_ops = ops;
	env->analyzer_priv = priv;

	/* grab the mutex to protect few globals used by verifier */
	mutex_lock(&bpf_verifier_lock);

	log_level = 0;

	env->strict_alignment = false;
	if (!IS_ENABLED(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS))
		env->strict_alignment = true;

	env->explored_states = kcalloc(env->prog->len,
				       sizeof(struct bpf_verifier_state_list *),
				       GFP_KERNEL);
	ret = -ENOMEM;
	if (!env->explored_states)
		goto skip_full_check;

	ret = check_cfg(env);
	if (ret < 0)
		goto skip_full_check;

	env->allow_ptr_leaks = capable(CAP_SYS_ADMIN);

	ret = do_check(env);

skip_full_check:
	while (pop_stack(env, NULL) >= 0);
	free_states(env);

	mutex_unlock(&bpf_verifier_lock);
	vfree(env->insn_aux_data);
err_free_env:
	kfree(env);
	return ret;
}
EXPORT_SYMBOL_GPL(bpf_analyzer);
