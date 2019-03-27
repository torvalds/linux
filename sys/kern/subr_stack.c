/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Antoine Brodin
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "opt_ddb.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#ifdef KTR
#include <sys/ktr.h>
#endif
#include <sys/linker.h>
#include <sys/malloc.h>
#include <sys/sbuf.h>
#include <sys/stack.h>
#include <sys/systm.h>
#include <sys/sysctl.h>

FEATURE(stack, "Support for capturing kernel stack");

static MALLOC_DEFINE(M_STACK, "stack", "Stack Traces");

static int stack_symbol(vm_offset_t pc, char *namebuf, u_int buflen,
	    long *offset, int flags);
static int stack_symbol_ddb(vm_offset_t pc, const char **name, long *offset);

struct stack *
stack_create(int flags)
{
	struct stack *st;

	st = malloc(sizeof(*st), M_STACK, flags | M_ZERO);
	return (st);
}

void
stack_destroy(struct stack *st)
{

	free(st, M_STACK);
}

int
stack_put(struct stack *st, vm_offset_t pc)
{

	if (st->depth < STACK_MAX) {
		st->pcs[st->depth++] = pc;
		return (0);
	} else
		return (-1);
}

void
stack_copy(const struct stack *src, struct stack *dst)
{

	*dst = *src;
}

void
stack_zero(struct stack *st)
{

	bzero(st, sizeof *st);
}

void
stack_print(const struct stack *st)
{
	char namebuf[64];
	long offset;
	int i;

	KASSERT(st->depth <= STACK_MAX, ("bogus stack"));
	for (i = 0; i < st->depth; i++) {
		(void)stack_symbol(st->pcs[i], namebuf, sizeof(namebuf),
		    &offset, M_WAITOK);
		printf("#%d %p at %s+%#lx\n", i, (void *)st->pcs[i],
		    namebuf, offset);
	}
}

void
stack_print_short(const struct stack *st)
{
	char namebuf[64];
	long offset;
	int i;

	KASSERT(st->depth <= STACK_MAX, ("bogus stack"));
	for (i = 0; i < st->depth; i++) {
		if (i > 0)
			printf(" ");
		if (stack_symbol(st->pcs[i], namebuf, sizeof(namebuf),
		    &offset, M_WAITOK) == 0)
			printf("%s+%#lx", namebuf, offset);
		else
			printf("%p", (void *)st->pcs[i]);
	}
	printf("\n");
}

void
stack_print_ddb(const struct stack *st)
{
	const char *name;
	long offset;
	int i;

	KASSERT(st->depth <= STACK_MAX, ("bogus stack"));
	for (i = 0; i < st->depth; i++) {
		stack_symbol_ddb(st->pcs[i], &name, &offset);
		printf("#%d %p at %s+%#lx\n", i, (void *)st->pcs[i],
		    name, offset);
	}
}

#if defined(DDB) || defined(WITNESS)
void
stack_print_short_ddb(const struct stack *st)
{
	const char *name;
	long offset;
	int i;

	KASSERT(st->depth <= STACK_MAX, ("bogus stack"));
	for (i = 0; i < st->depth; i++) {
		if (i > 0)
			printf(" ");
		if (stack_symbol_ddb(st->pcs[i], &name, &offset) == 0)
			printf("%s+%#lx", name, offset);
		else
			printf("%p", (void *)st->pcs[i]);
	}
	printf("\n");
}
#endif

/*
 * Format stack into sbuf from live kernel.
 *
 * flags - M_WAITOK or M_NOWAIT (EWOULDBLOCK).
 */
int
stack_sbuf_print_flags(struct sbuf *sb, const struct stack *st, int flags)
{
	char namebuf[64];
	long offset;
	int i, error;

	KASSERT(st->depth <= STACK_MAX, ("bogus stack"));
	for (i = 0; i < st->depth; i++) {
		error = stack_symbol(st->pcs[i], namebuf, sizeof(namebuf),
		    &offset, flags);
		if (error == EWOULDBLOCK)
			return (error);
		sbuf_printf(sb, "#%d %p at %s+%#lx\n", i, (void *)st->pcs[i],
		    namebuf, offset);
	}
	return (0);
}

void
stack_sbuf_print(struct sbuf *sb, const struct stack *st)
{

	(void)stack_sbuf_print_flags(sb, st, M_WAITOK);
}

#if defined(DDB) || defined(WITNESS)
void
stack_sbuf_print_ddb(struct sbuf *sb, const struct stack *st)
{
	const char *name;
	long offset;
	int i;

	KASSERT(st->depth <= STACK_MAX, ("bogus stack"));
	for (i = 0; i < st->depth; i++) {
		(void)stack_symbol_ddb(st->pcs[i], &name, &offset);
		sbuf_printf(sb, "#%d %p at %s+%#lx\n", i, (void *)st->pcs[i],
		    name, offset);
	}
}
#endif

#ifdef KTR
void
stack_ktr(u_int mask, const char *file, int line, const struct stack *st,
    u_int depth)
{
#ifdef DDB
	const char *name;
	long offset;
	int i;
#endif

	KASSERT(st->depth <= STACK_MAX, ("bogus stack"));
#ifdef DDB
	if (depth == 0 || st->depth < depth)
		depth = st->depth;
	for (i = 0; i < depth; i++) {
		(void)stack_symbol_ddb(st->pcs[i], &name, &offset);
		ktr_tracepoint(mask, file, line, "#%d %p at %s+%#lx",
		    i, st->pcs[i], (u_long)name, offset, 0, 0);
	}
#endif
}
#endif

/*
 * Two variants of stack symbol lookup -- one that uses the DDB interfaces
 * and bypasses linker locking, and the other that doesn't.
 */
static int
stack_symbol(vm_offset_t pc, char *namebuf, u_int buflen, long *offset,
    int flags)
{
	int error;

	error = linker_search_symbol_name_flags((caddr_t)pc, namebuf, buflen,
	    offset, flags);
	if (error == 0 || error == EWOULDBLOCK)
		return (error);

	*offset = 0;
	strlcpy(namebuf, "??", buflen);
	return (ENOENT);
}

static int
stack_symbol_ddb(vm_offset_t pc, const char **name, long *offset)
{
	linker_symval_t symval;
	c_linker_sym_t sym;

	if (linker_ddb_search_symbol((caddr_t)pc, &sym, offset) != 0)
		goto out;
	if (linker_ddb_symbol_values(sym, &symval) != 0)
		goto out;
	if (symval.name != NULL) {
		*name = symval.name;
		return (0);
	}
 out:
	*offset = 0;
	*name = "??";
	return (ENOENT);
}
