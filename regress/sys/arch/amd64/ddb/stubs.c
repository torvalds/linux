/* $OpenBSD: stubs.c,v 1.1 2024/11/19 05:50:41 anton Exp $ */

#include <err.h>
#include <stdarg.h>
#include <stdio.h>

#include <machine/db_machdep.h>
#include <machine/cpu_full.h>
#include <ddb/db_access.h>
#include <ddb/db_interface.h>
#include <ddb/db_command.h>
#include <ddb/db_output.h>
#include <ddb/db_sym.h>

#include "disasm.h"

void			 (*cnputc)(int) = NULL;
int			 (*cngetc)(void) = NULL;
char			*esym = NULL;
char			*ssym = NULL;
struct cpu_info_full	 cpu_info_full_primary = {0};

int
db_elf_sym_init(int symsize, void *symtab, void *esymtab, const char *name)
{
	return 0;
}

Elf_Sym *
db_elf_sym_search(vaddr_t off, db_strategy_t strategy, db_expr_t *diffp)
{
	return NULL;
}

int
db_elf_line_at_pc(Elf_Sym *cursym, const char **filename, int *linenum,
    db_expr_t off)
{
	return 0;
}

void
db_error(char *s)
{
}

void
db_stack_trace_print(db_expr_t addr, int have_addr, db_expr_t count,
    char *modif, int (*pr)(const char *, ...))
{
}

void
db_symbol_values(Elf_Sym *sym, const char **namep, db_expr_t *valuep)
{
}

db_expr_t
db_get_value(vaddr_t addr, size_t size, int is_signed)
{
	db_expr_t c;

	if (ctx->raw.len == 0)
		return 0;

	c = ctx->raw.buf[0];
	ctx->raw.buf++;
	ctx->raw.len--;
	return c;
}

int
db_printf(const char *fmt, ...)
{
	int n = 0;

	if (strcmp(fmt, "\n") == 0) {
		/* nothing */
	} else if (strcmp(fmt, "\t") == 0) {
		n = snprintf(&ctx->act.buf[ctx->act.len], ctx->act.siz, " ");
	} else {
		va_list ap;

		va_start(ap, fmt);
		n = vsnprintf(&ctx->act.buf[ctx->act.len], ctx->act.siz, fmt,
		    ap);
		va_end(ap);
	}
	if (n < 0 || (size_t)n >= ctx->act.siz)
		errx(1, "buffer too small");
	ctx->act.len += n;
	ctx->act.siz -= n;
	return 0;
}
