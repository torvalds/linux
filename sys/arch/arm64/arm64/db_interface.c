/*	$OpenBSD: db_interface.c,v 1.17 2025/07/22 09:20:41 kettenis Exp $	*/
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
#include <sys/reboot.h>
#include <sys/systm.h>
#include <sys/mutex.h>

#include <uvm/uvm_extern.h>

#include <dev/cons.h>

#include <machine/cpufunc.h>
#include <machine/db_machdep.h>
#include <machine/pmap.h>

#include <ddb/db_sym.h>
#include <ddb/db_command.h>
#include <ddb/db_extern.h>
#include <ddb/db_output.h>
#include <ddb/db_run.h>
#include <ddb/db_variables.h>

struct db_variable db_regs[] = {
	{ "x0", (long *)&DDB_REGS->tf_x[0], FCN_NULL, },
	{ "x1", (long *)&DDB_REGS->tf_x[1], FCN_NULL, },
	{ "x2", (long *)&DDB_REGS->tf_x[2], FCN_NULL, },
	{ "x3", (long *)&DDB_REGS->tf_x[3], FCN_NULL, },
	{ "x4", (long *)&DDB_REGS->tf_x[4], FCN_NULL, },
	{ "x5", (long *)&DDB_REGS->tf_x[5], FCN_NULL, },
	{ "x6", (long *)&DDB_REGS->tf_x[6], FCN_NULL, },
	{ "x7", (long *)&DDB_REGS->tf_x[7], FCN_NULL, },
	{ "x8", (long *)&DDB_REGS->tf_x[8], FCN_NULL, },
	{ "x9", (long *)&DDB_REGS->tf_x[9], FCN_NULL, },
	{ "x10", (long *)&DDB_REGS->tf_x[10], FCN_NULL, },
	{ "x11", (long *)&DDB_REGS->tf_x[11], FCN_NULL, },
	{ "x12", (long *)&DDB_REGS->tf_x[12], FCN_NULL, },
	{ "x13", (long *)&DDB_REGS->tf_x[13], FCN_NULL, },
	{ "x14", (long *)&DDB_REGS->tf_x[14], FCN_NULL, },
	{ "x15", (long *)&DDB_REGS->tf_x[15], FCN_NULL, },
	{ "x16", (long *)&DDB_REGS->tf_x[16], FCN_NULL, },
	{ "x17", (long *)&DDB_REGS->tf_x[17], FCN_NULL, },
	{ "x18", (long *)&DDB_REGS->tf_x[18], FCN_NULL, },
	{ "x19", (long *)&DDB_REGS->tf_x[19], FCN_NULL, },
	{ "x20", (long *)&DDB_REGS->tf_x[20], FCN_NULL, },
	{ "x21", (long *)&DDB_REGS->tf_x[21], FCN_NULL, },
	{ "x22", (long *)&DDB_REGS->tf_x[22], FCN_NULL, },
	{ "x23", (long *)&DDB_REGS->tf_x[23], FCN_NULL, },
	{ "x24", (long *)&DDB_REGS->tf_x[24], FCN_NULL, },
	{ "x25", (long *)&DDB_REGS->tf_x[25], FCN_NULL, },
	{ "x26", (long *)&DDB_REGS->tf_x[26], FCN_NULL, },
	{ "x27", (long *)&DDB_REGS->tf_x[27], FCN_NULL, },
	{ "x28", (long *)&DDB_REGS->tf_x[28], FCN_NULL, },
	{ "x29", (long *)&DDB_REGS->tf_x[29], FCN_NULL, },
	{ "x30", (long *)&DDB_REGS->tf_x[30], FCN_NULL, },
	{ "sp", (long *)&DDB_REGS->tf_sp, FCN_NULL, },
	{ "spsr", (long *)&DDB_REGS->tf_spsr, FCN_NULL, },
	{ "elr", (long *)&DDB_REGS->tf_elr, FCN_NULL, },
	{ "lr", (long *)&DDB_REGS->tf_lr, FCN_NULL, },
};

#ifdef MULTIPROCESSOR
struct db_mutex ddb_mp_mutex = DB_MUTEX_INITIALIZER;
volatile int ddb_state = DDB_STATE_NOT_RUNNING;
volatile cpuid_t ddb_active_cpu;
int		 db_switch_cpu;
long             db_switch_to_cpu;

void db_cpuinfo_cmd(db_expr_t, int, db_expr_t, char *);
void db_startproc_cmd(db_expr_t, int, db_expr_t, char *);
void db_stopproc_cmd(db_expr_t, int, db_expr_t, char *);
void db_ddbproc_cmd(db_expr_t, int, db_expr_t, char *);
void db_stopcpu(int cpu);
void db_startcpu(int cpu);
int db_enter_ddb(void);
#endif

extern label_t       *db_recover;

struct db_variable * db_eregs = db_regs + nitems(db_regs);

#ifdef DDB
/*
 *  db_ktrap - field a TRACE or BPT trap
 */
int
db_ktrap(int type, db_regs_t *regs)
{
	uint64_t mdscr;
	int s;

#ifdef MULTIPROCESSOR
	db_mtx_enter(&ddb_mp_mutex);
	if (ddb_state == DDB_STATE_EXITING)
		ddb_state = DDB_STATE_NOT_RUNNING;
	db_mtx_leave(&ddb_mp_mutex);
	while (db_enter_ddb()) {
#endif

	switch (type) {
	case EXCP_BRK:		/* breakpoint */
	case EXCP_WATCHPT_EL1:	/* watchpoint */
	case EXCP_SOFTSTP_EL1:	/* single-step */
	case -1:		/* keyboard interrupt */
		break;
	default:
		if (db_recover != 0) {
			/* This will longjmp back into db_command_loop() */
			db_error("Faulted in DDB; continuing...\n");
			/*NOTREACHED*/
		}
		break;
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

#ifdef MULTIPROCESSOR
		if (!db_switch_cpu)
			ddb_state = DDB_STATE_EXITING;
	}
#endif

	/* Enable debug exceptions in the kernel when needed. */
	mdscr = READ_SPECIALREG(mdscr_el1);
	if (regs->tf_spsr & PSR_SS) {
		mdscr |= (DBG_MDSCR_KDE | DBG_MDSCR_SS);
		regs->tf_spsr &= ~PSR_D;
	} else {
		mdscr &= ~(DBG_MDSCR_KDE | DBG_MDSCR_SS);
		regs->tf_spsr |= PSR_D;
	}
	WRITE_SPECIALREG(mdscr_el1, mdscr);

	return (1);
}
#endif

#define INKERNEL(va)	(((vaddr_t)(va)) & (1ULL << 63))

static int db_validate_address(vaddr_t addr);

static int
db_validate_address(vaddr_t addr)
{
	struct proc *p = curproc;
	struct pmap *pmap;

	if (!p || !p->p_vmspace || !p->p_vmspace->vm_map.pmap ||
	    INKERNEL(addr))
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

	if (db_validate_address((vaddr_t)src)) {
		db_printf("address %p is invalid\n", src);
		return;
	}

	if (size == 8 && (addr & 7) == 0 && ((vaddr_t)data & 7) == 0) {
		*((uint64_t*)data) = *((uint64_t*)src);
		return;
	}

	if (size == 4 && (addr & 3) == 0 && ((vaddr_t)data & 3) == 0) {
		*((int*)data) = *((int*)src);
		return;
	}

	if (size == 2 && (addr & 1) == 0 && ((vaddr_t)data & 1) == 0) {
		*((short*)data) = *((short*)src);
		return;
	}

	while (size-- > 0) {
		if (db_validate_address((vaddr_t)src)) {
			db_printf("address %p is invalid\n", src);
			return;
		}
		*data++ = *src++;
	}
}

/*
 * Write bytes somewhere in the kernel text.  Make the text
 * pages writable temporarily.
 */
static void
db_write_text(vaddr_t addr, size_t size, char *data)
{
	vaddr_t pgva;
	size_t limit;
	char *dst;

	if (size == 0)
		return;

	dst = (char *)addr;

	do {
		/*
		 * Get the VA for the page.
		 */
		pgva = trunc_page((vaddr_t)dst);

		/*
		 * Compute number of bytes that can be written
		 * with this mapping and subtract it from the
		 * total size.
		 */
		limit = PAGE_SIZE - ((vaddr_t)dst & PGOFSET);
		if (limit > size)
			limit = size;
		size -= limit;

		pmap_page_rw(pmap_kernel(), pgva);

		for (; limit > 0; limit--)
			*dst++ = *data++;

		/* Restore protection */
		pmap_page_ro(pmap_kernel(), pgva, PROT_READ|PROT_EXEC);

	} while (size != 0);
}

/*
 * Write bytes to kernel address space for debugger.
 */
void
db_write_bytes(vaddr_t addr, size_t size, void *datap)
{
	extern char etext[];
	char *data = datap, *dst;
	size_t loop;

	/* If any part is in kernel text, use db_write_text() */
	if (addr >= KERNBASE && addr < (vaddr_t)&etext) {
		db_write_text(addr, size, data);
		goto sync;
	}

	dst = (char *)addr;
	loop = size;
	while (loop-- > 0) {
		if (db_validate_address((vaddr_t)dst)) {
			db_printf("address %p is invalid\n", dst);
			return;
		}
		*dst++ = *data++;
	}

sync:
	/* make sure the caches and memory are in sync */
	cpu_icache_sync_range(addr, size);

	/* In case the current page tables have been modified ... */
	cpu_tlb_flush();
}

void
db_enter(void)
{
	/*
	 * This is the equivalent of LLVM's __builtin_debugtrap. The
	 * #0xf000 comment field allows us to recognize this as a
	 * permanent breakpoint and step over it.
	 */
	__asm volatile("brk #0xf000");
}

#ifdef MULTIPROCESSOR
void
db_cpuinfo_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	int i;

	for (i = 0; i < MAXCPUS; i++) {
		if (cpu_info[i] != NULL) {
			db_printf("%c%4d: ", (i == cpu_number()) ? '*' : ' ',
			    CPU_INFO_UNIT(cpu_info[i]));
			switch(cpu_info[i]->ci_ddb_paused) {
			case CI_DDB_RUNNING:
				db_printf("running\n");
				break;
			case CI_DDB_SHOULDSTOP:
				db_printf("stopping\n");
				break;
			case CI_DDB_STOPPED:
				db_printf("stopped\n");
				break;
			case CI_DDB_ENTERDDB:
				db_printf("entering ddb\n");
				break;
			case CI_DDB_INDDB:
				db_printf("ddb\n");
				break;
			default:
				db_printf("? (%d)\n",
				    cpu_info[i]->ci_ddb_paused);
				break;
			}
		}
	}
}

void
db_startproc_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	int i;

	if (have_addr) {
		if (addr >= 0 && addr < MAXCPUS &&
		    cpu_info[addr] != NULL && addr != cpu_number())
			db_startcpu(addr);
		else
			db_printf("Invalid cpu %d\n", (int)addr);
	} else {
		for (i = 0; i < MAXCPUS; i++) {
			if (cpu_info[i] != NULL && i != cpu_number())
				db_startcpu(i);
		}
	}
}

void
db_stopproc_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	int i;

	if (have_addr) {
		if (addr >= 0 && addr < MAXCPUS &&
		    cpu_info[addr] != NULL && addr != cpu_number())
			db_stopcpu(addr);
		else
			db_printf("Invalid cpu %d\n", (int)addr);
	} else {
		for (i = 0; i < MAXCPUS; i++) {
			if (cpu_info[i] != NULL && i != cpu_number())
				db_stopcpu(i);
		}
	}
}

void
db_ddbproc_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	if (have_addr) {
		if (addr >= 0 && addr < MAXCPUS &&
		    cpu_info[addr] != NULL && addr != cpu_number()) {
			db_stopcpu(addr);
			db_switch_to_cpu = addr;
			db_switch_cpu = 1;
			db_cmd_loop_done = 1;
		} else {
			db_printf("Invalid cpu %d\n", (int)addr);
		}
	} else {
		db_printf("CPU not specified\n");
	}
}

int
db_enter_ddb(void)
{
	int i;

	db_mtx_enter(&ddb_mp_mutex);

	/* If we are first in, grab ddb and stop all other CPUs */
	if (ddb_state == DDB_STATE_NOT_RUNNING) {
		ddb_active_cpu = cpu_number();
		ddb_state = DDB_STATE_RUNNING;
		curcpu()->ci_ddb_paused = CI_DDB_INDDB;
		db_mtx_leave(&ddb_mp_mutex);
		for (i = 0; i < MAXCPUS; i++) {
			if (cpu_info[i] != NULL && i != cpu_number() &&
			    cpu_info[i]->ci_ddb_paused != CI_DDB_STOPPED) {
				cpu_info[i]->ci_ddb_paused = CI_DDB_SHOULDSTOP;
				arm_send_ipi(cpu_info[i], ARM_IPI_DDB);
			}
		}
		return (1);
	}

	/* Leaving ddb completely.  Start all other CPUs and return 0 */
	if (ddb_active_cpu == cpu_number() && ddb_state == DDB_STATE_EXITING) {
		for (i = 0; i < MAXCPUS; i++) {
			if (cpu_info[i] != NULL) {
				cpu_info[i]->ci_ddb_paused = CI_DDB_RUNNING;
			}
		}
		db_mtx_leave(&ddb_mp_mutex);
		return (0);
	}

	/* We're switching to another CPU.  db_ddbproc_cmd() has made sure
	 * it is waiting for ddb, we just have to set ddb_active_cpu. */
	if (ddb_active_cpu == cpu_number() && db_switch_cpu) {
		curcpu()->ci_ddb_paused = CI_DDB_SHOULDSTOP;
		db_switch_cpu = 0;
		ddb_active_cpu = db_switch_to_cpu;
		cpu_info[db_switch_to_cpu]->ci_ddb_paused = CI_DDB_ENTERDDB;
	}

	/* Wait until we should enter ddb or resume */
	while (ddb_active_cpu != cpu_number() &&
	    curcpu()->ci_ddb_paused != CI_DDB_RUNNING) {
		if (curcpu()->ci_ddb_paused == CI_DDB_SHOULDSTOP)
			curcpu()->ci_ddb_paused = CI_DDB_STOPPED;
		db_mtx_leave(&ddb_mp_mutex);

		/* Busy wait without locking, we'll confirm with lock later */
		while (ddb_active_cpu != cpu_number() &&
		    curcpu()->ci_ddb_paused != CI_DDB_RUNNING)
			CPU_BUSY_CYCLE();

		db_mtx_enter(&ddb_mp_mutex);
	}

	/* Either enter ddb or exit */
	if (ddb_active_cpu == cpu_number() && ddb_state == DDB_STATE_RUNNING) {
		curcpu()->ci_ddb_paused = CI_DDB_INDDB;
		db_mtx_leave(&ddb_mp_mutex);
		return (1);
	} else {
		db_mtx_leave(&ddb_mp_mutex);
		return (0);
	}
}

void
db_startcpu(int cpu)
{
	if (cpu != cpu_number() && cpu_info[cpu] != NULL) {
		db_mtx_enter(&ddb_mp_mutex);
		cpu_info[cpu]->ci_ddb_paused = CI_DDB_RUNNING;
		db_mtx_leave(&ddb_mp_mutex);
	}
}

void
db_stopcpu(int cpu)
{
	db_mtx_enter(&ddb_mp_mutex);
	if (cpu != cpu_number() && cpu_info[cpu] != NULL &&
	    cpu_info[cpu]->ci_ddb_paused != CI_DDB_STOPPED) {
		cpu_info[cpu]->ci_ddb_paused = CI_DDB_SHOULDSTOP;
		db_mtx_leave(&ddb_mp_mutex);
		arm_send_ipi(cpu_info[cpu], ARM_IPI_DDB);
	} else {
		db_mtx_leave(&ddb_mp_mutex);
	}
}
#endif

const struct db_command db_machine_command_table[] = {
#ifdef MULTIPROCESSOR
	{ "cpuinfo",    db_cpuinfo_cmd,         0,      NULL },
	{ "startcpu",   db_startproc_cmd,       0,      NULL },
	{ "stopcpu",    db_stopproc_cmd,        0,      NULL },
	{ "ddbcpu",     db_ddbproc_cmd,         0,      NULL },
#endif
	{ NULL, 	NULL, 			0,	NULL }
};

extern vaddr_t esym;
extern vaddr_t end;

void
db_machine_init(void)
{
#ifdef MULTIPROCESSOR
	int i;

	for (i = 0; i < MAXCPUS; i++) {
		if (cpu_info[i] != NULL)
			cpu_info[i]->ci_ddb_paused = CI_DDB_RUNNING;
	}
#endif
}

vaddr_t
db_branch_taken(u_int insn, vaddr_t pc, db_regs_t *db_regs)
{
	/* implement */
	return pc + 4;
}
