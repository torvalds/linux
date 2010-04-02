/*
 * Kernel Debug Core
 *
 * Maintainer: Jason Wessel <jason.wessel@windriver.com>
 *
 * Copyright (C) 2000-2001 VERITAS Software Corporation.
 * Copyright (C) 2002-2004 Timesys Corporation
 * Copyright (C) 2003-2004 Amit S. Kale <amitkale@linsyssoft.com>
 * Copyright (C) 2004 Pavel Machek <pavel@suse.cz>
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
#include <linux/init.h>
#include <linux/kgdb.h>
#include <linux/pid.h>
#include <linux/smp.h>
#include <linux/mm.h>

#include <asm/cacheflush.h>
#include <asm/byteorder.h>
#include <asm/atomic.h>
#include <asm/system.h>

#include "debug_core.h"

static int kgdb_break_asap;

struct debuggerinfo_struct kgdb_info[NR_CPUS];

/**
 * kgdb_connected - Is a host GDB connected to us?
 */
int				kgdb_connected;
EXPORT_SYMBOL_GPL(kgdb_connected);

/* All the KGDB handlers are installed */
static int			kgdb_io_module_registered;

/* Guard for recursive entry */
static int			exception_level;

struct kgdb_io		*dbg_io_ops;
static DEFINE_SPINLOCK(kgdb_registration_lock);

/* kgdb console driver is loaded */
static int kgdb_con_registered;
/* determine if kgdb console output should be used */
static int kgdb_use_con;

static int __init opt_kgdb_con(char *str)
{
	kgdb_use_con = 1;
	return 0;
}

early_param("kgdbcon", opt_kgdb_con);

module_param(kgdb_use_con, int, 0644);

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

/*
 * We use NR_CPUs not PERCPU, in case kgdb is used to debug early
 * bootup code (which might not have percpu set up yet):
 */
static atomic_t			passive_cpu_wait[NR_CPUS];
static atomic_t			cpu_in_kgdb[NR_CPUS];
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
int __weak kgdb_arch_set_breakpoint(unsigned long addr, char *saved_instr)
{
	int err;

	err = probe_kernel_read(saved_instr, (char *)addr, BREAK_INSTR_SIZE);
	if (err)
		return err;

	return probe_kernel_write((char *)addr, arch_kgdb_ops.gdb_bpt_instr,
				  BREAK_INSTR_SIZE);
}

int __weak kgdb_arch_remove_breakpoint(unsigned long addr, char *bundle)
{
	return probe_kernel_write((char *)addr,
				  (char *)bundle, BREAK_INSTR_SIZE);
}

int __weak kgdb_validate_break_address(unsigned long addr)
{
	char tmp_variable[BREAK_INSTR_SIZE];
	int err;
	/* Validate setting the breakpoint and then removing it.  In the
	 * remove fails, the kernel needs to emit a bad message because we
	 * are deep trouble not being able to put things back the way we
	 * found them.
	 */
	err = kgdb_arch_set_breakpoint(addr, tmp_variable);
	if (err)
		return err;
	err = kgdb_arch_remove_breakpoint(addr, tmp_variable);
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

void __weak
kgdb_post_primary_code(struct pt_regs *regs, int e_vector, int err_code)
{
	return;
}

/**
 *	kgdb_disable_hw_debug - Disable hardware debugging while we in kgdb.
 *	@regs: Current &struct pt_regs.
 *
 *	This function will be called if the particular architecture must
 *	disable hardware debugging while it is processing gdb packets or
 *	handling exception.
 */
void __weak kgdb_disable_hw_debug(struct pt_regs *regs)
{
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
	unsigned long addr;
	int error;
	int ret = 0;
	int i;

	for (i = 0; i < KGDB_MAX_BREAKPOINTS; i++) {
		if (kgdb_break[i].state != BP_SET)
			continue;

		addr = kgdb_break[i].bpt_addr;
		error = kgdb_arch_set_breakpoint(addr,
				kgdb_break[i].saved_instr);
		if (error) {
			ret = error;
			printk(KERN_INFO "KGDB: BP install failed: %lx", addr);
			continue;
		}

		kgdb_flush_swbreak_addr(addr);
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

static int kgdb_deactivate_sw_breakpoints(void)
{
	unsigned long addr;
	int error;
	int ret = 0;
	int i;

	for (i = 0; i < KGDB_MAX_BREAKPOINTS; i++) {
		if (kgdb_break[i].state != BP_ACTIVE)
			continue;
		addr = kgdb_break[i].bpt_addr;
		error = kgdb_arch_remove_breakpoint(addr,
					kgdb_break[i].saved_instr);
		if (error) {
			printk(KERN_INFO "KGDB: BP remove failed: %lx\n", addr);
			ret = error;
		}

		kgdb_flush_swbreak_addr(addr);
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
	unsigned long addr;
	int error;
	int i;

	/* Clear memory breakpoints. */
	for (i = 0; i < KGDB_MAX_BREAKPOINTS; i++) {
		if (kgdb_break[i].state != BP_ACTIVE)
			goto setundefined;
		addr = kgdb_break[i].bpt_addr;
		error = kgdb_arch_remove_breakpoint(addr,
				kgdb_break[i].saved_instr);
		if (error)
			printk(KERN_ERR "KGDB: breakpoint remove failed: %lx\n",
			   addr);
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
	if (print_wait)
		printk(KERN_CRIT "KGDB: Waiting for remote debugger\n");
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
	kgdb_deactivate_sw_breakpoints();

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
	dump_stack();
	panic("Recursive entry to debugger");

	return 1;
}

static int kgdb_cpu_enter(struct kgdb_state *ks, struct pt_regs *regs)
{
	unsigned long flags;
	int sstep_tries = 100;
	int error = 0;
	int i, cpu;
	int trace_on = 0;
acquirelock:
	/*
	 * Interrupts will be restored by the 'trap return' code, except when
	 * single stepping.
	 */
	local_irq_save(flags);

	cpu = ks->cpu;
	kgdb_info[cpu].debuggerinfo = regs;
	kgdb_info[cpu].task = current;
	/*
	 * Make sure the above info reaches the primary CPU before
	 * our cpu_in_kgdb[] flag setting does:
	 */
	atomic_inc(&cpu_in_kgdb[cpu]);

	/*
	 * CPU will loop if it is a slave or request to become a kgdb
	 * master cpu and acquire the kgdb_active lock:
	 */
	while (1) {
		if (kgdb_info[cpu].exception_state & DCPU_WANT_MASTER) {
			if (atomic_cmpxchg(&kgdb_active, -1, cpu) == cpu)
				break;
		} else if (kgdb_info[cpu].exception_state & DCPU_IS_SLAVE) {
			if (!atomic_read(&passive_cpu_wait[cpu]))
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
			atomic_dec(&cpu_in_kgdb[cpu]);
			touch_softlockup_watchdog_sync();
			clocksource_touch_watchdog();
			local_irq_restore(flags);
			return 0;
		}
		cpu_relax();
	}

	/*
	 * For single stepping, try to only enter on the processor
	 * that was single stepping.  To gaurd against a deadlock, the
	 * kernel will only try for the value of sstep_tries before
	 * giving up and continuing on.
	 */
	if (atomic_read(&kgdb_cpu_doing_single_step) != -1 &&
	    (kgdb_info[cpu].task &&
	     kgdb_info[cpu].task->pid != kgdb_sstep_pid) && --sstep_tries) {
		atomic_set(&kgdb_active, -1);
		touch_softlockup_watchdog_sync();
		clocksource_touch_watchdog();
		local_irq_restore(flags);

		goto acquirelock;
	}

	if (!kgdb_io_ready(1)) {
		error = 1;
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

	kgdb_disable_hw_debug(ks->linux_regs);

	/*
	 * Get the passive CPU lock which will hold all the non-primary
	 * CPU in a spin state while the debugger is active
	 */
	if (!kgdb_single_step) {
		for (i = 0; i < NR_CPUS; i++)
			atomic_inc(&passive_cpu_wait[i]);
	}

#ifdef CONFIG_SMP
	/* Signal the other CPUs to enter kgdb_wait() */
	if ((!kgdb_single_step) && kgdb_do_roundup)
		kgdb_roundup_cpus(flags);
#endif

	/*
	 * Wait for the other CPUs to be notified and be waiting for us:
	 */
	for_each_online_cpu(i) {
		while (!atomic_read(&cpu_in_kgdb[i]))
			cpu_relax();
	}

	/*
	 * At this point the primary processor is completely
	 * in the debugger and all secondary CPUs are quiescent
	 */
	kgdb_post_primary_code(ks->linux_regs, ks->ex_vector, ks->err_code);
	kgdb_deactivate_sw_breakpoints();
	kgdb_single_step = 0;
	kgdb_contthread = current;
	exception_level = 0;
	trace_on = tracing_is_on();
	if (trace_on)
		tracing_off();

	/* Talk to debugger with gdbserial protocol */
	error = gdb_serial_stub(ks);

	/* Call the I/O driver's post_exception routine */
	if (dbg_io_ops->post_exception)
		dbg_io_ops->post_exception();

	atomic_dec(&cpu_in_kgdb[ks->cpu]);

	if (!kgdb_single_step) {
		for (i = NR_CPUS-1; i >= 0; i--)
			atomic_dec(&passive_cpu_wait[i]);
		/*
		 * Wait till all the CPUs have quit
		 * from the debugger.
		 */
		for_each_online_cpu(i) {
			while (atomic_read(&cpu_in_kgdb[i]))
				cpu_relax();
		}
	}

kgdb_restore:
	if (atomic_read(&kgdb_cpu_doing_single_step) != -1) {
		int sstep_cpu = atomic_read(&kgdb_cpu_doing_single_step);
		if (kgdb_info[sstep_cpu].task)
			kgdb_sstep_pid = kgdb_info[sstep_cpu].task->pid;
		else
			kgdb_sstep_pid = 0;
	}
	if (trace_on)
		tracing_on();
	/* Free kgdb_active */
	atomic_set(&kgdb_active, -1);
	touch_softlockup_watchdog_sync();
	clocksource_touch_watchdog();
	local_irq_restore(flags);

	return error;
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
	int ret;

	ks->cpu			= raw_smp_processor_id();
	ks->ex_vector		= evector;
	ks->signo		= signo;
	ks->ex_vector		= evector;
	ks->err_code		= ecode;
	ks->kgdb_usethreadid	= 0;
	ks->linux_regs		= regs;

	if (kgdb_reenter_check(ks))
		return 0; /* Ouch, double exception ! */
	kgdb_info[ks->cpu].exception_state |= DCPU_WANT_MASTER;
	ret = kgdb_cpu_enter(ks, regs);
	kgdb_info[ks->cpu].exception_state &= ~DCPU_WANT_MASTER;
	return ret;
}

int kgdb_nmicallback(int cpu, void *regs)
{
#ifdef CONFIG_SMP
	struct kgdb_state kgdb_var;
	struct kgdb_state *ks = &kgdb_var;

	memset(ks, 0, sizeof(struct kgdb_state));
	ks->cpu			= cpu;
	ks->linux_regs		= regs;

	if (!atomic_read(&cpu_in_kgdb[cpu]) &&
	    atomic_read(&kgdb_active) != -1 &&
	    atomic_read(&kgdb_active) != cpu) {
		kgdb_info[cpu].exception_state |= DCPU_IS_SLAVE;
		kgdb_cpu_enter(ks, regs);
		kgdb_info[cpu].exception_state &= ~DCPU_IS_SLAVE;
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
	if (!kgdb_connected || atomic_read(&kgdb_active) != -1)
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
static void sysrq_handle_dbg(int key, struct tty_struct *tty)
{
	if (!dbg_io_ops) {
		printk(KERN_CRIT "ERROR: No KGDB I/O module available\n");
		return;
	}
	if (!kgdb_connected)
		printk(KERN_CRIT "Entering KGDB\n");

	kgdb_breakpoint();
}

static struct sysrq_key_op sysrq_dbg_op = {
	.handler	= sysrq_handle_dbg,
	.help_msg	= "debug(G)",
	.action_msg	= "DEBUG",
};
#endif

static void kgdb_register_callbacks(void)
{
	if (!kgdb_io_module_registered) {
		kgdb_io_module_registered = 1;
		kgdb_arch_init();
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

	if (kgdb_io_module_registered)
		kgdb_initial_breakpoint();

	return 0;
}

early_param("kgdbwait", opt_kgdb_wait);
