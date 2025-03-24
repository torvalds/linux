// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)

#include <bpf/bpf.h>
#include "disasm.h"

struct print_insn_context {
	char scratch[16];
	char *buf;
	size_t sz;
};

static void print_insn_cb(void *private_data, const char *fmt, ...)
{
	struct print_insn_context *ctx = private_data;
	va_list args;

	va_start(args, fmt);
	vsnprintf(ctx->buf, ctx->sz, fmt, args);
	va_end(args);
}

static const char *print_call_cb(void *private_data, const struct bpf_insn *insn)
{
	struct print_insn_context *ctx = private_data;

	/* For pseudo calls verifier.c:jit_subprogs() hides original
	 * imm to insn->off and changes insn->imm to be an index of
	 * the subprog instead.
	 */
	if (insn->src_reg == BPF_PSEUDO_CALL) {
		snprintf(ctx->scratch, sizeof(ctx->scratch), "%+d", insn->off);
		return ctx->scratch;
	}

	return NULL;
}

struct bpf_insn *disasm_insn(struct bpf_insn *insn, char *buf, size_t buf_sz)
{
	struct print_insn_context ctx = {
		.buf = buf,
		.sz = buf_sz,
	};
	struct bpf_insn_cbs cbs = {
		.cb_print	= print_insn_cb,
		.cb_call	= print_call_cb,
		.private_data	= &ctx,
	};
	char *tmp, *pfx_end, *sfx_start;
	bool double_insn;
	int len;

	print_bpf_insn(&cbs, insn, true);
	/* We share code with kernel BPF disassembler, it adds '(FF) ' prefix
	 * for each instruction (FF stands for instruction `code` byte).
	 * Remove the prefix inplace, and also simplify call instructions.
	 * E.g.: "(85) call foo#10" -> "call foo".
	 * Also remove newline in the end (the 'max(strlen(buf) - 1, 0)' thing).
	 */
	pfx_end = buf + 5;
	sfx_start = buf + max((int)strlen(buf) - 1, 0);
	if (strncmp(pfx_end, "call ", 5) == 0 && (tmp = strrchr(buf, '#')))
		sfx_start = tmp;
	len = sfx_start - pfx_end;
	memmove(buf, pfx_end, len);
	buf[len] = 0;
	double_insn = insn->code == (BPF_LD | BPF_IMM | BPF_DW);
	return insn + (double_insn ? 2 : 1);
}
