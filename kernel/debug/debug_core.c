/*
 * Kernel Debug Core
 *
 * Maintainer: Jason Wessel <jason.wessel@windriver.com>
 *
 * Copyright (C) 2000-2001 VERITAS Software Corporation.
 * Copyright (C) 2002-2004 Timesys Corporation
 * Copyright (C) 2003-2004 Amit S. Kale <amitkale@linsyssoft.com>
 * Copyright (C) 2004 Pavel Machek <pavel@ucw.cz>
 * Copyright (C) 2004-2006 Tom Rini <trini@kernel.crashing.org>
 * Copyright (C) 2004-2006 LinSysSoft Technologies Pvt. Ltd.
 * Copyright (C) 2005-2009 Wind River Systems, Inc.
 * Copyright (C) 2007 MontaVista Software, Inc.
 * Copyright (C) 2008 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *
 * Contributors at various stages not listed above:
 *  Jason Wessel ( jason.wessel@windriver.com )
 *  George Anzinger <george@mvista.com>
 *  Anurekh Saxena (anurekh.saxena@timesys.com)
 *  Lake Stevens Instrument Division (Glenn Engel)
 *  Jim Kingdon, Cygnus Support.
 *
 * Original KGDB stub: David Grothe <dave@gcom.com>,
 * Tigran Aivazian <tigran@sco.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */
#include <linux/pid_namespace.h>
#include <linux/clocksource.h>
#include <linux/serial_core.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/console.h>
#include <linux/threads.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ptrace.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/sysrq.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/kgdb.h>
#include <linux/kdb.h>
#include <linux/pid.h>
#include <linux/smp.h>
#include <linux/mm.h>
#include <linux/rcupdate.h>

#include <asm/cacheflush.h>
#include <asm/byteorder.h>
#include <linux/atomic.h>

#include "debug_core.h"

static int kgdb_break_asap;

struct debuggerinfo_struct kgdb_info[NR_CPUS];

/**
 * kgdb_connected - Is a host GDB connected to us?
 */
int				kgdb_connected;
EXPORT_SYMBOL_GPL(kgdb_connected);

/* All the KGDB handlers are installed */
int			kgdb_io_module_registered;

/* Guard for recursive entry */
static int			exception_level;

struct kgdb_io		*dbg_io_ops;
static DEFINE_SPINLOCK(kgdb_registration_lock);

/* Action for the reboot notifiter, a global allow kdb to change it */
static int kgdbreboot;
/* kgdb console driver is loaded */
static int kgdb_con_registered;
/* determine if kgdb console output should be used */
static int kgdb_use_con;
/* Flag for alternate operations for early debugging */
bool dbg_is_early = true;
/* Next cpu to become the master debug core */
int dbg_switch_cpu;

/* Use kdb or gdbserver mode */
int dbg_kdb_mode = 1;

static int __init opt_kgdb_con(char *str)
{
	kgdb_use_con = 1;
	return 0;
}

early_param("kgdbcon", opt_kgdb_con);

module_param(kgdb_use_con, int, 0644);
module_param(kgdbreboot, int, 0644);

/*
 * Holds information about breakpoints in a kernel. These breakpoints are
 * added and removed by gdb.
 */
static struct kgdb_bkpt		kgdb_break[KGDB_MAX_BREAKPOINTS] = {
	[0 ... KGDB_MAX_BREAKPOINTS-1] = { .state = BP_UNDEFINED }
};

/*
 * The CPU# of the active CPU, or -1 if none:
 */
atomic_t			kgdb_active = ATOMIC_INIT(-1);
EXPORT_SYMBOL_GPL(kgdb_active);
static DEFINE_RAW_SPINLOCK(dbg_master_lock);
static DEFINE_RAW_SPINLOCK(dbg_slave_lock);

/*
 * We use NR_CPUs not PERCPU, in case kgdb is used to debug early
 * bootup code (which might not have percpu set up yet):
 */
static atomic_t			masters_in_kgdb;
static atomic_t			slaves_in_kgdb;
static atomic_t			kgdb_break_tasklet_var;
atomic_t			kgdb_setting_breakpoint;

struct task_struct		*kgdb_usethread;
struct task_struct		*kgdb_contthread;

int				kgdb_single_step;
static pid_t			kgdb_sstep_pid;

/* to keep track of the CPU which is doing the single stepping*/
atomic_t			kgdb_cpu_doing_single_step = ATOMIC_INIT(-1);

/*
 * If you are debugging a problem where roundup (the collection of
 * all other CPUs) is a problem [this should be extremely rare],
 * then use the nokgdbroundup option to avoid roundup. In that case
 * the other CPUs might interfere with your debugging context, so
 * use this with care:
 */
static int kgdb_do_roundup = 1;

static int __init opt_nokgdbroundup(char *str)
{
	kgdb_do_roundup = 0;

	return 0;
}

early_param("nokgdbroundup", opt_nokgdbroundup);

/*
 * Finally, some KGDB code :-)
 */

/*
 * Weak aliases for breakpoint management,
 * can be overriden by architectures when needed:
 */
int __weak kgdb_arch_set_breakpoint(struct kgdb_bkpt *bpt)
{
	int err;

	err = probe_kernel_read(bpt->saved_instr, (char *)bpt->bpt_addr,
				BREAK_INSTR_SIZE);
	if (err)
		return err;
	err = probe_kernel_write((char *)bpt->bpt_addr,
				 arch_kgdb_ops.gdb_bpt_instr, BREAK_INSTR_SIZE);
	return err;
}

int __weak kgdb_arch_remove_breakpoint(struct kgdb_bkpt *bpt)
{
	return probe_kernel_write((char *)bpt->bpt_addr,
				  (char *)bpt->saved_instr, BREAK_INSTR_SIZE);
}

int __weak kgdb_validate_break_address(unsigned long addr)
{
	struct kgdb_bkpt tmp;
	int err;
	/* Validate setting the breakpoint and then removing it.  If the
	 * remove fails, the kernel needs to emit a bad message because we
	 * are deep trouble not being able to put things back the way we
	 * found them.
	 */
	tmp.bpt_addr = addr;
	err = kgdb_arch_set_breakpoint(&tmp);
	if (err)
		return err;
	err = kgdb_arch_remove_breakpoint(&tmp);
	if (err)
		printk(KERN_ERR "KGDB: Critical breakpoint error, kernel "
		   "memory destroyed at: %lx", addr);
	return err;
}

unsigned long __weak kgdb_arch_pc(int exception, struct pt_regs *regs)
{
	return instruction_pointer(regs);
}

int __weak kgdb_arch_init(void)
{
	return 0;
}

int __weak kgdb_skipexception(int exception, struct pt_regs *regs)
{
	return 0;
}

/*
 * Some architectures need cache flushes when we set/clear a
 * breakpoint:
 */
static void kgdb_flush_swbreak_addr(unsigned long addr)
{
	if (!CACHE_FLUSH_IS_SAFE)
		return;

	if (current->mm && current->mm->mmap_cache) {
		flush_cache_range(current->mm->mmap_cache,
				  addr, addr + BREAK_INSTR_SIZE);
	}
	/* Force flush instruction cache if it was outside the mm */
	flush_icache_range(addr, addr + BREAK_INSTR_SIZE);
}

/*
 * SW breakpoint management:
 */
int dbg_activate_sw_breakpoints(void)
{
	int error;
	int ret = 0;
	int i;

	for (i = 0; i < KGDB_MAX_BREAKPOINTS; i++) {
		if (kgdb_break[i].state != BP_SET)
			continue;

		error = kgdb_arch_set_breakpoint(&kgdb_break[i]);
		if (error) {
			ret = error;
			printk(KERN_INFO "KGDB: BP install failed: %lx",
			       kgdb_break[i].bpt_addr);
			continue;
		}

		kgdb_flush_swbreak_addr(kgdb_break[i].bpt_addr);
		kgdb_break[i].state = BP_ACTIVE;
	}
	return ret;
}

int dbg_set_sw_break(unsigned long addr)
{
	int err = kgdb_validate_break_address(addr);
	int breakno = -1;
	int i;

	if (err)
		return err;

	for (i = 0; i < KGDB_MAX_BREAKPOINTS; i++) {
		if ((kgdb_break[i].state == BP_SET) &&
					(kgdb_break[i].bpt_addr == addr))
			return -EEXIST;
	}
	for (i = 0; i < KGDB_MAX_BREAKPOINTS; i++) {
		if (kgdb_break[i].state == BP_REMOVED &&
					kgdb_break[i].bpt_addr == addr) {
			breakno = i;
			break;
		}
	}

	if (breakno == -1) {
		for (i = 0; i < KGDB_MAX_BREAKPOINTS; i++) {
			if (kgdb_break[i].state == BP_UNDEFINED) {
				breakno = i;
				break;
			}
		}
	}

	if (breakno == -1)
		return -E2BIG;

	kgdb_break[breakno].state = BP_SET;
	kgdb_break[breakno].type = BP_BREAKPOINT;
	kgdb_break[breakno].bpt_addr = addr;

	return 0;
}

int dbg_deactivate_sw_breakpoints(void)
{
	int error;
	int ret = 0;
	int i;

	for (i = 0; i < KGDB_MAX_BREAKPOINTS; i++) {
		if (kgdb_break[i].state != BP_ACTIVE)
			continue;
		error = kgdb_arch_remove_breakpoint(&kgdb_break[i]);
		if (error) {
			printk(KERN_INFO "KGDB: BP remove failed: %lx\n",
			       kgdb_break[i].bpt_addr);
			ret = error;
		}

		kgdb_flush_swbreak_addr(kgdb_break[i].bpt_addr);
		kgdb_break[i].state = BP_SET;
	}
	return ret;
}

int dbg_remove_sw_break(unsigned long addr)
{
	int i;

	for (i = 0; i < KGDB_MAX_BREAKPOINTS; i++) {
		if ((kgdb_break[i].state == BP_SET) &&
				(kgdb_break[i].bpt_addr == addr)) {
			kgdb_break[i].state = BP_REMOVED;
			return 0;
		}
	}
	return -ENOENT;
}

int kgdb_isremovedbreak(unsigned long addr)
{
	int i;

	for (i = 0; i < KGDB_MAX_BREAKPOINTS; i++) {
		if ((kgdb_break[i].state == BP_REMOVED) &&
					(kgdb_break[i].bpt_addr == addr))
			return 1;
	}
	return 0;
}

int dbg_remove_all_break(void)
{
	int error;
	int i;

	/* Clear memory breakpoints. */
	for (i = 0; i < KGDB_MAX_BREAKPOINTS; i++) {
		if (kgdb_break[i].state != BP_ACTIVE)
			goto setundefined;
		error = kgdb_arch_remove_breakpoint(&kgdb_break[i]);
		if (error)
			printk(KERN_ERR "KGDB: breakpoint remove failed: %lx\n",
			       kgdb_break[i].bpt_addr);
setundefined:
		kgdb_break[i].state = BP_UNDEFINED;
	}

	/* Clear hardware breakpoints. */
	if (arch_kgdb_ops.remove_all_hw_break)
		arch_kgdb_ops.remove_all_hw_break();

	return 0;
}

/*
 * Return true if there is a valid kgdb I/O module.  Also if no
 * debugger is attached a message can be printed to the console about
 * waiting for the debugger to attach.
 *
 * The print_wait argument is only to be true when called from inside
 * the core kgdb_handle_exception, because it will wait for the
 * debugger to attach.
 */
static int kgdb_io_ready(int print_wait)
{
	if (!dbg_io_ops)
		return 0;
	if (kgdb_connected)
		return 1;
	if (atomic_read(&kgdb_setting_breakpoint))
		return 1;
	if (print_wait) {
#ifdef CONFIG_KGDB_KDB
		if (!dbg_kdb_mode)
			printk(KERN_CRIT "KGDB: waiting... or $3#33 for KDB\n");
#else
		printk(KERN_CRIT "KGDB: Waiting for remote debugger\n");
#endif
	}
	return 1;
}

static int kgdb_reenter_check(struct kgdb_state *ks)
{
	unsigned long addr;

	if (atomic_read(&kgdb_active) != raw_smp_processor_id())
		return 0;

	/* Panic on recursive debugger calls: */
	exception_level++;
	addr = kgdb_arch_pc(ks->ex_vector, ks->linux_regs);
	dbg_deactivate_sw_breakpoints();

	/*
	 * If the break point removed ok at the place exception
	 * occurred, try to recover and print a warning to the end
	 * user because the user planted a breakpoint in a place that
	 * KGDB needs in order to function.
	 */
	if (dbg_remove_sw_break(addr) == 0) {
		exception_level = 0;
		kgdb_skipexception(ks->ex_vector, ks->linux_regs);
		dbg_activate_sw_breakpoints();
		printk(KERN_CRIT "KGDB: re-enter error: breakpoint removed %lx\n",
			addr);
		WARN_ON_ONCE(1);

		return 1;
	}
	dbg_remove_all_break();
	kgdb_skipexception(ks->ex_vector, ks->linux_regs);

	if (exception_level > 1) {
		dump_stack();
		panic("Recursive entry to debugger");
	}

	printk(KERN_CRIT "KGDB: re-enter exception: ALL breakpoints killed\n");
#ifdef CONFIG_KGDB_KDB
	/* Allow kdb to debug itself one level */
	return 0;
#endif
	dump_stack();
	panic("Recursive entry to debugger");

	return 1;
}

static void dbg_touch_watchdogs(void)
{
	touch_softlockup_watchdog_sync();
	clocksource_touch_watchdog();
	rcu_cpu_stall_reset();
}

static int kgdb_cpu_enter(struct kgdb_state *ks, struct pt_regs *regs,
		int exception_state)
{
	unsigned long flags;
	int sstep_tries = 100;
	int error;
	int cpu;
	int trace_on = 0;
	int online_cpus = num_online_cpus();

	kgdb_info[ks->cpu].enter_kgdb++;
	kgdb_info[ks->cpu].exception_state |= exception_state;

	if (exception_state == DCPU_WANT_MASTER)
		atomic_inc(&masters_in_kgdb);
	else
		atomic_inc(&slaves_in_kgdb);

	if (arch_kgdb_ops.disable_hw_break)
		arch_kgdb_ops.disable_hw_break(regs);

acquirelock:
	/*
	 * Interrupts will be restored by the 'trap return' code, except when
	 * single stepping.
	 */
	local_irq_save(flags);

	cpu = ks->cpu;
	kgdb_info[cpu].debuggerinfo = regs;
	kgdb_info[cpu].task = current;
	kgdb_info[cpu].ret_state = 0;
	kgdb_info[cpu].irq_depth = hardirq_count() >> HARDIRQ_SHIFT;

	/* Make sure the above info reaches the primary CPU */
	smp_mb();

	if (exception_level == 1) {
		if (raw_spin_trylock(&dbg_master_lock))
			atomic_xchg(&kgdb_active, cpu);
		goto cpu_master_loop;
	}

	/*
	 * CPU will loop if it is a slave or request to become a kgdb
	 * master cpu and acquire the kgdb_active lock:
	 */
	while (1) {
cpu_loop:
		if (kgdb_info[cpu].exception_state & DCPU_NEXT_MASTER) {
			kgdb_info[cpu].exception_state &= ~DCPU_NEXT_MASTER;
			goto cpu_master_loop;
		} else if (kgdb_info[cpu].exception_state & DCPU_WANT_MASTER) {
			if (raw_spin_trylock(&dbg_master_lock)) {
				atomic_xchg(&kgdb_active, cpu);
				break;
			}
		} else if (kgdb_info[cpu].exception_state & DCPU_IS_SLAVE) {
			if (!raw_spin_is_locked(&dbg_slave_lock))
				goto return_normal;
		} else {
return_normal:
			/* Return to normal operation by executing any
			 * hw breakpoint fixup.
			 */
			if (arch_kgdb_ops.correct_hw_break)
				arch_kgdb_ops.correct_hw_break();
			if (trace_on)
				tracing_on();
			kgdb_info[cpu].exception_state &=
				~(DCPU_WANT_MASTER | DCPU_IS_SLAVE);
			kgdb_info[cpu].enter_kgdb--;
			smp_mb__before_atomic_dec();
			atomic_dec(&slaves_in_kgdb);
			dbg_touch_watchdogs();
			local_irq_restore(flags);
			return 0;
		}
		cpu_relax();
	}

	/*
	 * For single stepping, try to only enter on the processor
	 * that was single stepping.  To guard against a deadlock, the
	 * kernel will only try for the value of sstep_tries before
	 * giving up and continuing on.
	 */
	if (atomic_read(&kgdb_cpu_doing_single_step) != -1 &&
	    (kgdb_info[cpu].task &&
	     kgdb_info[cpu].task->pid != kgdb_sstep_pid) && --sstep_tries) {
		atomic_set(&kgdb_active, -1);
		raw_spin_unlock(&dbg_master_lock);
		dbg_touch_watchdogs();
		local_irq_restore(flags);

		goto acquirelock;
	}

	if (!kgdb_io_ready(1)) {
		kgdb_info[cpu].ret_state = 1;
		goto kgdb_restore; /* No I/O connection, resume the system */
	}

	/*
	 * Don't enter if we have hit a removed breakpoint.
	 */
	if (kgdb_skipexception(ks->ex_vector, ks->linux_regs))
		goto kgdb_restore;

	/* Call the I/O driver's pre_exception routine */
	if (dbg_io_ops->pre_exception)
		dbg_io_ops->pre_exception();

	/*
	 * Get the passive CPU lock which will hold all the non-primary
	 * CPU in a spin state while the debugger is active
	 */
	if (!kgdb_single_step)
		raw_spin_lock(&dbg_slave_lock);

#ifdef CONFIG_SMP
	/* If send_ready set, slaves are already waiting */
	if (ks->send_ready)
		atomic_set(ks->send_ready, 1);

	/* Signal the other CPUs to enter kgdb_wait() */
	else if ((!kgdb_single_step) && kgdb_do_roundup)
		kgdb_roundup_cpus(flags);
#endif

	/*
	 * Wait for the other CPUs to be notified and be waiting for us:
	 */
	while (kgdb_do_roundup && (atomic_read(&masters_in_kgdb) +
				atomic_read(&slaves_in_kgdb)) != online_cpus)
		cpu_relax();

	/*
	 * At this point the primary processor is completely
	 * in the debugger and all secondary CPUs are quiescent
	 */
	dbg_deactivate_sw_breakpoints();
	kgdb_single_step = 0;
	kgdb_contthread = current;
	exception_level = 0;
	trace_on = tracing_is_on();
	if (trace_on)
		tracing_off();

	while (1) {
cpu_master_loop:
		if (dbg_kdb_mode) {
			kgdb_connected = 1;
			error = kdb_stub(ks);
			if (error == -1)
				continue;
			kgdb_connected = 0;
		} else {
			error = gdb_serial_stub(ks);
		}

		if (error == DBG_PASS_EVENT) {
			dbg_kdb_mode = !dbg_kdb_mode;
		} else if (error == DBG_SWITCH_CPU_EVENT) {
			kgdb_info[dbg_switch_cpu].exception_state |=
				DCPU_NEXT_MASTER;
			goto cpu_loop;
		} else {
			kgdb_info[cpu].ret_state = error;
			break;
		}
	}

	/* Call the I/O driver's post_exception routine */
	if (dbg_io_ops->post_exception)
		dbg_io_ops->post_exception();

	if (!kgdb_single_step) {
		raw_spin_unlock(&dbg_slave_lock);
		/* Wait till all the CPUs have quit from the debugger. */
		while (kgdb_do_roundup && atomic_read(&slaves_in_kgdb))
			cpu_relax();
	}

kgdb_restore:
	if (atomic_read(&kgdb_cpu_doing_single_step) != -1) {
		int sstep_cpu = atomic_read(&kgdb_cpu_doing_single_step);
		if (kgdb_info[sstep_cpu].task)
			kgdb_sstep_pid = kgdb_info[sstep_cpu].task->pid;
		else
			kgdb_sstep_pid = 0;
	}
	if (arch_kgdb_ops.correct_hw_break)
		arch_kgdb_ops.correct_hw_break();
	if (trace_on)
		tracing_on();

	kgdb_info[cpu].exception_state &=
		~(DCPU_WANT_MASTER | DCPU_IS_SLAVE);
	kgdb_info[cpu].enter_kgdb--;
	smp_mb__before_atomic_dec();
	atomic_dec(&masters_in_kgdb);
	/* Free kgdb_active */
	atomic_set(&kgdb_active, -1);
	raw_spin_unlock(&dbg_master_lock);
	dbg_touch_watchdogs();
	local_irq_restore(flags);

	return kgdb_info[cpu].ret_state;
}

/*
 * kgdb_handle_exception() - main entry point from a kernel exception
 *
 * Locking hierarchy:
 *	interface locks, if any (begin_session)
 *	kgdb lock (kgdb_active)
 */
int
kgdb_handle_exception(int evector, int signo, int ecode, struct pt_regs *regs)
{
	struct kgdb_state kgdb_var;
	struct kgdb_state *ks = &kgdb_var;
	int ret = 0;

	if (arch_kgdb_ops.enable_nmi)
		arch_kgdb_ops.enable_nmi(0);

	memset(ks, 0, sizeof(struct kgdb_state));
	ks->cpu			= raw_smp_processor_id();
	ks->ex_vector		= evector;
	ks->signo		= signo;
	ks->err_code		= ecode;
	ks->linux_regs		= regs;

	if (kgdb_reenter_check(ks))
		goto out; /* Ouch, double exception ! */
	if (kgdb_info[ks->cpu].enter_kgdb != 0)
		goto out;

	ret = kgdb_cpu_enter(ks, regs, DCPU_WANT_MASTER);
out:
	if (arch_kgdb_ops.enable_nmi)
		arch_kgdb_ops.enable_nmi(1);
	return ret;
}

/*
 * GDB places a breakpoint at this function to know dynamically
 * loaded objects. It's not defined static so that only one instance with this
 * name exists in the kernel.
 */

static int module_event(struct notifier_block *self, unsigned long val,
	void *data)
{
	return 0;
}

static struct notifier_block dbg_module_load_nb = {
	.notifier_call	= module_event,
};

int kgdb_nmicallback(int cpu, void *regs)
{
#ifdef CONFIG_SMP
	struct kgdb_state kgdb_var;
	struct kgdb_state *ks = &kgdb_var;

	memset(ks, 0, sizeof(struct kgdb_state));
	ks->cpu			= cpu;
	ks->linux_regs		= regs;

	if (kgdb_info[ks->cpu].enter_kgdb == 0 &&
			raw_spin_is_locked(&dbg_master_lock)) {
		kgdb_cpu_enter(ks, regs, DCPU_IS_SLAVE);
		return 0;
	}
#endif
	return 1;
}

int kgdb_nmicallin(int cpu, int trapnr, void *regs, atomic_t *send_ready)
{
#ifdef CONFIG_SMP
	if (!kgdb_io_ready(0) || !send_ready)
		return 1;

	if (kgdb_info[cpu].enter_kgdb == 0) {
		struct kgdb_state kgdb_var;
		struct kgdb_state *ks = &kgdb_var;

		memset(ks, 0, sizeof(struct kgdb_state));
		ks->cpu			= cpu;
		ks->ex_vector		= trapnr;
		ks->signo		= SIGTRAP;
		ks->err_code		= KGDB_KDB_REASON_SYSTEM_NMI;
		ks->linux_regs		= regs;
		ks->send_ready		= send_ready;
		kgdb_cpu_enter(ks, regs, DCPU_WANT_MASTER);
		return 0;
	}
#endif
	return 1;
}

static void kgdb_console_write(struct console *co, const char *s,
   unsigned count)
{
	unsigned long flags;

	/* If we're debugging, or KGDB has not connected, don't try
	 * and print. */
	if (!kgdb_connected || atomic_read(&kgdb_active) != -1 || dbg_kdb_mode)
		return;

	local_irq_save(flags);
	gdbstub_msg_write(s, count);
	local_irq_restore(flags);
}

static struct console kgdbcons = {
	.name		= "kgdb",
	.write		= kgdb_console_write,
	.flags		= CON_PRINTBUFFER | CON_ENABLED,
	.index		= -1,
};

#ifdef CONFIG_MAGIC_SYSRQ
static void sysrq_handle_dbg(int key)
{
	if (!dbg_io_ops) {
		printk(KERN_CRIT "ERROR: No KGDB I/O module available\n");
		return;
	}
	if (!kgdb_connected) {
#ifdef CONFIG_KGDB_KDB
		if (!dbg_kdb_mode)
			printk(KERN_CRIT "KGDB or $3#33 for KDB\n");
#else
		printk(KERN_CRIT "Entering KGDB\n");
#endif
	}

	kgdb_breakpoint();
}

static struct sysrq_key_op sysrq_dbg_op = {
	.handler	= sysrq_handle_dbg,
	.help_msg	= "debug(g)",
	.action_msg	= "DEBUG",
};
#endif

static int kgdb_panic_event(struct notifier_block *self,
			    unsigned long val,
			    void *data)
{
	if (dbg_kdb_mode)
		kdb_printf("PANIC: %s\n", (char *)data);
	kgdb_breakpoint();
	return NOTIFY_DONE;
}

static struct notifier_block kgdb_panic_event_nb = {
       .notifier_call	= kgdb_panic_event,
       .priority	= INT_MAX,
};

void __weak kgdb_arch_late(void)
{
}

void __init dbg_late_init(void)
{
	dbg_is_early = false;
	if (kgdb_io_module_registered)
		kgdb_arch_late();
	kdb_init(KDB_INIT_FULL);
}

static int
dbg_notify_reboot(struct notifier_block *this, unsigned long code, void *x)
{
	/*
	 * Take the following action on reboot notify depending on value:
	 *    1 == Enter debugger
	 *    0 == [the default] detatch debug client
	 *   -1 == Do nothing... and use this until the board resets
	 */
	switch (kgdbreboot) {
	case 1:
		kgdb_breakpoint();
	case -1:
		goto done;
	}
	if (!dbg_kdb_mode)
		gdbstub_exit(code);
done:
	return NOTIFY_DONE;
}

static struct notifier_block dbg_reboot_notifier = {
	.notifier_call		= dbg_notify_reboot,
	.next			= NULL,
	.priority		= INT_MAX,
};

static void kgdb_register_callbacks(void)
{
	if (!kgdb_io_module_registered) {
		kgdb_io_module_registered = 1;
		kgdb_arch_init();
		if (!dbg_is_early)
			kgdb_arch_late();
		register_module_notifier(&dbg_module_load_nb);
		register_reboot_notifier(&dbg_reboot_notifier);
		atomic_notifier_chain_register(&panic_notifier_list,
					       &kgdb_panic_event_nb);
#ifdef CONFIG_MAGIC_SYSRQ
		register_sysrq_key('g', &sysrq_dbg_op);
#endif
		if (kgdb_use_con && !kgdb_con_registered) {
			register_console(&kgdbcons);
			kgdb_con_registered = 1;
		}
	}
}

static void kgdb_unregister_callbacks(void)
{
	/*
	 * When this routine is called KGDB should unregister from the
	 * panic handler and clean up, making sure it is not handling any
	 * break exceptions at the time.
	 */
	if (kgdb_io_module_registered) {
		kgdb_io_module_registered = 0;
		unregister_reboot_notifier(&dbg_reboot_notifier);
		unregister_module_notifier(&dbg_module_load_nb);
		atomic_notifier_chain_unregister(&panic_notifier_list,
					       &kgdb_panic_event_nb);
		kgdb_arch_exit();
#ifdef CONFIG_MAGIC_SYSRQ
		unregister_sysrq_key('g', &sysrq_dbg_op);
#endif
		if (kgdb_con_registered) {
			unregister_console(&kgdbcons);
			kgdb_con_registered = 0;
		}
	}
}

/*
 * There are times a tasklet needs to be used vs a compiled in
 * break point so as to cause an exception outside a kgdb I/O module,
 * such as is the case with kgdboe, where calling a breakpoint in the
 * I/O driver itself would be fatal.
 */
static void kgdb_tasklet_bpt(unsigned long ing)
{
	kgdb_breakpoint();
	atomic_set(&kgdb_break_tasklet_var, 0);
}

static DECLARE_TASKLET(kgdb_tasklet_breakpoint, kgdb_tasklet_bpt, 0);

void kgdb_schedule_breakpoint(void)
{
	if (atomic_read(&kgdb_break_tasklet_var) ||
		atomic_read(&kgdb_active) != -1 ||
		atomic_read(&kgdb_setting_breakpoint))
		return;
	atomic_inc(&kgdb_break_tasklet_var);
	tasklet_schedule(&kgdb_tasklet_breakpoint);
}
EXPORT_SYMBOL_GPL(kgdb_schedule_breakpoint);

static void kgdb_initial_breakpoint(void)
{
	kgdb_break_asap = 0;

	printk(KERN_CRIT "kgdb: Waiting for connection from remote gdb...\n");
	kgdb_breakpoint();
}

/**
 *	kgdb_register_io_module - register KGDB IO module
 *	@new_dbg_io_ops: the io ops vector
 *
 *	Register it with the KGDB core.
 */
int kgdb_register_io_module(struct kgdb_io *new_dbg_io_ops)
{
	int err;

	spin_lock(&kgdb_registration_lock);

	if (dbg_io_ops) {
		spin_unlock(&kgdb_registration_lock);

		printk(KERN_ERR "kgdb: Another I/O driver is already "
				"registered with KGDB.\n");
		return -EBUSY;
	}

	if (new_dbg_io_ops->init) {
		err = new_dbg_io_ops->init();
		if (err) {
			spin_unlock(&kgdb_registration_lock);
			return err;
		}
	}

	dbg_io_ops = new_dbg_io_ops;

	spin_unlock(&kgdb_registration_lock);

	printk(KERN_INFO "kgdb: Registered I/O driver %s.\n",
	       new_dbg_io_ops->name);

	/* Arm KGDB now. */
	kgdb_register_callbacks();

	if (kgdb_break_asap)
		kgdb_initial_breakpoint();

	return 0;
}
EXPORT_SYMBOL_GPL(kgdb_register_io_module);

/**
 *	kkgdb_unregister_io_module - unregister KGDB IO module
 *	@old_dbg_io_ops: the io ops vector
 *
 *	Unregister it with the KGDB core.
 */
void kgdb_unregister_io_module(struct kgdb_io *old_dbg_io_ops)
{
	BUG_ON(kgdb_connected);

	/*
	 * KGDB is no longer able to communicate out, so
	 * unregister our callbacks and reset state.
	 */
	kgdb_unregister_callbacks();

	spin_lock(&kgdb_registration_lock);

	WARN_ON_ONCE(dbg_io_ops != old_dbg_io_ops);
	dbg_io_ops = NULL;

	spin_unlock(&kgdb_registration_lock);

	printk(KERN_INFO
		"kgdb: Unregistered I/O driver %s, debugger disabled.\n",
		old_dbg_io_ops->name);
}
EXPORT_SYMBOL_GPL(kgdb_unregister_io_module);

int dbg_io_get_char(void)
{
	int ret = dbg_io_ops->read_char();
	if (ret == NO_POLL_CHAR)
		return -1;
	if (!dbg_kdb_mode)
		return ret;
	if (ret == 127)
		return 8;
	return ret;
}

/**
 * kgdb_breakpoint - generate breakpoint exception
 *
 * This function will generate a breakpoint exception.  It is used at the
 * beginning of a program to sync up with a debugger and can be used
 * otherwise as a quick means to stop program execution and "break" into
 * the debugger.
 */
void kgdb_breakpoint(void)
{
	atomic_inc(&kgdb_setting_breakpoint);
	wmb(); /* Sync point before breakpoint */
	arch_kgdb_breakpoint();
	wmb(); /* Sync point after breakpoint */
	atomic_dec(&kgdb_setting_breakpoint);
}
EXPORT_SYMBOL_GPL(kgdb_breakpoint);

static int __init opt_kgdb_wait(char *str)
{
	kgdb_break_asap = 1;

	kdb_init(KDB_INIT_EARLY);
	if (kgdb_io_module_registered)
		kgdb_initial_breakpoint();

	return 0;
}

early_param("kgdbwait", opt_kgdb_wait);
