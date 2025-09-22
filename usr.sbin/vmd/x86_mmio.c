/*	$OpenBSD: x86_mmio.c,v 1.1 2024/07/10 10:41:19 dv Exp $	*/
/*
 * Copyright (c) 2022 Dave Voutila <dv@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <errno.h>
#include <string.h>

#include <sys/types.h>
#include <machine/specialreg.h>

#include "vmd.h"
#include "mmio.h"

#define MMIO_DEBUG 0

extern char* __progname;

struct x86_decode_state {
	uint8_t	s_bytes[15];
	size_t	s_len;
	size_t	s_idx;
};

enum decode_result {
	DECODE_ERROR = 0,	/* Something went wrong. */
	DECODE_DONE,		/* Decode success and no more work needed. */
	DECODE_MORE,		/* Decode success and more work required. */
};

static const char *str_cpu_mode(int);
static const char *str_decode_res(enum decode_result);
static const char *str_opcode(struct x86_opcode *);
static const char *str_operand_enc(struct x86_opcode *);
static const char *str_reg(int);
static const char *str_sreg(int);
static int detect_cpu_mode(struct vcpu_reg_state *);

static enum decode_result decode_prefix(struct x86_decode_state *,
    struct x86_insn *);
static enum decode_result decode_opcode(struct x86_decode_state *,
    struct x86_insn *);
static enum decode_result decode_modrm(struct x86_decode_state *,
    struct x86_insn *);
static int get_modrm_reg(struct x86_insn *);
static int get_modrm_addr(struct x86_insn *, struct vcpu_reg_state *vrs);
static enum decode_result decode_disp(struct x86_decode_state *,
    struct x86_insn *);
static enum decode_result decode_sib(struct x86_decode_state *,
    struct x86_insn *);
static enum decode_result decode_imm(struct x86_decode_state *,
    struct x86_insn *);

static enum decode_result peek_byte(struct x86_decode_state *, uint8_t *);
static enum decode_result next_byte(struct x86_decode_state *, uint8_t *);
static enum decode_result next_value(struct x86_decode_state *, size_t,
    uint64_t *);
static int is_valid_state(struct x86_decode_state *, const char *);

static int emulate_mov(struct x86_insn *, struct vm_exit *);
static int emulate_movzx(struct x86_insn *, struct vm_exit *);

/* Lookup table for 1-byte opcodes, in opcode alphabetical order. */
const enum x86_opcode_type x86_1byte_opcode_tbl[255] = {
	/* MOV */
	[0x88] = OP_MOV,
	[0x89] = OP_MOV,
	[0x8A] = OP_MOV,
	[0x8B] = OP_MOV,
	[0x8C] = OP_MOV,
	[0xA0] = OP_MOV,
	[0xA1] = OP_MOV,
	[0xA2] = OP_MOV,
	[0xA3] = OP_MOV,

	/* MOVS */
	[0xA4] = OP_UNSUPPORTED,
	[0xA5] = OP_UNSUPPORTED,

	[ESCAPE] = OP_TWO_BYTE,
};

/* Lookup table for 1-byte operand encodings, in opcode alphabetical order. */
const enum x86_operand_enc x86_1byte_operand_enc_tbl[255] = {
	/* MOV */
	[0x88] = OP_ENC_MR,
	[0x89] = OP_ENC_MR,
	[0x8A] = OP_ENC_RM,
	[0x8B] = OP_ENC_RM,
	[0x8C] = OP_ENC_MR,
	[0xA0] = OP_ENC_FD,
	[0xA1] = OP_ENC_FD,
	[0xA2] = OP_ENC_TD,
	[0xA3] = OP_ENC_TD,

	/* MOVS */
	[0xA4] = OP_ENC_ZO,
	[0xA5] = OP_ENC_ZO,
};

const enum x86_opcode_type x86_2byte_opcode_tbl[255] = {
	/* MOVZX */
	[0xB6] = OP_MOVZX,
	[0xB7] = OP_MOVZX,
};

const enum x86_operand_enc x86_2byte_operand_enc_table[255] = {
	/* MOVZX */
	[0xB6] = OP_ENC_RM,
	[0xB7] = OP_ENC_RM,
};

/*
 * peek_byte
 *
 * Fetch the next byte fron the instruction bytes without advancing the
 * position in the stream.
 *
 * Return values:
 *  DECODE_DONE: byte was found and is the last in the stream
 *  DECODE_MORE: byte was found and there are more remaining to be read
 *  DECODE_ERROR: state is invalid and not byte was found, *byte left unchanged
 */
static enum decode_result
peek_byte(struct x86_decode_state *state, uint8_t *byte)
{
	enum decode_result res;

	if (state == NULL)
		return (DECODE_ERROR);

	if (state->s_idx == state->s_len)
		return (DECODE_ERROR);

	if (state->s_idx + 1 == state->s_len)
		res = DECODE_DONE;
	else
		res = DECODE_MORE;

	if (byte != NULL)
		*byte = state->s_bytes[state->s_idx];
	return (res);
}

/*
 * next_byte
 *
 * Fetch the next byte fron the instruction bytes, advancing the position in the
 * stream and mutating decode state.
 *
 * Return values:
 *  DECODE_DONE: byte was found and is the last in the stream
 *  DECODE_MORE: byte was found and there are more remaining to be read
 *  DECODE_ERROR: state is invalid and not byte was found, *byte left unchanged
 */
static enum decode_result
next_byte(struct x86_decode_state *state, uint8_t *byte)
{
	uint8_t next;

	/* Cheat and see if we're going to fail. */
	if (peek_byte(state, &next) == DECODE_ERROR)
		return (DECODE_ERROR);

	if (byte != NULL)
		*byte = next;
	state->s_idx++;

	return (state->s_idx < state->s_len ? DECODE_MORE : DECODE_DONE);
}

/*
 * Fetch the next `n' bytes as a single uint64_t value.
 */
static enum decode_result
next_value(struct x86_decode_state *state, size_t n, uint64_t *value)
{
	uint8_t bytes[8];
	size_t i;
	enum decode_result res;

	if (value == NULL)
		return (DECODE_ERROR);

	if (n == 0 || n > sizeof(bytes))
		return (DECODE_ERROR);

	memset(bytes, 0, sizeof(bytes));
	for (i = 0; i < n; i++)
		if ((res = next_byte(state, &bytes[i])) == DECODE_ERROR)
			return (DECODE_ERROR);

	*value = *((uint64_t*)bytes);

	return (res);
}

/*
 * is_valid_state
 *
 * Validate the decode state looks viable.
 *
 * Returns:
 *  1: if state is valid
 *  0: if an invariant is detected
 */
static int
is_valid_state(struct x86_decode_state *state, const char *fn_name)
{
	const char *s = (fn_name != NULL) ? fn_name : __func__;

	if (state == NULL) {
		log_warnx("%s: null state", s);
		return (0);
	}
	if (state->s_len > sizeof(state->s_bytes)) {
		log_warnx("%s: invalid length", s);
		return (0);
	}
	if (state->s_idx + 1 > state->s_len) {
		log_warnx("%s: invalid index", s);
		return (0);
	}

	return (1);
}

#ifdef MMIO_DEBUG
static void
dump_regs(struct vcpu_reg_state *vrs)
{
	size_t i;
	struct vcpu_segment_info *vsi;

	for (i = 0; i < VCPU_REGS_NGPRS; i++)
		log_info("%s: %s 0x%llx", __progname, str_reg(i),
		    vrs->vrs_gprs[i]);

	for (i = 0; i < VCPU_REGS_NSREGS; i++) {
		vsi = &vrs->vrs_sregs[i];
		log_info("%s: %s { sel: 0x%04x, lim: 0x%08x, ar: 0x%08x, "
		    "base: 0x%llx }", __progname, str_sreg(i),
		    vsi->vsi_sel, vsi->vsi_limit, vsi->vsi_ar, vsi->vsi_base);
	}
}

static void
dump_insn(struct x86_insn *insn)
{
	log_info("instruction { %s, enc=%s, len=%d, mod=0x%02x, ("
	    "reg=%s, addr=0x%lx) sib=0x%02x }",
	    str_opcode(&insn->insn_opcode),
	    str_operand_enc(&insn->insn_opcode), insn->insn_bytes_len,
	    insn->insn_modrm, str_reg(insn->insn_reg),
	    insn->insn_gva, insn->insn_sib);
}
#endif /* MMIO_DEBUG */

static const char *
str_cpu_mode(int mode)
{
	switch (mode) {
	case VMM_CPU_MODE_REAL: return "REAL";
	case VMM_CPU_MODE_PROT: return "PROT";
	case VMM_CPU_MODE_PROT32: return "PROT32";
	case VMM_CPU_MODE_COMPAT: return "COMPAT";
	case VMM_CPU_MODE_LONG: return "LONG";
	default: return "UKNOWN";
	}
}

__unused static const char *
str_decode_res(enum decode_result res) {
	switch (res) {
	case DECODE_DONE: return "DONE";
	case DECODE_MORE: return "MORE";
	case DECODE_ERROR: return "ERROR";
	default: return "UNKNOWN";
	}
}

static const char *
str_opcode(struct x86_opcode *opcode)
{
	switch (opcode->op_type) {
	case OP_IN: return "IN";
	case OP_INS: return "INS";
	case OP_MOV: return "MOV";
	case OP_MOVZX: return "MOVZX";
	case OP_OUT: return "OUT";
	case OP_OUTS: return "OUTS";
	case OP_UNSUPPORTED: return "UNSUPPORTED";
	default: return "UNKNOWN";
	}
}

static const char *
str_operand_enc(struct x86_opcode *opcode)
{
	switch (opcode->op_encoding) {
	case OP_ENC_I: return "I";
	case OP_ENC_MI: return "MI";
	case OP_ENC_MR: return "MR";
	case OP_ENC_RM: return "RM";
	case OP_ENC_FD: return "FD";
	case OP_ENC_TD: return "TD";
	case OP_ENC_OI: return "OI";
	case OP_ENC_ZO: return "ZO";
	default: return "UNKNOWN";
	}
}

static const char *
str_reg(int reg) {
	switch (reg) {
	case VCPU_REGS_RAX: return "RAX";
	case VCPU_REGS_RCX: return "RCX";
	case VCPU_REGS_RDX: return "RDX";
	case VCPU_REGS_RBX: return "RBX";
	case VCPU_REGS_RSI: return "RSI";
	case VCPU_REGS_RDI: return "RDI";
	case VCPU_REGS_R8:  return " R8";
	case VCPU_REGS_R9:  return " R9";
	case VCPU_REGS_R10: return "R10";
	case VCPU_REGS_R11: return "R11";
	case VCPU_REGS_R12: return "R12";
	case VCPU_REGS_R13: return "R13";
	case VCPU_REGS_R14: return "R14";
	case VCPU_REGS_R15: return "R15";
	case VCPU_REGS_RSP: return "RSP";
	case VCPU_REGS_RBP: return "RBP";
	case VCPU_REGS_RIP: return "RIP";
	case VCPU_REGS_RFLAGS: return "RFLAGS";
	default: return "UNKNOWN";
	}
}

static const char *
str_sreg(int sreg) {
	switch (sreg) {
	case VCPU_REGS_CS: return "CS";
	case VCPU_REGS_DS: return "DS";
	case VCPU_REGS_ES: return "ES";
	case VCPU_REGS_FS: return "FS";
	case VCPU_REGS_GS: return "GS";
	case VCPU_REGS_SS: return "GS";
	case VCPU_REGS_LDTR: return "LDTR";
	case VCPU_REGS_TR: return "TR";
	default: return "UKNOWN";
	}
}

static int
detect_cpu_mode(struct vcpu_reg_state *vrs)
{
	uint64_t cr0, cr4, cs, efer, rflags;

	/* Is protected mode enabled? */
	cr0 = vrs->vrs_crs[VCPU_REGS_CR0];
	if (!(cr0 & CR0_PE))
		return (VMM_CPU_MODE_REAL);

	cr4 = vrs->vrs_crs[VCPU_REGS_CR4];
	cs = vrs->vrs_sregs[VCPU_REGS_CS].vsi_ar;
	efer = vrs->vrs_msrs[VCPU_REGS_EFER];
	rflags = vrs->vrs_gprs[VCPU_REGS_RFLAGS];

	/* Check for Long modes. */
	if ((efer & EFER_LME) && (cr4 & CR4_PAE) && (cr0 & CR0_PG)) {
		if (cs & CS_L) {
			/* Long Modes */
			if (!(cs & CS_D))
				return (VMM_CPU_MODE_LONG);
			log_warnx("%s: invalid cpu mode", __progname);
			return (VMM_CPU_MODE_UNKNOWN);
		} else {
			/* Compatibility Modes */
			if (cs & CS_D) /* XXX Add Compat32 mode */
				return (VMM_CPU_MODE_UNKNOWN);
			return (VMM_CPU_MODE_COMPAT);
		}
	}

	/* Check for 32-bit Protected Mode. */
	if (cs & CS_D)
		return (VMM_CPU_MODE_PROT32);

	/* Check for virtual 8086 mode. */
	if (rflags & EFLAGS_VM) {
		/* XXX add Virtual8086 mode */
		log_warnx("%s: Virtual 8086 mode", __progname);
		return (VMM_CPU_MODE_UNKNOWN);
	}

	/* Can't determine mode. */
	log_warnx("%s: invalid cpu mode", __progname);
	return (VMM_CPU_MODE_UNKNOWN);
}

static enum decode_result
decode_prefix(struct x86_decode_state *state, struct x86_insn *insn)
{
	enum decode_result res = DECODE_ERROR;
	struct x86_prefix *prefix;
	uint8_t byte;

	if (!is_valid_state(state, __func__) || insn == NULL)
		return (-1);

	prefix = &insn->insn_prefix;
	memset(prefix, 0, sizeof(*prefix));

	/*
	 * Decode prefixes. The last of its kind wins. The behavior is undefined
	 * in the Intel SDM (see Vol 2, 2.1.1 Instruction Prefixes.)
	 */
	while ((res = peek_byte(state, &byte)) != DECODE_ERROR) {
		switch (byte) {
		case LEG_1_LOCK:
		case LEG_1_REPNE:
		case LEG_1_REP:
			prefix->pfx_group1 = byte;
			break;
		case LEG_2_CS:
		case LEG_2_SS:
		case LEG_2_DS:
		case LEG_2_ES:
		case LEG_2_FS:
		case LEG_2_GS:
			prefix->pfx_group2 = byte;
			break;
		case LEG_3_OPSZ:
			prefix->pfx_group3 = byte;
			break;
		case LEG_4_ADDRSZ:
			prefix->pfx_group4 = byte;
			break;
		case REX_BASE...REX_BASE + 0x0F:
			if (insn->insn_cpu_mode == VMM_CPU_MODE_LONG)
				prefix->pfx_rex = byte;
			else /* INC encountered */
				return (DECODE_ERROR);
			break;
		case VEX_2_BYTE:
		case VEX_3_BYTE:
			log_warnx("%s: VEX not supported", __func__);
			return (DECODE_ERROR);
		default:
			/* Something other than a valid prefix. */
			return (DECODE_MORE);
		}
		/* Advance our position. */
		next_byte(state, NULL);
	}

	return (res);
}

static enum decode_result
decode_modrm(struct x86_decode_state *state, struct x86_insn *insn)
{
	enum decode_result res;
	uint8_t byte = 0;

	if (!is_valid_state(state, __func__) || insn == NULL)
		return (DECODE_ERROR);

	insn->insn_modrm_valid = 0;

	/* Check the operand encoding to see if we fetch a byte or abort. */
	switch (insn->insn_opcode.op_encoding) {
	case OP_ENC_MR:
	case OP_ENC_RM:
	case OP_ENC_MI:
		res = next_byte(state, &byte);
		if (res == DECODE_ERROR) {
			log_warnx("%s: failed to get modrm byte", __func__);
			break;
		}
		insn->insn_modrm = byte;
		insn->insn_modrm_valid = 1;
		break;

	case OP_ENC_I:
	case OP_ENC_OI:
		log_warnx("%s: instruction does not need memory assist",
		    __func__);
		res = DECODE_ERROR;
		break;

	default:
		/* Peek to see if we're done decode. */
		res = peek_byte(state, NULL);
	}

	return (res);
}

static int
get_modrm_reg(struct x86_insn *insn)
{
	if (insn == NULL)
		return (-1);

	if (insn->insn_modrm_valid) {
		switch (MODRM_REGOP(insn->insn_modrm)) {
		case 0:
			insn->insn_reg = VCPU_REGS_RAX;
			break;
		case 1:
			insn->insn_reg = VCPU_REGS_RCX;
			break;
		case 2:
			insn->insn_reg = VCPU_REGS_RDX;
			break;
		case 3:
			insn->insn_reg = VCPU_REGS_RBX;
			break;
		case 4:
			insn->insn_reg = VCPU_REGS_RSP;
			break;
		case 5:
			insn->insn_reg = VCPU_REGS_RBP;
			break;
		case 6:
			insn->insn_reg = VCPU_REGS_RSI;
			break;
		case 7:
			insn->insn_reg = VCPU_REGS_RDI;
			break;
		}
	}

	/* REX R bit selects extended registers in LONG mode. */
	if (insn->insn_prefix.pfx_rex & REX_R)
		insn->insn_reg += 8;

	return (0);
}

static int
get_modrm_addr(struct x86_insn *insn, struct vcpu_reg_state *vrs)
{
	uint8_t mod, rm;
	vaddr_t addr = 0x0UL;

	if (insn == NULL || vrs == NULL)
		return (-1);

	if (insn->insn_modrm_valid) {
		rm = MODRM_RM(insn->insn_modrm);
		mod = MODRM_MOD(insn->insn_modrm);

		switch (rm) {
		case 0b000:
			addr = vrs->vrs_gprs[VCPU_REGS_RAX];
			break;
		case 0b001:
			addr = vrs->vrs_gprs[VCPU_REGS_RCX];
			break;
		case 0b010:
			addr = vrs->vrs_gprs[VCPU_REGS_RDX];
			break;
		case 0b011:
			addr = vrs->vrs_gprs[VCPU_REGS_RBX];
			break;
		case 0b100:
			if (mod == 0b11)
				addr = vrs->vrs_gprs[VCPU_REGS_RSP];
			break;
		case 0b101:
			if (mod != 0b00)
				addr = vrs->vrs_gprs[VCPU_REGS_RBP];
			break;
		case 0b110:
			addr = vrs->vrs_gprs[VCPU_REGS_RSI];
			break;
		case 0b111:
			addr = vrs->vrs_gprs[VCPU_REGS_RDI];
			break;
		}

		insn->insn_gva = addr;
	}

	return (0);
}

static enum decode_result
decode_disp(struct x86_decode_state *state, struct x86_insn *insn)
{
	enum decode_result res = DECODE_ERROR;
	uint64_t disp = 0;

	if (!is_valid_state(state, __func__) || insn == NULL)
		return (DECODE_ERROR);

	if (!insn->insn_modrm_valid)
		return (DECODE_ERROR);

	switch (MODRM_MOD(insn->insn_modrm)) {
	case 0x00:
		insn->insn_disp_type = DISP_0;
		res = DECODE_MORE;
		break;
	case 0x01:
		insn->insn_disp_type = DISP_1;
		res = next_value(state, 1, &disp);
		if (res == DECODE_ERROR)
			return (res);
		insn->insn_disp = disp;
		break;
	case 0x02:
		if (insn->insn_prefix.pfx_group4 == LEG_4_ADDRSZ) {
			insn->insn_disp_type = DISP_2;
			res = next_value(state, 2, &disp);
		} else {
			insn->insn_disp_type = DISP_4;
			res = next_value(state, 4, &disp);
		}
		if (res == DECODE_ERROR)
			return (res);
		insn->insn_disp = disp;
		break;
	default:
		insn->insn_disp_type = DISP_NONE;
		res = DECODE_MORE;
	}

	return (res);
}

static enum decode_result
decode_opcode(struct x86_decode_state *state, struct x86_insn *insn)
{
	enum decode_result res;
	enum x86_opcode_type type;
	enum x86_operand_enc enc;
	struct x86_opcode *opcode = &insn->insn_opcode;
	uint8_t byte, byte2;

	if (!is_valid_state(state, __func__) || insn == NULL)
		return (-1);

	memset(opcode, 0, sizeof(*opcode));

	res = next_byte(state, &byte);
	if (res == DECODE_ERROR)
		return (res);

	type = x86_1byte_opcode_tbl[byte];
	switch(type) {
	case OP_UNKNOWN:
	case OP_UNSUPPORTED:
		log_warnx("%s: unsupported opcode", __func__);
		return (DECODE_ERROR);

	case OP_TWO_BYTE:
		res = next_byte(state, &byte2);
		if (res == DECODE_ERROR)
			return (res);

		type = x86_2byte_opcode_tbl[byte2];
		if (type == OP_UNKNOWN || type == OP_UNSUPPORTED) {
			log_warnx("%s: unsupported 2-byte opcode", __func__);
			return (DECODE_ERROR);
		}

		opcode->op_bytes[0] = byte;
		opcode->op_bytes[1] = byte2;
		opcode->op_bytes_len = 2;
		enc = x86_2byte_operand_enc_table[byte2];
		break;

	default:
		/* We've potentially got a known 1-byte opcode. */
		opcode->op_bytes[0] = byte;
		opcode->op_bytes_len = 1;
		enc = x86_1byte_operand_enc_tbl[byte];
	}

	if (enc == OP_ENC_UNKNOWN)
		return (DECODE_ERROR);

	opcode->op_type = type;
	opcode->op_encoding = enc;

	return (res);
}

static enum decode_result
decode_sib(struct x86_decode_state *state, struct x86_insn *insn)
{
	enum decode_result res;
	uint8_t byte;

	if (!is_valid_state(state, __func__) || insn == NULL)
		return (-1);

	/* SIB is optional, so assume we will be continuing. */
	res = DECODE_MORE;

	insn->insn_sib_valid = 0;
	if (!insn->insn_modrm_valid)
		return (res);

	/* XXX is SIB valid in all cpu modes? */
	if (MODRM_RM(insn->insn_modrm) == 0b100) {
		res = next_byte(state, &byte);
		if (res != DECODE_ERROR) {
			insn->insn_sib_valid = 1;
			insn->insn_sib = byte;
		}
	}

	return (res);
}

static enum decode_result
decode_imm(struct x86_decode_state *state, struct x86_insn *insn)
{
	enum decode_result res;
	size_t num_bytes;
	uint64_t value;

	if (!is_valid_state(state, __func__) || insn == NULL)
		return (DECODE_ERROR);

	/* Only handle MI encoded instructions. Others shouldn't need assist. */
	if (insn->insn_opcode.op_encoding != OP_ENC_MI)
		return (DECODE_DONE);

	/* Exceptions related to MOV instructions. */
	if (insn->insn_opcode.op_type == OP_MOV) {
		switch (insn->insn_opcode.op_bytes[0]) {
		case 0xC6:
			num_bytes = 1;
			break;
		case 0xC7:
			if (insn->insn_cpu_mode == VMM_CPU_MODE_REAL)
				num_bytes = 2;
			else
				num_bytes = 4;
			break;
		default:
			log_warnx("%s: cannot decode immediate bytes for MOV",
			    __func__);
			return (DECODE_ERROR);
		}
	} else {
		/* Fallback to interpreting based on cpu mode and REX. */
		if (insn->insn_cpu_mode == VMM_CPU_MODE_REAL)
			num_bytes = 2;
		else if (insn->insn_prefix.pfx_rex == REX_NONE)
			num_bytes = 4;
		else
			num_bytes = 8;
	}

	res = next_value(state, num_bytes, &value);
	if (res != DECODE_ERROR) {
		insn->insn_immediate = value;
		insn->insn_immediate_len = num_bytes;
	}

	return (res);
}


/*
 * insn_decode
 *
 * Decode an x86 instruction from the provided instruction bytes.
 *
 * Return values:
 *  0: successful decode
 *  Non-zero: an exception occurred during decode
 */
int
insn_decode(struct vm_exit *exit, struct x86_insn *insn)
{
	enum decode_result res;
	struct vcpu_reg_state *vrs = &exit->vrs;
	struct x86_decode_state state;
	uint8_t *bytes, len;
	int mode;

	if (exit == NULL || insn == NULL) {
		log_warnx("%s: invalid input", __func__);
		return (DECODE_ERROR);
	}

	bytes = exit->vee.vee_insn_bytes;
	len = exit->vee.vee_insn_len;

	/* 0. Initialize state and instruction objects. */
	memset(insn, 0, sizeof(*insn));
	memset(&state, 0, sizeof(state));
	state.s_len = len;
	memcpy(&state.s_bytes, bytes, len);

	/* 1. Detect CPU mode. */
	mode = detect_cpu_mode(vrs);
	if (mode == VMM_CPU_MODE_UNKNOWN) {
		log_warnx("%s: failed to identify cpu mode", __func__);
#ifdef MMIO_DEBUG
		dump_regs(vrs);
#endif
		return (-1);
	}
	insn->insn_cpu_mode = mode;

#ifdef MMIO_DEBUG
	log_info("%s: cpu mode %s detected", __progname, str_cpu_mode(mode));
	printf("%s: got bytes: [ ", __progname);
	for (int i = 0; i < len; i++) {
		printf("%02x ", bytes[i]);
	}
	printf("]\n");
#endif
	/* 2. Decode prefixes. */
	res = decode_prefix(&state, insn);
	if (res == DECODE_ERROR) {
		log_warnx("%s: error decoding prefixes", __func__);
		goto err;
	} else if (res == DECODE_DONE)
		goto done;

#ifdef MMIO_DEBUG
	log_info("%s: prefixes {g1: 0x%02x, g2: 0x%02x, g3: 0x%02x, g4: 0x%02x,"
	    " rex: 0x%02x }", __progname, insn->insn_prefix.pfx_group1,
	    insn->insn_prefix.pfx_group2, insn->insn_prefix.pfx_group3,
	    insn->insn_prefix.pfx_group4, insn->insn_prefix.pfx_rex);
#endif

	/* 3. Pick apart opcode. Here we can start short-circuiting. */
	res = decode_opcode(&state, insn);
	if (res == DECODE_ERROR) {
		log_warnx("%s: error decoding opcode", __func__);
		goto err;
	} else if (res == DECODE_DONE)
		goto done;

#ifdef MMIO_DEBUG
	log_info("%s: found opcode %s (operand encoding %s) (%s)", __progname,
	    str_opcode(&insn->insn_opcode), str_operand_enc(&insn->insn_opcode),
	    str_decode_res(res));
#endif

	/* Process optional ModR/M byte. */
	res = decode_modrm(&state, insn);
	if (res == DECODE_ERROR) {
		log_warnx("%s: error decoding modrm", __func__);
		goto err;
	}
	if (get_modrm_addr(insn, vrs) != 0)
		goto err;
	if (get_modrm_reg(insn) != 0)
		goto err;
	if (res == DECODE_DONE)
		goto done;

#ifdef MMIO_DEBUG
	if (insn->insn_modrm_valid)
		log_info("%s: found ModRM 0x%02x (%s)", __progname,
		    insn->insn_modrm, str_decode_res(res));
#endif

	/* Process optional SIB byte. */
	res = decode_sib(&state, insn);
	if (res == DECODE_ERROR) {
		log_warnx("%s: error decoding sib", __func__);
		goto err;
	} else if (res == DECODE_DONE)
		goto done;

#ifdef MMIO_DEBUG
	if (insn->insn_sib_valid)
		log_info("%s: found SIB 0x%02x (%s)", __progname,
		    insn->insn_sib, str_decode_res(res));
#endif

	/* Process any Displacement bytes. */
	res = decode_disp(&state, insn);
	if (res == DECODE_ERROR) {
		log_warnx("%s: error decoding displacement", __func__);
		goto err;
	} else if (res == DECODE_DONE)
		goto done;

	/* Process any Immediate data bytes. */
	res = decode_imm(&state, insn);
	if (res == DECODE_ERROR) {
		log_warnx("%s: error decoding immediate bytes", __func__);
		goto err;
	}

done:
	insn->insn_bytes_len = state.s_idx;

#ifdef MMIO_DEBUG
	log_info("%s: final instruction length is %u", __func__,
		insn->insn_bytes_len);
	dump_insn(insn);
	log_info("%s: modrm: {mod: %d, regop: %d, rm: %d}", __func__,
	    MODRM_MOD(insn->insn_modrm), MODRM_REGOP(insn->insn_modrm),
	    MODRM_RM(insn->insn_modrm));
	dump_regs(vrs);
#endif /* MMIO_DEBUG */
	return (0);

err:
#ifdef MMIO_DEBUG
	dump_insn(insn);
	log_info("%s: modrm: {mod: %d, regop: %d, rm: %d}", __func__,
	    MODRM_MOD(insn->insn_modrm), MODRM_REGOP(insn->insn_modrm),
	    MODRM_RM(insn->insn_modrm));
	dump_regs(vrs);
#endif /* MMIO_DEBUG */
	return (-1);
}

static int
emulate_mov(struct x86_insn *insn, struct vm_exit *exit)
{
	/* XXX Only supports read to register for now */
	if (insn->insn_opcode.op_encoding != OP_ENC_RM)
		return (-1);

	/* XXX No device emulation yet. Fill with 0xFFs. */
	exit->vrs.vrs_gprs[insn->insn_reg] = 0xFFFFFFFFFFFFFFFF;

	return (0);
}

static int
emulate_movzx(struct x86_insn *insn, struct vm_exit *exit)
{
	uint8_t byte, len, src = 1, dst = 2;
	uint64_t value = 0;

	/* Only RM is valid for MOVZX. */
	if (insn->insn_opcode.op_encoding != OP_ENC_RM) {
		log_warnx("invalid op encoding for MOVZX: %d",
		    insn->insn_opcode.op_encoding);
		return (-1);
	}

	len = insn->insn_opcode.op_bytes_len;
	if (len < 1 || len > sizeof(insn->insn_opcode.op_bytes)) {
		log_warnx("invalid opcode byte length: %d", len);
		return (-1);
	}

	byte = insn->insn_opcode.op_bytes[len - 1];
	switch (byte) {
	case 0xB6:
		src = 1;
		if (insn->insn_cpu_mode == VMM_CPU_MODE_PROT
		    || insn->insn_cpu_mode == VMM_CPU_MODE_REAL)
			dst = 2;
		else if (insn->insn_prefix.pfx_rex == REX_NONE)
			dst = 4;
		else // XXX validate CPU mode
			dst = 8;
		break;
	case 0xB7:
		src = 2;
		if (insn->insn_prefix.pfx_rex == REX_NONE)
			dst = 4;
		else // XXX validate CPU mode
			dst = 8;
		break;
	default:
		log_warnx("invalid byte in MOVZX opcode: %x", byte);
		return (-1);
	}

	if (dst == 4)
		exit->vrs.vrs_gprs[insn->insn_reg] &= 0xFFFFFFFF00000000;
	else
		exit->vrs.vrs_gprs[insn->insn_reg] = 0x0UL;

	/* XXX No device emulation yet. Fill with 0xFFs. */
	switch (src) {
	case 1: value = 0xFF; break;
	case 2: value = 0xFFFF; break;
	case 4: value = 0xFFFFFFFF; break;
	case 8: value = 0xFFFFFFFFFFFFFFFF; break;
	default:
		log_warnx("invalid source size: %d", src);
		return (-1);
	}

	exit->vrs.vrs_gprs[insn->insn_reg] |= value;

	return (0);
}

/*
 * insn_emulate
 *
 * Returns:
 *  0: success
 *  EINVAL: exception occurred
 *  EFAULT: page fault occurred, requires retry
 *  ENOTSUP: an unsupported instruction was provided
 */
int
insn_emulate(struct vm_exit *exit, struct x86_insn *insn)
{
	int res;

	switch (insn->insn_opcode.op_type) {
	case OP_MOV:
		res = emulate_mov(insn, exit);
		break;

	case OP_MOVZX:
		res = emulate_movzx(insn, exit);
		break;

	default:
		log_warnx("%s: emulation not defined for %s", __func__,
		    str_opcode(&insn->insn_opcode));
		res = ENOTSUP;
	}

	if (res == 0)
		exit->vrs.vrs_gprs[VCPU_REGS_RIP] += insn->insn_bytes_len;

	return (res);
}
