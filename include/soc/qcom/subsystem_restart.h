/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2014-2019 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __SUBSYS_RESTART_H
#define __SUBSYS_RESTART_H

#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/device.h>

struct subsys_device;
extern struct bus_type subsys_bus_type;

enum {
	RESET_SOC = 0,
	RESET_SUBSYS_COUPLED,
	RESET_LEVEL_MAX
};

enum crash_status {
	CRASH_STATUS_NO_CRASH = 0,
	CRASH_STATUS_ERR_FATAL,
	CRASH_STATUS_WDOG_BITE,
};

struct device;
struct module;

enum ssr_comm {
	SUBSYS_TO_SUBSYS_SYSMON,
	SUBSYS_TO_HLOS,
	HLOS_TO_SUBSYS_SYSMON_SHUTDOWN,
	HLOS_TO_SUBSYS_SYSMON_DSENTER,
	NUM_SSR_COMMS,
};

/**
 * struct subsys_notif_timeout - timeout data used by notification timeout hdlr
 * @comm_type: Specifies if the type of communication being tracked is
 * through sysmon between two subsystems, subsystem notifier call chain, or
 * sysmon shutdown.
 * @dest_name: subsystem to which sysmon notification is being sent to
 * @source_name: subsystem which generated event that notification is being sent
 * for
 * @timer: timer for scheduling timeout
 */
struct subsys_notif_timeout {
	enum ssr_comm comm_type;
	const char *dest_name;
	const char *source_name;
	struct timer_list timer;
};

/**
 * struct subsys_desc - subsystem descriptor
 * @name: name of subsystem
 * @fw_name: firmware name
 * @pon_depends_on: subsystem this subsystem wants to power-on first. If the
 * dependednt subsystem is already powered-on, the framework won't try to power
 * it back up again.
 * @poff_depends_on: subsystem this subsystem wants to power-off first. If the
 * dependednt subsystem is already powered-off, the framework won't try to power
 * it off again.
 * @dev: parent device
 * @owner: module the descriptor belongs to
 * @shutdown: Stop a subsystem
 * @powerup_notify: Notify about start of a subsystem
 * @powerup: Start a subsystem
 * @crash_shutdown: Shutdown a subsystem when the system crashes (can't sleep)
 * @ramdump: Collect a ramdump of the subsystem
 * @free_memory: Free the memory associated with this subsystem
 * @no_auth: Set if subsystem does not rely on PIL to authenticate and bring
 * it out of reset
 * @ssctl_instance_id: Instance id used to connect with SSCTL service
 * @sysmon_pid:	pdev id that sysmon is probed with for the subsystem
 * @sysmon_shutdown_ret: Return value for the call to sysmon_send_shutdown
 * @system_debug: If "set", triggers a device restart when the
 * subsystem's wdog bite handler is invoked.
 * @ignore_ssr_failure: SSR failures are usually fatal and results in panic. If
 * set will ignore failure.
 * @edge: GLINK logical name of the subsystem
 */
struct subsys_desc {
	const char *name;
	char fw_name[256];
	const char *pon_depends_on;
	const char *poff_depends_on;
	struct device *dev;
	struct module *owner;

	int (*shutdown)(const struct subsys_desc *desc, bool force_stop);
	int (*enter_ds)(const struct subsys_desc *desc);
	int (*powerup_notify)(const struct subsys_desc *desc);
	int (*powerup)(const struct subsys_desc *desc);
	void (*crash_shutdown)(const struct subsys_desc *desc);
	int (*ramdump)(int need_dumps, const struct subsys_desc *desc);
	void (*free_memory)(const struct subsys_desc *desc);
	struct completion shutdown_ack;
	struct completion dsentry_ack;
	int err_fatal_gpio;
	int force_stop_bit;
	int ramdump_disable_irq;
	int shutdown_ack_irq;
	int dsentry_ack_irq;
	int ramdump_disable;
	bool no_auth;
	bool pil_mss_memsetup;
	int ssctl_instance_id;
	u32 sysmon_pid;
	int sysmon_shutdown_ret;
	int sysmon_dsentry_ret;
	bool system_debug;
	bool ignore_ssr_failure;
	const char *edge;
	struct qcom_smem_state *state;
#ifdef CONFIG_SETUP_SSR_NOTIF_TIMEOUTS
	struct subsys_notif_timeout timeout_data;
#endif /* CONFIG_SETUP_SSR_NOTIF_TIMEOUTS */
};

/**
 * struct notif_data - additional notif information
 * @crashed: indicates if subsystem has crashed due to wdog bite or err fatal
 * @enable_ramdump: ramdumps disabled if set to 0
 * @enable_mini_ramdumps: enable flag for minimized critical-memory-only
 * ramdumps
 * @no_auth: set if subsystem does not use PIL to bring it out of reset
 * @pdev: subsystem platform device pointer
 */
struct notif_data {
	enum crash_status crashed;
	int enable_ramdump;
	int enable_mini_ramdumps;
	bool no_auth;
	struct platform_device *pdev;
};

#if IS_ENABLED(CONFIG_MSM_SUBSYSTEM_RESTART)

extern int subsys_get_restart_level(struct subsys_device *dev);
extern int subsystem_restart_dev(struct subsys_device *dev);
extern int subsystem_restart(const char *name);
extern int subsystem_crashed(const char *name);
extern int subsystem_start_notify(const char *name);
extern int subsystem_stop_notify(const char *subsystem);
extern int subsystem_ds_entry(const char *subsystem);
extern int subsystem_ds_exit(const char *name);
extern int subsystem_s2d_entry(const char *subsystem);
extern int subsystem_s2d_exit(const char *name);

extern void *subsystem_get(const char *name);
extern void *subsystem_get_with_fwname(const char *name, const char *fw_name);
extern int subsystem_set_fwname(const char *name, const char *fw_name);
extern void subsystem_put(void *subsystem);

extern struct subsys_device *subsys_register(struct subsys_desc *desc);
extern void subsys_unregister(struct subsys_device *dev);

extern void subsys_set_crash_status(struct subsys_device *dev,
					enum crash_status crashed);
extern enum crash_status subsys_get_crash_status(struct subsys_device *dev);
void notify_proxy_vote(struct device *device);
void notify_proxy_unvote(struct device *device);
void notify_before_auth_and_reset(struct device *device);
static inline void complete_shutdown_ack(struct subsys_desc *desc)
{
	complete(&desc->shutdown_ack);
}
static inline void complete_dsentry_ack(struct subsys_desc *desc)
{
	complete(&desc->dsentry_ack);
}
struct subsys_device *find_subsys_device(const char *str);
#else

static inline int subsys_get_restart_level(struct subsys_device *dev)
{
	return 0;
}

static inline int subsystem_restart_dev(struct subsys_device *dev)
{
	return 0;
}

static inline int subsystem_restart(const char *name)
{
	return 0;
}

static inline int subsystem_crashed(const char *name)
{
	return 0;
}

extern int subsystem_start_notify(const char *name)
{
	return 0;
}

extern int subsystem_stop_notify(const char *subsystem)
{
	return 0;
}

/**
 * static int subsystem_ds_entry(const char *subsystem)
 * {
 *     return 0;
 * }
 *
 * static int subsystem_ds_exit(const char *name)
 * {
 *     return 0;
 * }
 *
 * static int subsystem_s2d_exit(const char *name)
 * {
 *     return 0;
 * }
 *
 * static int subsystem_s2d_entry(const char *name)
 * {
 *     return 0;
 * }
 */

static inline void *subsystem_get(const char *name)
{
	return NULL;
}

static inline void *subsystem_get_with_fwname(const char *name,
				const char *fw_name) {
	return NULL;
}

static inline int subsystem_set_fwname(const char *name,
				const char *fw_name) {
	return 0;
}

static inline void subsystem_put(void *subsystem) { }

static inline
struct subsys_device *subsys_register(struct subsys_desc *desc)
{
	return NULL;
}

static inline void subsys_unregister(struct subsys_device *dev) { }

static inline void subsys_set_crash_status(struct subsys_device *dev,
						enum crash_status crashed) { }
static inline
enum crash_status subsys_get_crash_status(struct subsys_device *dev)
{
	return false;
}
static inline void notify_proxy_vote(struct device *device) { }
static inline void notify_proxy_unvote(struct device *device) { }
static inline void notify_before_auth_and_reset(struct device *device) { }
#endif
/* CONFIG_MSM_SUBSYSTEM_RESTART */

/* Helper wrappers */
static inline void wakeup_source_trash(struct wakeup_source *ws)
{
	if (!ws)
		return;

	wakeup_source_remove(ws);
	__pm_relax(ws);
}

#endif
