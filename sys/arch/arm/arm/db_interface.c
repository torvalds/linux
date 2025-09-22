/*	$OpenBSD: db_interface.c,v 1.21 2024/02/23 18:19:02 cheloha Exp $	*/
/*	$NetBSD: db_interface.c,v 1.34 2003/10/26 23:11:15 chris Exp $	*/

/* 
 * Copyright (c) 1996 Scott K. Stevens
 *
 * Mach Operating System
 * Copyright (c) 1991,1990 Carnegie Mellon University
 * All Rights Reserved.
 * 
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 * 
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 * 
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 *
 *	From: db_interface.c,v 2.4 1991/02/05 17:11:13 mrt (CMU)
 */

/*
 * Interface to new debugger.
 */
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/systm.h>	/* just for boothowto */

#include <uvm/uvm_extern.h>

#include <arm/db_machdep.h>
#include <machine/pmap.h>
#include <arm/undefined.h>
#include <ddb/db_access.h>
#include <ddb/db_command.h>
#include <ddb/db_output.h>
#include <ddb/db_variables.h>
#include <ddb/db_extern.h>
#include <ddb/db_interface.h>
#include <dev/cons.h>

static long nil;

int db_access_und_sp (struct db_variable *, db_expr_t *, int);
int db_access_abt_sp (struct db_variable *, db_expr_t *, int);
int db_access_irq_sp (struct db_variable *, db_expr_t *, int);
u_int db_fetch_reg (int, db_regs_t *);

int db_trapper (u_int, u_int, trapframe_t *, int, uint32_t);

struct db_variable db_regs[] = {
	{ "spsr",	(long *)&ddb_regs.tf_spsr,	FCN_NULL, },
	{ "r0",		(long *)&ddb_regs.tf_r0,	FCN_NULL, },
	{ "r1",		(long *)&ddb_regs.tf_r1,	FCN_NULL, },
	{ "r2",		(long *)&ddb_regs.tf_r2,	FCN_NULL, },
	{ "r3",		(long *)&ddb_regs.tf_r3,	FCN_NULL, },
	{ "r4",		(long *)&ddb_regs.tf_r4,	FCN_NULL, },
	{ "r5",		(long *)&ddb_regs.tf_r5,	FCN_NULL, },
	{ "r6",		(long *)&ddb_regs.tf_r6,	FCN_NULL, },
	{ "r7",		(long *)&ddb_regs.tf_r7,	FCN_NULL, },
	{ "r8",		(long *)&ddb_regs.tf_r8,	FCN_NULL, },
	{ "r9",		(long *)&ddb_regs.tf_r9,	FCN_NULL, },
	{ "r10",	(long *)&ddb_regs.tf_r10,	FCN_NULL, },
	{ "r11",	(long *)&ddb_regs.tf_r11,	FCN_NULL, },
	{ "r12",	(long *)&ddb_regs.tf_r12,	FCN_NULL, },
	{ "usr_sp",	(long *)&ddb_regs.tf_usr_sp,	FCN_NULL, },
	{ "usr_lr",	(long *)&ddb_regs.tf_usr_lr,	FCN_NULL, },
	{ "svc_sp",	(long *)&ddb_regs.tf_svc_sp,	FCN_NULL, },
	{ "svc_lr",	(long *)&ddb_regs.tf_svc_lr,	FCN_NULL, },
	{ "pc",		(long *)&ddb_regs.tf_pc,	FCN_NULL, },
	{ "und_sp",	(long *)&nil,			db_access_und_sp, },
	{ "abt_sp",	(long *)&nil,			db_access_abt_sp, },
	{ "irq_sp",	(long *)&nil,			db_access_irq_sp, },
};

extern label_t       *db_recover;

struct db_variable * db_eregs = db_regs + nitems(db_regs);

int
db_access_und_sp(struct db_variable *vp, db_expr_t *valp, int rw)
{

	if (rw == DB_VAR_GET)
		*valp = get_stackptr(PSR_UND32_MODE);
	return(0);
}

int
db_access_abt_sp(struct db_variable *vp, db_expr_t *valp, int rw)
{

	if (rw == DB_VAR_GET)
		*valp = get_stackptr(PSR_ABT32_MODE);
	return(0);
}

int
db_access_irq_sp(struct db_variable *vp, db_expr_t *valp, int rw)
{

	if (rw == DB_VAR_GET)
		*valp = get_stackptr(PSR_IRQ32_MODE);
	return(0);
}

#ifdef DDB
/*
 *  db_ktrap - field a TRACE or BPT trap
 */
int
db_ktrap(int type, db_regs_t *regs)
{
	int s;

	switch (type) {
	case T_BREAKPOINT:	/* breakpoint */
	case -1:		/* keyboard interrupt */
		break;
	default:
		if (db_recover != 0) {
			/* This will longjmp back into db_command_loop() */
			db_error("Faulted in DDB; continuing...\n");
			/*NOTREACHED*/
		}
	}

	/* Should switch to kdb`s own stack here. */

	ddb_regs = *regs;

	s = splhigh();
	db_active++;
	cnpollc(1);
	db_trap(type, 0/*code*/);
	cnpollc(0);
	db_active--;
	splx(s);

	*regs = ddb_regs;

	return (1);
}
#endif


static int db_validate_address(vaddr_t addr);

static int
db_validate_address(vaddr_t addr)
{
	struct proc *p = curproc;
	struct pmap *pmap;

	if (!p || !p->p_vmspace || !p->p_vmspace->vm_map.pmap ||
#ifndef ARM32_NEW_VM_LAYOUT
	    addr >= VM_MAXUSER_ADDRESS
#else
	    addr >= VM_MIN_KERNEL_ADDRESS
#endif
	    )
		pmap = pmap_kernel();
	else
		pmap = p->p_vmspace->vm_map.pmap;

	return (pmap_extract(pmap, addr, NULL) == FALSE);
}

/*
 * Read bytes from kernel address space for debugger.
 */
void
db_read_bytes(vaddr_t addr, size_t size, void *datap)
{
	char *data = datap, *src = (char *)addr;

	if (db_validate_address((u_int)src)) {
		db_printf("address %p is invalid\n", src);
		return;
	}

	if (size == 4 && (addr & 3) == 0 && ((u_int32_t)data & 3) == 0) {
		*((int*)data) = *((int*)src);
		return;
	}

	if (size == 2 && (addr & 1) == 0 && ((u_int32_t)data & 1) == 0) {
		*((short*)data) = *((short*)src);
		return;
	}

	while (size-- > 0) {
		if (db_validate_address((u_int)src)) {
			db_printf("address %p is invalid\n", src);
			return;
		}
		*data++ = *src++;
	}
}

static void
db_write_text(vaddr_t addr, size_t size, char *data)
{        
	struct pmap *pmap = pmap_kernel();
	pd_entry_t *pde, oldpde, tmppde;
	pt_entry_t *pte, oldpte, tmppte;
	vaddr_t pgva;
	size_t limit, savesize;
	char *dst;

	/* XXX: gcc */
	oldpte = 0;

	if ((savesize = size) == 0)
		return;

	dst = (char *) addr;

	do {
		/* Get the PDE of the current VA. */
		if (pmap_get_pde_pte(pmap, (vaddr_t) dst, &pde, &pte) == FALSE)
			goto no_mapping;
		switch ((oldpde = *pde) & L1_TYPE_MASK) {
		case L1_TYPE_S:
			pgva = (vaddr_t)dst & L1_S_FRAME;
			limit = L1_S_SIZE - ((vaddr_t)dst & L1_S_OFFSET);

			tmppde = oldpde | L1_S_PROT(PTE_KERNEL, PROT_WRITE);
			*pde = tmppde;
			PTE_SYNC(pde);
			break;

		case L1_TYPE_C:
			pgva = (vaddr_t)dst & L2_S_FRAME;
			limit = L2_S_SIZE - ((vaddr_t)dst & L2_S_OFFSET);

			if (pte == NULL)
				goto no_mapping;
			oldpte = *pte;
			tmppte = oldpte | L2_S_PROT(PTE_KERNEL, PROT_WRITE);
			*pte = tmppte;
			PTE_SYNC(pte);
			break;

		default:
		no_mapping:
			printf(" address 0x%08lx not a valid page\n",
			    (vaddr_t) dst);
			return;
		}
		cpu_tlb_flushD_SE(pgva);
		cpu_cpwait();

		if (limit > size)
			limit = size;
		size -= limit;

		/*
		 * Page is now writable.  Do as much access as we
		 * can in this page.
		 */
		for (; limit > 0; limit--)
			*dst++ = *data++;

		/*
		 * Restore old mapping permissions.
		 */
		switch (oldpde & L1_TYPE_MASK) {
		case L1_TYPE_S:
			*pde = oldpde;
			PTE_SYNC(pde);
			break;

		case L1_TYPE_C:
			*pte = oldpte;
			PTE_SYNC(pte);
			break;
		}
		cpu_tlb_flushD_SE(pgva);
		cpu_cpwait();

	} while (size != 0);

	/* Sync the I-cache. */
	cpu_icache_sync_range(addr, savesize);
}

/*
 * Write bytes to kernel address space for debugger.
 */
void
db_write_bytes(vaddr_t addr, size_t size, void *datap)
{
	extern char etext[];
	extern char kernel_text[];
	char *data = datap, *dst;
	size_t loop;

	/* If any part is in kernel text, use db_write_text() */
	if (addr >= (vaddr_t) kernel_text && addr < (vaddr_t) etext) {
		db_write_text(addr, size, data);
		return;
	}

	dst = (char *)addr;
	loop = size;
	while (loop-- > 0) {
		if (db_validate_address((u_int)dst)) {
			db_printf("address %p is invalid\n", dst);
			return;
		}
		*dst++ = *data++;
	}
	/* make sure the caches and memory are in sync */
	cpu_icache_sync_range(addr, size);

	/* In case the current page tables have been modified ... */
	cpu_tlb_flushID();
	cpu_cpwait();
}

void
db_enter(void)
{
	asm(".word	0xe7ffffff");
}

const struct db_command db_machine_command_table[] = {
	{ "frame",	db_show_frame_cmd,	0, NULL },
#ifdef ARM32_DB_COMMANDS
	ARM32_DB_COMMANDS,
#endif
	{ NULL, 	NULL, 			0, NULL }
};

int
db_trapper(u_int addr, u_int inst, trapframe_t *frame, int fault_code,
    uint32_t fpexc)
{

	if (fault_code == 0) {
		if ((inst & ~INSN_COND_MASK) == (BKPT_INST & ~INSN_COND_MASK)) {
			db_ktrap(T_BREAKPOINT, frame);
			frame->tf_pc += INSN_SIZE;
		} else
			db_ktrap(-1, frame);
	} else
		return (1);
	return (0);
}

extern u_int esym;
extern u_int end;

static struct undefined_handler db_uh;

void
db_machine_init(void)
{
	/*
	 * We get called before malloc() is available, so supply a static
	 * struct undefined_handler.
	 */
	db_uh.uh_handler = db_trapper;
	install_coproc_handler_static(0, &db_uh);
}

u_int
db_fetch_reg(int reg, db_regs_t *db_regs)
{

	switch (reg) {
	case 0:
		return (db_regs->tf_r0);
	case 1:
		return (db_regs->tf_r1);
	case 2:
		return (db_regs->tf_r2);
	case 3:
		return (db_regs->tf_r3);
	case 4:
		return (db_regs->tf_r4);
	case 5:
		return (db_regs->tf_r5);
	case 6:
		return (db_regs->tf_r6);
	case 7:
		return (db_regs->tf_r7);
	case 8:
		return (db_regs->tf_r8);
	case 9:
		return (db_regs->tf_r9);
	case 10:
		return (db_regs->tf_r10);
	case 11:
		return (db_regs->tf_r11);
	case 12:
		return (db_regs->tf_r12);
	case 13:
		return (db_regs->tf_svc_sp);
	case 14:
		return (db_regs->tf_svc_lr);
	case 15:
		return (db_regs->tf_pc);
	default:
		panic("db_fetch_reg: botch");
	}
}

vaddr_t
db_branch_taken(u_int insn, vaddr_t pc, db_regs_t *db_regs)
{
	u_int addr, nregs;

	switch ((insn >> 24) & 0xf) {
	case 0xa:	/* b ... */
	case 0xb:	/* bl ... */
		addr = ((insn << 2) & 0x03ffffff);
		if (addr & 0x02000000)
			addr |= 0xfc000000;
		return (pc + 8 + addr);
	case 0x7:	/* ldr pc, [pc, reg, lsl #2] */
		addr = db_fetch_reg(insn & 0xf, db_regs);
		addr = pc + 8 + (addr << 2);
		db_read_bytes(addr, 4, (char *)&addr);
		return (addr);
	case 0x1:	/* mov pc, reg */
		addr = db_fetch_reg(insn & 0xf, db_regs);
		return (addr);
	case 0x8:	/* ldmxx reg, {..., pc} */
	case 0x9:
		addr = db_fetch_reg((insn >> 16) & 0xf, db_regs);
		nregs = (insn  & 0x5555) + ((insn  >> 1) & 0x5555);
		nregs = (nregs & 0x3333) + ((nregs >> 2) & 0x3333);
		nregs = (nregs + (nregs >> 4)) & 0x0f0f;
		nregs = (nregs + (nregs >> 8)) & 0x001f;
		switch ((insn >> 23) & 0x3) {
		case 0x0:	/* ldmda */
			addr = addr - 0;
			break;
		case 0x1:	/* ldmia */
			addr = addr + 0 + ((nregs - 1) << 2);
			break;
		case 0x2:	/* ldmdb */
			addr = addr - 4;
			break;
		case 0x3:	/* ldmib */
			addr = addr + 4 + ((nregs - 1) << 2);
			break;
		}
		db_read_bytes(addr, 4, (char *)&addr);
		return (addr);
	default:
		panic("branch_taken: botch");
	}
}
