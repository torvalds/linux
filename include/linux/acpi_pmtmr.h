/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ACPI_PMTMR_H_
#define _ACPI_PMTMR_H_

#include <linux/clocksource.h>

/* Number of PMTMR ticks expected during calibration run */
#define PMTMR_TICKS_PER_SEC 3579545

/* limit it to 24 bits */
#define ACPI_PM_MASK CLOCKSOURCE_MASK(24)

/* Overrun value */
#define ACPI_PM_OVRRUN	(1<<24)

#ifdef CONFIG_X86_PM_TIMER

extern u32 acpi_pm_read_verified(void);
extern u32 pmtmr_ioport;

static inline u32 acpi_pm_read_early(void)
{
	if (!pmtmr_ioport)
		return 0;
	/* mask the output to 24 bits */
	return acpi_pm_read_verified() & ACPI_PM_MASK;
}

/**
 * Register callback for suspend and resume event
 *
 * @cb Callback triggered on suspend and resume
 * @data Data passed with the callback
 */
void acpi_pmtmr_register_suspend_resume_callback(void (*cb)(void *data, bool suspend), void *data);

/**
 * Remove registered callback for suspend and resume event
 */
void acpi_pmtmr_unregister_suspend_resume_callback(void);

#else

static inline u32 acpi_pm_read_early(void)
{
	return 0;
}

#endif

#endif

