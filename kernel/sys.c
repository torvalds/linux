/*
 *  linux/kernel/sys.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/utsname.h>
#include <linux/mman.h>
#include <linux/smp_lock.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/prctl.h>
#include <linux/init.h>
#include <linux/highuid.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/kexec.h>
#include <linux/workqueue.h>
#include <linux/capability.h>
#include <linux/device.h>
#include <linux/key.h>
#include <linux/times.h>
#include <linux/posix-timers.h>
#include <linux/security.h>
#include <linux/dcookies.h>
#include <linux/suspend.h>
#include <linux/tty.h>
#include <linux/signal.h>
#include <linux/cn_proc.h>

#include <linux/compat.h>
#include <linux/syscalls.h>
#include <linux/kprobes.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/unistd.h>

#ifndef SET_UNALIGN_CTL
# define SET_UNALIGN_CTL(a,b)	(-EINVAL)
#endif
#ifndef GET_UNALIGN_CTL
# define GET_UNALIGN_CTL(a,b)	(-EINVAL)
#endif
#ifndef SET_FPEMU_CTL
# define SET_FPEMU_CTL(a,b)	(-EINVAL)
#endif
#ifndef GET_FPEMU_CTL
# define GET_FPEMU_CTL(a,b)	(-EINVAL)
#endif
#ifndef SET_FPEXC_CTL
# define SET_FPEXC_CTL(a,b)	(-EINVAL)
#endif
#ifndef GET_FPEXC_CTL
# define GET_FPEXC_CTL(a,b)	(-EINVAL)
#endif

/*
 * this is where the system-wide overflow UID and GID are defined, for
 * architectures that now have 32-bit UID/GID but didn't in the past
 */

int overflowuid = DEFAULT_OVERFLOWUID;
int overflowgid = DEFAULT_OVERFLOWGID;

#ifdef CONFIG_UID16
EXPORT_SYMBOL(overflowuid);
EXPORT_SYMBOL(overflowgid);
#endif

/*
 * the same as above, but for filesystems which can only store a 16-bit
 * UID and GID. as such, this is needed on all architectures
 */

int fs_overflowuid = DEFAULT_FS_OVERFLOWUID;
int fs_overflowgid = DEFAULT_FS_OVERFLOWUID;

EXPORT_SYMBOL(fs_overflowuid);
EXPORT_SYMBOL(fs_overflowgid);

/*
 * this indicates whether you can reboot with ctrl-alt-del: the default is yes
 */

int C_A_D = 1;
int cad_pid = 1;

/*
 *	Notifier list for kernel code which wants to be called
 *	at shutdown. This is used to stop any idling DMA operations
 *	and the like. 
 */

static BLOCKING_NOTIFIER_HEAD(reboot_notifier_list);

/*
 *	Notifier chain core routines.  The exported routines below
 *	are layered on top of these, with appropriate locking added.
 */

static int notifier_chain_register(struct notifier_block **nl,
		struct notifier_block *n)
{
	while ((*nl) != NULL) {
		if (n->priority > (*nl)->priority)
			break;
		nl = &((*nl)->next);
	}
	n->next = *nl;
	rcu_assign_pointer(*nl, n);
	return 0;
}

static int notifier_chain_unregister(struct notifier_block **nl,
		struct notifier_block *n)
{
	while ((*nl) != NULL) {
		if ((*nl) == n) {
			rcu_assign_pointer(*nl, n->next);
			return 0;
		}
		nl = &((*nl)->next);
	}
	return -ENOENT;
}

static int __kprobes notifier_call_chain(struct notifier_block **nl,
		unsigned long val, void *v)
{
	int ret = NOTIFY_DONE;
	struct notifier_block *nb;

	nb = rcu_dereference(*nl);
	while (nb) {
		ret = nb->notifier_call(nb, val, v);
		if ((ret & NOTIFY_STOP_MASK) == NOTIFY_STOP_MASK)
			break;
		nb = rcu_dereference(nb->next);
	}
	return ret;
}

/*
 *	Atomic notifier chain routines.  Registration and unregistration
 *	use a mutex, and call_chain is synchronized by RCU (no locks).
 */

/**
 *	atomic_notifier_chain_register - Add notifier to an atomic notifier chain
 *	@nh: Pointer to head of the atomic notifier chain
 *	@n: New entry in notifier chain
 *
 *	Adds a notifier to an atomic notifier chain.
 *
 *	Currently always returns zero.
 */

int atomic_notifier_chain_register(struct atomic_notifier_head *nh,
		struct notifier_block *n)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&nh->lock, flags);
	ret = notifier_chain_register(&nh->head, n);
	spin_unlock_irqrestore(&nh->lock, flags);
	return ret;
}

EXPORT_SYMBOL_GPL(atomic_notifier_chain_register);

/**
 *	atomic_notifier_chain_unregister - Remove notifier from an atomic notifier chain
 *	@nh: Pointer to head of the atomic notifier chain
 *	@n: Entry to remove from notifier chain
 *
 *	Removes a notifier from an atomic notifier chain.
 *
 *	Returns zero on success or %-ENOENT on failure.
 */
int atomic_notifier_chain_unregister(struct atomic_notifier_head *nh,
		struct notifier_block *n)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&nh->lock, flags);
	ret = notifier_chain_unregister(&nh->head, n);
	spin_unlock_irqrestore(&nh->lock, flags);
	synchronize_rcu();
	return ret;
}

EXPORT_SYMBOL_GPL(atomic_notifier_chain_unregister);

/**
 *	atomic_notifier_call_chain - Call functions in an atomic notifier chain
 *	@nh: Pointer to head of the atomic notifier chain
 *	@val: Value passed unmodified to notifier function
 *	@v: Pointer passed unmodified to notifier function
 *
 *	Calls each function in a notifier chain in turn.  The functions
 *	run in an atomic context, so they must not block.
 *	This routine uses RCU to synchronize with changes to the chain.
 *
 *	If the return value of the notifier can be and'ed
 *	with %NOTIFY_STOP_MASK then atomic_notifier_call_chain
 *	will return immediately, with the return value of
 *	the notifier function which halted execution.
 *	Otherwise the return value is the return value
 *	of the last notifier function called.
 */
 
int atomic_notifier_call_chain(struct atomic_notifier_head *nh,
		unsigned long val, void *v)
{
	int ret;

	rcu_read_lock();
	ret = notifier_call_chain(&nh->head, val, v);
	rcu_read_unlock();
	return ret;
}

EXPORT_SYMBOL_GPL(atomic_notifier_call_chain);

/*
 *	Blocking notifier chain routines.  All access to the chain is
 *	synchronized by an rwsem.
 */

/**
 *	blocking_notifier_chain_register - Add notifier to a blocking notifier chain
 *	@nh: Pointer to head of the blocking notifier chain
 *	@n: New entry in notifier chain
 *
 *	Adds a notifier to a blocking notifier chain.
 *	Must be called in process context.
 *
 *	Currently always returns zero.
 */
 
int blocking_notifier_chain_register(struct blocking_notifier_head *nh,
		struct notifier_block *n)
{
	int ret;

	/*
	 * This code gets used during boot-up, when task switching is
	 * not yet working and interrupts must remain disabled.  At
	 * such times we must not call down_write().
	 */
	if (unlikely(system_state == SYSTEM_BOOTING))
		return notifier_chain_register(&nh->head, n);

	down_write(&nh->rwsem);
	ret = notifier_chain_register(&nh->head, n);
	up_write(&nh->rwsem);
	return ret;
}

EXPORT_SYMBOL_GPL(blocking_notifier_chain_register);

/**
 *	blocking_notifier_chain_unregister - Remove notifier from a blocking notifier chain
 *	@nh: Pointer to head of the blocking notifier chain
 *	@n: Entry to remove from notifier chain
 *
 *	Removes a notifier from a blocking notifier chain.
 *	Must be called from process context.
 *
 *	Returns zero on success or %-ENOENT on failure.
 */
int blocking_notifier_chain_unregister(struct blocking_notifier_head *nh,
		struct notifier_block *n)
{
	int ret;

	/*
	 * This code gets used during boot-up, when task switching is
	 * not yet working and interrupts must remain disabled.  At
	 * such times we must not call down_write().
	 */
	if (unlikely(system_state == SYSTEM_BOOTING))
		return notifier_chain_unregister(&nh->head, n);

	down_write(&nh->rwsem);
	ret = notifier_chain_unregister(&nh->head, n);
	up_write(&nh->rwsem);
	return ret;
}

EXPORT_SYMBOL_GPL(blocking_notifier_chain_unregister);

/**
 *	blocking_notifier_call_chain - Call functions in a blocking notifier chain
 *	@nh: Pointer to head of the blocking notifier chain
 *	@val: Value passed unmodified to notifier function
 *	@v: Pointer passed unmodified to notifier function
 *
 *	Calls each function in a notifier chain in turn.  The functions
 *	run in a process context, so they are allowed to block.
 *
 *	If the return value of the notifier can be and'ed
 *	with %NOTIFY_STOP_MASK then blocking_notifier_call_chain
 *	will return immediately, with the return value of
 *	the notifier function which halted execution.
 *	Otherwise the return value is the return value
 *	of the last notifier function called.
 */
 
int blocking_notifier_call_chain(struct blocking_notifier_head *nh,
		unsigned long val, void *v)
{
	int ret;

	down_read(&nh->rwsem);
	ret = notifier_call_chain(&nh->head, val, v);
	up_read(&nh->rwsem);
	return ret;
}

EXPORT_SYMBOL_GPL(blocking_notifier_call_chain);

/*
 *	Raw notifier chain routines.  There is no protection;
 *	the caller must provide it.  Use at your own risk!
 */

/**
 *	raw_notifier_chain_register - Add notifier to a raw notifier chain
 *	@nh: Pointer to head of the raw notifier chain
 *	@n: New entry in notifier chain
 *
 *	Adds a notifier to a raw notifier chain.
 *	All locking must be provided by the caller.
 *
 *	Currently always returns zero.
 */

int raw_notifier_chain_register(struct raw_notifier_head *nh,
		struct notifier_block *n)
{
	return notifier_chain_register(&nh->head, n);
}

EXPORT_SYMBOL_GPL(raw_notifier_chain_register);

/**
 *	raw_notifier_chain_unregister - Remove notifier from a raw notifier chain
 *	@nh: Pointer to head of the raw notifier chain
 *	@n: Entry to remove from notifier chain
 *
 *	Removes a notifier from a raw notifier chain.
 *	All locking must be provided by the caller.
 *
 *	Returns zero on success or %-ENOENT on failure.
 */
int raw_notifier_chain_unregister(struct raw_notifier_head *nh,
		struct notifier_block *n)
{
	return notifier_chain_unregister(&nh->head, n);
}

EXPORT_SYMBOL_GPL(raw_notifier_chain_unregister);

/**
 *	raw_notifier_call_chain - Call functions in a raw notifier chain
 *	@nh: Pointer to head of the raw notifier chain
 *	@val: Value passed unmodified to notifier function
 *	@v: Pointer passed unmodified to notifier function
 *
 *	Calls each function in a notifier chain in turn.  The functions
 *	run in an undefined context.
 *	All locking must be provided by the caller.
 *
 *	If the return value of the notifier can be and'ed
 *	with %NOTIFY_STOP_MASK then raw_notifier_call_chain
 *	will return immediately, with the return value of
 *	the notifier function which halted execution.
 *	Otherwise the return value is the return value
 *	of the last notifier function called.
 */

int raw_notifier_call_chain(struct raw_notifier_head *nh,
		unsigned long val, void *v)
{
	return notifier_call_chain(&nh->head, val, v);
}

EXPORT_SYMBOL_GPL(raw_notifier_call_chain);

/**
 *	register_reboot_notifier - Register function to be called at reboot time
 *	@nb: Info about notifier function to be called
 *
 *	Registers a function with the list of functions
 *	to be called at reboot time.
 *
 *	Currently always returns zero, as blocking_notifier_chain_register
 *	always returns zero.
 */
 
int register_reboot_notifier(struct notifier_block * nb)
{
	return blocking_notifier_chain_register(&reboot_notifier_list, nb);
}

EXPORT_SYMBOL(register_reboot_notifier);

/**
 *	unregister_reboot_notifier - Unregister previously registered reboot notifier
 *	@nb: Hook to be unregistered
 *
 *	Unregisters a previously registered reboot
 *	notifier function.
 *
 *	Returns zero on success, or %-ENOENT on failure.
 */
 
int unregister_reboot_notifier(struct notifier_block * nb)
{
	return blocking_notifier_chain_unregister(&reboot_notifier_list, nb);
}

EXPORT_SYMBOL(unregister_reboot_notifier);

static int set_one_prio(struct task_struct *p, int niceval, int error)
{
	int no_nice;

	if (p->uid != current->euid &&
		p->euid != current->euid && !capable(CAP_SYS_NICE)) {
		error = -EPERM;
		goto out;
	}
	if (niceval < task_nice(p) && !can_nice(p, niceval)) {
		error = -EACCES;
		goto out;
	}
	no_nice = security_task_setnice(p, niceval);
	if (no_nice) {
		error = no_nice;
		goto out;
	}
	if (error == -ESRCH)
		error = 0;
	set_user_nice(p, niceval);
out:
	return error;
}

asmlinkage long sys_setpriority(int which, int who, int niceval)
{
	struct task_struct *g, *p;
	struct user_struct *user;
	int error = -EINVAL;

	if (which > 2 || which < 0)
		goto out;

	/* normalize: avoid signed division (rounding problems) */
	error = -ESRCH;
	if (niceval < -20)
		niceval = -20;
	if (niceval > 19)
		niceval = 19;

	read_lock(&tasklist_lock);
	switch (which) {
		case PRIO_PROCESS:
			if (!who)
				who = current->pid;
			p = find_task_by_pid(who);
			if (p)
				error = set_one_prio(p, niceval, error);
			break;
		case PRIO_PGRP:
			if (!who)
				who = process_group(current);
			do_each_task_pid(who, PIDTYPE_PGID, p) {
				error = set_one_prio(p, niceval, error);
			} while_each_task_pid(who, PIDTYPE_PGID, p);
			break;
		case PRIO_USER:
			user = current->user;
			if (!who)
				who = current->uid;
			else
				if ((who != current->uid) && !(user = find_user(who)))
					goto out_unlock;	/* No processes for this user */

			do_each_thread(g, p)
				if (p->uid == who)
					error = set_one_prio(p, niceval, error);
			while_each_thread(g, p);
			if (who != current->uid)
				free_uid(user);		/* For find_user() */
			break;
	}
out_unlock:
	read_unlock(&tasklist_lock);
out:
	return error;
}

/*
 * Ugh. To avoid negative return values, "getpriority()" will
 * not return the normal nice-value, but a negated value that
 * has been offset by 20 (ie it returns 40..1 instead of -20..19)
 * to stay compatible.
 */
asmlinkage long sys_getpriority(int which, int who)
{
	struct task_struct *g, *p;
	struct user_struct *user;
	long niceval, retval = -ESRCH;

	if (which > 2 || which < 0)
		return -EINVAL;

	read_lock(&tasklist_lock);
	switch (which) {
		case PRIO_PROCESS:
			if (!who)
				who = current->pid;
			p = find_task_by_pid(who);
			if (p) {
				niceval = 20 - task_nice(p);
				if (niceval > retval)
					retval = niceval;
			}
			break;
		case PRIO_PGRP:
			if (!who)
				who = process_group(current);
			do_each_task_pid(who, PIDTYPE_PGID, p) {
				niceval = 20 - task_nice(p);
				if (niceval > retval)
					retval = niceval;
			} while_each_task_pid(who, PIDTYPE_PGID, p);
			break;
		case PRIO_USER:
			user = current->user;
			if (!who)
				who = current->uid;
			else
				if ((who != current->uid) && !(user = find_user(who)))
					goto out_unlock;	/* No processes for this user */

			do_each_thread(g, p)
				if (p->uid == who) {
					niceval = 20 - task_nice(p);
					if (niceval > retval)
						retval = niceval;
				}
			while_each_thread(g, p);
			if (who != current->uid)
				free_uid(user);		/* for find_user() */
			break;
	}
out_unlock:
	read_unlock(&tasklist_lock);

	return retval;
}

/**
 *	emergency_restart - reboot the system
 *
 *	Without shutting down any hardware or taking any locks
 *	reboot the system.  This is called when we know we are in
 *	trouble so this is our best effort to reboot.  This is
 *	safe to call in interrupt context.
 */
void emergency_restart(void)
{
	machine_emergency_restart();
}
EXPORT_SYMBOL_GPL(emergency_restart);

void kernel_restart_prepare(char *cmd)
{
	blocking_notifier_call_chain(&reboot_notifier_list, SYS_RESTART, cmd);
	system_state = SYSTEM_RESTART;
	device_shutdown();
}

/**
 *	kernel_restart - reboot the system
 *	@cmd: pointer to buffer containing command to execute for restart
 *		or %NULL
 *
 *	Shutdown everything and perform a clean reboot.
 *	This is not safe to call in interrupt context.
 */
void kernel_restart(char *cmd)
{
	kernel_restart_prepare(cmd);
	if (!cmd) {
		printk(KERN_EMERG "Restarting system.\n");
	} else {
		printk(KERN_EMERG "Restarting system with command '%s'.\n", cmd);
	}
	printk(".\n");
	machine_restart(cmd);
}
EXPORT_SYMBOL_GPL(kernel_restart);

/**
 *	kernel_kexec - reboot the system
 *
 *	Move into place and start executing a preloaded standalone
 *	executable.  If nothing was preloaded return an error.
 */
void kernel_kexec(void)
{
#ifdef CONFIG_KEXEC
	struct kimage *image;
	image = xchg(&kexec_image, NULL);
	if (!image) {
		return;
	}
	kernel_restart_prepare(NULL);
	printk(KERN_EMERG "Starting new kernel\n");
	machine_shutdown();
	machine_kexec(image);
#endif
}
EXPORT_SYMBOL_GPL(kernel_kexec);

void kernel_shutdown_prepare(enum system_states state)
{
	blocking_notifier_call_chain(&reboot_notifier_list,
		(state == SYSTEM_HALT)?SYS_HALT:SYS_POWER_OFF, NULL);
	system_state = state;
	device_shutdown();
}
/**
 *	kernel_halt - halt the system
 *
 *	Shutdown everything and perform a clean system halt.
 */
void kernel_halt(void)
{
	kernel_shutdown_prepare(SYSTEM_HALT);
	printk(KERN_EMERG "System halted.\n");
	machine_halt();
}

EXPORT_SYMBOL_GPL(kernel_halt);

/**
 *	kernel_power_off - power_off the system
 *
 *	Shutdown everything and perform a clean system power_off.
 */
void kernel_power_off(void)
{
	kernel_shutdown_prepare(SYSTEM_POWER_OFF);
	printk(KERN_EMERG "Power down.\n");
	machine_power_off();
}
EXPORT_SYMBOL_GPL(kernel_power_off);
/*
 * Reboot system call: for obvious reasons only root may call it,
 * and even root needs to set up some magic numbers in the registers
 * so that some mistake won't make this reboot the whole machine.
 * You can also set the meaning of the ctrl-alt-del-key here.
 *
 * reboot doesn't sync: do that yourself before calling this.
 */
asmlinkage long sys_reboot(int magic1, int magic2, unsigned int cmd, void __user * arg)
{
	char buffer[256];

	/* We only trust the superuser with rebooting the system. */
	if (!capable(CAP_SYS_BOOT))
		return -EPERM;

	/* For safety, we require "magic" arguments. */
	if (magic1 != LINUX_REBOOT_MAGIC1 ||
	    (magic2 != LINUX_REBOOT_MAGIC2 &&
	                magic2 != LINUX_REBOOT_MAGIC2A &&
			magic2 != LINUX_REBOOT_MAGIC2B &&
	                magic2 != LINUX_REBOOT_MAGIC2C))
		return -EINVAL;

	/* Instead of trying to make the power_off code look like
	 * halt when pm_power_off is not set do it the easy way.
	 */
	if ((cmd == LINUX_REBOOT_CMD_POWER_OFF) && !pm_power_off)
		cmd = LINUX_REBOOT_CMD_HALT;

	lock_kernel();
	switch (cmd) {
	case LINUX_REBOOT_CMD_RESTART:
		kernel_restart(NULL);
		break;

	case LINUX_REBOOT_CMD_CAD_ON:
		C_A_D = 1;
		break;

	case LINUX_REBOOT_CMD_CAD_OFF:
		C_A_D = 0;
		break;

	case LINUX_REBOOT_CMD_HALT:
		kernel_halt();
		unlock_kernel();
		do_exit(0);
		break;

	case LINUX_REBOOT_CMD_POWER_OFF:
		kernel_power_off();
		unlock_kernel();
		do_exit(0);
		break;

	case LINUX_REBOOT_CMD_RESTART2:
		if (strncpy_from_user(&buffer[0], arg, sizeof(buffer) - 1) < 0) {
			unlock_kernel();
			return -EFAULT;
		}
		buffer[sizeof(buffer) - 1] = '\0';

		kernel_restart(buffer);
		break;

	case LINUX_REBOOT_CMD_KEXEC:
		kernel_kexec();
		unlock_kernel();
		return -EINVAL;

#ifdef CONFIG_SOFTWARE_SUSPEND
	case LINUX_REBOOT_CMD_SW_SUSPEND:
		{
			int ret = software_suspend();
			unlock_kernel();
			return ret;
		}
#endif

	default:
		unlock_kernel();
		return -EINVAL;
	}
	unlock_kernel();
	return 0;
}

static void deferred_cad(void *dummy)
{
	kernel_restart(NULL);
}

/*
 * This function gets called by ctrl-alt-del - ie the keyboard interrupt.
 * As it's called within an interrupt, it may NOT sync: the only choice
 * is whether to reboot at once, or just ignore the ctrl-alt-del.
 */
void ctrl_alt_del(void)
{
	static DECLARE_WORK(cad_work, deferred_cad, NULL);

	if (C_A_D)
		schedule_work(&cad_work);
	else
		kill_proc(cad_pid, SIGINT, 1);
}
	

/*
 * Unprivileged users may change the real gid to the effective gid
 * or vice versa.  (BSD-style)
 *
 * If you set the real gid at all, or set the effective gid to a value not
 * equal to the real gid, then the saved gid is set to the new effective gid.
 *
 * This makes it possible for a setgid program to completely drop its
 * privileges, which is often a useful assertion to make when you are doing
 * a security audit over a program.
 *
 * The general idea is that a program which uses just setregid() will be
 * 100% compatible with BSD.  A program which uses just setgid() will be
 * 100% compatible with POSIX with saved IDs. 
 *
 * SMP: There are not races, the GIDs are checked only by filesystem
 *      operations (as far as semantic preservation is concerned).
 */
asmlinkage long sys_setregid(gid_t rgid, gid_t egid)
{
	int old_rgid = current->gid;
	int old_egid = current->egid;
	int new_rgid = old_rgid;
	int new_egid = old_egid;
	int retval;

	retval = security_task_setgid(rgid, egid, (gid_t)-1, LSM_SETID_RE);
	if (retval)
		return retval;

	if (rgid != (gid_t) -1) {
		if ((old_rgid == rgid) ||
		    (current->egid==rgid) ||
		    capable(CAP_SETGID))
			new_rgid = rgid;
		else
			return -EPERM;
	}
	if (egid != (gid_t) -1) {
		if ((old_rgid == egid) ||
		    (current->egid == egid) ||
		    (current->sgid == egid) ||
		    capable(CAP_SETGID))
			new_egid = egid;
		else {
			return -EPERM;
		}
	}
	if (new_egid != old_egid)
	{
		current->mm->dumpable = suid_dumpable;
		smp_wmb();
	}
	if (rgid != (gid_t) -1 ||
	    (egid != (gid_t) -1 && egid != old_rgid))
		current->sgid = new_egid;
	current->fsgid = new_egid;
	current->egid = new_egid;
	current->gid = new_rgid;
	key_fsgid_changed(current);
	proc_id_connector(current, PROC_EVENT_GID);
	return 0;
}

/*
 * setgid() is implemented like SysV w/ SAVED_IDS 
 *
 * SMP: Same implicit races as above.
 */
asmlinkage long sys_setgid(gid_t gid)
{
	int old_egid = current->egid;
	int retval;

	retval = security_task_setgid(gid, (gid_t)-1, (gid_t)-1, LSM_SETID_ID);
	if (retval)
		return retval;

	if (capable(CAP_SETGID))
	{
		if(old_egid != gid)
		{
			current->mm->dumpable = suid_dumpable;
			smp_wmb();
		}
		current->gid = current->egid = current->sgid = current->fsgid = gid;
	}
	else if ((gid == current->gid) || (gid == current->sgid))
	{
		if(old_egid != gid)
		{
			current->mm->dumpable = suid_dumpable;
			smp_wmb();
		}
		current->egid = current->fsgid = gid;
	}
	else
		return -EPERM;

	key_fsgid_changed(current);
	proc_id_connector(current, PROC_EVENT_GID);
	return 0;
}
  
static int set_user(uid_t new_ruid, int dumpclear)
{
	struct user_struct *new_user;

	new_user = alloc_uid(new_ruid);
	if (!new_user)
		return -EAGAIN;

	if (atomic_read(&new_user->processes) >=
				current->signal->rlim[RLIMIT_NPROC].rlim_cur &&
			new_user != &root_user) {
		free_uid(new_user);
		return -EAGAIN;
	}

	switch_uid(new_user);

	if(dumpclear)
	{
		current->mm->dumpable = suid_dumpable;
		smp_wmb();
	}
	current->uid = new_ruid;
	return 0;
}

/*
 * Unprivileged users may change the real uid to the effective uid
 * or vice versa.  (BSD-style)
 *
 * If you set the real uid at all, or set the effective uid to a value not
 * equal to the real uid, then the saved uid is set to the new effective uid.
 *
 * This makes it possible for a setuid program to completely drop its
 * privileges, which is often a useful assertion to make when you are doing
 * a security audit over a program.
 *
 * The general idea is that a program which uses just setreuid() will be
 * 100% compatible with BSD.  A program which uses just setuid() will be
 * 100% compatible with POSIX with saved IDs. 
 */
asmlinkage long sys_setreuid(uid_t ruid, uid_t euid)
{
	int old_ruid, old_euid, old_suid, new_ruid, new_euid;
	int retval;

	retval = security_task_setuid(ruid, euid, (uid_t)-1, LSM_SETID_RE);
	if (retval)
		return retval;

	new_ruid = old_ruid = current->uid;
	new_euid = old_euid = current->euid;
	old_suid = current->suid;

	if (ruid != (uid_t) -1) {
		new_ruid = ruid;
		if ((old_ruid != ruid) &&
		    (current->euid != ruid) &&
		    !capable(CAP_SETUID))
			return -EPERM;
	}

	if (euid != (uid_t) -1) {
		new_euid = euid;
		if ((old_ruid != euid) &&
		    (current->euid != euid) &&
		    (current->suid != euid) &&
		    !capable(CAP_SETUID))
			return -EPERM;
	}

	if (new_ruid != old_ruid && set_user(new_ruid, new_euid != old_euid) < 0)
		return -EAGAIN;

	if (new_euid != old_euid)
	{
		current->mm->dumpable = suid_dumpable;
		smp_wmb();
	}
	current->fsuid = current->euid = new_euid;
	if (ruid != (uid_t) -1 ||
	    (euid != (uid_t) -1 && euid != old_ruid))
		current->suid = current->euid;
	current->fsuid = current->euid;

	key_fsuid_changed(current);
	proc_id_connector(current, PROC_EVENT_UID);

	return security_task_post_setuid(old_ruid, old_euid, old_suid, LSM_SETID_RE);
}


		
/*
 * setuid() is implemented like SysV with SAVED_IDS 
 * 
 * Note that SAVED_ID's is deficient in that a setuid root program
 * like sendmail, for example, cannot set its uid to be a normal 
 * user and then switch back, because if you're root, setuid() sets
 * the saved uid too.  If you don't like this, blame the bright people
 * in the POSIX committee and/or USG.  Note that the BSD-style setreuid()
 * will allow a root program to temporarily drop privileges and be able to
 * regain them by swapping the real and effective uid.  
 */
asmlinkage long sys_setuid(uid_t uid)
{
	int old_euid = current->euid;
	int old_ruid, old_suid, new_ruid, new_suid;
	int retval;

	retval = security_task_setuid(uid, (uid_t)-1, (uid_t)-1, LSM_SETID_ID);
	if (retval)
		return retval;

	old_ruid = new_ruid = current->uid;
	old_suid = current->suid;
	new_suid = old_suid;
	
	if (capable(CAP_SETUID)) {
		if (uid != old_ruid && set_user(uid, old_euid != uid) < 0)
			return -EAGAIN;
		new_suid = uid;
	} else if ((uid != current->uid) && (uid != new_suid))
		return -EPERM;

	if (old_euid != uid)
	{
		current->mm->dumpable = suid_dumpable;
		smp_wmb();
	}
	current->fsuid = current->euid = uid;
	current->suid = new_suid;

	key_fsuid_changed(current);
	proc_id_connector(current, PROC_EVENT_UID);

	return security_task_post_setuid(old_ruid, old_euid, old_suid, LSM_SETID_ID);
}


/*
 * This function implements a generic ability to update ruid, euid,
 * and suid.  This allows you to implement the 4.4 compatible seteuid().
 */
asmlinkage long sys_setresuid(uid_t ruid, uid_t euid, uid_t suid)
{
	int old_ruid = current->uid;
	int old_euid = current->euid;
	int old_suid = current->suid;
	int retval;

	retval = security_task_setuid(ruid, euid, suid, LSM_SETID_RES);
	if (retval)
		return retval;

	if (!capable(CAP_SETUID)) {
		if ((ruid != (uid_t) -1) && (ruid != current->uid) &&
		    (ruid != current->euid) && (ruid != current->suid))
			return -EPERM;
		if ((euid != (uid_t) -1) && (euid != current->uid) &&
		    (euid != current->euid) && (euid != current->suid))
			return -EPERM;
		if ((suid != (uid_t) -1) && (suid != current->uid) &&
		    (suid != current->euid) && (suid != current->suid))
			return -EPERM;
	}
	if (ruid != (uid_t) -1) {
		if (ruid != current->uid && set_user(ruid, euid != current->euid) < 0)
			return -EAGAIN;
	}
	if (euid != (uid_t) -1) {
		if (euid != current->euid)
		{
			current->mm->dumpable = suid_dumpable;
			smp_wmb();
		}
		current->euid = euid;
	}
	current->fsuid = current->euid;
	if (suid != (uid_t) -1)
		current->suid = suid;

	key_fsuid_changed(current);
	proc_id_connector(current, PROC_EVENT_UID);

	return security_task_post_setuid(old_ruid, old_euid, old_suid, LSM_SETID_RES);
}

asmlinkage long sys_getresuid(uid_t __user *ruid, uid_t __user *euid, uid_t __user *suid)
{
	int retval;

	if (!(retval = put_user(current->uid, ruid)) &&
	    !(retval = put_user(current->euid, euid)))
		retval = put_user(current->suid, suid);

	return retval;
}

/*
 * Same as above, but for rgid, egid, sgid.
 */
asmlinkage long sys_setresgid(gid_t rgid, gid_t egid, gid_t sgid)
{
	int retval;

	retval = security_task_setgid(rgid, egid, sgid, LSM_SETID_RES);
	if (retval)
		return retval;

	if (!capable(CAP_SETGID)) {
		if ((rgid != (gid_t) -1) && (rgid != current->gid) &&
		    (rgid != current->egid) && (rgid != current->sgid))
			return -EPERM;
		if ((egid != (gid_t) -1) && (egid != current->gid) &&
		    (egid != current->egid) && (egid != current->sgid))
			return -EPERM;
		if ((sgid != (gid_t) -1) && (sgid != current->gid) &&
		    (sgid != current->egid) && (sgid != current->sgid))
			return -EPERM;
	}
	if (egid != (gid_t) -1) {
		if (egid != current->egid)
		{
			current->mm->dumpable = suid_dumpable;
			smp_wmb();
		}
		current->egid = egid;
	}
	current->fsgid = current->egid;
	if (rgid != (gid_t) -1)
		current->gid = rgid;
	if (sgid != (gid_t) -1)
		current->sgid = sgid;

	key_fsgid_changed(current);
	proc_id_connector(current, PROC_EVENT_GID);
	return 0;
}

asmlinkage long sys_getresgid(gid_t __user *rgid, gid_t __user *egid, gid_t __user *sgid)
{
	int retval;

	if (!(retval = put_user(current->gid, rgid)) &&
	    !(retval = put_user(current->egid, egid)))
		retval = put_user(current->sgid, sgid);

	return retval;
}


/*
 * "setfsuid()" sets the fsuid - the uid used for filesystem checks. This
 * is used for "access()" and for the NFS daemon (letting nfsd stay at
 * whatever uid it wants to). It normally shadows "euid", except when
 * explicitly set by setfsuid() or for access..
 */
asmlinkage long sys_setfsuid(uid_t uid)
{
	int old_fsuid;

	old_fsuid = current->fsuid;
	if (security_task_setuid(uid, (uid_t)-1, (uid_t)-1, LSM_SETID_FS))
		return old_fsuid;

	if (uid == current->uid || uid == current->euid ||
	    uid == current->suid || uid == current->fsuid || 
	    capable(CAP_SETUID))
	{
		if (uid != old_fsuid)
		{
			current->mm->dumpable = suid_dumpable;
			smp_wmb();
		}
		current->fsuid = uid;
	}

	key_fsuid_changed(current);
	proc_id_connector(current, PROC_EVENT_UID);

	security_task_post_setuid(old_fsuid, (uid_t)-1, (uid_t)-1, LSM_SETID_FS);

	return old_fsuid;
}

/*
 * Samma på svenska..
 */
asmlinkage long sys_setfsgid(gid_t gid)
{
	int old_fsgid;

	old_fsgid = current->fsgid;
	if (security_task_setgid(gid, (gid_t)-1, (gid_t)-1, LSM_SETID_FS))
		return old_fsgid;

	if (gid == current->gid || gid == current->egid ||
	    gid == current->sgid || gid == current->fsgid || 
	    capable(CAP_SETGID))
	{
		if (gid != old_fsgid)
		{
			current->mm->dumpable = suid_dumpable;
			smp_wmb();
		}
		current->fsgid = gid;
		key_fsgid_changed(current);
		proc_id_connector(current, PROC_EVENT_GID);
	}
	return old_fsgid;
}

asmlinkage long sys_times(struct tms __user * tbuf)
{
	/*
	 *	In the SMP world we might just be unlucky and have one of
	 *	the times increment as we use it. Since the value is an
	 *	atomically safe type this is just fine. Conceptually its
	 *	as if the syscall took an instant longer to occur.
	 */
	if (tbuf) {
		struct tms tmp;
		struct task_struct *tsk = current;
		struct task_struct *t;
		cputime_t utime, stime, cutime, cstime;

		spin_lock_irq(&tsk->sighand->siglock);
		utime = tsk->signal->utime;
		stime = tsk->signal->stime;
		t = tsk;
		do {
			utime = cputime_add(utime, t->utime);
			stime = cputime_add(stime, t->stime);
			t = next_thread(t);
		} while (t != tsk);

		cutime = tsk->signal->cutime;
		cstime = tsk->signal->cstime;
		spin_unlock_irq(&tsk->sighand->siglock);

		tmp.tms_utime = cputime_to_clock_t(utime);
		tmp.tms_stime = cputime_to_clock_t(stime);
		tmp.tms_cutime = cputime_to_clock_t(cutime);
		tmp.tms_cstime = cputime_to_clock_t(cstime);
		if (copy_to_user(tbuf, &tmp, sizeof(struct tms)))
			return -EFAULT;
	}
	return (long) jiffies_64_to_clock_t(get_jiffies_64());
}

/*
 * This needs some heavy checking ...
 * I just haven't the stomach for it. I also don't fully
 * understand sessions/pgrp etc. Let somebody who does explain it.
 *
 * OK, I think I have the protection semantics right.... this is really
 * only important on a multi-user system anyway, to make sure one user
 * can't send a signal to a process owned by another.  -TYT, 12/12/91
 *
 * Auch. Had to add the 'did_exec' flag to conform completely to POSIX.
 * LBT 04.03.94
 */

asmlinkage long sys_setpgid(pid_t pid, pid_t pgid)
{
	struct task_struct *p;
	struct task_struct *group_leader = current->group_leader;
	int err = -EINVAL;

	if (!pid)
		pid = group_leader->pid;
	if (!pgid)
		pgid = pid;
	if (pgid < 0)
		return -EINVAL;

	/* From this point forward we keep holding onto the tasklist lock
	 * so that our parent does not change from under us. -DaveM
	 */
	write_lock_irq(&tasklist_lock);

	err = -ESRCH;
	p = find_task_by_pid(pid);
	if (!p)
		goto out;

	err = -EINVAL;
	if (!thread_group_leader(p))
		goto out;

	if (p->real_parent == group_leader) {
		err = -EPERM;
		if (p->signal->session != group_leader->signal->session)
			goto out;
		err = -EACCES;
		if (p->did_exec)
			goto out;
	} else {
		err = -ESRCH;
		if (p != group_leader)
			goto out;
	}

	err = -EPERM;
	if (p->signal->leader)
		goto out;

	if (pgid != pid) {
		struct task_struct *p;

		do_each_task_pid(pgid, PIDTYPE_PGID, p) {
			if (p->signal->session == group_leader->signal->session)
				goto ok_pgid;
		} while_each_task_pid(pgid, PIDTYPE_PGID, p);
		goto out;
	}

ok_pgid:
	err = security_task_setpgid(p, pgid);
	if (err)
		goto out;

	if (process_group(p) != pgid) {
		detach_pid(p, PIDTYPE_PGID);
		p->signal->pgrp = pgid;
		attach_pid(p, PIDTYPE_PGID, pgid);
	}

	err = 0;
out:
	/* All paths lead to here, thus we are safe. -DaveM */
	write_unlock_irq(&tasklist_lock);
	return err;
}

asmlinkage long sys_getpgid(pid_t pid)
{
	if (!pid) {
		return process_group(current);
	} else {
		int retval;
		struct task_struct *p;

		read_lock(&tasklist_lock);
		p = find_task_by_pid(pid);

		retval = -ESRCH;
		if (p) {
			retval = security_task_getpgid(p);
			if (!retval)
				retval = process_group(p);
		}
		read_unlock(&tasklist_lock);
		return retval;
	}
}

#ifdef __ARCH_WANT_SYS_GETPGRP

asmlinkage long sys_getpgrp(void)
{
	/* SMP - assuming writes are word atomic this is fine */
	return process_group(current);
}

#endif

asmlinkage long sys_getsid(pid_t pid)
{
	if (!pid) {
		return current->signal->session;
	} else {
		int retval;
		struct task_struct *p;

		read_lock(&tasklist_lock);
		p = find_task_by_pid(pid);

		retval = -ESRCH;
		if(p) {
			retval = security_task_getsid(p);
			if (!retval)
				retval = p->signal->session;
		}
		read_unlock(&tasklist_lock);
		return retval;
	}
}

asmlinkage long sys_setsid(void)
{
	struct task_struct *group_leader = current->group_leader;
	struct pid *pid;
	int err = -EPERM;

	mutex_lock(&tty_mutex);
	write_lock_irq(&tasklist_lock);

	pid = find_pid(PIDTYPE_PGID, group_leader->pid);
	if (pid)
		goto out;

	group_leader->signal->leader = 1;
	__set_special_pids(group_leader->pid, group_leader->pid);
	group_leader->signal->tty = NULL;
	group_leader->signal->tty_old_pgrp = 0;
	err = process_group(group_leader);
out:
	write_unlock_irq(&tasklist_lock);
	mutex_unlock(&tty_mutex);
	return err;
}

/*
 * Supplementary group IDs
 */

/* init to 2 - one for init_task, one to ensure it is never freed */
struct group_info init_groups = { .usage = ATOMIC_INIT(2) };

struct group_info *groups_alloc(int gidsetsize)
{
	struct group_info *group_info;
	int nblocks;
	int i;

	nblocks = (gidsetsize + NGROUPS_PER_BLOCK - 1) / NGROUPS_PER_BLOCK;
	/* Make sure we always allocate at least one indirect block pointer */
	nblocks = nblocks ? : 1;
	group_info = kmalloc(sizeof(*group_info) + nblocks*sizeof(gid_t *), GFP_USER);
	if (!group_info)
		return NULL;
	group_info->ngroups = gidsetsize;
	group_info->nblocks = nblocks;
	atomic_set(&group_info->usage, 1);

	if (gidsetsize <= NGROUPS_SMALL) {
		group_info->blocks[0] = group_info->small_block;
	} else {
		for (i = 0; i < nblocks; i++) {
			gid_t *b;
			b = (void *)__get_free_page(GFP_USER);
			if (!b)
				goto out_undo_partial_alloc;
			group_info->blocks[i] = b;
		}
	}
	return group_info;

out_undo_partial_alloc:
	while (--i >= 0) {
		free_page((unsigned long)group_info->blocks[i]);
	}
	kfree(group_info);
	return NULL;
}

EXPORT_SYMBOL(groups_alloc);

void groups_free(struct group_info *group_info)
{
	if (group_info->blocks[0] != group_info->small_block) {
		int i;
		for (i = 0; i < group_info->nblocks; i++)
			free_page((unsigned long)group_info->blocks[i]);
	}
	kfree(group_info);
}

EXPORT_SYMBOL(groups_free);

/* export the group_info to a user-space array */
static int groups_to_user(gid_t __user *grouplist,
    struct group_info *group_info)
{
	int i;
	int count = group_info->ngroups;

	for (i = 0; i < group_info->nblocks; i++) {
		int cp_count = min(NGROUPS_PER_BLOCK, count);
		int off = i * NGROUPS_PER_BLOCK;
		int len = cp_count * sizeof(*grouplist);

		if (copy_to_user(grouplist+off, group_info->blocks[i], len))
			return -EFAULT;

		count -= cp_count;
	}
	return 0;
}

/* fill a group_info from a user-space array - it must be allocated already */
static int groups_from_user(struct group_info *group_info,
    gid_t __user *grouplist)
 {
	int i;
	int count = group_info->ngroups;

	for (i = 0; i < group_info->nblocks; i++) {
		int cp_count = min(NGROUPS_PER_BLOCK, count);
		int off = i * NGROUPS_PER_BLOCK;
		int len = cp_count * sizeof(*grouplist);

		if (copy_from_user(group_info->blocks[i], grouplist+off, len))
			return -EFAULT;

		count -= cp_count;
	}
	return 0;
}

/* a simple Shell sort */
static void groups_sort(struct group_info *group_info)
{
	int base, max, stride;
	int gidsetsize = group_info->ngroups;

	for (stride = 1; stride < gidsetsize; stride = 3 * stride + 1)
		; /* nothing */
	stride /= 3;

	while (stride) {
		max = gidsetsize - stride;
		for (base = 0; base < max; base++) {
			int left = base;
			int right = left + stride;
			gid_t tmp = GROUP_AT(group_info, right);

			while (left >= 0 && GROUP_AT(group_info, left) > tmp) {
				GROUP_AT(group_info, right) =
				    GROUP_AT(group_info, left);
				right = left;
				left -= stride;
			}
			GROUP_AT(group_info, right) = tmp;
		}
		stride /= 3;
	}
}

/* a simple bsearch */
int groups_search(struct group_info *group_info, gid_t grp)
{
	unsigned int left, right;

	if (!group_info)
		return 0;

	left = 0;
	right = group_info->ngroups;
	while (left < right) {
		unsigned int mid = (left+right)/2;
		int cmp = grp - GROUP_AT(group_info, mid);
		if (cmp > 0)
			left = mid + 1;
		else if (cmp < 0)
			right = mid;
		else
			return 1;
	}
	return 0;
}

/* validate and set current->group_info */
int set_current_groups(struct group_info *group_info)
{
	int retval;
	struct group_info *old_info;

	retval = security_task_setgroups(group_info);
	if (retval)
		return retval;

	groups_sort(group_info);
	get_group_info(group_info);

	task_lock(current);
	old_info = current->group_info;
	current->group_info = group_info;
	task_unlock(current);

	put_group_info(old_info);

	return 0;
}

EXPORT_SYMBOL(set_current_groups);

asmlinkage long sys_getgroups(int gidsetsize, gid_t __user *grouplist)
{
	int i = 0;

	/*
	 *	SMP: Nobody else can change our grouplist. Thus we are
	 *	safe.
	 */

	if (gidsetsize < 0)
		return -EINVAL;

	/* no need to grab task_lock here; it cannot change */
	i = current->group_info->ngroups;
	if (gidsetsize) {
		if (i > gidsetsize) {
			i = -EINVAL;
			goto out;
		}
		if (groups_to_user(grouplist, current->group_info)) {
			i = -EFAULT;
			goto out;
		}
	}
out:
	return i;
}

/*
 *	SMP: Our groups are copy-on-write. We can set them safely
 *	without another task interfering.
 */
 
asmlinkage long sys_setgroups(int gidsetsize, gid_t __user *grouplist)
{
	struct group_info *group_info;
	int retval;

	if (!capable(CAP_SETGID))
		return -EPERM;
	if ((unsigned)gidsetsize > NGROUPS_MAX)
		return -EINVAL;

	group_info = groups_alloc(gidsetsize);
	if (!group_info)
		return -ENOMEM;
	retval = groups_from_user(group_info, grouplist);
	if (retval) {
		put_group_info(group_info);
		return retval;
	}

	retval = set_current_groups(group_info);
	put_group_info(group_info);

	return retval;
}

/*
 * Check whether we're fsgid/egid or in the supplemental group..
 */
int in_group_p(gid_t grp)
{
	int retval = 1;
	if (grp != current->fsgid) {
		retval = groups_search(current->group_info, grp);
	}
	return retval;
}

EXPORT_SYMBOL(in_group_p);

int in_egroup_p(gid_t grp)
{
	int retval = 1;
	if (grp != current->egid) {
		retval = groups_search(current->group_info, grp);
	}
	return retval;
}

EXPORT_SYMBOL(in_egroup_p);

DECLARE_RWSEM(uts_sem);

EXPORT_SYMBOL(uts_sem);

asmlinkage long sys_newuname(struct new_utsname __user * name)
{
	int errno = 0;

	down_read(&uts_sem);
	if (copy_to_user(name,&system_utsname,sizeof *name))
		errno = -EFAULT;
	up_read(&uts_sem);
	return errno;
}

asmlinkage long sys_sethostname(char __user *name, int len)
{
	int errno;
	char tmp[__NEW_UTS_LEN];

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (len < 0 || len > __NEW_UTS_LEN)
		return -EINVAL;
	down_write(&uts_sem);
	errno = -EFAULT;
	if (!copy_from_user(tmp, name, len)) {
		memcpy(system_utsname.nodename, tmp, len);
		system_utsname.nodename[len] = 0;
		errno = 0;
	}
	up_write(&uts_sem);
	return errno;
}

#ifdef __ARCH_WANT_SYS_GETHOSTNAME

asmlinkage long sys_gethostname(char __user *name, int len)
{
	int i, errno;

	if (len < 0)
		return -EINVAL;
	down_read(&uts_sem);
	i = 1 + strlen(system_utsname.nodename);
	if (i > len)
		i = len;
	errno = 0;
	if (copy_to_user(name, system_utsname.nodename, i))
		errno = -EFAULT;
	up_read(&uts_sem);
	return errno;
}

#endif

/*
 * Only setdomainname; getdomainname can be implemented by calling
 * uname()
 */
asmlinkage long sys_setdomainname(char __user *name, int len)
{
	int errno;
	char tmp[__NEW_UTS_LEN];

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (len < 0 || len > __NEW_UTS_LEN)
		return -EINVAL;

	down_write(&uts_sem);
	errno = -EFAULT;
	if (!copy_from_user(tmp, name, len)) {
		memcpy(system_utsname.domainname, tmp, len);
		system_utsname.domainname[len] = 0;
		errno = 0;
	}
	up_write(&uts_sem);
	return errno;
}

asmlinkage long sys_getrlimit(unsigned int resource, struct rlimit __user *rlim)
{
	if (resource >= RLIM_NLIMITS)
		return -EINVAL;
	else {
		struct rlimit value;
		task_lock(current->group_leader);
		value = current->signal->rlim[resource];
		task_unlock(current->group_leader);
		return copy_to_user(rlim, &value, sizeof(*rlim)) ? -EFAULT : 0;
	}
}

#ifdef __ARCH_WANT_SYS_OLD_GETRLIMIT

/*
 *	Back compatibility for getrlimit. Needed for some apps.
 */
 
asmlinkage long sys_old_getrlimit(unsigned int resource, struct rlimit __user *rlim)
{
	struct rlimit x;
	if (resource >= RLIM_NLIMITS)
		return -EINVAL;

	task_lock(current->group_leader);
	x = current->signal->rlim[resource];
	task_unlock(current->group_leader);
	if(x.rlim_cur > 0x7FFFFFFF)
		x.rlim_cur = 0x7FFFFFFF;
	if(x.rlim_max > 0x7FFFFFFF)
		x.rlim_max = 0x7FFFFFFF;
	return copy_to_user(rlim, &x, sizeof(x))?-EFAULT:0;
}

#endif

asmlinkage long sys_setrlimit(unsigned int resource, struct rlimit __user *rlim)
{
	struct rlimit new_rlim, *old_rlim;
	unsigned long it_prof_secs;
	int retval;

	if (resource >= RLIM_NLIMITS)
		return -EINVAL;
	if (copy_from_user(&new_rlim, rlim, sizeof(*rlim)))
		return -EFAULT;
	if (new_rlim.rlim_cur > new_rlim.rlim_max)
		return -EINVAL;
	old_rlim = current->signal->rlim + resource;
	if ((new_rlim.rlim_max > old_rlim->rlim_max) &&
	    !capable(CAP_SYS_RESOURCE))
		return -EPERM;
	if (resource == RLIMIT_NOFILE && new_rlim.rlim_max > NR_OPEN)
		return -EPERM;

	retval = security_task_setrlimit(resource, &new_rlim);
	if (retval)
		return retval;

	task_lock(current->group_leader);
	*old_rlim = new_rlim;
	task_unlock(current->group_leader);

	if (resource != RLIMIT_CPU)
		goto out;

	/*
	 * RLIMIT_CPU handling.   Note that the kernel fails to return an error
	 * code if it rejected the user's attempt to set RLIMIT_CPU.  This is a
	 * very long-standing error, and fixing it now risks breakage of
	 * applications, so we live with it
	 */
	if (new_rlim.rlim_cur == RLIM_INFINITY)
		goto out;

	it_prof_secs = cputime_to_secs(current->signal->it_prof_expires);
	if (it_prof_secs == 0 || new_rlim.rlim_cur <= it_prof_secs) {
		unsigned long rlim_cur = new_rlim.rlim_cur;
		cputime_t cputime;

		if (rlim_cur == 0) {
			/*
			 * The caller is asking for an immediate RLIMIT_CPU
			 * expiry.  But we use the zero value to mean "it was
			 * never set".  So let's cheat and make it one second
			 * instead
			 */
			rlim_cur = 1;
		}
		cputime = secs_to_cputime(rlim_cur);
		read_lock(&tasklist_lock);
		spin_lock_irq(&current->sighand->siglock);
		set_process_cpu_timer(current, CPUCLOCK_PROF, &cputime, NULL);
		spin_unlock_irq(&current->sighand->siglock);
		read_unlock(&tasklist_lock);
	}
out:
	return 0;
}

/*
 * It would make sense to put struct rusage in the task_struct,
 * except that would make the task_struct be *really big*.  After
 * task_struct gets moved into malloc'ed memory, it would
 * make sense to do this.  It will make moving the rest of the information
 * a lot simpler!  (Which we're not doing right now because we're not
 * measuring them yet).
 *
 * When sampling multiple threads for RUSAGE_SELF, under SMP we might have
 * races with threads incrementing their own counters.  But since word
 * reads are atomic, we either get new values or old values and we don't
 * care which for the sums.  We always take the siglock to protect reading
 * the c* fields from p->signal from races with exit.c updating those
 * fields when reaping, so a sample either gets all the additions of a
 * given child after it's reaped, or none so this sample is before reaping.
 *
 * tasklist_lock locking optimisation:
 * If we are current and single threaded, we do not need to take the tasklist
 * lock or the siglock.  No one else can take our signal_struct away,
 * no one else can reap the children to update signal->c* counters, and
 * no one else can race with the signal-> fields.
 * If we do not take the tasklist_lock, the signal-> fields could be read
 * out of order while another thread was just exiting. So we place a
 * read memory barrier when we avoid the lock.  On the writer side,
 * write memory barrier is implied in  __exit_signal as __exit_signal releases
 * the siglock spinlock after updating the signal-> fields.
 *
 * We don't really need the siglock when we access the non c* fields
 * of the signal_struct (for RUSAGE_SELF) even in multithreaded
 * case, since we take the tasklist lock for read and the non c* signal->
 * fields are updated only in __exit_signal, which is called with
 * tasklist_lock taken for write, hence these two threads cannot execute
 * concurrently.
 *
 */

static void k_getrusage(struct task_struct *p, int who, struct rusage *r)
{
	struct task_struct *t;
	unsigned long flags;
	cputime_t utime, stime;
	int need_lock = 0;

	memset((char *) r, 0, sizeof *r);
	utime = stime = cputime_zero;

	if (p != current || !thread_group_empty(p))
		need_lock = 1;

	if (need_lock) {
		read_lock(&tasklist_lock);
		if (unlikely(!p->signal)) {
			read_unlock(&tasklist_lock);
			return;
		}
	} else
		/* See locking comments above */
		smp_rmb();

	switch (who) {
		case RUSAGE_BOTH:
		case RUSAGE_CHILDREN:
			spin_lock_irqsave(&p->sighand->siglock, flags);
			utime = p->signal->cutime;
			stime = p->signal->cstime;
			r->ru_nvcsw = p->signal->cnvcsw;
			r->ru_nivcsw = p->signal->cnivcsw;
			r->ru_minflt = p->signal->cmin_flt;
			r->ru_majflt = p->signal->cmaj_flt;
			spin_unlock_irqrestore(&p->sighand->siglock, flags);

			if (who == RUSAGE_CHILDREN)
				break;

		case RUSAGE_SELF:
			utime = cputime_add(utime, p->signal->utime);
			stime = cputime_add(stime, p->signal->stime);
			r->ru_nvcsw += p->signal->nvcsw;
			r->ru_nivcsw += p->signal->nivcsw;
			r->ru_minflt += p->signal->min_flt;
			r->ru_majflt += p->signal->maj_flt;
			t = p;
			do {
				utime = cputime_add(utime, t->utime);
				stime = cputime_add(stime, t->stime);
				r->ru_nvcsw += t->nvcsw;
				r->ru_nivcsw += t->nivcsw;
				r->ru_minflt += t->min_flt;
				r->ru_majflt += t->maj_flt;
				t = next_thread(t);
			} while (t != p);
			break;

		default:
			BUG();
	}

	if (need_lock)
		read_unlock(&tasklist_lock);
	cputime_to_timeval(utime, &r->ru_utime);
	cputime_to_timeval(stime, &r->ru_stime);
}

int getrusage(struct task_struct *p, int who, struct rusage __user *ru)
{
	struct rusage r;
	k_getrusage(p, who, &r);
	return copy_to_user(ru, &r, sizeof(r)) ? -EFAULT : 0;
}

asmlinkage long sys_getrusage(int who, struct rusage __user *ru)
{
	if (who != RUSAGE_SELF && who != RUSAGE_CHILDREN)
		return -EINVAL;
	return getrusage(current, who, ru);
}

asmlinkage long sys_umask(int mask)
{
	mask = xchg(&current->fs->umask, mask & S_IRWXUGO);
	return mask;
}
    
asmlinkage long sys_prctl(int option, unsigned long arg2, unsigned long arg3,
			  unsigned long arg4, unsigned long arg5)
{
	long error;

	error = security_task_prctl(option, arg2, arg3, arg4, arg5);
	if (error)
		return error;

	switch (option) {
		case PR_SET_PDEATHSIG:
			if (!valid_signal(arg2)) {
				error = -EINVAL;
				break;
			}
			current->pdeath_signal = arg2;
			break;
		case PR_GET_PDEATHSIG:
			error = put_user(current->pdeath_signal, (int __user *)arg2);
			break;
		case PR_GET_DUMPABLE:
			error = current->mm->dumpable;
			break;
		case PR_SET_DUMPABLE:
			if (arg2 < 0 || arg2 > 2) {
				error = -EINVAL;
				break;
			}
			current->mm->dumpable = arg2;
			break;

		case PR_SET_UNALIGN:
			error = SET_UNALIGN_CTL(current, arg2);
			break;
		case PR_GET_UNALIGN:
			error = GET_UNALIGN_CTL(current, arg2);
			break;
		case PR_SET_FPEMU:
			error = SET_FPEMU_CTL(current, arg2);
			break;
		case PR_GET_FPEMU:
			error = GET_FPEMU_CTL(current, arg2);
			break;
		case PR_SET_FPEXC:
			error = SET_FPEXC_CTL(current, arg2);
			break;
		case PR_GET_FPEXC:
			error = GET_FPEXC_CTL(current, arg2);
			break;
		case PR_GET_TIMING:
			error = PR_TIMING_STATISTICAL;
			break;
		case PR_SET_TIMING:
			if (arg2 == PR_TIMING_STATISTICAL)
				error = 0;
			else
				error = -EINVAL;
			break;

		case PR_GET_KEEPCAPS:
			if (current->keep_capabilities)
				error = 1;
			break;
		case PR_SET_KEEPCAPS:
			if (arg2 != 0 && arg2 != 1) {
				error = -EINVAL;
				break;
			}
			current->keep_capabilities = arg2;
			break;
		case PR_SET_NAME: {
			struct task_struct *me = current;
			unsigned char ncomm[sizeof(me->comm)];

			ncomm[sizeof(me->comm)-1] = 0;
			if (strncpy_from_user(ncomm, (char __user *)arg2,
						sizeof(me->comm)-1) < 0)
				return -EFAULT;
			set_task_comm(me, ncomm);
			return 0;
		}
		case PR_GET_NAME: {
			struct task_struct *me = current;
			unsigned char tcomm[sizeof(me->comm)];

			get_task_comm(tcomm, me);
			if (copy_to_user((char __user *)arg2, tcomm, sizeof(tcomm)))
				return -EFAULT;
			return 0;
		}
		default:
			error = -EINVAL;
			break;
	}
	return error;
}
