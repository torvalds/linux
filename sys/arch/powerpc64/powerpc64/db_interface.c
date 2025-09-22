/*	$OpenBSD: db_interface.c,v 1.6 2022/04/14 19:47:11 naddy Exp $	*/
/*      $NetBSD: db_interface.c,v 1.12 2001/07/22 11:29:46 wiz Exp $ */

/*
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
 *      db_interface.c,v 2.4 1991/02/05 17:11:13 mrt (CMU)
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mutex.h>

#include <dev/cons.h>
#include <dev/ofw/fdt.h>

#include <machine/db_machdep.h>
#include <ddb/db_command.h>
#include <ddb/db_elf.h>
#include <ddb/db_extern.h>
#include <ddb/db_output.h>
#include <ddb/db_run.h>
#include <ddb/db_sym.h>

extern db_regs_t ddb_regs; /* db_trace.c */
extern db_symtab_t db_symtab; /* ddb/db_elf.c */
extern struct fdt_reg initrd_reg; /* machdep.c */

#ifdef MULTIPROCESSOR

struct db_mutex ddb_mp_mutex = DB_MUTEX_INITIALIZER;
volatile int ddb_state = DDB_STATE_NOT_RUNNING;
volatile cpuid_t ddb_active_cpu;
int	db_switch_cpu;
long	db_switch_to_cpu;

void db_cpuinfo_cmd(db_expr_t, int, db_expr_t, char *);
void db_startproc_cmd(db_expr_t, int, db_expr_t, char *);
void db_stopproc_cmd(db_expr_t, int, db_expr_t, char *);
void db_ddbproc_cmd(db_expr_t, int, db_expr_t, char *);

void db_stopcpu(int cpu);
void db_startcpu(int cpu);
int db_enter_ddb(void);

#endif

const struct db_command db_machine_command_table[] = {
#ifdef MULTIPROCESSOR
	{ "cpuinfo",    db_cpuinfo_cmd,         0,      NULL },
	{ "startcpu",   db_startproc_cmd,       0,      NULL },
	{ "stopcpu",    db_stopproc_cmd,        0,      NULL },
	{ "ddbcpu",     db_ddbproc_cmd,         0,      NULL },
#endif
	{ (char *)NULL }
};

void
db_machine_init(void)
{
	db_expr_t val;
	uint64_t a, b;
	char *prop_start, *prop_end;
	void *node;
#ifdef MULTIPROCESSOR
	int i;
#endif

	/*
	 * petitboot loads the kernel without symbols.
	 * If an initrd exists, try to load symbols from there.
	 */
	node = fdt_find_node("/chosen");
	if (fdt_node_property(node, "linux,initrd-start", &prop_start) != 8 ||
	    fdt_node_property(node, "linux,initrd-end", &prop_end) != 8) {
		printf("[ no initrd ]\n");
		return;
	}

	a = bemtoh64((uint64_t *)prop_start);
	b = bemtoh64((uint64_t *)prop_end);
	initrd_reg.addr = trunc_page(a);
	initrd_reg.size = round_page(b) - initrd_reg.addr;
	db_elf_sym_init(b - a, (char *)a, (char *)b, "initrd");

	/* The kernel is PIE. Add an offset to most symbols. */
	if (db_symbol_by_name("db_machine_init", &val) != NULL) {
		Elf_Sym *symp, *symtab_start, *symtab_end;
		Elf_Addr offset;
		
		symtab_start = STAB_TO_SYMSTART(&db_symtab);
		symtab_end = STAB_TO_SYMEND(&db_symtab);
		
		offset = (Elf_Addr)db_machine_init - (Elf_Addr)val;
		for (symp = symtab_start; symp < symtab_end; symp++) {
			if (symp->st_shndx != SHN_ABS)
				symp->st_value += offset;
		}
	}

#ifdef MULTIPROCESSOR
	for (i = 0; i < ncpus; i++) {
		cpu_info[i].ci_ddb_paused = CI_DDB_RUNNING;
	}
#endif
}

void
db_ktrap(int type, db_regs_t *frame)
{
	int s;

#ifdef MULTIPROCESSOR
	db_mtx_enter(&ddb_mp_mutex);
	if (ddb_state == DDB_STATE_EXITING)
		ddb_state = DDB_STATE_NOT_RUNNING;
	db_mtx_leave(&ddb_mp_mutex);

	while (db_enter_ddb()) {
#endif
		ddb_regs = *frame;

		s = splhigh();
		db_active++;
		cnpollc(1);
		db_trap(type, 0);
		cnpollc(0);
		db_active--;
		splx(s);

		*frame = ddb_regs;
#ifdef MULTIPROCESSOR
		if (!db_switch_cpu)
			ddb_state = DDB_STATE_EXITING;
	}
#endif
}

#ifdef MULTIPROCESSOR

int
db_enter_ddb(void)
{
	struct cpu_info *ci = curcpu();
	int i;

	db_mtx_enter(&ddb_mp_mutex);

	/* If we are first in, grab ddb and stop all other CPUs */
	if (ddb_state == DDB_STATE_NOT_RUNNING) {
		ddb_active_cpu = cpu_number();
		ddb_state = DDB_STATE_RUNNING;
		ci->ci_ddb_paused = CI_DDB_INDDB;
		db_mtx_leave(&ddb_mp_mutex);
		for (i = 0; i < ncpus; i++) {
			if (i != cpu_number() &&
			    cpu_info[i].ci_ddb_paused != CI_DDB_STOPPED) {
				cpu_info[i].ci_ddb_paused = CI_DDB_SHOULDSTOP;
				intr_send_ipi(&cpu_info[i], IPI_DDB);
			}
		}
		return (1);
	}

	/* Leaving ddb completely.  Start all other CPUs and return 0 */
	if (ddb_active_cpu == cpu_number() && ddb_state == DDB_STATE_EXITING) {
		for (i = 0; i < ncpus; i++) {
			cpu_info[i].ci_ddb_paused = CI_DDB_RUNNING;
		}
		db_mtx_leave(&ddb_mp_mutex);
		return (0);
	}

	/* We are switching to another CPU. ddb_ddbproc_cmd() has made sure
	 * it is waiting for ddb, we just have to set ddb_active_cpu. */
	if (ddb_active_cpu == cpu_number() && db_switch_cpu) {
		ci->ci_ddb_paused = CI_DDB_SHOULDSTOP;
		db_switch_cpu = 0;
		ddb_active_cpu = db_switch_to_cpu;
		cpu_info[db_switch_to_cpu].ci_ddb_paused = CI_DDB_ENTERDDB;
	}

	/* Wait until we should enter ddb or resume */
	while (ddb_active_cpu != cpu_number() &&
	    ci->ci_ddb_paused != CI_DDB_RUNNING) {
		if (ci->ci_ddb_paused == CI_DDB_SHOULDSTOP)
			ci->ci_ddb_paused = CI_DDB_STOPPED;
		db_mtx_leave(&ddb_mp_mutex);

		/* Busy wait without locking, we will confirm with lock later */
		while (ddb_active_cpu != cpu_number() &&
		    ci->ci_ddb_paused != CI_DDB_RUNNING)
			;	/* Do nothing */

		db_mtx_enter(&ddb_mp_mutex);
	}

	/* Either enter ddb or exit */
	if (ddb_active_cpu == cpu_number() && ddb_state == DDB_STATE_RUNNING) {
		ci->ci_ddb_paused = CI_DDB_INDDB;
		db_mtx_leave(&ddb_mp_mutex);
		return (1);
	} else {
		db_mtx_leave(&ddb_mp_mutex);
		return (0);
	}
}

void
db_cpuinfo_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	int i;

	for (i = 0; i < ncpus; i++) {
		db_printf("%c%4d: ", (i == cpu_number()) ? '*' : ' ',
		    cpu_info[i].ci_cpuid);
		switch(cpu_info[i].ci_ddb_paused) {
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
			    cpu_info[i].ci_ddb_paused);
			break;
		}
	}
}

void
db_ddbproc_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	int cpu_n;

	if (have_addr) {
		cpu_n = addr;
		if (cpu_n >= 0 && cpu_n < ncpus &&
		    cpu_n != cpu_number()) {
			db_switch_to_cpu = cpu_n;
			db_switch_cpu = 1;
			db_cmd_loop_done = 1;
		} else {
			db_printf("Invalid cpu %d\n", (int)addr);
		}
	} else {
		db_printf("CPU not specified\n");
	}
}

void
db_startproc_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	int cpu_n;

	if (have_addr) {
		cpu_n = addr;
		if (cpu_n >= 0 && cpu_n < ncpus &&
		    cpu_n != cpu_number())
			db_startcpu(cpu_n);
		else
			db_printf("Invalid cpu %d\n", (int)addr);
	} else {
		for (cpu_n = 0; cpu_n < ncpus; cpu_n++) {
			if (cpu_n != cpu_number()) {
				db_startcpu(cpu_n);
			}
		}
	}
}

void
db_stopproc_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	int cpu_n;

	if (have_addr) {
		cpu_n = addr;
		if (cpu_n >= 0 && cpu_n < ncpus &&
		    cpu_n != cpu_number())
			db_stopcpu(cpu_n);
		else
			db_printf("Invalid cpu %d\n", (int)addr);
	} else {
		for (cpu_n = 0; cpu_n < ncpus; cpu_n++) {
			if (cpu_n != cpu_number()) {
				db_stopcpu(cpu_n);
			}
		}
	}
}

void
db_startcpu(int cpu)
{
	if (cpu != cpu_number() && cpu < ncpus) {
		db_mtx_enter(&ddb_mp_mutex);
		cpu_info[cpu].ci_ddb_paused = CI_DDB_RUNNING;
		db_mtx_leave(&ddb_mp_mutex);
	}
}

void
db_stopcpu(int cpu)
{
	db_mtx_enter(&ddb_mp_mutex);
	if (cpu != cpu_number() && cpu < ncpus &&
	    cpu_info[cpu].ci_ddb_paused != CI_DDB_STOPPED) {
		cpu_info[cpu].ci_ddb_paused = CI_DDB_SHOULDSTOP;
		db_mtx_leave(&ddb_mp_mutex);
		intr_send_ipi(&cpu_info[cpu], IPI_DDB);
	} else {
		db_mtx_leave(&ddb_mp_mutex);
	}
}

#endif
