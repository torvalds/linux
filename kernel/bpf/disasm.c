// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (c) 2011-2014 PLUMgrid, http://plumgrid.com
 * Copyright (c) 2016 Facebook
 */

#include <linux/bpf.h>

#include "disasm.h"

#define __BPF_FUNC_STR_FN(x) [BPF_FUNC_ ## x] = __stringify(bpf_ ## x)
static const char * const func_id_str[] = {
	__BPF_FUNC_MAPPER(__BPF_FUNC_STR_FN)
};
#undef __BPF_FUNC_STR_FN

static const char *__func_get_name(const struct bpf_insn_cbs *cbs,
				   const struct bpf_insn *insn,
				   char *buff, size_t len)
{
	BUILD_BUG_ON(ARRAY_SIZE(func_id_str) != __BPF_FUNC_MAX_ID);

	if (!insn->src_reg &&
	    insn->imm >= 0 && insn->imm < __BPF_FUNC_MAX_ID &&
	    func_id_str[insn->imm])
		return func_id_str[insn->imm];

	if (cbs && cbs->cb_call) {
		const char *res;

		res = cbs->cb_call(cbs->private_data, insn);
		if (res)
			return res;
	}

	if (insn->src_reg == BPF_PSEUDO_CALL)
		snprintf(buff, len, "%+d", insn->imm);
	else if (insn->src_reg == BPF_PSEUDO_KFUNC_CALL)
		snprintf(buff, len, "kernel-function");

	return buff;
}

static const char *__func_imm_name(const struct bpf_insn_cbs *cbs,
				   const struct bpf_insn *insn,
				   u64 full_imm, char *buff, size_t len)
{
	if (cbs && cbs->cb_imm)
		return cbs->cb_imm(cbs->private_data, insn, full_imm);

	snprintf(buff, len, "0x%llx", (unsigned long long)full_imm);
	return buff;
}

const char *func_id_name(int id)
{
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
	[BPF_JMP32] = "jmp32",
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

static const char *const bpf_alu_sign_string[16] = {
	[BPF_DIV >> 4]  = "s/=",
	[BPF_MOD >> 4]  = "s%=",
};

static const char *const bpf_movsx_string[4] = {
	[0] = "(s8)",
	[1] = "(s16)",
	[3] = "(s32)",
};

static const char *const bpf_atomic_alu_string[16] = {
	[BPF_ADD >> 4]  = "add",
	[BPF_AND >> 4]  = "and",
	[BPF_OR >> 4]  = "or",
	[BPF_XOR >> 4]  = "xor",
};

static const char *const bpf_ldst_string[] = {
	[BPF_W >> 3]  = "u32",
	[BPF_H >> 3]  = "u16",
	[BPF_B >> 3]  = "u8",
	[BPF_DW >> 3] = "u64",
};

static const char *const bpf_ldsx_string[] = {
	[BPF_W >> 3]  = "s32",
	[BPF_H >> 3]  = "s16",
	[BPF_B >> 3]  = "s8",
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

static void print_bpf_end_insn(bpf_insn_print_t verbose,
			       void *private_data,
			       const struct bpf_insn *insn)
{
	verbose(private_data, "(%02x) r%d = %s%d r%d\n",
		insn->code, insn->dst_reg,
		BPF_SRC(insn->code) == BPF_TO_BE ? "be" : "le",
		insn->imm, insn->dst_reg);
}

static void print_bpf_bswap_insn(bpf_insn_print_t verbose,
			       void *private_data,
			       const struct bpf_insn *insn)
{
	verbose(private_data, "(%02x) r%d = bswap%d r%d\n",
		insn->code, insn->dst_reg,
		insn->imm, insn->dst_reg);
}

static bool is_sdiv_smod(const struct bpf_insn *insn)
{
	return (BPF_OP(insn->code)  == BPF_DIV || BPF_OP(insn->code) == BPF_MOD) &&
	       insn->off == 1;
}

static bool is_movsx(const struct bpf_insn *insn)
{
	return BPF_OP(insn->code) == BPF_MOV &&
	       (insn->off == 8 || insn->off == 16 || insn->off == 32);
}

static bool is_addr_space_cast(const struct bpf_insn *insn)
{
	return insn->code == (BPF_ALU64 | BPF_MOV | BPF_X) &&
		insn->off == BPF_ADDR_SPACE_CAST;
}

void print_bpf_insn(const struct bpf_insn_cbs *cbs,
		    const struct bpf_insn *insn,
		    bool allow_ptr_leaks)
{
	const bpf_insn_print_t verbose = cbs->cb_print;
	u8 class = BPF_CLASS(insn->code);

	if (class == BPF_ALU || class == BPF_ALU64) {
		if (BPF_OP(insn->code) == BPF_END) {
			if (class == BPF_ALU64)
				print_bpf_bswap_insn(verbose, cbs->private_data, insn);
			else
				print_bpf_end_insn(verbose, cbs->private_data, insn);
		} else if (BPF_OP(insn->code) == BPF_NEG) {
			verbose(cbs->private_data, "(%02x) %c%d = -%c%d\n",
				insn->code, class == BPF_ALU ? 'w' : 'r',
				insn->dst_reg, class == BPF_ALU ? 'w' : 'r',
				insn->dst_reg);
		} else if (is_addr_space_cast(insn)) {
			verbose(cbs->private_data, "(%02x) r%d = addr_space_cast(r%d, %d, %d)\n",
				insn->code, insn->dst_reg,
				insn->src_reg, ((u32)insn->imm) >> 16, (u16)insn->imm);
		} else if (BPF_SRC(insn->code) == BPF_X) {
			verbose(cbs->private_data, "(%02x) %c%d %s %s%c%d\n",
				insn->code, class == BPF_ALU ? 'w' : 'r',
				insn->dst_reg,
				is_sdiv_smod(insn) ? bpf_alu_sign_string[BPF_OP(insn->code) >> 4]
						   : bpf_alu_string[BPF_OP(insn->code) >> 4],
				is_movsx(insn) ? bpf_movsx_string[(insn->off >> 3) - 1] : "",
				class == BPF_ALU ? 'w' : 'r',
				insn->src_reg);
		} else {
			verbose(cbs->private_data, "(%02x) %c%d %s %d\n",
				insn->code, class == BPF_ALU ? 'w' : 'r',
				insn->dst_reg,
				is_sdiv_smod(insn) ? bpf_alu_sign_string[BPF_OP(insn->code) >> 4]
						   : bpf_alu_string[BPF_OP(insn->code) >> 4],
				insn->imm);
		}
	} else if (class == BPF_STX) {
		if (BPF_MODE(insn->code) == BPF_MEM)
			verbose(cbs->private_data, "(%02x) *(%s *)(r%d %+d) = r%d\n",
				insn->code,
				bpf_ldst_string[BPF_SIZE(insn->code) >> 3],
				insn->dst_reg,
				insn->off, insn->src_reg);
		else if (BPF_MODE(insn->code) == BPF_ATOMIC &&
			 (insn->imm == BPF_ADD || insn->imm == BPF_AND ||
			  insn->imm == BPF_OR || insn->imm == BPF_XOR)) {
			verbose(cbs->private_data, "(%02x) lock *(%s *)(r%d %+d) %s r%d\n",
				insn->code,
				bpf_ldst_string[BPF_SIZE(insn->code) >> 3],
				insn->dst_reg, insn->off,
				bpf_alu_string[BPF_OP(insn->imm) >> 4],
				insn->src_reg);
		} else if (BPF_MODE(insn->code) == BPF_ATOMIC &&
			   (insn->imm == (BPF_ADD | BPF_FETCH) ||
			    insn->imm == (BPF_AND | BPF_FETCH) ||
			    insn->imm == (BPF_OR | BPF_FETCH) ||
			    insn->imm == (BPF_XOR | BPF_FETCH))) {
			verbose(cbs->private_data, "(%02x) r%d = atomic%s_fetch_%s((%s *)(r%d %+d), r%d)\n",
				insn->code, insn->src_reg,
				BPF_SIZE(insn->code) == BPF_DW ? "64" : "",
				bpf_atomic_alu_string[BPF_OP(insn->imm) >> 4],
				bpf_ldst_string[BPF_SIZE(insn->code) >> 3],
				insn->dst_reg, insn->off, insn->src_reg);
		} else if (BPF_MODE(insn->code) == BPF_ATOMIC &&
			   insn->imm == BPF_CMPXCHG) {
			verbose(cbs->private_data, "(%02x) r0 = atomic%s_cmpxchg((%s *)(r%d %+d), r0, r%d)\n",
				insn->code,
				BPF_SIZE(insn->code) == BPF_DW ? "64" : "",
				bpf_ldst_string[BPF_SIZE(insn->code) >> 3],
				insn->dst_reg, insn->off,
				insn->src_reg);
		} else if (BPF_MODE(insn->code) == BPF_ATOMIC &&
			   insn->imm == BPF_XCHG) {
			verbose(cbs->private_data, "(%02x) r%d = atomic%s_xchg((%s *)(r%d %+d), r%d)\n",
				insn->code, insn->src_reg,
				BPF_SIZE(insn->code) == BPF_DW ? "64" : "",
				bpf_ldst_string[BPF_SIZE(insn->code) >> 3],
				insn->dst_reg, insn->off, insn->src_reg);
		} else {
			verbose(cbs->private_data, "BUG_%02x\n", insn->code);
		}
	} else if (class == BPF_ST) {
		if (BPF_MODE(insn->code) == BPF_MEM) {
			verbose(cbs->private_data, "(%02x) *(%s *)(r%d %+d) = %d\n",
				insn->code,
				bpf_ldst_string[BPF_SIZE(insn->code) >> 3],
				insn->dst_reg,
				insn->off, insn->imm);
		} else if (BPF_MODE(insn->code) == 0xc0 /* BPF_NOSPEC, no UAPI */) {
			verbose(cbs->private_data, "(%02x) nospec\n", insn->code);
		} else {
			verbose(cbs->private_data, "BUG_st_%02x\n", insn->code);
		}
	} else if (class == BPF_LDX) {
		if (BPF_MODE(insn->code) != BPF_MEM && BPF_MODE(insn->code) != BPF_MEMSX) {
			verbose(cbs->private_data, "BUG_ldx_%02x\n", insn->code);
			return;
		}
		verbose(cbs->private_data, "(%02x) r%d = *(%s *)(r%d %+d)\n",
			insn->code, insn->dst_reg,
			BPF_MODE(insn->code) == BPF_MEM ?
				 bpf_ldst_string[BPF_SIZE(insn->code) >> 3] :
				 bpf_ldsx_string[BPF_SIZE(insn->code) >> 3],
			insn->src_reg, insn->off);
	} else if (class == BPF_LD) {
		if (BPF_MODE(insn->code) == BPF_ABS) {
			verbose(cbs->private_data, "(%02x) r0 = *(%s *)skb[%d]\n",
				insn->code,
				bpf_ldst_string[BPF_SIZE(insn->code) >> 3],
				insn->imm);
		} else if (BPF_MODE(insn->code) == BPF_IND) {
			verbose(cbs->private_data, "(%02x) r0 = *(%s *)skb[r%d + %d]\n",
				insn->code,
				bpf_ldst_string[BPF_SIZE(insn->code) >> 3],
				insn->src_reg, insn->imm);
		} else if (BPF_MODE(insn->code) == BPF_IMM &&
			   BPF_SIZE(insn->code) == BPF_DW) {
			/* At this point, we already made sure that the second
			 * part of the ldimm64 insn is accessible.
			 */
			u64 imm = ((u64)(insn + 1)->imm << 32) | (u32)insn->imm;
			bool is_ptr = insn->src_reg == BPF_PSEUDO_MAP_FD ||
				      insn->src_reg == BPF_PSEUDO_MAP_VALUE;
			char tmp[64];

			if (is_ptr && !allow_ptr_leaks)
				imm = 0;

			verbose(cbs->private_data, "(%02x) r%d = %s\n",
				insn->code, insn->dst_reg,
				__func_imm_name(cbs, insn, imm,
						tmp, sizeof(tmp)));
		} else {
			verbose(cbs->private_data, "BUG_ld_%02x\n", insn->code);
			return;
		}
	} else if (class == BPF_JMP32 || class == BPF_JMP) {
		u8 opcode = BPF_OP(insn->code);

		if (opcode == BPF_CALL) {
			char tmp[64];

			if (insn->src_reg == BPF_PSEUDO_CALL) {
				verbose(cbs->private_data, "(%02x) call pc%s\n",
					insn->code,
					__func_get_name(cbs, insn,
							tmp, sizeof(tmp)));
			} else {
				strcpy(tmp, "unknown");
				verbose(cbs->private_data, "(%02x) call %s#%d\n", insn->code,
					__func_get_name(cbs, insn,
							tmp, sizeof(tmp)),
					insn->imm);
			}
		} else if (insn->code == (BPF_JMP | BPF_JA)) {
			verbose(cbs->private_data, "(%02x) goto pc%+d\n",
				insn->code, insn->off);
		} else if (insn->code == (BPF_JMP | BPF_JCOND) &&
			   insn->src_reg == BPF_MAY_GOTO) {
			verbose(cbs->private_data, "(%02x) may_goto pc%+d\n",
				insn->code, insn->off);
		} else if (insn->code == (BPF_JMP32 | BPF_JA)) {
			verbose(cbs->private_data, "(%02x) gotol pc%+d\n",
				insn->code, insn->imm);
		} else if (insn->code == (BPF_JMP | BPF_EXIT)) {
			verbose(cbs->private_data, "(%02x) exit\n", insn->code);
		} else if (BPF_SRC(insn->code) == BPF_X) {
			verbose(cbs->private_data,
				"(%02x) if %c%d %s %c%d goto pc%+d\n",
				insn->code, class == BPF_JMP32 ? 'w' : 'r',
				insn->dst_reg,
				bpf_jmp_string[BPF_OP(insn->code) >> 4],
				class == BPF_JMP32 ? 'w' : 'r',
				insn->src_reg, insn->off);
		} else {
			verbose(cbs->private_data,
				"(%02x) if %c%d %s 0x%x goto pc%+d\n",
				insn->code, class == BPF_JMP32 ? 'w' : 'r',
				insn->dst_reg,
				bpf_jmp_string[BPF_OP(insn->code) >> 4],
				insn->imm, insn->off);
		}
	} else {
		verbose(cbs->private_data, "(%02x) %s\n",
			insn->code, bpf_class_string[class]);
	}
}
