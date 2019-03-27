/*-
 * Copyright (c) 2015 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Semihalf under
 * the sponsorship of the FreeBSD Foundation.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <sys/param.h>
#include <sys/proc.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>

#ifdef KDB
#include <sys/kdb.h>
#endif

#include <ddb/ddb.h>
#include <ddb/db_variables.h>

#include <machine/cpu.h>
#include <machine/pcb.h>
#include <machine/stack.h>
#include <machine/vmparam.h>

static int
db_frame(struct db_variable *vp, db_expr_t *valuep, int op)
{
	long *reg;

	if (kdb_frame == NULL)
		return (0);

	reg = (long *)((uintptr_t)kdb_frame + (db_expr_t)vp->valuep);
	if (op == DB_VAR_GET)
		*valuep = *reg;
	else
		*reg = *valuep;
	return (1);
}

#define DB_OFFSET(x)	(db_expr_t *)offsetof(struct trapframe, x)
struct db_variable db_regs[] = {
	{ "ra",		DB_OFFSET(tf_ra),	db_frame },
	{ "sp",		DB_OFFSET(tf_sp),	db_frame },
	{ "gp",		DB_OFFSET(tf_gp),	db_frame },
	{ "tp",		DB_OFFSET(tf_tp),	db_frame },
	{ "t0",		DB_OFFSET(tf_t[0]),	db_frame },
	{ "t1",		DB_OFFSET(tf_t[1]),	db_frame },
	{ "t2",		DB_OFFSET(tf_t[2]),	db_frame },
	{ "t3",		DB_OFFSET(tf_t[3]),	db_frame },
	{ "t4",		DB_OFFSET(tf_t[4]),	db_frame },
	{ "t5",		DB_OFFSET(tf_t[5]),	db_frame },
	{ "t6",		DB_OFFSET(tf_t[6]),	db_frame },
	{ "s0",		DB_OFFSET(tf_s[0]),	db_frame },
	{ "s1",		DB_OFFSET(tf_s[1]),	db_frame },
	{ "s2",		DB_OFFSET(tf_s[2]),	db_frame },
	{ "s3",		DB_OFFSET(tf_s[3]),	db_frame },
	{ "s4",		DB_OFFSET(tf_s[4]),	db_frame },
	{ "s5",		DB_OFFSET(tf_s[5]),	db_frame },
	{ "s6",		DB_OFFSET(tf_s[6]),	db_frame },
	{ "s7",		DB_OFFSET(tf_s[7]),	db_frame },
	{ "s8",		DB_OFFSET(tf_s[8]),	db_frame },
	{ "s9",		DB_OFFSET(tf_s[9]),	db_frame },
	{ "s10",	DB_OFFSET(tf_s[10]),	db_frame },
	{ "s11",	DB_OFFSET(tf_s[11]),	db_frame },
	{ "a0",		DB_OFFSET(tf_a[0]),	db_frame },
	{ "a1",		DB_OFFSET(tf_a[1]),	db_frame },
	{ "a2",		DB_OFFSET(tf_a[2]),	db_frame },
	{ "a3",		DB_OFFSET(tf_a[3]),	db_frame },
	{ "a4",		DB_OFFSET(tf_a[4]),	db_frame },
	{ "a5",		DB_OFFSET(tf_a[5]),	db_frame },
	{ "a6",		DB_OFFSET(tf_a[6]),	db_frame },
	{ "a7",		DB_OFFSET(tf_a[7]),	db_frame },
	{ "sepc",	DB_OFFSET(tf_sepc),	db_frame },
	{ "sstatus",	DB_OFFSET(tf_sstatus),	db_frame },
	{ "stval",	DB_OFFSET(tf_stval),	db_frame },
	{ "scause",	DB_OFFSET(tf_scause),	db_frame },
};

struct db_variable *db_eregs = db_regs + nitems(db_regs);

void
db_show_mdpcpu(struct pcpu *pc)
{
}

/*
 * Read bytes from kernel address space for debugger.
 */
int
db_read_bytes(vm_offset_t addr, size_t size, char *data)
{
	jmp_buf jb;
	void *prev_jb;
	const char *src;
	int ret;

	prev_jb = kdb_jmpbuf(jb);
	ret = setjmp(jb);

	if (ret == 0) {
		src = (const char *)addr;
		while (size-- > 0)
			*data++ = *src++;
	}
	(void)kdb_jmpbuf(prev_jb);

	return (ret);
}

/*
 * Write bytes to kernel address space for debugger.
 */
int
db_write_bytes(vm_offset_t addr, size_t size, char *data)
{
	jmp_buf jb;
	void *prev_jb;
	char *dst;
	int ret;

	prev_jb = kdb_jmpbuf(jb);
	ret = setjmp(jb);
	if (ret == 0) {
		dst = (char *)addr;
		while (size-- > 0)
			*dst++ = *data++;

		/* Invalidate I-cache */
		fence_i();
	}
	(void)kdb_jmpbuf(prev_jb);

	return (ret);
}
