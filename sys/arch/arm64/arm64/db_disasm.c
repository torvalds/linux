/* $OpenBSD: db_disasm.c,v 1.4 2020/09/11 09:27:10 mpi Exp $ */
/* $NetBSD: db_disasm.c,v 1.10 2020/07/09 23:43:41 ryo Exp $ */

/*
 * Copyright (c) 2017 Ryo Shimizu <ryo@nerv.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <machine/db_machdep.h>
#include <ddb/db_interface.h>
#include <ddb/db_sym.h>
#include <ddb/db_output.h>
#include <ddb/db_access.h>

#include <arch/arm64/arm64/disasm.h>

static db_expr_t
db_disasm_readword(db_expr_t address)
{
	return db_get_value(address, sizeof(uint32_t), false);
}

static void
db_disasm_printaddr(db_expr_t address)
{
	db_printf("%lx <", address);
	db_printsym((vaddr_t)address, DB_STGY_ANY, db_printf);
	db_printf(">");
}

static const disasm_interface_t db_disasm_interface = {
	.di_readword = db_disasm_readword,
	.di_printaddr = db_disasm_printaddr,
	.di_printf = db_printf
};

vaddr_t
db_disasm(vaddr_t loc, int altfmt)
{
	return disasm(&db_disasm_interface, loc);
}
