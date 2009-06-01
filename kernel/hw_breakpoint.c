/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) 2007 Alan Stern
 * Copyright (C) IBM Corporation, 2009
 */

/*
 * HW_breakpoint: a unified kernel/user-space hardware breakpoint facility,
 * using the CPU's debug registers.
 * This file contains the arch-independent routines.
 */

#include <linux/irqflags.h>
#include <linux/kallsyms.h>
#include <linux/notifier.h>
#include <linux/kprobes.h>
#include <linux/kdebug.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/smp.h>

#include <asm/hw_breakpoint.h>
#include <asm/processor.h>

#ifdef CONFIG_X86
#include <asm/debugreg.h>
#endif
/*
 * Spinlock that protects all (un)register operations over kernel/user-space
 * breakpoint requests
 */
static DEFINE_SPINLOCK(hw_breakpoint_lock);

/* Array of kernel-space breakpoint structures */
struct hw_breakpoint *hbp_kernel[HBP_NUM];

/*
 * Per-processor copy of hbp_kernel[]. Used only when hbp_kernel is being
 * modified but we need the older copy to handle any hbp exceptions. It will
 * sync with hbp_kernel[] value after updation is done through IPIs.
 */
DEFINE_PER_CPU(struct hw_breakpoint*, this_hbp_kernel[HBP_NUM]);

/*
 * Kernel breakpoints grow downwards, starting from HBP_NUM
 * 'hbp_kernel_pos' denotes lowest numbered breakpoint register occupied for
 * kernel-space request. We will initialise it here and not in an __init
 * routine because load_debug_registers(), which uses this variable can be
 * called very early during CPU initialisation.
 */
unsigned int hbp_kernel_pos = HBP_NUM;

/*
 * An array containing refcount of threads using a given bkpt register
 * Accesses are synchronised by acquiring hw_breakpoint_lock
 */
unsigned int hbp_user_refcount[HBP_NUM];

/*
 * Load the debug registers during startup of a CPU.
 */
void load_debug_registers(void)
{
	unsigned long flags;
	struct task_struct *tsk = current;

	spin_lock_bh(&hw_breakpoint_lock);

	/* Prevent IPIs for new kernel breakpoint updates */
	local_irq_save(flags);
	arch_update_kernel_hw_breakpoint(NULL);
	local_irq_restore(flags);

	if (test_tsk_thread_flag(tsk, TIF_DEBUG))
		arch_install_thread_hw_breakpoint(tsk);

	spin_unlock_bh(&hw_breakpoint_lock);
}

/*
 * Erase all the hardware breakpoint info associated with a thread.
 *
 * If tsk != current then tsk must not be usable (for example, a
 * child being cleaned up from a failed fork).
 */
void flush_thread_hw_breakpoint(struct task_struct *tsk)
{
	int i;
	struct thread_struct *thread = &(tsk->thread);

	spin_lock_bh(&hw_breakpoint_lock);

	/* The thread no longer has any breakpoints associated with it */
	clear_tsk_thread_flag(tsk, TIF_DEBUG);
	for (i = 0; i < HBP_NUM; i++) {
		if (thread->hbp[i]) {
			hbp_user_refcount[i]--;
			kfree(thread->hbp[i]);
			thread->hbp[i] = NULL;
		}
	}

	arch_flush_thread_hw_breakpoint(tsk);

	/* Actually uninstall the breakpoints if necessary */
	if (tsk == current)
		arch_uninstall_thread_hw_breakpoint();
	spin_unlock_bh(&hw_breakpoint_lock);
}

/*
 * Copy the hardware breakpoint info from a thread to its cloned child.
 */
int copy_thread_hw_breakpoint(struct task_struct *tsk,
		struct task_struct *child, unsigned long clone_flags)
{
	/*
	 * We will assume that breakpoint settings are not inherited
	 * and the child starts out with no debug registers set.
	 * But what about CLONE_PTRACE?
	 */
	clear_tsk_thread_flag(child, TIF_DEBUG);

	/* We will call flush routine since the debugregs are not inherited */
	arch_flush_thread_hw_breakpoint(child);

	return 0;
}

static int __register_user_hw_breakpoint(int pos, struct task_struct *tsk,
					struct hw_breakpoint *bp)
{
	struct thread_struct *thread = &(tsk->thread);
	int rc;

	/* Do not overcommit. Fail if kernel has used the hbp registers */
	if (pos >= hbp_kernel_pos)
		return -ENOSPC;

	rc = arch_validate_hwbkpt_settings(bp, tsk);
	if (rc)
		return rc;

	thread->hbp[pos] = bp;
	hbp_user_refcount[pos]++;

	arch_update_user_hw_breakpoint(pos, tsk);
	/*
	 * Does it need to be installed right now?
	 * Otherwise it will get installed the next time tsk runs
	 */
	if (tsk == current)
		arch_install_thread_hw_breakpoint(tsk);

	return rc;
}

/*
 * Modify the address of a hbp register already in use by the task
 * Do not invoke this in-lieu of a __unregister_user_hw_breakpoint()
 */
static int __modify_user_hw_breakpoint(int pos, struct task_struct *tsk,
					struct hw_breakpoint *bp)
{
	struct thread_struct *thread = &(tsk->thread);

	if ((pos >= hbp_kernel_pos) || (arch_validate_hwbkpt_settings(bp, tsk)))
		return -EINVAL;

	if (thread->hbp[pos] == NULL)
		return -EINVAL;

	thread->hbp[pos] = bp;
	/*
	 * 'pos' must be that of a hbp register already used by 'tsk'
	 * Otherwise arch_modify_user_hw_breakpoint() will fail
	 */
	arch_update_user_hw_breakpoint(pos, tsk);

	if (tsk == current)
		arch_install_thread_hw_breakpoint(tsk);

	return 0;
}

static void __unregister_user_hw_breakpoint(int pos, struct task_struct *tsk)
{
	hbp_user_refcount[pos]--;
	tsk->thread.hbp[pos] = NULL;

	arch_update_user_hw_breakpoint(pos, tsk);

	if (tsk == current)
		arch_install_thread_hw_breakpoint(tsk);
}

/**
 * register_user_hw_breakpoint - register a hardware breakpoint for user space
 * @tsk: pointer to 'task_struct' of the process to which the address belongs
 * @bp: the breakpoint structure to register
 *
 * @bp.info->name or @bp.info->address, @bp.info->len, @bp.info->type and
 * @bp->triggered must be set properly before invocation
 *
 */
int register_user_hw_breakpoint(struct task_struct *tsk,
					struct hw_breakpoint *bp)
{
	struct thread_struct *thread = &(tsk->thread);
	int i, rc = -ENOSPC;

	spin_lock_bh(&hw_breakpoint_lock);

	for (i = 0; i < hbp_kernel_pos; i++) {
		if (!thread->hbp[i]) {
			rc = __register_user_hw_breakpoint(i, tsk, bp);
			break;
		}
	}
	if (!rc)
		set_tsk_thread_flag(tsk, TIF_DEBUG);

	spin_unlock_bh(&hw_breakpoint_lock);
	return rc;
}
EXPORT_SYMBOL_GPL(register_user_hw_breakpoint);

/**
 * modify_user_hw_breakpoint - modify a user-space hardware breakpoint
 * @tsk: pointer to 'task_struct' of the process to which the address belongs
 * @bp: the breakpoint structure to unregister
 *
 */
int modify_user_hw_breakpoint(struct task_struct *tsk, struct hw_breakpoint *bp)
{
	struct thread_struct *thread = &(tsk->thread);
	int i, ret = -ENOENT;

	spin_lock_bh(&hw_breakpoint_lock);
	for (i = 0; i < hbp_kernel_pos; i++) {
		if (bp == thread->hbp[i]) {
			ret = __modify_user_hw_breakpoint(i, tsk, bp);
			break;
		}
	}
	spin_unlock_bh(&hw_breakpoint_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(modify_user_hw_breakpoint);

/**
 * unregister_user_hw_breakpoint - unregister a user-space hardware breakpoint
 * @tsk: pointer to 'task_struct' of the process to which the address belongs
 * @bp: the breakpoint structure to unregister
 *
 */
void unregister_user_hw_breakpoint(struct task_struct *tsk,
						struct hw_breakpoint *bp)
{
	struct thread_struct *thread = &(tsk->thread);
	int i, pos = -1, hbp_counter = 0;

	spin_lock_bh(&hw_breakpoint_lock);
	for (i = 0; i < hbp_kernel_pos; i++) {
		if (thread->hbp[i])
			hbp_counter++;
		if (bp == thread->hbp[i])
			pos = i;
	}
	if (pos >= 0) {
		__unregister_user_hw_breakpoint(pos, tsk);
		hbp_counter--;
	}
	if (!hbp_counter)
		clear_tsk_thread_flag(tsk, TIF_DEBUG);

	spin_unlock_bh(&hw_breakpoint_lock);
}
EXPORT_SYMBOL_GPL(unregister_user_hw_breakpoint);

/**
 * register_kernel_hw_breakpoint - register a hardware breakpoint for kernel space
 * @bp: the breakpoint structure to register
 *
 * @bp.info->name or @bp.info->address, @bp.info->len, @bp.info->type and
 * @bp->triggered must be set properly before invocation
 *
 */
int register_kernel_hw_breakpoint(struct hw_breakpoint *bp)
{
	int rc;

	rc = arch_validate_hwbkpt_settings(bp, NULL);
	if (rc)
		return rc;

	spin_lock_bh(&hw_breakpoint_lock);

	rc = -ENOSPC;
	/* Check if we are over-committing */
	if ((hbp_kernel_pos > 0) && (!hbp_user_refcount[hbp_kernel_pos-1])) {
		hbp_kernel_pos--;
		hbp_kernel[hbp_kernel_pos] = bp;
		on_each_cpu(arch_update_kernel_hw_breakpoint, NULL, 1);
		rc = 0;
	}

	spin_unlock_bh(&hw_breakpoint_lock);
	return rc;
}
EXPORT_SYMBOL_GPL(register_kernel_hw_breakpoint);

/**
 * unregister_kernel_hw_breakpoint - unregister a HW breakpoint for kernel space
 * @bp: the breakpoint structure to unregister
 *
 * Uninstalls and unregisters @bp.
 */
void unregister_kernel_hw_breakpoint(struct hw_breakpoint *bp)
{
	int i, j;

	spin_lock_bh(&hw_breakpoint_lock);

	/* Find the 'bp' in our list of breakpoints for kernel */
	for (i = hbp_kernel_pos; i < HBP_NUM; i++)
		if (bp == hbp_kernel[i])
			break;

	/* Check if we did not find a match for 'bp'. If so return early */
	if (i == HBP_NUM) {
		spin_unlock_bh(&hw_breakpoint_lock);
		return;
	}

	/*
	 * We'll shift the breakpoints one-level above to compact if
	 * unregistration creates a hole
	 */
	for (j = i; j > hbp_kernel_pos; j--)
		hbp_kernel[j] = hbp_kernel[j-1];

	hbp_kernel[hbp_kernel_pos] = NULL;
	on_each_cpu(arch_update_kernel_hw_breakpoint, NULL, 1);
	hbp_kernel_pos++;

	spin_unlock_bh(&hw_breakpoint_lock);
}
EXPORT_SYMBOL_GPL(unregister_kernel_hw_breakpoint);

static struct notifier_block hw_breakpoint_exceptions_nb = {
	.notifier_call = hw_breakpoint_exceptions_notify,
	/* we need to be notified first */
	.priority = 0x7fffffff
};

static int __init init_hw_breakpoint(void)
{
	return register_die_notifier(&hw_breakpoint_exceptions_nb);
}

core_initcall(init_hw_breakpoint);
