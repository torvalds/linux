/*	$OpenBSD: db_interface.c,v 1.45 2023/01/30 10:49:05 jsg Exp $	*/
/*	$NetBSD: db_interface.c,v 1.22 1996/05/03 19:42:00 christos Exp $	*/

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
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 *
 *	db_interface.c,v 2.4 1991/02/05 17:11:13 mrt (CMU)
 */

/*
 * Interface to new debugger.
 */
#include <sys/param.h>
#include <sys/systm.h>

#include <dev/cons.h>

#include <machine/db_machdep.h>

#include <ddb/db_command.h>
#include <ddb/db_extern.h>
#include <ddb/db_output.h>
#include <ddb/db_run.h>
#include <ddb/db_var.h>

#include "acpi.h"
#if NACPI > 0
#include <dev/acpi/acpidebug.h>
#endif /* NACPI > 0 */

#include "wsdisplay.h"
#if NWSDISPLAY > 0
#include <dev/wscons/wsdisplayvar.h>
#endif

extern label_t	*db_recover;
extern char *trap_type[];
extern int trap_types;

#ifdef MULTIPROCESSOR
extern volatile int ddb_state;
int		 db_switch_cpu;
long		 db_switch_to_cpu;
#endif

db_regs_t	ddb_regs;

void kdbprinttrap(int, int);
void db_sysregs_cmd(db_expr_t, int, db_expr_t, char *);
#ifdef MULTIPROCESSOR
void db_cpuinfo_cmd(db_expr_t, int, db_expr_t, char *);
void db_startproc_cmd(db_expr_t, int, db_expr_t, char *);
void db_stopproc_cmd(db_expr_t, int, db_expr_t, char *);
void db_ddbproc_cmd(db_expr_t, int, db_expr_t, char *);
#endif /* MULTIPROCESSOR */

/*
 * Print trap reason.
 */
void
kdbprinttrap(int type, int code)
{
	db_printf("kernel: ");
	if (type >= trap_types || type < 0)
		db_printf("type %d", type);
	else
		db_printf("%s", trap_type[type]);
	db_printf(" trap, code=%x\n", code);
}

/*
 *  db_ktrap - field a TRACE or BPT trap
 */
int
db_ktrap(int type, int code, db_regs_t *regs)
{
	int s;

#if NWSDISPLAY > 0
	wsdisplay_enter_ddb();
#endif

	switch (type) {
	case T_BPTFLT:	/* breakpoint */
	case T_TRCTRAP:	/* single_step */
	case T_NMI:	/* NMI */
	case T_NMI|T_USER:
	case -1:	/* keyboard interrupt */
		break;
	default:
		if (!db_panic)
			return (0);

		kdbprinttrap(type, code);
		if (db_recover != 0) {
			db_error("Faulted in DDB; continuing...\n");
			/*NOTREACHED*/
		}
	}

#ifdef MULTIPROCESSOR
	db_mtx_enter(&ddb_mp_mutex);
	if (ddb_state == DDB_STATE_EXITING)
		ddb_state = DDB_STATE_NOT_RUNNING;
	db_mtx_leave(&ddb_mp_mutex);
	while (db_enter_ddb()) {
#endif /* MULTIPROCESSOR */

	/* XXX Should switch to kdb`s own stack here. */

	ddb_regs = *regs;
	if (KERNELMODE(regs->tf_cs, regs->tf_eflags)) {
		/*
		 * Kernel mode - esp and ss not saved
		 */
		ddb_regs.tf_esp = (int)&regs->tf_esp;	/* kernel stack pointer */
		__asm__("movw %%ss,%w0" : "=r" (ddb_regs.tf_ss));
	}
	ddb_regs.tf_cs &= 0xffff;
	ddb_regs.tf_ds &= 0xffff;
	ddb_regs.tf_es &= 0xffff;
	ddb_regs.tf_fs &= 0xffff;
	ddb_regs.tf_gs &= 0xffff;
	ddb_regs.tf_ss &= 0xffff;

	s = splhigh();
	db_active++;
	cnpollc(1);
	db_trap(type, code);
	cnpollc(0);
	db_active--;
	splx(s);

	regs->tf_fs     = ddb_regs.tf_fs & 0xffff;
	regs->tf_gs     = ddb_regs.tf_gs & 0xffff;
	regs->tf_es     = ddb_regs.tf_es & 0xffff;
	regs->tf_ds     = ddb_regs.tf_ds & 0xffff;
	regs->tf_edi    = ddb_regs.tf_edi;
	regs->tf_esi    = ddb_regs.tf_esi;
	regs->tf_ebp    = ddb_regs.tf_ebp;
	regs->tf_ebx    = ddb_regs.tf_ebx;
	regs->tf_edx    = ddb_regs.tf_edx;
	regs->tf_ecx    = ddb_regs.tf_ecx;
	regs->tf_eax    = ddb_regs.tf_eax;
	regs->tf_eip    = ddb_regs.tf_eip;
	regs->tf_cs     = ddb_regs.tf_cs & 0xffff;
	regs->tf_eflags = ddb_regs.tf_eflags;
	if (!KERNELMODE(regs->tf_cs, regs->tf_eflags)) {
		/* ring transit - saved esp and ss valid */
		regs->tf_esp    = ddb_regs.tf_esp;
		regs->tf_ss     = ddb_regs.tf_ss & 0xffff;
	}


#ifdef MULTIPROCESSOR
		if (!db_switch_cpu)
			ddb_state = DDB_STATE_EXITING;
	}
#endif /* MULTIPROCESSOR */
	return (1);
}

void
db_sysregs_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	int64_t idtr, gdtr;
	uint32_t cr;
	uint16_t ldtr, tr;

	__asm__ volatile("sidt %0" : "=m" (idtr));
	db_printf("idtr:   0x%08llx/%04llx\n", idtr >> 16, idtr & 0xffff);

	__asm__ volatile("sgdt %0" : "=m" (gdtr));
	db_printf("gdtr:   0x%08llx/%04llx\n", gdtr >> 16, gdtr & 0xffff);

	__asm__ volatile("sldt %0" : "=g" (ldtr));
	db_printf("ldtr:   0x%04x\n", ldtr);

	__asm__ volatile("str %0" : "=g" (tr));
	db_printf("tr:     0x%04x\n", tr);

	__asm__ volatile("movl %%cr0,%0" : "=r" (cr));
	db_printf("cr0:    0x%08x\n", cr);

	__asm__ volatile("movl %%cr2,%0" : "=r" (cr));
	db_printf("cr2:    0x%08x\n", cr);

	__asm__ volatile("movl %%cr3,%0" : "=r" (cr));
	db_printf("cr3:    0x%08x\n", cr);

	__asm__ volatile("movl %%cr4,%0" : "=r" (cr));
	db_printf("cr4:    0x%08x\n", cr);
}

#ifdef MULTIPROCESSOR
void
db_cpuinfo_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	int i;

	for (i = 0; i < MAXCPUS; i++) {
		if (cpu_info[i] != NULL) {
			db_printf("%c%4d: ", (i == cpu_number()) ? '*' : ' ',
			    cpu_info[i]->ci_dev->dv_unit);
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
			if (cpu_info[i] != NULL && i != cpu_number()) {
				db_stopcpu(i);
			}
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
#endif /* MULTIPROCESSOR */

#if NACPI > 0
const struct db_command db_acpi_cmds[] = {
	{ "disasm",	db_acpi_disasm,		CS_OWN,	NULL },
	{ "showval",	db_acpi_showval,	CS_OWN,	NULL },
	{ "tree",	db_acpi_tree,		0,	NULL },
	{ "trace",	db_acpi_trace,		0,	NULL },
	{ NULL,		NULL,			0,	NULL }
};
#endif /* NACPI > 0 */

const struct db_command db_machine_command_table[] = {
	{ "sysregs",	db_sysregs_cmd,		0,	0 },
#ifdef MULTIPROCESSOR
	{ "cpuinfo",	db_cpuinfo_cmd,		0,	0 },
	{ "startcpu",	db_startproc_cmd,	0,	0 },
	{ "stopcpu",	db_stopproc_cmd,	0,	0 },
	{ "ddbcpu",	db_ddbproc_cmd,		0,	0 },
#endif /* MULTIPROCESSOR */
#if NACPI > 0
	{ "acpi",	NULL,			0,	db_acpi_cmds },
#endif /* NACPI > 0 */
	{ NULL, }
};

void
db_machine_init(void)
{
#ifdef MULTIPROCESSOR
	int i;

	for (i = 0; i < MAXCPUS; i++) {
		if (cpu_info[i] != NULL)
			cpu_info[i]->ci_ddb_paused = CI_DDB_RUNNING;
	}
#endif /* MULTIPROCESSOR */
}

void
db_enter(void)
{
	__asm__("int $3");
}
