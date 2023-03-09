/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef PM_SILENT_MODE_H
#define PM_SILENT_MODE_H

#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/notifier.h>

/* Silent Modes  */
enum silent_boot_mode {
	MODE_NON_SILENT = 1,
	MODE_SILENT,
};

enum silent_boot_mode_gpio {
	MODE_GPIO_LOW = 0,
	MODE_GPIO_HIGH,
};

/* External Modes */
enum silent_mode_cpms_mode {
	USERSPACE_CONTROL_DISABLED = 0,
	USERSPACE_CONTROL_ENABLED,
};

/*
 * External functions to get and set the silent mode state
 * Cannot be accessed from User space
 */

extern int pm_silentmode_hw_state_get(void);


extern int  pm_silentmode_update(int val, struct kobject *kobj, bool us);
extern int  pm_silentmode_status(void);

extern int register_pm_silentmode_notifier(struct notifier_block *nb);
extern int unregister_pm_silentmode_notifier(struct notifier_block *nb);

extern int pm_silentmode_get_mode(void);
#endif /*PM_SILENT_MODE*/
