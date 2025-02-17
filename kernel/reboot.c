// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/kernel/reboot.c
 *
 *  Copyright (C) 2013  Linus Torvalds
 */

#define pr_fmt(fmt)	"reboot: " fmt

#include <linux/atomic.h>
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

static int C_A_D = 1;
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

struct sys_off_handler {
	struct notifier_block nb;
	int (*sys_off_cb)(struct sys_off_data *data);
	void *cb_data;
	enum sys_off_mode mode;
	bool blocking;
	void *list;
	struct device *dev;
};

/*
 * This variable is used to indicate if a halt was initiated instead of a
 * reboot when the reboot call was invoked with LINUX_REBOOT_CMD_POWER_OFF, but
 * the system cannot be powered off. This allowes kernel_halt() to notify users
 * of that.
 */
static bool poweroff_fallback_to_halt;

/*
 * Temporary stub that prevents linkage failure while we're in process
 * of removing all uses of legacy pm_power_off() around the kernel.
 */
void __weak (*pm_power_off)(void);

/*
 *	Notifier list for kernel code which wants to be called
 *	at shutdown. This is used to stop any idling DMA operations
 *	and the like.
 */
static BLOCKING_NOTIFIER_HEAD(reboot_notifier_list);

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
	system_state = SYSTEM_RESTART;
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

/*
 *	Notifier list for kernel code which wants to be called
 *	to prepare system for restart.
 */
static BLOCKING_NOTIFIER_HEAD(restart_prep_handler_list);

static void do_kernel_restart_prepare(void)
{
	blocking_notifier_call_chain(&restart_prep_handler_list, 0, NULL);
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
	do_kernel_restart_prepare();
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
	if (poweroff_fallback_to_halt)
		pr_emerg("Power off not available: System halted instead\n");
	else
		pr_emerg("System halted\n");
	kmsg_dump(KMSG_DUMP_SHUTDOWN);
	machine_halt();
}
EXPORT_SYMBOL_GPL(kernel_halt);

/*
 *	Notifier list for kernel code which wants to be called
 *	to prepare system for power off.
 */
static BLOCKING_NOTIFIER_HEAD(power_off_prep_handler_list);

/*
 *	Notifier list for kernel code which wants to be called
 *	to power off system.
 */
static ATOMIC_NOTIFIER_HEAD(power_off_handler_list);

static int sys_off_notify(struct notifier_block *nb,
			  unsigned long mode, void *cmd)
{
	struct sys_off_handler *handler;
	struct sys_off_data data = {};

	handler = container_of(nb, struct sys_off_handler, nb);
	data.cb_data = handler->cb_data;
	data.mode = mode;
	data.cmd = cmd;
	data.dev = handler->dev;

	return handler->sys_off_cb(&data);
}

static struct sys_off_handler platform_sys_off_handler;

static struct sys_off_handler *alloc_sys_off_handler(int priority)
{
	struct sys_off_handler *handler;
	gfp_t flags;

	/*
	 * Platforms like m68k can't allocate sys_off handler dynamically
	 * at the early boot time because memory allocator isn't available yet.
	 */
	if (priority == SYS_OFF_PRIO_PLATFORM) {
		handler = &platform_sys_off_handler;
		if (handler->cb_data)
			return ERR_PTR(-EBUSY);
	} else {
		if (system_state > SYSTEM_RUNNING)
			flags = GFP_ATOMIC;
		else
			flags = GFP_KERNEL;

		handler = kzalloc(sizeof(*handler), flags);
		if (!handler)
			return ERR_PTR(-ENOMEM);
	}

	return handler;
}

static void free_sys_off_handler(struct sys_off_handler *handler)
{
	if (handler == &platform_sys_off_handler)
		memset(handler, 0, sizeof(*handler));
	else
		kfree(handler);
}

/**
 *	register_sys_off_handler - Register sys-off handler
 *	@mode: Sys-off mode
 *	@priority: Handler priority
 *	@callback: Callback function
 *	@cb_data: Callback argument
 *
 *	Registers system power-off or restart handler that will be invoked
 *	at the step corresponding to the given sys-off mode. Handler's callback
 *	should return NOTIFY_DONE to permit execution of the next handler in
 *	the call chain or NOTIFY_STOP to break the chain (in error case for
 *	example).
 *
 *	Multiple handlers can be registered at the default priority level.
 *
 *	Only one handler can be registered at the non-default priority level,
 *	otherwise ERR_PTR(-EBUSY) is returned.
 *
 *	Returns a new instance of struct sys_off_handler on success, or
 *	an ERR_PTR()-encoded error code otherwise.
 */
struct sys_off_handler *
register_sys_off_handler(enum sys_off_mode mode,
			 int priority,
			 int (*callback)(struct sys_off_data *data),
			 void *cb_data)
{
	struct sys_off_handler *handler;
	int err;

	handler = alloc_sys_off_handler(priority);
	if (IS_ERR(handler))
		return handler;

	switch (mode) {
	case SYS_OFF_MODE_POWER_OFF_PREPARE:
		handler->list = &power_off_prep_handler_list;
		handler->blocking = true;
		break;

	case SYS_OFF_MODE_POWER_OFF:
		handler->list = &power_off_handler_list;
		break;

	case SYS_OFF_MODE_RESTART_PREPARE:
		handler->list = &restart_prep_handler_list;
		handler->blocking = true;
		break;

	case SYS_OFF_MODE_RESTART:
		handler->list = &restart_handler_list;
		break;

	default:
		free_sys_off_handler(handler);
		return ERR_PTR(-EINVAL);
	}

	handler->nb.notifier_call = sys_off_notify;
	handler->nb.priority = priority;
	handler->sys_off_cb = callback;
	handler->cb_data = cb_data;
	handler->mode = mode;

	if (handler->blocking) {
		if (priority == SYS_OFF_PRIO_DEFAULT)
			err = blocking_notifier_chain_register(handler->list,
							       &handler->nb);
		else
			err = blocking_notifier_chain_register_unique_prio(handler->list,
									   &handler->nb);
	} else {
		if (priority == SYS_OFF_PRIO_DEFAULT)
			err = atomic_notifier_chain_register(handler->list,
							     &handler->nb);
		else
			err = atomic_notifier_chain_register_unique_prio(handler->list,
									 &handler->nb);
	}

	if (err) {
		free_sys_off_handler(handler);
		return ERR_PTR(err);
	}

	return handler;
}
EXPORT_SYMBOL_GPL(register_sys_off_handler);

/**
 *	unregister_sys_off_handler - Unregister sys-off handler
 *	@handler: Sys-off handler
 *
 *	Unregisters given sys-off handler.
 */
void unregister_sys_off_handler(struct sys_off_handler *handler)
{
	int err;

	if (IS_ERR_OR_NULL(handler))
		return;

	if (handler->blocking)
		err = blocking_notifier_chain_unregister(handler->list,
							 &handler->nb);
	else
		err = atomic_notifier_chain_unregister(handler->list,
						       &handler->nb);

	/* sanity check, shall never happen */
	WARN_ON(err);

	free_sys_off_handler(handler);
}
EXPORT_SYMBOL_GPL(unregister_sys_off_handler);

static void devm_unregister_sys_off_handler(void *data)
{
	struct sys_off_handler *handler = data;

	unregister_sys_off_handler(handler);
}

/**
 *	devm_register_sys_off_handler - Register sys-off handler
 *	@dev: Device that registers handler
 *	@mode: Sys-off mode
 *	@priority: Handler priority
 *	@callback: Callback function
 *	@cb_data: Callback argument
 *
 *	Registers resource-managed sys-off handler.
 *
 *	Returns zero on success, or error code on failure.
 */
int devm_register_sys_off_handler(struct device *dev,
				  enum sys_off_mode mode,
				  int priority,
				  int (*callback)(struct sys_off_data *data),
				  void *cb_data)
{
	struct sys_off_handler *handler;

	handler = register_sys_off_handler(mode, priority, callback, cb_data);
	if (IS_ERR(handler))
		return PTR_ERR(handler);
	handler->dev = dev;

	return devm_add_action_or_reset(dev, devm_unregister_sys_off_handler,
					handler);
}
EXPORT_SYMBOL_GPL(devm_register_sys_off_handler);

/**
 *	devm_register_power_off_handler - Register power-off handler
 *	@dev: Device that registers callback
 *	@callback: Callback function
 *	@cb_data: Callback's argument
 *
 *	Registers resource-managed sys-off handler with a default priority
 *	and using power-off mode.
 *
 *	Returns zero on success, or error code on failure.
 */
int devm_register_power_off_handler(struct device *dev,
				    int (*callback)(struct sys_off_data *data),
				    void *cb_data)
{
	return devm_register_sys_off_handler(dev,
					     SYS_OFF_MODE_POWER_OFF,
					     SYS_OFF_PRIO_DEFAULT,
					     callback, cb_data);
}
EXPORT_SYMBOL_GPL(devm_register_power_off_handler);

/**
 *	devm_register_restart_handler - Register restart handler
 *	@dev: Device that registers callback
 *	@callback: Callback function
 *	@cb_data: Callback's argument
 *
 *	Registers resource-managed sys-off handler with a default priority
 *	and using restart mode.
 *
 *	Returns zero on success, or error code on failure.
 */
int devm_register_restart_handler(struct device *dev,
				  int (*callback)(struct sys_off_data *data),
				  void *cb_data)
{
	return devm_register_sys_off_handler(dev,
					     SYS_OFF_MODE_RESTART,
					     SYS_OFF_PRIO_DEFAULT,
					     callback, cb_data);
}
EXPORT_SYMBOL_GPL(devm_register_restart_handler);

static struct sys_off_handler *platform_power_off_handler;

static int platform_power_off_notify(struct sys_off_data *data)
{
	void (*platform_power_power_off_cb)(void) = data->cb_data;

	platform_power_power_off_cb();

	return NOTIFY_DONE;
}

/**
 *	register_platform_power_off - Register platform-level power-off callback
 *	@power_off: Power-off callback
 *
 *	Registers power-off callback that will be called as last step
 *	of the power-off sequence. This callback is expected to be invoked
 *	for the last resort. Only one platform power-off callback is allowed
 *	to be registered at a time.
 *
 *	Returns zero on success, or error code on failure.
 */
int register_platform_power_off(void (*power_off)(void))
{
	struct sys_off_handler *handler;

	handler = register_sys_off_handler(SYS_OFF_MODE_POWER_OFF,
					   SYS_OFF_PRIO_PLATFORM,
					   platform_power_off_notify,
					   power_off);
	if (IS_ERR(handler))
		return PTR_ERR(handler);

	platform_power_off_handler = handler;

	return 0;
}
EXPORT_SYMBOL_GPL(register_platform_power_off);

/**
 *	unregister_platform_power_off - Unregister platform-level power-off callback
 *	@power_off: Power-off callback
 *
 *	Unregisters previously registered platform power-off callback.
 */
void unregister_platform_power_off(void (*power_off)(void))
{
	if (platform_power_off_handler &&
	    platform_power_off_handler->cb_data == power_off) {
		unregister_sys_off_handler(platform_power_off_handler);
		platform_power_off_handler = NULL;
	}
}
EXPORT_SYMBOL_GPL(unregister_platform_power_off);

static int legacy_pm_power_off(struct sys_off_data *data)
{
	if (pm_power_off)
		pm_power_off();

	return NOTIFY_DONE;
}

static void do_kernel_power_off_prepare(void)
{
	blocking_notifier_call_chain(&power_off_prep_handler_list, 0, NULL);
}

/**
 *	do_kernel_power_off - Execute kernel power-off handler call chain
 *
 *	Expected to be called as last step of the power-off sequence.
 *
 *	Powers off the system immediately if a power-off handler function has
 *	been registered. Otherwise does nothing.
 */
void do_kernel_power_off(void)
{
	struct sys_off_handler *sys_off = NULL;

	/*
	 * Register sys-off handlers for legacy PM callback. This allows
	 * legacy PM callbacks temporary co-exist with the new sys-off API.
	 *
	 * TODO: Remove legacy handlers once all legacy PM users will be
	 *       switched to the sys-off based APIs.
	 */
	if (pm_power_off)
		sys_off = register_sys_off_handler(SYS_OFF_MODE_POWER_OFF,
						   SYS_OFF_PRIO_DEFAULT,
						   legacy_pm_power_off, NULL);

	atomic_notifier_call_chain(&power_off_handler_list, 0, NULL);

	unregister_sys_off_handler(sys_off);
}

/**
 *	kernel_can_power_off - check whether system can be powered off
 *
 *	Returns true if power-off handler is registered and system can be
 *	powered off, false otherwise.
 */
bool kernel_can_power_off(void)
{
	return !atomic_notifier_call_chain_is_empty(&power_off_handler_list) ||
		pm_power_off;
}
EXPORT_SYMBOL_GPL(kernel_can_power_off);

/**
 *	kernel_power_off - power_off the system
 *
 *	Shutdown everything and perform a clean system power_off.
 */
void kernel_power_off(void)
{
	kernel_shutdown_prepare(SYSTEM_POWER_OFF);
	do_kernel_power_off_prepare();
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
	if ((cmd == LINUX_REBOOT_CMD_POWER_OFF) && !kernel_can_power_off()) {
		poweroff_fallback_to_halt = true;
		cmd = LINUX_REBOOT_CMD_HALT;
	}

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

#define POWEROFF_CMD_PATH_LEN  256
static char poweroff_cmd[POWEROFF_CMD_PATH_LEN] = "/sbin/poweroff";
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

static const char *hw_protection_action_str(enum hw_protection_action action)
{
	switch (action) {
	case HWPROT_ACT_SHUTDOWN:
		return "shutdown";
	case HWPROT_ACT_REBOOT:
		return "reboot";
	default:
		return "undefined";
	}
}

static enum hw_protection_action hw_failure_emergency_action;

/**
 * hw_failure_emergency_action_func - emergency action work after a known delay
 * @work: work_struct associated with the emergency action function
 *
 * This function is called in very critical situations to force
 * a kernel poweroff or reboot after a configurable timeout value.
 */
static void hw_failure_emergency_action_func(struct work_struct *work)
{
	const char *action_str = hw_protection_action_str(hw_failure_emergency_action);

	pr_emerg("Hardware protection timed-out. Trying forced %s\n",
		 action_str);

	/*
	 * We have reached here after the emergency action waiting period has
	 * expired. This means orderly_poweroff/reboot has not been able to
	 * shut off the system for some reason.
	 *
	 * Try to shut off the system immediately if possible
	 */

	if (hw_failure_emergency_action == HWPROT_ACT_REBOOT)
		kernel_restart(NULL);
	else
		kernel_power_off();

	/*
	 * Worst of the worst case trigger emergency restart
	 */
	pr_emerg("Hardware protection %s failed. Trying emergency restart\n",
		 action_str);
	emergency_restart();
}

static DECLARE_DELAYED_WORK(hw_failure_emergency_action_work,
			    hw_failure_emergency_action_func);

/**
 * hw_failure_emergency_schedule - Schedule an emergency system shutdown or reboot
 *
 * @action:		The hardware protection action to be taken
 * @action_delay_ms:	Time in milliseconds to elapse before triggering action
 *
 * This may be called from any critical situation to trigger a system shutdown
 * or reboot after a given period of time.
 * If time is negative this is not scheduled.
 */
static void hw_failure_emergency_schedule(enum hw_protection_action action,
					  int action_delay_ms)
{
	if (action_delay_ms <= 0)
		return;
	hw_failure_emergency_action = action;
	schedule_delayed_work(&hw_failure_emergency_action_work,
			      msecs_to_jiffies(action_delay_ms));
}

/**
 * __hw_protection_shutdown - Trigger an emergency system shutdown or reboot
 *
 * @reason:		Reason of emergency shutdown or reboot to be printed.
 * @ms_until_forced:	Time to wait for orderly shutdown or reboot before
 *			triggering it. Negative value disables the forced
 *			shutdown or reboot.
 * @action:		The hardware protection action to be taken.
 *
 * Initiate an emergency system shutdown or reboot in order to protect
 * hardware from further damage. Usage examples include a thermal protection.
 * NOTE: The request is ignored if protection shutdown or reboot is already
 * pending even if the previous request has given a large timeout for forced
 * shutdown/reboot.
 */
void __hw_protection_shutdown(const char *reason, int ms_until_forced,
			      enum hw_protection_action action)
{
	static atomic_t allow_proceed = ATOMIC_INIT(1);

	pr_emerg("HARDWARE PROTECTION shutdown (%s)\n", reason);

	/* Shutdown should be initiated only once. */
	if (!atomic_dec_and_test(&allow_proceed))
		return;

	/*
	 * Queue a backup emergency shutdown in the event of
	 * orderly_poweroff failure
	 */
	hw_failure_emergency_schedule(action, ms_until_forced);
	if (action == HWPROT_ACT_REBOOT)
		orderly_reboot();
	else
		orderly_poweroff(true);
}
EXPORT_SYMBOL_GPL(__hw_protection_shutdown);

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
			/*
			 * reboot_cpu is s[mp]#### with #### being the processor
			 * to be used for rebooting. Skip 's' or 'smp' prefix.
			 */
			str += str[1] == 'm' && str[2] == 'p' ? 3 : 1;

			if (isdigit(str[0])) {
				int cpu = simple_strtoul(str, NULL, 0);

				if (cpu >= num_possible_cpus()) {
					pr_err("Ignoring the CPU number in reboot= option. "
					"CPU %d exceeds possible cpu number %d\n",
					cpu, num_possible_cpus());
					break;
				}
				reboot_cpu = cpu;
			} else
				*mode = REBOOT_SOFT;
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

#ifdef CONFIG_SYSFS

#define REBOOT_COLD_STR		"cold"
#define REBOOT_WARM_STR		"warm"
#define REBOOT_HARD_STR		"hard"
#define REBOOT_SOFT_STR		"soft"
#define REBOOT_GPIO_STR		"gpio"
#define REBOOT_UNDEFINED_STR	"undefined"

#define BOOT_TRIPLE_STR		"triple"
#define BOOT_KBD_STR		"kbd"
#define BOOT_BIOS_STR		"bios"
#define BOOT_ACPI_STR		"acpi"
#define BOOT_EFI_STR		"efi"
#define BOOT_PCI_STR		"pci"

static ssize_t mode_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	const char *val;

	switch (reboot_mode) {
	case REBOOT_COLD:
		val = REBOOT_COLD_STR;
		break;
	case REBOOT_WARM:
		val = REBOOT_WARM_STR;
		break;
	case REBOOT_HARD:
		val = REBOOT_HARD_STR;
		break;
	case REBOOT_SOFT:
		val = REBOOT_SOFT_STR;
		break;
	case REBOOT_GPIO:
		val = REBOOT_GPIO_STR;
		break;
	default:
		val = REBOOT_UNDEFINED_STR;
	}

	return sysfs_emit(buf, "%s\n", val);
}
static ssize_t mode_store(struct kobject *kobj, struct kobj_attribute *attr,
			  const char *buf, size_t count)
{
	if (!capable(CAP_SYS_BOOT))
		return -EPERM;

	if (!strncmp(buf, REBOOT_COLD_STR, strlen(REBOOT_COLD_STR)))
		reboot_mode = REBOOT_COLD;
	else if (!strncmp(buf, REBOOT_WARM_STR, strlen(REBOOT_WARM_STR)))
		reboot_mode = REBOOT_WARM;
	else if (!strncmp(buf, REBOOT_HARD_STR, strlen(REBOOT_HARD_STR)))
		reboot_mode = REBOOT_HARD;
	else if (!strncmp(buf, REBOOT_SOFT_STR, strlen(REBOOT_SOFT_STR)))
		reboot_mode = REBOOT_SOFT;
	else if (!strncmp(buf, REBOOT_GPIO_STR, strlen(REBOOT_GPIO_STR)))
		reboot_mode = REBOOT_GPIO;
	else
		return -EINVAL;

	reboot_default = 0;

	return count;
}
static struct kobj_attribute reboot_mode_attr = __ATTR_RW(mode);

#ifdef CONFIG_X86
static ssize_t force_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", reboot_force);
}
static ssize_t force_store(struct kobject *kobj, struct kobj_attribute *attr,
			  const char *buf, size_t count)
{
	bool res;

	if (!capable(CAP_SYS_BOOT))
		return -EPERM;

	if (kstrtobool(buf, &res))
		return -EINVAL;

	reboot_default = 0;
	reboot_force = res;

	return count;
}
static struct kobj_attribute reboot_force_attr = __ATTR_RW(force);

static ssize_t type_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	const char *val;

	switch (reboot_type) {
	case BOOT_TRIPLE:
		val = BOOT_TRIPLE_STR;
		break;
	case BOOT_KBD:
		val = BOOT_KBD_STR;
		break;
	case BOOT_BIOS:
		val = BOOT_BIOS_STR;
		break;
	case BOOT_ACPI:
		val = BOOT_ACPI_STR;
		break;
	case BOOT_EFI:
		val = BOOT_EFI_STR;
		break;
	case BOOT_CF9_FORCE:
		val = BOOT_PCI_STR;
		break;
	default:
		val = REBOOT_UNDEFINED_STR;
	}

	return sysfs_emit(buf, "%s\n", val);
}
static ssize_t type_store(struct kobject *kobj, struct kobj_attribute *attr,
			  const char *buf, size_t count)
{
	if (!capable(CAP_SYS_BOOT))
		return -EPERM;

	if (!strncmp(buf, BOOT_TRIPLE_STR, strlen(BOOT_TRIPLE_STR)))
		reboot_type = BOOT_TRIPLE;
	else if (!strncmp(buf, BOOT_KBD_STR, strlen(BOOT_KBD_STR)))
		reboot_type = BOOT_KBD;
	else if (!strncmp(buf, BOOT_BIOS_STR, strlen(BOOT_BIOS_STR)))
		reboot_type = BOOT_BIOS;
	else if (!strncmp(buf, BOOT_ACPI_STR, strlen(BOOT_ACPI_STR)))
		reboot_type = BOOT_ACPI;
	else if (!strncmp(buf, BOOT_EFI_STR, strlen(BOOT_EFI_STR)))
		reboot_type = BOOT_EFI;
	else if (!strncmp(buf, BOOT_PCI_STR, strlen(BOOT_PCI_STR)))
		reboot_type = BOOT_CF9_FORCE;
	else
		return -EINVAL;

	reboot_default = 0;

	return count;
}
static struct kobj_attribute reboot_type_attr = __ATTR_RW(type);
#endif

#ifdef CONFIG_SMP
static ssize_t cpu_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", reboot_cpu);
}
static ssize_t cpu_store(struct kobject *kobj, struct kobj_attribute *attr,
			  const char *buf, size_t count)
{
	unsigned int cpunum;
	int rc;

	if (!capable(CAP_SYS_BOOT))
		return -EPERM;

	rc = kstrtouint(buf, 0, &cpunum);

	if (rc)
		return rc;

	if (cpunum >= num_possible_cpus())
		return -ERANGE;

	reboot_default = 0;
	reboot_cpu = cpunum;

	return count;
}
static struct kobj_attribute reboot_cpu_attr = __ATTR_RW(cpu);
#endif

static struct attribute *reboot_attrs[] = {
	&reboot_mode_attr.attr,
#ifdef CONFIG_X86
	&reboot_force_attr.attr,
	&reboot_type_attr.attr,
#endif
#ifdef CONFIG_SMP
	&reboot_cpu_attr.attr,
#endif
	NULL,
};

#ifdef CONFIG_SYSCTL
static const struct ctl_table kern_reboot_table[] = {
	{
		.procname       = "poweroff_cmd",
		.data           = &poweroff_cmd,
		.maxlen         = POWEROFF_CMD_PATH_LEN,
		.mode           = 0644,
		.proc_handler   = proc_dostring,
	},
	{
		.procname       = "ctrl-alt-del",
		.data           = &C_A_D,
		.maxlen         = sizeof(int),
		.mode           = 0644,
		.proc_handler   = proc_dointvec,
	},
};

static void __init kernel_reboot_sysctls_init(void)
{
	register_sysctl_init("kernel", kern_reboot_table);
}
#else
#define kernel_reboot_sysctls_init() do { } while (0)
#endif /* CONFIG_SYSCTL */

static const struct attribute_group reboot_attr_group = {
	.attrs = reboot_attrs,
};

static int __init reboot_ksysfs_init(void)
{
	struct kobject *reboot_kobj;
	int ret;

	reboot_kobj = kobject_create_and_add("reboot", kernel_kobj);
	if (!reboot_kobj)
		return -ENOMEM;

	ret = sysfs_create_group(reboot_kobj, &reboot_attr_group);
	if (ret) {
		kobject_put(reboot_kobj);
		return ret;
	}

	kernel_reboot_sysctls_init();

	return 0;
}
late_initcall(reboot_ksysfs_init);

#endif
