/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019-2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _SOC_QCOM_WATCHDOG_H_
#define _SOC_QCOM_WATCHDOG_H_

#ifdef CONFIG_QCOM_FORCE_WDOG_BITE_ON_PANIC
#define WDOG_BITE_ON_PANIC 1
#else
#define WDOG_BITE_ON_PANIC 0
#endif

#ifdef CONFIG_QCOM_WDOG_BITE_EARLY_PANIC
#define WDOG_BITE_EARLY_PANIC 1
#else
#define WDOG_BITE_EARLY_PANIC 0
#endif

/*
 * Watchdog ipi optimization:
 * Does not ping cores in low power mode at pet time to save power.
 * This feature is enabled by default.
 *
 * Can be turned off, by enabling CONFIG_QCOM_WDOG_IPI_ENABLE.
 */
#ifdef CONFIG_QCOM_WDOG_IPI_ENABLE
#define IPI_CORES_IN_LPM 1
#else
#define IPI_CORES_IN_LPM 0
#endif

#if IS_ENABLED(CONFIG_QCOM_WDT_CORE)
#include <linux/platform_device.h>

/**
 *  The enable constant that can be used between the core framework and the
 *  watchdog driver.
 */
#define EN   0
#define UNMASKED_INT_EN  1

/* Watchdog property values */
#define QCOM_WATCHDOG_BARK_TIME	    CONFIG_QCOM_WATCHDOG_BARK_TIME
#define QCOM_WATCHDOG_PET_TIME	    CONFIG_QCOM_WATCHDOG_PET_TIME

#ifdef CONFIG_QCOM_WATCHDOG_IPI_PING
#define QCOM_WATCHDOG_IPI_PING 1
#else
#define QCOM_WATCHDOG_IPI_PING 0
#endif

#ifdef CONFIG_QCOM_WATCHDOG_WAKEUP_ENABLE
#define QCOM_WATCHDOG_WAKEUP_ENABLE 1
#else
#define QCOM_WATCHDOG_WAKEUP_ENABLE 0
#endif

#ifdef CONFIG_QCOM_WATCHDOG_USERSPACE_PET
#define QCOM_WATCHDOG_USERSPACE_PET 1
#else
#define QCOM_WATCHDOG_USERSPACE_PET 0
#endif


#define WDOG_NR_IPI	10
#define NR_TOP_HITTERS 5

struct qcom_wdt_ops;
struct msm_watchdog_data;

/** qcom_wdt_ops - The msm-watchdog-devices operations
 *
 * @set_bark_time:      The routine for setting the watchdog bark time.
 * @set_bite_time:      The routine for setting the watchdog bite time.
 * @reset_wdt:          The routine for resetting the watchdog timer.
 * @enable_wdt:         The routine for enabling the watchdog.
 * @disable_wdt:        The routine for disabling the watchdog.
 * @show_wdt_status:     The routine that shows the status of the watchdog.
 *
 * The qcom_wdt_ops structure contains a list of operations that are
 * used to control the watchdog.
 */
struct qcom_wdt_ops {
	int (*set_bark_time)(u32 time, struct msm_watchdog_data *wdog_dd);
	int (*set_bite_time)(u32 time, struct msm_watchdog_data *wdog_dd);
	int (*reset_wdt)(struct msm_watchdog_data *wdog_dd);
	int (*enable_wdt)(u32 val, struct msm_watchdog_data *wdog_dd);
	int (*disable_wdt)(struct msm_watchdog_data *wdog_dd);
	int (*show_wdt_status)(struct msm_watchdog_data *wdog_dd);
};

/*
 * user_pet_enable:
 *	Require userspace to write to a sysfs file every pet_time milliseconds.
 *	Disabled by default on boot.
 */
struct msm_watchdog_data {
	void __iomem *base;
	struct device *dev;
	struct qcom_wdt_ops *ops;
	unsigned int pet_time;
	unsigned int bark_time;
	unsigned int bark_irq;
	bool do_ipi_ping;
	bool in_panic;
	bool wakeup_irq_enable;
	bool irq_ppi;
	unsigned long long last_pet;
	cpumask_t alive_mask;
	struct mutex disable_lock;
	struct msm_watchdog_data * __percpu *wdog_cpu_dd;
	struct notifier_block panic_blk;
	struct notifier_block die_blk;
	struct notifier_block wdog_cpu_pm_nb;
	struct notifier_block restart_blk;

	bool enabled;
	bool user_pet_enabled;

	struct task_struct *watchdog_task;
	struct timer_list pet_timer;
	wait_queue_head_t pet_complete;

	bool timer_expired;
	bool user_pet_complete;
	unsigned long long timer_fired;
	unsigned long long thread_start;
	unsigned long long ping_start[NR_CPUS];
	unsigned long long ping_end[NR_CPUS];
	int cpu_idle_pc_state[NR_CPUS];
	bool freeze_in_progress;
	spinlock_t freeze_lock;
	struct timer_list user_pet_timer;
	bool hibernate;
};

extern void qcom_wdt_trigger_bite(void);
int qcom_wdt_register(struct platform_device *pdev,
			struct msm_watchdog_data *wdog_dd,
			char *wdog_dd_name);
int qcom_wdt_pet_suspend(struct device *dev);
int qcom_wdt_pet_resume(struct device *dev);
int qcom_wdt_remove(struct platform_device *pdev);
#else
static inline void qcom_wdt_trigger_bite(void) { }
#endif

#endif
