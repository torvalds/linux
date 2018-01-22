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

#include <linux/bpf.h>

#include "disasm.h"

#define __BPF_FUNC_STR_FN(x) [BPF_FUNC_ ## x] = __stringify(bpf_ ## x)
static const char * const func_id_str[] = {
	__BPF_FUNC_MAPPER(__BPF_FUNC_STR_FN)
};
#undef __BPF_FUNC_STR_FN

const char *func_id_name(int id)
{
	BUILD_BUG_ON(ARRAY_SIZE(func_id_str) != __BPF_FUNC_MAX_ID);

	if (id >= 0 && id < __BPF_FUNC_MAX_ID && func_id_str[id])
		return func_id_str[id];
	else
		return "unknown";
}

const char *const bpf_class_string[8] = {
	[BPF_LD]    = "ld",
	[BPF_LDX]   = "ldx",
	[BPF_ST]    = "st",
	[BPF_STX]   = "stx",
	[BPF_ALU]   = "alu",
	[BPF_JMP]   = "jmp",
	[BPF_RET]   = "BUG",
	[BPF_ALU64] = "alu64",
};

const char *const bpf_alu_string[16] = {
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
	[BPF_JLT >> 4]  = "<",
	[BPF_JGE >> 4]  = ">=",
	[BPF_JLE >> 4]  = "<=",
	[BPF_JSET >> 4] = "&",
	[BPF_JNE >> 4]  = "!=",
	[BPF_JSGT >> 4] = "s>",
	[BPF_JSLT >> 4] = "s<",
	[BPF_JSGE >> 4] = "s>=",
	[BPF_JSLE >> 4] = "s<=",
	[BPF_CALL >> 4] = "call",
	[BPF_EXIT >> 4] = "exit",
};

static void print_bpf_end_insn(bpf_insn_print_cb verbose,
			       struct bpf_verifier_env *env,
			       const struct bpf_insn *insn)
{
	verbose(env, "(%02x) r%d = %s%d r%d\n", insn->code, insn->dst_reg,
		BPF_SRC(insn->code) == BPF_TO_BE ? "be" : "le",
		insn->imm, insn->dst_reg);
}

void print_bpf_insn(bpf_insn_print_cb verbose, struct bpf_verifier_env *env,
		    const struct bpf_insn *insn, bool allow_ptr_leaks)
{
	u8 class = BPF_CLASS(insn->code);

	if (class == BPF_ALU || class == BPF_ALU64) {
		if (BPF_OP(insn->code) == BPF_END) {
			if (class == BPF_ALU64)
				verbose(env, "BUG_alu64_%02x\n", insn->code);
			else
				print_bpf_end_insn(verbose, env, insn);
		} else if (BPF_OP(insn->code) == BPF_NEG) {
			verbose(env, "(%02x) r%d = %s-r%d\n",
				insn->code, insn->dst_reg,
				class == BPF_ALU ? "(u32) " : "",
				insn->dst_reg);
		} else if (BPF_SRC(insn->code) == BPF_X) {
			verbose(env, "(%02x) %sr%d %s %sr%d\n",
				insn->code, class == BPF_ALU ? "(u32) " : "",
				insn->dst_reg,
				bpf_alu_string[BPF_OP(insn->code) >> 4],
				class == BPF_ALU ? "(u32) " : "",
				insn->src_reg);
		} else {
			verbose(env, "(%02x) %sr%d %s %s%d\n",
				insn->code, class == BPF_ALU ? "(u32) " : "",
				insn->dst_reg,
				bpf_alu_string[BPF_OP(insn->code) >> 4],
				class == BPF_ALU ? "(u32) " : "",
				insn->imm);
		}
	} else if (class == BPF_STX) {
		if (BPF_MODE(insn->code) == BPF_MEM)
			verbose(env, "(%02x) *(%s *)(r%d %+d) = r%d\n",
				insn->code,
				bpf_ldst_string[BPF_SIZE(insn->code) >> 3],
				insn->dst_reg,
				insn->off, insn->src_reg);
		else if (BPF_MODE(insn->code) == BPF_XADD)
			verbose(env, "(%02x) lock *(%s *)(r%d %+d) += r%d\n",
				insn->code,
				bpf_ldst_string[BPF_SIZE(insn->code) >> 3],
				insn->dst_reg, insn->off,
				insn->src_reg);
		else
			verbose(env, "BUG_%02x\n", insn->code);
	} else if (class == BPF_ST) {
		if (BPF_MODE(insn->code) != BPF_MEM) {
			verbose(env, "BUG_st_%02x\n", insn->code);
			return;
		}
		verbose(env, "(%02x) *(%s *)(r%d %+d) = %d\n",
			insn->code,
			bpf_ldst_string[BPF_SIZE(insn->code) >> 3],
			insn->dst_reg,
			insn->off, insn->imm);
	} else if (class == BPF_LDX) {
		if (BPF_MODE(insn->code) != BPF_MEM) {
			verbose(env, "BUG_ldx_%02x\n", insn->code);
			return;
		}
		verbose(env, "(%02x) r%d = *(%s *)(r%d %+d)\n",
			insn->code, insn->dst_reg,
			bpf_ldst_string[BPF_SIZE(insn->code) >> 3],
			insn->src_reg, insn->off);
	} else if (class == BPF_LD) {
		if (BPF_MODE(insn->code) == BPF_ABS) {
			verbose(env, "(%02x) r0 = *(%s *)skb[%d]\n",
				insn->code,
				bpf_ldst_string[BPF_SIZE(insn->code) >> 3],
				insn->imm);
		} else if (BPF_MODE(insn->code) == BPF_IND) {
			verbose(env, "(%02x) r0 = *(%s *)skb[r%d + %d]\n",
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

			if (map_ptr && !allow_ptr_leaks)
				imm = 0;

			verbose(env, "(%02x) r%d = 0x%llx\n", insn->code,
				insn->dst_reg, (unsigned long long)imm);
		} else {
			verbose(env, "BUG_ld_%02x\n", insn->code);
			return;
		}
	} else if (class == BPF_JMP) {
		u8 opcode = BPF_OP(insn->code);

		if (opcode == BPF_CALL) {
			verbose(env, "(%02x) call %s#%d\n", insn->code,
				func_id_name(insn->imm), insn->imm);
		} else if (insn->code == (BPF_JMP | BPF_JA)) {
			verbose(env, "(%02x) goto pc%+d\n",
				insn->code, insn->off);
		} else if (insn->code == (BPF_JMP | BPF_EXIT)) {
			verbose(env, "(%02x) exit\n", insn->code);
		} else if (BPF_SRC(insn->code) == BPF_X) {
			verbose(env, "(%02x) if r%d %s r%d goto pc%+d\n",
				insn->code, insn->dst_reg,
				bpf_jmp_string[BPF_OP(insn->code) >> 4],
				insn->src_reg, insn->off);
		} else {
			verbose(env, "(%02x) if r%d %s 0x%x goto pc%+d\n",
				insn->code, insn->dst_reg,
				bpf_jmp_string[BPF_OP(insn->code) >> 4],
				insn->imm, insn->off);
		}
	} else {
		verbose(env, "(%02x) %s\n",
			insn->code, bpf_class_string[class]);
	}
}
