// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/kernel/reboot.c
 *
 *  Copyright (C) 2013  Linus Torvalds
 */

#define pr_fmt(fmt)	"reboot: " fmt

#include <linux/ctype.h>
#include <linux/export.h>
#include <linux/kexec.h>
#include <linux/kmod.h>
#include <linux/kmsg_dump.h>
#include <linux/reboot.h>
#include <linux/suspend.h>
#include <linux/syscalls.h>
#include <linux/syscore_ops.h>
#include <linux/uaccess.h>

/*
 * this indicates whether you can reboot with ctrl-alt-del: the default is yes
 */

int C_A_D = 1;
struct pid *cad_pid;
EXPORT_SYMBOL(cad_pid);

#if defined(CONFIG_ARM)
#define DEFAULT_REBOOT_MODE		= REBOOT_HARD
#else
#define DEFAULT_REBOOT_MODE
#endif
enum reboot_mode reboot_mode DEFAULT_REBOOT_MODE;
EXPORT_SYMBOL_GPL(reboot_mode);
enum reboot_mode panic_reboot_mode = REBOOT_UNDEFINED;
EXPORT_SYMBOL_GPL(panic_reboot_mode);

/*
 * This variable is used privately to keep track of whether or not
 * reboot_type is still set to its default value (i.e., reboot= hasn't
 * been set on the command line).  This is needed so that we can
 * suppress DMI scanning for reboot quirks.  Without it, it's
 * impossible to override a faulty reboot quirk without recompiling.
 */
int reboot_default = 1;
int reboot_cpu;
enum reboot_type reboot_type = BOOT_ACPI;
int reboot_force;

/*
 * If set, this is used for preparing the system to power off.
 */

void (*pm_power_off_prepare)(void);
EXPORT_SYMBOL_GPL(pm_power_off_prepare);

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
	kmsg_dump(KMSG_DUMP_EMERG);
	machine_emergency_restart();
}
EXPORT_SYMBOL_GPL(emergency_restart);

void kernel_restart_prepare(char *cmd)
{
	blocking_notifier_call_chain(&reboot_notifier_list, SYS_RESTART, cmd);
	system_state = SYSTEM_RESTART;
	usermodehelper_disable();
	device_shutdown();
}

/**
 *	register_reboot_notifier - Register function to be called at reboot time
 *	@nb: Info about notifier function to be called
 *
 *	Registers a function with the list of functions
 *	to be called at reboot time.
 *
 *	Currently always returns zero, as blocking_notifier_chain_register()
 *	always returns zero.
 */
int register_reboot_notifier(struct notifier_block *nb)
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
int unregister_reboot_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&reboot_notifier_list, nb);
}
EXPORT_SYMBOL(unregister_reboot_notifier);

static void devm_unregister_reboot_notifier(struct device *dev, void *res)
{
	WARN_ON(unregister_reboot_notifier(*(struct notifier_block **)res));
}

int devm_register_reboot_notifier(struct device *dev, struct notifier_block *nb)
{
	struct notifier_block **rcnb;
	int ret;

	rcnb = devres_alloc(devm_unregister_reboot_notifier,
			    sizeof(*rcnb), GFP_KERNEL);
	if (!rcnb)
		return -ENOMEM;

	ret = register_reboot_notifier(nb);
	if (!ret) {
		*rcnb = nb;
		devres_add(dev, rcnb);
	} else {
		devres_free(rcnb);
	}

	return ret;
}
EXPORT_SYMBOL(devm_register_reboot_notifier);

/*
 *	Notifier list for kernel code which wants to be called
 *	to restart the system.
 */
static ATOMIC_NOTIFIER_HEAD(restart_handler_list);

/**
 *	register_restart_handler - Register function to be called to reset
 *				   the system
 *	@nb: Info about handler function to be called
 *	@nb->priority:	Handler priority. Handlers should follow the
 *			following guidelines for setting priorities.
 *			0:	Restart handler of last resort,
 *				with limited restart capabilities
 *			128:	Default restart handler; use if no other
 *				restart handler is expected to be available,
 *				and/or if restart functionality is
 *				sufficient to restart the entire system
 *			255:	Highest priority restart handler, will
 *				preempt all other restart handlers
 *
 *	Registers a function with code to be called to restart the
 *	system.
 *
 *	Registered functions will be called from machine_restart as last
 *	step of the restart sequence (if the architecture specific
 *	machine_restart function calls do_kernel_restart - see below
 *	for details).
 *	Registered functions are expected to restart the system immediately.
 *	If more than one function is registered, the restart handler priority
 *	selects which function will be called first.
 *
 *	Restart handlers are expected to be registered from non-architecture
 *	code, typically from drivers. A typical use case would be a system
 *	where restart functionality is provided through a watchdog. Multiple
 *	restart handlers may exist; for example, one restart handler might
 *	restart the entire system, while another only restarts the CPU.
 *	In such cases, the restart handler which only restarts part of the
 *	hardware is expected to register with low priority to ensure that
 *	it only runs if no other means to restart the system is available.
 *
 *	Currently always returns zero, as atomic_notifier_chain_register()
 *	always returns zero.
 */
int register_restart_handler(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&restart_handler_list, nb);
}
EXPORT_SYMBOL(register_restart_handler);

/**
 *	unregister_restart_handler - Unregister previously registered
 *				     restart handler
 *	@nb: Hook to be unregistered
 *
 *	Unregisters a previously registered restart handler function.
 *
 *	Returns zero on success, or %-ENOENT on failure.
 */
int unregister_restart_handler(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&restart_handler_list, nb);
}
EXPORT_SYMBOL(unregister_restart_handler);

/**
 *	do_kernel_restart - Execute kernel restart handler call chain
 *
 *	Calls functions registered with register_restart_handler.
 *
 *	Expected to be called from machine_restart as last step of the restart
 *	sequence.
 *
 *	Restarts the system immediately if a restart handler function has been
 *	registered. Otherwise does nothing.
 */
void do_kernel_restart(char *cmd)
{
	atomic_notifier_call_chain(&restart_handler_list, reboot_mode, cmd);
}

#ifdef CONFIG_NO_GKI
static ATOMIC_NOTIFIER_HEAD(pre_restart_handler_list);

int register_pre_restart_handler(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&pre_restart_handler_list, nb);
}
EXPORT_SYMBOL(register_pre_restart_handler);

int unregister_pre_restart_handler(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&pre_restart_handler_list, nb);
}
EXPORT_SYMBOL(unregister_pre_restart_handler);

void do_kernel_pre_restart(char *cmd)
{
	atomic_notifier_call_chain(&pre_restart_handler_list, reboot_mode, cmd);
}
#endif

void migrate_to_reboot_cpu(void)
{
	/* The boot cpu is always logical cpu 0 */
	int cpu = reboot_cpu;

	cpu_hotplug_disable();

	/* Make certain the cpu I'm about to reboot on is online */
	if (!cpu_online(cpu))
		cpu = cpumask_first(cpu_online_mask);

	/* Prevent races with other tasks migrating this task */
	current->flags |= PF_NO_SETAFFINITY;

	/* Make certain I only run on the appropriate processor */
	set_cpus_allowed_ptr(current, cpumask_of(cpu));
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
	migrate_to_reboot_cpu();
	syscore_shutdown();
	if (!cmd)
		pr_emerg("Restarting system\n");
	else
		pr_emerg("Restarting system with command '%s'\n", cmd);
	kmsg_dump(KMSG_DUMP_SHUTDOWN);
	machine_restart(cmd);
}
EXPORT_SYMBOL_GPL(kernel_restart);

static void kernel_shutdown_prepare(enum system_states state)
{
	blocking_notifier_call_chain(&reboot_notifier_list,
		(state == SYSTEM_HALT) ? SYS_HALT : SYS_POWER_OFF, NULL);
	system_state = state;
	usermodehelper_disable();
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
	migrate_to_reboot_cpu();
	syscore_shutdown();
	pr_emerg("System halted\n");
	kmsg_dump(KMSG_DUMP_SHUTDOWN);
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
	if (pm_power_off_prepare)
		pm_power_off_prepare();
	migrate_to_reboot_cpu();
	syscore_shutdown();
	pr_emerg("Power down\n");
	kmsg_dump(KMSG_DUMP_SHUTDOWN);
	machine_power_off();
}
EXPORT_SYMBOL_GPL(kernel_power_off);

DEFINE_MUTEX(system_transition_mutex);

/*
 * Reboot system call: for obvious reasons only root may call it,
 * and even root needs to set up some magic numbers in the registers
 * so that some mistake won't make this reboot the whole machine.
 * You can also set the meaning of the ctrl-alt-del-key here.
 *
 * reboot doesn't sync: do that yourself before calling this.
 */
SYSCALL_DEFINE4(reboot, int, magic1, int, magic2, unsigned int, cmd,
		void __user *, arg)
{
	struct pid_namespace *pid_ns = task_active_pid_ns(current);
	char buffer[256];
	int ret = 0;

	/* We only trust the superuser with rebooting the system. */
	if (!ns_capable(pid_ns->user_ns, CAP_SYS_BOOT))
		return -EPERM;

	/* For safety, we require "magic" arguments. */
	if (magic1 != LINUX_REBOOT_MAGIC1 ||
			(magic2 != LINUX_REBOOT_MAGIC2 &&
			magic2 != LINUX_REBOOT_MAGIC2A &&
			magic2 != LINUX_REBOOT_MAGIC2B &&
			magic2 != LINUX_REBOOT_MAGIC2C))
		return -EINVAL;

	/*
	 * If pid namespaces are enabled and the current task is in a child
	 * pid_namespace, the command is handled by reboot_pid_ns() which will
	 * call do_exit().
	 */
	ret = reboot_pid_ns(pid_ns, cmd);
	if (ret)
		return ret;

	/* Instead of trying to make the power_off code look like
	 * halt when pm_power_off is not set do it the easy way.
	 */
	if ((cmd == LINUX_REBOOT_CMD_POWER_OFF) && !pm_power_off)
		cmd = LINUX_REBOOT_CMD_HALT;

	mutex_lock(&system_transition_mutex);
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
		do_exit(0);
		panic("cannot halt");

	case LINUX_REBOOT_CMD_POWER_OFF:
		kernel_power_off();
		do_exit(0);
		break;

	case LINUX_REBOOT_CMD_RESTART2:
		ret = strncpy_from_user(&buffer[0], arg, sizeof(buffer) - 1);
		if (ret < 0) {
			ret = -EFAULT;
			break;
		}
		buffer[sizeof(buffer) - 1] = '\0';

		kernel_restart(buffer);
		break;

#ifdef CONFIG_KEXEC_CORE
	case LINUX_REBOOT_CMD_KEXEC:
		ret = kernel_kexec();
		break;
#endif

#ifdef CONFIG_HIBERNATION
	case LINUX_REBOOT_CMD_SW_SUSPEND:
		ret = hibernate();
		break;
#endif

	default:
		ret = -EINVAL;
		break;
	}
	mutex_unlock(&system_transition_mutex);
	return ret;
}

static void deferred_cad(struct work_struct *dummy)
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
	static DECLARE_WORK(cad_work, deferred_cad);

	if (C_A_D)
		schedule_work(&cad_work);
	else
		kill_cad_pid(SIGINT, 1);
}

char poweroff_cmd[POWEROFF_CMD_PATH_LEN] = "/sbin/poweroff";
static const char reboot_cmd[] = "/sbin/reboot";

static int run_cmd(const char *cmd)
{
	char **argv;
	static char *envp[] = {
		"HOME=/",
		"PATH=/sbin:/bin:/usr/sbin:/usr/bin",
		NULL
	};
	int ret;
	argv = argv_split(GFP_KERNEL, cmd, NULL);
	if (argv) {
		ret = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_EXEC);
		argv_free(argv);
	} else {
		ret = -ENOMEM;
	}

	return ret;
}

static int __orderly_reboot(void)
{
	int ret;

	ret = run_cmd(reboot_cmd);

	if (ret) {
		pr_warn("Failed to start orderly reboot: forcing the issue\n");
		emergency_sync();
		kernel_restart(NULL);
	}

	return ret;
}

static int __orderly_poweroff(bool force)
{
	int ret;

	ret = run_cmd(poweroff_cmd);

	if (ret && force) {
		pr_warn("Failed to start orderly shutdown: forcing the issue\n");

		/*
		 * I guess this should try to kick off some daemon to sync and
		 * poweroff asap.  Or not even bother syncing if we're doing an
		 * emergency shutdown?
		 */
		emergency_sync();
		kernel_power_off();
	}

	return ret;
}

static bool poweroff_force;

static void poweroff_work_func(struct work_struct *work)
{
	__orderly_poweroff(poweroff_force);
}

static DECLARE_WORK(poweroff_work, poweroff_work_func);

/**
 * orderly_poweroff - Trigger an orderly system poweroff
 * @force: force poweroff if command execution fails
 *
 * This may be called from any context to trigger a system shutdown.
 * If the orderly shutdown fails, it will force an immediate shutdown.
 */
void orderly_poweroff(bool force)
{
	if (force) /* do not override the pending "true" */
		poweroff_force = true;
	schedule_work(&poweroff_work);
}
EXPORT_SYMBOL_GPL(orderly_poweroff);

static void reboot_work_func(struct work_struct *work)
{
	__orderly_reboot();
}

static DECLARE_WORK(reboot_work, reboot_work_func);

/**
 * orderly_reboot - Trigger an orderly system reboot
 *
 * This may be called from any context to trigger a system reboot.
 * If the orderly reboot fails, it will force an immediate reboot.
 */
void orderly_reboot(void)
{
	schedule_work(&reboot_work);
}
EXPORT_SYMBOL_GPL(orderly_reboot);

static int __init reboot_setup(char *str)
{
	for (;;) {
		enum reboot_mode *mode;

		/*
		 * Having anything passed on the command line via
		 * reboot= will cause us to disable DMI checking
		 * below.
		 */
		reboot_default = 0;

		if (!strncmp(str, "panic_", 6)) {
			mode = &panic_reboot_mode;
			str += 6;
		} else {
			mode = &reboot_mode;
		}

		switch (*str) {
		case 'w':
			*mode = REBOOT_WARM;
			break;

		case 'c':
			*mode = REBOOT_COLD;
			break;

		case 'h':
			*mode = REBOOT_HARD;
			break;

		case 's':
			if (isdigit(*(str+1)))
				reboot_cpu = simple_strtoul(str+1, NULL, 0);
			else if (str[1] == 'm' && str[2] == 'p' &&
							isdigit(*(str+3)))
				reboot_cpu = simple_strtoul(str+3, NULL, 0);
			else
				*mode = REBOOT_SOFT;
			if (reboot_cpu >= num_possible_cpus()) {
				pr_err("Ignoring the CPU number in reboot= option. "
				       "CPU %d exceeds possible cpu number %d\n",
				       reboot_cpu, num_possible_cpus());
				reboot_cpu = 0;
				break;
			}
			break;

		case 'g':
			*mode = REBOOT_GPIO;
			break;

		case 'b':
		case 'a':
		case 'k':
		case 't':
		case 'e':
		case 'p':
			reboot_type = *str;
			break;

		case 'f':
			reboot_force = 1;
			break;
		}

		str = strchr(str, ',');
		if (str)
			str++;
		else
			break;
	}
	return 1;
}
__setup("reboot=", reboot_setup);
