/* Copyright (c) 2011-2014 PLUMgrid, http://plumgrid.com
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
#include <linux/filter.h>
#include <net/netlink.h>
#include <linux/file.h>
#include <linux/vmalloc.h>

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
 * analysis is limited to 32k insn, which may be hit even if total number of
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

/* types of values stored in eBPF registers */
enum bpf_reg_type {
	NOT_INIT = 0,		 /* nothing was written into register */
	UNKNOWN_VALUE,		 /* reg doesn't contain a valid pointer */
	PTR_TO_CTX,		 /* reg points to bpf_context */
	CONST_PTR_TO_MAP,	 /* reg points to struct bpf_map */
	PTR_TO_MAP_VALUE,	 /* reg points to map element value */
	PTR_TO_MAP_VALUE_OR_NULL,/* points to map elem value or NULL */
	FRAME_PTR,		 /* reg == frame_pointer */
	PTR_TO_STACK,		 /* reg == frame_pointer + imm */
	CONST_IMM,		 /* constant integer value */
};

struct reg_state {
	enum bpf_reg_type type;
	union {
		/* valid when type == CONST_IMM | PTR_TO_STACK */
		int imm;

		/* valid when type == CONST_PTR_TO_MAP | PTR_TO_MAP_VALUE |
		 *   PTR_TO_MAP_VALUE_OR_NULL
		 */
		struct bpf_map *map_ptr;
	};
};

enum bpf_stack_slot_type {
	STACK_INVALID,    /* nothing was stored in this stack slot */
	STACK_SPILL,      /* register spilled into stack */
	STACK_MISC	  /* BPF program wrote some data into this slot */
};

#define BPF_REG_SIZE 8	/* size of eBPF register in bytes */

/* state of the program:
 * type of all registers and stack info
 */
struct verifier_state {
	struct reg_state regs[MAX_BPF_REG];
	u8 stack_slot_type[MAX_BPF_STACK];
	struct reg_state spilled_regs[MAX_BPF_STACK / BPF_REG_SIZE];
};

/* linked list of verifier states used to prune search */
struct verifier_state_list {
	struct verifier_state state;
	struct verifier_state_list *next;
};

/* verifier_state + insn_idx are pushed to stack when branch is encountered */
struct verifier_stack_elem {
	/* verifer state is 'st'
	 * before processing instruction 'insn_idx'
	 * and after processing instruction 'prev_insn_idx'
	 */
	struct verifier_state st;
	int insn_idx;
	int prev_insn_idx;
	struct verifier_stack_elem *next;
};

#define MAX_USED_MAPS 64 /* max number of maps accessed by one eBPF program */

/* single container for all structs
 * one verifier_env per bpf_check() call
 */
struct verifier_env {
	struct bpf_prog *prog;		/* eBPF program being verified */
	struct verifier_stack_elem *head; /* stack of verifier states to be processed */
	int stack_size;			/* number of states to be processed */
	struct verifier_state cur_state; /* current verifier state */
	struct verifier_state_list **explored_states; /* search pruning optimization */
	struct bpf_map *used_maps[MAX_USED_MAPS]; /* array of map's used by eBPF program */
	u32 used_map_cnt;		/* number of used maps */
	bool allow_ptr_leaks;
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
	[FRAME_PTR]		= "fp",
	[PTR_TO_STACK]		= "fp",
	[CONST_IMM]		= "imm",
};

static void print_verifier_state(struct verifier_env *env)
{
	enum bpf_reg_type t;
	int i;

	for (i = 0; i < MAX_BPF_REG; i++) {
		t = env->cur_state.regs[i].type;
		if (t == NOT_INIT)
			continue;
		verbose(" R%d=%s", i, reg_type_str[t]);
		if (t == CONST_IMM || t == PTR_TO_STACK)
			verbose("%d", env->cur_state.regs[i].imm);
		else if (t == CONST_PTR_TO_MAP || t == PTR_TO_MAP_VALUE ||
			 t == PTR_TO_MAP_VALUE_OR_NULL)
			verbose("(ks=%d,vs=%d)",
				env->cur_state.regs[i].map_ptr->key_size,
				env->cur_state.regs[i].map_ptr->value_size);
	}
	for (i = 0; i < MAX_BPF_STACK; i += BPF_REG_SIZE) {
		if (env->cur_state.stack_slot_type[i] == STACK_SPILL)
			verbose(" fp%d=%s", -MAX_BPF_STACK + i,
				reg_type_str[env->cur_state.spilled_regs[i / BPF_REG_SIZE].type]);
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

static void print_bpf_insn(struct bpf_insn *insn)
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
		} else if (BPF_MODE(insn->code) == BPF_IMM) {
			verbose("(%02x) r%d = 0x%x\n",
				insn->code, insn->dst_reg, insn->imm);
		} else {
			verbose("BUG_ld_%02x\n", insn->code);
			return;
		}
	} else if (class == BPF_JMP) {
		u8 opcode = BPF_OP(insn->code);

		if (opcode == BPF_CALL) {
			verbose("(%02x) call %d\n", insn->code, insn->imm);
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

static int pop_stack(struct verifier_env *env, int *prev_insn_idx)
{
	struct verifier_stack_elem *elem;
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

static struct verifier_state *push_stack(struct verifier_env *env, int insn_idx,
					 int prev_insn_idx)
{
	struct verifier_stack_elem *elem;

	elem = kmalloc(sizeof(struct verifier_stack_elem), GFP_KERNEL);
	if (!elem)
		goto err;

	memcpy(&elem->st, &env->cur_state, sizeof(env->cur_state));
	elem->insn_idx = insn_idx;
	elem->prev_insn_idx = prev_insn_idx;
	elem->next = env->head;
	env->head = elem;
	env->stack_size++;
	if (env->stack_size > 1024) {
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

static void init_reg_state(struct reg_state *regs)
{
	int i;

	for (i = 0; i < MAX_BPF_REG; i++) {
		regs[i].type = NOT_INIT;
		regs[i].imm = 0;
		regs[i].map_ptr = NULL;
	}

	/* frame pointer */
	regs[BPF_REG_FP].type = FRAME_PTR;

	/* 1st arg to a function */
	regs[BPF_REG_1].type = PTR_TO_CTX;
}

static void mark_reg_unknown_value(struct reg_state *regs, u32 regno)
{
	BUG_ON(regno >= MAX_BPF_REG);
	regs[regno].type = UNKNOWN_VALUE;
	regs[regno].imm = 0;
	regs[regno].map_ptr = NULL;
}

enum reg_arg_type {
	SRC_OP,		/* register is used as source operand */
	DST_OP,		/* register is used as destination operand */
	DST_OP_NO_MARK	/* same as above, check only, don't mark */
};

static int check_reg_arg(struct reg_state *regs, u32 regno,
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
	case PTR_TO_STACK:
	case PTR_TO_CTX:
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
static int check_stack_write(struct verifier_state *state, int off, int size,
			     int value_regno)
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
			(struct reg_state) {};

		for (i = 0; i < size; i++)
			state->stack_slot_type[MAX_BPF_STACK + off + i] = STACK_MISC;
	}
	return 0;
}

static int check_stack_read(struct verifier_state *state, int off, int size,
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
			mark_reg_unknown_value(state->regs, value_regno);
		return 0;
	}
}

/* check read/write into map element returned by bpf_map_lookup_elem() */
static int check_map_access(struct verifier_env *env, u32 regno, int off,
			    int size)
{
	struct bpf_map *map = env->cur_state.regs[regno].map_ptr;

	if (off < 0 || off + size > map->value_size) {
		verbose("invalid access to map value, value_size=%d off=%d size=%d\n",
			map->value_size, off, size);
		return -EACCES;
	}
	return 0;
}

/* check access to 'struct bpf_context' fields */
static int check_ctx_access(struct verifier_env *env, int off, int size,
			    enum bpf_access_type t)
{
	if (env->prog->aux->ops->is_valid_access &&
	    env->prog->aux->ops->is_valid_access(off, size, t))
		return 0;

	verbose("invalid bpf_context access off=%d size=%d\n", off, size);
	return -EACCES;
}

static bool is_pointer_value(struct verifier_env *env, int regno)
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

/* check whether memory at (regno + off) is accessible for t = (read | write)
 * if t==write, value_regno is a register which value is stored into memory
 * if t==read, value_regno is a register which will receive the value from memory
 * if t==write && value_regno==-1, some unknown value is stored into memory
 * if t==read && value_regno==-1, don't care what we read from memory
 */
static int check_mem_access(struct verifier_env *env, u32 regno, int off,
			    int bpf_size, enum bpf_access_type t,
			    int value_regno)
{
	struct verifier_state *state = &env->cur_state;
	int size, err = 0;

	if (state->regs[regno].type == PTR_TO_STACK)
		off += state->regs[regno].imm;

	size = bpf_size_to_bytes(bpf_size);
	if (size < 0)
		return size;

	if (off % size != 0) {
		verbose("misaligned access off %d size %d\n", off, size);
		return -EACCES;
	}

	if (state->regs[regno].type == PTR_TO_MAP_VALUE) {
		if (t == BPF_WRITE && value_regno >= 0 &&
		    is_pointer_value(env, value_regno)) {
			verbose("R%d leaks addr into map\n", value_regno);
			return -EACCES;
		}
		err = check_map_access(env, regno, off, size);
		if (!err && t == BPF_READ && value_regno >= 0)
			mark_reg_unknown_value(state->regs, value_regno);

	} else if (state->regs[regno].type == PTR_TO_CTX) {
		if (t == BPF_WRITE && value_regno >= 0 &&
		    is_pointer_value(env, value_regno)) {
			verbose("R%d leaks addr into ctx\n", value_regno);
			return -EACCES;
		}
		err = check_ctx_access(env, off, size, t);
		if (!err && t == BPF_READ && value_regno >= 0)
			mark_reg_unknown_value(state->regs, value_regno);

	} else if (state->regs[regno].type == FRAME_PTR ||
		   state->regs[regno].type == PTR_TO_STACK) {
		if (off >= 0 || off < -MAX_BPF_STACK) {
			verbose("invalid stack off=%d size=%d\n", off, size);
			return -EACCES;
		}
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
	} else {
		verbose("R%d invalid mem access '%s'\n",
			regno, reg_type_str[state->regs[regno].type]);
		return -EACCES;
	}
	return err;
}

static int check_xadd(struct verifier_env *env, struct bpf_insn *insn)
{
	struct reg_state *regs = env->cur_state.regs;
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

	if (is_pointer_value(env, insn->src_reg)) {
		verbose("R%d leaks addr into mem\n", insn->src_reg);
		return -EACCES;
	}

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
static int check_stack_boundary(struct verifier_env *env,
				int regno, int access_size)
{
	struct verifier_state *state = &env->cur_state;
	struct reg_state *regs = state->regs;
	int off, i;

	if (regs[regno].type != PTR_TO_STACK)
		return -EACCES;

	off = regs[regno].imm;
	if (off >= 0 || off < -MAX_BPF_STACK || off + access_size > 0 ||
	    access_size <= 0) {
		verbose("invalid stack type R%d off=%d access_size=%d\n",
			regno, off, access_size);
		return -EACCES;
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

static int check_func_arg(struct verifier_env *env, u32 regno,
			  enum bpf_arg_type arg_type, struct bpf_map **mapp)
{
	struct reg_state *reg = env->cur_state.regs + regno;
	enum bpf_reg_type expected_type;
	int err = 0;

	if (arg_type == ARG_DONTCARE)
		return 0;

	if (reg->type == NOT_INIT) {
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

	if (arg_type == ARG_PTR_TO_STACK || arg_type == ARG_PTR_TO_MAP_KEY ||
	    arg_type == ARG_PTR_TO_MAP_VALUE) {
		expected_type = PTR_TO_STACK;
	} else if (arg_type == ARG_CONST_STACK_SIZE) {
		expected_type = CONST_IMM;
	} else if (arg_type == ARG_CONST_MAP_PTR) {
		expected_type = CONST_PTR_TO_MAP;
	} else if (arg_type == ARG_PTR_TO_CTX) {
		expected_type = PTR_TO_CTX;
	} else {
		verbose("unsupported arg_type %d\n", arg_type);
		return -EFAULT;
	}

	if (reg->type != expected_type) {
		verbose("R%d type=%s expected=%s\n", regno,
			reg_type_str[reg->type], reg_type_str[expected_type]);
		return -EACCES;
	}

	if (arg_type == ARG_CONST_MAP_PTR) {
		/* bpf_map_xxx(map_ptr) call: remember that map_ptr */
		*mapp = reg->map_ptr;

	} else if (arg_type == ARG_PTR_TO_MAP_KEY) {
		/* bpf_map_xxx(..., map_ptr, ..., key) call:
		 * check that [key, key + map->key_size) are within
		 * stack limits and initialized
		 */
		if (!*mapp) {
			/* in function declaration map_ptr must come before
			 * map_key, so that it's verified and known before
			 * we have to check map_key here. Otherwise it means
			 * that kernel subsystem misconfigured verifier
			 */
			verbose("invalid map_ptr to access map->key\n");
			return -EACCES;
		}
		err = check_stack_boundary(env, regno, (*mapp)->key_size);

	} else if (arg_type == ARG_PTR_TO_MAP_VALUE) {
		/* bpf_map_xxx(..., map_ptr, ..., value) call:
		 * check [value, value + map->value_size) validity
		 */
		if (!*mapp) {
			/* kernel subsystem misconfigured verifier */
			verbose("invalid map_ptr to access map->value\n");
			return -EACCES;
		}
		err = check_stack_boundary(env, regno, (*mapp)->value_size);

	} else if (arg_type == ARG_CONST_STACK_SIZE) {
		/* bpf_xxx(..., buf, len) call will access 'len' bytes
		 * from stack pointer 'buf'. Check it
		 * note: regno == len, regno - 1 == buf
		 */
		if (regno == 0) {
			/* kernel subsystem misconfigured verifier */
			verbose("ARG_CONST_STACK_SIZE cannot be first argument\n");
			return -EACCES;
		}
		err = check_stack_boundary(env, regno - 1, reg->imm);
	}

	return err;
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
	default:
		break;
	}

	return 0;
error:
	verbose("cannot pass map_type %d into func %d\n",
		map->map_type, func_id);
	return -EINVAL;
}

static int check_call(struct verifier_env *env, int func_id)
{
	struct verifier_state *state = &env->cur_state;
	const struct bpf_func_proto *fn = NULL;
	struct reg_state *regs = state->regs;
	struct bpf_map *map = NULL;
	struct reg_state *reg;
	int i, err;

	/* find function prototype */
	if (func_id < 0 || func_id >= __BPF_FUNC_MAX_ID) {
		verbose("invalid func %d\n", func_id);
		return -EINVAL;
	}

	if (env->prog->aux->ops->get_func_proto)
		fn = env->prog->aux->ops->get_func_proto(func_id);

	if (!fn) {
		verbose("unknown func %d\n", func_id);
		return -EINVAL;
	}

	/* eBPF programs must be GPL compatible to use GPL-ed functions */
	if (!env->prog->gpl_compatible && fn->gpl_only) {
		verbose("cannot call GPL only function from proprietary program\n");
		return -EINVAL;
	}

	/* check args */
	err = check_func_arg(env, BPF_REG_1, fn->arg1_type, &map);
	if (err)
		return err;
	err = check_func_arg(env, BPF_REG_2, fn->arg2_type, &map);
	if (err)
		return err;
	err = check_func_arg(env, BPF_REG_3, fn->arg3_type, &map);
	if (err)
		return err;
	err = check_func_arg(env, BPF_REG_4, fn->arg4_type, &map);
	if (err)
		return err;
	err = check_func_arg(env, BPF_REG_5, fn->arg5_type, &map);
	if (err)
		return err;

	/* reset caller saved regs */
	for (i = 0; i < CALLER_SAVED_REGS; i++) {
		reg = regs + caller_saved[i];
		reg->type = NOT_INIT;
		reg->imm = 0;
	}

	/* update return register */
	if (fn->ret_type == RET_INTEGER) {
		regs[BPF_REG_0].type = UNKNOWN_VALUE;
	} else if (fn->ret_type == RET_VOID) {
		regs[BPF_REG_0].type = NOT_INIT;
	} else if (fn->ret_type == RET_PTR_TO_MAP_VALUE_OR_NULL) {
		regs[BPF_REG_0].type = PTR_TO_MAP_VALUE_OR_NULL;
		/* remember map_ptr, so that check_map_access()
		 * can check 'value_size' boundary of memory access
		 * to map element returned from bpf_map_lookup_elem()
		 */
		if (map == NULL) {
			verbose("kernel subsystem misconfigured verifier\n");
			return -EINVAL;
		}
		regs[BPF_REG_0].map_ptr = map;
	} else {
		verbose("unknown return type %d of func %d\n",
			fn->ret_type, func_id);
		return -EINVAL;
	}

	err = check_map_func_compatibility(map, func_id);
	if (err)
		return err;

	return 0;
}

/* check validity of 32-bit and 64-bit arithmetic operations */
static int check_alu_op(struct verifier_env *env, struct bpf_insn *insn)
{
	struct reg_state *regs = env->cur_state.regs;
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
				regs[insn->dst_reg].type = UNKNOWN_VALUE;
				regs[insn->dst_reg].map_ptr = NULL;
			}
		} else {
			/* case: R = imm
			 * remember the value we stored into this reg
			 */
			regs[insn->dst_reg].type = CONST_IMM;
			regs[insn->dst_reg].imm = insn->imm;
		}

	} else if (opcode > BPF_END) {
		verbose("invalid BPF_ALU opcode %x\n", opcode);
		return -EINVAL;

	} else {	/* all other ALU ops: and, sub, xor, add, ... */

		bool stack_relative = false;

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

		/* pattern match 'bpf_add Rx, imm' instruction */
		if (opcode == BPF_ADD && BPF_CLASS(insn->code) == BPF_ALU64 &&
		    regs[insn->dst_reg].type == FRAME_PTR &&
		    BPF_SRC(insn->code) == BPF_K) {
			stack_relative = true;
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

		/* check dest operand */
		err = check_reg_arg(regs, insn->dst_reg, DST_OP);
		if (err)
			return err;

		if (stack_relative) {
			regs[insn->dst_reg].type = PTR_TO_STACK;
			regs[insn->dst_reg].imm = insn->imm;
		}
	}

	return 0;
}

static int check_cond_jmp_op(struct verifier_env *env,
			     struct bpf_insn *insn, int *insn_idx)
{
	struct reg_state *regs = env->cur_state.regs;
	struct verifier_state *other_branch;
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

	/* detect if R == 0 where R was initialized to zero earlier */
	if (BPF_SRC(insn->code) == BPF_K &&
	    (opcode == BPF_JEQ || opcode == BPF_JNE) &&
	    regs[insn->dst_reg].type == CONST_IMM &&
	    regs[insn->dst_reg].imm == insn->imm) {
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

	/* detect if R == 0 where R is returned value from bpf_map_lookup_elem() */
	if (BPF_SRC(insn->code) == BPF_K &&
	    insn->imm == 0 && (opcode == BPF_JEQ ||
			       opcode == BPF_JNE) &&
	    regs[insn->dst_reg].type == PTR_TO_MAP_VALUE_OR_NULL) {
		if (opcode == BPF_JEQ) {
			/* next fallthrough insn can access memory via
			 * this register
			 */
			regs[insn->dst_reg].type = PTR_TO_MAP_VALUE;
			/* branch targer cannot access it, since reg == 0 */
			other_branch->regs[insn->dst_reg].type = CONST_IMM;
			other_branch->regs[insn->dst_reg].imm = 0;
		} else {
			other_branch->regs[insn->dst_reg].type = PTR_TO_MAP_VALUE;
			regs[insn->dst_reg].type = CONST_IMM;
			regs[insn->dst_reg].imm = 0;
		}
	} else if (is_pointer_value(env, insn->dst_reg)) {
		verbose("R%d pointer comparison prohibited\n", insn->dst_reg);
		return -EACCES;
	} else if (BPF_SRC(insn->code) == BPF_K &&
		   (opcode == BPF_JEQ || opcode == BPF_JNE)) {

		if (opcode == BPF_JEQ) {
			/* detect if (R == imm) goto
			 * and in the target state recognize that R = imm
			 */
			other_branch->regs[insn->dst_reg].type = CONST_IMM;
			other_branch->regs[insn->dst_reg].imm = insn->imm;
		} else {
			/* detect if (R != imm) goto
			 * and in the fall-through state recognize that R = imm
			 */
			regs[insn->dst_reg].type = CONST_IMM;
			regs[insn->dst_reg].imm = insn->imm;
		}
	}
	if (log_level)
		print_verifier_state(env);
	return 0;
}

/* return the map pointer stored inside BPF_LD_IMM64 instruction */
static struct bpf_map *ld_imm64_to_map_ptr(struct bpf_insn *insn)
{
	u64 imm64 = ((u64) (u32) insn[0].imm) | ((u64) (u32) insn[1].imm) << 32;

	return (struct bpf_map *) (unsigned long) imm64;
}

/* verify BPF_LD_IMM64 instruction */
static int check_ld_imm(struct verifier_env *env, struct bpf_insn *insn)
{
	struct reg_state *regs = env->cur_state.regs;
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

	if (insn->src_reg == 0)
		/* generic move 64-bit immediate into a register */
		return 0;

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
static int check_ld_abs(struct verifier_env *env, struct bpf_insn *insn)
{
	struct reg_state *regs = env->cur_state.regs;
	u8 mode = BPF_MODE(insn->code);
	struct reg_state *reg;
	int i, err;

	if (!may_access_skb(env->prog->type)) {
		verbose("BPF_LD_ABS|IND instructions not allowed for this program type\n");
		return -EINVAL;
	}

	if (insn->dst_reg != BPF_REG_0 || insn->off != 0 ||
	    BPF_SIZE(insn->code) == BPF_DW ||
	    (mode == BPF_ABS && insn->src_reg != BPF_REG_0)) {
		verbose("BPF_LD_ABS uses reserved fields\n");
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
	for (i = 0; i < CALLER_SAVED_REGS; i++) {
		reg = regs + caller_saved[i];
		reg->type = NOT_INIT;
		reg->imm = 0;
	}

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

#define STATE_LIST_MARK ((struct verifier_state_list *) -1L)

static int *insn_stack;	/* stack of insns to process */
static int cur_stack;	/* current stack index */
static int *insn_state;

/* t, w, e - match pseudo-code above:
 * t - index of current instruction
 * w - next instruction
 * e - edge
 */
static int push_insn(int t, int w, int e, struct verifier_env *env)
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
static int check_cfg(struct verifier_env *env)
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
static bool states_equal(struct verifier_state *old, struct verifier_state *cur)
{
	int i;

	for (i = 0; i < MAX_BPF_REG; i++) {
		if (memcmp(&old->regs[i], &cur->regs[i],
			   sizeof(old->regs[0])) != 0) {
			if (old->regs[i].type == NOT_INIT ||
			    (old->regs[i].type == UNKNOWN_VALUE &&
			     cur->regs[i].type != NOT_INIT))
				continue;
			return false;
		}
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
			 * (struct reg_state) {.type = PTR_TO_STACK, .imm = -8}
			 * but current path has stored:
			 * (struct reg_state) {.type = PTR_TO_STACK, .imm = -16}
			 * such verifier states are not equivalent.
			 * return false to continue verification of this path
			 */
			return false;
		else
			continue;
	}
	return true;
}

static int is_state_visited(struct verifier_env *env, int insn_idx)
{
	struct verifier_state_list *new_sl;
	struct verifier_state_list *sl;

	sl = env->explored_states[insn_idx];
	if (!sl)
		/* this 'insn_idx' instruction wasn't marked, so we will not
		 * be doing state search here
		 */
		return 0;

	while (sl != STATE_LIST_MARK) {
		if (states_equal(&sl->state, &env->cur_state))
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
	new_sl = kmalloc(sizeof(struct verifier_state_list), GFP_USER);
	if (!new_sl)
		return -ENOMEM;

	/* add new state to the head of linked list */
	memcpy(&new_sl->state, &env->cur_state, sizeof(env->cur_state));
	new_sl->next = env->explored_states[insn_idx];
	env->explored_states[insn_idx] = new_sl;
	return 0;
}

static int do_check(struct verifier_env *env)
{
	struct verifier_state *state = &env->cur_state;
	struct bpf_insn *insns = env->prog->insnsi;
	struct reg_state *regs = state->regs;
	int insn_cnt = env->prog->len;
	int insn_idx, prev_insn_idx = 0;
	int insn_processed = 0;
	bool do_print_state = false;

	init_reg_state(regs);
	insn_idx = 0;
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

		if (++insn_processed > 32768) {
			verbose("BPF program is too large. Proccessed %d insn\n",
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

		if (log_level && do_print_state) {
			verbose("\nfrom %d to %d:", prev_insn_idx, insn_idx);
			print_verifier_state(env);
			do_print_state = false;
		}

		if (log_level) {
			verbose("%d: ", insn_idx);
			print_bpf_insn(insn);
		}

		if (class == BPF_ALU || class == BPF_ALU64) {
			err = check_alu_op(env, insn);
			if (err)
				return err;

		} else if (class == BPF_LDX) {
			enum bpf_reg_type src_reg_type;

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

			if (BPF_SIZE(insn->code) != BPF_W) {
				insn_idx++;
				continue;
			}

			if (insn->imm == 0) {
				/* saw a valid insn
				 * dst_reg = *(u32 *)(src_reg + off)
				 * use reserved 'imm' field to mark this insn
				 */
				insn->imm = src_reg_type;

			} else if (src_reg_type != insn->imm &&
				   (src_reg_type == PTR_TO_CTX ||
				    insn->imm == PTR_TO_CTX)) {
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
			enum bpf_reg_type dst_reg_type;

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

			if (insn->imm == 0) {
				insn->imm = dst_reg_type;
			} else if (dst_reg_type != insn->imm &&
				   (dst_reg_type == PTR_TO_CTX ||
				    insn->imm == PTR_TO_CTX)) {
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

				err = check_call(env, insn->imm);
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
		} else {
			verbose("unknown insn class %d\n", class);
			return -EINVAL;
		}

		insn_idx++;
	}

	return 0;
}

/* look for pseudo eBPF instructions that access map FDs and
 * replace them with actual map pointers
 */
static int replace_map_fd_with_map_ptr(struct verifier_env *env)
{
	struct bpf_insn *insn = env->prog->insnsi;
	int insn_cnt = env->prog->len;
	int i, j;

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
static void release_maps(struct verifier_env *env)
{
	int i;

	for (i = 0; i < env->used_map_cnt; i++)
		bpf_map_put(env->used_maps[i]);
}

/* convert pseudo BPF_LD_IMM64 into generic BPF_LD_IMM64 */
static void convert_pseudo_ld_imm64(struct verifier_env *env)
{
	struct bpf_insn *insn = env->prog->insnsi;
	int insn_cnt = env->prog->len;
	int i;

	for (i = 0; i < insn_cnt; i++, insn++)
		if (insn->code == (BPF_LD | BPF_IMM | BPF_DW))
			insn->src_reg = 0;
}

static void adjust_branches(struct bpf_prog *prog, int pos, int delta)
{
	struct bpf_insn *insn = prog->insnsi;
	int insn_cnt = prog->len;
	int i;

	for (i = 0; i < insn_cnt; i++, insn++) {
		if (BPF_CLASS(insn->code) != BPF_JMP ||
		    BPF_OP(insn->code) == BPF_CALL ||
		    BPF_OP(insn->code) == BPF_EXIT)
			continue;

		/* adjust offset of jmps if necessary */
		if (i < pos && i + insn->off + 1 > pos)
			insn->off += delta;
		else if (i > pos + delta && i + insn->off + 1 <= pos + delta)
			insn->off -= delta;
	}
}

/* convert load instructions that access fields of 'struct __sk_buff'
 * into sequence of instructions that access fields of 'struct sk_buff'
 */
static int convert_ctx_accesses(struct verifier_env *env)
{
	struct bpf_insn *insn = env->prog->insnsi;
	int insn_cnt = env->prog->len;
	struct bpf_insn insn_buf[16];
	struct bpf_prog *new_prog;
	u32 cnt;
	int i;
	enum bpf_access_type type;

	if (!env->prog->aux->ops->convert_ctx_access)
		return 0;

	for (i = 0; i < insn_cnt; i++, insn++) {
		if (insn->code == (BPF_LDX | BPF_MEM | BPF_W))
			type = BPF_READ;
		else if (insn->code == (BPF_STX | BPF_MEM | BPF_W))
			type = BPF_WRITE;
		else
			continue;

		if (insn->imm != PTR_TO_CTX) {
			/* clear internal mark */
			insn->imm = 0;
			continue;
		}

		cnt = env->prog->aux->ops->
			convert_ctx_access(type, insn->dst_reg, insn->src_reg,
					   insn->off, insn_buf, env->prog);
		if (cnt == 0 || cnt >= ARRAY_SIZE(insn_buf)) {
			verbose("bpf verifier is misconfigured\n");
			return -EINVAL;
		}

		if (cnt == 1) {
			memcpy(insn, insn_buf, sizeof(*insn));
			continue;
		}

		/* several new insns need to be inserted. Make room for them */
		insn_cnt += cnt - 1;
		new_prog = bpf_prog_realloc(env->prog,
					    bpf_prog_size(insn_cnt),
					    GFP_USER);
		if (!new_prog)
			return -ENOMEM;

		new_prog->len = insn_cnt;

		memmove(new_prog->insnsi + i + cnt, new_prog->insns + i + 1,
			sizeof(*insn) * (insn_cnt - i - cnt));

		/* copy substitute insns in place of load instruction */
		memcpy(new_prog->insnsi + i, insn_buf, sizeof(*insn) * cnt);

		/* adjust branches in the whole program */
		adjust_branches(new_prog, i, cnt - 1);

		/* keep walking new program and skip insns we just inserted */
		env->prog = new_prog;
		insn = new_prog->insnsi + i + cnt - 1;
		i += cnt - 1;
	}

	return 0;
}

static void free_states(struct verifier_env *env)
{
	struct verifier_state_list *sl, *sln;
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
	struct verifier_env *env;
	int ret = -EINVAL;

	if ((*prog)->len <= 0 || (*prog)->len > BPF_MAXINSNS)
		return -E2BIG;

	/* 'struct verifier_env' can be global, but since it's not small,
	 * allocate/free it every time bpf_check() is called
	 */
	env = kzalloc(sizeof(struct verifier_env), GFP_KERNEL);
	if (!env)
		return -ENOMEM;

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
			goto free_env;

		ret = -ENOMEM;
		log_buf = vmalloc(log_size);
		if (!log_buf)
			goto free_env;
	} else {
		log_level = 0;
	}

	ret = replace_map_fd_with_map_ptr(env);
	if (ret < 0)
		goto skip_full_check;

	env->explored_states = kcalloc(env->prog->len,
				       sizeof(struct verifier_state_list *),
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
free_env:
	if (!env->prog->aux->used_maps)
		/* if we didn't copy map pointers into bpf_prog_info, release
		 * them now. Otherwise free_bpf_prog_info() will release them.
		 */
		release_maps(env);
	*prog = env->prog;
	kfree(env);
	mutex_unlock(&bpf_verifier_lock);
	return ret;
}
