/*
 * Created by: Jason Wessel <jason.wessel@windriver.com>
 *
 * Copyright (c) 2009 Wind River Systems, Inc.  All Rights Reserved.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kgdb.h>
#include <linux/kdb.h>
#include <linux/kdebug.h>
#include "kdb_private.h"
#include "../debug_core.h"

/*
 * KDB interface to KGDB internals
 */
get_char_func kdb_poll_funcs[] = {
	dbg_io_get_char,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
};
EXPORT_SYMBOL_GPL(kdb_poll_funcs);

int kdb_poll_idx = 1;
EXPORT_SYMBOL_GPL(kdb_poll_idx);

int kdb_stub(struct kgdb_state *ks)
{
	int error = 0;
	kdb_bp_t *bp;
	unsigned long addr = kgdb_arch_pc(ks->ex_vector, ks->linux_regs);
	kdb_reason_t reason = KDB_REASON_OOPS;
	kdb_dbtrap_t db_result = KDB_DB_NOBPT;
	int i;

	if (KDB_STATE(REENTRY)) {
		reason = KDB_REASON_SWITCH;
		KDB_STATE_CLEAR(REENTRY);
		addr = instruction_pointer(ks->linux_regs);
	}
	ks->pass_exception = 0;
	if (atomic_read(&kgdb_setting_breakpoint))
		reason = KDB_REASON_KEYBOARD;

	for (i = 0, bp = kdb_breakpoints; i < KDB_MAXBPT; i++, bp++) {
		if ((bp->bp_enabled) && (bp->bp_addr == addr)) {
			reason = KDB_REASON_BREAK;
			db_result = KDB_DB_BPT;
			if (addr != instruction_pointer(ks->linux_regs))
				kgdb_arch_set_pc(ks->linux_regs, addr);
			break;
		}
	}
	if (reason == KDB_REASON_BREAK || reason == KDB_REASON_SWITCH) {
		for (i = 0, bp = kdb_breakpoints; i < KDB_MAXBPT; i++, bp++) {
			if (bp->bp_free)
				continue;
			if (bp->bp_addr == addr) {
				bp->bp_delay = 1;
				bp->bp_delayed = 1;
	/*
	 * SSBPT is set when the kernel debugger must single step a
	 * task in order to re-establish an instruction breakpoint
	 * which uses the instruction replacement mechanism.  It is
	 * cleared by any action that removes the need to single-step
	 * the breakpoint.
	 */
				reason = KDB_REASON_BREAK;
				db_result = KDB_DB_BPT;
				KDB_STATE_SET(SSBPT);
				break;
			}
		}
	}

	if (reason != KDB_REASON_BREAK && ks->ex_vector == 0 &&
		ks->signo == SIGTRAP) {
		reason = KDB_REASON_SSTEP;
		db_result = KDB_DB_BPT;
	}
	/* Set initial kdb state variables */
	KDB_STATE_CLEAR(KGDB_TRANS);
	kdb_initial_cpu = atomic_read(&kgdb_active);
	kdb_current_task = kgdb_info[ks->cpu].task;
	kdb_current_regs = kgdb_info[ks->cpu].debuggerinfo;
	/* Remove any breakpoints as needed by kdb and clear single step */
	kdb_bp_remove();
	KDB_STATE_CLEAR(DOING_SS);
	KDB_STATE_CLEAR(DOING_SSB);
	KDB_STATE_SET(PAGER);
	/* zero out any offline cpu data */
	for_each_present_cpu(i) {
		if (!cpu_online(i)) {
			kgdb_info[i].debuggerinfo = NULL;
			kgdb_info[i].task = NULL;
		}
	}
	if (ks->err_code == DIE_OOPS || reason == KDB_REASON_OOPS) {
		ks->pass_exception = 1;
		KDB_FLAG_SET(CATASTROPHIC);
	}
	if (KDB_STATE(SSBPT) && reason == KDB_REASON_SSTEP) {
		KDB_STATE_CLEAR(SSBPT);
		KDB_STATE_CLEAR(DOING_SS);
	} else {
		/* Start kdb main loop */
		error = kdb_main_loop(KDB_REASON_ENTER, reason,
				      ks->err_code, db_result, ks->linux_regs);
	}
	/*
	 * Upon exit from the kdb main loop setup break points and restart
	 * the system based on the requested continue state
	 */
	kdb_initial_cpu = -1;
	kdb_current_task = NULL;
	kdb_current_regs = NULL;
	KDB_STATE_CLEAR(PAGER);
	kdbnearsym_cleanup();
	if (error == KDB_CMD_KGDB) {
		if (KDB_STATE(DOING_KGDB) || KDB_STATE(DOING_KGDB2)) {
	/*
	 * This inteface glue which allows kdb to transition in into
	 * the gdb stub.  In order to do this the '?' or '' gdb serial
	 * packet response is processed here.  And then control is
	 * passed to the gdbstub.
	 */
			if (KDB_STATE(DOING_KGDB))
				gdbstub_state(ks, "?");
			else
				gdbstub_state(ks, "");
			KDB_STATE_CLEAR(DOING_KGDB);
			KDB_STATE_CLEAR(DOING_KGDB2);
		}
		return DBG_PASS_EVENT;
	}
	kdb_bp_install(ks->linux_regs);
	dbg_activate_sw_breakpoints();
	/* Set the exit state to a single step or a continue */
	if (KDB_STATE(DOING_SS))
		gdbstub_state(ks, "s");
	else
		gdbstub_state(ks, "c");

	KDB_FLAG_CLEAR(CATASTROPHIC);

	/* Invoke arch specific exception handling prior to system resume */
	kgdb_info[ks->cpu].ret_state = gdbstub_state(ks, "e");
	if (ks->pass_exception)
		kgdb_info[ks->cpu].ret_state = 1;
	if (error == KDB_CMD_CPU) {
		KDB_STATE_SET(REENTRY);
		/*
		 * Force clear the single step bit because kdb emulates this
		 * differently vs the gdbstub
		 */
		kgdb_single_step = 0;
		dbg_deactivate_sw_breakpoints();
		return DBG_SWITCH_CPU_EVENT;
	}
	return kgdb_info[ks->cpu].ret_state;
}

