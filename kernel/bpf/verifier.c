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

#define MAX_USED_MAPS 64 /* max number of maps accessed by one eBPF program */

/* single container for all structs
 * one verifier_env per bpf_check() call
 */
struct verifier_env {
	struct bpf_prog *prog;		/* eBPF program being verified */
	struct bpf_map *used_maps[MAX_USED_MAPS]; /* array of map's used by eBPF program */
	u32 used_map_cnt;		/* number of used maps */
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
static void verbose(const char *fmt, ...)
{
	va_list args;

	if (log_level == 0 || log_len >= log_size - 1)
		return;

	va_start(args, fmt);
	log_len += vscnprintf(log_buf + log_len, log_size - log_len, fmt, args);
	va_end(args);
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

static const char *const bpf_alu_string[] = {
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

static const char *const bpf_jmp_string[] = {
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

/* return the map pointer stored inside BPF_LD_IMM64 instruction */
static struct bpf_map *ld_imm64_to_map_ptr(struct bpf_insn *insn)
{
	u64 imm64 = ((u64) (u32) insn[0].imm) | ((u64) (u32) insn[1].imm) << 32;

	return (struct bpf_map *) (unsigned long) imm64;
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

			map = bpf_map_get(f);
			if (IS_ERR(map)) {
				verbose("fd %d is not pointing to valid bpf_map\n",
					insn->imm);
				fdput(f);
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

			/* remember this map */
			env->used_maps[env->used_map_cnt++] = map;

			/* hold the map. If the program is rejected by verifier,
			 * the map will be released by release_maps() or it
			 * will be used by the valid program until it's unloaded
			 * and all maps are released in free_bpf_prog_info()
			 */
			atomic_inc(&map->refcnt);

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

int bpf_check(struct bpf_prog *prog, union bpf_attr *attr)
{
	char __user *log_ubuf = NULL;
	struct verifier_env *env;
	int ret = -EINVAL;

	if (prog->len <= 0 || prog->len > BPF_MAXINSNS)
		return -E2BIG;

	/* 'struct verifier_env' can be global, but since it's not small,
	 * allocate/free it every time bpf_check() is called
	 */
	env = kzalloc(sizeof(struct verifier_env), GFP_KERNEL);
	if (!env)
		return -ENOMEM;

	env->prog = prog;

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

	/* ret = do_check(env); */

skip_full_check:

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
		prog->aux->used_maps = kmalloc_array(env->used_map_cnt,
						     sizeof(env->used_maps[0]),
						     GFP_KERNEL);

		if (!prog->aux->used_maps) {
			ret = -ENOMEM;
			goto free_log_buf;
		}

		memcpy(prog->aux->used_maps, env->used_maps,
		       sizeof(env->used_maps[0]) * env->used_map_cnt);
		prog->aux->used_map_cnt = env->used_map_cnt;

		/* program is valid. Convert pseudo bpf_ld_imm64 into generic
		 * bpf_ld_imm64 instructions
		 */
		convert_pseudo_ld_imm64(env);
	}

free_log_buf:
	if (log_level)
		vfree(log_buf);
free_env:
	if (!prog->aux->used_maps)
		/* if we didn't copy map pointers into bpf_prog_info, release
		 * them now. Otherwise free_bpf_prog_info() will release them.
		 */
		release_maps(env);
	kfree(env);
	mutex_unlock(&bpf_verifier_lock);
	return ret;
}
