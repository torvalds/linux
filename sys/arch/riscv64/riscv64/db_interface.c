/*	$OpenBSD: db_interface.c,v 1.10 2024/10/17 01:57:18 jsg Exp $	*/

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
#include <sys/systm.h>
#include <sys/proc.h>

#include <uvm/uvm_extern.h>

#include <machine/db_machdep.h>
#include <ddb/db_access.h>
#include <ddb/db_command.h>
#include <ddb/db_output.h>
#include <ddb/db_run.h>
#include <ddb/db_variables.h>
#include <ddb/db_extern.h>
#include <ddb/db_interface.h>
#include <dev/cons.h>


struct db_variable db_regs[] = {
	{ "ra", (long *)&DDB_REGS->tf_ra, FCN_NULL, },		/* x1 */
	{ "sp", (long *)&DDB_REGS->tf_sp, FCN_NULL, },		/* x2 */
	{ "gp", (long *)&DDB_REGS->tf_gp, FCN_NULL, },		/* x3 */
	{ "tp", (long *)&DDB_REGS->tf_tp, FCN_NULL, },		/* x4 */
	{ "t0", (long *)&DDB_REGS->tf_t[0], FCN_NULL, },	/* x5 */
	{ "t1", (long *)&DDB_REGS->tf_t[1], FCN_NULL, },	/* x6 */
	{ "t2", (long *)&DDB_REGS->tf_t[2], FCN_NULL, },	/* x7 */
	{ "s0", (long *)&DDB_REGS->tf_s[0], FCN_NULL, },	/* x8 */
	{ "s1", (long *)&DDB_REGS->tf_s[1], FCN_NULL, },	/* x9 */
	{ "a0", (long *)&DDB_REGS->tf_a[0], FCN_NULL, },	/* x10 */
	{ "a1", (long *)&DDB_REGS->tf_a[1], FCN_NULL, },	/* x11 */
	{ "a2", (long *)&DDB_REGS->tf_a[2], FCN_NULL, },	/* x12 */
	{ "a3", (long *)&DDB_REGS->tf_a[3], FCN_NULL, },	/* x13 */
	{ "a4", (long *)&DDB_REGS->tf_a[4], FCN_NULL, },	/* x14 */
	{ "a5", (long *)&DDB_REGS->tf_a[5], FCN_NULL, },	/* x15 */
	{ "a6", (long *)&DDB_REGS->tf_a[6], FCN_NULL, },	/* x16 */
	{ "a7", (long *)&DDB_REGS->tf_a[7], FCN_NULL, },	/* x17 */
	{ "s2", (long *)&DDB_REGS->tf_s[2], FCN_NULL, },	/* x18 */
	{ "s3", (long *)&DDB_REGS->tf_s[3], FCN_NULL, },	/* x19 */
	{ "s4", (long *)&DDB_REGS->tf_s[4], FCN_NULL, },	/* x20 */
	{ "s5", (long *)&DDB_REGS->tf_s[5], FCN_NULL, },	/* x21 */
	{ "s6", (long *)&DDB_REGS->tf_s[6], FCN_NULL, },	/* x22 */
	{ "s7", (long *)&DDB_REGS->tf_s[7], FCN_NULL, },	/* x23 */
	{ "s8", (long *)&DDB_REGS->tf_s[8], FCN_NULL, },	/* x24 */
	{ "s9", (long *)&DDB_REGS->tf_s[9], FCN_NULL, },	/* x25 */
	{ "s10", (long *)&DDB_REGS->tf_s[10], FCN_NULL, },	/* x26 */
	{ "s11", (long *)&DDB_REGS->tf_s[11], FCN_NULL, },	/* x27 */
	{ "t3", (long *)&DDB_REGS->tf_t[3], FCN_NULL, },	/* x28 */
	{ "t4", (long *)&DDB_REGS->tf_t[4], FCN_NULL, },	/* x29 */
	{ "t5", (long *)&DDB_REGS->tf_t[5], FCN_NULL, },	/* x30 */
	{ "t6", (long *)&DDB_REGS->tf_t[6], FCN_NULL, }		/* x31 */
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

extern label_t	*db_recover;

struct db_variable *db_eregs = db_regs + nitems(db_regs);

#ifdef DDB
/*
 *  kdb_trap - field a TRACE or BPT trap
 */
int
kdb_trap(int type, db_regs_t *regs)
{
	int s;

#ifdef MULTIPROCESSOR
	db_mtx_enter(&ddb_mp_mutex);
	if (ddb_state == DDB_STATE_EXITING)
		ddb_state = DDB_STATE_NOT_RUNNING;
	db_mtx_leave(&ddb_mp_mutex);
	while (db_enter_ddb()) {
#endif

	switch (type) {
	case T_BREAKPOINT:	/* breakpoint */
	case -1:		/* keyboard interrupt */
		break;
	default:
		if (db_recover != 0) {
			db_error("Faulted in DDB; continuing...\n");
			/* NOTREACHED */
		}
	}

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
	return (1);
}
#endif

#define INKERNEL(va)    (((vaddr_t)(va)) & (1ULL << 63))

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
 * Write bytes to kernel address space for debugger.
 */
void
db_write_bytes(vaddr_t addr, size_t size, void *datap)
{
	// XXX
}

void
db_enter(void)
{
	asm("ebreak");
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
				intr_send_ipi(cpu_info[i], IPI_DDB);
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
		intr_send_ipi(cpu_info[cpu], IPI_DDB);
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

int
db_trapper(vaddr_t addr, u_int inst, trapframe_t *frame, int fault_code)
{

	if (fault_code == EXCP_BREAKPOINT) {
		kdb_trap(T_BREAKPOINT, frame);
		frame->tf_sepc += 4;
	} else
		kdb_trap(-1, frame);

	return (0);
}


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
	// XXX
	return pc + 4;
}
