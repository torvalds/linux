/*
 * Kernel Debugger Architecture Independent Stack Traceback
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1999-2004 Silicon Graphics, Inc.  All Rights Reserved.
 * Copyright (c) 2009 Wind River Systems, Inc.  All Rights Reserved.
 */

#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/sched/signal.h>
#include <linux/sched/debug.h>
#include <linux/kdb.h>
#include <linux/nmi.h>
#include "kdb_private.h"


static void kdb_show_stack(struct task_struct *p, void *addr)
{
	int old_lvl = console_loglevel;
	console_loglevel = CONSOLE_LOGLEVEL_MOTORMOUTH;
	kdb_trap_printk++;
	kdb_set_current_task(p);
	if (addr) {
		show_stack((struct task_struct *)p, addr);
	} else if (kdb_current_regs) {
#ifdef CONFIG_X86
		show_stack(p, &kdb_current_regs->sp);
#else
		show_stack(p, NULL);
#endif
	} else {
		show_stack(p, NULL);
	}
	console_loglevel = old_lvl;
	kdb_trap_printk--;
}

/*
 * kdb_bt
 *
 *	This function implements the 'bt' command.  Print a stack
 *	traceback.
 *
 *	bt [<address-expression>]	(addr-exp is for alternate stacks)
 *	btp <pid>			Kernel stack for <pid>
 *	btt <address-expression>	Kernel stack for task structure at
 *					<address-expression>
 *	bta [DRSTCZEUIMA]		All useful processes, optionally
 *					filtered by state
 *	btc [<cpu>]			The current process on one cpu,
 *					default is all cpus
 *
 *	bt <address-expression> refers to a address on the stack, that location
 *	is assumed to contain a return address.
 *
 *	btt <address-expression> refers to the address of a struct task.
 *
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 *	Backtrack works best when the code uses frame pointers.  But even
 *	without frame pointers we should get a reasonable trace.
 *
 *	mds comes in handy when examining the stack to do a manual traceback or
 *	to get a starting point for bt <address-expression>.
 */

static int
kdb_bt1(struct task_struct *p, unsigned long mask,
	int argcount, int btaprompt)
{
	char buffer[2];
	if (kdb_getarea(buffer[0], (unsigned long)p) ||
	    kdb_getarea(buffer[0], (unsigned long)(p+1)-1))
		return KDB_BADADDR;
	if (!kdb_task_state(p, mask))
		return 0;
	kdb_printf("Stack traceback for pid %d\n", p->pid);
	kdb_ps1(p);
	kdb_show_stack(p, NULL);
	if (btaprompt) {
		kdb_getstr(buffer, sizeof(buffer),
			   "Enter <q> to end, <cr> to continue:");
		if (buffer[0] == 'q') {
			kdb_printf("\n");
			return 1;
		}
	}
	touch_nmi_watchdog();
	return 0;
}

int
kdb_bt(int argc, const char **argv)
{
	int diag;
	int argcount = 5;
	int btaprompt = 1;
	int nextarg;
	unsigned long addr;
	long offset;

	/* Prompt after each proc in bta */
	kdbgetintenv("BTAPROMPT", &btaprompt);

	if (strcmp(argv[0], "bta") == 0) {
		struct task_struct *g, *p;
		unsigned long cpu;
		unsigned long mask = kdb_task_state_string(argc ? argv[1] :
							   NULL);
		if (argc == 0)
			kdb_ps_suppressed();
		/* Run the active tasks first */
		for_each_online_cpu(cpu) {
			p = kdb_curr_task(cpu);
			if (kdb_bt1(p, mask, argcount, btaprompt))
				return 0;
		}
		/* Now the inactive tasks */
		kdb_do_each_thread(g, p) {
			if (KDB_FLAG(CMD_INTERRUPT))
				return 0;
			if (task_curr(p))
				continue;
			if (kdb_bt1(p, mask, argcount, btaprompt))
				return 0;
		} kdb_while_each_thread(g, p);
	} else if (strcmp(argv[0], "btp") == 0) {
		struct task_struct *p;
		unsigned long pid;
		if (argc != 1)
			return KDB_ARGCOUNT;
		diag = kdbgetularg((char *)argv[1], &pid);
		if (diag)
			return diag;
		p = find_task_by_pid_ns(pid, &init_pid_ns);
		if (p) {
			kdb_set_current_task(p);
			return kdb_bt1(p, ~0UL, argcount, 0);
		}
		kdb_printf("No process with pid == %ld found\n", pid);
		return 0;
	} else if (strcmp(argv[0], "btt") == 0) {
		if (argc != 1)
			return KDB_ARGCOUNT;
		diag = kdbgetularg((char *)argv[1], &addr);
		if (diag)
			return diag;
		kdb_set_current_task((struct task_struct *)addr);
		return kdb_bt1((struct task_struct *)addr, ~0UL, argcount, 0);
	} else if (strcmp(argv[0], "btc") == 0) {
		unsigned long cpu = ~0;
		struct task_struct *save_current_task = kdb_current_task;
		char buf[80];
		if (argc > 1)
			return KDB_ARGCOUNT;
		if (argc == 1) {
			diag = kdbgetularg((char *)argv[1], &cpu);
			if (diag)
				return diag;
		}
		/* Recursive use of kdb_parse, do not use argv after
		 * this point */
		argv = NULL;
		if (cpu != ~0) {
			if (cpu >= num_possible_cpus() || !cpu_online(cpu)) {
				kdb_printf("no process for cpu %ld\n", cpu);
				return 0;
			}
			sprintf(buf, "btt 0x%p\n", KDB_TSK(cpu));
			kdb_parse(buf);
			return 0;
		}
		kdb_printf("btc: cpu status: ");
		kdb_parse("cpu\n");
		for_each_online_cpu(cpu) {
			sprintf(buf, "btt 0x%p\n", KDB_TSK(cpu));
			kdb_parse(buf);
			touch_nmi_watchdog();
		}
		kdb_set_current_task(save_current_task);
		return 0;
	} else {
		if (argc) {
			nextarg = 1;
			diag = kdbgetaddrarg(argc, argv, &nextarg, &addr,
					     &offset, NULL);
			if (diag)
				return diag;
			kdb_show_stack(kdb_current_task, (void *)addr);
			return 0;
		} else {
			return kdb_bt1(kdb_current_task, ~0UL, argcount, 0);
		}
	}

	/* NOTREACHED */
	return 0;
}
