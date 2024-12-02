/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Definitions for talking to the PMU.  The PMU is a microcontroller
 * which controls battery charging and system power on PowerBook 3400
 * and 2400 models as well as the RTC and various other things.
 *
 * Copyright (C) 1998 Paul Mackerras.
 */
#ifndef _LINUX_PMU_H
#define _LINUX_PMU_H

#include <linux/rtc.h>
#include <uapi/linux/pmu.h>


extern int __init find_via_pmu(void);

extern int pmu_request(struct adb_request *req,
		void (*done)(struct adb_request *), int nbytes, ...);
extern int pmu_queue_request(struct adb_request *req);
extern void pmu_poll(void);
extern void pmu_poll_adb(void); /* For use by xmon */
extern void pmu_wait_complete(struct adb_request *req);

/* For use before switching interrupts off for a long time;
 * warning: not stackable
 */
#if defined(CONFIG_ADB_PMU)
extern void pmu_suspend(void);
extern void pmu_resume(void);
#else
static inline void pmu_suspend(void)
{}
static inline void pmu_resume(void)
{}
#endif

extern void pmu_enable_irled(int on);

extern time64_t pmu_get_time(void);
extern int pmu_set_rtc_time(struct rtc_time *tm);

extern void pmu_restart(void);
extern void pmu_shutdown(void);
extern void pmu_unlock(void);

extern int pmu_present(void);
extern int pmu_get_model(void);

extern void pmu_backlight_set_sleep(int sleep);

#define PMU_MAX_BATTERIES	2

/* values for pmu_power_flags */
#define PMU_PWR_AC_PRESENT	0x00000001

/* values for pmu_battery_info.flags */
#define PMU_BATT_PRESENT	0x00000001
#define PMU_BATT_CHARGING	0x00000002
#define PMU_BATT_TYPE_MASK	0x000000f0
#define PMU_BATT_TYPE_SMART	0x00000010 /* Smart battery */
#define PMU_BATT_TYPE_HOOPER	0x00000020 /* 3400/3500 */
#define PMU_BATT_TYPE_COMET	0x00000030 /* 2400 */

struct pmu_battery_info
{
	unsigned int	flags;
	unsigned int	charge;		/* current charge */
	unsigned int	max_charge;	/* maximum charge */
	signed int	amperage;	/* current, positive if charging */
	unsigned int	voltage;	/* voltage */
	unsigned int	time_remaining;	/* remaining time */
};

extern int pmu_battery_count;
extern struct pmu_battery_info pmu_batteries[PMU_MAX_BATTERIES];
extern unsigned int pmu_power_flags;

/* Backlight */
extern void pmu_backlight_init(void);

/* some code needs to know if the PMU was suspended for hibernation */
#if defined(CONFIG_SUSPEND) && defined(CONFIG_PPC32)
extern int pmu_sys_suspended;
#else
/* if power management is not configured it can't be suspended */
#define pmu_sys_suspended	0
#endif

#endif /* _LINUX_PMU_H */
