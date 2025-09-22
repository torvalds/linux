/* $OpenBSD: disasm.c,v 1.1 2024/11/19 05:50:41 anton Exp $ */

#include <stddef.h>	/* size_t */
#include <stdio.h>
#include <string.h>

#include "disasm.h"

#define test(instr, exp)						\
	test_impl((instr), sizeof(instr) - 1,				\
	    (exp), sizeof(exp) - 1,					\
	    __func__, __LINE__)

struct db_disasm_context *ctx = NULL;

static int
test_impl(const uint8_t *instr, size_t ninstr, const char *exp, size_t explen,
    const char *fun, int lno)
{
	const char *act;
	size_t actlen;

	static struct db_disasm_context c;
	memset(&c, 0, sizeof(c));
	ctx = &c;

	ctx->raw.buf = instr;
	ctx->raw.len = ninstr;
	ctx->act.len = 0;
	ctx->act.siz = sizeof(ctx->act.buf);

	db_disasm(0, 0);

	act = ctx->act.buf;
	actlen = ctx->act.len;
	/* Remove trailing whitespace from instructions w/o operands. */
	while (actlen > 0 && act[actlen - 1] == ' ')
		actlen--;

	if (explen != actlen || strncmp(exp, act, actlen) != 0) {
		fprintf(stderr, "%s:%d:\n", fun, lno);
		fprintf(stderr, "exp: \"%.*s\"\n", (int)explen, exp);
		fprintf(stderr, "act: \"%.*s\"\n", (int)actlen, act);
		return 1;
	}

	if (ctx->raw.len > 0) {
		fprintf(stderr, "%s:%d: %zu byte(s) not consumed\n", fun, lno,
		    ctx->raw.len);
		return 1;
	}

	return 0;
}

int
main(void)
{
	int error = 0;

	error |= test("\x90", "nop");
	error |= test("\x00", "addb %al,0(%rax)");
	error |= test("\xf2\x48\xa5", "repne movsq (%rsi),%es:(%rdi)");
	error |= test("\xf3\x48\xa5", "repe movsq (%rsi),%es:(%rdi)");
	error |= test("\xf3\x0f\x1e\xfa", "endbr64");

	return error;
}
