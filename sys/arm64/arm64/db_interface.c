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
	{ "spsr", DB_OFFSET(tf_spsr),	db_frame },
	{ "x0", DB_OFFSET(tf_x[0]),	db_frame },
	{ "x1", DB_OFFSET(tf_x[1]),	db_frame },
	{ "x2", DB_OFFSET(tf_x[2]),	db_frame },
	{ "x3", DB_OFFSET(tf_x[3]),	db_frame },
	{ "x4", DB_OFFSET(tf_x[4]),	db_frame },
	{ "x5", DB_OFFSET(tf_x[5]),	db_frame },
	{ "x6", DB_OFFSET(tf_x[6]),	db_frame },
	{ "x7", DB_OFFSET(tf_x[7]),	db_frame },
	{ "x8", DB_OFFSET(tf_x[8]),	db_frame },
	{ "x9", DB_OFFSET(tf_x[9]),	db_frame },
	{ "x10", DB_OFFSET(tf_x[10]),	db_frame },
	{ "x11", DB_OFFSET(tf_x[11]),	db_frame },
	{ "x12", DB_OFFSET(tf_x[12]),	db_frame },
	{ "x13", DB_OFFSET(tf_x[13]),	db_frame },
	{ "x14", DB_OFFSET(tf_x[14]),	db_frame },
	{ "x15", DB_OFFSET(tf_x[15]),	db_frame },
	{ "x16", DB_OFFSET(tf_x[16]),	db_frame },
	{ "x17", DB_OFFSET(tf_x[17]),	db_frame },
	{ "x18", DB_OFFSET(tf_x[18]),	db_frame },
	{ "x19", DB_OFFSET(tf_x[19]),	db_frame },
	{ "x20", DB_OFFSET(tf_x[20]),	db_frame },
	{ "x21", DB_OFFSET(tf_x[21]),	db_frame },
	{ "x22", DB_OFFSET(tf_x[22]),	db_frame },
	{ "x23", DB_OFFSET(tf_x[23]),	db_frame },
	{ "x24", DB_OFFSET(tf_x[24]),	db_frame },
	{ "x25", DB_OFFSET(tf_x[25]),	db_frame },
	{ "x26", DB_OFFSET(tf_x[26]),	db_frame },
	{ "x27", DB_OFFSET(tf_x[27]),	db_frame },
	{ "x28", DB_OFFSET(tf_x[28]),	db_frame },
	{ "x29", DB_OFFSET(tf_x[29]),	db_frame },
	{ "lr", DB_OFFSET(tf_lr),	db_frame },
	{ "elr", DB_OFFSET(tf_elr),	db_frame },
	{ "sp", DB_OFFSET(tf_sp), db_frame },
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

		dsb(ish);

		/* Clean D-cache and invalidate I-cache */
		cpu_dcache_wb_range(addr, (vm_size_t)size);
		cpu_icache_sync_range(addr, (vm_size_t)size);
	}
	(void)kdb_jmpbuf(prev_jb);

	return (ret);
}
