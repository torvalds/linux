/*	$OpenBSD: db_machdep.c,v 1.2 1998/09/15 10:50:13 pefo Exp $ */

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1998 Per Fogelstrom, Opsycon AB
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed under OpenBSD by
 *	Per Fogelstrom, Opsycon AB, Sweden.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	JNPR: db_interface.c,v 1.6.2.1 2007/08/29 12:24:49 girish
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cons.h>
#include <sys/lock.h>
#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/reboot.h>

#include <machine/cache.h>
#include <machine/db_machdep.h>
#include <machine/mips_opcode.h>
#include <machine/vmparam.h>
#include <machine/md_var.h>
#include <machine/setjmp.h>

#include <ddb/ddb.h>
#include <ddb/db_sym.h>
#include <ddb/db_access.h>
#include <ddb/db_output.h>
#include <ddb/db_variables.h>
#include <sys/kdb.h>

static db_varfcn_t db_frame;

#define	DB_OFFSET(x)	(db_expr_t *)offsetof(struct trapframe, x)
struct db_variable db_regs[] = {
	{ "at",  DB_OFFSET(ast),	db_frame },
	{ "v0",  DB_OFFSET(v0),		db_frame },
	{ "v1",  DB_OFFSET(v1),		db_frame },
	{ "a0",  DB_OFFSET(a0),		db_frame },
	{ "a1",  DB_OFFSET(a1),		db_frame },
	{ "a2",  DB_OFFSET(a2),		db_frame },
	{ "a3",  DB_OFFSET(a3),		db_frame },
#if defined(__mips_n32) || defined(__mips_n64)
	{ "a4",  DB_OFFSET(a4),		db_frame },
	{ "a5",  DB_OFFSET(a5),		db_frame },
	{ "a6",  DB_OFFSET(a6),		db_frame },
	{ "a7",  DB_OFFSET(a7),		db_frame },
	{ "t0",  DB_OFFSET(t0),		db_frame },
	{ "t1",  DB_OFFSET(t1),		db_frame },
	{ "t2",  DB_OFFSET(t2),		db_frame },
	{ "t3",  DB_OFFSET(t3),		db_frame },
#else
	{ "t0",  DB_OFFSET(t0),		db_frame },
	{ "t1",  DB_OFFSET(t1),		db_frame },
	{ "t2",  DB_OFFSET(t2),		db_frame },
	{ "t3",  DB_OFFSET(t3),		db_frame },
	{ "t4",  DB_OFFSET(t4),		db_frame },
	{ "t5",  DB_OFFSET(t5),		db_frame },
	{ "t6",  DB_OFFSET(t6),		db_frame },
	{ "t7",  DB_OFFSET(t7),		db_frame },
#endif
	{ "s0",  DB_OFFSET(s0),		db_frame },
	{ "s1",  DB_OFFSET(s1),		db_frame },
	{ "s2",  DB_OFFSET(s2),		db_frame },
	{ "s3",  DB_OFFSET(s3),		db_frame },
	{ "s4",  DB_OFFSET(s4),		db_frame },
	{ "s5",  DB_OFFSET(s5),		db_frame },
	{ "s6",  DB_OFFSET(s6),		db_frame },
	{ "s7",  DB_OFFSET(s7),		db_frame },
	{ "t8",  DB_OFFSET(t8),		db_frame },
	{ "t9",  DB_OFFSET(t9),		db_frame },
	{ "k0",  DB_OFFSET(k0),		db_frame },
	{ "k1",  DB_OFFSET(k1),		db_frame },
	{ "gp",  DB_OFFSET(gp),		db_frame },
	{ "sp",  DB_OFFSET(sp),		db_frame },
	{ "s8",  DB_OFFSET(s8),		db_frame },
	{ "ra",  DB_OFFSET(ra),		db_frame },
	{ "sr",  DB_OFFSET(sr),		db_frame },
	{ "lo",  DB_OFFSET(mullo),	db_frame },
	{ "hi",  DB_OFFSET(mulhi),	db_frame },
	{ "bad", DB_OFFSET(badvaddr),	db_frame },
	{ "cs",  DB_OFFSET(cause),	db_frame },
	{ "pc",  DB_OFFSET(pc),		db_frame },
};
struct db_variable *db_eregs = db_regs + nitems(db_regs);

int (*do_db_log_stack_trace_cmd)(char *);

static int
db_frame(struct db_variable *vp, db_expr_t *valuep, int op)
{
	register_t *reg;

	if (kdb_frame == NULL)
		return (0);

	reg = (register_t *)((uintptr_t)kdb_frame + (size_t)(intptr_t)vp->valuep);
	if (op == DB_VAR_GET)
		*valuep = *reg;
	else
		*reg = *valuep;
	return (1);
}

int
db_read_bytes(vm_offset_t addr, size_t size, char *data)
{
	jmp_buf jb;
	void *prev_jb;
	int ret;

	prev_jb = kdb_jmpbuf(jb);
	ret = setjmp(jb);
	if (ret == 0) {
		/*
		 * 'addr' could be a memory-mapped I/O address.  Try to
		 * do atomic load/store in unit of size requested.
		 * size == 8 is only atomic on 64bit or n32 kernel.
		 */
		if ((size == 2 || size == 4 || size == 8) &&
		    ((addr & (size -1)) == 0) &&
		    (((vm_offset_t)data & (size -1)) == 0)) {
			switch (size) {
			case 2:
				*(uint16_t *)data = *(uint16_t *)addr;
				break;
			case 4:
				*(uint32_t *)data = *(uint32_t *)addr;
				break;
			case 8:
				*(uint64_t *)data = *(uint64_t *)addr;
				break;
			}
		} else {
			char *src;

			src = (char *)addr;
			while (size-- > 0)
				*data++ = *src++;
		}
	}

	(void)kdb_jmpbuf(prev_jb);
	return (ret);
}

int
db_write_bytes(vm_offset_t addr, size_t size, char *data)
{
	int ret;
	jmp_buf jb;
	void *prev_jb;

	prev_jb = kdb_jmpbuf(jb);
	ret = setjmp(jb);

	if (ret == 0) {
		/*
		 * 'addr' could be a memory-mapped I/O address.  Try to
		 * do atomic load/store in unit of size requested.
		 * size == 8 is only atomic on 64bit or n32 kernel.
		 */
		if ((size == 2 || size == 4 || size == 8) &&
		    ((addr & (size -1)) == 0) &&
		    (((vm_offset_t)data & (size -1)) == 0)) {
			switch (size) {
			case 2:
				*(uint16_t *)addr = *(uint16_t *)data;
				break;
			case 4:
				*(uint32_t *)addr = *(uint32_t *)data;
				break;
			case 8:
				*(uint64_t *)addr = *(uint64_t *)data;
				break;
			}
		} else {
			char *dst;
			size_t len = size;

			dst = (char *)addr;
			while (len-- > 0)
				*dst++ = *data++;
		}

		mips_icache_sync_range((db_addr_t) addr, size);
		mips_dcache_wbinv_range((db_addr_t) addr, size);
	}
	(void)kdb_jmpbuf(prev_jb);
	return (ret);
}

/*
 *	To do a single step ddb needs to know the next address
 *	that we will get to. It means that we need to find out
 *	both the address for a branch taken and for not taken, NOT! :-)
 *	MipsEmulateBranch will do the job to find out _exactly_ which
 *	address we will end up at so the 'dual bp' method is not
 *	requiered.
 */
db_addr_t
next_instr_address(db_addr_t pc, boolean_t bd)
{
	db_addr_t next;

	next = (db_addr_t)MipsEmulateBranch(kdb_frame, pc, 0, 0);
	return (next);
}


/*
 *	Decode instruction and figure out type.
 */
int
db_inst_type(int ins)
{
	InstFmt inst;
	int	ityp = 0;

	inst.word = ins;
	switch ((int)inst.JType.op) {
	case OP_SPECIAL:
		switch ((int)inst.RType.func) {
		case OP_JR:
			ityp = IT_BRANCH;
			break;
		case OP_JALR:
		case OP_SYSCALL:
			ityp = IT_CALL;
			break;
		}
		break;

	case OP_BCOND:
		switch ((int)inst.IType.rt) {
		case OP_BLTZ:
		case OP_BLTZL:
		case OP_BGEZ:
		case OP_BGEZL:
			ityp = IT_BRANCH;
			break;

		case OP_BLTZAL:
		case OP_BLTZALL:
		case OP_BGEZAL:
		case OP_BGEZALL:
			ityp = IT_CALL;
			break;
		}
		break;

	case OP_JAL:
		ityp = IT_CALL;
		break;

	case OP_J:
	case OP_BEQ:
	case OP_BEQL:
	case OP_BNE:
	case OP_BNEL:
	case OP_BLEZ:
	case OP_BLEZL:
	case OP_BGTZ:
	case OP_BGTZL:
		ityp = IT_BRANCH;
		break;

	case OP_COP1:
		switch (inst.RType.rs) {
		case OP_BCx:
		case OP_BCy:
			ityp = IT_BRANCH;
			break;
		}
		break;

	case OP_LB:
	case OP_LH:
	case OP_LW:
	case OP_LD:
	case OP_LBU:
	case OP_LHU:
	case OP_LWU:
	case OP_LWC1:
		ityp = IT_LOAD;
		break;

	case OP_SB:
	case OP_SH:
	case OP_SW:
	case OP_SD:  
	case OP_SWC1:
		ityp = IT_STORE;
		break;
	}
	return (ityp);
}

/*
 * Return the next pc if the given branch is taken.
 * MachEmulateBranch() runs analysis for branch delay slot.
 */
db_addr_t
branch_taken(int inst, db_addr_t pc)
{
	db_addr_t ra;
	register_t fpucsr;

	/* TBD: when is fsr set */
	fpucsr = (curthread) ? curthread->td_pcb->pcb_regs.fsr : 0;
	ra = (db_addr_t)MipsEmulateBranch(kdb_frame, pc, fpucsr, 0);
	return (ra);
}
